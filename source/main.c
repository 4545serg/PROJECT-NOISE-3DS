/*
 * PROJECT NOISE - native Nintendo 3DS MP3 player
 * Old 3DS / Old 3DS XL / Old 2DS compatible.
 *
 * Top screen: streaming-app inspired Now Playing view.
 * Bottom screen: clean music-library browser with mini-player.
 */
#include <3ds.h>
#include <citro2d.h>
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

#define LIST_TOP 42
#define ROW_H 30
#define VISIBLE_ROWS 5
#define MINI_TOP 198

static Settings S;
static DirList L, Q;
static TrackMeta current_track;
static bool track_loaded = false;
static bool audio_ok = false;
static bool quick_menu = false;
static int queue_index = -1;
static int selected = 0;
static int scroll_top = 0;
static int menu_selected = 0;
static char status_text[96] = "";
static C2D_TextBuf text_buffer;
static float animation_phase = 0.0f;

static u32 color(Rgb c) { return C2D_Color32(c.r, c.g, c.b, 255); }

static Rgb rgb_mix(Rgb a, Rgb b, int amount)
{
    Rgb out;
    out.r = (u8)((a.r * (255 - amount) + b.r * amount) / 255);
    out.g = (u8)((a.g * (255 - amount) + b.g * amount) / 255);
    out.b = (u8)((a.b * (255 - amount) + b.b * amount) / 255);
    return out;
}

static void rounded_box(float x, float y, float w, float h, float r, u32 c)
{
    C2D_DrawRectSolid(x + r, y, 0.3f, w - 2 * r, h, c);
    C2D_DrawRectSolid(x, y + r, 0.3f, w, h - 2 * r, c);
    C2D_DrawCircleSolid(x + r, y + r, 0.3f, r, c);
    C2D_DrawCircleSolid(x + w - r, y + r, 0.3f, r, c);
    C2D_DrawCircleSolid(x + r, y + h - r, 0.3f, r, c);
    C2D_DrawCircleSolid(x + w - r, y + h - r, 0.3f, r, c);
}

static void text(const char *value, float x, float y, float scale,
                 u32 tint, u32 align, float max_width)
{
    char buffer[300];
    snprintf(buffer, sizeof(buffer), "%s", value ? value : "");
    C2D_Text parsed;
    C2D_TextParse(&parsed, text_buffer, buffer);

    float width = 0.0f, height = 0.0f;
    C2D_TextGetDimensions(&parsed, scale, scale, &width, &height);
    if (max_width > 0.0f && width > max_width) {
        size_t length = strlen(buffer);
        size_t cut = (size_t)((float)length * max_width / width);
        if (cut > 4) cut -= 3;
        if (cut >= sizeof(buffer)) cut = sizeof(buffer) - 4;
        while (cut > 0 && (((unsigned char)buffer[cut]) & 0xC0) == 0x80) cut--;
        strcpy(buffer + cut, "...");
        C2D_TextParse(&parsed, text_buffer, buffer);
    }
    C2D_TextOptimize(&parsed);
    C2D_DrawText(&parsed, C2D_WithColor | align, x, y, 0.5f,
                 scale, scale, tint);
}

static void format_time(char *out, size_t size, u32 milliseconds)
{
    unsigned seconds = milliseconds / 1000;
    snprintf(out, size, "%u:%02u", seconds / 60, seconds % 60);
}

static float progress(void)
{
    u32 duration = track_loaded ? current_track.duration_ms : 0;
    u32 position = track_loaded ? audio_position_ms() : 0;
    if (!duration) return 0.0f;
    float value = (float)position / (float)duration;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    return value;
}

static bool row_is_current(int row)
{
    return queue_index >= 0 && queue_index < Q.n && row >= 0 && row < L.n &&
           strcmp(L.dir, Q.dir) == 0 &&
           strcmp(L.e[row].name, Q.e[queue_index].name) == 0;
}

