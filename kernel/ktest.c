#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

#ifdef SNU
uint64
sys_ktest(void)
{
  int n;
  uint64 pa;

  argint(0, &n);
  argaddr(1, &pa);
  switch (n)
  {
    case KT_KALLOC:       return (uint64) kalloc();
    case KT_KFREE:        kfree((void *)pa); return 0;
    case KT_KALLOC_HUGE:  return (uint64) kalloc_huge();
    case KT_KFREE_HUGE:   kfree_huge((void *)pa); return 0;
    default:              return 0;
  }
}
#endif

