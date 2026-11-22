"""Determinism check: the same scripted run twice (one after the other), sim logs compared.

  python run_twice.py <seconds> "<script>" [ini lines]
"""
import os, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab

HERE = os.path.dirname(os.path.abspath(__file__))
COPY = os.environ.get("IN2_COPY", os.path.join(lab.WORK, "test-in2"))
args = sys.argv[1:]
logs = []
for tag in ("A", "B"):
    env = dict(os.environ, IN2_LOG=os.environ.get("IN2_LOG", "exception|stack:|ended|killed|ERROR"), IN2_TAIL="12")
    fast = [] if (tag == "B" and os.environ.get("IN2_B_SLOW")) else ["fast = 1"]
    out = subprocess.run([sys.executable, os.path.join(HERE, "run_one.py")] + args + ["sim_log = 1"] + fast,
                         capture_output=True, text=True, env=env).stdout
    print("--- run %s ---" % tag)
    print("\n".join(l for l in out.splitlines() if not l.startswith("shots:")))
    dst = os.path.join(COPY, "in2net-sim-%s.log" % tag)
    shutil.copyfile(os.path.join(COPY, "in2net-sim.log"), dst)
    logs.append(dst)
    rng = os.path.join(COPY, "in2net-rng.log")
    if os.path.exists(rng):
        shutil.copyfile(rng, os.path.join(COPY, "in2net-rng-%s.log" % tag))
        os.remove(rng)
    calls = os.path.join(COPY, "in2net-rngcalls.bin")
    if os.path.exists(calls):
        shutil.move(calls, os.path.join(COPY, "in2net-rngcalls-%s.bin" % tag))
    shots = os.path.join(COPY, "screenshots")
    keep = os.path.join(COPY, "screenshots-" + tag)
    if os.path.isdir(keep):
        shutil.rmtree(keep)
    if os.path.isdir(shots):
        shutil.copytree(shots, keep)


def strip(line):
    # the "stall" column is the clock's own bookkeeping; everything else must agree
    return line.split(" stall ")[0]


a = open(logs[0]).read().splitlines()
b = open(logs[1]).read().splitlines()
n = min(len(a), len(b))
first = next((i for i in range(n) if strip(a[i]) != strip(b[i])), None)
print("frames: A %d, B %d" % (len(a), len(b)))
if first is None:
    print("IDENTICAL over %d frames" % n)
else:
    print("DIFFERENT from line %d:" % (first + 1))
    for i in range(max(0, first - 2), min(n, first + 6)):
        print("  A", a[i])
        print("  B", b[i])
    same_stall = sum(1 for i in range(n) if a[i].split(" stall ")[-1] == b[i].split(" stall ")[-1])
    print("lines with the same stall value: %d of %d" % (same_stall, n))
