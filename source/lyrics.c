#include "lyrics.h"

#include <3ds.h>
#include <malloc.h>
#include <curl/curl.h>
#include <ctype.h>
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "noise.h"

#define LY_DIR        DATA_DIR "/lyrics"
#define LY_CFG        DATA_DIR "/lyrics.cfg"      /* optional: proxy=<url>  token=<secret>  insecure_tls=1 */
#define LY_MAX_LINES  700
#define LY_MAX_BODY   (192 * 1024)
#define LY_MAX_FILE   (96 * 1024)
#define LY_NF_DAYS    7
#define HTTP_TIMEOUT_NS (7000000000ULL)
#define HTTP_TOTAL_MS   18000
#define USER_AGENT    "PROJECT-NOISE-3DS/1.2 (https://github.com/)"

/* ------------------------------------------------------------------ data */
typedef struct {
    char *text;            /* all lines, NUL separated */
    u32  *off;             /* start of each line inside text */
    int  *ms;              /* timestamp of each line (synced only) */
    int   n;
    bool  synced;
    char  src[12];
} LyData;

static void data_free(LyData *d)
{
    free(d->text); free(d->off); free(d->ms);
    memset(d, 0, sizeof *d);
}

/* main-thread view */
static LyData  g_cur;
static LyState g_state = LY_IDLE;
static char    g_key[800];

/* worker */
static LightLock     g_lk;
static Thread        g_thread;
static volatile bool g_quit;
static volatile int  g_gen;
static bool          g_job_valid;
static LyTrack       g_job;
static LyOptions     g_job_opt;
static bool          g_job_force;
static int           g_job_gen;
static bool          g_mb_full;
static int           g_mb_gen;
static LyState       g_mb_state;
static LyData        g_mb_data;
static bool          g_ac_ok, g_http_ok, g_curl_ok, g_soc_ok, g_inited;
static u32           *g_soc_buf;
#define SOC_ALIGN      0x1000
#define SOC_BUFSIZE    0x100000

/* optional settings from lyrics.cfg (never compiled in) */
static char g_proxy[300], g_token[200];
static bool g_insecure;

/* ------------------------------------------------------------ text utils */
static void norm(const char *in, char *out, size_t cap)    /* lower-case, brackets / punctuation removed */
{
    size_t n = 0;
    int depth = 0;
    bool sp = true;
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 1 < cap; p++) {
        if (*p == '(' || *p == '[') { depth++; continue; }
        if (*p == ')' || *p == ']') { if (depth) depth--; continue; }
        if (depth) continue;
        if (*p >= 0x80) { out[n++] = (char)*p; sp = false; }
        else if (isalnum(*p)) { out[n++] = (char)tolower(*p); sp = false; }
        else if (!sp) { out[n++] = ' '; sp = true; }
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
    static const char *const feat[] = { " feat ", " ft ", " featuring ", " feat", " ft" };
    for (size_t i = 0; i < sizeof feat / sizeof feat[0]; i++) {
        char *f = strstr(out, feat[i]);
        if (f && (f[strlen(feat[i])] == 0 || feat[i][strlen(feat[i]) - 1] == ' ')) { *f = 0; break; }
    }
}

static void clean_title(const char *in, char *out, size_t cap)   /* keeps case, drops (...) / [...] */
{
    size_t n = 0;
    int depth = 0;
    for (const char *p = in; *p && n + 1 < cap; p++) {
        if (*p == '(' || *p == '[') { depth++; continue; }
        if (*p == ')' || *p == ']') { if (depth) depth--; continue; }
        if (!depth) out[n++] = *p;
    }
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
    if (!out[0]) snprintf(out, cap, "%s", in);
}

static bool has_word(const char *hay, const char *w)        /* hay is lower-case */
{
    size_t wl = strlen(w);
    for (const char *p = hay; (p = strstr(p, w)) != NULL; p++) {
        bool l = (p == hay) || !isalnum((unsigned char)p[-1]);
        bool r = !isalnum((unsigned char)p[wl]);
        if (l && r) return true;
    }
    return false;
}

static const char *const VARIANTS[] = { "remix", "cover", "live", "karaoke", "acoustic", "instrumental", "demo",
                                         "sped", "slowed", "reverb", "nightcore", "tribute", "unplugged", "acapella" };
#define N_VARIANTS ((int)(sizeof VARIANTS / sizeof VARIANTS[0]))

static void lower_copy(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    for (; in[n] && n + 1 < cap; n++) out[n] = (char)tolower((unsigned char)in[n]);
    out[n] = 0;
}

static u32 fnv(const char *s, u32 h)
{
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 16777619u; }
    return h;
}

static void trim(char *s)
{
    size_t n = strlen(s), a = 0;
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    while (s[a] && isspace((unsigned char)s[a])) a++;
    if (a) memmove(s, s + a, strlen(s + a) + 1);
}

static void url_enc(const char *in, char *out, size_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 4 < cap; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') out[n++] = (char)*p;
        else { out[n++] = '%'; out[n++] = hex[*p >> 4]; out[n++] = hex[*p & 15]; }
    }
    out[n] = 0;
}

