---
name: xv6-dual-arch-pitfalls
description: "Hardware, MMU, and QEMU quirks for RISC-V and LoongArch in xv6. Use when modifying architecture-specific kernel code: SMP bring-up, traps/IRQ controllers, PTE/TLB/DMWIN mappings, or PCI/MMIO hardware registers. Do not use for architecture-independent IPC protocol design, service recovery policy, or filesystem logic."
---

# xv6 Dual-Architecture Pitfalls (RISC-V & LoongArch)

Architecture-specific hardware and QEMU invariants for RISC-V and LoongArch kernel code (independent of higher-level IPC or service recovery policy).

## 1. Per-Hart Init, Timers & Wake Sources

- **Secondary hart initialization**: Each secondary hart must initialize its own MMU/page-table registers (`satp` on RISC-V; `pgdl`, `pwcl`, `pwch`, TLB on LoongArch) and trap/exception/TLB vectors (`trapinithart`, including `eentry`, `tlbrentry`, `merrentry`, `ecfg`, `tcfg` on LoongArch); unset vectors jump to `0x0` while holding locks on the first trap or refill.
- **LoongArch QEMU `virt` idle wakeups & timer cadence**: Keep interrupts enabled around `wfi`/`idle 0`. The baseline LoongArch `tcfg = 0x1000000` (~167.8 ms on a 100 MHz counter) stalls cross-core wakeups; shorten `tcfg` while calibrating tick accounting, and ensure secondary harts have a verified local wake source (per-hart timer or verified IPI).
- **SMP-safe kill flags in arch traps**: Always read/write process termination state through `killed(p)` / `setkilled(p)` rather than unlocked `p->killed` accesses.

## 2. MMU, PTE Flags & Hardware Address Conversion

- **Writable user PTE bits**: RISC-V requires `PTE_V | PTE_W | PTE_U`. LoongArch requires `PTE_V | PTE_P | PTE_W | PTE_D | PTE_PLV` (`PTE_U` aliases `PTE_PLV`), with `PTE_MAT` for cached coherent RAM and `MAT=0` (strongly ordered uncached) for MMIO.
- **LoongArch `DMWIN_MASK` & unaligned MMIO**: Strip `DMWIN_MASK` when programming DMA bus addresses and apply it when dereferencing physical pages from kernel code. Map page-aligned physical bases for MMIO and pass sub-page offsets (e.g., LoongArch `UART0 = 0x1fe001e0` has offset `+0x1e0`).
- **Pagetable unmap & fault target**: Unmap non-RAM MMIO regions placed above `p->sz` with `do_free = 0` before `proc_freepagetable()` so `kfree`/`freewalk` never panics. Parameterize arch `vmfault()` with an explicit target `struct proc *` instead of hardcoding `myproc()`.

## 3. PCI Registers & Interrupt Controller Quirks

- **LoongArch `virtio-pci` ECAM register control**: To halt DMA at the hardware level, clear `PCI_COMMAND_MASTER` via static PCI ECAM before touching dynamic BAR registers (which user-space drivers may have configured). Re-granting must restore `PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER`.
- **Independent multi-source IRQ dispatch & lost-IRQ mitigation**: Service all pending sources (IPI, external IRQ, timer) independently in one `devintr()` pass rather than an `else if` chain. Guard against dropped edge/level IRQs on LoongArch by draining device status, ACKing the IRQ, re-checking hardware status bits (`LSR_*` / VirtIO `used->idx`), and keeping a timer watchdog backstop.

## Verification

Verify both architecture builds serially after any arch-specific kernel change:

```bash
make -C src ARCH=riscv kernel/kernel && make -C src ARCH=loongarch kernel/kernel
```
