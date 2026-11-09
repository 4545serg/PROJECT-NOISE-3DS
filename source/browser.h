#pragma once
#include <stdbool.h>

typedef struct {
    char name[256];
    bool is_dir;
    bool meta_loaded;      /* lazily filled for visible rows */
    char title[96];
    char artist[64];
} Entry;

typedef struct {
    char   dir[512];       /* always ends with '/' */
    Entry *e;
    int    n, cap;
} DirList;

/* Lists sub-folders and .mp3 files of `path` (folders first, then A-Z).
 * Adds a ".." entry unless `path` is the music root. */
bool dir_load(DirList *d, const char *path);
void dir_free(DirList *d);
/* Copies only the files (no folders) of src into dst, including the dir. */
void dir_copy_files(DirList *dst, const DirList *src);
bool is_music_root(const char *dir);
