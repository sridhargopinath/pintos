#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

// Synchronization tools used in EXEC system call in start_process of process.c
struct condition exec_cond ;
struct lock exec_lock ;

// Lock for file system. Only 1 process inside filesys code. Is also used in process.c
struct lock file_lock ;

// Used in exception.c to exit a process when a page fault occurs
void exit ( int status ) ;

void syscall_init (void);

#endif /* userprog/syscall.h */
