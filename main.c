/*
 * PROJECT NOISE - native Nintendo 3DS music player
 *
 * Top screen : NOW PLAYING (artwork, title, artist, progress, time)
 * Bottom     : file browser + touch controls (or the settings page)
 */
#include <3ds.h>
#include <citro2d.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "art.h"
#include "audio.h"
#include "browser.h"
#include "flow.h"
#include "lyrics.h"
#include "lyfont.h"
#include "meta.h"
#include "placeholder.h"
#include "search.h"
#include "settings.h"
#include "noise.h"

/* ---------------------------------------------------------------- layout */
#define LIST_TOP   66          /* song list: 4 rows of ROW_H, scrollbar column on the right */
#define ROW_H      42
#define VIS_ROWS   3
#define CTRL_TOP   194         /* now-playing deck: Now Playing | transport | volume */
#define SL_X0      14          /* seek line inside the Now Playing capsule, x range */
#define SL_X1      112
#define VOL_X0     268         /* volume bar inside the volume capsule, x range */
#define VOL_X1     304
#define SB_TAP_X   300         /* touches right of this x in the list hit the scrollbar */
#define SB_X0      8           /* search bar (full width under the tabs) */
#define SS_X0      96          /* settings slider track, x range */
#define SS_X1      244

/* Audio tab: Manual-EQ / preset row, then five vertical band sliders. */
#define SND_TOP    70
#define SND_CTL_Y  SND_TOP
#define SND_CTL_H  26
#define SND_EQ_Y   (SND_CTL_Y + SND_CTL_H)
#define SND_BOTTOM 226
#define EQ_COL_X0  40          /* left edge of the first band column */
#define EQ_COL_W   54
#define EQ_TRK_Y0  135         /* slider track: +12 dB at the top ... */
#define EQ_TRK_Y1  201         /* ... -12 dB at the bottom           */

enum {                         /* settings rows, grouped in four pages */
    ROW_PRESET, ROW_OLED, ROW_GLOWCOL, ROW_GLOW, ROW_DYN, ROW_KB_FONT, ROW_WALLPAPER, /* Look    */
    ROW_EQMAN, ROW_EQPRE, ROW_B0, ROW_B1, ROW_B2, ROW_B3, ROW_B4,              /* Audio   */
    ROW_DISPLAY, ROW_FADE, ROW_INFO, ROW_RESUME, ROW_SEEK,                     /* Player  */
    ROW_LY_SEARCH, ROW_LY_DL, ROW_LY_OFFLINE, ROW_LY_SYNC, ROW_LY_ANIM, ROW_LY_FONT, ROW_LY_CLEAR, /* Lyrics */
    ROW_CLOCK, ROW_BATT, ROW_BATTSTYLE, ROW_SAVER,                             /* General */
    ROW_SLEEP_ENABLE, ROW_SLEEP_TIMER, ROW_GUIDE, ROW_EXIT,
    SET_ROWS
};

/* "Cards" are lists of settings rows drawn as one inset panel. Cards 0 and 2 are the Look and General
 * tabs (the Sound tab has its own layout); CARD_SETUP is the single combined page of the first-run setup. */
enum { CARD_LOOK, CARD_SOUND, CARD_PLAYER, CARD_GENERAL, CARD_SETUP, CARD_LYRICS, CARD_COUNT };
static const int   ROWS_LOOK[]    = { ROW_OLED, ROW_GLOWCOL, ROW_GLOW, ROW_DYN, ROW_KB_FONT };
static const int   ROWS_PLAYER[]  = { ROW_DISPLAY, ROW_FADE, ROW_INFO, ROW_RESUME, ROW_SEEK };
static const int   ROWS_GENERAL[] = { ROW_CLOCK, ROW_BATT, ROW_BATTSTYLE, ROW_SAVER, ROW_SLEEP_ENABLE, ROW_SLEEP_TIMER, ROW_GUIDE, ROW_EXIT };
static const int   ROWS_SETUP[]   = { ROW_PRESET, ROW_GLOW, ROW_CLOCK, ROW_BATT };
static const int   ROWS_LYRICS[]  = { ROW_LY_SEARCH, ROW_LY_DL, ROW_LY_OFFLINE, ROW_LY_SYNC, ROW_LY_ANIM, ROW_LY_FONT, ROW_LY_CLEAR };
static const int  *CARD_ROWS[CARD_COUNT] = { ROWS_LOOK, NULL, ROWS_PLAYER, ROWS_GENERAL, ROWS_SETUP, ROWS_LYRICS };
static const int   CARD_LEN[CARD_COUNT]  = { 5, 0, 5, 8, 4, 7 };
static const float CARD_ROWH[CARD_COUNT] = { 29.0f, 0.0f, 28.0f, 19.5f, 25.0f, 25.0f };
#define PAGES 5
static const int   PAGE_CARD[PAGES]      = { CARD_LOOK, CARD_SOUND, CARD_PLAYER, CARD_LYRICS, CARD_GENERAL };
static const int   TAB_FIRST[PAGES]      = { ROW_OLED, ROW_EQMAN, ROW_DISPLAY, ROW_LY_SEARCH, ROW_CLOCK };
static const char *PAGE_NAMES[PAGES]     = { "Look", "Audio", "Player", "Lyrics", "General" };
#define TAB_W   60             /* settings tab strip: PAGES segments of TAB_W px starting at x = 10 */

#define TOP_W   400            /* screen sizes in pixels */
#define BOT_W   320
#define SCR_H   240

/* ambient glow: how strongly the cover colour tints the top screen, and how far it reaches */
static const float GLOW_STRENGTH[GLOW_COUNT] = { 0.0f, 0.30f, 0.50f };
#define GLOW_CORE   62.0f      /* half-size of the fully-lit core around the centre cover */
#define GLOW_REACH  190.0f     /* distance from that core at which the glow has faded out */

/* ----------------------------------------------------------------- state */
static Settings  S;
/* effective theme (see theme_update): the saved colours, optionally recoloured by OLED mode and the album art */
static Rgb       th_bg, th_tx, th_acc, th_sel, th_glow;
static DirList   L;                 /* folder shown in the browser          */
static DirList   Q;                 /* playlist: files of the folder played */
static int       qi = -1;           /* current index in Q                   */
static bool      cur_from_queue = false;   /* the loaded track came from Up Next, not from Q */
static int       sel = 0, scroll_top = 0;

/* song search: L (or Q) holds search results instead of a folder's contents */
static bool      L_is_search = false;
static bool      L_is_favorites = false;
#define FAVORITE_LIMIT 512
static char      favorite_paths[FAVORITE_LIMIT][256];
static int       favorite_count = 0;
static bool      favorites_dirty = false;
static bool      sleep_timer_active = false;
static u64       sleep_timer_deadline = 0;
static bool      paused_for_sleep = false;
static aptHookCookie lid_hook_cookie;
static bool      Q_is_search = false;
static bool      Q_is_favorites = false;
#define MANUAL_QUEUE_LIMIT 64
static char      manual_queue[MANUAL_QUEUE_LIMIT][512];
static int       manual_queue_count = 0;
static bool      queue_open = false;
static int       queue_sel = 0;
static int       queue_scroll = 0;
static char      search_q[192];               /* the query being shown                 */
static char      pre_name[256];               /* entry that was selected before searching */
static int       search_busy = 0;             /* >0: "Searching..." screen is up, scan runs when it hits 0 */
static bool      kb_returned = false;         /* the system keyboard was just shown */

static bool      track_loaded = false;
static TrackMeta cur;
static bool      resume_pending = false;      /* a restored song is waiting for Play (not yet decoding) */
static u32       resume_pos_ms = 0;
static char      status_msg[96];

/* lyrics screen */
static bool      lyrics_open = false;
static char      ly_asked[800];               /* song the lyrics screen last asked lyrics for */
static float     ly_pos = 0.0f;               /* synced: eased line position; plain: eased scroll offset */
static float     ly_goal = 0.0f;              /* plain text: wanted scroll offset (D-pad) */

static bool      settings_open = false;
static int       set_row = 0;
static bool      settings_dirty = false;
static int       set_drag = -1;               /* slider row being dragged */

/* cover flow (top screen) */
static bool      flow_show = true;            /* user is browsing -> side covers wanted */
static float     flow_appear = 1.0f;          /* 0..1 animated */
static float     flow_pos = 0.0f;             /* fractional centre index (smooth slide) */
static int       flow_last_sel = -1;
static int       flow_idle = 0;               /* frames since the selection last moved */
static float     flow_vel = 0.0f;             /* spring velocity of flow_pos */
static u64       flow_prev_ms = 0;
static bool      flow_want_dirty = true;      /* load queue needs rebuilding */
static bool      flow_show_prev = true;
static bool      flow_idle_collapse = false; /* after 4s without input, leave only the selected cover */
static u64       flow_last_input_ms = 0;

/* ambient glow behind the cover (top screen): current colour + strength, eased toward the target */
static float     amb_r = 61.0f, amb_g = 156.0f, amb_b = 255.0f, amb_k = 0.0f;
static bool      amb_init = false;

/* now-playing cover fade-in: 0..1, restarts whenever a new cover becomes visible */
static float     art_fade = 1.0f;
static unsigned  art_seen = 0;

/* browsing = side covers wanted (the user is scrolling the library); otherwise the playing view is shown */
static bool browsing_view(void) { return flow_show || !track_loaded; }

/* exit confirmation dialog (START or Settings > Exit app) */
static bool      confirm_exit = false;
static int       confirm_sel = 0;             /* 0 = Cancel, 1 = Exit */
static int       dirty_timer = 0;

/* first-run setup + guide */
enum { WZ_WELCOME, WZ_SETUP, WZ_DONE, WZ_N };                      /* welcome + checks, make it yours, controls */
static bool      wizard_open = false;
static int       wz_page = 0;
static const int WZ_CARD[WZ_N] = { -1, CARD_SETUP, -1 };           /* settings card shown on each step */
static const char *WZ_TITLE[WZ_N] = { "Welcome", "Make it yours", "Controls" };

/* top-screen clock / battery, and the on-screen "+5s" toast */
static bool      mcu_ok = false, ptm_ok = false;
static int       batt_pct = -1, batt_timer = 0;
static bool      batt_chg = false;
static char      toast_msg[32];
static int       toast_t = 0;
static int       ctrl_flash_t = 0;  /* press highlight on a transport button: frames left + its x */
static float     ctrl_flash_x = 0.0f;
#define CTRL_FLASH_FRAMES 10

static void      toast(const char *msg);
static void      kb_begin(void);
static void      apply_lid(void);
static void      show_favorites(void);
static void      favorites_save(void);

static bool      audio_ok = false;
static C2D_TextBuf tbuf;
static float     eq_phase = 0.0f;      /* drives the equalizer bars */
static float     cd_phase = 0.0f;      /* spinning CD angle (advances while playing) */
static float     text_z = 0.5f;        /* depth of draw_text(); raised briefly for overlays such as the toast */
static float     vis_band[AUDIO_VIS_BANDS], vis_peak[AUDIO_VIS_BANDS], vis_wave[AUDIO_VIS_WAVE];
static float     vis_bass = 0.0f;      /* smoothed low-end level 0..1, also pulses the ambient glow */

/* once per frame: pull the analysis of the chunk that is playing right now and smooth it (fast attack, slow release) */
static void vis_update(void)
{
    float b[AUDIO_VIS_BANDS], w[AUDIO_VIS_WAVE];
    const bool live = audio_ok && audio_get_vis(b, w);
    /* Keep the analyser responsive, but deliberately tame the attack/release so normal
     * volume changes do not turn into large visual spikes. This is intentionally cheap
     * scalar smoothing for the Old 3DS; no extra FFT/shader work is introduced here. */
    for (int i = 0; i < AUDIO_VIS_BANDS; i++) {
        const float t = live ? b[i] * 0.62f : 0.0f;
        const float rate = t > vis_band[i] ? 0.22f : 0.075f;
        vis_band[i] += (t - vis_band[i]) * rate;
        vis_peak[i] -= 0.0065f;
        if (vis_peak[i] < vis_band[i]) vis_peak[i] = vis_band[i];
    }
    for (int k = 0; k < AUDIO_VIS_WAVE; k++) {
        const float target = live ? w[k] * 0.58f : 0.0f;
        vis_wave[k] += (target - vis_wave[k]) * 0.18f;
    }
    vis_bass += (((vis_band[0] + vis_band[1] + vis_band[2]) / 3.0f) - vis_bass) * 0.12f;
}
static bool      quit_req = false;
static char      cur_path[800];
static bool      slider_drag = false;
static int       vol_pct = 100;       /* software volume (on top of the 3DS volume slider) */
static bool      vol_drag = false;
static float     slider_frac = 0.0f;

/* ---------------------------------------------------------------- colour */
static u32 C(Rgb c) { return C2D_Color32(c.r, c.g, c.b, 255); }

static Rgb mix_rgb(Rgb a, Rgb b, int w)    /* w = 0..255 weight of b */
{
    Rgb c;
    c.r = (u8)((a.r * (255 - w) + b.r * w) / 255);
    c.g = (u8)((a.g * (255 - w) + b.g * w) / 255);
    c.b = (u8)((a.b * (255 - w) + b.b * w) / 255);
    return c;
}

/* colour with an explicit 0..1 alpha */
static u32 Ca(Rgb c, float a)
{
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    return C2D_Color32(c.r, c.g, c.b, (u8)(a * 255.0f));
}

/* animation state that is eased once per frame (see theme_update / label_update) */
static float     dyn_r = 47.0f, dyn_g = 140.0f, dyn_b = 255.0f, dyn_k = 0.0f;   /* album accent + blend */
static float     label_a = 1.0f, label_glow = 0.0f, info_a = 0.0f;
static bool      label_np = false;            /* label currently shows NOW PLAYING */
static float     bot_fade = 0.0f;             /* bottom-screen transition: 1 = just switched, eases to 0 */
static int       bot_key = -1;
static float     tab_x = 10.0f;
static float     sw_anim[SET_ROWS];
static bool      sw_init[SET_ROWS];
static unsigned  frame_no = 0;

/* Effective theme. th_* are what everything draws with:
 *  - OLED mode: true-black background, dimmer selection panels
 *  - Album colours: the accent (and selection tint) ease toward the colour of the playing cover
 *  - th_glow: halo colour (a Glow colour preset, or the accent) */
static void theme_update(float dt)
{
    const Rgb black = { 0, 0, 0 };
    th_bg = S.oled ? black : S.bg;
    th_tx = S.text;
    Rgb acc = S.accent, sel = S.select;

    u32 rgb = 0;
    float tr = dyn_r, tg = dyn_g, tb = dyn_b, tk = 0.0f;
    if (S.dyn_color && track_loaded && art_color(&rgb)) {
        tr = (float)((rgb >> 16) & 0xFF); tg = (float)((rgb >> 8) & 0xFF); tb = (float)(rgb & 0xFF);
        float mx = fmaxf(tr, fmaxf(tg, tb));
        if (mx < 1.0f) { tr = tg = tb = 200.0f; }
        else if (mx < 232.0f) { float k = 232.0f / mx; tr *= k; tg *= k; tb *= k; }   /* keep the hue, make it readable */
        tk = 1.0f;
        if (dyn_k < 0.01f) { dyn_r = tr; dyn_g = tg; dyn_b = tb; }                     /* first use: no hue wander */
    }
    const float a = S.saver ? 1.0f : 1.0f - expf(-dt * 2.2f);
    dyn_r += (tr - dyn_r) * a; dyn_g += (tg - dyn_g) * a; dyn_b += (tb - dyn_b) * a;
    dyn_k += (tk - dyn_k) * a;
    if (dyn_k > 0.004f) {
        Rgb d = { (u8)dyn_r, (u8)dyn_g, (u8)dyn_b };
        const int w = (int)(dyn_k * 255.0f);
        acc = mix_rgb(acc, d, w);
        sel = mix_rgb(sel, mix_rgb(th_bg, d, 105), w);
    }
    if (S.oled) sel = mix_rgb(sel, black, 110);
    th_acc = acc;
    th_sel = sel;
    if (S.glow_color > 0 && S.glow_color < GLOWCOL_COUNT) {
        const unsigned g = GLOWCOL_RGB[S.glow_color];
        th_glow.r = (g >> 16) & 0xFF; th_glow.g = (g >> 8) & 0xFF; th_glow.b = g & 0xFF;
    } else {
        th_glow = acc;
    }
}

/* Subtle top-lit background: a faint wash of the text colour fading to the base colour. */
static Rgb bg_at(float y)                         /* background colour at screen row y (0..240) */
{
    if (S.oled) return th_bg;                      /* true black, no wash */
    Rgb lit = mix_rgb(th_bg, th_tx, 8);
    return mix_rgb(lit, th_bg, (int)(255.0f * y / (float)SCR_H));
}

static void bg_gradient_z(float w, float z)
{
    u32 t = C(bg_at(0)), b = C(bg_at((float)SCR_H));
    C2D_DrawRectangle(0, 0, z, w, (float)SCR_H, t, t, b, b);
}
static void bg_gradient(float w) { bg_gradient_z(w, 0.05f); }

/* Ambient glow: the top-screen background, tinted with the cover's colour. The tint is strongest right
 * around the centre cover and fades smoothly to the plain background, so it never forms a visible box. */
static float glow_at(float x, float y)                 /* 0..1 */
{
    float dx = fabsf(x - 200.0f) - GLOW_CORE, dy = fabsf(y - 94.0f) - GLOW_CORE;
    if (dx < 0.0f) dx = 0.0f;
    if (dy < 0.0f) dy = 0.0f;
    float t = 1.0f - sqrtf(dx * dx + dy * dy) / GLOW_REACH;
    return t > 0.0f ? t * t : 0.0f;
}

static u32 top_bg_color(float x, float y)              /* also handed to the cover flow (FlowBgFn) */
{
    Rgb base = bg_at(y);
    float k = amb_k * glow_at(x, y);
    if (k < 0.004f) return C(base);
    Rgb amb = { (u8)amb_r, (u8)amb_g, (u8)amb_b };
    return C(mix_rgb(base, amb, (int)(k * 255.0f)));
}

/* The glow is a grid of gouraud-shaded cells. Vertex colours only depend on the glow colour, its strength and
 * the theme, so they are cached and rebuilt only while the colour is actually easing toward a new cover. */
#define GLOW_COLS    (TOP_W / 20)
#define GLOW_BANDS   7
static const float GLOW_YS[GLOW_BANDS + 1] = { 0, 22, 58, 94, 130, 166, 200, SCR_H };

static void top_background(void)
{
    if (amb_k < 0.004f) { bg_gradient_z(TOP_W, 0.02f); return; }        /* glow off: the plain gradient */

    static u32 mesh[(GLOW_BANDS + 1) * (GLOW_COLS + 1)];
    static u8  key[10];
    const u8 now[10] = { (u8)amb_r, (u8)amb_g, (u8)amb_b, (u8)(amb_k * 255.0f),
                         th_bg.r, th_bg.g, th_bg.b, th_tx.r, th_tx.g, th_tx.b };
    if (memcmp(key, now, sizeof key) != 0) {
        memcpy(key, now, sizeof key);
        for (int yi = 0; yi <= GLOW_BANDS; yi++)
            for (int xi = 0; xi <= GLOW_COLS; xi++)
                mesh[yi * (GLOW_COLS + 1) + xi] = top_bg_color((float)xi * 20.0f, GLOW_YS[yi]);
    }
    for (int yi = 0; yi < GLOW_BANDS; yi++) {
        for (int xi = 0; xi < GLOW_COLS; xi++) {
            const u32 *a = &mesh[yi * (GLOW_COLS + 1) + xi], *b = a + (GLOW_COLS + 1);
            C2D_DrawRectangle((float)xi * 20.0f, GLOW_YS[yi], 0.02f, 20.0f, GLOW_YS[yi + 1] - GLOW_YS[yi],
                              a[0], a[1], b[0], b[1]);
        }
    }
}

static void mark_dirty(void) { settings_dirty = true; dirty_timer = 60; }

/* ------------------------------------------------------------------ text */
static void draw_text(const char *s, float x, float y, float sc, u32 color, u32 align, float maxw)
{
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s", s);

    C2D_Text t;
    C2D_TextParse(&t, tbuf, tmp);

    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, sc, sc, &w, &h);
    if (maxw > 0 && w > maxw) {
        size_t len = strlen(tmp);
        size_t cut = (size_t)((float)len * maxw / w);
        cut = (cut > 4) ? cut - 3 : 1;
        while (cut > 0 && (((unsigned char)tmp[cut]) & 0xC0) == 0x80) cut--;
        strcpy(tmp + cut, "...");
        C2D_TextParse(&t, tbuf, tmp);
    }
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor | align, x, y, text_z, sc, sc, color);
}

static void draw_text_glow(const char *s, float x, float y, float sc, u32 align, Rgb col, Rgb gcol,
                           float glow_alpha, float glow_width, float maxw);

/* Draw a single-line marquee without allocating or adding a rendering effect. The
 * visible window advances slowly only when the text is actually wider than its panel. */
static void draw_marquee_text(const char *s, float x, float y, float sc, u32 color, float maxw, bool glow)
{
    if (!s || !s[0]) return;
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s", s);
    C2D_Text t;
    C2D_TextParse(&t, tbuf, tmp);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, sc, sc, &w, &h);
    if (w <= maxw) {
        if (glow) draw_text_glow(tmp, x, y, sc, C2D_AlignLeft, th_acc, th_glow, 0.55f, 1.0f, maxw);
        else draw_text(tmp, x, y, sc, color, C2D_AlignLeft, maxw);
        return;
    }
    const size_t len = strlen(tmp);
    const int chars = (int)(len * maxw / w);
    const int window = chars > 4 ? chars - 2 : 1;
    const int cycle = (int)len + window + 8;
    const int phase = (int)((osGetTime() / 75ULL) % (u64)cycle);
    int start = phase;
    if (start > (int)len - window) start = (int)len - window;
    if (start < 0) start = 0;
    size_t end = (size_t)start + (size_t)window;
    if (end > len) end = len;
    while (end > (size_t)start && (((unsigned char)tmp[end]) & 0xC0) == 0x80) end--;
    char view[300];
    size_t n = end - (size_t)start;
    if (n > sizeof view - 4) n = sizeof view - 4;
    memcpy(view, tmp + start, n);
    view[n] = 0;
    if (start > 0) { memmove(view + 1, view, n + 1); view[0] = ' '; }
    if (end < len && strlen(view) < sizeof view - 4) strcat(view, "...");
    if (glow) draw_text_glow(view, x, y, sc, C2D_AlignLeft, th_acc, th_glow, 0.55f, 1.0f, maxw);
    else draw_text(view, x, y, sc, color, C2D_AlignLeft, maxw);
}

static void draw_text_sh(const char *s, float x, float y, float sc, u32 color, u32 align, float maxw)
{
    Rgb black = { 0, 0, 0 };
    draw_text(s, x + 1, y + 1, sc, C(mix_rgb(bg_at(y), black, 190)), align, maxw);   /* soft shadow */
    draw_text(s, x, y, sc, color, align, maxw);
}

