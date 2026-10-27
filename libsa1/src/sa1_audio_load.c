/*
 * libsa1 audio: the loader thread, the clip cache and the locks (sa1_audio.h "Loader thread, clip cache, locks").
 *
 * G-SA1 0.3 froze whenever a clip started cold: the mixer read the whole ADX from the disc image on the caller's
 * thread (TIMER.ADX when the drowning countdown starts, voices, music; 27-430 ms cold) and decoded tones on first use
 * (sequences up to 5 ms). Here every file read and every tone decode happens on the loader thread:
 *   - the clip cache: SA1A_CLIP_SLOTS entries of whole ADX files with reference counts. Pinned entries (the jingles
 *     preloaded at open) stay; the others are evicted least recently used first when the slots or the byte budget run
 *     out (only entries without references that are read or failed);
 *   - the queue is the cache itself (entries waiting to be read, oldest first), then the tones the mixer asked for,
 *     then the tone preload (every SE tone in order);
 *   - sa1_audio_preload / its flags at open queue the preloads; PRELOAD_WAIT waits on the loader.
 * Without the thread (SA1_AUDIO_SYNC_LOADS) the same functions do the work on the caller's thread.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include "sa1_audio.h"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <pthread.h>
#endif

#define SA1A_CLIP_SLOTS   48u
#define SA1A_CLIP_BUDGET  (64u << 20)   /* bytes of unpinned files kept */

#define CL_FREE    0
#define CL_QUEUED  1
#define CL_LOADING 2
#define CL_READY   3
#define CL_FAILED  4

/* the jingles the characters' code starts (8C107C80 / 8C04EF52 calls in 1ST_READ: drowning countdown, invincibility,
 * speed up, Tails' race win, Big's fish caught / lost) and the 1-up */
static const char *const k_jingles[] = { "TIMER.ADX", "INVNCIBL.ADX", "SPEEDUP.ADX", "ONE_UP.ADX", "RNDCLEAR.ADX",
                                         "FISHGET.ADX", "FISHMISS.ADX" };

/* ---- platform ----------------------------------------------------------------------------------------------- */
#if defined(_WIN32)
typedef SRWLOCK sa1a_mutex;
typedef CONDITION_VARIABLE sa1a_cond;
typedef HANDLE sa1a_thread;
static void mtx_init(sa1a_mutex *m) { InitializeSRWLock(m); }
static void mtx_free(sa1a_mutex *m) { (void)m; }
static void mtx_lock(sa1a_mutex *m) { AcquireSRWLockExclusive(m); }
static int mtx_trylock(sa1a_mutex *m) { return TryAcquireSRWLockExclusive(m) != 0; }
static void mtx_unlock(sa1a_mutex *m) { ReleaseSRWLockExclusive(m); }
static void cnd_init(sa1a_cond *c) { InitializeConditionVariable(c); }
static void cnd_free(sa1a_cond *c) { (void)c; }
static void cnd_wait(sa1a_cond *c, sa1a_mutex *m) { SleepConditionVariableSRW(c, m, INFINITE, 0); }
static void cnd_broadcast(sa1a_cond *c) { WakeAllConditionVariable(c); }
#else
typedef pthread_mutex_t sa1a_mutex;
typedef pthread_cond_t sa1a_cond;
typedef pthread_t sa1a_thread;
static void mtx_init(sa1a_mutex *m) { pthread_mutex_init(m, NULL); }
static void mtx_free(sa1a_mutex *m) { pthread_mutex_destroy(m); }
static void mtx_lock(sa1a_mutex *m) { pthread_mutex_lock(m); }
static int mtx_trylock(sa1a_mutex *m) { return pthread_mutex_trylock(m) == 0; }
static void mtx_unlock(sa1a_mutex *m) { pthread_mutex_unlock(m); }
static void cnd_init(sa1a_cond *c) { pthread_cond_init(c, NULL); }
static void cnd_free(sa1a_cond *c) { pthread_cond_destroy(c); }
static void cnd_wait(sa1a_cond *c, sa1a_mutex *m) { pthread_cond_wait(c, m); }
static void cnd_broadcast(sa1a_cond *c) { pthread_cond_broadcast(c); }
#endif

