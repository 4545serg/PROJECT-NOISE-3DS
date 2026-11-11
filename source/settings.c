#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "noise.h"

typedef struct { const char *name; u32 bg, text, accent, select; } Preset;

static const Preset presets[PRESET_COUNT] = {
    { "Noise",   0x0B0D12, 0xF4F6FA, 0x3D9CFF, 0x1C2431 },
    { "Emerald", 0x0A1410, 0xE8F6EE, 0x35D07F, 0x1B3A2C },
    { "Ocean",   0x0B1720, 0xE8F4F8, 0x39B8D6, 0x183A49 },
    { "Sunset",  0x1F0E14, 0xFFEFE6, 0xFF7A45, 0x5A2431 },
    { "Mono",    0x101010, 0xEEEEEE, 0xFFFFFF, 0x3A3A3A }
};

const char *PRESET_NAMES[PRESET_COUNT] = {
    "Noise", "Emerald", "Ocean", "Sunset", "Mono"
};

static const int eq_presets[EQ_PRESET_COUNT][EQ_BANDS] = {
    {  0,  0,  0,  0,  0 },   /* Flat        */
    {  6,  4,  0,  0,  0 },   /* Bass Boost  */
    {  0,  0,  0,  4,  6 },   /* Treble      */
    { -2,  0,  3,  3,  0 },   /* Vocal       */
    {  4,  2, -2,  2,  4 },   /* Rock        */
    { -1,  2,  4,  2, -1 }    /* Pop         */
};

const char *EQ_PRESET_NAMES[EQ_PRESET_COUNT] = {
    "Flat", "Bass Boost", "Treble", "Vocal", "Rock", "Pop"
};

void settings_eq_refresh(Settings *s)
{
    if (s->eq_preset < 0 || s->eq_preset >= EQ_PRESET_COUNT) s->eq_preset = 0;
    for (int i = 0; i < EQ_BANDS; i++)
        s->eq[i] = s->eq_manual ? s->eq_user[i] : eq_presets[s->eq_preset][i];
}

void settings_apply_eq_preset(Settings *s, int idx)
{
    if (idx < 0 || idx >= EQ_PRESET_COUNT) return;
    s->eq_preset = idx;
    settings_eq_refresh(s);
}

static Rgb from_hex(u32 value)
{
    Rgb c;
    c.r = (value >> 16) & 0xFF;
    c.g = (value >> 8) & 0xFF;
    c.b = value & 0xFF;
    return c;
}

static u32 to_hex(Rgb c)
{
    return ((u32)c.r << 16) | ((u32)c.g << 8) | c.b;
}

void settings_apply_preset(Settings *s, int index)
{
    if (index < 0 || index >= PRESET_COUNT) return;
    s->bg = from_hex(presets[index].bg);
    s->text = from_hex(presets[index].text);
    s->accent = from_hex(presets[index].accent);
    s->select = from_hex(presets[index].select);
    s->preset = index;
}

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof(*s));
    settings_apply_preset(s, 0);
    s->volume = 80;
    settings_apply_eq_preset(s, 0);
    s->shuffle = false;
    s->repeat = 1;
    snprintf(s->last_dir, sizeof(s->last_dir), "%s", MUSIC_ROOT);
}

