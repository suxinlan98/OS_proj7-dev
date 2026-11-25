#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

#define PIPESIZE 512

struct pipe {
  struct spinlock lock;
  char data[PIPESIZE];
  uint nread;     // number of bytes read
  uint nwrite;    // number of bytes written
  int readopen;   // read fd is still open
  int writeopen;  // write fd is still open
};

int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *pi;

  pi = 0;
  *f0 = *f1 = 0;
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;
  if((pi = (struct pipe*)kalloc()) == 0)
    goto bad;
  pi->readopen = 1;
  pi->writeopen = 1;
  pi->nwrite = 0;
  pi->nread = 0;
  initlock(&pi->lock, "pipe");
  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->pipe = pi;
  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->pipe = pi;
  return 0;

 bad:
  if(pi)
    kfree((char*)pi);
  if(*f0)
    fileclose(*f0);
  if(*f1)
    fileclose(*f1);
  return -1;
}

void
pipeclose(struct pipe *pi, int writable)
{
  acquire(&pi->lock);
  if(writable){
    pi->writeopen = 0;
    wakeup(&pi->nread);
  } else {
    pi->readopen = 0;
    wakeup(&pi->nwrite);
  }
  if(pi->readopen == 0 && pi->writeopen == 0){
    release(&pi->lock);
    kfree((char*)pi);
  } else
    release(&pi->lock);
}

int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  char kbuf[64];
  int i = 0;
  struct proc *pr = myproc();

  while(i < n){
    int m = n - i;
    int page_rem = PGSIZE - ((addr + i) % PGSIZE);
    if(m > (int)sizeof(kbuf))
      m = sizeof(kbuf);
    if(m > page_rem)
      m = page_rem;

    if(copyin(pr->pagetable, kbuf, addr + i, m) == -1){
      if(i == 0)
        return -1;
      break;
    }

    acquire(&pi->lock);
    for(int j = 0; j < m; j++){
      while(pi->nwrite == pi->nread + PIPESIZE){ //DOC: pipewrite-full
        if(pi->readopen == 0 || killed(pr)){
          release(&pi->lock);
          return -1;
        }
        wakeup(&pi->nread);
        sleep(&pi->nwrite, &pi->lock);
      }
      if(pi->readopen == 0 || killed(pr)){
        release(&pi->lock);
        return -1;
      }
      pi->data[pi->nwrite++ % PIPESIZE] = kbuf[j];
      i++;
    }
    wakeup(&pi->nread);
    release(&pi->lock);
  }

  return i;
}

int
piperead(struct pipe *pi, uint64 addr, int n)
{
  char kbuf[64];
  int tot = 0;
  struct proc *pr = myproc();

  acquire(&pi->lock);
  while(pi->nread == pi->nwrite && pi->writeopen){  //DOC: pipe-empty
    if(killed(pr)){
      release(&pi->lock);
      return -1;
    }
    sleep(&pi->nread, &pi->lock); //DOC: piperead-sleep
  }

  while(tot < n && pi->nread != pi->nwrite){  //DOC: piperead-copy
    int m = n - tot;
    int avail = pi->nwrite - pi->nread;
    int page_rem = PGSIZE - ((addr + tot) % PGSIZE);
    if(m > (int)sizeof(kbuf))
      m = sizeof(kbuf);
    if(m > avail)
      m = avail;
    if(m > page_rem)
      m = page_rem;

    uint start = pi->nread;
    for(int j = 0; j < m; j++)
      kbuf[j] = pi->data[(start + j) % PIPESIZE];
    pi->nread += m;
    release(&pi->lock);

    if(copyout(pr->pagetable, addr + tot, kbuf, m) == -1){
      acquire(&pi->lock);
      if(pi->nread == start + m && pi->nwrite - start <= PIPESIZE)
        pi->nread = start;
      wakeup(&pi->nwrite);
      release(&pi->lock);
      return tot > 0 ? tot : -1;
    }

    tot += m;
    acquire(&pi->lock);
  }
  wakeup(&pi->nwrite);  //DOC: piperead-wakeup
  release(&pi->lock);
  return tot;
}
