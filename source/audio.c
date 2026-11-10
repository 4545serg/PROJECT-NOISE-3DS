#include "audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <strings.h>

#include "dr_flac.h"
#include "meta.h"
#include "minimp3.h"

#define NBUF            6
#define FRAME_SAMPLES   1152
#define IN_BUF          16384
#define REFILL_BELOW    4096
#define THREAD_STACK    (128 * 1024)

enum { FMT_MP3, FMT_WAV, FMT_FLAC };

/* ---- shared state (main thread <-> audio thread) ---- */
static Thread          g_thread;
static LightLock       g_lock;
static volatile bool   g_quit;
static char            g_req_path[512];
static u32             g_req_start;
static volatile int    g_req;            /* 0 none, 1 play, 2 stop */
static volatile float  g_req_frac = -1.0f;   /* >= 0: start this far into the file (seek) */
static volatile u32    g_req_ms;
static volatile bool   g_want_pause;
static volatile int    g_want_vol = 80;
static volatile bool   g_active, g_finished, g_error;
static volatile u32    g_rate, g_played; /* Hz, frames played so far */

/* ---- audio-thread-owned state ---- */
static FILE           *fp;
static int             g_fmt;
static drflac         *flac;
static s16            *flac_tmp;
static WavInfo         wav;
static u8             *wav_buf;          /* FRAME_SAMPLES * 32 bytes: one chunk of raw PCM */
static u32             wav_left;         /* data bytes not yet read */
static mp3dec_t       *dec;
static mp3d_sample_t  *pcm_tmp;
static u8             *inbuf;
static size_t          inlen, inoff;
static bool            in_eof, dec_done, rate_set;
static ndspWaveBuf     wb[NBUF];
static bool            wq[NBUF];
static s16            *out_pcm;
static int             fill_i, rec_i;
static bool            applied_pause;
static int             applied_vol = -1;
static bool            ready;

/* ---- 5-band peaking equalizer (RBJ biquads, run on the audio thread) ---- */
#define EQ_N 5
static const float     EQ_FREQ[EQ_N] = { 60.0f, 230.0f, 910.0f, 3600.0f, 14000.0f };
static volatile int    g_eq_gain[EQ_N];
static volatile bool   g_eq_dirty = true;
typedef struct { float b0, b1, b2, a1, a2; } Biquad;
static Biquad          eq_c[EQ_N];
static bool            eq_on[EQ_N];
static float           eq_z[2][EQ_N][2];
static float           eq_pre = 1.0f;
static bool            eq_active;
static int             eq_hz;

static void eq_update(int hz)
{
    g_eq_dirty = false;                       /* clear first: a change during this pass re-runs it */
    int gmax = 0;
    eq_active = false;
    for (int b = 0; b < EQ_N; b++) {
        int g = g_eq_gain[b];
        eq_on[b] = false;
        if (g == 0 || hz <= 0 || EQ_FREQ[b] >= (float)hz * 0.45f) continue;
        float A  = powf(10.0f, (float)g / 40.0f);
        float w0 = 6.2831853f * EQ_FREQ[b] / (float)hz;
        float al = sinf(w0) / (2.0f * 1.0f);            /* Q = 1 */
        float cw = cosf(w0);
        float a0 = 1.0f + al / A;
        eq_c[b].b0 = (1.0f + al * A) / a0;
        eq_c[b].b1 = (-2.0f * cw) / a0;
        eq_c[b].b2 = (1.0f - al * A) / a0;
        eq_c[b].a1 = (-2.0f * cw) / a0;
        eq_c[b].a2 = (1.0f - al / A) / a0;
        eq_on[b] = true;
        eq_active = true;
        if (g > gmax) gmax = g;
    }
    eq_pre = powf(10.0f, -((float)gmax * 0.5f) / 20.0f);   /* headroom so boosts don't clip */
    eq_hz = hz;
}

static void eq_process(s16 *d, int frames)
{
    if (!eq_active) return;
    for (int ch = 0; ch < 2; ch++) {
        for (int i = 0; i < frames; i++) {
            float x = (float)d[2 * i + ch] * eq_pre;
            for (int b = 0; b < EQ_N; b++) {
                if (!eq_on[b]) continue;
                const Biquad *c = &eq_c[b];
                float *z = eq_z[ch][b];
                float y = c->b0 * x + z[0];
                z[0] = c->b1 * x - c->a1 * y + z[1];
                z[1] = c->b2 * x - c->a2 * y;
                x = y;
            }
            if (x > 32767.0f) x = 32767.0f;
            if (x < -32768.0f) x = -32768.0f;
            d[2 * i + ch] = (s16)x;
        }
    }
}

