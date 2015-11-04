#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "vm/page.h"
#include "threads/palloc.h"
#include "filesys/file.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "threads/malloc.h"
#include "userprog/syscall.h"
#include "vm/swap.h"

// Initialize the supplymentary hash table
bool page_init (struct hash *pages)
{
	bool success ;

	success = hash_init (pages, page_hash, page_less, NULL);

	return success ;
}

/* Returns a hash value for page p. */
unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED)
{
	const struct page *p = hash_entry (p_, struct page, hash_elem);
	return hash_bytes (&p->addr, sizeof p->addr);
}

/* Returns true if page a precedes page b. */
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED)
{
	const struct page *a = hash_entry (a_, struct page, hash_elem);
	const struct page *b = hash_entry (b_, struct page, hash_elem);

	return a->addr < b->addr;
}

/* Returns the page containing the given virtual address,
   or a null pointer if no such page exists. */
struct page * page_lookup (void *address)
{
	struct page p;
	struct hash_elem *e;

	p.addr = address;
	e = hash_find (&thread_current()->pages, &p.hash_elem);
	return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;
}

// Insert an element into the supplymentary hash table
struct hash_elem * page_insert ( struct hash *pages, struct hash_elem *new )
{
	struct hash_elem *e ;

	e = hash_insert (pages, new);

	return e ;	
}

static bool install_page (void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	   address, then map our page there. */
	return (pagedir_get_page (t->pagedir, upage) == NULL
			&& pagedir_set_page (t->pagedir, upage, kpage, writable));
}

// Function to allocate a frame to the given address
// Return FALSE if it is a bad address
bool get_page( void *addr )
{
	/*printf ( "Inside get_page\n");*/
	/*printf ( "Page allocate address: %p\n", addr ) ;*/
	/*printf ( "Entered page_allocate of %s\n", thread_current()->name) ;*/
	/*struct thread *cur = thread_current() ;*/

	// Get the page number with the offset set to 0
	void *upage = pg_round_down(addr) ;
	/*printf ( "UPAGE is %p\n", upage ) ;*/

	// Find in the supplymentary page table
	/*printf ( "before lookup\n");*/
	/*printPageTable();*/
	struct page *p = page_lookup ( upage ) ;
	/*printf ( "After lookup\n");*/
	if ( p == NULL )
	{
		/*PANIC("Request for a page not present in the supplymentary page table\n") ;*/
		/*printf ( "Return false\n");*/
		return false ;
	}

	ASSERT( p->addr == upage ) ;

	// Page is in swap space
	if ( p->swap != NULL )
	{
		lock_acquire(&frame) ;
		load_swap_slot(p,thread_current());
		lock_release(&frame) ;
		return true ;
	}

	/* Get a page of memory. */
	/*uint8_t *kpage = palloc_get_page (PAL_USER);*/
	lock_acquire(&frame) ;
	struct frame *f = frame_allocate() ;
	lock_release(&frame) ;

	/*printf ( "Return from frame_allocate\n");*/
	f->p = p ;
	void *kpage = f->kpage ;

	if (kpage == NULL)
	{
		printf ( "NO MORE FRAMES. Palloc failed\n" ) ;
		return false;
	}
	/*printf ( "Address of the kernel page allocated: %p\n", kpage ) ;*/

	p->kpage = kpage ;

	file_seek(p->file, p->ofs) ;
	size_t zero_bytes = PGSIZE - p->read_bytes ;

	/*printf ( "file read of %s\n", thread_current()->name) ;*/
	/*printf ( "File size is %u and read bytes is %d\n", file_length(p->file),p->read_bytes);*/
	/*printf ( "Addr in file * is %p\n addr of addr %p\n offset is %u\nRead bytes is %d\nwritable is %d\n", p->file, p->addr, p->ofs, p->read_bytes, p->writable);*/
	/* Load this page. */

	lock_acquire(&file_lock);
	off_t old_ofs = file_tell(p->file) ;
	off_t read = file_read(p->file, p->kpage, p->read_bytes) ;
	file_seek(p->file,old_ofs);
	lock_release(&file_lock);


	/*printf ( "output from file read is %u\n", read) ;*/
	if (read != (int) p->read_bytes)
	{
		printf ( "FILE READ FAILED\n" ) ;
		palloc_free_page (kpage);
		return false;
	}
	memset (kpage + p->read_bytes, 0, zero_bytes);

	/*printf ( "install page: WRITABLE is %d\n", p->writable ) ;*/
	/* Add the page to the process's address space. */
	if (!install_page (p->addr, kpage, p->writable))
	{
		palloc_free_page (kpage);
		return false;
	}
	p->kpage = kpage ;
	/*printf ( "Kpage address is %p\n", p->kpage) ;*/

	return true ;
}

