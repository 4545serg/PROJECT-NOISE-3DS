/*
 * PROJECT NOISE - native Nintendo 3DS music player
 *
 * Top screen : NOW PLAYING (artwork, title, artist, progress, time)
 * Bottom     : file browser + touch controls (or the settings page)
 */
#include <3ds.h>
#include <citro2d.h>

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
#include "meta.h"
#include "placeholder.h"
#include "search.h"
#include "settings.h"
#include "noise.h"

/* ---------------------------------------------------------------- layout */
#define LIST_TOP   36
#define ROW_H      38
#define VIS_ROWS   4
#define CTRL_TOP   188
#define SL_X0      48          /* seek slider track, x range */
#define SL_X1      272
#define SB_X0      146         /* search bar: left edge (it runs to x = 316) */
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

enum {                         /* settings rows, grouped in three pages */
    ROW_PRESET, ROW_ACCENT, ROW_GLOW, ROW_FADE,                              /* Look    */
    ROW_EQMAN, ROW_EQPRE, ROW_B0, ROW_B1, ROW_B2, ROW_B3, ROW_B4,        /* Audio   */
    ROW_SEEK, ROW_CLOCK, ROW_BATT, ROW_SLEEP_ENABLE, ROW_SLEEP_TIMER, ROW_GUIDE, ROW_EXIT, /* General */
    SET_ROWS
};

/* "Cards" are lists of settings rows drawn as one inset panel. Cards 0 and 2 are the Look and General
 * tabs (the Sound tab has its own layout); CARD_SETUP is the single combined page of the first-run setup. */
enum { CARD_LOOK, CARD_SOUND, CARD_GENERAL, CARD_SETUP, CARD_COUNT };
static const int   ROWS_LOOK[]    = { ROW_PRESET, ROW_ACCENT, ROW_GLOW, ROW_FADE };
static const int   ROWS_GENERAL[] = { ROW_SEEK, ROW_CLOCK, ROW_BATT, ROW_SLEEP_ENABLE, ROW_SLEEP_TIMER, ROW_GUIDE, ROW_EXIT };
static const int   ROWS_SETUP[]   = { ROW_PRESET, ROW_ACCENT, ROW_GLOW, ROW_CLOCK, ROW_BATT };
static const int  *CARD_ROWS[CARD_COUNT] = { ROWS_LOOK, NULL, ROWS_GENERAL, ROWS_SETUP };
static const int   CARD_LEN[CARD_COUNT]  = { 4, 0, 7, 5 };
static const float CARD_ROWH[CARD_COUNT] = { 38.0f, 0.0f, 22.0f, 25.0f };
static const int   TAB_FIRST[3]          = { ROW_PRESET, ROW_EQMAN, ROW_SEEK };
static const char *PAGE_NAMES[3]         = { "Appearance", "Audio", "General" };

#define TOP_W   400            /* screen sizes in pixels */
#define BOT_W   320
#define SCR_H   240

/* ambient glow: how strongly the cover colour tints the top screen, and how far it reaches */
static const float GLOW_STRENGTH[GLOW_COUNT] = { 0.0f, 0.18f, 0.30f };
#define GLOW_CORE   50.0f      /* half-size of the fully-lit core around the centre cover */
#define GLOW_REACH  150.0f     /* distance from that core at which the glow has faded out */

/* ----------------------------------------------------------------- state */
static Settings  S;
static DirList   L;                 /* folder shown in the browser          */
static DirList   Q;                 /* playlist: files of the folder played */
static int       qi = -1;           /* current index in Q                   */
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
static char      status_msg[96];

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
static char      toast_msg[24];
static int       toast_t = 0;
static int       ctrl_flash_t = 0;  /* press highlight on a transport button: frames left + its x */
static float     ctrl_flash_x = 0.0f;
#define CTRL_FLASH_FRAMES 10

static void      toast(const char *msg);
static void      apply_lid(void);
static void      show_favorites(void);
static void      favorites_save(void);

static bool      audio_ok = false;
static C2D_TextBuf tbuf;
static float     eq_phase = 0.0f;      /* drives the equalizer bars */
static bool      quit_req = false;
static char      cur_path[800];
static bool      slider_drag = false;
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

/* Subtle top-lit background: a faint wash of the text colour fading to the base colour. */
static Rgb bg_at(float y)                         /* background colour at screen row y (0..240) */
{
    Rgb lit = mix_rgb(S.bg, S.text, 8);
    return mix_rgb(lit, S.bg, (int)(255.0f * y / (float)SCR_H));
}

static void bg_gradient(float w)
{
    u32 t = C(bg_at(0)), b = C(bg_at((float)SCR_H));
    C2D_DrawRectangle(0, 0, 0.05f, w, (float)SCR_H, t, t, b, b);
}

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
    if (amb_k < 0.004f) { bg_gradient(TOP_W); return; }        /* glow off: the plain gradient */

    static u32 mesh[(GLOW_BANDS + 1) * (GLOW_COLS + 1)];
    static u8  key[10];
    const u8 now[10] = { (u8)amb_r, (u8)amb_g, (u8)amb_b, (u8)(amb_k * 255.0f),
                         S.bg.r, S.bg.g, S.bg.b, S.text.r, S.text.g, S.text.b };
    if (memcmp(key, now, sizeof key) != 0) {
        memcpy(key, now, sizeof key);
        for (int yi = 0; yi <= GLOW_BANDS; yi++)
            for (int xi = 0; xi <= GLOW_COLS; xi++)
                mesh[yi * (GLOW_COLS + 1) + xi] = top_bg_color((float)xi * 20.0f, GLOW_YS[yi]);
    }
    for (int yi = 0; yi < GLOW_BANDS; yi++) {
        for (int xi = 0; xi < GLOW_COLS; xi++) {
            const u32 *a = &mesh[yi * (GLOW_COLS + 1) + xi], *b = a + (GLOW_COLS + 1);
            C2D_DrawRectangle((float)xi * 20.0f, GLOW_YS[yi], 0.05f, 20.0f, GLOW_YS[yi + 1] - GLOW_YS[yi],
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
    C2D_DrawText(&t, C2D_WithColor | align, x, y, 0.5f, sc, sc, color);
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

/* --------------------------------------------------------------- playback */
static void play_q(int idx)
{
    if (idx < 0 || idx >= Q.n) return;
    qi = idx;

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
        if (manual_queue_count > 0) play_manual_queue_head();
        return;
    }
    cur = m;
    track_loaded = true;
    snprintf(cur_path, sizeof cur_path, "%s", path);
    art_queue(art, art_size);
    if (audio_ok) { audio_play(path, m.audio_start); apply_lid(); }
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
    if (Q.n == 0) return;
    if (audio_is_active() && audio_position_ms() > 3000) { play_q(qi); return; }
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
static void skip_next(void) { if (Q.n == 0 && manual_queue_count == 0) return; flash_ctrl(208); flow_show = false; next_track(true); follow_playing(); }
static void skip_prev(void) { if (Q.n == 0) return; flash_ctrl(112); flow_show = false; prev_track(); follow_playing(); }

static void toggle_play(void)
{
    if (audio_is_active()) {
        if (audio_is_paused()) audio_resume(); else audio_pause();
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

/* Shows the system keyboard; a confirmed query starts the search (see run_search). */
static void open_search(void)
{
    SwkbdState kb;
    char buf[192] = "";
    swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 40);
    swkbdSetHintText(&kb, "Search songs");
    swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, "Search", true);
    swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
    if (L_is_search) swkbdSetInitialText(&kb, search_q);
    SwkbdButton b = swkbdInputText(&kb, buf, sizeof buf);
    kb_returned = true;
    if (b == SWKBD_BUTTON_RIGHT && buf[0]) {
        snprintf(search_q, sizeof search_q, "%s", buf);
        search_busy = 2;                              /* draw the "Searching..." screen first, then scan */
    }
}

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
    if (meta_read(path, &m, NULL, NULL, false)) {
        snprintf(e->title, sizeof e->title, "%s", m.title);
        snprintf(e->artist, sizeof e->artist, "%s", m.artist);
    }
    e->meta_loaded = true;
}


/* --------------------------------------------------------------- settings */
#define ACCENT_N 9
static const u32 ACCENTS[ACCENT_N] = {           /* blue cyan green yellow orange red pink purple white */
    0x3D9CFF, 0x39B8D6, 0x35D07F, 0xFFC933, 0xFF7A45, 0xFF5A5F, 0xFF6FB5, 0xA78BFA, 0xFFFFFF
};
#define PAL_X0   70            /* accent swatches: left edge and spacing (px) */
#define PAL_STEP 26

static Rgb hex_rgb(u32 v) { Rgb c = { (u8)(v >> 16), (u8)(v >> 8), (u8)v }; return c; }

static int accent_index(void)
{
    u32 rgb = ((u32)S.accent.r << 16) | ((u32)S.accent.g << 8) | S.accent.b;
    for (int i = 0; i < ACCENT_N; i++)
        if (ACCENTS[i] == rgb) return i;
    return -1;
}

static bool is_slider_row(int r) { return r >= ROW_B0 && r <= ROW_B4; }
static int  row_page(int r)      { return r < ROW_EQMAN ? 0 : (r < ROW_SEEK ? 1 : 2); }

static bool is_switch_row(int r) { return r == ROW_BATT || r == ROW_SLEEP_ENABLE || r == ROW_EQMAN; }
static bool is_band_row(int r)   { return r >= ROW_B0 && r <= ROW_B4; }
static bool is_choice_row(int r) { return r == ROW_PRESET || r == ROW_EQPRE || r == ROW_CLOCK || r == ROW_GLOW || r == ROW_FADE || r == ROW_SEEK || r == ROW_SLEEP_TIMER; }

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
    FILE *f = fopen(FAVORITES_PATH, "w"); if (!f) return;
    for (int i=0;i<favorite_count;i++) fprintf(f, "%s\n", favorite_paths[i]);
    fclose(f); favorites_dirty = false;
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
    if (L_is_favorites) show_favorites();
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
    } else if (set_row == ROW_ACCENT) {
        int i = accent_index();
        i = (i < 0) ? (dir > 0 ? 0 : ACCENT_N - 1) : (i + dir + ACCENT_N) % ACCENT_N;
        S.accent = hex_rgb(ACCENTS[i]);
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
    } else {
        return;
    }
    mark_dirty();
}

