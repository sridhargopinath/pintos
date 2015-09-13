#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "userprog/pagedir.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

static int write ( int fd, void *buffer, unsigned size ) ;
static void * translate ( void *addr) ;
static void syscall_exit ( int status ) ;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

void * translate ( void *addr) 
{
	if ( !is_user_vaddr(addr) )
	{
		//printf ( "Translate failed\n" ) ;
		return NULL ;
	}
	return pagedir_get_page(thread_current()->pagedir, addr) ;
}

static void
syscall_handler (struct intr_frame *f) 
{
  void *esp  = translate(f->esp) ;
  uint8_t num = *(uint8_t *)esp ;
  //printf("Syscall number: %d\n", num) ;
  int fd, status ;
  void *buffer ;
  unsigned size ;
  switch ( num )
  {
	  case SYS_WRITE: fd = *(int *)(translate(((uint32_t *)f->esp)+1)) ;
					  //printf ( "Write called: %d\n", fd ) ;
					  buffer = *(void **)(translate(((uint32_t *)f->esp)+2)) ;
					  size = *(unsigned *)(translate(((uint32_t *)f->esp)+3)) ;
					  f->eax = write (fd, buffer, size) ;
					  break ;
	  case SYS_EXIT: status = *(int *)(translate(((uint32_t *)f->esp)+1)) ;
					 //printf ( "Exit called\n" ) ;
					 syscall_exit ( status ) ;


	  default:  printf ( "other handler\n\n\n") ;
				break ;

  }
}

void syscall_exit ( int status )
{
	struct thread *cur = thread_current() ;

	printf ( "%s: exit(%d)\n",cur->name, status ) ;

	// Clear the process info objects created as well.

	// Store the exit status of the current thread in the process_info structure
	cur->info->exit_status = status ;
	sema_up(&cur->info->sema) ;

	thread_exit() ;
}

int write ( int fd, void *buffer, unsigned size )
{
	if ( fd == 1 )
		putbuf ( buffer, size ) ;
	
	return size ;
}
