#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "loongarch.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

void kernelvec();
void uservec();
void handle_tlbr();
void handle_merr();
void usertrapret(void);
void userret(uint64, uint64);

extern int devintr();

#define LA_ECODE_PIL 0x1
#define LA_ECODE_PIS 0x2
#define LA_ECODE_PIF 0x3
#define LA_ECODE_PME 0x4
#define LA_ECODE_PPI 0x7
#define LA_ECODE_ADE 0x8
#define LA_ECODE_SYS 0xb

#define LA_TIMER_PERIOD_10MS 1000000UL
#define LA_TIMER_SUBTICKS    10

static uint timer_subticks;

static int
la_page_fault(uint32 ecode, int *read)
{
  if(ecode == LA_ECODE_PIL){
    *read = 1;
    return 1;
  }
  if(ecode == LA_ECODE_PIS){
    *read = 0;
    return 1;
  }
  return 0;
}

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

void
trapinithart(void)
{
  uint32 ecfg = (0U << CSR_ECFG_VS_SHIFT) | HWI_VEC | TI_VEC | IPI_VEC;
  uint64 tcfg = LA_TIMER_PERIOD_10MS | CSR_TCFG_EN | CSR_TCFG_PER;
  uint32 stale_ipi = iocsr_readl(LOONGARCH_IOCSR_IPI_STATUS);
  if(stale_ipi)
    iocsr_writel(stale_ipi, LOONGARCH_IOCSR_IPI_CLEAR);
  iocsr_writel(0xffffffffU, LOONGARCH_IOCSR_IPI_EN);
  w_csr_ecfg(ecfg);
  w_csr_tcfg(tcfg);
  w_csr_eentry((uint64)kernelvec);
  w_csr_tlbrentry((uint64)handle_tlbr);
  w_csr_merrentry((uint64)handle_merr);
}

void
usertrap(void)
{
  int which_dev = 0;
  uint32 ecode;
  int read = 0;

  if((r_csr_prmd() & PRMD_PPLV) == 0)
    panic("usertrap: not from user mode");

  w_csr_eentry((uint64)kernelvec);

  struct proc *p = myproc();
  
  p->trapframe->era = r_csr_era();
  ecode = (r_csr_estat() & CSR_ESTAT_ECODE) >> 16;
  
  if(ecode == LA_ECODE_SYS){
    if(killed(p))
      kexit(-1);

    p->trapframe->era += 4;

    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
  } else if(la_page_fault(ecode, &read) &&
            vmfault(p->pagetable, r_csr_badv(), read) != 0){
  } else {
    printf("usertrap(): unexpected trapcause %x pid=%d\n", r_csr_estat(), p->pid);
    printf("            era=0x%lx badv=0x%lx badi=0x%x sp=0x%lx crmd=0x%x prmd=0x%x\n",
           r_csr_era(), r_csr_badv(), r_csr_badi(), p->trapframe->sp,
           r_csr_crmd(), r_csr_prmd());
    setkilled(p);
  }

  if(killed(p))
    kexit(-1);

  if(which_dev == 2)
    yield();

  usertrapret();
}

void
usertrapret(void)
{
  struct proc *p = myproc();

  intr_off();

  w_csr_eentry((uint64)uservec);

  p->trapframe->kernel_pgdl = r_csr_pgdl();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  uint32 x = r_csr_prmd();
  x |= PRMD_PPLV; // set PPLV to 3 for user mode
  x |= PRMD_PIE; // enable interrupts in user mode
  w_csr_prmd(x);

  w_csr_era(p->trapframe->era);

  volatile uint64 pgdl = (uint64)(p->pagetable);

  userret(TRAPFRAME, pgdl);
}

void 
kerneltrap()
{
  int which_dev = 0;
  uint64 era = r_csr_era();
  uint64 prmd = r_csr_prmd();
  
  if((prmd & PRMD_PPLV) != 0)
    panic("kerneltrap: not from privilege0");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("estat %x\n", r_csr_estat());
    printf("era=0x%lx eentry=0x%lx badv=0x%lx crmd=0x%x prmd=0x%x\n",
           r_csr_era(), r_csr_eentry(), r_csr_badv(), r_csr_crmd(), r_csr_prmd());
    panic("kerneltrap");
  }

  if(which_dev == 2 && myproc() != 0)
    yield();

  w_csr_era(era);
  w_csr_prmd(prmd);
}

void 
machine_trap()
{
  panic("machine error");
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

int
devintr()
{
  uint32 estat = r_csr_estat();
  uint32 ecfg = r_csr_ecfg();
  uint32 pending = estat & ecfg;
  int ret = 0;

  if(pending & HWI_VEC) {
    uint64 irq = hal_irq_claim();
    uint64 handled = 0;

    if(irq & (1UL << UART0_IRQ)){
      handled |= (1UL << UART0_IRQ);
      // UART0 is edge-triggered at the LS7A APIC; drain and re-check
      // after ACK so a late RX byte or TX-idle edge is never lost.
      do {
        uartintr();
        hal_irq_complete(1UL << UART0_IRQ);
      } while(uart_rx_pending() || uart_tx_pending());
    }
    if(irq & (1UL << VIRTIO0_IRQ)){
      handled |= (1UL << VIRTIO0_IRQ);
      // With legacy INTx signalling, a completion that arrives while
      // the previous one is being acknowledged can be swallowed by
      // the APIC/EXTIOI chain.  Loop until every posted completion has
      // been processed.
      do {
        virtio_disk_intr();
        hal_irq_complete(1UL << VIRTIO0_IRQ);
      } while(virtio_disk_pending());
    }
    if(irq & ~handled){
      uint64 unknown = irq & ~handled;
      printf("unexpected interrupt irq=0x%lx\n", unknown);
      hal_irq_complete(unknown);
    }

    ret = 1;
  }

  if(pending & IPI_VEC){
    uint32 status = iocsr_readl(LOONGARCH_IOCSR_IPI_STATUS);
    if(status)
      iocsr_writel(status, LOONGARCH_IOCSR_IPI_CLEAR);
    if(ret == 0)
      ret = 1;
  }

  if(pending & TI_VEC){
    if(cpuid() == 0){
      if(++timer_subticks >= LA_TIMER_SUBTICKS){
        timer_subticks = 0;
        clockintr();
      }
      // Backstop for lost virtio completion interrupts: the timer
      // does not depend on the external interrupt chain.
      virtio_disk_watchdog();
    }

    w_csr_ticlr(r_csr_ticlr() | CSR_TICLR_CLR);
    ret = 2;
  }

  return ret;
}