/* touch / drag directly on an EQ band slider */
static void slider_apply(int r, int px, int py)
{
    {
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
    }
    mark_dirty();
}

/* Up / Down through the settings rows; the EQ sliders are skipped while they are locked */
static int row_step(int r, int dir)
{
    int n = r + dir;
    if (n < 0 || n >= SET_ROWS) return r;
    if (is_band_row(n) && !S.eq_manual) n = (dir > 0) ? ROW_SEEK : ROW_EQPRE;
    return n;
}

static void close_settings(void)
{
    settings_open = false;
    set_drag = -1;
    if (settings_dirty) { settings_save(&S); settings_dirty = false; }
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
    if (!have) rgb = ((u32)S.accent.r << 16) | ((u32)S.accent.g << 8) | S.accent.b;

    float tr = (float)((rgb >> 16) & 0xFF), tg = (float)((rgb >> 8) & 0xFF), tb = (float)(rgb & 0xFF);
    float tk = GLOW_STRENGTH[S.glow];
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

    /* title/artist for the settled entry: one small file read, never while scrolling */
    if (flow_idle > 6 && sel >= 0 && sel < L.n && !L.e[sel].meta_loaded && !L.e[sel].is_dir) ensure_meta(&L.e[sel]);

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

static Rgb tile_color(const char *s)
{
    unsigned h = 5381;
    for (; *s; s++) h = h * 33u + (unsigned char)*s;
    return mix_rgb(S.select, S.accent, (int)(70 + h % 140));
}

static float progress_frac(u32 pos, u32 dur)
{
    float f = dur ? (float)pos / (float)dur : 0.0f;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return f;
}

static void draw_status_bar(void);
static void row_text(const char *s, float x, float y, float rh, float sc, u32 col, u32 align, float maxw);

/* ------------------------------------------------ TOP: now playing screen */
static void draw_top(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));

    /* cover, centered. The background around it carries the ambient glow (cover colours); there is no
     * frame / backing plate, so nothing grey shows through while the covers slide past. */
    const float asz = 144, ax = 128, ay = 22;                          /* matches FACE / CY in flow.c */
    top_background();

    bool use_flow = flow_appear > 0.01f && L.n > 0;
    if (use_flow) {
        FlowItem items[10];
        const int base = (int)floorf(flow_pos) - 4;
        const int ci = (int)floorf(flow_pos + 0.5f);
        const bool playing_view = !browsing_view();
        for (int i = 0; i < 10; i++) {
            int idx = base + i;
            memset(&items[i], 0, sizeof items[i]);
            if (idx < 0 || idx >= L.n || fabsf((float)idx - flow_pos) > FLOW_SIDE + 0.6f) continue;
            Entry *e = &L.e[idx];
            if (e->is_dir) {
                items[i].kind = 1;
                items[i].color = C(mix_rgb(S.select, S.text, 60));
            } else {
                items[i].kind = 2;
                items[i].color = C(mix_rgb(bg_at(94), tile_color(e->name), 90));   /* subdued: the cover fades in from it */
                items[i].fade = 1.0f;
                if (playing_view && idx == ci && art_ready()) {          /* the playing cover fades in from the background */
                    items[i].tex = art_image()->tex;
                    items[i].color = top_bg_color(200.0f, 94.0f);
                    float f = art_fade;
                    items[i].fade = f * f * (3.0f - 2.0f * f);
                } else if (is_current_row(idx) && art_ready()) {
                    items[i].tex = art_image()->tex;
                } else {
                    items[i].tex = flow_tex(idx, &items[i].fade);
                }
                if (playing_view && idx == ci && !items[i].tex) items[i].kind = 0;   /* never a flat grey card while art loads */
            }
        }
        flow_draw(items, 10, base, flow_pos, flow_appear, top_bg_color);
    } else if (art_ready()) {
        C2D_ImageTint tint;
        float f = art_fade * art_fade * (3.0f - 2.0f * art_fade);
        C2D_PlainImageTint(&tint, top_bg_color(200.0f, 94.0f), 1.0f - f);
        C2D_DrawImageAt(*art_image(), ax, ay, 0.40f, f < 0.999f ? &tint : NULL, asz / 128.0f, asz / 128.0f);
    } else {
        C2D_DrawRectSolid(ax, ay, 0.30f, asz, asz, cSel);
        icon_note(ax + asz / 2 - 7, ay + asz / 2 - 8, cDim);
    }

    /* label above the cover; title and artist below it */
    const float TY = 170, AY = 192;                       /* title / artist rows */
    char head[40] = "NOW PLAYING";
    if (use_flow && flow_appear > 0.5f && browsing_view()) {   /* browsing: describe the focused entry */
        int fi = (int)floorf(flow_pos + 0.5f);                /* the cover that is visually in the middle */
        if (fi < 0) fi = 0;
        if (fi >= L.n) fi = L.n - 1;
        Entry *e = &L.e[fi];
        snprintf(head, sizeof head, "%d / %d", fi + 1, L.n);
        if (e->is_dir) {
            draw_text_sh(!strcmp(e->name, "..") ? "Back" : e->name, 200, TY, 0.68f, cText, C2D_AlignCenter, 360);
            draw_text_sh("Folder", 200, AY, 0.48f, cDim, C2D_AlignCenter, 300);
        } else {
            char stem[96];
            const char *nm = e->title;
            if (!(e->meta_loaded && e->title[0])) {          /* not read yet: show the file name */
                snprintf(stem, sizeof stem, "%s", base_name(e->name));
                char *dot = strrchr(stem, '.');
                if (dot) *dot = 0;
                nm = stem;
            }
            draw_text_sh(nm, 200, TY, 0.68f, cText, C2D_AlignCenter, 360);
            draw_text_sh(e->artist[0] ? e->artist : "Unknown artist", 200, AY, 0.48f, cDim, C2D_AlignCenter, 300);
        }
    } else if (track_loaded) {
        draw_text_sh(cur.title, 200, TY, 0.68f, cText, C2D_AlignCenter, 360);
        draw_text_sh(cur.artist[0] ? cur.artist : "Unknown artist", 200, AY, 0.48f, cDim, C2D_AlignCenter, 300);
    } else {
        draw_text_sh("Nothing playing", 200, TY, 0.68f, cText, C2D_AlignCenter, 360);
        draw_text_sh("Pick a song below", 200, AY, 0.48f, cDim, C2D_AlignCenter, 300);
    }

    /* header slot: a notice pill (seek toast / problem) takes priority over the label */
    if (toast_t > 0 || status_msg[0]) {
        bool bad = (toast_t <= 0);
        const char *msg = (toast_t > 0) ? toast_msg : status_msg;
        float pw = bad ? 270.0f : 116.0f;
        rrect(200 - pw / 2, 3, pw, 19, 9.5f, 0.45f, bad ? C2D_Color32(255, 90, 95, 255) : cAcc);
        draw_text(msg, 200, 5, 0.42f, C(S.bg), C2D_AlignCenter, pw - 14);
    } else {
        draw_text(head, 200, 4, 0.5f, cAcc, C2D_AlignCenter, 200);
    }

    /* times + full-width progress bar along the bottom edge */
    u32 pos = track_loaded ? audio_position_ms() : 0;
    u32 dur = track_loaded ? cur.duration_ms : 0;
    float frac = progress_frac(pos, dur);

    char a[16], b[16];
    fmt_time(a, sizeof a, pos);
    if (dur) fmt_time(b, sizeof b, dur); else snprintf(b, sizeof b, "--:--");
    draw_text(a, 14, 207, 0.40f, cDim, C2D_AlignLeft, 0);
    draw_text(b, 386, 207, 0.40f, cDim, C2D_AlignRight, 0);

    const float by = 226;
    float dx = 6.0f + 388.0f * frac;
    C2D_DrawRectSolid(0, by + 1, 0.30f, 400, 3, cSel);
    C2D_DrawRectSolid(0, by + 1, 0.35f, dx, 3, cAcc);
    if (track_loaded) C2D_DrawCircleSolid(dx, by + 2.5f, 0.38f, 9, C(mix_rgb(S.bg, S.accent, 70)));
    C2D_DrawCircleSolid(dx, by + 2.5f, 0.40f, 5.5f, cAcc);

    draw_status_bar();
}

