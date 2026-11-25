#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
LOG_DIR="$SCRIPT_DIR/logs"
TEST_ARCH="${TEST_ARCH:-riscv}"
LOG_SUFFIX=""
CASE_NAME="ext2"
if [ "$TEST_ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$TEST_ARCH"
  CASE_NAME="${TEST_ARCH}_ext2"
fi
RAW_LOG="$LOG_DIR/qemu-ext2$LOG_SUFFIX.log"
RESULT_LOG="$LOG_DIR/result$LOG_SUFFIX.log"

mkdir -p "$LOG_DIR"

if [ ! -f "$XV6_DIR/fs.img" ]; then
  echo "[ext2] fs.img not found"
  echo "Use: make ext2"
  exit 1
fi

echo "[ext2] running QEMU command test"
python3 "$ROOT_DIR/tests/runner/run_qemu_case.py" \
  --case "$CASE_NAME" \
  --xv6-dir "$XV6_DIR" \
  --commands "$SCRIPT_DIR/cmds.sh" \
  --log "$RAW_LOG" \
  --prompt-wait \
  --cmd-timeout 180 \
  --expect "mount: ext2 on /mnt dev 2" \
  --expect-count "Hello, world" 2 \
  --expect-count "ext2-first" 2 \
  --expect-count "ext2-second" 2 \
  --expect-count "ext2-nested" 2 \
  --expect "bigfile: ok" \
  --expect "0 1 1048576" \
  --expect-count "EXT2_DONE" 2 \
  --reject "panic:" \
  --reject "kerneltrap" \
  --reject "mount: failed" \
  --reject "failed to delete" \
  --reject "open /mnt/a.txt failed" \
  --reject "cannot stat" \
  --reject "open /mnt/verylongfilename.txt failed" \
  --reject "mkdir: /mnt/d1 failed" \
  --reject "bigfile: FAIL" \
  --final-wait 150
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

echo "[ext2] result: $result"
echo "[ext2] logs: $LOG_DIR"

if [ "$result" != "PASS" ]; then
  exit 1
fi
