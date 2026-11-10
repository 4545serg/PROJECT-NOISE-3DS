#!/usr/bin/env python3
"""Generates resources/banner.wav (the HOME Menu banner jingle, < 3 s) for PROJECT NOISE.
resources/icon.png and banner.png are hand-made; the default cover comes from tools/make_cover.py."""
import math, os, struct, wave

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources")
os.makedirs(OUT, exist_ok=True)

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
