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
#include "devices/input.h"
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"

// Typedef used for process IDs
typedef int pid_t ;

// Typedef for Memory Mapping
typedef int mapid_t ;

/*static void *esp ;*/

// This structure is used to keep track of all the files opened by a particular thread
struct file_info
{
	int fd ;
	struct file *file ;
	struct list_elem elem ;
} ;

struct mmap_page
{
	struct list_elem elem ;
	struct page *p ;
} ;

// Structure to keep Map info for each memory-mapped region
struct map_info
{
	mapid_t mapid ;
	int fd ;
	struct file *file ;
	struct list_elem elem ;
	struct list pages ;
} ;

// Lock for assigning unique File Descriptors upon opening each file
struct lock fd_lock ;

// Lock when assigning unique Map ID for each mapped region
struct lock map_lock ;

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
static mapid_t mmap ( int fd, void *addr ) ;
static void munmap ( mapid_t mapping ) ;

static int get_word_user ( const int *uaddr ) ;
static int get_user ( const uint8_t *uaddr ) ;
static void check_buffer ( const uint8_t *addr, int size ) ;
static void check_file ( const uint8_t *addr) ;

static mapid_t allocateMAPID (void) ;
static int allocateFD (void) ;
static struct file_info *get_file_info ( int fd ) ;
static struct map_info *get_map_info ( mapid_t mapping ) ;
static struct map_info *get_map_info_FD ( int fd ) ;


void syscall_init (void)
{
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

	// Initialize lock used for accessing filesys
	lock_init(&file_lock) ;

	// Initialize the condtional lock used by EXEC
	cond_init(&exec_cond) ;
	lock_init(&exec_lock) ;

	// Initialize the File Descriptor lock
	lock_init(&fd_lock) ;

	// Initialze the Map ID lock
	lock_init(&map_lock) ;
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
	2,			// SYS_MMAP
	1			// SYS_UNMAP
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

	if ( lock_held_by_current_thread(&file_lock) )
		lock_release(&file_lock) ;

	printf ( "%s: exit(%d)\n", cur->name, status ) ;


	// Clear the process info objects of all the children and free the memory
	struct list_elem *e ;
	for ( e = list_begin(&cur->children) ; e != list_end(&cur->children) ; )
	{
		struct process_info *p = list_entry ( e, struct process_info, elem ) ;
		e = list_next(e) ;

		list_remove(&p->elem) ;

		// Make the parent pointer of all the children to NULL
		p->t->parent = NULL ;

		free(p) ;
	}

	/*printf ( "Reached 1\n");*/

	/*printf ( "Size of mmaps in EXIT is %d\n", list_size(&cur->mmaps)) ;*/
	// Clear the map_info objects
	for ( e = list_begin(&cur->mmaps) ; e != list_end(&cur->mmaps) ; )
	{
		struct map_info *m = list_entry ( e, struct map_info, elem ) ;
		e = list_next(e) ;

		munmap(m->mapid) ;
		/*file_close(f->file) ;*/
		/*list_remove(&f->elem) ;*/

		/*free(f) ;*/
	}

	/*printf ( "Reached 2\n");*/
	// Deallocated the pages allocated for this thread

	/*printf ( "Size of page table hash is %d\n",hash_size(&cur->pages));*/
	/*printf ( "Size of frame table hash is %d\n",hash_size(&frames));*/

	// Clear the file_info objects
	for ( e = list_begin(&cur->files) ; e != list_end(&cur->files) ; )
	{
		struct file_info *f = list_entry ( e, struct file_info, elem ) ;
		e = list_next(e) ;

		close(f->fd);
		/*if ( get_map_info_FD(f->fd) == NULL )*/
		/*{*/
		/*lock_acquire(&file_lock);*/
		/*file_close(f->file) ;*/
		/*lock_release(&file_lock);*/
		/*}*/

		/*list_remove(&f->elem) ;*/
		/*free(f) ;*/
	}

	invalidate_swap_slots(cur) ;

	// Free the Supplymentary hash table
	if ( hash_size(&cur->pages) != 0 )
		hash_destroy ( &cur->pages, page_deallocate) ;

	// TO implement WAIT syscall:
	// Store the exit status of the current thread in the process_info structure
	// Signal the semaphore to wake up the parent if the parent thread is waiting on it
	// DO this only if PARENT is still alive
	if ( cur->parent != NULL )
	{
		cur->info->exit_status = status ;
		sema_up(&cur->info->sema) ;
	}

	// process_exit will be called inside this function
	thread_exit() ;
}

