#include "vm/frame.h"

#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

#include <debug.h>
#include <stdint.h>

#define FRAME_SHIFT 12

struct frame
{
  // if needs to traversal the list may need to add a bool here
  // to track if the frame can be put in page table

  // user virtual address correspond to this frame
  // at the time of eviction, check on uaddr, if it belongs to a mmapped file
  // put it back to disk and put the item to spt, if not put it to swap and 
  // put the item to spt -> frame_get() needs to modify the other thread's spt,
  // should hold a lock for this
  void* uaddr;

  struct thread * thread;

  bool writable;
};

struct frame* frames;
// should obtain the lock when evicting pages/doing real io(?)
struct lock frame_lock;

void frame_init (void)
{
  lock_init (&frame_lock);
  frames = (struct frame *) malloc (init_ram_pages * sizeof (struct frame));
}

// for now simply get a new page from palloc, retrn null if there is no new page
// when eviction is implemented, evict a page to swap/filesys if needed
void *frame_get (int flags)
{
  lock_acquire (&frame_lock);
  void * rtn = palloc_get_page (flags | PAL_USER);
  lock_release (&frame_lock);
  return rtn;
}

// should call with the frame lock hold
// put the frame back to frame list
// TODO: need to revisit page deletion
void frame_free (void * p)
{
  palloc_free_page(p);
}

// when user tries to add an entry to the page table, update the frame table also
void frame_set_mapping (void *upage, void *kpage, bool writable)
{
  struct frame tmp;
  // TODO: may need to disable interrupt here?
  tmp.thread = thread_current ();
  tmp.uaddr = upage;
  tmp.writable = writable;

  uint32_t index = vtop (kpage) >> FRAME_SHIFT;
  ASSERT (index < init_ram_pages);

  frames[index] = tmp;
}
