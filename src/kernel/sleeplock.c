// Sleeping locks

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

void
initsleeplock(struct sleeplock *lk, char *name)
{
  initlock(&lk->lk, "sleep lock");
  lk->name = name;
  lk->locked = 0;
  lk->pid = 0;
}

void
acquiresleep(struct sleeplock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("acquiresleep: no proc");
  acquire(&lk->lk);
  if(lk->locked && lk->pid == p->pid)
    panic("acquiresleep: recursive");
  while (lk->locked) {
    sleep(lk, &lk->lk);
  }
  lk->locked = 1;
  lk->pid = p->pid;
  release(&lk->lk);
}

void
releasesleep(struct sleeplock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("releasesleep: no proc");
  acquire(&lk->lk);
  if(!lk->locked || lk->pid != p->pid)
    panic("releasesleep: not owner");
  lk->locked = 0;
  lk->pid = 0;
  wakeup(lk);
  release(&lk->lk);
}

int
holdingsleep(struct sleeplock *lk)
{
  struct proc *p = myproc();
  int r;
  
  if(p == 0)
    return 0;
  acquire(&lk->lk);
  r = lk->locked && (lk->pid == p->pid);
  release(&lk->lk);
  return r;
}



