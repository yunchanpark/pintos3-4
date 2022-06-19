#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#include "threads/fixed_point.h" // mlfqs 관련 변경
// #include "lib/stdio.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* Set reasonable depth limit, default for mlfqs (max level 8) */
#define MAXDEPTH 8

/* Set reasonable default for mlfqs */
#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0 

// /* Set default for nice, recent_cpu, and load_avg */
// #define NICE_DEFAULT 0
// #define RECENT_CPU_DEFAULT 0
// #define LOAD_AVG_DEFAULT 0

/* List of all process */
static struct list all_list; // mlfqs 관련 변경

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of processes in THREAD_BLOCKED state, that is, processes
   that are bloked and sleeping now */
static struct list sleep_list; // alarm-multiple 관련 변경
static int64_t next_tick_to_awake; /* alarm-multiple 관련 변경 */

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;
int load_avg; // mlfqs 관련 변경

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&all_list); // mlfqs 관련 변경
	list_init (&destruction_req);
	list_init (&sleep_list); // alarm-multiple 관련 변경 // initialize sleep_list
	next_tick_to_awake = INT64_MAX; // alarm-multiple 관련 변경 // initialize next_tick_to_awake

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);
	
	/* Start preemptive thread scheduling. */
	intr_enable ();
	load_avg = LOAD_AVG_DEFAULT; // mlfqs 관련 변경

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
/* alarm-priority, priority-fifo/preempt 관련 변경 */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Project 2 : system call 관련 추가 */
	/* add new thread 't' into current thread's child_list */
	struct thread *curr = thread_current();
	list_push_back(&curr->child_list, &t->child_elem);
	// File Descriptor Table 메모리 할당 // palloc이나 malloc?
	t->fd_table = palloc_get_multiple(PAL_ZERO, FDT_PAGES);
	if(t->fd_table == NULL)
		return TID_ERROR;
	t->fd_idx = 2; // 0 : stdin, 1: stdout

	// /* project 2 : Extra */
	t->fd_table[0] = 1; // dummy value : 0이 아니라 1을 주는 이유: 0을 주면, fd_table[fd]==NULL 을 확인할 때 걸릴 수 있음
	t->fd_table[1] = 2; // dummy value : 같은 맥락에서 여긴 2로 줌 
	t->stdin_count = 1;
	t->stdout_count = 1;

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t);

	/* after the unblocked thread added to ready list, check if current thread is still the thread with highest priority. 
	   (check if new inserted thread has higher priority than current one) */
	check_curr_max_priority(); // alarm-priority, priority-fifo/preempt 관련 변경 

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
/* alarm-priority, priority-fifo/preempt 관련 변경 */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_insert_ordered(&ready_list, &t->elem, cmp_priority, NULL); // alarm-priority, priority-fifo/preempt 관련 변경 // instead Round-Robin scheduling, insert into ready_list base on priority.
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) {
	ASSERT (!intr_context ());
/* executed by process_exit in Project 2 */
#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
/* alarm-priority, priority-fifo/preempt 관련 변경 */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_insert_ordered(&ready_list, &curr->elem, cmp_priority, NULL); // alarm-priority, priority-fifo/preempt 관련 변경 // instead Round-Robin scheduling, insert into ready_list base on priority.
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* alarm-multiple 관련 변경 */
/* block current thread, and insert it into sleep list */
void
thread_sleep (int64_t ticks) {
	enum intr_level old_level;
	old_level = intr_disable ();
	struct thread *curr = thread_current();
	// ASSERT (!intr_context ());
	ASSERT(curr != idle_thread); // check that current thread is not IDLE thread
	
	curr->wakeup_tick = ticks; // set current thread's local tick = ticks
	update_next_tick_to_awake(ticks); // update next thread to be awakened which has minimal ticks (update_next_tick_to_awake)
	list_push_back (&sleep_list, &curr->elem); // insert into sleep list
	
	thread_block(); // change state to BLOCKED
	intr_set_level (old_level);
}

/* alarm-multiple 관련 변경 */
/* check every threads in the sleep list and wakeup if necessary */
void 
thread_awake(int64_t ticks){
	struct list_elem *e = list_begin(&sleep_list);
	next_tick_to_awake = INT64_MAX; // initialize next_tick_to_awake
	while (e != list_end(&sleep_list)){
		struct thread * t = list_entry(e, struct thread, elem);
		if (t->wakeup_tick <= ticks){
			e = list_remove(&t->elem);
			thread_unblock(t); // wakeup (awake) !
		}
		else{
			e = list_next(e);
			update_next_tick_to_awake(t->wakeup_tick); // update next_tick_to_awake if needed
		}
	}
}

/* alarm-multiple 관련 변경 */
/* update the next_tick_to_awake value if needed. 
   parameter 'ticks' is the local tick(wakeup_tick field) of the new thread inserted into sleep list */
void
update_next_tick_to_awake(int64_t ticks){
	next_tick_to_awake = (next_tick_to_awake > ticks) ? ticks : next_tick_to_awake;
}

/* alarm-multiple 관련 변경 */
/* return the next_tick_to_awake value, in order to check which thread(in the sleep list) to awake next */
int64_t 
get_next_tick_to_awake(void){
	return next_tick_to_awake;
}

/* alarm-priority, priority-fifo/preempt 관련 변경 */
/* priority-donate 관련 변경 */
/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	if (thread_mlfqs) // if mlfqs activated, return // mlfqs 관련 변경
		return;
	struct thread *curr = thread_current();
	curr->init_priority = new_priority;
	refresh_priority(); // priority-donate 관련 변경 // after apply new_priority, refresh current thread's priority
	donate_priority(); // priority-donate 관련 변경 // if the current thread's priority changed due to refresh function, adjust donation (by donate again with new priority). 
	check_curr_max_priority(); // alarm-priority, priority-fifo/preempt 관련 변경 // check if current thread is still thread with the highest priority anymore. if not, yield ! 
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	enum intr_level old_level = intr_disable();
	int ret = thread_current ()->priority;
	intr_set_level(old_level);
	return ret;
}

