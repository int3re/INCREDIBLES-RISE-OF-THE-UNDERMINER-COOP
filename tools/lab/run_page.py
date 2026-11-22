"""The network page, end to end: two test copies start alone, go to СЕТЕВАЯ ИГРА in the game's main menu and
meet there (script lines with "menu."), the host starts, both games start themselves again into the session
(script lines with "game."); the session's sim logs are compared.

  python run_page.py <seconds> "<pad script of the session; lines>" [extra [debug] ini lines...]

<seconds> is each game's [debug] autoquit: the first games need about 30 of them for the page, the
second ones play the session for all of them. The session's script: lines with "game." (and random play from
frame 1700 on, when the first games are gone).
Environment: IN2_PAGE_VIA = mock (default: the lab's Steam) | addr (by address, no Steam);
IN2_A_VIDEO / IN2_B_VIDEO, IN2_BOOT, IN2_MASH, IN2_FAST (default 0: the page's keys are timed in frames),
IN2_HOST_KEYS / IN2_GUEST_KEYS to replace the page's presses, IN2_B_INI = extra [debug] lines of the guest.
Shots of the first games (the page) are moved to <copy>/screenshots/page.
"""
import glob, os, re, shutil, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab

A = os.path.join(lab.WORK, "test-in2")
B = os.path.join(lab.WORK, "test-in2b")
seconds = int(sys.argv[1])
script = sys.argv[2] if len(sys.argv) > 2 else ""
extra = "\n".join(sys.argv[3:])
via = os.environ.get("IN2_PAGE_VIA", "mock")
fast = os.environ.get("IN2_FAST", "0")
mock_dir = os.path.join(lab.WORK, "test-in2-mock-steam")
if os.path.isdir(mock_dir):
    shutil.rmtree(mock_dir)

# title -> main menu -> up to СЕТЕВАЯ ИГРА -> the page; host: СОЗДАТЬ ИГРУ, later НАЧАТЬ ИГРУ;
# guest: down to the host's game (or to the address row, typed) and in
HOST_KEYS = os.environ.get("IN2_HOST_KEYS", "420 menu.start; 640 menu.up; 760 menu.a; 960 menu.a; 1560 menu.a")
GUEST_KEYS = os.environ.get("IN2_GUEST_KEYS", "420 menu.start; 640 menu.up; 760 menu.a; 1260 menu.down; 1320 menu.a" if via == "mock" else
                            "420 menu.start; 640 menu.up; 760 menu.a; 1100 menu.down; 1160 type.127.0.0.1; 1260 menu.a")


def ini(name, slot, more="", video=""):
    return """[log]
level = debug
[video]
mode = window
%s
[net]
mode = off
name = %s
transport = %s
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
mock_slot = %d
mock_dir = %s
%s
%s
""" % (video.replace(";", "\n"), name, "mock" if via == "mock" else "enet", fast, seconds, slot, mock_dir, extra, more)


if os.environ.get("IN2_MASH"):
    seed, first, last = (os.environ["IN2_MASH"].split(",") + ["1700", "30000"])[:3]
    script = script + ";" + ";".join(lab.mash(int(seed), int(first), int(last)))
for copy, name, slot, keys, more, video in ((A, "Hostov", 1, HOST_KEYS, "", os.environ.get("IN2_A_VIDEO", "")),
                                            (B, "Gostev", 2, GUEST_KEYS, os.environ.get("IN2_B_INI", ""), os.environ.get("IN2_B_VIDEO", ""))):
    lab.install(copy, ini(name, slot, more, video))
    dll = os.path.join(lab.DIST, "steam_api.dll")
    if os.path.exists(dll):
        shutil.copyfile(dll, os.path.join(copy, "steam_api.dll"))
    lab.set_boot(copy, os.environ.get("IN2_BOOT", "UT01"))
    with open(os.path.join(copy, "in2net-script.txt"), "w") as fh:
        fh.write("\n".join(s.strip() for s in (keys + ";" + script).split(";")) + "\n")
    shots = os.path.join(copy, "screenshots")
    if os.path.isdir(os.path.join(shots, "page")):
        shutil.rmtree(os.path.join(shots, "page"))
    for f in glob.glob(os.path.join(shots, "*.bmp")) + glob.glob(os.path.join(shots, "*.png")):
        os.remove(f)
    for f in ("in2net-sim.log", "in2net-menu.log"):
        p = os.path.join(copy, f)
        if os.path.exists(p):
            os.remove(p)

ga = lab.Game(A)
time.sleep(1.0)
gb = lab.Game(B)
# the first games: until they close themselves (the start) or their time is up
ra = ga.wait(seconds + 10)
rb = gb.wait(seconds + 10)
print("first host :", ra)
print("first guest:", rb)
for copy in (A, B):  # the page's shots, before the second games reach the same frame numbers
    shots = os.path.join(copy, "screenshots")
    os.makedirs(os.path.join(shots, "page"), exist_ok=True)
    for f in glob.glob(os.path.join(shots, "*.bmp")):
        shutil.move(f, os.path.join(shots, "page", os.path.basename(f)))
print("second games:", lab.wait_copies((A, B), seconds + 25))

want = os.environ.get("IN2_LOG", "lobby:|net:|Steam|started again|exception|stack:|ERROR|WARN")
for tag, copy in (("host", A), ("guest", B)):
    for name in ("in2net-menu.log", "in2net.log"):
        p = os.path.join(copy, name)
        if not os.path.exists(p):
            print("  %-5s (no %s)" % (tag, name))
            continue
        lines = [l for l in open(p, encoding="utf-8", errors="replace").read().splitlines() if re.search(want, l)]
        for l in lines[-int(os.environ.get("IN2_TAIL", "12")):]:
            print("  %-5s %s %s" % (tag, "page" if name != "in2net.log" else "    ", l[:210]))


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
    for f in sorted(glob.glob(os.path.join(copy, "screenshots", "*.bmp")) + glob.glob(os.path.join(copy, "screenshots", "page", "*.bmp"))):
        im = Image.open(f).convert("RGB")
        im.save(f[:-4] + ".png")
        os.remove(f)
