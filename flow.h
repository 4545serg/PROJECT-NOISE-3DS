#pragma once
#include <citro2d.h>
#include <stdbool.h>
#include <stddef.h>

/* Cover Flow: a row of album covers around the focused song, drawn as perspective strips.
 *
 * Covers are loaded on a background thread (tags + JPEG/PNG decode, with an on-disk thumbnail
 * cache in SD:/3ds/PROJECT_NOISE/covers) and uploaded to the GPU one per frame, so scrolling
 * never waits for a decode. Cache entries are keyed by the entry's index in the current listing. */
#define FLOW_SIDE     3        /* covers shown on each side of the centre */
#define FLOW_MAX_WANT 16

typedef struct {
    int         kind;          /* 0 = nothing, 1 = folder, 2 = song */
    C3D_Tex    *tex;           /* NULL while a song's cover is still loading */
    u32         color;         /* fill colour for folders / not-yet-loaded covers */
    float       fade;          /* 0..1: cross-fade from the card colour to the cover */
} FlowItem;

typedef struct { int idx; char path[512]; } FlowWant;

/* Colour of the screen background at (x, y) as C2D_Color32: lets covers dim / reflect into whatever
 * is really behind them (a plain gradient, or the ambient glow). */
typedef u32 (*FlowBgFn)(float x, float y);

void flow_init(const unsigned char *placeholder_png, size_t size);
void flow_shutdown(void);
void flow_reset(void);                         /* folder changed: forget every cached cover */
bool flow_pump(void);                          /* once per frame (main thread): upload a finished cover; true if the cache changed */
bool flow_known(int idx);                      /* cached, or being loaded right now */
void flow_want(const FlowWant *w, int n);      /* replace the load queue (nearest first) */
C3D_Tex *flow_tex(int idx, float *fade);       /* cached cover (or placeholder) or NULL */
bool flow_color(int idx, u32 *rgb);            /* ambient colour (0xRRGGBB) of a loaded cover with real artwork */

/* items[i] is the entry with index base+i.  pos = fractional centre index,
 * appear = 0..1 (0 collapses the side covers into the centre), bg = background colour lookup. */
void flow_draw(const FlowItem *items, int n, int base, float pos, float appear, FlowBgFn bg);
