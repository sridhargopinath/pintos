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

// Function to allocate a frame to the given address
// Return FALSE if it is a bad address
bool get_page( void *addr )
{
	struct thread *cur = thread_current() ;

	// Get the page number with the offset set to 0
	void *upage = pg_round_down(addr) ;

	// Find in the supplymentary page table
	struct page *p = page_lookup ( upage ) ;
	if ( p == NULL )
		return false ;

	// Check if the page is in swap space
	if ( p->swap != NULL )
	{
		lock_acquire(&frame) ;
		load_swap_slot(p,thread_current());
		lock_release(&frame) ;
		
		return true ;
	}

	// Get a page of memory
	lock_acquire(&frame) ;
	struct frame *f = frame_allocate() ;
	lock_release(&frame) ;

	f->p = p ;

	p->kpage = f->kpage ;

	void *kpage = f->kpage ;

	lock_acquire(&file_lock);	
	// Store the old offset of the file
	off_t old_ofs = file_tell(p->file) ;

	// Read from the file at the particular offset
	file_seek(p->file, p->ofs) ;
	file_read(p->file, p->kpage, p->read_bytes) ;

	// Put the file pointer back to the old offset
	file_seek(p->file,old_ofs);
	lock_release(&file_lock);

	// Zero the remaining bytes, if any, in the page
	size_t zero_bytes = PGSIZE - p->read_bytes ;
	memset (kpage + p->read_bytes, 0, zero_bytes);


	/* Add the page to the process's address space. */
	bool success = pagedir_set_page( cur->pagedir, p->addr, kpage, p->writable) ;
	if (!success)
		PANIC("get_page: pagedir_set_page returned false");

	return true ;
}

// Function to satisfy the stack request at ADDR by allocating a new page
bool grow_stack ( void *addr )
{
	struct thread *cur = thread_current() ;

	// Get the page number with the offset set to 0
	void *upage = pg_round_down(addr) ;

	// Index the supplymentary page table
	struct page *page = page_lookup(upage) ;
	
	// Check if the page is present in the swap. If so, copy to memory
	if ( page != NULL )
	{
		if ( page->swap != NULL )
		{
			lock_acquire(&frame);
			load_swap_slot(page,thread_current());
			lock_release(&frame);
			
			return true ;
		}
	}

	// Get a new frame
	lock_acquire(&frame) ;
	struct frame *f = frame_allocate() ;
	lock_release(&frame) ;
	
	memset(f->kpage, 0, PGSIZE) ;
	
	void *kpage = f->kpage ;

	// Get a new supplymentary page table entry
	struct page *p = (struct page*) malloc (sizeof(struct page)) ;
	if ( p == NULL )
		PANIC("grow_stack: Failed to allocate memory to page table");
	
	p->file = NULL ;
	p->addr = upage ;
	p->kpage = kpage ;
	p->ofs = -1 ;
	p->read_bytes = -1 ;
	p->writable = true ;
	p->stack = true ;

	p->swap = NULL ;

	// Update frame table entry
	f->p = p ;

	// Insert it into the hash table
	page_insert ( &thread_current()->pages, &p->hash_elem ) ;

	bool success = pagedir_set_page( cur->pagedir, p->addr, p->kpage, true) ;
	if (!success)
		PANIC("grow_stack: pagedir_set_page returned false");

	return true ;
}

// Remove the page from the supplymentary page table
// IMPORTANT: This is called from two places:
// munmap: When unmapping a mapped memory. AUX will be 1 during this call. Do NOT free memory as it will be done by the caller along with deleting the entry in the hash
// exit: hash_destroy will call this function on all the entries in the hash i.e supplymentary page table. AUX will be 0 during this call. FREE memory in this case. Each hash element will be deleted by the caller.
void page_deallocate ( struct hash_elem *e, void *aux)
{
	struct page *p = hash_entry ( e, struct page, hash_elem) ;
	if ( p == NULL )
		PANIC("page_deallocate: Trying to deallocate the page not present");

	// If no frame table is allocated, just free page table
	void *kpage = p->kpage ;
	if ( kpage == NULL )
	{
		if ( (int)aux != 1 )
			free(p) ;
		return ;
	}

	// Deallocate the frame assigned to this page
	lock_acquire(&frame);
	frame_deallocate(kpage) ;
	lock_release(&frame);

	pagedir_clear_page(thread_current()->pagedir, p->addr) ;

	if ( (int)aux != 1 )
		free(p) ;

	return ;
}