static void fmt_time(char *out, size_t cap, u32 ms)
{
    u32 s = ms / 1000;
    snprintf(out, cap, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

/* playback position for display: a restored-but-not-started song shows where it will resume */
static u32 cur_pos_ms(void)
{
    if (!track_loaded) return 0;
    if (resume_pending && !audio_is_active()) return resume_pos_ms;
    return audio_position_ms();
}

/* file format + average bitrate of the loaded track, worked out once per track */
typedef struct { char fmt[8]; int kbps; char key[800]; u32 key_dur; } TrackInfo;
static TrackInfo ti = { "---", 0, "", 0 };

static void info_refresh(void)
{
    if (!track_loaded || cur_path[0] == 0) return;
    if (ti.key_dur == cur.duration_ms && !strcmp(ti.key, cur_path)) return;
    snprintf(ti.key, sizeof ti.key, "%s", cur_path);
    ti.key_dur = cur.duration_ms;
    ti.fmt[0] = 0;
    const char *dot = strrchr(cur_path, '.');
    if (dot) {
        size_t n = 0;
        for (const char *q = dot + 1; *q && n < sizeof ti.fmt - 1; q++) ti.fmt[n++] = (char)toupper((unsigned char)*q);
        ti.fmt[n] = 0;
    }
    if (!ti.fmt[0]) snprintf(ti.fmt, sizeof ti.fmt, "---");
    ti.kbps = 0;
    struct stat st;
    if (cur.duration_ms > 0 && stat(cur_path, &st) == 0 && (unsigned long long)st.st_size > cur.audio_start) {
        unsigned long long bytes = (unsigned long long)st.st_size - cur.audio_start;
        ti.kbps = (int)((bytes * 8ULL) / cur.duration_ms);           /* bits per millisecond == kbit/s */
    }
}

/* --------------------------------------------------------------- playback */
static void play_q(int idx)
{
    if (idx < 0 || idx >= Q.n) return;
    qi = idx;
    cur_from_queue = false;

    char path[800];
    snprintf(path, sizeof path, "%s%s", Q.dir, Q.e[idx].name);
    snprintf(cur_path, sizeof cur_path, "%s", path);

    TrackMeta m;
    u8 *art = NULL;
    size_t art_size = 0;
    if (!meta_read(path, &m, &art, &art_size, true)) {
        snprintf(status_msg, sizeof status_msg, "Can't open this file");
        return;
    }
    cur = m;
    track_loaded = true;
    resume_pending = false;
    art_queue(art, art_size);          /* takes ownership, NULL clears */

    if (!audio_ok) {
        snprintf(status_msg, sizeof status_msg, "No sound - dspfirm.cdc missing");
        return;
    }
    audio_play(path, m.audio_start);
    apply_lid();
    status_msg[0] = 0;
}

static void play_manual_queue_head(void)
{
    if (manual_queue_count <= 0) return;
    char path[512];
    snprintf(path, sizeof path, "%s", manual_queue[0]);
    memmove(manual_queue, manual_queue + 1, (size_t)(manual_queue_count - 1) * sizeof manual_queue[0]);
    manual_queue_count--;

    TrackMeta m;
    u8 *art = NULL;
    size_t art_size = 0;
    if (!meta_read(path, &m, &art, &art_size, true)) {
        snprintf(status_msg, sizeof status_msg, "Can't open queued file");
        free(art);
        if (manual_queue_count > 0) play_manual_queue_head();
        return;
    }
    cur = m;
    track_loaded = true;
    resume_pending = false;
    cur_from_queue = true;
    snprintf(cur_path, sizeof cur_path, "%s", path);
    art_queue(art, art_size);
    if (audio_ok) { audio_play(path, m.audio_start); apply_lid(); }
}

/* Restart the track that is loaded right now (repeat-one). It may have come from Up Next rather than
 * the playlist, so it is replayed from its own path instead of through Q / qi. */
static void replay_current(void)
{
    if (!track_loaded || !audio_ok || cur_path[0] == 0) return;
    resume_pending = false;
    audio_play(cur_path, cur.audio_start);
    apply_lid();
}

static void queue_add_from_list(int row, bool play_next)
{
    if (row < 0 || row >= L.n || L.e[row].is_dir) return;
    char path[512];
    snprintf(path, sizeof path, "%s%s", L.dir, L.e[row].name);
    if (strlen(path) >= sizeof manual_queue[0]) { toast("File path is too long"); return; }

    /* Re-adding an existing item moves it instead of creating a duplicate. */
    int existing = -1;
    for (int i = 0; i < manual_queue_count; i++)
        if (!strcmp(manual_queue[i], path)) { existing = i; break; }
    if (existing >= 0) {
        if (play_next && existing > 0) {
            char saved[512];
            snprintf(saved, sizeof saved, "%s", manual_queue[existing]);
            memmove(&manual_queue[1], &manual_queue[0], (size_t)existing * sizeof manual_queue[0]);
            snprintf(manual_queue[0], sizeof manual_queue[0], "%s", saved);
        } else {
            toast(play_next ? "Already next" : "Already queued");
            return;
        }
    } else {
        if (manual_queue_count >= MANUAL_QUEUE_LIMIT) { toast("Queue is full"); return; }
        if (play_next) {
            memmove(&manual_queue[1], &manual_queue[0], (size_t)manual_queue_count * sizeof manual_queue[0]);
            snprintf(manual_queue[0], sizeof manual_queue[0], "%s", path);
            manual_queue_count++;
        } else {
            snprintf(manual_queue[manual_queue_count++], sizeof manual_queue[0], "%s", path);
        }
    }
    toast(play_next ? "Added to play next" : "Added to queue");
    if (!audio_is_active()) play_manual_queue_head();
}

static void queue_remove_at(int index)
{
    if (index < 0 || index >= manual_queue_count) return;
    memmove(&manual_queue[index], &manual_queue[index + 1],
            (size_t)(manual_queue_count - index - 1) * sizeof manual_queue[0]);
    manual_queue_count--;
    if (index < queue_sel) queue_sel--;
    else if (index == queue_sel && queue_sel >= manual_queue_count) queue_sel = manual_queue_count - 1;
    if (queue_sel < 0) queue_sel = 0;
    if (queue_scroll > queue_sel) queue_scroll = queue_sel;
    toast("Removed from queue");
}

static void queue_play_at(int index)
{
    if (index < 0 || index >= manual_queue_count) return;
    char path[512];
    snprintf(path, sizeof path, "%s", manual_queue[index]);
    queue_remove_at(index);
    TrackMeta m;
    u8 *art = NULL;
    size_t art_size = 0;
    if (!meta_read(path, &m, &art, &art_size, true)) {
        snprintf(status_msg, sizeof status_msg, "Can't open queued file");
        free(art);
        return;
    }
    cur = m;
    track_loaded = true;
    resume_pending = false;
    cur_from_queue = true;
    snprintf(cur_path, sizeof cur_path, "%s", path);
    art_queue(art, art_size);
    flow_show = false;
    if (audio_ok) { audio_play(path, m.audio_start); apply_lid(); status_msg[0] = 0; }
    else snprintf(status_msg, sizeof status_msg, "No sound - dspfirm.cdc missing");
    queue_open = false;
    toast("Playing from queue");
}

static bool is_current_row(int row)       /* full-path compare: works for folders and search results */
{
    if (cur_path[0] == 0 || row < 0 || row >= L.n || L.e[row].is_dir) return false;
    size_t dl = strlen(L.dir);                                    /* cur_path == dir + name, compared in place */
    return strncmp(cur_path, L.dir, dl) == 0 && strcmp(cur_path + dl, L.e[row].name) == 0;
}

/* file name part of an entry name (search results carry a path relative to the Music folder) */
static const char *base_name(const char *name)
{
    const char *sl = strrchr(name, '/');
    return sl ? sl + 1 : name;
}

/* folder part of "Artist/Album/Song.mp3" -> "Artist/Album" ("" for a file in the Music folder itself) */
static void folder_of(const char *name, char *out, size_t cap)
{
    const char *sl = strrchr(name, '/');
    size_t n = sl ? (size_t)(sl - name) : 0;
    if (n >= cap) n = cap - 1;
    memcpy(out, name, n);
    out[n] = 0;
}

static void play_from_list(int row)
{
    if (row < 0 || row >= L.n || L.e[row].is_dir) return;
    flow_show = false;                 /* song chosen: side covers fold away */
    /* the playlist is whatever list the song was picked from: a folder's files or the search results */
    if (Q.n == 0 || L_is_search || Q_is_search || L_is_favorites || L_is_favorites != Q_is_favorites || strcmp(Q.dir, L.dir) != 0) {
        dir_copy_files(&Q, &L);
        Q_is_search = L_is_search;
        Q_is_favorites = L_is_favorites;
    }
    int idx = -1;
    for (int i = 0; i < Q.n; i++)
        if (!strcmp(Q.e[i].name, L.e[row].name)) { idx = i; break; }
    if (idx >= 0) play_q(idx);
}

static void next_track(bool manual)
{
    if (manual_queue_count > 0) { play_manual_queue_head(); return; }
    if (Q.n == 0) return;
    int n;
    if (S.shuffle && Q.n > 1) {
        do { n = rand() % Q.n; } while (n == qi);
    } else {
        n = qi + 1;
        if (n >= Q.n) {
            if (S.repeat == 1 || manual) n = 0;
            else { audio_stop(); apply_lid(); return; }
        }
    }
    play_q(n);
}

static void prev_track(void)
{
    if (!track_loaded) return;
    /* past the first seconds: restart what is playing (even if it came from Up Next) */
    if (audio_is_active() && audio_position_ms() > 3000) { replay_current(); return; }
    /* a queued song was interrupting the playlist: "previous" goes back to the song it interrupted */
    if (cur_from_queue && qi >= 0 && qi < Q.n) { play_q(qi); return; }
    if (Q.n == 0) return;
    int n;
    if (S.shuffle && Q.n > 1) {
        do { n = rand() % Q.n; } while (n == qi);
    } else {
        n = qi - 1;
        if (n < 0) n = Q.n - 1;
    }
    play_q(n);
}

/* Skipping by hand (L / R, on-screen buttons) shows what is playing now instead of the browse view. */
static void follow_playing(void);

static void flash_ctrl(float x) { ctrl_flash_x = x; ctrl_flash_t = CTRL_FLASH_FRAMES; }
/* size factor for the transport button at x: a short "pop" right after it is pressed */
static float pop_k(float x)
{
    if (S.saver || ctrl_flash_t <= 0 || fabsf(ctrl_flash_x - x) > 14.0f) return 1.0f;
    const float p = 1.0f - (float)ctrl_flash_t / (float)CTRL_FLASH_FRAMES;
    return 1.0f + 0.22f * sinf(3.14159f * p);
}
static void skip_next(void) { if (Q.n == 0 && manual_queue_count == 0) return; flash_ctrl(208); flow_show = false; next_track(true); follow_playing(); }
static void skip_prev(void) { if (Q.n == 0 && !track_loaded) return; flash_ctrl(156); flow_show = false; prev_track(); follow_playing(); }

static void toggle_play(void)
{
    if (audio_is_active()) {
        if (audio_is_paused()) audio_resume(); else audio_pause();
        apply_lid();
    } else if (resume_pending && track_loaded && audio_ok && cur_path[0]) {
        const float f = cur.duration_ms ? (float)resume_pos_ms / (float)cur.duration_ms : 0.0f;
        audio_seek(cur_path, cur.audio_start, f > 0.995f ? 0.0f : f, f > 0.995f ? 0 : resume_pos_ms);
        apply_lid();
    } else if (track_loaded && qi >= 0) {
        play_q(qi);
    } else if (sel < L.n && !L.e[sel].is_dir) {
        play_from_list(sel);
    }
}

/* ---------------------------------------------------------------- browser */
static void clamp_scroll(void)
{
    int max_top = L.n - VIS_ROWS;
    if (max_top < 0) max_top = 0;
    if (scroll_top > max_top) scroll_top = max_top;
    if (scroll_top < 0) scroll_top = 0;
}

/* Highlight the playing song in the list it was started from, so the list and the top screen agree after a skip.
 * Does nothing while the cover flow is open (you are browsing) or when the song came from another list. */
static void follow_playing(void)
{
    if (flow_show || qi < 0 || qi >= Q.n) return;
    if (L_is_search != Q_is_search || strcmp(Q.dir, L.dir) != 0) return;
    for (int i = 0; i < L.n; i++) {
        if (L.e[i].is_dir || strcmp(L.e[i].name, Q.e[qi].name) != 0) continue;
        if (i != sel) {
            sel = i;
            scroll_top = i - VIS_ROWS / 2;
            clamp_scroll();
        }
        return;
    }
}

static void set_dir(const char *path, const char *select_name)
{
    L_is_search = false; L_is_favorites = false;
    if (!dir_load(&L, path) && !is_music_root(path)) dir_load(&L, MUSIC_ROOT);
    sel = 0;
    scroll_top = 0;
    if (select_name) {
        for (int i = 0; i < L.n; i++)
            if (!strcmp(L.e[i].name, select_name)) { sel = i; break; }
    }
    scroll_top = sel - VIS_ROWS / 2;
    clamp_scroll();
    flow_reset();
    flow_want_dirty = true;
    flow_pos = (float)sel;
    flow_vel = 0.0f;
    flow_show = true;
    snprintf(S.last_dir, sizeof S.last_dir, "%s", L.dir);
    mark_dirty();
}

static void go_up(void)
{
    char p[512], child[256];
    snprintf(p, sizeof p, "%s", L.dir);
    size_t n = strlen(p);
    if (n > 0 && p[n - 1] == '/') p[--n] = 0;
    char *sl = strrchr(p, '/');
    if (!sl) return;
    snprintf(child, sizeof child, "%s", sl + 1);
    sl[1] = 0;
    set_dir(p, child);
}

static void exit_search(void)
{
    char back[256];
    snprintf(back, sizeof back, "%s", pre_name);
    set_dir(S.last_dir, back[0] ? back : NULL);      /* back to the folder we searched from */
}

/* Opens the on-screen keyboard (see the keyboard section below); a confirmed query starts the search (run_search). */
static void open_search(void) { kb_begin(); }

static void run_search(void)
{
    search_build_index();                             /* walks SD:/Music/ the first time only */
    if (!L_is_search)
        snprintf(pre_name, sizeof pre_name, "%s", (L.n > 0 && sel < L.n) ? L.e[sel].name : "");
    search_run(&L, search_q);
    L_is_search = true; L_is_favorites = false;
    sel = (L.n > 1) ? 1 : 0;                          /* first match (row 0 is "Back") */
    scroll_top = 0;
    clamp_scroll();
    flow_reset();
    flow_want_dirty = true;
    flow_pos = (float)sel;
    flow_vel = 0.0f;
    flow_show = true;
}

static void activate(int row)
{
    if (row < 0 || row >= L.n) return;
    sel = row;
    if (L.e[row].is_dir) {
        if (!strcmp(L.e[row].name, "..")) {
            if (L_is_search) exit_search(); else go_up();
        } else {
            char p[800];
            snprintf(p, sizeof p, "%s%s/", L.dir, L.e[row].name);
            set_dir(p, NULL);
        }
    } else {
        play_from_list(row);
    }
}

static void move_sel(int delta)
{
    if (L.n == 0) return;
    int old_sel = sel;
    sel += delta;
    if (sel < 0) sel = 0;
    if (sel >= L.n) sel = L.n - 1;
    if (sel < scroll_top) scroll_top = sel;
    if (sel >= scroll_top + VIS_ROWS) scroll_top = sel - VIS_ROWS + 1;
    clamp_scroll();
    if (sel != old_sel) flow_show = true;      /* browsing again: covers fan back out */
}

static void ensure_meta(Entry *e)
{
    char path[800];
    snprintf(path, sizeof path, "%s%s", L.dir, e->name);
    TrackMeta m;
    if (meta_read(path, &m, NULL, NULL, true)) {
        snprintf(e->title, sizeof e->title, "%s", m.title);
        snprintf(e->artist, sizeof e->artist, "%s", m.artist);
        e->duration_ms = m.duration_ms;
    }
    e->meta_loaded = true;
}


/* --------------------------------------------------------------- settings */
static bool is_slider_row(int r) { return r >= ROW_B0 && r <= ROW_B4; }
static int  row_page(int r)      { return r < ROW_EQMAN ? 0 : (r < ROW_DISPLAY ? 1 : (r < ROW_LY_SEARCH ? 2 : (r < ROW_CLOCK ? 3 : 4))); }

static bool is_switch_row(int r)
{
    return r == ROW_BATT || r == ROW_SLEEP_ENABLE || r == ROW_EQMAN || r == ROW_OLED || r == ROW_DYN ||
           r == ROW_INFO || r == ROW_RESUME || r == ROW_SAVER ||
           r == ROW_LY_SEARCH || r == ROW_LY_DL || r == ROW_LY_OFFLINE || r == ROW_LY_SYNC || r == ROW_LY_ANIM;
}
static bool is_band_row(int r)   { return r >= ROW_B0 && r <= ROW_B4; }
static bool is_choice_row(int r) { return r == ROW_PRESET || r == ROW_EQPRE || r == ROW_CLOCK || r == ROW_GLOW || r == ROW_FADE || r == ROW_DISPLAY || r == ROW_WALLPAPER || r == ROW_SEEK || r == ROW_SLEEP_TIMER || r == ROW_GLOWCOL || r == ROW_BATTSTYLE || r == ROW_LY_FONT || r == ROW_KB_FONT; }

static int card_pos(int card, int row)            /* index of `row` inside a card, or -1 */
{
    for (int i = 0; i < CARD_LEN[card]; i++)
        if (CARD_ROWS[card][i] == row) return i;
    return -1;
}

static void eq_push(void) { audio_set_eq(S.eq); }
static void apply_lid(void) { aptSetSleepAllowed(true); }

static const int SLEEP_CHOICES[6] = { 5, 10, 15, 30, 45, 60 };
static int sleep_choice_index(void) { for (int i=0;i<6;i++) if (S.sleep_minutes == SLEEP_CHOICES[i]) return i; return 2; }
static void sleep_timer_start_or_reset(void)
{
    bool was_active = sleep_timer_active;
    sleep_timer_deadline = osGetTime() + (u64)S.sleep_minutes * 60000ULL;
    sleep_timer_active = true;
    toast(was_active ? "Sleep timer reset" : "Sleep timer started");
}
static void sleep_timer_cancel(void)
{
    if (sleep_timer_active) { sleep_timer_active = false; toast("Sleep timer cancelled"); }
}
static void apt_lid_hook(APT_HookType hook, void *param)
{
    (void)param;
    if (hook == APTHOOK_ONSLEEP) {
        paused_for_sleep = audio_is_active() && !audio_is_paused();
        if (paused_for_sleep) audio_pause();
    } else if (hook == APTHOOK_ONWAKEUP) {
        if (paused_for_sleep) { audio_resume(); paused_for_sleep = false; }
    }
}
static void favorites_load(void)
{
    FILE *f = fopen(FAVORITES_PATH, "r"); char line[300];
    if (!f) {
        char backup_path[sizeof(FAVORITES_PATH) + 8];
        snprintf(backup_path, sizeof(backup_path), "%s.bak", FAVORITES_PATH);
        f = fopen(backup_path, "r");
    }
    if (!f) return;
    while (fgets(line, sizeof line, f) && favorite_count < FAVORITE_LIMIT) {
        size_t n = strlen(line); while (n && (line[n-1]=='\n' || line[n-1]=='\r')) line[--n]=0;
        if (!n || line[0]=='/' || strstr(line, "../") || strstr(line, "/..") || strstr(line, "\\") || !is_audio_file(line)) continue;
        snprintf(favorite_paths[favorite_count++], sizeof favorite_paths[0], "%s", line);
    }
    fclose(f);
}
static void favorites_save(void)
{
    mkdir(DATA_ROOT, 0777); mkdir(DATA_DIR, 0777);

    /* Keep the last valid favorites file if an interrupted write occurs. */
    char temp_path[sizeof(FAVORITES_PATH) + 8];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", FAVORITES_PATH);
    FILE *f = fopen(temp_path, "w");
    if (!f) return;

    for (int i = 0; i < favorite_count; i++)
        fprintf(f, "%s\n", favorite_paths[i]);

    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(temp_path);
        return;
    }
    char backup_path[sizeof(FAVORITES_PATH) + 8];
    snprintf(backup_path, sizeof(backup_path), "%s.bak", FAVORITES_PATH);
    bool had_old = false;
    FILE *old = fopen(FAVORITES_PATH, "r");
    if (old) {
        fclose(old);
        remove(backup_path);
        if (rename(FAVORITES_PATH, backup_path) != 0) {
            remove(temp_path);
            return;
        }
        had_old = true;
    }
    if (rename(temp_path, FAVORITES_PATH) != 0) {
        if (had_old) rename(backup_path, FAVORITES_PATH);
        remove(temp_path);
        return;
    }
    if (had_old) remove(backup_path);
    favorites_dirty = false;
}
static int favorite_index(const char *path)
{
    for (int i=0;i<favorite_count;i++) if (!strcmp(favorite_paths[i], path)) return i;
    return -1;
}
static bool is_favorite(const char *path) { return favorite_index(path) >= 0; }
static void entry_relative_path(const Entry *e, char *out, size_t cap)
{
    const char *sub = L.dir;
    size_t root_len = strlen(MUSIC_ROOT);
    if (strncmp(sub, MUSIC_ROOT, root_len) == 0) sub += root_len;
    if (*sub) snprintf(out, cap, "%s%s%s", sub, (sub[strlen(sub)-1] == '/' ? "" : "/"), e->name);
    else snprintf(out, cap, "%s", e->name);
}
static void favorite_toggle(const char *path)
{
    int i = favorite_index(path);
    if (i >= 0) { memmove(&favorite_paths[i], &favorite_paths[i+1], (size_t)(favorite_count-i-1)*sizeof favorite_paths[0]); favorite_count--; }
    else if (favorite_count < FAVORITE_LIMIT) snprintf(favorite_paths[favorite_count++], sizeof favorite_paths[0], "%s", path);
    favorites_dirty = true; favorites_save();
    toast(i >= 0 ? "Removed from Favorites" : "Added to Favorites");
    if (L_is_favorites) {                       /* rebuild the list but keep your place in it */
        int keep_sel = sel, keep_top = scroll_top;
        show_favorites();
        sel = (keep_sel < L.n) ? keep_sel : (L.n > 0 ? L.n - 1 : 0);
        scroll_top = keep_top;
        clamp_scroll();
        flow_pos = (float)sel;
    }
}
static void show_favorites(void)
{
    dir_free(&L); snprintf(L.dir, sizeof L.dir, "%s", MUSIC_ROOT);
    for (int i=0;i<favorite_count;i++) dir_add(&L, favorite_paths[i], false);
    L_is_search = false; L_is_favorites = true; sel = 0; scroll_top = 0; flow_show = true;
    flow_reset(); flow_want_dirty = true; flow_pos = 0.0f; flow_vel = 0.0f;
}

static void set_adjust(int dir)
{
    if (set_row == ROW_PRESET) {
        int p = S.preset + dir;
        if (S.preset < 0) p = (dir > 0) ? 0 : PRESET_COUNT - 1;
        if (p < 0) p = PRESET_COUNT - 1;
        if (p >= PRESET_COUNT) p = 0;
        settings_apply_preset(&S, p);
    } else if (set_row == ROW_EQMAN) {
        S.eq_manual = !S.eq_manual;
        if (S.eq_manual) {                    /* first time on: start from the curve you were hearing */
            bool flat = true;
            for (int i = 0; i < EQ_BANDS; i++) if (S.eq_user[i]) flat = false;
            if (flat) for (int i = 0; i < EQ_BANDS; i++) S.eq_user[i] = S.eq[i];
        }
        settings_eq_refresh(&S);
        eq_push();
    } else if (set_row == ROW_EQPRE) {
        if (S.eq_manual) return;              /* locked while the manual EQ is on */
        int p = S.eq_preset + dir;
        if (p < 0) p = EQ_PRESET_COUNT - 1;
        if (p >= EQ_PRESET_COUNT) p = 0;
        settings_apply_eq_preset(&S, p);
        eq_push();
    } else if (is_band_row(set_row)) {
        if (!S.eq_manual) return;             /* bands only move with the manual EQ on */
        int b = set_row - ROW_B0;
        int v = S.eq_user[b] + dir;
        if (v < -12) v = -12;
        if (v > 12) v = 12;
        S.eq_user[b] = v;
        settings_eq_refresh(&S);
        eq_push();
    } else if (set_row == ROW_CLOCK) {
        S.clock_mode = (S.clock_mode + dir + 3) % 3;
    } else if (set_row == ROW_GLOW) {
        S.glow = (S.glow + dir + GLOW_COUNT) % GLOW_COUNT;
    } else if (set_row == ROW_FADE) {
        S.fade_speed = (S.fade_speed + dir + FADE_COUNT) % FADE_COUNT;
    } else if (set_row == ROW_DISPLAY) {
        S.display_mode = (S.display_mode + dir + 4) % 4;
    } else if (set_row == ROW_WALLPAPER) {
        S.wallpaper = (S.wallpaper + dir + 5) % 5;
    } else if (set_row == ROW_SLEEP_ENABLE) {
        if (sleep_timer_active) sleep_timer_cancel();
        else sleep_timer_start_or_reset();
    } else if (set_row == ROW_SLEEP_TIMER) {
        int i = sleep_choice_index(); S.sleep_minutes = SLEEP_CHOICES[(i + dir + 6) % 6];
        if (sleep_timer_active) sleep_timer_start_or_reset();
    } else if (set_row == ROW_SEEK) {
        int i = 0;
        for (int k = 0; k < SEEK_STEP_COUNT; k++) if (SEEK_STEPS[k] == S.seek_step) i = k;
        S.seek_step = SEEK_STEPS[(i + dir + SEEK_STEP_COUNT) % SEEK_STEP_COUNT];
    } else if (set_row == ROW_BATT) {
        S.show_batt = !S.show_batt;
        batt_timer = 0;                       /* read the battery right away */
    } else if (set_row == ROW_BATTSTYLE) {
        S.batt_style = (S.batt_style + 1) % 2;
    } else if (set_row == ROW_OLED) {
        S.oled = !S.oled;
    } else if (set_row == ROW_GLOWCOL) {
        S.glow_color = (S.glow_color + dir + GLOWCOL_COUNT) % GLOWCOL_COUNT;
    } else if (set_row == ROW_DYN) {
        S.dyn_color = !S.dyn_color;
    } else if (set_row == ROW_INFO) {
        S.show_info = !S.show_info;
    } else if (set_row == ROW_RESUME) {
        S.resume = !S.resume;
        if (!S.resume) remove(RESUME_PATH);
    } else if (set_row == ROW_SAVER) {
        S.saver = !S.saver;
        toast(S.saver ? "Battery saver on" : "Battery saver off");
    } else if (set_row == ROW_LY_SEARCH) {
        S.ly_search = !S.ly_search;
    } else if (set_row == ROW_LY_DL) {
        S.ly_download = !S.ly_download;
    } else if (set_row == ROW_LY_OFFLINE) {
        S.ly_offline = !S.ly_offline;
    } else if (set_row == ROW_LY_SYNC) {
        S.ly_sync = !S.ly_sync;
    } else if (set_row == ROW_LY_FONT) {
        S.ly_font = (S.ly_font + dir + 4) % 4;
    } else if (set_row == ROW_KB_FONT) {
        S.kb_font = (S.kb_font + dir + 4) % 4;
    } else {
        return;
    }
    mark_dirty();
}

/* touch / drag directly on an EQ band slider */
static void slider_apply(int r, int px, int py)
{
    (void)px;
    if (!S.eq_manual) return;
    float f = (float)(EQ_TRK_Y1 - py) / (float)(EQ_TRK_Y1 - EQ_TRK_Y0);     /* 0 = bottom, 1 = top */
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    int v = (int)(f * 24.0f + 0.5f) - 12;
    int b = r - ROW_B0;
    if (v == S.eq_user[b]) return;
    S.eq_user[b] = v;
    settings_eq_refresh(&S);
    eq_push();
    mark_dirty();
}

/* Up / Down through the settings rows; the EQ sliders are skipped while they are locked */
static int row_step(int r, int dir)
{
    int n = r + dir;
    if (n < 0 || n >= SET_ROWS) return r;
    if (is_band_row(n) && !S.eq_manual) n = (dir > 0) ? ROW_DISPLAY : ROW_EQPRE;
    return n;
}

static void close_settings(void)
{
    settings_open = false;
    set_drag = -1;
    if (settings_dirty && settings_save(&S)) settings_dirty = false;
}

/* ------------------------------------------------------------ key repeat */
static bool repeat_fire(u32 held, u32 key, int *cnt)
{
    if (held & key) {
        (*cnt)++;
        if (*cnt == 1) return true;
        return (*cnt > 18) && (((*cnt - 18) % 4) == 0);
    }
    *cnt = 0;
    return false;
}

/* hold-to-repeat for seeking: first press at once, then ~6 skips a second after a short hold */
static bool repeat_seek(u32 held, u32 key, int *cnt)
{
    if (held & key) {
        (*cnt)++;
        if (*cnt == 1) return true;
        return (*cnt > 24) && (((*cnt - 24) % 10) == 0);
    }
    *cnt = 0;
    return false;
}

/* ------------------------------------------------------------- cover flow */
static void flow_refresh_wants(void)   /* queue the missing covers around the focus, nearest first */
{
    static const int order[9] = { 0, 1, -1, 2, -2, 3, -3, 4, -4 };    /* 3 visible per side + 1 prefetched */
    FlowWant w[FLOW_MAX_WANT];
    int n = 0;
    for (int k = 0; k < 9 && n < FLOW_MAX_WANT; k++) {
        int idx = sel + order[k];
        if (idx < 0 || idx >= L.n) continue;
        Entry *e = &L.e[idx];
        if (e->is_dir || flow_known(idx)) continue;
        if (is_current_row(idx) && art_ready()) continue;    /* the playing cover is already loaded */
        w[n].idx = idx;
        snprintf(w[n].path, sizeof w[n].path, "%s%s", L.dir, e->name);
        n++;
    }
    flow_want(w, n);
}

/* The glow follows the cover in the middle of the top screen: the focused entry while browsing, the
 * playing track once a song has been picked. Folders / covers without art use the theme accent. */
static void ambient_update(float dt)
{
    u32 rgb = 0;
    bool have = false;
    bool browsing = browsing_view();
    if (!browsing) {
        have = art_color(&rgb);                          /* playing view: always the playing track's cover */
    } else if (L.n > 0) {
        int fi = (int)floorf(flow_pos + 0.5f);
        if (fi < 0) fi = 0;
        if (fi >= L.n) fi = L.n - 1;
        if (!L.e[fi].is_dir) {
            if (is_current_row(fi) && art_ready()) have = art_color(&rgb);
            else have = flow_color(fi, &rgb);
        }
    }
    if (!have) rgb = ((u32)th_acc.r << 16) | ((u32)th_acc.g << 8) | th_acc.b;
    if (S.glow_color > 0) rgb = GLOWCOL_RGB[S.glow_color];         /* a chosen glow colour overrides the cover colour */

    float tr = (float)((rgb >> 16) & 0xFF), tg = (float)((rgb >> 8) & 0xFF), tb = (float)(rgb & 0xFF);
    float tk = GLOW_STRENGTH[S.glow];
    if (S.oled)  tk *= 0.60f;                                       /* keep most of the screen truly black */
    if (S.saver) tk *= 0.55f; else tk *= 1.0f + 0.20f * vis_bass;   /* breathes gently with the low end */
    if (!amb_init) { amb_r = tr; amb_g = tg; amb_b = tb; amb_k = tk; amb_init = true; return; }

    float a = 1.0f - expf(-dt * (browsing ? 3.6f : 2.4f));   /* slow, soft settle (~1 s) on a new cover */
    amb_r += (tr - amb_r) * a;
    amb_g += (tg - amb_g) * a;
    amb_b += (tb - amb_b) * a;
    amb_k += (tk - amb_k) * a;
}

static void flow_update(void)          /* once per frame: animation + selection tracking */
{
    u64 now = osGetTime();
    float dt = flow_prev_ms ? (float)(now - flow_prev_ms) / 1000.0f : (1.0f / 60.0f);
    flow_prev_ms = now;
    if (dt < 0.001f) dt = 0.001f;
    if (dt > 0.05f) dt = 0.05f;

    theme_update(dt);
    {   /* NOW PLAYING / BROWSING label cross-fade, label glow and info-card fade */
        const bool want_np = track_loaded && !browsing_view();
        const bool playing_now = track_loaded && audio_ok && audio_is_active() && !audio_is_paused();
        const float sp = S.saver ? 1.0f : 1.0f - expf(-dt * 7.0f);
        if (want_np != label_np) {
            label_a -= S.saver ? 1.0f : dt * 7.0f;
            if (label_a <= 0.0f) { label_a = 0.0f; label_np = want_np; }
        } else if (label_a < 1.0f) {
            label_a += S.saver ? 1.0f : dt * 7.0f;
            if (label_a > 1.0f) label_a = 1.0f;
        }
        label_glow += (((playing_now && label_np) ? 1.0f : 0.0f) - label_glow) * (S.saver ? 1.0f : 1.0f - expf(-dt * 4.0f));
        info_a += (((want_np && S.show_info) ? 1.0f : 0.0f) - info_a) * sp;
    }

    float target = ((flow_show || !track_loaded) && !flow_idle_collapse) ? 1.0f : 0.0f;
    float step = 7.5f * dt;                              /* ~130 ms to fan in / out */
    float dl = target - flow_appear;
    if (dl > step) dl = step;
    if (dl < -step) dl = -step;
    flow_appear += dl;

    /* critically damped spring: velocity carries over when the target moves, so held scrolling
     * glides instead of stepping, and a jump of several covers still ends softly */
    const float w0 = 18.0f;
    if (!browsing_view()) { flow_pos = (float)sel; flow_vel = 0.0f; }   /* playing view: no sliding past other covers (shuffle jumps) */
    float x = flow_pos - (float)sel;
    flow_vel += (-w0 * w0 * x - 2.0f * w0 * flow_vel) * dt;
    flow_pos += flow_vel * dt;
    if (fabsf(flow_pos - (float)sel) < 0.002f && fabsf(flow_vel) < 0.02f) { flow_pos = (float)sel; flow_vel = 0.0f; }

    if (sel != flow_last_sel) { flow_last_sel = sel; flow_idle = 0; flow_idle_collapse = false; flow_want_dirty = true; }
    else if (flow_idle < 1000) flow_idle++;
    if (flow_show != flow_show_prev) { flow_show_prev = flow_show; flow_want_dirty = true; }

    if (flow_want_dirty && flow_show) { flow_refresh_wants(); flow_want_dirty = false; }

    /* title/artist/length: reading a tag (and probing the first MP3 frame) is SD I/O plus a decode, so it never
     * happens while the list is being scrolled. Once the selection has been still for ~0.2 s, one entry per frame
     * is read: the selected one first, then its neighbours (so the next scroll finds the tags already loaded). */
    if (flow_idle > 12) {
        static const int tag_order[9] = { 0, 1, -1, 2, -2, 3, -3, 4, -4 };
        for (int k = 0; k < 9; k++) {
            const int i = sel + tag_order[k];
            if (i < 0 || i >= L.n || L.e[i].is_dir || L.e[i].meta_loaded) continue;
            ensure_meta(&L.e[i]);
            break;
        }
    }

    unsigned as = art_serial();
    if (as != art_seen) { art_seen = as; art_fade = 0.0f; }
    else if (art_fade < 1.0f) { art_fade += dt / FADE_SECS[S.fade_speed]; if (art_fade > 1.0f) art_fade = 1.0f; }

    ambient_update(dt);
}

/* ---------------------------------------------------------------- drawing */
/* ---- small icon helpers (s = half-height of the icon) ---- */
static void icon_prev(float cx, float cy, float s, u32 c)
{
    C2D_DrawRectSolid(cx - s, cy - s, 0.45f, s * 0.28f, 2 * s, c);
    C2D_DrawTriangle(cx + s, cy - s, c, cx + s, cy + s, c, cx - s * 0.6f, cy, c, 0.45f);
}

static void icon_next(float cx, float cy, float s, u32 c)
{
    C2D_DrawTriangle(cx - s, cy - s, c, cx - s, cy + s, c, cx + s * 0.6f, cy, c, 0.45f);
    C2D_DrawRectSolid(cx + s * 0.72f, cy - s, 0.45f, s * 0.28f, 2 * s, c);
}

static void icon_play(float cx, float cy, float s, u32 c)
{
    C2D_DrawTriangle(cx - s * 0.6f, cy - s, c, cx - s * 0.6f, cy + s, c, cx + s, cy, c, 0.45f);
}

static void icon_pause(float cx, float cy, float s, u32 c)
{
    C2D_DrawRectSolid(cx - s * 0.7f, cy - s, 0.45f, s * 0.5f, 2 * s, c);
    C2D_DrawRectSolid(cx + s * 0.2f, cy - s, 0.45f, s * 0.5f, 2 * s, c);
}

static void icon_search(float cx, float cy, u32 c, u32 hole)   /* magnifying glass, ~13 px wide */
{
    cx -= 1.5f; cy -= 1.0f;
    C2D_DrawCircleSolid(cx, cy, 0.45f, 4.8f, c);
    C2D_DrawCircleSolid(cx, cy, 0.46f, 2.9f, hole);
    C2D_DrawLine(cx + 3.6f, cy + 3.6f, c, cx + 7.0f, cy + 7.0f, c, 2.0f, 0.45f);
}

static void icon_shuffle(float cx, float cy, float s, u32 c)   /* two crossing arrows */
{
    const float x0 = cx - s, x1 = cx + s * 0.55f, y0 = cy - s * 0.6f, y1 = cy + s * 0.6f, h = s * 0.45f;
    C2D_DrawLine(x0, y0, c, x1, y1, c, 2.0f, 0.45f);
    C2D_DrawLine(x0, y1, c, x1, y0, c, 2.0f, 0.45f);
    C2D_DrawTriangle(x1 - s * 0.1f, y1 - h, c, x1 - s * 0.1f, y1 + h, c, cx + s, y1, c, 0.45f);
    C2D_DrawTriangle(x1 - s * 0.1f, y0 - h, c, x1 - s * 0.1f, y0 + h, c, cx + s, y0, c, 0.45f);
}

static void icon_repeat(float cx, float cy, float s, u32 c)    /* loop with two arrowheads */
{
    const float t = 2.0f, x0 = cx - s, x1 = cx + s, y0 = cy - s * 0.65f, y1 = cy + s * 0.65f, h = s * 0.45f;
    C2D_DrawRectSolid(x0, y0 - t / 2, 0.45f, (x1 - x0) - s * 0.6f, t, c);                  /* top bar -> */
    C2D_DrawTriangle(x1 - s * 0.6f, y0 - h, c, x1 - s * 0.6f, y0 + h, c, x1, y0, c, 0.45f);
    C2D_DrawRectSolid(x0, y0 - t / 2, 0.45f, t, (y1 - y0) - h + t / 2, c);                 /* left side */
    C2D_DrawRectSolid(x0 + s * 0.6f, y1 - t / 2, 0.45f, (x1 - x0) - s * 0.6f, t, c);       /* bottom bar <- */
    C2D_DrawTriangle(x0 + s * 0.6f, y1 - h, c, x0 + s * 0.6f, y1 + h, c, x0, y1, c, 0.45f);
    C2D_DrawRectSolid(x1 - t, y0 + h - t / 2, 0.45f, t, (y1 - y0) - h + t, c);             /* right side */
}

static void icon_note(float x, float y, u32 c)   /* ~14x16 music note, top-left at x,y */
{
    C2D_DrawCircleSolid(x + 4, y + 13, 0.45f, 3.5f, c);
    C2D_DrawRectSolid(x + 6.5f, y, 0.45f, 2, 13, c);
    C2D_DrawRectSolid(x + 6.5f, y, 0.45f, 6, 2, c);
}

/* rounded rectangle: two rects + four corner circles */
static void rrect(float x, float y, float w, float h, float r, float z, u32 c)
{
    C2D_DrawRectSolid(x + r, y, z, w - 2 * r, h, c);
    C2D_DrawRectSolid(x, y + r, z, w, h - 2 * r, c);
    C2D_DrawCircleSolid(x + r, y + r, z, r, c);
    C2D_DrawCircleSolid(x + w - r, y + r, z, r, c);
    C2D_DrawCircleSolid(x + r, y + h - r, z, r, c);
    C2D_DrawCircleSolid(x + w - r, y + h - r, z, r, c);
}

/* ---- neon building blocks shared by the top and bottom screens ----
 * Everything is "dark panel + bright rim + faint halo", like the reference mock-up. Colours come from the
 * theme (accent / bg / text), so the other presets simply recolour the same layout. */
static u32 neon_glow(void)                                                   /* halo behind a lit rim */
{
    return C(mix_rgb(th_bg, th_glow, S.saver ? 50 : (S.oled ? 90 : 70)));
}

static void neon_box(float x, float y, float w, float h, float r, float z, u32 rim, u32 glow, u32 fill)
{
    rrect(x - 1.5f, y - 1.5f, w + 3, h + 3, r + 1.5f, z, glow);
    rrect(x, y, w, h, r, z + 0.002f, rim);
    rrect(x + 1.6f, y + 1.6f, w - 3.2f, h - 3.2f, r > 2.0f ? r - 1.6f : 0.5f, z + 0.004f, fill);
}

static void neon_disc(float cx, float cy, float r, float z, u32 rim, u32 glow, u32 fill)
{
    C2D_DrawCircleSolid(cx, cy, z, r + 1.5f, glow);
    C2D_DrawCircleSolid(cx, cy, z + 0.002f, r, rim);
    C2D_DrawCircleSolid(cx, cy, z + 0.004f, r - 1.6f, fill);
}

/* outline around a whole screen. It also paints the inside, so draw wallpaper accents after it. */
static void neon_frame(float w, float h, float z)
{
    rrect(0, 0, w, h, 17, z, neon_glow());
    rrect(1, 1, w - 2, h - 2, 16, z + 0.002f, C(th_acc));
    rrect(3, 3, w - 6, h - 6, 14, z + 0.004f, C(th_bg));
}

/* Outline only (no fill) around a whole screen, so whatever is drawn behind it - the ambient glow - shows through. */
static void ring_path(float w, float h, float mg, float R, float th, u32 c, float z)
{
    const float x0 = mg, y0 = mg, x1 = w - mg, y1 = h - mg;
    C2D_DrawLine(x0 + R, y0, c, x1 - R, y0, c, th, z);
    C2D_DrawLine(x0 + R, y1, c, x1 - R, y1, c, th, z);
    C2D_DrawLine(x0, y0 + R, c, x0, y1 - R, c, th, z);
    C2D_DrawLine(x1, y0 + R, c, x1, y1 - R, c, th, z);
    const float ccx[4] = { x0 + R, x1 - R, x1 - R, x0 + R }, ccy[4] = { y0 + R, y0 + R, y1 - R, y1 - R };
    const float a_start[4] = { 3.14159f, 4.71239f, 0.0f, 1.5708f };
    for (int q = 0; q < 4; q++) {
        for (int i = 0; i < 6; i++) {
            const float a0 = a_start[q] + (float)i * 0.261799f, a1 = a0 + 0.261799f;
            C2D_DrawLine(ccx[q] + cosf(a0) * R, ccy[q] + sinf(a0) * R, c,
                         ccx[q] + cosf(a1) * R, ccy[q] + sinf(a1) * R, c, th, z);
        }
    }
}
static void neon_ring(float w, float h, float z)
{
    ring_path(w, h, 2.5f, 15.5f, 5.0f, neon_glow(), z);
    ring_path(w, h, 2.0f, 15.0f, 2.4f, C(th_acc), z + 0.002f);
}

/* Spinning compact disc: a rim, a dark body, two sheen wedges that rotate with the music and a hub. */
static void icon_cd(float cx, float cy, float r, float z)
{
    const u32 cAcc = C(th_acc), cGl = neon_glow();
    const u32 cBody = C(mix_rgb(th_bg, th_tx, 36));
    const u32 cMid  = C(mix_rgb(th_bg, th_acc, 90));
    const u32 cLite = C(mix_rgb(th_acc, th_tx, 150));
    const float rr = (r - 1.5f) * 0.97f;
    C2D_DrawCircleSolid(cx, cy, z, r + 1.5f, cGl);
    C2D_DrawCircleSolid(cx, cy, z + 0.002f, r, cAcc);
    C2D_DrawCircleSolid(cx, cy, z + 0.004f, r - 1.4f, cBody);
    for (int w = 0; w < 2; w++) {
        for (int i = 0; i < 3; i++) {
            const float a0 = cd_phase + (float)w * 3.14159f + (float)i * 0.32f, a1 = a0 + 0.32f;
            C2D_DrawTriangle(cx, cy, cMid, cx + cosf(a0) * rr, cy + sinf(a0) * rr, cLite,
                             cx + cosf(a1) * rr, cy + sinf(a1) * rr, cLite, z + 0.006f);
        }
    }
    C2D_DrawCircleSolid(cx, cy, z + 0.008f, r * 0.36f, cAcc);
    C2D_DrawCircleSolid(cx, cy, z + 0.010f, r * 0.17f, C(th_bg));
}

/* LED-style equalizer: stacks of small squares; animated while music plays, a fixed pattern otherwise */
static void icon_eq(float x, float y, float w, float h, bool live, u32 c)
{
    const int cols = 4, rows = 5;
    const float cw = w / cols, rh = h / rows;
    static const int idle[4] = { 2, 4, 3, 1 };
    for (int i = 0; i < cols; i++) {
        int lvl = idle[i];
        if (live) lvl = 1 + (int)((0.5f + 0.5f * sinf(eq_phase * 1.9f + (float)i * 1.7f)) * (float)(rows - 1) + 0.5f);
        for (int r = 0; r < lvl && r < rows; r++)
            C2D_DrawRectSolid(x + i * cw, y + h - (float)(r + 1) * rh, 0.45f, cw - 1.0f, rh - 1.0f, c);
    }
}

static void icon_heart_z(float cx, float cy, float s, u32 c, float z)
{
    C2D_DrawCircleSolid(cx - s * 0.5f, cy - s * 0.3f, z, s * 0.56f, c);
    C2D_DrawCircleSolid(cx + s * 0.5f, cy - s * 0.3f, z, s * 0.56f, c);
    C2D_DrawTriangle(cx - s * 1.02f, cy - s * 0.12f, c, cx + s * 1.02f, cy - s * 0.12f, c, cx, cy + s * 0.95f, c, z);
}
static void icon_heart(float cx, float cy, float s, u32 c) { icon_heart_z(cx, cy, s, c, 0.45f); }
static void icon_heart_line(float cx, float cy, float s, u32 rim, u32 fill)   /* outlined heart; fill = what lies behind it */
{
    const float t = 1.7f;                       /* ring thickness in px, kept even all the way round */
    icon_heart_z(cx, cy, s, rim, 0.45f);
    icon_heart_z(cx, cy + 0.05f * t, s - 1.1f * t, fill, 0.46f);
}

static void icon_home(float cx, float cy, u32 c, u32 door)
{
    C2D_DrawTriangle(cx - 8, cy - 0.5f, c, cx + 8, cy - 0.5f, c, cx, cy - 8, c, 0.45f);
    C2D_DrawRectSolid(cx - 5, cy - 0.5f, 0.45f, 10, 7.5f, c);
    C2D_DrawRectSolid(cx - 1.5f, cy + 2.5f, 0.46f, 3, 4.5f, door);
}

static void icon_small_note(float x, float y, u32 c)      /* ~13x16 note, top-left at x,y */
{
    C2D_DrawCircleSolid(x + 4.5f, y + 12, 0.45f, 3.0f, c);
    C2D_DrawRectSolid(x + 6.5f, y + 2, 0.45f, 2, 10, c);
    C2D_DrawTriangle(x + 8.5f, y + 2, c, x + 12.5f, y + 6.5f, c, x + 8.5f, y + 8, c, 0.45f);
}

static void icon_list(float cx, float cy, u32 c)           /* playlist: three lines with bullets */
{
    for (int i = 0; i < 3; i++) {
        C2D_DrawCircleSolid(cx - 6, cy - 4 + i * 4, 0.45f, 1.2f, c);
        C2D_DrawRectSolid(cx - 3, cy - 5 + i * 4, 0.45f, 9, 2, c);
    }
}

static void icon_speaker(float cx, float cy, bool waves, u32 c)
{
    C2D_DrawRectSolid(cx - 6, cy - 2.5f, 0.45f, 4, 5, c);
    C2D_DrawTriangle(cx - 2, cy - 2.5f, c, cx + 3, cy - 6, c, cx + 3, cy + 6, c, 0.45f);
    C2D_DrawTriangle(cx - 2, cy - 2.5f, c, cx + 3, cy + 6, c, cx - 2, cy + 2.5f, c, 0.45f);
    if (waves) {
        C2D_DrawLine(cx + 5.5f, cy - 3.5f, c, cx + 7.0f, cy, c, 1.5f, 0.45f);
        C2D_DrawLine(cx + 7.0f, cy, c, cx + 5.5f, cy + 3.5f, c, 1.5f, 0.45f);
    }
}

/* cover thumbnail: the whole texture squeezed into a sz x sz square (same orientation as the cover flow) */
static void draw_thumb(C3D_Tex *tex, float x, float y, float sz, float z)
{
    Tex3DS_SubTexture sub;
    C2D_Image img;
    C2D_DrawParams p;
    sub.width = (u16)sz; sub.height = (u16)sz;
    sub.left = 0.0f; sub.right = 1.0f; sub.top = 1.0f; sub.bottom = 0.0f;
    img.tex = tex; img.subtex = &sub;
    p.pos.x = x; p.pos.y = y; p.pos.w = sz; p.pos.h = sz;
    p.center.x = 0; p.center.y = 0; p.depth = z; p.angle = 0;
    C2D_DrawImage(img, &p, NULL);
}

static Rgb tile_color(const char *s)
{
    unsigned h = 5381;
    for (; *s; s++) h = h * 33u + (unsigned char)*s;
    return mix_rgb(th_sel, th_acc, (int)(70 + h % 140));
}

static float progress_frac(u32 pos, u32 dur)
{
    float f = dur ? (float)pos / (float)dur : 0.0f;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return f;
}

static void row_text(const char *s, float x, float y, float rh, float sc, u32 col, u32 align, float maxw)
{
    draw_text(s, x, y + (rh - 28.0f * sc) / 2.0f, sc, col, align, maxw);
}

/* the centre cover of the top screen: the cover flow (flow.c: CY / FACE) is drawn into this square */
#define ART_X   138.0f
#define ART_Y   28.0f
#define ART_SZ  124.0f

static bool track_is_favorite(void)
{
    size_t rl = strlen(MUSIC_ROOT);
    const char *rel = strncmp(cur_path, MUSIC_ROOT, rl) == 0 ? cur_path + rl : cur_path;
    return rel[0] && is_favorite(rel);
}

/* ---------------------------------------------------------------- status bar */
static void icon_bolt(float cx, float cy, float s, u32 c)          /* charging bolt */
{
    C2D_DrawTriangle(cx + s * 0.25f, cy - s, c, cx - s * 0.55f, cy + s * 0.10f, c, cx + s * 0.05f, cy + s * 0.10f, c, 0.46f);
    C2D_DrawTriangle(cx - s * 0.25f, cy + s, c, cx + s * 0.55f, cy - s * 0.10f, c, cx - s * 0.05f, cy - s * 0.10f, c, 0.46f);
}

/* ~24 x 11 battery: outline, terminal nub and a fill that shrinks with the charge (amber <= 30 %, red <= 15 %) */
static void draw_battery_icon(float x, float cy, int pct, bool chg)
{
    const float w = 22.0f, h = 11.0f, y = cy - h / 2.0f;
    Rgb lv = th_acc;
    if (pct <= 15)      { lv.r = 255; lv.g = 84;  lv.b = 80; }
    else if (pct <= 30) { lv.r = 255; lv.g = 190; lv.b = 64; }
    const u32 cLv = C(lv), cRim = C(mix_rgb(th_acc, th_tx, 60)), cIn = C(mix_rgb(th_bg, th_acc, 22));
    rrect(x, y, w, h, 3.0f, 0.40f, cRim);
    rrect(x + 1.5f, y + 1.5f, w - 3.0f, h - 3.0f, 1.8f, 0.41f, cIn);
    C2D_DrawRectSolid(x + w, cy - 2.5f, 0.40f, 2.0f, 5.0f, cRim);
    float fw = (w - 5.0f) * (float)pct / 100.0f;
    if (fw >= 1.0f) rrect(x + 2.5f, y + 2.5f, fw < 3.0f ? 3.0f : fw, h - 5.0f, 1.2f, 0.42f, cLv);
    if (chg) icon_bolt(x + w / 2.0f, cy, 4.2f, C(th_tx));
}

/* Right end of the one header bar: battery (icon or percentage) and the sleep-timer dot.  `right` is the
 * x the cluster is right-aligned to (just left of the clock when the clock is on). */
static void draw_status_bar(float right)
{
    float x = right;
    if (S.show_batt && batt_pct >= 0) {
        if (S.batt_style == 0) {
            draw_battery_icon(x - 24.0f, 15.0f, batt_pct, batt_chg);
            x -= 32.0f;
        } else {
            char b[8];
            snprintf(b, sizeof b, "%d%%", batt_pct);
            const u32 col = batt_pct <= 15 ? C2D_Color32(255, 84, 80, 255) : C(th_tx);
            row_text(b, x - 1.0f, 4, 22, 0.44f, col, C2D_AlignRight, 40);
            const float tw = 7.0f * (float)strlen(b) + 4.0f;
            if (batt_chg) icon_bolt(x - tw - 5.0f, 15.0f, 4.0f, C(th_acc));
            x -= tw + (batt_chg ? 12.0f : 6.0f);
        }
    }
    if (sleep_timer_active) C2D_DrawCircleSolid(x - 5.0f, 15.0f, 0.45f, 2.6f, C(th_acc));
}

/* Text with a soft halo: the string is parsed once and drawn a few times around itself at low alpha, then
 * once on top.  gk = 0..1 glow strength.  Battery saver keeps just four faint copies. */
static void draw_text_glow(const char *s, float x, float y, float sc, u32 align, Rgb col, Rgb gcol,
                           float gk, float alpha, float maxw)
{
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s", s);
    C2D_Text t;
    C2D_TextParse(&t, tbuf, tmp);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, sc, sc, &w, &h);
    if (maxw > 0 && w > maxw) {
        size_t len = strlen(tmp);
        size_t cut = (size_t)((float)len * maxw / w);
        cut = (cut > 4) ? cut - 3 : 1;
        while (cut > 0 && (((unsigned char)tmp[cut]) & 0xC0) == 0x80) cut--;
        strcpy(tmp + cut, "...");
        C2D_TextParse(&t, tbuf, tmp);
    }
    C2D_TextOptimize(&t);
    if (gk > 0.02f && alpha > 0.02f) {
        static const float RX[12] = { 1.8f, -1.8f, 0, 0, 1.3f, -1.3f, 1.3f, -1.3f, 3.2f, -3.2f, 0, 0 };
        static const float RY[12] = { 0, 0, 1.8f, -1.8f, 1.3f, 1.3f, -1.3f, -1.3f, 0, 0, 3.2f, -3.2f };
        const int n = S.saver ? 4 : 12;
        const u32 c1 = Ca(gcol, (S.saver ? 0.20f : 0.24f) * gk * alpha), c2 = Ca(gcol, 0.11f * gk * alpha);
        for (int i = 0; i < n; i++)
            C2D_DrawText(&t, C2D_WithColor | align, x + RX[i], y + RY[i], text_z - 0.01f, sc, sc, i < 8 ? c1 : c2);
    }
    C2D_DrawText(&t, C2D_WithColor | align, x, y, text_z, sc, sc, Ca(col, alpha));
}