/* ------------------------------------------- BOTTOM: library + seek slider */
static float slider_value(void)
{
    if (slider_drag) return slider_frac;
    return progress_frac(track_loaded ? audio_position_ms() : 0, track_loaded ? cur.duration_ms : 0);
}

static void draw_miniplayer(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select), cBg = C(S.bg);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));

    const float y = CTRL_TOP;
    u32 pt = C(mix_rgb(S.bg, S.select, 175)), pb = C(mix_rgb(S.bg, S.select, 105));
    C2D_DrawRectangle(0, y, 0.30f, 320, 240 - y, pt, pt, pb, pb);
    C2D_DrawRectSolid(0, y, 0.32f, 320, 1, C(mix_rgb(S.select, S.text, 40)));      /* lit top edge */

    /* seek slider: elapsed | track + knob | total */
    u32 dur = track_loaded ? cur.duration_ms : 0;
    float frac = slider_value();
    u32 shown = slider_drag ? (u32)(frac * (float)dur) : (track_loaded ? audio_position_ms() : 0);
    char a[16], b[16];
    fmt_time(a, sizeof a, shown);
    if (dur) fmt_time(b, sizeof b, dur); else snprintf(b, sizeof b, "--:--");
    draw_text(a, 10, y + 5, 0.42f, slider_drag ? cAcc : cDim, C2D_AlignLeft, 0);
    draw_text(b, 312, y + 5, 0.42f, cDim, C2D_AlignRight, 0);

    const float tx0 = SL_X0, tw = SL_X1 - SL_X0, ty = y + 12;
    rrect(tx0, ty - 2.5f, tw, 5, 2.5f, 0.34f, cSel);
    float fw = tw * frac;
    if (fw < 5) fw = 5;
    rrect(tx0, ty - 2.5f, fw, 5, 2.5f, 0.36f, cAcc);
    float kx = tx0 + tw * frac;
    C2D_DrawCircleSolid(kx, ty, 0.41f, slider_drag ? 10.0f : 7.5f, cAcc);
    C2D_DrawCircleSolid(kx, ty, 0.43f, slider_drag ? 6.5f : 4.5f, cText);

    /* transport buttons */
    const float cy = y + 37;
    bool playing = audio_is_active() && !audio_is_paused();
    if (ctrl_flash_t > 0) {                           /* press feedback: a disc that eases back into the panel colour */
        Rgb panel = mix_rgb(S.bg, S.select, 140);
        C2D_DrawCircleSolid(ctrl_flash_x, cy, 0.37f, 19, C(mix_rgb(panel, S.accent, 80 * ctrl_flash_t / CTRL_FLASH_FRAMES)));
    }
    icon_prev(112, cy, 8, cText);
    C2D_DrawCircleSolid(160, cy, 0.38f, 17, playing ? C(mix_rgb(S.bg, S.accent, 110)) : cSel);   /* status ring */
    C2D_DrawCircleSolid(160, cy, 0.40f, 14, cText);
    if (playing) icon_pause(160, cy, 8, cBg); else icon_play(160, cy, 8, cBg);
    icon_next(208, cy, 8, cText);
    /* Queue shortcut: three compact lines and a count badge when items are waiting. */
    for (int k = 0; k < 3; k++) {
        float yy = cy - 6.0f + k * 6.0f;
        C2D_DrawRectSolid(290, yy, 0.45f, 14, 1.8f, cDim);
        C2D_DrawCircleSolid(287, yy + 0.9f, 0.45f, 1.2f, cDim);
    }
    if (manual_queue_count > 0) {
        char qc[5]; snprintf(qc, sizeof qc, "%d", manual_queue_count > 99 ? 99 : manual_queue_count);
        C2D_DrawCircleSolid(307, cy - 10, 0.46f, 6.5f, cAcc);
        draw_text(qc, 307, cy - 14.5f, 0.30f, cBg, C2D_AlignCenter, 0);
    }

    /* shuffle (left) and repeat (right): accent colour + a dot underneath when on, "1" badge for repeat-one */
    icon_shuffle(64, cy, 8, S.shuffle ? cAcc : cDim);
    if (S.shuffle) C2D_DrawCircleSolid(64, cy + 12.5f, 0.45f, 1.7f, cAcc);
    icon_repeat(256, cy, 8, S.repeat ? cAcc : cDim);
    if (S.repeat) C2D_DrawCircleSolid(256, cy + 12.5f, 0.45f, 1.7f, cAcc);
    if (S.repeat == 2) {
        C2D_DrawCircleSolid(265, cy - 8, 0.47f, 4.6f, cAcc);
        draw_text("1", 265, cy - 8 - 4.4f, 0.32f, cBg, C2D_AlignCenter, 0);
    }
}


