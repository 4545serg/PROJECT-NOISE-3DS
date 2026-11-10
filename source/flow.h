#pragma once
#include <citro2d.h>
#include <stdbool.h>
#include <stddef.h>

/* Cover Flow: a row of album covers around the selected song, drawn as perspective strips.
 *
 * Covers are loaded lazily (one per call) into a small texture cache keyed by the
 * entry's index in the current folder listing. */
#define FLOW_SIDE 4            /* covers shown on each side of the centre */

typedef struct {
    int         kind;          /* 0 = nothing, 1 = folder, 2 = song */
    C3D_Tex    *tex;           /* NULL while a song's cover is still loading */
    u32         color;         /* fill colour for folders / not-yet-loaded covers */
} FlowItem;

void flow_init(const unsigned char *placeholder_png, size_t size);
void flow_shutdown(void);
void flow_reset(void);                         /* folder changed: forget every cached cover */
void flow_begin_frame(void);                   /* call once per frame right after C3D_FrameBegin */
bool flow_has(int idx);
C3D_Tex *flow_tex(int idx);                    /* cached cover (or placeholder) or NULL */
void flow_load(int idx, const char *path);     /* blocking: read tags + decode one cover */

/* items[i] is the entry with index base+i.  pos = fractional centre index,
 * appear = 0..1 (0 collapses the side covers into the centre), bg = background colour. */
void flow_draw(const FlowItem *items, int n, int base, float pos, float appear, u32 bg);
