#include "userprog/syscall.h"

#include <stdio.h>
#include <syscall-nr.h>

#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/kernel/console.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/frame.h"

/*
  Note: Pints kernel has mapped the user page to it's own page table, so it's
  possible to read the user data directly.
*/

static void syscall_handler (struct intr_frame *);
static int write (int fd, const void *buffer, unsigned length);
static void exit (int status);
static void halt (void);
static int wait (pid_t pid);
static int read (int fd, void *buffer, unsigned length);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);
static pid_t exec (const char *cmd_line);

static mapid_t mmap (int fd, void *addr);
static void munmap (mapid_t mapping);

static void get_arg (const uint8_t *uaddr, int * args);
static bool is_valid_filename (const char* source);
static bool is_valid_uaddr (const void* p, uint32_t range);
static bool is_valid_cmdline (const char* source);
static bool pin_memory (const void *buffer, unsigned length);
static void unpin_memory (const void *buffer, unsigned length);

void
syscall_init (void) 
{
  lock_init (&fs_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// validate the address is a correct user address, start from p and
// end at p + range
static bool
is_valid_uaddr (const void* p, uint32_t range)
{
  bool rtn = false;
  if (!is_user_vaddr (p) || !is_user_vaddr (p + range))
    rtn = false;
  else
    {
      enum intr_level old_level = intr_disable ();
      struct thread *t = thread_current ();
      if ((pagedir_get_page (t->pagedir, p) == NULL)
          || (pagedir_get_page (t->pagedir, p + range) == NULL))
        rtn = false;
      else
        rtn = true;
      intr_set_level (old_level);
    }
  return rtn;
}

static void
syscall_handler (struct intr_frame *f) 
{
  thread_current ()->uesp = f->esp;
  int32_t args[4];
  get_arg((uint8_t *)f->esp, args);

  switch (args[0])
    {
      case SYS_HALT:
        halt ();
        break;
      case SYS_EXIT:
        exit (args[1]);
        break;
      case SYS_WAIT:
        f->eax = wait (args[1]);
        break;
      case SYS_WRITE:
        f->eax = write (args[1], (void *)args[2], args[3]);
        break;
      case SYS_EXEC:
        f->eax = exec ((char *) args[1]);
        break;
      case SYS_READ:
        f->eax = read (args[1], (void *)args[2], args[3]);
        break;
      case SYS_CREATE:
        f->eax = create ((const char *)args[1], args[2]);
        break;
      case SYS_REMOVE:
        f->eax = remove ((const char *)args[1]);
        break;
      case SYS_OPEN:
        f->eax = open ((const char *)args[1]);
        break;
      case SYS_FILESIZE:
        f->eax = filesize (args[1]);
        break;
      case SYS_SEEK:
        seek (args[1], args[2]);
        break;
      case SYS_TELL:
        f->eax = tell (args[1]);
        break;
      case SYS_CLOSE:
        close (args[1]);
        break;
      case SYS_MMAP:
        f->eax = mmap (args[1], (void *) args[2]);
        break;
      case SYS_MUNMAP:
        munmap (args[1]);
        break;
      default:
        break;
    }
}

static bool create (const char *file, unsigned initial_size)
{
  if (!is_valid_filename (file))
    return false;
  lock_acquire (&fs_lock);
  bool rtn = filesys_create (file, initial_size);
  lock_release (&fs_lock);
  return rtn;
}

static bool remove (const char *file)
{
  if (!is_valid_filename (file))
    return false;
  lock_acquire (&fs_lock);
  bool rtn = filesys_remove (file); 
  lock_release (&fs_lock);
  return rtn;
}

static int open (const char *file)
{
  if (!is_valid_filename (file))
    return -1;
  lock_acquire (&fs_lock);
  struct file * f = filesys_open (file);
  lock_release (&fs_lock);
  if (f == NULL)
    return -1;
  return thread_open_file (f);
}

static mapid_t
mmap (int fd, void *addr)
{
  if (fd == 0 || fd == 1)
    return -1;

  if (pg_ofs (addr) != 0 || addr == 0)
    return -1;

  struct file * file = file_reopen (thread_lookup_fd (fd));
  lock_acquire (&fs_lock);
  uint32_t file_len = file_length (file);
  lock_release (&fs_lock);
  
  if (file_len  == 0)
    return -1;

  struct mmap_info mmap_info;
  mmap_info.file = file;
  mmap_info.start = addr;
  mmap_info.length = file_len;

  if (pg_round_down(addr + file_len) + (uint32_t ) 0x800000 >= PHYS_BASE)
    return -1;

  // make sure the mapping doesn't overlap with any current page
  uint32_t tmp_file_len = file_len;
  uint32_t tmp_ofs = 0;

  lock_acquire (&frame_lock);
  while (tmp_file_len > 0)
    {
      size_t tmp_length = tmp_file_len < PGSIZE ? tmp_file_len : PGSIZE;
      if (page_lookup (&thread_current ()->pages, addr + tmp_ofs, false) != NULL)
        {
          lock_release (&frame_lock);
          return -1;
        }
      if (pagedir_get_page (thread_current ()->pagedir, addr + tmp_ofs) != NULL)
        {
          lock_release (&frame_lock);
          return -1;
        }

      tmp_ofs += PGSIZE;
      tmp_file_len -= tmp_length;
    }
  lock_release (&frame_lock);

  off_t ofs = 0;
  while (file_len > 0)
    {
      size_t page_read_bytes = file_len < PGSIZE ? file_len : PGSIZE;

      struct fs_addr faddr;
      faddr.file = file;
      faddr.ofs = ofs;
      faddr.length = page_read_bytes;
      faddr.writable = true;
      faddr.zeroed = false;

      // TODO: need to release the resources already allocated in this while loop
      // TODO: should allocate the page at once, then fill them in. need refactor
      if (!page_add_fs (&thread_current ()->pages, addr, faddr, true))
        return -1;

      ofs += PGSIZE;
      file_len -= page_read_bytes;
      addr += PGSIZE;
    }

  int rtn = thread_mmap (mmap_info);
  return rtn;
}

static void
munmap (mapid_t mapping)
{
  thread_munmap(mapping);
}

static int filesize (int fd)
{
  struct file * file = thread_lookup_fd (fd);
  lock_acquire (&fs_lock);
  int rtn = file_length (file);
  lock_release (&fs_lock);
  return rtn;
}

static void seek (int fd, unsigned position)
{
  struct file * file = thread_lookup_fd (fd);
  lock_acquire (&fs_lock);
  file_seek (file, position);
  lock_release (&fs_lock);
}

static unsigned tell (int fd)
{
  struct file * file = thread_lookup_fd (fd);
  lock_acquire (&fs_lock);
  unsigned int rtn = file_tell (file);
  lock_release (&fs_lock);
  return rtn;
}

static void close (int fd)
{
  thread_close_file (fd);
}

static pid_t
exec (const char *cmd_line)
{
  if (!is_valid_cmdline(cmd_line))
    exit(-1);
  tid_t tid = process_execute (cmd_line);
  if (tid != TID_ERROR)
    return thread_get_pid (tid);
  else
    return -1;
}

static void
halt (void)
{
  shutdown_power_off ();
}

static void
exit (int status)
{
  thread_set_exit_status (status);
  thread_exit ();
}

static int
wait (pid_t pid)
{
  tid_t tid = thread_get_tid (pid);
  if (tid != -1)
    return process_wait (tid);
  else
    return -1;
}

static int
write (int fd, const void *buffer, unsigned length) {
  if (!is_valid_uaddr (buffer, length))
    exit(-1);
    
  int rtn = 0;
  if (fd == 0)
    {
      // do nothing
    }
  else if (fd == 1)
    {
      putbuf (buffer, length);
      rtn = length;
    }
  else
    {
      struct file * file = thread_lookup_fd (fd);
      if (file == NULL)
        {
          exit (-1);
        }
      if (!pin_memory (buffer, length))
        exit (-1);
      lock_acquire (&fs_lock);
      rtn = file_write (file, buffer, length);
      lock_release (&fs_lock);
      unpin_memory (buffer, length);
    }
  return rtn;
}

static int
read (int fd, void *buffer, unsigned length){
  if (!is_user_vaddr (buffer))
    exit(-1);

  unsigned i;
  int rtn = 0;
  if (fd == 0)
    {
      for (i = 0; i < length; i++)
        // input_getc would block if the buffer is empty
        *((char *) buffer + i) = input_getc ();
    }
  else if (fd == 1)
    {
      // do nothing
    }
  else
    {
      struct file * file = thread_lookup_fd (fd);
      if (file == NULL)
        {
          exit (-1);
        }
      if (!pin_memory (buffer, length))
        exit (-1);
      lock_acquire (&fs_lock);
      rtn = file_read (file, buffer, length);
      lock_release (&fs_lock);
      unpin_memory (buffer, length);
    }
  return rtn;
}

// get argument for system call, which are 16 bytes (4 integer)
static void
get_arg (const uint8_t *uaddr, int * args)
{
  if (!is_valid_uaddr (uaddr, 4 * 4)) 
    exit(-1);

  uint8_t *buf = (uint8_t *) args;
  int i, j;
  for (j = 0; j < 4; j++)
    {
      for (i = 0; i < 4; i++)
        {
          buf[i + j * 4] = *((uint8_t *)uaddr + i + j * 4);
        }
    }
}

// validate a file name, which is limited to 14 chars
static bool
is_valid_filename (const char* source)
{
  int i;
  for (i = 0; i < 15; i++)
    {
      if (!is_valid_uaddr (source + i, 0)) 
        exit(-1);
      if (*((char *)source + i) == '\0')
        break;
    }

  return i != 15;
}

static bool
is_valid_cmdline (const char* source)
{
  int i;
  for (i = 0; i < PGSIZE; i++)
    {
      if (!is_valid_uaddr (source + i, 0)) 
        exit(-1);
      if (*((char *)source + i) == '\0')
        break;
    }

  return i != PGSIZE;
}

static bool
pin_memory (const void *buffer, unsigned length)
{
  void *base_page = pg_round_down (buffer);
  void *end_page = pg_round_down (buffer + length);
  int i;
  for (i = 0; (base_page + i * PGSIZE) <= end_page; i++)
    {
      lock_acquire (&frame_lock);
      void * kpage = pagedir_get_page (thread_current ()->pagedir, base_page + i * PGSIZE);
      if (kpage == NULL)
        {
          lock_release (&frame_lock);
          if (((uint32_t) PHYS_BASE - (uint32_t) (base_page + i * PGSIZE)) < (uint32_t) 0x800000)
            {
              if (!page_stack_growth_handler (&(thread_current ()->pages), base_page + i * PGSIZE, thread_current ()->uesp, true))
                return false;
            }
          else
            {
              if (!page_fault_handler (&(thread_current ()->pages), base_page + i * PGSIZE, true))
                return false;
            }
        }
      else
        {
          frame_pin_memory (kpage);
          lock_release (&frame_lock);
        }
    }
/*
  int i = 0;
  do
    {
      lock_acquire (&frame_lock);
      void * kpage = pagedir_get_page (thread_current ()->pagedir, buffer + i * PGSIZE);
      if (kpage == NULL)
        {
          lock_release (&frame_lock);
          if (!page_fault_handler (&(thread_current ()->pages), buffer + i * PGSIZE, true))
            return false;
        }
      else
        {
          frame_pin_memory (kpage);
          lock_release (&frame_lock);
        }
      i++;
    } while ((unsigned) i * PGSIZE < length);
*/
  return true;
}

static void
unpin_memory (const void *buffer, unsigned length)
{
  void * base_page = pg_round_down (buffer);
  void * end_page = pg_round_down (buffer + length);
  int i;
  for (i = 0; (base_page + i * PGSIZE) <= end_page; i++)
    {
      void * kpage = pagedir_get_page (thread_current ()->pagedir, base_page + i * PGSIZE);
      ASSERT (kpage != NULL);
      frame_unpin_memory (kpage);
    }

/*
  int i = 0;
  do
    {
      void * kpage = pagedir_get_page (thread_current ()->pagedir, buffer + i * PGSIZE);
      ASSERT (kpage != NULL);
      frame_unpin_memory (kpage);
      i++;
    } while ((unsigned) i * PGSIZE < length);
*/
}
