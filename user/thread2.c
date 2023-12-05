#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void tmain(void *arg)
{
  sleep(10);
  printf("Thread %d is exiting\n", sthread_self());
  sthread_exit(0);
}

void
main(int argc, char *argv[])
{
  sthread_create(tmain, 0);
  printf("Thread %d is exiting\n", sthread_self());
  sthread_exit(0);
}
