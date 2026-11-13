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
static Tex3DS_SubTexture g_sub[LYF_NGLYPH];
static C2D_Image         g_img[LYF_NGLYPH];

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

    for (int i = 0; i < LYF_NGLYPH; i++) {
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

static int glyph_of_cp(unsigned cp)                              /* accented letters and inverted marks (lyf_ext) */
{
    for (int i = 0; i < LYF_NGLYPH - LYF_NASCII; i++)
        if (lyf_ext[i] == cp) return LYF_NASCII + i;
    return -1;
}

/* letter + combining mark (U+0301 acute, U+0303 tilde, U+0308 diaeresis) -> precomposed code point, 0 = no such glyph */
static unsigned compose(char base, unsigned mark)
{
    static const char           V[] = "aeiouAEIOU";
    static const unsigned short A[] = { 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xC1, 0xC9, 0xCD, 0xD3, 0xDA };
    if (mark == 0x301 && base && strchr(V, base)) return A[strchr(V, base) - V];
    if (mark == 0x303 && (base == 'n' || base == 'N')) return base == 'n' ? 0xF1 : 0xD1;
    if (mark == 0x308 && (base == 'u' || base == 'U')) return base == 'u' ? 0xFC : 0xDC;
    return 0;
}

/* UTF-8 -> glyph indices. Returns the count, or -1 when something has no glyph. */
static int decode(const char *s, int *out)
{
    int n = 0;
    char base = 0;                                              /* previous character if it was a plain ASCII letter */
    for (const unsigned char *p = (const unsigned char *)s; *p; ) {
        const char *rep = NULL;
        char one[2] = { 0, 0 };
        const char prev = base;
        base = 0;
        if (*p < 0x80) {
            char c = (char)*p++;
            if (c == '\t' || c == '\r' || c == '\n') c = ' ';
            if (c == '`') c = '\'';
            if (c == '[') c = '(';
            if (c == ']') c = ')';
            if (c == '_') c = '-';
            if (c == '{') c = '(';
            if (c == '}') c = ')';
            one[0] = c;                                          /* lower case stays lower case */
            rep = one;
            if (isalpha((unsigned char)c)) base = c;
        } else if (p[0] == 0xE2 && p[1] == 0x80) {
            const unsigned char k = p[2];
            if (k == 0x98 || k == 0x99) rep = "'";
            else if (k == 0x9C || k == 0x9D) rep = "\"";
            else if (k == 0x93 || k == 0x94) rep = "-";
            else if (k == 0xA6) rep = "...";
            else return -1;
            p += 3;
        } else if (p[0] >= 0xC2 && p[0] <= 0xDF && (p[1] & 0xC0) == 0x80) {      /* two-byte sequence: Latin-1 range, combining marks */
            const unsigned cp = ((unsigned)(p[0] & 0x1F) << 6) | (unsigned)(p[1] & 0x3F);
            p += 2;
            if (cp == 0xA0) rep = " ";
            else if (cp == 0xAB || cp == 0xBB) rep = "\"";                /* guillemets */
            else if (cp >= 0x300 && cp <= 0x36F) {                       /* decomposed accent: merge it into the letter before it */
                const unsigned pre = compose(prev, cp);
                const int g = (pre && n > 0) ? glyph_of_cp(pre) : -1;
                if (g < 0) return -1;
                out[n - 1] = g;
                continue;
            } else {
                const int g = glyph_of_cp(cp);
                if (g < 0 || n >= LYF_MAXG) return -1;
                out[n++] = g;
                continue;
            }
        }
        else return -1;                                         /* other scripts / symbols: system font */

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

const char *lyfont_fit(const char *s, float sc, float maxw, char *out, size_t cap)
{
    if (lyfont_width(s, sc) <= maxw) return s;
    size_t len = strlen(s);
    if (len > cap - 4) {
        len = cap - 4;
        while (len && ((unsigned char)s[len] & 0xC0) == 0x80) len--;
    }
    memcpy(out, s, len);
    out[len] = 0;
    while (len) {                                                /* drop whole characters until "text..." fits */
        do len--; while (len && ((unsigned char)out[len] & 0xC0) == 0x80);
        memcpy(out + len, "...", 4);
        if (lyfont_width(out, sc) <= maxw) break;
    }
    return out;
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
