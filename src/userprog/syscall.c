#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "lib/kernel/console.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

struct lock fs_lock;

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

static int get_arg (const uint8_t *uaddr);
static bool get_filename (char* dest, const char* source);

void
syscall_init (void) 
{
  lock_init(&fs_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static bool
validate_address(const void* p)
{
  bool rtn;
  if (!is_user_vaddr(p))
    rtn = false;
  else
    {
      //TODO: disable interrupt
      enum intr_level old_level = intr_disable ();
      struct thread *t = thread_current();
      if (pagedir_get_page(t->pagedir, p) == NULL)
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
  int* esp = f->esp;
  int syscall_num = get_arg((uint8_t *)esp);
  switch (syscall_num)
    {
      case SYS_HALT:
        halt ();
        break;
      case SYS_EXIT:
        exit(get_arg((uint8_t *)(esp + 1)));
        break;
      case SYS_WAIT:
        f->eax = wait ((int)get_arg((uint8_t *)(esp + 1)));
        break;
      case SYS_WRITE:
        f->eax = write(get_arg((uint8_t *)(esp + 1)), (void *)get_arg((uint8_t *)(esp + 2)), get_arg((uint8_t *)(esp + 3)));
        break;
      case SYS_EXEC:
        f->eax = exec((char *)get_arg((uint8_t *)(esp + 1)));
        break;
      case SYS_READ:
        f->eax = read(get_arg((uint8_t *)(esp + 1)), (void *)get_arg((uint8_t *)(esp + 2)), get_arg((uint8_t *)(esp + 3)));
        break;
      case SYS_CREATE:
        f->eax = create((const char *)get_arg((uint8_t *)(esp + 1)), get_arg((uint8_t *)(esp + 2)));
        break;
      case SYS_REMOVE:
        f->eax = remove((const char *)get_arg((uint8_t *)(esp + 1)));
        break;
      case SYS_OPEN:
        f->eax = open((const char *)get_arg((uint8_t *)(esp + 1)));
        break;
      case SYS_FILESIZE:
        f->eax = filesize(get_arg((uint8_t *)(esp + 1)));
        break;
      case SYS_SEEK:
        seek(get_arg((uint8_t *)(esp + 1)), get_arg((uint8_t *)(esp + 2)));
        break;
      case SYS_TELL:
        f->eax = tell(get_arg((uint8_t *)(esp + 1)));
        break;
      case SYS_CLOSE:
        close(get_arg((uint8_t *)(esp + 1)));
        break;
      default:
        break;
    }
}

static bool create (const char *file, unsigned initial_size)
{
  if (file == NULL)
    exit(-1);

  char file_name[15];
  if (!get_filename(file_name, file))
    return false;
  return filesys_create (file_name, initial_size);
}

static bool remove (const char *file)
{
  char file_name[15];
  get_filename(file_name, file);
  return filesys_remove (file_name);
}

static int open (const char *file)
{
  if (file == NULL)
    exit(-1);
  char file_name[15];
  if (!get_filename(file_name, file))
    return -1;
  struct file * f = filesys_open(file_name);
  if (f == NULL)
    return -1;
  return thread_open_file(f);
}

static int filesize (int fd)
{
  struct file * file = thread_lookup_fd(fd);
  return file_length(file);
}

static void seek (int fd, unsigned position)
{
  struct file * file = thread_lookup_fd(fd);
  file_seek(file, position);
}

static unsigned tell (int fd)
{
  struct file * file = thread_lookup_fd(fd);
  return file_tell(file);
}

static void close (int fd)
{
  thread_close_file(fd);
}

static pid_t
exec (const char *cmd_line)
{
  if (!validate_address(cmd_line))
    exit(-1);
  tid_t tid = process_execute(cmd_line);
  if (tid != TID_ERROR)
    return thread_get_pid(tid);
  else
    return -1;
}

static void
halt (void)
{
  shutdown_power_off ();
}

static void
exit(int status)
{
  thread_set_exit_status(status);
  thread_exit();
}

static int
wait (pid_t pid)
{
  tid_t tid = thread_get_tid(pid);
  if (tid != -1)
    return process_wait(tid);
  else
    return -1;
}

static int
write(int fd, const void *buffer, unsigned length) {
  if (!validate_address(buffer))
    exit(-1);
    
  char *kbuf = (char *) malloc (length);
  if (kbuf == NULL)
    return 0;

  unsigned i;
  for (i = 0; i< length; i++)
    kbuf[i] =  *(((char *)buffer) + i);

  int rtn = 0;
  if (fd == 0)
    {
      // do nothing
    }
  else if (fd == 1)
    {
      putbuf(kbuf, length);
      rtn = length;
    }
  else
    {
      struct file * file = thread_lookup_fd(fd);
      if (file == NULL)
        {
          free(kbuf);
          exit(-1);
        }
      rtn = file_write(file, kbuf, length);
    }
  free(kbuf);
  return rtn;
}

static int
read (int fd, void *buffer, unsigned length){
  if (!is_user_vaddr (buffer))
    exit(-1);

  char *kbuf = (char *) malloc (length);
  if (kbuf == NULL)
    return 0;

  unsigned i;
  int rtn = 0;
  if (fd == 0)
    {
      for (i=0; i< length; i++)
        kbuf[i] = input_getc();
    }
  else if (fd == 1)
    {
      // do nothing
    }
  else
    {
      struct file * file = thread_lookup_fd(fd);
      if (file == NULL)
        {
          free(kbuf);
          exit(-1);
        }
      rtn = file_read(file, kbuf, length);
    }
  for (i=0; i< length; i++)
    *((char *) buffer + i) = kbuf[i];

  free(kbuf);
  return rtn;
}

static int
get_arg (const uint8_t *uaddr)
{
  if (!validate_address(uaddr)) 
    exit(-1);

  int result;
  uint8_t *buf = (uint8_t *) &result;
  int i = 0;
  for (i = 0; i < 4; i++)
    {
      buf[i] = *((uint8_t *)uaddr + i);
    }
  return result;
}

static bool
get_filename (char * dest, const char* source)
{
  if (!validate_address(source)) 
    exit(-1);

  int i;
  for (i = 0; i < 15; i++) {
    dest[i] = *((char *)source + i);
    if (dest[i] == '\0')
      break;
  }

  if (i == 15)
    return false;
  else
    return true;
}
