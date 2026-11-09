/*
 * PROJECT SLITHER - native Nintendo 3DS MP3 player
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
#include "meta.h"
#include "placeholder.h"
#include "settings.h"
#include "slither.h"

/* ---------------------------------------------------------------- layout */
#define LIST_TOP   36
#define ROW_H      30
#define VIS_ROWS   5
#define CTRL_TOP   188
#define SL_X0      48          /* seek slider track, x range */
#define SL_X1      272
#define SET_TOP    28
#define SET_ROW_H  22
#define SET_ROWS   8

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
    sel += delta;
    if (sel < 0) sel = 0;
    if (sel >= L.n) sel = L.n - 1;
    if (sel < scroll_top) scroll_top = sel;
    if (sel >= scroll_top + VIS_ROWS) scroll_top = sel - VIS_ROWS + 1;
    clamp_scroll();
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
static void set_adjust(int dir, int step)
{
    if (set_row < 4) {
        Rgb *c = theme_color(set_row);
        u8 *ch = (set_chan[set_row] == 0) ? &c->r : (set_chan[set_row] == 1) ? &c->g : &c->b;
        int v = (int)*ch + dir * step;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        *ch = (u8)v;
        S.preset = -1;
    } else if (set_row == 4) {
        int p = S.preset + dir;
        if (S.preset < 0) p = (dir > 0) ? 0 : PRESET_COUNT - 1;
        if (p < 0) p = PRESET_COUNT - 1;
        if (p >= PRESET_COUNT) p = 0;
        settings_apply_preset(&S, p);
    } else if (set_row == 5) {
        S.volume += dir * 5;
        if (S.volume < 0) S.volume = 0;
        if (S.volume > 100) S.volume = 100;
        audio_set_volume(S.volume);
    } else {
        return;
    }
    mark_dirty();
}

