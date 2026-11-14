#!/usr/bin/env python3
"""Builds the PROJECT NOISE boot screen from resources/boot_screen_source.png (the "Neon Crown" art).

Output:
  resources/boot_art/boot_top.jpg     400x240  top screen, full art
  resources/boot_art/boot_bottom.jpg  320x240  bottom screen, with the static equaliser bars and the
                                               "Press A to continue" line painted out
  resources/boot_art/boot_press.png   RGBA     the "Press A to continue" line on its own (faded in by the app)
  source/boot_art.h                            the three files as C arrays + layout constants

The app draws live, animated equaliser bars where the painted ones were, so the bottom screen moves while
the library loads. Needs Pillow + numpy.   Run:  python3 tools/make_boot.py"""
import io, os
import numpy as np
from PIL import Image, ImageFilter

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "resources", "boot_screen_source.png")
OUT = os.path.join(ROOT, "resources", "boot_art")
os.makedirs(OUT, exist_ok=True)

im = Image.open(SRC).convert("RGB")
# the two mock screens inside the source sheet (found by scanning for non-black pixels)
top_src = im.crop((190, 11, 1258, 618))        # 1068 x 607
bot_src = im.crop((272, 652, 1176, 1076))      # 904 x 424

# ---------------------------------------------------------------- top: 5:3 centre crop -> 400x240
tw = round(top_src.height * 400 / 240)         # 1012
tx = (top_src.width - tw) // 2
top = top_src.crop((tx, 0, tx + tw, top_src.height)).resize((400, 240), Image.LANCZOS)

# ---------------------------------------------------------------- bottom: 4:3 centre crop on the crown -> 320x240
BX0 = 168
bw = round(bot_src.height * 320 / 240)         # 565
bot_crop = bot_src.crop((BX0, 0, BX0 + bw, bot_src.height))
S = 320.0 / bw
bot = bot_crop.resize((320, 240), Image.LANCZOS)
a = np.asarray(bot).astype(np.float32)
lum = a.max(axis=2)


def src_to_final(x, y):
    return (x - BX0) * S, y * S


# "Press A to continue": locate the lit pixels in the text rows
tx0, ty0 = src_to_final(190, 172)
tx1, ty1 = src_to_final(712, 238)
tx0, ty0, tx1, ty1 = int(tx0), int(ty0), int(tx1), int(ty1)
sub = lum[ty0:ty1, tx0:tx1]
ys, xs = np.where(sub > 110)
PX0, PX1 = tx0 + xs.min() - 3, tx0 + xs.max() + 4
PY0, PY1 = ty0 + ys.min() - 3, ty0 + ys.max() + 4

# equaliser bars: bright pixels in the lower middle
bx0f, by0f = src_to_final(236, 276)
bx1f, by1f = src_to_final(662, 424)
bx0f, by0f, bx1f, by1f = int(bx0f), int(by0f), int(bx1f), int(by1f)
sub = lum[by0f:by1f, bx0f:bx1f]
ys, xs = np.where(sub > 100)
EQ_X0, EQ_X1 = bx0f + xs.min(), bx0f + xs.max() + 1
EQ_TOP, EQ_BASE = by0f + ys.min(), by0f + ys.max() + 1


def paint_out(arr, x0, y0, x1, y1, thresh, pad=4, seed=1):
    """Replace a box with the surrounding dark background: per row, the median of its own dark pixels."""
    rng = np.random.default_rng(seed)
    H, W = arr.shape[:2]
    x0, y0, x1, y1 = max(0, x0 - pad), max(0, y0 - pad), min(W, x1 + pad), min(H, y1 + pad)
    box = arr[y0:y1, x0:x1]
    mx = box.max(axis=2)
    dark_all = box[mx < thresh]
    glob = np.median(dark_all, axis=0) if len(dark_all) else np.array([10, 4, 22], np.float32)
    fill = np.empty_like(box)
    for r in range(box.shape[0]):
        d = box[r][mx[r] < thresh]
        fill[r] = np.median(d, axis=0) if len(d) > 8 else glob
    fill += rng.normal(0, 1.6, fill.shape)
    # feathered mask so the patch melts into the art
    m = Image.new("L", (x1 - x0, y1 - y0), 0)
    inner = Image.new("L", (x1 - x0 - 2 * pad, y1 - y0 - 2 * pad), 255)
    m.paste(inner, (pad, pad))
    m = np.asarray(m.filter(ImageFilter.GaussianBlur(pad * 0.6))).astype(np.float32)[..., None] / 255.0
    arr[y0:y1, x0:x1] = box * (1 - m) + fill * m


