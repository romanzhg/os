#include "vm/frame.h"

#include <debug.h>
#include <stdint.h>
#include <stdio.h>
#include <random.h>

#include "threads/loader.h"
#include "threads/malloc.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "vm/page.h"
#include "vm/swap.h"

#define FRAME_SHIFT 12

struct frame
{
  //bool pageable;

  // if needs to traversal the list may need to add a bool here
  // to track if the frame can be put in page table

  bool present;
  // user virtual address correspond to this frame
  // at the time of eviction, check on uaddr, if it belongs to a mmapped file
  // put it back to disk and put the item to spt, if not put it to swap and 
  // put the item to spt -> frame_get() needs to modify the other thread's spt,
  // should hold a lock for this
  void* uaddr;

  struct thread * thread;

};

static void * frame_evict (void);

struct frame* frames;
// should obtain the lock when evicting pages/doing real io(?)
struct lock frame_lock;

void frame_init (void)
{
  lock_init (&frame_lock);
  random_init (234);
  frames = (struct frame *) malloc (init_ram_pages * sizeof (struct frame));

  uint32_t i;
  for (i = 0; i < init_ram_pages; i++)
    frames[i].present = false;
}

// for now simply get a new page from palloc, retrn null if there is no new page
// when eviction is implemented, evict a page to swap/filesys if needed
void *frame_get (int flags)
{
  lock_acquire (&frame_lock);
  void * rtn = palloc_get_page (flags | PAL_USER);
  // chose a page to evict
  if (rtn == NULL) {
    rtn = frame_evict();
  }
  lock_release (&frame_lock);
  return rtn;
}

static void *
frame_evict (void)
{
  int rand_index = 0;
  while (true)
  {
    rand_index = random_ulong () % init_ram_pages;
    if (frames[rand_index].present == true)
      {
        break;
      }
  }
  //printf ("evicting index: %d\n", rand_index);
  frames[rand_index].present = false;
  pagedir_clear_page (frames[rand_index].thread->pagedir, frames[rand_index].uaddr);
  
  // for now only consider evict to swap
  int swap_index = swap_get ();
  if (swap_index == -1)
    return NULL;

  if (!page_add_swap (&(frames[rand_index].thread->pages), frames[rand_index].uaddr, swap_index))
    return NULL;
  swap_write (swap_index, ptov(rand_index << FRAME_SHIFT));
  
  return ptov(rand_index << FRAME_SHIFT);
}

// should call with the frame lock hold
// put the frame back to frame list
// TODO: need to revisit page deletion
void frame_free (void * kpage)
{
  uint32_t index = vtop (kpage) >> FRAME_SHIFT;
  frames[index].present = false;
  palloc_free_page(kpage);
}

// when user tries to add an entry to the page table, update the frame table also
void frame_set_mapping (void *upage, void *kpage, bool writable UNUSED)
{
  uint32_t index = vtop (kpage) >> FRAME_SHIFT;
  ASSERT (index < init_ram_pages);

  frames[index].thread = thread_current ();
  frames[index].uaddr = upage;
  frames[index].present = true;
}
