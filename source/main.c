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
#define LIST_TOP   64
#define ROW_H      25
#define VIS_ROWS   4
#define CTRL_TOP   168
#define SL_X0      44          /* seek slider track, x range */
#define SL_X1      274
#define SB_X0      38          /* search bar starts after the Home button */
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
    ROW_PRESET, ROW_GLOW, ROW_FADE, ROW_DISPLAY, ROW_WALLPAPER,                /* Look    */
    ROW_EQMAN, ROW_EQPRE, ROW_B0, ROW_B1, ROW_B2, ROW_B3, ROW_B4,        /* Audio   */
    ROW_SEEK, ROW_CLOCK, ROW_BATT, ROW_SLEEP_ENABLE, ROW_SLEEP_TIMER, ROW_GUIDE, ROW_EXIT, /* General */
    SET_ROWS
};

/* "Cards" are lists of settings rows drawn as one inset panel. Cards 0 and 2 are the Look and General
 * tabs (the Sound tab has its own layout); CARD_SETUP is the single combined page of the first-run setup. */
enum { CARD_LOOK, CARD_SOUND, CARD_GENERAL, CARD_SETUP, CARD_COUNT };
static const int   ROWS_LOOK[]    = { ROW_PRESET, ROW_GLOW, ROW_FADE, ROW_DISPLAY, ROW_WALLPAPER };
static const int   ROWS_GENERAL[] = { ROW_SEEK, ROW_CLOCK, ROW_BATT, ROW_SLEEP_ENABLE, ROW_SLEEP_TIMER, ROW_GUIDE, ROW_EXIT };
static const int   ROWS_SETUP[]   = { ROW_PRESET, ROW_GLOW, ROW_CLOCK, ROW_BATT };
static const int  *CARD_ROWS[CARD_COUNT] = { ROWS_LOOK, NULL, ROWS_GENERAL, ROWS_SETUP };
static const int   CARD_LEN[CARD_COUNT]  = { 5, 0, 7, 4 };
static const float CARD_ROWH[CARD_COUNT] = { 28.0f, 0.0f, 22.0f, 25.0f };
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
        free(art);
        if (manual_queue_count > 0) play_manual_queue_head();
        return;
    }
    cur = m;
    track_loaded = true;
    snprintf(cur_path, sizeof cur_path, "%s", path);
    art_queue(art, art_size);
    if (audio_ok) { audio_play(path, m.audio_start); apply_lid(); }
}

/* Restart the track that is loaded right now (repeat-one). It may have come from Up Next rather than
 * the playlist, so it is replayed from its own path instead of through Q / qi. */
