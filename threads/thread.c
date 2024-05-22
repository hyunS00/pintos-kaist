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

/* 우선순위 체인 최대 깊이 */
#define NESTING_DEPTH 8

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;



/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* sleep 쓰레드 리스트 */
static struct list sleep_list;

/* sleep 리스트를 잠그는 락 */
static struct lock sleep_lock;

/* 모든 쓰레드를 관리할 리스트 */
static struct list all_list;

/* 1초에 평균 몇개의 쓰레드를 처리하는지 나타내는 변수 cpu 부하율 */
static real load_average;

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

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);
static void preemption (void);
static bool tick_less (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);
static bool donation_priority_more (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);

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
	list_init (&destruction_req);
	list_init(&all_list); // all_list 초기화

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->wakeup_tick = -1;
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();

	/* 슬립 리스트와 락 초기화 */
	list_init(&sleep_list);
	lock_init(&sleep_lock);
	/* 초기화된 쓰레드 우선순위 조정을 위해 all_list에 집어 넣기 */
	list_push_front(&all_list, &initial_thread->allelem);
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


	/* 초기화된 쓰레드 우선순위 조정을 위해 all_list에 집어 넣기 */
	if(name != "idle")
		list_push_front(&all_list, &t->allelem);

	/* Add to run queue. */
	thread_unblock (t);

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
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_insert_ordered(&ready_list, &t->elem, priority_more, NULL); // 우선순위대로 쓰레드 레디 리스트에 삽입
	t->status = THREAD_READY;
	if(!intr_context()) // 현재 실행중인 컨텍스트가 인터럽트이면 선점이 되어버리면 안됨
		preemption(); // 레디 리스트로 들어갔다면 선점 가능한지 확인
	intr_set_level (old_level);
}

/* 선점 가능한지 체크하고 가능하면 선점 */
void
preemption (void) {
	struct list_elem *e;
	struct thread *t;

	/* 현재 쓰레드가 idle이거나 ready리스트가 비어있다면 우선순위 비교 X */
	if(list_empty(&ready_list) || thread_current() == idle_thread)
		return;
		
	e = list_begin(&ready_list);
	t = list_entry(e, struct thread, elem);
	if(t->priority > thread_current()->priority) // 현재 쓰레드가 우선순위 내림차순으로 정렬된 ready리스트의 맨 앞의 우선순위와 비교
		thread_yield(); // 현재 쓰레드의 우선순위가 낮으면 양보
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

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	list_remove(&thread_current()->allelem);
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	list_insert_ordered(&ready_list, &curr->elem, priority_more, NULL); // ready리스트 내림차순으로 정렬
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	struct thread *curr = thread_current (); // 현재 실행중인 쓰레드

	if(!thread_mlfqs){ // mlfqs 모드가 아니면
		curr->origin_priority = new_priority; // origin_priority 업데이트
		donation_update_priority(curr); // 변경된 origin_priority와 도네이션 리스트 비교
	}
	else{ // mlfqs 모드이면
		curr->priority = new_priority;
		preemption();
	}
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) {
	thread_current()->nice = nice; // 현재 실행중인 쓰레드의 나이스를 변경
	update_priority(); // nice를 변경했으니 우선순위를 한번 업데이트
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* 
	 * load_avg 정수변환  
	 * 100을 곱하는 이유는 소수 둘째자리 까지 표현하는 거라서 그럼
	 * 사용예시: printf("%d.%02d",thread_get_load_avg()/100, thread_get_load_avg() %100)
	 */
	return fixed_to_nearest_integer(load_average * 100);
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* 
	 * recent_cpu를 정수변환  
	 * 100을 곱하는 이유는 소수 둘째자리 까지 표현하는 거라서 그럼
	 * 사용예시: printf("%d.%02d",thread_get_recent_cpu()/100, thread_get_recent_cpu() %100)
	 */
	return fixed_to_nearest_integer(thread_current()->recent_cpu * 100);
}

void
thread_set_wakeup_tick (int64_t tick) {
	thread_current ()->wakeup_tick = tick;
}

/* Returns the current thread's priority. */
int64_t
thread_get_wakeup_tick (void) {
	return thread_current ()->wakeup_tick;
}

