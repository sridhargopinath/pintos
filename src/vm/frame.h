#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <hash.h>
#include "threads/synch.h"

// FRAME TABLE
struct hash frames ;

// Lock to access the frame table
struct lock frame ;

struct list frame_list ;

// Frame table entry
struct frame
{
  struct hash_elem hash_elem ;
  void *kpage ;
  struct page *p ;
  struct thread *t ;
  struct list_elem elem ;
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

struct frame * evict_frame (void) ;

#endif
