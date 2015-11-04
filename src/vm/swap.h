#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"
#include "vm/page.h"

// List of all the swap slots present in the swap block device
struct list swap_slots ;

// Initialize the swap block device
void swap_init(void) ;

// Swap out the page P corresponding to the thread T
void swap_page ( struct page *p, struct thread *t) ;

// Load the page P of thread T back to the memory
void load_swap_slot ( struct page *p, struct thread *t ) ;

// Remove all the swap slots occupied by the thread T
void invalidate_swap_slots( struct thread *t ) ;

#endif
