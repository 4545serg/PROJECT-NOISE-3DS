#include "flow.h"

#include <3ds.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "art.h"
#include "meta.h"
#include "noise.h"

#define SLOTS      20
#define COVER_DIR  DATA_DIR "/covers"
#define CACHE_MAGIC 0x33435A4Eu          /* "NZC3" - invalidate pre-fix cover cache */
#define CX        200.0f       /* screen-space centre of the flow (top screen is 400 wide) */
#define CY        90.0f        /* vertical centre of the covers (cover is 124 tall at y=28; keep in sync with ART_* in main.c) */
#define FACE      124.0f       /* face-on cover size */
#define SIDE_W    44.0f        /* on-screen width of a turned cover */
#define INNER_GAP 6.0f         /* gap between the centre cover and the first side cover */
#define STEP      30.0f        /* spacing of the side covers */
#define REFLECT   0.18f        /* reflection height as a fraction of the cover */
#define FADE_FRAMES 10.0f

typedef struct {
    int      idx, gen, state;  /* state: 0 free, 1 own texture, 2 no artwork (placeholder) */
    unsigned last_use, born;
    bool     has_rgb;          /* rgb = ambient colour of the cover (only for state 1) */
    u32      rgb;
    C3D_Tex  tex;
} Slot;

/* ---- main-thread cache ---- */
static Slot     g_slot[SLOTS];
static unsigned g_tick;
static C3D_Tex  g_ph;
static bool     g_ph_ok;

/* ---- shared with the loader thread (guarded by g_lk) ---- */
static LightLock     g_lk;
static Thread        g_thread;
static volatile bool g_quit;
static volatile int  g_gen = 1;
static FlowWant      g_want[FLOW_MAX_WANT];
static int           g_nwant;
static int           g_busy = -1;                 /* index being loaded right now */
static bool          g_mb_full;                   /* one finished cover waiting for upload */
static int           g_mb_idx, g_mb_gen, g_mb_state;
static bool          g_mb_has_rgb;
static u32           g_mb_rgb;
static u8           *g_mb_px;                     /* the loader decodes straight into this */

