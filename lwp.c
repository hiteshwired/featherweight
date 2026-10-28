#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include "lwp.h"
#include "rr.h"

#define DEFAULT_STACK_SIZE (8 * 1024 * 1024)   //8mb

#define all_next lib_one
#define q_next   lib_two

static scheduler sched = &rr_publish;   //default round robin
static thread current = NULL;           //running lwp
static tid_t next_tid = 1;              // NO_THREAD (0) is never usedd
static thread all_threads = NULL;       //unreaped threds

//FIFO of terminated threads that have not been waited for
static thread term_head = NULL, term_tail = NULL;
//FIFO of threads blocked in lwp_wait()
static thread wait_head = NULL, wait_tail = NULL;

//append t to the FIFO described by *head and *tail
static void enqueue(thread *head, thread *tail, thread t) {
  t->q_next = NULL;
  if ( *tail )
    (*tail)->q_next = t;
  else
    *head = t;
  *tail = t;
}

//remove and return the oldest thread in the FIFO, or NULL if empty
static thread dequeue(thread *head, thread *tail) {
  thread t = *head;
  if ( t ) {
    *head = t->q_next;
    if ( !*head )
      *tail = NULL;
    t->q_next = NULL;
  }
  return t;
}

//remove t from the list of all threads
static void unlink_thread(thread t) {
  thread *p;
  for ( p = &all_threads; *p; p = &(*p)->all_next ) {
    if ( *p == t ) {
      *p = t->all_next;
      return;
    }
  }
}

// Stack size: RLIMIT_STACK if there is  one, else 8MB, rounded up to a multiple of the page size
static size_t stack_size(void) {
  struct rlimit rl;
  long page;
  size_t size;

  if ( getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY
       && rl.rlim_cur > 0 )
    size = rl.rlim_cur;
  else
    size = DEFAULT_STACK_SIZE;

  page = sysconf(_SC_PAGE_SIZE);
  if ( page <= 0 )
    page = 4096;
  return ((size + page - 1) / page) * page;
}

// new threads start here, Calling lwp_exit() with the function's return value turns that value into the termination status
static void lwp_wrap(lwpfun fun, void *arg) {
  int rval;
  rval = fun(arg);
  lwp_exit(rval);
}

tid_t lwp_create(lwpfun function, void *argument) {
  thread t;
  unsigned long *sp;

  t = malloc(sizeof(context));
  if ( !t )
    return NO_THREAD;

  t->stacksize = stack_size();
  t->stack = mmap(NULL, t->stacksize, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
  if ( t->stack == MAP_FAILED ) {
    free(t);
    return NO_THREAD;
  }

/* Set up the stack so swap_rfiles() returns into lwp_wrap.
   The fake stack frame maintains 16-byte alignment.
   lwp_wrap never returns, so its return address is unused.
 */
  sp = t->stack + t->stacksize / sizeof(unsigned long);
  sp[-1] = 0;
  sp[-2] = (unsigned long)lwp_wrap;
  sp[-3] = 0;

  //lwp_wrap(function, argument): first two args go in %rdi and %rsi
  t->state.rdi = (unsigned long)function;
  t->state.rsi = (unsigned long)argument;
  t->state.rbp = (unsigned long)(sp - 3);
  t->state.rsp = (unsigned long)(sp - 3);
  t->state.fxsave = FPU_INIT;

  t->tid = next_tid++;
  t->status = MKTERMSTAT(LWP_LIVE, 0);
  t->exited = NULL;
  t->q_next = NULL;

  t->all_next = all_threads;
  all_threads = t;

  sched->admit(t);
  return t->tid;
}

void lwp_start(void) {
  thread t;

  if ( current )           
    return;

  t = malloc(sizeof(context));
  if ( !t ) {
    perror("lwp_start");
    return;
  }

  t->tid = next_tid++;
  t->stack = NULL;
  t->stacksize = 0;
  t->status = MKTERMSTAT(LWP_LIVE, 0);
  t->exited = NULL;
  t->q_next = NULL;

  t->all_next = all_threads;
  all_threads = t;

  current = t;
  sched->admit(t);
  lwp_yield();
}

void lwp_yield(void) {
  thread old = current;
  thread nxt;

  if ( !old )                   
    return;

  nxt = sched->next();
  if ( !nxt ) {
    //nothing left to run, exit with caller status
    exit(LWPTERMSTAT(old->status));
  }

  current = nxt;
  swap_rfiles(&old->state, &nxt->state);
}

void lwp_exit(int exitval) {
  thread waiter;

  if ( !current )               // not an lwp end the process
    exit(LWPTERMSTAT(exitval));

  current->status = MKTERMSTAT(LWP_TERM, exitval);
  sched->remove(current);

  waiter = dequeue(&wait_head, &wait_tail);
  if ( waiter ) {
    waiter->exited = current;
    sched->admit(waiter);
  } else {
    enqueue(&term_head, &term_tail, current);
  }

  lwp_yield();
}

tid_t lwp_wait(int *status) {
  thread self = current;
  thread dead;
  tid_t tid;

  dead = dequeue(&term_head, &term_tail);
  if ( !dead ) {
    if ( !self || sched->qlen() <= 1 )
      return NO_THREAD;

    //block until lwp_exit() gives a terminated thread
    self->exited = NULL;
    sched->remove(self);
    enqueue(&wait_head, &wait_tail, self);
    lwp_yield();
    dead = self->exited;
    self->exited = NULL;
  }

  tid = dead->tid;
  if ( status )
    *status = dead->status;

  unlink_thread(dead);
  if ( dead->stack )            //never unmap the original thread's stack
    munmap(dead->stack, dead->stacksize);
  free(dead);
  return tid;
}

tid_t lwp_gettid(void) {
  return current ? current->tid : NO_THREAD;
}

thread tid2thread(tid_t tid) {
  thread t;
  if ( tid == NO_THREAD )
    return NULL;
  for ( t = all_threads; t; t = t->all_next ) {
    if ( t->tid == tid )
      return t;
  }
  return NULL;
}

void lwp_set_scheduler(scheduler fun) {
  scheduler old = sched;
  thread t;

  if ( !fun )
    fun = RoundRobin;
  if ( fun == old )
    return;

  if ( fun->init )
    fun->init();

  //move every runnable thread over, in the old scheduler's next() order
  while ( (t = old->next()) != NULL ) {
    old->remove(t);
    fun->admit(t);
  }

  if ( old->shutdown )
    old->shutdown();

  sched = fun;
}

scheduler lwp_get_scheduler(void) {
  return sched;
}
