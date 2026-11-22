"""Sheet of a soak run's shots: one row per level - host and guest at two frames.

  python soak_sheet.py <out.png> <frame1> <frame2> <scene> [<scene> ...]
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab
from PIL import Image, ImageDraw

out, f1, f2 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
scenes = sys.argv[4:]
A = os.path.join(lab.WORK, "test-in2")
B = os.path.join(lab.WORK, "test-in2b")
th = 240
cells = [("HOST", A, f1, 427), ("HOST", A, f2, 427), ("GUEST", B, f1, 320), ("GUEST", B, f2, 320)]
sheet = Image.new("RGB", (sum(c[3] for c in cells) + 9, th * len(scenes)), (40, 40, 40))
d = ImageDraw.Draw(sheet)
for r, scene in enumerate(scenes):
    x = 0
    for tag, copy, frame, w in cells:
        p = os.path.join(copy, "soak-" + scene, "in2net_f%06d.png" % frame)
        if os.path.exists(p):
            sheet.paste(Image.open(p).convert("RGB").resize((w, th)), (x, r * th))
        d.rectangle([x, r * th, x + 120, r * th + 13], fill=(0, 0, 0))
        d.text((x + 3, r * th + 1), "%s %s %d" % (scene, tag, frame), fill=(255, 255, 0))
        x += w + 3
sheet.save(out)
print(out)
