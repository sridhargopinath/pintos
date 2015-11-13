#include <stdio.h>
#include <string.h>
#include "filesys/cache.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include "devices/timer.h"

// List of all the blocks that are in the process of getting evicted
struct list evict_list ;

struct lock evict ;

// Initialize the cache block table, list of cache blocks and the lock to synchronize the access to the list of cache blocks
void cache_init ()
{
	/*printf ( "Enter init\n");*/
	hash_init(&cache_blocks, cache_hash, cache_less, NULL);

	lock_init(&cache) ;
	list_init(&cache_list);

	lock_init(&evict) ;
	list_init(&evict_list);

	/*printf ( "return from init");*/

	return ;
}

/* Returns a hash value for cache block p. */
unsigned cache_hash (const struct hash_elem *p_, void *aux UNUSED)
{
	const struct cache *c = hash_entry (p_, struct cache, hash_elem);
	return hash_bytes (&c->idx, sizeof c->idx);
}

/* Returns true if cache block a precedes cache block b. */
bool cache_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
	const struct cache *a = hash_entry (a_, struct cache, hash_elem);
	const struct cache *b = hash_entry (b_, struct cache, hash_elem);

	return a->idx < b->idx;
}

/* Returns the cache block containing the given file system block ID, or a null pointer if no such cache block exists. */
struct cache * cache_lookup (block_sector_t idx)
{
	struct cache c;
	struct hash_elem *e;

	c.idx = idx;
	e = hash_find (&cache_blocks, &c.hash_elem);

	return e != NULL ? hash_entry (e, struct cache, hash_elem) : NULL;
}

// Insert an element into the cache blocks table
struct hash_elem * cache_insert ( struct hash_elem *new )
{
	struct hash_elem *e ;

	e = hash_insert (&cache_blocks, new);

	return e ;	
}

// Search if the block IDX is present in the evict_list
// This means that the block is about to be evicted. The caller will wait till the block is evicted
static bool search_block_in_evicted ( block_sector_t idx )
{
	struct list_elem *e ;
	bool result = false ;

	for ( e = list_begin(&evict_list) ; e != list_end(&evict_list) ; e = list_next(e) )
	{
		struct cache *c = list_entry(e, struct cache, elem) ;
		if ( c->idx == idx )
		{
			result = true ;
			break ;
		}
	}

	return result ;
}

// Query the hash to find the cache block with key IDX
// If not present, evict a cache block and create a new cache block with IDX and INODE information
struct cache * get_cache_block ( block_sector_t idx, bool read )
{
	// I acquire the lock before I check if the block is present in evict_list because:
	// In between I release the lock EVICT and I acquire lock CACHE, someone else might come and evict IDX
	// Hence I need to acquire the lock on cache and make sure no one else can evict IDX
	lock_acquire(&cache);


	// Check if the block is about to be evicted from the buffer cache
	// If so, sleep for 20 ticks and wait for it to be evicted
	while ( 1 )
	{
		bool present ;
		lock_acquire(&evict) ;
		present = search_block_in_evicted ( idx ) ;
		lock_release(&evict) ;

		if ( present == true )
		{
			lock_release(&cache) ;
			timer_sleep(12);
			lock_acquire(&cache) ;
		}
		else
			break ;
	}

	// Check if the block is already present in the buffer cache
	struct cache *lookup = cache_lookup(idx) ;
	if ( lookup != NULL )
	{
		lookup->accessed = true ;
		lookup->in_use ++ ;

		lock_release(&cache);
		return lookup ;
	}

	lookup = cache_allocate ( idx ) ;

	lookup->accessed = true ;
	lookup->in_use ++ ;

	lock_release(&cache);

	if ( read == true )
	{
		// Actual read from the disk to the cache for the first time
		block_read ( fs_device, lookup->idx, lookup->kblock ) ;
	}

	return lookup ;
}

// Allocate a cache block in the memory
struct cache * cache_allocate ( block_sector_t idx )
{
	// Check the size of the buffer cache
	int size = hash_size(&cache_blocks) ;

	// Evict a buffer cache if the size if more than the limit
	if ( size >= MAX_BUFFER_CACHE )
	{
		// This will remove a cache entry both from the hash table and also from the list
		// This is followed by creating a new cache entry for the given IDX sector
		evict_cache() ;
	}

	struct cache *new = (struct cache*) malloc ( sizeof(struct cache) ) ;
	if ( new == NULL )
		PANIC("cache_allocate: Failed to allocate memory to cache");

	new->kblock = malloc (BLOCK_SECTOR_SIZE) ;
	if ( new->kblock == NULL )
		PANIC("cache_allocate: Failed to allocate memory to block");