typedef struct clip_entry {
    int32_t    state;           /* CL_* */
    int32_t    source;          /* SA1_AUDIO_SOURCE_* */
    uint32_t   index;
    sa1_status status;          /* CL_FAILED: why */
    uint8_t   *data;
    uint32_t   size;
    sa1a_adx   h;
    uint32_t   refs;
    int32_t    pinned;
    int32_t    reported;        /* a failure was handed to someone (one log line) */
    uint64_t   stamp;           /* queue order, then last use (LRU) */
} clip_entry;

struct sa1a_loader {
    sa1a_mutex  m, tone, disc;
    sa1a_cond   wake, done;
    sa1a_thread th;
    int32_t     threaded, quit;
    uint64_t    stamp;
    clip_entry  clips[SA1A_CLIP_SLOTS];
    uint8_t     tone_req[SA1A_KEYS];
    uint32_t    ntone_req;
    uint32_t    set_pending;    /* stage rules: bit per extra set whose banks are to be read */
    uint32_t    set_pre;        /* ... whose rule tones are to be decoded (set_cursor .. pre_last) */
    uint32_t    set_cursor[SA1_AUDIO_MAX_STAGE_SETS];
    uint8_t     ready[SA1A_KEYS];  /* a->sounds[tone] is set (written under tone, flagged under m): the mixer
                                               reads it without the tone lock */
    int32_t     pre_tones;      /* the tone preload is queued or running */
    int32_t     pre_tones_asked; /* ... was asked for (it starts over after sa1_audio_set_stage) */
    uint32_t    pre_cursor;
    int32_t     busy;           /* the loader works on something */
    /* counters (sa1_audio_get_stats) */
    uint32_t    tones_decoded, clip_reads, clip_hits, read_errors, caller_reads, cues_deferred, cues_dropped;
    float       max_read_ms;
};

/* ---- the cache (under m) ------------------------------------------------------------------------------------ */
static int32_t clip_find(const struct sa1a_loader *L, int32_t source, uint32_t index)
{
    uint32_t i;
    for (i = 0; i < SA1A_CLIP_SLOTS; i++)
        if (L->clips[i].state != CL_FREE && L->clips[i].source == source && L->clips[i].index == index)
            return (int32_t)i;
    return -1;
}

static void clip_evict(sa1_audio *a, clip_entry *e)
{
    sa1_mem_free(a->ctx, e->data);
    memset(e, 0, sizeof *e);
}

/* a free slot: evicts unpinned, unreferenced, read (or failed) entries, least recently used first, while the slots or
 * the byte budget are short; -1 if nothing can go */
static int32_t clip_alloc(sa1_audio *a, struct sa1a_loader *L)
{
    for (;;) {
        uint64_t bytes = 0;
        uint32_t i;
        int32_t free_slot = -1, victim = -1;
        for (i = 0; i < SA1A_CLIP_SLOTS; i++) {
            clip_entry *e = &L->clips[i];
            if (e->state == CL_FREE) {
                if (free_slot < 0)
                    free_slot = (int32_t)i;
                continue;
            }
            if (!e->pinned)
                bytes += e->size;
            if (!e->pinned && e->refs == 0 && (e->state == CL_READY || e->state == CL_FAILED) &&
                (victim < 0 || e->stamp < L->clips[victim].stamp))
                victim = (int32_t)i;
        }
        if (free_slot >= 0 && bytes <= SA1A_CLIP_BUDGET)
            return free_slot;
        if (victim < 0)
            return free_slot;           /* over the budget with nothing to let go: still take a free slot */
        clip_evict(a, &L->clips[victim]);
    }
}