/* ====================================================================== retro head-unit display
 * Full-width "display window" in the spirit of classic car-stereo analyzers: a metal bezel, dark glass,
 * a title / read-out strip, ghosted unlit segments, peak-hold caps, a graticule scope and analogue VU
 * needles.  Everything is built from rectangles and lines so it stays light on an Old 3DS. */
#define PV_X 10.0f
#define PV_Y 30.0f
#define PV_W 380.0f
#define PV_H 122.0f

static float pv_clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

static float pv_band(int i, int n, float *peak)       /* bar i of n from the 12 analysed bands */
{
    const float f = (float)i / (float)(n - 1) * (float)(AUDIO_VIS_BANDS - 1);
    const int i0 = (int)f, i1 = (i0 + 1 < AUDIO_VIS_BANDS) ? i0 + 1 : i0;
    const float fr = f - (float)i0;
    if (peak) *peak = vis_peak[i0] * (1.0f - fr) + vis_peak[i1] * fr;
    return vis_band[i0] * (1.0f - fr) + vis_band[i1] * fr;
}

static bool pv_standby(void)        /* nothing playing: a calm standby shimmer, like a unit waiting for a disc */
{
    return !S.saver && !(track_loaded && audio_is_active() && !audio_is_paused());
}
static float pv_tsec(void) { return (float)(osGetTime() % 100000ULL) * 0.001f; }

static void pv_window(const char *title)
{
    const Rgb black = { 0, 0, 0 };
    const u32 cHalo = neon_glow();
    const u32 cBez = C(mix_rgb(th_bg, th_tx, 52)), cHi = C(mix_rgb(th_bg, th_tx, 105));
    const u32 cRim = C(mix_rgb(th_bg, th_acc, 130)), cGlass = C(mix_rgb(th_bg, black, 120));
    const u32 cLine = C(mix_rgb(th_bg, th_acc, 60)), cTitle = C(mix_rgb(th_acc, th_bg, 40));
    const bool playing = track_loaded && audio_is_active() && !audio_is_paused();

    rrect(PV_X - 1.5f, PV_Y - 1.5f, PV_W + 3, PV_H + 3, 10.0f, 0.10f, cHalo);
    rrect(PV_X, PV_Y, PV_W, PV_H, 8.5f, 0.12f, cBez);
    C2D_DrawRectSolid(PV_X + 9, PV_Y + 0.5f, 0.13f, PV_W - 18, 1, cHi);          /* bezel highlight */
    rrect(PV_X + 4, PV_Y + 4, PV_W - 8, PV_H - 8, 5.0f, 0.14f, cRim);
    rrect(PV_X + 5, PV_Y + 5, PV_W - 10, PV_H - 10, 4.2f, 0.15f, cGlass);

    C2D_DrawCircleSolid(PV_X + 15, PV_Y + 14, 0.30f, 2.2f, playing ? C(th_acc) : C(mix_rgb(th_bg, th_acc, 70)));
    row_text(title, PV_X + 24, PV_Y + 7, 14, 0.32f, cTitle, C2D_AlignLeft, 170);
    if (S.show_info && track_loaded) {
        char ln[64], e[16], d[16], b[16];
        fmt_time(e, sizeof e, cur_pos_ms());
        if (cur.duration_ms) fmt_time(d, sizeof d, cur.duration_ms); else snprintf(d, sizeof d, "--:--");
        if (ti.kbps > 0) snprintf(b, sizeof b, "%d kbps", ti.kbps); else snprintf(b, sizeof b, "-- kbps");
        snprintf(ln, sizeof ln, "%s   %s   %s / %s", ti.fmt, b, e, d);
        row_text(ln, PV_X + PV_W - 12, PV_Y + 7, 14, 0.32f, C(mix_rgb(th_tx, th_bg, 70)), C2D_AlignRight, 190);
    }
    C2D_DrawRectSolid(PV_X + 10, PV_Y + 23, 0.16f, PV_W - 20, 1, cLine);
}

static void pv_scanlines(void)         /* faint raster lines over the glass */
{
    if (S.saver) return;
    const u32 c = C2D_Color32(0, 0, 0, 40);
    for (float y = PV_Y + 26; y < PV_Y + PV_H - 6; y += 4.0f) C2D_DrawRectSolid(PV_X + 6, y, 0.38f, PV_W - 12, 1, c);
}