static void play_queue(int index)
{
    if (index < 0 || index >= Q.n) return;

    char path[800];
    snprintf(path, sizeof(path), "%s%s", Q.dir, Q.e[index].name);

    TrackMeta metadata;
    u8 *cover = NULL;
    size_t cover_size = 0;
    if (!meta_read(path, &metadata, &cover, &cover_size, true)) {
        snprintf(status_text, sizeof(status_text), "Could not read this MP3");
        free(cover);
        return;
    }

    queue_index = index;
    current_track = metadata;
    track_loaded = true;
    art_queue(cover, cover_size);

    if (!audio_ok) {
        snprintf(status_text, sizeof(status_text), "Run DSP1 once to install DSP firmware");
        return;
    }

    audio_play(path, metadata.audio_start);
    status_text[0] = '\0';
}

static void play_selected_file(int row)
{
    if (row < 0 || row >= L.n || L.e[row].is_dir) return;
    dir_copy_files(&Q, &L);
    for (int i = 0; i < Q.n; i++) {
        if (strcmp(Q.e[i].name, L.e[row].name) == 0) {
            play_queue(i);
            return;
        }
    }
}

static void next_track(bool manual)
{
    if (Q.n <= 0) return;
    int next;
    if (S.shuffle && Q.n > 1) {
        do { next = rand() % Q.n; } while (next == queue_index);
    } else {
        next = queue_index + 1;
        if (next >= Q.n) {
            if (S.repeat == 1 || manual) next = 0;
            else { audio_stop(); return; }
        }
    }
    play_queue(next);
}

static void previous_track(void)
{
    if (Q.n <= 0) return;
    if (audio_is_active() && audio_position_ms() > 3000) {
        play_queue(queue_index);
        return;
    }
    int previous = queue_index - 1;
    if (previous < 0) previous = Q.n - 1;
    play_queue(previous);
}

static void toggle_playback(void)
{
    if (audio_is_active()) {
        if (audio_is_paused()) audio_resume();
        else audio_pause();
    } else if (track_loaded && queue_index >= 0) {
        play_queue(queue_index);
    } else if (selected < L.n && !L.e[selected].is_dir) {
        play_selected_file(selected);
    }
}

static void clamp_list(void)
{
    int maximum = L.n - VISIBLE_ROWS;
    if (maximum < 0) maximum = 0;
    if (scroll_top < 0) scroll_top = 0;
    if (scroll_top > maximum) scroll_top = maximum;
}

static void load_folder(const char *path, const char *select_name)
{
    if (!dir_load(&L, path) && !is_music_root(path))
        dir_load(&L, MUSIC_ROOT);

    selected = 0;
    scroll_top = 0;
    if (select_name) {
        for (int i = 0; i < L.n; i++) {
            if (strcmp(L.e[i].name, select_name) == 0) {
                selected = i;
                break;
            }
        }
    }
    scroll_top = selected - 2;
    clamp_list();
    snprintf(S.last_dir, sizeof(S.last_dir), "%s", L.dir);
}

static void go_up_folder(void)
{
    char path[512], child[256];
    snprintf(path, sizeof(path), "%s", L.dir);
    size_t length = strlen(path);
    if (length && path[length - 1] == '/') path[--length] = '\0';
    char *slash = strrchr(path, '/');
    if (!slash) return;
    snprintf(child, sizeof(child), "%s", slash + 1);
    slash[1] = '\0';
    load_folder(path, child);
}

static void activate_row(int row)
{
    if (row < 0 || row >= L.n) return;
    selected = row;
    if (L.e[row].is_dir) {
        if (strcmp(L.e[row].name, "..") == 0) {
            go_up_folder();
        } else {
            char path[800];
            snprintf(path, sizeof(path), "%s%s/", L.dir, L.e[row].name);
            load_folder(path, NULL);
        }
    } else {
        play_selected_file(row);
    }
}