/* reads entry `slot` (state CL_LOADING, set by the caller under m); called without m */
static void clip_load(sa1_audio *a, struct sa1a_loader *L, int32_t slot, int caller)
{
    clip_entry *e = &L->clips[slot];
    uint8_t *data = NULL;
    uint32_t size = 0;
    sa1a_adx h;
    double t0 = sa1_now_ms(), ms;
    sa1_status st = sa1a_read_adx(a, e->source, e->index, &data, &size);
    memset(&h, 0, sizeof h);
    if (st == SA1_OK)
        st = sa1a_adx_parse(data, size, &h);
    ms = sa1_now_ms() - t0;
    mtx_lock(&L->m);
    if (st == SA1_OK) {
        e->data = data;
        e->size = size;
        e->h = h;
        e->state = CL_READY;
    } else {
        sa1_mem_free(a->ctx, data);
        e->status = st;
        e->state = CL_FAILED;
        L->read_errors++;
    }
    L->clip_reads++;
    if (caller)
        L->caller_reads++;
    if ((float)ms > L->max_read_ms)
        L->max_read_ms = (float)ms;
    cnd_broadcast(&L->done);
    mtx_unlock(&L->m);
}

/* the oldest queued entry (under m), -1 if none */
static int32_t next_queued(const struct sa1a_loader *L)
{
    uint32_t i;
    int32_t best = -1;
    for (i = 0; i < SA1A_CLIP_SLOTS; i++)
        if (L->clips[i].state == CL_QUEUED && (best < 0 || L->clips[i].stamp < L->clips[best].stamp))
            best = (int32_t)i;
    return best;
}

/* the next tone to decode (under m): one the mixer asked for, else the preload's next; UINT32_MAX if none */
static uint32_t next_tone(sa1_audio *a, struct sa1a_loader *L)
{
    uint32_t t;
    if (L->ntone_req) {
        for (t = 0; t < SA1A_KEYS; t++) {
            if (L->tone_req[t]) {
                L->tone_req[t] = 0;
                L->ntone_req--;
                return t;
            }
        }
        L->ntone_req = 0;
    }
    if (L->pre_tones) {
        if (L->pre_cursor <= SA1A_LAST_SE)
            return L->pre_cursor++;
        L->pre_tones = 0;
    }
    if (L->set_pre) {
        /* stage rules: the tones of the rules' ranges whose bank the set holds */
        uint32_t x;
        for (x = 0; x < SA1_AUDIO_MAX_STAGE_SETS; x++) {
            sa1a_stage_set *S = &a->xset[x];
            if (!(L->set_pre & (1u << x)))
                continue;
            while (L->set_cursor[x] <= S->pre_last && L->set_cursor[x] <= SA1A_LAST_SE) {
                uint32_t tt = L->set_cursor[x]++;
                int32_t gb = sa1a_tone_game_bank(a, tt);
                if (gb >= 0 && gb < (int32_t)SA1A_GAME_BANKS && (S->have & (1u << gb)) && !L->ready[SA1A_KEY(x + 1u, tt)])
                    return SA1A_KEY(x + 1u, tt);
            }
            L->set_pre &= ~(1u << x);
        }
    }
    return UINT32_MAX;
}

/* stage rules: reads the banks of extra set x (index) and installs them (the loader thread, or the caller without it).
 * Then the set's range is queued for decoding on the loader thread (without the thread the mixer decodes a tone when a cue
 * asks for it, as for the object's own tones). A failure is only recorded here (the loader thread never logs): the mixer's
 * thread reports it at the first cue that meets it (sa1a_set_report_failed). */
static void set_load(sa1_audio *a, struct sa1a_loader *L, uint32_t x, int32_t stage, uint32_t banks)
{
    sa1a_bank tmp[SA1A_GAME_BANKS];
    uint32_t loaded = 0, i;
    memset(tmp, 0, sizeof tmp);
    if (banks)                  /* nothing new (a rule widened a held set's range): no read, only the decode below */
        (void)sa1a_set_read(a, stage, banks, x + 1u, tmp, &loaded);
    mtx_lock(&L->tone);         /* decoders read the banks under the tone lock */
    for (i = 0; i < SA1A_GAME_BANKS; i++) {
        if (loaded & (1u << i)) {
            sa1a_bank_free(a, &a->xset[x].banks[i]);
            a->xset[x].banks[i] = tmp[i];
        }
    }
    mtx_unlock(&L->tone);
    mtx_lock(&L->m);
    a->xset[x].have |= loaded;
    a->xset[x].failed |= banks & ~loaded;
    if (L->threaded) {
        L->set_pre |= 1u << x;
        L->set_cursor[x] = a->xset[x].pre_first;
        cnd_broadcast(&L->done);
    }
    mtx_unlock(&L->m);
}

