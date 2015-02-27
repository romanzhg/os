#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>

#include "devices/block.h"
#include "filesys/off_t.h"
#include "lib/kernel/hash.h"

struct fs_addr
{
  struct file * file;     /* the mmapped file */
  off_t ofs;              /* the offset for this page in the mmapped file */
  size_t length;          /* valid bytes for the page */
  bool writable;          /* is this file writable */
  bool zeroed;            /* true if the page is all zero */
};

struct page 
{ 
  void *vaddr;            /* the virtual address for this page */
  int swap_index;         /* swap slot for the page, -1 if in file system */
  struct fs_addr faddr;   /* location for the page in file system */
  struct hash_elem hash_elem;          /* element for the page table*/
}; 

bool page_add_fs (struct hash * pages, void * vaddr, struct fs_addr addr);
bool page_add_swap (struct hash * pages, void * vaddr, int index);
void page_destory (struct hash * pages);
bool page_fault_handler (struct hash * pages, const void * vaddr, bool pin_memory);
bool page_stack_growth_handler (struct hash * pages, void * vaddr, void * esp, bool pin_memory);
bool page_remove_mmap (struct hash * pages, void *vaddr);
struct page * page_lookup (struct hash * pages, void *vaddr, bool to_delete);

unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_,
                void *aux UNUSED);
void page_destructor (struct hash_elem *e, void *aux UNUSED);
#endif
