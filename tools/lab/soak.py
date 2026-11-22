"""Soak test: for each level, two copies in one network game play at random; the sim logs must agree.

  python soak.py <seconds per level> <scene> [<scene> ...]

The host's frame is 16:9 (1280x720), the guest's 4:3 (1024x768); both have their own cameras.
Shots of both copies are kept in <copy>/soak-<scene>. Environment: IN2_SOAK_SEED (default 1).
"""
import glob, os, re, shutil, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab

HERE = os.path.dirname(os.path.abspath(__file__))
A = os.path.join(lab.WORK, "test-in2")
B = os.path.join(lab.WORK, "test-in2b")
seconds = sys.argv[1]
seed = os.environ.get("IN2_SOAK_SEED", "1")
START = "420 start; 600 down; 700 a; 1000 a; 1300 a"
for scene in sys.argv[2:]:
    env = dict(os.environ, IN2_BOOT=scene, IN2_MASH="%s,1500,60000" % seed, IN2_A_VIDEO="resolution = 1280x720",
               IN2_B_VIDEO="resolution = 1024x768", IN2_VSYNC="-1", IN2_TAIL="400",
               IN2_LOG="DESYNC|exception|stack:|session is over|ERROR")
    out = subprocess.run([sys.executable, os.path.join(HERE, "run_net2.py"), seconds, START,
                          "shots = 1500, 2500, 4000, 6000, 9000, 13000"], capture_output=True, text=True, env=env).stdout
    lines = out.splitlines()
    verdict = next((l for l in lines if l.startswith("IDENTICAL") or l.startswith("DIFFERENT") or l.startswith("NO FRAMES")), "?")
    frames = next((l for l in lines if l.startswith("frames:")), "")
    bad = [l.strip() for l in lines if re.search(r"DESYNC|exception|stack:|session is over|ERROR", l)]
    scenes = set()
    p = os.path.join(A, "in2net-sim.log")
    if os.path.exists(p):
        for l in open(p):
            parts = l.split()
            if len(parts) > 1:
                scenes.add(parts[1])
    print("%-5s %s | %s | scenes seen: %s" % (scene, verdict, frames, " ".join(sorted(scenes))))
    for l in bad[:6]:
        print("      " + l[:230])
    if verdict.startswith("DIFFERENT"):
        i = lines.index(verdict)
        for l in lines[i + 1:i + 7]:
            print("      " + l)
    for copy in (A, B):
        dst = os.path.join(copy, "soak-" + scene)
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(os.path.join(copy, "screenshots"), dst)
    sys.stdout.flush()
for copy in (A, B):
    lab.set_boot(copy, "UT01")