static void apply_volume(int v)
{
    float mix[12];
    memset(mix, 0, sizeof mix);
    mix[0] = mix[1] = (float)v / 100.0f;
    ndspChnSetMix(0, mix);
}

static bool decode_mp3(s16 *dst, int *frames, int *hz)
{
    if (!fp) return false;
    for (;;) {
        if (!in_eof && inlen - inoff < REFILL_BELOW) {
            if (inoff) {
                memmove(inbuf, inbuf + inoff, inlen - inoff);
                inlen -= inoff;
                inoff = 0;
            }
            size_t n = fread(inbuf + inlen, 1, IN_BUF - inlen, fp);
            if (n == 0) in_eof = true;
            inlen += n;
        }

        size_t avail = inlen - inoff;
        if (avail == 0) return false;

        mp3dec_frame_info_t info;
        int smp = mp3dec_decode_frame(dec, inbuf + inoff, (int)avail, pcm_tmp, &info);

        if (info.frame_bytes == 0) {
            if (in_eof) return false;
            if (avail >= REFILL_BELOW) inoff = inlen;   /* garbage: skip it all */
            continue;                                   /* otherwise refill     */
        }
        inoff += (size_t)info.frame_bytes;
        if (smp <= 0) continue;                         /* skipped tag/junk     */
        if (smp > FRAME_SAMPLES) smp = FRAME_SAMPLES;

        if (info.channels == 2) {
            memcpy(dst, pcm_tmp, (size_t)smp * 2 * sizeof(s16));
        } else {
            for (int i = 0; i < smp; i++) dst[2 * i] = dst[2 * i + 1] = pcm_tmp[i];
        }
        *frames = smp;
        *hz = info.hz;
        return true;
    }
}

static bool decode_wav(s16 *dst, int *frames, int *hz)
{
    if (!fp) return false;
    u32 want = FRAME_SAMPLES * (u32)wav.align;
    if (want > wav_left) want = wav_left - wav_left % wav.align;
    if (!want) return false;
    size_t n = fread(wav_buf, 1, want, fp);
    if (n < want) wav_left = 0; else wav_left -= (u32)n;
    n -= n % wav.align;
    if (!n) return false;

    int ch = wav.channels, bps = wav.bits / 8, cnt = (int)(n / wav.align);
    for (int i = 0; i < cnt; i++) {
        const u8 *fr = wav_buf + (size_t)i * wav.align;
        for (int c = 0; c < 2; c++) {
            const u8 *p = fr + (size_t)(c < ch ? c : 0) * bps;      /* mono -> both sides, >2ch -> first two */
            int v;
            if (wav.tag == 3) {                                      /* 32-bit float */
                float f;
                memcpy(&f, p, 4);
                f *= 32767.0f;
                if (f > 32767.0f) f = 32767.0f;
                if (f < -32768.0f) f = -32768.0f;
                v = (int)f;
            } else if (bps == 1) {
                v = ((int)p[0] - 128) << 8;
            } else {
                v = (s16)(p[bps - 2] | (p[bps - 1] << 8));           /* keep the top 16 bits */
            }
            dst[2 * i + c] = (s16)v;
        }
    }
    *frames = cnt;
    *hz = (int)wav.rate;
    return true;
}

static bool decode_flac(s16 *dst, int *frames, int *hz)
{
    drflac_uint64 got = drflac_read_pcm_frames_s16(flac, FRAME_SAMPLES, flac_tmp);
    if (got == 0) return false;
    int ch = (int)flac->channels;
    for (drflac_uint64 i = 0; i < got; i++) {
        s16 l = flac_tmp[i * (drflac_uint64)ch];
        dst[2 * i] = l;
        dst[2 * i + 1] = (ch > 1) ? flac_tmp[i * (drflac_uint64)ch + 1] : l;
    }
    *frames = (int)got;
    *hz = (int)flac->sampleRate;
    return true;
}

static bool decode_frame(s16 *dst, int *frames, int *hz)
{
    if (g_fmt == FMT_FLAC) return decode_flac(dst, frames, hz);
    if (g_fmt == FMT_WAV)  return decode_wav(dst, frames, hz);
    return decode_mp3(dst, frames, hz);
}

static void close_track(void)
{
    if (fp) { fclose(fp); fp = NULL; }
    if (flac) { drflac_close(flac); flac = NULL; }
    free(flac_tmp); flac_tmp = NULL;
}

static bool has_ext(const char *path, const char *ext)
{
    size_t n = strlen(path), e = strlen(ext);
    return n > e && strcasecmp(path + n - e, ext) == 0;
}