static void pv_spectrum(void)
{
    const bool lite = S.saver;
    const int N = lite ? 16 : 24, M = lite ? 8 : 12;
    const float ax0 = PV_X + 32, ax1 = PV_X + PV_W - 14, ay0 = PV_Y + 29, ay1 = PV_Y + PV_H - 20;
    const float sp = (ax1 - ax0) / (float)N, bw = sp - 3.0f, sh = (ay1 - ay0) / (float)M, segh = sh - 1.5f;
    const Rgb red = { 255, 70, 52 };
    const u32 cGhost = C(mix_rgb(th_bg, th_acc, 28)), cOn = C(th_acc), cMid = C(mix_rgb(th_acc, th_tx, 90));
    const u32 cHot = C(mix_rgb(th_acc, red, 150)), cCap = C(th_tx), cDim = C(mix_rgb(th_tx, th_bg, 130));
    const bool idle = pv_standby();
    const float ts = pv_tsec();

    for (int i = 0; i < N; i++) {
        float pk = 0.0f;
        float v = pv_band(i, N, &pk);
        if (idle) { v = 0.10f + 0.08f * sinf(ts * 1.7f + (float)i * 0.55f); pk = v; }
        const int lit = (int)(pv_clamp01(v) * (float)M + 0.5f);
        const int cap = (int)(pv_clamp01(pk) * (float)M + 0.5f) - 1;
        const float x = ax0 + (float)i * sp + 1.5f;
        for (int m = 0; m < M; m++) {
            u32 col;
            if (m < lit) col = (m >= M - 2) ? cHot : ((m >= (M * 2) / 3) ? cMid : cOn);
            else if (!lite) col = cGhost;
            else continue;
            C2D_DrawRectSolid(x, ay1 - (float)(m + 1) * sh + 0.75f, 0.30f, bw, segh, col);
        }
        if (cap >= lit && cap >= 0 && cap < M)
            C2D_DrawRectSolid(x, ay1 - (float)(cap + 1) * sh + 0.75f, 0.31f, bw, segh, cCap);
    }
    if (!lite) {                                                  /* dB scale */
        static const char *DB[4] = { "0", "-10", "-20", "-30" };
        for (int k = 0; k < 4; k++) {
            const float y = ay0 + (float)k * (ay1 - ay0) / 4.0f;
            C2D_DrawRectSolid(ax0 - 5, y, 0.30f, 3, 1, cDim);
            draw_text(DB[k], ax0 - 8, y - 4, 0.26f, cDim, C2D_AlignRight, 0);
        }
    }
    static const char *HZ[5] = { "60", "230", "910", "3.6k", "14k" };      /* frequency scale */
    for (int k = 0; k < 5; k++) {
        const int bi = (int)((float)k / 4.0f * (float)(N - 1) + 0.5f);
        draw_text(HZ[k], ax0 + (float)bi * sp + sp / 2.0f, ay1 + 4, 0.28f, cDim, C2D_AlignCenter, 0);
    }
}

static void pv_scope(void)
{
    const bool lite = S.saver;
    const float ax0 = PV_X + 14, ax1 = PV_X + PV_W - 14, ay0 = PV_Y + 29, ay1 = PV_Y + PV_H - 24;
    const float mid = (ay0 + ay1) / 2.0f, amp = (ay1 - ay0) * 0.46f;
    const u32 cGrid = C(mix_rgb(th_bg, th_acc, 26)), cAxis = C(mix_rgb(th_bg, th_acc, 52));
    const u32 cTrace = C(mix_rgb(th_acc, th_tx, 70)), cHalo = Ca(th_glow, 0.26f);
    const u32 cOn = C(th_acc), cMid = C(mix_rgb(th_acc, th_tx, 90)), cGhost = C(mix_rgb(th_bg, th_acc, 28));
    const bool idle = pv_standby();
    const float ts = pv_tsec();

    for (int k = 0; k <= 10; k++) C2D_DrawRectSolid(ax0 + (float)k * (ax1 - ax0) / 10.0f, ay0, 0.28f, 1, ay1 - ay0, cGrid);
    for (int k = 0; k <= 4; k++) C2D_DrawRectSolid(ax0, ay0 + (float)k * (ay1 - ay0) / 4.0f, 0.28f, ax1 - ax0, 1, k == 2 ? cAxis : cGrid);

    const float step = (ax1 - ax0) / (float)(AUDIO_VIS_WAVE - 1);
    float px = ax0, py = mid;
    for (int i = 0; i < AUDIO_VIS_WAVE; i++) {
        float w = vis_wave[i];
        if (idle) w = 0.16f * sinf(ts * 2.0f + (float)i * 0.45f);
        w *= 0.72f;
        if (w > 1.0f) w = 1.0f;
        if (w < -1.0f) w = -1.0f;
        const float x = ax0 + (float)i * step, y = mid + w * amp;
        if (i > 0) {
            if (!lite) C2D_DrawLine(px, py, cHalo, x, y, cHalo, 5.0f, 0.32f);
            C2D_DrawLine(px, py, cTrace, x, y, cTrace, 2.0f, 0.34f);
        }
        px = x; py = y;
    }
    /* level strip along the bottom */
    const int SEG = lite ? 20 : 40;
    const float sw = (ax1 - ax0) / (float)SEG;
    const int lit = (int)(pv_clamp01(vis_bass * 0.95f) * (float)SEG + 0.5f);
    for (int k = 0; k < SEG; k++) {
        u32 col = k < lit ? (k >= SEG - 4 ? cMid : cOn) : cGhost;
        if (k >= lit && lite) continue;
        C2D_DrawRectSolid(ax0 + (float)k * sw + 0.5f, ay1 + 8, 0.30f, sw - 1.5f, 6, col);
    }
}

static float vu_l = 0.0f, vu_r = 0.0f;

static void pv_meter(float cx, float val, const char *tag)
{
    const bool lite = S.saver;
    const float R = 76.0f, py = PV_Y + PV_H - 14.0f, A0 = -0.698f, A1 = 0.698f;
    const u32 cTick = C(mix_rgb(th_tx, th_bg, 90)), cRed = C(mix_rgb(th_acc, (Rgb){ 255, 70, 52 }, 190));
    const u32 cNeedle = C(mix_rgb(th_acc, th_tx, 110)), cHalo = Ca(th_glow, 0.28f), cDim = C(mix_rgb(th_tx, th_bg, 130));

    for (int t = 0; t <= 12; t++) {                               /* scale ticks, last quarter in the red */
        const float a = A0 + (A1 - A0) * (float)t / 12.0f;
        const float len = (t % 3 == 0) ? 9.0f : 5.0f;
        const float sx = sinf(a), cy = -cosf(a);
        C2D_DrawLine(cx + sx * (R - len), py + cy * (R - len), t >= 9 ? cRed : cTick,
                     cx + sx * R, py + cy * R, t >= 9 ? cRed : cTick, 1.6f, 0.30f);
    }
    for (int k = 0; k < 24; k++) {                                /* scale arc */
        const float a0 = A0 + (A1 - A0) * (float)k / 24.0f, a1 = A0 + (A1 - A0) * (float)(k + 1) / 24.0f;
        const u32 c = k >= 18 ? cRed : cTick;
        C2D_DrawLine(cx + sinf(a0) * R, py - cosf(a0) * R, c, cx + sinf(a1) * R, py - cosf(a1) * R, c, 1.2f, 0.30f);
    }
    const float an = A0 + (A1 - A0) * pv_clamp01(val * 1.15f);
    const float tx = cx + sinf(an) * (R - 3.0f), ty = py - cosf(an) * (R - 3.0f);
    if (!lite) C2D_DrawLine(cx, py, cHalo, tx, ty, cHalo, 4.6f, 0.32f);
    C2D_DrawLine(cx, py, cNeedle, tx, ty, cNeedle, 1.8f, 0.34f);
    C2D_DrawCircleSolid(cx, py, 0.36f, 4.2f, cNeedle);
    C2D_DrawCircleSolid(cx, py, 0.37f, 1.8f, C(th_bg));
    draw_text(tag, cx - 56, PV_Y + 28, 0.34f, cDim, C2D_AlignLeft, 0);
}

static void pv_deck(void)
{
    const bool lite = S.saver;
    float lo = 0.0f, hi = 0.0f;
    for (int i = 0; i < 6; i++) lo += vis_band[i] / 6.0f;
    for (int i = 5; i < AUDIO_VIS_BANDS; i++) hi += vis_band[i] / 7.0f;
    if (pv_standby()) { const float ts = pv_tsec(); lo = 0.10f + 0.05f * sinf(ts * 1.3f); hi = 0.10f + 0.05f * sinf(ts * 1.1f + 1.0f); }
    lo *= 0.82f; hi *= 0.92f;
    vu_l += (lo - vu_l) * (lo > vu_l ? 0.45f : 0.12f);              /* needles: quick rise, lazy fall */
    vu_r += (hi - vu_r) * (hi > vu_r ? 0.45f : 0.12f);

    pv_meter(PV_X + 92.0f, vu_l, "L");
    pv_meter(PV_X + PV_W - 92.0f, vu_r, "R");

    /* five-band stack between the meters */
    const int B = 5, M = lite ? 6 : 10;
    const float ay0 = PV_Y + 31, ay1 = PV_Y + PV_H - 22, sh = (ay1 - ay0) / (float)M;
    const float x0 = PV_X + PV_W / 2.0f - 34.0f;
    const u32 cGhost = C(mix_rgb(th_bg, th_acc, 28)), cOn = C(th_acc), cMid = C(mix_rgb(th_acc, th_tx, 90));
    const u32 cHot = C(mix_rgb(th_acc, (Rgb){ 255, 70, 52 }, 150)), cDim = C(mix_rgb(th_tx, th_bg, 130));
    static const char *HZ[5] = { "60", "230", "910", "3.6k", "14k" };
    for (int b = 0; b < B; b++) {
        float v = vis_band[(b * (AUDIO_VIS_BANDS - 1)) / (B - 1)];
        if (pv_standby()) v = 0.12f + 0.08f * sinf(pv_tsec() * 1.6f + (float)b);
        const int lit = (int)(pv_clamp01(v) * (float)M + 0.5f);
        const float x = x0 + (float)b * 15.0f;
        for (int m = 0; m < M; m++) {
            u32 col;
            if (m < lit) col = (m >= M - 2) ? cHot : ((m >= (M * 2) / 3) ? cMid : cOn);
            else if (!lite) col = cGhost;
            else continue;
            C2D_DrawRectSolid(x, ay1 - (float)(m + 1) * sh + 0.75f, 0.30f, 9, sh - 1.5f, col);
        }
        if (!lite) draw_text(HZ[b], x + 4.5f, ay1 + 4, 0.26f, cDim, C2D_AlignCenter, 0);
    }
}

static void pv_draw(void)
{
    static const char *TITLES[4] = { "", "NOISE  SPECTRUM ANALYZER", "NOISE  SIGNAL SCOPE", "NOISE  LEVEL METERS" };
    pv_window(TITLES[(unsigned)S.display_mode % 4]);
    if (S.display_mode == 1) pv_spectrum();
    else if (S.display_mode == 2) pv_scope();
    else pv_deck();
    pv_scanlines();
}

/* NOW PLAYING / BROWSING, centred directly under the cover. The label cross-fades when the view changes and
 * glows in the halo colour while music plays (the glow breathes very gently and fades out on pause). */
static void draw_state_label(void)
{
    if (label_a < 0.02f) return;
    const char *txt = label_np ? "NOW PLAYING" : "BROWSING";
    Rgb col = label_np ? mix_rgb(th_acc, th_tx, (int)(45.0f * label_glow)) : mix_rgb(th_tx, th_bg, 60);
    float gk = label_glow;
    if (gk > 0.01f && !S.saver) gk *= 0.82f + 0.18f * sinf(eq_phase * 0.55f) + 0.10f * vis_bass;
    draw_text_glow(txt, 200.0f, 153.0f, 0.64f, C2D_AlignCenter, col, th_glow, gk, label_a, 0);
}

/* Music information: two quiet cards beside the cover (time on the left, file format on the right).
 * They fade in only on the Now Playing view, so the artwork and the controls stay clear. */
static void draw_info_cards(float a)
{
    if (a < 0.02f || !track_loaded) return;
    char el[16], du[16], br[24];
    fmt_time(el, sizeof el, cur_pos_ms());
    if (cur.duration_ms) fmt_time(du, sizeof du, cur.duration_ms); else snprintf(du, sizeof du, "--:--");
    if (ti.kbps > 0) snprintf(br, sizeof br, "%d kbps", ti.kbps); else snprintf(br, sizeof br, "-- kbps");

    const float y = 46.0f, h = 78.0f, w = 108.0f;
    const Rgb rim = mix_rgb(th_bg, th_acc, 120);
    const u32 cCap = Ca(mix_rgb(th_tx, th_bg, 130), a), cVal = Ca(th_tx, a), cAcc = Ca(th_acc, a);
    for (int side = 0; side < 2; side++) {
        const float x = side ? 276.0f : 16.0f, cx = x + w / 2.0f;
        rrect(x - 1.5f, y - 1.5f, w + 3, h + 3, 11.5f, 0.08f, Ca(mix_rgb(th_bg, th_glow, 60), a * 0.6f));
        rrect(x, y, w, h, 10.0f, 0.082f, Ca(rim, a));
        rrect(x + 1.5f, y + 1.5f, w - 3, h - 3, 8.5f, 0.084f, Ca(mix_rgb(th_bg, th_acc, 12), a * 0.94f));
        C2D_DrawRectSolid(x + 14, y + 39, 0.09f, w - 28, 1, Ca(rim, a * 0.8f));
        if (!side) {
            draw_text("ELAPSED", cx, y + 6, 0.30f, cCap, C2D_AlignCenter, 0);
            draw_text(el, cx, y + 15, 0.64f, cAcc, C2D_AlignCenter, w - 12);
            draw_text("LENGTH", cx, y + 44, 0.30f, cCap, C2D_AlignCenter, 0);
            draw_text(du, cx, y + 53, 0.64f, cVal, C2D_AlignCenter, w - 12);
        } else {
            draw_text("FORMAT", cx, y + 6, 0.30f, cCap, C2D_AlignCenter, 0);
            draw_text(ti.fmt, cx, y + 15, 0.64f, cAcc, C2D_AlignCenter, w - 12);
            draw_text("BITRATE", cx, y + 44, 0.30f, cCap, C2D_AlignCenter, 0);
            draw_text(br, cx, y + 55, 0.50f, cVal, C2D_AlignCenter, w - 12);
        }
    }
}

static void draw_top(void)
{
    /*
     * Neon top screen: one seamless header bar, the cover (or the full-width head-unit display), the
     * NOW PLAYING / BROWSING label, music-information cards, the song panel and the seek bar.
     * The cover flow renderer is untouched - it just draws into the framed square.
     */
    const u32 cAcc  = C(th_acc), cText = C(th_tx), cBg = C(th_bg);
    const u32 cDim  = C(mix_rgb(th_tx, th_bg, 120));
    const u32 cGlow = neon_glow();
    const u32 cFill = C(mix_rgb(th_bg, th_acc, 8));
    const u32 cRowSel = C(mix_rgb(th_bg, th_acc, 38));
    const u32 cTrk  = C(mix_rgb(th_bg, th_acc, 70));
    const bool playing = track_loaded && audio_is_active() && !audio_is_paused();
    const bool use_flow = (S.display_mode == 0) && flow_appear > 0.01f && L.n > 0;
    const bool halo_text = S.oled && !S.saver;                 /* OLED mode: glowing brand + title */

    info_refresh();
    top_background();                      /* ambient glow tinted by the cover (behind everything) */
    neon_ring(TOP_W, SCR_H, 0.03f);        /* outline only, so the glow shows through */

    /* ---- header: ONE bar (brand | clock, battery); it never splits, whatever the clock setting ---- */
    neon_box(8, 4, 384, 22, 11.0f, 0.08f, cAcc, cGlow, cFill);
    icon_eq(19, 8, 16, 14, playing, cAcc);
    if (halo_text) {
        draw_text_glow("PROJECT", 42, 4 + (22 - 28 * 0.50f) / 2.0f, 0.50f, C2D_AlignLeft, th_tx, th_glow, 0.45f, 1.0f, 80);
        draw_text_glow("NOISE", 108, 4 + (22 - 28 * 0.50f) / 2.0f, 0.50f, C2D_AlignLeft, th_acc, th_glow, 0.70f, 1.0f, 64);
    } else {
        row_text("PROJECT", 42, 4, 22, 0.50f, cText, C2D_AlignLeft, 80);
        row_text("NOISE", 108, 4, 22, 0.50f, cAcc, C2D_AlignLeft, 64);
    }
    float status_right = 382.0f;
    if (S.clock_mode) {
        time_t now = time(NULL);
        struct tm *tmv = localtime(&now);
        if (tmv) {
            char cb[32];
            if (S.clock_mode == 1) {
                int hr = tmv->tm_hour % 12; if (hr == 0) hr = 12;
                snprintf(cb, sizeof cb, "%d:%02d %s", hr, tmv->tm_min, tmv->tm_hour < 12 ? "AM" : "PM");
            } else {
                snprintf(cb, sizeof cb, "%02d:%02d", tmv->tm_hour, tmv->tm_min);
            }
            row_text(cb, 380, 4, 22, 0.46f, cText, C2D_AlignRight, 72);
        }
        C2D_DrawRectSolid(306, 9, 0.40f, 1, 12, cTrk);         /* thin divider inside the same bar */
        status_right = 300.0f;
    }
    draw_status_bar(status_right);

    /* ---- cover area: the cover flow / cover, or the full-width head-unit display ---- */
    /* no ring around the cover: the ambient glow behind it (tinted by the artwork) is the frame */

    if (!use_flow && S.display_mode != 0) {
        pv_draw();
    } else if (use_flow) {
        FlowItem items[10];
        const int base = (int)floorf(flow_pos) - 4;
        const int ci = (int)floorf(flow_pos + 0.5f);
        const bool playing_view = !browsing_view();

        for (int i = 0; i < 10; i++) {
            int idx = base + i;
            memset(&items[i], 0, sizeof items[i]);
            if (idx < 0 || idx >= L.n || fabsf((float)idx - flow_pos) > FLOW_SIDE + 0.6f) continue;

            Entry *e = &L.e[idx];
            items[i].kind = e->is_dir ? 1 : 2;
            items[i].color = e->is_dir
                ? C(mix_rgb(mix_rgb(th_bg, th_acc, 100), th_tx, 55))
                : C(mix_rgb(th_bg, tile_color(e->name), 90));
            items[i].fade = 1.0f;

            if (playing_view && idx == ci && art_ready()) {
                items[i].tex = art_image()->tex;
                items[i].color = cAcc;
                float f = art_fade;
                items[i].fade = f * f * (3.0f - 2.0f * f);
            } else if (is_current_row(idx) && art_ready()) {
                items[i].tex = art_image()->tex;
            } else {
                items[i].tex = flow_tex(idx, &items[i].fade);
            }

            if (playing_view && idx == ci && !items[i].tex) items[i].kind = 0;
        }

        /* the 3D-style cover carousel, unchanged */
        flow_set_fast(fabsf(flow_pos - (float)sel) > 0.12f || (flow_appear > 0.02f && flow_appear < 0.98f));   /* moving: wide strips */
        flow_draw(items, 10, base, flow_pos, flow_appear, top_bg_color);
    } else if (art_ready()) {
        /* the cover settles in with a tiny scale-up and breathes with the bass (both off in Battery saver) */
        const float ease = art_fade * art_fade * (3.0f - 2.0f * art_fade);
        const float k = S.saver ? 1.0f : (0.94f + 0.06f * ease) * (1.0f + 0.010f * vis_bass);
        const float sz = ART_SZ * k, ox = ART_X + (ART_SZ - sz) / 2.0f, oy = ART_Y + (ART_SZ - sz) / 2.0f;
        C2D_DrawImageAt(*art_image(), ox, oy, 0.40f, NULL, sz / 128.0f, sz / 128.0f);
        if (art_fade < 0.999f) {
            C2D_DrawRectSolid(ox, oy, 0.41f, sz, sz,
                              C2D_Color32(th_bg.r, th_bg.g, th_bg.b, (u8)(255.0f * (1.0f - art_fade))));
        }
    } else {
        icon_note(ART_X + ART_SZ / 2 - 7, ART_Y + ART_SZ / 2 - 8, cText);
    }

    /* ---- label right under the artwork + music information beside it ---- */
    draw_state_label();
    if (S.display_mode == 0) draw_info_cards(info_a * (1.0f - flow_appear));

    /* ---- song panel: the same look as a highlighted row on the bottom screen ---- */
    const float py = 176, ph = 40, pcy = py + ph / 2;
    neon_box(24, py, 352, ph, 12, 0.08f, cAcc, cGlow, cRowSel);
    icon_cd(48, pcy, 14.0f, 0.10f);                          /* spins while the music plays */

    const char *title = "Nothing playing", *sub = "Pick a song to start";
    if (track_loaded) {
        title = cur.title[0] ? cur.title : base_name(cur_path);
        sub = cur.artist;
    } else if (use_flow && L.n > 0) {
        int fi = (int)floorf(flow_pos + 0.5f);
        if (fi < 0) fi = 0;
        if (fi >= L.n) fi = L.n - 1;
        Entry *e = &L.e[fi];
        title = e->is_dir ? (!strcmp(e->name, "..") ? "Back" : e->name)
                          : (e->title[0] ? e->title : base_name(e->name));
        sub = e->is_dir ? "Folder" : e->artist;
    }
    /* Clear hierarchy: title is the primary line, artist is secondary. Both stay inside
     * the panel and marquee automatically when metadata is longer than the available width. */
    draw_marquee_text(title, 72, py + 2, 0.70f, cAcc, track_loaded ? 262.0f : 280.0f, halo_text);
    if (sub[0]) draw_marquee_text(sub, 72, py + 23, 0.44f, cText, 262.0f, false);
    if (track_loaded) {
        if (track_is_favorite()) icon_heart(357, pcy, 7.0f, cAcc);
        else icon_heart_line(357, pcy, 7.0f, cDim, cRowSel);
    }

    /* ---- seek bar ---- */
    u32 pos = track_loaded ? cur_pos_ms() : 0;
    u32 dur = track_loaded ? cur.duration_ms : 0;
    float frac = progress_frac(pos, dur);
    const float bx0 = 66.0f, bw = 268.0f, by = 226.0f;
    char tb[24];

    if (track_loaded) {
        fmt_time(tb, sizeof tb, pos);
        row_text(tb, 26, by - 8, 16, 0.40f, cDim, C2D_AlignLeft, 38);
        fmt_time(tb, sizeof tb, dur);
        row_text(tb, 374, by - 8, 16, 0.40f, cDim, C2D_AlignRight, 38);
    }
    rrect(bx0 - 2, by - 5, bw + 4, 10, 5, 0.06f, cGlow);
    rrect(bx0, by - 3, bw, 6, 3, 0.08f, cTrk);
    float fw = bw * frac; if (fw < 6) fw = 6;
    rrect(bx0, by - 3, fw, 6, 3, 0.09f, cAcc);
    float kx = bx0 + bw * frac;
    C2D_DrawCircleSolid(kx, by, 0.12f, 7.0f, cGlow);
    C2D_DrawCircleSolid(kx, by, 0.13f, 5.4f, cAcc);
    C2D_DrawCircleSolid(kx, by, 0.14f, 2.6f, cBg);
}

/* ====================================================================== lyrics screen
 * Top screen: current line highlighted in the middle, upcoming lines below (synced), or a scrollable text
 * (plain). Bottom screen intentionally remains the normal browser/player UI. */
static float fit_scale(const char *s, float sc, float maxw)       /* shrink a line to fit the screen width */
{
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s", s);
    C2D_Text t;
    C2D_TextParse(&t, tbuf, tmp);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, sc, sc, &w, &h);
    if (w > maxw && w > 0.0f) { sc *= maxw / w; if (sc < 0.32f) sc = 0.32f; }
    return sc;
}

static float ly_font_scale(float sc)
{
    if (S.ly_font == 2) return sc * 0.88f;
    return sc;
}

static void ly_draw_text(const char *s, float x, float y, float sc, u32 color, u32 align, float maxw)
{
    sc = ly_font_scale(sc);
    if (S.ly_font == 1) {
        /* Bold style: two sub-pixel-offset passes using the same native 3DS font.
         * This keeps the build self-contained while making the lyric face visibly heavier. */
        draw_text(s, x + 0.65f, y, sc, color, align, maxw);
        draw_text(s, x, y + 0.45f, sc, color, align, maxw);
    }
    draw_text(s, x, y, sc, color, align, maxw);
}

/* Gothic lyrics typeface (Settings > Lyrics > Lyrics font). Lyrics screen only: drawn from the glyph atlas in
 * lyfont.c. A line is shrunk to fit (and, only past LY_GOTHIC_MIN, cut with "...") so English and Spanish lines look
 * alike; only a line holding a character the atlas has no glyph for (other scripts, symbols) returns false and the
 * caller uses the system font, so no lyric is ever lost. yc = vertical centre of the letters. */
#define LY_GOTHIC_MIN 0.36f
static bool ly_gothic_line(const char *s, float x, float yc, float sc, Rgb col, float glow, float maxw)
{
    if (S.ly_font != 0 || !lyfont_ok() || !lyfont_can(s)) return false;
    char cut[320];
    const float w = lyfont_width(s, sc);
    if (w > maxw) sc *= maxw / w;
    if (sc < LY_GOTHIC_MIN) { sc = LY_GOTHIC_MIN; s = lyfont_fit(s, sc, maxw, cut, sizeof cut); }
    if (glow > 0.01f && !S.saver) {                                   /* soft halo: four offset copies */
        static const float ox[4] = { 1.5f, -1.5f, 0.0f, 0.0f }, oy[4] = { 0.0f, 0.0f, 1.5f, -1.5f };
        const u32 g = C(mix_rgb(th_bg, th_glow, (int)(110.0f * glow)));
        for (int i = 0; i < 4; i++) lyfont_draw(s, x + ox[i], yc + oy[i], sc, g, C2D_AlignCenter, text_z - 0.01f);
    }
    lyfont_draw(s, x, yc, sc, C(col), C2D_AlignCenter, text_z);
    return true;
}

static const char *ly_state_msg(LyState st, const char **sub)
{
    switch (st) {
    case LY_LOADING:  *sub = "";                                        return "Searching for lyrics";
    case LY_IDLE:     *sub = "Press A to search online";            return "No saved lyrics for this song";
    case LY_NOTFOUND: *sub = "Press A to search again";             return "LYRICS NOT FOUND";
    case LY_OFFLINE:  *sub = "Turn off Offline only to search";     return "No cached lyrics (offline only)";
    case LY_NONET:    *sub = "Connect to Wi-Fi, then press A";      return "No Wi-Fi connection";
    case LY_ERROR:    *sub = "Press A to search again";             return "LYRICS NOT FOUND";
    default:          *sub = "";                                    return "";
    }
}