static void replay_current(void)
{
    if (!track_loaded || !audio_ok || cur_path[0] == 0) return;
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
static void skip_next(void) { if (Q.n == 0 && manual_queue_count == 0) return; flash_ctrl(232); flow_show = false; next_track(true); follow_playing(); }
static void skip_prev(void) { if (Q.n == 0) return; flash_ctrl(168); flow_show = false; prev_track(); follow_playing(); }

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
static bool is_slider_row(int r) { return r >= ROW_B0 && r <= ROW_B4; }
static int  row_page(int r)      { return r < ROW_EQMAN ? 0 : (r < ROW_SEEK ? 1 : 2); }

static bool is_switch_row(int r) { return r == ROW_BATT || r == ROW_SLEEP_ENABLE || r == ROW_EQMAN; }
static bool is_band_row(int r)   { return r >= ROW_B0 && r <= ROW_B4; }
static bool is_choice_row(int r) { return r == ROW_PRESET || r == ROW_EQPRE || r == ROW_CLOCK || r == ROW_GLOW || r == ROW_FADE || r == ROW_DISPLAY || r == ROW_WALLPAPER || r == ROW_SEEK || r == ROW_SLEEP_TIMER; }

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
    if (is_band_row(n) && !S.eq_manual) n = (dir > 0) ? ROW_SEEK : ROW_EQPRE;
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

static void draw_status_bar(void)
{
    /* The reference header already contains the status information. Keep battery/sleep state as a
     * tiny green indicator at the extreme right so it does not compete with the clock. */
    if (sleep_timer_active) {
        C2D_DrawCircleSolid(388, 28, 0.42f, 2.2f, C(S.accent));
    }
    if (S.show_batt && batt_pct >= 0) {
        float fw = 8.0f * (float)batt_pct / 100.0f;
        rrect(378, 27, 10, 2, 1, 0.40f, C(S.select));
        if (fw > 0.5f) rrect(378, 27, fw, 2, 1, 0.40f, C(S.accent));
        if (batt_chg) C2D_DrawCircleSolid(374, 28, 0.42f, 1.5f, C(S.accent));
    }
}

static void draw_visualizer(float x, float y, float size)
{
    u32 accent = C(S.accent), soft = C(mix_rgb(S.accent, S.bg, 110));
    float cx = x + size * 0.5f, cy = y + size * 0.5f;
    if (S.display_mode == 1) {
        for (int i = 0; i < 24; i++) {
            float a = sinf(eq_phase * (1.0f + (i % 5) * 0.13f) + i * 0.57f);
            float b = cosf(eq_phase * 0.63f + i * 0.31f);
            float h = 15.0f + (a * 0.5f + 0.5f) * 74.0f + (b * 0.5f + 0.5f) * 20.0f;
            rrect(x + 5 + i * 5.55f, cy - h * 0.5f, 3.2f, h, 1.6f, 0.40f, (i % 3) ? accent : soft);
        }
    } else if (S.display_mode == 2) {
        for (int i = 0; i < 38; i++) {
            float a = (float)i / 37.0f;
            float yy = cy + sinf(a * 12.566f + eq_phase * 1.4f) * (18.0f + 17.0f * sinf(eq_phase * 0.7f + a * 6.28f));
            C2D_DrawCircleSolid(x + 5 + a * (size - 10), yy, 0.40f, 2.2f, accent);
        }
        rrect(x + 5, cy - 1, size - 10, 2, 1, 0.35f, soft);
    } else {
        for (int ring = 0; ring < 3; ring++) {
            float radius = 23.0f + ring * 19.0f;
            for (int i = 0; i < 12; i++) {
                float a = (float)i * 0.523599f + eq_phase * (ring % 2 ? -0.35f : 0.35f);
                float pulse = 0.75f + 0.25f * sinf(eq_phase * 1.8f + i + ring);
                C2D_DrawCircleSolid(cx + cosf(a) * radius, cy + sinf(a) * radius,
                                    0.40f, 2.5f + pulse * 2.0f, ring == 1 ? soft : accent);
            }
        }
        C2D_DrawCircleSolid(cx, cy, 0.40f, 7.0f, accent);
        C2D_DrawCircleSolid(cx, cy, 0.41f, 3.0f, C(S.bg));
    }
}


static void row_text(const char *s, float x, float y, float rh, float sc, u32 col, u32 align, float maxw)
{
    draw_text(s, x, y + (rh - 28.0f * sc) / 2.0f, sc, col, align, maxw);
}

static void draw_top(void)
{
    /*
     * Neon top-screen skin.
     *
     * The carousel/flow renderer is deliberately left intact below: the centre
     * artwork and side cards still move exactly as before.  Only the chrome,
     * glow, title treatment and seek bar are restyled to match the neon-green
     * reference.
     */
    const Rgb neon       = { 36, 255, 104 };
    const Rgb neon_bright= { 70, 255, 126 };
    const Rgb neon_dim   = { 12, 104, 42 };
    const Rgb white      = { 238, 244, 242 };
    const Rgb black      = { 0, 0, 0 };
    const Rgb panel      = { 2, 8, 4 };

    const u32 cNeon = C(neon);
    const u32 cBright = C(neon_bright);
    const u32 cDim = C(neon_dim);
    const u32 cText = C(white);
    const u32 cBlack = C(black);
    const u32 cPanel = C(panel);

    /* Deep black base with a very subtle green lift behind the player. */
    C2D_DrawRectSolid(0, 0, 0.01f, TOP_W, SCR_H, cBlack);
    C2D_DrawRectangle(0, 0, 0.02f, TOP_W, SCR_H,
                      C2D_Color32(0, 12, 5, 255), C2D_Color32(0, 4, 2, 255),
                      C2D_Color32(0, 8, 3, 255), C2D_Color32(0, 2, 1, 255));

    /* Outer neon frame: multiple passes create the soft tube-like bloom. */
    rrect(0, 0, 400, 240, 18, 0.03f, C2D_Color32(7, 64, 25, 150));
    rrect(1, 1, 398, 238, 17, 0.04f, C2D_Color32(15, 150, 53, 190));
    rrect(2, 2, 396, 236, 16, 0.05f, cNeon);
    rrect(5, 5, 390, 230, 13, 0.06f, cBlack);

    /* Header capsules. */
    rrect(8, 6, 264, 20, 10, 0.08f, C2D_Color32(6, 74, 29, 170));
    rrect(9, 7, 262, 18, 9, 0.09f, cNeon);
    rrect(12, 10, 256, 12, 6, 0.10f, cPanel);

    rrect(276, 6, 116, 20, 10, 0.08f, C2D_Color32(6, 74, 29, 170));
    rrect(277, 7, 114, 18, 9, 0.09f, cNeon);
    rrect(280, 10, 108, 12, 6, 0.10f, cPanel);

    /* Three-bar signal mark. */
    for (int i = 0; i < 3; ++i) {
        float h = 6.0f + i * 4.5f;
        rrect(13 + i * 7.0f, 22.0f - h, 5.0f, h, 1.5f, 0.12f,
              i == 2 ? cBright : cNeon);
    }

    draw_text("PROJECT", 43, 8, 0.42f, cText, C2D_AlignLeft, 70);
    draw_text("NOISE", 92, 8, 0.42f, cNeon, C2D_AlignLeft, 56);

    time_t now = time(NULL);
    struct tm *tmv = localtime(&now);
    char clockbuf[32] = "11/10 (Tue) 15:24";
    if (tmv) {
        static const char *wd[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        snprintf(clockbuf, sizeof clockbuf, "%02d/%02d (%s) %02d:%02d",
                 tmv->tm_mon + 1, tmv->tm_mday, wd[tmv->tm_wday],
                 tmv->tm_hour, tmv->tm_min);
    }
    draw_text(clockbuf, 334, 8, 0.40f, cText, C2D_AlignCenter, 106);

    /*
     * Player art area.  The carousel remains the source of the artwork, so
     * swiping/turning through tracks is unchanged.  Neon rings sit behind it
     * rather than replacing the flow renderer.
     */
    const float asz = 140.0f, ax = 130.0f, ay = 38.0f;
    bool use_flow = (S.display_mode == 0) && flow_appear > 0.01f && L.n > 0;

    /* Broad green bloom. */
    rrect(ax - 8, ay - 8, asz + 16, asz + 16, 15, 0.07f,
          C2D_Color32(0, 60, 20, 115));
    rrect(ax - 5, ay - 5, asz + 10, asz + 10, 13, 0.08f,
          C2D_Color32(10, 180, 55, 150));
    rrect(ax - 2, ay - 2, asz + 4, asz + 4, 11, 0.09f, cBright);
    rrect(ax, ay, asz, asz, 9, 0.10f, cBlack);

    if (!use_flow && S.display_mode != 0) {
        draw_visualizer(ax + 3, ay + 3, asz - 6);
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
                ? C(mix_rgb(neon_dim, white, 55))
                : C(mix_rgb(black, tile_color(e->name), 90));
            items[i].fade = 1.0f;

            if (playing_view && idx == ci && art_ready()) {
                items[i].tex = art_image()->tex;
                items[i].color = C(neon);
                float f = art_fade;
                items[i].fade = f * f * (3.0f - 2.0f * f);
            } else if (is_current_row(idx) && art_ready()) {
                items[i].tex = art_image()->tex;
            } else {
                items[i].tex = flow_tex(idx, &items[i].fade);
            }

            if (playing_view && idx == ci && !items[i].tex) items[i].kind = 0;
        }

        /* Preserve the existing 3D-style cover carousel. */
        flow_draw(items, 10, base, flow_pos, flow_appear, top_bg_color);
    } else if (art_ready()) {
        C2D_DrawImageAt(*art_image(), ax + 2, ay + 2, 0.40f, NULL,
                        (asz - 4) / 128.0f, (asz - 4) / 128.0f);
        if (art_fade < 0.999f) {
            C2D_DrawRectSolid(ax + 2, ay + 2, 0.41f, asz - 4, asz - 4,
                              C2D_Color32(0, 0, 0,
                              (u8)(255.0f * (1.0f - art_fade))));
        }
    } else {
        icon_note(ax + asz / 2 - 7, ay + asz / 2 - 8, cText);
    }

    /* Title area: deliberately high contrast like the reference. */
    if (track_loaded) {
        draw_text_sh(cur.title[0] ? cur.title : "Song",
                     200, 181, 0.72f, cText, C2D_AlignCenter, 300);
        if (cur.artist[0])
            draw_text(cur.artist, 200, 199, 0.34f, cDim,
                      C2D_AlignCenter, 240);
    } else if (use_flow && L.n > 0) {
        int fi = (int)floorf(flow_pos + 0.5f);
        if (fi < 0) fi = 0;
        if (fi >= L.n) fi = L.n - 1;
        Entry *e = &L.e[fi];
        draw_text_sh(e->is_dir
                     ? (!strcmp(e->name, "..") ? "Back" : e->name)
                     : (e->title[0] ? e->title : base_name(e->name)),
                     200, 181, 0.72f, cText, C2D_AlignCenter, 300);
    } else {
        draw_text_sh("Song", 200, 181, 0.72f, cText,
                     C2D_AlignCenter, 300);
    }

    /* Neon seek bar.  Keep the same touch/input coordinates. */
    u32 pos = track_loaded ? audio_position_ms() : 0;
    u32 dur = track_loaded ? cur.duration_ms : 0;
    float frac = progress_frac(pos, dur);

    const float px0 = 94.0f, pw = 212.0f, py = 214.0f;

    rrect(px0 - 2, py - 4, pw + 4, 10, 5, 0.06f,
          C2D_Color32(5, 92, 31, 120));
    rrect(px0, py - 3, pw, 8, 4, 0.08f, cDim);

    float fw = pw * frac;
    if (fw < 5) fw = 5;
    rrect(px0, py - 3, fw, 8, 4, 0.09f, cBright);

    float kx = px0 + pw * frac;
    C2D_DrawCircleSolid(kx, py + 1, 0.10f, 12.0f, C2D_Color32(0, 80, 28, 130));
    C2D_DrawCircleSolid(kx, py + 1, 0.12f, 8.5f, cNeon);
    C2D_DrawCircleSolid(kx, py + 1, 0.13f, 5.0f, cBlack);

    draw_status_bar();
}

/* ====================================================================== bottom-screen UI
 * Layering (citro2d depth: higher z = in front, equal z = draw order, text is always z 0.5):
 *   0.05 gradient   0.08 wallpaper   0.10 frame   0.20 panels   0.22 panel insets
 *   0.30 widgets    0.45 icons       0.46 toast
 */
static void icon_heart(float cx, float cy, float s, u32 c)
{
    C2D_DrawCircleSolid(cx - s * 0.5f, cy - s * 0.3f, 0.45f, s * 0.56f, c);
    C2D_DrawCircleSolid(cx + s * 0.5f, cy - s * 0.3f, 0.45f, s * 0.56f, c);
    C2D_DrawTriangle(cx - s * 1.02f, cy - s * 0.12f, c, cx + s * 1.02f, cy - s * 0.12f, c, cx, cy + s * 0.95f, c, 0.45f);
}

static void icon_home(float cx, float cy, u32 c, u32 door)
{
    C2D_DrawTriangle(cx - 8, cy - 0.5f, c, cx + 8, cy - 0.5f, c, cx, cy - 8, c, 0.45f);
    C2D_DrawRectSolid(cx - 5, cy - 0.5f, 0.45f, 10, 7.5f, c);
    C2D_DrawRectSolid(cx - 1.5f, cy + 2.5f, 0.46f, 3, 4.5f, door);
}

static void icon_queue(float cx, float cy, u32 c)
{
    C2D_DrawRectSolid(cx - 8, cy - 6, 0.45f, 16, 2, c);
    C2D_DrawRectSolid(cx - 8, cy - 1, 0.45f, 16, 2, c);
    C2D_DrawRectSolid(cx - 8, cy + 4, 0.45f, 8, 2, c);
    C2D_DrawTriangle(cx + 2, cy + 2.5f, c, cx + 2, cy + 8.5f, c, cx + 8, cy + 5.5f, c, 0.45f);
}

static void icon_small_note(float x, float y, u32 c)      /* ~13x16 note, top-left at x,y */
{
    C2D_DrawCircleSolid(x + 4.5f, y + 12, 0.45f, 3.0f, c);
    C2D_DrawRectSolid(x + 6.5f, y + 2, 0.45f, 2, 10, c);
    C2D_DrawTriangle(x + 8.5f, y + 2, c, x + 12.5f, y + 6.5f, c, x + 8.5f, y + 8, c, 0.45f);
}

static void draw_frame(void)                  /* thin neon outline around the whole bottom screen */
{
    u32 c = C(mix_rgb(S.bg, S.accent, 130));
    C2D_DrawRectSolid(2, 2, 0.10f, 316, 1.5f, c);
    C2D_DrawRectSolid(2, 236.5f, 0.10f, 316, 1.5f, c);
    C2D_DrawRectSolid(2, 2, 0.10f, 1.5f, 236, c);
    C2D_DrawRectSolid(316.5f, 2, 0.10f, 1.5f, 236, c);
}

static void draw_toast(void)                  /* short status pill just above the transport deck */
{
    if (toast_t <= 0) return;
    rrect(70, 142, 180, 20, 10, 0.46f, C(S.accent));
    row_text(toast_msg, 160, 142, 20, 0.42f, C(S.bg), C2D_AlignCenter, 168);
}

static void draw_miniplayer(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cBg = C(S.bg);
    u32 cDim  = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));
    u32 cRing = C(mix_rgb(S.bg, S.accent, 130));
    u32 cTrk  = C(mix_rgb(S.bg, S.select, 255));
    const float y = CTRL_TOP;
    char buf[24];

    rrect(6, y, 308, 68, 13, 0.20f, cRing);
    rrect(7.5f, y + 1.5f, 305, 65, 12, 0.22f, cCard);

    /* seek bar with elapsed / total time */
    u32 pos = track_loaded ? audio_position_ms() : 0;
    u32 dur = track_loaded ? cur.duration_ms : 0;
    float frac = slider_drag ? slider_frac : progress_frac(pos, dur);
    const float tw = SL_X1 - SL_X0, ty = y + 12;
    fmt_time(buf, sizeof buf, slider_drag ? (u32)(slider_frac * (float)dur) : pos);
    row_text(buf, 14, y + 5, 14, 0.34f, cDim, C2D_AlignLeft, 34);
    fmt_time(buf, sizeof buf, dur);
    row_text(buf, 281, y + 5, 14, 0.34f, cDim, C2D_AlignLeft, 34);
    rrect(SL_X0, ty - 2.5f, tw, 5, 2.5f, 0.30f, cTrk);
    float fw = tw * frac; if (fw < 5) fw = 5;
    rrect(SL_X0, ty - 2.5f, fw, 5, 2.5f, 0.32f, cAcc);
    C2D_DrawCircleSolid(SL_X0 + tw * frac, ty, 0.36f, slider_drag ? 7.5f : 5.5f, cAcc);
    C2D_DrawCircleSolid(SL_X0 + tw * frac, ty, 0.38f, slider_drag ? 3.0f : 2.0f, cBg);

    /* what is playing */
    const char *title = track_loaded ? (cur.title[0] ? cur.title : base_name(cur_path)) : "Nothing playing";
    const char *sub   = track_loaded ? (cur.artist[0] ? cur.artist : (audio_is_paused() ? "Paused" : "Now playing"))
                                     : "Pick a song to start";
    draw_text(title, 14, y + 27, 0.46f, track_loaded ? cAcc : cText, C2D_AlignLeft, 112);
    draw_text(sub,   14, y + 43, 0.32f, cDim, C2D_AlignLeft, 112);

    /* transport (tap zones: see handle_ctrl_tap) */
    const float cy = y + 44;
    if (ctrl_flash_t > 0) C2D_DrawCircleSolid(ctrl_flash_x, cy, 0.26f, 17, C(mix_rgb(S.select, S.accent, 90)));
    icon_shuffle(142, cy, 7, S.shuffle ? cAcc : cDim);
    icon_prev(168, cy, 8, cText);
    C2D_DrawCircleSolid(200, cy, 0.30f, 16, cAcc);
    if (audio_is_active() && !audio_is_paused()) icon_pause(200, cy, 8, cBg); else icon_play(200, cy, 8, cBg);
    icon_next(232, cy, 8, cText);
    icon_repeat(260, cy, 7, S.repeat ? cAcc : cDim);
    icon_queue(292, cy, queue_open ? cAcc : cText);
    if (manual_queue_count > 0) {
        C2D_DrawCircleSolid(302, cy - 9, 0.46f, 6.5f, cAcc);
        snprintf(buf, sizeof buf, "%d", manual_queue_count);
        row_text(buf, 302, cy - 15.5f, 13, 0.30f, cBg, C2D_AlignCenter, 0);
    }
}

