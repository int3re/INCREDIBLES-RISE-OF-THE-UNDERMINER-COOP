"""Contact sheet of a test copy's shots: python sheet.py [copy dir] [columns] [first] [count] -> <copy>/screenshots/sheet.png"""
import glob, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab
from PIL import Image, ImageDraw

copy = sys.argv[1] if len(sys.argv) > 1 else os.path.join(lab.WORK, "test-in2")
cols = int(sys.argv[2]) if len(sys.argv) > 2 else 4
first = int(sys.argv[3]) if len(sys.argv) > 3 else 0
count = int(sys.argv[4]) if len(sys.argv) > 4 else 12
files = sorted(f for f in glob.glob(os.path.join(copy, "screenshots", "in2net_f*.png")))[first:first + count]
if not files:
    sys.exit("no shots")
tw, th = 400, 300
rows = (len(files) + cols - 1) // cols
sheet = Image.new("RGB", (cols * tw, rows * th), (20, 20, 20))
d = ImageDraw.Draw(sheet)
for i, f in enumerate(files):
    im = Image.open(f).convert("RGB").resize((tw, th))
    x, y = (i % cols) * tw, (i // cols) * th
    sheet.paste(im, (x, y))
    label = os.path.basename(f)[8:14].lstrip("0")
    d.rectangle([x, y, x + 60, y + 14], fill=(0, 0, 0))
    d.text((x + 3, y + 1), label, fill=(255, 255, 0))
out = os.path.join(copy, "screenshots", "sheet.png")
sheet.save(out)
print(out, len(files))
