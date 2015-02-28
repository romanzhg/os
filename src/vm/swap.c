#include "vm/swap.h"

#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

#define BLOCKS_PER_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

struct swap
{
  bool available;
};

struct swap *swap_table;
struct block *swap_space;
int swap_size;
struct lock swap_lock;

/* Open the block device swap, and organize it to page size slots. */
void
swap_init (void)
{
  lock_init (&swap_lock);
  swap_space = block_get_role (BLOCK_SWAP);
  swap_size = ((uint32_t) block_size (swap_space)) / BLOCKS_PER_PAGE;
  swap_table = malloc (swap_size * sizeof (struct swap));
  if (swap_table == NULL)
    PANIC ("Cannot allocate space for swap table");

  int i;
  for (i = 0; i < swap_size; i++)
    swap_table[i].available = true;
}

/* Return the index of an empty swap slot, -1 if no swap is available. */
int swap_get (void)
{
  lock_acquire (&swap_lock);
  int i;
  for (i = 0; i< swap_size; i++)
    if (swap_table[i].available)
      {
        swap_table[i].available = false;
        lock_release (&swap_lock);
        return i;
      }
  lock_release (&swap_lock);
  PANIC ("Out of swap space");
}

/* Free the swap slot. */
void swap_free (int index)
{
  lock_acquire (&swap_lock);
  swap_table[index].available = true;
  lock_release (&swap_lock);
}

/* Copy PGSIZE bytes starting from source to the swap slot index. */
void
swap_write (int index, void  source)
{
  int base_sector = index * BLOCKS_PER_PAGE, i;
  for (i = 0; i < BLOCKS_PER_PAGE; i++)
    block_write (swap_space, base_sector + i, source + i * BLOCK_SECTOR_SIZE);
}


/* Copy PGSIZE bytes in swap slot index to dest. */
void
swap_read (int index, void *dest)
{
  int base_sector = index * BLOCKS_PER_PAGE, i;
  for (i = 0; i < BLOCKS_PER_PAGE; i++)
    block_read (swap_space, base_sector + i, dest + i * BLOCK_SECTOR_SIZE);
}
