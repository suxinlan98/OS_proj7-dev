#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

void
hal_intr_init(void)
{
  apicinit();
  extioiinit();
}

void
hal_intr_inithart(void)
{
}

uint64
hal_irq_claim(void)
{
  // EXTIOI reports pending interrupts as a bitmask.
  return extioi_claim();
}

void
hal_irq_complete(uint64 irq)
{
  // The virtio-pci interrupt is level-triggered and routed through
  // the LS7A APIC, so it must be cleared there as well.  The UART is
  // edge-triggered and needs no APIC clear.  Every interrupt is
  // acknowledged at the EXTIOI.
  if(irq & (1UL << VIRTIO0_IRQ))
    apic_complete(1UL << VIRTIO0_IRQ);
  extioi_complete(irq);
}

void
hal_send_ipi(int cpu)
{
  if(cpu < 0 || cpu >= NCPU)
    return;
  __sync_synchronize();
  iocsr_writel((1U << 31) | ((uint32)cpu << 16) | 0U,
               LOONGARCH_IOCSR_IPI_SEND);
}

void
hal_boot_secondary(void)
{
  extern void _entry(void);
  uint64 entry_pa = (uint64)_entry & ~DMWIN_MASK;

  // QEMU LoongArch virt direct-kernel boot parks secondary CPUs in
  // slave_boot_code waiting for mailbox 0 + IPI action 0.
  for(int cpu = 1; cpu < NCPU; cpu++){
    uint64 hi = (1ULL << 31) | (1ULL << 2) | ((uint64)cpu << 16) |
                (entry_pa & 0xffffffff00000000ULL);
    uint64 lo = (1ULL << 31) | (0ULL << 2) | ((uint64)cpu << 16) |
                ((entry_pa & 0xffffffffULL) << 32);
    iocsr_writeq(hi, LOONGARCH_IOCSR_MAIL_SEND);
    iocsr_writeq(lo, LOONGARCH_IOCSR_MAIL_SEND);
    hal_send_ipi(cpu);
  }
}

void
hal_disk_init(void)
{
  virtio_disk_init();
}

void
hal_disk_rw(struct buf *b, int write)
{
  virtio_disk_rw(b, write);
}
