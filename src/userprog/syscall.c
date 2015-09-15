#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include <string.h>
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "threads/malloc.h"

// Typedef used for process IDs
typedef int pid_t ;

struct file_info
{
	int fd ;
	struct file *file ;
	struct list_elem elem ;
	bool closed ;
} ;

struct lock file_lock ;
struct lock fd_lock ;

static void syscall_handler (struct intr_frame *);

static void halt (void) ;
/*static void exit ( int status ) ;*/
static pid_t exec ( const char *cmd_line ) ;
static int wait ( pid_t pid ) ;
static bool create ( const char *file, unsigned initial_size ) ;
static bool remove ( const char *file ) ;
static int open ( const char *file ) ;
static int filesize ( int fd ) ;
static int read ( int fd, void *buffer, unsigned size ) ;
static int write ( int fd, void *buffer, unsigned size ) ;
static void seek ( int fd, unsigned position ) ;
static unsigned tell ( int fd ) ;
static void close ( int fd ) ;

static void *convertAddr ( const void *addr) ;
static int allocateFD (void) ;
static struct file_info *get_file_info ( int fd ) ;


void syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  // Initialize lock used for accessing filesys
  lock_init(&file_lock) ;

  // Initialize the condtional lock used by EXEC
  cond_init(&exec_cond) ;
  lock_init(&exec_lock) ;
  lock_init(&fd_lock) ;
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

// Exit the OS by just calling the shutdown function
void halt (void)
{
	shutdown_power_off() ;
	return ;
}

// Print the message and exit the thread
void exit ( int status )
{
	struct thread *cur = thread_current() ;

	printf ( "%s: exit(%d)\n", cur->name, status ) ;

	// Clear the process info objects created as well.
	// Clear the file_info objects

	// TO implement WAIT syscall
	// Store the exit status of the current thread in the process_info structure
	// Signal the semaphore to wake up the parent if the parent thread is waiting on it
	cur->info->exit_status = status ;
	sema_up(&cur->info->sema) ;

	// process_exit will be called inside this function
	thread_exit() ;
}

// Create a new process using process_execute command and execute the command given
pid_t exec ( const char *cmd_line )
{
	/*enum intr_level old_level = intr_disable() ;*/
	//printf ( "Enter the exec call\n" ) ;
	convertAddr(cmd_line) ;

	pid_t pid = process_execute(cmd_line) ;

	// Was not able to create a thread for the new process
	if ( pid == TID_ERROR)
		/*goto done1 ;*/
		return -1 ;

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
		/*goto done1 ;*/
		return -1 ;
	/*printf ( "Reached condwait in exec\n" ) ;*/

	while ( info->status == PROCESS_STARTING )
		cond_wait ( &exec_cond, &exec_lock ) ;

	/*printf ( "Passed condwait in exec with status error? %d and pid %d\n", info->status==PROCESS_ERROR, pid ) ;*/
	lock_release(&exec_lock) ;

	if ( info->status == PROCESS_ERROR )
	{
		/*printf ( "Inside error returning -1 value\n" ) ;*/
		/*goto done1 ;*/
		return -1 ;
	}

	/*printf ( "EXEC return value: %d\n", pid ) ;*/
	/*intr_set_level(old_level) ;*/
	return pid ;

/*done1:*/
	/*intr_set_level ( old_level ) ; */
	/*return -1 ;*/

}

int wait ( pid_t pid )
{
	//printf ( "reached wait syscall\n" ) ;
	return process_wait ( pid ) ;
}

// Creates a new file
bool create ( const char *file, unsigned initial_size )
{
	// Check the validity of the address of the file and also the filename
	convertAddr(file) ;
	if ( strlen(file) == 0 )
		return 0 ;

	// Create the file
	lock_acquire ( &file_lock) ;
	bool ret = filesys_create ( file, initial_size ) ;
	lock_release ( &file_lock ) ;

	return ret ;
}

bool remove ( const char *file )
{
	// Check the validity of the address of the file and also the filename
	convertAddr(file) ;
	if ( strlen(file) == 0 )
		return 0 ;

	// Create the file
	lock_acquire ( &file_lock) ;
	bool ret = filesys_remove ( file ) ;
	lock_release ( &file_lock ) ;

	return ret ;
}

int open ( const char *file )
{
	// Check if the pointer is valid
	convertAddr(file) ;

	lock_acquire ( &file_lock) ;

	bool check ;
	struct inode *inode ;

	check = dir_lookup ( dir_open_root(), file, &inode ) ;
	if ( check == false )
		return -1 ;

	struct file *newFile = file_open ( inode ) ;
	if ( newFile == NULL )
		return -1 ;

	struct file_info *info = (struct file_info *) malloc ( sizeof(struct file_info)) ;
	if ( info == NULL )
		return -1 ;

	info->fd = allocateFD() ;
	info->file = newFile ;
	list_push_back ( &thread_current()->files, &info->elem ) ;
	info->closed = false ;

	lock_release ( &file_lock ) ;

	return info->fd ;
}

