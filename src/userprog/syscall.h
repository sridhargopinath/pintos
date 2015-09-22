#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

// Synchronization tools used in EXEC system call
struct condition exec_cond ;
struct lock exec_lock ;

// Lock for file system. Only 1 process inside filesys code
struct lock file_lock ;

void syscall_init (void);

void exit ( int status ) ;

#endif /* userprog/syscall.h */