/* decodes a tone under the tone lock (called without m) */
static void tone_decode(sa1_audio *a, struct sa1a_loader *L, uint32_t tone)
{
    sa1_status st;
    int fresh;
    int ok;
    mtx_lock(&L->tone);
    fresh = a->sounds[tone] == NULL;
    if (fresh)
        (void)sa1a_sound_get(a, tone, &st);
    ok = a->sounds[tone] != NULL;
    mtx_unlock(&L->tone);
    mtx_lock(&L->m);
    if (fresh)
        L->tones_decoded++;
    L->ready[tone] = (uint8_t)ok;
    mtx_unlock(&L->m);
}

/* ---- the thread ---------------------------------------------------------------------------------------------- */
static void loader_run(sa1_audio *a)
{
    struct sa1a_loader *L = a->loader;
    mtx_lock(&L->m);
    for (;;) {
        int32_t slot;
        uint32_t tone;
        if (L->quit)
            break;
        slot = next_queued(L);
        if (slot >= 0) {
            L->clips[slot].state = CL_LOADING;
            L->busy = 1;
            mtx_unlock(&L->m);
            clip_load(a, L, slot, 0);
            mtx_lock(&L->m);
            L->busy = 0;
            continue;
        }
        if (L->set_pending) {
            uint32_t x = 0, banks;
            int32_t stage;
            while (!(L->set_pending & (1u << x)))
                x++;
            L->set_pending &= ~(1u << x);
            stage = a->xset[x].stage;
            banks = a->xset[x].want & ~a->xset[x].have & ~a->xset[x].failed;
            L->busy = 1;
            mtx_unlock(&L->m);
            set_load(a, L, x, stage, banks);
            mtx_lock(&L->m);
            L->busy = 0;
            continue;
        }
        tone = next_tone(a, L);
        if (tone != UINT32_MAX) {
            L->busy = 1;
            mtx_unlock(&L->m);
            tone_decode(a, L, tone);
            mtx_lock(&L->m);
            L->busy = 0;
            if (!L->pre_tones && !L->ntone_req && !L->set_pre)
                cnd_broadcast(&L->done);
            continue;
        }
        cnd_broadcast(&L->done);
        cnd_wait(&L->wake, &L->m);
    }
    mtx_unlock(&L->m);
}

#if defined(_WIN32)
static DWORD WINAPI loader_main(LPVOID arg)
{
    loader_run((sa1_audio *)arg);
    return 0;
}
#else
static void *loader_main(void *arg)
{
    loader_run((sa1_audio *)arg);
    return NULL;
}
#endif

/* ---- API (sa1_audio.h) --------------------------------------------------------------------------------------- */
sa1_status sa1a_loader_init(sa1_audio *a)
{
    struct sa1a_loader *L = (struct sa1a_loader *)sa1_mem_alloc(a->ctx, sizeof *L);
    if (!L)
        return SA1_ERR_OUT_OF_MEMORY;
    mtx_init(&L->m);
    mtx_init(&L->tone);
    mtx_init(&L->disc);
    cnd_init(&L->wake);
    cnd_init(&L->done);
    a->loader = L;
    if (a->flags & SA1_AUDIO_SYNC_LOADS)
        return SA1_OK;
#if defined(_WIN32)
    L->th = CreateThread(NULL, 0, loader_main, a, 0, NULL);
    L->threaded = L->th != NULL;
#else
    L->threaded = pthread_create(&L->th, NULL, loader_main, a) == 0;
#endif
    if (!L->threaded)
        sa1_log(a->ctx, SA1_LOG_WARN, "audio: no loader thread: clips load on the calling thread");
    return SA1_OK;
}

