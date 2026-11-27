#!/usr/bin/env python3

#
# python script that tests xv6 without having to boot it and type to its shell
#
# ./test-xv6.py usertests  (runs usertests)
# ./test-xv6.py -q usertests (runs the quick tests of usertests)
# ./test-xv6.py crash  (runs the crash tests)
# ./test-xv6.py log (runs the log crash test)

import argparse, os, inspect, re, select, signal, subprocess, sys, time
from subprocess import run

parser = argparse.ArgumentParser()
parser.add_argument('testrex', help="test name or regular expression")
parser.add_argument("-q", action='store_true', help="usertests quick")
args = parser.parse_args()

TEST_ARCH = os.environ.get("ARCH") or os.environ.get("TEST_ARCH") or "riscv"

class QEMU(object):

    def __init__(self, reset=False):
        if reset:
            self.build_xv6()
            self.reset_fs()
        q = ["make", f"ARCH={TEST_ARCH}", "qemu"]
        self.proc = subprocess.Popen(q, stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT,
                                      preexec_fn=os.setsid)
        self.output = ""
        self.outbytes = bytearray()
        self.wait_output(r'(?:^|\n)\$ ', timeout=30)

    def reset_fs(self):
        try:
            fs_img = os.path.join("..", "build", TEST_ARCH, "fs.img")
            if os.path.exists(fs_img):
                os.remove(fs_img)
            run(["make", f"ARCH={TEST_ARCH}", "fs.img"], check=True)
        except (OSError, subprocess.CalledProcessError) as e:
            print(f"reset_fs failed: {e}")
            sys.exit(1)

    def build_xv6(self):
        try:
            run(["make", f"ARCH={TEST_ARCH}", "kernel/kernel"], check=True)
        except subprocess.CalledProcessError as e:
            print(f"build_xv6 failed with exit code {e.returncode}")
            sys.exit(1)

    def save_output(self):
        try:
            with open("test-xv6.out", "w") as f:
                f.write(self.output)
        except OSError as e:
            print("Provided a bad results path. Error:", e)

    def cmd(self, c):
        if isinstance(c, str):
            c = c.encode('utf-8')
        self.proc.stdin.write(c)
        self.proc.stdin.flush()

    def crash(self):
        try:
            pgid = os.getpgid(self.proc.pid)
        except ProcessLookupError:
            pgid = self.proc.pid
        ps = run(['ps', '-o', 'pid=,comm=', '-g', str(pgid)],
                 stdout=subprocess.PIPE, encoding='utf8')
        qemu_pids = []
        for line in ps.stdout.splitlines():
            parts = line.strip().split(None, 1)
            if len(parts) == 2 and parts[1].startswith("qemu-system-"):
                qemu_pids.append(int(parts[0]))
        if not qemu_pids:
            ps = run(['ps', '-opid', '--no-headers', '--ppid', str(self.proc.pid)],
                     stdout=subprocess.PIPE, encoding='utf8')
            qemu_pids = [int(line) for line in ps.stdout.splitlines() if line.strip()]
        if not qemu_pids:
            print("no qemu")
            self.stop()
            sys.exit(1)
        for pid in qemu_pids:
            print("kill", pid)
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def stop(self):
        try:
            os.killpg(self.proc.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            self.proc.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(self.proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                self.proc.wait(timeout=2.0)
            except Exception:
                pass

    def read(self, timeout=0.5, idle_timeout=0.05):
        fd = self.proc.stdout.fileno()
        hard_deadline = time.time() + timeout
        deadline = hard_deadline
        while True:
            rem = max(0.0, deadline - time.time())
            r, _, _ = select.select([fd], [], [], rem)
            if not r:
                break
            buf = os.read(fd, 4096)
            if not buf:
                break
            self.outbytes.extend(buf)
            self.output = self.outbytes.decode("utf-8", "replace")
            deadline = min(hard_deadline, time.time() + idle_timeout)

    def wait_output(self, pattern, timeout=30, start_pos=0):
        deadline = time.time() + timeout
        while True:
            if re.search(pattern, self.output[start_pos:], re.M):
                return True
            if self.proc.poll() is not None:
                self.read(timeout=0.1)
                return bool(re.search(pattern, self.output[start_pos:], re.M))
            rem = deadline - time.time()
            if rem <= 0:
                return False
            self.read(timeout=min(0.5, rem))

    def lines(self):
        return self.output.splitlines()

    def error(self, regexps=()):
        print("FAIL: match failed", regexps)
        tail = self.lines()[-30:]
        if tail:
            print("--- qemu output tail ---")
            for line in tail:
                print(line)
        self.save_output()
        self.stop()
        sys.exit(1)

    def match(self, *regexps, exit=True):
        lines = self.lines()
        last = -1
        for i, line in enumerate(lines):
            if any(re.match(r, line) for r in regexps):
                print(line)
                last = i
        if last == -1 and exit:
            self.error(regexps)
        l = ""
        if last >= 0:
            l = lines[last]
        return last >= 0, l

    def monitor(self, *regexps, progress="", fail="", timeout):
        deadline = time.time() + timeout
        printed = 0
        while True:
            timeleft = deadline - time.time()
            if timeleft <= 0:
                self.error(regexps)
            self.read(timeout=min(1.0, timeleft))
            lines = self.lines()
            complete = len(lines)
            if complete > 0 and not self.output.endswith(("\n", "\r")):
                complete -= 1
            for line in lines[printed:complete]:
                if any(re.match(r, line) for r in regexps):
                    print(line)
                    return
                if fail and re.search(fail, line):
                    print(line)
                    self.error(regexps)
                if progress and re.match(progress, line):
                    print(line)
            printed = complete
            if self.proc.poll() is not None:
                for line in lines[printed:]:
                    if any(re.match(r, line) for r in regexps):
                        print(line)
                        return
                self.error(regexps)

def crash_log(delay=1.5):
    q = QEMU(True)
    pos = len(q.output)
    t0 = time.time()
    q.cmd("logstress f0 f1 f2 f3 f4 f5\n")
    # Monitor stdout while waiting: on fast runners (e.g. LoongArch CI)
    # logstress can fill fs.img and exit in <1.5s, leaving an empty log if we
    # keep sleeping blindly past completion.
    finished_early = q.wait_output(
        r'(?:^|\n)\$ |balloc: out of blocks',
        timeout=delay,
        start_pos=pos,
    )
    elapsed = time.time() - t0
    q.crash()
    q.stop()
    return finished_early, elapsed

def recover_log():
    q = QEMU()
    ok, _ = q.match('^recovering', exit=False)
    if ok:
        pos = len(q.output)
        q.cmd("ls\n")
        q.wait_output(r'(?:^|\n)\$ ', timeout=15, start_pos=pos)
        q.match('f5')
    q.stop()
    return ok

def forphan():
    q = QEMU(True)
    pos = len(q.output)
    q.cmd("forphan\n")
    q.wait_output(r'^wait\b', timeout=15, start_pos=pos)
    q.match('wait')
    q.crash()
    q.stop()

def dorphan():
    q = QEMU(True)
    pos = len(q.output)
    q.cmd("dorphan\n")
    q.wait_output(r'^wait\b', timeout=15, start_pos=pos)
    q.match('wait')
    q.crash()
    q.stop()

def recover_orphan():
    q = QEMU()
    q.match('^ireclaim')
    q.stop()

def test_log():
    print("Test recovery of log")
    # Physical redo WAL spends roughly 1/3 of each commit cycle inside
    # install_trans() (between committing and clearing the log header), so
    # allow enough independent random-crash attempts and auto-calibrate the
    # crash delay if logstress finishes early on a fast host.
    base_delay = 1.5
    jitters = (0.0, 0.12, -0.10, 0.20, -0.15)
    for i in range(25):
        delay = max(0.10, base_delay * (1.0 + jitters[i % len(jitters)]))
        finished_early, elapsed = crash_log(delay)
        if finished_early:
            base_delay = max(0.10, min(base_delay * 0.6, elapsed * 0.55))
            print(f"log attempt {i+1}: logstress finished early in {elapsed:.2f}s, recalibrating delay to {base_delay:.2f}s")
            continue
        ok = recover_log()
        if ok:
            print("OK")
            return
        print(f"log attempt {i+1} (delay={delay:.2f}s)")
    print("FAIL")
    sys.exit(1)
    
def test_forphan():
    print("Test recovery of an orphaned file")
    forphan()
    recover_orphan()
    print("OK")

def test_dorphan():
    print("Test recovery of an orphaned file")
    dorphan()
    recover_orphan()
    print("OK")

def test_crash():
    test_log()
    test_forphan()
    test_dorphan()

def test_usertests(test=""):
    timeout = 600
    opt = ""
    if args.q:
        opt += " -q"
        timeout = 300
    if test != "":
        opt += " " + test
    q = QEMU(True)
    q.cmd("usertests" + opt + "\n")
    q.monitor('^ALL TESTS PASSED', progress='test',
              fail='SOME TESTS FAILED|panic:|kerneltrap', timeout=timeout)
    q.stop()

def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(line_buffering=True)
    print(args)
    rex = r'%s' % args.testrex
    funcs = [(obj,name) for name,obj in inspect.getmembers(sys.modules[__name__]) 
                     if (inspect.isfunction(obj) and 
                         name.startswith('test'))]
    none = True
    for (f,n) in funcs:
        if re.search(rex, n):
            none = False
            f()
    if none:
        test_usertests(test=args.testrex)

main()