static void move_selection(int amount)
{
    if (!L.n) return;
    selected += amount;
    if (selected < 0) selected = 0;
    if (selected >= L.n) selected = L.n - 1;
    if (selected < scroll_top) scroll_top = selected;
    if (selected >= scroll_top + VISIBLE_ROWS)
        scroll_top = selected - VISIBLE_ROWS + 1;
    clamp_list();
}

static void draw_note(float x, float y, u32 tint)
{
    C2D_DrawCircleSolid(x + 4, y + 13, 0.4f, 3.5f, tint);
    C2D_DrawRectSolid(x + 6.5f, y, 0.4f, 2, 13, tint);
    C2D_DrawRectSolid(x + 6.5f, y, 0.4f, 6, 2, tint);
}

static void draw_play_icon(float x, float y, float size, u32 tint)
{
    C2D_DrawTriangle(x - size, y - size, tint,
                     x - size, y + size, tint,
                     x + size, y, tint, 0.4f);
}

static void draw_pause_icon(float x, float y, float size, u32 tint)
{
    C2D_DrawRectSolid(x - size, y - size, 0.4f, size * 0.55f, size * 2, tint);
    C2D_DrawRectSolid(x + size * 0.35f, y - size, 0.4f, size * 0.55f, size * 2, tint);
}

static void draw_previous_icon(float x, float y, u32 tint)
{
    C2D_DrawRectSolid(x - 10, y - 9, 0.4f, 2, 18, tint);
    C2D_DrawTriangle(x + 9, y - 9, tint, x + 9, y + 9, tint,
                     x - 4, y, tint, 0.4f);
}

static void draw_next_icon(float x, float y, u32 tint)
{
    C2D_DrawTriangle(x - 9, y - 9, tint, x - 9, y + 9, tint,
                     x + 4, y, tint, 0.4f);
    C2D_DrawRectSolid(x + 8, y - 9, 0.4f, 2, 18, tint);
}

static void draw_top_screen(void)
{
    u32 white = color(S.text);
    u32 muted = color(rgb_mix(S.text, S.bg, 115));
    u32 accent = color(S.accent);
    u32 panel = color(S.select);
    u32 dark = color(S.bg);

    text("PROJECT NOISE", 16, 9, 0.55f, accent, C2D_AlignLeft, 0);
    text("NOW PLAYING", 384, 10, 0.42f, muted, C2D_AlignRight, 0);

    rounded_box(113, 29, 174, 142, 10, panel);
    if (art_ready()) {
        C2D_DrawImageAt(*art_image(), 126, 35, 0.4f, NULL,
                        148.0f / 128.0f, 130.0f / 128.0f);
    } else {
        draw_note(192, 88, accent);
        text("NO COVER ART", 200, 132, 0.38f, muted, C2D_AlignCenter, 0);
    }

    if (track_loaded) {
        text(current_track.title, 200, 176, 0.68f, white,
             C2D_AlignCenter, 360);
        text(current_track.artist[0] ? current_track.artist : "Unknown artist",
             200, 199, 0.45f, muted, C2D_AlignCenter, 340);
    } else {
        text("Nothing playing", 200, 180, 0.65f, white,
             C2D_AlignCenter, 360);
        text("Choose a song from your library", 200, 204, 0.42f,
             muted, C2D_AlignCenter, 360);
    }

    bool playing = audio_is_active() && !audio_is_paused();
    C2D_DrawCircleSolid(200, 146, 0.4f, 19, accent);
    if (playing) draw_pause_icon(200, 146, 7, dark);
    else draw_play_icon(200, 146, 9, dark);
    draw_previous_icon(157, 146, white);
    draw_next_icon(243, 146, white);

    float amount = progress();
    C2D_DrawRectSolid(16, 225, 0.3f, 368, 3, panel);
    C2D_DrawRectSolid(16, 225, 0.4f, 368.0f * amount, 3, accent);

    char elapsed[16], total[16];
    format_time(elapsed, sizeof(elapsed), track_loaded ? audio_position_ms() : 0);
    if (track_loaded && current_track.duration_ms)
        format_time(total, sizeof(total), current_track.duration_ms);
    else snprintf(total, sizeof(total), "--:--");
    text(elapsed, 16, 211, 0.38f, muted, C2D_AlignLeft, 0);
    text(total, 384, 211, 0.38f, muted, C2D_AlignRight, 0);

    if (status_text[0])
        text(status_text, 200, 231, 0.34f, accent, C2D_AlignCenter, 380);
}