// Create a new process using process_execute command and execute the command given
pid_t exec ( const char *cmd_line )
{
	// Check the validity of the address given by checking only the first 14 bytes of the string
	check_file ( (uint8_t *)cmd_line ) ;

	// Spawn the child by by creating a new thread
	pid_t pid = process_execute(cmd_line) ;

	// Was not able to create a thread for the new process
	if ( pid == TID_ERROR )
		return -1 ;

	// Check if the new thread was able to load successfully
	// This uses the Condition variables. Check its usage online
	lock_acquire(&exec_lock) ;

	// Get the INFO structure of the thread which was just created
	// Get the PROCESS_INFO pointer of the new thread
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
	// New thread not found. This won't happen
	if ( found == false )
	{
		lock_release(&exec_lock) ;
		return -1 ;
	}

	// Wait till signalled by the child process that it has completed load
	while ( info->status == PROCESS_STARTING )
		cond_wait ( &exec_cond, &exec_lock ) ;

	lock_release(&exec_lock) ;

	// Check if LOAD has been failed
	if ( info->status == PROCESS_ERROR )
		return -1 ;

	return pid ;
}

// Current process waits for the thread with pid PID to exit
// Implementation present in process.c
int wait ( pid_t pid )
{
	return process_wait ( pid ) ;
}

// Creates a new file
bool create ( const char *file, unsigned initial_size )
{
	// Check the validity of the filename
	check_file ( (uint8_t *)file ) ;
	if ( strlen(file) == 0 )
		return 0 ;

	// Create the file
	lock_acquire ( &file_lock) ;
	bool ret = filesys_create ( file, initial_size ) ;
	lock_release ( &file_lock ) ;

	return ret ;
}

// Removes an already existing file
bool remove ( const char *file )
{
	// Check the validity of the address of the file and also the filename
	check_file ( (uint8_t *)file ) ;
	if ( strlen(file) == 0 )
		return 0 ;

	// Remove the file
	lock_acquire ( &file_lock) ;
	bool ret = filesys_remove ( file ) ;
	lock_release ( &file_lock ) ;

	return ret ;
}

// Opens an already existing file
int open ( const char *file )
{
	/*printf ( "Inside open %s\n", file ) ;*/
	// Check the validity of the address of the file and also the filename
	check_file ( (uint8_t *)file ) ;

	/*printf ( "after check file \n");*/
	lock_acquire ( &file_lock) ;

	bool check ;
	struct inode *inode ;

	// Open the root directory where all the files are present
	struct dir *direc = dir_open_root() ;
	if ( direc == NULL )
	{
		lock_release ( &file_lock ) ;
		return -1 ;
	}

	// Check if the file already exists
	check = dir_lookup ( direc, file, &inode ) ;
	if ( check == false )
	{
		dir_close(direc) ;
		lock_release ( &file_lock ) ;
		return -1 ;
	}
	dir_close(direc) ;

	// Open the file
	struct file *newFile = file_open ( inode ) ;
	if ( newFile == NULL )
	{
		lock_release ( &file_lock ) ;
		return -1 ;
	}

	// Create a FILE_INFO object to store the info of the open file
	struct file_info *info = (struct file_info *) malloc ( sizeof(struct file_info)) ;
	if ( info == NULL )
	{
		file_close ( newFile ) ;
		lock_release ( &file_lock ) ;
		return -1 ;
	}

	// Allocate new File Descriptor to the opened file
	info->fd = allocateFD() ;
	info->file = newFile ;
	list_push_back ( &thread_current()->files, &info->elem ) ;

	lock_release ( &file_lock ) ;

	/*printf ( "Fininsh open\n");*/
	return info->fd ;
}

// Returns the size of the file
int filesize ( int fd )
{
	struct file_info *f = get_file_info ( fd ) ;
	if ( f == NULL )
		return -1 ;

	lock_acquire ( &file_lock ) ;
	int size = file_length(f->file) ;
	lock_release ( &file_lock ) ;

	return size ;
}

