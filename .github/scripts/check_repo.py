#!/usr/bin/env python3
"""Static repository checks for CI.

Checks:
  1. Relative markdown links (no broken or absolute/file:// links).
  2. Tracked generated artifacts and secret files.
  3. Kernel IPC purity (src/kernel/ipc.c must not include FS/driver headers).
  4. Shared ABI header purity (no conditional compilation except outer include
     guard, and no hardware physical address constants).
  5. Reserved kernel ELF symbol purity check (detects residual FS/driver symbols
     after microkernel migration, or on demand via --check-elf <path>).
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")

FORBIDDEN_GENERATED_RE = re.compile(
    r"(^|/)\.env(\.[^/]+)?$"
    r"|\.(img|asm|sym)$"
    r"|^src/user/usys\.S$"
    r"|^src/\.arch-"
    r"|^build/"
)

ALLOWED_ENV_FILES = {".env.example", "web/.env.example"}

IPC_FORBIDDEN_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*["<](?:fs|file|ext2|fat32|vfs|buf|virtio[^">]*|uart|console|log)\.h[">]'
)

SHARED_ABI_HEADER_RE = re.compile(
    r"^src/(?:"
    r"(?:include|common|abi)/.+\.h$"
    r"|kernel/(?:.*_abi|ipc|ipc_[^/]+|bootfs|devdrv)\.h$"
    r")"
)

PP_COND_RE = re.compile(
    r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(?:\s+([^\s/]+))?"
)
PP_DEFINE_RE = re.compile(r"^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\b")

ABI_FORBIDDEN_HW_IDENT_RE = re.compile(
    r"\b(?:"
    r"UART0(?:_IRQ)?|VIRTIO0(?:_IRQ)?|"
    r"PLIC(?:_[A-Z0-9_]+)?|CLINT(?:_[A-Z0-9_]+)?|"
    r"DMWIN_MASK|DMWIN_UC_MASK|"
    r"LS7A_[A-Z0-9_]+|PCI_ECAM|PCI_MMIO_BASE|PCI_MMIO_SIZE|"
    r"KERNBASE|RAMBASE|RAMSTOP|PHYSTOP|"
    r"TRAMPOLINE|TRAPFRAME|KSTACK"
    r")\b"
)

ABI_FORBIDDEN_HW_ADDR_RE = re.compile(
    r"\b0[xX](?:"
    r"0?2000000|"      # RISC-V CLINT
    r"0?c000000|"      # RISC-V PLIC
    r"10000000|"       # RISC-V UART0 / LoongArch LS7A PCH
    r"10001000|"       # RISC-V VirtIO0 MMIO
    r"1fe001e0|"       # LoongArch UART0
    r"20000000|"       # LoongArch PCI ECAM
    r"40000000|"       # LoongArch PCI MMIO
    r"80000000(?:[0-9a-fA-F]{8})?|"  # RISC-V KERNBASE / LoongArch DMWIN1
    r"90000000(?:[0-9a-fA-F]{8})?"   # LoongArch RAMBASE / DMWIN0
    r")[uUlL]*\b"
)

# Forbidden kernel ELF symbols after filesystems and regular drivers move to user space.
ELF_FORBIDDEN_SYMBOL_RULES = [
    ("ext2 filesystem", re.compile(r"^ext2_")),
    ("fat32 filesystem", re.compile(r"^fat32_")),
    ("VFS layer", re.compile(r"^vfs_")),
    (
        "xv6fs / inode / directory layer",
        re.compile(
            r"^(?:fsinit|iinit|ialloc|iget|iput|ilock|iunlock|iunlockput|"
            r"iupdate|itrunc|ireclaim|dirlookup|dirlink|namei|nameiparent|"
            r"readi|writei|stati|balloc|bfree|bzero)$"
        ),
    ),
    (
        "block cache / raw block layer",
        re.compile(r"^(?:binit|bread|bwrite|brelse|bpin|bunpin|block_(?:read|write)_bytes)$"),
    ),
    (
        "WAL log layer",
        re.compile(r"^(?:initlog|begin_op|end_op|log_write|recover_from_log|commit|install_trans)$"),
    ),
    ("virtio block/PCI driver", re.compile(r"^(?:virtio_disk_|virtio_pci_|hal_disk_)")),
    (
        "UART / console driver",
        re.compile(
            r"^(?:uartinit|uartintr|uartstart|uartputc|uartgetc|uart_rx_pending|"
            r"uart_tx_pending|consoleinit|consoleintr|consoleread|consolewrite)$"
        ),
    ),
]

ELF_ALLOWED_SYMBOLS = {
    "early_putc",
    "consputc",
    "procdump",
    "uartputc_sync",
}


def git_ls_files() -> list[str]:
    out = subprocess.check_output(["git", "ls-files"], cwd=ROOT, text=True)
    return [line.strip() for line in out.splitlines() if line.strip()]


def strip_c_comments_preserve_lines(source: str) -> list[str]:
    """Remove C /* */ and // comments while keeping 1:1 line numbering."""
    out = []
    i = 0
    n = len(source)
    in_block = False
    in_line = False
    in_str = None
    while i < n:
        ch = source[i]
        nxt = source[i + 1] if i + 1 < n else ""
        if in_block:
            if ch == "*" and nxt == "/":
                in_block = False
                out.append("  ")
                i += 2
            else:
                out.append("\n" if ch == "\n" else " ")
                i += 1
        elif in_line:
            if ch == "\n":
                in_line = False
                out.append("\n")
            else:
                out.append(" ")
            i += 1
        elif in_str is not None:
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(nxt)
                i += 2
            else:
                if ch == in_str:
                    in_str = None
                i += 1
        else:
            if ch == "/" and nxt == "*":
                in_block = True
                out.append("  ")
                i += 2
            elif ch == "/" and nxt == "/":
                in_line = True
                out.append("  ")
                i += 2
            elif ch in ('"', "'"):
                in_str = ch
                out.append(ch)
                i += 1
            else:
                out.append(ch)
                i += 1
    return "".join(out).splitlines()


