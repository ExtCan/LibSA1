/*
 * audio_test: the libsa1 audio layer (include/libsa1_audio.h) against the user's own disc.
 *
 *   audio_test <game.gdi> [outdir]            (outdir default: build/audio_test; never commit it)
 *
 * Checks
 *   - sa1_audio_open, the game's tables (tone ranges, 3D radii, music names), allocation balance;
 *   - every program of Sonic's banks (P_SONICTAILS.MLT bank 3 = tones 723-736, V_SONICTAILS_E.MLT bank 6 =
 *     tones 1112-1144) and of the common banks 0 (tones 0-39) and 2 (tones 699-722) of EMERALDCOAST.MLT:
 *     the layers sa1_audio_sound_layer reports are compared with an independent parse of the MLT read
 *     through the disc layer (sample count = LEA, loop = LPCTL / LSA, rate = 44100 * 2^((60 - base +
 *     fine / 256) / 12), format = PCMS), and the decoded PCM must not be silent;
 *   - the sequence tones of E-102 (777-781) render to non-silent PCM;
 *   - voices (ADX from the AFS) and two jingles decode to exactly the frame count of their header;
 *   - cue mapping of every event kind libsa1 emits;
 *   - the mixer: one-shot, keep-alive (refresh / timeout), stop, voice, positional attenuation (the script waits for
 *     the loader thread where it would otherwise depend on its speed: tones decoded, the voice's file read);
 *   - music (header version 3): a MUSIC cue for 69 (NIGHTS_K.ADX, not on the disc) says SA1_ERR_NOT_FOUND at once,
 *     queues no read and stops the music; sa1_audio_mixer_duck ramps the background music down and spares the jingles
 *     (sample for sample what an unducked mixer plays) unless SA1_AUDIO_DUCK_JINGLES;
 *   - the game's music state (header version 4, sa1_audio_mixer_music_state; check_music_state): the drowning chain
 *     frame by frame (jingle 96 twice, the drown's music stop, the countdown's RESTORE), jingles that play out, are
 *     restored, stopped, replaced or ended by MUSIC, their kinds / lengths / loops (45 FISHING.ADX loops), delays,
 *     SA1_AUDIO_MIX_NO_JINGLES, the host's stop, bgm_playing, struct_size; the 1-up (tone 704) is not a jingle;
 *   - the music list of an SA1_AUDIO_MUSIC_ONLY object (no banks, no voices, no preload; streams work): 110 infos,
 *     109 on the disc, 104 in the sound test on pages of 11/40/11/12/10/20 with the game's titles, the jingles
 *     {26 44 45 46 55 56 85 96}, 87 truncated, every header equal to sa1_audio_stream_open's, the first call reading
 *     ADVERTISE.PRS (87 sectors) and then at most 2 sectors per number (counted by the file callback); with one byte
 *     of ADVERTISE.PRS flipped by the file callback: no titles and SA1_OK;
 *   - SA1_AUDIO_LAZY_STREAMS: an open reads 2 sectors, the PCM equals the whole-file stream's (pieces, loop seam,
 *     seeks back and forward) and each file is read once;
 *   - robustness: the ADX parser, the ADPCM decoder, program and sequence parsing on corrupted copies;
 *   - no stalls (G-SA1 0.3's freezes, docs/research/gsa1_ramp_freeze.md fix 7): on a disc whose every read takes
 *     25 ms (a cold disc image), after the preload no audio call of a 20 s session - jingles (TIMER every 2 s and a
 *     cold one), voices that were never read, sounds of every kind, stops, characters, listener, 1/30 s renders -
 *     takes more than 1 ms; the loader thread reads the clips (none on the calling thread), the preloaded TIMER sounds
 *     in the very next render; the same session with SA1_AUDIO_SYNC_LOADS (header version 1) is reported; without the
 *     preload a cue whose tone is not decoded yet waits for the loader and starts a moment later;
 *   - stage rules (header version 5, group stage_rules; sadc docs/research/super2_plan.md 6.3 (a)-(l)): without rules a
 *     fixed 4,000-cue script hashes 1bea9caaae7a1f05 as the 0.9.4 baseline did; a character's rule {19, 1035, 1036} (Super
 *     Sonic's whoosh) sets in < 2 ms, reads CHAOS7B.MLT bank 5 on the loader thread (< 0.5 MB) and renders 1035 exactly as
 *     an object opened on stage 19, another character's 1035 stays Emerald Coast's (empty); toggling a rule 10 times while
 *     a loop plays costs < 2 ms and stops nothing; the board's rules {8, 208, 238} and {38, 690, 698} render 237 / 691 as
 *     Ice Cap / Sand Hill; sa1_audio_set_stage keeps the rules; limits and arguments; sa1_audio_mixer_stop forgets them; a
 *     cue before the banks are read waits (< 0.5 s); SA1_AUDIO_SYNC_LOADS reads on the caller; an unreadable MLT (the file
 *     callback fails its sectors) leaves the rule's tones silent with one warning on the mixer's thread; closing with sets
 *     READY, FAILED and LOADING frees everything (run it under SA1_GUARD_ALLOC=1 too);
 *   - a tone started again while it plays (group repeats; super2_plan.md 10.3): the game lets the ring (tone 7) pile up
 *     (every 8C0940DA call its own queue slot and MIDI channel): 10 rings 3 steps apart reach +6 dB in the loudest 50 ms and
 *     clip; with SA1_AUDIO_MIX_RESTART_REPEATS a fast ring line peaks at one ring's level, while two characters' rings,
 *     other tones and positional sounds still overlap;
 *   - the whoosh end to end (group whoosh_world; super2_plan.md 6.3 (m)): kind-8 and General Super worlds driven at the
 *     forced speeds 8.5 / 14.5 / 2 send SOUND 1035, 1036 and the stops; through a mixer whose character has the rule they
 *     sound (two layers each, then the release), without it they are silent, and the General form with SA1_SUPOPT_NO_AURA
 *     sends none.
 * Groups: audio_test GDI [OUTDIR] [--only NAME[,NAME]] [--skip NAME[,NAME]] (names at want()).
 * Writes WAV files for listening into outdir: tone_<n>.wav (the mixer playing the tone alone, stereo
 * 44.1 kHz), layers/<n>_<layer>.wav (each layer's raw PCM at its own rate), voice_<n>.wav, music_<name>.wav,
 * mixer_script.wav, and tones.csv (what every tone resolved to).
 * Uses internal headers (links the static library).
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#include "sa1_audio.h"
#include "test_util.h"

#include <math.h>
#include <stddef.h>

/* the page-guard allocator (tests/guard_alloc.h): SA1_GUARD_ALLOC=1 puts libsa1's heap blocks against no-access pages */
#include "guard_alloc.c"
#define sa1_host_defaults(h) (sa1_host_defaults(h), ga_host(h))

static int g_fail, g_pass;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) {                                                              \
            g_pass++;                                                            \
        } else {                                                                 \
            g_fail++;                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                        \
    } while (0)

/* counting allocator (the audio loader thread allocates too): blocks and, since header version 5's tests, bytes (a 16-byte
 * prefix holds the size; the block's end stays against the guard page under SA1_GUARD_ALLOC) */
static long g_allocs;
static long long g_bytes;
static void *count_alloc(void *user, size_t size)
{
    uint8_t *p;
    (void)user;
    p = (uint8_t *)ga_malloc(size + 16u);
    if (!p)
        return NULL;
    memcpy(p, &size, sizeof size);
    __atomic_fetch_add(&g_allocs, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&g_bytes, (long long)size, __ATOMIC_RELAXED);
    return p + 16;
}
static void count_free(void *user, void *p)
{
    size_t size;
    (void)user;
    if (!p)
        return;
    memcpy(&size, (uint8_t *)p - 16, sizeof size);
    __atomic_fetch_sub(&g_allocs, 1, __ATOMIC_RELAXED);
    __atomic_fetch_sub(&g_bytes, (long long)size, __ATOMIC_RELAXED);
    ga_free((uint8_t *)p - 16);
}

/* a slow disc: stdio file callbacks with g_slow_ms of delay per read (a cold GD-ROM image on a slow drive); they also
 * count the sectors read (g_read_sectors, 2352-byte raw sectors) and can flip one byte of one track file (g_flip_track,
 * g_flip_off: a modified file on the disc) */
static int g_slow_ms;
static uint32_t g_read_sectors;
static char g_flip_track[260];
static uint64_t g_flip_off;
/* stage rules (j): every read of g_fail_track that overlaps the bytes g_fail_from..g_fail_to fails (an unreadable MLT) */
static char g_fail_track[260];
static uint64_t g_fail_from, g_fail_to;
typedef struct slow_file {
    FILE *f;
    int   flip;
    int   fail;
} slow_file;
static void slow_sleep(int ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
}
static void *slow_open(void *user, const char *path)
{
    slow_file *s;
    FILE *f = fopen(path, "rb");
    size_t pl = strlen(path), tl = strlen(g_flip_track);
    (void)user;
    if (!f)
        return NULL;
    s = (slow_file *)calloc(1, sizeof *s);
    if (!s) {
        fclose(f);
        return NULL;
    }
    s->f = f;
    s->flip = tl > 0 && pl >= tl && strcmp(path + pl - tl, g_flip_track) == 0 &&
              (pl == tl || path[pl - tl - 1] == '/' || path[pl - tl - 1] == '\\');
    tl = strlen(g_fail_track);
    s->fail = tl > 0 && pl >= tl && strcmp(path + pl - tl, g_fail_track) == 0 &&
              (pl == tl || path[pl - tl - 1] == '/' || path[pl - tl - 1] == '\\');
    return s;
}
static int32_t slow_seek(FILE *f, uint64_t off, int whence)
{
#if defined(_WIN32)
    return _fseeki64(f, (__int64)off, whence);
#else
    return fseeko(f, (off_t)off, whence);
#endif
}
static int32_t slow_size(void *user, void *file, uint64_t *size)
{
    slow_file *s = (slow_file *)file;
    (void)user;
    if (!s || slow_seek(s->f, 0, SEEK_END) != 0)
        return -1;
#if defined(_WIN32)
    *size = (uint64_t)_ftelli64(s->f);
#else
    *size = (uint64_t)ftello(s->f);
#endif
    return 0;
}
static int32_t slow_read(void *user, void *file, uint64_t off, void *buf, size_t n)
{
    slow_file *s = (slow_file *)file;
    int ms = __atomic_load_n(&g_slow_ms, __ATOMIC_RELAXED);
    (void)user;
    if (ms > 0)
        slow_sleep(ms);
    if (!s || slow_seek(s->f, off, SEEK_SET) != 0)
        return -1;
    if (s->fail && off < g_fail_to && off + n > g_fail_from)
        return -1;
    if (fread(buf, 1, n, s->f) != n)
        return -1;
    __atomic_fetch_add(&g_read_sectors, (uint32_t)((n + 2351u) / 2352u), __ATOMIC_RELAXED);
    if (s->flip && g_flip_off >= off && g_flip_off - off < n)
        ((uint8_t *)buf)[g_flip_off - off] ^= 0x5Au;
    return 0;
}
static void slow_close(void *user, void *file)
{
    slow_file *s = (slow_file *)file;
    (void)user;
    if (s) {
        fclose(s->f);
        free(s);
    }
}
static int g_log_advertise;     /* a warning about ADVERTISE.PRS was logged (the loader thread never logs) */
/* stage rules (j): the warnings about a stage rule's failed stage, on the main thread and elsewhere, and the last one */
static int g_rule_warns, g_rule_warns_away;
static char g_rule_warn[256];
#if defined(_WIN32)
static DWORD g_main_tid;
static int on_main_thread(void) { return GetCurrentThreadId() == g_main_tid; }
static void note_main_thread(void) { g_main_tid = GetCurrentThreadId(); }
#else
#  include <pthread.h>
static pthread_t g_main_tid;
static int on_main_thread(void) { return pthread_equal(pthread_self(), g_main_tid); }
static void note_main_thread(void) { g_main_tid = pthread_self(); }
#endif
static void log_cb(void *user, int32_t level, const char *msg)
{
    (void)user;
    if (level >= SA1_LOG_WARN) {
        printf("  [log %d] %s\n", (int)level, msg);
        if (strstr(msg, "ADVERTISE.PRS"))
            g_log_advertise = 1;
        if (strstr(msg, "stage rule")) {
            if (on_main_thread())
                g_rule_warns++;
            else
                g_rule_warns_away++;
            snprintf(g_rule_warn, sizeof g_rule_warn, "%s", msg);
        }
    }
}

/* ---- waiting for the loader thread ----
 * The mixer never waits for the disc or for decoding: a clip whose file the loader thread has not read yet is silent, a
 * cue whose tone is not decoded yet waits a moment (and is given up after half a second of rendered audio). A test that
 * renders faster than real time must therefore wait for the loader before it checks what it hears, instead of assuming
 * how fast the loader is (it is slower under SA1_GUARD_ALLOC, and with a busy machine). */
#define WAIT_MS 20000.0

/* until the mixer can start `tone` at once (the loader decoded it, or it is decoded and found); 0 after WAIT_MS */
static int wait_tone(sa1_audio *a, uint32_t tone)
{
    double t0 = tu_now_ms();
    for (;;) {
        sa1_status st = SA1_OK;
        int pending = 0;
        if (sa1a_sound_nowait(a, tone, &st, &pending) || !pending)
            return 1;
        if (tu_now_ms() - t0 > WAIT_MS) {
            printf("  tone %u: not decoded after %.0f s\n", (unsigned)tone, WAIT_MS / 1000.0);
            return 0;
        }
        slow_sleep(1);
    }
}

/* until the clip cache holds the file of a voice / music (read by the loader thread); 0 if it failed or after WAIT_MS */
static int wait_clip(sa1_audio *a, int32_t source, uint32_t index)
{
    sa1_status st = SA1_OK;
    double t0 = tu_now_ms();
    int ok = 0;
    int32_t slot = sa1a_clip_get(a, source, index, &st);
    if (slot < 0)
        return 0;
    for (;;) {
        const uint8_t *d = NULL;
        const sa1a_adx *h = NULL;
        uint32_t n = 0;
        int first = 0;
        int32_t r = sa1a_clip_poll(a, slot, &d, &n, &h, &st, &first);
        if (r == SA1A_CLIP_READY) {
            ok = 1;
            break;
        }
        if (r == SA1A_CLIP_FAILED || tu_now_ms() - t0 > WAIT_MS) {
            printf("  %s %u: %s\n", source == SA1_AUDIO_SOURCE_VOICE ? "voice" : "music", (unsigned)index,
                   r == SA1A_CLIP_FAILED ? sa1_status_string(st) : "not read in time");
            break;
        }
        slow_sleep(1);
    }
    sa1a_clip_put(a, slot);
    return ok;
}

static uint32_t g_rng = 2463534242u;
static uint32_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* ---- WAV ---- */
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static int write_wav(const char *path, const int16_t *pcm, uint32_t frames, uint32_t channels, uint32_t rate)
{
    uint8_t h[44];
    uint32_t bytes = frames * channels * 2u, i;
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36u + bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    h[20] = 1;
    h[21] = 0;
    h[22] = (uint8_t)channels;
    h[23] = 0;
    put32(h + 24, rate);
    put32(h + 28, rate * channels * 2u);
    h[32] = (uint8_t)(channels * 2u);
    h[33] = 0;
    h[34] = 16;
    h[35] = 0;
    memcpy(h + 36, "data", 4);
    put32(h + 40, bytes);
    fwrite(h, 1, 44, f);
    for (i = 0; i < frames * channels; i++) {
        uint8_t b[2];
        b[0] = (uint8_t)((uint16_t)pcm[i] & 0xFFu);
        b[1] = (uint8_t)((uint16_t)pcm[i] >> 8);
        fwrite(b, 1, 2, f);
    }
    fclose(f);
    return 0;
}

static double rms16(const int16_t *p, uint32_t n)
{
    double s = 0.0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += (double)p[i] * (double)p[i];
    return n ? sqrt(s / (double)n) : 0.0;
}

/* ---- an independent look at the MLT banks (through the disc layer, not sa1_audio) ---- */
typedef struct ref_split {
    uint32_t lea, lsa, loop, pcms, addr;
    double   rate;
} ref_split;

/* an independent decoder of the split data (YMZ280B / AICA ADPCM as documented, PCM8, PCM16) */
static void ref_decode(const uint8_t *mpb, uint32_t size, const ref_split *r, int16_t *out)
{
    static const int k_diff[16] = { 1, 3, 5, 7, 9, 11, 13, 15, -1, -3, -5, -7, -9, -11, -13, -15 };
    static const int k_scale[8] = { 230, 230, 230, 230, 307, 409, 512, 614 };
    uint32_t i;
    int prev = 0, step = 127;
    for (i = 0; i < r->lea; i++) {
        if (r->pcms == 0) {
            out[i] = r->addr + 2u * i + 1u < size ? (int16_t)(mpb[r->addr + 2u * i] | (mpb[r->addr + 2u * i + 1u] << 8)) : 0;
        } else if (r->pcms == 1) {
            out[i] = r->addr + i < size ? (int16_t)((int8_t)mpb[r->addr + i] * 256) : 0;
        } else {
            int nib = r->addr + i / 2u < size ? (mpb[r->addr + i / 2u] >> ((i & 1u) * 4u)) & 15 : 0;
            int mag = step * (2 * (nib & 7) + 1) / 8;
            if (mag > 32767)
                mag = 32767;
            prev += nib & 8 ? -mag : mag;
            prev = prev < -32768 ? -32768 : (prev > 32767 ? 32767 : prev);
            step = step * k_scale[nib & 7] / 256;
            step = step < 127 ? 127 : (step > 24576 ? 24576 : step);
            out[i] = (int16_t)prev;
            (void)k_diff;
        }
    }
}

static int ref_peak(const int16_t *p, uint32_t n)
{
    int m = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int x = p[i] < 0 ? -p[i] : p[i];
        if (x > m)
            m = x;
    }
    return m;
}

static uint8_t *read_disc_file(sa1_context *ctx, const char *gdi, const char *path, uint32_t *size)
{
    sa1_disc d;
    uint8_t *data = NULL;
    if (sa1_disc_open(ctx, gdi, &d) != SA1_OK)
        return NULL;
    if (sa1_disc_read_file(&d, path, &data, size) != SA1_OK)
        data = NULL;
    sa1_disc_close(&d);
    return data;
}

/* MPB of `bank` inside an MLT: pointer + size, or NULL */
static const uint8_t *ref_mpb(const uint8_t *mlt, uint32_t len, uint32_t bank, uint32_t *size)
{
    uint32_t n, i;
    if (len < 0x20 || memcmp(mlt, "SMLT", 4) != 0)
        return NULL;
    n = sa1_rd32(mlt + 8);
    for (i = 0; i < n && 0x40u + i * 0x20u <= len; i++) {
        const uint8_t *e = mlt + 0x20u + i * 0x20u;
        if (memcmp(e, "SMPB", 4) == 0 && sa1_rd32(e + 4) == bank && sa1_rd32(e + 16) != 0xFFFFFFFFu) {
            *size = sa1_rd32(e + 20);
            return mlt + sa1_rd32(e + 16);
        }
    }
    return NULL;
}

/* the splits of program `p` that contain note 60, in layer order */
static uint32_t ref_program(const uint8_t *m, uint32_t size, uint32_t p, ref_split *out)
{
    uint32_t ptab = sa1_rd32(m + 0x10), n = sa1_rd32(m + 0x14), po, li, cnt = 0;
    if (p >= n)
        return 0;
    po = sa1_rd32(m + ptab + 4u * p);
    if (!po || po + 0x18u > size)
        return 0;
    for (li = 0; li < 4; li++) {
        uint32_t lo = sa1_rd32(m + po + 4u * li), ns, so, s;
        if (!lo)
            continue;
        ns = sa1_rd32(m + lo);
        so = sa1_rd32(m + lo + 4u);
        for (s = 0; s < ns && so + 0x30u * (s + 1u) <= size; s++) {
            const uint8_t *d = m + so + 0x30u * s;
            if (60 < d[0x24] || 60 > d[0x25])
                continue;
            if (sa1_rd16(d + 6) == 0)
                break;
            out[cnt].lea = sa1_rd16(d + 6);
            out[cnt].lsa = sa1_rd16(d + 4);
            out[cnt].loop = (sa1_rd16(d) >> 9) & 1u;
            out[cnt].pcms = (sa1_rd16(d) >> 7) & 3u;
            out[cnt].addr = ((sa1_rd16(d) & 0x7Fu) << 16) | sa1_rd16(d + 2);
            out[cnt].rate = 44100.0 * pow(2.0, ((double)(60 - (int)d[0x26]) + (double)(int8_t)d[0x27] / 256.0) / 12.0);
            cnt++;
            break;
        }
    }
    return cnt;
}