/* ------------------------------------------------------------- identify */
void lyrics_identify(LyTrack *t, const char *path, const char *tag_title, const char *tag_artist, unsigned duration_ms)
{
    memset(t, 0, sizeof *t);
    snprintf(t->path, sizeof t->path, "%s", path);
    t->duration_ms = duration_ms;
    snprintf(t->title, sizeof t->title, "%s", tag_title ? tag_title : "");
    snprintf(t->artist, sizeof t->artist, "%s", tag_artist ? tag_artist : "");

    char base[256];
    const char *b = strrchr(path, '/');
    snprintf(base, sizeof base, "%s", b ? b + 1 : path);
    char *dot = strrchr(base, '.');
    if (dot) *dot = 0;

    /* A real title tag is authoritative, but an empty artist tag should still be
     * recoverable from a useful filename such as "Artist - Title.mp3".  This
     * materially improves online lookup when files have incomplete metadata. */
    if (t->title[0] && strcmp(t->title, base) != 0) {
        if (!t->artist[0]) {
            char fn[256];
            snprintf(fn, sizeof fn, "%s", base);
            char *sep = strstr(fn, " - ");
            if (sep) {
                *sep = 0;
                char *ft = sep + 3;
                trim(fn); trim(ft);
                if (fn[0] && ft[0] && !strcasecmp(ft, t->title))
                    snprintf(t->artist, sizeof t->artist, "%s", fn);
            }
        }
        return;
    }

    /* file-name fallback: "01 - Artist - Title", "Artist - Title" or just "Title" */
    char *p = base;
    int digits = 0;
    while (isdigit((unsigned char)p[digits]) && digits < 3) digits++;
    if (digits && (p[digits] == '.' || p[digits] == ' ' || p[digits] == '-' || p[digits] == '_' || p[digits] == ')')) {
        p += digits;
        while (*p == '.' || *p == ' ' || *p == '-' || *p == '_' || *p == ')') p++;
    }
    char *sep = strstr(p, " - ");
    if (sep && !t->artist[0]) {
        *sep = 0;
        snprintf(t->artist, sizeof t->artist, "%s", p);
        snprintf(t->title, sizeof t->title, "%s", sep + 3);
    } else {
        snprintf(t->title, sizeof t->title, "%s", p);
    }
    trim(t->artist);
    trim(t->title);
}

/* ---------------------------------------------------------------- cache */
static void cache_name(const LyTrack *t, const char *ext, char *out, size_t cap)
{
    char a[160], ti[160], k[330];
    norm(t->artist, a, sizeof a);
    norm(t->title, ti, sizeof ti);
    snprintf(k, sizeof k, "%s|%s", a, ti);
    snprintf(out, cap, LY_DIR "/%08x%08x.%s", (unsigned)fnv(k, 2166136261u), (unsigned)fnv(k, 0x9E3779B9u), ext);
}

static void sidecar_name(const LyTrack *t, const char *ext, char *out, size_t cap)
{
    snprintf(out, cap, "%s", t->path);
    char *dot = strrchr(out, '.');
    if (dot && strlen(dot) <= 5) {
        snprintf(dot, cap - (size_t)(dot - out), ".%s", ext);
    } else {
        size_t n = strlen(out);
        if (n + strlen(ext) + 2 < cap) snprintf(out + n, cap - n, ".%s", ext);
    }
}

static void ensure_dirs(void)
{
    mkdir(DATA_ROOT, 0777);
    mkdir(DATA_DIR, 0777);
    mkdir(LY_DIR, 0777);
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > LY_MAX_FILE) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[n] = 0;
    if (len) *len = n;
    return buf;
}

static void write_file(const char *path, const char *data)
{
    ensure_dirs();
    FILE *f = fopen(path, "wb");
    if (!f) return;
    size_t n = strlen(data);
    if (fwrite(data, 1, n, f) != n) { fclose(f); remove(path); return; }
    fclose(f);
}

/* ---------------------------------------------------------------- parse */
typedef struct { int ms; u32 off; } Row;

static bool parse_stamp(const char **pp, int *ms)           /* [mm:ss], [mm:ss.x..], [mm:ss:xx]  -> true, *pp after ']' */
{
    const char *p = *pp;
    if (*p != '[') return false;
    p++;
    if (!isdigit((unsigned char)*p)) return false;
    int mm = 0;
    while (isdigit((unsigned char)*p)) { mm = mm * 10 + (*p - '0'); p++; if (mm > 9999) return false; }
    if (*p != ':') return false;
    p++;
    if (!isdigit((unsigned char)*p)) return false;
    int ss = 0;
    while (isdigit((unsigned char)*p)) { ss = ss * 10 + (*p - '0'); p++; if (ss > 999) return false; }
    int frac = 0;
    if (*p == '.' || *p == ':') {
        p++;
        int digits = 0;
        while (isdigit((unsigned char)*p)) {
            if (digits < 3) { frac = frac * 10 + (*p - '0'); digits++; }
            p++;
        }
        while (digits < 3) { frac *= 10; digits++; }
    }
    if (*p != ']') return false;
    *pp = p + 1;
    *ms = (mm * 60 + ss) * 1000 + frac;
    return true;
}

static void strip_inline_tags(char *s)                     /* enhanced-LRC word stamps  <00:12.00> */
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '<' && isdigit((unsigned char)r[1])) {
            char *e = strchr(r, '>');
            if (e) { r = e; continue; }
        }
        *w++ = *r;
    }
    *w = 0;
}

static int row_cmp(const void *a, const void *b)
{
    const Row *x = a, *y = b;
    if (x->ms != y->ms) return x->ms < y->ms ? -1 : 1;
    return x->off < y->off ? -1 : (x->off > y->off);
}

