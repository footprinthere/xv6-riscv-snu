#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void tmain(void *arg)
{
  int t = (int)(long) arg;

  sleep(t);
  printf("Thread %d is exiting\n", sthread_self());
  sthread_exit(0);
}

void
main(int argc, char *argv[])
{
  sthread_create(tmain, (void *)30);
  sthread_create(tmain, (void *)10);
  sthread_create(tmain, (void *)20);
  printf("Thread %d is exiting\n", sthread_self());
  sthread_exit(0);
}
