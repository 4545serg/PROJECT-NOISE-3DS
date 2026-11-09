#!/usr/bin/env python3
"""Generates resources/icon.png (48x48), resources/banner.png (256x128) and
resources/banner.wav (3 s) for PROJECT SLITHER. Needs Pillow."""
import math, os, struct, wave
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources")
os.makedirs(OUT, exist_ok=True)

BG, BODY, SEL = (11, 26, 18), (57, 217, 138), (31, 77, 54)


def snake(draw, x0, y0, size, amp=0.22, n=20):
    pts = []
    for k in range(n):
        t = k / (n - 1)
        x = x0 + size * (0.10 + 0.80 * t)
        y = y0 + size * (0.50 + amp * math.sin(t * 7.0 + 0.6))
        r = size * (0.035 + 0.075 * t)
        pts.append((x, y, r))
    for x, y, r in pts:
        draw.ellipse((x - r, y - r, x + r, y + r), fill=BODY)
    x, y, r = pts[-1]
    draw.ellipse((x + r * .05, y - r * .55, x + r * .55, y - r * .05), fill=BG)


# icon: 48x48
S = 4
img = Image.new("RGB", (48 * S, 48 * S), SEL)
d = ImageDraw.Draw(img)
d.rectangle((0, 0, 48 * S - 1, 48 * S - 1), outline=BODY, width=2 * S)
snake(d, 0, 0, 48 * S)
img.resize((48, 48), Image.LANCZOS).save(os.path.join(OUT, "icon.png"))

# banner: 256x128
img = Image.new("RGB", (256 * 2, 128 * 2), BG)
d = ImageDraw.Draw(img)
snake(d, 20, 10, 220 * 2 // 2, amp=0.18)
d.rectangle((0, 0, 511, 255), outline=SEL, width=6)
img.resize((256, 128), Image.LANCZOS).save(os.path.join(OUT, "banner.png"))

# banner jingle: gliding sine, 3 seconds max, 16-bit mono 22.05 kHz
rate, secs = 22050, 2.4
with wave.open(os.path.join(OUT, "banner.wav"), "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    ph = 0.0
    frames = bytearray()
    for i in range(int(rate * secs)):
        t = i / rate
        f = 330 + 110 * math.sin(2 * math.pi * 1.6 * t) + 90 * t
        ph += 2 * math.pi * f / rate
        env = min(1.0, t / 0.05) * max(0.0, 1.0 - t / secs)
        frames += struct.pack("<h", int(9000 * env * math.sin(ph)))
    w.writeframes(bytes(frames))
print("assets written to", os.path.normpath(OUT))
