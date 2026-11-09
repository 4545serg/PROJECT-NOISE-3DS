#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "slither.h"

typedef struct { const char *name; u32 bg, text, accent, select; } Preset;

static const Preset presets[PRESET_COUNT] = {
    { "Slither",  0x0B1A12, 0xE6F2E9, 0x39D98A, 0x1F4D36 },
    { "Midnight", 0x0D1020, 0xE8EAF6, 0x7C8CFF, 0x26305F },
    { "Sunset",   0x1F0E14, 0xFFEFE6, 0xFF7A45, 0x5A2431 },
    { "Paper",    0xF3EFE6, 0x222222, 0xC8412F, 0xD9D2C0 },
    { "Mono",     0x101010, 0xEEEEEE, 0xFFFFFF, 0x3A3A3A },
};

const char *PRESET_NAMES[PRESET_COUNT] = { "Slither", "Midnight", "Sunset", "Paper", "Mono" };

static Rgb from_hex(u32 h)
{
    Rgb c;
    c.r = (h >> 16) & 0xFF;
    c.g = (h >> 8) & 0xFF;
    c.b = h & 0xFF;
    return c;
}

static u32 to_hex(Rgb c) { return ((u32)c.r << 16) | ((u32)c.g << 8) | c.b; }

void settings_apply_preset(Settings *s, int idx)
{
    if (idx < 0 || idx >= PRESET_COUNT) return;
    s->bg     = from_hex(presets[idx].bg);
    s->text   = from_hex(presets[idx].text);
    s->accent = from_hex(presets[idx].accent);
    s->select = from_hex(presets[idx].select);
    s->preset = idx;
}

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof *s);
    settings_apply_preset(s, 0);
    s->volume  = 80;
    s->shuffle = false;
    s->repeat  = 1;
    snprintf(s->last_dir, sizeof s->last_dir, "%s", MUSIC_ROOT);
}

void settings_load(Settings *s)
{
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return;

    char line[600];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line;
        char *val = eq + 1;
        size_t n = strlen(val);
        while (n > 0 && (val[n - 1] == '\n' || val[n - 1] == '\r')) val[--n] = 0;

        unsigned v = 0;
        if      (!strcmp(key, "bg")      && sscanf(val, "%x", &v) == 1) s->bg     = from_hex(v);
        else if (!strcmp(key, "text")    && sscanf(val, "%x", &v) == 1) s->text   = from_hex(v);
        else if (!strcmp(key, "accent")  && sscanf(val, "%x", &v) == 1) s->accent = from_hex(v);
        else if (!strcmp(key, "select")  && sscanf(val, "%x", &v) == 1) s->select = from_hex(v);
        else if (!strcmp(key, "preset")  && sscanf(val, "%d", (int *)&v) == 1) s->preset = (int)v;
        else if (!strcmp(key, "volume")  && sscanf(val, "%d", (int *)&v) == 1) s->volume = (int)v;
        else if (!strcmp(key, "shuffle")) s->shuffle = atoi(val) != 0;
        else if (!strcmp(key, "repeat")  && sscanf(val, "%d", (int *)&v) == 1) s->repeat = (int)v;
        else if (!strcmp(key, "last_dir") && strncmp(val, MUSIC_ROOT, strlen(MUSIC_ROOT)) == 0)
            snprintf(s->last_dir, sizeof s->last_dir, "%s", val);
    }
    fclose(f);

    if (s->volume < 0) s->volume = 0;
    if (s->volume > 100) s->volume = 100;
    if (s->repeat < 0 || s->repeat > 2) s->repeat = 1;
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
    fprintf(f, "shuffle=%d\n", s->shuffle ? 1 : 0);
    fprintf(f, "repeat=%d\n", s->repeat);
    fprintf(f, "last_dir=%s\n", s->last_dir);
    fclose(f);
    return true;
}
