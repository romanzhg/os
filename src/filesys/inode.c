#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include <stdio.h>
#include "filesys/cache.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define INDIRECT_BASE 124
#define DINDIRECT_BASE (INDIRECT_BASE + 128)
/* The maximum of sectors can be allocated. */
#define DINDIRECT_LIMIT (DINDIRECT_BASE + 128 * 128)

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    block_sector_t blocks[INDIRECT_BASE];
    block_sector_t first_indirect;
    block_sector_t double_indirect;
  };

struct indirect_disk
  {
    block_sector_t blocks[128];
  };

static block_sector_t
inode_get_index (const struct inode_disk *node, size_t index);
bool
extend_inode_length (struct inode_disk *inode_disk, off_t new_length, bool create);
int
extend_inode (block_sector_t* blocks, int blocks_length, int start_index, int blocks_to_allocate);
int
extend_indirect_inode(block_sector_t* indirect_block_p, int start_index, int blocks_to_allocate);
int
extend_dindirect_inode(block_sector_t* dindirect_block_p, int start_index, int blocks_to_allocate);

static char zeros[BLOCK_SECTOR_SIZE];

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
    struct lock lock;
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode_length (inode))
    return inode_get_index (&inode->data, pos / BLOCK_SECTOR_SIZE);
  else
    return -1;
}

