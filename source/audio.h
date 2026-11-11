#pragma once
#include <3ds.h>
#include <stdbool.h>

/* Playback engine (MP3 via minimp3, FLAC via dr_flac, WAV built in): decoding on a worker
 * thread -> ndsp channel 0. */

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
void audio_set_eq(const int gains_db[5]);   /* 5-band equalizer: 60/230/910/3.6k/14k Hz, -12..+12 dB */

bool audio_take_finished(void);        /* true once when a track ended naturally */
bool audio_take_error(void);           /* true once when a file could not be opened */

/* Live analysis of what is being played right now (for the visualizer). bands[] are log-spaced from bass
 * (index 0) to treble, wave[] is a short mono snapshot, both normalised to 0..1 / -1..1. Returns false
 * when nothing is playing. Cheap: the audio thread analyses one decoded chunk at a time. */
#define AUDIO_VIS_BANDS 12
#define AUDIO_VIS_WAVE  48
bool audio_get_vis(float bands[AUDIO_VIS_BANDS], float wave[AUDIO_VIS_WAVE]);
