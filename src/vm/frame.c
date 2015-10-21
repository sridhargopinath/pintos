#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "threads/palloc.h"
#include "vm/frame.h"
#include "threads/malloc.h"
#include "threads/thread.h"

// Initialize the frame table and the lock to synchronize the access to frame table
void frame_init ()
{
	hash_init (&frames, frame_hash, frame_less, NULL);
	lock_init(&frame) ;

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

  /*lock_acquire(&frame) ;*/
  e = hash_find (&frames, &p.hash_elem);
  /*lock_release(&frame) ;*/

  return e != NULL ? hash_entry (e, struct frame, hash_elem) : NULL;
}

// Insert an element into the frame table
struct hash_elem * frame_insert ( struct hash_elem *new )
{
	struct hash_elem *e ;
	
	/*lock_acquire(&frame);*/
	e = hash_insert (&frames, new);
	/*lock_release(&frame);*/

	return e ;	
}

// Allocate a frame from the user pool
void * frame_allocate (void)
{
	void *kpage = palloc_get_page(PAL_USER) ;
	if ( kpage == NULL )
		PANIC("OUT OF MEMORY");

	struct frame *f = (struct frame *) malloc ( sizeof(struct frame) ) ;
	if ( f == NULL )
	{
		palloc_free_page(kpage) ;
		PANIC("OUT OF KERNEL MEMEORY");
	}
	f->kpage = kpage ;
	f->t = thread_current() ;

	/*lock_acquire(&frame);*/
	frame_insert(&f->hash_elem);
	/*lock_release(&frame);*/

	return kpage ;
}

// Deallocate a frame and update the same in the frame table
void frame_deallocate (void *kpage)
{

}
