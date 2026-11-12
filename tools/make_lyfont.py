#!/usr/bin/env python3
"""Builds the gothic lyrics glyph atlas (source/lyfont_data.h) for PROJECT NOISE.
Pointed, condensed block-gothic capitals (see the reference style): heavy stems that end in sharp spikes.
Needs Pillow.  Usage: python3 tools/make_lyfont.py [preview.png]"""
import sys, io
from PIL import Image, ImageDraw

CW, CH = 28, 40                  # atlas cell, px
COLS, ROWS = 16, 4               # 512 x 160 -> padded to 512 x 256
AW, AH = 512, 256
SS = 8                           # supersampling
K = 0.30                         # design unit -> px
WM = 1.62                        # stroke weight multiplier (heavy, like the reference)
T = 13                           # stem thickness (units)
CHARS = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?'\"-:;()/&"

def glyph_defs():
    g = {}
    def stem(x, y0, y1, top=True, bot=True, w=T):
        s = [('l', [(x, y0), (x, y1)], w)]
        if top: s.append(('t', [(x - w*WM/2 - 1, y0), (x + w*WM/2 + 1, y0), (x, y0 - 21)]))
        if bot: s.append(('t', [(x - w*WM/2 - 1, y1), (x + w*WM/2 + 1, y1), (x, y1 + 21)]))
        return s
    def line(pts, w=T): return [('l', pts, w)]
    def tri(a, b, c): return [('t', [a, b, c])]
    def dot(x, y, r=6.5): return [('t', [(x - r, y), (x, y - r - 2), (x + r, y), (x, y + r + 2)])]

    def octa(x0, y0, x1, y1, c=11):
        return line([(x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c), (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c), (x0 + c, y0)])
    def spikes(cx, y0=7, y1=93, hw=9):
        return tri((cx - hw, y0 - 3), (cx + hw, y0 - 3), (cx, y0 - 17)) + tri((cx - hw, y1 + 3), (cx + hw, y1 + 3), (cx, y1 + 17))

    W = {}
    W['A'] = 54; g['A'] = line([(8, 96), (27, 7), (46, 96)]) + line([(15, 66), (39, 66)], 9) + tri((19, 6), (35, 6), (27, -9)) + tri((2, 96), (14, 96), (8, 110)) + tri((40, 96), (52, 96), (46, 110))
    W['B'] = 52; g['B'] = stem(8, 7, 93) + line([(8, 7), (34, 7), (44, 17), (44, 38), (34, 49), (8, 49)]) + line([(8, 49), (38, 49), (47, 59), (47, 83), (38, 93), (8, 93)])
    W['C'] = 48; g['C'] = line([(42, 17), (32, 7), (16, 7), (7, 18), (7, 82), (16, 93), (32, 93), (42, 83)]) + spikes(24)
    W['D'] = 52; g['D'] = stem(8, 7, 93) + line([(8, 7), (31, 7), (45, 22), (45, 78), (31, 93), (8, 93)])
    W['E'] = 42; g['E'] = stem(8, 7, 93) + line([(8, 7), (38, 7)]) + line([(8, 50), (32, 50)], 11) + line([(8, 93), (38, 93)])
    W['F'] = 40; g['F'] = stem(8, 7, 93) + line([(8, 7), (36, 7)]) + line([(8, 50), (30, 50)], 11)
    W['G'] = 52; g['G'] = line([(44, 17), (33, 7), (17, 7), (7, 18), (7, 82), (17, 93), (34, 93), (45, 82), (45, 52), (27, 52)]) + spikes(26, 7, 93)[:1]
    W['H'] = 54; g['H'] = stem(8, 7, 93) + stem(46, 7, 93) + line([(8, 50), (46, 50)], 11)
    W['I'] = 26; g['I'] = stem(13, 7, 93)
    W['J'] = 38; g['J'] = line([(29, 7), (29, 80), (22, 93), (13, 93), (6, 84)]) + tri((20, 4), (38, 4), (29, -11))
    W['K'] = 54; g['K'] = stem(8, 7, 93) + line([(46, 7), (16, 50), (48, 93)]) + tri((40, 4), (54, 4), (47, -9))
    W['L'] = 44; g['L'] = stem(8, 7, 93, bot=False) + line([(8, 93), (40, 93)]) + tri((34, 100), (44, 86), (44, 100))
    W['M'] = 68; g['M'] = stem(8, 7, 93) + stem(60, 7, 93) + line([(8, 9), (34, 66), (60, 9)])
    W['N'] = 56; g['N'] = stem(8, 7, 93) + stem(48, 7, 93) + line([(8, 10), (48, 90)], 12)
    W['O'] = 52; g['O'] = octa(8, 7, 44, 93) + spikes(26)
    W['P'] = 48; g['P'] = stem(8, 7, 93) + line([(8, 7), (33, 7), (42, 17), (42, 41), (33, 51), (8, 51)])
    W['Q'] = 52; g['Q'] = octa(8, 7, 44, 93) + spikes(26, 7, 93)[:1] + line([(32, 78), (49, 100)], 11) + tri((40, 100), (56, 96), (50, 112))
    W['R'] = 52; g['R'] = stem(8, 7, 93, bot=True) + line([(8, 7), (33, 7), (43, 17), (43, 41), (33, 51), (8, 51)]) + line([(24, 51), (46, 93)])
    W['S'] = 48; g['S'] = line([(42, 17), (33, 7), (16, 7), (7, 17), (7, 35), (16, 46), (33, 54), (42, 65), (42, 83), (33, 93), (16, 93), (6, 83)]) + spikes(24)[:1]
    W['T'] = 48; g['T'] = line([(4, 7), (44, 7)]) + stem(24, 7, 93, top=False) + tri((2, 3), (14, 3), (4, -9)) + tri((34, 3), (46, 3), (44, -9))
    W['U'] = 52; g['U'] = line([(8, 7), (8, 80), (19, 93), (33, 93), (44, 80), (44, 7)]) + tri((1, 3), (15, 3), (8, -12)) + tri((37, 3), (51, 3), (44, -12)) + tri((19, 96), (33, 96), (26, 110))
    W['V'] = 54; g['V'] = line([(8, 7), (27, 93), (46, 7)]) + tri((1, 3), (15, 3), (8, -12)) + tri((39, 3), (53, 3), (46, -12)) + tri((19, 96), (35, 96), (27, 112))
    W['W'] = 78; g['W'] = line([(8, 7), (22, 93), (39, 32), (56, 93), (70, 7)]) + tri((1, 3), (15, 3), (8, -12)) + tri((63, 3), (77, 3), (70, -12)) + tri((15, 96), (29, 96), (22, 111)) + tri((49, 96), (63, 96), (56, 111))
    W['X'] = 54; g['X'] = line([(8, 7), (46, 93)]) + line([(46, 7), (8, 93)]) + tri((1, 3), (15, 3), (8, -12)) + tri((39, 3), (53, 3), (46, -12)) + tri((1, 97), (15, 97), (8, 112)) + tri((39, 97), (53, 97), (46, 112))
    W['Y'] = 52; g['Y'] = line([(8, 7), (26, 48), (44, 7)]) + stem(26, 48, 93, top=False) + tri((1, 3), (15, 3), (8, -12)) + tri((37, 3), (51, 3), (44, -12))
    W['Z'] = 48; g['Z'] = line([(6, 8), (40, 8), (8, 92), (42, 92)], 12) + tri((34, 0), (46, 8), (46, -10))
    W['0'] = 46; g['0'] = octa(8, 7, 38, 93) + spikes(23)
    W['1'] = 30; g['1'] = stem(18, 7, 93) + line([(18, 9), (6, 24)], 11)
    W['2'] = 46; g['2'] = line([(6, 22), (14, 8), (31, 8), (40, 19), (40, 38), (8, 92), (42, 92)], 12) + tri((34, 0), (46, 8), (44, -8))
    W['3'] = 46; g['3'] = line([(7, 12), (15, 7), (32, 7), (41, 18), (41, 36), (31, 47), (16, 49)]) + line([(31, 47), (43, 58), (43, 82), (33, 93), (15, 93), (6, 86)]) + spikes(24, 7, 93)[:1]
    W['4'] = 50; g['4'] = line([(35, 96), (35, 7), (5, 66), (46, 66)], 12) + tri((27, 100), (43, 100), (35, 112))
    W['5'] = 46; g['5'] = line([(40, 8), (11, 8), (8, 46), (28, 41), (40, 53), (40, 82), (30, 93), (14, 93), (6, 86)], 12)
    W['6'] = 46; g['6'] = line([(37, 9), (16, 14), (7, 40), (7, 80), (16, 93), (31, 93), (41, 82), (41, 60), (31, 50), (7, 54)]) + spikes(24, 7, 93)[:1]
    W['7'] = 46; g['7'] = line([(4, 8), (42, 8), (22, 96)], 12) + tri((36, 0), (48, 8), (45, -9))
    W['8'] = 46; g['8'] = octa(9, 7, 37, 50, 9) + octa(7, 50, 39, 93, 10) + tri((15, 4), (31, 4), (23, -10)) + tri((15, 96), (31, 96), (23, 110))
    W['9'] = 46; g['9'] = line([(40, 46), (15, 50), (5, 40), (5, 20), (15, 8), (31, 8), (41, 20), (41, 78), (32, 91), (11, 96)]) + spikes(24, 7, 93)[1:]
    W[' '] = 30; g[' '] = []
    W['.'] = 22; g['.'] = dot(11, 88)
    W[','] = 22; g[','] = dot(11, 88) + line([(11, 94), (6, 110)], 8)
    W['!'] = 24; g['!'] = [('t', [(3, 4), (21, 4), (12, 68)])] + dot(12, 88)
    W['?'] = 42; g['?'] = line([(6, 20), (14, 8), (30, 8), (38, 19), (38, 38), (21, 54), (21, 66)], 12) + dot(21, 88)
    W["'"] = 20; g["'"] = [('t', [(3, 4), (17, 4), (10, 40)])]
    W['"'] = 34; g['"'] = [('t', [(3, 4), (15, 4), (9, 40)]), ('t', [(19, 4), (31, 4), (25, 40)])]
    W['-'] = 32; g['-'] = line([(4, 52), (28, 52)], 11)
    W[':'] = 22; g[':'] = dot(11, 34) + dot(11, 82)
    W[';'] = 22; g[';'] = dot(11, 34) + dot(11, 82) + line([(11, 88), (6, 104)], 8)
    W['('] = 28; g['('] = line([(22, 2), (9, 28), (9, 72), (22, 98)], 10)
    W[')'] = 28; g[')'] = line([(6, 2), (19, 28), (19, 72), (6, 98)], 10)
    W['/'] = 34; g['/'] = line([(28, 4), (6, 96)], 10)
    W['&'] = 56; g['&'] = line([(46, 94), (14, 56), (10, 24), (20, 8), (32, 14), (32, 32), (10, 66), (14, 86), (28, 94), (40, 80), (46, 62)], 11)
    return g, W

