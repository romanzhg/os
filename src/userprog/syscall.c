#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "lib/kernel/console.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"

typedef int pid_t;

static void syscall_handler (struct intr_frame *);
static int get_arg (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static int get_user (const uint8_t *uaddr);
static int write (int fd, const void *buffer, unsigned length);
static void exit (int status);
static void halt (void);
static int wait (pid_t pid);

static struct list pid_list;

struct pid_mapping
{
  pid_t pid;
  tid_t tid;
  struct list_elem elem;
};

void
syscall_init (void) 
{
  //list_init (&pid_list);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
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
        wait (get_arg((uint8_t *)(esp + 1)));
        break;
      case SYS_WRITE:
        f->eax = write(get_arg((uint8_t *)(esp + 1)), (void *)get_arg((uint8_t *)(esp + 2)), get_arg((uint8_t *)(esp + 3)));
        break;
      case SYS_EXEC:
        break;
      default:
        break;
    }
}

static pid_t
exec (const char *cmd_line)
{
  tid_t tid = process_execute(cmd_line);
  return 0;
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

static int wait (pid_t pid)
{
  //wait ()
  return 0;
}

static int
write(int fd, const void *buffer, unsigned length) {
  // to do: check user buffer
  if (!is_user_vaddr (buffer))
    exit(-1);
    
  char *kbuf = (char *) malloc (length);;

  unsigned i;
  for (i = 0; i< length; i++)
    kbuf[i] = get_user(buffer+i);

  if (fd == 1) {
    putbuf(kbuf, length);
    return length;
  }
  return 0;

}

static int
get_arg (const uint8_t *uaddr)
{
  int result;
  uint8_t *buf = (uint8_t *) &result;
  int i = 0;
  for (i = 0; i < 4; i++)
    {
      buf[i] = get_user(uaddr + i);
    }
  return result;
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}


