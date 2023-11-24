#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

#ifdef SNU
int pagefaults;

struct vm_area mmap_areas[MMAP_GLOBAL_MAX] = {0};
struct shared_page shared_pages[MAXPGS] = {0};
#endif

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
/*
fork에서 새로운 proc 만들기 위해 호출함.
*/
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  
  for (int i=0; i<MMAP_PROC_MAX; i++) {
    p->mmap[i] = NULL;
  }
  p->mmap_count = 0;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  #ifndef SNU
  if (uvmcopy(p->pagetable, np->pagetable, p->sz) < 0)
  #else
  if (uvmcopy(p, np, p->sz) < 0)
  #endif
  {
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  // unmap mmap-ed areas
  munmap_all();

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    for(p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if(p->state == RUNNABLE) {
        // Switch to chosen process.  It is the process's job
        // to release its lock and then reacquire it
        // before jumping back to us.
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->context, &p->context);

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

/*
process를 killed로 표시.
p->lock 잡고 있으면 안 됨.
*/
void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

#ifdef SNU
/*
addr부터 length만큼 할당
  - 이미 할당되어 있으면 0 반환
  - else 할당하고 addr 그대로 반환

prot 보고 RO / RW 판정 (WO는 없음)
flag에 따라 shared / private 지원

assumption:
  - length <= 64MiB
  - 각 프로세스는 mmap-ed region을 4개까지 가질 수 있음
  - 시스템은 총 64개 mmap-region을 가질 수 있음
*/
void *
mmap(void *addr, int length, int prot, int flags)
{
  uint64 a = (uint64)addr;
  struct proc *p = myproc();
  int is_huge = (flags & MAP_HUGEPAGE) ? TRUE : FALSE;

  // addr가 page-aligned 되어 있지 않으면 NULL 반환
  if (is_huge && a % HUGEPGSIZE != 0)
    return NULL;
  if (!is_huge && a % PGSIZE != 0)
    return NULL;

  acquire(&p->lock);
  int mmap_count = p->mmap_count;
  release(&p->lock);
  if (length > MMAP_MAX_SIZE || mmap_count >= MMAP_PROC_MAX) {
    return NULL;
  }

  // zero page로 연결되는 PTE 생성
  int pte_flags = options_to_flags(prot | flags);
  if (prot & PROT_WRITE) {
    pte_flags |= PTE_R; // W 설정되어 있으면 R 자동 추가
  }
  acquire(&p->lock);
  if (flexmappages(p->pagetable, a, length, NULL, flags & MAP_HUGEPAGE, pte_flags) == -1) {
    release(&p->lock);
    return NULL;
  }
  add_vma(p, a, length, prot | flags, FALSE);
  release(&p->lock);

  return addr;
}

/*
mmap 옵션을 받아 PTE flags로 변환
*/
int
options_to_flags(int options)
{
  int pte_flags = PTE_V | PTE_U;
  if (options & PROT_READ)
    pte_flags |= PTE_R;
  if (options & PROT_WRITE)
    pte_flags |= PTE_W;
  if (options & MAP_SHARED)
    pte_flags |= PTE_SHR;
  return pte_flags;
}

/*
addr 위치에 할당되어 있는 공간 해제
  - 성공 시 0, 실패 시 -1 반환
uvmunmap 구현 참고할 것

해제 후 page frame이 비었으면 kfree

프로세스가 종료될 때는 자동으로 munmap 되어야 함
  - exit, exec
*/
int
munmap(void *addr)
{
  struct proc *p = myproc();
  uint64 va = (uint64)addr;

  acquire(&p->lock);
  struct vm_area *area = get_vma(p, va, TRUE);
  release(&p->lock);
  if (area == NULL) {
    return -1;
  }

  // area의 시작 주소가 아니면 fail (alignment도 보장 가능)
  if (va != area->start) {
    return -1;
  }

  // shared이면 공유하는 다른 process가 있는지 검사
  struct shared_page *shpg;
  int is_idle = TRUE;
  if (area->options & MAP_SHARED) {
    shpg = find_shpg(area->idx, va);
    if (shpg == NULL) {
      // mmap만 하고 실제 할당은 안 했을 때
      is_idle = FALSE;
    } else {
      acquire(&shpg->lock);
      if (--(shpg->ref_count) == 0) {
        // 더 이상 쓰이지 않으면 vm area를 invalid로 표시하고 shared page 초기화
        area->is_valid = FALSE;
        shpg->vma_idx = -1;
        shpg->start_va = 0;
        shpg->pte = 0;
      } else {
        is_idle = FALSE;
      }
      release(&shpg->lock);
    }
  }

  uint64 a = va;
  uint64 last = va + area->length - 1;
  int is_huge = (area->options & MAP_HUGEPAGE) ? TRUE : FALSE;
  if (is_huge) {
    last = HUGEPGROUNDDOWN(last);
  } else {
    last = PGROUNDDOWN(last);
  }

  pte_t *pte;
  uint64 pa;
  while (a <= last) {
    acquire(&p->lock);
    if (is_huge) {
      pte = hugewalk(p->pagetable, a, FALSE);
    } else {
      pte = walk(p->pagetable, a, FALSE);
    }
    release(&p->lock);
    if (pte == NULL || PTE_FLAGS(*pte) == PTE_V) {
      // PTE가 없거나 leaf가 아니면 fail
      return -1;
    }

    // PTE 내용 지워서 map 해제
    pa = PTE2PA(*pte);
    *pte = 0;
    // 더 이상 쓰이지 않으면 kfree
    if (is_idle) {
      kfree_flex((void *)pa, is_huge);
    }

    a += (is_huge) ? HUGEPGSIZE : PGSIZE;
  }

  return 0;
}

/*
현재 process가 mmap으로 할당 받은 모든 공간 해제
*/
int
munmap_all(void)
{
  struct proc *p = myproc();
  struct vm_area *area;
  void *addr;

  for (int i=0; i<MMAP_PROC_MAX; i++) {
    acquire(&p->lock);
    area = p->mmap[i];
    if (area == NULL) {
      release(&p->lock);
      continue;
    }
    addr = (void *)area->start;
    release(&p->lock);

    if (munmap(addr) == -1) {
      return -1;
    }
  }
  return 0;
}

void
pagefault(uint64 scause, uint64 stval)
{
  struct proc *p = myproc();
  pte_t *pte;
  struct vm_area *area;
  int is_huge;

  pagefaults++;
  acquire(&p->lock);

  pte = walkfind(p->pagetable, stval, &is_huge);
  if (pte == NULL) {
    // PTE가 없으면 kill
    release(&p->lock);
    printf("pagefault (PTE not found): pid=%d scause=%d stval=%d\n", p->pid, scause, stval);
    setkilled(p);
    return;
  }

  area = get_vma(p, stval, FALSE);
  if (area == NULL) {
    // mmap area가 아니면 kill
    release(&p->lock);
    printf("pagefault (area not found): pid=%d scause=%d stval=%d\n", p->pid, scause, stval);
    setkilled(p);
    return;
  }

  if (area->options && MAP_SHARED) {
    // shared area
    handle_shared_fault(p, pte, stval, area, is_huge);
    release(&p->lock);
    return;
  } else if (scause == SCAUSE_LOAD) {
    release(&p->lock);
    // shared가 아닌데 load이면 진짜 못 읽는 것
    printf("pagefault (load): pid=%d scause=%d stval=%d\n", p->pid, scause, stval);
    setkilled(p);
    return;
  }

  // 이제부터 전부 store, private
  if (!(area->options & PROT_WRITE)) {
    // not writable이면 kill
    release(&p->lock);
    printf("pagefault (store): pid=%d scause=%d stval=%d\n", p->pid, scause, stval);
    setkilled(p);
    return;
  }

  handle_private_fault(p, pte, stval, area, is_huge);
  release(&p->lock);
}

/*
shared 영역에 대한 store/load fault 처리.
area는 NULL이 아니어야 함.
*/
void
handle_shared_fault
(
  struct proc *p,
  pte_t *pte,
  uint64 va,
  struct vm_area *area,
  int is_huge
)
{
  // load 이면
  uint64 start_va = (is_huge) ? HUGEPGROUNDDOWN(va) : PGROUNDDOWN(va);
  struct shared_page *shpg = find_shpg(area->idx, start_va);
  char *mem;

  if (shpg == NULL) {
    // 최초 시도 -> 새 PP 핼당 필요
    mem = kalloc_flex(is_huge);
    if (mem == NULL) {
      panic("pagefault: kalloc failed\n");
    }

    // 0으로 채우고 권한 설정
    memset(mem, 0, (is_huge) ? HUGEPGSIZE : PGSIZE);
    *pte = PA2PTE(mem) | PTE_V | PTE_U | PTE_R;
    if (area->options & PROT_WRITE) {
      *pte |= PTE_W;
    }

    shpg = get_shpg((uint64)mem);
    acquire(&shpg->lock);
    if (shpg->ref_count > 0) {
      panic("pagefault: shared page already in use");
    }
    shpg->vma_idx = area->idx;
    shpg->start_va = start_va;
    shpg->ref_count++;
    shpg->pte = *pte;
    release(&shpg->lock);
  } else {
    // 이미 존재 -> 동일 위치에 할당
    acquire(&shpg->lock);
    *pte = shpg->pte;
    shpg->ref_count++;
    release(&shpg->lock);
  }
}

/*
private, writable 영역에 대한 store fault 처리.
area는 NULL이 아니어야 함.
*/
void
handle_private_fault
(
  struct proc *p,
  pte_t *pte,
  uint64 va,
  struct vm_area *area,
  int is_huge
)
{
  char *mem = kalloc_flex(is_huge);
  if (mem == NULL) {
    panic("pagefault: kalloc failed\n");
  }

  if (area->needs_cow) {
    // allocated
    area->needs_cow = FALSE;  // TODO: needs_cow 다시 점검해보자
    memmove(mem, (void*)PTE2PA(*pte), (is_huge) ? HUGEPGSIZE : PGSIZE);
  } else {
    // zero-mapped
    memset(mem, 0, (is_huge) ? HUGEPGSIZE : PGSIZE);
  }
  *pte = PA2PTE(mem) | PTE_V | PTE_U | PTE_R | PTE_W;
}

/*
vm area들의 global 배열 중에서
쓰이지 않고 비어 있는 것을 찾아 반환.
없으면 NULL 반환.
*/
struct vm_area *
find_empty_vma(void)
{
  struct vm_area *area;

  for (int i=0; i<MMAP_GLOBAL_MAX; i++) {
    area = mmap_areas + i;
    if (!area->is_valid) {
      area->idx = i;
      return area;
    }
  }
  return NULL;
}

/*
proc의 mmap_area를 순회하며 빈 자리를 찾아 새로운 area 저장.
빈 자리가 없으면 -1 반환.
p->lock 잡은 채로 호출해야 함.
*/
int
add_vma
(
  struct proc *p,
  uint64 start,
  uint64 length,
  int options,
  int needs_cow
)
{
  struct vm_area *area = find_empty_vma();
  if (area == NULL) {
    return -1;
  }

  // p->mmap 중 빈 slot 찾기
  for (int i=0; i<MMAP_PROC_MAX; i++) {
    if (p->mmap[i] == NULL) {
      p->mmap[i] = area;
      
      area->is_valid = TRUE;
      area->start = start;
      area->end = start + length;
      area->length = length;
      area->options = options;
      area->needs_cow = needs_cow;
      p->mmap_count++;
      return 0;
    }
  }
  return -1;
}

/*
이미 있는 vm area를 새로운 process와 동시에 가리키도록 설정.
빈 자리가 없으면 -1 반환.
*/
int
share_vma(struct proc *np, struct vm_area *area) {
  // np->mmap 중 빈 slot 찾기
  for (int i=0; i<MMAP_PROC_MAX; i++) {
    if (np->mmap[i] == NULL || np->mmap[i]->is_valid == FALSE) {
      np->mmap[i] = area;
      np->mmap_count++;
      return 0;
    }
  }
  return -1;
}

/*
proc의 mmap_area를 순회하며 주어진 주소가 포함되는 vm_area 찾아 반환.
해당하는 것이 없으면 NULL 반환.
p->lock 잡은 채로 호출해야 함.
pop=TRUE이면 pointer를 NULL로 고치고 mmap_count 감소시킴.
is_valid를 FALSE로 바꾸는 것은 따로 처리해야 함.
*/
struct vm_area *
get_vma(struct proc *p, uint64 addr, int pop)
{
  struct vm_area *area;

  for (int i=0; i<MMAP_PROC_MAX; i++) {
    area = p->mmap[i];
    if (area == NULL) {
      continue;
    }
    if (area->start <= addr && addr < area->end) {
      if (pop) {
        p->mmap[i] = NULL;
        p->mmap_count--;
      }
      return area;
    }
  }
  return NULL;
}

/*
2^15 배열에서 PA에 맞는 shared page를 반환.
*/
struct shared_page *
get_shpg(uint64 pa)
{
  return shared_pages + PGINDEX(pa);
}

/*
2^15 배열을 순회하며 조건에 맞는 shared page를 찾음.
없으면 NULL 반환.
*/
struct shared_page *
find_shpg(int vma_idx, uint64 start_va)
{
  struct shared_page *shpg;

  for (int i=0; i<MAXPGS; i++) {
    shpg = shared_pages + i;
    acquire(&shpg->lock);
    if (shpg->vma_idx == vma_idx && shpg->start_va == start_va) {
      release(&shpg->lock);
      return shpg;
    }
    release(&shpg->lock);
  }
  return NULL;
}

// FIXME: FOR TEST
void show_pte(pte_t *pte)
{
  printf("----------- PTE: %p\n", pte);
  printf("PTE_V: %d\n", (*pte & PTE_V) != 0);
  printf("PTE_R: %d\n", (*pte & PTE_R) != 0);
  printf("PTE_W: %d\n", (*pte & PTE_W) != 0);
  printf("PTE_SHR: %d\n", (*pte & PTE_SHR) != 0);
  printf("PTE2PA: %p\n", PTE2PA(*pte));
}

void show_vm_areas(struct proc *p)
{
  struct vm_area *area;
  for (int i=0; i<MMAP_PROC_MAX; i++) {
    area = p->mmap[i];
    if (area == NULL) {
      continue;
    }
    printf("----------- vm areas of proc %d\n", p->pid);
    printf("start: %p\n", area->start);
    printf("end: %p\n", area->end);
    printf("length: %d\n", area->length);
    printf("options: %x\n", area->options);
    printf("needs_cow: %d\n", area->needs_cow);
  }
}
#endif
