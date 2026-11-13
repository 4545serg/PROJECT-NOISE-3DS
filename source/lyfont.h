#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

/* Gothic lyrics typeface: a small pre-rendered glyph atlas (tools/make_lyfont.py -> lyfont_data.h) drawn as
 * tinted sprites. It is used ONLY by the lyrics screen; the rest of the app keeps the system font.
 * Blackletter lower case, capitals, digits, common punctuation and the Spanish set (a e i o u with acute, n tilde,
 * u diaeresis in both cases, inverted ? and !) are available; a line with any other character (other accents,
 * other scripts) is left to the system font. */

bool  lyfont_init(void);                     /* main thread, outside a frame; false = unavailable (system font is used) */
void  lyfont_exit(void);
bool  lyfont_ok(void);
bool  lyfont_can(const char *utf8);          /* every character of the line has a glyph */
float lyfont_width(const char *utf8, float sc);
/* Returns s itself when it is at most maxw wide at scale sc, otherwise a copy in out (capacity cap) cut to "text..." that fits. */
const char *lyfont_fit(const char *utf8, float sc, float maxw, char *out, size_t cap);
/* Draws one line. yc = vertical centre of the capital letters. align: C2D_AlignLeft / Center / Right. color: 0xAABBGGRR */
void  lyfont_draw(const char *utf8, float x, float yc, float sc, u32 color, u32 align, float z);
