"""Two test copies in one network game (ENet on localhost), sim logs compared.

  python run_net2.py <seconds> "<script; lines>" [extra [debug] ini lines...]

Environment: IN2_A_VIDEO / IN2_B_VIDEO = extra [video] lines of the host / the guest (";" between lines);
IN2_BOOT=<scene> the level a new game starts in; IN2_MASH=<seed>[,first,last] random play of both players;
IN2_FAST=0 for real-time pace; IN2_DELAY=<frames>; IN2_B_INI="<extra [debug] lines for the guest>".
The script names players (p1 = host, p2 = guest); each copy takes its own player's presses.
"""
import glob, os, re, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab

A = os.path.join(lab.WORK, "test-in2")
B = os.path.join(lab.WORK, "test-in2b")
seconds = int(sys.argv[1])
script = sys.argv[2] if len(sys.argv) > 2 else ""
extra = "\n".join(sys.argv[3:])
fast = os.environ.get("IN2_FAST", "1")
delay = os.environ.get("IN2_DELAY", "2")


def ini(mode, name, more="", video=""):
    return """[log]
level = debug
[video]
mode = window
%s
[net]
mode = %s
connect = 127.0.0.1
name = %s
delay = %s
wait = 40
[debug]
guard = 1
no_activate = 1
offscreen = 1
multi = 1
mute = 1
save_dir = lab-save-net
sim_log = 1
fast = %s
autoquit = %d
script = in2net-script.txt
%s
%s
""" % (video.replace(";", "\n"), mode, name, delay, fast, seconds, extra, more)


if os.environ.get("IN2_MASH"):
    seed, first, last = (os.environ["IN2_MASH"].split(",") + ["1500", "30000"])[:3]
    script = script + ";" + ";".join(lab.mash(int(seed), int(first), int(last)))
for copy, mode, name, more, video in ((A, "host", "Hostov", "", os.environ.get("IN2_A_VIDEO", "")),
                                      (B, "join", "Gostev", os.environ.get("IN2_B_INI", ""), os.environ.get("IN2_B_VIDEO", ""))):
    lab.install(copy, ini(mode, name, more, video))
    lab.set_boot(copy, os.environ.get("IN2_BOOT", "UT01"))
    with open(os.path.join(copy, "in2net-script.txt"), "w") as fh:
        fh.write("\n".join(s.strip() for s in script.split(";")) + "\n")
    for f in glob.glob(os.path.join(copy, "screenshots", "*.bmp")) + glob.glob(os.path.join(copy, "screenshots", "*.png")):
        os.remove(f)
    for f in ("in2net-sim.log",):
        p = os.path.join(copy, f)
        if os.path.exists(p):
            os.remove(p)

ga = lab.Game(A)
time.sleep(1.0)
gb = lab.Game(B)
ra = ga.wait(seconds + 20)
rb = gb.wait(seconds + 20)
print("host :", ra)
print("guest:", rb)
want = os.environ.get("IN2_LOG", "net:|exception|stack:|ERROR|WARN|saves: red")
for tag, copy in (("host", A), ("guest", B)):
    p = os.path.join(copy, "in2net.log")
    if os.path.exists(p):
        lines = [l for l in open(p, encoding="utf-8", errors="replace").read().splitlines() if re.search(want, l)]
        for l in lines[-int(os.environ.get("IN2_TAIL", "14")):]:
            print("  %-5s %s" % (tag, l))


def load(copy):
    p = os.path.join(copy, "in2net-sim.log")
    return open(p).read().splitlines() if os.path.exists(p) else []


def strip(line):
    return line.split(" stall ")[0]


a, b = load(A), load(B)
n = min(len(a), len(b))
first = next((i for i in range(n) if strip(a[i]) != strip(b[i])), None)
print("frames: host %d, guest %d" % (len(a), len(b)))
if n == 0:
    print("NO FRAMES")
elif first is None:
    print("IDENTICAL over %d frames" % n)
else:
    print("DIFFERENT from line %d:" % (first + 1))
    for i in range(max(0, first - 2), min(n, first + 4)):
        print("  host ", a[i])
        print("  guest", b[i])
from PIL import Image
for copy in (A, B):
    for f in sorted(glob.glob(os.path.join(copy, "screenshots", "*.bmp"))):
        im = Image.open(f).convert("RGB")
        im.save(f[:-4] + ".png")
        os.remove(f)