/* alarm-priority, priority-fifo/preempt 관련 변경 */
/* check if current thread is still the highest priority thread. if not, yield. */
void 
check_curr_max_priority(void){
	// 검증중 // 되네
	// alarm-priority, priority-fifo/preempt 관련 변경 // checking list_empty is necessary (if not, list_front: ASSERT (!list_empty (list)); FAILS and return debug-panic)
	if (!list_empty(&ready_list) && thread_get_priority() < list_entry(list_front(&ready_list), struct thread, elem)->priority){ // empty 확인 필요없이 begin으로만 해주면 가능. empty+front는?
		/* 변경사항: yield 관련 디버깅 */
		if(!intr_context())
			thread_yield();
	}

	//되는거
	// alarm-priority, priority-fifo/preempt 관련 변경 
	// if (thread_get_priority() < list_entry(list_begin(&ready_list), struct thread, elem)->priority) // empty 확인 필요없이 begin으로만 해주면 가능. empty+front는?
	// 	thread_yield();
}

/* alarm-priority, priority-fifo/preempt 관련 변경 */
/* compare priority of two threads. check if a has higher priority than b.  */
bool 
cmp_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	struct thread * t_a = list_entry(a, struct thread, elem);
	struct thread * t_b = list_entry(b, struct thread, elem);
	if (t_a->priority > t_b->priority)
		return true; // return true, and at the called spot, a will be inserted into the list, right in front of b. list(- - - - (insert a here) b - - ) 
	else
		return false;
}

/* priority-donate 관련 변경 */
/* inherit(donate) current thread's priority to the lock holder. (nested donation is limited by MAXDEPTH 8 )  */
void 
donate_priority(void){
	struct thread *target_lock_caller = thread_current();
	struct lock *target_lock = target_lock_caller->wait_on_lock;
	int depth; /* impose a reasonable limit on depth of nested priority donation */

	for (depth = 0; depth < MAXDEPTH; depth++){
		if (!target_lock) // if caller is not waiting for a lock, end of the loop
			return;
		target_lock->holder->priority = target_lock_caller->priority; /* donation */ 
		target_lock_caller = target_lock->holder; // update caller to check if further donation is needed
		target_lock = target_lock_caller->wait_on_lock; // update target lock (the lock which new caller is waiting for)
	}
}

/* priority-donate 관련 변경 */
/* remove the donors who donated its priority in order to get the lock which is now released by current thread. */
void 
remove_donors_on_released_lock(struct lock *lock){
	struct thread *curr = thread_current();
	struct list_elem *e = list_begin(&curr->donations); // list_front is not working : assertion !list_empty(list) error. (the case when current thread got 0 donations)
	while (e != list_end(&curr->donations)){
		struct thread *t = list_entry(e, struct thread, donation_elem); // need to use donation_elem, instead of elem. (elem is for ready_list, sleep_list, destruction_req.)
		if (t->wait_on_lock == lock)
			e = list_remove(&t->donation_elem);
		else
			e = list_next(e);
	}
}

/* priority-donate 관련 변경 */
/* refresh current thread's priority, after release a lock */
void refresh_priority(void){
	struct thread *curr = thread_current();
	curr->priority = curr->init_priority; // initialize to the init_priority (its own default priority)
	if (list_empty(&curr->donations)) // if there isn't any donor, return (no need to check the highest donation priority)
		return;
	list_sort(&curr->donations, cmp_donation_priority, NULL); // sort the donations list based on donation priority
	struct thread *p = list_entry(list_front(&curr->donations), struct thread, donation_elem); // thread with the higest priority from the donations
	if (p->priority > curr->init_priority) // if the highest donation priority is higher than default priority (init_priority)
		curr->priority = p->priority; // boost current thread's priority to degree of the highest donation priority
}