/* raw = LRC or plain text. Detects timestamps itself, so a "plain" field that is really LRC still syncs. */
static bool parse_lyrics(const char *raw, LyData *d)
{
    memset(d, 0, sizeof *d);
    size_t rl = strlen(raw);
    char *work = malloc(rl + 1);
    char *text = malloc(rl + LY_MAX_LINES * 4 + 16);
    Row *rows = malloc(sizeof(Row) * LY_MAX_LINES);
    if (!work || !text || !rows) { free(work); free(text); free(rows); return false; }

    /* pass 1: does it look synchronized? */
    int stamped = 0, total = 0;
    for (const char *p = raw; *p; ) {
        const char *e = strchr(p, '\n');
        size_t ll = e ? (size_t)(e - p) : strlen(p);
        int tmp; const char *q = p;
        if (ll && parse_stamp(&q, &tmp)) stamped++;
        total++;
        p = e ? e + 1 : p + ll;
    }
    const bool synced = stamped >= 3 && stamped * 4 >= total;

    int n = 0, offset_ms = 0;
    size_t tl = 0;
    int blank_run = 0;
    memcpy(work, raw, rl + 1);
    char *nl;
    for (char *line = work; line; line = nl ? nl + 1 : NULL) {
        nl = strchr(line, '\n');
        if (nl) *nl = 0;
        size_t ll = strlen(line);
        while (ll && (line[ll - 1] == '\r' || isspace((unsigned char)line[ll - 1]))) line[--ll] = 0;

        int stamps[16], ns = 0, ms = 0;
        const char *p = line;
        while (*p == '[') {
            if (ns < 16 && parse_stamp(&p, &ms)) { stamps[ns++] = ms; continue; }
            if (!strncasecmp(p, "[offset:", 8)) offset_ms = atoi(p + 8);
            const char *e = strchr(p, ']');
            if (!e) break;
            p = e + 1;                                   /* other tag ([ar:..], [ti:..], [Chorus]) */
        }
        while (*p == ' ') p++;
        char txt[512];
        snprintf(txt, sizeof txt, "%s", p);
        strip_inline_tags(txt);
        trim(txt);

        if (synced) {
            if (!ns) continue;                           /* metadata / junk line */
            if (!txt[0]) snprintf(txt, sizeof txt, "...");
            for (int k = 0; k < ns && n < LY_MAX_LINES; k++) {
                rows[n].ms = stamps[k];
                rows[n].off = (u32)tl;
                n++;
                if (k == 0) { size_t l = strlen(txt) + 1; memcpy(text + tl, txt, l); tl += l; }
                else rows[n - 1].off = rows[n - 2].off;      /* repeated stamp: share the first copy */
            }
        } else {
            if (!txt[0]) { if (!n || ++blank_run > 1) continue; }
            else blank_run = 0;
            if (n >= LY_MAX_LINES) break;
            rows[n].ms = 0;
            rows[n].off = (u32)tl;
            n++;
            size_t l = strlen(txt) + 1;
            memcpy(text + tl, txt, l);
            tl += l;
        }
    }
    if (!synced) {
        while (n > 0 && text[rows[n - 1].off] == 0) n--;              /* trailing blank */
    }
    free(work);

    if (n == 0) { free(text); free(rows); return false; }
    if (synced) {
        for (int i = 0; i < n; i++) { rows[i].ms -= offset_ms; if (rows[i].ms < 0) rows[i].ms = 0; }
        qsort(rows, (size_t)n, sizeof(Row), row_cmp);
    }

    d->text = text;
    d->off = malloc(sizeof(u32) * (size_t)n);
    d->ms = malloc(sizeof(int) * (size_t)n);
    if (!d->off || !d->ms) { data_free(d); free(rows); return false; }
    for (int i = 0; i < n; i++) { d->off[i] = rows[i].off; d->ms[i] = rows[i].ms; }
    d->n = n;
    d->synced = synced;
    free(rows);
    return true;
}

/* --------------------------------------------------------- cache lookup */
static bool load_into(const char *path, const char *src, LyData *d)
{
    char *raw = read_file(path, NULL);
    if (!raw) return false;
    bool ok = parse_lyrics(raw, d);
    free(raw);
    if (ok) snprintf(d->src, sizeof d->src, "%s", src);
    return ok;
}

static bool cache_lookup(const LyTrack *t, const LyOptions *o, LyData *d)
{
    char side[820], lrc[96], txt[96];
    sidecar_name(t, "lrc", side, sizeof side);
    cache_name(t, "lrc", lrc, sizeof lrc);
    cache_name(t, "txt", txt, sizeof txt);

    if (o->prefer_synced)
        return load_into(side, "file", d) || load_into(lrc, "cache", d) || load_into(txt, "cache", d);
    return load_into(txt, "cache", d) || load_into(side, "file", d) || load_into(lrc, "cache", d);
}

static bool nf_fresh(const LyTrack *t)
{
    char p[96];
    cache_name(t, "nf", p, sizeof p);
    struct stat st;
    if (stat(p, &st) != 0) return false;
    time_t now = time(NULL);
    double age = difftime(now, st.st_mtime);
    return age >= 0 && age < LY_NF_DAYS * 86400.0;
}

/* ----------------------------------------------------------------- JSON */
static const char *jws(const char *p, const char *e) { while (p < e && isspace((unsigned char)*p)) p++; return p; }

static const char *jskip(const char *p, const char *e)      /* after the value starting at p (or e when truncated) */
{
    if (p >= e) return e;
    if (*p == '"') {
        for (p++; p < e; p++) {
            if (*p == '\\') p++;
            else if (*p == '"') return p + 1;
        }
        return e;
    }
    if (*p == '{' || *p == '[') {
        int depth = 0;
        for (; p < e; p++) {
            if (*p == '"') { p = jskip(p, e) - 1; continue; }
            if (*p == '{' || *p == '[') depth++;
            else if (*p == '}' || *p == ']') { if (--depth == 0) return p + 1; }
        }
        return e;
    }
    while (p < e && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p)) p++;
    return p;
}

static const char *jfind(const char *obj, const char *e, const char *key)   /* value start of key in the {object} at obj */
{
    size_t kl = strlen(key);
    const char *p = obj + 1;
    for (;;) {
        p = jws(p, e);
        if (p >= e || *p != '"') return NULL;
        const char *ke = jskip(p, e);
        bool match = (size_t)(ke - p) == kl + 2 && !strncmp(p + 1, key, kl);
        p = jws(ke, e);
        if (p >= e || *p != ':') return NULL;
        p = jws(p + 1, e);
        if (match) return p;
        p = jws(jskip(p, e), e);
        if (p < e && *p == ',') p++;
    }
}

static char *jstr_alloc(const char *v, const char *e)        /* decoded copy of the string at v, NULL if not a string */
{
    if (!v || v >= e || *v != '"') return NULL;
    const char *end = jskip(v, e);
    if (end <= v + 1 || end[-1] != '"') return NULL;
    char *out = malloc((size_t)(end - v) + 4);
    if (!out) return NULL;
    size_t n = 0;
    for (const char *p = v + 1; p < end - 1; p++) {
        if (*p != '\\') { out[n++] = *p; continue; }
        p++;
        switch (*p) {
        case 'n': out[n++] = '\n'; break;
        case 'r': break;
        case 't': out[n++] = ' '; break;
        case 'b': case 'f': break;
        case 'u': {
            unsigned cp = 0;
            if (p + 4 < end - 1) {
                for (int i = 1; i <= 4; i++) { cp <<= 4; char c = p[i]; cp |= (unsigned)(isdigit((unsigned char)c) ? c - '0' : (tolower((unsigned char)c) - 'a' + 10) & 15); }
                p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && p + 6 < end - 1 && p[1] == '\\' && p[2] == 'u') {            /* surrogate pair */
                    unsigned lo = 0;
                    for (int i = 3; i <= 6; i++) { lo <<= 4; char c = p[i]; lo |= (unsigned)(isdigit((unsigned char)c) ? c - '0' : (tolower((unsigned char)c) - 'a' + 10) & 15); }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    p += 6;
                }
            }
            if (cp < 0x80) out[n++] = (char)cp;
            else if (cp < 0x800) { out[n++] = (char)(0xC0 | (cp >> 6)); out[n++] = (char)(0x80 | (cp & 63)); }
            else if (cp < 0x10000) { out[n++] = (char)(0xE0 | (cp >> 12)); out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63)); }
            else { out[n++] = (char)(0xF0 | (cp >> 18)); out[n++] = (char)(0x80 | ((cp >> 12) & 63)); out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63)); }
            break;
        }
        default: out[n++] = *p; break;           /* \" \\ \/ */
        }
    }
    out[n] = 0;
    return out;
}

