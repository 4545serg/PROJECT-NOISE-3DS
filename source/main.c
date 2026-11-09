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
#include "settings.h"
#include "slither.h"

/* ---------------------------------------------------------------- layout */
#define LIST_TOP   26
#define ROW_H      20
#define VIS_ROWS   8
#define CTRL_TOP   192
#define SET_TOP    28
#define SET_ROW_H  24
#define SET_ROWS   7

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
static float     snake_phase = 0.0f;

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
static void draw_snake(float bx, float by, float size, u32 body, u32 eye, float phase)
{
    const int N = 16;
    float hx = 0, hy = 0, hr = 0;
    for (int k = 0; k < N; k++) {
        float t = (float)k / (float)(N - 1);
        float x = bx + size * (0.10f + 0.80f * t);
        float y = by + size * (0.50f + 0.22f * sinf(t * 7.0f + phase));
        float r = size * (0.035f + 0.070f * t);
        C2D_DrawCircleSolid(x, y, 0.40f, r, body);
        hx = x; hy = y; hr = r;
    }
    C2D_DrawCircleSolid(hx + hr * 0.25f, hy - hr * 0.30f, 0.45f, hr * 0.30f, eye);
}

static void draw_top(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));

    draw_text("NOW PLAYING", 200, 4, 0.6f, cAcc, C2D_AlignCenter, 0);

    /* artwork */
    const float ax = 144, ay = 30, asz = 112;
    if (art_ready()) {
        C2D_DrawImageAt(*art_image(), ax, ay, 0.40f, NULL, asz / 128.0f, asz / 128.0f);
    } else {
        C2D_DrawRectSolid(ax, ay, 0.30f, asz, asz, cSel);
        draw_snake(ax, ay, asz, cAcc, C(S.bg), snake_phase);
    }

    if (track_loaded) {
        draw_text(cur.title, 200, 146, 0.65f, cText, C2D_AlignCenter, 380);
        draw_text(cur.artist[0] ? cur.artist : "Unknown artist", 200, 168, 0.5f, cDim, C2D_AlignCenter, 380);
    } else {
        draw_text("No track selected", 200, 146, 0.65f, cText, C2D_AlignCenter, 380);
        draw_text("Pick a song on the bottom screen", 200, 168, 0.5f, cDim, C2D_AlignCenter, 380);
    }

    /* progress */
    u32 pos = track_loaded ? audio_position_ms() : 0;
    u32 dur = track_loaded ? cur.duration_ms : 0;
    float frac = dur ? (float)pos / (float)dur : 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    const float bx = 40, by = 196, bw = 320, bh = 6;
    C2D_DrawRectSolid(bx, by, 0.30f, bw, bh, cSel);
    C2D_DrawRectSolid(bx, by, 0.35f, bw * frac, bh, cAcc);
    C2D_DrawCircleSolid(bx + bw * frac, by + bh / 2, 0.40f, 6.0f, cAcc);

    char a[16], b[16];
    fmt_time(a, sizeof a, pos);
    if (dur) fmt_time(b, sizeof b, dur); else snprintf(b, sizeof b, "--:--");
    draw_text(a, bx, 208, 0.5f, cText, C2D_AlignLeft, 0);
    draw_text(b, bx + bw, 208, 0.5f, cText, C2D_AlignRight, 0);

    if (status_msg[0]) draw_text(status_msg, 200, 224, 0.4f, cAcc, C2D_AlignCenter, 390);
}

static void draw_button_icons(int i, float x, float y, u32 fg)
{
    switch (i) {
    case 0: /* previous */
        C2D_DrawRectSolid(x + 13, y + 12, 0.45f, 3, 16, fg);
        C2D_DrawTriangle(x + 34, y + 12, fg, x + 34, y + 28, fg, x + 18, y + 20, fg, 0.45f);
        break;
    case 1: /* play / pause */
        if (audio_is_active() && !audio_is_paused()) {
            C2D_DrawRectSolid(x + 15, y + 11, 0.45f, 6, 18, fg);
            C2D_DrawRectSolid(x + 27, y + 11, 0.45f, 6, 18, fg);
        } else {
            C2D_DrawTriangle(x + 16, y + 10, fg, x + 16, y + 30, fg, x + 34, y + 20, fg, 0.45f);
        }
        break;
    case 2: /* next */
        C2D_DrawTriangle(x + 14, y + 12, fg, x + 14, y + 28, fg, x + 30, y + 20, fg, 0.45f);
        C2D_DrawRectSolid(x + 32, y + 12, 0.45f, 3, 16, fg);
        break;
    default:
        break;
    }
}

static void draw_controls(void)
{
    u32 cSel = C(S.select), cText = C(S.text), cAcc = C(S.accent);
    u32 cDim = C(mix_rgb(S.text, S.bg, 140));
    static const char *rep_label[3] = { "OFF", "ALL", "ONE" };

    for (int i = 0; i < 6; i++) {
        float x = 6.0f + i * 52.0f, y = CTRL_TOP + 2.0f;
        C2D_DrawRectSolid(x, y, 0.30f, 48, 42, cSel);
        if (i < 3) {
            draw_button_icons(i, x, y, cText);
        } else if (i == 3) {
            u32 c = S.shuffle ? cAcc : cDim;
            draw_text("SHUF", x + 24, y + 6, 0.45f, c, C2D_AlignCenter, 0);
            draw_text(S.shuffle ? "ON" : "OFF", x + 24, y + 22, 0.45f, c, C2D_AlignCenter, 0);
        } else if (i == 4) {
            u32 c = S.repeat ? cAcc : cDim;
            draw_text("REP", x + 24, y + 6, 0.45f, c, C2D_AlignCenter, 0);
            draw_text(rep_label[S.repeat], x + 24, y + 22, 0.45f, c, C2D_AlignCenter, 0);
        } else {
            draw_text("MENU", x + 24, y + 12, 0.45f, cText, C2D_AlignCenter, 0);
        }
    }
}

