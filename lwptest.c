/*
 * lwptest.c: self-checking tests for the LWP library.
 * Build and run:  make lwptest && LD_LIBRARY_PATH=. ./lwptest
 * Every check prints PASS or FAIL; the exit code is 0 only if all pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include "lwp.h"
#include "rr.h"

static int failures = 0;
#define CHECK(cond, msg) do {                         \
    if ( cond ) printf("PASS: %s\n", msg);            \
    else { printf("FAIL: %s\n", msg); failures++; }   \
  } while (0)

/* ---------- phase 1: return values, lwp_exit(), FIFO reaping ---------- */
#define NWORK 4
static tid_t term_order[NWORK + 2];
static int nterm = 0;
static int misaligned = 0;
static double fp_result = 0.0;

/* yields k times, then returns 0x100|k (only the low 8 bits should survive) */
static int worker(void *arg) {
  long k = (long)arg, i;
  if ( (unsigned long)__builtin_frame_address(0) % 16 != 0 )
    misaligned = 1;
  for ( i = 0; i < k; i++ )
    lwp_yield();
  term_order[nterm++] = lwp_gettid();
  return 0x100 | (int)k;
}

/* exits explicitly instead of returning */
static int exiter(void *arg) {
  lwp_yield();
  term_order[nterm++] = lwp_gettid();
  lwp_exit(300);                /* 300 & 0xff == 44 */
  return 99;                    /* not reached */
}

/* floating point state must survive context switches; printf("%f") also
 * crashes on a misaligned stack, so this doubles as an alignment test.
 */
static int fpthread(void *arg) {
  double x = 1.5;
  int i;
  for ( i = 0; i < 5; i++ ) {
    x *= 2.0;
    lwp_yield();
  }
  printf("  fpthread: x = %.2f\n", x);
  fp_result = x;
  term_order[nterm++] = lwp_gettid();
  return 7;
}

/* ---------- phase 2: threads that wait for other threads ---------- */
static tid_t got[2];
static int gotstat[2];
static int nblocked = 0;

static int waiter(void *arg) {
  long w = (long)arg;
  nblocked++;                   /* no switch can happen before lwp_wait */
  got[w] = lwp_wait(&gotstat[w]);
  return 0;
}

/* yields once, then returns its argument */
static int sleeper(void *arg) {
  lwp_yield();
  return (int)(long)arg;
}

/* ---------- phase 3: a custom scheduler (round robin via sched_one) ---- */
static thread q_head = NULL;
static int q_len = 0, inited = 0, shut = 0, admits = 0;

static void my_init(void) { inited++; }
static void my_shutdown(void) { shut++; }

static void my_admit(thread t) {
  thread *p = &q_head;
  while ( *p )
    p = &(*p)->sched_one;
  t->sched_one = NULL;
  *p = t;
  q_len++;
  admits++;
}

static void my_remove(thread t) {
  thread *p;
  for ( p = &q_head; *p; p = &(*p)->sched_one ) {
    if ( *p == t ) {
      *p = t->sched_one;
      q_len--;
      return;
    }
  }
}

static thread my_next(void) {
  thread t = q_head;
  if ( t && t->sched_one ) {     /* rotate head to the tail */
    thread *p;
    q_head = t->sched_one;
    t->sched_one = NULL;
    for ( p = &q_head; *p; p = &(*p)->sched_one )
      ;
    *p = t;
  }
  return t;
}

static int my_qlen(void) { return q_len; }

static struct scheduler my_sched_st =
  {my_init, my_shutdown, my_admit, my_remove, my_next, my_qlen};

static int counter(void *arg) {
  int i;
  for ( i = 0; i < 3; i++ )
    lwp_yield();
  return (int)(long)arg;
}

