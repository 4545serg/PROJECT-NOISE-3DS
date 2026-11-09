#include "art.h"

#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

#define ART_SIZE        128
/* Old 3DS has little RAM: skip images above ~1.2 megapixels (placeholder is shown). */
#define ART_MAX_PIXELS  1200000L

static C3D_Tex            g_tex;
static bool               g_have;
static Tex3DS_SubTexture  g_sub = { ART_SIZE, ART_SIZE, 0.0f, 1.0f, 1.0f, 0.0f };
static C2D_Image          g_img;

static unsigned char     *g_pending;
static size_t             g_pending_size;
static bool               g_pending_flag;

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

static bool build_texture(const unsigned char *data, size_t size)
{
    int w = 0, h = 0, c = 0;
    if (!stbi_info_from_memory(data, (int)size, &w, &h, &c)) return false;
    if (w <= 0 || h <= 0 || (long)w * (long)h > ART_MAX_PIXELS) return false;

    unsigned char *px = stbi_load_from_memory(data, (int)size, &w, &h, &c, 4);
    if (!px) return false;

    if (!C3D_TexInit(&g_tex, ART_SIZE, ART_SIZE, GPU_RGBA8)) {
        stbi_image_free(px);
        return false;
    }
    unsigned char *dst = (unsigned char *)g_tex.data;

    for (int dj = 0; dj < ART_SIZE; dj++) {
        int sj = ART_SIZE - 1 - dj;                       /* GPU textures are stored bottom-up */
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

    GSPGPU_FlushDataCache(g_tex.data, ART_SIZE * ART_SIZE * 4);
    C3D_TexSetFilter(&g_tex, GPU_LINEAR, GPU_NEAREST);
    C3D_TexSetWrap(&g_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    g_img.tex = &g_tex;
    g_img.subtex = &g_sub;
    return true;
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
    if (g_pending && g_pending_size) g_have = build_texture(g_pending, g_pending_size);
    free(g_pending);
    g_pending = NULL;
    g_pending_size = 0;
}

bool art_ready(void) { return g_have; }
const C2D_Image *art_image(void) { return &g_img; }

void art_shutdown(void)
{
    free(g_pending);
    g_pending = NULL;
    release_tex();
}
