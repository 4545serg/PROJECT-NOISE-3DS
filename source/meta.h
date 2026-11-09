#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char title[128];      /* UTF-8; falls back to the file name      */
    char artist[128];     /* UTF-8; may be empty                     */
    u32  duration_ms;     /* 0 if unknown                            */
    u32  audio_start;     /* byte offset where audio data begins     */
} TrackMeta;

/*
 * Reads ID3v2 (v2.2/2.3/2.4) and ID3v1 tags.
 *  art / art_size : pass non-NULL to also extract embedded JPEG/PNG artwork.
 *                   On success *art is malloc'd (caller frees or hands it to
 *                   art_queue) or NULL when there is no artwork.
 *  want_duration  : also probe the first MP3 frame (Xing/VBRI/CBR estimate).
 */
bool meta_read(const char *path, TrackMeta *m, u8 **art, size_t *art_size, bool want_duration);
