"""The launcher's icon (src/launcher/assets/launcher.ico): the launcher's sign on the suit's red - an oval
that leans, a ring of yellow and orange around black, two figures in it side by side (the two players);
drawn large and scaled down. Needs Pillow.

    python tools/launcher_icon.py src/launcher/assets/launcher.ico"""
import math
import sys

from PIL import Image, ImageDraw

S = 1024
C = S / 2


def gradient(top, bottom, y0, y1):
    """A picture of S x S going from `top` at y0 to `bottom` at y1."""
    g = Image.new("RGBA", (S, S))
    px = g.load()
    for y in range(S):
        t = min(1.0, max(0.0, (y - y0) / (y1 - y0)))
        col = tuple(int(round(top[i] + (bottom[i] - top[i]) * t)) for i in range(3)) + (255,)
        for x in range(S):
            px[x, y] = col
    return g


def shape(draw_fn):
    m = Image.new("L", (S, S), 0)
    draw_fn(ImageDraw.Draw(m))
    return m


def rgb(v):
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
# the suit: a rounded square, lit from above
img.paste(gradient(rgb(0xEC2C1E), rgb(0x8F0C12), 40, S - 40),
          mask=shape(lambda d: d.rounded_rectangle([24, 24, S - 24, S - 24], radius=230, fill=255)))

# the oval and its ring, leaning
oval = Image.new("RGBA", (S, S), (0, 0, 0, 0))
rx, ry = 430, 312
ImageDraw.Draw(oval).ellipse([C - rx - 30, C - ry - 30, C + rx + 30, C + ry + 30], fill=(0, 0, 0, 255))
oval.paste(gradient(rgb(0xFFD63C), rgb(0xC9480C), C - ry, C + ry),
           mask=shape(lambda d: d.ellipse([C - rx, C - ry, C + rx, C + ry], fill=255)))
ImageDraw.Draw(oval).ellipse([C - rx * 0.81, C - ry * 0.75, C + rx * 0.81, C + ry * 0.75], fill=(0, 0, 0, 255))
img.alpha_composite(oval.rotate(12, resample=Image.BICUBIC))

# the two figures: a head and a body each, leaning like the letters
figs = Image.new("RGBA", (S, S), (0, 0, 0, 0))
h = ry * 0.55
for cx, top, bottom in ((C - ry * 0.34, 0xFFEA7A, 0xF78F1E), (C + ry * 0.34, 0xFFF4E4, 0xDFC4AE)):
    def fig(d, cx=cx):
        d.ellipse([cx - h * 0.30, C - h * 0.96, cx + h * 0.30, C - h * 0.36], fill=255)
        d.rounded_rectangle([cx - h * 0.27, C - h * 0.24, cx + h * 0.27, C + h * 0.96], radius=h * 0.14, fill=255)
    figs.paste(gradient(rgb(top), rgb(bottom), C - h * 0.96, C + h * 0.96), mask=shape(fig))
k = math.tan(math.radians(10))
figs = figs.transform((S, S), Image.AFFINE, (1, k, -k * C, 0, 1, 0), resample=Image.BICUBIC)
img.alpha_composite(figs)

img.save(sys.argv[1], sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (40, 40), (48, 48), (64, 64), (256, 256)])
if len(sys.argv) > 2:
    img.resize((256, 256), Image.LANCZOS).save(sys.argv[2])
print("icon written:", sys.argv[1])
