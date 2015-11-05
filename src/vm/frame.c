#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "threads/palloc.h"
#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include <random.h>
#include "userprog/pagedir.h"
#include "vm/swap.h"

// IMPORTANT: Functions in this file are always invoked by holding the FRAME lock

// Initialize the frame table and the lock to synchronize the access to frame table
void frame_init ()
{
	hash_init (&frames, frame_hash, frame_less, NULL);
	lock_init(&frame) ;
	list_init(&frame_list);

	return ;
}

/* Returns a hash value for frame p. */
unsigned frame_hash (const struct hash_elem *p_, void *aux UNUSED)
{
	const struct frame *p = hash_entry (p_, struct frame, hash_elem);
	return hash_bytes (&p->kpage, sizeof p->kpage);
}

/* Returns true if frame a precedes frame b. */
bool frame_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
	const struct frame *a = hash_entry (a_, struct frame, hash_elem);
	const struct frame *b = hash_entry (b_, struct frame, hash_elem);

	return a->kpage < b->kpage;
}

/* Returns the frame containing the given physical address,
   or a null pointer if no such frame exists. */
struct frame * frame_lookup (void *address)
{
	struct frame p;
	struct hash_elem *e;

	p.kpage = address;
	e = hash_find (&frames, &p.hash_elem);

	return e != NULL ? hash_entry (e, struct frame, hash_elem) : NULL;
}

// Insert an element into the frame table
struct hash_elem * frame_insert ( struct hash_elem *new )
{
	struct hash_elem *e ;

	e = hash_insert (&frames, new);

	return e ;	
}

// Allocate a frame from the user pool.
// If the user pool is empty, evict a page and then return that.
// The evicted page will be written to the swap space if it is dirty. Else, its reference is dropped
struct frame * frame_allocate (void)
{
	void *kpage = palloc_get_page(PAL_USER) ;
	if ( kpage == NULL )
	{
		// No more free frames available, evict a frame in memory
		struct frame *evicted = evict_frame() ;
		evicted->t = thread_current() ;
		
		return evicted ;
	}

	// Get memory for new frame table entry
	struct frame *f = (struct frame *)malloc(sizeof(struct frame)) ;
	if ( f == NULL )
		PANIC("frame_allocate: Could not allocate memory for struct frame");
	
	f->kpage = kpage ;
	f->t = thread_current() ;
	frame_insert(&f->hash_elem);

	// Add the frame to the list of frames
	list_push_back(&frame_list,&f->elem);

	return f ;
}

// Deallocate a frame and update the same in the frame table
void frame_deallocate (void *kpage)
{
	struct frame *f = frame_lookup(kpage) ;
	if ( f == NULL )
		PANIC("Deallocating a FRAME not present\n");

	palloc_free_page(kpage) ;
	hash_delete( &frames, &f->hash_elem) ;
	list_remove(&f->elem) ;
	free(f) ;

	return ;
}

// Function to evict a frame.
// If the frame is dirty, write it to SWAP. Else, remove its reference
// IMPORTANT: Uses FIFO page replacement algorithm. This can be improved TODO
struct frame * evict_frame()
{
	// Get the frame to evict using FIFO algorithm
	// Pop from the frame_list and push it back again
	struct list_elem *e ;
	e = list_pop_front (&frame_list) ;
	struct frame *f = list_entry(e, struct frame, elem) ;
	list_push_back(&frame_list, e) ;

	bool dirty = pagedir_is_dirty(f->t->pagedir, f->p->addr) ;
	if ( dirty )
	{
		// Move the frame to swap block device
		swap_page(f->p, f->t) ;
	}
	else
	{
		// Remove the frames' references
		f->p->kpage = NULL ;
		pagedir_clear_page(f->t->pagedir,f->p->addr);
	}

	f->p = NULL ;
	f->t = NULL ;

	return f ;
}
