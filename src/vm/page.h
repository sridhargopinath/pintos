#include <hash.h>
#include <debug.h>
#include "threads/thread.h"

// Supplymentary page table
struct page
{
  struct hash_elem hash_elem; /* Hash table element. */
  void *addr;                 /* Virtual address. */
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