// Reads SIZE bytes from the file with file descriptor FD into the BUFFER
int read ( int fd, void *buffer, unsigned size )
{
	/*printf ( "Inside read esp: %p\n", thread_current()->esp ) ;*/
	// Reading from STD OUTPUT
	if ( fd == 1 )
		return 0 ;

	// Check if the buffer is valid or not upto size bytes
	check_buffer ( (uint8_t *)buffer, size ) ;

	// Reading from STD INPUT
	if ( fd == 0 )
	{
		unsigned i ;
		for ( i = 0 ; i < size ; i ++ )
			*((char*)buffer+i) = input_getc() ;

		return size ;
	}

	struct file_info *f = get_file_info ( fd ) ;
	if ( f == NULL )
		return 0 ;

	lock_acquire ( &file_lock ) ;
	int read = file_read ( f->file, buffer, size ) ;
	lock_release ( &file_lock ) ;

	/*printf ( "Data read\n");*/
	/*write(1,buffer,size);*/
	return read ;
}

// Writes SIZE bytes to BUFFER from the file pointed by the file descriptor FD
int write ( int fd, void *buffer, unsigned size )
{
	// Writing to STD INPUT
	if ( fd == 0 )
		return 0 ;

	/*printf ( "Inside write\n") ;*/
	// Check if the buffer is valid or not upto size bytes
	check_buffer ( (uint8_t *)buffer, size ) ;
	/*printf ("reached 2\n");*/

	// Write to terminal
	if ( fd == 1 )
	{
		lock_acquire ( &file_lock ) ;
		putbuf ( buffer, size ) ;
		lock_release ( &file_lock ) ;
	}

	/*printf ("reached 3\n");*/
	struct file_info *f = get_file_info ( fd ) ;
	if ( f == NULL )
		return 0 ;

	lock_acquire ( &file_lock ) ;
	int wrote = file_write ( f->file, buffer, size ) ;
	lock_release ( &file_lock ) ;

	/*printf ( "Finish write\n");*/
	return wrote ;
}

// Change the position of the pointer in the file descriptor
void seek ( int fd, unsigned position )
{
	struct file_info *f = get_file_info ( fd ) ;
	if ( f == NULL )
		return ;

	lock_acquire ( &file_lock ) ;
	file_seek ( f->file, position ) ;
	lock_release ( &file_lock ) ;

	return ;
}

// Current position of the pointer in the file descriptor
unsigned tell ( int fd )
{
	struct file_info *f = get_file_info ( fd ) ;
	if ( f == NULL )
		return 0 ;

	lock_acquire ( &file_lock ) ;
	unsigned tell = file_tell ( f->file ) ;
	lock_release ( &file_lock ) ;

	return tell ;
}

// Close the file pointed by the file descriptor FD
void close ( int fd )
{
	if ( fd == 0 || fd == 1 )
		return ;

	struct file_info *f = get_file_info(fd) ;
	if ( f == NULL )
		return ;

	struct map_info *m = get_map_info_FD(fd);
	if ( m == NULL )
	{
		/*printf ( "Mmap NULL\n");*/
		lock_acquire ( &file_lock ) ;
		file_close ( f->file ) ;
		lock_release ( &file_lock ) ;
	}
	/*else*/
	/*printf ("mmap not null\n");*/


	list_remove ( &f->elem) ;
	free(f) ;

	/*printf ( "finish close\n");*/
	return ;
}

