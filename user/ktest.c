#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


void 
meminfo(char *s)
{
  printf("%s: freemem=%d, used4k=%d, used2m=%d\n", 
      s, kcall(KC_FREEMEM), kcall(KC_USED4K), kcall(KC_USED2M));
}

void
main(int argc, char *argv[])
{
  void *pa;

  meminfo("Init");
  pa = ktest(KT_KALLOC, 0);
  meminfo("After kalloc()");
  ktest(KT_KFREE, pa);
  meminfo("After kfree()");
  pa = ktest(KT_KALLOC_HUGE, 0);
  meminfo("After kalloc_huge()");
  ktest(KT_KFREE_HUGE, pa);
  meminfo("After kfree_huge()");
  return;
}