def check_markdown_links(files: list[str]) -> list[str]:
    errors = []
    checked = 0
    for rel_fp in files:
        if not rel_fp.endswith(".md"):
            continue
        fp = os.path.join(ROOT, rel_fp)
        dirpath = os.path.dirname(fp)
        with open(fp, "r", encoding="utf-8") as f:
            for lineno, line in enumerate(f, 1):
                for m in LINK_RE.finditer(line):
                    target = m.group(1).strip()
                    if target.startswith(("http://", "https://", "mailto:", "#")):
                        continue
                    checked += 1
                    if target.startswith("file://") or target.startswith("/"):
                        errors.append(
                            f"{rel_fp}:{lineno}: non-relative link '{target}'"
                        )
                        continue
                    path_part = target.split("#", 1)[0]
                    if not path_part:
                        continue
                    resolved = os.path.normpath(os.path.join(dirpath, path_part))
                    if not os.path.exists(resolved):
                        errors.append(
                            f"{rel_fp}:{lineno}: broken link '{target}'"
                        )
    print(f"[repo-check] checked {checked} markdown links")
    return errors


def check_tracked_artifacts(files: list[str]) -> list[str]:
    errors = []
    for rel_fp in files:
        if rel_fp in ALLOWED_ENV_FILES:
            continue
        if FORBIDDEN_GENERATED_RE.search(rel_fp):
            errors.append(f"forbidden tracked file: {rel_fp}")
    print(f"[repo-check] checked {len(files)} tracked files for generated/secret leaks")
    return errors


def check_kernel_ipc_purity() -> list[str]:
    ipc_path = os.path.join(ROOT, "src", "kernel", "ipc.c")
    if not os.path.exists(ipc_path):
        print("[repo-check] src/kernel/ipc.c not present yet; skipping IPC purity check")
        return []
    errors = []
    with open(ipc_path, "r", encoding="utf-8") as f:
        for lineno, line in enumerate(f, 1):
            if IPC_FORBIDDEN_INCLUDE_RE.match(line):
                errors.append(
                    f"src/kernel/ipc.c:{lineno}: forbidden subsystem include: {line.strip()}"
                )
    print("[repo-check] checked kernel IPC purity")
    return errors


