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

// Reference count for each physical page
// Indexed by (pa - KERNBASE) / PGSIZE
#define REFCNT_PGN(pa) (((uint64)(pa) - KERNBASE) / PGSIZE)
#define REFCNT_MAX_PGN ((PHYSTOP - KERNBASE) / PGSIZE)
int refcnt[REFCNT_MAX_PGN];
struct spinlock refcnt_lock;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refcnt_lock, "refcnt");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  int pgn = REFCNT_PGN(pa);
  acquire(&refcnt_lock);
  if(refcnt[pgn] > 0)
    refcnt[pgn]--;
  int need_free = (refcnt[pgn] == 0);
  release(&refcnt_lock);

  if(!need_free)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    int pgn = REFCNT_PGN(r);
    acquire(&refcnt_lock);
    refcnt[pgn] = 1;
    release(&refcnt_lock);
  }
  return (void*)r;
}

void
incref(void *pa)
{
  int pgn = REFCNT_PGN(pa);
  acquire(&refcnt_lock);
  refcnt[pgn]++;
  release(&refcnt_lock);
}
