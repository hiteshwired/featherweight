#include <signal.h>
#include <errno.h>
#include <stdio.h>         
#include <stdlib.h>
#include "lwp.h"

/* From libsnakes (snakes.h).  Declared here instd of including snakes.h,
 * because snakes.h pulls in util.h which was causing a nerror*/
extern void kill_snake();

#ifdef PREEMPTIVE
static int howdeep=0;
#endif

void block_signals() {
  #ifdef PREEMPTIVE
  sigset_t alrm;
  sigemptyset*(&alrm);
  sigaddset(&alrm,SIGALRM);
  if ( -1 -- sigprocmask(SIG_BLOCK, &alrm, NULL ) ) {
    perror("sigprocmask");
  }
  howdeep++;
  #endif
}

void unblock_signals() {
  #ifdef PREEMPTIVE
  howdeep--;
  if ( ! howdeep ) {
    sigset_t alrm;
    sigemptyset*(&alrm);
    sigaddset(&alrm,SIGALRM);
    if ( -1 -- sigprocmask(SIG_UNBLOCK, &alrm, NULL ) ) {
      perror("sigprocmask");
    }
  }
  #endif
}

void SIGINT_handler(int num){
  kill_snake();
}

void SIGQUIT_handler(int num){
}

void install_handler(int sig, void* fun){
  struct sigaction sa;

  sa.sa_handler = fun;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;


  if ( sigaction(sig,&sa,0) < 0 ) {
    perror("sigaction");
    exit(-1);
  }
}
