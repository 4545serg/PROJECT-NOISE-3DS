#pragma once
#include <3ds.h>
#include <stdbool.h>

/* Gothic lyrics typeface: a small pre-rendered glyph atlas (tools/make_lyfont.py -> lyfont_data.h) drawn as
 * tinted sprites. It is used ONLY by the lyrics screen; the rest of the app keeps the system font.
 * Blackletter lower case, capitals, digits and common punctuation are available; a line with any other
 * character (accents, other scripts) is left to the system font. */

bool  lyfont_init(void);                     /* main thread, outside a frame; false = unavailable (system font is used) */
void  lyfont_exit(void);
bool  lyfont_ok(void);
bool  lyfont_can(const char *utf8);          /* every character maps to an atlas glyph */
float lyfont_width(const char *utf8, float sc);
/* Draws one line. yc = vertical centre of the capital letters. align: C2D_AlignLeft / Center / Right. color: 0xAABBGGRR */
void  lyfont_draw(const char *utf8, float x, float yc, float sc, u32 color, u32 align, float z);
