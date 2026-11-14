#!/usr/bin/env python3
"""Builds resources/icon.png (48x48) and resources/banner.png (256x128) from the Neon Crown art
(resources/boot_screen_source.png) so the HOME Menu icon / banner match the boot screen.  Needs Pillow.
Run:  python3 tools/make_icons.py"""
import os
from PIL import Image, ImageEnhance

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
im = Image.open(os.path.join(ROOT, "resources", "boot_screen_source.png")).convert("RGB")
top = im.crop((190, 11, 1258, 618))                      # 1068 x 607, the mock top screen

# banner: 2:1 slice of the logo (crown + PROJECT NOISE + chains), the same artwork as the boot screen
by = 22
ban = top.crop((0, by, 1068, by + 534)).resize((256, 128), Image.LANCZOS)
ban = ImageEnhance.Contrast(ban).enhance(1.08)
ban.save(os.path.join(ROOT, "resources", "banner.png"), optimize=True)

# icon: the crown alone (the lettering is unreadable at 48 px) on a dark purple glow, feathered into the background
import numpy as np
from PIL import ImageFilter
crown = top.crop((392, 8, 678, 168))                     # 286 x 160
SQ = 306
yy, xx = np.mgrid[0:SQ, 0:SQ].astype(np.float32)
r = np.sqrt((xx - SQ / 2) ** 2 + (yy - SQ * 0.52) ** 2) / (SQ * 0.62)
g = np.clip(1.0 - r, 0.0, 1.0) ** 1.6
bg = np.dstack([8 + 70 * g, 3 + 18 * g, 20 + 120 * g]).astype(np.uint8)
canvas = Image.fromarray(bg, "RGB")
mask = Image.new("L", crown.size, 0)
mask.paste(255, (14, 14, crown.width - 14, crown.height - 14))
mask = mask.filter(ImageFilter.GaussianBlur(9))
canvas.paste(crown, ((SQ - crown.width) // 2, int(SQ * 0.52 - crown.height / 2)), mask)
ico = canvas.resize((48, 48), Image.LANCZOS)
ico = ImageEnhance.Brightness(ico).enhance(1.15)
ico = ImageEnhance.Contrast(ico).enhance(1.10)
ico.save(os.path.join(ROOT, "resources", "icon.png"), optimize=True)
print("icon.png 48x48, banner.png 256x128 written")
