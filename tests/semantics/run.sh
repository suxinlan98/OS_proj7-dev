#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
LOG_SUFFIX=""
CASE_NAME="semantics"
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
  CASE_NAME="${TEST_ARCH}_semantics"
fi
RAW_LOG="$LOG_DIR/qemu-semantics$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result$LOG_SUFFIX.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$ROOT_DIR/build/$TEST_ARCH/fs.img" ]; then
  echo "[semantics] fs.img not found"
  echo "Use: make semantics"
  exit 1
fi

echo "[semantics] running QEMU command test"
python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
  --case "$CASE_NAME" \
  --xv6-dir "$XV6_DIR" \
  --commands "$SCRIPT_DIR/cmds.sh" \
  --log "$RAW_LOG" \
  --prompt-wait \
  --cmd-timeout 180 \
  --expect "mount: ext2 on /mnt dev 2" \
  --expect "mount: fat32 on /fat dev 3" \
  --expect-count "EXT2_EXEC_OK" 2 \
  --expect-count "FAT32_EXEC_OK" 2 \
  --expect-count "VFS_PARENT_OK" 3 \
  --expect-count "Hello, world" 2 \
  --expect "link /fat/a.txt /fat/b.txt: failed" \
  --expect-count "unlinkopen: ok" 2 \
  --expect-count "umount: busy or invalid" 2 \
  --expect "umount: /mnt" \
  --expect "umount: /fat" \
  --expect-count "XV6_MNT_BASE" 2 \
  --expect-count "XV6_FAT_BASE" 2 \
  --expect-count "UMOUNT_SEMANTICS_OK" 2 \
  --expect-count "FAT32_REMOUNT_OK" 2 \
  --expect-count "SEMANTICS_DONE" 2 \
  --reject "panic:" \
  --reject "kerneltrap" \
  --reject "mount: failed" \
  --reject "exec /mnt/echo failed" \
  --reject "exec /fat/echo failed" \
  --reject "link /mnt/hello.c /mnt/hlink.c: failed" \
  --reject "open /mnt/hlink.c failed" \
  --reject "unlinkopen: FAIL"
test_status=$?

if [ "$test_status" -eq 0 ]; then
  result="PASS"
else
  result="FAIL"
fi

{
  echo "Result: $result"
  echo "Test Status: $test_status"
  echo "Command File: $SCRIPT_DIR/cmds.sh"
  echo "Raw Log: $RAW_LOG"
} > "$RESULT_LOG"

echo "[semantics] result: $result"
echo "[semantics] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