/* ---- mixer helpers ---- */
static int16_t *g_mixbuf;
static uint32_t render_tone(sa1_audio *a, uint32_t tone, int16_t *buf, uint32_t max_frames)
{
    sa1_audio_mixer *m;
    sa1_audio_mixer_desc md;
    sa1_audio_cue c;
    sa1_audio_sound s;
    uint32_t done = 0, hold;
    memset(&md, 0, sizeof md);
    md.struct_size = sizeof md;
    md.sample_rate = 44100;
    if (sa1_audio_mixer_create(a, &md, &m) != SA1_OK)
        return 0;
    s.struct_size = sizeof s;
    sa1_audio_sound_info(a, tone, &s);
    /* the cue must start now: when the loader thread is busy (its preload) the mixer would wait for it and, rendering
       faster than real time, give the cue up (or see nothing playing and stop) before it starts */
    CHECK(wait_tone(a, tone));
    memset(&c, 0, sizeof c);
    c.struct_size = sizeof c;
    c.type = SA1_AUDIO_CUE_PLAY;
    c.character = 1;
    c.tone = tone;
    c.volume = 1.0f;
    c.lifetime = -1.0f;
    sa1_audio_mixer_cue(m, &c);
    hold = s.loops ? 44100u * 2u : max_frames;     /* loops: 2 s, then stop */
    while (done < max_frames) {
        uint32_t n = max_frames - done < 1024u ? max_frames - done : 1024u;
        if (s.loops && done >= hold) {
            c.type = SA1_AUDIO_CUE_STOP;
            sa1_audio_mixer_cue(m, &c);
            hold = UINT32_MAX;
        }
        sa1_audio_mixer_render(m, buf + done * 2u, n);
        done += n;
        if (sa1_audio_mixer_active(m) == 0)
            break;
    }
    sa1_audio_mixer_destroy(m);
    return done;
}

/* ---- tests ---- */
static void check_bank(sa1_context *ctx, sa1_audio *a, const char *gdi, const char *mltname, uint32_t bank,
                       uint32_t first_tone, uint32_t ntones, const char *outdir, FILE *csv)
{
    uint32_t size = 0, msize = 0, p;
    char path[512];
    uint8_t *mlt;
    const uint8_t *mpb;
    snprintf(path, sizeof path, "SONICADV/%s", mltname);
    mlt = read_disc_file(ctx, gdi, path, &size);
    CHECK(mlt != NULL);
    if (!mlt)
        return;
    mpb = ref_mpb(mlt, size, bank, &msize);
    CHECK(mpb != NULL);
    for (p = 0; mpb && p < ntones; p++) {
        uint32_t tone = first_tone + p, nref, k;
        ref_split ref[4];
        sa1_audio_sound s;
        s.struct_size = sizeof s;
        nref = ref_program(mpb, msize, p, ref);
        {
            uint32_t j, keep = 0;
            for (j = 0; j < nref; j++) {
                int16_t *tmp = (int16_t *)malloc((size_t)ref[j].lea * sizeof(int16_t));
                if (tmp) {
                    ref_decode(mpb, msize, &ref[j], tmp);
                    if (ref_peak(tmp, ref[j].lea) >= 64)
                        ref[keep++] = ref[j];
                    free(tmp);
                }
            }
            nref = keep;
        }
        CHECK(sa1_audio_sound_info(a, tone, &s) == SA1_OK);
        CHECK(s.bank == bank);
        CHECK(s.request == p);
        CHECK(s.file != NULL && strcmp(s.file, mltname) == 0);
        if (s.kind == SA1_AUDIO_SOUND_SEQUENCE)
            continue;
        CHECK(s.num_layers == nref);
        CHECK(s.kind == (nref ? SA1_AUDIO_SOUND_PROGRAM : SA1_AUDIO_SOUND_NONE));
        if (csv)
            fprintf(csv, "%u,%d,%u,%u,%s,%u,%.3f,%d", (unsigned)tone, (int)s.kind, (unsigned)s.bank,
                    (unsigned)s.request, s.file ? s.file : "", (unsigned)s.num_layers, (double)s.duration, (int)s.loops);
        for (k = 0; k < s.num_layers && k < nref; k++) {
            sa1_audio_layer l;
            l.struct_size = sizeof l;
            CHECK(sa1_audio_sound_layer(a, tone, k, &l) == SA1_OK);
            CHECK(l.num_samples == ref[k].lea);
            CHECK(l.loop_end == ref[k].lea);
            CHECK(l.loop == (int32_t)(ref[k].loop && ref[k].lsa < ref[k].lea));
            CHECK(!l.loop || l.loop_start == ref[k].lsa);
            CHECK(l.format == ref[k].pcms);
            CHECK(fabs((double)l.sample_rate - ref[k].rate) < 0.5);
            CHECK(l.pcm != NULL);
            if (l.pcm && l.num_samples > 64)
                CHECK(rms16(l.pcm, l.num_samples) > 1.0);
            if (l.pcm && l.num_samples == ref[k].lea) {
                int16_t *tmp = (int16_t *)malloc((size_t)ref[k].lea * sizeof(int16_t));
                if (tmp) {
                    ref_decode(mpb, msize, &ref[k], tmp);
                    CHECK(memcmp(tmp, l.pcm, (size_t)ref[k].lea * sizeof(int16_t)) == 0);
                    free(tmp);
                }
            }
            if (csv)
                fprintf(csv, ",%u:%.0fHz:%s%u:g%.2f:p%.2f", (unsigned)l.num_samples, (double)l.sample_rate,
                        l.loop ? "loop@" : "", (unsigned)l.loop_start, (double)l.gain, (double)l.pan);
            if (outdir && l.pcm) {
                snprintf(path, sizeof path, "%s/layers/%04u_%u.wav", outdir, (unsigned)tone, (unsigned)k);
                write_wav(path, l.pcm, l.num_samples, 1, (uint32_t)(l.sample_rate + 0.5f));
            }
        }
        if (csv)
            fprintf(csv, "\n");
        if (outdir && s.num_layers) {
            uint32_t n = render_tone(a, tone, g_mixbuf, 44100u * 6u);
            double r = rms16(g_mixbuf, n * 2u);
            if (!(n > 0 && r > 1.0)) {
                uint32_t j;
                printf("  tone %u: mixer output rms %.2f over %u frames;", (unsigned)tone, r, (unsigned)n);
                for (j = 0; j < s.num_layers; j++) {
                    sa1_audio_layer l;
                    l.struct_size = sizeof l;
                    sa1_audio_sound_layer(a, tone, j, &l);
                    printf(" [%u: n %u rate %.0f gain %.3f pan %.2f att %.4f d1 %.1f dl %.0f d2 %.1f rel %.0f fl %x]",
                           (unsigned)j, (unsigned)l.num_samples, (double)l.sample_rate, (double)l.gain, (double)l.pan,
                           (double)l.attack, (double)l.decay1, (double)l.decay_level, (double)l.decay2,
                           (double)l.release, (unsigned)l.flags);
                }
                printf("\n");
            }
            CHECK(n > 0 && r > 1.0);
            snprintf(path, sizeof path, "%s/tone_%04u.wav", outdir, (unsigned)tone);
            write_wav(path, g_mixbuf, n, 2, 44100);
        }
    }
    sa1_mem_free(ctx, mlt);
}

static void check_cues(sa1_audio *a)
{
    static const struct { uint32_t type, id, arg; int32_t want; uint32_t flags; } k[] = {
        { SA1_EVENT_SOUND, 723, 0x8C0940DAu, SA1_AUDIO_CUE_PLAY, 0 },
        { SA1_EVENT_SOUND, 34, 0x8C09413Au, SA1_AUDIO_CUE_PLAY, SA1_AUDIO_CUE_POSITIONAL },
        { SA1_EVENT_SOUND, 0x2F9, 0x8C0941FCu, SA1_AUDIO_CUE_PLAY, SA1_AUDIO_CUE_OWNED },
        { SA1_EVENT_SOUND, 0x2D4, 0x8C094330u, SA1_AUDIO_CUE_KEEP, SA1_AUDIO_CUE_OWNED },
        { SA1_EVENT_SOUND, 15, 0x8C0943E0u, SA1_AUDIO_CUE_KEEP, SA1_AUDIO_CUE_OWNED | SA1_AUDIO_CUE_POSITIONAL },
        { SA1_EVENT_SOUND, 1303, 0x8C0940DAu, SA1_AUDIO_CUE_VOICE, 0 },
        { SA1_EVENT_SOUND_STOP, 15, 0x8C0945A0u, SA1_AUDIO_CUE_STOP, 0 },
        { SA1_EVENT_SOUND_STOP, 0, 0x8C0945D0u, SA1_AUDIO_CUE_STOP, 0 },
        { SA1_EVENT_VOICE, 0xA6, 0x8C107BA0u, SA1_AUDIO_CUE_VOICE, 0 },
        { SA1_EVENT_VOICE, 1348, 0x8C04EF22u, SA1_AUDIO_CUE_VOICE, 0 },
        { SA1_EVENT_VOICE, 0x458, 0x8C04EF22u, SA1_AUDIO_CUE_PLAY, 0 },
        { SA1_EVENT_JINGLE, 55, 0x8C107C80u, SA1_AUDIO_CUE_JINGLE, 0 },
        { SA1_EVENT_JINGLE, 10, 0x8C107AC6u, SA1_AUDIO_CUE_MUSIC, 0 },
        { SA1_EVENT_JINGLE, 75, 0x8C04EF52u, SA1_AUDIO_CUE_MUSIC, 0 },
        { SA1_EVENT_JINGLE, 0, 0x8C107B26u, SA1_AUDIO_CUE_MUSIC_RESTORE, 0 },
        { SA1_EVENT_JINGLE, 0, 0x8C107ADAu, SA1_AUDIO_CUE_MUSIC_STOP, 0 },
        { SA1_EVENT_JINGLE, 0, 0x8C107D80u, SA1_AUDIO_CUE_NONE, 0 },
        { SA1_EVENT_RUMBLE, 3, 0x8C109EACu, SA1_AUDIO_CUE_NONE, 0 },
    };
    size_t i;
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        sa1_event ev;
        sa1_audio_cue c;
        memset(&ev, 0, sizeof ev);
        ev.character = 1;
        ev.type = k[i].type;
        ev.id = k[i].id;
        ev.arg = k[i].arg;
        ev.position[0] = 100.0f;
        c.struct_size = sizeof c;
        CHECK(sa1_audio_cue_from_event(a, &ev, &c) == SA1_OK);
        if (c.type != k[i].want || c.flags != k[i].flags)
            printf("FAIL cue %u: event %u id %u arg %08X -> type %d flags %u (want %d %u)\n", (unsigned)i,
                   (unsigned)ev.type, (unsigned)ev.id, (unsigned)ev.arg, (int)c.type, (unsigned)c.flags,
                   (int)k[i].want, (unsigned)k[i].flags);
        CHECK(c.type == k[i].want);
        CHECK(c.flags == k[i].flags);
        if (c.type == SA1_AUDIO_CUE_VOICE && ev.type == SA1_EVENT_SOUND)
            CHECK(c.index == ev.id + 455u);
        if (c.type == SA1_AUDIO_CUE_KEEP)
            CHECK(c.lifetime > 0.0f && c.lifetime < 0.5f);
        if (ev.arg == 0x8C0940DAu && c.type == SA1_AUDIO_CUE_PLAY)
            CHECK(fabs((double)c.lifetime - 2.0) < 1e-4);
        if (ev.arg == 0x8C04EF22u)
            CHECK(c.delay > 0.05f && c.delay < 0.1f);
        CHECK(c.max_distance > c.min_distance && c.min_distance > 0.0f);
    }
}

static void check_mixer(sa1_audio *a, const char *outdir)
{
    sa1_audio_mixer *m;
    sa1_audio_mixer_desc md;
    sa1_event ev;
    const uint32_t rate = 44100, total = rate * 5u;
    int16_t *buf = (int16_t *)calloc((size_t)total * 2u, sizeof(int16_t));
    float lp[3] = { 0, 0, 0 }, fw[3] = { 1, 0, 0 }, up[3] = { 0, 0, 1 }, far_pos[3] = { 0, 0, 0 };
    uint32_t done = 0, tick = 0, active_keep = 0, after_keep = 0;
    char path[512];
    if (!buf)
        return;
    memset(&md, 0, sizeof md);
    md.struct_size = sizeof md;
    md.sample_rate = rate;
    CHECK(sa1_audio_mixer_create(a, &md, &m) == SA1_OK);
    CHECK(sa1_audio_mixer_listener(m, lp, fw, up) == SA1_OK);
    CHECK(sa1_audio_mixer_character(m, 1, lp, 1) == SA1_OK);
    /* the script renders faster than real time: its tones decoded first (the voice's file is waited for below) */
    CHECK(wait_tone(a, 723) && wait_tone(a, 724) && wait_tone(a, 34));
    memset(&ev, 0, sizeof ev);
    ev.character = 1;
    /* script at 30 Hz ticks: t=0 one-shot 723; t=0.5..1.5 s keep-alive 724 (refreshed per tick), then nothing
       (times out); t=2 voice 1730 (tone 1275); t=3.5 positional tone 34 far away (attenuated) */
    while (done < total) {
        uint32_t n = rate / 30u;
        double t = (double)tick / 30.0;
        if (tick == 0) {
            ev.type = SA1_EVENT_SOUND;
            ev.id = 723;
            ev.arg = 0x8C0940DAu;
            CHECK(sa1_audio_mixer_event(m, &ev) == SA1_OK);
        }
        if (t >= 0.5 && t < 1.5) {
            ev.type = SA1_EVENT_SOUND;
            ev.id = 724;
            ev.arg = 0x8C094330u;
            CHECK(sa1_audio_mixer_event(m, &ev) == SA1_OK);
        }
        if (tick == 60) {
            ev.type = SA1_EVENT_SOUND;
            ev.id = 1275;
            ev.arg = 0x8C0940DAu;
            CHECK(sa1_audio_mixer_event(m, &ev) == SA1_OK);
            /* the voice is silent until the loader thread has read its file: wait for it (a real-time host hears
               it a few milliseconds late) */
            CHECK(wait_clip(a, SA1_AUDIO_SOURCE_VOICE, 1730));
        }
        if (tick == 105) {
            ev.type = SA1_EVENT_SOUND;
            ev.id = 34;
            ev.arg = 0x8C09413Au;
            far_pos[0] = 200.0f * 4.0f;     /* 200 SA1 units in the default (Source) scale */
            memcpy(ev.position, far_pos, sizeof far_pos);
            CHECK(sa1_audio_mixer_event(m, &ev) == SA1_OK);
            memset(ev.position, 0, sizeof ev.position);
        }
        if (done + n > total)
            n = total - done;
        CHECK(sa1_audio_mixer_render(m, buf + done * 2u, n) == SA1_OK);
        done += n;
        if (tick == 40)
            active_keep = sa1_audio_mixer_active(m);
        if (tick == 52)
            after_keep = sa1_audio_mixer_active(m);
        tick++;
    }
    CHECK(active_keep > 0);
    CHECK(after_keep < active_keep);            /* the keep-alive sound timed out after the refreshes stopped */
    CHECK(rms16(buf, rate * 2u) > 10.0);         /* first second */
    CHECK(rms16(buf + rate * 2u * 2u, rate * 2u) > 10.0); /* the voice */
    printf("  mixer: active %u during keep-alive, %u after; rms %.0f / %.0f\n", (unsigned)active_keep,
           (unsigned)after_keep, rms16(buf, rate * 2u), rms16(buf + rate * 4u, rate * 2u));
    if (outdir) {
        snprintf(path, sizeof path, "%s/mixer_script.wav", outdir);
        write_wav(path, buf, total, 2, rate);
    }
    /* stop everything */
    CHECK(sa1_audio_mixer_stop(m, 0) == SA1_OK);
    CHECK(sa1_audio_mixer_active(m) == 0);
    /* bad arguments */
    CHECK(sa1_audio_mixer_volume(m, 7, 1.0f) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_volume(m, SA1_AUDIO_VOL_SFX, -1.0f) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_character(m, 0, lp, 1) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_flags(m, 0x100u) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_flags(m, SA1_AUDIO_MIX_NO_VOICES | SA1_AUDIO_MIX_3D_VOICES) == SA1_OK);
    {
        sa1_audio_cue vc;
        memset(&vc, 0, sizeof vc);
        vc.struct_size = sizeof vc;
        vc.type = SA1_AUDIO_CUE_VOICE;
        vc.character = 1;
        vc.index = 1730;
        CHECK(sa1_audio_mixer_cue(m, &vc) == SA1_OK);
        CHECK(sa1_audio_mixer_active(m) == 0);              /* voices are off */
        CHECK(sa1_audio_mixer_flags(m, 0) == SA1_OK);
        CHECK(sa1_audio_mixer_cue(m, &vc) == SA1_OK);
        CHECK(sa1_audio_mixer_active(m) == 1);
        CHECK(sa1_audio_mixer_flags(m, SA1_AUDIO_MIX_NO_VOICES) == SA1_OK);
        CHECK(sa1_audio_mixer_active(m) == 0);
    }
    sa1_audio_mixer_destroy(m);
    free(buf);
}

static void check_streams(sa1_audio *a, const char *outdir)
{
    static const uint32_t voices[] = { 0, 0xA6, 1730, 1734, 1758, 1803 };
    static const uint32_t music[] = { 71, 85 };
    size_t i;
    char path[512];
    CHECK(sa1_audio_voice_count(a) == 2044u);
    CHECK(sa1_audio_music_count(a) == 110u);
    CHECK(sa1_audio_music_name(a, 55) && strcmp(sa1_audio_music_name(a, 55), "INVNCIBL.ADX") == 0);
    CHECK(sa1_audio_music_name(a, 110) == NULL);
    for (i = 0; i < sizeof voices / sizeof voices[0] + sizeof music / sizeof music[0]; i++) {
        int is_voice = i < sizeof voices / sizeof voices[0];
        uint32_t idx = is_voice ? voices[i] : music[i - sizeof voices / sizeof voices[0]];
        sa1_audio_stream *s = NULL;
        sa1_audio_stream_info info;
        int16_t *pcm;
        uint32_t got = 0, more = 0;
        info.struct_size = sizeof info;
        CHECK(sa1_audio_stream_open(a, is_voice ? SA1_AUDIO_SOURCE_VOICE : SA1_AUDIO_SOURCE_MUSIC, idx, &s, &info) ==
              SA1_OK);
        if (!s)
            continue;
        CHECK(info.channels == (is_voice ? 1u : 2u));
        CHECK(info.num_frames > 1000u);
        pcm = (int16_t *)malloc((size_t)(info.num_frames + 64u) * info.channels * sizeof(int16_t));
        if (pcm) {
            CHECK(sa1_audio_stream_read(s, pcm, info.num_frames + 64u, 0, &got) == SA1_OK);
            CHECK(got == info.num_frames);
            CHECK(sa1_audio_stream_read(s, pcm, 16, 0, &more) == SA1_OK && more == 0);
            CHECK(rms16(pcm, got * info.channels) > 10.0);
            /* seek back and decode again: identical */
            if (got > 5000u) {
                int16_t again[64];
                uint32_t g2 = 0;
                CHECK(sa1_audio_stream_seek(s, 4000) == SA1_OK);
                CHECK(sa1_audio_stream_read(s, again, 32u / info.channels, 0, &g2) == SA1_OK);
                CHECK(memcmp(again, pcm + 4000u * info.channels, (size_t)g2 * info.channels * 2u) == 0);
            }
            printf("  %s %u: %u frames, %u Hz, %u ch%s\n", is_voice ? "voice" : "music", (unsigned)idx,
                   (unsigned)got, (unsigned)info.sample_rate, (unsigned)info.channels, info.loop ? ", loop" : "");
            if (outdir) {
                if (is_voice)
                    snprintf(path, sizeof path, "%s/voice_%04u.wav", outdir, (unsigned)idx);
                else
                    snprintf(path, sizeof path, "%s/music_%s.wav", outdir, sa1_audio_music_name(a, idx));
                write_wav(path, pcm, got, info.channels, info.sample_rate);
            }
            free(pcm);
        }
        sa1_audio_stream_close(s);
    }
    {
        sa1_audio_stream *s = NULL;
        CHECK(sa1_audio_stream_open(a, SA1_AUDIO_SOURCE_VOICE, 99999u, &s, NULL) == SA1_ERR_NOT_FOUND);
        CHECK(sa1_audio_stream_open(a, 7, 0, &s, NULL) == SA1_ERR_INVALID_ARGUMENT);
    }
}

static void check_sequences(sa1_audio *a, const char *outdir)
{
    uint32_t t;
    char path[512];
    for (t = 777; t <= 781; t++) {
        sa1_audio_sound s;
        sa1_audio_layer l;
        s.struct_size = sizeof s;
        l.struct_size = sizeof l;
        CHECK(sa1_audio_sound_info(a, t, &s) == SA1_OK);
        CHECK(s.kind == SA1_AUDIO_SOUND_SEQUENCE);
        CHECK(s.file && strcmp(s.file, "P_E102.MLT") == 0);
        if (s.kind != SA1_AUDIO_SOUND_SEQUENCE)
            continue;
        CHECK(sa1_audio_sound_layer(a, t, 0, &l) == SA1_OK);
        CHECK(l.num_samples > 4410u && l.format == SA1_AUDIO_FORMAT_RENDERED);
        CHECK(rms16(l.pcm, l.num_samples) > 10.0);
        printf("  sequence tone %u (E-102 request %u): %.2f s\n", (unsigned)t, (unsigned)s.request,
               (double)l.num_samples / 44100.0);
        if (outdir) {
            snprintf(path, sizeof path, "%s/tone_%04u.wav", outdir, (unsigned)t);
            write_wav(path, l.pcm, l.num_samples, 1, 44100);
        }
    }
}

