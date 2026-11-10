#include "flow.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "meta.h"

#define SLOTS     12
#define CX        200.0f       /* screen-space centre of the flow (top screen is 400 wide) */
#define CY        72.0f        /* vertical centre of the covers (cover is 128 tall at y=8) */
#define FACE      128.0f       /* face-on cover size */
#define SIDE_W    40.0f        /* on-screen width of a turned cover */
#define INNER_GAP 6.0f         /* gap between the centre cover and the first side cover */
#define STEP      28.0f        /* spacing of the side covers */
#define REFLECT   0.22f        /* reflection height as a fraction of the cover */

typedef struct {
    int     idx, gen, state;   /* state: 0 free, 1 own texture, 2 no artwork (placeholder) */
    unsigned last_use;
    C3D_Tex tex;
} Slot;

static Slot     g_slot[SLOTS];
static int      g_gen = 1;
static unsigned g_tick;
static C3D_Tex  g_ph;
static bool     g_ph_ok;

/* ------------------------------------------------------------------ cache */
void flow_init(const unsigned char *png, size_t size)
{
    g_ph_ok = (png && size) ? art_build_tex(&g_ph, png, size) : false;
}

static void slot_free(Slot *s)
{
    if (s->state == 1) C3D_TexDelete(&s->tex);
    s->state = 0;
}

void flow_shutdown(void)
{
    for (int i = 0; i < SLOTS; i++) slot_free(&g_slot[i]);
    if (g_ph_ok) { C3D_TexDelete(&g_ph); g_ph_ok = false; }
}

/* Textures are freed lazily (in flow_load) because the GPU may still be reading
 * them for the frame in flight when the folder changes. */
void flow_reset(void) { g_gen++; }

void flow_begin_frame(void) { g_tick++; }

static Slot *find(int idx)
{
    for (int i = 0; i < SLOTS; i++)
        if (g_slot[i].state && g_slot[i].gen == g_gen && g_slot[i].idx == idx) return &g_slot[i];
    return NULL;
}

bool flow_has(int idx) { return find(idx) != NULL; }

C3D_Tex *flow_tex(int idx)
{
    Slot *s = find(idx);
    if (!s) return NULL;
    s->last_use = g_tick;
    if (s->state == 1) return &s->tex;
    return g_ph_ok ? &g_ph : NULL;
}

void flow_load(int idx, const char *path)
{
    if (find(idx)) return;

    Slot *v = NULL;
    for (int i = 0; i < SLOTS && !v; i++)
        if (g_slot[i].state == 0) v = &g_slot[i];
    for (int i = 0; i < SLOTS && !v; i++)
        if (g_slot[i].gen != g_gen) v = &g_slot[i];
    if (!v) {
        v = &g_slot[0];
        for (int i = 1; i < SLOTS; i++)
            if (g_slot[i].last_use < v->last_use) v = &g_slot[i];
    }
    slot_free(v);

    TrackMeta m;
    u8 *art = NULL;
    size_t art_size = 0;
    int state = 2;
    if (meta_read(path, &m, &art, &art_size, false) && art && art_size) {
        if (art_build_tex(&v->tex, art, art_size)) state = 1;
    }
    free(art);

    v->idx = idx;
    v->gen = g_gen;
    v->state = state;
    v->last_use = g_tick;
}

/* ---------------------------------------------------------------- drawing */
static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

static u32 mix32(u32 a, u32 b, float w)        /* w = weight of b; both 0xAABBGGRR from C2D_Color32 */
{
    u32 out = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        float ca = (float)((a >> sh) & 0xFF), cb = (float)((b >> sh) & 0xFF);
        out |= (u32)(ca + (cb - ca) * w) << sh;
    }
    return out | 0xFF000000u;
}

static void quad_solid(float xl, float xr, float hl, float hr, u32 c, float z)
{
    float tl = CY - hl / 2, bl = CY + hl / 2, tr = CY - hr / 2, br = CY + hr / 2;
    C2D_DrawTriangle(xl, tl, c, xr, tr, c, xl, bl, c, z);
    C2D_DrawTriangle(xr, tr, c, xr, br, c, xl, bl, c, z);
}

