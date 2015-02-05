#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "filesys/file.h"
#include "filesys/filesys.h"

typedef int pid_t;
extern struct list pid_list;
tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);
pid_t process_get_pid(tid_t tid);
tid_t process_get_tid(pid_t pid);
void process_init(void);
int process_open_file(struct file * f);
void process_close_file(int fd);
struct file * process_lookup_fd(int fd);

#endif /* userprog/process.h */
