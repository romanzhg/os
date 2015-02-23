#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdbool.h>
#include <stdint.h>

extern struct frame* frames;

void frame_init(void);
void *frame_get(int flags);
void frame_free(void * kpage);
void frame_set_mapping (void *upage, void *kpage, bool writable);

extern struct lock frame_lock;
#endif
