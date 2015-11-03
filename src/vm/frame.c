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
struct frame * frame_allocate (void)
{
	void *kpage = palloc_get_page(PAL_USER) ;
	if ( kpage == NULL )
	{
		/*printf ("Ran out of frame\n");*/
		struct frame *evicted = evict_frame() ;
		if ( evicted == NULL )
			printf ( "EVICTED is NULL\n");
		/*printf ( "Exit frame_allocate\n");*/
		return evicted ;
		//PANIC("OUT OF MEMORY");
	}

	struct frame *f = (struct frame *) malloc ( sizeof(struct frame) ) ;
	if ( f == NULL )
	{
		palloc_free_page(kpage) ;
		PANIC("OUT OF KERNEL MEMEORY");
	}
	f->kpage = kpage ;
	f->t = thread_current() ;
	list_push_back(&frame_list,&f->elem);

	/*lock_acquire(&frame);*/
	frame_insert(&f->hash_elem);
	/*lock_release(&frame);*/

	return f ;
}

// Deallocate a frame and update the same in the frame table
void frame_deallocate (void *kpage)
{
	struct frame *f = frame_lookup(kpage) ;
	if ( f == NULL )
	{
		PANIC("Deallocating a FRAME not present\n");
		return ;
	}

	palloc_free_page(kpage) ;
	hash_delete( &frames, &f->hash_elem) ;
	free(f) ;

	return ;
}

struct frame * evict_frame()
{
	/*int x ;*/

	struct list_elem *e ;
	e = list_pop_front (&frame_list) ;
	struct frame *f = list_entry(e, struct frame, elem) ;
	list_push_back(&frame_list, e) ;

	/*static int victim = 1 ;*/
	/*[>int victim = random_ulong() % hash_size(&frames);<]*/

	/*struct hash *h = &frames ;*/
	/*struct hash_iterator i;*/
	/*hash_first(&i,h);*/
	/*for ( x = 0 ; x < victim ; x ++ )*/
		/*hash_next(&i);*/
	/*victim++ ;*/

	/*struct frame *f = hash_entry(hash_cur(&i),struct frame, hash_elem);*/

	if ( f->p->addr == NULL )
		printf ( "F became NULL\n") ;

	/*printf ( "Hash Entry, addr: %p\n", f->p->addr ) ;*/
	bool dirty = pagedir_is_dirty(f->t->pagedir, f->p->addr) ;
	if ( dirty )
	{
		/*printf ( "Dirty. Writing to swap\n");*/
		swap_page(f->p, f->t) ;
	}
	else
	{
		/*printf ( "NOT dirty\n");*/
		f->p->kpage = NULL ;
		pagedir_clear_page(f->t->pagedir,f->p->addr);
	}

	f->p = NULL ;
	f->t = NULL ;

	/*printf ( "Exit evict_frame\n");*/
	return f ;
}