static block_sector_t
inode_get_index (const struct inode_disk *node, size_t index)
{
  // index < 124
  if (index < INDIRECT_BASE)
    {
      return node->blocks[index];
    }
  // index < 252
  else if (index < DINDIRECT_BASE)
    {
      struct indirect_disk indirect;
      cache_read(node->first_indirect, 0, &indirect, BLOCK_SECTOR_SIZE);
      return indirect.blocks[index - INDIRECT_BASE];
    }
  else if (index < DINDIRECT_LIMIT)
    {
      int d_index = (index - DINDIRECT_BASE) / 128;

      struct indirect_disk indirect;
      cache_read(node->double_indirect, 0, &indirect, BLOCK_SECTOR_SIZE);

      struct indirect_disk d_indirect;
      cache_read(indirect.blocks[d_index], 0, &d_indirect, BLOCK_SECTOR_SIZE);

      return d_indirect.blocks[(index - DINDIRECT_BASE) % 128];
    }
  else
    PANIC ("file too long");
  return 0;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
  cache_init ();
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      if (!extend_inode_length (disk_inode, length, true))
        return false;
      disk_inode->magic = INODE_MAGIC;
      cache_write (sector, 0, disk_inode, BLOCK_SECTOR_SIZE);
      success = true;
      //printf ("successfully created the inode\n");
      //printf ("created the inode with length: %d\n", disk_inode->length);
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_init (&inode->lock);
  cache_read (inode->sector, 0, &inode->data, BLOCK_SECTOR_SIZE);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          size_t i;
          for (i = 0; i < bytes_to_sectors (inode_length(inode)); i++)
            free_map_release (inode_get_index (&inode->data, i), 1);
        }

      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;

  // TODO: for everything beyond the length return no byes
  // for a read only return the bytes is in the range
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      //printf ("going to read at index: %d\n", sector_idx);
      if ((int)sector_idx == -1)
        return bytes_read;
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          cache_read (sector_idx, 0, buffer + bytes_read, BLOCK_SECTOR_SIZE);
        }
      else 
        {
          cache_read (sector_idx, sector_ofs, buffer + bytes_read, chunk_size);
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

int
extend_inode (block_sector_t* blocks, int blocks_length, int start_index, int blocks_to_allocate)
{
  if (blocks_to_allocate == 0)
    return 0;
  int to_allocate_max = blocks_length - start_index;
  int to_allocate = blocks_to_allocate < to_allocate_max ? blocks_to_allocate : to_allocate_max;

  if (!free_map_allocate_mul (blocks + start_index, to_allocate))
    return -1;

  int i;
  // zero newly allocated blocks
  for (i = 0; i < to_allocate; i++)
    cache_write(blocks[start_index + i], 0, zeros, BLOCK_SECTOR_SIZE);
  return to_allocate;
}

int
extend_indirect_inode(block_sector_t* indirect_block_p, int start_index, int blocks_to_allocate)
{
  if (blocks_to_allocate == 0)
    return 0;
  struct indirect_disk indirect_block;
  if (*indirect_block_p== 0)
    {
      if (!free_map_allocate(1, indirect_block_p))
        return -1;
      cache_write(*indirect_block_p, 0, zeros, BLOCK_SECTOR_SIZE);
    }
  cache_read (*indirect_block_p, 0, &indirect_block, BLOCK_SECTOR_SIZE);
  int rtn = extend_inode (indirect_block.blocks, 128, start_index, blocks_to_allocate);

  if (rtn != -1)
    cache_write (*indirect_block_p, 0, &indirect_block, BLOCK_SECTOR_SIZE);
  return rtn;
}

int
extend_dindirect_inode(block_sector_t* dindirect_block_p, int start_index, int blocks_to_allocate)
{
  if (blocks_to_allocate == 0)
    return 0;
  struct indirect_disk dindirect_block;
  if (*dindirect_block_p == 0)
    {
      if (!free_map_allocate(1, dindirect_block_p))
        return -1;
      cache_write(*dindirect_block_p, 0, zeros, BLOCK_SECTOR_SIZE);
    }
  cache_read (*dindirect_block_p, 0, &dindirect_block, BLOCK_SECTOR_SIZE);

  int start_dindex = start_index / 128;
  int blocks_allocated = 0;
  while (blocks_to_allocate != 0)
    {
      int rtn = extend_indirect_inode(&dindirect_block.blocks[start_dindex], start_index % 128, blocks_to_allocate);
      if (rtn ==  -1)
        break;
      blocks_allocated += rtn;
      blocks_to_allocate -= rtn;
      start_index += rtn;
      start_dindex += 1;
    }
  return blocks_allocated;
}

bool
extend_inode_length (struct inode_disk *inode_disk, off_t new_length, bool create)
{
  // TODO: need to obtain the lock for this inode
  if (new_length <= inode_disk->length)
    return true;
  
  // old and new ending block index
  int blocks_to_allocate;
  int start_index;
  if (create)
    {
      blocks_to_allocate = bytes_to_sectors(new_length);
      start_index = 0;
    }
  else
    {
      blocks_to_allocate = bytes_to_sectors(new_length) - bytes_to_sectors(inode_disk->length);
      start_index = bytes_to_sectors(inode_disk->length);
    }

  if (start_index < INDIRECT_BASE)
    {
      int rtn = extend_inode (inode_disk->blocks, INDIRECT_BASE, start_index, blocks_to_allocate);
      if (rtn == -1)
        return false;
      //printf ("allocated blocks: %d\n", rtn);
      blocks_to_allocate -= rtn;
      start_index += rtn;
    }
  if (start_index < DINDIRECT_BASE)
    {
      int rtn = extend_indirect_inode (&inode_disk->first_indirect, start_index - INDIRECT_BASE, blocks_to_allocate);
      if (rtn == -1)
        return false;
      blocks_to_allocate -= rtn;
      start_index += rtn;
    }
  if (start_index < DINDIRECT_LIMIT)
    {
      int rtn = extend_dindirect_inode (&inode_disk->double_indirect, start_index - DINDIRECT_BASE, blocks_to_allocate);
      if (rtn == -1)
        return false;
      blocks_to_allocate -= rtn;
      start_index += rtn;
    }
  ASSERT (blocks_to_allocate == 0);

  inode_disk->length = new_length;
  return true;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;

  if (inode->deny_write_cnt)
    return 0;

  if (!extend_inode_length (&inode->data, size + offset, false))
    return 0;
  cache_write (inode->sector, 0, &inode->data, BLOCK_SECTOR_SIZE);
  
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          cache_write (sector_idx, 0, buffer + bytes_written, BLOCK_SECTOR_SIZE);
        }
      else 
        {
          cache_write (sector_idx, sector_ofs, buffer + bytes_written, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  struct inode_disk inode_disk;
  cache_read(inode->sector, 0, &inode_disk, BLOCK_SECTOR_SIZE);
  return inode_disk.length;
}