static void draw_browser(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select), cBg = C(S.bg);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));

    /* large title: "Library" at the top level, the folder name inside a folder, "Results" while searching */
    const char *title = L_is_search ? "Results" : (L_is_favorites ? "Favorites" : "Library");
    char fname[256] = "";
    if (!L_is_search && !is_music_root(L.dir)) {
        char tmp[512];
        snprintf(tmp, sizeof tmp, "%s", L.dir);
        size_t n = strlen(tmp);
        if (n > 0 && tmp[n - 1] == '/') tmp[--n] = 0;
        char *sl = strrchr(tmp, '/');
        snprintf(fname, sizeof fname, "%s", sl ? sl + 1 : tmp);
        if (fname[0]) title = fname;
    }
    draw_text(title, 12, 3, 0.85f, cText, C2D_AlignLeft, 112);
    draw_text("♥", 132, 5, 0.62f, L_is_favorites ? cAcc : cDim, C2D_AlignCenter, 0);

    /* search bar: tap to type; while showing results it holds the query and an "x" to clear it */
    {
        const float bx = SB_X0, by = 7, bw = 316 - SB_X0, bh = 22, cy = by + bh / 2;
        Rgb barRgb = L_is_search ? mix_rgb(S.select, S.accent, 90) : S.select;
        u32 barc = C(barRgb);
        rrect(bx, by, bw, bh, bh / 2, 0.30f, barc);
        icon_search(bx + 14, cy, L_is_search ? cText : cDim, barc);
        if (L_is_search) {
            row_text(search_q, bx + 28, by, bh, 0.45f, cText, C2D_AlignLeft, bw - 28 - 26);
            const float xc = bx + bw - 14;
            C2D_DrawCircleSolid(xc, cy, 0.34f, 7.5f, C(mix_rgb(barRgb, S.text, 60)));
            C2D_DrawLine(xc - 2.6f, cy - 2.6f, cBg, xc + 2.6f, cy + 2.6f, cBg, 1.7f, 0.46f);
            C2D_DrawLine(xc - 2.6f, cy + 2.6f, cBg, xc + 2.6f, cy - 2.6f, cBg, 1.7f, 0.46f);
        } else {
            row_text("Search songs", bx + 28, by, bh, 0.45f, cDim, C2D_AlignLeft, bw - 36);
        }
    }

    if (L.n == 0) {
        C2D_DrawCircleSolid(160, 82, 0.30f, 26, cSel);
        icon_note(153, 73, cDim);
        if (L_is_favorites) {
            draw_text("No favorites yet", 160, 116, 0.6f, cText, C2D_AlignCenter, 300);
            draw_text("Tap a song's heart to add it here", 160, 140, 0.4f, cDim, C2D_AlignCenter, 300);
        } else {
            draw_text("No music here yet", 160, 116, 0.6f, cText, C2D_AlignCenter, 300);
            draw_text("Copy .mp3, .flac or .wav files to SD:/Music/", 160, 140, 0.4f, cDim, C2D_AlignCenter, 300);
        }
    } else if (L_is_search && L.n <= 1) {                 /* only the "Back" row: nothing matched */
        draw_text("No songs match", 160, 104, 0.6f, cText, C2D_AlignCenter, 300);
        draw_text("Searches file and folder names in SD:/Music/", 160, 130, 0.4f, cDim, C2D_AlignCenter, 300);
    }

    int loads_this_frame = 0;                          /* at most one tag read per frame while browsing */
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = scroll_top + i;
        if (idx >= L.n) break;
        Entry *e = &L.e[idx];
        float y = LIST_TOP + i * ROW_H;
        bool cur_row = is_current_row(idx);
        bool playing = cur_row && audio_is_active() && !audio_is_paused();

        if (idx == sel) {
            rrect(4, y + 1, 304, ROW_H - 2, 8, 0.25f, C(mix_rgb(S.select, S.accent, 45)));
            rrect(5, y + ROW_H / 2 - 8, 3, 16, 1.5f, 0.33f, cAcc);                    /* selection bar */
        }
        u32 col = cur_row ? cAcc : cText;

        /* square thumbnail tile */
        const float tx = 12, ty = y + 4, ts = 30;
        Rgb tc = e->is_dir ? mix_rgb(S.select, S.text, 60) : tile_color(e->name);
        rrect(tx, ty, ts, ts, 6, 0.32f, C(tc));
        if (e->is_dir) {
            C2D_DrawRectSolid(tx + 6, ty + 8, 0.45f, 7, 4, cText);
            C2D_DrawRectSolid(tx + 6, ty + 12, 0.45f, 18, 10, cText);
        } else if (playing) {
            for (int k = 0; k < 3; k++) {
                float h = 6.0f + 13.0f * (0.5f + 0.5f * sinf(eq_phase * 2.0f + k * 1.7f));
                C2D_DrawRectSolid(tx + 6 + k * 7, ty + ts - 6 - h, 0.45f, 5, h, cAcc);
            }
        } else {
            icon_note(tx + 8, ty + 7, cText);
        }

        /* text */
        const float lx = 52;
        if (e->is_dir) {
            draw_text(!strcmp(e->name, "..") ? "Back" : e->name, lx, y + 10, 0.62f, col, C2D_AlignLeft, 254);
        } else {
            if (!e->meta_loaded && loads_this_frame < 1 && flow_idle > 2) { ensure_meta(e); loads_this_frame++; }
            const char *name = (e->meta_loaded && e->title[0]) ? e->title : base_name(e->name);
            draw_text(name, lx, y + 3, 0.58f, col, C2D_AlignLeft, 224);
            char favpath[256]; entry_relative_path(e, favpath, sizeof favpath);
            bool fav = is_favorite(favpath);
            draw_text(fav ? "♥" : "♡", 296, y + 7, 0.58f, fav ? cAcc : cDim, C2D_AlignCenter, 0);
            if (e->meta_loaded && e->artist[0]) {
                draw_text(e->artist, lx, y + 21, 0.44f, cDim, C2D_AlignLeft, 224);
            } else if (L_is_search) {                              /* no artist tag: show which folder it lives in */
                char fd[256];
                folder_of(e->name, fd, sizeof fd);
                if (fd[0]) draw_text(fd, lx, y + 21, 0.44f, cDim, C2D_AlignLeft, 254);
            }
        }

        C2D_DrawRectSolid(lx, y + ROW_H - 1, 0.30f, 256, 1, cSel);   /* divider */
    }

    if (L.n > VIS_ROWS) {
        float track_h = VIS_ROWS * ROW_H;
        float th = track_h * VIS_ROWS / L.n;
        if (th < 12) th = 12;
        float ty2 = LIST_TOP + (track_h - th) * scroll_top / (L.n - VIS_ROWS);
        C2D_DrawRectSolid(316, LIST_TOP, 0.30f, 3, track_h, cSel);
        C2D_DrawRectSolid(316, ty2, 0.35f, 3, th, cAcc);
    }

    draw_miniplayer();
}

