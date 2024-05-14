/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
static bool cond_priority_more (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
/* 	세마포어에 대한 내려가기("P") 연산입니다.
   	세마포어 SEMA의 값이 양수가 될 때까지 대기한 후,
   	그 값을 원자적으로 감소시킵니다.

   	이 함수는 대기(sleep) 상태가 될 수 있으므로
	인터럽트 핸들러 내에서 호출되어서는 안 됩니다.
   	인터럽트가 비활성화된 상태에서 이 함수를 호출할 수는 있지만,
	만약 함수가 대기 상태로 들어가면
   	다음에 스케줄된 스레드가 아마 인터럽트를 다시 활성화할 것입니다.
	이것은 sema_down 함수입니다. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (sema->value == 0) {
		list_insert_ordered(&sema->waiters, &thread_current ()->elem, priority_more, NULL);
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
/* 세마포어의 "P" 연산(내려가기)을 시도하지만,
   세마포어가 이미 0이 아닐 경우에만 수행합니다. 세마포어가
   감소되면 true를 반환하고, 그렇지 않으면 false를 반환합니다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */   
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
/* 세마포어의 "V" 연산(올리기)입니다. SEMA의 값을 증가시키고,
   대기 중인 스레드가 있으면 그 중 하나를 깨웁니다.

   이 함수는 인터럽트 핸들러에서 호출될 수 있습니다. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;
	
	ASSERT (sema != NULL);

	old_level = intr_disable ();
	sema->value++;
	if (!list_empty (&sema->waiters))
		thread_unblock (list_entry (list_pop_front (&sema->waiters),
					struct thread, elem));
	intr_set_level (old_level);
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */

/*락 초기화. 락은 주어진 시간에 최대 한 스레드만이 소유할 수 있습니다. 우리의 락은 "재귀적"이지 않습니다. 즉, 현재 락을 보유하고 있는 스레드가 해당 락을 다시 획득하려고 시도하는 것은 오류입니다.

락은 초기 값이 1인 세마포어의 특화된 형태입니다. 락과 이러한 세마포어
사이의 차이점은 두 가지입니다.
첫째, 세마포어는 값이 1보다 클 수 있지만 락은 한 번에 하나의 스레드만이
소유할 수 있습니다.
둘째, 세마포어는 소유자가 없어 한 스레드가
세마포어를 '내려감(down)' 할 수 있고
다른 스레드가 '올림(up)' 할 수 있지만,
락은 동일한 스레드가 획득하고 해제해야 합니다.
이러한 제한이 과도하다고 느껴질 때는 락 대신
세마포어를 사용하는 것이 좋습니다.*/
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */

   /*락을 획득합니다. 필요한 경우 사용 가능해질 때까지
    슬립 상태로 전환하여 대기합니다.
	현재 스레드에 의해 이미 락이 보유되고 있지 않아야 합니다.

	이 함수는 슬립할 수 있으므로
	인터럽트 핸들러 내에서 호출되어서는 안 됩니다. 
	이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만,
	슬립이 필요하면 인터럽트는 다시 켜질 것입니다.*/
void
lock_acquire (struct lock *lock) {
   struct thread *curr;
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));

   if(lock->holder != NULL){
      printf("홀더: %s\n",lock->holder->name);
      curr = thread_current();
      printf("홀더: %d\n", lock->holder->priority);
      printf("홀더: %d\n", curr->priority);
      if(curr->priority > lock->holder->priority){
         donate_priority(&lock->holder, curr);
      }
   }
	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
   /*락을 시도적으로 획득하고 성공하면 참(true)을, 
   실패하면 거짓(false)을 반환합니다. 
   현재 스레드에 의해 이미 락이 보유되고 있지 않아야 합니다.
   
   이 함수는 슬립하지 않으므로
   인터럽트 핸들러 내에서 호출될 수 있습니다.*/
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
   /*현재 스레드가 소유하고 있는 락을 해제합니다. 
   이것은 lock_release 함수입니다.
   인터럽트 핸들러는 락을 획득할 수 없기 때문에, 
   인터럽트 핸들러 내에서 락을 해제하려는 시도는 의미가 없습니다.*/
void
lock_release (struct lock *lock) {
   enum intr_level old_level;
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

   old_level = intr_disable();
   // remove_donation(lock);
	lock->holder = NULL;
	sema_up (&lock->semaphore);
   intr_set_level(old_level);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
   struct thread *holder
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
/*조건 변수 COND를 초기화합니다. 
조건 변수는 한 코드 부분이 조건을 신호로 알릴 수 있게 해주며, 
협력하는 코드가 이 신호를 받아서 그에 따라 행동할 수 있게 합니다.*/
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */

   /*이 함수는 LOCK을 원자적으로 해제하고 다른 코드에 의해 
   COND가 신호될 때까지 기다립니다. 
   COND가 신호되고 나면, 반환하기 전에 LOCK을 다시 획득합니다. 
   이 함수를 호출하기 전에는 LOCK이 반드시 획득되어 있어야 합니다.
   
   이 함수에 의해 구현된 모니터는 
   'Hoare' 스타일이 아닌 'Mesa' 스타일입니다. 
   즉, 신호 보내기와 받기가 원자적 연산이 아니기 때문에, 
   대개 호출자는 대기가 완료된 후 조건을 다시 확인해야 하며, 
   필요한 경우 다시 대기해야 합니다.
   
   특정 조건 변수는 오직 한 개의 락과만 연관될 수 있지만, 
   하나의 락은 여러 조건 변수와 연관될 수 있습니다. 
   즉, 락에서 조건 변수로의 매핑은 일대다 관계입니다.
   
   이 함수는 수면 상태로 전환될 수 있으므로 
   인터럽트 핸들러 내에서 호출해서는 안 됩니다. 
   이 함수는 인터럽트가 비활성화된 상태에서 호출될 수 있지만, 
   수면이 필요할 경우 인터럽트는 다시 활성화됩니다.*/
void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
   waiter.holder = thread_current();
   list_insert_ordered(&cond->waiters, &waiter.elem, cond_priority_more, NULL);
	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
/* 	COND에 대기 중인 스레드가 있는 경우(LOCK로 보호됨), 
	이 함수는 대기 중인
	스레드 중 하나에 대해 대기를 깨우는 신호를 보냅니다.
	이 함수를 호출하기 전에 LOCK을 보유해야 합니다.

	인터럽트 핸들러는 잠금을 얻을 수 없으므로,
	인터럽트 핸들러 내에서 조건 변수에 대해
	신호를 보내려고 시도하는 것은 의미가 없습니다. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters)){
		sema_up (&list_entry (list_pop_front (&cond->waiters),
					struct semaphore_elem, elem)->semaphore);
   }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */


/*  COND에 대기 중인 모든 스레드를 깨웁니다(LOCK로 보호됨).
	이 함수를 호출하기 전에 LOCK을 보유해야 합니다.

	인터럽트 핸들러는 잠금을 얻을 수 없으므로,
	인터럽트 핸들러 내에서 조건 변수에 대해
	신호를 보내려는 것은 의미가 없습니다. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}

bool
cond_priority_more (const struct list_elem *a_, const struct list_elem *b_, void *aux UNUSED) 
{
  const struct thread *a = list_entry (a_, struct semaphore_elem, elem)->holder;
  const struct thread *b = list_entry (b_, struct semaphore_elem, elem)->holder;
  
  return a->priority > b->priority;
}