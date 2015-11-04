#include <stdio.h>
#include "vm/swap.h"
#include "devices/block.h"
#include <list.h>
#include <bitmap.h>
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"

// SWAP Block device pointer
struct block *block ;

// Structure to keep info about each swap slot
struct swap_slot
{
	struct list_elem elem ;

	block_sector_t pos ;
	struct page *p ;
} ;

// Bitmap for the swap space to keep track of the free and empty swap slots
struct bitmap *bitmap ;

// Initialize the swap space
void swap_init(void)
{
	block = block_get_role(BLOCK_SWAP);
	list_init(&swap_slots) ;

	block_sector_t size = block_size(block) ;
	bitmap = bitmap_create(size) ;
	if ( bitmap == NULL )
		PANIC("Not able to create BITMAP\n");

	return ;
}

// Move the page P of thread T to the swap space
// NOTE: This function is called while holding the FRAME lock
void swap_page ( struct page *p, struct thread *t )
{
	ASSERT ( p != NULL ) ;

	// Get a new swap slot
	struct swap_slot *slot = (struct swap_slot *)malloc(sizeof(struct swap_slot));
	if ( slot == NULL )
		PANIC("Couldn't allocate memory for swap_slot\n");

	// Get the position of the free space in the swap slot where this page can be moved
	size_t pos = bitmap_scan_and_flip(bitmap,0,PGSIZE/BLOCK_SECTOR_SIZE,false);
	if ( pos == BITMAP_ERROR )
		PANIC ( "Not able to get a swap slot from bitmap\n");

	slot->p = p ;
	slot->pos = pos ;
	list_push_back(&swap_slots,&slot->elem);

	void *addr = p->kpage ;
	int i ;
	// Write the page to the swap
	for ( i = 0 ; i < PGSIZE/BLOCK_SECTOR_SIZE ; i ++ )
	{
		block_write(block,pos+i,addr);
		addr += BLOCK_SECTOR_SIZE ;
	}

	p->swap = slot ;
	p->kpage = NULL ;

	// Clear the entry in the threads' page directory and also set the dirty bit to 0 now
	pagedir_clear_page(t->pagedir,p->addr);
	pagedir_set_dirty(t->pagedir,p->addr,false) ;

	return ;
}

// Function to load a page back from the swap to the memory
// NOTE: This function will be called while holding the FRAME lock
void load_swap_slot(struct page *p, struct thread *t)
{
	// Get a new free frame in the memory
	struct frame *f = frame_allocate() ;
	f->p = p ;

	struct swap_slot *slot = p->swap ;

	p->swap = NULL ;
	p->kpage = f->kpage ;

	void *addr = p->kpage ;
	block_sector_t pos = slot->pos ;
	int i ;
	// Read the page back to the memory
	for ( i = 0 ; i < PGSIZE/BLOCK_SECTOR_SIZE ; i ++ )
	{
		block_read(block,pos+i,addr);
		addr += BLOCK_SECTOR_SIZE ;
	}

	// Mark this page as available in the page directory of the current thread
	// Set this page as dirty again since it was in swap because it was dirty
	pagedir_set_page ( t->pagedir, p->addr, p->kpage, p->writable ) ;
	pagedir_set_dirty ( t->pagedir, p->addr, true ) ;

	// Mark the positions in the BITMAP in the swap device as free
	bitmap_set_multiple(bitmap, slot->pos, PGSIZE/BLOCK_SECTOR_SIZE, false ) ;

	// Remove the swap slot from the list and free memory
	list_remove(&slot->elem) ;
	free(slot) ;

	return ;
}

// For all the pages of the thread CUR present in the swap slot, this function will invalidate them
// NOTE: This function is called while holding the FRAME lock
void invalidate_swap_slots ( struct thread *cur)
{
	// Iterate over the supplymentary page table
	struct hash *h = &cur->pages ;
	struct hash_iterator i ;
	hash_first(&i,h);
	while ( hash_next(&i) )
	{
		struct page *p = hash_entry(hash_cur(&i),struct page, hash_elem);
		if ( p->swap == NULL )
			continue ;

		struct swap_slot *slot = p->swap ;
		p->swap = NULL ;

		// Reset the bits in the BITMAP representing the free sectors in the swap slot
		bitmap_set_multiple(bitmap, slot->pos, PGSIZE/BLOCK_SECTOR_SIZE, false ) ;

		list_remove ( &slot->elem );
		free(slot);
	}

	return ;
}
