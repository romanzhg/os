#include "vm/swap.h"

#include "threads/synch.h"
#include "threads/malloc.h"
#include "devices/block.h"

// for debug only
#include <stdio.h>

struct swap
{
  bool available;
};

struct swap * swap_table;
struct block * swap_space;
int swap_size;
struct lock swap_lock;

// open the block device swap, and organize it to page size slots
void
swap_init (void)
{
  lock_init (&swap_lock);
  swap_space = block_get_role (BLOCK_SWAP);
  swap_size = ((uint32_t) block_size (swap_space)) / 8;
  swap_table = malloc (swap_size * sizeof (struct swap));
  if (swap_table == NULL)
    {
      ASSERT(false);
    }

  int i;
  for (i = 0; i < swap_size; i++)
    swap_table[i].available = true;
}

// return the index of an empty swap slot, -1 if no swap is available
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
  return -1;
}

// free the swap slot
void swap_free (int index)
{
  lock_acquire (&swap_lock);
  swap_table[index].available = true;
  lock_release (&swap_lock);
}

// copy PGSIZE bytes starting from source to the swap slot index
void
swap_write (int index, void * source)
{
  int base_sector = index * 8;
  int i;
  for (i = 0; i < 8; i++)
    block_write (swap_space, base_sector + i, source + i * BLOCK_SECTOR_SIZE);
}


// copy PGSIZE bytes in swap slot index to dest
void
swap_read (int index, void * dest)
{
  int base_sector = index * 8;
  int i;
  for (i = 0; i < 8; i++)
    block_read (swap_space, base_sector + i, dest + i * BLOCK_SECTOR_SIZE);
}
