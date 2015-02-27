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
#include "userprog/process.h"       /* for the file system lock */
#include "vm/page.h"
#include "vm/swap.h"

#define FRAME_SHIFT 12

static void * frame_evict (void);
static int clock_hand = 0;

static struct mmap_info * get_mmap_info (struct thread *thread, void *uaddr);

struct frame* frames;
struct lock frame_lock;

void frame_init (void)
{
  lock_init (&frame_lock);
  frames = (struct frame *) malloc (init_ram_pages * sizeof (struct frame));

  uint32_t i;
  for (i = 0; i < init_ram_pages; i++)
    frames[i].present = false;
    frames[i].pinned = false;
}

// Get a new page, return it's kernel virtual address. Return NULL if failed.
void *frame_get (int flags)
{
  void * rtn = palloc_get_page (flags | PAL_USER);
  if (rtn == NULL) {
    rtn = frame_evict();
  }
  return rtn;
}

static void *
frame_evict (void)
{
  lock_acquire (&frame_lock);
  while (true)
  {
    clock_hand = (clock_hand + 1) % init_ram_pages; 
    if (frames[clock_hand].present == true
        && frames[clock_hand].pinned == false)
      {
        if (pagedir_is_accessed (frames[clock_hand].thread->pagedir, frames[clock_hand].uaddr))
          {
            pagedir_set_accessed (frames[clock_hand].thread->pagedir, frames[clock_hand].uaddr, false);
            continue;
          }
        else
          break;
      }
  }

  int index = clock_hand;
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

      struct page * page = page_add_fs(&(frames[index].thread->pages), frames[index].uaddr, faddr, false);
      lock_release (&frame_lock);
      if (page == NULL)
        return NULL;

      // write back to file if the page is dirty
      if (pagedir_is_dirty (frames[index].thread->pagedir, frames[index].uaddr))
        {
          lock_acquire (&fs_lock);
          ASSERT ((uint32_t)file_write_at (map_info->file, ptov(index << FRAME_SHIFT), faddr.length, faddr.ofs) == faddr.length);
          lock_release (&fs_lock);
        }
      sema_up (&page->ready);
    }
  else
    {
      // write to swap space
      int swap_index = swap_get ();
      if (swap_index == -1)
        return NULL;

      struct page * page = page_add_swap (&(frames[index].thread->pages), frames[index].uaddr, swap_index, false);
      lock_release (&frame_lock);
      if (page == NULL)
        return NULL;

      swap_write (swap_index, ptov(index << FRAME_SHIFT));
      sema_up (&page->ready);
    }
  
  return ptov(index << FRAME_SHIFT);
}

void frame_free (void * kpage)
{
  uint32_t index = vtop (kpage) >> FRAME_SHIFT;
  frames[index].present = false;
  frames[index].pinned = false;
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

void frame_pin_memory (void *kpage)
{
  uint32_t index = vtop (kpage) >> FRAME_SHIFT;
  frames[index].pinned = true;
}

void frame_unpin_memory (void *kpage)
{
  uint32_t index = vtop (kpage) >> FRAME_SHIFT;
  frames[index].pinned = false;
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