void settings_load(Settings *s)
{
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return;

    char line[600];
    while (fgets(line, sizeof(line), f)) {
        char *equals = strchr(line, '=');
        if (!equals) continue;
        *equals = '\0';
        char *key = line;
        char *value = equals + 1;
        size_t n = strlen(value);
        while (n && (value[n - 1] == '\n' || value[n - 1] == '\r'))
            value[--n] = '\0';

        unsigned parsed = 0;
        if (!strcmp(key, "bg") && sscanf(value, "%x", &parsed) == 1)
            s->bg = from_hex(parsed);
        else if (!strcmp(key, "text") && sscanf(value, "%x", &parsed) == 1)
            s->text = from_hex(parsed);
        else if (!strcmp(key, "accent") && sscanf(value, "%x", &parsed) == 1)
            s->accent = from_hex(parsed);
        else if (!strcmp(key, "select") && sscanf(value, "%x", &parsed) == 1)
            s->select = from_hex(parsed);
        else if (!strcmp(key, "preset") && sscanf(value, "%d", (int *)&parsed) == 1)
            s->preset = (int)parsed;
        else if (!strcmp(key, "volume") && sscanf(value, "%d", (int *)&parsed) == 1)
            s->volume = (int)parsed;
        else if (!strcmp(key, "eq_preset") && sscanf(value, "%d", (int *)&parsed) == 1)
            s->eq_preset = (int)parsed;
        else if (!strcmp(key, "eq_user") || !strcmp(key, "eq")) {      /* "eq" = older settings files */
            int g[EQ_BANDS];
            if (sscanf(value, "%d,%d,%d,%d,%d", &g[0], &g[1], &g[2], &g[3], &g[4]) == EQ_BANDS)
                for (int i = 0; i < EQ_BANDS; i++) s->eq_user[i] = g[i];
        }
        else if (!strcmp(key, "eq_manual"))
            s->eq_manual = atoi(value) != 0;
        else if (!strcmp(key, "lid_play"))
            s->lid_play = atoi(value) != 0;
        else if (!strcmp(key, "clock_mode"))
            s->clock_mode = atoi(value);
        else if (!strcmp(key, "show_batt"))
            s->show_batt = atoi(value) != 0;
        else if (!strcmp(key, "setup_done"))
            s->setup_done = atoi(value) != 0;
        else if (!strcmp(key, "shuffle"))
            s->shuffle = atoi(value) != 0;
        else if (!strcmp(key, "repeat") && sscanf(value, "%d", (int *)&parsed) == 1)
            s->repeat = (int)parsed;
        else if (!strcmp(key, "last_dir") &&
                 strncmp(value, MUSIC_ROOT, strlen(MUSIC_ROOT)) == 0)
            snprintf(s->last_dir, sizeof(s->last_dir), "%s", value);
    }
    fclose(f);

    if (s->volume < 0) s->volume = 0;
    if (s->volume > 100) s->volume = 100;
    if (s->repeat < 0 || s->repeat > 2) s->repeat = 1;
    if (s->clock_mode < 0 || s->clock_mode > 2) s->clock_mode = 0;
    if (s->eq_preset < 0) { s->eq_manual = true; s->eq_preset = 0; }   /* older files: "custom" = manual */
    if (s->eq_preset >= EQ_PRESET_COUNT) s->eq_preset = 0;
    for (int i = 0; i < EQ_BANDS; i++) {
        if (s->eq_user[i] < -12) s->eq_user[i] = -12;
        if (s->eq_user[i] > 12) s->eq_user[i] = 12;
    }
    settings_eq_refresh(s);
    if (s->preset < -1 || s->preset >= PRESET_COUNT) s->preset = -1;
}

bool settings_save(const Settings *s)
{
    mkdir(DATA_ROOT, 0777);
    mkdir(DATA_DIR, 0777);
    FILE *f = fopen(SETTINGS_PATH, "w");
    if (!f) return false;

    fprintf(f, "bg=%06X\n", (unsigned)to_hex(s->bg));
    fprintf(f, "text=%06X\n", (unsigned)to_hex(s->text));
    fprintf(f, "accent=%06X\n", (unsigned)to_hex(s->accent));
    fprintf(f, "select=%06X\n", (unsigned)to_hex(s->select));
    fprintf(f, "preset=%d\n", s->preset);
    fprintf(f, "volume=%d\n", s->volume);
    fprintf(f, "eq_preset=%d\n", s->eq_preset);
    fprintf(f, "eq_manual=%d\n", s->eq_manual ? 1 : 0);
    fprintf(f, "eq_user=%d,%d,%d,%d,%d\n", s->eq_user[0], s->eq_user[1], s->eq_user[2], s->eq_user[3], s->eq_user[4]);
    fprintf(f, "lid_play=%d\n", s->lid_play ? 1 : 0);
    fprintf(f, "clock_mode=%d\n", s->clock_mode);
    fprintf(f, "show_batt=%d\n", s->show_batt ? 1 : 0);
    fprintf(f, "setup_done=%d\n", s->setup_done ? 1 : 0);
    fprintf(f, "shuffle=%d\n", s->shuffle ? 1 : 0);
    fprintf(f, "repeat=%d\n", s->repeat);
    fprintf(f, "last_dir=%s\n", s->last_dir);
    fclose(f);
    return true;
}
