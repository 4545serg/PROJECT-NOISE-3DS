#!/usr/bin/env python3
"""Builds the blackletter lyrics glyph atlas (source/lyfont_data.h) for PROJECT NOISE.

Style: the heavy "Old English / Chicano" blackletter of the reference sheet - thick vertical stems, hairline
joins, pointed diamond feet, faceted bowls.  Every letter is drawn with ONE virtual broad-nib pen held at a fixed
angle (stems thick, "/" diagonals thin), so the whole alphabet stays consistent.  Lower case AND capitals are
drawn, so lyrics read normally (no all-caps).
Needs Pillow.  Usage: python3 tools/make_lyfont.py [preview.png]"""
import sys, io, math
from PIL import Image, ImageDraw

CW, CH = 32, 48                  # atlas cell, px
COLS = 16                        # 16 x 5 cells -> 512 x 240, padded to 512 x 256
AW, AH = 512, 256
SS = 8                           # supersampling
K = 0.30                         # design unit -> px
SB = 1.5                         # side bearing each side of the ink, px (spacing is measured from the ink, so it is even)
BASE, XTOP, CAPTOP = 100, 44, 14 # baseline / x-height / cap height (design units)
OY = 5.0                         # design y=0 sits OY px below the cell top
NIB_L, NIB_ANG = 21.0, 38.0      # nib length (units) and angle
HAIR = 6.0                       # hairline width (units)
CHARS = " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?'\"-:;()/&"

_n = (math.cos(math.radians(NIB_ANG)), -math.sin(math.radians(NIB_ANG)))


def P(*pts):                     # pen stroke (broad nib)
    return ('p', list(pts))


def H(*pts, w=HAIR):             # hairline
    return ('h', list(pts), w)


def T(*pts):                     # filled polygon
    return ('t', list(pts))


def dia(x, y, r=7.5, rv=None):   # diamond (foot / dot / terminal)
    rv = r * 1.05 if rv is None else rv
    return T((x - r, y), (x, y - rv), (x + r, y), (x, y + rv))


def foot(x, y=BASE):             # pointed foot under a stem
    return [T((x - 9, y - 3), (x + 9, y - 3), (x + 1, y + 9))]


def flag(x, y):                  # slanted serif at the top of a stem
    return [T((x - 10, y + 6), (x - 2, y - 7), (x + 8, y - 4), (x + 8, y + 5))]


def stem(x, y0, y1, top=True, bot=True):
    s = [P((x, y0), (x, y1))]
    if top: s += flag(x, y0)
    if bot: s += foot(x, y1)
    return s


