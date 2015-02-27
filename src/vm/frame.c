#include "vm/frame.h"

#include <debug.h>
#include <stdint.h>
#include <stdio.h>

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

static void * frame_evict (void);
static int clock_value = 0;

static struct mmap_info *
get_mmap_info (struct thread *thread, void *uaddr);

struct frame* frames;

// TODO: should obtain the lock when evicting pages/doing real io(?)
struct lock frame_lock;

void frame_init (void)
{
  lock_init (&frame_lock);
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
  while (true)
  {
    clock_value = (clock_value + 1) % init_ram_pages; 
    if (frames[clock_value].present == true)
      {
        if (pagedir_is_accessed (frames[clock_value].thread->pagedir, frames[clock_value].uaddr))
          {
            pagedir_set_accessed (frames[clock_value].thread->pagedir, frames[clock_value].uaddr, false);
            continue;
          }
        else
          break;
      }
  }

  int index = clock_value;
  frames[index].present = false;
  pagedir_clear_page (frames[index].thread->pagedir, frames[index].uaddr);
  
  struct mmap_info * map_info = get_mmap_info (frames[index].thread, frames[index].uaddr);
  if (map_info != NULL)
    {
      struct fs_addr faddr;
      faddr.file = map_info->file;
      faddr.ofs = frames[index].uaddr - map_info->start;
      faddr.length = pg_round_up(frames[index].uaddr) > (map_info->start + map_info->length) ?
          map_info->length % PGSIZE : PGSIZE;
      faddr.writable = true;
      faddr.zeroed = false;

      if (!page_add_fs(&(frames[index].thread->pages), frames[index].uaddr, faddr))
        return NULL;

      // write back to file if the page is dirty
      if (pagedir_is_dirty (frames[clock_value].thread->pagedir, frames[index].uaddr))
        ASSERT ((uint32_t)file_write_at (map_info->file, ptov(index << FRAME_SHIFT), faddr.length, faddr.ofs) == faddr.length);
    }
  else
    {
      // write to swap space
      int swap_index = swap_get ();
      if (swap_index == -1)
        return NULL;

      if (!page_add_swap (&(frames[index].thread->pages), frames[index].uaddr, swap_index))
        return NULL;
      swap_write (swap_index, ptov(index << FRAME_SHIFT));
    }
  
  return ptov(index << FRAME_SHIFT);
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

static struct mmap_info *
get_mmap_info (struct thread *thread, void *uaddr)
{
  struct list_elem *e;
  for (e = list_begin (&thread->mmap_list); e != list_end (&thread->mmap_list);
       e = list_next (e))
    {
      struct mmap_info * tmp = list_entry (e, struct mmap_info, elem);
      if ((pg_round_down(tmp->start + tmp->length) >= uaddr)
          && (pg_round_down(tmp->start) <= uaddr))
        return tmp;
    }
  return NULL;
}
