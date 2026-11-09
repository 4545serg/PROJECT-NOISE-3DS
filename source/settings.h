#pragma once
#include <3ds.h>
#include <stdbool.h>

typedef struct { u8 r, g, b; } Rgb;

typedef struct {
    Rgb  bg, text, accent, select;   /* theme colours                     */
    int  preset;                     /* -1 = custom, else preset index    */
    int  volume;                     /* 0..100                            */
    bool shuffle;
    int  repeat;                     /* 0 = off, 1 = all, 2 = one         */
    char last_dir[512];              /* last browsed folder               */
} Settings;

#define PRESET_COUNT 5
extern const char *PRESET_NAMES[PRESET_COUNT];

void settings_defaults(Settings *s);
void settings_apply_preset(Settings *s, int idx);
void settings_load(Settings *s);
bool settings_save(const Settings *s);
