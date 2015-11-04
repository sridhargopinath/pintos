#include <stdio.h>
#include "vm/swap.h"
#include "devices/block.h"
#include <list.h>
#include <bitmap.h>
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"


struct swap_slot
{
	struct list_elem elem ;

	block_sector_t pos ;
	struct page *p ;
} ;

struct bitmap *bitmap ;

struct block *block ;


void swap_init(void)
{
	block = block_get_role(BLOCK_SWAP);

	list_init(&swap_slots) ;

	block_sector_t size = block_size(block) ;

	bitmap = bitmap_create(size) ;
	if ( bitmap == NULL )
		PANIC("Not able to create BITMAP\n");

	/*printf ( "Size of swap block is %d\n", size ) ;*/

	return ;
}

void swap_page ( struct page *p, struct thread *t )
{
	/*printf ( "Enter swap page\n");*/
	ASSERT(p!=NULL);

	int i ;

	struct swap_slot *slot = (struct swap_slot *)malloc(sizeof(struct swap_slot));
	if ( slot == NULL )
		PANIC("Couldn't allocate memory for swap_slot\n");
	slot->p = p ;

	size_t pos = bitmap_scan_and_flip(bitmap,0,PGSIZE/BLOCK_SECTOR_SIZE,false);
	if ( pos == BITMAP_ERROR )
		PANIC ( "Not able to get a swap slot from bitmap\n");

	slot->pos = pos ;
	list_push_back(&swap_slots,&slot->elem);

	void *addr = p->kpage ;
	for ( i = 0 ; i < PGSIZE/BLOCK_SECTOR_SIZE ; i ++ )
	{
		block_write(block,pos+i,addr);
		addr += BLOCK_SECTOR_SIZE ;
	}

	p->swap = slot ;
	p->kpage = NULL ;

	pagedir_clear_page(t->pagedir,p->addr);

	return ;
}

void load_swap_slot(struct page *p, struct thread *t)
{

	struct frame *f = frame_allocate() ;

	struct swap_slot *slot = p->swap ;

	p->swap = NULL ;
	p->kpage = f->kpage ;

	f->p = p ;
	f->t = t ;

	void *addr = p->kpage ;
	block_sector_t pos = slot->pos ;
	int i ;
	for ( i = 0 ; i < PGSIZE/BLOCK_SECTOR_SIZE ; i ++ )
	{
		block_read(block,pos+i,addr);
		addr += BLOCK_SECTOR_SIZE ;
	}


	pagedir_set_page ( t->pagedir, p->addr, p->kpage, p->writable ) ;
	pagedir_set_dirty ( t->pagedir, p->addr, true ) ;

	/*bitmap_reset(bitmap, slot->pos);*/
	bitmap_set_multiple(bitmap, slot->pos, PGSIZE/BLOCK_SECTOR_SIZE, false ) ;

	list_remove(&slot->elem) ;
	free(slot) ;

	return ;
}

void invalidate_swap_slots ( struct thread *cur)
{
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

		/*bitmap_reset ( bitmap, slot->pos ) ;*/
		bitmap_set_multiple(bitmap, slot->pos, PGSIZE/BLOCK_SECTOR_SIZE, false ) ;

		list_remove ( &slot->elem );
		free(slot);
	}

	return ;
}