static void check_robustness(sa1_audio *a)
{
    uint8_t buf[4096];
    int16_t pcm[2048];
    uint32_t run, i;
    /* the loader thread decodes tones from the banks corrupted below: let it finish, and keep it out meanwhile */
    CHECK(sa1_audio_preload(a, SA1_AUDIO_PRELOAD_ALL | SA1_AUDIO_PRELOAD_WAIT) == SA1_OK);
    sa1a_tone_lock(a);
    /* random ADX headers and data */
    for (run = 0; run < 2000; run++) {
        sa1a_adx h;
        uint32_t len = 16u + rnd() % (sizeof buf - 16u);
        for (i = 0; i < len; i++)
            buf[i] = (uint8_t)rnd();
        if (run & 1) {                          /* mostly-valid header */
            buf[0] = 0x80;
            buf[1] = 0;
            buf[2] = 0;
            buf[3] = 0x20;
            memcpy(buf + 0x1E, "(c)CRI", 6);
            buf[4] = 3;
            buf[5] = 18;
            buf[6] = 4;
            buf[7] = (uint8_t)(1u + (rnd() & 1u));
        }
        if (sa1a_adx_parse(buf, len, &h) == SA1_OK) {
            sa1a_adx_dec dec;
            sa1a_adx_dec_init(&dec, buf, len, &h);
            (void)sa1a_adx_dec_read(&dec, pcm, 1024u / h.channels, 1);
            sa1a_adx_dec_seek(&dec, rnd() % (h.frames + 1u));
            (void)sa1a_adx_dec_read(&dec, pcm, 1024u / h.channels, 1);
        }
    }
    for (run = 0; run < 200; run++) {
        for (i = 0; i < 1024; i++)
            buf[i] = (uint8_t)rnd();
        sa1a_adpcm_decode(buf, 2048, pcm);
    }
    g_pass++;
    /* corrupted copies of a real player bank: programs and sequences must fail cleanly */
    {
        sa1a_bank *b = &a->player[3];               /* E102: layered programs and sequences */
        uint8_t *mpb0, *msb0;
        uint32_t ms = b->mpb_size, ss = b->msb_size;
        if (!b->loaded || !b->msb)
            return;
        mpb0 = (uint8_t *)malloc(ms);
        msb0 = (uint8_t *)malloc(ss);
        if (!mpb0 || !msb0) {
            free(mpb0);
            free(msb0);
            return;
        }
        memcpy(mpb0, b->mpb, ms);
        memcpy(msb0, b->msb, ss);
        for (run = 0; run < 300; run++) {
            sa1a_layer layers[SA1A_MAX_LAYERS];
            int16_t *out = NULL;
            uint32_t cnt = 0, k, nflip = 1u + rnd() % 64u, p;
            memcpy(b->mpb, mpb0, ms);
            memcpy(b->msb, msb0, ss);
            for (k = 0; k < nflip; k++) {
                if (rnd() & 1u)
                    b->mpb[rnd() % (ms < 0x800u ? ms : 0x800u)] = (uint8_t)rnd();   /* headers and splits */
                else
                    b->msb[rnd() % ss] = (uint8_t)rnd();
            }
            for (p = 0; p < 40; p++)
                (void)sa1a_program_layers(a, b, p, (int32_t)(rnd() % 128u), layers);
            /* bank_pcm caches by address: drop the cache so corrupted splits decode fresh */
            for (k = 0; k < b->pcm.count; k++)
                sa1_mem_free(a->ctx, SA1_VEC(b->pcm, sa1a_pcm)[k].pcm);
            b->pcm.count = 0;
            if (sa1a_render_sequence(a, b, rnd() % (b->num_sequences + 2u), &out, &cnt) == SA1_OK && out)
                sa1_mem_free(a->ctx, out);
        }
        memcpy(b->mpb, mpb0, ms);
        memcpy(b->msb, msb0, ss);
        free(mpb0);
        free(msb0);
        g_pass++;
    }
    sa1a_tone_unlock(a);
}

/* ---- the music list (header version 3) ---- */

static double rms_frames(const int16_t *p, uint32_t frames) { return rms16(p, frames * 2u); }

/* renders `frames` stereo frames into buf in pieces of 1/100 s */
static void render_n(sa1_audio_mixer *m, int16_t *buf, uint32_t frames)
{
    uint32_t done = 0;
    while (done < frames) {
        uint32_t n = frames - done < 441u ? frames - done : 441u;
        sa1_audio_mixer_render(m, buf + done * 2u, n);
        done += n;
    }
}

/* the music mixer on the session-like object: MUSIC cues for a file that is not on the disc, the host duck */
static void check_music_mixer(sa1_audio *a)
{
    sa1_audio_mixer *m = NULL, *ref = NULL;
    sa1_audio_mixer_desc md;
    sa1_audio_cue c;
    sa1_audio_stats s0, s1;
    sa1_audio_music mi;
    const uint32_t rate = 44100;
    int16_t *buf = (int16_t *)calloc((size_t)rate * 2u, sizeof(int16_t));
    int16_t *buf2 = (int16_t *)calloc((size_t)rate * 2u, sizeof(int16_t));
    double r_music, r_ramp, r_ducked, r_jingle, r_jducked, r_back, r_stop;
    if (!buf || !buf2) {
        CHECK(0);
        free(buf);
        free(buf2);
        return;
    }
    memset(&md, 0, sizeof md);
    md.struct_size = sizeof md;
    md.sample_rate = rate;
    md.flags = SA1_AUDIO_MIX_MUSIC;
    CHECK(sa1_audio_mixer_create(a, &md, &m) == SA1_OK);
    CHECK(sa1_audio_mixer_create(a, &md, &ref) == SA1_OK);
    if (!m || !ref)
        goto out;
    memset(&c, 0, sizeof c);
    c.struct_size = sizeof c;
    c.character = 1;
    c.volume = 1.0f;
    /* the info works on a full object too */
    mi.struct_size = sizeof mi;
    CHECK(sa1_audio_music_info(a, 96, &mi) == SA1_OK && (mi.flags & SA1_AUDIO_MUSIC_JINGLE) &&
          (mi.flags & SA1_AUDIO_MUSIC_ON_DISC) && mi.sample_rate == 32000u && strcmp(mi.file, "TIMER.ADX") == 0);
    /* background music 28 (ECOAST1.ADX) */
    c.type = SA1_AUDIO_CUE_MUSIC;
    c.index = 28;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    CHECK(wait_clip(a, SA1_AUDIO_SOURCE_MUSIC, 28));
    render_n(m, buf, rate / 2u);
    r_music = rms_frames(buf + rate / 4u * 2u, rate / 4u);
    /* the duck ramps it down (not a jump) to silence; it keeps playing */
    CHECK(sa1_audio_mixer_duck(m, 0.0f, 0) == SA1_OK);
    render_n(m, buf, rate / 20u);
    r_ramp = rms_frames(buf, rate / 100u);
    render_n(m, buf, rate * 6u / 10u);
    r_ducked = rms_frames(buf + rate / 2u * 2u, rate / 10u);
    CHECK(r_music > 100.0 && r_ramp > 10.0 && r_ducked == 0.0 && sa1_audio_mixer_active(m) == 1u);
    CHECK(sa1_audio_mixer_duck(m, 1.5f, 0) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_duck(m, -0.1f, 0) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_duck(m, (float)sqrt(-1.0), 0) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_duck(m, 0.0f, 0x100u) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_duck(NULL, 0.0f, 0) == SA1_ERR_INVALID_ARGUMENT);
    /* a jingle over the ducked music plays at its full level: sample for sample what an unducked mixer plays */
    c.type = SA1_AUDIO_CUE_JINGLE;
    c.index = 85;                                       /* SPEEDUP.ADX, 15.8 s */
    CHECK(wait_clip(a, SA1_AUDIO_SOURCE_MUSIC, 85));
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    CHECK(sa1_audio_mixer_cue(ref, &c) == SA1_OK);
    render_n(m, buf, rate);
    render_n(ref, buf2, rate);
    r_jingle = rms_frames(buf, rate);
    CHECK(r_jingle > 100.0 && memcmp(buf, buf2, (size_t)rate * 2u * sizeof(int16_t)) == 0);
    /* SA1_AUDIO_DUCK_JINGLES ducks the jingle too; DUCK_NOW brings it back at once */
    CHECK(sa1_audio_mixer_duck(m, 0.0f, SA1_AUDIO_DUCK_JINGLES) == SA1_OK);
    render_n(m, buf, rate * 6u / 10u);
    r_jducked = rms_frames(buf + rate / 2u * 2u, rate / 10u);
    CHECK(sa1_audio_mixer_duck(m, 1.0f, SA1_AUDIO_DUCK_NOW) == SA1_OK);
    render_n(m, buf, rate / 100u);
    r_back = rms_frames(buf, rate / 100u);
    CHECK(r_jducked == 0.0 && r_back > 10.0);
    /* the jingle ends (RESTORE): the background music, unducked, is heard again */
    c.type = SA1_AUDIO_CUE_MUSIC_RESTORE;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    render_n(m, buf, rate / 2u);
    CHECK(rms_frames(buf + rate / 4u * 2u, rate / 4u) > 100.0);
    /* a MUSIC cue for 69 (NIGHTS_K.ADX, not on the disc): SA1_ERR_NOT_FOUND at once, no failed read, the music stops
       (8C10792A's ADXT_StartFname stops the old track before it fails to open the new one) */
    s0.struct_size = sizeof s0;
    s1.struct_size = sizeof s1;
    CHECK(sa1_audio_get_stats(a, &s0) == SA1_OK);
    c.type = SA1_AUDIO_CUE_MUSIC;
    c.index = 69;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_ERR_NOT_FOUND);
    render_n(m, buf, rate / 5u);
    r_stop = rms_frames(buf + rate / 10u * 2u, rate / 10u);
    slow_sleep(50);                                     /* the loader thread would have failed by now */
    CHECK(sa1_audio_get_stats(a, &s1) == SA1_OK);
    CHECK(s1.read_errors == s0.read_errors && s1.clips_waiting == 0u);
    CHECK(r_stop == 0.0 && sa1_audio_mixer_active(m) == 0u);
    c.type = SA1_AUDIO_CUE_JINGLE;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_ERR_NOT_FOUND);
    CHECK(sa1_audio_mixer_active(m) == 0u);
    /* without SA1_AUDIO_MIX_MUSIC the cue is only remembered, and still says the file is missing */
    CHECK(sa1_audio_mixer_flags(m, 0) == SA1_OK);
    c.type = SA1_AUDIO_CUE_MUSIC;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_ERR_NOT_FOUND);
    c.index = 28;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK && sa1_audio_mixer_active(m) == 0u);
    CHECK(sa1_audio_get_stats(a, &s1) == SA1_OK && s1.read_errors == s0.read_errors);
    printf("  music mixer: music rms %.0f, ducking %.0f -> %.0f, jingle over it %.0f (= the unducked mixer's), "
           "ducked %.0f, back %.0f; cue for 69: NOT_FOUND, %u failed reads, then rms %.0f\n", r_music, r_ramp, r_ducked,
           r_jingle, r_jducked, r_back, (unsigned)(s1.read_errors - s0.read_errors), r_stop);
out:
    sa1_audio_mixer_destroy(m);
    sa1_audio_mixer_destroy(ref);
    free(buf);
    free(buf2);
}

/* ---- the game's music state (header version 4) ---- */

static sa1_audio_music_state mstate(const sa1_audio_mixer *m)
{
    sa1_audio_music_state s;
    memset(&s, 0xA5, sizeof s);
    s.struct_size = sizeof s;
    if (sa1_audio_mixer_music_state(m, &s) != SA1_OK) {
        CHECK(0);
        memset(&s, 0, sizeof s);
        s.jingle = -2;
    }
    return s;
}

/* renders `seconds` of a mixer into buf in pieces of at most `rate` frames (buf holds 2 * rate samples) */
static void render_secs(sa1_audio_mixer *m, int16_t *buf, uint32_t rate, double seconds)
{
    uint32_t total = (uint32_t)(seconds * (double)rate + 0.5), done = 0;
    while (done < total) {
        uint32_t n = total - done < rate ? total - done : rate;
        render_n(m, buf, n);
        done += n;
    }
}

static int close_to(double v, double want, double tol) { return fabs(v - want) <= tol; }

/* one game music call as the world reports it (SA1_EVENT_JINGLE, arg = the DC function) through the mixer */
static sa1_status music_event(sa1_audio_mixer *m, uint32_t id, uint32_t fn)
{
    sa1_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = SA1_EVENT_JINGLE;
    ev.character = 1;
    ev.id = id;
    ev.arg = fn;
    return sa1_audio_mixer_event(m, &ev);
}

/* sa1_audio_mixer_music_state (gsa1_070_drown.md A1 and its review) on a mixer with G-SA1's default flags (jingles, no
 * background music) and on one that plays the background music:
 *   - the drowning chain frame by frame as the game sends it (drown doc section 1: JINGLE 96 in steps 1082 and 1083,
 *     the drown's MUSIC_STOP in 1862, the countdown destructor's MUSIC_RESTORE in 2103): 96 reported with its length
 *     from its first frame, restarted once, ended by the stop, `stopped` until the RESTORE, bgm -1 throughout;
 *   - 96 alone is over within 16 s (PLAYED); the kinds, lengths and loops of the game's eight jingles (only 45
 *     FISHING.ADX loops, and it plays on past its length); 85 then RESTORE is silent within 0.05 s; a MUSIC cue ends a
 *     jingle; the music stop and what clears it; a delayed cue; a file that is not on the disc;
 *   - the 1-up (tone 704, a sound effect) is not a jingle and does not end one;
 *   - SA1_AUDIO_MIX_NO_JINGLES: -1 always; the host's stop; bgm_playing; struct_size; arguments. */
