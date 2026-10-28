/*
 * lwpedge.c: edge-case tests for the LWP library (complements lwptest.c).
 * Build and run:  make edgetest
 *
 * Covers: stack sizing from RLIMIT_STACK, mmap failure, scheduler edge
 * cases (no-op switch, blocked threads during a switch, next() order,
 * init/shutdown timing), lifecycle edge cases, and the original thread
 * exiting first and being reaped by another LWP.
 *
 * The last phase ends the program via lwp_exit() with no threads left, so
 * the exit code (0 = all passed) also checks lwp_yield()'s "no next thread"
 * path.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>
#include "lwp.h"
#include "rr.h"

static int failures = 0;
#define CHECK(cond, msg) do {                         \
    if ( cond ) printf("PASS: %s\n", msg);            \
    else { printf("FAIL: %s\n", msg); failures++; }   \
  } while (0)
#define SKIP(msg) printf("SKIP: %s\n", msg)

/* ---------------- simple thread bodies ---------------- */
static int ret_arg(void *arg) { return (int)(long)arg; }

static int exit_neg(void *arg) { lwp_exit(-1); return 0; }

static int done_flag = 0;
static int set_flag(void *arg) { done_flag = 1; return 3; }

static int yield3(void *arg) {
  int i;
  for ( i = 0; i < 3; i++ )
    lwp_yield();
  return (int)(long)arg;
}

static tid_t child_tid = NO_THREAD;
static int child_ran = 0;
static int child(void *arg) { child_ran = 1; return 21; }
static int parent(void *arg) {
  child_tid = lwp_create(child, NULL);      /* create from inside an LWP */
  return 20;
}

/* waiter records what it reaped; "blocked" is set just before lwp_wait()
 * so once the creator runs again the waiter is known to be blocked.
 */
static int blocked = 0;
static tid_t wgot = NO_THREAD;
static int wstat = 0;
static int waiter(void *arg) {
  blocked = 1;
  wgot = lwp_wait(&wstat);
  return 0;
}

/* ---------------- recording scheduler (FIFO round robin) ---------------- */
static thread q_head = NULL;
static int q_len = 0, inited = 0, admit_before_init = 0;
static int shut = 0, shut_nonempty = 0;
static tid_t admitted[16];
static int nadmitted = 0;

