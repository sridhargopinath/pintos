#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

#include "threads/synch.h"
#include <hash.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

// enum to keep track of the status of the process
enum process_status
{
	PROCESS_STARTING,
	PROCESS_LOADED,
	PROCESS_ERROR
} ;

// Structure to keep track of the info of the children. Used to get the return status of a child
struct process_info
{
	tid_t tid ;							// TID of the thread
	struct thread *t ;					// Pointer to the thread this process belongs to
	enum process_status status ;		// To keep track of the status of the Process
	struct list_elem elem ;				// This element is used to insert into the list of children
	struct semaphore sema ;				// Semaphore used to wait the parent thread
	int exit_status ;					// Exit status which is returned on wait system call
	bool waited ;						// TRUE if the parent has already waited for the children
} ;

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
{
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */

	struct thread *parent ;				// Parent thread
	struct process_info *info ;			// Pointer to the entry of the structure in the parent thread
	struct list children ;				// List of all the children of this thread

	struct list files ;					// List of all the files opened by this thread

	struct file *executable ;			// Executable file of the current process (thread)

	struct hash pages ;					// Supplementary page table
#endif

#ifdef VM
	uint32_t esp ;						// Saving the stack pointer
	struct list mmaps ;					// List of the memory maps by this thread
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */

	// Store the number of ticks that the thread should sleep
	int64_t ticks ;

	// Original priority of the thread
	int real_priority ;

	// Lock on which this thread is waiting
	struct lock *lock ;

	// List to store the threads which have donated priority to this thread
	struct list donors ;

	// List_elem of this thread if it wants to donate priority to some other thread
	struct list_elem pri_elem ;

	// Nice value of this thread for mlfqs
	int nice ;

	// Recent CPU value of the thread
	int recent_cpu ;
};

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
struct list all_list;

// This is used to store the threads which are currently sleeping
struct list sleep_list ;

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

// Function to compare the priority of the two threads given as list elements
bool min_priority ( const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) ;

#endif /* threads/thread.h */
