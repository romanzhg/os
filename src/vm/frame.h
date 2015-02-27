#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>


struct frame
{
  // TODO: need to implement memory pinning

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

extern struct lock frame_lock;

void frame_init(void);
void *frame_get(int flags);
void frame_free(void * kpage);
void frame_set_mapping (void *upage, void *kpage, bool writable);

#endif
