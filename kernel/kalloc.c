// kernel/kalloc.c

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

struct {
  struct spinlock lock;
  int refcnt[PHYSTOP / PGSIZE];
} pref;

static int
pa2idx(uint64 pa)
{
  return pa / PGSIZE;
}

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&pref.lock, "pref");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;

  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE){
    acquire(&pref.lock);
    pref.refcnt[pa2idx((uint64)p)] = 1;
    release(&pref.lock);
    kfree(p);
  }
}

void
kaddref(uint64 pa)
{
  acquire(&pref.lock);
  pref.refcnt[pa2idx(pa)]++;
  release(&pref.lock);
}

int
kgetref(uint64 pa)
{
  int n;
  acquire(&pref.lock);
  n = pref.refcnt[pa2idx(pa)];
  release(&pref.lock);
  return n;
}

void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&pref.lock);
  if(pref.refcnt[pa2idx((uint64)pa)] < 1)
    panic("kfree ref");
  pref.refcnt[pa2idx((uint64)pa)]--;
  if(pref.refcnt[pa2idx((uint64)pa)] > 0){
    release(&pref.lock);
    return;
  }
  release(&pref.lock);

  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r){
    memset((char*)r, 5, PGSIZE);
    acquire(&pref.lock);
    pref.refcnt[pa2idx((uint64)r)] = 1;
    release(&pref.lock);
  }

  return (void*)r;
}