# keep the text as a sprite BEFORE it is painted out
press_box = (PX0, PY0, PX1, PY1)
txt = a[PY0:PY1, PX0:PX1].copy()
tm = txt.max(axis=2)
alpha = np.clip((tm - 34.0) / 150.0, 0.0, 1.0)
rgb = np.clip(txt / np.maximum(tm, 1.0)[..., None] * 255.0, 0, 255)
rgb = np.where(alpha[..., None] > 0.02, rgb, 0)
sprite = np.dstack([rgb, alpha * 255.0]).astype(np.uint8)
sprite_im = Image.fromarray(sprite, "RGBA")
sprite_im.save(os.path.join(OUT, "boot_press.png"), optimize=True)

paint_out(a, PX0, PY0, PX1, PY1, 70, pad=5, seed=2)
paint_out(a, EQ_X0, EQ_TOP, EQ_X1, EQ_BASE, 70, pad=5, seed=3)
bot = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGB")

top.save(os.path.join(OUT, "boot_top.jpg"), quality=90, subsampling=0, optimize=True)
bot.save(os.path.join(OUT, "boot_bottom.jpg"), quality=90, subsampling=0, optimize=True)
# a preview of the assembled bottom screen (text back in) for quick checking
prev = bot.convert("RGBA")
prev.alpha_composite(sprite_im, (PX0, PY0))
prev.convert("RGB").save(os.path.join(OUT, "preview_bottom.png"))


def carr(name, data):
    lines = ["    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + "," for i in range(0, len(data), 16)]
    return "static const unsigned char %s[] = {\n%s\n};\nstatic const unsigned int %s_len = %d;\n" % (
        name, "\n".join(lines), name, len(data))


def rd(p):
    with open(os.path.join(OUT, p), "rb") as f:
        return f.read()


hdr = ("#pragma once\n"
       "/* Boot screen art, generated by tools/make_boot.py from resources/boot_screen_source.png. Do not edit by hand.\n"
       " * boot_top_jpg    400x240   top screen\n"
       " * boot_bottom_jpg 320x240   bottom screen (static bars / text painted out)\n"
       " * boot_press_png  RGBA      \"Press A to continue\" line, drawn at BOOT_PRESS_X / BOOT_PRESS_Y */\n"
       "#define BOOT_PRESS_X %d\n#define BOOT_PRESS_Y %d\n#define BOOT_PRESS_W %d\n#define BOOT_PRESS_H %d\n"
       "#define BOOT_EQ_X0 %d\n#define BOOT_EQ_X1 %d\n#define BOOT_EQ_TOP %d\n#define BOOT_EQ_BASE %d\n\n"
       % (PX0, PY0, PX1 - PX0, PY1 - PY0, EQ_X0, EQ_X1, EQ_TOP, EQ_BASE))
with open(os.path.join(ROOT, "source", "boot_art.h"), "w") as f:
    f.write(hdr + carr("boot_top_jpg", rd("boot_top.jpg")) + carr("boot_bottom_jpg", rd("boot_bottom.jpg")) +
            carr("boot_press_png", rd("boot_press.png")))
print("press box", press_box, "eq", EQ_X0, EQ_X1, EQ_TOP, EQ_BASE)
print("top %d B, bottom %d B, press %d B" % (len(rd("boot_top.jpg")), len(rd("boot_bottom.jpg")), len(rd("boot_press.png"))))