static void check_music_state(sa1_audio *a)
{
    static const uint32_t k_game[8] = { 26, 44, 45, 46, 55, 56, 85, 96 };
    static const int32_t k_kind[8] = { SA1_AUDIO_JINGLE_CONTINUE, SA1_AUDIO_JINGLE_FISH_CAUGHT, SA1_AUDIO_JINGLE_FISHING,
                                       SA1_AUDIO_JINGLE_FISH_LOST, SA1_AUDIO_JINGLE_INVINCIBLE, SA1_AUDIO_JINGLE_ITEM,
                                       SA1_AUDIO_JINGLE_SPEED_UP, SA1_AUDIO_JINGLE_COUNTDOWN };
    const uint32_t rate = 44100, frame = rate / 60u;   /* 735 output frames: one game frame */
    sa1_audio_mixer *m = NULL, *mm = NULL;
    sa1_audio_mixer_desc md;
    sa1_audio_music_state s, t;
    sa1_audio_music mi;
    sa1_audio_sound snd;
    sa1_audio_cue c, q;
    sa1_event ev;
    double len[8], L45, L55, L85, L96;
    double r_count = 0.0, r_drown = 0.0, r_out = -1.0, r_jingle = -1.0, r_restore = -1.0, r_music = -1.0;
    double r_loop = -1.0, r_off = -1.0, r_1up = -1.0;
    uint32_t i, k, kinds_ok = 0, n;
    int16_t *buf = (int16_t *)calloc((size_t)rate * 2u, sizeof(int16_t));
    memset(&md, 0, sizeof md);
    md.struct_size = sizeof md;
    md.sample_rate = rate;                              /* flags 0: G-SA1's default */
    CHECK(buf && sa1_audio_mixer_create(a, &md, &m) == SA1_OK);
    md.flags = SA1_AUDIO_MIX_MUSIC;
    CHECK(sa1_audio_mixer_create(a, &md, &mm) == SA1_OK);
    if (!buf || !m || !mm)
        goto out;
    memset(&c, 0, sizeof c);
    c.struct_size = sizeof c;
    c.character = 1;
    c.volume = 1.0f;
    /* the game's eight jingles: kind, length, loop (only 45 has one); 71 ONE_UP.ADX is not a jingle */
    for (i = 0; i < 8u; i++) {
        memset(&mi, 0, sizeof mi);
        mi.struct_size = sizeof mi;
        len[i] = 0.0;
        if (sa1_audio_music_info(a, k_game[i], &mi) == SA1_OK && mi.sample_rate) {
            len[i] = (double)mi.num_frames / (double)mi.sample_rate;
            kinds_ok += (mi.flags & SA1_AUDIO_MUSIC_JINGLE) && (mi.loop != 0) == (k_game[i] == 45u) &&
                        sa1a_jingle_kind(k_game[i]) == k_kind[i];
        }
    }
    L45 = len[2];
    L55 = len[4];
    L85 = len[6];
    L96 = len[7];
    CHECK(kinds_ok == 8u && close_to(L96, 15.23, 0.01) && close_to(L55, 23.50, 0.01) && close_to(L85, 15.78, 0.01) &&
          close_to(L45, 50.14, 0.01));
    memset(&mi, 0, sizeof mi);
    mi.struct_size = sizeof mi;
    CHECK(sa1_audio_music_info(a, 71, &mi) == SA1_OK && !(mi.flags & SA1_AUDIO_MUSIC_JINGLE) &&
          sa1a_jingle_kind(71) == SA1_AUDIO_JINGLE_OTHER && sa1a_jingle_kind(28) == SA1_AUDIO_JINGLE_OTHER);

    /* arguments; a new mixer */
    CHECK(sa1_audio_mixer_music_state(NULL, &s) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_music_state(m, NULL) == SA1_ERR_INVALID_ARGUMENT);
    s.struct_size = 0;
    CHECK(sa1_audio_mixer_music_state(m, &s) == SA1_ERR_INVALID_ARGUMENT);
    s = mstate(m);
    CHECK(s.struct_size == sizeof s && s.jingle == -1 && s.jingle_left == 0.0f && s.stopped == 0 && s.bgm == -1 &&
          s.jingle_kind == SA1_AUDIO_JINGLE_NONE && s.jingle_waiting == 0 && s.jingle_loops == 0 &&
          s.jingle_length == 0.0f && s.jingle_time == 0.0f && s.jingles_started == 0u && s.jingles_ended == 0u &&
          s.last_jingle == -1 && s.last_end == SA1_AUDIO_JINGLE_END_NONE && s.bgm_playing == 0);
    /* a smaller struct (the first five fields) gets what fits; the rest is left alone */
    memset(&t, 0x5A, sizeof t);
    t.struct_size = (uint32_t)offsetof(sa1_audio_music_state, jingle_kind);
    CHECK(sa1_audio_mixer_music_state(m, &t) == SA1_OK &&
          t.struct_size == (uint32_t)offsetof(sa1_audio_music_state, jingle_kind) && t.jingle == -1 && t.bgm == -1 &&
          t.stopped == 0 && t.jingle_left == 0.0f && t.jingle_kind == 0x5A5A5A5A && t.bgm_playing == 0x5A5A5A5A);

    /* the drowning chain frame by frame (k = step - 1082) */
    CHECK(wait_clip(a, SA1_AUDIO_SOURCE_MUSIC, 96));
    for (k = 0; k <= 1022u; k++) {
        if (k <= 1u)
            CHECK(music_event(m, 96, 0x8C107C80u) == SA1_OK);   /* 8C07BE6E, task frames 0 and 1 */
        else if (k == 780u)
            CHECK(music_event(m, 0, 0x8C107ADAu) == SA1_OK);    /* the drown: the music stops */
        else if (k == 1021u)
            CHECK(music_event(m, 0, 0x8C107B26u) == SA1_OK);    /* the destructor, one frame after RESTART */
        s = mstate(m);
        if (k == 0u)
            CHECK(s.jingle == 96 && s.jingle_kind == SA1_AUDIO_JINGLE_COUNTDOWN && !s.jingle_waiting && !s.jingle_loops &&
                  close_to(s.jingle_length, L96, 0.001) && close_to(s.jingle_left, L96, 0.001) && s.jingle_time == 0.0f &&
                  s.jingles_started == 1u && s.jingles_ended == 0u && !s.stopped && s.bgm == -1);
        else if (k == 1u)
            CHECK(s.jingle == 96 && s.jingles_started == 2u && s.jingles_ended == 1u && s.last_jingle == 96 &&
                  s.last_end == SA1_AUDIO_JINGLE_END_REPLACED && close_to(s.jingle_left, L96, 0.001));
        else if (k == 779u)
            CHECK(s.jingle == 96 && close_to(s.jingle_time, 778.0 / 60.0, 0.002) &&
                  close_to(s.jingle_left, L96 - 778.0 / 60.0, 0.002) && !s.stopped && s.jingles_ended == 1u);
        else if (k == 780u)
            CHECK(s.jingle == -1 && s.jingle_left == 0.0f && s.jingle_kind == SA1_AUDIO_JINGLE_NONE &&
                  s.last_end == SA1_AUDIO_JINGLE_END_STOP && s.stopped == 1 && s.bgm == -1 && s.jingles_started == 2u &&
                  s.jingles_ended == 2u);
        else if (k == 1020u)
            CHECK(s.jingle == -1 && s.stopped == 1);
        else if (k == 1021u)
            CHECK(s.jingle == -1 && s.stopped == 0 && s.bgm == -1 && s.last_end == SA1_AUDIO_JINGLE_END_STOP &&
                  s.jingles_ended == 2u);
        render_n(m, buf, frame);
        if (k >= 300u && k < 360u)
            r_count += rms_frames(buf, frame) / 60.0;
        if (k >= 784u && k < 1020u)
            r_drown = fmax(r_drown, rms_frames(buf, frame));
    }
    CHECK(r_count > 100.0 && r_drown == 0.0);

    /* 96 alone is over within 16 s (A1), ended as PLAYED; silence after it */
    c.type = SA1_AUDIO_CUE_JINGLE;
    c.index = 96;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.jingle == 96 && close_to(s.jingle_left, L96, 0.001) && s.jingles_started == 3u && !s.stopped);
    render_secs(m, buf, rate, 15.0);
    s = mstate(m);
    CHECK(s.jingle == 96 && close_to(s.jingle_left, L96 - 15.0, 0.002) && close_to(s.jingle_time, 15.0, 0.002));
    render_secs(m, buf, rate, 1.0);
    s = mstate(m);
    r_out = rms_frames(buf + (rate - rate / 10u) * 2u, rate / 10u);
    CHECK(s.jingle == -1 && s.last_jingle == 96 && s.last_end == SA1_AUDIO_JINGLE_END_PLAYED &&
          s.jingles_ended == s.jingles_started && r_out == 0.0);

    /* a MUSIC cue (only remembered here: this mixer does not play the background music) sets bgm; a jingle leaves it
       alone; the next MUSIC cue ends the jingle (8C10792A's ADXT_StartFname stops the current track) */
    c.type = SA1_AUDIO_CUE_MUSIC;
    c.index = 28;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.bgm == 28 && !s.bgm_playing && !s.stopped && s.jingle == -1);
    CHECK(wait_clip(a, SA1_AUDIO_SOURCE_MUSIC, 55));
    c.type = SA1_AUDIO_CUE_JINGLE;
    c.index = 55;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    render_n(m, buf, rate / 2u);
    s = mstate(m);
    CHECK(s.jingle == 55 && s.jingle_kind == SA1_AUDIO_JINGLE_INVINCIBLE && close_to(s.jingle_length, L55, 0.001) &&
          close_to(s.jingle_left, L55 - 0.5, 0.002) && s.bgm == 28);
    c.type = SA1_AUDIO_CUE_MUSIC;
    c.index = 28;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    render_n(m, buf, rate / 10u);
    r_music = rms_frames(buf + rate / 20u * 2u, rate / 20u);
    CHECK(s.jingle == -1 && s.last_jingle == 55 && s.last_end == SA1_AUDIO_JINGLE_END_MUSIC && s.bgm == 28 &&
          r_music == 0.0);

    /* 85 then RESTORE: -1 at once and silent within 0.05 s (the fade); the saved track stays */
    CHECK(wait_clip(a, SA1_AUDIO_SOURCE_MUSIC, 85));
    c.type = SA1_AUDIO_CUE_JINGLE;
    c.index = 85;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.jingle == 85 && s.jingle_kind == SA1_AUDIO_JINGLE_SPEED_UP && close_to(s.jingle_length, L85, 0.001) &&
          close_to(s.jingle_left, L85, 0.001));
    render_n(m, buf, rate / 2u);
    r_jingle = rms_frames(buf + rate / 4u * 2u, rate / 4u);
    c.type = SA1_AUDIO_CUE_MUSIC_RESTORE;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.jingle == -1 && s.last_jingle == 85 && s.last_end == SA1_AUDIO_JINGLE_END_RESTORE && s.bgm == 28 &&
          !s.stopped);
    render_n(m, buf, rate / 10u);
    r_restore = rms_frames(buf + rate / 20u * 2u, rate / 20u);
    CHECK(r_jingle > 100.0 && r_restore == 0.0);

    /* the music stop: `stopped` until MUSIC, JINGLE or RESTORE (A1); the saved track is forgotten (bgm -1) and a
       RESTORE after it brings none back (8C107B26 without a saved track) */
    c.type = SA1_AUDIO_CUE_MUSIC_STOP;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.stopped == 1 && s.bgm == -1 && s.jingle == -1);
    c.type = SA1_AUDIO_CUE_MUSIC;
    c.index = 28;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.stopped == 0 && s.bgm == 28);
    c.type = SA1_AUDIO_CUE_MUSIC_STOP;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    CHECK(mstate(m).stopped == 1);
    c.type = SA1_AUDIO_CUE_JINGLE;
    c.index = 85;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.stopped == 0 && s.jingle == 85 && s.bgm == -1);
    c.type = SA1_AUDIO_CUE_MUSIC_STOP;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.stopped == 1 && s.jingle == -1 && s.last_end == SA1_AUDIO_JINGLE_END_STOP);
    c.type = SA1_AUDIO_CUE_MUSIC_RESTORE;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.stopped == 0 && s.bgm == -1 && s.jingle == -1 && s.last_end == SA1_AUDIO_JINGLE_END_STOP);

    /* 45 FISHING.ADX loops like the game's ADXT: still playing past its length, until the game ends it */
    CHECK(wait_clip(a, SA1_AUDIO_SOURCE_MUSIC, 45));
    c.type = SA1_AUDIO_CUE_JINGLE;
    c.index = 45;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.jingle == 45 && s.jingle_kind == SA1_AUDIO_JINGLE_FISHING && s.jingle_loops == 1 && s.jingle_left == -1.0f &&
          close_to(s.jingle_length, L45, 0.001));
    render_secs(m, buf, rate, L45 + 1.0);
    s = mstate(m);
    render_n(m, buf, rate / 10u);
    r_loop = rms_frames(buf, rate / 10u);
    CHECK(s.jingle == 45 && s.jingle_left == -1.0f && close_to(s.jingle_time, L45 + 1.0, 0.002) && r_loop > 10.0);
    c.type = SA1_AUDIO_CUE_MUSIC_RESTORE;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.jingle == -1 && s.last_jingle == 45 && s.last_end == SA1_AUDIO_JINGLE_END_RESTORE);

    /* a delayed cue: reported at once, waiting until its delay has run */
    c.type = SA1_AUDIO_CUE_JINGLE;
    c.index = 96;
    c.delay = 0.5f;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    c.delay = 0.0f;
    s = mstate(m);
    CHECK(s.jingle == 96 && s.jingle_waiting == 1 && s.jingle_time == 0.0f && close_to(s.jingle_left, 0.5 + L96, 0.002));
    render_n(m, buf, rate / 4u);
    s = mstate(m);
    CHECK(s.jingle == 96 && s.jingle_waiting == 1 && close_to(s.jingle_left, 0.25 + L96, 0.003));
    render_n(m, buf, rate / 2u);
    s = mstate(m);
    CHECK(s.jingle == 96 && s.jingle_waiting == 0 && close_to(s.jingle_time, 0.25, 0.003) &&
          close_to(s.jingle_left, L96 - 0.25, 0.003));

    /* the 1-up: tone 704 (8C08A120) is a sound effect of the common bank 2, not a jingle; it does not end one */
    memset(&snd, 0, sizeof snd);
    snd.struct_size = sizeof snd;
    CHECK(sa1_audio_sound_info(a, 704, &snd) == SA1_OK && snd.kind == SA1_AUDIO_SOUND_PROGRAM && snd.bank == 2u);
    memset(&ev, 0, sizeof ev);
    ev.type = SA1_EVENT_SOUND;
    ev.character = 1;
    ev.id = 704;
    ev.bank = 2;
    ev.arg = 0x8C0940DAu;
    memset(&q, 0, sizeof q);
    q.struct_size = sizeof q;
    CHECK(sa1_audio_cue_from_event(a, &ev, &q) == SA1_OK && q.type == SA1_AUDIO_CUE_PLAY && q.tone == 704u);
    n = mstate(m).jingles_started;
    CHECK(sa1_audio_mixer_event(m, &ev) == SA1_OK);
    render_n(m, buf, rate / 10u);
    s = mstate(m);
    CHECK(s.jingle == 96 && s.jingles_started == n && s.jingles_ended == n - 1u);
    c.type = SA1_AUDIO_CUE_MUSIC_RESTORE;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    CHECK(sa1_audio_mixer_event(m, &ev) == SA1_OK);
    s = mstate(m);
    CHECK(s.jingle == -1 && s.jingles_started == n && s.last_end == SA1_AUDIO_JINGLE_END_RESTORE);
    render_n(m, buf, rate / 10u);
    r_1up = rms_frames(buf + rate / 20u * 2u, rate / 20u);         /* after the restored 96's fade */
    CHECK(r_1up > 10.0 && mstate(m).jingle == -1);                  /* the 1-up sounds; the state has no jingle */
    CHECK(sa1_audio_mixer_stop(m, 1) == SA1_OK);                    /* its tones (character 1) end; the state stays */
    s = mstate(m);
    CHECK(s.jingle == -1 && s.jingles_started == n && s.last_end == SA1_AUDIO_JINGLE_END_RESTORE);

    /* SA1_AUDIO_MIX_NO_JINGLES: switching jingles off ends the one playing (HOST); then -1 always */
    c.type = SA1_AUDIO_CUE_JINGLE;
    c.index = 85;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    CHECK(sa1_audio_mixer_flags(m, SA1_AUDIO_MIX_NO_JINGLES) == SA1_OK);
    s = mstate(m);
    CHECK(s.jingle == -1 && s.last_jingle == 85 && s.last_end == SA1_AUDIO_JINGLE_END_HOST &&
          s.jingles_ended == s.jingles_started);
    n = s.jingles_started;
    c.index = 55;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    s = mstate(m);
    CHECK(s.jingle == -1 && s.jingles_started == n);
    render_n(m, buf, rate / 2u);
    r_off = rms_frames(buf, rate / 2u);
    s = mstate(m);
    CHECK(s.jingle == -1 && s.jingles_started == n && r_off == 0.0);
    CHECK(sa1_audio_mixer_flags(m, 0) == SA1_OK);

    /* a JINGLE cue for 69 (NIGHTS_K.ADX, not on the disc): SA1_ERR_NOT_FOUND; the jingle before it has ended (the game's
       ADXT_StartFname stops the track first); nothing started */
    c.index = 85;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    n = mstate(m).jingles_started;
    c.index = 69;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_ERR_NOT_FOUND);
    s = mstate(m);
    CHECK(s.jingle == -1 && s.jingles_started == n && s.jingles_ended == n && s.last_jingle == 85 &&
          s.last_end == SA1_AUDIO_JINGLE_END_REPLACED);

    /* the host's stop: a character's leaves the jingle (it is nobody's); everything (0) ends it and forgets the state */
    c.type = SA1_AUDIO_CUE_MUSIC;
    c.index = 28;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    c.type = SA1_AUDIO_CUE_JINGLE;
    c.index = 96;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    CHECK(sa1_audio_mixer_stop(m, 1) == SA1_OK);
    CHECK(mstate(m).jingle == 96);
    CHECK(sa1_audio_mixer_stop(m, 0) == SA1_OK);
    s = mstate(m);
    CHECK(s.jingle == -1 && s.last_end == SA1_AUDIO_JINGLE_END_HOST && s.bgm == -1 && s.stopped == 0);
    c.type = SA1_AUDIO_CUE_MUSIC_STOP;
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    CHECK(mstate(m).stopped == 1);
    CHECK(sa1_audio_mixer_stop(m, 0) == SA1_OK);
    s = mstate(m);
    CHECK(s.stopped == 0 && s.jingle == -1 && s.jingles_ended == s.jingles_started);

    /* bgm_playing on a mixer that plays the background music: a jingle replaces it, RESTORE brings it back at once,
       the music stop ends it */
    CHECK(wait_clip(a, SA1_AUDIO_SOURCE_MUSIC, 28));
    c.type = SA1_AUDIO_CUE_MUSIC;
    c.index = 28;
    CHECK(sa1_audio_mixer_cue(mm, &c) == SA1_OK);
    render_n(mm, buf, rate / 10u);
    s = mstate(mm);
    CHECK(s.bgm == 28 && s.bgm_playing == 1 && s.jingle == -1);
    c.type = SA1_AUDIO_CUE_JINGLE;
    c.index = 85;
    CHECK(sa1_audio_mixer_cue(mm, &c) == SA1_OK);
    s = mstate(mm);
    CHECK(s.bgm == 28 && s.bgm_playing == 0 && s.jingle == 85);
    render_n(mm, buf, rate / 10u);
    CHECK(mstate(mm).bgm_playing == 0);
    c.type = SA1_AUDIO_CUE_MUSIC_RESTORE;
    CHECK(sa1_audio_mixer_cue(mm, &c) == SA1_OK);
    s = mstate(mm);
    CHECK(s.bgm == 28 && s.bgm_playing == 1 && s.jingle == -1);
    c.type = SA1_AUDIO_CUE_MUSIC_STOP;
    CHECK(sa1_audio_mixer_cue(mm, &c) == SA1_OK);
    s = mstate(mm);
    CHECK(s.bgm == -1 && s.bgm_playing == 0 && s.stopped == 1);
    printf("  music state: countdown rms %.0f, after the drown %.0f; TIMER %.2f s played out (then rms %.0f); SPEEDUP rms "
           "%.0f, 0.05 s after RESTORE %.0f; after MUSIC %.0f; FISHING (%.2f s, loops) at %.2f s rms %.0f; the 1-up rms "
           "%.0f (no jingle); jingles off rms %.0f; %u jingles started\n", r_count, r_drown, L96, r_out, r_jingle,
           r_restore, r_music, L45, L45 + 1.0, r_loop, r_1up, r_off, (unsigned)mstate(m).jingles_started);
out:
    sa1_audio_mixer_destroy(m);
    sa1_audio_mixer_destroy(mm);
    free(buf);
}

/* the SONICADV directory record of `name` in a directory extent: its first sector, 0 if absent */
static uint32_t dir_lba(const uint8_t *dir, uint32_t size, const char *name)
{
    uint32_t pos = 0, nl = (uint32_t)strlen(name);
    while (pos + 34u <= size) {
        const uint8_t *r = dir + pos;
        if (r[0] == 0) {
            pos = (pos / 2048u + 1u) * 2048u;
            continue;
        }
        if (r[0] < 34u || pos + r[0] > size)
            return 0;
        if (r[32] >= nl && 33u + nl <= r[0] && memcmp(r + 33, name, nl) == 0 && (r[32] == nl || r[33 + nl] == ';'))
            return sa1_rd32(r + 2);
        pos += r[0];
    }
    return 0;
}

static sa1_audio *open_music_only(const char *gdi, uint32_t flags, sa1_context **pctx, sa1_status *pst, double *ms)
{
    sa1_host host;
    sa1_audio_desc d;
    sa1_audio *a = NULL;
    double t0;
    *pctx = NULL;
    sa1_host_defaults(&host);
    host.alloc = count_alloc;
    host.free = count_free;
    host.log = log_cb;
    host.log_level = SA1_LOG_WARN;
    host.file_open = slow_open;
    host.file_size = slow_size;
    host.file_read = slow_read;
    host.file_close = slow_close;
    *pst = sa1_init(&host, pctx);
    if (*pst != SA1_OK)
        return NULL;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.gdi_path = gdi;
    d.stage = SA1_AUDIO_STAGE_DEFAULT;
    d.flags = SA1_AUDIO_MUSIC_ONLY | flags;
    t0 = tu_now_ms();
    *pst = sa1_audio_open(*pctx, &d, &a);
    *ms = tu_now_ms() - t0;
    return a;
}

/* reads `frames` from both streams in the same uneven pieces; 1 if every piece is the same (and as long) */
static int same_reads(sa1_audio_stream *x, sa1_audio_stream *y, uint32_t nch, uint32_t frames, int32_t loop,
                      uint32_t *total)
{
    static int16_t b1[2 * 30000], b2[2 * 30000];
    uint32_t done = 0;
    while (done < frames) {
        uint32_t want = 1u + rnd() % 30000u, n1 = 0, n2 = 0;
        if (want > frames - done)
            want = frames - done;
        if (sa1_audio_stream_read(x, b1, want, loop, &n1) != SA1_OK ||
            sa1_audio_stream_read(y, b2, want, loop, &n2) != SA1_OK || n1 != n2 ||
            memcmp(b1, b2, (size_t)n1 * nch * sizeof(int16_t)) != 0)
            return 0;
        if (n1 == 0)
            break;
        done += n1;
    }
    if (total)
        *total = done;
    return 1;
}

/* SA1_AUDIO_LAZY_STREAMS: an open reads the header only and reads fetch the file as decoding reaches it; the PCM is
 * the eager stream's sample for sample (sequential reads in uneven pieces to the end, across the loop end, seeks back
 * and far forward), and the whole file is read once */
static void check_lazy_streams(const char *gdi, sa1_audio *eager, const sa1_audio_music *mi)
{
    static const uint32_t k_tracks[5] = { 28, 87, 46, 74, 6 };     /* loop 44.1 kHz, truncated, tiny, long loop, 15 MB */
    sa1_context *ctx = NULL;
    sa1_status st;
    double open_ms, worst_open = 0.0;
    uint32_t t, max_open_sec = 0, checked = 0;
    sa1_audio *lz = open_music_only(gdi, SA1_AUDIO_LAZY_STREAMS, &ctx, &st, &open_ms);
    CHECK(st == SA1_OK && lz != NULL);
    if (!lz) {
        sa1_shutdown(ctx);
        return;
    }
    for (t = 0; t < 5; t++) {
        uint32_t i = k_tracks[t], frames = mi[i].num_frames, sec0, sec_open, sec_all, file_sec, total = 0;
        sa1_audio_stream *e = NULL, *z = NULL;
        sa1_audio_stream_info ei, zi;
        double t0;
        memset(&ei, 0, sizeof ei);
        memset(&zi, 0, sizeof zi);
        ei.struct_size = zi.struct_size = sizeof ei;
        /* the eager stream first (it reads the whole file through the same counting callbacks) */
        if (i != 6)
            CHECK(sa1_audio_stream_open(eager, SA1_AUDIO_SOURCE_MUSIC, i, &e, &ei) == SA1_OK);
        sec0 = __atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED);
        t0 = tu_now_ms();
        CHECK(sa1_audio_stream_open(lz, SA1_AUDIO_SOURCE_MUSIC, i, &z, &zi) == SA1_OK);
        open_ms = tu_now_ms() - t0;
        sec_open = __atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED) - sec0;
        if (sec_open > max_open_sec)
            max_open_sec = sec_open;
        if (open_ms > worst_open)
            worst_open = open_ms;
        CHECK(zi.num_frames == frames && zi.loop == mi[i].loop && zi.loop_start == mi[i].loop_start &&
              zi.loop_end == mi[i].loop_end && zi.sample_rate == mi[i].sample_rate);
        if (i == 6 || !z || !e) {               /* the largest file: the open alone */
            sa1_audio_stream_close(z);
            sa1_audio_stream_close(e);
            continue;
        }
        /* the first second reads one step (256 KB = 128 sectors) at most; then sequential to the end: the same PCM,
           every sector of the file read once */
        CHECK(same_reads(e, z, ei.channels, 44100u, 0, NULL));
        CHECK(__atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED) - sec0 <= 2u + 128u);
        CHECK(same_reads(e, z, ei.channels, frames + 100u, 0, &total) && total == frames - 44100u);
        sec_all = __atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED) - sec0;
        file_sec = (mi[i].file_size + 2047u) / 2048u;
        CHECK(sec_all == file_sec);
        /* across the loop end (loop = 1), then a seek back */
        if (ei.loop) {
            uint32_t at = ei.loop_end > 5000u ? ei.loop_end - 5000u : 0u;
            CHECK(sa1_audio_stream_seek(e, at) == SA1_OK && sa1_audio_stream_seek(z, at) == SA1_OK);
            CHECK(same_reads(e, z, ei.channels, 3u * (ei.loop_end - ei.loop_start) + 7000u, 1, NULL));
        }
        CHECK(sa1_audio_stream_seek(e, 1000u) == SA1_OK && sa1_audio_stream_seek(z, 1000u) == SA1_OK);
        CHECK(same_reads(e, z, ei.channels, 50000u, 0, NULL));
        sa1_audio_stream_close(z);
        /* a fresh lazy stream: a seek far forward first, then loop reads from there */
        z = NULL;
        CHECK(sa1_audio_stream_open(lz, SA1_AUDIO_SOURCE_MUSIC, i, &z, NULL) == SA1_OK);
        if (z) {
            uint32_t at = frames > 3000u ? frames - 3000u : 0u;
            CHECK(sa1_audio_stream_seek(e, at) == SA1_OK && sa1_audio_stream_seek(z, at) == SA1_OK);
            CHECK(same_reads(e, z, ei.channels, 40000u, ei.loop, &total) && total == (ei.loop ? 40000u : frames - at));
            CHECK(sa1_audio_stream_seek(z, frames + 1u) == SA1_ERR_INVALID_ARGUMENT);
            sa1_audio_stream_close(z);
        }
        sa1_audio_stream_close(e);
        checked++;
    }
    {
        sa1_audio_stream *z = NULL;
        uint32_t sec0 = __atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED);
        CHECK(sa1_audio_stream_open(lz, SA1_AUDIO_SOURCE_MUSIC, 69, &z, NULL) == SA1_ERR_NOT_FOUND && z == NULL);
        CHECK(__atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED) == sec0);
    }
    CHECK(max_open_sec <= 2u && checked == 4u);
    printf("  lazy streams: an open reads at most %u sectors (slowest %.1f ms, AMY.ADX included); %u tracks decode as "
           "the whole-file streams do, each file read once\n", (unsigned)max_open_sec, worst_open, (unsigned)checked);
    sa1_audio_close(lz);
    sa1_shutdown(ctx);
}

