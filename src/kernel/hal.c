// SPDX-License-Identifier: BSD-2-Clause
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
  plicinit();
}

void
hal_intr_inithart(void)
{
  plicinithart();
}

uint64
hal_irq_claim(void)
{
  return (uint64)plic_claim();
}

void
hal_irq_complete(uint64 irq)
{
  plic_complete((int)irq);
}

void
hal_send_ipi(int cpu)
{
  if(cpu < 0 || cpu >= NCPU)
    return;
  __sync_synchronize();
  *(volatile uint32*)CLINT_MSIP(cpu) = 1;
}

void
hal_boot_secondary(void)
{
  // QEMU RISC-V virt boots all harts directly into _entry.
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
