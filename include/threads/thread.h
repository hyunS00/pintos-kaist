#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/fixed_point.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
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

#define USERPROG // 유저프로그램 모드 활성화
/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
#ifdef USERPROG
	/* Owned by userprog/process.c. */
	/* pml4는 "Page Map Level 4"를 나타내는 약어다
	 * x86-64 아키텍처에서는 4단계 페이지 테이블 구조를 사용하여 가상 주소를 물리 주소로 변환한다
	 * 이 중에서 가장 상위 레벨이 바로 PML4입니다.*/
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
	int64_t wakeup_tick; // 슬립 쓰레드 시간되면 깨우는 틱
	int origin_priority; // 실제 오리진 우선순위
	struct lock *wait_on_lock; // 대기중인 락
	struct list donations; // 이 쓰레드에 우선순위를 기부하는 쓰레드들의 리스트
	struct list_elem d_elem; // donations리스트의 elem
	struct list_elem allelem; // all_list에 저장될 elem
	int nice; // 다른 쓰레드들에게 얼마나 양보를 해주는지 나타내는 변수 -20 ~ 20 을 가짐 음수이면 우선순위가 높아짐 양수이면 우선순위가 낮아짐
	real recent_cpu; // 이 쓰레드가 최근에 cpu를 얼마나 사용했는지 나타내는 변수 현재 쓰레드가 많이 running할 수록 값이 낮아짐
	int exit_status;
};

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

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void thread_set_wakeup_tick (int64_t tick);
int64_t thread_get_wakeup_tick (void);

void do_iret (struct intr_frame *tf);

void thread_sleep(int64_t end_tick);
void thread_check_sleep_list();

/* 우선순위를 기부 */
void donate_priority(struct thread *holder, struct thread *receiver);
/* 기부받은 도네이션 제거 */
void donation_remove(struct lock *lock);
/* 현재 우선순위를 origin priority업데이트 */
void donation_update_priority(struct thread *t);
/* 연쇄적인 priority chain priority 업데이트 */
void donate_priority_nested(struct thread *t);
/* donations_list 업데이트 */
void update_donations_list(struct list *waiters);

/* 1초마다 recent_cpu를 새 값으로 업데이트 */
void update_recent_cpu();

/* 매 tick마다 recent_cpu를 1씩 증가 */
void increase_recent_cpu();

/* 4tick마다 모든 쓰레드의 우선순위 업데이트 */
void update_priority();


/* 우선순위 내림차순 정렬*/
bool priority_more (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);

#endif /* threads/thread.h */
