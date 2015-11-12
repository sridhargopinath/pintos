#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <hash.h>
#include "threads/synch.h"
#include "devices/block.h"
#include <hash.h>

#define MAX_BUFFER_CACHE 64

// BUFFER CACHE Blocks
struct hash cache_blocks ;

// Lock to access the cache blocks
struct lock cache ;

// List of all the cache blocks in memory
struct list cache_list ;

// Cache block table entry
struct cache
{
	block_sector_t idx ;					// HASH KEY. Sector number on the disk this block belongs to

	void *kblock ;							// Kernel block which stores the block data

	bool accessed ;							// Accessed flag
	bool dirty ;							// Dirty flag
	int in_use ;							// Number of processes currently using this cache block

	struct hash_elem hash_elem ;			// Hash element for storing cache block in the hash
	struct list_elem elem ;					// List element for the list used for eviction algorithm
} ;

// Initialize the cache blocks and the lock to synchronize the access to cache blocks
void cache_init (void) ;

/* Returns a hash value for cache block p. */
unsigned cache_hash (const struct hash_elem *p_, void *aux UNUSED) ;

/* Returns true if cache block a precedes cache block b. */
bool cache_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) ;

/* Returns the cache block containing the given file system block ID, or a null pointer if no such cache block exists. */
struct cache * cache_lookup (block_sector_t idx) ;

// Insert an element into the cache block
struct hash_elem * cache_insert ( struct hash_elem *new ) ;

// Query the hash to find the cache block with key IDX
// If not present, evict a cache block and create a new cache block with IDX and INODE information
struct cache * get_cache_block ( block_sector_t idx ) ;

// Allocate a cache block in the memory
struct cache * cache_allocate ( block_sector_t idx ) ;

// Read from a block IDX in the buffer cache to ADDR
void read_cache ( block_sector_t idx, void *addr ) ;

// Write to the block IDX in the buffer cache from ADDR
void write_cache ( block_sector_t idx, const void *addr ) ;

// Deallocate the cache block and write back to disk if necessary
void cache_deallocate (block_sector_t idx) ;

// Evict a cache block using clock algorithm and return the free cache block
void evict_cache (void) ;

// Asynchronously write the block to the file system and free the cache block in memory
void release_block ( void *aux ) ;

// Release all the cache blocks in memory and write the dirty blocks to disk
void release_cache (void) ;

#endif
