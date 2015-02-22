#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>

#include "devices/block.h"
#include "filesys/off_t.h"
#include "lib/kernel/hash.h"

// if a page is not in memory then it must be in some block device
// (at least for this project)
struct swap_addr
{
  struct block * block;
  block_sector_t sector;
  // only length bytes needs to be read, other should be zeroed
  size_t length;
  bool writable;
};

struct fs_addr
{
  struct file * file;
  off_t ofs;
  // only length bytes needs to be read, other should be zeroed
  size_t length;
  bool writable;
};

struct page 
{ 
  struct hash_elem hash_elem; 
  // page alligned virtual address
  void *vaddr;
  // true if the page is in file systems instead of swap
  bool in_fs;
  struct swap_addr saddr;
  struct fs_addr faddr;
}; 

unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_,
                void *aux UNUSED);
void page_destructor (struct hash_elem *e, void *aux UNUSED);

bool page_add_fs (struct hash * pages, void * vaddr, struct fs_addr addr);
bool page_add_swap (struct hash * pages, void * vaddr, struct swap_addr addr);
void page_destory (struct hash * pages);
bool page_fault_handler (struct hash * pages, void * vaddr);
bool page_stack_growth_handler (void * vaddr, void * esp);

#endif