void sa1a_loader_free(sa1_audio *a)
{
    struct sa1a_loader *L = a ? a->loader : NULL;
    uint32_t i;
    if (!L)
        return;
    if (L->threaded) {
        mtx_lock(&L->m);
        L->quit = 1;
        cnd_broadcast(&L->wake);
        mtx_unlock(&L->m);
#if defined(_WIN32)
        WaitForSingleObject(L->th, INFINITE);
        CloseHandle(L->th);
#else
        pthread_join(L->th, NULL);
#endif
        L->threaded = 0;
    }
    for (i = 0; i < SA1A_CLIP_SLOTS; i++)
        clip_evict(a, &L->clips[i]);
    cnd_free(&L->wake);
    cnd_free(&L->done);
    mtx_free(&L->m);
    mtx_free(&L->tone);
    mtx_free(&L->disc);
    sa1_mem_free(a->ctx, L);
    a->loader = NULL;
}

int sa1a_threaded(const sa1_audio *a)
{
    return a && a->loader && a->loader->threaded;
}

void sa1a_disc_lock(sa1_audio *a)
{
    if (a->loader)
        mtx_lock(&a->loader->disc);
}

void sa1a_disc_unlock(sa1_audio *a)
{
    if (a->loader)
        mtx_unlock(&a->loader->disc);
}

void sa1a_tone_lock(sa1_audio *a)
{
    if (a->loader)
        mtx_lock(&a->loader->tone);
}

void sa1a_tone_unlock(sa1_audio *a)
{
    if (a->loader)
        mtx_unlock(&a->loader->tone);
}

const sa1a_sound *sa1a_sound_nowait(sa1_audio *a, uint32_t key, sa1_status *st, int *pending)
{
    const uint32_t tone = key;      /* the arrays are indexed by tone key (set 0: the tone) */
    struct sa1a_loader *L = a->loader;
    const sa1a_sound *s = NULL;
    *pending = 0;
    *st = SA1_OK;
    if (!L || !L->threaded) {
        if (L)
            mtx_lock(&L->tone);
        s = sa1a_sound_get(a, tone, st);
        if (L)
            mtx_unlock(&L->tone);
        return s;
    }
    if (key >= SA1A_KEYS) {
        *st = SA1_ERR_NOT_FOUND;
        return NULL;
    }
    /* decoded: flagged under m after the decode (a->sounds[tone] then stays until set_stage / close, which clear the
       flags first) */
    mtx_lock(&L->m);
    if (L->ready[tone])
        s = a->sounds[tone];
    mtx_unlock(&L->m);
    if (s)
        return s;
    if (mtx_trylock(&L->tone)) {
        s = a->sounds[tone];
        if (!s && SA1A_KEY_TONE(key) > SA1A_LAST_SE)
            s = sa1a_sound_get(a, tone, st);    /* a voice: nothing to decode */
        mtx_unlock(&L->tone);
        if (s) {
            mtx_lock(&L->m);
            L->ready[tone] = 1;
            mtx_unlock(&L->m);
        }
        if (s || *st != SA1_OK)
            return s;
    }
    /* not decoded yet, or the loader is decoding: ask it (first in line) and come back */
    mtx_lock(&L->m);
    if (!L->tone_req[tone]) {
        L->tone_req[tone] = 1;
        L->ntone_req++;
    }
    L->cues_deferred++;
    cnd_broadcast(&L->wake);
    mtx_unlock(&L->m);
    *pending = 1;
    return NULL;
}

int32_t sa1a_clip_get(sa1_audio *a, int32_t source, uint32_t index, sa1_status *st)
{
    struct sa1a_loader *L = a->loader;
    int32_t slot;
    clip_entry *e;
    *st = sa1a_adx_exists(a, source, index);
    if (*st != SA1_OK || !L)
        return -1;
    mtx_lock(&L->m);
    slot = clip_find(L, source, index);
    if (slot >= 0) {
        e = &L->clips[slot];
        e->refs++;
        e->stamp = ++L->stamp;
        if (e->state == CL_READY)
            L->clip_hits++;
        mtx_unlock(&L->m);
        return slot;
    }
    slot = clip_alloc(a, L);
    if (slot < 0) {
        mtx_unlock(&L->m);
        *st = SA1_ERR_LIMIT;
        return -1;
    }
    e = &L->clips[slot];
    memset(e, 0, sizeof *e);
    e->source = source;
    e->index = index;
    e->refs = 1;
    e->stamp = ++L->stamp;
    if (L->threaded) {
        e->state = CL_QUEUED;
        cnd_broadcast(&L->wake);
        mtx_unlock(&L->m);
        return slot;
    }
    e->state = CL_LOADING;
    mtx_unlock(&L->m);
    clip_load(a, L, slot, 1);
    return slot;
}

