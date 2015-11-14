#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"

#include "threads/synch.h"
#include "filesys/off_t.h"

/* Maximum length of a file name component.
   This is the traditional UNIX maximum length.
   After directories are implemented, this maximum length may be
   retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

struct inode;

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
	struct lock lock ;
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */

	bool isdir ;
  };

/* Opening and closing directories. */
bool dir_create (block_sector_t sector, size_t entry_cnt, struct dir *parent);
struct dir *dir_open (struct inode *);
struct dir *dir_open_root (void);
struct dir *dir_reopen (struct dir *);
void dir_close (struct dir *);
struct inode *dir_get_inode (struct dir *);

/* Reading and writing. */
bool dir_lookup (const struct dir *, const char *name, struct inode **);
bool dir_add (struct dir *, const char *name, block_sector_t, bool isdir);
bool dir_remove (struct dir *, const char *name);
bool dir_readdir (struct dir *, char name[NAME_MAX + 1]);
bool dir_readdir_without_dot (struct dir *, char name[NAME_MAX + 1]);

bool lookup ( const struct dir *dir, const char *name, struct dir_entry *ep, off_t *ofsp ) ;

int dir_size ( const struct dir *dir ) ;
bool dir_mkdir ( char *path ) ;
bool verify_path ( char *path, struct dir **dir, char **file_name, bool only_open ) ;

#endif /* filesys/directory.h */