/* A dedicated Up Next screen makes the manual queue discoverable and manageable. */
static void draw_queue(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));

    draw_text("Up Next", 12, 3, 0.85f, cText, C2D_AlignLeft, 180);
    char count[32]; snprintf(count, sizeof count, "%d queued", manual_queue_count);
    draw_text(count, 246, 9, 0.40f, cDim, C2D_AlignRight, 80);
    draw_text("Tap a song to play it now", 14, 23, 0.40f, cDim, C2D_AlignLeft, 220);
    rrect(260, 5, 52, 22, 11, 0.30f, cAcc);
    draw_text("Done", 286, 10, 0.42f, C(S.bg), C2D_AlignCenter, 0);

    if (manual_queue_count == 0) {
        rrect(18, 54, 284, 112, 12, 0.28f, cCard);
        C2D_DrawCircleSolid(160, 86, 0.30f, 19, cSel);
        icon_note(153, 78, cAcc);
        draw_text("Nothing queued yet", 160, 116, 0.56f, cText, C2D_AlignCenter, 270);
        draw_text("Browse your music and press Y to add a song", 160, 139, 0.37f, cDim, C2D_AlignCenter, 270);
        draw_text("Press X to put it at the front of Up Next", 160, 153, 0.34f, cDim, C2D_AlignCenter, 270);
    } else {
        const int visible = 4;
        if (queue_sel < queue_scroll) queue_scroll = queue_sel;
        if (queue_sel >= queue_scroll + visible) queue_scroll = queue_sel - visible + 1;
        for (int i = 0; i < visible; i++) {
            int idx = queue_scroll + i;
            if (idx >= manual_queue_count) break;
            float y = 48.0f + i * 34.0f;
            const char *base = base_name(manual_queue[idx]);
            char title[160]; snprintf(title, sizeof title, "%s", base);
            char *dot = strrchr(title, '.'); if (dot) *dot = 0;
            const char *slash = strrchr(manual_queue[idx], '/');
            char folder[160] = "";
            if (slash) {
                size_t n = (size_t)(slash - manual_queue[idx]);
                if (n >= sizeof folder) n = sizeof folder - 1;
                memcpy(folder, manual_queue[idx], n); folder[n] = 0;
                const char *last = strrchr(folder, '/');
                if (last) memmove(folder, last + 1, strlen(last + 1) + 1);
            }
            if (idx == queue_sel) rrect(6, y, 308, 31, 8, 0.28f, C(mix_rgb(S.select, S.accent, 55)));
            else rrect(6, y, 308, 31, 8, 0.24f, cCard);
            rrect(12, y + 6, 19, 19, 5, 0.30f, cSel);
            draw_text("♪", 21.5f, y + 8, 0.42f, cAcc, C2D_AlignCenter, 0);
            draw_text(title, 40, y + 3, 0.48f, cText, C2D_AlignLeft, 218);
            draw_text(folder[0] ? folder : "Music", 40, y + 18, 0.34f, cDim, C2D_AlignLeft, 210);
            draw_text("×", 296, y + 6, 0.58f, cDim, C2D_AlignCenter, 0);
        }
    }
    /* Keep the queue view calm: no destructive X/Y shortcuts or footer crowding. */
    draw_miniplayer();
}


/* ---- settings: iOS-style inset cards + large title + segmented control,
 *      Material-style filled sliders, swatches and switches ---- */
static void row_text(const char *s, float x, float y, float rh, float sc, u32 col, u32 align, float maxw)
{
    draw_text(s, x, y + (rh - 28.0f * sc) / 2.0f, sc, col, align, maxw);
}

static void chevron(float cx, float cy, bool right, u32 c)
{
    float d = right ? -3.0f : 3.0f;
    C2D_DrawTriangle(cx + d, cy - 5, c, cx + d, cy + 5, c, cx - d, cy, c, 0.45f);
}

static void m_slider(float x0, float x1, float cy, float frac, float from, bool active,
                     u32 cTrack, u32 cAcc, u32 cHalo)
{
    rrect(x0, cy - 2, x1 - x0, 4, 2, 0.34f, cTrack);
    float a = x0 + (x1 - x0) * from, b = x0 + (x1 - x0) * frac;
    if (a > b) { float t = a; a = b; b = t; }
    if (b - a < 4) { a = b - 2; b = b + 2; }
    if (b - a >= 5) rrect(a, cy - 2, b - a, 4, 2, 0.36f, cAcc);
    else C2D_DrawRectSolid(a, cy - 2, 0.36f, b - a, 4, cAcc);
    float kx = x0 + (x1 - x0) * frac;
    if (active) C2D_DrawCircleSolid(kx, cy, 0.40f, 10, cHalo);
    C2D_DrawCircleSolid(kx, cy, 0.42f, 6.5f, cAcc);
}

static void switch_ctl(float x, float cy, bool on, u32 cOn, u32 cOff, u32 cKnob)
{
    rrect(x, cy - 12, 44, 24, 12, 0.34f, on ? cOn : cOff);
    C2D_DrawCircleSolid(on ? x + 32 : x + 12, cy, 0.42f, 9, cKnob);
}

/* One inset card of settings rows (used by the Settings tabs and by the setup steps). */
static void switch_sm(float x, float cy, bool on, u32 cOn, u32 cOff, u32 cKnob)   /* compact switch for tight rows */
{
    rrect(x, cy - 7.5f, 30, 15, 7.5f, 0.34f, on ? cOn : cOff);
    C2D_DrawCircleSolid(on ? x + 22.5f : x + 7.5f, cy, 0.42f, 5.5f, cKnob);
}