/* 1초마다 load_average업데이트 */
void update_load_average(){
	int ready_threads = list_size(&ready_list); // 레디중인 쓰레드와 현재 실행중인 쓰레드의 갯수

	/* 현재 실행중인 쓰레드가 idle_thread가 아니면 현재 실행중인 쓰래드 까지 포함시켜 load_avergea 계산*/
	if(thread_current() != idle_thread)
		ready_threads++;

	/* 59를 실수로 변환한 fifty_nine, 60을 실수로 변환한 sixty 1을 실수로 변환한 one 계산한 값을 임시 저장할 num1, num2 */
	real fifty_nine, sixty, one, num1, num2;

	fifty_nine = integer_to_fixed(59); // 정수 59를 실수로 변환
	sixty = integer_to_fixed(60); // 정수 60을 실수로 변환
	one = integer_to_fixed(1); // 정수 1을 실수로 변환

	num1 = divide_fixed(fifty_nine, sixty); // 59/60 실수계산
	num1 = multiple_fixed(num1, load_average); // (59/60 * load_average)
	num2 = divide_fixed(one, sixty); // 1/60 실수 계산

	num2 *= ready_threads; // (1/60 * ready_threads)

	/* load_average 업데이트 */
	load_average = num1 + num2; // (59/60 * load_average) + (1/60 * ready_threads)
}

/* 감쇠율을 계산하는 함수 */
real get_decay(){
	real num1, num2;

	num1 = load_average * 2; // (2 * load_average)
	num2 = add_fixed_from_integer(num1, 1); // (2 * load_average + 1)

	return divide_fixed(num1, num2); // (2 * load_average) / (2 * load_average + 1)
}

/* 1초마다 recent_cpu를 새 값으로 업데이트 */
void update_recent_cpu(){
	struct thread *curr = thread_current(); // 현재 실행중인 쓰레드 curr

	real decay, calulated; // 고정소수점실수형(int) 감쇠율 decay, 계산되는 값을 임시로 담을 calculated

	decay = get_decay(); // decay값 계산

	/* all_list를 순회하며 해당 쓰레드의 recent_cpu 업데이트 */
	for(struct list_elem *e = list_begin(&all_list); e != list_end(&all_list); e = list_next(&curr->allelem)){
		curr = list_entry(e, struct thread, allelem); // all_list에서 thread 조회
		calulated = multiple_fixed(decay, curr->recent_cpu); // decay * recent_cpu
		/* 
		 * 수식: decay * recent_cpu * nice 
		 * nice는 정수이기 때문에 고정 소수점 연산을 위해 nice * 2^14로 변환하고 계산
		 */
		calulated = add_fixed_from_integer(calulated, curr->nice);

		curr->recent_cpu = calulated; // recent_cpu 업데이트
	}
}

/* 매 tick마다 recent_cpu를 1씩 증가 */
void increase_recent_cpu() {
	struct thread *curr = thread_current();; // 현재 실행중인 쓰레드 curr

	/* 
	 * idle_thread는 ready 리스트가 비어있고 현재 진행 중인 쓰레드가 없을때 실행되는 쓰레드다
	 * 그렇기 때문에 idle_thread의 우선순위는 항상 0이어야 하고 cpu를 얼마나 점유하든 recent_cpu를 증가 시켜선 안된다
	 * 현재 실행중인 쓰레드가 idle_thread이면 함수를 실행시키지 않는다
	 */
	if(thread_current() == idle_thread)
		return;

	curr->recent_cpu = add_one_fixed(curr->recent_cpu); // 실행중인 쓰래드의 recent_cpu에 1을 더함
}

/* 4tick마다 모든 쓰레드의 우선순위 업데이트 */
void update_priority(){
	struct list_elem *e; // all_list를 순회할 elem
	struct thread *curr; // all_list를 순회할 thread
	int calulated; // 우선순위를 계산해 잠깐 담아둘 변수

	for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)){ // all_list 순회
		curr = list_entry(e, struct thread, allelem); // all_list에서 쓰레드 조회

		/* PRI_MAX -(recent_cpt/4) - (2*nice) */
		calulated = PRI_MAX - fixed_to_nearest_integer((curr->recent_cpu / 4)) - (curr->nice * 2);

		if (calulated > 63) //아주 만약에 우선순위가 63 보다 커지면 63으로 맞춰주기
			calulated = PRI_MAX;
		else if (calulated < PRI_MIN) // 아무 만약에 우선순위가 0보다 작아지면 0으로 맞춰주기
			calulated = PRI_MIN;

		curr->priority = calulated; // all_list에서 조회한 쓰레드 우선순위 업데이트
	}
}

