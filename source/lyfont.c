#include "lyfont.h"

#include <citro2d.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "libcfg.h"
#include "stb_image.h"
#include "lyfont_data.h"

#define LYF_MAXG 200

static C3D_Tex           g_tex;
static bool              g_ok;
static Tex3DS_SubTexture g_sub[96];
static C2D_Image         g_img[96];
static int               g_nchars;

static u32 morton_interleave(u32 x, u32 y)
{
    u32 i = (x & 7) | ((y & 7) << 8);
    i = (i ^ (i << 2)) & 0x1313;
    i = (i ^ (i << 1)) & 0x1515;
    return (i | (i >> 7)) & 0x3F;
}

bool lyfont_init(void)
{
    if (g_ok) return true;
    int w = 0, h = 0, c = 0;
    unsigned char *px = stbi_load_from_memory(lyf_png, (int)lyf_png_len, &w, &h, &c, 1);
    if (!px) return false;
    if (w != LYF_AW || h != LYF_AH || !C3D_TexInit(&g_tex, LYF_AW, LYF_AH, GPU_A8)) { stbi_image_free(px); return false; }

    unsigned char *dst = (unsigned char *)g_tex.data;
    memset(dst, 0, (size_t)LYF_AW * LYF_AH);
    for (int y = 0; y < h; y++)                                /* row 0 = image top, 8x8 tiled: same layout as art.c */
        for (int x = 0; x < w; x++) {
            const u32 o = morton_interleave((u32)x, (u32)y) + ((u32)x & ~7u) * 8u + ((u32)y & ~7u) * (u32)LYF_AW;
            dst[o] = px[(size_t)y * (size_t)w + (size_t)x];
        }
    stbi_image_free(px);
    GSPGPU_FlushDataCache(g_tex.data, (u32)LYF_AW * LYF_AH);
    C3D_TexSetFilter(&g_tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&g_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    g_nchars = (int)strlen(LYF_CHARS);
    if (g_nchars > 96) g_nchars = 96;
    for (int i = 0; i < g_nchars; i++) {
        const float x0 = (float)((i % LYF_COLS) * LYF_CW), y0 = (float)((i / LYF_COLS) * LYF_CH);
        g_sub[i].width = LYF_CW;
        g_sub[i].height = LYF_CH;
        g_sub[i].left = x0 / (float)LYF_AW;
        g_sub[i].right = (x0 + (float)LYF_CW) / (float)LYF_AW;
        g_sub[i].top = 1.0f - y0 / (float)LYF_AH;
        g_sub[i].bottom = 1.0f - (y0 + (float)LYF_CH) / (float)LYF_AH;
        g_img[i].tex = &g_tex;
        g_img[i].subtex = &g_sub[i];
    }
    g_ok = true;
    return true;
}

void lyfont_exit(void)
{
    if (!g_ok) return;
    C3D_TexDelete(&g_tex);
    g_ok = false;
}

bool lyfont_ok(void) { return g_ok; }

static int glyph_of(char c)
{
    const char *p = strchr(LYF_CHARS, c);
    return (p && c) ? (int)(p - LYF_CHARS) : -1;
}

/* UTF-8 -> glyph indices. Known typographic punctuation is normalized to the
 * matching atlas glyph. Other valid Unicode is rejected as a whole line so the
 * caller can render it with citro2d's system font without losing characters. */
static int decode(const char *s, int *out)
{
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        const char *rep = NULL;
        char one[2] = { 0, 0 };
        if (*p < 0x80) {
            char c = (char)*p++;
            if (c == '\t' || c == '\r' || c == '\n') c = ' ';
            if (c == '`') c = '\'';
            if (c == '[' || c == '{') c = '(';
            if (c == ']' || c == '}') c = ')';
            if (c == '_') c = '-';
            one[0] = c;
            rep = one;
        } else {
            /* Validate sequence length and continuation bytes before reading
             * them. Lyrics can come from network/cache data, so malformed UTF-8
             * must never make the decoder read beyond its terminating NUL. */
            unsigned cp;
            int bytes;
            if (p[0] >= 0xC2 && p[0] <= 0xDF) {
                if (!p[1] || (p[1] & 0xC0) != 0x80) return -1;
                cp = ((unsigned)(p[0] & 0x1F) << 6) | (unsigned)(p[1] & 0x3F);
                bytes = 2;
            } else if (p[0] >= 0xE0 && p[0] <= 0xEF) {
                if (!p[1] || !p[2] || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80) return -1;
                if ((p[0] == 0xE0 && p[1] < 0xA0) || (p[0] == 0xED && p[1] >= 0xA0)) return -1;
                cp = ((unsigned)(p[0] & 0x0F) << 12) | ((unsigned)(p[1] & 0x3F) << 6) | (unsigned)(p[2] & 0x3F);
                bytes = 3;
            } else if (p[0] >= 0xF0 && p[0] <= 0xF4) {
                if (!p[1] || !p[2] || !p[3] || (p[1] & 0xC0) != 0x80 || (p[2] & 0xC0) != 0x80 || (p[3] & 0xC0) != 0x80) return -1;
                if ((p[0] == 0xF0 && p[1] < 0x90) || (p[0] == 0xF4 && p[1] >= 0x90)) return -1;
                cp = ((unsigned)(p[0] & 0x07) << 18) | ((unsigned)(p[1] & 0x3F) << 12) | ((unsigned)(p[2] & 0x3F) << 6) | (unsigned)(p[3] & 0x3F);
                bytes = 4;
            } else return -1;

            switch (cp) {
            case 0x00A0: rep = " "; break;                         /* non-breaking space */
            case 0x2018: case 0x2019: case 0x201A: case 0x201B: rep = "'"; break;
            case 0x201C: case 0x201D: case 0x201E: case 0x201F: rep = "\""; break;
            case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2015: rep = "-"; break;
            case 0x2026: rep = "..."; break;
            default: return -1;                                     /* preserve via system-font fallback */
            }
            p += bytes;
        }
        for (; *rep; rep++) {
            const int g = glyph_of(*rep);
            if (g < 0 || n >= LYF_MAXG) return -1;
            out[n++] = g;
        }
    }
    return n;
}

bool lyfont_can(const char *s)
{
    int g[LYF_MAXG];
    return g_ok && decode(s, g) > 0;
}

float lyfont_width(const char *s, float sc)
{
    int g[LYF_MAXG];
    const int n = g_ok ? decode(s, g) : -1;
    float w = 0.0f;
    for (int i = 0; i < n; i++) w += (float)lyf_adv[g[i]];
    return w * sc;
}

void lyfont_draw(const char *s, float x, float yc, float sc, u32 color, u32 align, float z)
{
    int g[LYF_MAXG];
    const int n = g_ok ? decode(s, g) : -1;
    if (n <= 0) return;
    float w = 0.0f;
    for (int i = 0; i < n; i++) w += (float)lyf_adv[g[i]];
    w *= sc;
    if (align & C2D_AlignCenter) x -= w * 0.5f;
    else if (align & C2D_AlignRight) x -= w;

    C2D_ImageTint tint;
    C2D_PlainImageTint(&tint, color, 1.0f);
    const float top = yc - LYF_YC * sc;
    for (int i = 0; i < n; i++) {
        const float a = (float)lyf_adv[g[i]];
        if (g[i] != 0) C2D_DrawImageAt(g_img[g[i]], x + (a - (float)LYF_CW) * 0.5f * sc, top, z, &tint, sc, sc);   /* ink is centred in its cell */
        x += a * sc;
    }
}