def build():
    g, W = {}, {}

    def add(ch, w, prims):
        g[ch], W[ch] = prims, w

    # ------------------------------------------------------------- lower case
    add(' ', 26, [])
    add('i', 28, stem(14, XTOP, BASE) + [dia(15, 24, 6.5)])
    add('j', 28, [P((16, XTOP), (16, 120), (10, 130), (2, 128))] + flag(16, XTOP) + [dia(17, 24, 6.5)])
    add('l', 28, stem(14, 6, BASE))
    add('t', 32, stem(16, 20, BASE, top=False) + [T((6, 24), (22, 12), (22, 28)), P((4, 48), (32, 48))])
    add('n', 48, stem(10, XTOP, BASE) + [P((10, 56), (22, 44), (36, 52), (38, 64), (38, BASE))] + foot(38))
    add('m', 70, stem(10, XTOP, BASE) + [P((10, 56), (21, 44), (31, 52), (33, 64), (33, BASE)),
                                         P((33, 56), (44, 44), (54, 52), (56, 64), (56, BASE))] + foot(33) + foot(56))
    add('h', 48, stem(10, 6, BASE) + [P((10, 60), (22, 46), (36, 54), (38, 66), (38, BASE))] + foot(38))
    add('u', 48, [P((10, XTOP), (10, 84), (20, BASE), (34, BASE))] + flag(10, XTOP) + stem(38, XTOP, BASE))
    add('r', 36, stem(10, XTOP, BASE) + [P((10, 58), (22, 44), (34, 48)), dia(36, 54, 6)])
    add('o', 44, [P((22, 44), (8, 56), (8, 88), (22, 100), (36, 88), (36, 56), (22, 44))])
    add('e', 42, [P((34, 60), (22, 44), (8, 56), (8, 88), (22, 100), (36, 90)), H((8, 72), (34, 70), w=6)])
    add('c', 40, [P((34, 54), (22, 44), (8, 56), (8, 88), (22, 100), (34, 92)), dia(35, 55, 5.5)])
    add('a', 46, [P((36, 58), (22, 48), (8, 60), (8, 88), (22, 100), (36, 90))] + stem(38, XTOP, BASE))
    add('d', 48, [P((36, 62), (22, 48), (8, 60), (8, 88), (22, 100), (36, 92))] + stem(38, 6, BASE))
    add('b', 48, stem(10, 6, BASE, bot=True) + [P((12, 62), (24, 46), (38, 58), (38, 88), (24, 100), (12, 92))])
    add('p', 48, stem(10, XTOP, 132) + [P((12, 62), (24, 46), (38, 58), (38, 88), (24, 100), (12, 92))])
    add('q', 48, [P((36, 62), (22, 48), (8, 60), (8, 88), (22, 100), (36, 92))] + stem(38, XTOP, 132))
    add('g', 46, [P((36, 58), (22, 48), (8, 60), (8, 84), (22, 94), (36, 88))] +
        [P((38, XTOP), (38, 116), (30, 130), (16, 132), (6, 124))] + flag(38, XTOP))
    add('f', 34, [P((18, 36), (18, BASE)), P((18, 36), (22, 18), (34, 10), (42, 16)), P((4, 48), (32, 48)), dia(43, 17, 5)] + foot(18))
    add('k', 46, stem(10, 6, BASE) + [H((38, 46), (14, 76), w=7), P((16, 76), (38, BASE))] + foot(40))
    add('s', 38, [P((32, 54), (22, 44), (10, 50), (10, 64), (30, 78), (32, 90), (22, 100), (8, 98)), dia(33, 55, 5)])
    add('v', 46, [P((8, XTOP), (23, BASE), (38, XTOP))] + flag(8, XTOP) + [T((30, 41), (46, 41), (40, 52))])
    add('w', 64, [P((6, XTOP), (19, BASE), (32, 62), (45, BASE), (58, XTOP))] + flag(6, XTOP) + [T((50, 41), (66, 41), (60, 52))])
    add('x', 46, [P((8, XTOP), (38, BASE)), H((38, 46), (8, 98), w=7)] + flag(8, XTOP) + foot(38))
    add('y', 48, [P((10, XTOP), (10, 84), (20, BASE), (34, BASE))] + flag(10, XTOP) +
        [P((38, XTOP), (38, 120), (30, 131), (14, 130))] + flag(38, XTOP))
    add('z', 42, [P((6, 48), (36, 48)), H((36, 52), (8, 96), w=7), P((6, 98), (38, 98))])

    # --------------------------------------------------------------- capitals
    C0 = CAPTOP
    add('A', 56, [P((8, BASE), (28, C0)), P((28, C0), (48, BASE)), P((16, 68), (40, 68))] + foot(8) + foot(48) + flag(28, C0))
    add('B', 54, stem(10, C0, BASE) + [P((10, C0), (32, C0), (44, 26), (44, 44), (32, 57), (10, 57)),
                                       P((10, 57), (36, 57), (48, 70), (48, 88), (36, BASE), (10, BASE))])
    add('C', 52, [P((44, 30), (32, C0), (16, C0), (8, 30), (8, 84), (16, BASE), (32, BASE), (46, 88)), dia(45, 29, 6), dia(47, 88, 6)])
    add('D', 56, stem(10, C0, BASE) + [P((10, C0), (32, C0), (48, 32), (48, 82), (32, BASE), (10, BASE))])
    add('E', 46, stem(10, C0, BASE) + [P((10, C0), (38, C0)), P((10, 57), (32, 57)), P((10, BASE), (40, BASE))])
    add('F', 44, stem(10, C0, BASE) + [P((10, C0), (38, C0)), P((10, 57), (32, 57))])
    add('G', 56, [P((46, 30), (32, C0), (16, C0), (8, 30), (8, 84), (16, BASE), (32, BASE), (48, 86), (48, 58), (30, 58)), dia(47, 29, 6)])
    add('H', 58, stem(10, C0, BASE) + stem(48, C0, BASE) + [P((10, 58), (48, 58))])
    add('I', 30, stem(14, C0, BASE))
    add('J', 38, [P((28, C0), (28, 86), (20, BASE), (8, 96))] + flag(28, C0) + [dia(7, 96, 5)])
    add('K', 54, stem(10, C0, BASE) + [H((44, C0), (12, 58), w=7), P((14, 58), (46, BASE))] + foot(46) + flag(44, C0))
    add('L', 46, stem(10, C0, BASE, bot=False) + [P((10, BASE), (40, BASE)), dia(41, 96, 5)])
    add('M', 70, stem(10, C0, BASE) + stem(60, C0, BASE) + [P((10, C0), (35, 76), (60, C0))])
    add('N', 58, stem(10, C0, BASE) + stem(48, C0, BASE) + [P((10, C0), (48, BASE))])
    add('O', 58, [P((29, C0), (8, 32), (8, 82), (29, BASE), (50, 82), (50, 32), (29, C0))])
    add('P', 50, stem(10, C0, BASE) + [P((10, C0), (32, C0), (44, 27), (44, 46), (32, 58), (10, 58))])
    add('Q', 58, [P((29, C0), (8, 32), (8, 82), (29, BASE), (50, 82), (50, 32), (29, C0)), P((34, 88), (52, 112))])
    add('R', 54, stem(10, C0, BASE) + [P((10, C0), (32, C0), (44, 27), (44, 46), (32, 58), (10, 58)), P((26, 58), (48, BASE))] + foot(48))
    add('S', 46, [P((40, 28), (28, C0), (14, 16), (8, 30), (14, 46), (32, 62), (40, 80), (34, 96), (18, BASE), (6, 90))])
    add('T', 50, [P((4, C0), (46, C0)), P((25, C0), (25, BASE))] + foot(25) + [T((4, 24), (16, C0 - 8), (16, 24)), T((34, 24), (46, C0 - 8), (46, 24))])
    add('U', 56, [P((8, C0), (8, 82), (22, BASE), (36, BASE), (46, 90))] + flag(8, C0) + stem(50, C0, BASE))
    add('V', 54, [P((6, C0), (27, BASE), (48, C0))] + flag(6, C0) + [T((40, 11), (56, 11), (50, 22))])
    add('W', 78, [P((4, C0), (20, BASE), (38, 36), (56, BASE), (72, C0))] + flag(4, C0) + [T((64, 11), (80, 11), (74, 22))])
    add('X', 54, [P((8, C0), (44, BASE)), H((46, C0), (8, BASE), w=7)] + flag(8, C0) + foot(44))
    add('Y', 54, [P((6, C0), (27, 58)), H((48, C0), (27, 58), w=7), P((27, 58), (27, BASE))] + flag(6, C0) + foot(27))
    add('Z', 50, [P((6, 18), (42, 18)), H((42, 22), (8, 94), w=8), P((6, 98), (44, 98))])

    # ------------------------------------------------- digits (lining, cap height)
    add('0', 48, [P((24, C0), (8, 30), (8, 84), (24, BASE), (40, 84), (40, 30), (24, C0))])
    add('1', 32, stem(18, C0, BASE) + [P((18, C0), (6, 28))])
    add('2', 46, [P((6, 30), (16, C0), (32, C0), (42, 28), (42, 44), (8, 96)), P((6, 98), (44, 98))])
    add('3', 46, [P((8, 24), (18, C0), (34, C0), (42, 28), (36, 46), (20, 52)), P((20, 52), (40, 62), (42, 84), (32, BASE), (16, BASE), (6, 92))])
    add('4', 50, [P((34, BASE), (34, C0), (6, 70), (46, 70))] + foot(34))
    add('5', 46, [P((40, 16), (12, 16), (8, 52), (28, 46), (40, 58), (42, 84), (32, BASE), (14, BASE), (6, 92))])
    add('6', 46, [P((38, 18), (18, 22), (8, 44), (8, 84), (22, BASE), (38, 90), (38, 64), (24, 54), (8, 60))])
    add('7', 46, [P((6, 18), (42, 18), (22, 100)), dia(8, 18, 4)])
    add('8', 46, [P((23, C0), (10, 28), (23, 54), (38, 28), (23, C0)), P((23, 54), (7, 76), (23, BASE), (40, 76), (23, 54))])
    add('9', 46, [P((38, 40), (22, 50), (8, 38), (8, 26), (22, C0), (38, 28), (38, 84), (24, 98), (8, 94))])

    # ------------------------------------------------------------- punctuation
    add('.', 24, [dia(12, 92, 7)])
    add(',', 24, [dia(12, 92, 7), P((12, 96), (7, 114))])
    add('!', 26, [T((4, C0), (22, C0), (13, 72)), dia(13, 92, 7)])
    add('?', 44, [P((6, 28), (16, C0), (32, C0), (40, 28), (38, 46), (22, 58), (22, 70)), dia(22, 92, 7)])
    add("'", 22, [T((4, C0), (18, C0), (10, 44))])
    add('"', 36, [T((4, C0), (16, C0), (10, 44)), T((20, C0), (32, C0), (26, 44))])
    add('-', 32, [P((4, 62), (28, 62))])
    add(':', 24, [dia(12, 58, 6.5), dia(12, 92, 6.5)])
    add(';', 24, [dia(12, 58, 6.5), dia(12, 92, 6.5), P((12, 96), (7, 114))])
    add('(', 28, [H((22, 6), (9, 34), (9, 80), (22, 114), w=9)])
    add(')', 28, [H((6, 6), (19, 34), (19, 80), (6, 114), w=9)])
    add('/', 34, [H((28, 6), (6, 112), w=8)])
    add('&', 58, [P((48, BASE), (14, 58), (10, 30), (22, C0), (34, 24), (30, 40), (10, 74), (14, 92), (30, BASE), (44, 84), (50, 66))])
    return g, W