static bool jstr_small(const char *obj, const char *e, const char *key, char *out, size_t cap)
{
    out[0] = 0;
    char *s = jstr_alloc(jfind(obj, e, key), e);
    if (!s) return false;
    snprintf(out, cap, "%s", s);
    free(s);
    return true;
}

static bool jnull_or_empty(const char *v, const char *e)
{
    if (!v || v >= e) return true;
    if (*v != '"') return true;                                  /* null / missing */
    return v + 1 < e && v[1] == '"';
}

/* ------------------------------------------------------------- matching */
typedef struct {
    char title[160], artist[160];       /* normalised */
    char clean[160];                    /* readable title without (...) */
    char ours[512];                     /* lower-case title + file name: variants the song itself has */
    int  dur_s;
    bool prefer_synced;
} Query;

static void query_make(Query *q, const LyTrack *t, const LyOptions *o)
{
    memset(q, 0, sizeof *q);
    norm(t->title, q->title, sizeof q->title);
    norm(t->artist, q->artist, sizeof q->artist);
    clean_title(t->title, q->clean, sizeof q->clean);
    const char *b = strrchr(t->path, '/');
    char tmp[400];
    snprintf(tmp, sizeof tmp, "%s %s", t->title, b ? b + 1 : t->path);
    lower_copy(tmp, q->ours, sizeof q->ours);
    q->dur_s = (int)((t->duration_ms + 500) / 1000);
    q->prefer_synced = o->prefer_synced;
}

/* score of one candidate object; < 0 = reject. Missing fields (a proxy that only sends lyrics) are trusted. */
static int score_obj(const Query *q, const char *obj, const char *e, bool trust)
{
    char name[200], art[200], alb[200], low[420], nn[200], na[200];
    bool has_name = jstr_small(obj, e, "trackName", name, sizeof name);
    bool has_art = jstr_small(obj, e, "artistName", art, sizeof art);
    jstr_small(obj, e, "albumName", alb, sizeof alb);

    const char *vi = jfind(obj, e, "instrumental");
    if (vi && !strncmp(vi, "true", 4)) return -1;

    const char *vs = jfind(obj, e, "syncedLyrics"), *vp = jfind(obj, e, "plainLyrics");
    const bool has_sync = !jnull_or_empty(vs, e), has_plain = !jnull_or_empty(vp, e);
    if (!has_sync && !has_plain) return -1;

    int score = 0;
    if (!has_name && trust) score = 60;
    else if (!has_name) return -1;
    else {
        snprintf(low, sizeof low, "%s %s", name, alb);
        char lo[420];
        lower_copy(low, lo, sizeof lo);
        for (int i = 0; i < N_VARIANTS; i++)                    /* no remix / cover / live ... unless the song is one */
            if (has_word(lo, VARIANTS[i]) && !has_word(q->ours, VARIANTS[i])) return -1;

        norm(name, nn, sizeof nn);
        if (!strcmp(nn, q->title)) score += 50;
        else {
            size_t a = strlen(nn), b = strlen(q->title);
            size_t sh = a < b ? a : b, lg = a < b ? b : a;
            if (sh >= 4 && sh * 100 >= lg * 70 && (strstr(nn, q->title) || strstr(q->title, nn))) score += 20;
            else return -1;
        }
        if (q->artist[0]) {
            if (!has_art) return -1;
            norm(art, na, sizeof na);
            if (!strcmp(na, q->artist)) score += 30;
            else if (strstr(na, q->artist) || strstr(q->artist, na)) score += 15;
            else return -1;
        }
        double cd = 0;
        const char *vd = jfind(obj, e, "duration");
        if (vd) cd = atof(vd);
        if (q->dur_s > 0 && cd > 0) {
            int diff = (int)(cd + 0.5) - q->dur_s;
            if (diff < 0) diff = -diff;
            if (diff <= 2) score += 20;
            else if (diff <= 4) score += 10;
            else if (diff > 8) return -1;
        } else if (!q->artist[0]) {
            return -1;                                            /* title alone is too weak without a length */
        }
        if (score < 50) return -1;
    }
    if (q->prefer_synced ? has_sync : has_plain) score += 10;
    return score;
}

typedef struct { char *raw; bool from_sync; char src[12]; } Found;

/* body: one object or an array of them. Picks the best candidate and decodes only its lyrics. */
static bool pick_best(const char *body, size_t len, const Query *q, bool trust, const char *src, Found *out)
{
    const char *e = body + len, *p = jws(body, e);
    const char *best = NULL;
    int best_score = -1;

    if (p < e && *p == '{') {
        const char *end = jskip(p, e);
        if (end > p && end[-1] == '}') { int s = score_obj(q, p, end, trust); if (s >= 0) { best = p; best_score = s; } }
        if (best) { e = end; }
    } else if (p < e && *p == '[') {
        for (p = jws(p + 1, e); p < e && *p == '{'; p = jws(p, e)) {
            const char *end = jskip(p, e);
            if (end <= p || end[-1] != '}') break;                   /* truncated download */
            int s = score_obj(q, p, end, trust);
            if (s > best_score) { best = p; best_score = s; }
            p = jws(end, e);
            if (p < e && *p == ',') p = jws(p + 1, e);
        }
    }
    if (!best) {
        /* some services reply {"lyrics": "..."} only */
        const char *v = (p < e && *p == '{') ? jfind(p, e, "lyrics") : NULL;
        char *s = jstr_alloc(v, e);
        if (!s || !s[0]) { free(s); return false; }
        out->raw = s; out->from_sync = false;
        snprintf(out->src, sizeof out->src, "%s", src);
        return true;
    }
    const char *oe = jskip(best, body + len);
    char *sy = jstr_alloc(jfind(best, oe, "syncedLyrics"), oe);
    char *pl = jstr_alloc(jfind(best, oe, "plainLyrics"), oe);
    if (sy && !sy[0]) { free(sy); sy = NULL; }
    if (pl && !pl[0]) { free(pl); pl = NULL; }
    char *pick;
    if (q->prefer_synced) { pick = sy ? sy : pl; free(sy ? pl : sy); }
    else { pick = pl ? pl : sy; free(pl ? sy : pl); }
    if (!pick) return false;
    out->raw = pick;
    out->from_sync = (pick == sy);
    snprintf(out->src, sizeof out->src, "%s", src);
    return true;
}