/* ------------------------------------------------------- loader thread */
static u32 fnv(const char *s, u32 h)
{
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

/* returns 1 = cover decoded into px, 2 = no usable artwork */
static int load_job(const char *path, u8 *px)
{
    struct stat st;
    u32 size = 0, mtime = 0;
    if (stat(path, &st) == 0) { size = (u32)st.st_size; mtime = (u32)st.st_mtime; }

    char cpath[96];
    snprintf(cpath, sizeof cpath, COVER_DIR "/%08x%08x.cv", (unsigned)fnv(path, 2166136261u),
             (unsigned)fnv(path, 0x9E3779B9u));

    FILE *cf = fopen(cpath, "rb");
    if (cf) {
        u32 hd[4];
        if (fread(hd, sizeof(u32), 4, cf) == 4 && hd[0] == CACHE_MAGIC && hd[1] == size && hd[2] == mtime) {
            if (hd[3] == 2) { fclose(cf); return 2; }
            if (hd[3] == 1 && fread(px, 1, ART_PIXEL_BYTES, cf) == ART_PIXEL_BYTES) { fclose(cf); return 1; }
        }
        fclose(cf);
    }

    TrackMeta m;
    u8 *art = NULL;
    size_t asz = 0;
    int state = 2;
    if (meta_read(path, &m, &art, &asz, false) && art && asz && art_decode(art, asz, px)) state = 1;
    free(art);

    /* High-success artwork fallback: many perfectly valid music libraries keep
     * cover.jpg/folder.jpg/album.jpg/front.jpg beside the audio instead of embedding APIC. */
    if (state != 1) {
        const char *slash = strrchr(path, '/');
        char dir[800];
        if (slash) {
            size_t dn = (size_t)(slash - path + 1);
            if (dn >= sizeof dir) dn = sizeof dir - 1;
            memcpy(dir, path, dn); dir[dn] = 0;
        } else { dir[0] = 0; }
        static const char *names[] = { "cover.jpg", "cover.jpeg", "cover.png", "folder.jpg", "folder.png", "album.jpg", "album.png", "front.jpg", "front.png" };
        for (unsigned ni = 0; ni < sizeof(names)/sizeof(names[0]) && state != 1; ni++) {
            char ap[800];
            snprintf(ap, sizeof ap, "%s%s", dir, names[ni]);
            FILE *af = fopen(ap, "rb");
            if (!af) continue;
            fseek(af, 0, SEEK_END);
            long alen = ftell(af);
            fseek(af, 0, SEEK_SET);
            if (alen > 0 && alen <= 3 * 1024 * 1024L) {
                u8 *ab = malloc((size_t)alen);
                if (ab && fread(ab, 1, (size_t)alen, af) == (size_t)alen && art_decode(ab, (size_t)alen, px)) state = 1;
                free(ab);
            }
            fclose(af);
        }
    }

    cf = fopen(cpath, "wb");
    if (cf) {
        u32 hd[4] = { CACHE_MAGIC, size, mtime, (u32)state };
        fwrite(hd, sizeof(u32), 4, cf);
        if (state == 1) fwrite(px, 1, ART_PIXEL_BYTES, cf);
        fclose(cf);
    }
    return state;
}

static void loader(void *arg)
{
    (void)arg;
    while (!g_quit) {
        FlowWant job;
        job.idx = -1;
        job.path[0] = 0;
        int gen = 0;
        bool have = false;

        LightLock_Lock(&g_lk);
        if (!g_mb_full && g_nwant > 0) {
            job = g_want[0];
            memmove(&g_want[0], &g_want[1], sizeof(FlowWant) * (size_t)(g_nwant - 1));
            g_nwant--;
            g_busy = job.idx;
            gen = g_gen;
            have = true;
        }
        LightLock_Unlock(&g_lk);

        if (!have) { svcSleepThread(8 * 1000 * 1000LL); continue; }

        int st = load_job(job.path, g_mb_px);
        u32 rgb = 0;
        bool has_rgb = (st == 1) && art_color_of(g_mb_px, &rgb);      /* ambient colour, cheap */

        LightLock_Lock(&g_lk);
        if (gen == g_gen) {
            g_mb_idx = job.idx; g_mb_gen = gen; g_mb_state = st;
            g_mb_has_rgb = has_rgb; g_mb_rgb = rgb;
            g_mb_full = true;
        }
        g_busy = -1;
        LightLock_Unlock(&g_lk);
    }
}

/* ------------------------------------------------------------------ cache */
void flow_init(const unsigned char *png, size_t size)
{
    g_ph_ok = (png && size) ? art_build_tex(&g_ph, png, size) : false;

    mkdir(DATA_ROOT, 0777);
    mkdir(DATA_DIR, 0777);
    mkdir(COVER_DIR, 0777);

    LightLock_Init(&g_lk);
    g_mb_px = malloc(ART_PIXEL_BYTES);
    if (!g_mb_px) return;

    APT_SetAppCpuTimeLimit(30);                   /* lets an Old 3DS app use the second core */
    g_thread = threadCreate(loader, NULL, 128 * 1024, 0x30, 1, false);
    if (!g_thread) g_thread = threadCreate(loader, NULL, 128 * 1024, 0x3F, -2, false);
}

static void slot_free(Slot *s)
{
    if (s->state == 1) C3D_TexDelete(&s->tex);
    s->state = 0;
}

void flow_shutdown(void)
{
    if (g_thread) {
        g_quit = true;
        threadJoin(g_thread, U64_MAX);
        threadFree(g_thread);
        g_thread = NULL;
    }
    for (int i = 0; i < SLOTS; i++) slot_free(&g_slot[i]);
    if (g_ph_ok) { C3D_TexDelete(&g_ph); g_ph_ok = false; }
    free(g_mb_px);
    g_mb_px = NULL;
}

/* Textures are freed lazily (when a slot is reused) because the GPU may still be reading
 * them for the frame in flight when the folder changes. */
void flow_reset(void)
{
    LightLock_Lock(&g_lk);
    g_gen++;
    g_nwant = 0;
    LightLock_Unlock(&g_lk);
}

static Slot *find(int idx)
{
    for (int i = 0; i < SLOTS; i++)
        if (g_slot[i].state && g_slot[i].gen == g_gen && g_slot[i].idx == idx) return &g_slot[i];
    return NULL;
}

bool flow_color(int idx, u32 *rgb)
{
    Slot *s = find(idx);
    if (!s || s->state != 1 || !s->has_rgb) return false;
    *rgb = s->rgb;
    return true;
}

bool flow_known(int idx)
{
    if (find(idx)) return true;
    LightLock_Lock(&g_lk);
    bool k = (g_busy == idx) || (g_mb_full && g_mb_idx == idx && g_mb_gen == g_gen);
    LightLock_Unlock(&g_lk);
    return k;
}

void flow_want(const FlowWant *w, int n)
{
    if (n > FLOW_MAX_WANT) n = FLOW_MAX_WANT;
    LightLock_Lock(&g_lk);
    memcpy(g_want, w, sizeof(FlowWant) * (size_t)n);
    g_nwant = n;
    LightLock_Unlock(&g_lk);
}

C3D_Tex *flow_tex(int idx, float *fade)
{
    Slot *s = find(idx);
    if (!s) return NULL;
    s->last_use = g_tick;
    if (fade) {
        float f = ((float)(g_tick - s->born)) / FADE_FRAMES;
        *fade = f > 1.0f ? 1.0f : f;
    }
    if (s->state == 1) return &s->tex;
    return g_ph_ok ? &g_ph : NULL;
}

bool flow_pump(void)
{
    g_tick++;

    LightLock_Lock(&g_lk);
    bool full = g_mb_full;
    int idx = g_mb_idx, gen = g_mb_gen, st = g_mb_state;
    bool has_rgb = g_mb_has_rgb;
    u32 rgb = g_mb_rgb;
    LightLock_Unlock(&g_lk);
    if (!full) return false;

    if (gen == g_gen) {                            /* the loader never touches g_mb_px while it is full */
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
        v->idx = idx;
        v->gen = g_gen;
        v->state = (st == 1 && art_upload(&v->tex, g_mb_px)) ? 1 : 2;
        v->has_rgb = (v->state == 1) && has_rgb;
        v->rgb = rgb;
        v->last_use = g_tick;
        v->born = g_tick;
    }

    LightLock_Lock(&g_lk);
    g_mb_full = false;
    LightLock_Unlock(&g_lk);
    return true;
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

static void draw_item(const FlowItem *it, float o, float appear, FlowBgFn bgf)
{
    float ao = fabsf(o), s = (o < 0) ? -1.0f : 1.0f;
    float t = clampf(ao, 0.0f, 1.0f);                   /* 0 = face-on, 1 = fully turned */
    float d = ao > 1.0f ? ao - 1.0f : 0.0f;
    float w = FACE - (FACE - SIDE_W) * t;
    float disp = t * (FACE / 2 + INNER_GAP + SIDE_W / 2) + d * STEP;
    float cx = CX + s * disp * appear;
    float xl = cx - w / 2, xr = cx + w / 2;
    u32 bg = bgf(cx, CY);                                /* background colour right behind this cover */

    float H  = FACE - 3.0f * fminf(d, 3.0f);
    float hi = H * (1.0f - 0.16f * t);                   /* edge nearest the centre is further away */
    float ho = H;
    float hl = (s < 0) ? ho : hi;                        /* left edge height  */
    float hr = (s < 0) ? hi : ho;                        /* right edge height */

    float fade = clampf((ao - 2.2f) / 1.3f, 0.0f, 1.0f);
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
    int n = (t < 0.02f) ? 1 : (int)ceilf(w / 3.0f);
    if (n < 1) n = 1;
    float refl = REFLECT;

    /* one tint for both effects: cross-fade card -> cover (fade-in) and dim toward the background */
    float unfaded = 1.0f - clampf(it->fade, 0.0f, 1.0f);            /* 1 = still the flat card, 0 = cover fully in */
    float tint_blend = 1.0f - (1.0f - unfaded) * (1.0f - dim);
    u32 tint_col = bg;
    if (tint_blend > 0.001f) tint_col = mix32(bg, it->color, unfaded * (1.0f - dim) / tint_blend);

    /* reflection fades into the real (glow-tinted) background: sample it once per strip edge, not per corner */
    float hmax = fmaxf(hl, hr);
    float ry0 = CY + hmax / 2 + 1, ry1 = ry0 + hmax * refl;
    u32 bg_top = bgf(xl, ry0), bg_bot = bgf(xl, ry1);               /* colours at the current strip's left edge */

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
        C2D_PlainImageTint(&tint, tint_col, tint_blend);
        C2D_DrawImage(img, &p, tint_blend > 0.01f ? &tint : NULL);

        /* mirrored reflection fading into the background */
        float rh = h * refl;
        sub.height = (u16)rh;
        sub.top = 0.0f; sub.bottom = refl;
        p.pos.y = CY + h / 2 + 1; p.pos.h = rh; p.depth = z - 0.02f;
        float top_b = clampf(0.74f + dim + unfaded, 0.0f, 1.0f), bot_b = 1.0f;
        u32 next_top = (i + 1 < n) ? bgf(x1, ry0) : bgf(xr, ry0);
        u32 next_bot = (i + 1 < n) ? bgf(x1, ry1) : bgf(xr, ry1);
        C2D_SetImageTint(&tint, C2D_TopLeft,  bg_top,   top_b);
        C2D_SetImageTint(&tint, C2D_TopRight, next_top, top_b);
        C2D_SetImageTint(&tint, C2D_BotLeft,  bg_bot,   bot_b);
        C2D_SetImageTint(&tint, C2D_BotRight, next_bot, bot_b);
        C2D_DrawImage(img, &p, &tint);
        bg_top = next_top;                                           /* this strip's right edge is the next one's left */
        bg_bot = next_bot;
    }
}

void flow_draw(const FlowItem *items, int n, int base, float pos, float appear, FlowBgFn bg)
{
    int centre = -1;
    float best = 1e9f;
    for (int i = 0; i < n; i++) {
        float o = (float)(base + i) - pos;
        if (fabsf(o) < best) { best = fabsf(o); centre = i; }
    }
    for (int i = 0; i < n; i++) {
        if (!items[i].kind) continue;
        float o = (float)(base + i) - pos;
        if (fabsf(o) > FLOW_SIDE + 0.6f) continue;
        if (i != centre && appear < 0.03f) continue;     /* collapsed: only the centre remains */
        draw_item(&items[i], o, appear, bg);
    }
}