def stamp_pen(d, pts, p):
    a = (_n[0] * NIB_L / 2, _n[1] * NIB_L / 2)
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        quad = [(x0 - a[0], y0 - a[1]), (x0 + a[0], y0 + a[1]), (x1 + a[0], y1 + a[1]), (x1 - a[0], y1 - a[1])]
        d.polygon([p(q) for q in quad], fill=255)
    for pt in pts:                            # a nib stamp at every vertex closes the joins between facets
        d.polygon([p((pt[0] - a[0], pt[1] - a[1])), p((pt[0] + a[0], pt[1] + a[1])),
                   p((pt[0] + a[0] + 0.01, pt[1] + a[1] + 0.01))], fill=255)
    # hairline core so a stroke that runs along the nib direction never disappears
    d.line([p(q) for q in pts], fill=255, width=max(1, int(round(HAIR * 0.7 * K * SS))), joint='curve')


def render():
    g, W = build()
    assert len(CHARS) <= 96 and all(c in g for c in CHARS), [c for c in CHARS if c not in g]
    atlas = Image.new('L', (AW, AH), 0)
    adv = []
    for i, ch in enumerate(CHARS):
        w = W[ch]
        cell = Image.new('L', (CW * SS, CH * SS), 0)
        d = ImageDraw.Draw(cell)
        ox = (CW - w * K) / 2.0

        def p(pt): return ((ox + pt[0] * K) * SS, (OY + pt[1] * K) * SS)
        for prim in g[ch]:
            if prim[0] == 't':
                d.polygon([p(q) for q in prim[1]], fill=255)
            elif prim[0] == 'p':
                stamp_pen(d, prim[1], p)
            else:
                d.line([p(q) for q in prim[1]], fill=255, width=max(1, int(round(prim[2] * K * SS))), joint='curve')
        bb = cell.getbbox()
        if bb:                                   # centre the ink in the cell; advance = ink width + even side bearings
            shift = int(round(((CW * SS) - (bb[0] + bb[2])) / 2.0))
            cell = cell.transform(cell.size, Image.AFFINE, (1, 0, -shift, 0, 1, 0))
            inkw = (bb[2] - bb[0]) / SS
            adv.append(int(round(inkw + 2 * SB)) if ch != ' ' else 8)
        else:
            adv.append(8)
        cell = cell.resize((CW, CH), Image.LANCZOS)
        atlas.paste(cell, ((i % COLS) * CW, (i // COLS) * CH))
    return atlas, adv


def emit(atlas, adv, path):
    buf = io.BytesIO()
    atlas.save(buf, 'PNG', optimize=True)
    data = buf.getvalue()
    yc = OY + (CAPTOP + BASE) / 2.0 * K          # cell row of the capital-height centre
    with open(path, 'w') as f:
        f.write('#pragma once\n/* Blackletter lyrics glyph atlas, generated by tools/make_lyfont.py - do not edit. */\n')
        f.write('#define LYF_CW %d\n#define LYF_CH %d\n#define LYF_COLS %d\n#define LYF_AW %d\n#define LYF_AH %d\n' % (CW, CH, COLS, AW, AH))
        f.write('#define LYF_YC %.2ff\n' % yc)
        f.write('#define LYF_CHARS "%s"\n' % CHARS.replace('\\', '\\\\').replace('"', '\\"'))
        f.write('static const unsigned char lyf_adv[] = { %s };\n' % ', '.join(map(str, adv)))
        f.write('static const unsigned char lyf_png[] = {\n')
        for i in range(0, len(data), 16):
            f.write('    ' + ', '.join('0x%02x' % b for b in data[i:i + 16]) + ',\n')
        f.write('};\nstatic const unsigned lyf_png_len = %d;\n' % len(data))
    return len(data)


def preview(atlas, adv, path, lines, scales, bg=(6, 12, 24)):
    img = Image.new('RGB', (400, 240), bg)
    y = 8
    for (text, col), s in zip(lines, scales):
        w = sum(adv[CHARS.index(c)] for c in text) * s
        x = 200 - w / 2
        for c in text:
            i = CHARS.index(c)
            gx, gy = (i % COLS) * CW, (i // COLS) * CH
            cell = atlas.crop((gx, gy, gx + CW, gy + CH)).resize((max(1, int(CW * s)), max(1, int(CH * s))), Image.BILINEAR)
            img.paste(Image.new('RGB', cell.size, col), (int(round(x + (adv[i] - CW) * 0.5 * s)), y), cell)   # same placement as lyfont_draw
            x += adv[i] * s
        y += int(CH * s) + 4
    img.resize((1200, 720), Image.NEAREST).save(path)


if __name__ == '__main__':
    atlas, adv = render()
    root = __file__.rsplit('tools/', 1)[0] if 'tools/' in __file__ else ''
    n = emit(atlas, adv, root + 'source/lyfont_data.h')
    print('atlas png bytes:', n)
    out = sys.argv[1] if len(sys.argv) > 1 else '/tmp/lyf_preview.png'
    atlas.save('/tmp/lyf_atlas.png')
    sheet = Image.new('RGB', (COLS * CW * 3, 5 * CH * 3), (6, 12, 24))
    sheet.paste(Image.merge('RGB', (atlas.crop((0, 0, COLS * CW, 5 * CH)),) * 3).resize((COLS * CW * 3, 5 * CH * 3), Image.LANCZOS), (0, 0))
    sheet.save('/tmp/lyf_sheet.png')
    preview(atlas, adv, out, [("I can't stop the feeling", (150, 205, 255)), ("Don't stop me now, I'm having such a good time", (205, 215, 230)),
                              ("abcdefghijklm nopqrstuvwxyz", (205, 215, 230)), ("ABCDEFGHIJKLM NOPQRSTUVWXYZ 0123456789", (205, 215, 230))],
            [0.85, 0.62, 0.62, 0.5])