static void draw_lyrics_top(void)
{
    const u32 cAcc = C(th_acc), cText = C(th_tx), cDim = C(mix_rgb(th_tx, th_bg, 120));
    const u32 cGlow = neon_glow(), cFill = C(mix_rgb(th_bg, th_acc, 8)), cTrk = C(mix_rgb(th_bg, th_acc, 70));
    const u32 pos = track_loaded ? cur_pos_ms() : 0;
    const LyState st = lyrics_state();
    const bool ready = (st == LY_READY);
    const bool synced = ready && lyrics_synced();
    const int n = lyrics_count();

    top_background();
    neon_ring(TOP_W, SCR_H, 0.03f);

    /* ---- header: LYRICS | song - artist | state ---- */
    neon_box(8, 4, 384, 22, 11.0f, 0.08f, cAcc, cGlow, cFill);
    icon_cd(24, 15, 7.5f, 0.10f);
    row_text("LYRICS", 38, 4, 22, 0.50f, cAcc, C2D_AlignLeft, 60);
    char hd[300];
    const char *title = cur.title[0] ? cur.title : base_name(cur_path);
    if (cur.artist[0]) snprintf(hd, sizeof hd, "%s - %s", title, cur.artist);
    else snprintf(hd, sizeof hd, "%s", title);
    row_text(hd, 104, 4, 22, 0.42f, cText, C2D_AlignLeft, 200);
    const char *chip = ready ? (synced ? "SYNCED" : "TEXT") : (st == LY_LOADING ? "SEARCHING" : "");
    if (chip[0]) row_text(chip, 380, 4, 22, 0.40f, cAcc, C2D_AlignRight, 70);

    /* ---- body ---- */
    if (!ready) {
        const char *sub;
        const char *msg = ly_state_msg(st, &sub);
        if (!ly_gothic_line(msg, 200, 100, 0.74f, th_tx, 0.0f, 370)) ly_draw_text(msg, 200, 92, 0.62f, cText, C2D_AlignCenter, 370);
        if (sub[0]) ly_draw_text(sub, 200, 124, 0.42f, cDim, C2D_AlignCenter, 370);
        if (st == LY_LOADING) {
            const float ts = pv_tsec();
            for (int k = 0; k < 3; k++) {
                const float b = 0.5f + 0.5f * sinf(ts * 6.0f - (float)k * 0.9f);
                C2D_DrawCircleSolid(184.0f + (float)k * 16.0f, 156.0f - 5.0f * b, 0.30f, 3.5f, Ca(th_acc, 0.35f + 0.65f * b));
            }
        }
    } else if (synced) {
        const int idx = lyrics_index_at(pos);
        const float goal = (float)idx;
        if (!S.ly_animated || S.saver || fabsf(goal - ly_pos) > 6.0f) ly_pos = goal; else ly_pos += (goal - ly_pos) * 0.20f;
        const int c0 = (int)floorf(ly_pos + 0.5f);
        for (int k = -3; k <= 3; k++) {
            const int i = c0 + k;
            if (i < 0 || i >= n) continue;
            const float d = (float)i - ly_pos, ad = fabsf(d);
            const float a = 1.0f - ad * 0.27f;
            if (a < 0.08f) continue;
            const float cy = 118.0f + d * 25.0f + 3.0f * fmaxf(-1.0f, fminf(1.0f, d));   /* a little air around the current line */
            const char *tx = lyrics_text(i);
            if (i == idx) {
                if (!ly_gothic_line(tx, 200, cy, 0.78f, mix_rgb(th_acc, th_tx, 60), 0.45f, 372.0f)) {
                    const float sc = fit_scale(tx, 0.70f, 372.0f);
                    if (S.ly_font == 0)
                        draw_text_glow(tx, 200, cy - 14.0f * sc, sc, C2D_AlignCenter, mix_rgb(th_acc, th_tx, 60), th_glow, 0.45f, 1.0f, 372);
                    else
                        ly_draw_text(tx, 200, cy - 14.0f * ly_font_scale(sc), sc, C(mix_rgb(th_acc, th_tx, 60)), C2D_AlignCenter, 372);
                }
            } else {
                const Rgb dim = mix_rgb(mix_rgb(th_tx, th_bg, 55), th_bg, (int)((1.0f - a) * 255.0f));
                if (!ly_gothic_line(tx, 200, cy, 0.60f, dim, 0.0f, 372.0f))
                    ly_draw_text(tx, 200, cy - 14.0f * ly_font_scale(0.46f), 0.46f, Ca(mix_rgb(th_tx, th_bg, 55), a), C2D_AlignCenter, 372);
            }
        }
    } else {
        const float max_off = n > 7 ? (float)(n - 7) : 0.0f;
        /* Plain text is advanced gently so long lyrics remain readable without
         * requiring the bottom screen to become a separate lyrics UI. */
        if (max_off > 0.0f) {
            const float span = max_off + 2.0f;
            ly_goal = fmodf((float)(osGetTime() % (u64)60000) / 7000.0f, span);
            if (ly_goal > max_off) ly_goal = max_off;
        } else ly_goal = 0.0f;
        if (!S.ly_animated || S.saver) ly_pos = ly_goal; else ly_pos += (ly_goal - ly_pos) * 0.08f;
        for (int i = (int)floorf(ly_pos); i < n && i < (int)ly_pos + 9; i++) {
            const float y = 38.0f + ((float)i - ly_pos) * 23.0f;
            float a = fminf((y - 28.0f) / 12.0f, (206.0f - y) / 12.0f);
            if (a <= 0.02f) continue;
            if (a > 1.0f) a = 1.0f;
            if (!ly_gothic_line(lyrics_text(i), 200, y + 10.0f, 0.60f, mix_rgb(th_bg, th_tx, (int)(a * 255.0f)), 0.0f, 366.0f))
                ly_draw_text(lyrics_text(i), 200, y, 0.50f, Ca(th_tx, a), C2D_AlignCenter, 366);
        }
        if (n > 7) {                                                   /* scroll position */
            rrect(391, 36, 4, 164, 2, 0.08f, cTrk);
            const float th = 164.0f * 7.0f / (float)n, ty = 36.0f + (164.0f - th) * (ly_pos / max_off);
            rrect(391, ty, 4, th < 10.0f ? 10.0f : th, 2, 0.09f, cAcc);
        }
    }

    /* ---- seek bar (same as the player view) ---- */
    const u32 dur = track_loaded ? cur.duration_ms : 0;
    const float frac = progress_frac(pos, dur);
    const float bx0 = 66.0f, bw = 268.0f, by = 226.0f;
    char tb[24];
    if (track_loaded) {
        fmt_time(tb, sizeof tb, pos);
        row_text(tb, 26, by - 8, 16, 0.40f, cDim, C2D_AlignLeft, 38);
        fmt_time(tb, sizeof tb, dur);
        row_text(tb, 374, by - 8, 16, 0.40f, cDim, C2D_AlignRight, 38);
    }
    rrect(bx0 - 2, by - 5, bw + 4, 10, 5, 0.06f, cGlow);
    rrect(bx0, by - 3, bw, 6, 3, 0.08f, cTrk);
    float fw = bw * frac; if (fw < 6) fw = 6;
    rrect(bx0, by - 3, fw, 6, 3, 0.09f, cAcc);
    C2D_DrawCircleSolid(bx0 + bw * frac, by, 0.13f, 5.4f, cAcc);
    C2D_DrawCircleSolid(bx0 + bw * frac, by, 0.14f, 2.6f, C(th_bg));
}

/* ====================================================================== bottom-screen UI
 * Layout (320 x 240):   header 5..31 | tabs 36..60 | song list 66..190 | deck 194..236
 * Layering (citro2d depth: higher z = in front, equal z = draw order, text is always z 0.5):
 *   0.10 frame   0.20 panels / rims / insets   0.22 buttons   0.26 press flash
 *   0.30 widgets   0.45 icons   0.46 toast
 */
static void draw_frame(void)                  /* neon outline + dark fill for the whole bottom screen */
{
    neon_frame(BOT_W, SCR_H, 0.10f);
}

static void draw_toast(void)                  /* status pill above the deck; sits in front of the list text */
{
    if (toast_t <= 0) return;
    const float w = 204, x = (BOT_W - w) / 2, h = 22;
    float y = 168;
    if (!S.saver) {                                    /* slide up on arrival, sink back on the way out */
        if (toast_t > 67) y += (float)(toast_t - 67) * 1.6f;
        else if (toast_t < 8) y += (float)(8 - toast_t) * 1.6f;
    }
    neon_box(x, y, w, h, 11, 0.55f, C(th_acc), neon_glow(), C(mix_rgb(th_bg, th_acc, 60)));
    text_z = 0.6f;                              /* text is normally z 0.5, which would show list text through the pill */
    row_text(toast_msg, BOT_W / 2.0f, y, h, 0.44f, C(th_tx), C2D_AlignCenter, w - 18);
    text_z = 0.5f;
}

/* Now Playing | shuffle - prev - play/pause - next - repeat | volume.  Touch zones: handle_ctrl_tap. */
static void draw_deck(void)
{
    u32 cText = C(th_tx), cAcc = C(th_acc), cBg = C(th_bg);
    u32 cDim  = C(mix_rgb(th_tx, th_bg, 120));
    u32 cGlow = neon_glow();
    u32 cFill = C(mix_rgb(th_bg, th_acc, 8));
    u32 cTrk  = C(mix_rgb(th_bg, th_acc, 75));
    u32 cPale = C(mix_rgb(th_acc, th_tx, 90));      /* prev / next */
    u32 cOff  = C(mix_rgb(th_acc, th_bg, 130));       /* shuffle / repeat when off */
    const float y = CTRL_TOP, h = 42, cy = y + h / 2;
    const bool playing = audio_is_active() && !audio_is_paused();
    char buf[32];

    /* --- Now Playing capsule: label, title, seek line --- */
    u32 pos = track_loaded ? cur_pos_ms() : 0;
    u32 dur = track_loaded ? cur.duration_ms : 0;
    float frac = slider_drag ? slider_frac : progress_frac(pos, dur);
    neon_box(6, y, 114, h, 13, 0.20f, cAcc, cGlow, cFill);
    icon_cd(23, y + 18, 10.0f, 0.45f);
    char sub[48];
    if (slider_drag) {
        char a[16], b[16];
        fmt_time(a, sizeof a, (u32)(slider_frac * (float)dur));
        fmt_time(b, sizeof b, dur);
        snprintf(sub, sizeof sub, "%s / %s", a, b);
    } else {
        snprintf(sub, sizeof sub, "%s", track_loaded ? cur.artist : "Pick a song");
    }
    const char *title = track_loaded ? (cur.title[0] ? cur.title : base_name(cur_path)) : "Not playing";
    draw_text(title, 38, y + 2, 0.50f, track_loaded ? cAcc : cText, C2D_AlignLeft, 78);
    if (sub[0]) draw_text(sub, 38, y + 18, 0.34f, cDim, C2D_AlignLeft, 78);

    const float tw = SL_X1 - SL_X0, ty = y + 33;
    rrect(SL_X0, ty - 2, tw, 4, 2, 0.30f, cTrk);
    float fw = tw * frac; if (fw < 4) fw = 4;
    rrect(SL_X0, ty - 2, fw, 4, 2, 0.32f, cAcc);
    if (track_loaded) C2D_DrawCircleSolid(SL_X0 + tw * frac, ty, 0.36f, slider_drag ? 5.0f : 3.6f, cAcc);

    /* --- transport capsule --- */
    neon_box(123, y, 117, h, 21, 0.20f, cAcc, cGlow, cFill);
    neon_disc(182, cy, 14.0f * (1.0f + (pop_k(182) - 1.0f) * 0.35f), 0.22f, cAcc, cGlow, cFill);
    if (ctrl_flash_t > 0) {
        const float fade = (float)ctrl_flash_t / (float)CTRL_FLASH_FRAMES;       /* the press highlight fades out */
        C2D_DrawCircleSolid(ctrl_flash_x, cy, 0.26f, 14, S.saver ? C(mix_rgb(th_bg, th_acc, 90)) : Ca(mix_rgb(th_bg, th_acc, 120), 0.35f + 0.65f * fade));
    }
    icon_shuffle(137, cy, 6 * pop_k(137), S.shuffle ? cAcc : cOff);
    icon_prev(156, cy, 6 * pop_k(156), cPale);
    if (playing) icon_pause(182, cy, 7 * pop_k(182), cAcc); else icon_play(183.5f, cy, 7 * pop_k(182), cAcc);
    icon_next(208, cy, 6 * pop_k(208), cPale);
    icon_repeat(227, cy, 6 * pop_k(227), S.repeat ? cAcc : cOff);
    if (S.repeat == 2) {                                     /* repeat-one: small "1" badge */
        C2D_DrawCircleSolid(233.5f, cy - 8, 0.46f, 4.5f, cAcc);
        row_text("1", 233.5f, cy - 12.5f, 9, 0.28f, cBg, C2D_AlignCenter, 0);
    }

    /* --- volume capsule --- */
    neon_box(243, y, 71, h, 21, 0.20f, cAcc, cGlow, cFill);
    icon_speaker(254, cy, vol_pct > 0, cAcc);
    const float vw = VOL_X1 - VOL_X0, vf = (float)vol_pct / 100.0f;
    rrect(VOL_X0, cy - 3, vw, 6, 3, 0.30f, cTrk);
    if (vol_pct > 0) rrect(VOL_X0, cy - 3, vw * vf < 6 ? 6 : vw * vf, 6, 3, 0.32f, cAcc);
    C2D_DrawCircleSolid(VOL_X0 + vw * vf, cy, 0.36f, vol_drag ? 6.0f : 4.6f, cAcc);
    C2D_DrawCircleSolid(VOL_X0 + vw * vf, cy, 0.38f, 2.0f, cBg);
}

static void draw_browser(void)
{
    u32 cText = C(th_tx), cAcc = C(th_acc), cBg = C(th_bg);
    u32 cDim  = C(mix_rgb(th_tx, th_bg, 120));
    u32 cGlow = neon_glow();
    u32 cFill = C(mix_rgb(th_bg, th_acc, 8));
    u32 cRowSel  = C(mix_rgb(th_bg, th_acc, 38));          /* highlighted row: dark green */
    u32 cRowFill = C(mix_rgb(th_bg, th_tx, 10));            /* other rows: dark grey */
    u32 cGrey    = C(mix_rgb(th_bg, th_tx, 42));            /* grey rim */
    u32 cGreyGl  = C(mix_rgb(th_bg, th_tx, 14));
    u32 cHeart   = C(mix_rgb(th_tx, th_bg, 70));
    char buf[32];

    /* ---- tabs: All Songs | Liked Songs | Queue (Queue opens Up Next) ---- */
    static const char *TABS[3] = { "All Songs", "Liked Songs", "Queue" };
    for (int t = 0; t < 3; t++) {
        const float x = 8.0f + t * 103.0f, ty = 6, tw = 98, th = 24;
        bool on = (t == 0 && !L_is_favorites) || (t == 1 && L_is_favorites);
        u32 ic = on ? cBg : cDim;
        if (on) {
            rrect(x - 1.5f, ty - 1.5f, tw + 3, th + 3, 13.5f, 0.20f, cGlow);
            rrect(x, ty, tw, th, 12, 0.222f, cAcc);
        } else {
            neon_box(x, ty, tw, th, 12, 0.20f, cGrey, cGreyGl, cFill);
        }
        if (t == 0) icon_small_note(x + 9, ty + 4, ic);
        else if (t == 1) { if (on) icon_heart(x + 16, ty + 11.5f, 5.0f, ic); else icon_heart_line(x + 16, ty + 11.5f, 5.0f, ic, cFill); }
        else icon_list(x + 16, ty + 12, ic);
        row_text(TABS[t], x + 58, ty, th, 0.38f, on ? cBg : cDim, C2D_AlignCenter, 66);
    }
    if (manual_queue_count > 0) {                           /* badge: songs waiting in Up Next */
        C2D_DrawCircleSolid(8 + 2 * 103 + 92, 8, 0.46f, 6.5f, cAcc);
        snprintf(buf, sizeof buf, "%d", manual_queue_count);
        row_text(buf, 8 + 2 * 103 + 92, 1.5f, 13, 0.30f, cBg, C2D_AlignCenter, 0);
    }

    /* ---- search bar, full width under the tabs ---- */
    neon_box(8, 35, 230, 26, 13, 0.20f, cAcc, cGlow, cFill);
    icon_search(26, 48, cDim, cFill);
    if (L_is_search) {
        row_text(search_q, 40, 35, 26, 0.42f, cText, C2D_AlignLeft, 160);
        C2D_DrawLine(212, 43, cDim, 222, 53, cDim, 1.8f, 0.45f);
        C2D_DrawLine(222, 43, cDim, 212, 53, cDim, 1.8f, 0.45f);
    } else {
        row_text("Search Music...", 40, 35, 26, 0.42f, cDim, C2D_AlignLeft, 160);
    }
    /* LYRICS: opens the lyrics screen for the song that is loaded */
    if (track_loaded && lyrics_open) {
        /* Active state belongs to the LYRICS control itself; the rest of the bottom
         * screen keeps its normal colours and layout. */
        neon_box(246, 35, 66, 26, 13, 0.30f, cAcc, cGlow, C(mix_rgb(th_bg, th_acc, 42)));
        row_text("LYRICS", 279, 35, 26, 0.40f, cBg, C2D_AlignCenter, 58);
    } else if (track_loaded) {
        neon_box(246, 35, 66, 26, 13, 0.20f, cAcc, cGlow, cFill);
        row_text("LYRICS", 279, 35, 26, 0.40f, cAcc, C2D_AlignCenter, 58);
    } else {
        neon_box(246, 35, 66, 26, 13, 0.20f, cGrey, cGreyGl, cFill);
        row_text("LYRICS", 279, 35, 26, 0.40f, cDim, C2D_AlignCenter, 58);
    }

    /* ---- song list ---- */
    const float RX = 8, RW = 290, RH = ROW_H - 4;
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = scroll_top + i;
        if (idx >= L.n) break;
        Entry *e = &L.e[idx];
        float y = LIST_TOP + i * ROW_H;
        bool cur_row = is_current_row(idx);
        bool playing = cur_row && audio_is_active() && !audio_is_paused();
        bool selected = (idx == sel);
        u32 rowfill = selected ? cRowSel : cRowFill;

        if (selected) neon_box(RX, y, RW, RH, 11, 0.20f, cAcc, cGlow, cRowSel);
        else          neon_box(RX, y, RW, RH, 11, 0.20f, cGrey, cGreyGl, cRowFill);

        /* cover thumbnail (or a tile while it is still loading / for folders) */
        const float ts = 30, tx = RX + 7, tyy = y + (RH - ts) / 2;
        C3D_Tex *tex = NULL;
        if (!e->is_dir) {
            if (cur_row && art_ready()) tex = art_image()->tex;
            else { float fd; tex = flow_tex(idx, &fd); }
        }
        if (tex) {
            draw_thumb(tex, tx, tyy, ts, 0.30f);
        } else {
            Rgb tc = e->is_dir ? mix_rgb(th_sel, th_tx, 60) : tile_color(e->name);
            rrect(tx, tyy, ts, ts, 6, 0.30f, C(tc));
            if (e->is_dir) {
                C2D_DrawRectSolid(tx + 6, tyy + 9, 0.45f, 8, 4, cText);
                C2D_DrawRectSolid(tx + 6, tyy + 12, 0.45f, 18, 12, cText);
            } else if (!playing) {
                icon_small_note(tx + 8.5f, tyy + 7, cText);
            }
        }
        if (playing) {                                      /* animated bars over the thumbnail */
            if (tex) C2D_DrawRectSolid(tx, tyy + ts - 14, 0.42f, ts, 14, C2D_Color32(0, 0, 0, 170));
            for (int k = 0; k < 3; k++) {
                float hb = 3.0f + 9.0f * (0.5f + 0.5f * sinf(eq_phase * 2.0f + k * 1.7f));
                C2D_DrawRectSolid(tx + 7 + k * 7, tyy + ts - 3 - hb, 0.45f, 4, hb, cAcc);
            }
        }

        const float lx = RX + 46;
        if (e->is_dir) {
            row_text(!strcmp(e->name, "..") ? "Back" : e->name, lx, y, RH, 0.60f, selected ? cAcc : cText, C2D_AlignLeft, 232);
            continue;
        }
        const char *name = (e->meta_loaded && e->title[0]) ? e->title : base_name(e->name);
        u32 tcol = (selected || cur_row) ? cAcc : cText;
        if (e->meta_loaded && e->artist[0]) {
            draw_text(name, lx, y + 3, 0.58f, tcol, C2D_AlignLeft, 170);
            draw_text(e->artist, lx, y + 22, 0.42f, cDim, C2D_AlignLeft, 170);
        } else {
            row_text(name, lx, y, RH, 0.58f, tcol, C2D_AlignLeft, 170);
        }
        u32 dms = (cur_row && track_loaded && cur.duration_ms) ? cur.duration_ms : e->duration_ms;
        if (dms) {
            fmt_time(buf, sizeof buf, dms);
            row_text(buf, 264, y, RH, 0.48f, cText, C2D_AlignRight, 40);
        }
        char favpath[256]; entry_relative_path(e, favpath, sizeof favpath);
        if (is_favorite(favpath)) icon_heart(283, y + RH / 2, 6.6f, cAcc);
        else                      icon_heart_line(283, y + RH / 2, 6.6f, cHeart, rowfill);
    }

    if (L.n == 0) {
        draw_text(L_is_favorites ? "No liked songs yet" : "No songs here", 160, 100, 0.55f, cText, C2D_AlignCenter, 290);
        draw_text(L_is_favorites ? "Tap a heart on any song to add it" : "Copy music to SD:/Music/", 160, 124, 0.38f, cDim, C2D_AlignCenter, 290);
    }

    /* ---- scrollbar: arrows (tap) + thumb ---- */
    const float sbx = 303, sbw = 12, sbh = VIS_ROWS * ROW_H - 4;
    neon_box(sbx, LIST_TOP, sbw, sbh, 6, 0.20f, cGrey, cGreyGl, cRowFill);
    C2D_DrawTriangle(sbx + 3, LIST_TOP + 11, cAcc, sbx + sbw - 3, LIST_TOP + 11, cAcc, sbx + sbw / 2, LIST_TOP + 5, cAcc, 0.45f);
    C2D_DrawTriangle(sbx + 3, LIST_TOP + sbh - 11, cAcc, sbx + sbw - 3, LIST_TOP + sbh - 11, cAcc, sbx + sbw / 2, LIST_TOP + sbh - 5, cAcc, 0.45f);
    {
        const float y0 = LIST_TOP + 15, rng = sbh - 30;
        float th = (L.n > VIS_ROWS) ? rng * VIS_ROWS / (float)L.n : rng;
        if (th < 12) th = 12;
        float thy = y0 + ((L.n > VIS_ROWS) ? (rng - th) * (float)scroll_top / (float)(L.n - VIS_ROWS) : 0.0f);
        rrect(sbx + 3, thy, sbw - 6, th, 3, 0.30f, cAcc);
    }

    draw_deck();
}

/* ------------------------------------------------------------- lyrics (bottom screen) */
static void draw_lyrics_bottom(void)
{
    const u32 cText = C(th_tx), cAcc = C(th_acc), cBg = C(th_bg);
    const u32 cDim = C(mix_rgb(th_tx, th_bg, 120));
    const u32 cGlow = neon_glow(), cFill = C(mix_rgb(th_bg, th_acc, 8));
    const u32 cGrey = C(mix_rgb(th_bg, th_tx, 42)), cGreyGl = C(mix_rgb(th_bg, th_tx, 14));
    const LyState st = lyrics_state();
    const bool ready = (st == LY_READY);
    const char *title = cur.title[0] ? cur.title : base_name(cur_path);
    const char *artist = cur.artist[0] ? cur.artist : "Unknown artist";

    draw_text("Lyrics", 14, 2, 0.85f, cText, C2D_AlignLeft, 180);
    rrect(250, 6, 62, 22, 11, 0.30f, cAcc);
    row_text("Back", 281, 6, 22, 0.45f, cBg, C2D_AlignCenter, 0);

    /* Clean song header: one compact card, no duplicated status prose. */
    neon_box(8, 44, 304, 48, 12, 0.18f, cAcc, cGlow, cFill);
    ly_draw_text(title, 18, 48, 0.52f, cAcc, C2D_AlignLeft, 248);
    ly_draw_text(artist, 18, 66, 0.38f, cText, C2D_AlignLeft, 248);
    const char *chip = ready ? (lyrics_synced() ? "SYNC" : "TEXT") :
                       (st == LY_LOADING ? "SEARCH" : (st == LY_OFFLINE ? "OFFLINE" : ""));
    if (chip[0]) row_text(chip, 292, 48, 22, 0.36f, cAcc, C2D_AlignRight, 50);

    /* Primary action is always in the same place, so it is fast to reach. */
    if (!ready) {
        const bool can = !S.ly_offline && st != LY_LOADING;
        const char *lab = S.ly_offline ? "Offline only" : (st == LY_LOADING ? "Searching..." : "Search online  (A)");
        neon_box(8, 100, 304, 34, 17, 0.20f, can ? cAcc : cGrey, can ? cGlow : cGreyGl, cFill);
        row_text(lab, 160, 100, 34, 0.46f, can ? cAcc : cDim, C2D_AlignCenter, 280);
    } else {
        const char *hint = lyrics_synced() ? "Synced to playback" : "D-pad Up / Down to scroll";
        ly_draw_text(hint, 160, 104, 0.40f, cDim, C2D_AlignCenter, 296);
    }

    /* Source/status line and a single settings entry keep the screen uncluttered. */
    char status[96];
    if (ready) {
        const char *src = lyrics_source();
        if (!strcmp(src, "cache")) src = "SD cache";
        else if (!strcmp(src, "file")) src = ".lrc file";
        snprintf(status, sizeof status, "%s  •  %s", lyrics_synced() ? "Synced" : "Plain text", src);
    } else {
        const char *sub;
        snprintf(status, sizeof status, "%s", ly_state_msg(st, &sub));
    }
    ly_draw_text(status, 160, 140, 0.34f, cDim, C2D_AlignCenter, 296);
    neon_box(8, 154, 304, 28, 14, 0.16f, cGrey, cGreyGl, cFill);
    row_text("Lyrics settings  (SELECT)", 160, 154, 28, 0.42f, cText, C2D_AlignCenter, 280);

    draw_deck();
}

/* ------------------------------------------------------- first-run setup */
static void start_wizard(void)
{
    wizard_open = true;
    lyrics_open = false;
    settings_open = false;
    wz_page = 0;
    set_drag = -1;
}

static void wz_enter(int page)
{
    wz_page = page;
    set_drag = -1;
    if (WZ_CARD[page] >= 0) set_row = CARD_ROWS[WZ_CARD[page]][0];
}

static void wz_finish(void)
{
    wizard_open = false;
    set_drag = -1;
    S.setup_done = true;
    flow_show = true;
    settings_dirty = !settings_save(&S);
}

static void wz_next(void) { if (wz_page >= WZ_N - 1) wz_finish(); else wz_enter(wz_page + 1); }
static void wz_back(void) { if (wz_page > 0) wz_enter(wz_page - 1); }

static void wz_line(const char *key, const char *desc, float y, u32 ck, u32 cd)
{
    if (key[0]) draw_text(key, 36, y, 0.5f, ck, C2D_AlignLeft, 76);
    draw_text(desc, 118, y, 0.5f, cd, C2D_AlignLeft, 260);
}

static void draw_wizard_top(void)         /* welcome and controls pages (the setup page shows the live player) */
{
    u32 cText = C(th_tx), cAcc = C(th_acc);
    u32 cDim = C(mix_rgb(th_tx, th_bg, 120));
    u32 cCard = C(mix_rgb(th_bg, th_sel, 150));

    rrect(14, 12, 372, 214, 16, 0.20f, cCard);
    C2D_DrawRectSolid(30, 12, 0.21f, 340, 1, C(mix_rgb(th_bg, th_tx, 30)));

    if (wz_page == WZ_WELCOME) {
        draw_text("Welcome to", 200, 26, 0.55f, cDim, C2D_AlignCenter, 0);
        draw_text("PROJECT NOISE", 200, 48, 0.95f, cAcc, C2D_AlignCenter, 360);
        draw_text("A native music player for MP3, FLAC and WAV.", 200, 98, 0.5f, cText, C2D_AlignCenter, 350);
        draw_text("Two quick steps and you're listening.", 200, 124, 0.5f, cDim, C2D_AlignCenter, 350);
        draw_text("You can change everything later in Settings (SELECT).", 200, 150, 0.42f, cDim, C2D_AlignCenter, 350);
        draw_text("Press A or tap Next to begin", 200, 192, 0.42f, cAcc, C2D_AlignCenter, 0);
    } else {
        char skip[48];
        snprintf(skip, sizeof skip, "Left / Right: back / forward %d sec", S.seek_step);
        draw_text("Controls", 200, 18, 0.75f, cAcc, C2D_AlignCenter, 0);
        wz_line("A",      "Play song / open folder",          44,  cAcc, cText);
        wz_line("B",      "Pause",                            63,  cAcc, cText);
        wz_line("L / R",  "Previous / next song",             82,  cAcc, cText);
        wz_line("D-pad",  skip,                               101, cAcc, cText);
        wz_line("",       "Up / Down: move through the list", 120, cAcc, cText);
        wz_line("X / Y",  "Play next / Add to queue",         139, cAcc, cText);
        wz_line("ZL",     "Favorite / unfavorite a song",     158, cAcc, cText);
        wz_line("SELECT", "Open Settings",                    177, cAcc, cText);
        wz_line("START",  "Exit the app",                     196, cAcc, cText);
    }
}

/* ------------------------------------------------------- battery / wallpaper */
static void batt_poll(void)                 /* called about every 2 s while the battery readout is on */
{
    u8 lvl = 0, chg = 0;
    if (mcu_ok && R_SUCCEEDED(MCUHWC_GetBatteryLevel(&lvl))) {
        batt_pct = lvl > 100 ? 100 : (int)lvl;
    } else if (ptm_ok && R_SUCCEEDED(PTMU_GetBatteryLevel(&lvl))) {      /* coarse fallback: 0..5 */
        batt_pct = lvl > 5 ? 100 : (int)lvl * 20;
    }
    if (ptm_ok && R_SUCCEEDED(PTMU_GetBatteryChargeState(&chg))) batt_chg = chg != 0;
}

