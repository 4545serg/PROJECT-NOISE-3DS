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
    int  volume;                     /* 0..100                            */
    int  eq_preset;                  /* -1 = custom, else EQ preset index */
    int  eq[EQ_BANDS];               /* band gains in dB, -12..+12        */
    bool lid_play;                   /* keep playing when the lid is closed */
    bool shuffle;
    int  repeat;                     /* 0 = off, 1 = all, 2 = one         */
    char last_dir[512];              /* last browsed folder               */
} Settings;

#define PRESET_COUNT 5
extern const char *PRESET_NAMES[PRESET_COUNT];

#define EQ_PRESET_COUNT 6
extern const char *EQ_PRESET_NAMES[EQ_PRESET_COUNT];
void settings_apply_eq_preset(Settings *s, int idx);

void settings_defaults(Settings *s);
void settings_apply_preset(Settings *s, int idx);
void settings_load(Settings *s);
bool settings_save(const Settings *s);