static int loads_this_frame = 0;

static void draw_browser(void)
{
    u32 cText = C(S.text), cAcc = C(S.accent), cSel = C(S.select);
    u32 cDim = C(mix_rgb(S.text, S.bg, 120));

    const char *shown = L.dir;
    if (!strncmp(shown, "sdmc:/", 6)) shown += 6;
    draw_text(shown, 8, 4, 0.5f, cAcc, C2D_AlignLeft, 304);
    C2D_DrawRectSolid(8, LIST_TOP - 3, 0.30f, 304, 1, cSel);

    if (L.n == 0) {
        draw_text("No MP3 files here", 160, 80, 0.55f, cText, C2D_AlignCenter, 0);
        draw_text("Copy music to SD:/Music/", 160, 104, 0.45f, cDim, C2D_AlignCenter, 0);
    }

    loads_this_frame = 0;
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = scroll_top + i;
        if (idx >= L.n) break;
        Entry *e = &L.e[idx];
        float y = LIST_TOP + i * ROW_H;
        bool cur_row = is_current_row(idx);
        bool playing = cur_row && audio_is_active() && !audio_is_paused();

        if (idx == sel) C2D_DrawRectSolid(0, y, 0.25f, 312, ROW_H, cSel);
        u32 col = cur_row ? cAcc : cText;

        /* icon */
        if (e->is_dir) {
            C2D_DrawRectSolid(8, y + 5, 0.40f, 5, 3, col);
            C2D_DrawRectSolid(8, y + 8, 0.40f, 12, 8, col);
        } else if (playing) {
            C2D_DrawTriangle(9, y + 5, col, 9, y + 15, col, 19, y + 10, col, 0.40f);
        } else {
            C2D_DrawCircleSolid(13, y + 12, 0.40f, 3.5f, col);
            C2D_DrawRectSolid(15.5f, y + 4, 0.40f, 2, 9, col);
        }

        /* label */
        char label[300];
        if (e->is_dir) {
            snprintf(label, sizeof label, "%s", !strcmp(e->name, "..") ? ".. (up)" : e->name);
        } else {
            if (!e->meta_loaded && loads_this_frame < 2) { ensure_meta(e); loads_this_frame++; }
            if (e->meta_loaded && e->title[0]) {
                if (e->artist[0]) snprintf(label, sizeof label, "%s - %s", e->title, e->artist);
                else              snprintf(label, sizeof label, "%s", e->title);
            } else {
                snprintf(label, sizeof label, "%s", e->name);
            }
        }
        draw_text(label, 26, y + 2, 0.5f, col, C2D_AlignLeft, 282);
    }

    if (L.n > VIS_ROWS) {
        float track_h = VIS_ROWS * ROW_H;
        float th = track_h * VIS_ROWS / L.n;
        if (th < 12) th = 12;
        float ty = LIST_TOP + (track_h - th) * scroll_top / (L.n - VIS_ROWS);
        C2D_DrawRectSolid(316, LIST_TOP, 0.30f, 3, track_h, cSel);
        C2D_DrawRectSolid(316, ty, 0.35f, 3, th, cAcc);
    }

    draw_controls();
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
        } else {
            draw_text("Close", 160, y + 4, 0.5f, cText, C2D_AlignCenter, 0);
        }

        if (r < 6) {   /* touch +/- buttons */
            C2D_DrawRectSolid(266, y + 1, 0.30f, 22, SET_ROW_H - 4, cDim);
            C2D_DrawRectSolid(292, y + 1, 0.30f, 22, SET_ROW_H - 4, cDim);
            draw_text("-", 277, y + 2, 0.55f, C(S.bg), C2D_AlignCenter, 0);
            draw_text("+", 303, y + 2, 0.55f, C(S.bg), C2D_AlignCenter, 0);
        }
    }
    draw_text("D-pad: pick/adjust   A: next R/G/B   SELECT/B: back", 160, 205, 0.38f, cDim, C2D_AlignCenter, 316);
    draw_text("Changes are saved automatically", 160, 220, 0.38f, cDim, C2D_AlignCenter, 316);
}

/* ------------------------------------------------------------------ input */
static void handle_ctrl_tap(int px)
{
    int i = (px - 6) / 52;
    int off = (px - 6) % 52;
    if (px < 6 || i < 0 || i > 5 || off >= 48) return;
    switch (i) {
    case 0: prev_track(); break;
    case 1: toggle_play(); break;
    case 2: next_track(true); break;
    case 3: S.shuffle = !S.shuffle; mark_dirty(); break;
    case 4: S.repeat = (S.repeat + 1) % 3; mark_dirty(); break;
    case 5: settings_open = true; set_row = 0; break;
    }
}

static void handle_settings_touch(touchPosition tp)
{
    int r = (tp.py - SET_TOP) / SET_ROW_H;
    if (tp.py < SET_TOP || r < 0 || r >= SET_ROWS) return;
    set_row = r;
    if (r == 6) { close_settings(); return; }

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

        if (kd & KEY_START) break;

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
                if (tp.py >= CTRL_TOP) handle_ctrl_tap(tp.px);
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
