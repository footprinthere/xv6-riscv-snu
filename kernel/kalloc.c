// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct hugepage_entry {
  struct run *freelist;
  int freecount;
  int hugealloced;
  struct spinlock lock;
};

struct hugepage_entry hugepages[MAXHUGEPGS] = {0};

#ifdef SNU
int freemem, used4k, used2m;
struct spinlock memstat_lock;
#endif

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&memstat_lock, "memstat_lock");
  freerange(end, (void*)PHYSTOP);

  acquire(&memstat_lock);
  used4k = (1 << 15) - freemem;
  used2m = 0;
  release(&memstat_lock);

  // 상수 위치에 zero pager 할당
  struct hugepage_entry *zeropage = hugepages + HUGEPGINDEX(ZEROHUGEPG);
  acquire(&zeropage->lock);
  zeropage->hugealloced = TRUE;
  release(&zeropage->lock);
  memset(ZEROHUGEPG, 0, HUGEPGSIZE);

  acquire(&memstat_lock);
  freemem -= PGINHUGEPG;
  used2m += 1;
  release(&memstat_lock);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
#ifndef SNU
void
kfree(void *pa)
{
  struct run *r;

  // align이 안 맞거나 할당 가능 공간 범위를 넘어서면
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  // pa부터 PGSIZE만큼을 전부 1로 채움
  memset(pa, 1, PGSIZE);

  // page를 freelist의 node로 cast
  r = (struct run*)pa;

  acquire(&kmem.lock);
  // freelist의 맨 앞에 페이지 추가
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

#else
void
kfree(void *pa)
{
  struct run *r;
  struct hugepage_entry *hugepage;

  // align이 안 맞거나 할당 가능 공간 범위를 넘어서면
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // TODO: freelist에 이미 있으면 (kalloc 된 적 없으면) fail

  // Fill with junk
  memset(pa, 1, PGSIZE);

  // page를 freelist의 node로 cast
  r = (struct run*)pa;
  
  // 주소에 해당하는 hugepage entry 찾기
  hugepage = hugepages + HUGEPGINDEX(pa);

  // freelist의 맨 앞에 페이지 추가
  acquire(&hugepage->lock);
  r->next = hugepage->freelist;
  hugepage->freelist = r;
  hugepage->freecount++;
  release(&hugepage->lock);

  acquire(&memstat_lock);
  freemem++;
  used4k--;
  release(&memstat_lock);
}
#endif

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
#ifndef SNU
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  // freelist의 맨 앞에 있는 페이지 꺼냄
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}

#else
/*
hugepage->freelist의 맨 앞에서 run 하나 꺼내고 freecount 감소시킴.
hugepage->lock 잡은 상태에서 호출해야 함.
freelist가 비어 있었다면 NULL 반환.
*/
struct run *
_pop_page(struct hugepage_entry *hugepage)
{
  struct run *r;

  if (hugepage->hugealloced)
    panic("_pop_page: hugealloced");

  r = hugepage->freelist;
  if (r) {
    hugepage->freelist = r->next;
    hugepage->freecount--;
  }

  return r;
}

void *
kalloc(void)
{
  struct run *r = NULL;
  struct hugepage_entry *hugepage = NULL;
  struct hugepage_entry *free_hugepage = NULL;
  int i;

  // hugepage 배열 순회
  for (i=0; i<MAXHUGEPGS; i++) {
    hugepage = hugepages + i;

    acquire(&hugepage->lock);
    if (hugepage->hugealloced) {
      // kalloc_huge로 할당됨 -> skip
    } else if (hugepage->freecount == PGINHUGEPG) {
      // 아직 쪼개지지 않은 hugepage -> 일단 기억해두고 skip
      if (!free_hugepage) {
        free_hugepage = hugepage;
      }
    } else if (hugepage->freecount > 0) {
      // 이미 쪼개진 hugepage -> 할당
      r = _pop_page(hugepage);
      release(&hugepage->lock);
      break;
    }
    release(&hugepage->lock);
  }

  // 쪼개진 hugepage가 없었던 경우
  if (!r && free_hugepage) {
    acquire(&free_hugepage->lock);
    r = _pop_page(free_hugepage);
    release(&free_hugepage->lock);
  }

  if (r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    acquire(&memstat_lock);
    freemem--;
    used4k++;
    release(&memstat_lock);
  }
  return (void*)r;
}

void *
kalloc_huge(void)
{
  void *pa = NULL;
  struct hugepage_entry *hugepage = NULL;
  int target = -1;
  int i;

  // hugepage 배열 순회
  for (i=0; i<MAXHUGEPGS; i++) {
    hugepage = hugepages + i;

    acquire(&hugepage->lock);
    if (hugepage->freecount == PGINHUGEPG && !hugepage->hugealloced) {
      hugepage->hugealloced = TRUE;
      release(&hugepage->lock);
      target = i;
      break;
    }
    release(&hugepage->lock);
  }

  if (target > -1) {
    pa = HUGEPAGEADDR(target);
    memset((char*)pa, 5, HUGEPGSIZE); // fill with junk
    acquire(&memstat_lock);
    freemem -= PGINHUGEPG;
    used2m += 1;
    release(&memstat_lock);
  }

  return pa;
}

void 
kfree_huge(void *pa)
{
  struct hugepage_entry *hugepage;

  // align이 안 맞거나 할당 가능 공간 범위를 넘어서면
  if(((uint64)pa % HUGEPGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree_huge");

  // Fill with junk
  memset(pa, 1, HUGEPGSIZE);

  // 주소에 해당하는 hugepage entry 찾기
  hugepage = hugepages + HUGEPGINDEX(pa);

  acquire(&hugepage->lock);
  if (!hugepage->hugealloced) {
    // 할당되지 않은 huge page에 대해 호출되면 panic
    release(&hugepage->lock);
    panic("kfree_huge");
  }
  hugepage->hugealloced = FALSE;
  release(&hugepage->lock);

  acquire(&memstat_lock);
  freemem += PGINHUGEPG;
  used2m -= 1;
  release(&memstat_lock);
}

void *
kalloc_flex(int is_huge)
{
  if (is_huge) {
    return kalloc_huge();
  } else {
    return kalloc();
  }
}

void
kfree_flex(void *pa, int is_huge)
{
  if (is_huge) {
    kfree_huge(pa);
  } else {
    kfree(pa);
  }
}
#endif
