#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

#ifdef SNU
uint64
sys_kcall(void)
{
  int n;
  uint64 ret;

  argint(0, &n);
  acquire(&memstat_lock);
  switch (n)
  {
    case KC_FREEMEM:    ret = freemem; break;
    case KC_USED4K:     ret = used4k; break;
    case KC_USED2M:     ret = used2m; break;
    case KC_PF:         ret =  pagefaults; break;
    default:            ret = -1;
  }
  release(&memstat_lock);
  return ret;
}

uint64
sys_mmap(void)
{
  void *addr;
  uint64 addr_value;
  int length, prot, flags;
  argaddr(0, &addr_value);
  addr = (void *)addr_value;
  argint(1, &length);
  argint(2, &prot);
  argint(3, &flags);

  return (uint64)mmap(addr, length, prot, flags);
}

uint64
sys_munmap(void)
{
  void *addr;
  uint64 addr_value;
  argaddr(0, &addr_value);
  addr = (void *)addr_value;

  return munmap(addr);
}
#endif