	new->idx = idx ;

	/*new->inode = inode ;*/
	new->accessed = false ;
	new->dirty = false ;
	new->in_use = 0 ;

	cache_insert(&new->hash_elem);
	list_push_back(&cache_list, &new->elem);

	return new ;
}

// Read from the buffer cache of IDX to ADDR
void read_cache ( block_sector_t idx, void *addr, off_t ofs, int size )
{
	/*printf ( "read_cache\n");*/
	struct cache *c = get_cache_block(idx, true) ;

	memcpy (addr, c->kblock + ofs, size ) ;

	c->in_use -- ;
	c->accessed = true ;

	return ;
}

// Write to the buffer cache of IDX from ADDR
void write_cache ( block_sector_t idx, const void *addr, off_t ofs, int size, bool read_before_write )
{
	/*printf ( "write_cache\n");*/
	struct cache *c = get_cache_block(idx, read_before_write) ;

	if ( read_before_write == false && size != BLOCK_SECTOR_SIZE )
		memset ( c->kblock , 0, BLOCK_SECTOR_SIZE ) ;

	memcpy ( c->kblock + ofs, addr, size ) ;

	c->in_use -- ;
	c->accessed = true ;
	c->dirty = true ;

	return ;
}

// Deallocate the cache block and write back to disk if necessary
void cache_deallocate (block_sector_t idx)
{
	struct cache *c = cache_lookup(idx) ;
	if ( c == NULL )
		PANIC("cache_deallocate: Deallocating a cache block not present");

	// Acquire lock since you are modifying the cache block list
	lock_acquire(&cache) ;

	hash_delete(&cache_blocks, &c->hash_elem);
	list_remove(&c->elem);

	// Add this block to the list of evicting blocks
	lock_acquire(&evict) ;
	list_push_back(&evict_list, &c->elem) ;
	lock_release(&evict) ;

	lock_release(&cache) ;

	/*release_block(c) ;*/
	// Asynchrously write to the file system if required
	tid_t tid ;
	tid = thread_create("evict_cache", PRI_DEFAULT+1, release_block, c) ;
	if ( tid == TID_ERROR )
		PANIC("cache_deallocate: Not able to create a new thread");

	return ;
}

// Evict a cache block using clock algorithm and free the memory occupied by it
void evict_cache (void)
{
	struct list_elem *e ;
	struct cache *c ;

	while ( 1 )
	{
		e = list_pop_front(&cache_list) ;
		c = list_entry(e, struct cache, elem) ;

		if ( c->accessed == true )
		{
			c->accessed = false ;
			list_push_back(&cache_list, e) ;
		}
		else
		{
			if ( c->in_use != 0 )
				list_push_back(&cache_list, e) ;
			else
			{
				hash_delete(&cache_blocks, &c->hash_elem);

				// Add this block to the list of evicting blocks
				lock_acquire(&evict) ;
				list_push_back(&evict_list, &c->elem) ;
				lock_release(&evict) ;

				/*release_block(c) ;*/
				// Asynchronously write the block to the file system
				tid_t tid ;
				tid = thread_create( "evict_cache", PRI_DEFAULT+1, release_block, c) ;
				if ( tid == TID_ERROR )
					PANIC("evict_cache: Couldn't create a thread for release_block");

				break ;
			}
		}
	}

	return ;
}

// ASYNCHRONOUSLY Release the block from the buffer cache. Write this block to disk if it is dirty
void release_block ( void *aux )
{
	struct cache *c = (struct cache *) aux ;

	if ( c->dirty == true )
		block_write(fs_device, c->idx, c->kblock) ;

	// REMOVE this block from the list of evicting blocks
	lock_acquire(&evict) ;
	list_remove(&c->elem) ;
	lock_release(&evict) ;

	free(c->kblock) ;
	free(c);

	return ;
}

// Remove all the cache blocks in memory and write the dirty blocks to disk
// NO NEED to do this asynchronously
void release_cache (void)
{
	struct list_elem *e, *next ;

	lock_acquire(&cache);

	for ( e = list_begin(&cache_list) ; e != list_end(&cache_list) ; e = next )
	{
		next = list_next(e) ;

		struct cache *c = list_entry(e, struct cache, elem) ;

		hash_delete(&cache_blocks, &c->hash_elem);
		list_remove(&c->elem);

		if ( c->dirty == true )
			block_write(fs_device, c->idx, c->kblock) ;

		free(c->kblock) ;
		free(c);
	}

	lock_release(&cache);
	return ;
}