int filesize ( int fd )
{
	struct file_info *f = get_file_info ( fd ) ;
	if ( f == NULL )
		return -1 ;

	return file_length(f->file) ;
}

int read ( int fd, void *buffer, unsigned size )
{
	buffer = convertAddr(buffer) ;
	/*printf ( "Here\n" ) ;*/


	struct file_info *f = get_file_info ( fd ) ;
	if ( f == NULL )
	{
		/*printf ( "Null\n" ) ;*/
		return 0 ;
	}

	/*printf ( "Passed get_info\n" ) ;*/
	int read = file_read ( f->file, buffer, size ) ;
	/*printf ( "Buffer:\n%s\n\n", (char*)buffer ) ;*/
	/*printf ( "Passed read call\n" ) ;*/

	return read ;
}

int write ( int fd, void *buffer, unsigned size )
{
	if ( fd == 1 )
		putbuf ( buffer, size ) ;

	buffer = convertAddr(buffer) ;

	struct file_info *f = get_file_info ( fd ) ;
	if ( f == NULL )
		return 0 ;

	int wrote = file_write ( f->file, buffer, size ) ;

	return wrote ;
}

void seek ( int fd, unsigned position )
{
	struct file_info *f = get_file_info ( fd ) ;
	if ( f == NULL )
		return ;

	file_seek ( f->file, position ) ;

	return ;
}

unsigned tell ( int fd )
{
	struct file_info *f = get_file_info ( fd ) ;
	if ( f == NULL )
		return 0 ;

	return file_tell ( f->file ) ;
}

void close ( int fd )
{
	/*printf ( "Inside close\n" ) ;*/
	//convertAddr( (void *)fd ) ;
	if ( fd == 0 || fd == 1 )
		return ;

	struct file_info *f = get_file_info(fd) ;
	if ( f == NULL )
		return ;

	/*if ( f->closed == true )*/
		/*return ;*/

	file_close ( f->file ) ;
	f->closed = true ;

	return ;
}

// Get the file_info structure for a given File Descriptor
struct file_info *get_file_info ( int fd )
{
	struct thread *cur = thread_current() ;

	// Check if the FD given is opened by the current thread
	bool check = false ;
	struct list_elem *e ;
	struct file_info *f ;
	for ( e = list_begin(&cur->files) ; e != list_end(&cur->files) ; e = list_next(e) )
	{
		f = list_entry(e, struct file_info, elem ) ;
		if ( f->fd == fd )
		{
			check = true ;
			break ;
		}
	}
	if ( check == false || f->closed == true )
		return NULL ;

	return f ;
}

// Function to convert a virtual user space address to a physicall address
// This will EXIT if the given address is not in user space or is unmapped
// Also used to check the validity of a address
void *convertAddr ( const void *addr) 
{
	if ( addr == NULL )
		exit(-1) ;
	if ( !is_user_vaddr(addr) )
		exit(-1) ;

	void *x = pagedir_get_page(thread_current()->pagedir, addr) ;
	if ( x == NULL )
		exit(-1) ;
	return x ;
}

// Function used allocate File Descriptors for each open file
int allocateFD ()
{
	static int fd = 2 ;
	int newFd ;

	lock_acquire(&fd_lock) ;
	newFd = fd++ ;
	lock_release(&fd_lock) ;

	return newFd ;
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

  //printf ( "numver of args is %d\n", n ) ;
  switch ( sysNum )
  {
	  case SYS_HALT:		halt () ;
							break ;

	  case SYS_EXIT:		exit ( (int)pargs[0] ) ;
							break ;

	  case SYS_EXEC:		f->eax = exec ( (char*)pargs[0] ) ;
							break ;

	  case SYS_WAIT:		f->eax = wait ( (pid_t)pargs[0] ) ;
							break ;

	  case SYS_CREATE:		f->eax = create ( (char*)pargs[0], (unsigned)pargs[1] ) ;
							break ;

	  case SYS_REMOVE:		f->eax = remove ( (char*)pargs[0] ) ;
							break ;

	  case SYS_OPEN:		f->eax = open ( (char*)pargs[0] ) ;
							break ;

	  case SYS_FILESIZE:	f->eax = filesize ( (int)pargs[0] ) ;
							break ;

	  case SYS_READ:		f->eax = read ( (int)pargs[0], (void *)pargs[1], (unsigned)pargs[2] ) ;
							break ;

	  case SYS_WRITE:		f->eax = write ( (int)pargs[0], (void *)pargs[1], (unsigned)pargs[2]) ;
							break ;

	  case SYS_SEEK:		seek ( (int)pargs[0], (unsigned)pargs[1] ) ;
							break ;

	  case SYS_TELL:		f->eax = tell ( (int)pargs[0] ) ;
							break ;

	  case SYS_CLOSE:		close ( (int)pargs[0] ) ;
							break ;

	  default:				printf ( "Invalid Handler\n\n\n") ;
							break ;
  }
}