static void draw_item(const FlowItem *it, float o, float appear, u32 bg)
{
    float ao = fabsf(o), s = (o < 0) ? -1.0f : 1.0f;
    float t = clampf(ao, 0.0f, 1.0f);                   /* 0 = face-on, 1 = fully turned */
    float d = ao > 1.0f ? ao - 1.0f : 0.0f;
    float w = FACE - (FACE - SIDE_W) * t;
    float disp = t * (FACE / 2 + INNER_GAP + SIDE_W / 2) + d * STEP;
    float cx = CX + s * disp * appear;
    float xl = cx - w / 2, xr = cx + w / 2;

    float H  = FACE - 3.0f * fminf(d, 3.0f);
    float hi = H * (1.0f - 0.16f * t);                   /* edge nearest the centre is further away */
    float ho = H;
    float hl = (s < 0) ? ho : hi;                        /* left edge height  */
    float hr = (s < 0) ? hi : ho;                        /* right edge height */

    float fade = clampf((ao - 3.2f) / 1.3f, 0.0f, 1.0f);
    float dim  = clampf(0.30f * t + fade + (1.0f - appear), 0.0f, 1.0f);   /* blend toward bg */
    float z = 0.30f + (6.0f - fminf(ao, 6.0f)) * 0.012f;

    if (!it->tex) {                                      /* folder / still loading: flat card */
        u32 c = mix32(it->color, bg, dim);
        quad_solid(xl, xr, hl, hr, c, z);
        if (t < 0.3f) {                                  /* little glyph on the centre card */
            u32 g = mix32(0xFFFFFFFFu, bg, 0.35f + dim);
            if (it->kind == 1) {
                C2D_DrawRectSolid(CX - 14, CY - 10, z + 0.01f, 12, 6, g);
                C2D_DrawRectSolid(CX - 14, CY - 5, z + 0.01f, 28, 18, g);
            } else {
                C2D_DrawCircleSolid(CX - 4, CY + 10, z + 0.01f, 7, g);
                C2D_DrawRectSolid(CX + 1, CY - 20, z + 0.01f, 4, 30, g);
            }
        }
        return;
    }

    C2D_Image img;
    img.tex = it->tex;
    int n = (t < 0.02f) ? 1 : (int)ceilf(w / 2.0f);
    if (n < 1) n = 1;
    float refl = REFLECT;

    for (int i = 0; i < n; i++) {
        float f0 = (float)i / n, f1 = (float)(i + 1) / n;
        float fm = (f0 + f1) * 0.5f;
        float x0 = xl + (xr - xl) * f0, x1 = xl + (xr - xl) * f1;
        float h = hl + (hr - hl) * fm;
        Tex3DS_SubTexture sub;
        C2D_DrawParams p;
        C2D_ImageTint tint;

        /* the cover itself (orientation matches the main now-playing cover: top = v 1.0) */
        sub.width = (u16)(w / n + 1); sub.height = (u16)h;
        sub.left = f0; sub.right = f1; sub.top = 1.0f; sub.bottom = 0.0f;
        img.subtex = &sub;
        p.pos.x = x0; p.pos.y = CY - h / 2; p.pos.w = (x1 - x0) + (n > 1 ? 0.6f : 0.0f); p.pos.h = h;
        p.center.x = 0; p.center.y = 0; p.depth = z; p.angle = 0;
        C2D_PlainImageTint(&tint, bg, dim);
        C2D_DrawImage(img, &p, dim > 0.01f ? &tint : NULL);

        /* mirrored reflection fading into the background */
        float rh = h * refl;
        sub.height = (u16)rh;
        sub.top = 0.0f; sub.bottom = refl;
        p.pos.y = CY + h / 2 + 1; p.pos.h = rh; p.depth = z - 0.02f;
        float top_b = clampf(0.62f + dim, 0.0f, 1.0f), bot_b = 1.0f;
        C2D_SetImageTint(&tint, C2D_TopLeft,  bg, top_b);
        C2D_SetImageTint(&tint, C2D_TopRight, bg, top_b);
        C2D_SetImageTint(&tint, C2D_BotLeft,  bg, bot_b);
        C2D_SetImageTint(&tint, C2D_BotRight, bg, bot_b);
        C2D_DrawImage(img, &p, &tint);
    }
}

void flow_draw(const FlowItem *items, int n, int base, float pos, float appear, u32 bg)
{
    int centre = -1;
    float best = 1e9f;
    for (int i = 0; i < n; i++) {
        float o = (float)(base + i) - pos;
        if (items[i].kind && fabsf(o) < best) { best = fabsf(o); centre = i; }
    }
    for (int i = 0; i < n; i++) {
        if (!items[i].kind) continue;
        float o = (float)(base + i) - pos;
        if (fabsf(o) > FLOW_SIDE + 0.6f) continue;
        if (i != centre && appear < 0.03f) continue;     /* collapsed: only the centre remains */
        draw_item(&items[i], o, appear, bg);
    }
}