mapid_t mmap ( int fd, void *addr )
{
	/*printf ( "Inside mmap\n" ) ;*/
	/*return -1 ;*/
	struct thread *cur = thread_current() ;

	if ( pg_ofs(addr) != 0 )
		return -1 ;

	if ( is_user_vaddr(addr) == false )
		return -1 ;

	if ( addr == NULL )
		return -1 ;

	struct file_info *file_info = get_file_info(fd) ;
	if ( file_info == NULL )
		return -1 ;

	if ( page_lookup(addr) != NULL )
		return -1 ;

	/*printf ("Before file_length\n");*/
	lock_acquire(&file_lock) ;
	int size = file_length(file_info->file) ;
	lock_release(&file_lock) ;

	if ( size == 0 )
		return -1 ;

	struct map_info *newMap = (struct map_info *) malloc (sizeof(struct map_info));
	if ( newMap == NULL )
		PANIC("MMAP: Failed to allocate memory for map_info structure\n");

	newMap->mapid = allocateMAPID() ;
	newMap->fd = file_info->fd ;
	newMap->file = file_info->file ;
	list_push_back(&cur->mmaps, &newMap->elem) ;

	list_init(&newMap->pages);


	int read_bytes = size ;
	int page_read_bytes, ofs = 0 ;
	int pagesNum = 0 ;
	/*printf ( "file length in mmap is %u\n", file_length(file_info->file));*/
	while ( read_bytes > 0 )
	{
		struct page *p = (struct page *) malloc ( sizeof(struct page));
		if ( p == NULL )
			PANIC("MMAP: SPTE memory allocation failed\n");

		page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE ;

		/*printf ("inside loop of mmap\n");*/

		p->file = file_info->file ;
		p->addr = addr ;
		p->ofs = ofs ;
		p->read_bytes = page_read_bytes ;
		p->writable = true ;
		p->kpage = NULL ;

		p->stack = false ;

		p->swap = NULL ;


		page_insert ( &cur->pages, &p->hash_elem ) ;

		read_bytes -= page_read_bytes ;
		ofs += page_read_bytes ;
		addr += PGSIZE ;
		pagesNum ++ ;

		/*printf ( "Addr of file * is %p\n addr of addr %p\n offset is %u\nRead bytes is %d\nWritable is %d\n", p->file, p->addr, p->ofs, p->read_bytes, p->writable) ;*/

		struct mmap_page *m_page = (struct mmap_page *)malloc(sizeof(struct mmap_page));
		m_page->p = p ;
		list_push_back(&newMap->pages, &m_page->elem) ;
	}


	/*printf ( "%d number of pages allocated using mmap\n",pagesNum);*/
	/*printf ( "RETURN from MMAP\n");*/
	return newMap->mapid ;
}

void munmap ( mapid_t mapping )
{
	/*printf ( "Inside munmap of %s\n", thread_current()->name);*/
	/*printPageTable();*/
	struct thread *cur = thread_current() ;

	/*printf ( "Size of mmaps before munmap is %d\n", list_size(&cur->mmaps)) ;*/
	struct map_info *map = get_map_info ( mapping ) ;
	if ( map == NULL )
		return ;

	/*printf ( "FD is %d\n", map->fd);*/
	/*if ( map->file != get_file_info(map->fd)->file )*/
	/*printf ( "FDs are not equal\n");*/

	off_t old_ofs = tell(map->fd) ;
	/*printf ( "Old ofs is %u\n", old_ofs);*/

	/*printf ( "Map id to unmap is %d\n",map->mapid) ;*/
	struct list_elem *e, *next ;
	for ( e = list_begin(&map->pages) ; e != list_end(&map->pages) ; e = next )
	{
		/*printf ( "Inside loop\n");*/
		next = list_next(e) ;
		struct mmap_page *map_page = list_entry(e, struct mmap_page, elem) ;
		struct page *p = map_page->p ;
		struct hash_elem *h = &p->hash_elem ;

		if ( p->swap != NULL )
		{
			load_swap_slot(p,cur);
		}

		void *upage = p->addr ;
		bool is_dirty = pagedir_is_dirty(cur->pagedir,upage) ;
		if ( is_dirty )
		{
			/*printf ( "Dirty\n");*/
			/*write ( 1, upage, p->read_bytes) ;*/
			seek(map->fd, p->ofs);
			/*printf ( "seek\n");*/
			/*printf ( "ofs: %d\n", tell(map->fd));*/
			write(map->fd, upage, p->read_bytes) ;
			/*printf ( "Write\n");*/
			/*if ( written == p->read_bytes )*/
			/*printf ( "Wrote successfully\n");*/
			/*else*/
			/*printf ( "Write failed: %u and %u\n", written, p->read_bytes);*/
			/*lock_acquire(&file_lock);*/
			/*file_seek(p->file, p->ofs) ;*/
			/*file_write(p->file, upage, p->read_bytes);*/
			/*lock_release(&file_lock);*/
		}

		/*printf ( "before deallocate::\n");*/
		/*printPageTable();*/
		page_deallocate(h, (void*)1) ;

		/*printf ( "After deallocate::\n");*/
		/*printPageTable();*/
		hash_delete(&cur->pages, h);

		free(p);
		free(map_page) ;
		/*list_remove(e) ;*/
	}
	/*printf ( "Out of the loop\n");*/
	seek(map->fd, old_ofs);

	/*printf ("before close\n");*/
	if ( get_file_info(map->fd) == NULL )
	{
		/*printf ( "Closing file in munmap\n");*/
		lock_acquire(&file_lock);
		file_close(map->file);
		lock_release(&file_lock);
	}
	/*printf ( "After close\n");*/

	list_remove(&map->elem);
	free(map) ;

	/*printf ( "after munmap:\n");*/
	/*printPageTable();*/
	return ;
}