/* priority-donate 관련 변경 */
/* compare donor's donation priority, in order to check who donated higher priority. 
   if the priority of donor a is higher than donor b, return true  */
bool cmp_donation_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED){
	struct thread * t_a = list_entry(a, struct thread, donation_elem);
	struct thread * t_b = list_entry(b, struct thread, donation_elem);
	if (t_a->priority > t_b->priority)
		return true;
	else
		return false;
} 

/* mlfqs 관련 변경 */
/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	enum intr_level old_level = intr_disable();
	struct thread *curr = thread_current();
	curr->nice = nice;
	mlfqs_priority(curr);
	check_curr_max_priority();
	intr_set_level(old_level);
	/* TODO: Your implementation goes here */
}

/* mlfqs 관련 변경 */
/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level = intr_disable();
	int nice_value = thread_current() -> nice;
	intr_set_level(old_level);
	return nice_value;
}

/* mlfqs 관련 변경 */
/* Returns 100 times the system load average. rounded to the nearest integer. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level = intr_disable();
	int load_avg_value = fp_to_int_round(mult_mixed(load_avg, 100));
	intr_set_level(old_level);
	return load_avg_value;
}

/* mlfqs 관련 변경 */
/* Returns 100 times the current thread's recent_cpu value. rounded to the nearest integer. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	enum intr_level old_level = intr_disable();
	struct thread *curr = thread_current();
	int recent_cpu_value = fp_to_int_round(mult_mixed(curr->recent_cpu, 100));
	intr_set_level(old_level);
	return recent_cpu_value;
}

// mlfqs 관련 변경 
/* set target thread's priority on mlfqs */
void mlfqs_priority (struct thread *t){
	if (t == idle_thread)
		return;
	t->priority = fp_to_int(add_mixed(div_mixed(t->recent_cpu, -4), PRI_MAX - t->nice * 2));
}

// mlfqs 관련 변경
/* calculate recent_cpu field for target thread, which is not idle thread */
void mlfqs_recent_cpu (struct thread *t){
	if(t == idle_thread) // make sure that current thread is not idle thread
		return;
	t->recent_cpu = add_mixed(mult_fp(div_fp(mult_mixed(load_avg, 2), add_mixed(mult_mixed(load_avg, 2),1)), t->recent_cpu), t->nice);
}

// mlfqs 관련 변경
/* caculate load_avg which is used system-wide (not thread specific) */
void mlfqs_load_avg (void){
	int ready_threads = list_size(&ready_list);
	struct thread *curr = thread_current();
	if (curr != idle_thread)
		ready_threads ++;
	// ready_threads += list_size(&ready_list);
	/* if current thread is not idle thread, ready_threads should include the running thread. therefore, ready_threads ++ */
	// if (curr != idle_thread)
	// 	ready_threads ++;
	load_avg = add_fp(mult_fp(div_fp(int_to_fp(59), int_to_fp(60)), load_avg), mult_mixed(div_fp(int_to_fp(1), int_to_fp(60)), ready_threads));
	if (load_avg < 0){ // load_avg는 0보다 작아질 수 없다.
		load_avg = LOAD_AVG_DEFAULT;
	}
}

// mlfqs 관련 변경
/* if current thread is not idle thread, recent_cpu ++ */
void mlfqs_increment (void){
	struct thread *curr = thread_current();
	if (curr != idle_thread) 
		curr->recent_cpu = add_mixed(curr->recent_cpu, 1);
}

// mlfqs 관련 변경
// recalculate every thread's recent_cpu & priority
void mlfqs_recalc(void)
{
	struct list_elem *e;
	for(e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)){
		mlfqs_recent_cpu(list_entry(e, struct thread, all_elem));
		mlfqs_priority(list_entry(e, struct thread, all_elem));
	}
}


/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
	
	/* priority-donate 관련 변경 */
	/* initialize fields for priority donation */
	t->init_priority = priority;
	t->wait_on_lock = NULL;
	list_init(&t->donations);
	/* mlfqs 관련 변경 */
	t->nice = NICE_DEFAULT;
	t->recent_cpu = RECENT_CPU_DEFAULT;
    t->vm_rsp = t->tf.rsp; // team 7

	list_init(&t->child_list);
	sema_init(&t->wait_sema,0);
	sema_init(&t->fork_sema,0);
	sema_init(&t->free_sema,0);

	if(t!= idle_thread){
		list_push_back (&all_list, &t->all_elem);
	}
	/* system call 관련 변경 */
	// t->exit_status = 0;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

/* schedule willbe execute by process_activate() in Project 2*/
#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used bye the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
			list_remove(&curr->all_elem); //mlfqs 관련 변경 // 여기가 6 FAIL의 원인이었음 
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}