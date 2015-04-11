#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"
#include "filesys/file.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

struct inode;

/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt);
struct file *dir_open (struct inode *);
struct file *dir_open_root (void);
struct file *dir_reopen (struct file *);
void dir_close (struct file *);
struct inode *dir_get_inode (struct file *);

/* Reading and writing. */
bool dir_lookup (const struct file *, const char *name, struct inode **);
bool dir_add (struct file *, const char *name, block_sector_t);
bool dir_remove (struct file *, const char *name);
bool dir_readdir (struct file *, char name[NAME_MAX + 1]);

#endif /* filesys/directory.h */