static void draw_card(int card)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));
    u32 cHi = C(mix_rgb(S.select, S.accent, 60));
    u32 cDanger = C2D_Color32(255, 90, 95, 255);

    const int *rows = CARD_ROWS[card];
    const int n = CARD_LEN[card];
    const float rh = CARD_ROWH[card], top = 70;
    const bool tight = rh < 30.0f;                 /* compact rows: one-line labels, small switches */
    rrect(6, top, 308, n * rh, 10, 0.26f, cCard);
    C2D_DrawRectSolid(16, top, 0.27f, 288, 1, C(mix_rgb(S.bg, S.text, 30)));          /* lit top edge */

    for (int i = 0; i < n; i++) {
        int r = rows[i];
        float y = top + i * rh, cy = y + rh / 2;
        bool on = (r == set_row);
        char buf[64];

        if (on) {
            rrect(8, y + 1, 304, rh - 2, 8, 0.28f, cHi);
            rrect(9, y + rh / 2 - 7, 3, 14, 1.5f, 0.32f, cAcc);        /* nav-indicator bar */
        } else if (i > 0 && rows[i - 1] != set_row) {
            C2D_DrawRectSolid(18, y, 0.29f, 284, 1, cSel);
        }

        if (r == ROW_PRESET) {
            row_text("Theme", 18, y, rh, 0.5f, cText, C2D_AlignLeft, 60);
            Rgb pv[3] = { mix_rgb(S.bg, S.text, 40), S.select, S.accent };      /* live preview dots */
            for (int k = 0; k < 3; k++) {
                C2D_DrawCircleSolid(84 + k * 12, cy, 0.38f, 8.5f, cText);
                C2D_DrawCircleSolid(84 + k * 12, cy, 0.40f + k * 0.01f, 7, C(pv[k]));
            }
            snprintf(buf, sizeof buf, "%s", S.preset >= 0 ? PRESET_NAMES[S.preset] : "Custom");
            row_text(buf, 236, y, rh, 0.5f, cAcc, C2D_AlignCenter, 92);
            chevron(176, cy, false, cDim);
            chevron(296, cy, true, cDim);
        } else if (r == ROW_ACCENT) {
            row_text("Accent", 18, y, rh, 0.5f, cText, C2D_AlignLeft, 50);
            int ai = accent_index();
            const float sr = tight ? 7.5f : 9.5f;                              /* swatch radius */
            for (int k = 0; k < ACCENT_N; k++) {
                float sx = PAL_X0 + PAL_STEP * k + PAL_STEP / 2.0f;
                if (k == ai) {
                    C2D_DrawCircleSolid(sx, cy, 0.38f, sr + 3.0f, cText);
                    C2D_DrawCircleSolid(sx, cy, 0.39f, sr + 1.0f, on ? cHi : cCard);
                }
                C2D_DrawCircleSolid(sx, cy, 0.40f, (k == ai) ? sr - 1.0f : sr, C(hex_rgb(ACCENTS[k])));
            }
        } else if (r == ROW_GLOW) {
            if (!tight) {
                float ty = y + (rh - 30.0f) / 2.0f;
                draw_text("Ambient glow", 18, ty, 0.5f, cText, C2D_AlignLeft, 150);
                draw_text("Glow from the album art", 18, ty + 16, 0.34f, cDim, C2D_AlignLeft, 150);
            } else {
                row_text("Ambient glow", 18, y, rh, 0.5f, cText, C2D_AlignLeft, 150);
            }
            row_text(GLOW_NAMES[S.glow], 236, y, rh, 0.5f, S.glow ? cAcc : cDim, C2D_AlignCenter, 92);
            chevron(176, cy, false, cDim);
            chevron(296, cy, true, cDim);
        } else if (r == ROW_FADE) {
            if (!tight) {
                float ty = y + (rh - 30.0f) / 2.0f;
                draw_text("Cover fade", 18, ty, 0.5f, cText, C2D_AlignLeft, 150);
                draw_text("Fade-in on new songs", 18, ty + 16, 0.34f, cDim, C2D_AlignLeft, 150);
            } else {
                row_text("Cover fade", 18, y, rh, 0.5f, cText, C2D_AlignLeft, 150);
            }
            row_text(FADE_NAMES[S.fade_speed], 236, y, rh, 0.5f, cAcc, C2D_AlignCenter, 92);
            chevron(176, cy, false, cDim);
            chevron(296, cy, true, cDim);
        } else if (r == ROW_SEEK) {
            row_text("Skip length", 18, y, rh, 0.5f, cText, C2D_AlignLeft, 100);
            snprintf(buf, sizeof buf, "%d sec", S.seek_step);
            row_text(buf, 236, y, rh, 0.5f, cAcc, C2D_AlignCenter, 92);
            chevron(176, cy, false, cDim);
            chevron(296, cy, true, cDim);
        } else if (r == ROW_SLEEP_ENABLE) {
            row_text("Sleep timer", 18, y, rh, 0.5f, cText, C2D_AlignLeft, 150);
            row_text(sleep_timer_active ? "ON" : "OFF", 238, y, rh, 0.42f,
                     sleep_timer_active ? cAcc : cDim, C2D_AlignCenter, 28);
            switch_sm(276, cy, sleep_timer_active, cAcc, cSel, cText);
        } else if (r == ROW_SLEEP_TIMER) {
            char timer_label[32];
            snprintf(timer_label, sizeof timer_label, "%d min", S.sleep_minutes);
            row_text("Duration", 18, y, rh, 0.5f, cText, C2D_AlignLeft, 120);
            row_text(timer_label, 236, y, rh, 0.48f, cAcc, C2D_AlignCenter, 110);
            chevron(176, cy, false, cDim); chevron(296, cy, true, cDim);
        } else if (r == ROW_CLOCK) {
            row_text("Clock", 18, y, rh, 0.5f, cText, C2D_AlignLeft, 80);
            static const char *CLK[3] = { "Off", "12-hour", "24-hour" };
            row_text(CLK[S.clock_mode], 236, y, rh, 0.5f, S.clock_mode ? cAcc : cDim, C2D_AlignCenter, 92);
            chevron(176, cy, false, cDim);
            chevron(296, cy, true, cDim);
        } else if (r == ROW_BATT) {
            row_text("Battery percentage", 18, y, rh, 0.5f, cText, C2D_AlignLeft, 230);
            if (tight) switch_sm(276, cy, S.show_batt, cAcc, cSel, cText);
            else       switch_ctl(262, cy, S.show_batt, cAcc, cSel, cText);
        } else if (r == ROW_GUIDE) {
            row_text("Show setup guide", 18, y, rh, 0.5f, cText, C2D_AlignLeft, 230);
            chevron(296, cy, true, cDim);
        } else {
            row_text("Exit app", 160, y, rh, 0.5f, cDanger, C2D_AlignCenter, 0);
        }
    }
}

/* Audio tab: Manual EQ + preset and five vertical band sliders. System volume is controlled by the 3DS. */
static void draw_sound(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));
    u32 cHi = C(mix_rgb(S.select, S.accent, 60));
    u32 cHalo = C(mix_rgb(S.select, S.accent, 110));
    u32 cLock = C(mix_rgb(S.accent, S.bg, 150));
    char buf[32];

    rrect(6, SND_TOP, 308, SND_BOTTOM - SND_TOP, 10, 0.26f, cCard);
    C2D_DrawRectSolid(16, SND_TOP, 0.27f, 288, 1, C(mix_rgb(S.bg, S.text, 30)));          /* lit top edge */

    /* -- Manual EQ switch (left) and preset selector (right) -- */
    {
        const float y = SND_CTL_Y, cy = y + SND_CTL_H / 2.0f;
        bool on_m = (set_row == ROW_EQMAN), on_p = (set_row == ROW_EQPRE);
        if (!(on_m || on_p))
            C2D_DrawRectSolid(18, y, 0.29f, 284, 1, cSel);
        if (on_m) {
            rrect(8, y + 1, 148, SND_CTL_H - 2, 8, 0.28f, cHi);
            rrect(9, cy - 7, 3, 14, 1.5f, 0.32f, cAcc);
        }
        if (on_p) {
            rrect(160, y + 1, 152, SND_CTL_H - 2, 8, 0.28f, cHi);
            rrect(161, cy - 7, 3, 14, 1.5f, 0.32f, cAcc);
        }
        row_text("Manual EQ", 18, y, SND_CTL_H, 0.45f, cText, C2D_AlignLeft, 80);
        switch_sm(100, cy, S.eq_manual, cAcc, cSel, cText);
        row_text(S.eq_manual ? "User" : EQ_PRESET_NAMES[S.eq_preset], 236, y, SND_CTL_H, 0.5f,
                 S.eq_manual ? cDim : cAcc, C2D_AlignCenter, 92);
        if (!S.eq_manual) {
            chevron(176, cy, false, cDim);
            chevron(296, cy, true, cDim);
        }
    }

    /* -- vertical sliders -- */
    if (!(set_row == ROW_EQMAN || set_row == ROW_EQPRE))
        C2D_DrawRectSolid(18, SND_EQ_Y, 0.29f, 284, 1, cSel);

    static const char *BAND[EQ_BANDS] = { "60 Hz", "230 Hz", "910 Hz", "3.6 kHz", "14 kHz" };
    const float th = EQ_TRK_Y1 - EQ_TRK_Y0, mid = (EQ_TRK_Y0 + EQ_TRK_Y1) / 2.0f;
    const bool locked = !S.eq_manual;                       /* preset playing: sliders show its curve */

    static const char *SCALE[3] = { "+12", "0", "-12" };    /* dB scale */
    const float scale_y[3] = { EQ_TRK_Y0, mid, EQ_TRK_Y1 };
    for (int k = 0; k < 3; k++)
        draw_text(SCALE[k], 34, scale_y[k] - 4.5f, 0.32f, cDim, C2D_AlignRight, 0);
    C2D_DrawRectSolid(EQ_COL_X0 + 2, mid - 0.5f, 0.30f, EQ_COL_W * EQ_BANDS - 4, 1, cSel);   /* 0 dB line */

    for (int i = 0; i < EQ_BANDS; i++) {
        const int r = ROW_B0 + i;
        const float cx = EQ_COL_X0 + EQ_COL_W * i + EQ_COL_W / 2.0f;
        bool focus = (set_row == r) && !locked, drag = (set_drag == r);
        int g = S.eq[i];
        float ky = EQ_TRK_Y1 - (float)(g + 12) / 24.0f * th;       /* knob position */
        u32 cFill = locked ? cLock : cAcc;

        if (focus) rrect(cx - 25, SND_EQ_Y + 1, 50, SND_BOTTOM - SND_EQ_Y - 3, 8, 0.28f, cHi);

        rrect(cx - 2.5f, EQ_TRK_Y0 - 2, 5, th + 4, 2.5f, 0.34f, cSel);              /* track */
        float fa = (ky < mid) ? ky : mid, fb = (ky < mid) ? mid : ky;                 /* fill: 0 dB -> knob */
        if (fb - fa >= 5) rrect(cx - 2.5f, fa, 5, fb - fa, 2.5f, 0.36f, cFill);
        else if (fb - fa > 0.5f) C2D_DrawRectSolid(cx - 2.5f, fa, 0.36f, 5, fb - fa, cFill);

        if (focus || drag) C2D_DrawCircleSolid(cx, ky, 0.40f, 9.5f, cHalo);
        C2D_DrawCircleSolid(cx, ky, 0.42f, 7.5f, cFill);
        if (!locked) C2D_DrawCircleSolid(cx, ky, 0.43f, 3.5f, cText);

        if (g == 0) snprintf(buf, sizeof buf, "0"); else snprintf(buf, sizeof buf, "%+d", g);
        draw_text(buf, cx, 115, 0.38f, locked ? cDim : ((focus || drag) ? cAcc : cText), C2D_AlignCenter, 0);
        draw_text(BAND[i], cx, 210, 0.36f, (set_row == r) ? cAcc : cDim, C2D_AlignCenter, 0);
    }
}