static void draw_browser(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent);
    u32 cDim  = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));
    u32 cRing = C(mix_rgb(S.bg, S.accent, 130));
    u32 cRow  = C(mix_rgb(S.bg, S.select, 80));
    u32 cRowSel = C(mix_rgb(S.bg, S.accent, 45));

    /* header: home button + search pill */
    C2D_DrawCircleSolid(21, 21, 0.20f, 13, cRing);
    C2D_DrawCircleSolid(21, 21, 0.22f, 11.8f, cCard);
    icon_home(21, 21, cAcc, cCard);
    rrect(40, 8, 272, 26, 13, 0.20f, cRing);
    rrect(41.2f, 9.2f, 269.6f, 23.6f, 11.8f, 0.22f, cCard);
    icon_search(55, 21, cDim, cCard);
    if (L_is_search) {
        row_text(search_q, 69, 8, 26, 0.42f, cText, C2D_AlignLeft, 205);
        C2D_DrawLine(293, 16, cDim, 303, 26, cDim, 1.8f, 0.45f);
        C2D_DrawLine(303, 16, cDim, 293, 26, cDim, 1.8f, 0.45f);
    } else {
        row_text("Search music", 69, 8, 26, 0.42f, cDim, C2D_AlignLeft, 205);
    }

    /* tabs: All Songs | Liked */
    rrect(8, 38, 304, 22, 11, 0.20f, cCard);
    char liked[24];
    snprintf(liked, sizeof liked, "Liked  %d", favorite_count);
    bool fav_tab = L_is_favorites;
    rrect(fav_tab ? 162 : 10, 40, 148, 18, 9, 0.24f, cAcc);
    row_text("All Songs", 84,  40, 18, 0.40f, fav_tab ? cDim : cBg, C2D_AlignCenter, 140);
    row_text(liked,       236, 40, 18, 0.40f, fav_tab ? cBg : cDim, C2D_AlignCenter, 140);

    /* song list */
    int loads_this_frame = 0;
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = scroll_top + i;
        if (idx >= L.n) break;
        Entry *e = &L.e[idx];
        float y = LIST_TOP + i * ROW_H;
        bool cur_row = is_current_row(idx);
        bool playing = cur_row && audio_is_active() && !audio_is_paused();
        bool selected = (idx == sel);

        if (selected) {
            rrect(8, y, 298, 23, 7, 0.20f, cAcc);
            rrect(9.2f, y + 1.2f, 295.6f, 20.6f, 6, 0.22f, cRowSel);
        } else {
            rrect(8, y, 298, 23, 7, 0.20f, cRow);
        }

        const float tx = 12, tyy = y + 3;
        Rgb tc = e->is_dir ? mix_rgb(S.select, S.text, 60) : tile_color(e->name);
        rrect(tx, tyy, 17, 17, 5, 0.30f, C(tc));
        if (e->is_dir) {
            C2D_DrawRectSolid(tx + 3, tyy + 4, 0.45f, 5, 2.5f, cText);
            C2D_DrawRectSolid(tx + 3, tyy + 6, 0.45f, 11, 7, cText);
        } else if (playing) {
            for (int k = 0; k < 3; k++) {
                float h = 3.0f + 8.0f * (0.5f + 0.5f * sinf(eq_phase * 2.0f + k * 1.7f));
                C2D_DrawRectSolid(tx + 3.5f + k * 4, tyy + 14 - h, 0.45f, 3, h, cAcc);
            }
        } else {
            icon_small_note(tx + 2, tyy + 0.5f, cText);
        }

        const float lx = 36;
        if (e->is_dir) {
            row_text(!strcmp(e->name, "..") ? "Back" : e->name, lx, y, 23, 0.46f, selected ? cAcc : cText, C2D_AlignLeft, 250);
        } else {
            if (!e->meta_loaded && loads_this_frame < 1 && flow_idle > 2) { ensure_meta(e); loads_this_frame++; }
            const char *name = (e->meta_loaded && e->title[0]) ? e->title : base_name(e->name);
            u32 tcol = cur_row ? cAcc : cText;
            if (e->meta_loaded && e->artist[0]) {
                draw_text(name, lx, y + 0.5f, 0.42f, tcol, C2D_AlignLeft, 235);
                draw_text(e->artist, lx, y + 12.5f, 0.28f, cDim, C2D_AlignLeft, 235);
            } else {
                row_text(name, lx, y, 23, 0.44f, tcol, C2D_AlignLeft, 235);
            }
            char favpath[256]; entry_relative_path(e, favpath, sizeof favpath);
            icon_heart(293, y + 11, 4.6f, is_favorite(favpath) ? cAcc : C(mix_rgb(S.bg, S.text, 70)));
        }
    }

    if (L.n == 0) {
        draw_text(L_is_favorites ? "No liked songs yet" : "No songs here", 160, 92, 0.55f, cText, C2D_AlignCenter, 290);
        draw_text(L_is_favorites ? "Tap a heart on any song to add it" : "Copy music to SD:/Music/", 160, 116, 0.38f, cDim, C2D_AlignCenter, 290);
    }
    if (L.n > VIS_ROWS) {
        float track_h = VIS_ROWS * ROW_H - 2;
        float th = track_h * VIS_ROWS / L.n; if (th < 10) th = 10;
        float ty2 = LIST_TOP + (track_h - th) * scroll_top / (L.n - VIS_ROWS);
        rrect(309, LIST_TOP, 3, track_h, 1.5f, 0.20f, cRow);
        rrect(309, ty2, 3, th, 1.5f, 0.30f, cAcc);
    }

    draw_miniplayer();
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
static const char *UI_DISPLAY_NAMES[4]   = { "Cover", "Spectrum", "Waveform", "Orbit" };
static const char *UI_CLOCK_NAMES[3]     = { "Off", "12-hour", "24-hour" };

