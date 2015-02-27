#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>

struct frame
{
  bool pinned;            /* Whether this frame should be locked in memory */
  bool present;           /* Is this a pagable user frame */
  void* uaddr;            /* The user virtual addredd mapped to this frame */
  struct thread * thread; /* The owner of this frame */
};

extern struct lock frame_lock;

void frame_init(void);
void *frame_get(int flags);
void frame_free(void * kpage);
void frame_set_mapping (void *upage, void *kpage, bool writable);
void frame_pin_memory (void *kpage);
void frame_unpin_memory (void *kpage);

#endif