static void draw_settings(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select), cBg = C(S.bg);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));

    /* title and listening-personalization subtitle */
    draw_text("Settings", 14, 5, 0.82f, cText, C2D_AlignLeft, 0);
    draw_text("Make your listening experience yours", 15, 27, 0.47f, cDim, C2D_AlignLeft, 230);
    rrect(250, 10, 62, 22, 11, 0.30f, cAcc);
    row_text("Done", 281, 10, 22, 0.45f, cBg, C2D_AlignCenter, 0);

    /* segmented control */
    int page = row_page(set_row);
    rrect(8, 42, 304, 22, 11, 0.28f, cSel);
    rrect(10 + page * 100, 44, 100, 18, 9, 0.32f, cAcc);
    for (int i = 0; i < 3; i++)
        row_text(PAGE_NAMES[i], 60 + i * 100, 44, 18, 0.45f, i == page ? cBg : cDim, C2D_AlignCenter, 0);

    if (page == 1) draw_sound();
    else draw_card(page == 0 ? CARD_LOOK : CARD_GENERAL);
}

/* ---------------------------------------------- top-screen clock + battery */
static void batt_poll(void)
{
    u8 v = 0, c = 0;
    if (mcu_ok && R_SUCCEEDED(MCUHWC_GetBatteryLevel(&v))) batt_pct = v;
    else if (ptm_ok && R_SUCCEEDED(PTMU_GetBatteryLevel(&v))) batt_pct = (int)v * 20;   /* coarse 0-5 bars */
    if (batt_pct > 100) batt_pct = 100;
    if (ptm_ok && R_SUCCEEDED(PTMU_GetBatteryChargeState(&c))) batt_chg = (c != 0);
}

static void draw_status_bar(void)
{
    u32 cAcc = C(S.accent), cBg = C(S.bg);
    u32 cInk = C(mix_rgb(S.text, S.bg, 70));

    if (S.clock_mode) {
        time_t t = time(NULL);
        struct tm *tmv = localtime(&t);
        if (tmv) {
            char b[24];
            if (S.clock_mode == 2) {
                snprintf(b, sizeof b, "%02d:%02d", tmv->tm_hour, tmv->tm_min);
            } else {
                int h = tmv->tm_hour % 12;
                snprintf(b, sizeof b, "%d:%02d %s", h ? h : 12, tmv->tm_min, tmv->tm_hour >= 12 ? "PM" : "AM");
            }
            draw_text(b, 8, 2, 0.45f, cInk, C2D_AlignLeft, 0);
        }
    }

    if (sleep_timer_active) {
        u64 left = sleep_timer_deadline > osGetTime() ? sleep_timer_deadline - osGetTime() : 0;
        char timer[32];
        snprintf(timer, sizeof timer, "SLEEP %02u:%02u", (unsigned)(left/60000), (unsigned)((left/1000)%60));
        draw_text(timer, 70, 2, 0.38f, cAcc, C2D_AlignLeft, 88);
    }
    if (S.show_batt) {
        char b[8];
        if (batt_pct >= 0) snprintf(b, sizeof b, "%d%%", batt_pct); else snprintf(b, sizeof b, "--%%");
        u32 fill = batt_chg ? cAcc : ((batt_pct >= 0 && batt_pct <= 15) ? C2D_Color32(255, 90, 95, 255) : cInk);
        draw_text(b, 365, 2, 0.45f, batt_chg ? cAcc : cInk, C2D_AlignRight, 0);
        const float bx = 369, by = 4;                                /* little battery icon */
        C2D_DrawRectSolid(bx, by, 0.50f, 21, 11, cInk);
        C2D_DrawRectSolid(bx + 21, by + 3, 0.50f, 2, 5, cInk);
        C2D_DrawRectSolid(bx + 1, by + 1, 0.51f, 19, 9, cBg);
        float fw = 17.0f * (float)(batt_pct < 0 ? 0 : batt_pct) / 100.0f;
        if (fw > 0.5f) C2D_DrawRectSolid(bx + 2, by + 2, 0.52f, fw, 7, fill);
    }
}

/* ------------------------------------------------------- first-run setup */
static void start_wizard(void)
{
    wizard_open = true;
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
    settings_save(&S);
    settings_dirty = false;
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
    u32 cText = C(S.text), cAcc = C(S.accent);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));

    rrect(14, 12, 372, 214, 16, 0.20f, cCard);
    C2D_DrawRectSolid(30, 12, 0.21f, 340, 1, C(mix_rgb(S.bg, S.text, 30)));

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
        wz_line("A",      "Play song / open folder",          50,  cAcc, cText);
        wz_line("B",      "Pause",                            72,  cAcc, cText);
        wz_line("L / R",  "Previous / next song",             94,  cAcc, cText);
        wz_line("D-pad",  skip,                               116, cAcc, cText);
        wz_line("",       "Up / Down: move through the list", 138, cAcc, cText);
        wz_line("X / Y",  "Play next / Add to queue",                            160, cAcc, cText);
        wz_line("SELECT", "Open Settings",                    182, cAcc, cText);
        wz_line("START",  "Exit the app",                     204, cAcc, cText);
    }
}

static void draw_wizard_bottom(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select), cBg = C(S.bg);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));
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
            "Tap the queue button to open Up Next",
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
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));

    C2D_DrawCircleSolid(160, 78, 0.30f, 26, cSel);
    icon_search(160, 78, cAcc, cSel);
    draw_text("Searching...", 160, 112, 0.7f, cText, C2D_AlignCenter, 300);
    draw_text(search_index_ready() ? "Looking through your music" : "Reading SD:/Music/ - the first search takes a moment",
              160, 142, 0.4f, cDim, C2D_AlignCenter, 300);
}