int32_t sa1a_clip_poll(sa1_audio *a, int32_t slot, const uint8_t **data, uint32_t *size, const sa1a_adx **h,
                       sa1_status *st, int *first)
{
    struct sa1a_loader *L = a->loader;
    clip_entry *e;
    int32_t r;
    *first = 0;
    if (!L || slot < 0 || (uint32_t)slot >= SA1A_CLIP_SLOTS)
        return SA1A_CLIP_FAILED;
    mtx_lock(&L->m);
    e = &L->clips[slot];
    if (e->state == CL_READY) {
        *data = e->data;
        *size = e->size;
        *h = &e->h;
        r = SA1A_CLIP_READY;
    } else if (e->state == CL_FAILED || e->state == CL_FREE) {
        *st = e->state == CL_FREE ? SA1_ERR_NOT_FOUND : e->status;
        *first = !e->reported;
        e->reported = 1;
        r = SA1A_CLIP_FAILED;
    } else {
        r = SA1A_CLIP_WAIT;
    }
    mtx_unlock(&L->m);
    return r;
}

void sa1a_clip_put(sa1_audio *a, int32_t slot)
{
    struct sa1a_loader *L = a->loader;
    if (!L || slot < 0 || (uint32_t)slot >= SA1A_CLIP_SLOTS)
        return;
    mtx_lock(&L->m);
    if (L->clips[slot].refs)
        L->clips[slot].refs--;
    mtx_unlock(&L->m);
}

void sa1a_tones_dropped(sa1_audio *a)
{
    struct sa1a_loader *L = a->loader;
    if (!L)
        return;
    mtx_lock(&L->m);
    {
        /* only the object's own stage (set 0): the extra sets keep their tones */
        uint32_t k;
        for (k = 0; k < SA1A_TONES; k++) {
            if (L->tone_req[k]) {
                L->tone_req[k] = 0;
                if (L->ntone_req)
                    L->ntone_req--;
            }
            L->ready[k] = 0;
        }
    }
    if (L->pre_tones_asked && L->threaded) {
        L->pre_tones = 1;
        L->pre_cursor = 0;
        cnd_broadcast(&L->wake);
    }
    mtx_unlock(&L->m);
}

/* the preloads are done (under m) */
static int preload_idle(const struct sa1a_loader *L)
{
    uint32_t i;
    if (L->pre_tones || L->ntone_req || L->set_pre || L->set_pending)
        return 0;
    for (i = 0; i < SA1A_CLIP_SLOTS; i++)
        if (L->clips[i].pinned && (L->clips[i].state == CL_QUEUED || L->clips[i].state == CL_LOADING))
            return 0;
    return 1;
}

sa1_status sa1_audio_preload(sa1_audio *a, uint32_t flags)
{
    struct sa1a_loader *L;
    uint32_t i, t;
    if (!a || !a->loader || (flags & ~(SA1_AUDIO_PRELOAD_ALL | SA1_AUDIO_PRELOAD_WAIT)))
        return SA1_ERR_INVALID_ARGUMENT;
    L = a->loader;
    if (flags & SA1_AUDIO_PRELOAD_JINGLES) {
        for (i = 0; i < sizeof k_jingles / sizeof k_jingles[0]; i++) {
            uint32_t music;
            sa1_status st;
            int32_t slot;
            for (music = 0; music < a->num_music; music++)
                if (strcmp(a->music[music], k_jingles[i]) == 0)
                    break;
            if (music >= a->num_music)
                continue;
            slot = sa1a_clip_get(a, SA1_AUDIO_SOURCE_MUSIC, music, &st);
            if (slot < 0)
                continue;
            mtx_lock(&L->m);
            L->clips[slot].pinned = 1;
            if (L->clips[slot].refs)
                L->clips[slot].refs--;      /* the pin keeps it, not a reference */
            mtx_unlock(&L->m);
        }
    }
    if (flags & SA1_AUDIO_PRELOAD_TONES) {
        if (L->threaded) {
            mtx_lock(&L->m);
            L->pre_tones_asked = 1;
            if (!L->pre_tones && L->pre_cursor <= SA1A_LAST_SE) {
                L->pre_tones = 1;
                cnd_broadcast(&L->wake);
            }
            mtx_unlock(&L->m);
        } else {
            mtx_lock(&L->tone);
            for (t = 0; t <= SA1A_LAST_SE; t++) {
                sa1_status st;
                if (!a->sounds[t]) {
                    (void)sa1a_sound_get(a, t, &st);
                    L->tones_decoded++;
                }
            }
            mtx_unlock(&L->tone);
            L->pre_tones_asked = 1;
        }
    }
    if ((flags & SA1_AUDIO_PRELOAD_WAIT) && L->threaded) {
        mtx_lock(&L->m);
        while (!preload_idle(L) || L->busy)
            cnd_wait(&L->done, &L->m);
        mtx_unlock(&L->m);
    }
    return SA1_OK;
}