/* Faint decoration behind the bottom screen (Settings > Appearance > Wallpaper). Drawn just above the
 * background gradient and below everything else. */
static void draw_wallpaper_accents(float w, float h)
{
    if (S.wallpaper <= 0 || S.wallpaper > 4) return;
    const float z = 0.08f;
    u32 c  = C(mix_rgb(S.bg, S.accent, 22));
    u32 c2 = C(mix_rgb(S.bg, S.accent, 34));
    switch (S.wallpaper) {
    case 1:                                                       /* scanlines */
        for (float y = 3; y < h; y += 6) C2D_DrawRectSolid(0, y, z, w, 1, c);
        break;
    case 2:                                                       /* dot grid */
        for (float y = 8; y < h; y += 16)
            for (float x = 8; x < w; x += 16) C2D_DrawRectSolid(x, y, z, 2, 2, c2);
        break;
    case 3:                                                       /* corner glows */
        C2D_DrawTriangle(0, 0, c2, 90, 0, c, 0, 90, c, z);
        C2D_DrawTriangle(w, h, c2, w - 90, h, c, w, h - 90, c, z);
        break;
    case 4:                                                       /* grid */
        for (float x = 0; x < w; x += 40) C2D_DrawRectSolid(x, 0, z, 1, h, c);
        for (float y = 0; y < h; y += 40) C2D_DrawRectSolid(0, y, z, w, 1, c);
        break;
    }
}

