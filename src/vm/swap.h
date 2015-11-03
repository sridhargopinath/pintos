#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include "vm/page.h"


struct list swap_slots ;

void swap_init(void) ;

void swap_page ( struct page *p, struct thread *t) ;

void load_swap_slot ( struct page *p, struct thread *t ) ;

void invalidate_swap_slots( struct thread *t ) ;

#endif