bool grow_stack ( void *addr )
{
	/*printf ( "stack growth at %p\n", addr ) ;*/
	void *upage = pg_round_down(addr) ;

	struct page *page = page_lookup(upage) ;
	if ( page != NULL )
	{
		if ( page->swap != NULL )
		{
			/*printf ( "Loading stack in swap\n");*/
			lock_acquire(&frame);
			load_swap_slot(page,thread_current());
			lock_release(&frame);
			return true ;
		}
	}

	/*printf ( "Not null\n");*/
	lock_acquire(&frame) ;
	struct frame *f = frame_allocate() ;
	lock_release(&frame) ;

	void *kpage = f->kpage ;

	memset(kpage, 0, PGSIZE) ;

	struct page *p = (struct page*) malloc (sizeof(struct page)) ;
	p->file = NULL ;
	p->addr = upage ;
	p->kpage = kpage ;
	p->ofs = -1 ;
	p->read_bytes = -1 ;
	p->writable = true ;
	p->stack = true ;

	p->swap = NULL ;

	f->p = p ;

	page_insert ( &thread_current()->pages, &p->hash_elem ) ;

	install_page ( p->addr, p->kpage, true ) ;

	return true ;
}

// Remove the page from the supplymentary page table
// IMPORTANT: This is called only inside EXIT when the process is exiting
// This function shouldn't be called explicitely
/*void page_deallocate ( void *upage)*/
void page_deallocate ( struct hash_elem *e, void *aux)
{
	/*printf ( "Inside page page_deallocate\n");*/
	/*struct page *p = page_lookup(upage) ;*/
	struct page *p = hash_entry ( e, struct page, hash_elem) ;
	if ( p == NULL )
	{
		PANIC("Trying to deallocate the page not present");
		return ;
	} 

	void *kpage = p->kpage ;
	if ( kpage == NULL )
	{
		/*PANIC("Deallocation page which has kpage == NULL\n");*/
		if ( (int)aux != 1 )
			free(p) ;
		return ;
	}

	/*printf ( "Frame deallocate: uaddr: %p kpage: %p\n", p->addr, p->kpage) ;*/

	lock_acquire(&frame);
	frame_deallocate(kpage) ;
	lock_release(&frame);

	/*hash_delete (&thread_current()->pages,&p->hash_elem) ;*/

	/*printf ( "Before free in deallocate\n") ;*/
	/*printPageTable();*/

	pagedir_clear_page(thread_current()->pagedir, p->addr) ;

	if ( (int)aux != 1 )
		free(p) ;

	/*printf ( "after free in deallocate\n") ;*/
	/*printPageTable();*/
	return ;
}

void printPageTable(void)
{
	struct hash_iterator i;

	hash_first (&i, &thread_current()->pages);
	while (hash_next (&i))
	{
		struct page *p = hash_entry (hash_cur(&i), struct page, hash_elem);
		/*struct foo *f = hash_entry (hash_cur (&i), struct foo, elem);*/
		printf ( "Upage: %p kpage: %p\n", p->addr, p->kpage) ;
	}
	return ;
}
