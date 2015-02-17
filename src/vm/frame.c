#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/loader.h"
#include "threads/vaddr.h"
#include "threads/synch.h"

#include <debug.h>
#include <stdint.h>

#define FRAME_SHIFT 12

struct frame
{
  // if needs to traversal the list may need to add a bool here
  // to track if the frame can be put in page table

  // user virtual address correspond to this frame
  void* uaddr;
  // the page directory stores the virtual address
  void* pd;
};

struct frame* frames;
// should obtain the lock when evicting pages/doing real io(?)
struct lock frame_lock;

void frame_init(void)
{
  lock_init(&frame_lock);
  frames = (struct frame *) malloc (init_ram_pages * sizeof (struct frame));
}

// for now simply get a new page from palloc, retrn null if there is no new page
// when eviction is implemented, evict a page to swap/filesys if needed
void *frame_get(int flags)
{
  return palloc_get_page(flags | PAL_USER);
}

// put the frame back to frame list
void frame_free(void * p)
{
  palloc_free_page(p);
}

// when user tries to add an entry to the page table, update the frame table also
void frame_set_mapping (uint32_t *pd, void *upage, void *kpage)
{
  struct frame tmp;
  tmp.pd = pd;
  tmp.uaddr = upage;

  uint32_t index = vtop(kpage) >> FRAME_SHIFT;
  ASSERT (index < init_ram_pages);

  frames[index] = tmp;
}
