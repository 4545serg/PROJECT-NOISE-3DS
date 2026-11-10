/*
 * PROJECT SLITHER - native Nintendo 3DS music player
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
#include "settings.h"
#include "slither.h"

/* ---------------------------------------------------------------- layout */
#define LIST_TOP   36
#define ROW_H      38
#define VIS_ROWS   4
#define CTRL_TOP   188
#define SL_X0      48          /* seek slider track, x range */
#define SL_X1      272
#define SS_X0      96          /* settings slider track, x range */
#define SS_X1      244

enum {                         /* settings rows, grouped in three pages */
    ROW_PRESET, ROW_BG, ROW_TEXT, ROW_ACCENT, ROW_HILITE,          /* Theme */
    ROW_VOL, ROW_EQPRE, ROW_B0, ROW_B1, ROW_B2, ROW_B3, ROW_B4,    /* Sound */
    ROW_CLOSE, ROW_EXIT,                                           /* More  */
    SET_ROWS
};

/* ----------------------------------------------------------------- state */
static Settings  S;
static DirList   L;                 /* folder shown in the browser          */
static DirList   Q;                 /* playlist: files of the folder played */
static int       qi = -1;           /* current index in Q                   */
static int       sel = 0, scroll_top = 0;

static bool      track_loaded = false;
static TrackMeta cur;
static char      status_msg[96];

static bool      settings_open = false;
static int       set_row = 0;
static int       set_chan[4] = { 0, 0, 0, 0 };   /* 0=R 1=G 2=B per colour row */
static bool      settings_dirty = false;
static int       set_drag = -1;               /* slider row being dragged */

/* cover flow (top screen) */
static bool      flow_show = true;            /* user is browsing -> side covers wanted */
static float     flow_appear = 1.0f;          /* 0..1 animated */
static float     flow_pos = 0.0f;             /* fractional centre index (smooth slide) */
static int       flow_last_sel = -1;
static int       flow_idle = 0;               /* frames since the selection last moved */
static int       dirty_timer = 0;

static bool      audio_ok = false;
static C2D_TextBuf tbuf;
static float     snake_phase = 0.0f;      /* drives the equalizer bars */
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

static Rgb *theme_color(int i)
{
    switch (i) {
    case 0:  return &S.bg;
    case 1:  return &S.text;
    case 2:  return &S.accent;
    default: return &S.select;
    }
}

static const char *THEME_LABELS[4] = { "Background", "Text", "Accent", "Highlight" };

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
        snprintf(status_msg, sizeof status_msg, "Cannot open file");
        return;
    }
    cur = m;
    track_loaded = true;
    art_queue(art, art_size);          /* takes ownership, NULL clears */

    if (!audio_ok) {
        snprintf(status_msg, sizeof status_msg, "Audio unavailable (DSP firmware missing)");
        return;
    }
    audio_play(path, m.audio_start);
    status_msg[0] = 0;
}

static bool is_current_row(int row)
{
    return qi >= 0 && qi < Q.n && row >= 0 && row < L.n &&
           strcmp(L.dir, Q.dir) == 0 && strcmp(L.e[row].name, Q.e[qi].name) == 0;
}

static void play_from_list(int row)
{
    if (row < 0 || row >= L.n || L.e[row].is_dir) return;
    flow_show = false;                 /* song chosen: side covers fold away */
    if (Q.n == 0 || strcmp(Q.dir, L.dir) != 0) dir_copy_files(&Q, &L);
    int idx = -1;
    for (int i = 0; i < Q.n; i++)
        if (!strcmp(Q.e[i].name, L.e[row].name)) { idx = i; break; }
    if (idx >= 0) play_q(idx);
}

