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

static int la_ncpus = 1;

void
hal_send_ipi(int cpu)
{
  if(cpu < 0 || cpu >= la_ncpus)
    return;
  __sync_synchronize();
  iocsr_writel((1U << 31) | ((uint32)cpu << 16) | 0U,
               LOONGARCH_IOCSR_IPI_SEND);
}

#define FW_CFG_DATA       ((volatile uchar *)(FW_CFG_BASE + 0x00))
#define FW_CFG_CTL        ((volatile ushort *)(FW_CFG_BASE + 0x08))
#define FW_CFG_SIGNATURE  0x0000U
#define FW_CFG_NB_CPUS    0x0005U

static inline ushort
bswap16(ushort v)
{
  return (ushort)((v << 8) | (v >> 8));
}

static int
la_detect_ncpus(void)
{
  uchar sig[4];

  // Probe QEMU virt fw_cfg MMIO device for the active -smp CPU count so we
  // never send IOCSR mailbox/IPI writes to non-existent CPUs (which crashes
  // QEMU 8.2's loongarch_ipi controller with a NULL vCPU dereference).
  *FW_CFG_CTL = bswap16(FW_CFG_SIGNATURE);
  __sync_synchronize();
  for(int i = 0; i < 4; i++)
    sig[i] = *FW_CFG_DATA;
  if(sig[0] != 'Q' || sig[1] != 'E' || sig[2] != 'M' || sig[3] != 'U')
    return 1;

  *FW_CFG_CTL = bswap16(FW_CFG_NB_CPUS);
  __sync_synchronize();
  int lo = *FW_CFG_DATA;
  int hi = *FW_CFG_DATA;
  int ncpus = lo | (hi << 8);
  if(ncpus < 1)
    ncpus = 1;
  if(ncpus > NCPU)
    ncpus = NCPU;
  return ncpus;
}

void
hal_boot_secondary(void)
{
  extern void _entry(void);
  uint64 entry_pa = (uint64)_entry & ~DMWIN_MASK;
  la_ncpus = la_detect_ncpus();

  // QEMU LoongArch virt direct-kernel boot parks secondary CPUs in
  // slave_boot_code waiting for mailbox 0 + IPI action 0.
  for(int cpu = 1; cpu < la_ncpus; cpu++){
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
