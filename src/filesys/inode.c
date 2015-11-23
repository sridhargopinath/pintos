#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

#include "filesys/cache.h"
#include "threads/vaddr.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define MAXFILESIZE (1 << 23)

#define LEVEL1SHIFT 16
#define LEVEL1MASK BITMASK(16, 7)
#define LEVEL1SIZE 128

#define LEVEL2SHIFT 9
#define LEVEL2MASK BITMASK(9, 7)
#define LEVEL2SIZE 128

#define SECTORMASK BITMASK(0,9)

// Block of all zeroes which is used to write 0s to a file block
/*static char zeros[BLOCK_SECTOR_SIZE];*/
char *zeros ;

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos) 
{
	bool success ;

	ASSERT (inode != NULL);
	if ( pos > MAXFILESIZE )
		return -1 ;

	if ( inode->sector == 0 )
		return inode->data.start + pos / BLOCK_SECTOR_SIZE ;

	/*printf ( "inside byte to sector with pos %u\n", pos);*/
	int level1pos = ((pos & LEVEL1MASK) >> LEVEL1SHIFT) * sizeof(block_sector_t) ;
	block_sector_t level1 ;

	/*printf ( "   level1pos: %d\n", level1pos) ;*/
	/*printf ( "   LEVEL1MASK: %d and LEVEL2MASK: %d\n", LEVEL1MASK, LEVEL2MASK ) ;*/
	read_cache ( inode->data.start, &level1, level1pos, sizeof(block_sector_t) ) ;

	// If second level not present, create a second level entry
	if ( level1 == 0 )
	{
		success = free_map_allocate(1, &level1 ) ;
		if ( success == false )
		{
			/*printf ( "failed1\n\n") ;*/
			return -1 ;
		}

		write_cache ( level1, zeros, 0, BLOCK_SECTOR_SIZE, false ) ;

		write_cache ( inode->data.start, &level1, level1pos, sizeof(block_sector_t), true) ;
	}

	int level2pos = ((pos & LEVEL2MASK) >> LEVEL2SHIFT) * sizeof(block_sector_t) ;
	block_sector_t level2 ;

	read_cache ( level1, &level2, level2pos, sizeof(block_sector_t) ) ;

	// If second level not present, create a second level entry
	if ( level2 == 0 )
	{
		success = free_map_allocate(1, &level2 ) ;
		if ( success == false )
		{
			/*printf ( "failed2\n\n") ;*/
			return -1 ;
		}

		write_cache ( level2, zeros, 0, BLOCK_SECTOR_SIZE, false ) ;

		write_cache ( level1, &level2, level2pos, sizeof(block_sector_t), true) ;
	}

	/*printf ( "  Level1: %d Level2: %d\n", level1, level2 ) ;*/
	/*if ( pos > inode->data.length )*/
	/*{*/
		/*printf ( "pos greater than length\n") ;*/
		/*inode->data.length = pos ;*/

		/*write_cache ( inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE, false ) ;*/
	/*}*/

	return level2 ;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);

  zeros = (char*)malloc(BLOCK_SECTOR_SIZE) ;
  memset(zeros, 0, BLOCK_SECTOR_SIZE) ;
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool isdir)
{
	/*printf ( "\nin inode create with size %d\n", length);*/
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      /*size_t sectors = bytes_to_sectors (length);*/
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
	  disk_inode->isdir = isdir ;
      if (free_map_allocate (1, &disk_inode->start)) 
        {
          /*block_write (fs_device, sector, disk_inode);*/
		  write_cache ( sector, disk_inode, 0, BLOCK_SECTOR_SIZE, false ) ;

		  write_cache ( disk_inode->start, zeros, 0, BLOCK_SECTOR_SIZE, false ) ;
          /*if (sectors > 0) */
            /*{*/
              /*size_t i;*/
              
              /*for (i = 0; i < sectors; i++) */
                /*block_write (fs_device, disk_inode->start + i, zeros);*/
            /*}*/
          success = true; 
        } 
      free (disk_inode);
    }
  /*printf ( "After inode create\n");*/
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
	/*printf ( "inside inode open\n");*/
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
  /*block_read (fs_device, inode->sector, &inode->data);*/
  read_cache(inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE ) ;
  /*printf ( "End loop inode: \n");*/
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
			/*printf ( "REmoveing inside inode\n") ;*/
		  int i, j ;
		  char level1arr[BLOCK_SECTOR_SIZE] ;
		  read_cache( inode->data.start, level1arr, 0, BLOCK_SECTOR_SIZE ) ;

		  for ( i = 0 ; i < LEVEL1SIZE ; i ++ )
		  {
			  block_sector_t *level1 = (block_sector_t *) (level1arr+(i*sizeof(block_sector_t))) ;
			  /*printf ( "\nlevel%d: %d\n", i, *level1) ;*/
			  if ( *level1 == 0 )
				  continue ;

			  char level2arr[BLOCK_SECTOR_SIZE] ;
			  read_cache( *level1, level2arr, 0, BLOCK_SECTOR_SIZE ) ;

			  for ( j = 0 ; j < LEVEL2SIZE ; j ++ )
			  {
				  block_sector_t *level2 = (block_sector_t *) (level2arr+(j*sizeof(block_sector_t))) ;
				  /*printf ( "level%d: %d   ", j, *level2 ) ;*/
				  if ( *level2 == 0 )
					  continue ;

				  free_map_release(*level2, 1);
			  }

			  free_map_release(*level1, 1) ;
		  }

          free_map_release (inode->sector, 1);
          /*free_map_release (inode->data.start,*/
                            /*bytes_to_sectors (inode->data.length)); */
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
	/*printf ( "inside inode read at\n") ;*/
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  /*uint8_t *bounce = NULL;*/

  if ( offset + size > inode_length(inode) )
	  return 0 ;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
	  if ( (signed)sector_idx == -1 )
		  break ;

	  /*printf ( "after byte to sector %d\n", sector_idx) ;*/
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
          /* Read full sector directly into caller's buffer. */
          /*block_read (fs_device, sector_idx, buffer + bytes_read);*/
			read_cache ( sector_idx, buffer + bytes_read, 0, BLOCK_SECTOR_SIZE ) ;
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          /*if (bounce == NULL) */
            /*{*/
              /*bounce = malloc (BLOCK_SECTOR_SIZE);*/
              /*if (bounce == NULL)*/
                /*break;*/
            /*}*/
          /*block_read (fs_device, sector_idx, bounce);*/
		  read_cache ( sector_idx, buffer + bytes_read, sector_ofs, chunk_size ) ;
          /*memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);*/
        }
      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  /*free (bounce);*/

  return bytes_read;
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
	/*printf ( "Inside inode write at %u\n", inode->sector );*/
	/*printf ( "size: %d, offset: %d\n", size, offset) ;*/
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  /*uint8_t *bounce = NULL;*/

  if (inode->deny_write_cnt)
    return 0;

  if ( size+offset > inode->data.length )
  {
	  /*printf ( "pos greater than length\n") ;*/
	  inode->data.length = size+offset ;

	  write_cache ( inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE, false ) ;
  }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
	  if ( (signed)sector_idx == -1 )
		  break ;

	  /*printf ( "  finished byte to sector\n");*/
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

	  /*printf ( "inodeleft %d sector %d minleft %d\n", inode_left, sector_left, min_left) ;*/
      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          /*block_write (fs_device, sector_idx, buffer + bytes_written);*/
			write_cache ( sector_idx, buffer + bytes_written, 0, BLOCK_SECTOR_SIZE, false ) ;
        }
      else 
        {
          /*[> We need a bounce buffer. <]*/
          /*if (bounce == NULL) */
            /*{*/
              /*bounce = malloc (BLOCK_SECTOR_SIZE);*/
              /*if (bounce == NULL)*/
                /*break;*/
            /*}*/

		  bool read_before_write = false ;
          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            /*block_read (fs_device, sector_idx, bounce);*/
			  /*read_cache ( sector_idx, bounce ) ;*/
			  read_before_write = true ;
          else
            /*memset (bounce, 0, BLOCK_SECTOR_SIZE);*/
			  read_before_write = false ;
          /*memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);*/
          /*block_write (fs_device, sector_idx, bounce);*/
		  write_cache ( sector_idx, buffer + bytes_written, sector_ofs, chunk_size, read_before_write ) ;
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  /*free (bounce);*/

  /*printf ( "Bytes written: %d\n", bytes_written) ;*/
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
	/*printf ( "inode length %d\n", inode->data.length) ;*/
  return inode->data.length;
}

// Returns true if the inode represents a directory
bool inode_isdir ( const struct inode *inode )
{
	if ( inode->data.isdir == 1 )
		return true ;
	return false ;
}

void free_zeros (void)
{
	free(zeros) ;
	return ;
}
