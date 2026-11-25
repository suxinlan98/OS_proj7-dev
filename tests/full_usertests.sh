#!/usr/bin/env bash
# Run the COMPLETE usertests suite (no -q; includes the six slow tests
# bigdir/manywrites/badwrite/execout/diskfull/outofinodes) on one
# architecture, mirroring the official CPUS=3 QEMU environment.
#
# Usage: bash tests/full_usertests.sh [riscv|loongarch]
#
# This is part of the pre-push gate together with tests/run_all.sh:
# a push must not happen unless both are green.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
XV6_DIR="$ROOT_DIR/src"
ARCH="${1:-riscv}"
LOG_SUFFIX=""
if [ "$ARCH" != "riscv" ]; then
  LOG_SUFFIX="-$ARCH"
fi
LOG="$SCRIPT_DIR/test_baseline/logs/usertests-full$LOG_SUFFIX.log"
BUILD_LOG="$SCRIPT_DIR/test_baseline/logs/usertests-full-build$LOG_SUFFIX.log"

mkdir -p "$(dirname "$LOG")"

echo "[full_usertests] building $ARCH kernel and fs.img"
(
  cd "$XV6_DIR" || exit 1
  make ARCH="$ARCH" clean
  rm -f fs.img ext2.img fat32.img
  make ARCH="$ARCH" kernel/kernel fs.img
) > "$BUILD_LOG" 2>&1 || {
  echo "[full_usertests] build failed (see $BUILD_LOG)"
  tail -n 30 "$BUILD_LOG"
  exit 1
}

echo "[full_usertests] running complete usertests on $ARCH (CPUS=3, no -q)"
setsid bash -c '
  cd "$1" || exit 1
  ( sleep 8; printf "usertests\n"; sleep 780 ) | timeout 800 make ARCH="$2" CPUS=3 qemu
' _ "$XV6_DIR" "$ARCH" > "$LOG" 2>&1 &
runpid=$!

cleanup() {
  kill -TERM -- -"$runpid" 2>/dev/null || true
  sleep 0.5
  kill -KILL -- -"$runpid" 2>/dev/null || true
  wait "$runpid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# The guest verdict appears long before the stdin pipe closes; watch
# the log and stop QEMU as soon as the outcome is known.
for i in $(seq 1 165); do
  if grep -qE "ALL TESTS PASSED|SOME TESTS FAILED|FAILED -- lost|panic:|kerneltrap" "$LOG" 2>/dev/null; then
    break
  fi
  if ! kill -0 "$runpid" 2>/dev/null; then
    break
  fi
  sleep 5
done
cleanup
trap - EXIT INT TERM

if grep -q "ALL TESTS PASSED" "$LOG" \
   && ! grep -q "SOME TESTS FAILED" "$LOG" \
   && ! grep -q "FAILED -- lost" "$LOG" \
   && ! grep -q "panic:" "$LOG" \
   && ! grep -q "kerneltrap" "$LOG"; then
  echo "[full_usertests] $ARCH: PASS"
  echo "[full_usertests] log: $LOG"
  exit 0
fi

echo "[full_usertests] $ARCH: FAIL (see $LOG)"
exit 1