/* ----------------------------------------------------------------- HTTP */
/*
 * The 3DS http:C service is limited by the console SSL stack.  Current
 * public lyrics APIs are commonly served with TLS versions/certificates that
 * http:C cannot negotiate reliably.  devkitPro ships a 3DS curl port backed
 * by mbedTLS 2.28.x, which gives homebrew a modern TLS client.  Use it first;
 * keep the native http:C implementation below as a compatibility fallback.
 */
typedef struct {
    char *data;
    size_t len;
    size_t cap;
} CurlBuffer;

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    CurlBuffer *b = (CurlBuffer *)userdata;
    size_t n = size * nmemb;
    if (n > LY_MAX_BODY - b->len) return 0;
    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap ? b->cap : 16384;
        while (nc < b->len + n + 1) {
            if (nc >= LY_MAX_BODY) { nc = LY_MAX_BODY; break; }
            nc *= 2;
        }
        char *nb = realloc(b->data, nc);
        if (!nb) return 0;
        b->data = nb;
        b->cap = nc;
    }
    memcpy(b->data + b->len, ptr, n);
    b->len += n;
    b->data[b->len] = 0;
    return n;
}

static int curl_get(const char *url, const char *token, int gen, char **out, size_t *outlen)
{
    if (!g_curl_ok) return -1;
    *out = NULL;
    *outlen = 0;

    CURL *c = curl_easy_init();
    if (!c) return -1;

    CurlBuffer b = {0};
    struct curl_slist *headers = NULL;
    char auth[240];
    CURLcode rc;
    long code = 0;

    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Cache-Control: no-cache");
    if (token && token[0]) {
        snprintf(auth, sizeof auth, "Authorization: Bearer %s", token);
        headers = curl_slist_append(headers, auth);
    }

    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 6000L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, HTTP_TOTAL_MS);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 0L);
    curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);

    /* Prefer the CA bundle maintained by Luma/Universal-Updater. If it is
     * unavailable or the old bundle cannot validate the current chain, retry
     * once without peer verification. This is a read-only lyrics request; no
     * credentials are sent to the public providers. */
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_CAINFO, "sdmc:/config/ssl/cacert.pem");
    if (g_insecure) {
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    rc = curl_easy_perform(c);
    if (rc == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);

    /* If CA verification is the only thing that failed, retry the same URL
     * with verification disabled. This specifically handles older 3DS CA
     * bundles while still using mbedTLS for modern TLS negotiation. */
    if (rc != CURLE_OK && !g_insecure &&
        (rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CONNECT_ERROR)) {
        free(b.data); b.data = NULL; b.len = b.cap = 0;
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
        rc = curl_easy_perform(c);
        if (rc == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(c);

    if (g_quit || g_gen != gen) { free(b.data); return -2; }
    if (rc != CURLE_OK) { free(b.data); return -1; }
    if (code < 100 || code > 599) { free(b.data); return -1; }
    if (code != 200) { free(b.data); return (int)code; }
    if (!b.data || !b.len) { free(b.data); return -1; }

    *out = b.data;
    *outlen = b.len;
    return 200;
}

/* returns the HTTP status (>0), -1 on a transport / TLS / timeout failure, -2 when cancelled */
static int native_http_get(const char *url, const char *token, int gen, char **out, size_t *outlen)
{
    char cur[1200];
    snprintf(cur, sizeof cur, "%s", url);
    *out = NULL; *outlen = 0;

    /* Fresh 3DS HTTPC contexts have a very small/default trust store. Modern
     * public lyrics services can therefore fail TLS verification even though
     * the console has working Wi-Fi. Try verified TLS first, then retry the
     * same read-only request with verification disabled. This preserves normal
     * TLS whenever the console can verify the server, while making online
     * lyrics usable on consoles whose CA set cannot validate the provider. */
    bool secure_attempted = false;
    bool insecure_attempted = false;

    for (int hop = 0; hop < 4; hop++) {
        if (g_quit || g_gen != gen) return -2;

        for (int tls_try = 0; tls_try < 2; tls_try++) {
            bool insecure = g_insecure || (tls_try == 1);
            if (insecure && insecure_attempted) continue;
            if (!insecure && secure_attempted) continue;
            if (insecure) insecure_attempted = true;
            else secure_attempted = true;

            httpcContext ctx;
            if (R_FAILED(httpcOpenContext(&ctx, HTTPC_METHOD_GET, cur, 1))) {
                if (!insecure) continue;
                return -1;
            }
            if (insecure) httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);

            httpcAddRequestHeaderField(&ctx, "User-Agent", USER_AGENT);
            httpcAddRequestHeaderField(&ctx, "Accept", "application/json");
            if (token && token[0]) {
                char auth[240];
                snprintf(auth, sizeof auth, "Bearer %s", token);
                httpcAddRequestHeaderField(&ctx, "Authorization", auth);
            }

            if (R_FAILED(httpcBeginRequest(&ctx))) {
                httpcCloseContext(&ctx);
                if (!insecure) continue;
                return -1;
            }

            u32 code = 0;
            if (R_FAILED(httpcGetResponseStatusCodeTimeout(&ctx, &code, HTTP_TIMEOUT_NS))) {
                httpcCancelConnection(&ctx);
                httpcCloseContext(&ctx);
                if (g_quit || g_gen != gen) return -2;
                if (!insecure) continue;
                return -1;
            }

            if (code >= 300 && code < 400) {
                char loc[600] = "";
                httpcGetResponseHeader(&ctx, "Location", loc, sizeof loc);
                httpcCloseContext(&ctx);
                if (!strncmp(loc, "http", 4)) {
                    snprintf(cur, sizeof cur, "%s", loc);
                } else if (loc[0] == '/') {
                    const char *s = strstr(cur, "://");
                    const char *slash = s ? strchr(s + 3, '/') : NULL;
                    size_t hl = slash ? (size_t)(slash - cur) : strlen(cur);
                    if (hl + strlen(loc) + 1 >= sizeof cur) return -1;
                    snprintf(cur + hl, sizeof cur - hl, "%s", loc);
                } else {
                    return -1;
                }
                /* Retry the redirected URL with the same TLS mode first. */
                if (insecure) {
                    insecure_attempted = false;
                } else {
                    secure_attempted = false;
                }
                continue;
            }

            if (code != 200) {
                httpcCloseContext(&ctx);
                return (int)code;
            }

            size_t cap = 16384, n = 0;
            char *buf = malloc(cap + 1);
            if (!buf) {
                httpcCloseContext(&ctx);
                return -1;
            }

            u32 start = 0;
            httpcGetDownloadSizeState(&ctx, &start, NULL);
            const u64 deadline = osGetTime() + HTTP_TOTAL_MS;
            int result = 200;
            for (;;) {
                if (g_quit || g_gen != gen) { result = -2; break; }
                if (osGetTime() > deadline) { result = -1; break; }
                if (n == cap) {
                    if (cap >= LY_MAX_BODY) break;
                    char *nb = realloc(buf, cap * 2 + 1);
                    if (!nb) { result = -1; break; }
                    buf = nb;
                    cap *= 2;
                }
                u32 chunk = (u32)(cap - n > 8192 ? 8192 : cap - n);
                Result rc = httpcReceiveDataTimeout(&ctx, (u8 *)buf + n, chunk, HTTP_TIMEOUT_NS);
                u32 got = 0;
                httpcGetDownloadSizeState(&ctx, &got, NULL);
                n = (size_t)(got - start);
                if (n > cap) n = cap;
                if (rc == 0) break;
                if (rc != (Result)HTTPC_RESULTCODE_DOWNLOADPENDING) {
                    result = -1;
                    break;
                }
            }

            if (result != 200) httpcCancelConnection(&ctx);
            httpcCloseContext(&ctx);
            if (result != 200) {
                free(buf);
                if (!insecure && result == -1) continue;
                return result;
            }

            buf[n] = 0;
            *out = buf;
            *outlen = n;
            return 200;
        }
    }
    return -1;
}

/* Each provider returns: 1 found, 0 answered "not here", -1 unreachable / failed, -2 cancelled */
static int http_get(const char *url, const char *token, int gen, char **out, size_t *outlen)
{
    /* Modern TLS path first. Native http:C remains available for older/local
     * endpoints and for environments where curl is not usable. */
    int rc = curl_get(url, token, gen, out, outlen);
    if (rc == 200 || rc == 404 || rc == 400 || rc == 429 || rc == 403 || rc == 500 || rc == 503 || rc == -2)
        return rc;
    return native_http_get(url, token, gen, out, outlen);
}

static int run_get(const char *url, const char *token, int gen, const Query *q, bool trust, const char *src, Found *f)
{
    char *body; size_t len;
    int rc = http_get(url, token, gen, &body, &len);
    if (rc == -2 || rc == -1) return rc;
    if (rc == 404 || rc == 400) return 0;
    if (rc != 200) return -1;                                   /* 429 / 5xx: treat as unreachable */
    bool ok = pick_best(body, len, q, trust, src, f);
    free(body);
    return ok ? 1 : 0;
}

static int try_lrclib(const LyTrack *t, const Query *q, int gen, Found *f)
{
    char a[400], ti[400], cl[400], raw[400], url[1100];
    url_enc(t->artist, a, sizeof a);
    url_enc(t->title, ti, sizeof ti);
    url_enc(q->clean, cl, sizeof cl);
    url_enc(q->title, raw, sizeof raw);

    int answered = 0, failed = 0;

    /* Exact lookup is the strongest match: artist + title + duration. */
    if (t->artist[0] && t->duration_ms) {
        snprintf(url, sizeof url, "https://lrclib.net/api/get?artist_name=%s&track_name=%s&duration=%d", a, ti, q->dur_s);
        int r = run_get(url, NULL, gen, q, false, "LRCLIB", f);
        if (r == 1 || r == -2) return r;
        if (r == 0) answered++; else if (r < 0) failed++;
    }

    /* Search with the cleaned human title. */
    if (t->artist[0]) snprintf(url, sizeof url, "https://lrclib.net/api/search?artist_name=%s&track_name=%s", a, cl);
    else snprintf(url, sizeof url, "https://lrclib.net/api/search?track_name=%s", cl);
    int r = run_get(url, NULL, gen, q, false, "LRCLIB", f);
    if (r == 1 || r == -2) return r;
    if (r == 0) answered++; else if (r < 0) failed++;

    /* Some tagged files contain punctuation/parentheses that the search endpoint
     * handles differently. Try the normalized title as a final LRCLIB query. */
    if (strcmp(raw, cl) != 0) {
        if (t->artist[0]) snprintf(url, sizeof url, "https://lrclib.net/api/search?artist_name=%s&track_name=%s", a, raw);
        else snprintf(url, sizeof url, "https://lrclib.net/api/search?track_name=%s", raw);
        r = run_get(url, NULL, gen, q, false, "LRCLIB", f);
        if (r == 1 || r == -2) return r;
        if (r == 0) answered++; else if (r < 0) failed++;
    }

    /* LRCLIB's q search is intentionally broad and can recover tracks whose
     * artist/title fields contain unusual punctuation, featuring text, or
     * metadata that does not match the structured search exactly.  We still
     * run the normal candidate scorer before accepting anything. */
    char qterm[700];
    snprintf(qterm, sizeof qterm, "%s %s", t->artist, q->clean);
    char qenc[800];
    url_enc(qterm, qenc, sizeof qenc);
    snprintf(url, sizeof url, "https://lrclib.net/api/search?q=%s", qenc);
    r = run_get(url, NULL, gen, q, false, "LRCLIB", f);
    if (r == 1 || r == -2) return r;
    if (r == 0) answered++; else if (r < 0) failed++;

    /* A failed search must not prevent the later providers from being tried. */
    return answered ? 0 : (failed ? -1 : 0);
}

static int try_proxy(const LyTrack *t, const Query *q, int gen, Found *f)
{
    if (!g_proxy[0]) return 0;
    char a[400], ti[400], url[1100];
    url_enc(t->artist, a, sizeof a);
    url_enc(t->title, ti, sizeof ti);
    snprintf(url, sizeof url, "%s%sartist_name=%s&track_name=%s&duration=%d&synced=%d", g_proxy,
             strchr(g_proxy, '?') ? "&" : "?", a, ti, q->dur_s, q->prefer_synced ? 1 : 0);
    return run_get(url, g_token, gen, q, true, "Service", f);
}

static int try_lrcmux(const LyTrack *t, const Query *q, int gen, Found *f)
{
    /* LRCMux exposes an LRCLIB-compatible API, but fans requests across
     * multiple lyric backends. This is a useful independent fallback when
     * lrclib.net itself is slow/rate-limited. The compatibility endpoint
     * deliberately returns the same fields our LRCLIB parser already knows. */
    char a[400], ti[400], url[1100];
    url_enc(t->artist, a, sizeof a);
    url_enc(t->title, ti, sizeof ti);

    snprintf(url, sizeof url,
             "https://api.lrcmux.dev/compat/lrclib/api/get?artist_name=%s&track_name=%s&duration=%d",
             a, ti, q->dur_s);
    int r = run_get(url, NULL, gen, q, false, "LRCMux", f);
    if (r == 1 || r == -2) return r;

    /* Search compatibility endpoint is intentionally the second request;
     * avoid hammering either service with four LRCLIB queries in a row. */
    snprintf(url, sizeof url,
             "https://api.lrcmux.dev/compat/lrclib/api/search?artist_name=%s&track_name=%s",
             a, ti);
    return run_get(url, NULL, gen, q, false, "LRCMux", f);
}

static int try_ovh(const LyTrack *t, const Query *q, int gen, Found *f)
{
    if (!t->artist[0]) return 0;
    char a[400], cl[400], url[900];
    url_enc(t->artist, a, sizeof a);
    url_enc(q->clean, cl, sizeof cl);
    snprintf(url, sizeof url, "https://api.lyrics.ovh/v1/%s/%s", a, cl);
    int r = run_get(url, NULL, gen, q, true, "lyrics.ovh", f);
    if (r == 1 && !strncasecmp(f->raw, "Paroles de la chanson", 21)) {            /* drop that provider's header line */
        char *nl = strchr(f->raw, '\n');
        if (nl) memmove(f->raw, nl + 1, strlen(nl + 1) + 1);
    }
    return r;
}

/* ---------------------------------------------------------------- worker */
static void job_run(const LyTrack *t, const LyOptions *o, bool force, int gen, LyState *st, LyData *d)
{
    memset(d, 0, sizeof *d);
    if (cache_lookup(t, o, d)) { *st = LY_READY; return; }
    if (o->offline_only) { *st = LY_OFFLINE; return; }
    if (!o->auto_search && !force) { *st = LY_IDLE; return; }
    if (!force && nf_fresh(t)) { *st = LY_NOTFOUND; return; }
    if (!g_curl_ok && !g_http_ok) { *st = LY_ERROR; return; }

    u32 wifi = 1;
    if (g_ac_ok && R_SUCCEEDED(ACU_GetWifiStatus(&wifi)) && wifi == 0) { *st = LY_NONET; return; }

    Query q;
    query_make(&q, t, o);
    if (!q.title[0]) { *st = LY_NOTFOUND; return; }

    Found f;
    memset(&f, 0, sizeof f);
    int answered = 0, failed = 0, r;
    int (*const providers[4])(const LyTrack *, const Query *, int, Found *) = { try_lrclib, try_lrcmux, try_proxy, try_ovh };
    bool found = false;
    for (int i = 0; i < 4 && !found; i++) {
        if (i == 2 && !g_proxy[0]) continue;
        if (i > 0) svcSleepThread(350 * 1000 * 1000LL);
        r = providers[i](t, &q, gen, &f);
        if (r == -2) { *st = LY_IDLE; return; }                 /* superseded: result is dropped anyway */
        if (r == 1) found = true; else if (r == 0) answered++; else failed++;
    }
    if (found) {
        if (parse_lyrics(f.raw, d)) {
            snprintf(d->src, sizeof d->src, "%s", f.src);
            if (o->auto_download) {
                char p[96];
                cache_name(t, d->synced ? "lrc" : "txt", p, sizeof p);
                write_file(p, f.raw);

                /* LRCGET's useful convention is to place a same-name .lrc
                 * beside the audio file.  PROJECT NOISE keeps its private
                 * hashed cache too, but also writes the portable sidecar so
                 * another lyrics-aware player can reuse the download. */
                if (d->synced) {
                    char side[820];
                    sidecar_name(t, "lrc", side, sizeof side);
                    write_file(side, f.raw);
                }
            }
            free(f.raw);
            *st = LY_READY;
            return;
        }
        free(f.raw);
        answered++;
    }
    if (answered > 0) {
        *st = LY_NOTFOUND;
        if (o->auto_download && failed == 0) { char p[96]; cache_name(t, "nf", p, sizeof p); write_file(p, "-"); }
    } else {
        *st = LY_ERROR;
    }
}

static void worker(void *arg)
{
    (void)arg;
    while (!g_quit) {
        LyTrack t; LyOptions o; bool force = false, have = false; int gen = 0;
        LightLock_Lock(&g_lk);
        if (g_job_valid) { t = g_job; o = g_job_opt; force = g_job_force; gen = g_job_gen; g_job_valid = false; have = true; }
        LightLock_Unlock(&g_lk);
        if (!have) { svcSleepThread(40 * 1000 * 1000LL); continue; }

        LyState st = LY_IDLE;
        LyData d;
        job_run(&t, &o, force, gen, &st, &d);

        LightLock_Lock(&g_lk);
        if (gen == g_gen) {
            if (g_mb_full) data_free(&g_mb_data);
            g_mb_data = d; g_mb_state = st; g_mb_gen = gen; g_mb_full = true;
        } else {
            data_free(&d);
        }
        LightLock_Unlock(&g_lk);
    }
}

/* -------------------------------------------------------------- public API */
static void load_cfg(void)
{
    FILE *f = fopen(LY_CFG, "r");
    if (!f) return;
    char line[400];
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        if (!strncmp(line, "proxy=", 6) && !strncmp(line + 6, "http", 4)) snprintf(g_proxy, sizeof g_proxy, "%s", line + 6);
        else if (!strncmp(line, "token=", 6)) snprintf(g_token, sizeof g_token, "%s", line + 6);
        else if (!strncmp(line, "insecure_tls=", 13)) g_insecure = atoi(line + 13) != 0;
    }
    fclose(f);
}

