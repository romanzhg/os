#ifndef VM_SWAP_H
#define VM_SWAP_H

// open the block device swap, and organize it to page size slots
void swap_init (void);

// return the index of an empty swap slot, -1 if no swap is available
int swap_get (void);

// free the swap slot
void swap_free (int index);

// copy PGSIZE bytes starting from source to the swap slot index
void swap_write (int index, void * source);

// copy PGSIZE bytes in swap slot index to dest
void swap_read (int index, void * dest);
#endif