static void next_track(bool manual)
{
    if (Q.n == 0) return;
    int n;
    if (S.shuffle && Q.n > 1) {
        do { n = rand() % Q.n; } while (n == qi);
    } else {
        n = qi + 1;
        if (n >= Q.n) {
            if (S.repeat == 1 || manual) n = 0;
            else { audio_stop(); return; }
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

static void toggle_play(void)
{
    if (audio_is_active()) {
        if (audio_is_paused()) audio_resume(); else audio_pause();
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

static void set_dir(const char *path, const char *select_name)
{
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
    flow_pos = (float)sel;
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

static void activate(int row)
{
    if (row < 0 || row >= L.n) return;
    sel = row;
    if (L.e[row].is_dir) {
        if (!strcmp(L.e[row].name, "..")) {
            go_up();
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
static bool is_color_row(int r)  { return r >= ROW_BG && r <= ROW_HILITE; }
static bool is_slider_row(int r) { return r == ROW_VOL || (r >= ROW_B0 && r <= ROW_B4); }
static int  row_page(int r)      { return r < ROW_VOL ? 0 : (r < ROW_CLOSE ? 1 : 2); }

static const int   PAGE_FIRST[3] = { ROW_PRESET, ROW_VOL, ROW_CLOSE };
static const int   PAGE_COUNT[3] = { 5, 7, 2 };
static const float PAGE_ROWH[3]  = { 30.0f, 22.0f, 36.0f };
static const char *PAGE_NAMES[3] = { "Theme", "Sound", "More" };

static void eq_push(void) { audio_set_eq(S.eq); }

static void set_adjust(int dir, int step)
{
    if (is_color_row(set_row)) {
        int ci = set_row - ROW_BG;
        Rgb *c = theme_color(ci);
        u8 *ch = (set_chan[ci] == 0) ? &c->r : (set_chan[ci] == 1) ? &c->g : &c->b;
        int v = (int)*ch + dir * step;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        *ch = (u8)v;
        S.preset = -1;
    } else if (set_row == ROW_PRESET) {
        int p = S.preset + dir;
        if (S.preset < 0) p = (dir > 0) ? 0 : PRESET_COUNT - 1;
        if (p < 0) p = PRESET_COUNT - 1;
        if (p >= PRESET_COUNT) p = 0;
        settings_apply_preset(&S, p);
    } else if (set_row == ROW_VOL) {
        S.volume += dir * 5;
        if (S.volume < 0) S.volume = 0;
        if (S.volume > 100) S.volume = 100;
        audio_set_volume(S.volume);
    } else if (set_row == ROW_EQPRE) {
        int p = S.eq_preset + dir;
        if (S.eq_preset < 0) p = (dir > 0) ? 0 : EQ_PRESET_COUNT - 1;
        if (p < 0) p = EQ_PRESET_COUNT - 1;
        if (p >= EQ_PRESET_COUNT) p = 0;
        settings_apply_eq_preset(&S, p);
        eq_push();
    } else if (set_row >= ROW_B0 && set_row <= ROW_B4) {
        int b = set_row - ROW_B0;
        int v = S.eq[b] + dir;
        if (v < -12) v = -12;
        if (v > 12) v = 12;
        S.eq[b] = v;
        S.eq_preset = -1;
        eq_push();
    } else {
        return;
    }
    mark_dirty();
}

/* touch / drag directly on a slider track */
static void slider_apply(int r, int px)
{
    float f = (float)(px - SS_X0) / (float)(SS_X1 - SS_X0);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    if (r == ROW_VOL) {
        S.volume = (int)(f * 100.0f + 0.5f);
        audio_set_volume(S.volume);
    } else {
        S.eq[r - ROW_B0] = (int)(f * 24.0f + 0.5f) - 12;
        S.eq_preset = -1;
        eq_push();
    }
    mark_dirty();
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

/* ------------------------------------------------------------- cover flow */
static void flow_load_next(void)       /* loads at most one missing cover, nearest first */
{
    static const int order[2 * FLOW_SIDE + 1] = { 0, 1, -1, 2, -2, 3, -3, 4, -4 };
    for (int k = 0; k < 2 * FLOW_SIDE + 1; k++) {
        int idx = sel + order[k];
        if (idx < 0 || idx >= L.n) continue;
        Entry *e = &L.e[idx];
        if (e->is_dir || flow_has(idx)) continue;
        if (is_current_row(idx) && art_ready()) continue;    /* the playing cover is already loaded */
        char path[800];
        snprintf(path, sizeof path, "%s%s", L.dir, e->name);
        flow_load(idx, path);
        return;
    }
}

static void flow_update(void)          /* once per frame: animation + selection tracking */
{
    float target = (flow_show || !track_loaded) ? 1.0f : 0.0f;
    float dl = target - flow_appear;
    if (dl > 0.12f) dl = 0.12f;
    if (dl < -0.12f) dl = -0.12f;
    flow_appear += dl;

    float dp = (float)sel - flow_pos;
    flow_pos += dp * 0.3f;
    if (fabsf(dp) < 0.01f) flow_pos = (float)sel;

    if (sel != flow_last_sel) { flow_last_sel = sel; flow_idle = 0; }
    else if (flow_idle < 1000) flow_idle++;
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

/* ------------------------------------------------ TOP: now playing screen */
static void draw_top(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));

    /* cover, centered */
    const float asz = 128, ax = 136, ay = 8;                           /* native texture size, crisp */
    C2D_DrawRectSolid(0, 0, 0.10f, 400, 1, cSel);                      /* top hairline */
    Rgb black = { 0, 0, 0 };
    rrect(ax - 1, ay + 3, asz + 6, asz + 6, 8, 0.25f, C(mix_rgb(S.bg, black, 140))); /* drop shadow */
    rrect(ax - 3, ay - 3, asz + 6, asz + 6, 8, 0.28f, cSel);          /* soft frame */
    bool use_flow = flow_appear > 0.01f && L.n > 0;
    if (use_flow) {
        FlowItem items[12];
        const int base = (int)floorf(flow_pos) - 5;
        for (int i = 0; i < 12; i++) {
            int idx = base + i;
            memset(&items[i], 0, sizeof items[i]);
            if (idx < 0 || idx >= L.n || fabsf((float)idx - flow_pos) > FLOW_SIDE + 0.6f) continue;
            Entry *e = &L.e[idx];
            if (e->is_dir) {
                items[i].kind = 1;
                items[i].color = C(mix_rgb(S.select, S.text, 60));
            } else {
                items[i].kind = 2;
                items[i].color = C(tile_color(e->name));
                items[i].tex = (is_current_row(idx) && art_ready()) ? art_image()->tex : flow_tex(idx);
            }
        }
        flow_draw(items, 12, base, flow_pos, flow_appear, C(S.bg));
    } else if (art_ready()) {
        C2D_DrawImageAt(*art_image(), ax, ay, 0.40f, NULL, asz / 128.0f, asz / 128.0f);
    } else {
        C2D_DrawRectSolid(ax, ay, 0.30f, asz, asz, cSel);
        icon_note(ax + asz / 2 - 7, ay + asz / 2 - 8, cDim);
    }

    /* label, title and artist under the cover */
    if (use_flow && flow_appear > 0.5f) {                 /* browsing: describe the focused entry */
        Entry *e = &L.e[sel];
        char pos_txt[32];
        snprintf(pos_txt, sizeof pos_txt, "%d / %d", sel + 1, L.n);
        draw_text(pos_txt, 200, 141, 0.38f, cAcc, C2D_AlignCenter, 0);
        if (e->is_dir) {
            draw_text(!strcmp(e->name, "..") ? "Back" : e->name, 200, 153, 0.65f, cText, C2D_AlignCenter, 360);
            draw_text("Folder", 200, 175, 0.48f, cDim, C2D_AlignCenter, 360);
        } else {
            const char *nm = (e->meta_loaded && e->title[0]) ? e->title : e->name;
            draw_text(nm, 200, 153, 0.65f, cText, C2D_AlignCenter, 360);
            draw_text(e->artist[0] ? e->artist : "Unknown artist", 200, 175, 0.48f, cDim, C2D_AlignCenter, 360);
        }
    } else {
        draw_text("NOW PLAYING", 200, 141, 0.38f, cAcc, C2D_AlignCenter, 0);
        if (track_loaded) {
            draw_text(cur.title, 200, 153, 0.65f, cText, C2D_AlignCenter, 360);
            draw_text(cur.artist[0] ? cur.artist : "Unknown artist", 200, 175, 0.48f, cDim, C2D_AlignCenter, 360);
        } else {
            draw_text("Nothing playing", 200, 153, 0.65f, cText, C2D_AlignCenter, 360);
            draw_text("Pick a song below", 200, 175, 0.48f, cDim, C2D_AlignCenter, 360);
        }
    }

    /* times + full-width progress bar along the bottom edge */
    u32 pos = track_loaded ? audio_position_ms() : 0;
    u32 dur = track_loaded ? cur.duration_ms : 0;
    float frac = progress_frac(pos, dur);

    char a[16], b[16];
    fmt_time(a, sizeof a, pos);
    if (dur) fmt_time(b, sizeof b, dur); else snprintf(b, sizeof b, "--:--");
    draw_text(a, 16, 195, 0.45f, cDim, C2D_AlignLeft, 0);
    draw_text(b, 384, 195, 0.45f, cDim, C2D_AlignRight, 0);

    const float by = 214;
    float dx = 6.0f + 388.0f * frac;
    C2D_DrawRectSolid(0, by, 0.30f, 400, 4, cSel);
    C2D_DrawRectSolid(0, by, 0.35f, dx, 4, cAcc);
    C2D_DrawCircleSolid(dx, by + 2, 0.40f, 6, cAcc);

    if (status_msg[0]) draw_text(status_msg, 200, 223, 0.4f, cAcc, C2D_AlignCenter, 390);
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
    C2D_DrawRectSolid(0, y, 0.30f, 320, 240 - y, C(mix_rgb(S.bg, S.select, 150)));
    C2D_DrawRectSolid(0, y, 0.32f, 320, 1, cSel);

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
    icon_prev(112, cy, 8, cText);
    C2D_DrawCircleSolid(160, cy, 0.40f, 14, cText);
    if (playing) icon_pause(160, cy, 8, cBg); else icon_play(160, cy, 8, cBg);
    icon_next(208, cy, 8, cText);
    for (int k = -1; k <= 1; k++) C2D_DrawCircleSolid(298 + k * 8, cy, 0.45f, 2.2f, cDim);   /* menu */
}

static int loads_this_frame = 0;

static void draw_browser(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select), cBg = C(S.bg);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));

    /* large title: "Library" at the top level, the folder name inside a folder */
    const char *title = "Library";
    char fname[256] = "";
    if (!is_music_root(L.dir)) {
        char tmp[512];
        snprintf(tmp, sizeof tmp, "%s", L.dir);
        size_t n = strlen(tmp);
        if (n > 0 && tmp[n - 1] == '/') tmp[--n] = 0;
        char *sl = strrchr(tmp, '/');
        snprintf(fname, sizeof fname, "%s", sl ? sl + 1 : tmp);
        if (fname[0]) title = fname;
    }
    draw_text(title, 12, 3, 0.85f, cText, C2D_AlignLeft, 176);

    rrect(196, 8, 58, 20, 10, 0.30f, S.shuffle ? cAcc : cSel);
    draw_text("Shuffle", 225, 11, 0.42f, S.shuffle ? cBg : cText, C2D_AlignCenter, 0);
    rrect(258, 8, 58, 20, 10, 0.30f, S.repeat ? cAcc : cSel);
    draw_text(S.repeat == 0 ? "Repeat" : (S.repeat == 1 ? "Rep All" : "Rep One"), 287, 11, 0.42f,
              S.repeat ? cBg : cText, C2D_AlignCenter, 0);

    if (L.n == 0) {
        draw_text("No music files here", 160, 70, 0.55f, cText, C2D_AlignCenter, 0);
        draw_text("Copy music to SD:/Music/", 160, 94, 0.45f, cDim, C2D_AlignCenter, 0);
    }

    loads_this_frame = 0;
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = scroll_top + i;
        if (idx >= L.n) break;
        Entry *e = &L.e[idx];
        float y = LIST_TOP + i * ROW_H;
        bool cur_row = is_current_row(idx);
        bool playing = cur_row && audio_is_active() && !audio_is_paused();

        if (idx == sel) rrect(4, y + 1, 304, ROW_H - 2, 8, 0.25f, cSel);
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
                float h = 6.0f + 13.0f * (0.5f + 0.5f * sinf(snake_phase * 2.0f + k * 1.7f));
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
            if (!e->meta_loaded && loads_this_frame < 2) { ensure_meta(e); loads_this_frame++; }
            const char *name = (e->meta_loaded && e->title[0]) ? e->title : e->name;
            draw_text(name, lx, y + 3, 0.58f, col, C2D_AlignLeft, 254);
            if (e->meta_loaded && e->artist[0])
                draw_text(e->artist, lx, y + 21, 0.44f, cDim, C2D_AlignLeft, 254);
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

/* ---- settings: iOS-style inset cards + large title + segmented control,
 *      Material-style filled sliders, tonal pills, round icon buttons ---- */
static void row_text(const char *s, float x, float y, float rh, float sc, u32 col, u32 align, float maxw)
{
    draw_text(s, x, y + (rh - 28.0f * sc) / 2.0f, sc, col, align, maxw);
}

static void chevron(float cx, float cy, bool right, u32 c)
{
    float d = right ? -3.0f : 3.0f;
    C2D_DrawTriangle(cx + d, cy - 5, c, cx + d, cy + 5, c, cx - d, cy, c, 0.45f);
}

static void round_btn(float cx, float cy, bool plus, u32 bg, u32 fg)
{
    C2D_DrawCircleSolid(cx, cy, 0.40f, 8.5f, bg);
    C2D_DrawRectSolid(cx - 4, cy - 1, 0.45f, 8, 2, fg);
    if (plus) C2D_DrawRectSolid(cx - 1, cy - 4, 0.45f, 2, 8, fg);
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

static void draw_settings(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select), cBg = C(S.bg);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));
    u32 cCard = C(mix_rgb(S.bg, S.select, 150));
    u32 cHi = C(mix_rgb(S.select, S.accent, 60));
    u32 cHalo = C(mix_rgb(S.select, S.accent, 110));
    u32 cDanger = C2D_Color32(255, 90, 95, 255);

    /* large title + "Done" pill */
    draw_text("Settings", 14, 2, 0.85f, cText, C2D_AlignLeft, 0);
    rrect(250, 6, 62, 22, 11, 0.30f, cAcc);
    row_text("Done", 281, 6, 22, 0.45f, cBg, C2D_AlignCenter, 0);

    /* segmented control */
    int page = row_page(set_row);
    rrect(8, 34, 304, 22, 11, 0.28f, cSel);
    rrect(10 + page * 100, 36, 100, 18, 9, 0.32f, cAcc);
    for (int i = 0; i < 3; i++)
        row_text(PAGE_NAMES[i], 60 + i * 100, 36, 18, 0.45f, i == page ? cBg : cDim, C2D_AlignCenter, 0);

    /* inset grouped card */
    const int first = PAGE_FIRST[page], n = PAGE_COUNT[page];
    const float rh = PAGE_ROWH[page], top = 62;
    rrect(6, top, 308, n * rh, 10, 0.26f, cCard);

    for (int i = 0; i < n; i++) {
        int r = first + i;
        float y = top + i * rh, cy = y + rh / 2;
        bool on = (r == set_row);
        char buf[64];

        if (on) {
            rrect(8, y + 1, 304, rh - 2, 8, 0.28f, cHi);
            rrect(9, y + rh / 2 - 7, 3, 14, 1.5f, 0.32f, cAcc);        /* nav-indicator bar */
        } else if (i > 0 && (r - 1) != set_row) {
            C2D_DrawRectSolid(18, y, 0.29f, 284, 1, cSel);
        }

        if (is_color_row(r)) {
            int ci = r - ROW_BG;
            Rgb *c = theme_color(ci);
            C2D_DrawCircleSolid(26, cy, 0.38f, 9.5f, cText);
            C2D_DrawCircleSolid(26, cy, 0.40f, 8, C(*c));
            row_text(THEME_LABELS[ci], 42, y, rh, 0.48f, cText, C2D_AlignLeft, 62);
            const char *nm[3] = { "R", "G", "B" };
            u8 vals[3] = { c->r, c->g, c->b };
            for (int k = 0; k < 3; k++) {
                float px = 108.0f + k * 50.0f;
                bool ch = (set_chan[ci] == k);
                rrect(px, y + 5, 46, rh - 10, 8, 0.34f, ch ? cAcc : cSel);
                snprintf(buf, sizeof buf, "%s %u", nm[k], (unsigned)vals[k]);
                row_text(buf, px + 23, y, rh, 0.42f, ch ? cBg : cText, C2D_AlignCenter, 0);
            }
            round_btn(274, cy, false, cSel, cText);
            round_btn(298, cy, true, cSel, cText);
        } else if (r == ROW_PRESET || r == ROW_EQPRE) {
            bool theme = (r == ROW_PRESET);
            row_text(theme ? "Preset" : "Equalizer", 18, y, rh, 0.5f, cText, C2D_AlignLeft, 80);
            int idx = theme ? S.preset : S.eq_preset;
            snprintf(buf, sizeof buf, "%s", idx >= 0 ? (theme ? PRESET_NAMES[idx] : EQ_PRESET_NAMES[idx]) : "Custom");
            row_text(buf, 236, y, rh, 0.5f, cAcc, C2D_AlignCenter, 92);
            chevron(176, cy, false, cDim);
            chevron(296, cy, true, cDim);
        } else if (is_slider_row(r)) {
            bool vol = (r == ROW_VOL);
            static const char *BAND[EQ_BANDS] = { "60 Hz", "230 Hz", "910 Hz", "3.6 kHz", "14 kHz" };
            float frac, from;
            if (vol) {
                frac = S.volume / 100.0f; from = 0.0f;
                snprintf(buf, sizeof buf, "%d%%", S.volume);
            } else {
                int g = S.eq[r - ROW_B0];
                frac = (g + 12) / 24.0f; from = 0.5f;
                snprintf(buf, sizeof buf, "%+d dB", g);
                C2D_DrawRectSolid(SS_X0 + (SS_X1 - SS_X0) * 0.5f - 0.5f, cy - 5, 0.35f, 1, 10, cDim);   /* 0 dB tick */
            }
            row_text(vol ? "Volume" : BAND[r - ROW_B0], 18, y, rh, vol ? 0.5f : 0.42f, cText, C2D_AlignLeft, 76);
            m_slider(SS_X0, SS_X1, cy, frac, from, on || set_drag == r, cSel, cAcc, cHalo);
            row_text(buf, 306, y, rh, vol ? 0.5f : 0.42f, cAcc, C2D_AlignRight, 0);
        } else if (r == ROW_CLOSE) {
            row_text("Close settings", 160, y, rh, 0.5f, cText, C2D_AlignCenter, 0);
        } else {
            row_text("Exit app", 160, y, rh, 0.5f, cDanger, C2D_AlignCenter, 0);
        }
    }

    if (page == 2) {
        draw_text("PROJECT NOISE", 160, 148, 0.45f, cDim, C2D_AlignCenter, 0);
        draw_text("MP3 player for Nintendo 3DS", 160, 162, 0.4f, cDim, C2D_AlignCenter, 0);
    }
    draw_text("D-pad: move/adjust   A: R/G/B   B: back   auto-saved", 160, 224, 0.36f, cDim, C2D_AlignCenter, 316);
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
    audio_seek(cur_path, cur.audio_start, frac, (u32)(frac * (float)cur.duration_ms));
}

static void handle_ctrl_tap(int px, int py)      /* bottom bar: slider on top, buttons below */
{
    if (py < CTRL_TOP + 24) {
        if (cur.duration_ms && track_loaded && px >= SL_X0 - 14 && px <= SL_X1 + 14) {
            slider_drag = true;
            slider_frac = frac_from_px(px);
        }
        return;
    }
    if (px >= 84 && px < 136)       prev_track();
    else if (px >= 136 && px < 184) toggle_play();
    else if (px >= 184 && px < 236) next_track(true);
    else if (px >= 262)             { settings_open = true; set_row = 0; }
}

static void handle_header_tap(int px)    /* shuffle / repeat chips */
{
    if (px >= 196 && px < 256)      { S.shuffle = !S.shuffle; mark_dirty(); }
    else if (px >= 256 && px < 318) { S.repeat = (S.repeat + 1) % 3; mark_dirty(); }
}

static void handle_settings_touch(touchPosition tp)
{
    if (tp.py < 32) {                                   /* header: Done pill */
        if (tp.px >= 244) close_settings();
        return;
    }
    if (tp.py < 60) {                                   /* segmented control */
        int t = ((int)tp.px - 10) / 100;
        if (t < 0) t = 0;
        if (t > 2) t = 2;
        set_row = PAGE_FIRST[t];
        return;
    }
    int page = row_page(set_row);
    int i = (int)(((float)tp.py - 62.0f) / PAGE_ROWH[page]);
    if (tp.py < 62 || i < 0 || i >= PAGE_COUNT[page]) return;
    int r = PAGE_FIRST[page] + i;
    set_row = r;

    if (r == ROW_CLOSE) { close_settings(); return; }
    if (r == ROW_EXIT)  { quit_req = true; return; }

    if (is_slider_row(r)) {
        if (tp.px >= SS_X0 - 12 && tp.px <= SS_X1 + 12) { set_drag = r; slider_apply(r, tp.px); }
    } else if (r == ROW_PRESET || r == ROW_EQPRE) {
        if (tp.px >= 150 && tp.px < 232)      set_adjust(-1, 1);
        else if (tp.px >= 232)                set_adjust(+1, 1);
    } else if (is_color_row(r)) {
        int ci = r - ROW_BG;
        if (tp.px >= 264 && tp.px < 286)      set_adjust(-1, 8);
        else if (tp.px >= 288)                set_adjust(+1, 8);
        else if (tp.px >= 106 && tp.px < 260) { int k = ((int)tp.px - 106) / 50; set_chan[ci] = k > 2 ? 2 : k; }
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

    audio_ok = audio_init();
    audio_set_volume(S.volume);
    eq_push();
    art_set_default(placeholder_png, placeholder_png_len);
    art_queue(NULL, 0);                 /* show the logo cover until a song with art plays */
    flow_init(placeholder_png, placeholder_png_len);
    if (!audio_ok) snprintf(status_msg, sizeof status_msg, "Audio unavailable: dspfirm.cdc missing (run DSP1)");

    set_dir(S.last_dir, NULL);
    settings_dirty = false;

    int hold_up = 0, hold_down = 0, hold_left = 0, hold_right = 0;
    touchPosition tp = { 0, 0 }, tp_start = { 0, 0 }, tp_prev = { 0, 0 };
    bool touch_in_list = false, dragging = false;
    int drag_accum = 0;

    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown(), kh = hidKeysHeld(), ku = hidKeysUp();
        if (kh & KEY_TOUCH) hidTouchRead(&tp);

        if (kd & KEY_START) kd |= KEY_A;          /* START = select / confirm */
        if (quit_req) break;

        /* ---- track end / error handling ---- */
        if (audio_take_finished()) {
            if (S.repeat == 2) play_q(qi); else next_track(false);
        }
        if (audio_take_error()) snprintf(status_msg, sizeof status_msg, "Playback error: cannot open file");

        /* ---- always-on playback keys ---- */
        if (kd & KEY_L) prev_track();
        if (kd & KEY_R) next_track(true);

        if (settings_open) {
            if (kd & (KEY_SELECT | KEY_B)) close_settings();
            if (repeat_fire(kh, KEY_DUP, &hold_up) && set_row > 0) set_row--;
            if (repeat_fire(kh, KEY_DDOWN, &hold_down) && set_row < SET_ROWS - 1) set_row++;
            int step = is_color_row(set_row) ? ((hold_left > 60 || hold_right > 60) ? 16 : 4) : 1;
            if (repeat_fire(kh, KEY_DLEFT, &hold_left))  set_adjust(-1, step);
            if (repeat_fire(kh, KEY_DRIGHT, &hold_right)) set_adjust(+1, step);
            if ((kd & KEY_A)) {
                if (is_color_row(set_row)) set_chan[set_row - ROW_BG] = (set_chan[set_row - ROW_BG] + 1) % 3;
                else if (set_row == ROW_CLOSE) close_settings();
                else if (set_row == ROW_EXIT) quit_req = true;
            }
            if (kd & KEY_TOUCH) handle_settings_touch(tp);
            if ((kh & KEY_TOUCH) && set_drag >= 0) slider_apply(set_drag, tp.px);
            if (ku & KEY_TOUCH) set_drag = -1;
        } else {
            if (kd & KEY_SELECT) { settings_open = true; set_row = 0; }

            /* A: open folder / play selected / resume.  B: pause. */
            if (kd & KEY_A) {
                if (sel < L.n && L.e[sel].is_dir) activate(sel);
                else if (sel < L.n && !is_current_row(sel)) play_from_list(sel);
                else if (audio_is_paused()) audio_resume();
                else if (!audio_is_active()) toggle_play();
            }
            if ((kd & KEY_B) && audio_is_active() && !audio_is_paused()) audio_pause();
            if (kd & KEY_X) { S.repeat = (S.repeat + 1) % 3; mark_dirty(); }
            if (kd & KEY_Y) { S.shuffle = !S.shuffle; mark_dirty(); }

            if (repeat_fire(kh, KEY_DUP, &hold_up))     move_sel(-1);
            if (repeat_fire(kh, KEY_DDOWN, &hold_down)) move_sel(+1);
            if (repeat_fire(kh, KEY_DLEFT, &hold_left))   move_sel(-VIS_ROWS);
            if (repeat_fire(kh, KEY_DRIGHT, &hold_right)) move_sel(+VIS_ROWS);

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
                activate(scroll_top + ((int)tp_start.py - LIST_TOP) / ROW_H);
            }
            if ((kh & KEY_TOUCH) && slider_drag) slider_frac = frac_from_px(tp.px);
            if ((ku & KEY_TOUCH) && slider_drag) { seek_to(slider_frac); slider_drag = false; }
            if (ku & KEY_TOUCH) touch_in_list = false;
        }

        /* ---- auto-save theme/settings shortly after the last change ---- */
        if (settings_dirty && --dirty_timer <= 0) { settings_save(&S); settings_dirty = false; }

        if (audio_is_active() && !audio_is_paused()) snake_phase += 0.08f;

        /* ---- draw ---- */
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        art_commit();
        flow_begin_frame();
        flow_update();
        if (!settings_open && flow_appear > 0.5f && flow_idle > 10 && L.n > 0) flow_load_next();
        C2D_TextBufClear(tbuf);

        u32 bg = C(S.bg);
        C2D_TargetClear(top, bg);
        C2D_SceneBegin(top);
        draw_top();

        C2D_TargetClear(bot, bg);
        C2D_SceneBegin(bot);
        if (settings_open) draw_settings(); else draw_browser();

        C3D_FrameEnd(0);
    }

    snprintf(S.last_dir, sizeof S.last_dir, "%s", L.dir);
    settings_save(&S);

    if (audio_ok) { audio_stop(); audio_exit(); }
    art_shutdown();
    flow_shutdown();
    dir_free(&L);
    dir_free(&Q);
    C2D_TextBufDelete(tbuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
