---
name: xv6-ipc-boundary
description: "Architecture-independent IPC ABI, capability boundary, and service crash-recovery contracts for an xv6 microkernel. Use when designing or modifying kernel IPC transport, service lifecycle, fd/device proxy protocols, or crash recovery policy. Do not use for RISC-V/LoongArch hardware register, PTE, or IRQ-controller quirks, or single-backend filesystem logic."
---

# xv6 IPC & Service Boundary

Keep IPC transport clean, decoupled, and fault-isolated while enforcing strict separation between kernel mechanism and user-space service policy.

## 1. Mechanism vs. Policy & Low Coupling

- **Kernel owns mechanics**: scheduling, VM/protection, traps, generic IPC transport and endpoint/capability authorization, device grant lifecycle, IRQ routing, native pipes, and emergency polling output (`early_putc`).
- **User services own policy and state**: filesystem, block, console, and supervisor policy/state stay out of the kernel IPC layer—zero FS, block, or console opcodes or structs in kernel IPC code.
- **Explicit narrow interfaces**: Cross-boundary communication uses fixed-width message headers and capability-checked endpoints; avoid hidden global state or cross-layer callbacks.

## 2. Wire ABI & OOM Progress

- **Pointer-free, fixed-width ABI**: Never pass kernel pointers, locks, or internal kernel structs across the boundary; lock wire layouts with `_Static_assert`.
- **Allocation-free hot path & teardown**: IPC send/receive/reply, close, and crash-cleanup paths must make progress under zero free pages without calling `kalloc()`.
- **Service resource reservations**: Reserve dedicated process slots and minimum physical pages for core services and the supervisor so user-space fork/memory bombs cannot starve recovery.

## 3. Concurrency & Wait Interruptibility

- **Cross-address-space transfer safety**: Never hold a spinlock across user-memory copies or RPCs. Pin active cross-address-space grants (`in_use` refcount) until completion before reusing or freeing their slots.
- **Lock-safe process lifecycle RPCs**: In `fork()`, release child `p->lock` while the child is still `USED` before issuing any VFS/service RPC, then transition to `RUNNABLE` only after success.
- **Killable waits**: Blocking IPC and service waits must check `killed()` so terminated clients and services exit promptly.

## 4. Service Fault Isolation & Recovery Policy

- **No blind RPC replay**: Track request lifecycle (`UNSENT` -> `DISPATCHED` -> `COMMITTED` -> `REPLIED`) and never silently replay uncertain non-idempotent writes across a service restart.
- **Generation invalidation**: Bump endpoint/handle epochs across service restarts so stale handles fail cleanly instead of silently re-opening reused paths, and drain old in-flight requests before a restarted service resumes I/O.
- **Fail-stop DMA page quarantine**: Return DMA pages to the allocator or restart a DMA-capable service only after the architecture HAL confirms hardware quiescence; otherwise fail-stop the device and quarantine those pages permanently.
- **Valid fault-injection triggers**: Because xv6 maps user `.text` readable at VA `0x0`, fault-injection tests must provoke an illegal **write** (e.g., write to VA `0x0` or an unmapped address), not a NULL read.

## Verification

Verify both kernel targets serially (and run the kernel-IPC purity static check once `src/kernel/ipc.c` exists):

```bash
make -C src ARCH=riscv kernel/kernel && make -C src ARCH=loongarch kernel/kernel
```