def check_single_abi_header(abs_path: str, display_name: str) -> list[str]:
    errors = []
    with open(abs_path, "r", encoding="utf-8") as f:
        lines = strip_c_comments_preserve_lines(f.read())

    non_empty = [(idx + 1, line) for idx, line in enumerate(lines) if line.strip()]
    guard_lines = set()
    if len(non_empty) >= 3:
        first_ln, first_txt = non_empty[0]
        second_ln, second_txt = non_empty[1]
        last_ln, last_txt = non_empty[-1]
        m_ifndef = PP_COND_RE.match(first_txt)
        m_define = PP_DEFINE_RE.match(second_txt)
        m_endif = PP_COND_RE.match(last_txt)
        if (
            m_ifndef
            and m_ifndef.group(1) == "ifndef"
            and m_define
            and m_ifndef.group(2) == m_define.group(1)
            and m_endif
            and m_endif.group(1) == "endif"
        ):
            guard_lines = {first_ln, last_ln}

    for lineno, line in enumerate(lines, 1):
        m_cond = PP_COND_RE.match(line)
        if m_cond and lineno not in guard_lines:
            errors.append(
                f"{display_name}:{lineno}: forbidden conditional compilation in shared ABI header: {line.strip()}"
            )
        m_hw = ABI_FORBIDDEN_HW_IDENT_RE.search(line)
        if m_hw:
            errors.append(
                f"{display_name}:{lineno}: forbidden hardware/platform constant '{m_hw.group(0)}' in shared ABI header"
            )
        m_addr = ABI_FORBIDDEN_HW_ADDR_RE.search(line)
        if m_addr:
            errors.append(
                f"{display_name}:{lineno}: forbidden hardware physical address literal '{m_addr.group(0)}' in shared ABI header"
            )
    return errors


def check_shared_abi_headers(files: list[str], extra_headers: list[str]) -> list[str]:
    targets: list[tuple[str, str]] = []
    seen = set()
    for rel_fp in files:
        if SHARED_ABI_HEADER_RE.match(rel_fp):
            abs_fp = os.path.join(ROOT, rel_fp)
            if os.path.exists(abs_fp) and abs_fp not in seen:
                seen.add(abs_fp)
                targets.append((abs_fp, rel_fp))
    for hdr in extra_headers:
        abs_fp = os.path.abspath(hdr if os.path.isabs(hdr) else os.path.join(ROOT, hdr))
        if abs_fp not in seen:
            seen.add(abs_fp)
            targets.append((abs_fp, hdr))

    if not targets:
        print("[repo-check] no shared ABI headers present yet; skipping ABI header check")
        return []

    errors = []
    for abs_fp, display_name in targets:
        if not os.path.exists(abs_fp):
            errors.append(f"{display_name}: ABI header not found")
            continue
        errors.extend(check_single_abi_header(abs_fp, display_name))
    print(f"[repo-check] checked {len(targets)} shared ABI header(s)")
    return errors


def extract_elf_defined_symbols(path: str) -> list[str]:
    """Extract defined symbol names from a kernel ELF binary or .sym file."""
    if path.endswith(".sym"):
        syms = []
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) >= 2:
                    syms.append(parts[-1])
        return syms

    readelf = shutil.which("readelf")
    if readelf:
        out = subprocess.check_output(
            [readelf, "-Ws", path], text=True, stderr=subprocess.STDOUT
        )
        syms = []
        for line in out.splitlines():
            parts = line.split()
            # Format: Num: Value Size Type Bind Vis Ndx Name
            if len(parts) < 8 or not parts[0].endswith(":"):
                continue
            sym_type, ndx, name = parts[3], parts[6], parts[7]
            if ndx == "UND" or sym_type in ("FILE", "SECTION"):
                continue
            if name.startswith("$"):
                continue
            syms.append(name)
        return syms

    for tool in ("nm", "riscv64-linux-gnu-nm", "loongarch64-linux-gnu-nm", "llvm-nm"):
        tool_path = shutil.which(tool)
        if not tool_path:
            continue
        out = subprocess.check_output(
            [tool_path, "-P", "--defined-only", path],
            text=True,
            stderr=subprocess.STDOUT,
        )
        syms = []
        for line in out.splitlines():
            parts = line.split()
            if parts and not parts[0].startswith("$"):
                syms.append(parts[0])
        return syms

    raise RuntimeError("no ELF symbol reader found (tried readelf, nm)")


