#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void tmain(void *arg)
{
  long i = (long) arg;

  sleep(1);
  printf("tid %d: got 0x%x\n", sthread_self(), i);
  sthread_exit(0x900dbeef);
}

int
main(int argc, char *argv[])
{
  int tid;
  int ret;

  tid = sthread_create(tmain, (void *)0xdeadbeef);
  sthread_join(tid, &ret);
  printf("tid %d: got 0x%x\n", sthread_self(), ret);

  exit(0);
}

