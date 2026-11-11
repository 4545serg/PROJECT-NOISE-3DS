#pragma once
/*
 * Build configuration shared by every file that includes minimp3 / stb_image / dr_flac.
 *
 * The three libraries are single-header: exactly one .c file (mp3impl.c, stbimpl.c, flacimpl.c)
 * defines the *_IMPLEMENTATION macro, and every other file only sees the declarations. All of
 * them must see the SAME configuration macros, so they live here and nowhere else.
 */

/* minimp3: MP3 (layer 3) only, scalar code path (no SIMD on the 3DS's ARM11) */
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD

/* stb_image: JPEG + PNG album art, decoded from memory only */
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR

/* dr_flac: native FLAC only, no Ogg container, no CRC checks / SIMD to keep decoding cheap */
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_CRC
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_WCHAR
