#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <hash.h>
#include <debug.h>
#include "threads/thread.h"
#include "threads/vaddr.h"

// Supplymentary page table
struct page
{
  struct hash_elem hash_elem ;		/* Hash table element. */
  struct file *file ;

  void *addr ;						/* Virtual address. */
  void *kpage ;						/* Physical address */
  int32_t ofs ;						/* Offset within the executable */
  size_t read_bytes ;				/* Size of bytes to be read */
  bool writable ;					/* Writable or Read-Only */

  bool stack ;

  struct swap_slot *swap ;
} ;

// Initialize the supplymentary hash table
bool page_init (struct hash *pages) ;

/* Returns a hash value for page p. */
unsigned page_hash (const struct hash_elem *p_, void *aux UNUSED) ;

/* Returns true if page a precedes page b. */
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) ;

/* Returns the page containing the given virtual address, or a null pointer if no such page exists. */
struct page * page_lookup (void *address) ;

// Insert an element into the supplymentary hash table
struct hash_elem * page_insert ( struct hash *pages, struct hash_elem *new ) ;

// Allocate a frame to the faluting address
//bool page_allocate ( void *addr ) ;

// Load the page from the executable containing the virtual address ADDR
bool get_page ( void *addr ) ;

// Allocate extra page for the stack
bool grow_stack(void *addr) ;

// Remove the entry from the supplymentary page table
void page_deallocate ( struct hash_elem *h, void *aux ) ;

void printPageTable (void) ;

#endif
