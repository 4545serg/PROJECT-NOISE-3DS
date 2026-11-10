#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>

typedef struct {
    char title[128];      /* UTF-8; falls back to the file name      */
    char artist[128];     /* UTF-8; may be empty                     */
    u32  duration_ms;     /* 0 if unknown                            */
    u32  audio_start;     /* byte offset where audio data begins     */
} TrackMeta;

/* Parsed RIFF/WAVE header (PCM or IEEE float). */
typedef struct {
    u16 tag, channels, bits, align;      /* tag: 1 = PCM, 3 = float (WAVE_FORMAT_EXTENSIBLE is resolved) */
    u32 rate, byterate;
    u32 data_off, data_size;             /* byte range of the sample data */
} WavInfo;

/* Parses the RIFF header of an open file. m (optional) receives LIST/INFO title+artist. */
bool wav_parse(FILE *f, WavInfo *w, TrackMeta *m);

/*
 * Reads tags for .mp3 (ID3v2/ID3v1), .flac (Vorbis comments, STREAMINFO, PICTURE) and .wav
 * (LIST/INFO).  For the last two the duration is always filled in.  Original MP3 notes:
 *
 * Reads ID3v2 (v2.2/2.3/2.4) and ID3v1 tags.
 *  art / art_size : pass non-NULL to also extract embedded JPEG/PNG artwork.
 *                   On success *art is malloc'd (caller frees or hands it to
 *                   art_queue) or NULL when there is no artwork.
 *  want_duration  : also probe the first MP3 frame (Xing/VBRI/CBR estimate).
 */
bool meta_read(const char *path, TrackMeta *m, u8 **art, size_t *art_size, bool want_duration);
