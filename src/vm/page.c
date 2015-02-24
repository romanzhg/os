#include "vm/page.h"

#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "filesys/off_t.h"
#include <string.h>
#include <stdio.h>

static void page_read_from_fs (struct page * page, void * kpage);
static void print_pages (struct hash * pages);

// when a process end, destory the supplimental page table and free all
// the resources
void
page_destory (struct hash * pages)
{
  hash_destroy (pages, page_destructor);
}

// Remove the page at vaddr. Return true if the page was actually removed.
bool
page_remove_mmap (struct hash * pages, void *vaddr)
{
  struct page p;
  struct hash_elem *e;
  p.vaddr = vaddr;
  e = hash_delete (pages, &p.hash_elem);
  if (e == NULL)
    return false;
  else
    {
      ASSERT (hash_entry (e, struct page, hash_elem)->in_fs);
      free (hash_entry (e, struct page, hash_elem));
      return true;
    }
}

// add a mapping to the supplemental page table, from virtual address to
// where the data actually resides
// called when add a new mapping/frame table need to evict a page
// need to revisit for concurrency concerns
bool
page_add_swap (struct hash * pages, void * vaddr,
               int index)
{
  struct page * page = malloc (sizeof (struct page));
  if (page == NULL)
    return false;

  page->vaddr = vaddr;
  page->in_fs = false;
  page->swap_index = index;
  ASSERT (hash_insert (pages, &page->hash_elem) == NULL);
  return true;
}

bool
page_add_fs (struct hash * pages, void * vaddr, struct fs_addr addr)
{
  struct page * page = malloc (sizeof (struct page));
  if (page == NULL)
    return false;
  page->vaddr = vaddr;
  page->in_fs = true;
  page->faddr = addr;
  ASSERT (hash_insert (pages, &page->hash_elem) == NULL);
  return true;
}

// valid vaddr: (vaddr >= esp) or (esp - vaddr <= 32)
bool
page_stack_growth_handler (struct hash * pages, void * vaddr, void * esp)
{
  void *kpage;
  if ((uint32_t) vaddr >= (uint32_t) esp)
    {
      /* valid address, do nothing */
      kpage = frame_get (0);
      if (kpage == NULL)
        return false;

      // the page maybe in swap
      struct page * page;
      if ((page = page_lookup (pages, pg_round_down(vaddr), true)) != NULL)
        {
          ASSERT (!page->in_fs);
          swap_read (page->swap_index, kpage);
          swap_free (page->swap_index);
        }
      free (page);
      return pagedir_set_page (thread_current ()->pagedir,
                               pg_round_down(vaddr), kpage, true);
    }

  if (((uint32_t) esp - (uint32_t) vaddr) > (uint32_t) 32)
    return false;

  // allocate a page and install it, the page should always be writable
  // a stack page don't need to be zeroed
  kpage = frame_get (0);
  if (kpage == NULL)
    return false;

  return pagedir_set_page (thread_current ()->pagedir,
                           pg_round_down(vaddr), kpage, true);
}

// put vaddr to a frame and page table, also remove the mapping from
// supplemental page table
bool
page_fault_handler (struct hash * pages, void * vaddr)
{
  struct page * page;
  // first check if the vaddr is valid by look it up in the hash table
  // should kill the process instead of return false here
  //printf("vaddr: %p\n", vaddr);
  //printf("vaddr pg_no: %p\n",  pg_round_down(vaddr));
  //print_pages (pages);
  if ((page = page_lookup (pages, pg_round_down(vaddr), true)) == NULL)
    return false;
  
  // get an empty frame from the frame table
  // TODO: what if the allocated kpage was evicted before we even install it?
  // TODO: do we need to zero out the page?
  void * kpage = frame_get(0);
  if (kpage == NULL)
    return false;

  // read data from the block
  if (page->in_fs)
    page_read_from_fs (page, kpage);
  else
    {
      swap_read (page->swap_index, kpage);
      swap_free (page->swap_index);
    }

  bool rtn;
  // install the page
  if (page->in_fs)
    rtn = pagedir_set_page (thread_current ()->pagedir,
                            pg_round_down(vaddr), kpage, page->faddr.writable);
  else
    rtn = pagedir_set_page (thread_current ()->pagedir,
                            pg_round_down(vaddr), kpage, true);

  free (page);
  return rtn;
}

// TODO: see if need to handle return value?
static void
page_read_from_fs (struct page * page, void * kpage)
{
  file_seek (page->faddr.file, page->faddr.ofs);
  file_read (page->faddr.file, kpage, page->faddr.length);
  memset (kpage + page->faddr.length, 0, PGSIZE - page->faddr.length);
}

// TODO: need to deny write access to a read only page and any access to kernel
// memory
// lookup and remove the element from pages
struct page *
page_lookup (struct hash * pages, void *vaddr, bool to_delete)
{
  struct page p;
  struct hash_elem *e;

  p.vaddr = vaddr;
  if (to_delete)
    e = hash_delete (pages, &p.hash_elem);
  else
    e = hash_find (pages, &p.hash_elem);
  return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->vaddr, sizeof p->vaddr);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_,
           void *aux UNUSED)
{
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->vaddr < b->vaddr;
}

void
page_destructor (struct hash_elem *e, void *aux UNUSED)
{
  struct page * page = hash_entry (e, struct page, hash_elem);
  if (!page->in_fs)
    swap_free (page->swap_index);
  free(page);
}

static void
print_pages (struct hash * pages)
{
  struct hash_iterator i;

  hash_first (&i, pages);
  while (hash_next (&i))
    {
      struct page *p = hash_entry (hash_cur (&i), struct page, hash_elem);
      printf ("page in hash table: %p", p->vaddr);
    }
}
