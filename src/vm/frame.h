#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "threads/synch.h"

// FRAME TABLE
struct hash frames ;

// Lock to access the frame table
struct lock frame ;

// List of all the frames present in the user pool
struct list frame_list ;

// Frame table entry
struct frame
{
	struct hash_elem hash_elem ;			// Hash element for storing frame table in the hash
	
	void *kpage ;							// Kernel virtual address of the frame
	struct page *p ;						// Supplymentary page table entry this frame corresponds to
	struct thread *t ;						// The thread to which this frame belongs to
	struct list_elem elem ;					// List element for the list used for eviction algorithm
} ;

// Initialize the frame table and the lock to synchronize the access to frame table
void frame_init (void) ;

/* Returns a hash value for frame p. */
unsigned frame_hash (const struct hash_elem *p_, void *aux UNUSED) ;

/* Returns true if frame a precedes frame b. */
bool frame_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) ;

/* Returns the frame containing the given physical address, or a null pointer if no such frame exists. */
struct frame * frame_lookup (void *address) ;

// Insert an element into the frame table
struct hash_elem * frame_insert ( struct hash_elem *new ) ;

// Allocate a frame from user pool
struct frame * frame_allocate (void) ;

// Deallocate the frame and update in the frame table
void frame_deallocate (void *kpage) ;

// Evict a frame using FIFO replacement algorithm and return a free frame
struct frame * evict_frame (void) ;

#endif
