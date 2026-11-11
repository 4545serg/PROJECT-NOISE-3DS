#include "meta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "libcfg.h"
#include "minimp3.h"

#define MAX_ART_BYTES (3u * 1024u * 1024u)

static u32 synchsafe(const u8 *p)
{
    return ((u32)(p[0] & 0x7F) << 21) | ((u32)(p[1] & 0x7F) << 14) |
           ((u32)(p[2] & 0x7F) << 7) | (u32)(p[3] & 0x7F);
}

static u32 be32(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static size_t put_utf8(char *o, size_t cap, size_t n, u32 cp)
{
    if (cp < 0x80) {
        if (n + 1 < cap) o[n++] = (char)cp;
    } else if (cp < 0x800) {
        if (n + 2 < cap) { o[n++] = (char)(0xC0 | (cp >> 6)); o[n++] = (char)(0x80 | (cp & 0x3F)); }
    } else if (cp < 0x10000) {
        if (n + 3 < cap) {
            o[n++] = (char)(0xE0 | (cp >> 12));
            o[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            o[n++] = (char)(0x80 | (cp & 0x3F));
        }
    } else {
        if (n + 4 < cap) {
            o[n++] = (char)(0xF0 | (cp >> 18));
            o[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            o[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            o[n++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    return n;
}

/* Decodes an ID3v2 text frame body (first byte = encoding) into UTF-8. */
static void decode_text(const u8 *p, size_t len, char *out, size_t cap)
{
    size_t n = 0;
    out[0] = 0;
    if (len < 2) return;

    int enc = p[0];
    p++;
    len--;

    if (enc == 0) {                         /* ISO-8859-1 */
        for (size_t i = 0; i < len && p[i]; i++) n = put_utf8(out, cap, n, p[i]);
    } else if (enc == 3) {                  /* UTF-8 */
        for (size_t i = 0; i < len && p[i] && n + 1 < cap; i++) out[n++] = (char)p[i];
    } else {                                /* UTF-16 (with BOM) / UTF-16BE */
        bool be = (enc == 2);
        size_t i = 0;
        if (len >= 2) {
            if (p[0] == 0xFF && p[1] == 0xFE) { be = false; i = 2; }
            else if (p[0] == 0xFE && p[1] == 0xFF) { be = true; i = 2; }
        }
        for (; i + 1 < len; i += 2) {
            u32 u = be ? (u32)((p[i] << 8) | p[i + 1]) : (u32)((p[i + 1] << 8) | p[i]);
            if (u == 0) break;
            if (u >= 0xD800 && u < 0xDC00 && i + 3 < len) {
                u32 lo = be ? (u32)((p[i + 2] << 8) | p[i + 3]) : (u32)((p[i + 3] << 8) | p[i + 2]);
                if (lo >= 0xDC00 && lo < 0xE000) {
                    u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    i += 2;
                }
            }
            n = put_utf8(out, cap, n, u);
        }
    }
    out[n] = 0;
}

static void latin1_field(const u8 *p, size_t max, char *out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < max && p[i]; i++) n = put_utf8(out, cap, n, p[i]);
    while (n > 0 && out[n - 1] == ' ') n--;
    out[n] = 0;
}

static long find_image_start(const u8 *p, size_t len)
{
    size_t lim = len < 512 ? len : 512;
    for (size_t i = 0; i + 4 < lim; i++) {
        if (p[i] == 0xFF && p[i + 1] == 0xD8 && p[i + 2] == 0xFF) return (long)i;
        if (p[i] == 0x89 && !memcmp(p + i + 1, "PNG", 3)) return (long)i;
    }
    return -1;
}

static void title_from_path(const char *path, char *out, size_t cap)
{
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    snprintf(out, cap, "%s", b);
    char *dot = strrchr(out, '.');
    if (dot) *dot = 0;
}

static void probe_duration(FILE *f, TrackMeta *m, bool has_v1)
{
    u8 *buf = malloc(8192);
    mp3dec_t *d = malloc(sizeof *d);
    mp3d_sample_t *pcm = malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t));

    do {
        if (!buf || !d || !pcm) break;
        if (fseek(f, (long)m->audio_start, SEEK_SET) != 0) break;
        size_t n = fread(buf, 1, 8192, f);
        if (n < 8) break;

        mp3dec_init(d);
        mp3dec_frame_info_t info;
        memset(&info, 0, sizeof info);
        int smp = 0;
        size_t off = 0;
        for (int tries = 0; tries < 4 && off < n; tries++) {
            smp = mp3dec_decode_frame(d, buf + off, (int)(n - off), pcm, &info);
            if (smp > 0 || info.frame_bytes == 0) break;
            off += (size_t)info.frame_bytes;
        }
        if (smp <= 0 || info.hz <= 0) break;

        size_t flen = (size_t)info.frame_bytes;
        if (off + flen > n) flen = n - off;
        const u8 *fr = buf + off;
        u32 frames = 0;
        for (size_t i = 0; i + 16 < flen && i < 200; i++) {
            if (!memcmp(fr + i, "Xing", 4) || !memcmp(fr + i, "Info", 4)) {
                u32 flags = be32(fr + i + 4);
                if (flags & 1) frames = be32(fr + i + 8);
                break;
            }
            if (!memcmp(fr + i, "VBRI", 4)) {
                frames = be32(fr + i + 14);
                break;
            }
        }

        if (frames) {
            m->duration_ms = (u32)((u64)frames * (u64)smp * 1000u / (u64)info.hz);
        } else if (info.bitrate_kbps > 0) {
            if (fseek(f, 0, SEEK_END) != 0) break;
            long sz = ftell(f);
            long bytes = sz - (long)m->audio_start - (has_v1 ? 128 : 0);
            if (bytes > 0) m->duration_ms = (u32)((u64)bytes * 8u / (u64)info.bitrate_kbps);
        }
    } while (0);

    free(buf);
    free(d);
    free(pcm);
}

static u16 le16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 le32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static bool ext_is(const char *path, const char *ext)
{
    size_t n = strlen(path), e = strlen(ext);
    return n > e && strcasecmp(path + n - e, ext) == 0;
}

/* ------------------------------------------------------------------- WAV */
bool wav_parse(FILE *f, WavInfo *w, TrackMeta *m)
{
    memset(w, 0, sizeof *w);
    u8 h[12];
    if (fseek(f, 0, SEEK_SET) != 0 || fread(h, 1, 12, f) != 12) return false;
    if (memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
    if (fseek(f, 0, SEEK_END) != 0) return false;
    long fsz = ftell(f), pos = 12;
    bool have_fmt = false, have_data = false;

    while (pos + 8 <= fsz) {
        u8 ch[8];
        if (fseek(f, pos, SEEK_SET) != 0 || fread(ch, 1, 8, f) != 8) break;
        u32 sz = le32(ch + 4);
        long body = pos + 8;

        if (!memcmp(ch, "fmt ", 4) && sz >= 16) {
            u8 b[40];
            size_t n = sz < sizeof b ? sz : sizeof b;
            if (fread(b, 1, n, f) != n) break;
            w->tag = le16(b);
            w->channels = le16(b + 2);
            w->rate = le32(b + 4);
            w->byterate = le32(b + 8);
            w->align = le16(b + 12);
            w->bits = le16(b + 14);
            if (w->tag == 0xFFFE && n >= 26) w->tag = le16(b + 24);   /* extensible: real tag in the GUID */
            if (!w->byterate) w->byterate = w->rate * w->align;
            have_fmt = true;
        } else if (!memcmp(ch, "data", 4)) {
            long avail = fsz - body;
            w->data_off = (u32)body;
            w->data_size = (sz == 0 || (long)sz > avail) ? (u32)avail : sz;
            have_data = true;
        } else if (m && !memcmp(ch, "LIST", 4) && sz >= 4 && sz <= 4096) {
            u8 buf[4096];
            if (fread(buf, 1, sz, f) == sz && !memcmp(buf, "INFO", 4)) {
                size_t o = 4;
                while (o + 8 <= sz) {
                    u32 ss = le32(buf + o + 4);
                    if (ss > sz || o + 8 + ss > sz) break;
                    char *dst = NULL;
                    size_t cap = 0;
                    if (!memcmp(buf + o, "INAM", 4)) { dst = m->title;  cap = sizeof m->title; }
                    if (!memcmp(buf + o, "IART", 4)) { dst = m->artist; cap = sizeof m->artist; }
                    if (dst && !dst[0]) {
                        size_t k = ss < cap - 1 ? ss : cap - 1;
                        memcpy(dst, buf + o + 8, k);
                        dst[k] = 0;
                    }
                    o += 8 + ss + (ss & 1);
                }
            }
        }
        if (have_data && have_fmt && !m) break;          /* the player needs nothing after the data */
        if (sz > 0x7FFFFFFFu) break;
        pos = body + (long)sz + (long)(sz & 1);
    }
    return have_fmt && have_data && w->channels && w->rate && w->align;
}

static void meta_wav(FILE *f, TrackMeta *m)
{
    WavInfo w;
    if (wav_parse(f, &w, m) && w.byterate)
        m->duration_ms = (u32)((u64)w.data_size * 1000u / w.byterate);
    m->audio_start = w.data_off;
}

/* ------------------------------------------------------------------ FLAC */
static void copy_text(char *dst, size_t cap, const u8 *src, size_t len)
{
    size_t k = len < cap - 1 ? len : cap - 1;
    memcpy(dst, src, k);
    dst[k] = 0;
}

static void vorbis_comments(const u8 *b, size_t len, TrackMeta *m)
{
    if (len < 8) return;
    size_t vl = le32(b);
    if (vl > len || 4 + vl + 4 > len) return;
    size_t o = 4 + vl;
    u32 cnt = le32(b + o);
    o += 4;
    for (u32 i = 0; i < cnt && o + 4 <= len; i++) {
        size_t l = le32(b + o);
        o += 4;
        if (l > len || o + l > len) break;
        const char *e = (const char *)(b + o);
        if (l > 6 && !strncasecmp(e, "TITLE=", 6) && !m->title[0])
            copy_text(m->title, sizeof m->title, b + o + 6, l - 6);
        else if (l > 7 && !strncasecmp(e, "ARTIST=", 7) && !m->artist[0])
            copy_text(m->artist, sizeof m->artist, b + o + 7, l - 7);
        o += l;
    }
}

static void flac_picture(const u8 *b, size_t len, u8 **art, size_t *art_size)
{
    size_t o = 4;                                        /* picture type */
    if (o + 4 > len) return;
    size_t ml = be32(b + o);                             /* mime */
    if (ml > len) return;
    o += 4 + ml;
    if (o + 4 > len) return;
    size_t dl = be32(b + o);                             /* description */
    if (dl > len) return;
    o += 4 + dl;
    if (o + 20 > len) return;
    o += 16;                                             /* width, height, depth, colours */
    size_t n = be32(b + o);
    o += 4;
    if (n == 0 || n > len || o + n > len) return;
    if (find_image_start(b + o, n) != 0) return;         /* only JPEG / PNG */
    u8 *copy = malloc(n);
    if (!copy) return;
    memcpy(copy, b + o, n);
    *art = copy;
    if (art_size) *art_size = n;
}

static void meta_flac(FILE *f, TrackMeta *m, u8 **art, size_t *art_size)
{
    u8 h[10];
    long pos = 0;
    if (fseek(f, 0, SEEK_SET) != 0 || fread(h, 1, 10, f) != 10) return;
    if (!memcmp(h, "ID3", 3)) pos = 10 + (long)synchsafe(h + 6) + ((h[5] & 0x10) ? 10 : 0);
    u8 mg[4];
    if (fseek(f, pos, SEEK_SET) != 0 || fread(mg, 1, 4, f) != 4 || memcmp(mg, "fLaC", 4)) return;

    for (int blocks = 0; blocks < 64; blocks++) {
        u8 bh[4];
        if (fread(bh, 1, 4, f) != 4) break;
        bool last = (bh[0] & 0x80) != 0;
        int type = bh[0] & 0x7F;
        u32 len = ((u32)bh[1] << 16) | ((u32)bh[2] << 8) | bh[3];
        long next = ftell(f) + (long)len;

        if (type == 0 && len >= 18) {                   /* STREAMINFO */
            u8 si[18];
            if (fread(si, 1, 18, f) == 18) {
                u32 rate = ((u32)si[10] << 12) | ((u32)si[11] << 4) | (si[12] >> 4);
                u64 total = ((u64)(si[13] & 0x0F) << 32) | be32(si + 14);
                if (rate && total) m->duration_ms = (u32)(total * 1000u / rate);
            }
        } else if ((type == 4 && len <= 65536) ||
                   (type == 6 && art && !*art && len <= MAX_ART_BYTES + 1024u)) {
            u8 *b = malloc(len);
            if (b && fread(b, 1, len, f) == len) {
                if (type == 4) vorbis_comments(b, len, m);
                else flac_picture(b, len, art, art_size);
            }
            free(b);
        }
        if (last) break;
        if (fseek(f, next, SEEK_SET) != 0) break;
    }
}

bool meta_read(const char *path, TrackMeta *m, u8 **art, size_t *art_size, bool want_duration)
{
    memset(m, 0, sizeof *m);
    if (art) *art = NULL;
    if (art_size) *art_size = 0;

    FILE *f = fopen(path, "rb");
    if (!f) return false;

    if (ext_is(path, ".wav") || ext_is(path, ".flac")) {
        if (ext_is(path, ".wav")) meta_wav(f, m);
        else meta_flac(f, m, art, art_size);
        if (!m->title[0]) title_from_path(path, m->title, sizeof m->title);
        fclose(f);
        return true;
    }

    u8 h[10];
    if (fread(h, 1, 10, f) == 10 && !memcmp(h, "ID3", 3) && h[3] >= 2 && h[3] <= 4) {
        int ver = h[3];
        u8 flags = h[5];
        u32 tagsz = synchsafe(h + 6);
        long end = 10 + (long)tagsz;
        long pos = 10;
        m->audio_start = (u32)(end + ((flags & 0x10) ? 10 : 0));

        if (ver >= 3 && (flags & 0x40)) {                 /* extended header */
            u8 eh[4];
            if (fread(eh, 1, 4, f) == 4) {
                u32 es = (ver == 4) ? synchsafe(eh) : be32(eh) + 4;
                pos += (long)es;
                fseek(f, pos, SEEK_SET);
            }
        }

        const int hdr = (ver == 2) ? 6 : 10;
        while (pos + hdr <= end) {
            u8 fh[10];
            if (fread(fh, 1, (size_t)hdr, f) != (size_t)hdr) break;
            if (fh[0] == 0) break;                         /* padding */

            char id[5] = { 0 };
            u32 sz;
            if (ver == 2) {
                memcpy(id, fh, 3);
                sz = ((u32)fh[3] << 16) | ((u32)fh[4] << 8) | fh[5];
            } else {
                memcpy(id, fh, 4);
                sz = (ver == 4) ? synchsafe(fh + 4) : be32(fh + 4);
            }
            long data_pos = pos + hdr;
            if (sz == 0 || data_pos + (long)sz > end) break;

            bool is_title  = !strcmp(id, "TIT2") || !strcmp(id, "TT2");
            bool is_artist = !strcmp(id, "TPE1") || !strcmp(id, "TP1");
            bool is_pic    = !strcmp(id, "APIC") || !strcmp(id, "PIC");

            if ((is_title || is_artist) && sz <= 2048) {
                u8 tb[2048];
                if (fread(tb, 1, sz, f) == sz) {
                    if (is_title && !m->title[0])   decode_text(tb, sz, m->title, sizeof m->title);
                    if (is_artist && !m->artist[0]) decode_text(tb, sz, m->artist, sizeof m->artist);
                }
            } else if (is_pic && art && !*art && sz <= MAX_ART_BYTES) {
                u8 *b = malloc(sz);
                if (b && fread(b, 1, sz, f) == sz) {
                    long off = find_image_start(b, sz);
                    if (off >= 0) {
                        memmove(b, b + off, sz - (size_t)off);
                        *art = b;
                        if (art_size) *art_size = sz - (size_t)off;
                        b = NULL;
                    }
                }
                free(b);
            }

            pos = data_pos + (long)sz;
            fseek(f, pos, SEEK_SET);
        }
    }

    /* ID3v1 fallback / presence check */
    bool has_v1 = false;
    {
        u8 b[128];
        if (fseek(f, -128, SEEK_END) == 0 && fread(b, 1, 128, f) == 128 && !memcmp(b, "TAG", 3)) {
            has_v1 = true;
            if (!m->title[0])  latin1_field(b + 3, 30, m->title, sizeof m->title);
            if (!m->artist[0]) latin1_field(b + 33, 30, m->artist, sizeof m->artist);
        }
    }

    if (!m->title[0]) title_from_path(path, m->title, sizeof m->title);
    if (want_duration) probe_duration(f, m, has_v1);

    fclose(f);
    return true;
}
