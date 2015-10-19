#include "vm/page.h"


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