static void draw_mini_player(void)
{
    u32 white = color(S.text);
    u32 muted = color(rgb_mix(S.text, S.bg, 120));
    u32 accent = color(S.accent);
    u32 panel = color(rgb_mix(S.bg, S.select, 145));

    C2D_DrawRectSolid(0, MINI_TOP, 0.3f, 320, 42, panel);
    C2D_DrawRectSolid(0, MINI_TOP, 0.4f, 320.0f * progress(), 2, accent);

    if (track_loaded && art_ready())
        C2D_DrawImageAt(*art_image(), 8, MINI_TOP + 7, 0.4f, NULL,
                        28.0f / 128.0f, 28.0f / 128.0f);
    else {
        rounded_box(8, MINI_TOP + 7, 28, 28, 4, color(S.select));
        draw_note(15, MINI_TOP + 13, muted);
    }

    text(track_loaded ? current_track.title : "Nothing playing",
         44, MINI_TOP + 8, 0.43f, white, C2D_AlignLeft, 116);
    text(track_loaded ? current_track.artist : "Pick a song",
         44, MINI_TOP + 24, 0.35f, muted, C2D_AlignLeft, 116);

    bool playing = audio_is_active() && !audio_is_paused();
    draw_previous_icon(205, MINI_TOP + 21, white);
    if (playing) draw_pause_icon(244, MINI_TOP + 21, 7, white);
    else draw_play_icon(244, MINI_TOP + 21, 9, white);
    draw_next_icon(283, MINI_TOP + 21, white);
}

