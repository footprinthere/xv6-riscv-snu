#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void test_private_chages(void)
{
  uint64 p = (uint64) 0x100000000ULL;
  void *pt;

  pt = mmap((void *)p, 1000, PROT_WRITE, MAP_PRIVATE);
  printf("pt : %p\n", pt);
  printf("pid %d: %x\n", getpid(), *(int *)pt); // 0

  if (fork() == 0) {
    printf("pid %d: %x\n", getpid(), *(int *)pt); // 0
    *(int *)pt = 0x900dbeef;
    printf("pid %d: %x\n", getpid(), *(int *)pt); // 900dbeef
    exit(0);
  }

  wait(0);
  printf("pid %d: %x\n", getpid(), *(int *)pt); // 0
  *(int *)pt = 0x555555;
  printf("pid %d: %x\n", getpid(), *(int *)pt); // 555555

  if (fork() == 0) {
    printf("pid %d: %x\n", getpid(), *(int *)pt); // 555555
    *(int *)pt = 0x900dbeef;
    printf("pid %d: %x\n", getpid(), *(int *)pt); // 900dbeef
    exit(0);
  }

  wait(0);
  printf("pid %d: %x\n", getpid(), *(int *)pt); // 555555
  *(int *)pt = 0x666666;
  printf("pid %d: %x\n", getpid(), *(int *)pt); // 666666

  munmap(pt);

  printf("pid %d: %x\n", getpid(), *(int *)pt); // error
  printf("successful\n");
  return;
}

void test_shared_chages(void)
{
  uint64 p = (uint64) 0x100000000ULL;
  void *pt;

  pt = mmap((void *)p, 1000, PROT_WRITE, MAP_SHARED);
  printf("pt : %p\n", pt);
  printf("pid %d: %x\n", getpid(), *(int *)pt); // 0
  *(int *)pt = 0xdeadbeef;
  printf("pid %d: %x\n", getpid(), *(int *)pt); // deadbeef

  if (fork() == 0) {
    printf("pid %d: %x\n", getpid(), *(int *)pt); // deadbeef
    *(int *)pt = 0x900dbeef;
    printf("pid %d: %x\n", getpid(), *(int *)pt); // 900dbeef
    exit(0);
  }

  wait(0);
  printf("pid %d: %x\n", getpid(), *(int *)pt); // 900dbeef
  *(int *)pt = 0x555555;
  printf("pid %d: %x\n", getpid(), *(int *)pt); // 555555
  munmap(pt);
  return;
}

void test_cow(void)
{
  uint64 p = (uint64) 0x100000000ULL;
  void *pt;

  pt = mmap((void *)p, 100, PROT_WRITE, MAP_PRIVATE);
  *(int *)pt = 0xdeadbeef;
  printf("pid %d: %x\n", getpid(), *(int *)pt);

  if (fork() == 0) {
    *(int *)pt = 0x900dbeef;
    printf("pid %d: %x\n", getpid(), *(int *)pt);
    exit(0);
  }

  wait(0);
  printf("pid %d: %x\n", getpid(), *(int *)pt);
  munmap(pt);
  return;
}

void test_shared_ro(void)
{
  uint64 p = (uint64) 0x100000000ULL;
  void *pt;

  pt = mmap((void *)p, 100, PROT_READ, MAP_SHARED);
  printf("pt : %p\n", pt);
  printf("pid %d: %x\n", getpid(), *(int *)pt);
  *(int *)pt = 0xdeadbeef;
  printf("pid %d: %x\n", getpid(), *(int *)pt);
}

void test_write_on_ro(void)
{
  uint64 p = (uint64) 0x100000000ULL;
  void *pt;

  pt = mmap((void *)p, 100, PROT_READ, MAP_PRIVATE);
  printf("pt : %p\n", pt);
  if (fork() == 0) {
    *(int *)pt = 0xdeadbeef;
    printf("pid %d: %x\n", getpid(), *(int *)pt);
    exit(0);
  }
  wait(0);
  printf("pid %d: %x\n", getpid(), *(int *)pt);
  munmap(pt);
  // printf("pid %d: %x\n", getpid(), *(int *)p);

  pt = mmap((void *)p, 100, PROT_READ, MAP_SHARED | MAP_HUGEPAGE);
  printf("pt : %p\n", pt);
  if (fork() == 0) {
    // *(int *)pt = 0xdeadbeef;
    printf("pid %d: %x\n", getpid(), *(int *)pt);
    exit(0);
  }
  wait(0);
  printf("pid %d: %x\n", getpid(), *(int *)pt);
  munmap(pt);
}

