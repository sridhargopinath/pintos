#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

#include "threads/thread.h"
#include "threads/interrupt.h"
#include "filesys/free-map.h"

/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt, struct dir *parent)
{
  bool success = inode_create ( sector, entry_cnt * sizeof (struct dir_entry), true );
  if ( success == false )
	  return success ;

  struct dir *dir = dir_open(inode_open(sector)) ;

  dir_add ( dir, ".", sector, true ) ;

  if ( sector == ROOT_DIR_SECTOR )
	  dir_add ( dir, "..", ROOT_DIR_SECTOR, true ) ;
  else
	  dir_add ( dir, "..", dir_get_inode(parent)->sector, true ) ;

  dir_close(dir) ;

  return true ;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = 0;
	  lock_init(&dir->lock) ;
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      inode_close (dir->inode);
      free (dir);
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   otherwise, returns false and ignores EP and OFSP. */
bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  /*printf ( "Lookup failed\n");*/
  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  if (lookup (dir, name, &e, NULL))
  {
	  /*printf ( "here\n");*/
    *inode = inode_open (e.inode_sector);
  }
  else
  {
	  /*printf ( "Became null\n");*/
    *inode = NULL;
  }

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector, bool isdir)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX)
    return false;

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL))
    goto done;

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  e.isdir = isdir ;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:
  return success;
}

static bool dir_remove_dir ( struct inode *inode )
{
	if ( inode->data.isdir == false )
		return false ;

	struct dir *dir = dir_open(inode) ;

	lock_acquire(&dir->lock) ;

	int size = dir_size(dir) ;
	if ( size != 2 )
	{
		lock_release(&dir->lock) ;
		dir_close(dir) ;
		return false ;
	}

	bool success = dir_remove ( dir, "." ) && dir_remove ( dir, "..") ;

	// Iterate over all threads and set the CURDIR to -1 if it is equal to DIR
	if ( success == true )
	{
		enum intr_level old_level = intr_disable();

		struct list_elem *e ;
		for ( e = list_begin(&all_list) ; e != list_end(&all_list) ; e = list_next(e) )
		{
			struct thread *t = list_entry ( e, struct thread, allelem) ;
			if ( t->curdir == inode_get_inumber(inode) )
				t->curdir = 0 ;
		}

		intr_set_level(old_level) ;
	}

	lock_release(&dir->lock) ;
	dir_close(dir) ;
	
	return success ;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  if ( e.isdir == true )
  {
	  if ( dir_remove_dir(inode) == false )
		  goto done;
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}

bool
dir_readdir_without_dot (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
			if ( strcmp(e.name,".") == 0 || strcmp(e.name,"..") == 0 )
				continue ;

			strlcpy (name, e.name, NAME_MAX + 1) ;
			return true;
        } 
    }
  return false;
}

bool dir_mkdir ( char *path )
{
	struct dir *dir ;
	char *name ;
	bool success ;

	// Verify the path given and return the directory where we should create a file
	success = verify_path ( path, &dir, &name, false ) ;
	/*printf ( " Name in mkdir: %s\n", name ) ;*/

	if ( success == false )
		return false ;

	// The directory where NAME should be created is DIR. The lock for DIR is already acquired

	block_sector_t inode_sector = 0 ;

	if ( free_map_allocate ( 1, &inode_sector ) == false )
	{
		lock_release(&dir->lock) ;
		dir_close(dir) ;
		return false ;
	}

	if ( dir_create(inode_sector, 16, dir) == false )
	{
		free_map_release(inode_sector, 1) ;
		lock_release(&dir->lock) ;
		dir_close(dir) ;
		return false ;
	}

	if ( dir_add ( dir, name, inode_sector, true ) == false )
	{
		// Steps to remove an inode. TODO
		struct inode *inode = inode_open(inode_sector) ;
		inode_remove(inode) ;
		inode_close(inode) ;

		/*free_map_release(inode_sector, 1) ;*/
		lock_release(&dir->lock) ;
		dir_close(dir) ;
		return false ;
	}

	lock_release(&dir->lock) ;
	dir_close(dir) ;

	return true ;
}

// Given PATH, it will store to DIR the end directory and to FILE_NAME the last name in PATH
// NOTE: Caller must close the DIR and also release the LOCK on DIR
bool verify_path ( char *path, struct dir **dir, char **file_name, bool open_file )
{
	struct dir *curdir ;
	struct thread *cur = thread_current() ;
	struct dir_entry e ;

	if ( strlen(path) == 0 )
		return false ;

	if ( path[0] == '/' )
		curdir = dir_open_root() ;
	else
	{
		// If the current directory has been deleted
		if ( cur->curdir == 0 )
			return false ;

		curdir = dir_open(inode_open(cur->curdir)) ;
	}

	lock_acquire ( &curdir->lock) ;

	char *par, *child ;
	char *save_ptr ;
	bool success = true ;

	par = strtok_r ( path, "/", &save_ptr ) ;
	if ( par == NULL )
	{
		*dir = curdir ;
		printf ( "Here\n") ;
		/*(*file_name)[0] = '/' ;*/
		/*(*file_name)[1] = '\0' ;*/
		path[1] = '\0' ;
		*file_name = path ;
		/*strlcpy(*file_name, path, 2);*/
		printf ( "Here\n") ;
		printf ( "inumber in verify is: %d\n", inode_get_inumber(dir_get_inode(curdir))) ;
		return true ;
	}

	child = strtok_r ( NULL, "/", &save_ptr ) ;
	while ( 1 )
	{
		// Reached the end of tokenizing
		if ( child == NULL )
		{
			// If OPEN_FILE is TURE and lookup fails, then FAILURE since file is not present
			// If CREATE_FILE i.e open_file is false and lookup is TRUE, then FAILURE since directory already present
			// Hence should check for lookup != open_file for failure
			if ( lookup(curdir, par, NULL, NULL ) != open_file )
			{
				success = false ;
				break ;
			}

			*dir = curdir ;
			*file_name = par ;
			break ;
		}

		// Check if the parent is present in the current directory
		if ( lookup(curdir, par, &e, NULL ) == false )
		{
			success = false ;
			break ;
		}

		// Check if parent ia a directory
		if ( e.isdir == false )
		{
			success = false ;
			break ;
		}

		// Move forward
		lock_release(&curdir->lock) ;
		dir_close(curdir);

		curdir = dir_open(inode_open(e.inode_sector)) ;
		lock_acquire(&curdir->lock) ;

		par = child ;
		child = strtok_r ( NULL, "/", &save_ptr ) ;
	}

	if ( success == false )
	{
		lock_release(&curdir->lock);
		dir_close(curdir) ;
	}

	return success ;
}

int dir_size ( const struct dir *dir )
{
	struct dir_entry e;
	size_t ofs;

	ASSERT (dir != NULL);

	int size = 0 ;
	for ( ofs = 0 ; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e ; ofs += sizeof e )
	{
		if ( e.in_use )
			size ++ ;
	}

	return size ;
}
