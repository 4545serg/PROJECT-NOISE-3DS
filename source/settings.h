#pragma once
#include <3ds.h>
#include <stdbool.h>

#ifndef EQ_BANDS
#define EQ_BANDS 5
#endif

typedef struct { u8 r, g, b; } Rgb;

typedef struct {
    Rgb  bg, text, accent, select;   /* theme colours                     */
    int  preset;                     /* -1 = custom, else preset index    */
    int  eq_preset;                  /* chosen EQ preset index (used while manual EQ is off) */
    bool eq_manual;                  /* true: use the user's own band gains instead of the preset */
    int  eq_user[EQ_BANDS];          /* the user's manual band gains, -12..+12 dB */
    int  eq[EQ_BANDS];               /* ACTIVE gains (derived: user's or preset's) - what the audio engine gets */
    int  clock_mode;                 /* top-screen clock: 0 = off, 1 = 12-hour, 2 = 24-hour */
    bool show_batt;                  /* top-screen battery percentage */
    int  glow;                       /* ambient glow around the cover: 0 = off, 1 = soft, 2 = vivid */
    int  fade_speed;                 /* cover / glow fade-in: 0 = quick, 1 = smooth, 2 = slow */
    int  display_mode;                /* 0 = album cover, 1 = spectrum, 2 = waveform, 3 = orbit */
    int  wallpaper;                   /* built-in wallpaper style, 0..4 */
    int  seek_step;                  /* D-pad Left/Right skip length in seconds (5 / 10 / 15 / 30) */
    bool setup_done;                 /* first-run setup + guide has been completed */
    bool shuffle;
    int  sleep_minutes;              /* sleep timer selection: 5, 10, 15, 30, 45 or 60 */
    int  repeat;                     /* 0 = off, 1 = all, 2 = one         */
    int  glow_color;                 /* 0 = Auto (album / theme), else a GLOWCOL preset */
    bool dyn_color;                  /* accent colours follow the playing album artwork */
    int  batt_style;                 /* 0 = battery icon, 1 = percentage only */
    bool oled;                       /* true-black interface */
    bool saver;                      /* battery saver: fewer effects, 30 fps drawing */
    bool show_info;                  /* music information (format / bitrate / times) */
    bool resume;                     /* remember the last song + position */
    bool ly_search;                  /* lyrics: look online automatically when the lyrics screen opens */
    bool ly_download;                /* lyrics: keep downloaded lyrics in the SD cache */
    bool ly_offline;                 /* lyrics: never use the network, cache only */
    bool ly_sync;                    /* lyrics: prefer synchronized (LRC) lyrics */
    int  ly_font;                    /* lyrics text style: 0 = Gothic, 1 = Bold, 2 = Compact, 3 = System */
    int  ui_ver;                     /* settings-file UI version (2 = Noise blue default theme) */
    char last_dir[512];              /* last browsed folder               */
} Settings;

#define PRESET_COUNT 6
extern const char *PRESET_NAMES[PRESET_COUNT];

#define GLOW_COUNT 3
extern const char *GLOW_NAMES[GLOW_COUNT];
#define GLOWCOL_COUNT 8
extern const char *GLOWCOL_NAMES[GLOWCOL_COUNT];
extern const unsigned GLOWCOL_RGB[GLOWCOL_COUNT];     /* index 0 (Auto) is unused */
#define FADE_COUNT 3
extern const char *FADE_NAMES[FADE_COUNT];
extern const float FADE_SECS[FADE_COUNT];
#define SEEK_STEP_COUNT 4
extern const int SEEK_STEPS[SEEK_STEP_COUNT];

#define EQ_PRESET_COUNT 6
extern const char *EQ_PRESET_NAMES[EQ_PRESET_COUNT];
void settings_apply_eq_preset(Settings *s, int idx);
void settings_eq_refresh(Settings *s);        /* recompute s->eq from eq_manual / eq_user / eq_preset */

void settings_defaults(Settings *s);
void settings_apply_preset(Settings *s, int idx);
void settings_load(Settings *s);
bool settings_save(const Settings *s);
