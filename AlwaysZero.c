#include <lwp.h>
#include <stdlib.h>
#include <stdio.h>
#include "schedulers.h"

static thread qhead=NULL;
#define tlist_next lib_one
#define tlist_prev lib_two
#define tnext sched_one
#define tprev sched_two
static thread lwp_tlist;
static int len=0;

static void s_admit(thread new) {
  len++;
  /* add to queue */
  if ( qhead ) {
    new->tnext = qhead;
    new->tprev = qhead->tprev;
    new->tprev->tnext = new;
    qhead->tprev = new;
  } else {
    qhead = new;
    qhead->tnext = new;
    qhead->tprev = new;
  }


  /* add to global thread list */
  new->tlist_next = lwp_tlist;
  lwp_tlist = new;
}

static void s_remove(thread victim) {
  context dummy;
  thread l;

  len--;

  dummy.tlist_next = lwp_tlist;

  for(l=&dummy; l->tlist_next && l->tlist_next != victim ; l=l->tlist_next )
    /* dum dee dum */;

  /* if we found it in tlist */
  if ( l->tlist_next ) {
    /* cut out of lwp_tlist */
    l->tlist_next = l->tlist_next->tlist_next;
    lwp_tlist = dummy.tlist_next;

    /* cut out of queue */
    victim->tprev->tnext = victim->tnext;
    victim->tnext->tprev = victim->tprev;
    /* what if it were qhead? */
    if ( victim == qhead ) {
      if ( victim->tnext != victim)
        qhead = victim->tnext;
      else
        qhead = NULL;
    }
  }
}

static thread s_next() {
  thread res;

  if ( qhead )
    res = qhead;
  else
    res = NO_THREAD;

  return res;
}

static int s_qlen(void) {
  /* return number of ready threads */
  return len;
}

static struct scheduler publish = {NULL,NULL,s_admit,s_remove,s_next,s_qlen};
scheduler AlwaysZero=&publish;

/*********************************************************/
__attribute__ ((unused))
void az_dp() {
  thread l;
  if ( !qhead )
    fprintf(stderr,"  AZ qhead is NULL\n");
  else {
    fprintf(stderr,"  AZ queue:\n");
    l = qhead;
    do {
      fprintf(stderr,"  (tid=%lu)\n", l->tid);
      l=l->tnext;
    } while ( l != qhead ) ;
    fprintf(stderr,"\n");
  }
}

