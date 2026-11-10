#!/usr/bin/env python3
"""Generates the built-in default cover (128x128) and writes source/placeholder.h.
Needs Pillow.  Run:  python3 tools/make_cover.py"""
import math, os, glob
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
S, N = 4, 128                      # supersampling, final size
W = N * S

TOP, BOT = (22, 30, 46), (8, 10, 15)
ACC, WHITE = (61, 156, 255), (244, 247, 252)

# vertical gradient
img = Image.new("RGB", (W, W))
px = img.load()
for y in range(W):
    t = y / (W - 1)
    c = tuple(int(TOP[i] + (BOT[i] - TOP[i]) * t) for i in range(3))
    for x in range(W):
        px[x, y] = c

# soft blue glow behind the note
glow = Image.new("RGB", (W, W), (0, 0, 0))
gd = ImageDraw.Draw(glow)
cx, cy, r = W * 0.5, W * 0.42, W * 0.34
gd.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(28, 76, 140))
glow = glow.filter(ImageFilter.GaussianBlur(W * 0.12))
img = ImageChops.add(img, glow)

d = ImageDraw.Draw(img)

def head(x, y, rx, ry, ang=-22):
    h = Image.new("L", (W, W), 0)
    hd = ImageDraw.Draw(h)
    hd.ellipse((x - rx, y - ry, x + rx, y + ry), fill=255)
    h = h.rotate(ang, center=(x, y), resample=Image.BICUBIC)
    img.paste(Image.new("RGB", (W, W), WHITE), (0, 0), h)

# beamed pair of eighth notes
u = W / 128.0
x1, y1 = 40 * u, 74 * u
x2, y2 = 78 * u, 66 * u
rx, ry = 9.5 * u, 7 * u
head(x1, y1, rx, ry)
head(x2, y2, rx, ry)
sw = 3.6 * u
for (x, y) in ((x1, y1), (x2, y2)):
    d.rectangle((x + rx * 0.75 - sw, y - 44 * u, x + rx * 0.75, y), fill=WHITE)
beam = [(x1 + rx * 0.75 - sw, y1 - 44 * u), (x2 + rx * 0.75, y2 - 44 * u),
        (x2 + rx * 0.75, y2 - 34 * u), (x1 + rx * 0.75 - sw, y1 - 34 * u)]
d.polygon(beam, fill=WHITE)

# wordmark
font = None
for path in glob.glob("/usr/share/fonts/**/DejaVuSans-Bold.ttf", recursive=True) + \
           glob.glob("/usr/share/fonts/**/LiberationSans-Bold.ttf", recursive=True):
    font = ImageFont.truetype(path, int(11 * u)); break
if font is None:
    font = ImageFont.load_default(int(11 * u))
text, track = "NOISE", 3.2 * u
widths = [d.textlength(ch, font=font) for ch in text]
total = sum(widths) + track * (len(text) - 1)
x = (W - total) / 2
for ch, w in zip(text, widths):
    d.text((x, 100 * u), ch, font=font, fill=WHITE)
    x += w + track
d.rectangle((W * 0.5 - 14 * u, 96 * u, W * 0.5 + 14 * u, 97.6 * u), fill=ACC)

out = img.resize((N, N), Image.LANCZOS)
png = os.path.join(ROOT, "resources", "cover.png")
out.save(png, optimize=True)
data = open(png, "rb").read()
lines = []
for i in range(0, len(data), 16):
    lines.append("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",")
hdr = ("#pragma once\n/* Default cover art (128x128 PNG, from tools/make_cover.py). "
       "Embedded as a C array so the build needs no extra tooling. */\n")
with open(os.path.join(ROOT, "source", "placeholder.h"), "w") as f:
    f.write(hdr + "static const unsigned char placeholder_png[] = {\n" + "\n".join(lines) +
            "\n};\nstatic const unsigned int placeholder_png_len = %d;\n" % len(data))
print("cover written,", len(data), "bytes")