static void close_settings(void)
{
    settings_open = false;
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
    const float asz = 116, ax = 142, ay = 12;
    rrect(ax - 3, ay - 3, asz + 6, asz + 6, 7, 0.28f, cSel);          /* soft frame */
    if (art_ready()) {
        C2D_DrawImageAt(*art_image(), ax, ay, 0.40f, NULL, asz / 128.0f, asz / 128.0f);
    } else {
        C2D_DrawRectSolid(ax, ay, 0.30f, asz, asz, cSel);
        icon_note(ax + asz / 2 - 7, ay + asz / 2 - 8, cDim);
    }

    /* label, title and artist under the cover */
    draw_text("NOW PLAYING", 200, 134, 0.4f, cAcc, C2D_AlignCenter, 0);
    if (track_loaded) {
        draw_text(cur.title, 200, 148, 0.7f, cText, C2D_AlignCenter, 340);
        draw_text(cur.artist[0] ? cur.artist : "Unknown artist", 200, 174, 0.5f, cDim, C2D_AlignCenter, 340);
    } else {
        draw_text("Nothing playing", 200, 148, 0.7f, cText, C2D_AlignCenter, 340);
        draw_text("Pick a song below", 200, 174, 0.5f, cDim, C2D_AlignCenter, 340);
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
        draw_text("No MP3 files here", 160, 70, 0.55f, cText, C2D_AlignCenter, 0);
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
        const float tx = 12, ty = y + 3, ts = 24;
        Rgb tc = e->is_dir ? mix_rgb(S.select, S.text, 60) : tile_color(e->name);
        rrect(tx, ty, ts, ts, 5, 0.32f, C(tc));
        if (e->is_dir) {
            C2D_DrawRectSolid(tx + 5, ty + 7, 0.45f, 6, 3, cText);
            C2D_DrawRectSolid(tx + 5, ty + 10, 0.45f, 14, 8, cText);
        } else if (playing) {
            for (int k = 0; k < 3; k++) {
                float h = 5.0f + 10.0f * (0.5f + 0.5f * sinf(snake_phase * 2.0f + k * 1.7f));
                C2D_DrawRectSolid(tx + 5 + k * 6, ty + ts - 5 - h, 0.45f, 4, h, cAcc);
            }
        } else {
            icon_note(tx + 5, ty + 4, cText);
        }

        /* text */
        const float lx = 44;
        if (e->is_dir) {
            draw_text(!strcmp(e->name, "..") ? "Back" : e->name, lx, y + 7, 0.55f, col, C2D_AlignLeft, 262);
        } else {
            if (!e->meta_loaded && loads_this_frame < 2) { ensure_meta(e); loads_this_frame++; }
            const char *name = (e->meta_loaded && e->title[0]) ? e->title : e->name;
            draw_text(name, lx, y + 2, 0.5f, col, C2D_AlignLeft, 262);
            if (e->meta_loaded && e->artist[0])
                draw_text(e->artist, lx, y + 16, 0.4f, cDim, C2D_AlignLeft, 262);
        }

        C2D_DrawRectSolid(lx, y + ROW_H - 1, 0.30f, 264, 1, cSel);   /* divider */
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

static void draw_settings(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));

    draw_text("SETTINGS", 160, 3, 0.55f, cAcc, C2D_AlignCenter, 0);

    for (int r = 0; r < SET_ROWS; r++) {
        float y = SET_TOP + r * SET_ROW_H;
        if (r == set_row) C2D_DrawRectSolid(0, y, 0.25f, 320, SET_ROW_H - 2, cSel);

        char buf[64];
        if (r < 4) {
            Rgb *c = theme_color(r);
            draw_text(THEME_LABELS[r], 8, y + 4, 0.5f, cText, C2D_AlignLeft, 80);
            C2D_DrawRectSolid(90, y + 3, 0.40f, 18, 16, C(*c));
            C2D_DrawRectSolid(90, y + 3, 0.38f, 18, 16, cText);        /* outline */
            C2D_DrawRectSolid(91, y + 4, 0.40f, 16, 14, C(*c));
            const char *nm[3] = { "R", "G", "B" };
            u8 vals[3] = { c->r, c->g, c->b };
            for (int k = 0; k < 3; k++) {
                float cx = 114.0f + k * 50.0f;
                if (set_chan[r] == k) C2D_DrawRectSolid(cx - 2, y + 1, 0.30f, 48, SET_ROW_H - 4, cAcc);
                snprintf(buf, sizeof buf, "%s %3u", nm[k], (unsigned)vals[k]);
                draw_text(buf, cx, y + 4, 0.45f, (set_chan[r] == k) ? C(S.bg) : cText, C2D_AlignLeft, 0);
            }
        } else if (r == 4) {
            draw_text("Preset", 8, y + 4, 0.5f, cText, C2D_AlignLeft, 80);
            snprintf(buf, sizeof buf, "%s", S.preset >= 0 ? PRESET_NAMES[S.preset] : "Custom");
            draw_text(buf, 190, y + 4, 0.5f, cAcc, C2D_AlignCenter, 120);
        } else if (r == 5) {
            draw_text("Volume", 8, y + 4, 0.5f, cText, C2D_AlignLeft, 80);
            snprintf(buf, sizeof buf, "%d%%", S.volume);
            draw_text(buf, 190, y + 4, 0.5f, cAcc, C2D_AlignCenter, 120);
        } else if (r == 6) {
            draw_text("Close menu", 160, y + 3, 0.5f, cText, C2D_AlignCenter, 0);
        } else {
            draw_text("Exit app", 160, y + 3, 0.5f, cAcc, C2D_AlignCenter, 0);
        }

        if (r < 6) {   /* touch +/- buttons */
            C2D_DrawRectSolid(266, y + 1, 0.30f, 22, SET_ROW_H - 4, cDim);
            C2D_DrawRectSolid(292, y + 1, 0.30f, 22, SET_ROW_H - 4, cDim);
            draw_text("-", 277, y + 2, 0.55f, C(S.bg), C2D_AlignCenter, 0);
            draw_text("+", 303, y + 2, 0.55f, C(S.bg), C2D_AlignCenter, 0);
        }
    }
    draw_text("D-pad: pick/adjust   A/START: next R/G/B   SELECT/B: back", 160, 207, 0.38f, cDim, C2D_AlignCenter, 316);
    draw_text("Changes are saved automatically", 160, 222, 0.38f, cDim, C2D_AlignCenter, 316);
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
    int r = (tp.py - SET_TOP) / SET_ROW_H;
    if (tp.py < SET_TOP || r < 0 || r >= SET_ROWS) return;
    set_row = r;
    if (r == 6) { close_settings(); return; }
    if (r == 7) { quit_req = true; return; }

    int step = (r < 4) ? 8 : 1;
    if (tp.px >= 266 && tp.px < 288)      set_adjust(-1, step);
    else if (tp.px >= 292 && tp.px < 314) set_adjust(+1, step);
    else if (r < 4 && tp.px >= 112 && tp.px < 264) set_chan[r] = (tp.px - 112) / 50 > 2 ? 2 : (tp.px - 112) / 50;
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
    art_set_default(placeholder_png, placeholder_png_len);
    art_queue(NULL, 0);                 /* show the logo cover until a song with art plays */
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
            int step = (set_row < 4) ? ((hold_left > 60 || hold_right > 60) ? 16 : 4) : 1;
            if (repeat_fire(kh, KEY_DLEFT, &hold_left))  set_adjust(-1, step);
            if (repeat_fire(kh, KEY_DRIGHT, &hold_right)) set_adjust(+1, step);
            if ((kd & KEY_A)) {
                if (set_row < 4) set_chan[set_row] = (set_chan[set_row] + 1) % 3;
                else if (set_row == 6) close_settings();
                else if (set_row == 7) quit_req = true;
            }
            if (kd & KEY_TOUCH) handle_settings_touch(tp);
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
            if (kd & KEY_X) { S.shuffle = !S.shuffle; mark_dirty(); }
            if (kd & KEY_Y) { S.repeat = (S.repeat + 1) % 3; mark_dirty(); }

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
    dir_free(&L);
    dir_free(&Q);
    C2D_TextBufDelete(tbuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