/* ------------------------------------------------------------ settings UI */
static const char *row_label(int r)
{
    switch (r) {
    case ROW_PRESET:       return "Theme";
    case ROW_GLOW:         return "Ambient glow";
    case ROW_FADE:         return "Cover fade";
    case ROW_DISPLAY:      return "Display";
    case ROW_WALLPAPER:    return "Wallpaper";
    case ROW_EQMAN:        return "Manual EQ";
    case ROW_EQPRE:        return "EQ preset";
    case ROW_SEEK:         return "Skip length";
    case ROW_CLOCK:        return "Clock";
    case ROW_BATT:         return "Battery %";
    case ROW_SLEEP_ENABLE: return "Sleep timer";
    case ROW_SLEEP_TIMER:  return "Timer length";
    case ROW_GUIDE:        return "Replay guide";
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
    case ROW_WALLPAPER: s = UI_WALLPAPER_NAMES[(unsigned)S.wallpaper % 5]; break;
    case ROW_EQPRE:     s = EQ_PRESET_NAMES[(unsigned)S.eq_preset % EQ_PRESET_COUNT]; break;
    case ROW_CLOCK:     s = UI_CLOCK_NAMES[(unsigned)S.clock_mode % 3]; break;
    case ROW_SEEK:      snprintf(out, cap, "%d s", S.seek_step); return;
    case ROW_SLEEP_TIMER: snprintf(out, cap, "%d min", S.sleep_minutes); return;
    default: break;
    }
    snprintf(out, cap, "%s", s);
}