static void draw_library(void)
{
    u32 white = color(S.text);
    u32 muted = color(rgb_mix(S.text, S.bg, 120));
    u32 accent = color(S.accent);
    u32 selected_color = color(S.select);
    u32 panel = color(rgb_mix(S.bg, S.select, 110));

    text("Your Library", 10, 5, 0.72f, white, C2D_AlignLeft, 0);
    text("SD CARD  /  MUSIC", 310, 13, 0.34f, muted, C2D_AlignRight, 0);

    rounded_box(9, 27, 72, 18, 8, accent);
    text("SONGS", 45, 30, 0.35f, color(S.bg), C2D_AlignCenter, 0);
    rounded_box(87, 27, 72, 18, 8, selected_color);
    text("FOLDERS", 123, 30, 0.35f, white, C2D_AlignCenter, 0);

    const char *folder = L.dir;
    if (!strncmp(folder, "sdmc:/", 6)) folder += 6;
    text(folder, 310, 31, 0.32f, muted, C2D_AlignRight, 145);

    if (L.n == 0) {
        text("Your library is empty", 160, 92, 0.55f, white,
             C2D_AlignCenter, 0);
        text("Put MP3 files in SD:/Music/", 160, 116, 0.4f,
             muted, C2D_AlignCenter, 0);
    }

    for (int visible = 0; visible < VISIBLE_ROWS; visible++) {
        int index = scroll_top + visible;
        if (index >= L.n) break;
        Entry *entry = &L.e[index];
        float y = LIST_TOP + visible * ROW_H;
        bool current = row_is_current(index);

        if (index == selected)
            rounded_box(5, y, 310, ROW_H - 2, 5, selected_color);

        rounded_box(10, y + 3, 24, 24, 4,
                    entry->is_dir ? selected_color : panel);
        if (entry->is_dir) {
            C2D_DrawRectSolid(15, y + 10, 0.4f, 13, 10, white);
            C2D_DrawRectSolid(15, y + 8, 0.4f, 7, 3, white);
        } else if (current && audio_is_active() && !audio_is_paused()) {
            for (int bar = 0; bar < 3; bar++) {
                float height = 5.0f + 7.0f *
                    (0.5f + 0.5f * (float)((bar + (int)animation_phase) % 3));
                C2D_DrawRectSolid(15 + bar * 5, y + 24 - height,
                                  0.4f, 3, height, accent);
            }
        } else {
            draw_note(14, y + 7, white);
        }

        if (entry->is_dir) {
            text(strcmp(entry->name, "..") == 0 ? "Back" : entry->name,
                 43, y + 8, 0.43f, white, C2D_AlignLeft, 252);
        } else {
            if (!entry->meta_loaded) {
                char path[800];
                snprintf(path, sizeof(path), "%s%s", L.dir, entry->name);
                TrackMeta metadata;
                if (meta_read(path, &metadata, NULL, NULL, false)) {
                    snprintf(entry->title, sizeof(entry->title), "%s", metadata.title);
                    snprintf(entry->artist, sizeof(entry->artist), "%s", metadata.artist);
                }
                entry->meta_loaded = true;
            }
            text(entry->title[0] ? entry->title : entry->name,
                 43, y + 3, 0.42f, current ? accent : white,
                 C2D_AlignLeft, 252);
            if (entry->artist[0])
                text(entry->artist, 43, y + 17, 0.32f, muted,
                     C2D_AlignLeft, 252);
        }
        C2D_DrawRectSolid(43, y + ROW_H - 2, 0.3f, 267, 1, selected_color);
    }

    draw_mini_player();
}

static bool key_repeat(u32 held, u32 key, int *counter)
{
    if (held & key) {
        (*counter)++;
        return *counter == 1 || (*counter > 18 && ((*counter - 18) % 4) == 0);
    }
    *counter = 0;
    return false;
}

