#include "art.h"

#include <stdlib.h>
#include <string.h>

#include "libcfg.h"
#include "stb_image.h"

#define ART_SIZE        128
/* Old 3DS has little RAM: skip images above ~1.2 megapixels (placeholder is shown). */
#define ART_MAX_PIXELS  1200000L

static C3D_Tex            g_tex;
static bool               g_have;
static Tex3DS_SubTexture  g_sub = { ART_SIZE, ART_SIZE, 0.0f, 1.0f, 1.0f, 0.0f };
static C2D_Image          g_img;

static const unsigned char *g_default;
static size_t             g_default_size;

static unsigned char     *g_pending;
static size_t             g_pending_size;
static bool               g_pending_flag;

static u32                g_color;                /* ambient colour of the artwork on screen */
static bool               g_color_ok;
static unsigned           g_serial;

static u32 morton_interleave(u32 x, u32 y)
{
    u32 i = (x & 7) | ((y & 7) << 8);
    i = (i ^ (i << 2)) & 0x1313;
    i = (i ^ (i << 1)) & 0x1515;
    return (i | (i >> 7)) & 0x3F;
}

static u32 morton_offset(u32 x, u32 y, u32 bpp)
{
    u32 i = morton_interleave(x, y);
    u32 offset = (x & ~7u) * 8;
    return (i + offset) * bpp;
}

static void release_tex(void)
{
    if (g_have) {
        C3D_TexDelete(&g_tex);
        g_have = false;
    }
}

/* Pure-CPU half (safe on a worker thread): decode JPEG/PNG and box-filter it down to a 128x128
 * RGBA8 image in the GPU's tiled layout. `out` must hold ART_SIZE * ART_SIZE * 4 bytes. */
bool art_decode(const unsigned char *data, size_t size, unsigned char *out)
{
    int w = 0, h = 0, c = 0;
    if (!stbi_info_from_memory(data, (int)size, &w, &h, &c)) return false;
    if (w <= 0 || h <= 0 || (long)w * (long)h > ART_MAX_PIXELS) return false;

    unsigned char *px = stbi_load_from_memory(data, (int)size, &w, &h, &c, 4);
    if (!px) return false;
    unsigned char *dst = out;

    for (int dj = 0; dj < ART_SIZE; dj++) {
        int sj = dj;                                      /* top row first: fixes upside-down cover */
        int sy0 = sj * h / ART_SIZE, sy1 = (sj + 1) * h / ART_SIZE;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > h) sy1 = h;
        int stepy = (sy1 - sy0) / 3; if (stepy < 1) stepy = 1;

        for (int i = 0; i < ART_SIZE; i++) {
            int sx0 = i * w / ART_SIZE, sx1 = (i + 1) * w / ART_SIZE;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > w) sx1 = w;
            int stepx = (sx1 - sx0) / 3; if (stepx < 1) stepx = 1;

            unsigned r = 0, g = 0, b = 0, a = 0, cnt = 0;
            for (int y = sy0; y < sy1; y += stepy) {
                const unsigned char *row = px + (size_t)y * (size_t)w * 4;
                for (int x = sx0; x < sx1; x += stepx) {
                    const unsigned char *p = row + (size_t)x * 4;
                    r += p[0]; g += p[1]; b += p[2]; a += p[3];
                    cnt++;
                }
            }
            if (!cnt) cnt = 1;

            u32 coarse_y = (u32)dj & ~7u;
            u32 o = morton_offset((u32)i, (u32)dj, 4) + coarse_y * ART_SIZE * 4;
            dst[o + 0] = (unsigned char)(a / cnt);        /* GPU RGBA8 = bytes A,B,G,R */
            dst[o + 1] = (unsigned char)(b / cnt);
            dst[o + 2] = (unsigned char)(g / cnt);
            dst[o + 3] = (unsigned char)(r / cnt);
        }
    }
    stbi_image_free(px);
    return true;
}

/* GPU half (main thread only): wrap a decoded image in a texture the caller owns / deletes. */
bool art_upload(C3D_Tex *tex, const unsigned char *px)
{
    if (!C3D_TexInit(tex, ART_SIZE, ART_SIZE, GPU_RGBA8)) return false;
    memcpy(tex->data, px, (size_t)ART_SIZE * ART_SIZE * 4);
    GSPGPU_FlushDataCache(tex->data, ART_SIZE * ART_SIZE * 4);
    C3D_TexSetFilter(tex, GPU_LINEAR, GPU_NEAREST);
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    return true;
}

/* Decode JPEG/PNG into a texture of tw x th (powers of two, >= the image) - used for the boot screen art.
 * The image sits in the top-left corner; *iw / *ih return its size so the caller can build a sub-texture. */
bool art_build_tex_ex(C3D_Tex *tex, const unsigned char *data, size_t size, int tw, int th, int *iw, int *ih)
{
    int w = 0, h = 0, c = 0;
    if (!stbi_info_from_memory(data, (int)size, &w, &h, &c) || w <= 0 || h <= 0 || w > tw || h > th) return false;
    unsigned char *px = stbi_load_from_memory(data, (int)size, &w, &h, &c, 4);
    if (!px) return false;
    const size_t bytes = (size_t)tw * (size_t)th * 4;
    unsigned char *out = calloc(1, bytes);
    if (!out) { stbi_image_free(px); return false; }
    for (int y = 0; y < h; y++) {
        const unsigned char *row = px + (size_t)y * (size_t)w * 4;
        for (int x = 0; x < w; x++) {
            const unsigned char *p = row + (size_t)x * 4;
            unsigned char *d = out + morton_offset((u32)x, (u32)y, 4) + ((u32)y & ~7u) * (u32)tw * 4;
            d[0] = p[3]; d[1] = p[2]; d[2] = p[1]; d[3] = p[0];        /* GPU RGBA8 = bytes A,B,G,R */
        }
    }
    stbi_image_free(px);
    bool ok = C3D_TexInit(tex, tw, th, GPU_RGBA8);
    if (ok) {
        memcpy(tex->data, out, bytes);
        GSPGPU_FlushDataCache(tex->data, bytes);
        C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
        *iw = w; *ih = h;
    }
    free(out);
    return ok;
}