/* 우선순위를 기부 */
void donate_priority(struct thread *holder, struct thread *receiver) {
	enum intr_level old_level;

	if(holder->priority < receiver->priority)
		holder->priority = receiver->priority; // lock을 가지는 쓰레드의 우선순위를 lock을 요청한 쓰레드의 우선순위로 업데이트

	old_level = intr_disable();
	list_push_front(&holder->donations, &receiver->d_elem); // lock을 가지고있는 holder의 도네이션 리스트에 락을 요청하는 reciver 삽입
	donate_priority_nested(holder); // lock을 소유하는 holder에 우선순위 전파
	intr_set_level(old_level);
}
/* 기부받은 도네이션 제거 */
void donation_remove(struct lock *lock) {
	enum intr_level old_level;
	struct thread *holder, *curr_thread; // lock을 가지고 있던 holder와 도네이션 리스트를 순회할 쓰레드 curr_thread
	struct list_elem *curr; // 도네이션 리스트를 순회할 curr

	holder = thread_current(); // 락을 반납하고 도네이션을 반납하는 쓰레드는 실행중인 쓰레드
	ASSERT(holder == lock->holder); // 당연히 lock의 홀더랑 현재 쓰레드랑 다르면 락을 반납할 수가 없으니

	if(list_empty(&holder->donations)) // 도네이션 리스트가 비어있으면 비교할게 없음
		return;

	curr = list_front(&holder->donations); // 도네이션을 순회할 curr 설정
	old_level = intr_disable();

	/* 도네이션 리스트를 순회하며 holder쓰레드가 가지던 락을 요청하는 모든 쓰레드들을 donations 리스트에서 제거 */
	while (curr != list_tail(&holder->donations)) // curr가 donations 리스트의 tial이면 탐색 종료
	{
		curr_thread = list_entry(curr, struct thread, d_elem); // curr을 통해 curr_thread 조회
		if(curr_thread->wait_on_lock == lock){ // curr_thread가 요청하는 락이 홀더의 lock과 같다면 리스트에서 제거
			list_remove(curr); // 제거
		}
		curr = list_next(curr); // curr을 next로 옮김
	}

	donation_update_priority(holder); // 도네이션 리스트에서 제거를 했으면 우선순위 업데이트
	intr_set_level(old_level);
}

/* 현재 우선순위를 origin priority업데이트 */
void donation_update_priority(struct thread *t) {
	struct thread *max_thread; // 도네이션 리스트에서 가장 큰 우선순위를 갖는 쓰레드
	struct list_elem *max_elem; // 도네이션 리스트에서 가장 큰 우선순위를 갖는 list_elem

	/* 도네이션 리스트가 비어있다면 origin_priority로 업데이트 */
	if(!list_empty(&t->donations)){
		max_elem = list_max(&t->donations, donation_priority_more, NULL); // 도네이션 리스트에서 가장 큰값 조회
		max_thread = list_entry(max_elem, struct thread, d_elem); // max_elem으로 쓰레드 조회
		if(t->origin_priority < max_thread->priority) // donations 리스트에는 우선순위가 작은 녀석들도 들어있으니 오리진 보다 작으면 업데이트 시키면 안됨
			t->priority = max_thread->priority; // max_thread의 우선순위로 업데이트
		else
			t->priority = t->origin_priority; // 그냥 오리진으로 업데이트
	}
	else{
		t->priority = t->origin_priority; // 도네이션 리스트가 비어있다면 origin_priority로 업데이트
	}
	
	preemption(); // 우선순위를 업데이트를 했으니 선점되는 쓰레드인지 체크
}