static bool row_switch_on(int r)
{
    if (r == ROW_BATT)         return S.show_batt;
    if (r == ROW_EQMAN)        return S.eq_manual;
    if (r == ROW_SLEEP_ENABLE) return sleep_timer_active;
    return false;
}

static void draw_switch(float x, float cy, bool on, u32 cOn, u32 cOff, u32 cKnobOn, u32 cKnobOff)
{
    rrect(x, cy - 7, 30, 14, 7, 0.30f, on ? cOn : cOff);
    C2D_DrawCircleSolid(on ? x + 23 : x + 7, cy, 0.34f, 5, on ? cKnobOn : cKnobOff);
}

static void draw_arrow(float x, float cy, int dir, u32 c)     /* small triangle, dir -1 = left, +1 = right */
{
    C2D_DrawTriangle(x + dir * 3.0f, cy, c, x - dir * 2.0f, cy - 4, c, x - dir * 2.0f, cy + 4, c, 0.34f);
}

/* A list of settings rows drawn as one inset panel. Rows start at y = 70 (card_touch uses the same grid). */
static void draw_card(int card)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select), cBg = C(S.bg);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));
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

        row_text(row_label(r), 18, y, rh, 0.45f, r == ROW_EXIT ? cDanger : cText, C2D_AlignLeft, 128);

        if (is_switch_row(r)) {
            if (r == ROW_SLEEP_ENABLE && sleep_timer_active) {          /* live countdown beside the label */
                s64 left = (s64)sleep_timer_deadline - (s64)osGetTime();
                fmt_time(val, sizeof val, left > 0 ? (u32)left : 0);
                row_text(val, 140, y, rh, 0.42f, cAcc, C2D_AlignLeft, 90);
            }
            draw_switch(272, y + rh / 2, row_switch_on(r), cAcc, sel ? cBg : cSel, cBg, cDim);
        } else if (is_choice_row(r)) {
            float ph = rh - 6.0f; if (ph > 20.0f) ph = 20.0f;
            float py = y + (rh - ph) / 2;
            rrect(150, py, 156, ph, ph / 2, 0.30f, cBg);
            draw_arrow(162, py + ph / 2, -1, cAcc);
            draw_arrow(294, py + ph / 2, +1, cAcc);
            row_value(r, val, sizeof val);
            row_text(val, 228, py, ph, 0.40f, cText, C2D_AlignCenter, 100);
        } else if (r == ROW_GUIDE || r == ROW_EXIT) {
            draw_arrow(298, y + rh / 2, +1, r == ROW_EXIT ? cDanger : cAcc);
        }
    }
}