static const char *UI_WALLPAPER_NAMES[5] = { "Plain", "Scanlines", "Dots", "Corners", "Grid" };
static const char *UI_DISPLAY_NAMES[4]   = { "Cover", "LED Spectrum", "Scope", "VU Deck" };
static const char *UI_BATT_NAMES[2]      = { "Icon", "Percent" };
static const char *UI_CLOCK_NAMES[3]     = { "Off", "12-hour", "24-hour" };
static const char *UI_LY_FONT_NAMES[4]   = { "Gothic", "Bold", "Compact", "System" };
static const char *UI_KB_FONT_NAMES[4]   = { "Gothic", "Bold", "System", "Compact" };

/* ------------------------------------------------------------ settings UI */
static const char *row_label(int r)
{
    switch (r) {
    case ROW_PRESET:       return "Theme";
    case ROW_GLOW:         return "Ambient glow";
    case ROW_FADE:         return "Cover fade";
    case ROW_DISPLAY:      return "Visualizer";
    case ROW_EQMAN:        return "Manual EQ";
    case ROW_EQPRE:        return "EQ preset";
    case ROW_SEEK:         return "Skip length";
    case ROW_CLOCK:        return "Clock";
    case ROW_BATT:         return "Battery";
    case ROW_BATTSTYLE:    return "Battery style";
    case ROW_OLED:         return "OLED mode";
    case ROW_GLOWCOL:      return "Glow color";
    case ROW_DYN:          return "Album colors";
    case ROW_INFO:         return "Track info";
    case ROW_RESUME:       return "Remember song";
    case ROW_SAVER:        return "Battery saver";
    case ROW_SLEEP_ENABLE: return "Sleep timer";
    case ROW_SLEEP_TIMER:  return "Timer length";
    case ROW_GUIDE:        return "Replay guide";
    case ROW_LY_SEARCH:    return "Automatic search";
    case ROW_LY_DL:        return "Auto download";
    case ROW_LY_OFFLINE:   return "Offline only";
    case ROW_LY_SYNC:      return "Prefer synced";
    case ROW_LY_ANIM:      return "Animated lyrics";
    case ROW_LY_FONT:      return "Lyrics font";
    case ROW_KB_FONT:      return "Keyboard font";
    case ROW_LY_CLEAR:     return "Clear lyrics cache";
    case ROW_EXIT:         return "Exit app";
    }
    return "";
}

static void row_value(int r, char *out, size_t cap)
{
    const char *s = "";
    out[0] = 0;
    switch (r) {
    case ROW_PRESET:    s = (S.preset >= 0 && S.preset < PRESET_COUNT) ? PRESET_NAMES[S.preset] : "Custom"; break;
    case ROW_GLOW:      s = GLOW_NAMES[(unsigned)S.glow % GLOW_COUNT]; break;
    case ROW_FADE:      s = FADE_NAMES[(unsigned)S.fade_speed % FADE_COUNT]; break;
    case ROW_DISPLAY:   s = UI_DISPLAY_NAMES[(unsigned)S.display_mode % 4]; break;
    case ROW_EQPRE:     s = EQ_PRESET_NAMES[(unsigned)S.eq_preset % EQ_PRESET_COUNT]; break;
    case ROW_CLOCK:     s = UI_CLOCK_NAMES[(unsigned)S.clock_mode % 3]; break;
    case ROW_GLOWCOL:   s = GLOWCOL_NAMES[(unsigned)S.glow_color % GLOWCOL_COUNT]; break;
    case ROW_BATTSTYLE: s = UI_BATT_NAMES[(unsigned)S.batt_style % 2]; break;
    case ROW_LY_FONT:   s = UI_LY_FONT_NAMES[(unsigned)S.ly_font % 4]; break;
    case ROW_KB_FONT:   s = UI_KB_FONT_NAMES[(unsigned)S.kb_font % 4]; break;
    case ROW_SEEK:      snprintf(out, cap, "%d s", S.seek_step); return;
    case ROW_SLEEP_TIMER: snprintf(out, cap, "%d min", S.sleep_minutes); return;
    default: break;
    }
    snprintf(out, cap, "%s", s);
}

static bool row_switch_on(int r)
{
    if (r == ROW_BATT)         return S.show_batt;
    if (r == ROW_OLED)         return S.oled;
    if (r == ROW_DYN)          return S.dyn_color;
    if (r == ROW_INFO)         return S.show_info;
    if (r == ROW_RESUME)       return S.resume;
    if (r == ROW_SAVER)        return S.saver;
    if (r == ROW_LY_SEARCH)    return S.ly_search;
    if (r == ROW_LY_DL)        return S.ly_download;
    if (r == ROW_LY_OFFLINE)   return S.ly_offline;
    if (r == ROW_LY_SYNC)      return S.ly_sync;
    if (r == ROW_LY_ANIM)      return S.ly_animated;
    if (r == ROW_EQMAN)        return S.eq_manual;
    if (r == ROW_SLEEP_ENABLE) return sleep_timer_active;
    return false;
}

/* the knob glides between its two positions (sw_anim eases once per drawn frame; instant in Battery saver) */
static float sw_ease(int r, bool on)
{
    const float t = on ? 1.0f : 0.0f;
    if (!sw_init[r] || S.saver) { sw_init[r] = true; sw_anim[r] = t; return t; }
    sw_anim[r] += (t - sw_anim[r]) * 0.32f;
    if (fabsf(t - sw_anim[r]) < 0.02f) sw_anim[r] = t;
    return sw_anim[r];
}

static void draw_switch(float x, float cy, float t, u32 cOn, u32 cOff, u32 cKnobOn, u32 cKnobOff)
{
    rrect(x, cy - 7, 30, 14, 7, 0.30f, t > 0.5f ? cOn : cOff);
    C2D_DrawCircleSolid(x + 7.0f + 16.0f * t, cy, 0.34f, 5, t > 0.5f ? cKnobOn : cKnobOff);
}

static void draw_arrow(float x, float cy, int dir, u32 c)     /* small triangle, dir -1 = left, +1 = right */
{
    C2D_DrawTriangle(x + dir * 3.0f, cy, c, x - dir * 2.0f, cy - 4, c, x - dir * 2.0f, cy + 4, c, 0.34f);
}

/* A list of settings rows drawn as one inset panel. Rows start at y = 70 (card_touch uses the same grid). */
static void draw_card(int card)
{
    u32 cText = C(th_tx), cAcc = C(th_acc), cSel = C(th_sel), cBg = C(th_bg);
    u32 cDim = C(mix_rgb(th_tx, th_bg, 120));
    u32 cCard = C(mix_rgb(th_bg, th_sel, 150));
    u32 cDanger = C2D_Color32(255, 90, 95, 255);
    const float rh = CARD_ROWH[card];
    const int n = CARD_LEN[card];
    char val[48];

    rrect(6, 66, 308, n * rh + 8, 10, 0.26f, cCard);
    for (int i = 0; i < n; i++) {
        int r = CARD_ROWS[card][i];
        float y = 70.0f + i * rh;
        bool sel = (r == set_row);
        if (sel) rrect(9, y, 302, rh, 8, 0.28f, cSel);
        else if (i > 0) C2D_DrawRectSolid(18, y, 0.28f, 284, 1, cSel);

        row_text(row_label(r), 18, y, rh, 0.45f, (r == ROW_EXIT || r == ROW_LY_CLEAR) ? cDanger : cText, C2D_AlignLeft, is_switch_row(r) ? 190 : 128);

        if (is_switch_row(r)) {
            if (r == ROW_SLEEP_ENABLE && sleep_timer_active) {          /* live countdown beside the label */
                s64 left = (s64)sleep_timer_deadline - (s64)osGetTime();
                fmt_time(val, sizeof val, left > 0 ? (u32)left : 0);
                row_text(val, 140, y, rh, 0.42f, cAcc, C2D_AlignLeft, 90);
            }
            draw_switch(272, y + rh / 2, sw_ease(r, row_switch_on(r)), cAcc, sel ? cBg : cSel, cBg, cDim);
        } else if (is_choice_row(r)) {
            float ph = rh - 6.0f; if (ph > 20.0f) ph = 20.0f;
            float py = y + (rh - ph) / 2;
            rrect(150, py, 156, ph, ph / 2, 0.30f, cBg);
            draw_arrow(162, py + ph / 2, -1, cAcc);
            draw_arrow(294, py + ph / 2, +1, cAcc);
            row_value(r, val, sizeof val);
            row_text(val, 228, py, ph, 0.40f, cText, C2D_AlignCenter, 100);
            if (r == ROW_GLOWCOL) {                                    /* swatch of the chosen halo colour */
                Rgb sw = th_acc;
                if (S.glow_color > 0) { const unsigned g = GLOWCOL_RGB[S.glow_color]; sw.r = (g >> 16) & 0xFF; sw.g = (g >> 8) & 0xFF; sw.b = g & 0xFF; }
                C2D_DrawCircleSolid(182, py + ph / 2, 0.34f, 4.5f, C(sw));
            }
        } else if (r == ROW_GUIDE || r == ROW_EXIT || r == ROW_LY_CLEAR) {
            draw_arrow(298, y + rh / 2, +1, (r == ROW_EXIT || r == ROW_LY_CLEAR) ? cDanger : cAcc);
        }
    }
}

/* Audio tab: Manual-EQ switch + preset chooser, then five vertical band sliders (touch grid: see sound_touch). */
static void draw_sound(void)
{
    u32 cText = C(th_tx), cAcc = C(th_acc), cSel = C(th_sel), cBg = C(th_bg);
    u32 cDim = C(mix_rgb(th_tx, th_bg, 120));
    u32 cCard = C(mix_rgb(th_bg, th_sel, 150));
    static const char *FREQ[EQ_BANDS] = { "60", "230", "910", "3.6k", "14k" };
    const float ch = SND_CTL_H - 2;
    char buf[16];

    /* left chip: Manual EQ switch */
    rrect(8, SND_CTL_Y, 148, ch, 11, 0.26f, set_row == ROW_EQMAN ? cSel : cCard);
    row_text("Manual EQ", 20, SND_CTL_Y, ch, 0.45f, cText, C2D_AlignLeft, 90);
    draw_switch(120, SND_CTL_Y + ch / 2, sw_ease(ROW_EQMAN, S.eq_manual), cAcc, cBg, cBg, cDim);

    /* right chip: preset chooser (locked while the manual EQ is on) */
    rrect(160, SND_CTL_Y, 152, ch, 11, 0.26f, set_row == ROW_EQPRE ? cSel : cCard);
    u32 pc = S.eq_manual ? cDim : cAcc;
    draw_arrow(172, SND_CTL_Y + ch / 2, -1, pc);
    draw_arrow(300, SND_CTL_Y + ch / 2, +1, pc);
    row_value(ROW_EQPRE, buf, sizeof buf);
    row_text(buf, 236, SND_CTL_Y, ch, 0.42f, S.eq_manual ? cDim : cText, C2D_AlignCenter, 100);

    /* slider panel */
    rrect(6, SND_EQ_Y + 2, 308, SND_BOTTOM - SND_EQ_Y - 2, 10, 0.26f, cCard);
    const float ymid = (EQ_TRK_Y0 + EQ_TRK_Y1) / 2.0f;
    draw_text("+12", 34, EQ_TRK_Y0 - 7, 0.30f, cDim, C2D_AlignRight, 0);
    draw_text("0",   34, ymid - 7,      0.30f, cDim, C2D_AlignRight, 0);
    draw_text("-12", 34, EQ_TRK_Y1 - 7, 0.30f, cDim, C2D_AlignRight, 0);

    for (int b = 0; b < EQ_BANDS; b++) {
        float cx = EQ_COL_X0 + b * EQ_COL_W + EQ_COL_W / 2.0f;
        bool sel = (set_row == ROW_B0 + b);
        int v = S.eq_manual ? S.eq_user[b] : S.eq[b];
        float f = (float)(v + 12) / 24.0f;
        float ky = EQ_TRK_Y1 - f * (EQ_TRK_Y1 - EQ_TRK_Y0);
        u32 cOn = S.eq_manual ? cAcc : cDim;

        if (sel) rrect(cx - 24, SND_EQ_Y + 6, 48, SND_BOTTOM - SND_EQ_Y - 10, 8, 0.28f, cSel);

        snprintf(buf, sizeof buf, "%+d", v);
        draw_text(buf, cx, EQ_TRK_Y0 - 28, 0.42f, v ? cOn : cDim, C2D_AlignCenter, 0);

        rrect(cx - 3, EQ_TRK_Y0, 6, EQ_TRK_Y1 - EQ_TRK_Y0, 3, 0.30f, sel ? cBg : cSel);
        C2D_DrawRectSolid(cx - 9, ymid, 0.31f, 18, 1, cDim);
        if (ky < ymid) C2D_DrawRectSolid(cx - 3, ky, 0.32f, 6, ymid - ky, cOn);
        else           C2D_DrawRectSolid(cx - 3, ymid, 0.32f, 6, ky - ymid, cOn);
        C2D_DrawCircleSolid(cx, ky, 0.36f, 8, cOn);
        C2D_DrawCircleSolid(cx, ky, 0.38f, 4.5f, cBg);

        draw_text(FREQ[b], cx, EQ_TRK_Y1 + 7, 0.36f, sel ? cText : cDim, C2D_AlignCenter, 0);
    }
}

static void draw_settings(void)
{
    u32 cText = C(th_tx), cAcc = C(th_acc), cBg = C(th_bg);
    u32 cDim = C(mix_rgb(th_tx, th_bg, 120));
    u32 cCard = C(mix_rgb(th_bg, th_sel, 150));
    int page = row_page(set_row);

    draw_text("Settings", 14, 2, 0.85f, cText, C2D_AlignLeft, 220);
    draw_text(PAGE_NAMES[page], 14, 26, 0.34f, cDim, C2D_AlignLeft, 160);
    rrect(250, 6, 62, 22, 11, 0.30f, cAcc);                      /* Done pill (touch: y < 40, x >= 244) */
    row_text("Done", 281, 6, 22, 0.45f, cBg, C2D_AlignCenter, 0);

    rrect(10, 42, 300, 24, 12, 0.26f, cCard);                    /* segmented control: five 60 px tabs */
    const float tab_target = 10.0f + (float)page * TAB_W;
    if (S.saver || fabsf(tab_target - tab_x) > 200.0f) tab_x = tab_target;
    tab_x += (tab_target - tab_x) * 0.35f;                       /* the highlight slides to the chosen tab */
    if (fabsf(tab_target - tab_x) < 0.4f) tab_x = tab_target;
    rrect(tab_x + 2, 44, TAB_W - 4, 20, 10, 0.30f, cAcc);
    for (int t = 0; t < PAGES; t++)
        row_text(PAGE_NAMES[t], 10.0f + t * TAB_W + TAB_W / 2.0f, 44, 20, 0.40f, t == page ? cBg : cDim, C2D_AlignCenter, TAB_W - 6);

    if (page == 1) draw_sound();
    else draw_card(PAGE_CARD[page]);
}

/* ------------------------------------------------------------------ Up Next */
#define QUEUE_ROWS 4                    /* rows 48..184 fit above the deck at CTRL_TOP */
static void draw_queue(void)
{
    u32 cText = C(th_tx), cAcc = C(th_acc), cSel = C(th_sel), cBg = C(th_bg);
    u32 cDim = C(mix_rgb(th_tx, th_bg, 120));
    char buf[96];

    draw_text("Up Next", 14, 6, 0.7f, cText, C2D_AlignLeft, 150);
    snprintf(buf, sizeof buf, "%d queued", manual_queue_count);
    draw_text(buf, 14, 29, 0.32f, cDim, C2D_AlignLeft, 150);
    rrect(246, 8, 64, 22, 11, 0.30f, cAcc);                      /* Close pill (touch: y < 36, x >= 244) */
    row_text("Close", 278, 8, 22, 0.45f, cBg, C2D_AlignCenter, 0);

    if (manual_queue_count == 0) {
        draw_text("Nothing queued", 160, 54, 0.6f, cText, C2D_AlignCenter, 290);
        draw_text("Press Y to add the selected song to the end", 160, 88, 0.42f, cDim, C2D_AlignCenter, 290);
        draw_text("Press X to play it next", 160, 108, 0.42f, cDim, C2D_AlignCenter, 290);
        draw_text("Tapping a song here plays it now", 160, 128, 0.42f, cDim, C2D_AlignCenter, 290);
    } else {
        if (queue_sel >= manual_queue_count) queue_sel = manual_queue_count - 1;
        if (queue_sel < queue_scroll) queue_scroll = queue_sel;
        if (queue_sel >= queue_scroll + QUEUE_ROWS) queue_scroll = queue_sel - QUEUE_ROWS + 1;
        if (queue_scroll > manual_queue_count - QUEUE_ROWS) queue_scroll = manual_queue_count - QUEUE_ROWS;
        if (queue_scroll < 0) queue_scroll = 0;

        for (int i = 0; i < QUEUE_ROWS; i++) {
            int idx = queue_scroll + i;
            if (idx >= manual_queue_count) break;
            float y = 48.0f + i * 34.0f;
            bool sel = (idx == queue_sel);

            rrect(8, y, 300, 32, 8, 0.26f, sel ? cAcc : cSel);
            rrect(10, y + 2, 296, 28, 6, 0.28f, cBg);

            C2D_DrawCircleSolid(25, y + 16, 0.32f, 9, idx == 0 ? cAcc : cSel);
            snprintf(buf, sizeof buf, "%d", idx + 1);
            draw_text(buf, 25, y + 8, 0.4f, idx == 0 ? cBg : cText, C2D_AlignCenter, 0);

            const char *path = manual_queue[idx];
            size_t rl = strlen(MUSIC_ROOT);
            const char *rel = strncmp(path, MUSIC_ROOT, rl) == 0 ? path + rl : path;
            char title[160], folder[160];
            snprintf(title, sizeof title, "%s", base_name(rel));
            char *dot = strrchr(title, '.');
            if (dot && dot != title) *dot = 0;
            folder_of(rel, folder, sizeof folder);
            draw_text(title, 42, y + 2, 0.45f, sel ? cAcc : cText, C2D_AlignLeft, 228);
            if (folder[0]) draw_text(folder, 42, y + 17, 0.30f, cDim, C2D_AlignLeft, 228);

            C2D_DrawCircleSolid(293, y + 16, 0.32f, 9, cSel);    /* remove button (touch: x >= 278) */
            C2D_DrawLine(289, y + 12, cText, 297, y + 20, cText, 1.6f, 0.36f);
            C2D_DrawLine(297, y + 12, cText, 289, y + 20, cText, 1.6f, 0.36f);
        }
        if (manual_queue_count > QUEUE_ROWS) {
            const float qh = QUEUE_ROWS * 34.0f - 2.0f;
            float th = qh * QUEUE_ROWS / manual_queue_count; if (th < 10) th = 10;
            float ty = 48.0f + (qh - th) * queue_scroll / (manual_queue_count - QUEUE_ROWS);
            rrect(311, 48, 3, qh, 1.5f, 0.30f, cSel);
            rrect(311, ty, 3, th, 1.5f, 0.34f, cAcc);
        }
    }

    draw_deck();
}

static void draw_wizard_bottom(void)
{
    u32 cText = C(th_tx), cAcc = C(th_acc), cSel = C(th_sel), cBg = C(th_bg);
    u32 cDim = C(mix_rgb(th_tx, th_bg, 120));
    u32 cCard = C(mix_rgb(th_bg, th_sel, 150));
    u32 cDanger = C2D_Color32(255, 90, 95, 255);
    char buf[64];

    draw_text(WZ_TITLE[wz_page], 14, 2, 0.85f, cText, C2D_AlignLeft, 220);
    if (wz_page < WZ_N - 1) {
        rrect(250, 6, 62, 22, 11, 0.30f, cSel);
        row_text("Skip", 281, 6, 22, 0.45f, cDim, C2D_AlignCenter, 0);
    }
    for (int i = 0; i < WZ_N; i++) {                          /* step dots */
        float dx = 160.0f + ((float)i - (WZ_N - 1) / 2.0f) * 16.0f;
        C2D_DrawCircleSolid(dx, 46, 0.35f, i == wz_page ? 4.5f : 3.0f, i == wz_page ? cAcc : cSel);
    }

    if (WZ_CARD[wz_page] >= 0) {
        draw_card(WZ_CARD[wz_page]);
    } else if (wz_page == WZ_WELCOME) {                       /* quick checks */
        rrect(6, 62, 308, 96, 10, 0.26f, cCard);
        draw_text("Sound", 18, 70, 0.5f, cText, C2D_AlignLeft, 100);
        draw_text(audio_ok ? "Ready" : "DSP firmware missing", 306, 70, 0.5f, audio_ok ? cAcc : cDanger, C2D_AlignRight, 190);
        if (!audio_ok) draw_text("Run DSP1 once to dump SD:/3ds/dspfirm.cdc", 18, 92, 0.36f, cDim, C2D_AlignLeft, 290);
        C2D_DrawRectSolid(18, 110, 0.29f, 284, 1, cSel);
        draw_text("Music folder", 18, 118, 0.5f, cText, C2D_AlignLeft, 120);
        bool have = is_music_root(L.dir) && L.n > 0;
        if (have) snprintf(buf, sizeof buf, "%d item%s found", L.n, L.n == 1 ? "" : "s");
        else snprintf(buf, sizeof buf, "Empty");
        draw_text(buf, 306, 118, 0.5f, have ? cAcc : cDim, C2D_AlignRight, 170);
        draw_text("SD:/Music/  (.mp3  .flac  .wav)", 18, 138, 0.36f, cDim, C2D_AlignLeft, 290);
        if (!have) draw_text("Copy your songs there from a PC any time.", 160, 170, 0.4f, cDim, C2D_AlignCenter, 300);
    } else {                                                  /* touch guide */
        rrect(6, 62, 308, 118, 10, 0.26f, cCard);
        static const char *T[6] = {
            "Tap a song to play, a folder to open",
            "Drag the list up / down to scroll",
            "Drag the seek bar to jump in a song",
            "Tap the search bar to find a song",
            "Tap Queue to open Up Next",
            "Settings: press SELECT; replay guide there"
        };
        for (int i = 0; i < 6; i++) {
            C2D_DrawCircleSolid(20, 75 + i * 18, 0.30f, 3, cAcc);
            draw_text(T[i], 30, 67 + i * 18, 0.42f, cText, C2D_AlignLeft, 278);
        }
    }

    if (wz_page > 0) {
        rrect(14, 196, 96, 28, 14, 0.30f, cSel);
        row_text("Back", 62, 196, 28, 0.5f, cText, C2D_AlignCenter, 0);
    }
    rrect(210, 196, 96, 28, 14, 0.30f, cAcc);
    row_text(wz_page == WZ_N - 1 ? "Start" : "Next", 258, 196, 28, 0.5f, cBg, C2D_AlignCenter, 0);
}

/* shown for two frames, then the scan / filter runs (the first search reads the whole Music folder) */
static void draw_search_busy(void)
{
    u32 cText = C(th_tx), cAcc = C(th_acc), cSel = C(th_sel);
    u32 cDim = C(mix_rgb(th_tx, th_bg, 120));

    C2D_DrawCircleSolid(160, 78, 0.30f, 26, cSel);
    icon_search(160, 78, cAcc, cSel);
    draw_text("Searching...", 160, 112, 0.7f, cText, C2D_AlignCenter, 300);
    draw_text(search_index_ready() ? "Looking through your music" : "Reading SD:/Music/ - the first search takes a moment",
              160, 142, 0.4f, cDim, C2D_AlignCenter, 300);
}

static void draw_exit_confirm(void)
{
    u32 cText = C(th_tx), cAcc = C(th_acc), cSel = C(th_sel), cBg = C(th_bg);
    u32 cDim = C(mix_rgb(th_tx, th_bg, 120));
    u32 cCard = C(mix_rgb(th_bg, th_sel, 150));
    u32 cDanger = C2D_Color32(255, 90, 95, 255);
    Rgb black = { 0, 0, 0 };

    rrect(26, 66, 268, 124, 16, 0.22f, C(mix_rgb(bg_at(120), black, 170)));        /* shadow */
    rrect(24, 60, 272, 124, 16, 0.26f, cCard);
    C2D_DrawRectSolid(40, 60, 0.27f, 240, 1, C(mix_rgb(th_bg, th_tx, 30)));

    draw_text("Exit PROJECT NOISE?", 160, 72, 0.6f, cText, C2D_AlignCenter, 250);
    draw_text("Playback will stop.", 160, 98, 0.42f, cDim, C2D_AlignCenter, 0);

    rrect(42, 130, 112, 36, 18, 0.30f, confirm_sel == 0 ? cAcc : cSel);
    row_text("Cancel", 98, 130, 36, 0.5f, confirm_sel == 0 ? cBg : cText, C2D_AlignCenter, 0);
    rrect(166, 130, 112, 36, 18, 0.30f, confirm_sel == 1 ? cDanger : cSel);
    row_text("Exit", 222, 130, 36, 0.5f, confirm_sel == 1 ? cBg : cDanger, C2D_AlignCenter, 0);

    draw_text("D-pad  Choose        A  Confirm        B  Cancel", 160, 210, 0.36f, cDim, C2D_AlignCenter, 316);
}

/* ------------------------------------------------------------------ input */
static float frac_from_px(int px)
{
    float f = (float)(px - SL_X0) / (float)(SL_X1 - SL_X0);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return f;
}

static void seek_to(float frac)
{
    if (!track_loaded || !audio_ok || cur.duration_ms == 0 || cur_path[0] == 0) return;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 0.995f) frac = 0.995f;
    bool was_paused = audio_is_paused();
    audio_seek(cur_path, cur.audio_start, frac, (u32)(frac * (float)cur.duration_ms));
    if (was_paused) audio_pause();             /* seeking while paused stays paused */
}

static void toast(const char *msg)
{
    snprintf(toast_msg, sizeof toast_msg, "%s", msg);
    toast_t = 75;                              /* ~1.2 s */
}

/* D-pad Left / Right: jump `secs` seconds from the current position. Going past the end plays the next track. */
static void skip_seconds(int secs)
{
    if (!track_loaded || !audio_ok || !audio_is_active()) return;
    if (cur.duration_ms == 0) { toast("Can't seek here"); return; }
    long target = (long)audio_position_ms() + (long)secs * 1000L;
    if (target < 0) target = 0;
    if (secs > 0 && target + 500 >= (long)cur.duration_ms) { next_track(true); return; }
    seek_to((float)target / (float)cur.duration_ms);
    char msg[24];
    snprintf(msg, sizeof msg, "%+d sec", secs);
    toast(msg);
}

static void vol_apply(int px)                    /* volume bar: tap or drag */
{
    float f = (float)(px - VOL_X0) / (float)(VOL_X1 - VOL_X0);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    vol_pct = (int)(f * 100.0f + 0.5f);
    audio_set_volume(vol_pct);
}

static void handle_ctrl_tap(int px, int py)      /* bottom deck: seek line, transport and volume */
{
    if (px >= 243) { vol_drag = true; vol_apply(px); return; }                   /* volume capsule */
    if (px < 123) {                                                              /* Now Playing capsule: seek line */
        if (py >= CTRL_TOP + 24 && cur.duration_ms && track_loaded && px >= SL_X0 - 8 && px <= SL_X1 + 8) {
            slider_drag = true;
            slider_frac = frac_from_px(px);
        }
        return;
    }
    if (px < 147)      { flash_ctrl(137); S.shuffle = !S.shuffle; mark_dirty(); }
    else if (px < 169) skip_prev();
    else if (px < 195) { flash_ctrl(182); toggle_play(); }
    else if (px < 217) skip_next();
    else               { flash_ctrl(227); S.repeat = (S.repeat + 1) % 3; mark_dirty(); }
}

static void open_up_next(void) { queue_open = true; queue_sel = 0; queue_scroll = 0; }