def check_kernel_elf_symbols(elf_paths: list[str], auto_post_migration: bool) -> list[str]:
    targets = list(elf_paths)
    if not targets and auto_post_migration:
        candidates = [
            "src/kernel/kernel",
            "build/riscv/kernel/kernel",
            "build/loongarch/kernel/kernel",
        ]
        targets = [c for c in candidates if os.path.exists(os.path.join(ROOT, c))]

    if not targets:
        print(
            "[repo-check] kernel ELF symbol check reserved "
            "(activates post-migration or via --check-elf <elf-or-sym>)"
        )
        return []

    errors = []
    for target in targets:
        abs_path = target if os.path.isabs(target) else os.path.join(ROOT, target)
        if not os.path.exists(abs_path):
            errors.append(f"{target}: ELF/sym file not found")
            continue
        try:
            syms = extract_elf_defined_symbols(abs_path)
        except (OSError, subprocess.CalledProcessError, RuntimeError) as exc:
            errors.append(f"{target}: failed to inspect ELF symbols: {exc}")
            continue
        hits = []
        for sym in sorted(set(syms)):
            if sym in ELF_ALLOWED_SYMBOLS:
                continue
            for category, regex in ELF_FORBIDDEN_SYMBOL_RULES:
                if regex.search(sym):
                    hits.append((category, sym))
                    break
        for category, sym in hits:
            errors.append(
                f"{target}: forbidden residual {category} symbol '{sym}'"
            )
        print(
            f"[repo-check] checked kernel ELF symbols in {target} "
            f"({len(set(syms))} defined, {len(hits)} violation(s))"
        )
    return errors


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Static repository hygiene, ABI purity, and reserved ELF symbol checks."
    )
    parser.add_argument(
        "--abi-header",
        action="append",
        default=[],
        metavar="PATH",
        help="additional shared ABI header file to validate",
    )
    parser.add_argument(
        "--check-elf",
        action="append",
        default=[],
        metavar="PATH",
        help="validate a kernel ELF binary or .sym file for residual FS/driver symbols",
    )
    parser.add_argument(
        "--list-forbidden-elf-symbols",
        action="store_true",
        help="print reserved forbidden ELF symbol patterns and exit",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.list_forbidden_elf_symbols:
        print("Forbidden post-migration kernel ELF symbol patterns:")
        for category, regex in ELF_FORBIDDEN_SYMBOL_RULES:
            print(f"  - {category}: {regex.pattern}")
        print("Allowed minimal kernel symbols:", ", ".join(sorted(ELF_ALLOWED_SYMBOLS)))
        return 0

    files = git_ls_files()
    # Auto-activate ELF purity check once microkernel migration removes in-kernel fs.c
    # and introduces src/kernel/ipc.c.
    post_migration = (
        os.path.exists(os.path.join(ROOT, "src", "kernel", "ipc.c"))
        and not os.path.exists(os.path.join(ROOT, "src", "kernel", "fs.c"))
    )

    errors = []
    errors.extend(check_markdown_links(files))
    errors.extend(check_tracked_artifacts(files))
    errors.extend(check_kernel_ipc_purity())
    errors.extend(check_shared_abi_headers(files, args.abi_header))
    errors.extend(check_kernel_elf_symbols(args.check_elf, post_migration))

    if errors:
        print("\nRepository checks FAILED:", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        return 1
    print("[repo-check] ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
