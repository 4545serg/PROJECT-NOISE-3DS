#pragma once
#include <citro2d.h>
#include <stdbool.h>
#include <stddef.h>

/* Album artwork: JPEG/PNG -> 128x128 GPU texture.
 *
 * art_queue() takes ownership of `data` (malloc'd; NULL clears the artwork).
 * art_commit() must be called once per frame right AFTER C3D_FrameBegin() and
 * BEFORE drawing, so the texture is never replaced while the GPU is using it. */
/* Cover shown when nothing (or an undecodable image) is queued. Data must outlive the app. */
void  art_set_default(const unsigned char *data, size_t size);
void  art_queue(unsigned char *data, size_t size);
void  art_commit(void);
bool  art_ready(void);
unsigned art_serial(void);   /* increments each time a new cover actually becomes visible */
const C2D_Image *art_image(void);
void  art_shutdown(void);
/* Build a standalone 128x128 texture from JPEG/PNG data (used by the cover-flow cache). */
bool  art_build_tex(C3D_Tex *tex, const unsigned char *data, size_t size);
/* Any size up to tw x th (powers of two): boot screen art. *iw / *ih = the image's own size inside the texture. */
bool  art_build_tex_ex(C3D_Tex *tex, const unsigned char *data, size_t size, int tw, int th, int *iw, int *ih);
/* Split version for background loading: art_decode() is pure CPU (any thread), art_upload()
 * creates the GPU texture (main thread). `px` is ART_SIZE*ART_SIZE*4 bytes in GPU layout. */
#define ART_PIXEL_BYTES (128 * 128 * 4)
bool  art_decode(const unsigned char *data, size_t size, unsigned char *px);
bool  art_upload(C3D_Tex *tex, const unsigned char *px);

/* Ambient colour of a cover: the dominant vivid hue (or a neutral average for grey covers), as
 * 0xRRGGBB, brightness-normalised so it makes a good glow. Pure CPU, any thread, order-independent
 * (works directly on the tiled `px` buffer). Returns false for a (nearly) black / empty image. */
bool  art_color_of(const unsigned char *px, u32 *rgb);
/* Colour of the artwork currently shown by art_image(); false while it is the default logo. */
bool  art_color(u32 *rgb);
