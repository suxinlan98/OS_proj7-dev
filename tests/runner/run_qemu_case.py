#!/usr/bin/env python3
import argparse
import os
import signal
import subprocess
import sys
import time


def read_commands(path):
    commands = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            commands.append(line)
    return commands


def main():
    parser = argparse.ArgumentParser(description="Run an xv6 QEMU shell test case.")
    parser.add_argument("--case", required=True)
    parser.add_argument("--xv6-dir", required=True)
    parser.add_argument("--commands", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--expect", action="append", default=[])
    parser.add_argument("--expect-count", action="append", nargs=2,
                        metavar=("PATTERN", "COUNT"), default=[],
                        help="require PATTERN to occur exactly COUNT times")
    parser.add_argument("--reject", action="append", default=[])
    parser.add_argument("--boot-wait", type=float, default=5.0)
    parser.add_argument("--step-wait", type=float, default=1.0)
    parser.add_argument("--cpus", type=int, default=1,
                        help="number of guest CPUs (-smp); default 1")
    parser.add_argument("--prompt-wait", action="store_true",
                        help="wait for the shell prompt after each command "
                             "instead of a fixed delay; prevents dropping "
                             "input when the guest is busy (UART buffer is 128B)")
    parser.add_argument("--cmd-timeout", type=float, default=180.0,
                        help="per-command prompt wait limit in seconds")
    parser.add_argument("--final-wait", type=float, default=0.0,
                        help="after the last command, keep draining up to this many "
                             "seconds until all --expect patterns have appeared")
    parser.add_argument("--live", action="store_true",
                        help="mirror QEMU output to the terminal while saving the log")
    args = parser.parse_args()

    expected_counts = []
    for pattern, count_text in args.expect_count:
        try:
            count = int(count_text)
        except ValueError:
            parser.error(f"invalid --expect-count COUNT: {count_text}")
        if count < 0:
            parser.error("--expect-count COUNT must be non-negative")
        expected_counts.append((pattern, count))

    commands = read_commands(args.commands)
    os.makedirs(os.path.dirname(args.log), exist_ok=True)

    print(f"== xv6 VFS Test Runner: {args.case} ==", flush=True)
    print(f"[case] commands: {args.commands}", flush=True)
    print(f"[case] raw log:  {args.log}", flush=True)
    arch = os.environ.get("ARCH", "riscv")
    print(f"[case] arch:     {arch}", flush=True)
    print(flush=True)

    qemu = subprocess.Popen(
        ["make", f"ARCH={arch}", f"CPUS={args.cpus}", "qemu"],
        cwd=args.xv6_dir,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        preexec_fn=os.setsid,
    )
    os.set_blocking(qemu.stdout.fileno(), False)
    log = bytearray()

    def drain(seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                chunk = os.read(qemu.stdout.fileno(), 4096)
                if chunk:
                    log.extend(chunk)
                    if args.live:
                        sys.stdout.buffer.write(chunk)
                        sys.stdout.buffer.flush()
                else:
                    # EOF on QEMU stdout pipe
                    break
            except BlockingIOError:
                if qemu.poll() is not None:
                    break
                time.sleep(0.05)

    def drain_nowait():
        while True:
            try:
                chunk = os.read(qemu.stdout.fileno(), 4096)
                if not chunk:
                    break
                log.extend(chunk)
                if args.live:
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()
            except BlockingIOError:
                break

    def text():
        return log.decode("utf-8", "replace")

    def find_rejected(current):
        return [pattern for pattern in args.reject if pattern in current]

    def send_command(line):
        try:
            qemu.stdin.write((line + "\n").encode("utf-8"))
            qemu.stdin.flush()
            return True
        except (BrokenPipeError, OSError):
            drain_nowait()
            return False

    def wait_for_initial_prompt(timeout):
        end = time.time() + timeout
        quiet_since = None
        while time.time() < end:
            before = len(log)
            drain(0.15)
            current = text()
            hit = find_rejected(current)
            if hit:
                return False, "Rejected output: " + ", ".join(hit)
            if qemu.poll() is not None:
                drain_nowait()
                current = text()
                hit = find_rejected(current)
                if hit:
                    return False, "Rejected output: " + ", ".join(hit)
                return False, f"QEMU exited unexpectedly (rc={qemu.returncode})"
            if len(log) != before:
                quiet_since = None
                continue
            if not current.rstrip().endswith("$"):
                quiet_since = None
                continue
            if quiet_since is None:
                quiet_since = time.time()
            if time.time() - quiet_since >= 0.3:
                return True, ""
        return False, "Initial shell prompt timeout"

    def wait_for_sentinel(marker, start_offset, timeout):
        end = time.time() + timeout
        while time.time() < end:
            drain(0.15)
            current = text()
            hit = find_rejected(current)
            if hit:
                return False, "Rejected output: " + ", ".join(hit)
            tail = log[start_offset:].decode("utf-8", "replace")
            # Marker appears once in console input echo ("echo <marker>")
            # and a second time when echo executes after the command finishes.
            if tail.count(marker) >= 2:
                # Give sh a brief moment to emit the next "$ " prompt.
                settle_end = time.time() + 1.0
                while time.time() < settle_end:
                    if text().rstrip().endswith("$"):
                        break
                    drain(0.05)
                return True, ""
            if qemu.poll() is not None:
                drain_nowait()
                current = text()
                hit = find_rejected(current)
                if hit:
                    return False, "Rejected output: " + ", ".join(hit)
                return False, f"QEMU exited unexpectedly (rc={qemu.returncode})"
        return False, "Command prompt timeout"

    result = "PASS"
    failed_reason = ""

    try:
        drain(args.boot_wait)
        if args.prompt_wait:
            ok, reason = wait_for_initial_prompt(args.cmd_timeout)
            if not ok:
                result = "FAIL"
                failed_reason = reason
        for idx, command in enumerate(commands, 1):
            if result != "PASS":
                break
            prefix = "\n" if args.live else ""
            print(f"{prefix}[{idx:02d}/{len(commands):02d}] {command}", flush=True)
            if args.prompt_wait and command.endswith("&"):
                # Leave an intentional race window for the next foreground
                # command; that command will wait for the shell to settle.
                if not send_command(command):
                    result = "FAIL"
                    failed_reason = f"QEMU stdin closed while sending: {command}"
                    break
                drain(args.step_wait)
            elif args.prompt_wait:
                if not send_command(command):
                    result = "FAIL"
                    failed_reason = f"QEMU stdin closed while sending: {command}"
                    break
                # Wait briefly for the guest shell to consume the command line
                # before queueing the short sentinel command (128B UART buffer).
                cmd_echo_end = time.time() + 1.0
                cmd_start = len(log)
                while time.time() < cmd_echo_end:
                    drain(0.05)
                    if command in log[cmd_start:].decode("utf-8", "replace"):
                        break
                    if qemu.poll() is not None:
                        break
                marker = f"__XV6_DONE_{idx:03d}__"
                marker_offset = len(log)
                if not send_command(f"echo {marker}"):
                    result = "FAIL"
                    failed_reason = f"QEMU stdin closed after command: {command}"
                    break
                prompt_ready, reason = wait_for_sentinel(
                    marker, marker_offset, args.cmd_timeout
                )
                if os.environ.get("RUNNER_DEBUG"):
                    print(f"    [prompt-wait] ready={prompt_ready} "
                          f"at {len(text())} bytes",
                          flush=True)
                if not prompt_ready:
                    result = "FAIL"
                    failed_reason = (
                        f"{reason}: {command}"
                        if reason == "Command prompt timeout"
                        else reason
                    )
                    break
            else:
                if not send_command(command):
                    result = "FAIL"
                    failed_reason = f"QEMU stdin closed while sending: {command}"
                    break
                drain(args.step_wait)
            current = text()
            hit = find_rejected(current)
            if hit:
                result = "FAIL"
                failed_reason = "Rejected output: " + ", ".join(hit)
                break

        drain(0.5)
        if args.final_wait > 0 and result == "PASS":
            end = time.time() + args.final_wait
            while time.time() < end:
                current = text()
                if find_rejected(current):
                    break
                counts_ok = all(current.count(pattern) == count
                                for pattern, count in expected_counts)
                if (all(pattern in current for pattern in args.expect)
                        and counts_ok):
                    break
                if qemu.poll() is not None:
                    drain_nowait()
                    break
                drain(0.5)
        drain_nowait()
        current = text()
        hit = find_rejected(current)
        if hit and result == "PASS":
            result = "FAIL"
            failed_reason = "Rejected output: " + ", ".join(hit)
        if result == "PASS":
            missing = [pattern for pattern in args.expect if pattern not in current]
            if missing:
                result = "FAIL"
                failed_reason = "Missing expected output: " + ", ".join(missing)
            mismatched = [
                f"{pattern!r}: expected {count}, got {current.count(pattern)}"
                for pattern, count in expected_counts
                if current.count(pattern) != count
            ]
            if mismatched:
                result = "FAIL"
                failed_reason = "Output count mismatch: " + "; ".join(mismatched)
    finally:
        try:
            if qemu.poll() is None and qemu.stdin:
                qemu.stdin.write(b"\x01x")
                qemu.stdin.flush()
                qemu.wait(timeout=1.5)
        except Exception:
            pass
        try:
            if qemu.poll() is None:
                os.killpg(os.getpgid(qemu.pid), signal.SIGTERM)
        except Exception:
            pass
        time.sleep(0.2)
        with open(args.log, "wb") as f:
            f.write(log)

    print()
    for pattern in args.expect:
        marker = "PASS" if pattern in text() else "MISS"
        print(f"[expect:{marker}] {pattern}")
    for pattern, count in expected_counts:
        actual = text().count(pattern)
        marker = "PASS" if actual == count else "MISS"
        print(f"[expect-count:{marker}] {pattern}: {actual}/{count}")
    for pattern in args.reject:
        marker = "OK" if pattern not in text() else "HIT"
        print(f"[reject:{marker}] {pattern}")

    print()
    print(f"Result: {result}")
    if failed_reason:
        print(f"Reason: {failed_reason}")
    print(f"Log: {args.log}")

    return 0 if result == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
