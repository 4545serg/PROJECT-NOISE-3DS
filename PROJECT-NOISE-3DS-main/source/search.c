#include "search.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "noise.h"

#define IDX_MAX     30000      /* songs remembered                              */
#define SCAN_DEPTH  12         /* sub-folder levels walked                      */
#define NAME_MAX_REL 255       /* Entry.name holds 256 bytes                    */
#define MAX_TOKENS  6

static char **g_idx = NULL;
static int    g_n = 0, g_cap = 0;
static bool   g_built = false;
static char   g_path[700];     /* shared path buffer for the recursive walk     */
static size_t g_root_len = 0;

static bool idx_add(const char *rel)
{
    if (g_n >= IDX_MAX) return false;
    if (g_n >= g_cap) {
        int ncap = g_cap ? g_cap * 2 : 512;
        char **ni = realloc(g_idx, (size_t)ncap * sizeof *ni);
        if (!ni) return false;
        g_idx = ni;
        g_cap = ncap;
    }
    char *c = strdup(rel);
    if (!c) return false;
    g_idx[g_n++] = c;
    return true;
}

static void scan_dir(int depth)
{
    size_t base = strlen(g_path);                 /* g_path always ends with '/' here */
    DIR *dp = opendir(g_path);
    if (!dp) return;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (de->d_name[0] == '.') continue;       /* hidden, ".", ".." */
        size_t nl = strlen(de->d_name);
        if (base + nl + 2 >= sizeof g_path) continue;

        if (de->d_type == DT_DIR) {
            if (depth + 1 >= SCAN_DEPTH) continue;
            memcpy(g_path + base, de->d_name, nl);
            g_path[base + nl] = '/';
            g_path[base + nl + 1] = 0;
            scan_dir(depth + 1);
            g_path[base] = 0;
        } else if (is_audio_file(de->d_name)) {
            memcpy(g_path + base, de->d_name, nl + 1);
            if (strlen(g_path + g_root_len) <= NAME_MAX_REL) idx_add(g_path + g_root_len);
            g_path[base] = 0;
        }
        if (g_n >= IDX_MAX) break;
    }
    closedir(dp);
}

bool search_build_index(void)
{
    if (g_built) return true;
    snprintf(g_path, sizeof g_path, "%s", MUSIC_ROOT);       /* "sdmc:/Music/" */
    g_root_len = strlen(g_path);
    g_n = 0;
    scan_dir(0);
    g_built = true;
    return true;
}

bool search_index_ready(void) { return g_built; }

void search_free(void)
{
    for (int i = 0; i < g_n; i++) free(g_idx[i]);
    free(g_idx);
    g_idx = NULL;
    g_n = g_cap = 0;
    g_built = false;
}

/* ---- matching ---- */
static const char *base_of(const char *rel)
{
    const char *sl = strrchr(rel, '/');
    return sl ? sl + 1 : rel;
}

static bool ci_contains(const char *hay, const char *needle)   /* needle already lower-case */
{
    size_t nl = strlen(needle);
    if (nl == 0) return true;
    for (; *hay; hay++) {
        size_t k = 0;
        while (k < nl && hay[k] && (char)tolower((unsigned char)hay[k]) == needle[k]) k++;
        if (k == nl) return true;
    }
    return false;
}

static bool has_all(const char *text, char tok[][64], int nt)
{
    for (int i = 0; i < nt; i++)
        if (!ci_contains(text, tok[i])) return false;
    return true;
}

static int cmp_result(const void *a, const void *b)
{
    const Entry *x = a, *y = b;
    int c = strcasecmp(base_of(x->name), base_of(y->name));
    return c ? c : strcasecmp(x->name, y->name);
}

int search_run(DirList *out, const char *query)
{
    dir_free(out);
    snprintf(out->dir, sizeof out->dir, "%s", MUSIC_ROOT);
    dir_add(out, "..", true);                      /* row 0: leave the results */

    char tok[MAX_TOKENS][64];
    int nt = 0;
    const char *p = query;
    while (*p && nt < MAX_TOKENS) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        int k = 0;
        while (*p && *p != ' ' && *p != '\t') {
            if (k < 63) tok[nt][k++] = (char)tolower((unsigned char)*p);
            p++;
        }
        tok[nt][k] = 0;
        nt++;
    }
    if (nt == 0) return 0;

    /* two passes so songs whose own file name matches come before ones that only match through
     * their folder names; each group is sorted A-Z by file name */
    int first = out->n, split = first;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < g_n; i++) {
            bool in_name = has_all(base_of(g_idx[i]), tok, nt);
            if (pass == 0 ? !in_name : (in_name || !has_all(g_idx[i], tok, nt))) continue;
            if (!dir_add(out, g_idx[i], false)) goto done;
        }
        if (pass == 0) split = out->n;
    }
done:
    if (split > out->n) split = out->n;
    if (split - first > 1) qsort(out->e + first, (size_t)(split - first), sizeof(Entry), cmp_result);
    if (out->n - split > 1) qsort(out->e + split, (size_t)(out->n - split), sizeof(Entry), cmp_result);
    return out->n - first;
}