/* ================================================================ on-screen keyboard
 * Replaces the system keyboard for song search. Full bottom screen, drawn in the app's own style (dark rounded
 * keys, accent rim, glow on the key being pressed) with the Settings > Look > Keyboard font typeface:
 * Gothic (the lyrics glyph atlas), Bold, System or Compact. Touch a key, or use the D-pad + A.
 * Keys are plain rounded rectangles (no halo unless pressed / selected) to keep the draw cost low. */
#define KB_MAXLEN  40
#define KB_U       28.5f       /* one key unit: ten units + nine gaps span the 312 px keyboard width */
#define KB_G       3.0f
#define KB_X0      4.0f
#define KB_Y0      72.0f
#define KB_H       32.0f
#define KB_VG      4.0f
#define KB_FIELD_X 34.0f       /* typed text: left edge and visible width */
#define KB_FIELD_W 258.0f
#define KB_FIELD_SC 0.52f      /* typed text size (system-font scale; the Gothic glyphs are scaled to match) */

enum { KK_CHAR, KK_SHIFT, KK_BKSP, KK_PAGE, KK_SPACE, KK_GO };
typedef struct { float x, y, w, h; unsigned char kind; char ch; } KbKey;

static bool   kb_open = false;
static char   kb_buf[KB_MAXLEN + 1];
static int    kb_len = 0;
static int    kb_view = 0;                 /* first visible character of the typed text */
static int    kb_page = 0;                 /* 0 = letters, 1 = digits / symbols */
static bool   kb_shift = false;            /* one-shot capital */
static bool   kb_pad = false;              /* D-pad cursor visible */
static int    kb_sel = 0, kb_flash = -1, kb_flash_t = 0, kb_hold = 0;
static KbKey  kb_key[32];
static int    kb_nkey = 0;

static void kb_add(int row, float *col, float w, int kind, char ch)
{
    KbKey *k = &kb_key[kb_nkey++];
    k->x = KB_X0 + *col * (KB_U + KB_G);
    k->y = KB_Y0 + (float)row * (KB_H + KB_VG);
    k->w = w * (KB_U + KB_G) - KB_G;
    k->h = KB_H;
    k->kind = (unsigned char)kind;
    k->ch = ch;
    *col += w;
}

static void kb_chars(int row, float *col, const char *chars)
{
    for (; *chars; chars++) kb_add(row, col, 1.0f, KK_CHAR, *chars);
}

static void kb_layout(void)
{
    float c = 0.0f;
    kb_nkey = 0;
    kb_chars(0, &c, kb_page ? "1234567890" : "qwertyuiop");
    c = 0.5f;
    kb_chars(1, &c, kb_page ? "-/:;()&'\"" : "asdfghjkl");
    c = 0.0f;
    if (kb_page) {
        kb_add(2, &c, 2.0f, KK_CHAR, '.');
        kb_add(2, &c, 2.0f, KK_CHAR, ',');
        kb_add(2, &c, 2.0f, KK_CHAR, '!');
        kb_add(2, &c, 2.0f, KK_CHAR, '?');
        kb_add(2, &c, 2.0f, KK_BKSP, 0);
    } else {
        kb_add(2, &c, 1.5f, KK_SHIFT, 0);
        kb_chars(2, &c, "zxcvbnm");
        kb_add(2, &c, 1.5f, KK_BKSP, 0);
    }
    c = 0.0f;
    kb_add(3, &c, 2.0f, KK_PAGE, 0);
    kb_add(3, &c, 5.0f, KK_SPACE, ' ');
    kb_add(3, &c, 3.0f, KK_GO, 0);
    if (kb_sel >= kb_nkey) kb_sel = kb_nkey - 1;
}

/* text in the chosen keyboard font. sc is the System-font scale; cy is the vertical centre of the text */
static bool  kb_gothic(const char *s) { return S.kb_font == 0 && lyfont_ok() && lyfont_can(s); }
static float kb_scale(const char *s, float sc)
{
    if (kb_gothic(s)) return sc * 0.75f;                 /* Gothic glyph cells are taller than the system font's */
    return S.kb_font == 3 ? sc * 0.85f : sc;
}

static float kb_text_w(const char *s, float sc)
{
    if (!s[0]) return 0.0f;
    const float k = kb_scale(s, sc);
    if (kb_gothic(s)) return lyfont_width(s, k);
    char tmp[64];
    snprintf(tmp, sizeof tmp, "%s", s);
    C2D_Text t;
    C2D_TextParse(&t, tbuf, tmp);
    float w = 0, h = 0;
    C2D_TextGetDimensions(&t, k, k, &w, &h);
    return w + (S.kb_font == 1 ? 0.55f : 0.0f);
}

static void kb_text(const char *s, float x, float cy, float sc, u32 col, u32 align)
{
    if (!s[0]) return;
    const float k = kb_scale(s, sc);
    if (kb_gothic(s)) { lyfont_draw(s, x, cy, k, col, align, text_z); return; }
    const float y = cy - 14.0f * k;
    if (S.kb_font == 1) draw_text(s, x + 0.55f, y, k, col, align, 0);
    draw_text(s, x, y, k, col, align, 0);
}

/* keep the end of the typed text in view: the start of the line scrolls off when it gets too long */
static void kb_refit(void)
{
    if (kb_view > kb_len) kb_view = kb_len;
    while (kb_view < kb_len && kb_text_w(kb_buf + kb_view, KB_FIELD_SC) > KB_FIELD_W) kb_view++;
    while (kb_view > 0 && kb_text_w(kb_buf + kb_view - 1, KB_FIELD_SC) <= KB_FIELD_W) kb_view--;
}

static void kb_close(void)
{
    kb_open = false;
    kb_returned = true;                    /* the browser ignores touch state / held keys until they are released */
}

static void kb_begin(void)
{
    kb_len = 0;
    kb_buf[0] = 0;
    if (L_is_search) {
        snprintf(kb_buf, sizeof kb_buf, "%s", search_q);
        kb_len = (int)strlen(kb_buf);
    }
    kb_page = 0; kb_shift = false; kb_pad = false; kb_sel = 0; kb_view = 0;
    kb_flash = -1; kb_flash_t = 0; kb_hold = 0;
    kb_layout();
    kb_refit();
    kb_open = true;
}

static void kb_submit(void)
{
    int n = kb_len;
    while (n > 0 && kb_buf[n - 1] == ' ') n--;
    if (n == 0) { toast("Type a song or artist"); return; }          /* same rule as before: not empty, not blank */
    kb_buf[n] = 0;
    kb_len = n;
    snprintf(search_q, sizeof search_q, "%s", kb_buf);
    search_busy = 2;                                                  /* draw the "Searching..." screen first, then scan */
    kb_close();
}

static void kb_backspace(void)
{
    if (kb_len <= 0) return;
    kb_buf[--kb_len] = 0;
    kb_refit();
}

static void kb_press(int i)
{
    if (i < 0 || i >= kb_nkey) return;
    const KbKey *k = &kb_key[i];
    kb_flash = i;
    kb_flash_t = 6;
    switch (k->kind) {
    case KK_CHAR:
    case KK_SPACE: {
        if (kb_len >= KB_MAXLEN) break;
        char c = k->ch;
        if (k->kind == KK_SPACE && (kb_len == 0 || kb_buf[kb_len - 1] == ' ')) break;   /* no leading / double spaces */
        if (c >= 'a' && c <= 'z' && kb_shift) { c = (char)(c - 'a' + 'A'); kb_shift = false; }
        kb_buf[kb_len++] = c;
        kb_buf[kb_len] = 0;
        kb_refit();
        break;
    }
    case KK_SHIFT: kb_shift = !kb_shift; break;
    case KK_BKSP:  kb_backspace(); break;
    case KK_PAGE:  kb_page ^= 1; kb_shift = false; kb_layout(); break;
    case KK_GO:    kb_submit(); break;
    }
}

static int kb_hit(int px, int py)
{
    for (int i = 0; i < kb_nkey; i++) {                              /* the gaps between keys belong to the nearest key */
        const KbKey *k = &kb_key[i];
        if (px >= k->x - KB_G / 2 && px < k->x + k->w + KB_G / 2 &&
            py >= k->y - KB_VG / 2 && py < k->y + k->h + KB_VG / 2) return i;
    }
    return -1;
}

static void kb_move(int dx, int dy)                                  /* D-pad: nearest key in that direction */
{
    const KbKey *c = &kb_key[kb_sel];
    const float cx = c->x + c->w / 2, cy = c->y + c->h / 2;
    int best = -1;
    float bs = 1e9f;
    for (int i = 0; i < kb_nkey; i++) {
        if (i == kb_sel) continue;
        const KbKey *k = &kb_key[i];
        const float ddx = k->x + k->w / 2 - cx, ddy = k->y + k->h / 2 - cy;
        float sc;
        if (dx) {
            if (fabsf(ddy) > 4.0f || (dx < 0 ? ddx > -1.0f : ddx < 1.0f)) continue;
            sc = fabsf(ddx);
        } else {
            if (dy < 0 ? ddy > -4.0f : ddy < 4.0f) continue;
            sc = fabsf(ddy) * 4.0f + fabsf(ddx);
        }
        if (sc < bs) { bs = sc; best = i; }
    }
    if (best >= 0) kb_sel = best;
}

/* once per frame while the keyboard is open (input) */
static void kb_update(u32 kd, u32 kh, touchPosition tp, int *hu, int *hd, int *hl, int *hr)
{
    if (kb_flash_t > 0) kb_flash_t--;
    if (kd & KEY_SELECT) { kb_close(); return; }
    if (kd & KEY_TOUCH) {
        if (tp.py < 32) {
            if (tp.px >= 244) { kb_close(); return; }                // Cancel
        } else {
            const int i = kb_hit(tp.px, tp.py);
            if (i >= 0) {
                kb_pad = false;
                kb_sel = i;
                kb_hold = (kb_key[i].kind == KK_BKSP) ? 1 : 0;
                kb_press(i);
                if (!kb_open) return;                                // Search was pressed
            }
        }
    }
    if (kb_hold && (kh & KEY_TOUCH)) {                               /* hold Backspace to keep deleting */
        if (++kb_hold > 24 && (kb_hold % 3) == 0) kb_backspace();
    } else {
        kb_hold = 0;
    }

    if (repeat_fire(kh, KEY_DLEFT, hl))  { kb_pad = true; kb_move(-1, 0); }
    if (repeat_fire(kh, KEY_DRIGHT, hr)) { kb_pad = true; kb_move(+1, 0); }
    if (repeat_fire(kh, KEY_DUP, hu))    { kb_pad = true; kb_move(0, -1); }
    if (repeat_fire(kh, KEY_DDOWN, hd))  { kb_pad = true; kb_move(0, +1); }
    if (kd & KEY_A) { if (kb_pad) kb_press(kb_sel); else kb_pad = true; }
    if (kd & KEY_B) { if (kb_len > 0) kb_backspace(); else { kb_close(); return; } }
    if ((kd & KEY_X) && kb_page == 0) kb_shift = !kb_shift;
    if (kd & (KEY_Y | KEY_START)) kb_submit();
}

static void draw_keyboard(void)
{
    const u32 cText = C(th_tx), cAcc = C(th_acc), cBg = C(th_bg);
    const u32 cDim = C(mix_rgb(th_tx, th_bg, 120));
    const u32 cGlow = neon_glow(), cFill = C(mix_rgb(th_bg, th_acc, 8));
    const u32 cGrey = C(mix_rgb(th_bg, th_tx, 42));
    const u32 cKey = C(mix_rgb(th_bg, th_tx, 10)), cGo = C(mix_rgb(th_bg, th_acc, 38)), cHot = C(mix_rgb(th_bg, th_acc, 90));
    char s[2] = { 0, 0 };

    /* header: title + Cancel pill (same pill as Lyrics > Back) */
    kb_text("Search", 14, 17, 0.80f, cText, C2D_AlignLeft);
    rrect(250, 6, 62, 22, 11, 0.30f, cAcc);
    kb_text("Cancel", 281, 17, 0.45f, cBg, C2D_AlignCenter);

    /* typed text */
    neon_box(8, 34, 304, 30, 15, 0.20f, cAcc, cGlow, cFill);
    icon_search(22, 49, cDim, cFill);
    if (kb_len == 0) {
        kb_text("Search music...", KB_FIELD_X, 49, KB_FIELD_SC, cDim, C2D_AlignLeft);
    } else {
        kb_text(kb_buf + kb_view, KB_FIELD_X, 49, KB_FIELD_SC, cText, C2D_AlignLeft);
    }
    if ((frame_no >> 5) & 1u) {                                      /* blinking caret */
        const float cx = KB_FIELD_X + kb_text_w(kb_buf + kb_view, KB_FIELD_SC) + 2.0f;
        C2D_DrawRectSolid(cx, 40.0f, 0.45f, 2.0f, 18.0f, cAcc);
    }

    /* keys */
    for (int i = 0; i < kb_nkey; i++) {
        const KbKey *k = &kb_key[i];
        const bool hot = (i == kb_flash && kb_flash_t > 0);
        const bool sel = kb_pad && i == kb_sel;
        const bool lit = hot || sel || k->kind == KK_GO || (k->kind == KK_SHIFT && kb_shift);
        const u32 fill = hot ? cHot : (k->kind == KK_GO ? cGo : cKey);
        if (hot || sel) {
            neon_box(k->x, k->y, k->w, k->h, 8, 0.20f, cAcc, cGlow, fill);
        } else {
            rrect(k->x, k->y, k->w, k->h, 8, 0.202f, lit ? cAcc : cGrey);
            rrect(k->x + 1.6f, k->y + 1.6f, k->w - 3.2f, k->h - 3.2f, 6.4f, 0.204f, fill);
        }
        const float cx = k->x + k->w / 2, cy = k->y + k->h / 2;
        switch (k->kind) {
        case KK_CHAR:
            s[0] = (kb_shift && k->ch >= 'a' && k->ch <= 'z') ? (char)(k->ch - 'a' + 'A') : k->ch;
            kb_text(s, cx, cy, 0.66f, cText, C2D_AlignCenter);
            break;
        case KK_SHIFT: {
            const u32 ic = kb_shift ? cAcc : cText;
            C2D_DrawTriangle(cx, cy - 8, ic, cx - 8, cy + 1, ic, cx + 8, cy + 1, ic, 0.45f);
            C2D_DrawRectSolid(cx - 3.5f, cy + 1, 0.45f, 7, 7, ic);
            break;
        }
        case KK_BKSP:
            C2D_DrawTriangle(cx - 11, cy, cText, cx - 3, cy - 8, cText, cx - 3, cy + 8, cText, 0.45f);
            C2D_DrawRectSolid(cx - 3, cy - 8, 0.45f, 14, 16, cText);
            C2D_DrawLine(cx + 0.5f, cy - 3.5f, cKey, cx + 6.5f, cy + 3.5f, cKey, 1.8f, 0.46f);
            C2D_DrawLine(cx + 6.5f, cy - 3.5f, cKey, cx + 0.5f, cy + 3.5f, cKey, 1.8f, 0.46f);
            break;
        case KK_PAGE:  kb_text(kb_page ? "abc" : "123", cx, cy, 0.50f, cText, C2D_AlignCenter); break;
        case KK_SPACE: kb_text("space", cx, cy, 0.46f, cDim, C2D_AlignCenter); break;
        case KK_GO:    kb_text("Search", cx, cy, 0.52f, cAcc, C2D_AlignCenter); break;
        }
    }
}

/* ------------------------------------------------------------------ lyrics */
static LyOptions ly_opts(void)
{
    LyOptions o;
    o.auto_search = S.ly_search;
    o.auto_download = S.ly_download;
    o.offline_only = S.ly_offline;
    o.prefer_synced = S.ly_sync;
    return o;
}

static void lyrics_ask(bool force)                 /* look up lyrics for the loaded song (cache first, then online if allowed) */
{
    if (!track_loaded || cur_path[0] == 0) return;
    LyTrack t;
    lyrics_identify(&t, cur_path, cur.title, cur.artist, cur.duration_ms);
    const LyOptions o = ly_opts();
    /* Cache/sidecar reads are tiny and synchronous; adopt them before starting
     * the network worker so opening Lyrics feels instant when an .lrc already exists. */
    if (!lyrics_preload(&t, &o)) lyrics_request(&t, &o, force);
    snprintf(ly_asked, sizeof ly_asked, "%s", cur_path);
    ly_pos = 0.0f;
    ly_goal = 0.0f;
}

static void open_lyrics(void)
{
    if (!track_loaded) { toast("Play a song first"); return; }
    lyrics_open = true;
    const LyState st = lyrics_state();
    if (strcmp(ly_asked, cur_path) != 0 || st == LY_ERROR || st == LY_NONET || st == LY_IDLE) lyrics_ask(false);
}

static void ly_search_now(void)                    /* A / "Search online" */
{
    if (lyrics_state() == LY_READY || lyrics_state() == LY_LOADING) return;
    if (S.ly_offline) { toast("Offline only is on"); return; }
    lyrics_ask(true);
}

static void clear_lyrics_cache(void)
{
    char msg[32];
    snprintf(msg, sizeof msg, "Lyrics cache cleared (%d)", lyrics_clear_cache());
    toast(msg);
}

static void handle_header_tap(int px, int py)    /* tabs (All Songs / Liked Songs / Queue) and the search bar under them */
{
    if (py < 33) {
        if (px < 108)      { if (L_is_favorites || L_is_search) set_dir(S.last_dir, NULL); }
        else if (px < 211) { if (!L_is_favorites) show_favorites(); }
        else               open_up_next();
        return;
    }
    if (px >= 244) { if (lyrics_open) lyrics_open = false; else open_lyrics(); } /* LYRICS pill */
    else if (L_is_search && px >= 204) exit_search();
    else open_search();
}

/* touch on a settings card (Settings tabs and setup steps share this) */
static void card_touch(touchPosition tp, int card)
{
    int i = (int)(((float)tp.py - 70.0f) / CARD_ROWH[card]);
    if (tp.py < 70 || i < 0 || i >= CARD_LEN[card]) return;
    int r = CARD_ROWS[card][i];
    set_row = r;

    if (r == ROW_EXIT)  { confirm_exit = true; confirm_sel = 0; return; }
    if (r == ROW_GUIDE) { start_wizard(); return; }
    if (r == ROW_LY_CLEAR) { clear_lyrics_cache(); return; }
    if (is_switch_row(r)) { set_adjust(+1); return; }

    if (is_slider_row(r)) {
        if (tp.px >= SS_X0 - 12 && tp.px <= SS_X1 + 12) { set_drag = r; slider_apply(r, tp.px, tp.py); }
    } else if (is_choice_row(r)) {
        if (tp.px >= 150 && tp.px < 228)      set_adjust(-1);
        else if (tp.px >= 228)                set_adjust(+1);
    }
}

/* touch on the Audio tab: Manual EQ / preset row and vertical band sliders */
static void sound_touch(touchPosition tp)
{
    if (tp.py < SND_TOP) return;
    if (tp.py < SND_EQ_Y) {                                   /* Manual EQ | preset */
        if (tp.px < 158) { set_row = ROW_EQMAN; set_adjust(+1); }
        else { set_row = ROW_EQPRE; set_adjust(tp.px < 236 ? -1 : +1); }
    } else {                                                   /* band sliders */
        int b = ((int)tp.px - EQ_COL_X0) / EQ_COL_W;
        if (tp.px < EQ_COL_X0 || b < 0 || b >= EQ_BANDS) return;
        if (!S.eq_manual) { toast("Turn on Manual EQ"); return; }
        set_row = ROW_B0 + b;
        if (tp.py >= EQ_TRK_Y0 - 14 && tp.py <= EQ_TRK_Y1 + 12) {      /* on the slider, not on its labels */
            set_drag = set_row;
            slider_apply(set_row, tp.px, tp.py);
        }
    }
}

static void handle_settings_touch(touchPosition tp)
{
    if (tp.py < 40) {                                   /* header: Done pill */
        if (tp.px >= 244) close_settings();
        return;
    }
    if (tp.py < 70) {                                   /* segmented control */
        int t = ((int)tp.px - 10) / TAB_W;
        if (t < 0) t = 0;
        if (t > PAGES - 1) t = PAGES - 1;
        set_row = TAB_FIRST[t];
        return;
    }
    int page = row_page(set_row);
    if (page == 1) sound_touch(tp); else card_touch(tp, PAGE_CARD[page]);
}

static void handle_wizard_touch(touchPosition tp)
{
    if (tp.py < 32) {                                   /* Skip pill */
        if (tp.px >= 244 && wz_page < WZ_N - 1) wz_finish();
        return;
    }
    if (tp.py >= 192) {                                 /* Back / Next */
        if (tp.px < 118) wz_back();
        else if (tp.px >= 204) wz_next();
        return;
    }
    if (WZ_CARD[wz_page] >= 0) card_touch(tp, WZ_CARD[wz_page]);
}

/* ------------------------------------------------------------ playback memory */
static char rs_path[800];
static bool rs_playing = false;
static int  rs_timer = 0;

static void resume_save(u32 pos)
{
    if (!S.resume || !track_loaded || cur_path[0] == 0) return;
    mkdir(DATA_ROOT, 0777);
    mkdir(DATA_DIR, 0777);
    FILE *f = fopen(RESUME_PATH, "w");
    if (!f) return;
    fprintf(f, "path=%s\npos=%u\n", cur_path, (unsigned)pos);
    fclose(f);
}

/* Called once per frame: saves when the song changes, on pause / play, and every ~10 s while playing. */
static void resume_tick(void)
{
    if (resume_pending && audio_is_active()) resume_pending = false;      /* it really started */
    if (!S.resume || !track_loaded || cur_path[0] == 0) return;
    if (strcmp(rs_path, cur_path) != 0) {                                  /* a different song began */
        snprintf(rs_path, sizeof rs_path, "%s", cur_path);
        rs_timer = 0;
        rs_playing = audio_is_active() && !audio_is_paused();
        if (audio_is_active()) resume_save(0);
        return;
    }
    const bool active = audio_is_active();
    if (!active && !resume_pending) { rs_playing = false; return; }
    const bool pl = active && !audio_is_paused();
    if (pl != rs_playing) { rs_playing = pl; rs_timer = 0; resume_save(cur_pos_ms()); return; }
    if (pl && ++rs_timer >= (S.saver ? 1800 : 600)) { rs_timer = 0; resume_save(cur_pos_ms()); }
}

/* Reopen on the song (and position) that was playing last time: shown paused, Play continues from there. */
static void resume_restore(void)
{
    if (!S.resume || !S.setup_done) return;
    FILE *f = fopen(RESUME_PATH, "r");
    if (!f) return;
    char path[800] = "", line[900];
    unsigned long pos = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (!strncmp(line, "path=", 5)) snprintf(path, sizeof path, "%s", line + 5);
        else if (!strncmp(line, "pos=", 4)) pos = strtoul(line + 4, NULL, 10);
    }
    fclose(f);

    const size_t rl = strlen(MUSIC_ROOT);
    const char *sl = strrchr(path, '/');
    if (!sl || strncmp(path, MUSIC_ROOT, rl) != 0 || !is_audio_file(path)) return;
    TrackMeta m;
    u8 *art = NULL;
    size_t art_size = 0;
    if (!meta_read(path, &m, &art, &art_size, true)) { free(art); return; }      /* file is gone: start fresh */

    char dir[512];
    const size_t dl = (size_t)(sl - path) + 1;
    if (dl >= sizeof dir) { free(art); return; }
    memcpy(dir, path, dl);
    dir[dl] = 0;

    DirList tmp;
    memset(&tmp, 0, sizeof tmp);
    if (dir_load(&tmp, dir)) {                       /* the playlist is the song's own folder */
        dir_copy_files(&Q, &tmp);
        Q_is_search = false;
        Q_is_favorites = false;
        qi = -1;
        for (int i = 0; i < Q.n; i++)
            if (!strcmp(Q.e[i].name, sl + 1)) { qi = i; break; }
    }
    dir_free(&tmp);

    cur = m;
    track_loaded = true;
    cur_from_queue = false;
    snprintf(cur_path, sizeof cur_path, "%s", path);
    snprintf(rs_path, sizeof rs_path, "%s", path);
    art_queue(art, art_size);
    if (m.duration_ms && pos + 1500UL >= m.duration_ms) pos = 0;               /* it had basically finished */
    resume_pos_ms = (u32)pos;
    resume_pending = true;
    flow_show = false;                               /* open on the Now Playing view */
}

/* which bottom-screen page is up: a change starts the soft cross-fade */
static int bot_state_key(void)
{
    if (search_busy > 0) return 1;
    if (confirm_exit) return 2;
    if (kb_open) return 5;
    if (wizard_open) return 10 + wz_page;
    if (settings_open) return 20 + row_page(set_row);
    if (queue_open) return 3;
    if (lyrics_open) return 4;
    return 0;
}

/* ====================================================================== loading screen
 * A classic application window (bevelled frame, gradient title bar, sunken progress bar of blocks) in the
 * theme colours. Progress is real: every task below runs between two frames and moves the bar by its weight.
 * Nothing here searches online; only cached lyrics of the song that is selected / was playing last are read. */
typedef struct { const char *name; int weight; } LoadTask;
static const LoadTask LOAD_TASKS[] = {
    { "Starting audio engine",     2 },
    { "Loading interface assets",  2 },
    { "Starting lyrics service",   1 },
    { "Reading favorites",         1 },
    { "Opening music library",     3 },
    { "Restoring last song",       3 },
    { "Reading music metadata",    4 },
    { "Loading album artwork",     3 },
    { "Checking cached lyrics",    1 }
};
#define LOAD_N ((int)(sizeof LOAD_TASKS / sizeof LOAD_TASKS[0]))

static struct { float shown, target, veil; int task, frame; bool done; } ld;
static C3D_Tex           ld_logo_tex;
static bool              ld_logo_ok = false;
static Tex3DS_SubTexture ld_logo_sub = { 128, 128, 0.0f, 1.0f, 1.0f, 0.0f };

static void ld_bevel(float x, float y, float w, float h, bool sunken, float z)
{
    const Rgb face = mix_rgb(th_bg, th_tx, 20), black = { 0, 0, 0 };
    const u32 hi = C(mix_rgb(face, th_tx, 120)), lo = C(mix_rgb(th_bg, black, 170));
    const u32 a = sunken ? lo : hi, b = sunken ? hi : lo;
    C2D_DrawRectSolid(x, y, z, w, 1, a);
    C2D_DrawRectSolid(x, y, z, 1, h, a);
    C2D_DrawRectSolid(x, y + h - 1, z, w, 1, b);
    C2D_DrawRectSolid(x + w - 1, y, z, 1, h, b);
}

static void ld_window(float x, float y, float w, float h, const char *title, float t)
{
    const Rgb face = mix_rgb(th_bg, th_tx, 20);
    C2D_DrawRectSolid(x + 4, y + 4, 0.20f, w, h, C2D_Color32(0, 0, 0, 110));          /* drop shadow */
    C2D_DrawRectSolid(x, y, 0.22f, w, h, C(face));
    ld_bevel(x, y, w, h, false, 0.23f);
    ld_bevel(x + 1, y + 1, w - 2, h - 2, false, 0.23f);

    /* title bar: dark -> bright accent gradient with a slow highlight sweeping across it */
    const float bx = x + 3, by = y + 3, bw = w - 6, bh = 18;
    const u32 c0 = C(mix_rgb(th_bg, th_acc, 130)), c1 = C(th_acc);
    C2D_DrawRectangle(bx, by, 0.24f, bw, bh, c0, c1, c0, c1);
    if (!S.saver) {
        float sx = bx + fmodf(t * 70.0f, bw + 80.0f) - 40.0f, x0 = sx < bx ? bx : sx, x1 = sx + 26.0f > bx + bw ? bx + bw : sx + 26.0f;
        if (x1 > x0) C2D_DrawRectSolid(x0, by, 0.25f, x1 - x0, bh, C2D_Color32(255, 255, 255, 34));
    }
    icon_note(bx + 5, by + 1, C2D_Color32(255, 255, 255, 255));
    row_text(title, bx + 24, by, bh, 0.48f, C2D_Color32(255, 255, 255, 255), C2D_AlignLeft, bw - 90);

    /* window buttons (decorative): minimise, maximise, close */
    for (int k = 0; k < 3; k++) {
        const float gx = bx + bw - 17.0f * (float)(3 - k) - 1.0f, gy = by + 2, gw = 15, gh = 14;
        C2D_DrawRectSolid(gx, gy, 0.26f, gw, gh, C(face));
        ld_bevel(gx, gy, gw, gh, false, 0.27f);
        const u32 g = C(th_tx);
        if (k == 0) C2D_DrawRectSolid(gx + 4, gy + 9, 0.28f, 7, 2, g);
        else if (k == 1) {
            C2D_DrawRectSolid(gx + 4, gy + 3, 0.28f, 8, 2, g);
            C2D_DrawRectSolid(gx + 4, gy + 3, 0.28f, 1, 8, g);
            C2D_DrawRectSolid(gx + 11, gy + 3, 0.28f, 1, 8, g);
            C2D_DrawRectSolid(gx + 4, gy + 10, 0.28f, 8, 1, g);
        }
        else { C2D_DrawLine(gx + 4, gy + 3, g, gx + 11, gy + 10, g, 1.6f, 0.28f); C2D_DrawLine(gx + 11, gy + 3, g, gx + 4, gy + 10, g, 1.6f, 0.28f); }
    }
}

