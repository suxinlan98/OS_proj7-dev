---
name: xv6-fs-compatibility
description: "Observable filesystem semantics and storage-safety invariants for xv6, ext2, and FAT32 backends. Use when changing pathname lookup, VFS routing, backend logic, mount lifecycle, or on-disk filesystem crash recovery. Do not use for non-filesystem IPC, console, or pure architecture changes."
---

# xv6 Filesystem Compatibility

Preserve observable POSIX/xv6 syscall semantics and on-disk storage safety across xv6fs, ext2, and FAT32 backends regardless of service partitioning.

## 1. Observable VFS & Exec Contract

Preserve baseline return values, partial side effects, and final file offsets across:
- invalid, read-only, or unmapped user buffers and cross-page I/O;
- shared offsets across `fork`/`dup`, and `unlink` while open (in FAT32, clear the inode's directory-entry offset tracking on unlink so subsequent writes to an unlinked open FD cannot corrupt a reused slot);
- mount-root `..`, busy `umount`, mountpoint unlink rejection enforced in VFS (no backend-to-VFS reverse callbacks), failed mount rollback, and nested mounts;
- `kexec()` bare-name `"/" + path` lookup fallback and `test-xv6.py crash` log markers (`^recovering`, `^ireclaim`).

## 2. Resource Exhaustion & Allocation Ordering

- **Isolate table exhaustion**: In-memory slot exhaustion (e.g., `iget`) must fail the single operation cleanly rather than crashing the filesystem service and invalidating all live handles.
- **Acquire memory tracking before disk mutation**: Reserve in-memory inode/tracking slots before marking on-disk structures allocated and logging them (e.g., `ialloc`), so slot exhaustion never leaks orphan on-disk objects.
- **Transaction bounding**: Preserve `MAXOPBLOCKS` write chunking and WAL commit ordering for xv6fs so large writes never overflow the redo log.

## 3. Mount Identity, Concurrency & On-Disk Recovery

- **Object-identity mounts**: Track mount points by inode/object identity rather than lexical path strings.
- **Single-executor deadlock freedom**: Never sleep inside a single-threaded filesystem/VFS service on an internal lock or blocking pipe whose only releaser is queued behind it on the same IPC endpoint.
- **Conservative on-disk recovery**: Validate on-disk superblock and redo-log headers/bounds before WAL replay; never fabricate consistency for non-journaled backends (ext2/FAT32) after an interrupted write—mark affected volumes dirty/error while keeping unaffected volumes serving.

## Verification

Run the focused VFS/filesystem suite matching the edited backend on **both** architectures serially (e.g., `make ext2 && make run && make loongarch_ext2 && make run`), plus `python3 test-xv6.py crash` inside `src/` when modifying WAL or on-disk recovery logic.