static void draw_exit_confirm(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select), cBg = C(S.bg);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));
    u32 cDanger = C2D_Color32(255, 90, 95, 255);
    Rgb black = { 0, 0, 0 };

    rrect(26, 66, 268, 124, 16, 0.22f, C(mix_rgb(bg_at(120), black, 170)));        /* shadow */
    rrect(24, 60, 272, 124, 16, 0.26f, cCard);
    C2D_DrawRectSolid(40, 60, 0.27f, 240, 1, C(mix_rgb(S.bg, S.text, 30)));

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

static void handle_ctrl_tap(int px, int py)      /* bottom bar: seek, transport and Up Next */
{
    if (py < CTRL_TOP + 24) {
        if (cur.duration_ms && track_loaded && px >= SL_X0 - 14 && px <= SL_X1 + 14) {
            slider_drag = true;
            slider_frac = frac_from_px(px);
        }
        return;
    }
    if (px < 88)       { flash_ctrl(64);  S.shuffle = !S.shuffle; mark_dirty(); }
    else if (px < 136) skip_prev();
    else if (px < 184) { flash_ctrl(160); toggle_play(); }
    else if (px < 232) skip_next();
    else if (px < 278) { flash_ctrl(256); S.repeat = (S.repeat + 1) % 3; mark_dirty(); }
    else               { flash_ctrl(298); queue_open = !queue_open; queue_sel = 0; queue_scroll = 0; }
}

static void handle_header_tap(int px)    /* search and Favorites shortcuts */
{
    if (px >= 120 && px < SB_X0 - 2) { if (L_is_favorites) set_dir(S.last_dir, NULL); else show_favorites(); return; }
    if (px < SB_X0 - 4) return;
    if (L_is_search && px >= 316 - 30) exit_search();
    else open_search();
}

static void set_accent(int i)
{
    if (i < 0) i = 0;
    if (i >= ACCENT_N) i = ACCENT_N - 1;
    S.accent = hex_rgb(ACCENTS[i]);
    mark_dirty();
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
    if (is_switch_row(r)) { set_adjust(+1); return; }

    if (is_slider_row(r)) {
        if (tp.px >= SS_X0 - 12 && tp.px <= SS_X1 + 12) { set_drag = r; slider_apply(r, tp.px, tp.py); }
    } else if (is_choice_row(r)) {
        if (tp.px >= 150 && tp.px < 232)      set_adjust(-1);
        else if (tp.px >= 232)                set_adjust(+1);
    } else if (r == ROW_ACCENT) {
        if (tp.px >= PAL_X0) set_accent(((int)tp.px - PAL_X0) / PAL_STEP);
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
        int t = ((int)tp.px - 10) / 100;
        if (t < 0) t = 0;
        if (t > 2) t = 2;
        set_row = TAB_FIRST[t];
        return;
    }
    int page = row_page(set_row);
    if (page == 1) sound_touch(tp); else card_touch(tp, page == 0 ? CARD_LOOK : CARD_GENERAL);
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
    favorites_load();
    aptHook(&lid_hook_cookie, apt_lid_hook, NULL);
    apply_lid();

    mcu_ok = R_SUCCEEDED(mcuHwcInit());      /* battery percentage (optional) */
    ptm_ok = R_SUCCEEDED(ptmuInit());        /* charging state / fallback level (optional) */

    audio_ok = audio_init();
    audio_set_volume(100);
    eq_push();
    art_set_default(placeholder_png, placeholder_png_len);
    art_queue(NULL, 0);                 /* show the logo cover until a song with art plays */
    flow_init(placeholder_png, placeholder_png_len);
    if (!audio_ok) snprintf(status_msg, sizeof status_msg, "No sound - dspfirm.cdc missing");

    set_dir(S.last_dir, NULL);
    settings_dirty = false;
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
        if (!dlg && (kd & KEY_START)) { confirm_exit = true; confirm_sel = 0; }
        if (quit_req) break;

        /* ---- sleep timer: elapsed wall-clock time, including time spent in system sleep ---- */
        if (sleep_timer_active && osGetTime() >= sleep_timer_deadline) {
            sleep_timer_active = false; audio_stop(); apply_lid(); toast("Sleep timer ended");
        }

        /* ---- track end / error handling ---- */
        if (audio_take_finished()) {
            if (S.repeat == 2) play_q(qi); else next_track(false);
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
                else if (tp.py >= 48 && tp.py < 184 && manual_queue_count > 0) {
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
                if (repeat_fire(kh, KEY_DRIGHT, &hold_right)) set_row = (set_row < ROW_B4) ? set_row + 1 : ROW_SEEK;
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
                    if (set_row == ROW_SLEEP_ENABLE) set_adjust(+1);
                    else if (set_row == ROW_SLEEP_TIMER) set_adjust(+1);
                    else if (is_switch_row(set_row) || set_row == ROW_CLOCK || set_row == ROW_GLOW || set_row == ROW_FADE || set_row == ROW_SEEK) set_adjust(+1);
                    else if (set_row == ROW_GUIDE) start_wizard();
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
            if ((kd & KEY_B) && audio_is_active() && !audio_is_paused()) { audio_pause(); apply_lid(); }
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
                else if (tp.py < LIST_TOP) handle_header_tap(tp.px);
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
                if (row >= 0 && row < L.n && !L.e[row].is_dir && tp_start.px >= 278) { char fp[256]; entry_relative_path(&L.e[row], fp, sizeof fp); favorite_toggle(fp); }
                else activate(row);
            }
            if ((kh & KEY_TOUCH) && slider_drag) slider_frac = frac_from_px(tp.px);
            if ((ku & KEY_TOUCH) && slider_drag) { seek_to(slider_frac); slider_drag = false; }
            if (ku & KEY_TOUCH) touch_in_list = false;
            if (kb_returned) {                       /* the system keyboard was just shown */
                kb_returned = false;
                touch_in_list = dragging = slider_drag = false;
                hold_up = hold_down = hold_left = hold_right = -100000;   /* ignore keys still held until released */
            }
        }

        /* ---- auto-save theme/settings shortly after the last change ---- */
        if (settings_dirty && --dirty_timer <= 0) { settings_save(&S); settings_dirty = false; }

        if (audio_is_active() && !audio_is_paused()) eq_phase += 0.08f;
        if (toast_t > 0) toast_t--;
        if (ctrl_flash_t > 0) ctrl_flash_t--;
        if (S.show_batt) { if (--batt_timer <= 0) { batt_poll(); batt_timer = 120; } }   /* every ~2 s */

        /* ---- draw ---- */
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        art_commit();
        if (flow_pump()) flow_want_dirty = true;       /* a cover finished loading */
        flow_update();
        C2D_TextBufClear(tbuf);

        u32 bg = C(S.bg);
        C2D_TargetClear(top, bg);
        C2D_SceneBegin(top);
        if (wizard_open && wz_page != WZ_SETUP) { bg_gradient(TOP_W); draw_wizard_top(); }
        else draw_top();                                     /* draws its own (ambient) background */

        C2D_TargetClear(bot, bg);
        C2D_SceneBegin(bot);
        bg_gradient(BOT_W);
        if (search_busy > 0) draw_search_busy();
        else if (confirm_exit) draw_exit_confirm();
        else if (wizard_open) draw_wizard_bottom();
        else if (settings_open) draw_settings();
        else if (queue_open) draw_queue();
        else draw_browser();

        C3D_FrameEnd(0);

        if (search_busy > 0 && --search_busy == 0) run_search();     /* the busy screen has been shown: do the search */
    }

    if (!L_is_search) snprintf(S.last_dir, sizeof S.last_dir, "%s", L.dir);     /* results aren't a folder */
    settings_save(&S);
    if (favorites_dirty) favorites_save();
    aptUnhook(&lid_hook_cookie);

    if (audio_ok) { audio_stop(); audio_exit(); }
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