void test_forkfork(void)
{
  const int N = 2;

  for(int i = 0; i < N; i++){
    int pid = fork();
    if(pid < 0){
      printf("fork failed");
      exit(1);
    }
    if(pid == 0){
      for(int j = 0; j < 200; j++){
        int pid1 = fork();
        if(pid1 < 0){
          exit(1);
        }
        if(pid1 == 0){
          exit(0);
        }
        wait(0);
      }
      exit(0);
    }
  }

  int xstatus;
  for(int i = 0; i < N; i++){
    wait(&xstatus);
    if(xstatus != 0) {
      printf("fork in child failed");
      exit(1);
    } else {
      printf("success\n");
    }
  }
}

void test_shared(void)
{
  uint64 p_sh = (uint64) 0x100000000ULL;
  uint64 p_pr = (uint64) 0x100800000ULL;

  mmap((void *)p_sh, 100, PROT_WRITE, MAP_SHARED);
  mmap((void *)p_pr, 100, PROT_WRITE, MAP_PRIVATE | MAP_HUGEPAGE);

  // *(int *)p_sh = 0x00001111;
  // printf("pid %d: shared %x\n", getpid(), *(int *)p_sh);
  // *(int *)p_pr = 0x00001111;
  // printf("pid %d: private %x\n", getpid(), *(int *)p_pr);

  printf("shared %p, private(huge) %p\n", p_sh, p_pr);

  if (fork() == 0) {
    // child
    *(int *)p_sh = 0xdeadbeef;
    printf("pid %d: shared %x\n", getpid(), *(int *)p_sh);
    *(int *)p_pr = 0xdeadbeef;
    printf("pid %d: private %x\n", getpid(), *(int *)p_pr);
    exit(0);
  }

  wait(0);
  printf("pid %d: shared %x\n", getpid(), *(int *)p_sh);
  printf("pid %d: private %x\n", getpid(), *(int *)p_pr);
  return;
}

void test_mmap_fail(void)
{
  uint64 va = (uint64) 0x100000000ULL;

  void *p1 = mmap((void *)va, 100, PROT_READ | PROT_WRITE, MAP_PRIVATE);
  printf("p1 %p\n", p1);
  printf("pid %d: value %x\n", getpid(), *(int *)p1);
  
  void *p2 = mmap((void *)va, 100, PROT_READ | PROT_WRITE, MAP_PRIVATE);
  printf("p2 (dup) %p\n", p2);

  void *p3 = mmap((void *)(va - 0x120), 100, PROT_READ | PROT_WRITE, MAP_PRIVATE);
  printf("p3 (unaligned) %p\n", p3);
  
  return;
}

void test_mmap_options(void)
{
  int prot[4] = {0, PROT_READ, PROT_WRITE, PROT_READ | PROT_WRITE};
  int flags[4] = {0, MAP_PRIVATE, MAP_SHARED, MAP_PRIVATE | MAP_SHARED};
  int huge[2] = {0, MAP_HUGEPAGE};

  for (int i=0; i<4; i++) {
    for (int j=0; j<4; j++) {
      for (int k=0; k<2; k++) {
        uint64 va = (uint64) 0x100000000ULL;
        void *p = mmap((void *)va, 100, prot[i], flags[j] | huge[k]);
        printf("* prot %x, flags %x, huge %x, p %p\n", prot[i], flags[j], huge[k], p);
        if (p) {
          printf("munmap\n");
          munmap(p);
        }
        printf("\n");
      }
    }
  }
}

void test_small_then_huge(void)
{
  uint64 va = (uint64) 0x100000000ULL;

  void *p = mmap((void *)va, 100, 0x002, 0x020);
  printf("p %p\n", p);
  printf("pid %d: value %x\n", getpid(), *(int *)p);
  munmap(p);

  void *h = mmap((void *)va, 100, 0x002, 0x120);
  printf("h %p\n", h);
  printf("pid %d: value %x\n", getpid(), *(int *)h);
  munmap(h);
  return;
}

void
main(int argc, char *argv[])
{
  // uint64 p = (uint64) 0x100000000ULL;

  // mmap((void *)p, 100, PROT_WRITE, MAP_PRIVATE);
  // if (fork() == 0)
  // {
  //   if (fork() == 0)
  //   {
  //     *(int *)p = 0x900dbeef;
  //     printf("pid %d: %x\n", getpid(), *(int *)p);
  //     exit(0);
  //   }
  //   wait(0);
  //   printf("pid %d: %x\n", getpid(), *(int *)p);
  //   exit(0);
  // }
  // wait(0);
  // printf("pid %d: %x\n", getpid(), *(int *)p);
  // munmap((void *)p);

  test_private_chages();

  exit(0);
  return;
}