def render():
    g, W = glyph_defs()
    atlas = Image.new('L', (AW, AH), 0)
    adv = []
    for i, ch in enumerate(CHARS):
        w = W[ch]
        cell = Image.new('L', (CW * SS, CH * SS), 0)
        d = ImageDraw.Draw(cell)
        ox = (CW - w * K) / 2.0
        oy = 7.5                       # design y=0 sits 6.5 px below the cell top (room for spikes)
        def P(pt): return ((ox + pt[0] * K) * SS, (oy + pt[1] * K) * SS)
        for prim in g[ch]:
            if prim[0] == 't':
                d.polygon([P(p) for p in prim[1]], fill=255)
            else:
                pts = [P(p) for p in prim[1]]
                wd = prim[2] * WM * K * SS
                d.line(pts, fill=255, width=int(round(wd)), joint='curve')
                for p in (pts[0], pts[-1]):          # flat-ish ends: small square caps
                    r = wd / 2
                    d.rectangle([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=255)
        cell = cell.resize((CW, CH), Image.LANCZOS)
        atlas.paste(cell, ((i % COLS) * CW, (i // COLS) * CH))
        adv.append(int(round(w * K + 4.4)))
    return atlas, adv

def emit(atlas, adv, path):
    buf = io.BytesIO()
    atlas.save(buf, 'PNG', optimize=True)
    data = buf.getvalue()
    with open(path, 'w') as f:
        f.write('#pragma once\n/* Gothic lyrics glyph atlas, generated by tools/make_lyfont.py - do not edit. */\n')
        f.write('#define LYF_CW %d\n#define LYF_CH %d\n#define LYF_COLS %d\n#define LYF_AW %d\n#define LYF_AH %d\n' % (CW, CH, COLS, AW, AH))
        f.write('#define LYF_CHARS "%s"\n' % CHARS.replace('\\', '\\\\').replace('"', '\\"'))
        f.write('static const unsigned char lyf_adv[] = { %s };\n' % ', '.join(map(str, adv)))
        f.write('static const unsigned char lyf_png[] = {\n')
        for i in range(0, len(data), 16):
            f.write('    ' + ', '.join('0x%02x' % b for b in data[i:i + 16]) + ',\n')
        f.write('};\nstatic const unsigned lyf_png_len = %d;\n' % len(data))
    return len(data)

def preview(atlas, adv, path, lines, sc):
    img = Image.new('RGB', (400, 240), (6, 12, 24)); px = img.load()
    a = atlas.load(); y = 20
    for text, s, col in zip(lines, sc, [(120, 190, 255), (200, 210, 225), (200, 210, 225)]):
        x = 200 - sum(adv[CHARS.index(c)] for c in text) * s / 2
        for c in text:
            i = CHARS.index(c); gx, gy = (i % COLS) * CW, (i // COLS) * CH
            gw, gh = int(CW * s), int(CH * s)
            cell = atlas.crop((gx, gy, gx + CW, gy + CH)).resize((max(1, gw), max(1, gh)), Image.BILINEAR)
            img.paste(Image.new('RGB', cell.size, col), (int(x), y), cell)
            x += adv[i] * s
        y += int(CH * s) + 8
    img = img.resize((1200, 720), Image.NEAREST); img.save(path)
    big = Image.new('RGB', (COLS*CW*3, 3*CH*3), (6,12,24)); big.paste(Image.merge('RGB',(atlas.crop((0,0,COLS*CW,3*CH)),)*3).resize((COLS*CW*3,3*CH*3), Image.LANCZOS),(0,0)); big.save('/tmp/lyf_sheet.png')

if __name__ == '__main__':
    atlas, adv = render()
    n = emit(atlas, adv, __file__.replace('tools/make_lyfont.py', 'source/lyfont_data.h') if '/' in __file__ else 'source/lyfont_data.h')
    print('atlas png bytes:', n)
    atlas.save('/tmp/lyf_atlas.png')
    preview(atlas, adv, '/tmp/lyf_preview.png', ["I CAN'T STOP THE FEELING", "ABCDEFGHIJKLM NOPQRSTUVWXYZ", "0123456789 .,!?-:;()&"], [0.62, 0.50, 0.5])
