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

static bool
cmp_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
    struct thread *t_a = list_entry(a, struct thread, elem);
    struct thread *t_b = list_entry(b, struct thread, elem);
    return t_a->priority > t_b->priority;  // 우선순위가 높은 것이 앞에 오도록
}

static bool
cmp_sema_priority(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
    struct semaphore *sema_a = list_entry(a, struct semaphore, elem);
    struct semaphore *sema_b = list_entry(b, struct semaphore, elem);

    if (list_empty(&sema_a->waiters)) {
        return false;
    }
    if (list_empty(&sema_b->waiters)) {
        return true;
    }

    struct thread *t_a = list_entry(list_front(&sema_a->waiters), struct thread, elem);
    struct thread *t_b = list_entry(list_front(&sema_b->waiters), struct thread, elem);

    return t_a->priority > t_b->priority;
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
    enum intr_level old_level;

    ASSERT (sema != NULL);
    ASSERT (!intr_context ());

    old_level = intr_disable ();
    while (sema->value == 0) {
        // 우선순위에 따라 정렬된 순서로 대기 리스트에 삽입
        list_insert_ordered(&sema->waiters, &thread_current()->elem, cmp_priority, NULL);
		list_sort(&sema->waiters, cmp_priority, NULL);
        thread_block ();
    }
    sema->value--;
    intr_set_level (old_level);

}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
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
void
sema_up (struct semaphore *sema) {
    enum intr_level old_level;
    struct thread *t = NULL;

    ASSERT(sema != NULL);

    old_level = intr_disable();

    if (!list_empty(&sema->waiters)) {
        list_sort(&sema->waiters, cmp_priority, NULL);  // 대기 리스트 정렬
        t = list_entry(list_pop_front(&sema->waiters), struct thread, elem);
        thread_unblock(t);
    }

    sema->value++;
    intr_set_level(old_level);

    // 언블록된 스레드의 우선순위가 현재 스레드보다 높으면 CPU를 양보
    if (t != NULL && t->priority > thread_current()->priority) {
        thread_yield();
    }
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
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

void wait_on_lock(void) {
    struct thread *cur = thread_current();
    struct lock *lock = cur->waiting_lock;

    /* 대기 중인 락 소유자가 있는 동안 우선순위를 기부 */
    while (lock != NULL && lock->holder != NULL) {
        struct thread *holder = lock->holder;

        // 현재 스레드의 우선순위가 락 소유자의 우선순위보다 높은 경우 기부
        if (holder->priority < cur->priority) {
            holder->priority = cur->priority;

            // 기부 후 donations 리스트 다시 정렬
            list_sort(&holder->donations, cmp_priority, NULL);
        }

        // 다음 대기 중인 락으로 이동 (재귀적으로 기부)
        lock = holder->waiting_lock;
    }
}


/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void 
lock_acquire(struct lock *lock) {
    ASSERT (lock != NULL);
    ASSERT (!intr_context ());
    ASSERT (!lock_held_by_current_thread (lock));

    struct thread *cur = thread_current();
    if (lock->holder != NULL) {
        cur->waiting_lock = lock;
        list_insert_ordered(&lock->holder->donations, &cur->d_elem, cmp_priority, NULL);
        wait_on_lock();  // 우선순위 기부 수행
    }
    sema_down(&lock->semaphore);
    cur->waiting_lock = NULL;
    lock->holder = cur;
}



/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
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
void 
lock_release(struct lock *lock) {
    ASSERT(lock != NULL);
    ASSERT(lock_held_by_current_thread(lock));

    struct thread *cur = thread_current();

    // 우선순위 기부된 스레드 목록에서 현재 락에 대해 대기 중인 스레드들을 삭제
    for (struct list_elem *e = list_begin(&cur->donations); e != list_end(&cur->donations); ) {
        struct thread *t = list_entry(e, struct thread, d_elem);
        if (t->waiting_lock == lock) {
            e = list_remove(e);  // 락을 해제할 때 해당 기부 항목도 삭제
        } else {
            e = list_next(e);
        }
    }

    // 현재 스레드의 우선순위를 기본 우선순위로 복원
    cur->priority = cur->base_priority;

    // 현재 스레드의 donations 리스트가 비어있지 않다면, 가장 높은 우선순위를 다시 설정
    if (!list_empty(&cur->donations)) {
        struct thread *highest_donation = list_entry(list_front(&cur->donations), struct thread, d_elem);
        if (highest_donation->priority > cur->priority) {
            cur->priority = highest_donation->priority;
        }
    }

    // 락을 해제하고 세마포어 업
    lock->holder = NULL;
    sema_up(&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}


/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
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
void
cond_wait (struct condition *cond, struct lock *lock) {
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));

    struct semaphore waiter;
    sema_init(&waiter, 0);

    enum intr_level old_level = intr_disable();  // 인터럽트를 비활성화
    list_insert_ordered(&cond->waiters, &waiter.elem, cmp_priority, NULL);  // 우선순위 정렬 삽입
    intr_set_level(old_level);  // 인터럽트를 복원

    lock_release(lock);
    sema_down(&waiter);
    lock_acquire(lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
    ASSERT(cond != NULL);
    ASSERT(lock != NULL);
    ASSERT(!intr_context());
    ASSERT(lock_held_by_current_thread(lock));

    if (!list_empty(&cond->waiters)) {
        // 인터럽트를 비활성화하고, 조건 변수의 waiters 리스트를 정렬
        enum intr_level old_level = intr_disable();

        // cond->waiters 리스트의 각 세마포어의 대기 리스트에서 가장 높은 우선순위를 가진 스레드 기준으로 정렬
        list_sort(&cond->waiters, cmp_sema_priority, NULL);

        // 정렬된 대기 리스트에서 가장 높은 우선순위를 가진 세마포어를 선택
        struct semaphore *sema = list_entry(list_pop_front(&cond->waiters), struct semaphore, elem);
        
        // 해당 세마포어의 대기 중인 스레드를 깨움
        sema_up(sema);

        intr_set_level(old_level); // 인터럽트를 복원
    }
}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
