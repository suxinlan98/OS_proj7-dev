# AGENTS.md

## Commands and Tiered Verification

Run the narrowest verification tier that matches the change:

- **Tier 0 (every code edit)**: Compile both targets serially via `make -C src ARCH=riscv kernel/kernel && make -C src ARCH=loongarch kernel/kernel` (run `bash tests/check_env.sh` if toolchain setup changed, and run the kernel-IPC purity static check once `src/kernel/ipc.c` exists).
- **Tier 1 (subsystem or feature change)**: Run the matching focused QEMU suite on **both** architectures serially (`make <suite> && make run`, then `make loongarch_<suite> && make run`; note baseline targets are `test_baseline` and `loongarch_baseline`; other suites: `public_vfs`, `ext2`, `fat32`, `error`, `semantics`, `persist`, `smp`, `mount_lifecycle`, `remount_io`, `mount_storm`). For changes strictly confined to one architecture's HAL, you may iterate on that architecture first, then verify both before finishing.
- **Tier 2 (pre-PR or milestone only)**: Full dual-architecture regression via `make verify-all`, plus `python3 test-xv6.py crash` inside `src/` when modifying logging or crash recovery.

If only docs or comments changed, skip Tier 1 and Tier 2 unless requested. `src/Makefile` shares object paths via `.arch-$(ARCH)`, so build and test RISC-V and LoongArch serially unless outputs are isolated per architecture.

## Repo Invariants

- **High code quality & low coupling**: Keep modules small, cohesive, and single-responsibility. Communicate across kernel/service and subsystem boundaries solely through explicit fixed-width headers and ABIs—no hidden shared global state or cross-layer backdoors.
- **Single default boot path**: Keep the kernel and core services on one default build/boot path. Put tests in user programs or host scripts, with at most one lightweight switch for fault injection; do not scatter test-only `BOOT_MODE` branches across core logic.
- **Pure kernel IPC**: The kernel IPC layer owns transport, capability/endpoint routing, and isolation mechanics only—zero filesystem, block, or console policy, opcodes, or structs.
- **Root-cause fixes & honest verification**: When a test script or check fails, diagnose and fix the underlying root cause in the implementation (or a demonstrably broken harness bug). Never patch code to game test-script assertions, hardcode expected outputs, weaken `-Werror`, alter inherited test assertions, silently fall back to in-kernel subsystems, or inflate timeouts to mask scheduling/IRQ hangs.
- **Critical engineering judgment**: Never treat user prompts or existing assumptions as unquestionable truth. When a requested approach has technical flaws, hidden hardware/concurrency traps, or a simpler, safer design, push back directly with evidence and propose the better alternative; if the user explicitly confirms an override after trade-offs are clear, follow their direction.

## Git, Prompts, and Maintenance Rules

- **Working-tree & remote safety**: Never overwrite, discard, or revert pre-existing uncommitted user changes; inspect `git status` and `git diff` before staging, and stage only files relevant to the current task. Never push to any remote (`git push`) without explicit user approval.
- **Git commit policy**: Do not create commits on your own for routine or small tasks unless the user asks. During large multi-step tasks, you may create intermediate checkpoint commits without prior approval when helpful to isolate stages or manage risk, and must append `-AI` to the commit subject (e.g., `feat(ipc): add endpoint dispatch -AI`).
- **Key AI prompt logging**: Record engineering-significant prompts (architecture trade-offs, complex root-cause debugging, milestone design shifts) in `docs/AI_prompts/<category>/YYYYMMDD-NNN-brief-description.md` following `docs/AI_prompts/README.md`—preserve verbatim prompts, skip trivial/repetitive Q&A, and strip any secrets.
- **Self-healing stale or inaccurate files**: Agents may directly update stale, outdated, or factually inaccurate documentation, comments, `AGENTS.md`, and `.agents/skills/` files when verified against the live codebase (such as outdated paths, commands, flags, or superseded claims). Confirm with the user before changing architectural invariants, security boundaries, or deleting domain rules.

## Security and Safety Boundaries

- **Credentials & prompt hygiene**: Never commit `.env*`, private keys, API keys, passwords, or tokens anywhere in the repository, including `docs/AI_prompts/` records and logs.
- **Privilege & memory boundaries**: Never bypass user/kernel permission checks, `TRAPFRAME`/`MAXVA` address bounds, DMA bus-master quiescence, or privileged RPC authorization to make a test pass.
- **Destructive commands**: Require explicit user confirmation before destructive operations (`git reset --hard`, `git push --force`, `rm -rf` on tracked or persistent state, or wiping persistent test disk images during failure investigation).
- **Protected & generated files**: Never hand-edit or commit generated build outputs (`*.img`, `*.asm`, `*.sym`, `src/user/usys.S`, `src/.arch-*`) or alter inherited baseline test expectations to hide failures; regenerate build artifacts via `make`.

## Documentation Orientation

| File | Contains | When to Read |
| :--- | :--- | :--- |
| `docs/architecture/README.md` | Microkernel topology, IPC/capability ABI, service state ownership, and recovery contracts | Before changing syscalls, IPC ABI, service boundaries, or recovery semantics |
| `docs/baseline_debug.md` | Verified baseline bugs, dual-arch QEMU/hardware landmines, and migration traps | Before debugging build/runtime failures or migrating subsystems to user space |
| `docs/AI_prompts/README.md` | Format and policy for recording engineering-significant AI prompts | When logging milestone prompts or adding a new prompt category |
| `Makefile` | Top-level dual-arch test suite targets and `verify-all` pipeline | Before invoking unfamiliar QEMU suites or adding new test targets |