/* Opens `path` for the detected format and positions it. frac >= 0 seeks that far in. */
static bool open_track(const char *path, u32 start, float frac)
{
    if (has_ext(path, ".flac")) {
        g_fmt = FMT_FLAC;
        flac = drflac_open_file(path, NULL);
        if (!flac) return false;
        if (flac->channels == 0 || flac->channels > 8 || flac->sampleRate == 0) return false;
        flac_tmp = malloc((size_t)FRAME_SAMPLES * flac->channels * sizeof(s16));
        if (!flac_tmp) return false;
        if (frac >= 0.0f && flac->totalPCMFrameCount)
            drflac_seek_to_pcm_frame(flac, (drflac_uint64)((double)flac->totalPCMFrameCount * (double)frac));
        return true;
    }

    fp = fopen(path, "rb");
    if (!fp) return false;

    if (has_ext(path, ".wav")) {
        g_fmt = FMT_WAV;
        if (!wav_parse(fp, &wav, NULL)) return false;
        bool ok_fmt = (wav.tag == 1 && (wav.bits == 8 || wav.bits == 16 || wav.bits == 24 || wav.bits == 32)) ||
                      (wav.tag == 3 && wav.bits == 32);
        if (!ok_fmt || wav.channels > 8 || wav.align < wav.channels * (wav.bits / 8) || wav.align > 32) return false;
        wav_left = wav.data_size - wav.data_size % wav.align;
        u32 skip = 0;
        if (frac >= 0.0f) skip = (u32)((float)(wav_left / wav.align) * frac) * wav.align;
        wav_left -= skip;
        return fseek(fp, (long)(wav.data_off + skip), SEEK_SET) == 0;
    }

    g_fmt = FMT_MP3;
    if (frac >= 0.0f) {
        long sz = 0;
        if (fseek(fp, 0, SEEK_END) == 0) sz = ftell(fp);
        long end = sz - 128;                         /* ignore a trailing ID3v1 tag */
        long off = (long)start;
        if (end > off) off += (long)((float)(end - off) * frac);
        fseek(fp, off, SEEK_SET);
    } else {
        fseek(fp, (long)start, SEEK_SET);
    }
    return true;
}

static void handle_request(void)
{
    char path[512];
    LightLock_Lock(&g_lock);
    int req = g_req;
    u32 start = g_req_start;
    float frac = g_req_frac;
    u32 ms = g_req_ms;
    snprintf(path, sizeof path, "%s", g_req_path);
    g_req = 0;
    LightLock_Unlock(&g_lock);

    ndspChnReset(0);
    for (int i = 0; i < NBUF; i++) { wq[i] = false; memset(&wb[i], 0, sizeof wb[i]); }
    close_track();

    if (req == 2) { g_active = false; return; }

    if (!open_track(path, start, frac)) {
        close_track();
        g_active = false;
        g_error = true;
        return;
    }

    mp3dec_init(dec);
    memset(eq_z, 0, sizeof eq_z);
    inlen = inoff = 0;
    in_eof = dec_done = rate_set = false;
    fill_i = rec_i = 0;
    g_played = (frac >= 0.0f) ? (u32)((u64)ms * (g_rate ? g_rate : 44100u) / 1000u) : 0;

    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    apply_volume(g_want_vol);
    applied_vol = g_want_vol;
    ndspChnSetPaused(0, false);
    applied_pause = false;
    g_active = true;
}

static void pump(void)
{
    /* reclaim finished buffers, strictly in queue order */
    while (wq[rec_i] && wb[rec_i].status == NDSP_WBUF_DONE) {
        wq[rec_i] = false;
        g_played += wb[rec_i].nsamples;
        rec_i = (rec_i + 1) % NBUF;
    }

    /* refill, strictly in queue order */
    while (!dec_done && !wq[fill_i]) {
        int frames = 0, hz = 0;
        s16 *dst = out_pcm + (size_t)fill_i * FRAME_SAMPLES * 2;
        if (!decode_frame(dst, &frames, &hz)) { dec_done = true; break; }

        if (g_eq_dirty || hz != eq_hz) eq_update(hz);
        eq_process(dst, frames);

        if (!rate_set) {
            ndspChnSetRate(0, (float)hz);
            g_rate = (u32)hz;
            rate_set = true;
        }
        wb[fill_i].data_vaddr = dst;
        wb[fill_i].nsamples = (u32)frames;
        wb[fill_i].looping = false;
        DSP_FlushDataCache(dst, (u32)frames * 4);
        ndspChnWaveBufAdd(0, &wb[fill_i]);
        wq[fill_i] = true;
        fill_i = (fill_i + 1) % NBUF;
    }

    if (dec_done) {
        bool any = false;
        for (int i = 0; i < NBUF; i++) any = any || wq[i];
        if (!any) {
            close_track();
            g_active = false;
            g_finished = true;
        }
    }
}

