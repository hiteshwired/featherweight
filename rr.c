#include <lwp.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <rr.h>

static thread head=NULL;
static int len=0;               /* current queue length */

#define next_thread sched_one
#define prev_thread sched_two


static void rr_admit(thread new) {

  len++;                
  /* add to queue */
  if ( head ) {
    new->next_thread = head;
    new->prev_thread = head->prev_thread;
    new->prev_thread->next_thread = new;
    head->prev_thread = new;
  } else {
    head = new;
    head->next_thread = new;
    head->prev_thread = new;
  }
}

static void rr_remove(thread outgoing) {
  len--;                        
  outgoing->prev_thread->next_thread = outgoing->next_thread;
  outgoing->next_thread->prev_thread = outgoing->prev_thread;

  /* what if it were qhead? */
  if ( outgoing == head ) {
    if ( outgoing->next_thread != outgoing )
      head = outgoing->next_thread;
    else
      head = NULL;
  }
}

static thread rr_next() {
  thread t;

  t = head;
  if ( head ) {
      head = head->next_thread;
  }
  return t;
}

static int rr_qlen(void) {
  return len;
}

struct scheduler rr_publish = {NULL, NULL, rr_admit,rr_remove,rr_next,rr_qlen};
scheduler RoundRobin = &rr_publish;