/* the music list of a MUSIC_ONLY object: sa1_audio_music_info against the J0 list of jukebox.md 7 (with its Review) */
static void check_music_info(sa1_context *main_ctx, const char *gdi)
{
    static const uint32_t k_pages[6] = { 11, 40, 11, 12, 10, 20 };
    static const uint8_t k_jingles[8] = { 26, 44, 45, 46, 55, 56, 85, 96 };
    static const uint8_t k_unlisted[5] = { 68, 70, 84, 87, 102 };
    sa1_audio_music *mi = (sa1_audio_music *)calloc(110, sizeof *mi);
    sa1_context *ctx = NULL;
    sa1_audio *a;
    sa1_status st;
    sa1_audio_stats stats;
    uint32_t i, k, on_disc = 0, listed = 0, jingles = 0, truncated = 0, page_n[6] = { 0 }, warm, max_sec = 0;
    uint32_t sec0, same = 0, unlisted = 0, entry_bits[6][2];
    double open_ms, t0, info_ms, streams_ms;
    if (!mi) {
        CHECK(0);
        return;
    }
    memset(entry_bits, 0, sizeof entry_bits);
    __atomic_store_n(&g_slow_ms, 0, __ATOMIC_RELAXED);
    a = open_music_only(gdi, 0, &ctx, &st, &open_ms);
    CHECK(st == SA1_OK && a != NULL);
    if (!a) {
        sa1_shutdown(ctx);
        free(mi);
        return;
    }
    /* MUSIC_ONLY: no banks, no voice archive, no preload; music numbers and streams work */
    for (i = 0; i < SA1A_GAME_BANKS; i++)
        CHECK(!a->stage_banks[i].loaded);
    for (i = 0; i < SA1A_NUM_GROUPS; i++)
        CHECK(!a->player[i].loaded && !a->voiceb[i].loaded);
    CHECK(sa1_audio_voice_count(a) == 0u && sa1_audio_music_count(a) == 110u);
    {
        sa1_audio_sound s;
        sa1_audio_stream *vs = NULL;
        s.struct_size = sizeof s;
        CHECK(sa1_audio_sound_info(a, 723, &s) == SA1_OK && s.kind == SA1_AUDIO_SOUND_NONE);
        CHECK(sa1_audio_stream_open(a, SA1_AUDIO_SOURCE_VOICE, 0, &vs, NULL) == SA1_ERR_NOT_LOADED && vs == NULL);
        CHECK(sa1_audio_set_stage(a, 26) == SA1_OK);
    }
    stats.struct_size = sizeof stats;
    CHECK(sa1_audio_get_stats(a, &stats) == SA1_OK && stats.clips_pinned == 0u && stats.clips_cached == 0u &&
          stats.tones_decoded == 0u);
    /* argument checks */
    mi[0].struct_size = 0;
    CHECK(sa1_audio_music_info(a, 0, &mi[0]) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_music_info(a, 0, NULL) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_music_info(NULL, 0, &mi[0]) == SA1_ERR_INVALID_ARGUMENT);
    for (i = 0; i < 110; i++)
        mi[i].struct_size = sizeof mi[i];
    CHECK(sa1_audio_music_info(a, 110, &mi[0]) == SA1_ERR_NOT_FOUND);
    /* the first call reads ADVERTISE.PRS (87 sectors) and a header; then at most 2 sectors per music number (its
       header), nothing for a number asked for again */
    sec0 = __atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED);
    t0 = tu_now_ms();
    CHECK(sa1_audio_music_info(a, 0, &mi[0]) == SA1_OK);
    warm = __atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED) - sec0;
    for (i = 0; i < 110; i++) {
        uint32_t before = __atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED), n;
        CHECK(sa1_audio_music_info(a, i, &mi[i]) == SA1_OK);
        n = __atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED) - before;
        if (n > max_sec)
            max_sec = n;
        if (i == 0)
            CHECK(n == 0u);
    }
    info_ms = tu_now_ms() - t0;
    sec0 = __atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED);
    for (i = 0; i < 110; i++) {
        sa1_audio_music again;
        again.struct_size = sizeof again;
        CHECK(sa1_audio_music_info(a, i, &again) == SA1_OK && memcmp(&again, &mi[i], sizeof again) == 0);
    }
    CHECK(__atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED) == sec0);
    CHECK(warm >= 87u && warm <= 87u + 2u && max_sec <= 2u);
    /* the list */
    for (i = 0; i < 110; i++) {
        const sa1_audio_music *m = &mi[i];
        int jingle = 0;
        CHECK(m->music == i && m->file && strcmp(m->file, sa1_audio_music_name(a, i)) == 0);
        for (k = 0; k < 8; k++)
            jingle |= k_jingles[k] == i;
        CHECK(!!(m->flags & SA1_AUDIO_MUSIC_JINGLE) == jingle);
        on_disc += !!(m->flags & SA1_AUDIO_MUSIC_ON_DISC);
        jingles += !!(m->flags & SA1_AUDIO_MUSIC_JINGLE);
        truncated += !!(m->flags & SA1_AUDIO_MUSIC_TRUNCATED);
        if (m->flags & SA1_AUDIO_MUSIC_LISTED) {
            listed++;
            CHECK(m->title && strlen(m->title) > 0 && m->page >= 0 && m->page < 6 && m->entry >= 0 &&
                  (uint32_t)m->entry < k_pages[m->page < 0 || m->page > 5 ? 0 : m->page]);
            if (m->page >= 0 && m->page < 6 && m->entry >= 0 && m->entry < 64) {
                page_n[m->page]++;
                CHECK(!(entry_bits[m->page][m->entry / 32] & (1u << (m->entry % 32))));    /* each entry once */
                entry_bits[m->page][m->entry / 32] |= 1u << (m->entry % 32);
            }
        } else {
            CHECK(m->title == NULL && m->page == -1 && m->entry == -1);
            if (m->flags & SA1_AUDIO_MUSIC_ON_DISC) {
                for (k = 0; k < 5; k++)
                    unlisted += k_unlisted[k] == i;
            }
        }
        if (m->flags & SA1_AUDIO_MUSIC_ON_DISC)
            CHECK(m->channels == 2u && (m->sample_rate == 32000u || m->sample_rate == 44100u) && m->num_frames > 0u &&
                  m->file_size > 0u && m->loop_end <= m->num_frames && m->loop_start < m->loop_end);
        else
            CHECK(m->channels == 0u && m->num_frames == 0u && m->file_size == 0u);
    }
    CHECK(on_disc == 109u && listed == 104u && jingles == 8u && truncated == 1u && unlisted == 5u);
    for (k = 0; k < 6; k++)
        CHECK(page_n[k] == k_pages[k]);
    CHECK(!(mi[69].flags & (SA1_AUDIO_MUSIC_ON_DISC | SA1_AUDIO_MUSIC_LISTED)) && strcmp(mi[69].file, "NIGHTS_K.ADX") == 0);
    CHECK((mi[87].flags & SA1_AUDIO_MUSIC_TRUNCATED) && !(mi[87].flags & SA1_AUDIO_MUSIC_LISTED) &&
          mi[87].num_frames == 5632416u);
    CHECK(mi[66].title && strcmp(mi[66].title, "Open Your Heart") == 0 && mi[66].page == 0 && mi[66].entry == 0);
    CHECK(mi[28].title && strcmp(mi[28].title, "Emerald Coast:  Type 1") == 0 && mi[28].page == 1 && mi[28].entry == 0);
    CHECK(mi[61].title && strcmp(mi[61].title, "Jingle#E") == 0 && mi[61].page == 5 && mi[61].entry == 19);
    /* for every file the info is what sa1_audio_stream_open reports (it reads the whole file); 69 reads nothing */
    t0 = tu_now_ms();
    for (i = 0; i < 110; i++) {
        sa1_audio_stream *s = NULL;
        sa1_audio_stream_info si;
        if (!(mi[i].flags & SA1_AUDIO_MUSIC_ON_DISC))
            continue;
        si.struct_size = sizeof si;
        CHECK(sa1_audio_stream_open(a, SA1_AUDIO_SOURCE_MUSIC, i, &s, &si) == SA1_OK);
        if (!s)
            continue;
        if (si.channels == mi[i].channels && si.sample_rate == mi[i].sample_rate && si.num_frames == mi[i].num_frames &&
            si.loop == mi[i].loop && si.loop_start == mi[i].loop_start && si.loop_end == mi[i].loop_end)
            same++;
        else
            printf("  music %u: info %u ch %u Hz %u frames loop %d %u-%u, stream %u ch %u Hz %u frames loop %d %u-%u\n",
                   (unsigned)i, (unsigned)mi[i].channels, (unsigned)mi[i].sample_rate, (unsigned)mi[i].num_frames,
                   (int)mi[i].loop, (unsigned)mi[i].loop_start, (unsigned)mi[i].loop_end, (unsigned)si.channels,
                   (unsigned)si.sample_rate, (unsigned)si.num_frames, (int)si.loop, (unsigned)si.loop_start,
                   (unsigned)si.loop_end);
        if (i == 28) {
            /* a stream of the music-only object plays */
            int16_t *pcm = (int16_t *)malloc((size_t)44100u * 2u * sizeof(int16_t));
            uint32_t got = 0;
            if (pcm) {
                CHECK(sa1_audio_stream_seek(s, 44100u * 10u) == SA1_OK);
                CHECK(sa1_audio_stream_read(s, pcm, 44100u, 1, &got) == SA1_OK && got == 44100u);
                CHECK(rms16(pcm, got * 2u) > 100.0);
                free(pcm);
            }
        }
        sa1_audio_stream_close(s);
    }
    streams_ms = tu_now_ms() - t0;
    CHECK(same == 109u);
    {
        sa1_audio_stream *s = NULL;
        sec0 = __atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED);
        CHECK(sa1_audio_stream_open(a, SA1_AUDIO_SOURCE_MUSIC, 69, &s, NULL) == SA1_ERR_NOT_FOUND && s == NULL);
        CHECK(sa1_audio_stream_open(a, SA1_AUDIO_SOURCE_MUSIC, 110, &s, NULL) == SA1_ERR_NOT_FOUND && s == NULL);
        CHECK(__atomic_load_n(&g_read_sectors, __ATOMIC_RELAXED) == sec0);
    }
    printf("  music only: open %.0f ms; 110 infos in %.0f ms (first call %u sectors, then at most %u per number); %u on "
           "the disc, %u listed (pages %u/%u/%u/%u/%u/%u), %u jingles, %u truncated; %u of %u equal their stream "
           "(all files opened in %.0f ms)\n", open_ms, info_ms, (unsigned)warm, (unsigned)max_sec, (unsigned)on_disc,
           (unsigned)listed, (unsigned)page_n[0], (unsigned)page_n[1], (unsigned)page_n[2], (unsigned)page_n[3],
           (unsigned)page_n[4], (unsigned)page_n[5], (unsigned)jingles, (unsigned)truncated, (unsigned)same,
           (unsigned)on_disc, streams_ms);
    check_lazy_streams(gdi, a, mi);
    sa1_audio_close(a);
    sa1_shutdown(ctx);
    for (i = 0; i < 110; i++)
        mi[i].file = mi[i].title = NULL;        /* they belonged to the object just closed */

    /* a modified ADVERTISE.PRS (the file callback flips one byte of it): no titles, still SA1_OK, the rest unchanged */
    {
        sa1_disc d;
        uint8_t *data = NULL;
        uint32_t size = 0, lba = 0;
        if (sa1_disc_open(main_ctx, gdi, &d) == SA1_OK) {
            if (sa1_disc_read_file(&d, "SONICADV/ADVERTISE.PRS", &data, &size) == SA1_OK && d.dir)
                lba = dir_lba(d.dir, d.dir_size, "ADVERTISE.PRS");      /* d.dir: the SONICADV directory now */
            if (lba && lba >= d.tracks[d.hd].info.lba && size > 41u * 2048u) {
                const sa1_disc_track *t = &d.tracks[d.hd];
                snprintf(g_flip_track, sizeof g_flip_track, "%s", t->info.name);
                g_flip_off = (uint64_t)(lba + 40u - t->info.lba) * t->info.sector_size + 1000u;  /* sector 40, user data */
            }
            sa1_mem_free(main_ctx, data);
            sa1_disc_close(&d);
        }
        CHECK(g_flip_track[0] != 0);
        g_log_advertise = 0;
        a = open_music_only(gdi, 0, &ctx, &st, &open_ms);
        CHECK(st == SA1_OK && a != NULL);
        if (a) {
            static const uint32_t k_some[4] = { 66, 28, 61, 87 };
            static const char *const k_some_file[4] = { "MAINTHEM.ADX", "ECOAST1.ADX", "JINGLE_5.ADX", "SSRACING.ADX" };
            for (k = 0; k < 4; k++) {
                sa1_audio_music m2;
                i = k_some[k];
                m2.struct_size = sizeof m2;
                CHECK(sa1_audio_music_info(a, i, &m2) == SA1_OK);
                CHECK(m2.title == NULL && !(m2.flags & SA1_AUDIO_MUSIC_LISTED) && m2.page == -1 && m2.entry == -1);
                CHECK(m2.flags == (mi[i].flags & ~SA1_AUDIO_MUSIC_LISTED) && m2.num_frames == mi[i].num_frames &&
                      m2.loop_start == mi[i].loop_start && m2.loop_end == mi[i].loop_end &&
                      m2.sample_rate == mi[i].sample_rate && m2.file && strcmp(m2.file, k_some_file[k]) == 0);
            }
            CHECK(g_log_advertise);
            printf("  a modified ADVERTISE.PRS (byte %llu of %s flipped): no titles, SA1_OK, the headers unchanged\n",
                   (unsigned long long)g_flip_off, g_flip_track);
            sa1_audio_close(a);
        }
        sa1_shutdown(ctx);
        g_flip_track[0] = 0;
    }
    free(mi);
}

/* ---- no stalls: every audio call of a session on a slow disc, timed ---- */
typedef struct call_time {
    const char *name;
    double max_ms;
    uint32_t calls, over;
} call_time;

static call_time g_ct[8];
static void ct_add(int k, const char *name, double ms)
{
    g_ct[k].name = name;
    g_ct[k].calls++;
    if (ms > g_ct[k].max_ms)
        g_ct[k].max_ms = ms;
    g_ct[k].over += ms > 1.0;
}
#define TIMED(k, name, call)                         \
    do {                                             \
        double t0_ = tu_now_ms();                    \
        call;                                        \
        ct_add((k), (name), tu_now_ms() - t0_);      \
    } while (0)

static double session(const char *gdi, uint32_t flags, int check)
{
    static const uint32_t voices[] = { 100, 137, 174, 211, 248, 285, 322, 359, 396, 433, 470, 507, 544, 581, 618, 655,
                                       692, 729, 766, 803, 840, 877, 914, 951, 988 };
    sa1_host host;
    sa1_context *ctx = NULL;
    sa1_audio *a = NULL;
    sa1_audio_desc d;
    sa1_audio_mixer *m = NULL;
    sa1_audio_mixer_desc md;
    sa1_audio_stats s0, s1;
    float lp[3] = { 0, 0, 0 }, fw[3] = { 1, 0, 0 }, up[3] = { 0, 0, 1 }, p2[3] = { 400, 0, 0 };
    int16_t *buf = (int16_t *)calloc(1470u * 2u, sizeof(int16_t));
    double t0, preload_ms, worst = 0.0;
    uint32_t tick, k, nv = 0, timer_cues = 0;
    const char *worst_name = "";
    sa1_audio_music_state ms;
    int hurry_ok = 0;
    memset(&ms, 0, sizeof ms);
    memset(g_ct, 0, sizeof g_ct);
    sa1_host_defaults(&host);
    host.log = log_cb;
    host.log_level = SA1_LOG_WARN;
    host.file_open = slow_open;
    host.file_size = slow_size;
    host.file_read = slow_read;
    host.file_close = slow_close;
    __atomic_store_n(&g_slow_ms, 0, __ATOMIC_RELAXED);
    if (!buf || sa1_init(&host, &ctx) != SA1_OK) {
        CHECK(0);
        free(buf);
        return -1.0;
    }
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.gdi_path = gdi;
    d.stage = SA1_AUDIO_STAGE_DEFAULT;
    d.flags = flags;
    memset(&md, 0, sizeof md);
    md.struct_size = sizeof md;
    md.sample_rate = 44100;
    if (SA1_FAILED(sa1_audio_open(ctx, &d, &a)) || sa1_audio_mixer_create(a, &md, &m) != SA1_OK) {
        CHECK(0);
        sa1_audio_close(a);
        sa1_shutdown(ctx);
        free(buf);
        return -1.0;
    }
    t0 = tu_now_ms();
    CHECK(sa1_audio_preload(a, SA1_AUDIO_PRELOAD_ALL | SA1_AUDIO_PRELOAD_WAIT) == SA1_OK);
    preload_ms = tu_now_ms() - t0;
    s0.struct_size = sizeof s0;
    CHECK(sa1_audio_get_stats(a, &s0) == SA1_OK);
    sa1_audio_mixer_listener(m, lp, fw, up);
    /* from here on every read of the disc takes 25 ms */
    __atomic_store_n(&g_slow_ms, 25, __ATOMIC_RELAXED);
#if defined(_WIN32)
    /* the session's thread above the build jobs that share the machine: a call that waits (for the disc, for a lock)
       still shows, the scheduler taking the CPU away for a while does not */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
    for (tick = 0; tick < 600; tick++) {
        sa1_event ev[6];
        uint32_t ne = 0;
        memset(ev, 0, sizeof ev);
        for (k = 0; k < 6; k++)
            ev[k].character = 1;
        if (tick % 4 == 0) {
            ev[ne].type = SA1_EVENT_SOUND, ev[ne].id = 723, ev[ne].arg = 0x8C0940DAu, ne++;
        }
        if ((tick / 30) % 2 == 0) {
            ev[ne].type = SA1_EVENT_SOUND, ev[ne].id = 724, ev[ne].arg = 0x8C094330u, ne++;
        }
        if (tick % 7 == 0) {
            ev[ne].type = SA1_EVENT_SOUND, ev[ne].id = 34, ev[ne].arg = 0x8C09413Au;
            ev[ne].position[0] = 80.0f;
            ne++;
        }
        if (tick % 50 == 25) {
            ev[ne].type = SA1_EVENT_SOUND, ev[ne].id = 1216, ev[ne].arg = 0x8C0940DAu, ne++;
        }
        if (tick % 60 == 10) {
            ev[ne].type = SA1_EVENT_JINGLE, ev[ne].id = 96, ev[ne].arg = 0x8C107C80u, ne++;   /* TIMER.ADX */
            timer_cues++;
        } else if (tick % 60 == 50) {
            ev[ne].type = SA1_EVENT_JINGLE, ev[ne].arg = 0x8C107B26u, ne++;   /* restore */
        } else if (tick == 291) {
            ev[ne].type = SA1_EVENT_JINGLE, ev[ne].id = 51, ev[ne].arg = 0x8C107C80u, ne++;   /* HURRYUP.ADX, cold */
        }
        if (tick % 24 == 5) {
            ev[ne].type = SA1_EVENT_VOICE, ev[ne].id = voices[nv++ % (sizeof voices / sizeof voices[0])];
            ev[ne].arg = 0x8C107BA0u;
            ne++;
        }
        if (tick % 90 == 45) {
            ev[ne].type = SA1_EVENT_SOUND_STOP, ev[ne].id = 723, ev[ne].arg = 0x8C0945A0u, ne++;
        }
        TIMED(0, "sa1_audio_mixer_character", sa1_audio_mixer_character(m, 1, lp, 1));
        TIMED(0, "sa1_audio_mixer_character", sa1_audio_mixer_character(m, 2 + (tick / 200), p2, 0));
        TIMED(1, "sa1_audio_mixer_listener", sa1_audio_mixer_listener(m, lp, fw, up));
        for (k = 0; k < ne; k++)
            TIMED(2, "sa1_audio_mixer_event", sa1_audio_mixer_event(m, &ev[k]));
        TIMED(3, "sa1_audio_mixer_render (1470 frames)", sa1_audio_mixer_render(m, buf, 1470u));
        TIMED(4, "sa1_audio_mixer_active", (void)sa1_audio_mixer_active(m));
        TIMED(6, "sa1_audio_mixer_music_state", ms = mstate(m));
        if (tick == 291u)   /* the cold HURRYUP.ADX: reported at once (a music number the game never plays as a jingle) */
            hurry_ok = ms.jingle == 51 && ms.jingle_kind == SA1_AUDIO_JINGLE_OTHER;
        slow_sleep(1);      /* time passes a little (the files take 25 ms per read) */
    }
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
#endif
    s1.struct_size = sizeof s1;
    TIMED(5, "sa1_audio_get_stats", sa1_audio_get_stats(a, &s1));
    /* the loader catches up with the cold files after the session */
    for (k = 0; k < 2000u && s1.clips_waiting; k++) {
        slow_sleep(5);
        sa1_audio_get_stats(a, &s1);
    }
    for (k = 0; k < 8; k++) {
        if (g_ct[k].calls && g_ct[k].max_ms > worst) {
            worst = g_ct[k].max_ms;
            worst_name = g_ct[k].name;
        }
    }
    printf("  %s: preload %.0f ms (%u tones, %u jingles pinned, %.1f MB); session on a disc with 25 ms per read: "
           "longest call %.3f ms (%s)\n", (flags & SA1_AUDIO_SYNC_LOADS) ? "SA1_AUDIO_SYNC_LOADS" : "loader thread",
           preload_ms, (unsigned)s0.tones_decoded, (unsigned)s0.clips_pinned, (double)s0.cache_bytes / 1048576.0, worst,
           worst_name);
    for (k = 0; k < 8; k++)
        if (g_ct[k].calls)
            printf("    %-40s %6u calls, longest %.3f ms, %u over 1 ms\n", g_ct[k].name, (unsigned)g_ct[k].calls,
                   g_ct[k].max_ms, (unsigned)g_ct[k].over);
    printf("    loader: %u files read (%u on the calling thread, slowest %.0f ms), %u clip starts found their file read "
           "(%u TIMER cues: preloaded), %u cues waited for their tone (%u given up)\n",
           (unsigned)(s1.clip_reads - s0.clip_reads), (unsigned)(s1.caller_reads - s0.caller_reads), (double)s1.max_read_ms,
           (unsigned)(s1.clip_hits - s0.clip_hits), (unsigned)timer_cues, (unsigned)s1.cues_deferred,
           (unsigned)s1.cues_dropped);
    if (check) {
        CHECK(s0.loader_thread == 1 && s0.preload_done == 1 && s0.clips_pinned >= 5u && s0.tones_decoded > 1000u);
        CHECK(s1.caller_reads == s0.caller_reads);
        CHECK(s1.clips_waiting == 0 && s1.clip_reads - s0.clip_reads >= 20u && s1.max_read_ms >= 20.0f);   /* 25 voices and a
                                                                                                    jingle, cold */
        CHECK(timer_cues == 10u && s1.clip_hits - s0.clip_hits >= timer_cues);
        /* the music state through the session: every TIMER and the HURRYUP started and ended (restored / replaced) */
        CHECK(hurry_ok && ms.jingle == -1 && ms.jingles_started == timer_cues + 1u &&
              ms.jingles_ended == ms.jingles_started && ms.last_jingle == 96 &&
              ms.last_end == SA1_AUDIO_JINGLE_END_RESTORE);
    }
    __atomic_store_n(&g_slow_ms, 0, __ATOMIC_RELAXED);
    sa1_audio_mixer_destroy(m);
    sa1_audio_close(a);
    sa1_shutdown(ctx);
    free(buf);
    return worst;
}