/* Audio tab: Manual-EQ switch + preset chooser, then five vertical band sliders (touch grid: see sound_touch). */
static void draw_sound(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select), cBg = C(S.bg);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));
    static const char *FREQ[EQ_BANDS] = { "60", "230", "910", "3.6k", "14k" };
    const float ch = SND_CTL_H - 2;
    char buf[16];

    /* left chip: Manual EQ switch */
    rrect(8, SND_CTL_Y, 148, ch, 11, 0.26f, set_row == ROW_EQMAN ? cSel : cCard);
    row_text("Manual EQ", 20, SND_CTL_Y, ch, 0.45f, cText, C2D_AlignLeft, 90);
    draw_switch(120, SND_CTL_Y + ch / 2, S.eq_manual, cAcc, cBg, cBg, cDim);

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
    u32 cText = C(S.text), cAcc = C(S.accent), cBg = C(S.bg);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));
    int page = row_page(set_row);

    draw_text("Settings", 14, 2, 0.85f, cText, C2D_AlignLeft, 220);
    draw_text(PAGE_NAMES[page], 14, 26, 0.34f, cDim, C2D_AlignLeft, 160);
    rrect(250, 6, 62, 22, 11, 0.30f, cAcc);                      /* Done pill (touch: y < 40, x >= 244) */
    row_text("Done", 281, 6, 22, 0.45f, cBg, C2D_AlignCenter, 0);

    rrect(10, 42, 300, 24, 12, 0.26f, cCard);                    /* segmented control: three 100 px tabs */
    for (int t = 0; t < 3; t++) {
        float x = 10.0f + t * 100.0f;
        if (t == page) rrect(x + 2, 44, 96, 20, 10, 0.30f, cAcc);
        row_text(PAGE_NAMES[t], x + 50, 44, 20, 0.42f, t == page ? cBg : cDim, C2D_AlignCenter, 92);
    }

    if (page == 1) draw_sound();
    else draw_card(page == 0 ? CARD_LOOK : CARD_GENERAL);
}

