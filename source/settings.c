#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "noise.h"

typedef struct { const char *name; u32 bg, text, accent, select; } Preset;

static const Preset presets[PRESET_COUNT] = {
    { "Noise",   0x040A14, 0xEAF2FF, 0x2F8CFF, 0x15366A },   /* the logo blue: default */
    { "Neon",    0x030806, 0xEEF4F2, 0x24FF68, 0x16502C },
    { "Emerald", 0x0A1410, 0xE8F6EE, 0x35D07F, 0x1B3A2C },
    { "Ocean",   0x0B1720, 0xE8F4F8, 0x39B8D6, 0x183A49 },
    { "Sunset",  0x1F0E14, 0xFFEFE6, 0xFF7A45, 0x5A2431 },
    { "Mono",    0x101010, 0xEEEEEE, 0xFFFFFF, 0x3A3A3A }
};

const char *PRESET_NAMES[PRESET_COUNT] = {
    "Noise", "Neon", "Emerald", "Ocean", "Sunset", "Mono"
};

const char *GLOW_NAMES[GLOW_COUNT] = { "Off", "Soft", "Vivid" };
const char *FADE_NAMES[FADE_COUNT] = { "Quick", "Smooth", "Slow" };
const float FADE_SECS[FADE_COUNT]   = { 0.5f, 1.1f, 2.0f };
const int   SEEK_STEPS[SEEK_STEP_COUNT] = { 5, 10, 15, 30 };

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
    s->ui_ver = 2;
    s->glow = 2;
    s->seek_step = 5;
    s->fade_speed = 1;
    s->display_mode = 0;
    s->wallpaper = 0;
    settings_apply_eq_preset(s, 0);
    s->shuffle = false;
    s->sleep_minutes = 15;
    s->repeat = 1;
    snprintf(s->last_dir, sizeof(s->last_dir), "%s", MUSIC_ROOT);
}

void settings_load(Settings *s)
{
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) {
        char backup_path[sizeof(SETTINGS_PATH) + 8];
        snprintf(backup_path, sizeof(backup_path), "%s.bak", SETTINGS_PATH);
        f = fopen(backup_path, "r");
    }
    if (!f) return;
    s->ui_ver = 1;                       /* files written before the Noise theme have no ui_ver */

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
        int parsed_int = 0;
        if (!strcmp(key, "bg") && sscanf(value, "%x", &parsed) == 1)
            s->bg = from_hex(parsed);
        else if (!strcmp(key, "text") && sscanf(value, "%x", &parsed) == 1)
            s->text = from_hex(parsed);
        else if (!strcmp(key, "accent") && sscanf(value, "%x", &parsed) == 1)
            s->accent = from_hex(parsed);
        else if (!strcmp(key, "select") && sscanf(value, "%x", &parsed) == 1)
            s->select = from_hex(parsed);
        else if (!strcmp(key, "preset") && sscanf(value, "%d", &parsed_int) == 1)
            s->preset = parsed_int;
        else if (!strcmp(key, "eq_preset") && sscanf(value, "%d", &parsed_int) == 1)
            s->eq_preset = parsed_int;
        else if (!strcmp(key, "eq_user") || !strcmp(key, "eq")) {      /* "eq" = older settings files */
            int g[EQ_BANDS];
            if (sscanf(value, "%d,%d,%d,%d,%d", &g[0], &g[1], &g[2], &g[3], &g[4]) == EQ_BANDS)
                for (int i = 0; i < EQ_BANDS; i++) s->eq_user[i] = g[i];
        }
        else if (!strcmp(key, "eq_manual"))
            s->eq_manual = atoi(value) != 0;
        else if (!strcmp(key, "clock_mode"))
            s->clock_mode = atoi(value);
        else if (!strcmp(key, "show_batt"))
            s->show_batt = atoi(value) != 0;
        else if (!strcmp(key, "glow"))
            s->glow = atoi(value);
        else if (!strcmp(key, "fade_speed"))
            s->fade_speed = atoi(value);
        else if (!strcmp(key, "display_mode"))
            s->display_mode = atoi(value);
        else if (!strcmp(key, "wallpaper"))
            s->wallpaper = atoi(value);
        else if (!strcmp(key, "seek_step"))
            s->seek_step = atoi(value);
        else if (!strcmp(key, "ui_ver"))
            s->ui_ver = atoi(value);
        else if (!strcmp(key, "setup_done"))
            s->setup_done = atoi(value) != 0;
        else if (!strcmp(key, "shuffle"))
            s->shuffle = atoi(value) != 0;
        else if (!strcmp(key, "sleep_minutes"))
            s->sleep_minutes = atoi(value);
        else if (!strcmp(key, "repeat") && sscanf(value, "%d", &parsed_int) == 1)
            s->repeat = parsed_int;
        else if (!strcmp(key, "last_dir") &&
                 strncmp(value, MUSIC_ROOT, strlen(MUSIC_ROOT)) == 0)
            snprintf(s->last_dir, sizeof(s->last_dir), "%s", value);
    }
    fclose(f);

    if (s->repeat < 0 || s->repeat > 2) s->repeat = 1;
    { const int opts[] = {5,10,15,30,45,60}; bool ok = false; for (int i=0;i<6;i++) if (s->sleep_minutes == opts[i]) ok = true; if (!ok) s->sleep_minutes = 15; }
    if (s->clock_mode < 0 || s->clock_mode > 2) s->clock_mode = 0;
    if (s->glow < 0 || s->glow >= GLOW_COUNT) s->glow = 2;
    if (s->fade_speed < 0 || s->fade_speed >= FADE_COUNT) s->fade_speed = 1;
    if (s->display_mode < 0 || s->display_mode > 3) s->display_mode = 0;
    if (s->wallpaper < 0 || s->wallpaper > 4) s->wallpaper = 0;
    {
        bool ok = false;
        for (int i = 0; i < SEEK_STEP_COUNT; i++) if (s->seek_step == SEEK_STEPS[i]) ok = true;
        if (!ok) s->seek_step = 5;
    }
    if (s->eq_preset < 0) { s->eq_manual = true; s->eq_preset = 0; }   /* older files: "custom" = manual */
    if (s->eq_preset >= EQ_PRESET_COUNT) s->eq_preset = 0;
    for (int i = 0; i < EQ_BANDS; i++) {
        if (s->eq_user[i] < -12) s->eq_user[i] = -12;
        if (s->eq_user[i] > 12) s->eq_user[i] = 12;
    }
    settings_eq_refresh(s);
    if (s->ui_ver < 2) {                 /* migrate: old Neon (0) becomes the new blue default, others shift by one */
        if (s->preset == 0) settings_apply_preset(s, 0);
        else if (s->preset >= 1) s->preset += 1;
        s->ui_ver = 2;
    }
    if (s->preset < -1 || s->preset >= PRESET_COUNT) s->preset = -1;
}