/* Without the preload a cue whose tone is not decoded yet waits in the mixer (the loader decodes it first) and starts
 * a moment later, instead of decoding it on the caller's thread (E-102's sequence tone 777 takes about 5 ms). */
static void check_deferred(const char *gdi)
{
    sa1_context *ctx = NULL;
    sa1_audio *a = NULL;
    sa1_audio_desc d;
    sa1_audio_mixer *m = NULL;
    sa1_audio_mixer_desc md;
    sa1_audio_cue c;
    sa1_audio_stats st;
    int16_t buf[2 * 512];
    double t0, cue_ms;
    uint32_t i, started = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.gdi_path = gdi;
    d.stage = SA1_AUDIO_STAGE_DEFAULT;
    d.flags = SA1_AUDIO_NO_PRELOAD;
    memset(&md, 0, sizeof md);
    md.struct_size = sizeof md;
    if (sa1_init(NULL, &ctx) != SA1_OK || SA1_FAILED(sa1_audio_open(ctx, &d, &a)) ||
        sa1_audio_mixer_create(a, &md, &m) != SA1_OK) {
        CHECK(0);
        sa1_audio_close(a);
        sa1_shutdown(ctx);
        return;
    }
    memset(&c, 0, sizeof c);
    c.struct_size = sizeof c;
    c.type = SA1_AUDIO_CUE_PLAY;
    c.character = 1;
    c.tone = 777;
    c.volume = 1.0f;
    c.lifetime = -1.0f;
    t0 = tu_now_ms();
    CHECK(sa1_audio_mixer_cue(m, &c) == SA1_OK);
    cue_ms = tu_now_ms() - t0;
    for (i = 0; i < 500u && !started; i++) {
        sa1_audio_mixer_render(m, buf, 512u);
        started = sa1_audio_mixer_active(m) > 0;
        if (!started)
            slow_sleep(2);
    }
    st.struct_size = sizeof st;
    sa1_audio_get_stats(a, &st);
    printf("  without the preload: the cue of sequence tone 777 took %.3f ms, waited for the loader (%u cue(s) deferred) "
           "and started after %u renders\n", cue_ms, (unsigned)st.cues_deferred, (unsigned)i);
    CHECK(cue_ms < 1.0 && st.cues_deferred >= 1u && st.cues_dropped == 0u && started);
    sa1_audio_mixer_destroy(m);
    sa1_audio_close(a);
    sa1_shutdown(ctx);
}

static void check_stalls(const char *gdi)
{
    double threaded, sync;
    int tries = 1;
    /* a wait for the disc shows in every run; a run disturbed by the machine (another process taking the CPU in the
       middle of a call) is repeated, at most twice */
    threaded = session(gdi, 0u, 1);
    while (threaded > 1.0 && tries < 3) {
        threaded = fmin(threaded, session(gdi, 0u, 0));
        tries++;
    }
    sync = session(gdi, SA1_AUDIO_SYNC_LOADS, 0);
    printf("  longest audio call: %.3f ms with the loader thread (%d run(s)), %.1f ms with every clip read on the calling "
           "thread\n", threaded, tries, sync);
    CHECK(threaded <= 1.0);
    CHECK(sync > 20.0);     /* the session does read cold clips: the thread is what keeps them off the caller */
}

/* ---- groups: audio_test GDI [OUTDIR] [--only NAME[,NAME...]] [--skip NAME[,NAME...]] ----
 * banks sequences cues streams mixer music_mixer music_state robustness all_tones stage_change music_info stalls (the
 * slow-disc session and the deferred cue), and header version 5's stage_rules, repeats and whoosh_world (the world part:
 * a disc load and Super worlds, the slowest). The opening checks always run. */
static const char *g_only, *g_skip;
static const char *g_gdi;               /* the disc, for the groups that open objects of their own */
static sa1_context *g_main_ctx;         /* the main context (sr_fail_file's disc reads) */
static int in_list(const char *list, const char *name)
{
    size_t n = strlen(name);
    while (list && *list) {
        const char *e = strchr(list, ',');
        size_t len = e ? (size_t)(e - list) : strlen(list);
        if (len == n && strncmp(list, name, n) == 0)
            return 1;
        list = e ? e + 1 : NULL;
    }
    return 0;
}
static int want(const char *group)
{
    if (g_only && !in_list(g_only, group))
        return 0;
    return !(g_skip && in_list(g_skip, group));
}

/* ==== stage rules (header version 5; sadc docs/research/super2_plan.md 6.3, super2_sound.md 6 and 8.1) ==================
 * Cases (a)-(l) of super2_plan.md 6.3, on objects of their own context (bytes counted, the slow-disc file callbacks with a
 * failing range for (j)). The tones: 1035 / 1036 = CHAOS7B.MLT (stage 19) bank 5 requests 8 / 9 (Super Sonic's aura
 * whoosh; empty in EMERALDCOAST.MLT, the object's own stage); 237 = ICECAP.MLT (8) bank 1 (the board's snow); 691 =
 * SANDHILL.MLT (38) bank 1 (the board's sand; EMERALDCOAST.MLT holds another program there). */
#define SR_RATE 44100u

static sa1_context *sr_ctx(void)
{
    sa1_host h;
    sa1_context *ctx = NULL;
    sa1_coords co;
    sa1_host_defaults(&h);
    h.alloc = count_alloc;
    h.free = count_free;
    h.log = log_cb;
    h.log_level = SA1_LOG_WARN;
    h.file_open = slow_open;
    h.file_size = slow_size;
    h.file_read = slow_read;
    h.file_close = slow_close;
    if (sa1_init(&h, &ctx) != SA1_OK)
        return NULL;
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &co);
    sa1_set_coords(ctx, &co);
    return ctx;
}

static sa1_audio *sr_open(sa1_context *ctx, int32_t stage, uint32_t flags)
{
    sa1_audio_desc d;
    sa1_audio *a = NULL;
    sa1_status st;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.gdi_path = g_gdi;
    d.flags = flags;
    d.stage = stage;
    st = sa1_audio_open(ctx, &d, &a);
    if (SA1_FAILED(st)) {
        printf("  sa1_audio_open(stage %d): %s: %s\n", (int)stage, sa1_status_string(st), sa1_last_error(ctx));
        return NULL;
    }
    if (!(flags & (SA1_AUDIO_MUSIC_ONLY | SA1_AUDIO_NO_PRELOAD)))
        sa1_audio_preload(a, SA1_AUDIO_PRELOAD_ALL | SA1_AUDIO_PRELOAD_WAIT);
    return a;
}

static sa1_audio_mixer *sr_mixer(sa1_audio *a, uint32_t flags)
{
    static const float zero[3] = { 0, 0, 0 }, fwd[3] = { 1, 0, 0 }, up[3] = { 0, 0, 1 };
    sa1_audio_mixer_desc md;
    sa1_audio_mixer *m = NULL;
    memset(&md, 0, sizeof md);
    md.struct_size = sizeof md;
    md.sample_rate = SR_RATE;
    md.flags = flags;
    if (!a || sa1_audio_mixer_create(a, &md, &m) != SA1_OK)
        return NULL;
    sa1_audio_mixer_listener(m, zero, fwd, up);
    sa1_audio_mixer_character(m, 1, zero, 1);
    sa1_audio_mixer_character(m, 2, zero, 1);
    return m;
}

/* an event as the game's call sends it: SOUND from 8C0940DA (or `fn`), SOUND_STOP from 8C0945A0 */
static void sr_ev_fn(sa1_audio_mixer *m, sa1_character_id ch, uint32_t type, uint32_t tone, uint32_t fn)
{
    sa1_event x;
    memset(&x, 0, sizeof x);
    x.character = ch;
    x.type = type;
    x.id = tone;
    x.arg = fn ? fn : (type == SA1_EVENT_SOUND ? 0x8C0940DAu : 0x8C0945A0u);
    sa1_audio_mixer_event(m, &x);
}

static void sr_ev(sa1_audio_mixer *m, sa1_character_id ch, uint32_t type, uint32_t tone)
{
    sr_ev_fn(m, ch, type, tone, 0);
}

/* renders `frames` stereo frames into buf in 1024-frame pieces; the RMS in dBFS */
static double sr_render(sa1_audio_mixer *m, int16_t *buf, uint32_t frames)
{
    uint32_t k, done = 0;
    double s2 = 0;
    while (done < frames) {
        uint32_t n = frames - done > 1024u ? 1024u : frames - done;
        sa1_audio_mixer_render(m, buf + 2u * done, n);
        done += n;
    }
    for (k = 0; k < frames * 2u; k++) {
        double v = buf[k] / 32768.0;
        s2 += v * v;
    }
    return 10.0 * log10(s2 / (frames * 2.0) + 1e-20);
}

static int sr_maxdiff(const int16_t *x, const int16_t *y, uint32_t n)
{
    uint32_t k;
    int d = 0;
    for (k = 0; k < n; k++)
        if (abs(x[k] - y[k]) > d)
            d = abs(x[k] - y[k]);
    return d;
}

/* the fixed regression script: 4000 cues (PLAY of every SE tone in turn for two characters, a STOP every 7th), its output
   hashed (FNV-1a over the PCM); research probe_rules.c, the same numbers */
static unsigned long long sr_regress(sa1_audio *a)
{
    sa1_audio_mixer *m = sr_mixer(a, 0);
    static int16_t buf[2 * 512];
    unsigned long long h = 1469598103934665603ULL;
    uint32_t i, k, rng = 12345;
    if (!m)
        return 0;
    for (i = 0; i < 4000; i++) {
        uint32_t tone, ch;
        rng = rng * 1103515245u + 12345u;
        tone = (rng >> 8) % 1275u;
        ch = 1u + (i & 1u);
        sr_ev(m, ch, (i % 7u) == 6u ? SA1_EVENT_SOUND_STOP : SA1_EVENT_SOUND, tone);
        sa1_audio_mixer_render(m, buf, 512);
        for (k = 0; k < 1024; k++) {
            h ^= (unsigned short)buf[k];
            h *= 1099511628211ULL;
        }
    }
    sa1_audio_mixer_destroy(m);
    return h;
}

/* the stage's state while LOADING, at most limit_ms; the milliseconds waited */
static double sr_wait_ready(sa1_audio *a, int32_t stage, double limit_ms)
{
    double t0 = tu_now_ms();
    while (sa1_audio_stage_state(a, stage) == SA1_AUDIO_STAGE_LOADING && tu_now_ms() - t0 < limit_ms)
        slow_sleep(1);
    return tu_now_ms() - t0;
}

/* one tone rendered by an object whose own stage is `stage` (character 1, from a cold mixer) */
static void sr_reference(sa1_context *ctx, int32_t stage, uint32_t tone, int16_t *out, uint32_t frames)
{
    sa1_audio *r = sr_open(ctx, stage, 0);
    sa1_audio_mixer *m = sr_mixer(r, 0);
    memset(out, 0, (size_t)frames * 2u * sizeof(int16_t));
    if (m) {
        sr_ev(m, 1, SA1_EVENT_SOUND, tone);
        sr_render(m, out, frames);
        sa1_audio_mixer_destroy(m);
    }
    sa1_audio_close(r);
}

/* the failing range of (j): the sectors of SONICADV/<name> in the data track (its bytes in the track file) */
static int sr_fail_file(const char *name)
{
    sa1_disc d;
    uint8_t *data = NULL;
    uint32_t size = 0, lba = 0;
    char path[64];
    int ok = 0;
    snprintf(path, sizeof path, "SONICADV/%s", name);
    if (sa1_disc_open(g_main_ctx, g_gdi, &d) != SA1_OK)
        return 0;
    if (sa1_disc_read_file(&d, path, &data, &size) == SA1_OK && d.dir)
        lba = dir_lba(d.dir, d.dir_size, name);     /* d.dir: the SONICADV directory now */
    if (lba && lba >= d.tracks[d.hd].info.lba && size > 0) {
        const sa1_disc_track *t = &d.tracks[d.hd];
        snprintf(g_fail_track, sizeof g_fail_track, "%s", t->info.name);
        g_fail_from = (uint64_t)(lba - t->info.lba) * t->info.sector_size;
        g_fail_to = g_fail_from + (uint64_t)((size + 2047u) / 2048u) * t->info.sector_size;
        ok = 1;
    }
    sa1_mem_free(g_main_ctx, data);
    sa1_disc_close(&d);
    return ok;
}

