"""One guarded lab run of a test copy.

  python run_one.py <seconds> "<script; lines>" [extra ini lines of [debug]...]

Script lines: "<frame>[-<frame>] [p2.]<control>" separated by ';' ("" = none).
"""
import glob, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab

COPY = os.environ.get("IN2_COPY", os.path.join(lab.WORK, "test-in2"))
seconds = int(sys.argv[1])
script = sys.argv[2] if len(sys.argv) > 2 else ""
extra = "\n".join(sys.argv[3:])
ini = """[log]
level = debug
[video]
mode = %s
vsync = %s
%s
[debug]
guard = 1
no_activate = 1
offscreen = 1
save_dir = lab-save
mute = 1
autoquit = %d
%s
%s
""" % (os.environ.get("IN2_MODE", "window"), os.environ.get("IN2_VSYNC", "0"), os.environ.get("IN2_VIDEO", "").replace(";", "\n"), seconds, "script = in2net-script.txt" if script else "", extra)
if os.environ.get("IN2_MASH"):
    seed, first, last = (os.environ["IN2_MASH"].split(",") + ["1500", "30000"])[:3]
    script = script + ";" + ";".join(lab.mash(int(seed), int(first), int(last)))
lab.install(COPY, ini)
lab.set_boot(COPY, os.environ.get("IN2_BOOT", "UT01"))
with open(os.path.join(COPY, "in2net-script.txt"), "w") as fh:
    fh.write("\n".join(s.strip() for s in script.split(";")) + "\n")
for f in glob.glob(os.path.join(COPY, "screenshots", "*.bmp")) + glob.glob(os.path.join(COPY, "screenshots", "*.png")):
    os.remove(f)
g = lab.Game(COPY)
print(g.wait(seconds + 15))
want = os.environ.get("IN2_LOG", "exception|stack|eax|ended|killed|ERROR|WARN|input:|fps|sim:")
import re
p = os.path.join(COPY, "in2net.log")
if os.path.exists(p):
    lines = open(p, encoding="utf-8", errors="replace").read().splitlines()
    shown = [l for l in lines if re.search(want, l)]
    for l in shown[-int(os.environ.get("IN2_TAIL", "60")):]:
        print(l)
# shots -> png for viewing
from PIL import Image
for f in sorted(glob.glob(os.path.join(COPY, "screenshots", "*.bmp"))):
    im = Image.open(f).convert("RGB")
    if im.width > 1280:
        im = im.resize((1280, int(im.height * 1280 / im.width)))
    im.save(f[:-4] + ".png")
    os.remove(f)
print("shots:", " ".join(os.path.basename(p) for p in sorted(glob.glob(os.path.join(COPY, "screenshots", "*.png")))))



