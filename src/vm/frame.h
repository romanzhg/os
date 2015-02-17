#include <stdint.h>

extern struct frame* frames;

void frame_init(void);
void *frame_get(int flags);
void frame_free(void * p);
void frame_set_mapping (uint32_t *pd, void *upage, void *kpage);