static void ld_top(float t)
{
    const float x = 40, y = 32, w = 320, h = 176;
    const u32 cText = C(th_tx), cAcc = C(th_acc), cDim = C(mix_rgb(th_tx, th_bg, 120));
    ld_window(x, y, w, h, "PROJECT NOISE", t);

    /* logo in a sunken frame, name, tagline */
    C2D_DrawRectSolid(x + 14, y + 30, 0.28f, 76, 76, C(mix_rgb(th_bg, th_acc, 14)));
    ld_bevel(x + 14, y + 30, 76, 76, true, 0.29f);
    if (ld_logo_ok) { C2D_Image img = { &ld_logo_tex, &ld_logo_sub }; C2D_DrawImageAt(img, x + 20, y + 36, 0.30f, NULL, 0.5f, 0.5f); }
    else icon_note(x + 45, y + 60, cAcc);
    draw_text("PROJECT NOISE", x + 104, y + 38, 0.80f, cAcc, C2D_AlignLeft, 200);
    draw_text("Native music player", x + 104, y + 66, 0.46f, cText, C2D_AlignLeft, 200);
    draw_text("YOUR MUSIC. YOUR NOISE.", x + 104, y + 84, 0.38f, cDim, C2D_AlignLeft, 200);

    /* etched divider */
    C2D_DrawRectSolid(x + 10, y + 112, 0.28f, w - 20, 1, C(mix_rgb(th_bg, th_tx, 8)));
    C2D_DrawRectSolid(x + 10, y + 113, 0.28f, w - 20, 1, C(mix_rgb(th_bg, th_tx, 70)));

    /* current task */
    char buf[96];
    const int dots = ((int)(t * 3.0f)) % 4;
    snprintf(buf, sizeof buf, "%s%.*s", ld.task < LOAD_N ? LOAD_TASKS[ld.task].name : "Ready", ld.task < LOAD_N ? dots : 0, "...");
    draw_text(buf, x + 14, y + 118, 0.46f, cText, C2D_AlignLeft, w - 90);
    snprintf(buf, sizeof buf, "%d%%", (int)(ld.shown * 100.0f + 0.5f));
    draw_text(buf, x + w - 14, y + 118, 0.46f, cAcc, C2D_AlignRight, 60);

    /* progress bar: sunken well filled with blocks, the leading block pulses */
    const float px = x + 14, py = y + 134, pw = w - 28, ph = 16;
    C2D_DrawRectSolid(px, py, 0.28f, pw, ph, C(mix_rgb(th_bg, th_tx, 4)));
    ld_bevel(px, py, pw, ph, true, 0.29f);
    const float bw = 9, step = 11;
    const int count = (int)((pw - 6) / step);
    const float filled = ld.shown * (float)count;
    for (int i = 0; i < count && (float)i < filled; i++) {
        float k = 1.0f;
        if ((float)(i + 1) > filled) k = 0.45f + 0.55f * (filled - (float)i);                 /* partly filled block */
        else if (i + 1 >= (int)filled && !ld.done && !S.saver) k = 0.7f + 0.3f * sinf(t * 9.0f);
        const float bx = px + 3 + (float)i * step;
        C2D_DrawRectSolid(bx, py + 3, 0.30f, bw, ph - 6, Ca(th_acc, k));
        C2D_DrawRectSolid(bx, py + 3, 0.31f, bw, 3, Ca(mix_rgb(th_acc, th_tx, 90), k * 0.9f));
    }

    /* status bar */
    C2D_DrawRectSolid(x + 6, y + h - 22, 0.28f, w - 12, 16, C(mix_rgb(th_bg, th_tx, 4)));
    ld_bevel(x + 6, y + h - 22, w - 12, 16, true, 0.29f);
    row_text("MADE BY GRIMZZZZ", x + 12, y + h - 22, 16, 0.34f, cDim, C2D_AlignLeft, 160);
    snprintf(buf, sizeof buf, "Task %d of %d", ld.task < LOAD_N ? ld.task + 1 : LOAD_N, LOAD_N);
    row_text(buf, x + w - 12, y + h - 22, 16, 0.34f, cDim, C2D_AlignRight, 120);
}

static void ld_bottom(float t)
{
    const float x = 30, y = 32, w = 260, h = 176;
    const u32 cText = C(th_tx), cAcc = C(th_acc), cDim = C(mix_rgb(th_tx, th_bg, 100));
    ld_window(x, y, w, h, "Starting up", t);
    for (int i = 0; i < LOAD_N; i++) {
        const float ry = y + 30 + (float)i * 15.0f;
        const bool done = i < ld.task || ld.done, now = i == ld.task && !ld.done;
        C2D_DrawRectSolid(x + 14, ry + 2, 0.28f, 10, 10, C(mix_rgb(th_bg, th_tx, 4)));
        ld_bevel(x + 14, ry + 2, 10, 10, true, 0.29f);
        if (done) {
            C2D_DrawLine(x + 16, ry + 7, cAcc, x + 18.5f, ry + 10, cAcc, 1.8f, 0.32f);
            C2D_DrawLine(x + 18.5f, ry + 10, cAcc, x + 22.5f, ry + 4, cAcc, 1.8f, 0.32f);
        } else if (now) {
            C2D_DrawRectSolid(x + 17, ry + 5, 0.32f, 4, 4, Ca(th_acc, 0.5f + 0.5f * sinf(t * 9.0f)));
        }
        row_text(LOAD_TASKS[i].name, x + 32, ry, 14, 0.38f, now ? cAcc : (done ? cText : cDim), C2D_AlignLeft, w - 46);
    }
}

static void ld_render(C3D_RenderTarget *top, C3D_RenderTarget *bot)
{
    ld.frame++;
    ld.shown += (ld.target - ld.shown) * 0.35f;
    if (ld.target - ld.shown < 0.004f) ld.shown = ld.target;
    const float t = (float)ld.frame / 60.0f;
    const float intro = ld.frame < 9 ? 1.0f - (float)ld.frame / 9.0f : 0.0f;      /* the window fades in ... */
    const float veil = intro > ld.veil ? intro : ld.veil;                          /* ... and out into the player */

    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    art_commit();
    flow_pump();                                   /* upload a finished cover */
    C2D_TextBufClear(tbuf);
    C2D_TargetClear(top, C(th_bg));
    C2D_SceneBegin(top);
    bg_gradient(TOP_W);
    ld_top(t);
    if (veil > 0.01f) C2D_DrawRectSolid(0, 0, 0.95f, TOP_W, SCR_H, Ca(th_bg, veil));
    C2D_TargetClear(bot, C(th_bg));
    C2D_SceneBegin(bot);
    bg_gradient(BOT_W);
    ld_bottom(t);
    if (veil > 0.01f) C2D_DrawRectSolid(0, 0, 0.95f, BOT_W, SCR_H, Ca(th_bg, veil));
    C3D_FrameEnd(0);
}

static void startup_load(C3D_RenderTarget *top, C3D_RenderTarget *bot)
{
    memset(&ld, 0, sizeof ld);
    int total = 0, before = 0;
    for (int i = 0; i < LOAD_N; i++) total += LOAD_TASKS[i].weight;
    ld_logo_ok = art_build_tex(&ld_logo_tex, placeholder_png, placeholder_png_len);

    for (int i = 0; i < LOAD_N; i++) {
        ld.task = i;
        ld.target = (float)before / (float)total;
        ld_render(top, bot);                                   /* shows this task's name while it runs */
        const float wgt = (float)LOAD_TASKS[i].weight;

        switch (i) {
        case 0:
            audio_ok = audio_init();
            audio_set_volume(100);
            eq_push();
            if (!audio_ok) snprintf(status_msg, sizeof status_msg, "No sound - dspfirm.cdc missing");
            break;
        case 1: flow_init(placeholder_png, placeholder_png_len); break;
        case 2: lyrics_init(); lyfont_init(); break;
        case 3: favorites_load(); break;
        case 4:
            set_dir(S.last_dir, NULL);
            settings_dirty = false;
            break;
        case 5: resume_restore(); break;                       /* last song: tags, playlist, artwork queued */
        case 6: {                                              /* tags of the selected song first, then its neighbours */
            static const int order[6] = { 0, 1, -1, 2, -2, 3 };
            int pick[6], np = 0;
            for (int k = 0; k < 6; k++) {
                const int idx = sel + order[k];
                if (idx >= 0 && idx < L.n && !L.e[idx].is_dir && !L.e[idx].meta_loaded) pick[np++] = idx;
            }
            for (int k = 0; k < np; k++) {
                ensure_meta(&L.e[pick[k]]);
                ld.target = ((float)before + wgt * (float)(k + 1) / (float)np) / (float)total;
                if (k + 1 < np) ld_render(top, bot);
            }
            break;
        }
        case 7: {                                              /* selected cover first; the rest loads in the background */
            flow_refresh_wants();
            flow_want_dirty = false;
            for (int k = 0; k < 24; k++) {
                const bool need = L.n > 0 && sel < L.n && !L.e[sel].is_dir && !(is_current_row(sel) && art_ready());
                float f;
                if (k >= 2 && !(need && !flow_tex(sel, &f))) break;
                ld_render(top, bot);
            }
            break;
        }
        case 8: {                                              /* cached lyrics only: never a network request here */
            LyTrack lt;
            bool have = false;
            if (track_loaded && cur_path[0]) {
                lyrics_identify(&lt, cur_path, cur.title, cur.artist, cur.duration_ms);
                have = true;
            } else if (L.n > 0 && sel < L.n && !L.e[sel].is_dir) {
                char p[800];
                snprintf(p, sizeof p, "%s%s", L.dir, L.e[sel].name);
                lyrics_identify(&lt, p, L.e[sel].title, L.e[sel].artist, L.e[sel].duration_ms);
                have = true;
            }
            if (have) { const LyOptions o = ly_opts(); lyrics_preload(&lt, &o); }
            break;
        }
        }
        before += LOAD_TASKS[i].weight;
        ld.target = (float)before / (float)total;
    }

    ld.done = true;
    ld.task = LOAD_N;
    for (int k = 0; k < 24; k++) {                             /* let the bar reach 100 % */
        ld_render(top, bot);
        if (ld.shown >= 0.995f) break;
    }
    for (int k = 1; k <= 6; k++) { ld.veil = (float)k / 6.0f; ld_render(top, bot); }
    if (ld_logo_ok) C3D_TexDelete(&ld_logo_tex);
    ld_logo_ok = false;
}

int main(void)
{
    osSetSpeedupEnable(true);
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    tbuf = C2D_TextBufNew(6144);

    srand((unsigned)time(NULL));
    mkdir("sdmc:/Music", 0777);        /* harmless if it already exists */

    settings_defaults(&S);
    settings_load(&S);
    theme_update(1.0f / 60.0f);
    aptHook(&lid_hook_cookie, apt_lid_hook, NULL);
    apply_lid();

    mcu_ok = R_SUCCEEDED(mcuHwcInit());      /* battery percentage (optional) */
    ptm_ok = R_SUCCEEDED(ptmuInit());        /* charging state / fallback level (optional) */

    art_set_default(placeholder_png, placeholder_png_len);
    art_queue(NULL, 0);                 /* show the logo cover until a song with art plays */
    startup_load(top, bot);             /* loading window: audio, assets, library, last song, artwork, cached lyrics */
    if (!S.setup_done) start_wizard();       /* first launch: setup + guide */

    int hold_up = 0, hold_down = 0, hold_left = 0, hold_right = 0;
    flow_last_input_ms = osGetTime();
    bool set_in_band = false;
    touchPosition tp = { 0, 0 }, tp_start = { 0, 0 }, tp_prev = { 0, 0 };
    bool touch_in_list = false, dragging = false;
    int drag_accum = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown(), kh = hidKeysHeld(), ku = hidKeysUp();
        if (kh & KEY_TOUCH) hidTouchRead(&tp);
        /* Any physical-button or touch activity restores the carousel. */
        if (kd || kh || ku) {
            flow_last_input_ms = osGetTime();
            if (flow_idle_collapse) flow_idle_collapse = false;
        }
        if (flow_show && osGetTime() - flow_last_input_ms >= 4000)
            flow_idle_collapse = true;
        if (search_busy > 0) kd = kh = ku = 0;       /* "Searching..." is up: ignore input for those frames */

        bool dlg = confirm_exit;                  /* START asks before leaving */
        if (!dlg && !kb_open && (kd & KEY_START)) { confirm_exit = true; confirm_sel = 0; }   /* in the keyboard START = Search */
        if (quit_req) break;

        /* ---- sleep timer: elapsed wall-clock time, including time spent in system sleep ---- */
        if (sleep_timer_active && osGetTime() >= sleep_timer_deadline) {
            sleep_timer_active = false; audio_stop(); apply_lid(); toast("Sleep timer ended");
        }

        /* ---- track end / error handling ---- */
        if (audio_take_finished()) {
            if (S.repeat == 2) replay_current(); else next_track(false);
            follow_playing();
        }
        if (audio_take_error()) { snprintf(status_msg, sizeof status_msg, "Can't play this file"); apply_lid(); }

        /* ---- always-on playback keys ---- */
        if (!dlg && !wizard_open) {          /* L / R: previous / next song */
            if (kd & KEY_L) skip_prev();
            if (kd & KEY_R) skip_next();
        }

        if (dlg) {
            if (kd & KEY_B) confirm_exit = false;
            if (kd & (KEY_DLEFT | KEY_DRIGHT)) confirm_sel ^= 1;
            if (kd & KEY_A) { if (confirm_sel) quit_req = true; else confirm_exit = false; }
            if (kd & KEY_START) quit_req = true;                 /* START twice = leave */
            if ((kd & KEY_TOUCH) && tp.py >= 130 && tp.py < 166) {
                if (tp.px >= 42 && tp.px < 154) confirm_exit = false;
                else if (tp.px >= 166 && tp.px < 278) quit_req = true;
            }
        } else if (kb_open) {
            kb_update(kd, kh, tp, &hold_up, &hold_down, &hold_left, &hold_right);
        } else if (wizard_open) {
            if (kd & KEY_A) wz_next();
            if (kd & KEY_B) wz_back();
            int card = WZ_CARD[wz_page];
            if (card >= 0) {
                int pos = card_pos(card, set_row);
                if (pos < 0) pos = 0;
                if (repeat_fire(kh, KEY_DUP, &hold_up) && pos > 0) set_row = CARD_ROWS[card][pos - 1];
                if (repeat_fire(kh, KEY_DDOWN, &hold_down) && pos < CARD_LEN[card] - 1) set_row = CARD_ROWS[card][pos + 1];
                bool fl = repeat_fire(kh, KEY_DLEFT, &hold_left), fr = repeat_fire(kh, KEY_DRIGHT, &hold_right);
                if (is_switch_row(set_row)) { fl = (kd & KEY_DLEFT) != 0; fr = (kd & KEY_DRIGHT) != 0; }
                if (fl) set_adjust(-1);
                if (fr) set_adjust(+1);
            }
            if (kd & KEY_TOUCH) handle_wizard_touch(tp);
            if ((kh & KEY_TOUCH) && set_drag >= 0) slider_apply(set_drag, tp.px, tp.py);
            if (ku & KEY_TOUCH) set_drag = -1;
        } else if (queue_open) {
            if (kd & (KEY_B | KEY_SELECT)) queue_open = false;
            if (manual_queue_count == 0) queue_sel = queue_scroll = 0;
            if (kd & KEY_TOUCH) {
                if (tp.py < 36 && tp.px >= 244) queue_open = false;
                else if (tp.py >= CTRL_TOP) handle_ctrl_tap(tp.px, tp.py);
                else if (tp.py >= 48 && tp.py < 48 + QUEUE_ROWS * 34 && manual_queue_count > 0) {
                    int row = queue_scroll + ((int)tp.py - 48) / 34;
                    if (row >= 0 && row < manual_queue_count) {
                        if (tp.px >= 278) queue_remove_at(row);
                        else queue_play_at(row);
                    }
                }
            }
            else {
                if (repeat_fire(kh, KEY_DUP, &hold_up)) { if (queue_sel > 0) queue_sel--; }
                if (repeat_fire(kh, KEY_DDOWN, &hold_down)) { if (queue_sel < manual_queue_count - 1) queue_sel++; }
                if (kd & KEY_A) queue_play_at(queue_sel);
                /* X and Y are intentionally not destructive inside Up Next. */
            }
        } else if (settings_open) {
            if (kd & (KEY_SELECT | KEY_B)) close_settings();
            if (is_band_row(set_row) && !S.eq_manual) set_row = ROW_EQPRE;     /* locked sliders are display-only */
            bool band = is_band_row(set_row);
            if (band != set_in_band) {          /* crossed into / out of the slider panel: ignore keys still held */
                hold_up = hold_down = hold_left = hold_right = -100000;
                set_in_band = band;
            }
            if (band) {
                /* vertical sliders: Left/Right pick the band, Up/Down move it; the panel's ends lead out of it */
                if (repeat_fire(kh, KEY_DLEFT, &hold_left))   set_row = (set_row > ROW_B0) ? set_row - 1 : ROW_EQPRE;
                if (repeat_fire(kh, KEY_DRIGHT, &hold_right)) set_row = (set_row < ROW_B4) ? set_row + 1 : ROW_DISPLAY;
                bool gu = repeat_fire(kh, KEY_DUP, &hold_up), gd = repeat_fire(kh, KEY_DDOWN, &hold_down);
                if (is_band_row(set_row)) {
                    if (gu) set_adjust(+1);
                    if (gd) set_adjust(-1);
                }
                if (kd & KEY_A) set_row = ROW_EQPRE;
            } else {
                if (repeat_fire(kh, KEY_DUP, &hold_up))     set_row = row_step(set_row, -1);
                if (repeat_fire(kh, KEY_DDOWN, &hold_down)) set_row = row_step(set_row, +1);
                bool fl = repeat_fire(kh, KEY_DLEFT, &hold_left), fr = repeat_fire(kh, KEY_DRIGHT, &hold_right);
                if (is_switch_row(set_row)) { fl = (kd & KEY_DLEFT) != 0; fr = (kd & KEY_DRIGHT) != 0; }   /* switch: no auto-repeat */
                if (fl) set_adjust(-1);
                if (fr) set_adjust(+1);
                if (kd & KEY_A) {
                    if (is_switch_row(set_row) || is_choice_row(set_row)) set_adjust(+1);
                    else if (set_row == ROW_GUIDE) start_wizard();
                    else if (set_row == ROW_LY_CLEAR) clear_lyrics_cache();
                    else if (set_row == ROW_EXIT) { confirm_exit = true; confirm_sel = 0; }
                }
            }
            if (kd & KEY_TOUCH) handle_settings_touch(tp);
            if ((kh & KEY_TOUCH) && set_drag >= 0) slider_apply(set_drag, tp.px, tp.py);
            if (ku & KEY_TOUCH) set_drag = -1;
        } else {
            if (kd & KEY_SELECT) { settings_open = true; set_row = 0; }

            /* A: open folder / play selected / resume.  B: pause. */
            if (kd & KEY_A) {
                if (sel < L.n && L.e[sel].is_dir) activate(sel);
                else if (sel < L.n && !is_current_row(sel)) play_from_list(sel);
                else if (audio_is_paused()) { audio_resume(); apply_lid(); }
                else if (!audio_is_active()) toggle_play();
            }
            if (kd & KEY_B) {
                if (lyrics_open) lyrics_open = false;
                else if (audio_is_active() && !audio_is_paused()) { audio_pause(); apply_lid(); }
            }
            if ((kd & KEY_X) && sel < L.n && !L.e[sel].is_dir) queue_add_from_list(sel, true);
            if ((kd & KEY_Y) && sel < L.n && !L.e[sel].is_dir) queue_add_from_list(sel, false);
            if ((kd & KEY_ZL) && sel < L.n && !L.e[sel].is_dir) { char fp[256]; entry_relative_path(&L.e[sel], fp, sizeof fp); favorite_toggle(fp); }

            if (repeat_fire(kh, KEY_DUP, &hold_up))     move_sel(-1);
            if (repeat_fire(kh, KEY_DDOWN, &hold_down)) move_sel(+1);
            if (repeat_seek(kh, KEY_DLEFT, &hold_left))   skip_seconds(-S.seek_step);   /* back */
            if (repeat_seek(kh, KEY_DRIGHT, &hold_right)) skip_seconds(+S.seek_step);   /* forward */

            /* touch: controls act on press; list rows act on release (tap) */
            if (kd & KEY_TOUCH) {
                tp_start = tp_prev = tp;
                dragging = false;
                drag_accum = 0;
                touch_in_list = (tp.py >= LIST_TOP && tp.py < LIST_TOP + VIS_ROWS * ROW_H);
                if (tp.py >= CTRL_TOP) handle_ctrl_tap(tp.px, tp.py);
                else if (tp.py < LIST_TOP) handle_header_tap(tp.px, tp.py);
            }
            if ((kh & KEY_TOUCH) && touch_in_list) {
                if (!dragging && abs((int)tp.py - (int)tp_start.py) > 8) dragging = true;
                if (dragging) {
                    drag_accum += (int)tp_prev.py - (int)tp.py;
                    while (drag_accum >= ROW_H)  { scroll_top++; drag_accum -= ROW_H; }
                    while (drag_accum <= -ROW_H) { scroll_top--; drag_accum += ROW_H; }
                    clamp_scroll();
                    int mid = scroll_top + VIS_ROWS / 2;      /* focus follows the drag */
                    if (mid >= L.n) mid = L.n - 1;
                    if (mid < 0) mid = 0;
                    if (L.n > 0 && mid != sel) { sel = mid; flow_show = true; }
                }
                tp_prev = tp;
            }
            if ((ku & KEY_TOUCH) && touch_in_list && !dragging) {
                int row = scroll_top + ((int)tp_start.py - LIST_TOP) / ROW_H;
                if (tp_start.px >= SB_TAP_X) {                                   /* scrollbar: arrows move the list by a row */
                    int dir = 0;
                    if (tp_start.py < LIST_TOP + 22) dir = -1;
                    else if (tp_start.py >= LIST_TOP + VIS_ROWS * ROW_H - 25) dir = +1;
                    if (dir && L.n > 0) {
                        scroll_top += dir;
                        clamp_scroll();
                        int mid = scroll_top + VIS_ROWS / 2;
                        if (mid >= L.n) mid = L.n - 1;
                        if (mid != sel) { sel = mid; flow_show = true; }
                    }
                }
                else if (row >= 0 && row < L.n && !L.e[row].is_dir && tp_start.px >= 272) { char fp[256]; entry_relative_path(&L.e[row], fp, sizeof fp); favorite_toggle(fp); }
                else activate(row);
            }
            if ((kh & KEY_TOUCH) && slider_drag) slider_frac = frac_from_px(tp.px);
            if ((ku & KEY_TOUCH) && slider_drag) { slider_frac = frac_from_px(tp.px); seek_to(slider_frac); slider_drag = false; }
            if (ku & KEY_TOUCH) touch_in_list = false;
            if (kb_returned) {                       /* the system keyboard was just shown */
                kb_returned = false;
                touch_in_list = dragging = slider_drag = false;
                hold_up = hold_down = hold_left = hold_right = -100000;   /* ignore keys still held until released */
            }
        }

        if (vol_drag) { if (kh & KEY_TOUCH) vol_apply(tp.px); else vol_drag = false; }

        /* ---- auto-save theme/settings shortly after the last change ---- */
        if (settings_dirty && --dirty_timer <= 0) {
            if (settings_save(&S)) settings_dirty = false;
            else dirty_timer = 180; /* retry later; do not discard unsaved changes */
        }

        if (audio_is_active() && !audio_is_paused()) { eq_phase += 0.08f; cd_phase += 0.075f; }
        vis_update();
        if (toast_t > 0) toast_t--;
        if (ctrl_flash_t > 0) ctrl_flash_t--;
        if (S.show_batt) { if (--batt_timer <= 0) { batt_poll(); batt_timer = S.saver ? 300 : 120; } }   /* every ~2 s (5 s in Battery saver) */
        resume_tick();
        lyrics_pump();                                              /* adopt a finished background lookup */
        if (track_loaded && strcmp(ly_asked, cur_path) != 0) {
            if (S.ly_search) lyrics_ask(false);                       /* prefetch while the song plays */
            else if (lyrics_open) lyrics_ask(false);
        }

        {   /* bottom-screen page change -> soft cross-fade (none in Battery saver) */
            const int bk = bot_state_key();
            if (bk != bot_key) {
                if (bot_key >= 0 && !S.saver) bot_fade = (bk >= 20 && bot_key >= 20) ? 0.55f : 0.90f;
                bot_key = bk;
            }
        }

        /* Battery saver: draw every other frame (30 fps); input, audio and timers keep running at full rate */
        frame_no++;
        if (S.saver && (frame_no & 1u) && search_busy == 0) { gspWaitForVBlank(); continue; }

        /* ---- draw ---- */
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        art_commit();
        if (flow_pump()) flow_want_dirty = true;       /* a cover finished loading */
        flow_update();
        C2D_TextBufClear(tbuf);

        u32 bg = C(th_bg);
        C2D_TargetClear(top, bg);
        C2D_SceneBegin(top);
        if (wizard_open && wz_page != WZ_SETUP) { bg_gradient(TOP_W); draw_wizard_top(); }
        else if (lyrics_open) draw_lyrics_top();
        else draw_top();                                     /* draws its own (ambient) background */

        C2D_TargetClear(bot, bg);
        C2D_SceneBegin(bot);
        bg_gradient(BOT_W);
        draw_frame();
        if (search_busy > 0) draw_search_busy();
        else if (confirm_exit) draw_exit_confirm();
        else if (kb_open) draw_keyboard();
        else if (wizard_open) draw_wizard_bottom();
        else if (settings_open) draw_settings();
        else if (queue_open) draw_queue();
        else draw_browser();
        draw_toast();
        if (bot_fade > 0.01f) {                                   /* page transition: fade in from the background */
            C2D_DrawRectSolid(0, 0, 0.90f, BOT_W, SCR_H, Ca(th_bg, bot_fade));
            bot_fade -= 0.085f;
        } else bot_fade = 0.0f;

        C3D_FrameEnd(0);

        if (search_busy > 0 && --search_busy == 0) run_search();     /* the busy screen has been shown: do the search */
    }

    if (!L_is_search) snprintf(S.last_dir, sizeof S.last_dir, "%s", L.dir);     /* results aren't a folder */
    settings_save(&S);
    if (favorites_dirty) favorites_save();
    aptUnhook(&lid_hook_cookie);

    if (S.resume && track_loaded && (audio_is_active() || resume_pending)) resume_save(cur_pos_ms());
    if (audio_ok) { audio_stop(); audio_exit(); }
    lyrics_exit();
    lyfont_exit();
    if (ptm_ok) ptmuExit();
    if (mcu_ok) mcuHwcExit();
    art_shutdown();
    flow_shutdown();
    dir_free(&L);
    dir_free(&Q);
    search_free();
    C2D_TextBufDelete(tbuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