/* 연쇄적인 priority chain priority 업데이트 */
void donate_priority_nested(struct thread *t) {
	struct thread *holder, *curr = t; // lock을 소유하는 holder, 락의 홀더를 계속 탐색하며 앞으로 나아갈 curr
	int depth = 0; // 업데이트한 깊이

	/* 기다리는 락이 있으면 탐색 기다리고 있는 락이 없으면 멈춤  너무 깊어지면 안되니까 8depth까지만 업데이트*/
	while (curr->wait_on_lock != NULL && depth < NESTING_DEPTH)
	{
		holder = curr->wait_on_lock->holder; // holder를 업데이트
		if( holder->priority > t->priority){
			break;
		}
		holder->priority = t->priority; // 홀더의 우선순위를 상속해주는 쓰레드의 우선순위로 업데이트
		curr = holder; // curr를 초기화
		depth++; 
	}
}

/* 락을 얻은 쓰레드가 */
void update_donations_list(struct list *waiters){
	struct list_elem *e, *d_e; // waiters list를 순회할 elem과 d_elem
	struct thread *curr; // 현재 실행중인 쓰레드 락을 가지고있는 쓰레드

	if(list_empty(waiters)) // 대기중인 쓰레드가 없다면 return
		return;
	
	curr = thread_current(); // 현재 실행중인 쓰레드 지정

	/* waiter 리스트를 순회하며 curr쓰레드의 donations리스트에 d_elem 삽입 */
	for (e = list_begin (waiters); e != list_end (waiters); e = list_next (e)){
		d_e = &list_entry(e, struct thread, elem)->d_elem;
		list_push_front(&curr->donations, d_e);
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
	t->origin_priority = priority; // 원래의 우선순위 설정
	list_init(&t->donations); // 기부해준 쓰레드들을 저장할 쓰레드 초기화
	t->wait_on_lock = NULL; // 초기화
	t->recent_cpu = 0; //최근에 cpu를 얼마나 사용했는지 나타내는 변수 초기화
	t->nice = 0; // 다른쓰레드들에게 얼마나 양보를 해주는지 나타내는 변수 nice 초기화
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

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			ASSERT (curr != next);
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

void thread_sleep(int64_t end_tick){
	enum intr_level old_level;
	struct thread *cur = thread_current();

	ASSERT(!intr_context());
	ASSERT(cur != idle_thread);

	old_level = intr_disable();
	cur->wakeup_tick = end_tick; // 쓰레드에 종료틱 설정

	lock_acquire(&sleep_lock); // 슬립리스트는 공유 변수이니까 락 설정

	list_insert_ordered(&sleep_list, &cur->elem, tick_less, NULL); // 슬립리스트에 종료틱 오름차순으로 삽입
	lock_release(&sleep_lock); // 슬립락 해제

	thread_block(); // 현재 쓰레드 블록

	intr_set_level(old_level);
}

void thread_check_sleep_list(){
	enum intr_level old_level;
	struct thread *t;
	old_level = intr_disable();
	int64_t ticks = timer_ticks();
	// lock_acquire(&sleep_lock); // 인터럽트 컨텍스트에는 락을 걸면 안됨
    while (!list_empty(&sleep_list)) { // 슬립 리스트에서 꺠울 쓰레드 탐색
        t = list_entry(list_front(&sleep_list), struct thread, elem); // 슬립 리스트 맨 앞 쓰레드 조회
        if (t->wakeup_tick > ticks) { // 쓰레드가 현재 글로벌 틱보다 크다면 탐색 종료
            break;
        }
        list_pop_front(&sleep_list); // 쓰레드가 현재 글로벌 틱보다 작거나 같다면 슬립 리스트에서 제거
        thread_unblock(t); // 해당 쓰레드 언블록
    }
	// lock_release(&sleep_lock);
	intr_set_level(old_level);
}

/* 쓰레드 wakeup_tick 오름차순 정렬 함수*/
bool
tick_less (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  
  return a->wakeup_tick < b->wakeup_tick;
}

/* 우선순위 내림차순으로 insert_ordered */
bool
priority_more (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, elem);
  const struct thread *b = list_entry (b_, struct thread, elem);
  return a->priority > b->priority;
}

/* 도네이션 리스트에서 우선순위가 가장큰 쓰레드를 뽑아내는 list_max 비교함수 */
bool
donation_priority_more (const struct list_elem *a_, const struct list_elem *b_,
            void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct thread, d_elem);
  const struct thread *b = list_entry (b_, struct thread, d_elem);
  return a->priority < b->priority;
}