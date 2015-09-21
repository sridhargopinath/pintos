#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/synch.h"

// Synchronization tools used in EXEC system call
struct condition exec_cond ;
struct lock exec_lock ;

void syscall_init (void);

void exit ( int status ) ;

void *convertAddr ( const void *addr ) ;

#endif /* userprog/syscall.h */