static void check_stage_rules(void)
{
    static int16_t ba[2 * SR_RATE], bb[2 * SR_RATE], ref[2 * SR_RATE];
    const uint32_t N = SR_RATE / 2u;       /* 0.5 s */
    sa1_context *ctx;
    sa1_audio *a;
    sa1_audio_mixer *m;
    long allocs0 = __atomic_load_n(&g_allocs, __ATOMIC_RELAXED);
    long long b0, b1, bstart;
    double t0, ms, dbA, dbB;
    sa1_status st;
    uint32_t act0, act1;
    unsigned long long hash;

    __atomic_store_n(&g_slow_ms, 0, __ATOMIC_RELAXED);
    ctx = sr_ctx();
    CHECK(ctx != NULL);
    if (!ctx)
        return;
    /* (a) regression: without rules the fixed 4,000-cue script hashes as before (header version 4, the 0.9.4 baseline:
       SADC/build/research/super2/audio094a/probe_rules against base094's archive) */
    a = sr_open(ctx, SA1_AUDIO_STAGE_DEFAULT, 0);
    CHECK(a != NULL);
    if (!a) {
        sa1_shutdown(ctx);
        return;
    }
    hash = sr_regress(a);
    printf("  (a) the 4,000-cue script without rules: hash %016llx (0.9.4 baseline 1bea9caaae7a1f05)\n", hash);
    CHECK(hash == 0x1bea9caaae7a1f05ULL);

    /* (b) the whoosh rule: character A (1) gets {19, 1035, 1036}; B (2) none */
    m = sr_mixer(a, 0);
    CHECK(m != NULL);
    if (!m) {
        sa1_audio_close(a);
        sa1_shutdown(ctx);
        return;
    }
    CHECK(sa1_audio_stage_state(a, 19) == SA1_AUDIO_STAGE_ABSENT && sa1_audio_stage_state(a, 1) == SA1_AUDIO_STAGE_READY);
    b0 = bstart = __atomic_load_n(&g_bytes, __ATOMIC_RELAXED);
    t0 = tu_now_ms();
    st = sa1_audio_mixer_stage_rule(m, 1, 0, 19, 1035, 1036);
    ms = tu_now_ms() - t0;
    CHECK(st == SA1_OK && ms < 2.0);
    printf("  (b) sa1_audio_mixer_stage_rule(A, 0, 19, 1035, 1036): %s in %.3f ms on the caller's thread\n",
           sa1_status_string(st), ms);
    ms = sr_wait_ready(a, 19, WAIT_MS);
    sa1_audio_preload(a, SA1_AUDIO_PRELOAD_WAIT);
    b1 = __atomic_load_n(&g_bytes, __ATOMIC_RELAXED);
    CHECK(sa1_audio_stage_state(a, 19) == SA1_AUDIO_STAGE_READY);
    /* (k) memory: CHAOS7B.MLT bank 5 and the two decoded tones only */
    CHECK(b1 - b0 < 512 * 1024);
    printf("  (b) stage 19 READY after %.1f ms (loader thread); (k) the whoosh rule holds %+.3f MB (< 0.5 MB)\n", ms,
           (double)(b1 - b0) / (1024.0 * 1024.0));
    sr_ev(m, 1, SA1_EVENT_SOUND, 1035);
    act0 = sa1_audio_mixer_active(m);
    dbA = sr_render(m, ba, N);
    sr_reference(ctx, 19, 1035, ref, N);
    CHECK(act0 == 2u && dbA > -30.0 && sr_maxdiff(ba, ref, N * 2u) == 0);
    printf("  (b) A's 1035: %u voices, %.1f dBFS, max difference %d to an object opened on stage 19\n", (unsigned)act0, dbA,
           sr_maxdiff(ba, ref, N * 2u));
    sr_ev(m, 1, SA1_EVENT_SOUND_STOP, 1035);
    sr_render(m, ba, 4410);
    sa1_audio_mixer_stop(m, 0);
    sa1_audio_mixer_character(m, 1, (const float[3]){ 0, 0, 0 }, 1);
    sa1_audio_mixer_character(m, 2, (const float[3]){ 0, 0, 0 }, 1);
    sr_ev(m, 2, SA1_EVENT_SOUND, 1035);
    act0 = sa1_audio_mixer_active(m);
    dbB = sr_render(m, bb, N);
    CHECK(act0 == 0u && dbB < -90.0);
    printf("  (b) B's 1035 resolves from EMERALDCOAST.MLT as before: %u voices, %.1f dBFS\n", (unsigned)act0, dbB);

    /* (c) C5's toggle (gsa1_060_board.md R.4): 10 set-and-clear toggles while B's looping 1031 plays */
    sr_ev(m, 2, SA1_EVENT_SOUND, 1031);     /* EMERALDCOAST.MLT bank 5 request 4: a looping program */
    dbB = sr_render(m, bb, 4410);
    act0 = sa1_audio_mixer_active(m);
    {
        int k;
        double db2;
        t0 = tu_now_ms();
        for (k = 0; k < 10; k++) {
            sa1_audio_mixer_stage_rule(m, 1, 1, 8, 208, 238);
            sa1_audio_mixer_stage_rule(m, 1, 1, -1, 0, 0);
        }
        ms = tu_now_ms() - t0;
        act1 = sa1_audio_mixer_active(m);
        db2 = sr_render(m, bb, 4410);
        CHECK(ms < 2.0 && act0 > 0u && act1 == act0 && fabs(db2 - dbB) < 1.5);
        printf("  (c) 10 set-and-clear toggles: %.3f ms in all (< 2 ms); B's loop %u -> %u voices, %.1f -> %.1f dBFS\n",
               ms, (unsigned)act0, (unsigned)act1, dbB, db2);
    }
    sa1_audio_mixer_stop(m, 2);
    sa1_audio_mixer_character(m, 2, (const float[3]){ 0, 0, 0 }, 1);

    /* (d) the board's rules on A: {8, 208, 238} and {38, 690, 698} */
    b0 = __atomic_load_n(&g_bytes, __ATOMIC_RELAXED);
    CHECK(sa1_audio_mixer_stage_rule(m, 1, 1, 8, 208, 238) == SA1_OK);
    CHECK(sa1_audio_mixer_stage_rule(m, 1, 2, 38, 690, 698) == SA1_OK);
    sr_wait_ready(a, 8, WAIT_MS);
    sr_wait_ready(a, 38, WAIT_MS);
    sa1_audio_preload(a, SA1_AUDIO_PRELOAD_WAIT);
    CHECK(sa1_audio_stage_state(a, 8) == SA1_AUDIO_STAGE_READY && sa1_audio_stage_state(a, 38) == SA1_AUDIO_STAGE_READY);
    printf("  (d) stages 8 and 38 READY: %+.3f MB more; (k) the three rules (19, 8, 38) hold %+.3f MB in all\n",
           (double)(__atomic_load_n(&g_bytes, __ATOMIC_RELAXED) - b0) / (1024.0 * 1024.0),
           (double)(__atomic_load_n(&g_bytes, __ATOMIC_RELAXED) - bstart) / (1024.0 * 1024.0));
    sa1_audio_mixer_stop(m, 0);             /* every sound stops; the characters and their rules stay */
    sr_ev(m, 1, SA1_EVENT_SOUND, 237);
    dbA = sr_render(m, ba, N);
    sr_reference(ctx, 8, 237, ref, N);
    CHECK(dbA > -40.0 && sr_maxdiff(ba, ref, N * 2u) == 0);
    printf("  (d) A's 237 (snow): %.1f dBFS, max difference %d to an object on Ice Cap\n", dbA, sr_maxdiff(ba, ref, N * 2u));
    sa1_audio_mixer_stop(m, 0);
    sr_ev(m, 1, SA1_EVENT_SOUND, 691);
    dbA = sr_render(m, ba, N);
    sr_reference(ctx, 38, 691, ref, N);
    CHECK(dbA > -40.0 && sr_maxdiff(ba, ref, N * 2u) == 0);
    printf("  (d) A's 691 (sand): %.1f dBFS, max difference %d to an object on Sand Hill\n", dbA, sr_maxdiff(ba, ref, N * 2u));
    sa1_audio_mixer_stop(m, 1);
    sa1_audio_mixer_character(m, 1, (const float[3]){ 0, 0, 0 }, 1);
    CHECK(sa1_audio_mixer_stage_rule(m, 1, 0, 19, 1035, 1036) == SA1_OK);
    sr_ev(m, 2, SA1_EVENT_SOUND, 237);
    act0 = sa1_audio_mixer_active(m);
    sr_ev(m, 2, SA1_EVENT_SOUND, 691);
    act1 = sa1_audio_mixer_active(m);
    dbB = sr_render(m, bb, N);
    sr_reference(ctx, 1, 691, ref, N);
    CHECK(act0 == 0u && act1 > 0u && sr_maxdiff(bb, ref, N * 2u) == 0);
    printf("  (d) B: 237 silent as on Emerald Coast (%u voices), 691 = Emerald Coast's program as before (%u voices, "
           "%.1f dBFS)\n", (unsigned)act0, (unsigned)act1, dbB);
    sa1_audio_mixer_stop(m, 2);
    sa1_audio_mixer_character(m, 2, (const float[3]){ 0, 0, 0 }, 1);

    /* (e) sa1_audio_set_stage keeps the rules and their banks */
    CHECK(sa1_audio_set_stage(a, 1) == SA1_OK);
    sa1_audio_preload(a, SA1_AUDIO_PRELOAD_TONES | SA1_AUDIO_PRELOAD_WAIT);
    CHECK(sa1_audio_stage_state(a, 19) == SA1_AUDIO_STAGE_READY);
    sr_ev(m, 1, SA1_EVENT_SOUND, 1035);
    dbA = sr_render(m, ba, N);
    sr_reference(ctx, 19, 1035, ref, N);
    CHECK(sr_maxdiff(ba, ref, N * 2u) == 0);
    printf("  (e) after sa1_audio_set_stage(1): A's 1035 still identical to stage 19's (%.1f dBFS)\n", dbA);
    sa1_audio_mixer_stop(m, 0);
    sa1_audio_mixer_character(m, 1, (const float[3]){ 0, 0, 0 }, 1);
    sa1_audio_mixer_character(m, 2, (const float[3]){ 0, 0, 0 }, 1);

    /* (f) arguments and limits (19, 8, 38 held: a 4th stage fits, a 5th does not) */
    CHECK(sa1_audio_mixer_stage_rule(m, 2, 0, 15, 40, 53) == SA1_OK);
    CHECK(sa1_audio_mixer_stage_rule(m, 2, 1, 16, 40, 53) == SA1_ERR_LIMIT);
    CHECK(sa1_audio_mixer_stage_rule(m, 2, 1, 43, 40, 53) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_stage_rule(m, 2, 1, 19, 60, 50) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_stage_rule(m, 2, 1, 19, 1035, 1275) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_stage_rule(m, 2, SA1_AUDIO_MAX_STAGE_RULES, 19, 1035, 1036) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_stage_rule(m, 0, 0, 19, 1035, 1036) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_stage_rule(NULL, 2, 0, 19, 1035, 1036) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_audio_mixer_stage_rule(m, 2, 0, -1, 0, 0) == SA1_OK);
    CHECK(sa1_audio_stage_state(NULL, 19) == SA1_AUDIO_STAGE_ABSENT && sa1_audio_stage_state(a, 16) ==
          SA1_AUDIO_STAGE_ABSENT);
    /* stage -1 clears: A's whoosh rule cleared, its 1035 is Emerald Coast's (empty) again; set again: stage 19's */
    CHECK(sa1_audio_mixer_stage_rule(m, 1, 0, -1, 0, 0) == SA1_OK);
    sr_ev(m, 1, SA1_EVENT_SOUND, 1035);
    act0 = sa1_audio_mixer_active(m);
    CHECK(sa1_audio_mixer_stage_rule(m, 1, 0, 19, 1035, 1036) == SA1_OK);
    sr_ev(m, 1, SA1_EVENT_SOUND, 1035);
    act1 = sa1_audio_mixer_active(m);
    CHECK(act0 == 0u && act1 == 2u);
    sa1_audio_mixer_stop(m, 0);
    sa1_audio_mixer_character(m, 1, (const float[3]){ 0, 0, 0 }, 1);
    printf("  (f) a 4th extra stage ok, a 5th SA1_ERR_LIMIT; stage 43, first > last, last > 1274, slot %u, character 0, no "
           "mixer: SA1_ERR_INVALID_ARGUMENT; stage -1 clears (1035: %u voices cleared, %u set again)\n",
           (unsigned)SA1_AUDIO_MAX_STAGE_RULES, (unsigned)act0, (unsigned)act1);
    /* (g) sa1_audio_mixer_stop(mixer, A) forgets A's rules */
    CHECK(sa1_audio_mixer_stage_rule(m, 1, 0, 19, 1035, 1036) == SA1_OK);
    sa1_audio_mixer_stop(m, 1);
    sa1_audio_mixer_character(m, 1, (const float[3]){ 0, 0, 0 }, 1);
    sr_ev(m, 1, SA1_EVENT_SOUND, 1035);
    act0 = sa1_audio_mixer_active(m);
    CHECK(act0 == 0u);
    printf("  (g) after sa1_audio_mixer_stop(A): A's 1035 is Emerald Coast's again (%u voices)\n", (unsigned)act0);
    sa1_audio_mixer_destroy(m);
    sa1_audio_close(a);
    {
        /* MUSIC_ONLY: no banks, so no rules */
        sa1_audio *mo = sr_open(ctx, SA1_AUDIO_STAGE_DEFAULT, SA1_AUDIO_MUSIC_ONLY);
        sa1_audio_mixer *mm = sr_mixer(mo, 0);
        CHECK(mm != NULL && sa1_audio_mixer_stage_rule(mm, 1, 0, 19, 1035, 1036) == SA1_ERR_UNSUPPORTED &&
              sa1_audio_stage_state(mo, 19) == SA1_AUDIO_STAGE_ABSENT);
        sa1_audio_mixer_destroy(mm);
        sa1_audio_close(mo);
        printf("  (f) SA1_AUDIO_MUSIC_ONLY: SA1_ERR_UNSUPPORTED\n");
    }

    /* (h) a cue sent before the rule's banks are read waits (a fresh object; the rule and the cue at once) */
    a = sr_open(ctx, SA1_AUDIO_STAGE_DEFAULT, 0);
    m = sr_mixer(a, 0);
    CHECK(m != NULL);
    if (m) {
        int k, heard = -1, waited;
        int16_t blk[2 * 221];
        CHECK(sa1_audio_mixer_stage_rule(m, 1, 0, 19, 1035, 1036) == SA1_OK);
        waited = sa1_audio_stage_state(a, 19) == SA1_AUDIO_STAGE_LOADING;
        sr_ev(m, 1, SA1_EVENT_SOUND, 1035);
        for (k = 0; k < 200 && heard < 0; k++) {       /* 5 ms blocks, rendered in real time: 1 s */
            if (sr_render(m, blk, 221) > -60.0)
                heard = k;
            slow_sleep(5);
        }
        CHECK(heard >= 0 && heard * 5 < 500);
        printf("  (h) the 1035 sent %s the banks were read was heard after %d ms of rendering (at most 500 ms)\n",
               waited ? "before" : "(the loader was quick) when", heard * 5);
        sa1_audio_mixer_destroy(m);
    }
    sa1_audio_close(a);

    /* (i) SA1_AUDIO_SYNC_LOADS: the rule reads the MLT on the caller's thread; the cue plays at once, identical */
    a = sr_open(ctx, SA1_AUDIO_STAGE_DEFAULT, SA1_AUDIO_SYNC_LOADS);
    m = sr_mixer(a, 0);
    CHECK(m != NULL);
    if (m) {
        t0 = tu_now_ms();
        st = sa1_audio_mixer_stage_rule(m, 1, 0, 19, 1035, 1036);
        ms = tu_now_ms() - t0;
        CHECK(st == SA1_OK && sa1_audio_stage_state(a, 19) == SA1_AUDIO_STAGE_READY);
        sr_ev(m, 1, SA1_EVENT_SOUND, 1035);
        act0 = sa1_audio_mixer_active(m);
        dbA = sr_render(m, ba, N);
        sr_reference(ctx, 19, 1035, ref, N);
        CHECK(act0 == 2u && sr_maxdiff(ba, ref, N * 2u) == 0);
        printf("  (i) SYNC_LOADS: the rule took %.1f ms on the caller (the MLT read), READY at once; 1035: %u voices at "
               "once, identical (%.1f dBFS)\n", ms, (unsigned)act0, dbA);
        sa1_audio_mixer_destroy(m);
    }
    sa1_audio_close(a);

    /* (j) FAILED: the file callback fails every read inside SANDHILL.MLT; A's 691 (a program on Emerald Coast) stays
       silent, the stage is FAILED, exactly one warning on this (the mixer's) thread names stage, MLT and bank */
    CHECK(sr_fail_file("SANDHILL.MLT"));
    a = sr_open(ctx, SA1_AUDIO_STAGE_DEFAULT, 0);
    m = sr_mixer(a, 0);
    CHECK(m != NULL);
    if (m) {
        int i;
        g_rule_warns = g_rule_warns_away = 0;
        g_rule_warn[0] = 0;
        CHECK(sa1_audio_mixer_stage_rule(m, 1, 2, 38, 690, 698) == SA1_OK);
        sr_wait_ready(a, 38, WAIT_MS);
        CHECK(sa1_audio_stage_state(a, 38) == SA1_AUDIO_STAGE_FAILED);
        for (i = 0; i < 3; i++) {
            sr_ev(m, 1, SA1_EVENT_SOUND, 691);
            act0 = sa1_audio_mixer_active(m);
            dbA = sr_render(m, ba, 4410);
            CHECK(act0 == 0u && dbA < -90.0);
        }
        sr_ev(m, 2, SA1_EVENT_SOUND, 691);
        act1 = sa1_audio_mixer_active(m);
        CHECK(act1 > 0u);
        CHECK(g_rule_warns == 1 && g_rule_warns_away == 0 && strstr(g_rule_warn, "SANDHILL.MLT") &&
              strstr(g_rule_warn, "stage 38") && strstr(g_rule_warn, "bank 1"));
        printf("  (j) SANDHILL.MLT unreadable: stage 38 FAILED, A's 691 silent x3 (%.1f dBFS), B's 691 Emerald Coast's (%u "
               "voices); %d warning(s) on the mixer's thread (%d elsewhere): \"%s\"\n", dbA, (unsigned)act1,
               g_rule_warns, g_rule_warns_away, g_rule_warn);
        sa1_audio_mixer_destroy(m);
    }
    sa1_audio_close(a);

    /* (l) close with extra sets in every state (READY 19, FAILED 38, LOADING 8: set right before the close): every
       allocation returned (run under SA1_GUARD_ALLOC=1 for the guard heap's checks) */
    a = sr_open(ctx, SA1_AUDIO_STAGE_DEFAULT, 0);
    m = sr_mixer(a, 0);
    CHECK(m != NULL);
    if (m) {
        CHECK(sa1_audio_mixer_stage_rule(m, 1, 0, 19, 1035, 1036) == SA1_OK);
        sr_wait_ready(a, 19, WAIT_MS);
        CHECK(sa1_audio_mixer_stage_rule(m, 1, 2, 38, 690, 698) == SA1_OK);
        sr_wait_ready(a, 38, WAIT_MS);
        CHECK(sa1_audio_mixer_stage_rule(m, 1, 1, 8, 208, 238) == SA1_OK);
        sr_ev(m, 1, SA1_EVENT_SOUND, 237);   /* waits (LOADING) */
        sr_render(m, ba, 64);
        CHECK(sa1_audio_stage_state(a, 19) == SA1_AUDIO_STAGE_READY && sa1_audio_stage_state(a, 38) ==
              SA1_AUDIO_STAGE_FAILED);
        printf("  (l) closing with the states (2 READY, 3 FAILED, 1 LOADING) of stage 19: %d, 38: %d, 8: %d\n",
               (int)sa1_audio_stage_state(a, 19), (int)sa1_audio_stage_state(a, 38), (int)sa1_audio_stage_state(a, 8));
    }
    sa1_audio_close(a);         /* frees the mixer too */
    g_fail_track[0] = 0;
    sa1_shutdown(ctx);
    CHECK(__atomic_load_n(&g_allocs, __ATOMIC_RELAXED) == allocs0);
    printf("  (l) after close and shutdown: %ld allocations not freed (guard heap %s)\n",
           __atomic_load_n(&g_allocs, __ATOMIC_RELAXED) - allocs0, ga_enabled() ? "on" : "off");
}

/* ==== a tone started again while it plays (super2_plan.md 10.3: "the ring sound effect gets really loud when collecting
 * multiple rings quickly") ===================================================================================================
 * The game's rule (1ST_READ asm, read 2026-10-26): 8C0940DA (tone, id, pri, volofs) takes a slot of the sound queue
 * 0x8C79E40C from 8C093F02(pri, tone): starting at the cursor B_8C79EC44 it takes the first of the 32 normal slots that is
 * free (timer 0) or whose priority word is <= pri (8C0940DA stores -1 there, so every one-shot slot qualifies for the
 * ring's pri 0); the cursor moves on by one. Nothing compares the tone. The sound task 8C0948A0 starts a new entry with
 * 8C093B2A -> 8C093140 (bank select, program, pan - the ring alternates 34 / 94 - on MIDI channel slot & 15 of port
 * slot >> 4) and 8C093360 (note on, note 60, on that channel); its 120-frame timer ends in a note off (8C0933BA) on that
 * channel only. So every ring is its own channel and 10 rings 50 ms apart sound together on the Dreamcast as they do in
 * the mixer (the Manatee driver's own voice allocation, on the ARM side, was not examined: UNVERIFIED, but each request
 * is a different channel). 8C094330 / 8C0943E0 (keep-alive) find the entry of the same id and tone (8C094280) and only
 * refresh it: libsa1's KEEP cue. SA1_AUDIO_MIX_RESTART_REPEATS (header version 5) is the host's option that restarts
 * instead; these cases measure both (the numbers before / after for the owner). */
typedef struct rp_meas {
    double peak_db, rms_db, win_db;
    uint32_t clipped, maxv;
} rp_meas;

/* n cues of `tone` every `gap` steps (1/60 s), characters alternating with two_chars, `tone2` on odd cues if set, `fn`
   the play function (0: 8C0940DA); `secs` rendered; voices sampled after each step */
static rp_meas rp_script(sa1_audio *a, uint32_t mixflags, uint32_t tone, int n, int gap, int two_chars, uint32_t tone2,
                         uint32_t fn, double secs)
{
    const uint32_t step = SR_RATE / 60u;
    sa1_audio_mixer *m = sr_mixer(a, mixflags);
    uint32_t total = (uint32_t)(secs * SR_RATE), done = 0, k;
    int16_t *buf = (int16_t *)calloc((size_t)total * 2u, sizeof(int16_t));
    double s2 = 0, best = 0;
    int i = 0, pk = 0;
    rp_meas r;
    memset(&r, 0, sizeof r);
    if (!m || !buf) {
        sa1_audio_mixer_destroy(m);
        free(buf);
        r.peak_db = 99.0;
        return r;
    }
    while (done < total) {
        uint32_t chunk = step;
        while (i < n && done == (uint32_t)(i * gap) * step) {
            sr_ev_fn(m, two_chars ? (sa1_character_id)(1 + (i & 1)) : 1, SA1_EVENT_SOUND, (tone2 && (i & 1)) ? tone2 : tone,
                     fn);
            i++;
        }
        if (done + chunk > total)
            chunk = total - done;
        sa1_audio_mixer_render(m, buf + 2u * done, chunk);
        if (sa1_audio_mixer_active(m) > r.maxv)
            r.maxv = sa1_audio_mixer_active(m);
        done += chunk;
    }
    for (k = 0; k < total * 2u; k++) {
        int v = abs(buf[k]);
        double f = buf[k] / 32768.0;
        if (v > pk)
            pk = v;
        if (v >= 32767)
            r.clipped++;
        s2 += f * f;
    }
    {
        uint32_t w = SR_RATE / 20u, s;    /* the loudest 50 ms window (25 ms hops) */
        for (s = 0; s + w <= total; s += w / 2u) {
            double q = 0;
            for (k = 2u * s; k < 2u * (s + w); k++)
                q += (buf[k] / 32768.0) * (buf[k] / 32768.0);
            q /= 2.0 * w;
            if (q > best)
                best = q;
        }
    }
    r.peak_db = 20.0 * log10(pk / 32768.0 + 1e-12);
    r.rms_db = 10.0 * log10(s2 / (2.0 * total) + 1e-20);
    r.win_db = 10.0 * log10(best + 1e-20);
    free(buf);
    sa1_audio_mixer_destroy(m);
    return r;
}

static void rp_show(const char *what, rp_meas r, const rp_meas *one)
{
    printf("    %-46s peak %6.1f dBFS, loudest 50 ms %6.1f dBFS, clipped %4u, voices <= %2u\n", what, r.peak_db, r.win_db,
           (unsigned)r.clipped, (unsigned)r.maxv);
    if (one)
        printf("    %-46s (%+.1f dB peak, %+.1f dB loudest 50 ms against one ring)\n", "", r.peak_db - one->peak_db,
               r.win_db - one->win_db);
}