void lyrics_init(void)
{
    if (g_inited) return;
    g_inited = true;
    LightLock_Init(&g_lk);
    load_cfg();
    g_ac_ok = R_SUCCEEDED(acInit());

    /* libcurl ultimately uses the 3DS BSD socket layer. A 3DS process must
     * initialize soc:u with a page-aligned buffer before DNS/TCP sockets can
     * work. This was missing from the earlier lyrics implementation and can
     * produce exactly the generic "couldn't reach lyrics servers" screen even
     * though the console has working Wi-Fi. */
    g_soc_buf = (u32 *)memalign(SOC_ALIGN, SOC_BUFSIZE);
    if (g_soc_buf) g_soc_ok = R_SUCCEEDED(socInit(g_soc_buf, SOC_BUFSIZE));
    if (!g_soc_ok) {
        free(g_soc_buf);
        g_soc_buf = NULL;
    }

    g_curl_ok = g_soc_ok && (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    /* Keep http:C initialized as a real fallback even when curl is available. */
    g_http_ok = R_SUCCEEDED(httpcInit(0));
    g_quit = false;
    g_thread = threadCreate(worker, NULL, 64 * 1024, 0x30, 1, false);
    if (!g_thread) g_thread = threadCreate(worker, NULL, 64 * 1024, 0x3F, -2, false);
}

void lyrics_exit(void)
{
    if (!g_inited) return;
    g_quit = true;
    g_gen++;
    if (g_thread) {
        if (R_FAILED(threadJoin(g_thread, 1500ULL * 1000 * 1000))) return;   /* still inside a network call: the process exit ends it */
        threadFree(g_thread);
        g_thread = NULL;
    }
    if (g_http_ok) httpcExit();
    if (g_curl_ok) curl_global_cleanup();
    if (g_soc_ok) socExit();
    free(g_soc_buf);
    g_soc_buf = NULL;
    g_soc_ok = false;
    if (g_ac_ok) acExit();
    data_free(&g_cur);
    data_free(&g_mb_data);
    g_inited = false;
}

static void adopt(LyData *d, LyState st, const LyTrack *t)
{
    data_free(&g_cur);
    if (d) g_cur = *d;
    g_state = st;
    snprintf(g_key, sizeof g_key, "%s", t->path);
}

bool lyrics_preload(const LyTrack *t, const LyOptions *o)
{
    if (!g_inited) return false;
    LyData d;
    memset(&d, 0, sizeof d);
    if (!cache_lookup(t, o, &d)) return false;
    adopt(&d, LY_READY, t);
    return true;
}

void lyrics_request(const LyTrack *t, const LyOptions *o, bool force_online)
{
    if (!g_inited) return;
    if (!force_online && (g_state == LY_READY || g_state == LY_LOADING) && !strcmp(g_key, t->path)) return;

    LightLock_Lock(&g_lk);
    g_gen++;                                         /* cancels whatever the worker is doing */
    if (g_mb_full) { data_free(&g_mb_data); g_mb_full = false; }
    g_job = *t; g_job_opt = *o; g_job_force = force_online; g_job_gen = g_gen; g_job_valid = true;
    LightLock_Unlock(&g_lk);
    adopt(NULL, LY_LOADING, t);
}

void lyrics_pump(void)
{
    if (!g_inited || g_state != LY_LOADING) return;
    LightLock_Lock(&g_lk);
    if (g_mb_full && g_mb_gen == g_gen) {
        data_free(&g_cur);
        g_cur = g_mb_data;
        memset(&g_mb_data, 0, sizeof g_mb_data);
        g_state = g_mb_state;
        g_mb_full = false;
    }
    LightLock_Unlock(&g_lk);
}

LyState     lyrics_state(void)  { return g_state; }
const char *lyrics_key(void)    { return g_key; }
bool        lyrics_synced(void) { return g_state == LY_READY && g_cur.synced; }
const char *lyrics_source(void) { return g_cur.src; }
int         lyrics_count(void)  { return g_state == LY_READY ? g_cur.n : 0; }
const char *lyrics_text(int i)  { return (g_state == LY_READY && i >= 0 && i < g_cur.n) ? g_cur.text + g_cur.off[i] : ""; }
int         lyrics_time(int i)  { return (g_state == LY_READY && i >= 0 && i < g_cur.n) ? g_cur.ms[i] : 0; }

int lyrics_index_at(unsigned pos_ms)
{
    if (g_state != LY_READY || !g_cur.synced) return -1;
    int lo = 0, hi = g_cur.n - 1, ans = -1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if ((unsigned)g_cur.ms[mid] <= pos_ms) { ans = mid; lo = mid + 1; } else hi = mid - 1;
    }
    return ans;
}

int lyrics_clear_cache(void)
{
    DIR *dir = opendir(LY_DIR);
    if (!dir) return 0;
    int removed = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        const char *dot = strrchr(de->d_name, '.');
        if (!dot || (strcmp(dot, ".lrc") && strcmp(dot, ".txt") && strcmp(dot, ".nf"))) continue;
        char p[320];
        snprintf(p, sizeof p, LY_DIR "/%s", de->d_name);
        if (remove(p) == 0) removed++;
    }
    closedir(dir);
    return removed;
}