static void audio_thread(void *arg)
{
    (void)arg;
    while (!g_quit) {
        if (g_req) handle_request();

        if (g_want_pause != applied_pause) {
            ndspChnSetPaused(0, g_want_pause);
            applied_pause = g_want_pause;
        }
        if (g_want_vol != applied_vol) {
            apply_volume(g_want_vol);
            applied_vol = g_want_vol;
        }
        if (fp || flac) pump();

        svcSleepThread(4 * 1000 * 1000LL);   /* 4 ms */
    }
}

bool audio_init(void)
{
    if (R_FAILED(ndspInit())) return false;     /* usually: dspfirm.cdc missing */
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);

    out_pcm = linearAlloc((size_t)NBUF * FRAME_SAMPLES * 2 * sizeof(s16));
    dec     = malloc(sizeof *dec);
    pcm_tmp = malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(mp3d_sample_t));
    inbuf   = malloc(IN_BUF);
    wav_buf = malloc((size_t)FRAME_SAMPLES * 32);
    if (!out_pcm || !dec || !pcm_tmp || !inbuf || !wav_buf) { audio_exit(); return false; }

    LightLock_Init(&g_lock);
    g_quit = false;
    ready = true;
    /* core 0, priority 0x18: above the UI thread (0x30) but it sleeps between pumps */
    g_thread = threadCreate(audio_thread, NULL, THREAD_STACK, 0x18, 0, false);
    if (!g_thread) { audio_exit(); return false; }
    return true;
}

void audio_exit(void)
{
    if (g_thread) {
        g_quit = true;
        threadJoin(g_thread, U64_MAX);
        threadFree(g_thread);
        g_thread = NULL;
    }
    close_track();
    if (ready) { ndspChnReset(0); }
    ndspExit();
    if (out_pcm) { linearFree(out_pcm); out_pcm = NULL; }
    free(dec);     dec = NULL;
    free(pcm_tmp); pcm_tmp = NULL;
    free(inbuf);   inbuf = NULL;
    free(wav_buf); wav_buf = NULL;
    ready = false;
}

static void request_play(const char *path, u32 audio_start, float frac, u32 start_ms)
{
    if (!ready) return;
    LightLock_Lock(&g_lock);
    snprintf(g_req_path, sizeof g_req_path, "%s", path);
    g_req_start = audio_start;
    g_req_frac = frac;
    g_req_ms = start_ms;
    g_req = 1;
    LightLock_Unlock(&g_lock);
    g_want_pause = false;
    g_finished = false;
    g_error = false;
    g_played = (frac >= 0.0f) ? (u32)((u64)start_ms * (g_rate ? g_rate : 44100u) / 1000u) : 0;
    g_active = true;
}

void audio_play(const char *path, u32 audio_start)
{
    request_play(path, audio_start, -1.0f, 0);
}

/* Jump to `frac` (0..1) of the audio data; start_ms is the time shown on the clock afterwards.
 * Position inside the file is estimated from the byte offset, so it is exact for CBR files and
 * approximate for VBR files without a seek table. */
void audio_seek(const char *path, u32 audio_start, float frac, u32 start_ms)
{
    request_play(path, audio_start, frac, start_ms);
}

void audio_stop(void)
{
    if (!ready) return;
    LightLock_Lock(&g_lock);
    g_req = 2;
    LightLock_Unlock(&g_lock);
    g_active = false;
}

void audio_pause(void)  { g_want_pause = true; }
void audio_resume(void) { g_want_pause = false; }

bool audio_is_active(void) { return g_active; }
bool audio_is_paused(void) { return g_active && g_want_pause; }

u32 audio_position_ms(void)
{
    u32 rate = g_rate ? g_rate : 44100;
    return (u32)((u64)g_played * 1000u / rate);
}

void audio_set_volume(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    g_want_vol = percent;
}

void audio_set_eq(const int gains_db[5])
{
    for (int i = 0; i < EQ_N; i++) {
        int g = gains_db[i];
        if (g < -12) g = -12;
        if (g > 12) g = 12;
        g_eq_gain[i] = g;
    }
    g_eq_dirty = true;
}

bool audio_take_finished(void)
{
    if (!g_finished) return false;
    g_finished = false;
    return true;
}

bool audio_take_error(void)
{
    if (!g_error) return false;
    g_error = false;
    return true;
}