/* ---- stage rules (header version 5) --------------------------------------------------------------------------------- */
int32_t sa1a_set_use(sa1_audio *a, int32_t stage, uint32_t banks, uint32_t first, uint32_t last)
{
    struct sa1a_loader *L = a->loader;
    int32_t x, free_x = -1, found = -1;
    uint32_t need;
    if (!L)
        return -1;
    mtx_lock(&L->m);
    for (x = 0; x < (int32_t)SA1_AUDIO_MAX_STAGE_SETS; x++) {
        if (a->xset[x].used && a->xset[x].stage == stage) {
            found = x;
            break;
        }
        if (!a->xset[x].used && free_x < 0)
            free_x = x;
    }
    if (found < 0) {
        if (free_x < 0) {
            mtx_unlock(&L->m);
            return -1;
        }
        found = free_x;
        a->xset[found].used = 1;
        a->xset[found].stage = stage;
        a->xset[found].pre_first = first;
        a->xset[found].pre_last = last;
    } else {
        if (first < a->xset[found].pre_first)
            a->xset[found].pre_first = first;
        if (last > a->xset[found].pre_last)
            a->xset[found].pre_last = last;
    }
    a->xset[found].want |= banks;
    need = a->xset[found].want & ~a->xset[found].have & ~a->xset[found].failed;
    if (need) {
        L->set_pending |= 1u << found;
    } else if (L->threaded) {
        L->set_pre |= 1u << found;      /* the banks are held: decode the (maybe widened) range */
        L->set_cursor[found] = a->xset[found].pre_first;
    }
    if (L->threaded) {
        cnd_broadcast(&L->wake);
        mtx_unlock(&L->m);
    } else {
        L->set_pending &= ~(1u << found);
        mtx_unlock(&L->m);
        if (need)
            set_load(a, L, (uint32_t)found, stage, need);   /* SA1_AUDIO_SYNC_LOADS: now, on the caller's thread */
    }
    return found + 1;
}

int32_t sa1a_set_lookup(sa1_audio *a, int32_t stage, uint32_t game_bank, uint32_t *set)
{
    struct sa1a_loader *L = a->loader;
    int32_t x, r = SA1_AUDIO_STAGE_ABSENT;
    if (!L || game_bank >= SA1A_GAME_BANKS)
        return SA1_AUDIO_STAGE_FAILED;
    mtx_lock(&L->m);
    for (x = 0; x < (int32_t)SA1_AUDIO_MAX_STAGE_SETS; x++) {
        const sa1a_stage_set *S = &a->xset[x];
        if (!S->used || S->stage != stage)
            continue;
        *set = (uint32_t)x + 1u;
        if (S->have & (1u << game_bank))
            r = SA1_AUDIO_STAGE_READY;
        else if ((S->failed & (1u << game_bank)) || !(S->want & (1u << game_bank)))
            r = SA1_AUDIO_STAGE_FAILED;
        else
            r = SA1_AUDIO_STAGE_LOADING;
        break;
    }
    mtx_unlock(&L->m);
    return r;
}