/* Decode JPEG/PNG into a 128x128 GPU texture `tex` (caller owns / deletes it). */
bool art_build_tex(C3D_Tex *tex, const unsigned char *data, size_t size)
{
    unsigned char *buf = malloc((size_t)ART_SIZE * ART_SIZE * 4);
    if (!buf) return false;
    bool ok = art_decode(data, size, buf) && art_upload(tex, buf);
    free(buf);
    return ok;
}

/* Dominant-hue colour: pixels vote into 12 hue bins weighted by saturation x brightness; the winning
 * bin (plus its two neighbours) is averaged. Covers with no real colour fall back to a plain average. */
bool art_color_of(const unsigned char *px, u32 *rgb)
{
    enum { HUES = 12 };
    unsigned long w_sum[HUES] = { 0 }, r_sum[HUES] = { 0 }, g_sum[HUES] = { 0 }, b_sum[HUES] = { 0 };
    unsigned long ar = 0, ag = 0, ab = 0, an = 0;

    for (int i = 0; i < ART_SIZE * ART_SIZE; i += 2) {         /* every other pixel is plenty */
        const unsigned char *p = px + (size_t)i * 4;            /* bytes are A,B,G,R */
        int b = p[1], g = p[2], r = p[3];
        ar += (unsigned long)r; ag += (unsigned long)g; ab += (unsigned long)b; an++;

        int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
        int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
        int d = mx - mn;
        if (mx < 40 || d < 24) continue;                        /* too dark / too grey to carry a hue */

        int h;
        if (mx == r)      h = 60 * (g - b) / d;
        else if (mx == g) h = 120 + 60 * (b - r) / d;
        else              h = 240 + 60 * (r - g) / d;
        if (h < 0) h += 360;
        int bin = (h * HUES / 360) % HUES;

        unsigned long w = (unsigned long)((d * 255 / mx) * mx) >> 8;     /* saturation x value, 0..255 */
        w_sum[bin] += w;
        r_sum[bin] += (unsigned long)r * w;
        g_sum[bin] += (unsigned long)g * w;
        b_sum[bin] += (unsigned long)b * w;
    }
    if (!an) return false;

    int best = 0;
    unsigned long best_score = 0;
    for (int k = 0; k < HUES; k++) {
        unsigned long s = w_sum[k] * 2 + w_sum[(k + 1) % HUES] + w_sum[(k + HUES - 1) % HUES];
        if (s > best_score) { best_score = s; best = k; }
    }

    int r, g, b;
    if (best_score > an * 8) {                                  /* a real colour is present */
        unsigned long w = 0, rr = 0, gg = 0, bb = 0;
        for (int k = -1; k <= 1; k++) {
            int j = (best + k + HUES) % HUES;
            w += w_sum[j]; rr += r_sum[j]; gg += g_sum[j]; bb += b_sum[j];
        }
        if (!w) return false;
        r = (int)(rr / w); g = (int)(gg / w); b = (int)(bb / w);
        int m = r > g ? (r > b ? r : b) : (g > b ? g : b);      /* bring it to a bright, clean glow colour */
        if (m < 1) return false;
        int target = m < 170 ? 170 : (m > 240 ? 240 : m);
        r = r * target / m; g = g * target / m; b = b * target / m;
    } else {                                                    /* grey / black-and-white cover */
        r = (int)(ar / an); g = (int)(ag / an); b = (int)(ab / an);
        int m = r > g ? (r > b ? r : b) : (g > b ? g : b);
        if (m < 20) return false;                               /* (nearly) black: no glow */
        if (m > 200) { r = r * 200 / m; g = g * 200 / m; b = b * 200 / m; }
    }
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    *rgb = ((u32)r << 16) | ((u32)g << 8) | (u32)b;
    return true;
}

bool art_color(u32 *rgb)
{
    if (!g_color_ok) return false;
    *rgb = g_color;
    return true;
}

static bool build_texture(const unsigned char *data, size_t size, bool real)
{
    unsigned char *buf = malloc((size_t)ART_SIZE * ART_SIZE * 4);
    if (!buf) return false;
    bool ok = art_decode(data, size, buf) && art_upload(&g_tex, buf);
    if (ok && real) g_color_ok = art_color_of(buf, &g_color);
    free(buf);
    if (!ok) return false;
    g_img.tex = &g_tex;
    g_img.subtex = &g_sub;
    return true;
}

void art_set_default(const unsigned char *data, size_t size)
{
    g_default = data;
    g_default_size = size;
}

void art_queue(unsigned char *data, size_t size)
{
    free(g_pending);
    g_pending = data;
    g_pending_size = size;
    g_pending_flag = true;
}

void art_commit(void)
{
    if (!g_pending_flag) return;
    g_pending_flag = false;

    release_tex();
    g_serial++;
    g_color_ok = false;
    if (g_pending && g_pending_size) g_have = build_texture(g_pending, g_pending_size, true);
    if (!g_have && g_default && g_default_size) g_have = build_texture(g_default, g_default_size, false);
    free(g_pending);
    g_pending = NULL;
    g_pending_size = 0;
}

bool art_ready(void) { return g_have; }
unsigned art_serial(void) { return g_serial; }
const C2D_Image *art_image(void) { return &g_img; }

void art_shutdown(void)
{
    free(g_pending);
    g_pending = NULL;
    release_tex();
}