static void handle_touch(int x, int y)
{
    if (y >= MINI_TOP) {
        if (x < 225) previous_track();
        else if (x < 264) toggle_playback();
        else next_track(true);
        return;
    }
    if (y >= LIST_TOP && y < LIST_TOP + VISIBLE_ROWS * ROW_H) {
        int row = scroll_top + (y - LIST_TOP) / ROW_H;
        activate_row(row);
    } else if (y >= 27 && y < 46) {
        if (x < 84) {
            /* Songs view: return to the Music root. */
            load_folder(MUSIC_ROOT, NULL);
        } else if (x < 164) {
            /* Folders view uses the same SD folder browser. */
            status_text[0] = '\0';
        }
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
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    text_buffer = C2D_TextBufNew(6144);

    srand((unsigned)time(NULL));
    mkdir("sdmc:/Music", 0777);
    settings_defaults(&S);
    settings_load(&S);

    audio_ok = audio_init();
    audio_set_volume(S.volume);
    if (!audio_ok)
        snprintf(status_text, sizeof(status_text), "DSP firmware missing: run DSP1 once");

    load_folder(S.last_dir[0] ? S.last_dir : MUSIC_ROOT, NULL);
    if (!L.dir[0]) load_folder(MUSIC_ROOT, NULL);

    int hold_up = 0, hold_down = 0, hold_left = 0, hold_right = 0;
    touchPosition touch = {0};
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        u32 held = hidKeysHeld();
        u32 up = hidKeysUp();

        if (held & KEY_TOUCH) hidTouchRead(&touch);
        if (down & KEY_START) break;

        if (audio_take_finished()) {
            if (S.repeat == 2) play_queue(queue_index);
            else next_track(false);
        }
        if (audio_take_error())
            snprintf(status_text, sizeof(status_text), "Playback error");

        if (down & KEY_L) previous_track();
        if (down & KEY_R) next_track(true);

        if (down & KEY_SELECT) {
            quick_menu = !quick_menu;
            menu_selected = 0;
        }

        if (quick_menu) {
            if (key_repeat(held, KEY_DUP, &hold_up) && menu_selected > 0)
                menu_selected--;
            if (key_repeat(held, KEY_DDOWN, &hold_down) && menu_selected < 2)
                menu_selected++;
            if (down & KEY_B) quick_menu = false;
            if (down & KEY_A) {
                if (menu_selected == 0) S.shuffle = !S.shuffle;
                else if (menu_selected == 1) S.repeat = (S.repeat + 1) % 3;
                else quick_menu = false;
                settings_save(&S);
            }
            if (down & KEY_TOUCH) {
                if (touch.py >= 45 && touch.py < 95) {
                    S.shuffle = !S.shuffle;
                    settings_save(&S);
                } else if (touch.py >= 95 && touch.py < 145) {
                    S.repeat = (S.repeat + 1) % 3;
                    settings_save(&S);
                } else if (touch.py >= 145) quick_menu = false;
            }
        } else {
            if (down & KEY_A) activate_row(selected);
            if (down & KEY_B && audio_is_active() && !audio_is_paused())
                audio_pause();
            if (down & KEY_X) { S.shuffle = !S.shuffle; settings_save(&S); }
            if (down & KEY_Y) { S.repeat = (S.repeat + 1) % 3; settings_save(&S); }

            if (key_repeat(held, KEY_DUP, &hold_up)) move_selection(-1);
            if (key_repeat(held, KEY_DDOWN, &hold_down)) move_selection(1);
            if (key_repeat(held, KEY_DLEFT, &hold_left)) move_selection(-VISIBLE_ROWS);
            if (key_repeat(held, KEY_DRIGHT, &hold_right)) move_selection(VISIBLE_ROWS);

            if (down & KEY_TOUCH) handle_touch(touch.px, touch.py);
        }

        if (audio_is_active() && !audio_is_paused()) animation_phase += 0.12f;

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        art_commit();
        C2D_TextBufClear(text_buffer);

        C2D_TargetClear(top, color(S.bg));
        C2D_SceneBegin(top);
        draw_top_screen();

        C2D_TargetClear(bottom, color(S.bg));
        C2D_SceneBegin(bottom);
        if (quick_menu) {
            u32 white = color(S.text);
            u32 muted = color(rgb_mix(S.text, S.bg, 120));
            text("QUICK SETTINGS", 160, 14, 0.62f, color(S.accent),
                 C2D_AlignCenter, 0);
            const char *repeat_name = S.repeat == 0 ? "Off" :
                                      (S.repeat == 1 ? "All" : "One");
            const char *items[3];
            char repeat_label[32];
            snprintf(repeat_label, sizeof(repeat_label), "Repeat: %s", repeat_name);
            items[0] = S.shuffle ? "Shuffle: On" : "Shuffle: Off";
            items[1] = repeat_label;
            items[2] = "Close";
            for (int i = 0; i < 3; i++) {
                float y = 48 + i * 48;
                if (i == menu_selected)
                    rounded_box(20, y, 280, 38, 7, color(S.select));
                text(items[i], 160, y + 11, 0.5f,
                     i == menu_selected ? white : muted,
                     C2D_AlignCenter, 0);
            }
            text("A: select   B: back   SELECT: close", 160, 211, 0.36f,
                 muted, C2D_AlignCenter, 0);
        } else {
            draw_library();
        }
        C3D_FrameEnd(0);
    }

    snprintf(S.last_dir, sizeof(S.last_dir), "%s", L.dir);
    settings_save(&S);
    if (audio_ok) { audio_stop(); audio_exit(); }
    art_shutdown();
    dir_free(&L);
    dir_free(&Q);
    C2D_TextBufDelete(text_buffer);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