int sa1a_set_report_failed(sa1_audio *a, uint32_t set)
{
    struct sa1a_loader *L = a->loader;
    int first = 0;
    if (!L || set == 0 || set > SA1_AUDIO_MAX_STAGE_SETS)
        return 0;
    mtx_lock(&L->m);
    if (!a->xset[set - 1u].reported) {
        a->xset[set - 1u].reported = 1;
        first = 1;
    }
    mtx_unlock(&L->m);
    return first;
}

int32_t sa1_audio_stage_state(sa1_audio *a, int32_t stage)
{
    struct sa1a_loader *L;
    int32_t x, r = SA1_AUDIO_STAGE_ABSENT;
    if (!a || !a->loader)
        return SA1_AUDIO_STAGE_ABSENT;
    if (stage == a->stage)
        return SA1_AUDIO_STAGE_READY;
    L = a->loader;
    mtx_lock(&L->m);
    for (x = 0; x < (int32_t)SA1_AUDIO_MAX_STAGE_SETS; x++) {
        const sa1a_stage_set *S = &a->xset[x];
        if (!S->used || S->stage != stage)
            continue;
        if ((S->want & ~S->have & ~S->failed) || (L->set_pre & (1u << x)))
            r = SA1_AUDIO_STAGE_LOADING;
        else if (S->failed)             /* the MLT, or a bank a rule needs, could not be read (libsa1_audio.h) */
            r = SA1_AUDIO_STAGE_FAILED;
        else
            r = SA1_AUDIO_STAGE_READY;
        break;
    }
    mtx_unlock(&L->m);
    return r;
}

sa1_status sa1_audio_get_stats(const sa1_audio *ac, sa1_audio_stats *out)
{
    sa1_audio *a = (sa1_audio *)(uintptr_t)ac;
    struct sa1a_loader *L;
    sa1_audio_stats s;
    uint32_t i;
    if (!a || !out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    memset(&s, 0, sizeof s);
    s.struct_size = sizeof s;
    L = a->loader;
    if (L) {
        mtx_lock(&L->m);
        s.loader_thread = L->threaded;
        s.preload_done = preload_idle(L);
        s.tones_decoded = L->tones_decoded;
        for (i = 0; i < SA1A_CLIP_SLOTS; i++) {
            const clip_entry *e = &L->clips[i];
            if (e->state == CL_FREE)
                continue;
            s.clips_cached++;
            s.clips_pinned += e->pinned != 0;
            s.clips_waiting += e->state == CL_QUEUED || e->state == CL_LOADING;
            s.cache_bytes += e->size;
        }
        s.clip_reads = L->clip_reads;
        s.clip_hits = L->clip_hits;
        s.read_errors = L->read_errors;
        s.caller_reads = L->caller_reads;
        s.cues_deferred = L->cues_deferred;
        s.cues_dropped = L->cues_dropped;
        s.max_read_ms = L->max_read_ms;
        mtx_unlock(&L->m);
    }
    sa1_copy_out(out, &s, sizeof s);
    return SA1_OK;
}

void sa1a_count_dropped(sa1_audio *a, uint32_t n)
{
    if (!a->loader || !n)
        return;
    mtx_lock(&a->loader->m);
    a->loader->cues_dropped += n;
    mtx_unlock(&a->loader->m);
}

uint8_t *sa1a_clip_copy(sa1_audio *a, int32_t source, uint32_t index, uint32_t *size)
{
    struct sa1a_loader *L = a->loader;
    uint8_t *copy = NULL;
    int32_t slot;
    if (!L)
        return NULL;
    mtx_lock(&L->m);
    slot = clip_find(L, source, index);
    if (slot >= 0 && L->clips[slot].state == CL_READY) {
        copy = (uint8_t *)sa1_mem_alloc(a->ctx, L->clips[slot].size ? L->clips[slot].size : 1u);
        if (copy) {
            memcpy(copy, L->clips[slot].data, L->clips[slot].size);
            *size = L->clips[slot].size;
        }
    }
    mtx_unlock(&L->m);
    return copy;
}

void sa1a_count_caller_read(sa1_audio *a)
{
    if (!a->loader)
        return;
    mtx_lock(&a->loader->m);
    a->loader->caller_reads++;
    a->loader->clip_reads++;
    mtx_unlock(&a->loader->m);
}