/* ------------------------------------------------------------------ Up Next */
#define QUEUE_ROWS 3                    /* rows 48..150 fit above the transport deck at CTRL_TOP */
static void draw_queue(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select), cBg = C(S.bg);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));
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
            float th = 102.0f * QUEUE_ROWS / manual_queue_count; if (th < 10) th = 10;
            float ty = 48.0f + (102.0f - th) * queue_scroll / (manual_queue_count - QUEUE_ROWS);
            rrect(311, 48, 3, 102, 1.5f, 0.30f, cSel);
            rrect(311, ty, 3, th, 1.5f, 0.34f, cAcc);
        }
    }

    draw_miniplayer();
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

static void handle_ctrl_tap(int px, int py)      /* bottom deck: seek, transport and Up Next */
{
    if (py < CTRL_TOP + 24) {
        if (cur.duration_ms && track_loaded && px >= SL_X0 - 14 && px <= SL_X1 + 14) {
            slider_drag = true;
            slider_frac = frac_from_px(px);
        }
        return;
    }
    if (px < 130)      return;                                                  /* title area */
    if (px < 155)      { flash_ctrl(142); S.shuffle = !S.shuffle; mark_dirty(); }
    else if (px < 184) skip_prev();
    else if (px < 216) { flash_ctrl(200); toggle_play(); }
    else if (px < 245) skip_next();
    else if (px < 276) { flash_ctrl(260); S.repeat = (S.repeat + 1) % 3; mark_dirty(); }
    else               { flash_ctrl(292); queue_open = !queue_open; queue_sel = 0; queue_scroll = 0; }
}

static void handle_header_tap(int px, int py)    /* home / search row and the All Songs | Liked tabs */
{
    if (py >= 38) {                                                             /* tabs */
        if (px < 160) { if (L_is_favorites || L_is_search) set_dir(S.last_dir, NULL); }
        else if (!L_is_favorites) show_favorites();
        return;
    }
    if (px < 36) {                                                              /* home */
        if (L_is_favorites || L_is_search) set_dir(S.last_dir, NULL);
        else if (strcmp(L.dir, MUSIC_ROOT) != 0) set_dir(MUSIC_ROOT, NULL);
        return;
    }
    if (L_is_search && px >= 284) exit_search();
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

static void draw_splash(C3D_RenderTarget *top, C3D_RenderTarget *bot)
{
    for (int frame = 0; frame < 72 && aptMainLoop(); frame++) {
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        art_commit();
        C2D_TextBufClear(tbuf);
        C2D_TargetClear(top, C2D_Color32(7, 10, 17, 255));
        C2D_SceneBegin(top);
        float pulse = 0.72f + 0.28f * sinf((float)frame * 0.09f);
        u32 accent = C(S.accent);
        C2D_DrawCircleSolid(200, 105, 0.20f, 70.0f, C2D_Color32(15, 25, 40, 255));
        C2D_DrawCircleSolid(200, 105, 0.21f, 53.0f, C2D_Color32(12, 18, 29, 255));
        if (art_ready()) C2D_DrawImageAt(*art_image(), 136, 36, 0.32f, NULL, 1.0f, 1.0f);
        draw_text("PROJECT NOISE", 200, 145, 0.78f,
                  C2D_Color32((u8)(S.accent.r * pulse), (u8)(S.accent.g * pulse), (u8)(S.accent.b * pulse), 255),
                  C2D_AlignCenter, 0);
        C2D_DrawRectSolid(142, 179, 0.30f, 116, 2, accent);
        draw_text("MADE BY GRIMZZZZ", 200, 195, 0.48f, C2D_Color32(220, 226, 238, 255), C2D_AlignCenter, 0);
        C2D_TargetClear(bot, C2D_Color32(7, 10, 17, 255));
        C2D_SceneBegin(bot);
        draw_text("YOUR MUSIC. YOUR NOISE.", 160, 105, 0.48f, C2D_Color32(105, 125, 151, 255), C2D_AlignCenter, 0);
        C3D_FrameEnd(0);
    }
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
    draw_splash(top, bot);

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
                    if (is_switch_row(set_row) || is_choice_row(set_row)) set_adjust(+1);
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
        if (settings_dirty && --dirty_timer <= 0) {
            if (settings_save(&S)) settings_dirty = false;
            else dirty_timer = 180; /* retry later; do not discard unsaved changes */
        }

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
        draw_wallpaper_accents(BOT_W, SCR_H);
        draw_frame();
        if (search_busy > 0) draw_search_busy();
        else if (confirm_exit) draw_exit_confirm();
        else if (wizard_open) draw_wizard_bottom();
        else if (settings_open) draw_settings();
        else if (queue_open) draw_queue();
        else draw_browser();
        draw_toast();

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
