#pragma once
#include <citro2d.h>
#include <stdbool.h>
#include <stddef.h>

/* Album artwork: JPEG/PNG -> 128x128 GPU texture.
 *
 * art_queue() takes ownership of `data` (malloc'd; NULL clears the artwork).
 * art_commit() must be called once per frame right AFTER C3D_FrameBegin() and
 * BEFORE drawing, so the texture is never replaced while the GPU is using it. */
void  art_queue(unsigned char *data, size_t size);
void  art_commit(void);
bool  art_ready(void);
const C2D_Image *art_image(void);
void  art_shutdown(void);
