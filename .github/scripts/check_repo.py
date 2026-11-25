#!/usr/bin/env python3
"""Static repository checks for CI (doc links, tracked artifacts, kernel-IPC purity)."""

import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")

FORBIDDEN_GENERATED_RE = re.compile(
    r"(^|/)\.env(\.[^/]+)?$"
    r"|\.(img|asm|sym)$"
    r"|^src/user/usys\.S$"
    r"|^src/\.arch-"
)

ALLOWED_ENV_FILES = {".env.example", "web/.env.example"}

IPC_FORBIDDEN_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*["<](?:fs|file|ext2|fat32|vfs|buf|virtio[^">]*|uart|console|log)\.h[">]'
)


def git_ls_files() -> list[str]:
    out = subprocess.check_output(["git", "ls-files"], cwd=ROOT, text=True)
    return [line.strip() for line in out.splitlines() if line.strip()]


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


def main() -> int:
    files = git_ls_files()
    errors = []
    errors.extend(check_markdown_links(files))
    errors.extend(check_tracked_artifacts(files))
    errors.extend(check_kernel_ipc_purity())
    if errors:
        print("\nRepository checks FAILED:", file=sys.stderr)
        for err in errors:
            print(f"  - {err}", file=sys.stderr)
        return 1
    print("[repo-check] ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