static void my_init(void) { inited++; }
static void my_shutdown(void) {
  shut++;
  if ( q_len != 0 )
    shut_nonempty = 1;
}
static void my_admit(thread t) {
  thread *p = &q_head;
  if ( !inited )
    admit_before_init = 1;
  if ( nadmitted < 16 )
    admitted[nadmitted++] = t->tid;
  while ( *p )
    p = &(*p)->sched_one;
  t->sched_one = NULL;
  *p = t;
  q_len++;
}
static void my_remove(thread t) {
  thread *p;
  for ( p = &q_head; *p; p = &(*p)->sched_one )
    if ( *p == t ) {
      *p = t->sched_one;
      q_len--;
      return;
    }
}
static thread my_next(void) {
  thread t = q_head, *p;
  if ( t && t->sched_one ) {             /* rotate head to the tail */
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

/* ---------------- final phase: reap the original thread ---------------- */
static tid_t main_tid;
static int reaper(void *arg) {
  int st;
  tid_t t = lwp_wait(&st);
  CHECK(t == main_tid && LWPTERMINATED(st) && LWPTERMSTAT(st) == 7,
        "another LWP reaps the exited original thread with its status");
  CHECK(tid2thread(main_tid) == NULL, "reaped original thread is gone");
  CHECK(lwp_wait(NULL) == NO_THREAD, "reaper alone: wait gives NO_THREAD");
  printf("%s: %d failure(s); exit code should be %d\n",
         failures ? "FAILED" : "ALL PASSED", failures, failures ? 1 : 0);
  return failures ? 1 : 0;   /* last thread: lwp_yield() exits with this */
}

/* is a[0..n-1] a rotation of b[0..n-1]? */
static int is_rotation(tid_t *a, tid_t *b, int n) {
  int s, i, ok;
  for ( s = 0; s < n; s++ ) {
    ok = 1;
    for ( i = 0; i < n; i++ )
      if ( a[i] != b[(s + i) % n] )
        ok = 0;
    if ( ok )
      return 1;
  }
  return 0;
}

int main(void) {
  struct rlimit orig, rl;
  long page = sysconf(_SC_PAGE_SIZE);
  tid_t t, a, b, e, w, c1, c2, early[3];
  int estat[3];                 /* expected status of each early thread */
  int nearly = 0, st, i, ok, before;

  /* ---- A. before lwp_start() ---- */
  lwp_set_scheduler(lwp_get_scheduler());       /* RR -> RR: no-op */
  CHECK(lwp_get_scheduler() == RoundRobin, "set_scheduler(current) no-op");

  getrlimit(RLIMIT_STACK, &orig);

  /* soft limit that is not a page multiple: must round up */
  rl = orig;
  rl.rlim_cur = 1024 * 1024 + 1;
  if ( (orig.rlim_max == RLIM_INFINITY || orig.rlim_max >= rl.rlim_cur)
       && setrlimit(RLIMIT_STACK, &rl) == 0 ) {
    t = lwp_create(ret_arg, (void *)1L);
    setrlimit(RLIMIT_STACK, &orig);
    CHECK(t != NO_THREAD && tid2thread(t)->stacksize % page == 0
          && tid2thread(t)->stacksize >= 1024 * 1024 + 1
          && tid2thread(t)->stacksize < 1024 * 1024 + 1 + page,
          "stack size = soft RLIMIT_STACK rounded up to a page multiple");
    if ( t != NO_THREAD ) { early[nearly] = t; estat[nearly++] = 1; }
  } else
    SKIP("can't set soft RLIMIT_STACK for rounding test");

  /* unlimited soft limit: must fall back to 8MB */
  rl = orig;
  rl.rlim_cur = RLIM_INFINITY;
  if ( orig.rlim_max == RLIM_INFINITY
       && setrlimit(RLIMIT_STACK, &rl) == 0 ) {
    t = lwp_create(ret_arg, (void *)2L);
    setrlimit(RLIMIT_STACK, &orig);
    CHECK(t != NO_THREAD && tid2thread(t)->stacksize == 8 * 1024 * 1024,
          "RLIM_INFINITY stack limit gives an 8MB stack");
    if ( t != NO_THREAD ) { early[nearly] = t; estat[nearly++] = 2; }
  } else
    SKIP("can't set soft RLIMIT_STACK to RLIM_INFINITY");

  /* 2^62-byte (4 EiB) stack: larger than any x86-64 user address space
   * (2^47 with 4-level paging, 2^56 with 5-level), so mmap() must fail
   * whatever the overcommit setting.  Not so large that lwp.c's page
   * round-up overflows.
   */
  rl = orig;
  rl.rlim_cur = 1UL << 62;
  if ( (orig.rlim_max == RLIM_INFINITY || orig.rlim_max >= rl.rlim_cur)
       && setrlimit(RLIMIT_STACK, &rl) == 0 ) {
    before = RoundRobin->qlen();
    t = lwp_create(ret_arg, (void *)9L);
    setrlimit(RLIMIT_STACK, &orig);
    CHECK(t == NO_THREAD, "lwp_create returns NO_THREAD when mmap fails");
    CHECK(RoundRobin->qlen() == before, "failed create admits nothing");
    /* if it somehow succeeded, reap it later so later phases still work */
    if ( t != NO_THREAD ) { early[nearly] = t; estat[nearly++] = 9; }
  } else
    SKIP("can't raise soft RLIMIT_STACK for mmap failure test");

  lwp_start();
  main_tid = lwp_gettid();

  /* reap the stack-size threads (they have run by now) */
  ok = 1;
  for ( i = 0; i < nearly; i++ ) {
    t = lwp_wait(&st);
    if ( t != early[i] || LWPTERMSTAT(st) != estat[i] )
      ok = 0;
  }
  CHECK(ok, "threads created with modified stack sizes run and are reaped");

  /* ---- B. lifecycle edge cases ---- */
  a = lwp_create(exit_neg, NULL);
  b = lwp_create(ret_arg, (void *)256L);
  t = lwp_wait(&st);
  CHECK(t == a && LWPTERMINATED(st) && LWPTERMSTAT(st) == 255,
        "lwp_exit(-1) gives status 255");
  t = lwp_wait(&st);
  CHECK(t == b && LWPTERMINATED(st) && LWPTERMSTAT(st) == 0,
        "returning 256 gives status 0 (still terminated)");

  /* create from inside an LWP */
  a = lwp_create(parent, NULL);
  t = lwp_wait(&st);
  CHECK(t == a && LWPTERMSTAT(st) == 20, "parent thread reaped");
  t = lwp_wait(&st);
  CHECK(child_tid != NO_THREAD && t == child_tid && child_ran
        && LWPTERMSTAT(st) == 21, "thread created by an LWP runs and is reaped");

  /* terminated-but-unreaped threads are still visible; a live one isn't
   * terminated; wait returns a terminated thread even when the caller is
   * the only runnable thread.
   */
  done_flag = 0;
  a = lwp_create(set_flag, NULL);
  CHECK(tid2thread(a) && !LWPTERMINATED(tid2thread(a)->status),
        "new thread's status is live");
  while ( !done_flag )               /* set_flag sets this and exits in */
    lwp_yield();                     /* the same turn, so it's done now */
  CHECK(tid2thread(a) && LWPTERMINATED(tid2thread(a)->status),
        "tid2thread finds a terminated, unreaped thread");
  CHECK(RoundRobin->qlen() == 1, "only main is runnable");
  t = lwp_wait(&st);
  CHECK(t == a && LWPTERMSTAT(st) == 3,
        "wait returns a terminated thread even with qlen() == 1");
  CHECK(tid2thread(a) == NULL, "tid2thread(reaped) is NULL");

  /* a blocked waiter is not runnable: main alone must get NO_THREAD */
  blocked = 0;
  w = lwp_create(waiter, NULL);
  while ( !blocked )
    lwp_yield();
  CHECK(lwp_wait(NULL) == NO_THREAD,
        "wait gives NO_THREAD when the only other thread is blocked");
  e = lwp_create(ret_arg, (void *)5L);
  t = lwp_wait(&st);                /* main queues behind the waiter */
  CHECK(wgot == e && LWPTERMSTAT(wstat) == 5,
        "the earlier waiter gets the first thread to exit");
  CHECK(t == w, "the later waiter (main) gets the next one");

  /* ---- C. scheduler edge cases ---- */
  blocked = 0;
  wgot = NO_THREAD;
  w = lwp_create(waiter, NULL);
  while ( !blocked )
    lwp_yield();                    /* w is now blocked, outside RR */
  c1 = lwp_create(yield3, (void *)11L);
  c2 = lwp_create(yield3, (void *)12L);

  lwp_set_scheduler(&my_sched_st);
  {
    tid_t expect[3];
    expect[0] = main_tid; expect[1] = c1; expect[2] = c2;
    CHECK(nadmitted == 3 && is_rotation(admitted, expect, 3),
          "migration admits runnable threads in RR next() order");
  }
  CHECK(!admit_before_init, "init() runs before any admit()");
  CHECK(RoundRobin->qlen() == 0, "old scheduler is empty after the switch");

  e = lwp_create(ret_arg, (void *)13L);   /* goes to the new scheduler */
  ok = 1;
  for ( i = 0; i < 3; i++ ) {             /* c1, c2, w in some order */
    t = lwp_wait(&st);
    if ( t != c1 && t != c2 && t != w )
      ok = 0;
  }
  CHECK(ok, "threads finish under the new scheduler");
  CHECK(wgot == e, "waiter blocked during the switch is woken later");
  CHECK(RoundRobin->qlen() == 0, "woken waiter was admitted to the new "
        "scheduler, not the old one");

  lwp_set_scheduler(NULL);
  CHECK(shut == 1 && !shut_nonempty, "shutdown() runs after the old "
        "scheduler is emptied");
  CHECK(lwp_get_scheduler() == RoundRobin && RoundRobin->qlen() == 1,
        "set_scheduler(NULL) returns to round robin with main");

  /* ---- D. original thread exits first; another LWP reaps it ---- */
  lwp_create(reaper, NULL);
  lwp_exit(7);
  printf("FAIL: lwp_exit returned\n");
  return 1;
}
