#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


void tmain(void *arg)
{
  char *args[] = {"ls", "/", 0};

  exec("ls", args);
}

void
main(int argc, char *argv[])
{
  int pid;
  int ret = 999;

  if ((pid = fork()) == 0)
  {
    sthread_create(tmain, 0);
    while (1);
  }
  wait(&ret);
  printf("ret = %d\n", ret);
}
