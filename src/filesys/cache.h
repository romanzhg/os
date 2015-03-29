#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdbool.h>
#include "devices/block.h"
#include "threads/synch.h"
#include "filesys/off_t.h"

struct cache_entry
{
  // protected by the cache table lock
  block_sector_t old_sec;
  block_sector_t new_sec;
  struct condition cache_ready;
  bool accessed; // should be initialized when allocating the cache
  bool dirty;
  bool available;

  // the monitor on cache reference
  int reference;
  struct lock entry_lock;
  struct condition cache_ref;

  uint8_t data[BLOCK_SECTOR_SIZE];
};

void cache_init (void);
void cache_close (void);
int32_t cache_write (block_sector_t sector, off_t off, const void *buffer, int32_t length);
int32_t cache_read (block_sector_t sector, off_t off, void *buffer, int32_t length);
#endif
