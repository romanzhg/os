#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "lib/kernel/console.h"
#include "devices/shutdown.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

typedef int pid_t;

static void syscall_handler (struct intr_frame *);
static int get_arg (const uint8_t *uaddr);
static bool put_user (uint8_t *udst, uint8_t byte);
static int get_user (const uint8_t *uaddr);
static int write (int fd, const void *buffer, unsigned length);
static void exit (int status);
static void halt (void);
static int wait (pid_t pid);
static pid_t exec (const char *cmd_line);

void
syscall_init (void) 
{
  process_init();
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
        f->eax = wait ((int)get_arg((uint8_t *)(esp + 1)));
        break;
      case SYS_WRITE:
        f->eax = write(get_arg((uint8_t *)(esp + 1)), (void *)get_arg((uint8_t *)(esp + 2)), get_arg((uint8_t *)(esp + 3)));
        break;
      case SYS_EXEC:
        f->eax = exec((char *)get_arg((uint8_t *)(esp + 1)));
        break;
      default:
        break;
    }
}

static pid_t
exec (const char *cmd_line)
{
  tid_t tid = process_execute(cmd_line);

  int rtn = process_get_pid(tid);
  return rtn;
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
  tid_t tid = process_get_tid(pid);
  if (tid != -1)
    return process_wait(tid);
  else
    return -1;
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
  int tmp;
  uint8_t *buf = (uint8_t *) &result;
  int i = 0;
  for (i = 0; i < 4; i++)
    {
      tmp = get_user(uaddr + i);
      if (tmp == -1) {
        printf("error in get arg\n");
        return -1;
      } else
        buf[i] = tmp;
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
