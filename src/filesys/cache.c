#include "filesys/cache.h"

#include <stdio.h>
#include <string.h>
#include <debug.h>
#include "devices/block.h"
#include "threads/malloc.h"
#include "threads/synch.h"

#define CACHE_SIZE 64

static struct lock cache_lock;
struct cache_entry * cache;
static int clock_hand;
struct block *block_fs;

static int allocate_cache (block_sector_t sector);
static void write (int i, off_t off, const void *buffer, int32_t length);
static void read (int i, off_t off, void *buffer, int32_t length);

void cache_close (void)
{
  int i;
  for (i = 0; i < CACHE_SIZE; i++)
    {
      if (cache[i].available == false) {
        ASSERT (cache[i].new_sec != 0xFFFFFFFF);
        //printf ("actually write to sector: %d\n", cache[i].new_sec);
        block_write (block_fs, cache[i].new_sec, cache[i].data);
      }
    }
}

void cache_init(void)
{
  lock_init (&cache_lock);
  block_fs = block_get_role (BLOCK_FILESYS);
  cache = malloc (CACHE_SIZE * sizeof (struct cache_entry));
  clock_hand = 0;

  int i;
  for (i = 0; i < CACHE_SIZE; i++)
    {
      cache[i].old_sec = 0xFFFFFFFF;
      cache[i].new_sec = 0xFFFFFFFF;
      cache[i].available = true;
      cond_init (&cache[i].cache_ready);

      cache[i].reference = 0;
      lock_init (&cache[i].entry_lock);
      cond_init (&cache[i].cache_ref);
    }
}

// take a sector number and return the index in cache table which contains
// this sector. This function is called with the cache_lock hold
static int
allocate_cache (block_sector_t sector) {
  int i;
  for (i = 0; i < CACHE_SIZE; i++)
    {
      if (cache[i].available == true) {
        cache[i].available = false;
        // i is available, bring in the sector, initialize the struct and return
        cache[i].new_sec = sector;
        cache[i].dirty = false;
        cache[i].accessed = false;
        lock_release(&cache_lock);
        
        block_read (block_fs, sector, cache[i].data);
        
        lock_acquire (&cache_lock);
        cache[i].old_sec = sector;
        cond_broadcast (&cache[i].cache_ready, &cache_lock);
        return i;
      }
    }
  // no cache entry available, going to evict a random one
  clock_hand = 10;
  cache[clock_hand].new_sec = sector;
  lock_release (&cache_lock);

  // wait for the reference count to be zero
  lock_acquire (&cache[clock_hand].entry_lock);
  while (cache[clock_hand].reference != 0)
    cond_wait(&cache[clock_hand].cache_ref, &cache[clock_hand].entry_lock);
  lock_release (&cache[clock_hand].entry_lock);

  // TODO: do not need to write if it's not dirty
  block_write (block_fs, cache[clock_hand].old_sec, cache[clock_hand].data);
  block_read (block_fs, cache[clock_hand].new_sec, cache[clock_hand].data);
  
  lock_acquire (&cache_lock);
  cache[clock_hand].old_sec = sector;
  cond_broadcast (&cache[i].cache_ready, &cache_lock);
  return clock_hand;
}

static void write (int i, off_t off, const void *buffer, int32_t length) {
  lock_acquire (&cache[i].entry_lock);
  lock_release (&cache_lock);
  cache[i].reference++;
  lock_release (&cache[i].entry_lock);

  memcpy(cache[i].data + off, buffer, length);

  lock_acquire (&cache[i].entry_lock);
  cache[i].reference--;
  cond_broadcast (&cache[i].cache_ref, &cache[i].entry_lock);
  lock_release (&cache[i].entry_lock);
}

static void read (int i, off_t off, void *buffer, int32_t length) {
  lock_acquire (&cache[i].entry_lock);
  lock_release (&cache_lock);
  cache[i].reference++;
  lock_release (&cache[i].entry_lock);
  
  memcpy(buffer, cache[i].data + off, length);

  lock_acquire (&cache[i].entry_lock);
  cache[i].reference--;
  cond_broadcast (&cache[i].cache_ref, &cache[i].entry_lock);
  lock_release (&cache[i].entry_lock);
}

int32_t
cache_write (block_sector_t sector, off_t off, const void *buffer, int32_t length) {
  int i;
  lock_acquire (&cache_lock);
  loop_start_write:
  for (i = 0; i < CACHE_SIZE; i++)
    {
      if (cache[i].available == true)
        continue;
      if ((cache[i].old_sec == cache[i].new_sec) && cache[i].new_sec == sector)
        {
          // the sector is in cache, going to write to it
          // cache_lock get released in this function
          write (i, off, buffer, length);
          return length;
        }
      if (cache[i].old_sec == sector)
        {
          // the sector is being evicted, wait until eviction finish and
          // get a new cache entry
          cond_wait (&cache[i].cache_ready, &cache_lock);
          goto loop_start_write;
        }
      if (cache[i].new_sec == sector)
        {
          // the sector is being bringing in
          cond_wait (&cache[i].cache_ready, &cache_lock);
          write (i, off, buffer, length);
          return length;
        }
    }

  // went through the cache table but did not found the sector, allocate a new
  // entry for the sector
  // when exit this function cache_lock is released
  i = allocate_cache (sector);

  write (i, off, buffer, length);
  return length;
}

int32_t
cache_read (block_sector_t sector, off_t off, void *buffer, int32_t length) {
  int i;
  lock_acquire (&cache_lock);
  loop_start_read:
  for (i = 0; i < CACHE_SIZE; i++)
    {
      if (cache[i].available == true)
        continue;
      if ((cache[i].old_sec == cache[i].new_sec) && cache[i].new_sec == sector)
        {
          // the sector is in cache, going to write to it
          // cache_lock get released in this function
          read (i, off, buffer, length);
          return length;
        }
      if (cache[i].old_sec == sector)
        {
          // the sector is being evicted, wait until eviction finish and
          // get a new cache entry
          cond_wait (&cache[i].cache_ready, &cache_lock);
          goto loop_start_read;
        }
      if (cache[i].new_sec == sector)
        {
          // the sector is being bringing in
          cond_wait (&cache[i].cache_ready, &cache_lock);
          read (i, off, buffer, length);
          return length;
        }
    }

  // went through the cache table but did not found the sector, allocate a new
  // entry for the sector
  // when exit this function cache_lock is released
  i = allocate_cache (sector);

  read (i, off, buffer, length);
  return length;
}
