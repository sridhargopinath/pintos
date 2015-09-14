#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "userprog/pagedir.h"
#include "threads/vaddr.h"

// Typedef used for process IDs
typedef int pid_t ;

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  // Initialize the condtional lock used by EXEC
  cond_init(&exec_cond) ;
  lock_init(&exec_lock) ;
}

// Array to store the number of arguments required for each system call
static int argsNum[] = {
	0,			// SYS_HALT
	1,			// SYS_EXIT
	1,			// SYS_EXEC
	1,			// SYS_WAIT
	2,			// SYS_CREATE
	1,			// SYS_REMOVE
	1,			// SYS_OPEN
	1,			// SYS_FILESIZE
	3,			// SYS_READ
	3,			// SYS_WRITE
	2,			// SYS_SEEK
	1,			// SYS_TELL
	1,			// SYS_CLOSE
} ;

static void halt (void)
{
	shutdown_power_off() ;
	return ;
}

static void exit ( int status )
{
	struct thread *cur = thread_current() ;

	printf ( "%s: exit(%d)\n",cur->name, status ) ;

	// Clear the process info objects created as well.

	// Store the exit status of the current thread in the process_info structure
	cur->info->exit_status = status ;
	sema_up(&cur->info->sema) ;

	thread_exit() ;
}

static pid_t exec ( const char *cmd_line )
{
	enum intr_level old_level = intr_disable() ;

	pid_t pid = process_execute(cmd_line) ;

	// Was not able to create a thread for the new process
	if ( pid == TID_ERROR)
		goto done1 ;
		//return -1 ;

	// Check if the new thread was able to load successfully
	// This uses the Condition variables. Check its usage online
	lock_acquire(&exec_lock) ;

	// Get the INFO structure of the thread which was just created
	struct thread *cur = thread_current() ;
	struct list_elem *e ;
	bool found = false ;
	struct process_info *info ;
	for ( e = list_begin(&cur->children) ; e != list_end(&cur->children) ; e = list_next(e) )
	{
		info = list_entry ( e, struct process_info, elem ) ;
		if ( info->tid == pid )
		{
			found = true ;
			break ;
		}
	}
	if ( found == false )
		goto done1 ;
		/*return -1 ;*/
	//printf ( "Reached condwait in exec\n" ) ;

	while ( info->status == PROCESS_STARTING )
		cond_wait ( &exec_cond, &exec_lock ) ;

	printf ( "Passed condwait in exec with status error? %d and pid %d\n", info->status==PROCESS_ERROR, pid ) ;
	lock_release(&exec_lock) ;

	if ( info->status == PROCESS_ERROR )
	{
		printf ( "Inside error returning -1 value\n" ) ;
		goto done1 ;
		/*return -1 ;*/
	}

	printf ( "EXEC return value: %d\n", pid ) ;
	intr_set_level(old_level) ;
	return pid ;

done1:
	intr_set_level ( old_level ) ;
	return -1 ;

}

static int wait ( pid_t pid )
{
	//printf ( "reached wait syscall\n" ) ;
	return process_wait ( pid ) ;
}

static bool create ( const char *file, unsigned initial_size )
{
	return true ;
}

static bool remove ( const char *file )
{
	return false ;
}

static int open ( const char *file )
{
	return 0 ;
}

static int filesize ( int fd )
{
	return 0 ;
}

static int read ( int fd, void *buffer, unsigned size )
{
	return 0 ;
}

static int write ( int fd, void *buffer, unsigned size )
{
	if ( fd == 1 )
		putbuf ( buffer, size ) ;
	
	return size ;
}

static void seek ( int fd, unsigned position )
{
	return ;
}

static unsigned tell ( int fd )
{
	return 0 ;
}

static void close ( int fd )
{
	return ;
}

static void *convertAddr ( void *addr) 
{
	if ( !is_user_vaddr(addr) )
		exit(-1) ;

	void *x = pagedir_get_page(thread_current()->pagedir, addr) ;
	if ( x == NULL )
		exit(-1) ;
	return x ;
}

// The system call number is present as the first entry on top of the stack
// Above that, the arguments for the specific system call are present as 4 byte (32 bit) addresses
// All the addresses are virtually and should be converted to physical addresses before use
static void syscall_handler (struct intr_frame *f) 
{
  // Get the syscall number from the stack pointer
  uint8_t sysNum  = *(uint8_t *)convertAddr(f->esp) ;

  //printf("Syscall number: %d\n", sysNum) ;
  
  // Number of arguments required for this system call
  int n = argsNum[sysNum] ;

  // Get N arguments from the stack and store their values as void pointers in an array
  void *pargs[n] ;
  int i ;
  for ( i = 0 ; i < n ; i ++ )
  {
	  uint32_t *vaddr = (uint32_t *)f->esp + i + 1 ;
	  uint32_t *paddr = convertAddr(vaddr) ;
	  pargs[i] = *(void **)paddr ;
  }

  switch ( sysNum )
  {
	  case SYS_HALT:		halt () ;
							break ;

	  case SYS_EXIT:		exit ( (int)pargs[0] ) ;
							break ;

	  case SYS_EXEC:		exec ( (char*)pargs[0] ) ;
							break ;

	  case SYS_WAIT:		wait ( (pid_t)pargs[0] ) ;
							break ;

	  case SYS_CREATE:		create ( (char*)pargs[0], (unsigned)pargs[1] ) ;
							break ;

	  case SYS_REMOVE:		remove ( (char*)pargs[0] ) ;
							break ;

	  case SYS_OPEN:		open ( (char*)pargs[0] ) ;
							break ;

	  case SYS_FILESIZE:	filesize ( (int)pargs[0] ) ;
							break ;

	  case SYS_READ:		read ( (int)pargs[0], (void *)pargs[1], (unsigned)pargs[2] ) ;
							break ;

	  case SYS_WRITE:		f->eax = write ( (int)pargs[0], (void *)pargs[1], (unsigned)pargs[2]) ;
							break ;

	  case SYS_SEEK:		seek ( (int)pargs[0], (unsigned)pargs[1] ) ;
							break ;

	  case SYS_TELL:		tell ( (int)pargs[0] ) ;
							break ;

	  case SYS_CLOSE:		close ( (int)pargs[0] ) ;
							break ;

	  default:				printf ( "Invalid Handler\n\n\n") ;
							break ;
  }
}