/* Reads a word at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the word value if successful, -1 if a segfault occurred. */
int get_word_user (const int *uaddr)
{
	int result;
	if ((void *) uaddr >= PHYS_BASE)
		exit(-1);

	asm ("movl $1f, %0; movl %1, %0; 1:"
			: "=&a" (result) : "m" (*uaddr));
	return result;
}

/*Reads a byte at user virtual address UADDR.
  UADDR must be below PHYS_BASE.
  Returns the byte value if successful, -1 if a segfault occurred.*/
int get_user (const uint8_t *uaddr)
{
	int result;
	if ((void *) uaddr >= PHYS_BASE)
		return -1;

	asm ("movl $1f, %0; movzbl %1, %0; 1:"
			: "=&a" (result) : "m" (*uaddr));
	return result;
}

// Checks if the BUFFER of size SIZE bytes is mapped and are in user address space
// If not, exit
void check_buffer ( const uint8_t *addr, int size )
{
	int tmp, i ;
	for( i = 0 ; i <= size-1 ; i++ )
	{
		tmp = get_user(addr + i);
		if(tmp == -1)
			/*{*/
			/*printf ( "Exit %d\n", i) ;*/
			exit (-1);
		/*}*/
	}
	return ;
}

// Checks if the first 14 bytes of the address are mapped and are in user address space
// If not, exit
void check_file ( const uint8_t *addr )
{
	check_buffer ( addr, strlen((char*)addr)+1) ;
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
	if ( check == false )
		return NULL ;

	return f ;
}

// Get the map_info structure for a given Mapping ID
struct map_info *get_map_info ( mapid_t mapping )
{
	struct thread *cur = thread_current() ;

	// Check if the FD given is opened by the current thread
	bool check = false ;
	struct list_elem *e ;
	struct map_info *m ;
	for ( e = list_begin(&cur->mmaps) ; e != list_end(&cur->mmaps) ; e = list_next(e) )
	{
		m = list_entry(e, struct map_info, elem ) ;
		if ( m->mapid == mapping  )
		{
			check = true ;
			break ;
		}
	}
	if ( check == false )
		return NULL ;

	return m ;
}

// Get the map_info structure for a given FD
struct map_info *get_map_info_FD ( int fd )
{
	struct thread *cur = thread_current() ;

	// Check if the FD given is opened by the current thread
	bool check = false ;
	struct list_elem *e ;
	struct map_info *m ;
	for ( e = list_begin(&cur->mmaps) ; e != list_end(&cur->mmaps) ; e = list_next(e) )
	{
		m = list_entry(e, struct map_info, elem ) ;
		if ( m->fd == fd  )
		{
			check = true ;
			break ;
		}
	}
	if ( check == false )
		return NULL ;

	return m ;
}

// Function used allocate MAPID to each memory mapping
mapid_t allocateMAPID ()
{
	static int mapid = 0 ;
	int newMapID ;

	lock_acquire(&map_lock) ;
	newMapID = mapid++ ;
	lock_release(&map_lock) ;

	return newMapID ;
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
	// Storing the stack pointer address in the thread structure to access later
	thread_current()->esp = (uint32_t)f->esp ;

	// Get the syscall number from the stack pointer
	int sysNum  = get_word_user((int*)f->esp) ;

	// Number of arguments required for this system call
	int n = argsNum[sysNum] ;

	// Get N arguments from the stack and store their values as void pointers in an array
	void *pargs[n] ;
	int i ;
	for ( i = 0 ; i < n ; i ++ )
		pargs[i] = (void *) get_word_user ( (int*)f->esp + i + 1 ) ;

	/*printf ( "sysnum is %d\n", sysNum ) ;*/
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

		case SYS_MMAP:		//printf ( "mmap called\n");
							f->eax = mmap ( (int)pargs[0], (void *)pargs[1] ) ;
							break ;

		case SYS_MUNMAP:	//printf ("UNMAP IS called\n");
							munmap ( (mapid_t)pargs[0] ) ;

							break ;

		default:				break ;
	}
}
