#ifndef VM_SWAP_H
#define VM_SWAP_H

/* Open the block device swap, and organize it to page size slots. */
void swap_init (void);

/* Return the index of an empty swap slot, -1 if no swap is available. */
int swap_get (void);

/* Free the swap slot. */
void swap_free (int index);

/* Copy PGSIZE bytes starting from source to the swap slot index. */
void swap_write (int index, void *source);

/* Copy PGSIZE bytes in swap slot index to dest. */
void swap_read (int index, void *dest);
#endif
