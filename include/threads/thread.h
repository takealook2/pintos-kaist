#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
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
	int64_t wakeup_tick;

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

    /* 우선순위 도네이션을 위한 추가 필드들 */
    int base_priority;           // 스레드의 기본 우선순위 (기부받은 우선순위가 없을 때의 우선순위)
    struct lock *waiting_lock;   // 현재 스레드가 대기 중인 락
    struct list donations;       // 현재 스레드에게 기부된 스레드들의 리스트
    struct list_elem d_elem; // 도네이션 리스트에 삽입되는 리스트 엘리먼트

	/* nice, recent_cpu */
	int nice;
	int recent_cpu;
	struct list_elem allelem;


#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

/* thread.h */

/* ... 기존 코드 ... */

/* 새로운 필드 추가: 스레드의 wakeup tick 값 */
int64_t wakeup_tick;

/* 외부에서 접근 가능한 함수 프로토타입 선언 */
void thread_sleep(int64_t ticks);
void thread_wake(int64_t current_ticks);
void update_global_tick(int64_t ticks);
int64_t get_global_tick(void);

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *function, void *aux);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

// 1. MLFQS 관련 기본 함수들
void increase_recent_cpu(void);
void cal_priority(struct thread *t);
void cal_load_avg(void);
void cal_recent_cpu(struct thread *t);

// 2. MLFQS 업데이트 함수들
void update_priority(void);
void update_recent_cpu(void);

// 3. thread 관련 인터페이스 함수들
void thread_set_priority(int);
int thread_get_priority(void);
void thread_set_nice(int);
int thread_get_nice(void);
int thread_get_load_avg(void);
int thread_get_recent_cpu(void);

extern struct list all_list;
extern struct thread *idle_thread;
extern struct list ready_list;

#define F (1 << 14) // 17.14 고정 소수점 형식에서 1에 해당하는 값

#define FP_ADD_INT(x, n) ((x) + ((n) * F))

// n을 고정 소수점으로 변환
#define INT_TO_FP(n) ((n) * F)

// 고정 소수점을 정수로 변환 (0 쪽으로 반올림)
#define FP_TO_INT_ZERO(x) ((x) / F)

// 고정 소수점을 정수로 변환 (가까운 정수로 반올림)
#define FP_TO_INT_NEAREST(x) (((x) >= 0) ? (((x) + F / 2) / F) : (((x) - F / 2) / F))

// 고정 소수점 덧셈 (x + y)
#define FP_ADD(x, y) ((x) + (y))

// 고정 소수점 뺄셈 (x - y)
#define FP_SUB(x, y) ((x) - (y))

// 고정 소수점에서 정수 뺄셈 (x - n)
#define FP_SUB_INT(x, n) ((x) - ((n) * F))

// 고정 소수점 곱셈 (x * y)
#define FP_MUL(x, y) (((int64_t)(x)) * (y) / F)

// 고정 소수점과 정수 곱셈 (x * n)
#define FP_MUL_INT(x, n) ((x) * (n))

// 고정 소수점 나눗셈 (x / y)
#define FP_DIV(x, y) (((int64_t)(x)) * F / (y))

// 고정 소수점과 정수 나눗셈 (x / n)
#define FP_DIV_INT(x, n) ((x) / (n))


#endif /* threads/thread.h */