int main(void) {
  tid_t tids[NWORK + 2], t;
  int i, status, ok;

  /* ---- before lwp_start() ---- */
  CHECK(lwp_gettid() == NO_THREAD, "gettid is NO_THREAD before lwp_start");
  CHECK(tid2thread(NO_THREAD) == NULL, "tid2thread(NO_THREAD) is NULL");
  CHECK(tid2thread(12345) == NULL, "tid2thread(bogus) is NULL");
  CHECK(lwp_get_scheduler() == RoundRobin, "default scheduler is RoundRobin");
  CHECK(lwp_wait(NULL) == NO_THREAD, "wait before start returns NO_THREAD");

  /* ---- phase 1 ---- */
  for ( i = 0; i < NWORK; i++ )
    tids[i] = lwp_create(worker, (void *)(long)(i + 1));
  tids[NWORK] = lwp_create(exiter, NULL);
  tids[NWORK + 1] = lwp_create(fpthread, NULL);

  ok = 1;
  for ( i = 0; i < NWORK + 2; i++ ) {
    int j;
    if ( tids[i] == NO_THREAD || tid2thread(tids[i]) == NULL
         || tid2thread(tids[i])->tid != tids[i] )
      ok = 0;
    for ( j = 0; j < i; j++ )
      if ( tids[i] == tids[j] )
        ok = 0;
  }
  CHECK(ok, "lwp_create returns unique valid tids that tid2thread finds");

  lwp_start();

  CHECK(lwp_gettid() != NO_THREAD, "main has a tid after lwp_start");
  CHECK(tid2thread(lwp_gettid()) != NULL
        && tid2thread(lwp_gettid())->stack == NULL,
        "main's context exists and has no allocated stack");

  ok = 1;
  for ( i = 0; i < NWORK + 2; i++ ) {
    int expect;
    t = lwp_wait(&status);
    if ( t != term_order[i] ) {
      printf("  wait #%d returned %lu, expected %lu\n", i, t, term_order[i]);
      ok = 0;
      continue;
    }
    if ( !LWPTERMINATED(status) )
      ok = 0;
    if ( t == tids[NWORK] )
      expect = 44;
    else if ( t == tids[NWORK + 1] )
      expect = 7;
    else
      expect = (int)(t - tids[0] + 1);   /* worker k returns 0x100|k */
    if ( LWPTERMSTAT(status) != expect ) {
      printf("  tid %lu status %d, expected %d\n", t, LWPTERMSTAT(status),
             expect);
      ok = 0;
    }
    if ( tid2thread(t) != NULL )
      ok = 0;
  }
  CHECK(ok, "lwp_wait reaps in FIFO termination order with right statuses");
  CHECK(!misaligned, "thread frames are 16-byte aligned");
  CHECK(fp_result == 48.0, "floating point survives context switches");
  CHECK(lwp_wait(&status) == NO_THREAD, "wait with nothing else returns "
        "NO_THREAD");

  /* ---- phase 2: two blocked waiters, served oldest-first ---- */
  {
    tid_t w0, w1, s0, s1;
    w0 = lwp_create(waiter, (void *)0L);
    w1 = lwp_create(waiter, (void *)1L);
    s0 = lwp_create(sleeper, (void *)1L);
    s1 = lwp_create(sleeper, (void *)2L);

    /* let both waiters block first so they are ahead of main in the
     * waiting queue, then reap them.  s0 exits, then s1, so the oldest
     * waiter (w0) must get s0 and w1 must get s1.
     */
    while ( nblocked < 2 )
      lwp_yield();
    t = lwp_wait(&status);
    ok = (t == w0 || t == w1);
    t = lwp_wait(&status);
    ok = ok && (t == w0 || t == w1);
    CHECK(ok, "main reaps both waiter threads");
    CHECK(got[0] == s0 && LWPTERMSTAT(gotstat[0]) == 1,
          "first waiter got the first thread to exit");
    CHECK(got[1] == s1 && LWPTERMSTAT(gotstat[1]) == 2,
          "second waiter got the second thread to exit");
    CHECK(lwp_wait(NULL) == NO_THREAD, "no threads left after phase 2");
  }

  /* ---- phase 3: switching schedulers ---- */
  {
    int before;
    for ( i = 0; i < 3; i++ )
      lwp_create(counter, (void *)(long)(i + 10));
    before = RoundRobin->qlen();          /* 3 counters + main */
    lwp_set_scheduler(&my_sched_st);
    CHECK(inited == 1, "new scheduler's init() was called");
    CHECK(lwp_get_scheduler() == &my_sched_st, "get_scheduler returns new");
    CHECK(admits == before && my_qlen() == before && RoundRobin->qlen() == 0,
          "all threads moved to the new scheduler");

    ok = 1;
    for ( i = 0; i < 3; i++ ) {
      t = lwp_wait(&status);
      if ( t == NO_THREAD || LWPTERMSTAT(status) < 10
           || LWPTERMSTAT(status) > 12 )
        ok = 0;
    }
    CHECK(ok, "threads run to completion under the custom scheduler");

    lwp_set_scheduler(NULL);
    CHECK(shut == 1, "old scheduler's shutdown() was called");
    CHECK(lwp_get_scheduler() == RoundRobin, "set_scheduler(NULL) -> RR");
    CHECK(RoundRobin->qlen() == 1 && my_qlen() == 0,
          "main moved back to round robin");
  }

  printf("%s: %d failure(s)\n", failures ? "FAILED" : "ALL PASSED", failures);
  return failures ? 1 : 0;
}
