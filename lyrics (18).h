#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Lyrics: SD-card cache + background online lookup.
 *
 * Lookup order for a song:  <song>.lrc next to the file  ->  SD:/3ds/PROJECT_NOISE/lyrics cache  ->  online
 * (LRCLIB, then the optional external service from lyrics.cfg, then lyrics.ovh).  Everything online runs on a
 * worker thread, so the UI and the audio engine are never blocked; results are adopted by lyrics_pump(). */

typedef enum {
    LY_IDLE,        /* nothing cached and no search has been made (automatic search is off) */
    LY_LOADING,     /* worker is looking */
    LY_READY,       /* lyrics available: lyrics_count() > 0 */
    LY_NOTFOUND,    /* providers answered, nobody has this song */
    LY_OFFLINE,     /* offline-only mode and nothing cached */
    LY_NONET,       /* no Wi-Fi connection */
    LY_ERROR        /* providers could not be reached (TLS / timeout / server error) */
} LyState;

typedef struct {
    bool auto_search;     /* look online as soon as the lyrics screen opens */
    bool auto_download;   /* keep what was found in the SD cache */
    bool offline_only;    /* never touch the network */
    bool prefer_synced;   /* synchronized (LRC) over plain text */
} LyOptions;

typedef struct {
    char     path[800];
    char     title[128];
    char     artist[128];
    unsigned duration_ms;
} LyTrack;

void lyrics_init(void);
void lyrics_exit(void);

/* Fill t from the file's tags (as read by meta.c); falls back to "Artist - Title" in the file name. */
void lyrics_identify(LyTrack *t, const char *path, const char *tag_title, const char *tag_artist, unsigned duration_ms);

/* Cache only, synchronous and cheap (a few small file reads): used while the app is loading. */
bool lyrics_preload(const LyTrack *t, const LyOptions *o);

/* (Re)start the lookup for t. Cached lyrics are picked up first; the network is used only when allowed.
 * force_online: the user asked for a search, so ignore "automatic search off" and earlier misses. */
void lyrics_request(const LyTrack *t, const LyOptions *o, bool force_online);

void        lyrics_pump(void);                 /* once per frame (main thread) */
LyState     lyrics_state(void);
const char *lyrics_key(void);                  /* path of the song the current state belongs to */
bool        lyrics_synced(void);
const char *lyrics_source(void);               /* "cache", "file", "LRCLIB", ... */
int         lyrics_count(void);
const char *lyrics_text(int i);
int         lyrics_time(int i);                /* ms, synced lyrics only */
int         lyrics_index_at(unsigned pos_ms);  /* last line whose time <= pos, -1 before the first */

int         lyrics_clear_cache(void);          /* deletes cached lyrics; returns number of files removed */
