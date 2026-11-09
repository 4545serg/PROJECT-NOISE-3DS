#include "browser.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "slither.h"

#define MAX_ENTRIES 2000

bool is_music_root(const char *dir) { return strcmp(dir, MUSIC_ROOT) == 0; }

static bool has_mp3_ext(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".mp3") == 0;
}

static bool add_entry(DirList *d, const char *name, bool is_dir)
{
    if (d->n >= MAX_ENTRIES) return false;
    if (d->n >= d->cap) {
        int ncap = d->cap ? d->cap * 2 : 128;
        Entry *ne = realloc(d->e, (size_t)ncap * sizeof(Entry));
        if (!ne) return false;
        d->e = ne;
        d->cap = ncap;
    }
    Entry *e = &d->e[d->n++];
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", name);
    e->is_dir = is_dir;
    return true;
}

static int cmp_entry(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    bool xu = !strcmp(x->name, ".."), yu = !strcmp(y->name, "..");
    if (xu != yu) return xu ? -1 : 1;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

void dir_free(DirList *d)
{
    free(d->e);
    d->e = NULL;
    d->n = d->cap = 0;
}

bool dir_load(DirList *d, const char *path)
{
    char p[512];
    snprintf(p, sizeof p, "%s", path);
    size_t len = strlen(p);
    if (len == 0 || p[len - 1] != '/') {
        if (len + 1 < sizeof p) { p[len] = '/'; p[len + 1] = 0; }
    }

    dir_free(d);
    snprintf(d->dir, sizeof d->dir, "%s", p);

    DIR *dp = opendir(p);
    if (!dp) return false;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.') continue;            /* hidden, ".", ".." */
        if (de->d_type == DT_DIR) add_entry(d, de->d_name, true);
        else if (has_mp3_ext(de->d_name)) add_entry(d, de->d_name, false);
    }
    closedir(dp);

    if (!is_music_root(d->dir)) add_entry(d, "..", true);
    if (d->n > 1) qsort(d->e, (size_t)d->n, sizeof(Entry), cmp_entry);
    return true;
}

void dir_copy_files(DirList *dst, const DirList *src)
{
    dir_free(dst);
    snprintf(dst->dir, sizeof dst->dir, "%s", src->dir);
    for (int i = 0; i < src->n; i++) {
        if (!src->e[i].is_dir) add_entry(dst, src->e[i].name, false);
    }
}
