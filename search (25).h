#pragma once
#include <stdbool.h>
#include "browser.h"

/* Song search over everything under SD:/Music/ (all sub-folders).
 *
 * The first search walks the Music folder once and remembers the relative paths of every
 * .mp3 / .flac / .wav file (kept in memory until the app exits, because the SD card cannot
 * change while the app runs).  Each search is then an in-memory filter, so it is instant.
 *
 * Matching is case-insensitive; every word of the query must appear somewhere in the file's
 * path (folder names included, so "daft punk" also finds Daft Punk/Discovery/01 One More Time.mp3).
 *
 * search_run() fills `out` with a ".." entry followed by the matches.  The matches' names are
 * paths RELATIVE to the Music folder and out->dir is the Music folder, so dir + name is always
 * the full file path, exactly like a normal folder listing. */
bool search_build_index(void);          /* idempotent; walks SD:/Music/ the first time */
bool search_index_ready(void);
int  search_run(DirList *out, const char *query);   /* number of matches (not counting "..") */
void search_free(void);