bool settings_save(const Settings *s)
{
    mkdir(DATA_ROOT, 0777);
    mkdir(DATA_DIR, 0777);
    /* Write to a temporary file so an interrupted save cannot truncate the
     * last valid settings file. */
    char temp_path[sizeof(SETTINGS_PATH) + 8];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", SETTINGS_PATH);
    FILE *f = fopen(temp_path, "w");
    if (!f) return false;

    fprintf(f, "bg=%06X\n", (unsigned)to_hex(s->bg));
    fprintf(f, "text=%06X\n", (unsigned)to_hex(s->text));
    fprintf(f, "accent=%06X\n", (unsigned)to_hex(s->accent));
    fprintf(f, "select=%06X\n", (unsigned)to_hex(s->select));
    fprintf(f, "preset=%d\n", s->preset);
    fprintf(f, "eq_preset=%d\n", s->eq_preset);
    fprintf(f, "eq_manual=%d\n", s->eq_manual ? 1 : 0);
    fprintf(f, "eq_user=%d,%d,%d,%d,%d\n", s->eq_user[0], s->eq_user[1], s->eq_user[2], s->eq_user[3], s->eq_user[4]);
    fprintf(f, "clock_mode=%d\n", s->clock_mode);
    fprintf(f, "show_batt=%d\n", s->show_batt ? 1 : 0);
    fprintf(f, "glow=%d\n", s->glow);
    fprintf(f, "fade_speed=%d\n", s->fade_speed);
    fprintf(f, "display_mode=%d\n", s->display_mode);
    fprintf(f, "wallpaper=%d\n", s->wallpaper);
    fprintf(f, "seek_step=%d\n", s->seek_step);
    fprintf(f, "ui_ver=%d\n", s->ui_ver);
    fprintf(f, "setup_done=%d\n", s->setup_done ? 1 : 0);
    fprintf(f, "shuffle=%d\n", s->shuffle ? 1 : 0);
    fprintf(f, "sleep_minutes=%d\n", s->sleep_minutes);
    fprintf(f, "repeat=%d\n", s->repeat);
    fprintf(f, "last_dir=%s\n", s->last_dir);

    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(temp_path);
        return false;
    }
    char backup_path[sizeof(SETTINGS_PATH) + 8];
    snprintf(backup_path, sizeof(backup_path), "%s.bak", SETTINGS_PATH);
    bool had_old = false;
    FILE *old = fopen(SETTINGS_PATH, "r");
    if (old) {
        fclose(old);
        remove(backup_path);
        if (rename(SETTINGS_PATH, backup_path) != 0) {
            remove(temp_path);
            return false;
        }
        had_old = true;
    }
    if (rename(temp_path, SETTINGS_PATH) != 0) {
        if (had_old) rename(backup_path, SETTINGS_PATH);
        remove(temp_path);
        return false;
    }
    if (had_old) remove(backup_path);
    return true;
}