static void check_repeats(void)
{
    static const int k_gap[4] = { 1, 3, 6, 10 };
    sa1_context *ctx = sr_ctx();
    sa1_audio *a = ctx ? sr_open(ctx, SA1_AUDIO_STAGE_DEFAULT, 0) : NULL;
    rp_meas one, r;
    int g;
    char what[80];
    CHECK(a != NULL);
    if (!a) {
        sa1_shutdown(ctx);
        return;
    }
    {
        sa1_audio_sound s;
        s.struct_size = sizeof s;
        CHECK(sa1_audio_sound_info(a, 7, &s) == SA1_OK && s.kind == SA1_AUDIO_SOUND_PROGRAM && s.bank == 0u &&
              s.request == 7u && s.num_layers == 1u && !s.loops);
        printf("  the ring: tone 7 = bank 0 request 7, %u layer, %.3f s, %s\n", (unsigned)s.num_layers, (double)s.duration,
               s.loops ? "looping" : "one-shot");
    }
    one = rp_script(a, 0, 7, 1, 0, 0, 0, 0, 3.0);
    printf("  the game's rule (no flag; every call its own slot and MIDI channel: the rings pile up):\n");
    rp_show("one ring", one, NULL);
    for (g = 0; g < 4; g++) {
        r = rp_script(a, 0, 7, 10, k_gap[g], 0, 0, 0, 3.5);
        snprintf(what, sizeof what, "10 rings %d step%s apart", k_gap[g], k_gap[g] == 1 ? "" : "s");
        rp_show(what, r, &one);
        if (k_gap[g] == 3)
            CHECK(r.maxv >= 8u && r.win_db > one.win_db + 5.0);     /* the pile-up the owner hears */
    }
    printf("  SA1_AUDIO_MIX_RESTART_REPEATS (the host's option: the same one-shot restarts):\n");
    for (g = 0; g < 4; g++) {
        r = rp_script(a, SA1_AUDIO_MIX_RESTART_REPEATS, 7, 10, k_gap[g], 0, 0, 0, 3.5);
        snprintf(what, sizeof what, "10 rings %d step%s apart", k_gap[g], k_gap[g] == 1 ? "" : "s");
        rp_show(what, r, &one);
        CHECK(r.maxv == 1u && r.clipped == 0u && fabs(r.peak_db - one.peak_db) < 0.2 && fabs(r.win_db - one.win_db) < 0.5);
    }
    r = rp_script(a, SA1_AUDIO_MIX_RESTART_REPEATS, 7, 10, 3, 1, 0, 0, 3.5);
    rp_show("two characters' rings, 3 steps apart", r, &one);
    CHECK(r.maxv == 2u && r.win_db > one.win_db + 1.0);             /* another character's ring still overlaps */
    r = rp_script(a, SA1_AUDIO_MIX_RESTART_REPEATS, 7, 10, 3, 0, 33, 0, 3.5);
    rp_show("rings and landings (33) in turn, 3 steps", r, &one);
    CHECK(r.maxv == 2u);                                           /* other tones still overlap */
    r = rp_script(a, SA1_AUDIO_MIX_RESTART_REPEATS, 7, 10, 3, 0, 0, 0x8C09413Au, 3.5);
    rp_show("positional rings (8C09413A), 3 steps apart", r, &one);
    CHECK(r.maxv >= 8u);                                           /* positional sounds are not restarted */
    {
        /* the flag at run time (sa1_audio_mixer_flags), and its argument check */
        sa1_audio_mixer *m = sr_mixer(a, 0);
        int16_t blk[2 * 735];
        uint32_t v0, v1;
        CHECK(m != NULL && sa1_audio_mixer_flags(m, SA1_AUDIO_MIX_RESTART_REPEATS) == SA1_OK);
        sr_ev(m, 1, SA1_EVENT_SOUND, 7);
        sa1_audio_mixer_render(m, blk, 735);
        sr_ev(m, 1, SA1_EVENT_SOUND, 7);
        sa1_audio_mixer_render(m, blk, 735);
        v0 = sa1_audio_mixer_active(m);
        CHECK(sa1_audio_mixer_flags(m, 0) == SA1_OK);
        sr_ev(m, 1, SA1_EVENT_SOUND, 7);
        sa1_audio_mixer_render(m, blk, 735);
        v1 = sa1_audio_mixer_active(m);
        CHECK(v0 == 1u && v1 == 2u && sa1_audio_mixer_flags(m, 0x80000000u) == SA1_ERR_INVALID_ARGUMENT);
        printf("  sa1_audio_mixer_flags: with the flag a second ring leaves %u voice, without it a third makes %u\n",
               (unsigned)v0, (unsigned)v1);
        sa1_audio_mixer_destroy(m);
    }
    sa1_audio_close(a);
    sa1_shutdown(ctx);
}

/* ==== (m) the whoosh end to end: Super worlds' events through a mixer with the rule (super2_plan.md 6.3 (m), review R.15)
 * World helpers copied from tests/super_test.c (scene_grid, mk, the pair at a forced speed: playerwk.spd.x written before a
 * pair, the aura reads it after the player in each step); super_test.c is 094a-lib-anim's file and is not edited. */
struct sa1g_world;
struct sa1g_world *sa1_character_world(const sa1_context *ctx, sa1_character_id id);
int sa1g_world_rd32(const struct sa1g_world *w, uint32_t guest, uint32_t *v);
int sa1g_world_wr32(struct sa1g_world *w, uint32_t guest, uint32_t v);
#define WW_PLAYER_PWP 0x8C78C668u       /* playerpwp[8] */

static int ww_grid(sa1_context *ctx)
{
    sa1_collision_tri tri[2];
    static const float q[4][3] = { { -3000, 0, -3000 }, { 3000, 0, -3000 }, { 3000, 0, 3000 }, { -3000, 0, 3000 } };
    memset(tri, 0, sizeof tri);
    memcpy(tri[0].v[0], q[0], 12);
    memcpy(tri[0].v[1], q[2], 12);
    memcpy(tri[0].v[2], q[1], 12);
    memcpy(tri[1].v[0], q[0], 12);
    memcpy(tri[1].v[1], q[3], 12);
    memcpy(tri[1].v[2], q[2], 12);
    tri[0].surface = tri[1].surface = SA1_SURFACE_SOLID;
    return SA1_FAILED(sa1_collision_set_static(ctx, tri, 2)) ? -1 : 0;
}

static sa1_character_id ww_mk(sa1_context *ctx, int32_t kind, uint32_t flags, uint32_t super_flags)
{
    static const float at[3] = { 0.0f, 0.5f, 0.0f };
    sa1_character_desc d;
    sa1_character_id id = 0;
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    d.kind = kind;
    d.flags = flags | SA1_CREATE_WORLD | SA1_CREATE_SA1_SPACE | SA1_CREATE_ANGLES | SA1_CREATE_START_RINGS |
              SA1_CREATE_SUPER;
    d.start_rings = 50;
    d.super_flags = super_flags;
    memcpy(d.position, at, sizeof at);
    if (SA1_FAILED(sa1_character_create_ex(ctx, &d, &id))) {
        printf("    create: %s\n", sa1_last_error(ctx));
        return 0;
    }
    return id;
}

typedef struct ww_count {
    uint32_t play35, play36, stop35, stop36;
    uint32_t other[8], nother;          /* the other SOUND tones of the run (distinct, at most 8) */
} ww_count;

/* one pair (speed forced first when spd >= 0); its sound events go to the mixer (if any) and are counted */
static int ww_pair(sa1_context *ctx, sa1_character_id id, float spd, sa1_audio_mixer *m, ww_count *c)
{
    sa1_input in;
    sa1_event ev[64];
    uint32_t n, k;
    if (spd >= 0.0f) {
        struct sa1g_world *w = sa1_character_world(ctx, id);
        uint32_t pwp = 0, v;
        if (!w || sa1g_world_rd32(w, WW_PLAYER_PWP, &pwp) != 0 || pwp == 0)
            return -1;
        memcpy(&v, &spd, 4);
        sa1g_world_wr32(w, pwp + 0x38u, v);
    }
    memset(&in, 0, sizeof in);
    in.struct_size = sizeof in;
    if (sa1_character_tick(ctx, id, &in, 1.0f / 30.0f, NULL, NULL) != SA1_OK)
        return -1;
    do {
        n = 0;
        sa1_poll_events(ctx, ev, 64, &n);
        for (k = 0; k < n; k++) {
            if (ev[k].type == SA1_EVENT_SOUND || ev[k].type == SA1_EVENT_SOUND_STOP) {
                int play = ev[k].type == SA1_EVENT_SOUND;
                if (ev[k].id == 1035u)
                    *(play ? &c->play35 : &c->stop35) += 1u;
                if (ev[k].id == 1036u)
                    *(play ? &c->play36 : &c->stop36) += 1u;
                if (play && ev[k].id != 1035u && ev[k].id != 1036u) {
                    uint32_t j;
                    for (j = 0; j < c->nother && c->other[j] != ev[k].id; j++)
                        ;
                    if (j == c->nother && c->nother < 8u)
                        c->other[c->nother++] = ev[k].id;
                }
            }
            if (m)
                sa1_audio_mixer_event(m, &ev[k]);
        }
    } while (n == 64);
    return 0;
}

static void check_whoosh_world(void)
{
    static const char *const k_name[3] = { "kind 8 (Vs. Perfect Chaos)", "the General form (aura on, SPEED_RUN)",
                                           "the General form, SA1_SUPOPT_NO_AURA" };
    static int16_t buf[2 * 1470];
    sa1_context *ctx = sr_ctx();
    sa1_load_options lo;
    sa1_coords co;
    sa1_audio *a;
    int world, rule;
    if (!ctx) {
        CHECK(0);
        return;
    }
    memset(&co, 0, sizeof co);
    co.struct_size = sizeof co;
    sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &co);
    sa1_set_coords(ctx, &co);
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    lo.characters = (1u << SA1_CHAR_SONIC) | (1u << SA1_CHAR_SUPER_SONIC);
    lo.flags = SA1_LOAD_DEFAULT | SA1_LOAD_SUPER_FORMS;
    if (SA1_FAILED(sa1_load_disc(ctx, g_gdi, &lo)) || ww_grid(ctx) != 0) {
        printf("  load / collision: %s\n", sa1_last_error(ctx));
        CHECK(0);
        sa1_shutdown(ctx);
        return;
    }
    a = sr_open(ctx, SA1_AUDIO_STAGE_DEFAULT, 0);     /* G-SA1's object: Emerald Coast's stage banks */
    CHECK(a != NULL);
    /* each world twice, the same script: without the rule, then with it (the worlds are deterministic, so the voices the
       rule adds are the whoosh's; the General worlds also play other sounds, e.g. the landing 33 after the start) */
    for (world = 0; world < 3 && a; world++) {
        ww_count c[2];
        uint32_t v35[2] = { 0, 0 }, v36[2] = { 0, 0 }, v_end[2] = { 99, 99 };
        double db35[2] = { -200.0, -200.0 }, db36[2] = { -200.0, -200.0 };
        int bad = 0;
        memset(c, 0, sizeof c);
        for (rule = 0; rule < 2; rule++) {
            sa1_character_id id = world == 0 ? ww_mk(ctx, SA1_CHAR_SUPER_SONIC, 0, SA1_SUPER_NO_DRAIN) :
                                  ww_mk(ctx, SA1_CHAR_SONIC, SA1_CREATE_SUPER_CAPABLE,
                                        SA1_SUPER_GENERAL | SA1_SUPER_START | SA1_SUPER_NO_DRAIN);
            sa1_audio_mixer *m = sr_mixer(a, 0);
            uint32_t j;
            int i;
            if (!id || !m) {
                bad = 1;
                sa1_audio_mixer_destroy(m);
                if (id)
                    sa1_character_destroy(ctx, id);
                continue;
            }
            if (world > 0)
                sa1_character_set_super_option(ctx, id, SA1_SUPOPT_SPEED, SA1_SUPER_SPEED_RUN);
            if (world == 2)
                sa1_character_set_super_option(ctx, id, SA1_SUPOPT_NO_AURA, 1);
            sa1_audio_mixer_character(m, id, (const float[3]){ 0, 0, 0 }, 1);
            if (rule) {
                CHECK(sa1_audio_mixer_stage_rule(m, id, 0, 19, 1035, 1036) == SA1_OK);
                sr_wait_ready(a, 19, WAIT_MS);
                sa1_audio_preload(a, SA1_AUDIO_PRELOAD_WAIT);
            }
            for (i = 0; i < 10 && !bad; i++) {            /* standing */
                bad |= ww_pair(ctx, id, -1.0f, m, &c[rule]);
                sr_render(m, buf, 1470);
            }
            bad |= ww_pair(ctx, id, 8.5f, m, &c[rule]);    /* above 7: SOUND 1035 */
            v35[rule] = sa1_audio_mixer_active(m);
            db35[rule] = sr_render(m, buf, 1470);
            bad |= ww_pair(ctx, id, 14.5f, m, &c[rule]);   /* above 13: SOUND_STOP 1035, SOUND 1036 */
            v36[rule] = sa1_audio_mixer_active(m);
            db36[rule] = sr_render(m, buf, 1470);
            bad |= ww_pair(ctx, id, 2.0f, m, &c[rule]);    /* below 6: SOUND_STOP 1036 */
            for (i = 0; i < 300; i++)                      /* 10 s: the releases fade out */
                sr_render(m, buf, 1470);
            v_end[rule] = sa1_audio_mixer_active(m);
            printf("  %s, %s: SOUND 1035 x%u, 1036 x%u, SOUND_STOP 1035 x%u, 1036 x%u; voices after 1035 %u (%.1f dBFS), "
                   "after 1036 %u (%.1f dBFS), 10 s after the stop %u; other tones:", k_name[world],
                   rule ? "with the rule {19, 1035, 1036}" : "without a rule", (unsigned)c[rule].play35,
                   (unsigned)c[rule].play36, (unsigned)c[rule].stop35, (unsigned)c[rule].stop36, (unsigned)v35[rule],
                   db35[rule], (unsigned)v36[rule], db36[rule], (unsigned)v_end[rule]);
            for (j = 0; j < c[rule].nother; j++)
                printf(" %u", (unsigned)c[rule].other[j]);
            printf("%s\n", c[rule].nother ? "" : " none");
            sa1_audio_mixer_destroy(m);
            sa1_character_destroy(ctx, id);
        }
        CHECK(!bad);
        if (world < 2) {
            CHECK(c[0].play35 == 1u && c[0].play36 == 1u && c[0].stop35 >= 1u && c[0].stop36 >= 1u);
            CHECK(c[1].play35 == 1u && c[1].play36 == 1u && c[1].stop35 >= 1u && c[1].stop36 >= 1u);
            /* with the rule: 1035's two layers, then 1036's two (1035's release still fading), then nothing */
            CHECK(v35[1] == v35[0] + 2u && db35[1] > -40.0 && v36[1] >= v36[0] + 2u && db36[1] > -40.0 && v_end[1] == 0u);
            /* without it: Emerald Coast's bank 5 has nothing there: no whoosh (only the other sounds, if any) */
            CHECK(db35[0] < -60.0 && db36[0] < -60.0);
        } else {
            /* no aura task, no whoosh (the game's rule; super2_plan.md 10 O7) */
            CHECK(c[0].play35 == 0u && c[0].play36 == 0u && c[1].play35 == 0u && c[1].play36 == 0u &&
                  v35[1] == v35[0] && v36[1] == v36[0]);
        }
    }
    sa1_audio_close(a);
    sa1_shutdown(ctx);
}

int main(int argc, char **argv)
{
    const char *gdi = NULL;
    const char *outdir = "build/audio_test";
    sa1_host host;
    sa1_context *ctx = NULL;
    sa1_audio *a = NULL;
    sa1_audio_desc d;
    sa1_status st;
    char path[512];
    FILE *csv;
    double t0;
    int arg, npos = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    note_main_thread();
    for (arg = 1; arg < argc; arg++) {
        if (!strcmp(argv[arg], "--only") && arg + 1 < argc)
            g_only = argv[++arg];
        else if (!strcmp(argv[arg], "--skip") && arg + 1 < argc)
            g_skip = argv[++arg];
        else if (npos++ == 0)
            gdi = argv[arg];
        else
            outdir = argv[arg];
    }
    if (!gdi)
        gdi = getenv("SA1_TEST_GDI");
    if (!gdi || !*gdi) {
        printf("usage: audio_test <game.gdi> [outdir] [--only NAME[,NAME]] [--skip NAME[,NAME]]   (or set SA1_TEST_GDI)\n");
        return 2;
    }
    g_gdi = gdi;
    tu_mkdir("build");
    tu_mkdir(outdir);
    snprintf(path, sizeof path, "%s/layers", outdir);
    tu_mkdir(path);
    g_mixbuf = (int16_t *)malloc((size_t)44100u * 6u * 2u * sizeof(int16_t));
    sa1_host_defaults(&host);
    host.alloc = count_alloc;
    host.free = count_free;
    host.log = log_cb;
    host.log_level = SA1_LOG_INFO;
    CHECK(sa1_init(&host, &ctx) == SA1_OK);
    if (!ctx || !g_mixbuf)
        return 1;
    g_main_ctx = ctx;
    /* argument checks */
    memset(&d, 0, sizeof d);
    d.struct_size = sizeof d;
    CHECK(sa1_audio_open(ctx, &d, &a) == SA1_ERR_INVALID_ARGUMENT && a == NULL);
    d.gdi_path = gdi;
    d.stage = 99;
    CHECK(sa1_audio_open(ctx, &d, &a) == SA1_ERR_INVALID_ARGUMENT && a == NULL);
    CHECK(strcmp(sa1_audio_stage_file(6, 0), "SKYDECK_E.MLT") == 0);
    CHECK(strcmp(sa1_audio_stage_file(6, SA1_AUDIO_JAPANESE), "SKYDECK.MLT") == 0);
    CHECK(strcmp(sa1_audio_stage_file(11, 0), "DRYMOUNTAIN.MLT") == 0);
    CHECK(sa1_audio_stage_file(43, 0) == NULL);

    d.stage = SA1_AUDIO_STAGE_DEFAULT;
    t0 = tu_now_ms();
    st = sa1_audio_open(ctx, &d, &a);
    printf("sa1_audio_open: %s (%.0f ms)\n", sa1_status_string(st), tu_now_ms() - t0);
    CHECK(st == SA1_OK);
    if (!a)
        return 1;
    CHECK(a->num_ranges == 63u);
    CHECK(a->num_3d == 35u);
    {
        sa1_audio_sound s;
        s.struct_size = sizeof s;
        CHECK(sa1_audio_sound_info(a, 1500, &s) == SA1_ERR_NOT_FOUND);
        CHECK(sa1_audio_sound_info(a, 1300, &s) == SA1_OK && s.kind == SA1_AUDIO_SOUND_VOICE && s.voice == 1755u);
        CHECK(sa1_audio_sound_info(a, 1216, &s) == SA1_OK && s.kind == SA1_AUDIO_SOUND_NONE && s.bank == 6u);
        CHECK(sa1_audio_sound_info(a, 34, &s) == SA1_OK && s.bank == 0u && s.request == 34u);
        CHECK(sa1_audio_sound_info(a, 772, &s) == SA1_OK && s.radius == 200.0f);
    }
    if (want("banks")) {
        snprintf(path, sizeof path, "%s/tones.csv", outdir);
        csv = fopen(path, "w");
        if (csv)
            fprintf(csv, "tone,kind,bank,request,file,layers,duration,loops,layer:samples:rate:loop:gain:pan...\n");
        t0 = tu_now_ms();
        printf("Sonic/Tails player bank (tones 723-736):\n");
        check_bank(ctx, a, gdi, "P_SONICTAILS.MLT", 3, 723, 14, outdir, csv);
        printf("Sonic/Tails voice bank (tones 1112-1144):\n");
        check_bank(ctx, a, gdi, "V_SONICTAILS_E.MLT", 6, 1112, 33, outdir, csv);
        printf("common bank 0 (tones 0-39):\n");
        check_bank(ctx, a, gdi, "EMERALDCOAST.MLT", 0, 0, 40, outdir, csv);
        printf("common bank 2 (tones 699-722):\n");
        check_bank(ctx, a, gdi, "EMERALDCOAST.MLT", 2, 699, 24, outdir, csv);
        printf("  banks checked and written in %.0f ms\n", tu_now_ms() - t0);
        if (csv)
            fclose(csv);
    }
    if (want("sequences")) {
        printf("E-102 sequence tones:\n");
        check_sequences(a, outdir);
    }
    if (want("cues")) {
        printf("cues:\n");
        check_cues(a);
    }
    if (want("streams")) {
        printf("voices and music:\n");
        check_streams(a, outdir);
    }
    if (want("mixer")) {
        printf("mixer:\n");
        check_mixer(a, outdir);
    }
    if (want("music_mixer")) {
        printf("music cues and the host duck:\n");
        check_music_mixer(a);
    }
    if (want("music_state")) {
        printf("the game's music state (header version 4):\n");
        check_music_state(a);
    }
    if (want("robustness")) {
        printf("robustness:\n");
        check_robustness(a);
    }
    /* every tone of the game resolves without error (stage Emerald Coast, all characters) */
    if (want("all_tones")) {
        uint32_t t, kinds[4] = { 0, 0, 0, 0 };
        for (t = 0; t <= SA1A_MAX_TONE; t++) {
            sa1_audio_sound s;
            s.struct_size = sizeof s;
            CHECK(sa1_audio_sound_info(a, t, &s) == SA1_OK);
            if (s.kind >= 0 && s.kind < 4)
                kinds[s.kind]++;
        }
        printf("all tones: %u none, %u programs, %u sequences, %u voices\n", (unsigned)kinds[0], (unsigned)kinds[1],
               (unsigned)kinds[2], (unsigned)kinds[3]);
    }
    /* another stage */
    if (want("stage_change")) {
        CHECK(sa1_audio_set_stage(a, 26) == SA1_OK);
        sa1_audio_sound s;
        s.struct_size = sizeof s;
        CHECK(sa1_audio_sound_info(a, 7, &s) == SA1_OK && s.file && strcmp(s.file, "STATIONSQUARE.MLT") == 0);
        CHECK(sa1_audio_sound_info(a, 723, &s) == SA1_OK && s.kind == SA1_AUDIO_SOUND_PROGRAM);
    }
    sa1_audio_close(a);
    if (want("music_info")) {
        printf("the music list (SA1_AUDIO_MUSIC_ONLY, sa1_audio_music_info):\n");
        check_music_info(ctx, gdi);
    }
    if (want("stage_rules")) {
        printf("stage rules (header version 5: a character's tones from another stage's banks):\n");
        check_stage_rules();
    }
    if (want("repeats")) {
        printf("a tone started again while it plays (the ring; SA1_AUDIO_MIX_RESTART_REPEATS):\n");
        check_repeats();
    }
    if (want("whoosh_world")) {
        printf("the whoosh end to end (Super worlds' events through a mixer with the rule):\n");
        check_whoosh_world();
    }
    g_main_ctx = NULL;
    sa1_shutdown(ctx);
    if (want("stalls")) {
        printf("no stalls (a slow disc, the session timed call by call):\n");
        check_stalls(gdi);
        check_deferred(gdi);
    }
    free(g_mixbuf);
    CHECK(g_allocs == 0);
    if (g_allocs)
        printf("  %ld allocations not freed\n", g_allocs);
    printf("audio_test: %d passed, %d failed; WAV files in %s\n", g_pass, g_fail, outdir);
    return g_fail ? 1 : 0;
}
