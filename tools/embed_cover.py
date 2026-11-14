#!/usr/bin/env python3
"""Embeds resources/cover.png (128x128) into source/placeholder.h as the built-in default cover.

This is the cover shown on the boot screen and for songs without artwork. It only READS
resources/cover.png - it never changes any artwork.  Needs Pillow.  Run:  python3 tools/embed_cover.py
(tools/make_cover.py is the old procedural generator; it overwrites resources/cover.png, so do not use it
with the branded artwork.)"""
import io, os
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
src = os.path.join(ROOT, "resources", "cover.png")
im = Image.open(src).convert("RGB")
assert im.size == (128, 128), "cover.png must be 128x128, got %r" % (im.size,)
buf = io.BytesIO()
im.save(buf, "PNG", optimize=True)              # lossless re-encode, usually a little smaller
data = buf.getvalue()
assert Image.open(io.BytesIO(data)).convert("RGB").tobytes() == im.tobytes(), "re-encode must be lossless"

lines = ["    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + "," for i in range(0, len(data), 16)]
hdr = ("#pragma once\n/* Default cover art (128x128 PNG, from resources/cover.png via tools/embed_cover.py). "
       "Embedded as a C array so the build needs no extra tooling. */\n")
with open(os.path.join(ROOT, "source", "placeholder.h"), "w") as f:
    f.write(hdr + "static const unsigned char placeholder_png[] = {\n" + "\n".join(lines) +
            "\n};\nstatic const unsigned int placeholder_png_len = %d;\n" % len(data))
print("placeholder.h written,", len(data), "bytes")
