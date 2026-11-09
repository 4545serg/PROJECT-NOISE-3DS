#pragma once
#include <3ds.h>
#include <stdbool.h>

/* MP3 playback engine: minimp3 decoding on a worker thread -> ndsp channel 0. */

bool audio_init(void);                 /* false => DSP firmware missing, etc. */
void audio_exit(void);

void audio_play(const char *path, u32 audio_start);   /* async: starts a new track */
void audio_seek(const char *path, u32 audio_start, float frac, u32 start_ms);   /* frac 0..1 */
void audio_pause(void);
void audio_resume(void);
void audio_stop(void);

bool audio_is_active(void);            /* a track is loaded (playing or paused) */
bool audio_is_paused(void);
u32  audio_position_ms(void);
void audio_set_volume(int percent);    /* 0..100 */

bool audio_take_finished(void);        /* true once when a track ended naturally */
bool audio_take_error(void);           /* true once when a file could not be opened */
