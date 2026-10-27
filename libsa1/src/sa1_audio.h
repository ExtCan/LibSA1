/*
 * libsa1 audio: internal declarations shared by sa1_audio.c (disc, banks, tones, cues, API),
 * sa1_audio_codec.c (AICA ADPCM, CRI ADX), sa1_audio_mix.c (voices, sequences, mixer) and
 * sa1_audio_load.c (the loader thread, the clip cache and the locks).
 * Not part of the public API (include/libsa1_audio.h).
 */
#ifndef SA1_AUDIO_INTERNAL_H
#define SA1_AUDIO_INTERNAL_H

#include "sa1_internal.h"
#include "libsa1_audio.h"

#define SA1A_MAX_TONE        1400u      /* tones above SA1A_LAST_SE are voices; nothing in the game goes higher */
#define SA1A_LAST_SE         1274u      /* 8C0948A0: tone > 0x4FA -> voice */
#define SA1A_VOICE_OFFSET    455u       /* 8C0948A0: voice = tone + 0x1C7 */
#define SA1A_NOTE            60         /* 8C093360: note on 60 (D_8C1C19A4), velocity 127 */
#define SA1A_AICA_RATE       44100.0
#define SA1A_GAME_BANKS      9u
#define SA1A_MAX_LAYERS      4u
#define SA1A_MAX_RANGES      128u
#define SA1A_NUM_GROUPS      5u         /* character sound groups: SONICTAILS, KNUCKLES, AMY, E102, BIG */
#define SA1A_SEQ_RATE        44100u
#define SA1A_SEQ_MAX_SECONDS 20u
#define SA1A_MAX_MUSIC       128u
#define SA1A_MAX_VOICES      4096u
#define SA1A_FRAME_SECONDS   (1.0f / 60.0f) /* the sound task's timers count 60 Hz frames */
/* stage rules (header version 5): tone keys = set * SA1A_TONES + tone; set 0 = the object's own stage, 1.. = the extra
   stages (a->xset[set - 1]) */
#define SA1A_TONES           (SA1A_MAX_TONE + 1u)
#define SA1A_SETS            (1u + SA1_AUDIO_MAX_STAGE_SETS)
#define SA1A_KEYS            (SA1A_SETS * SA1A_TONES)
#define SA1A_KEY(set, tone)  ((uint32_t)(set) * SA1A_TONES + (uint32_t)(tone))
#define SA1A_KEY_TONE(key)   ((uint32_t)(key) % SA1A_TONES)
#define SA1A_KEY_SET(key)    ((uint32_t)(key) / SA1A_TONES)

/* the game's attenuation by distance (8C0947BA): full within 20 units, -127 "volofs" at 600 */
#define SA1A_NEAR            20.0f
#define SA1A_FAR             600.0f

/* ------------------------------------------------------------------------- */
/* Codecs (sa1_audio_codec.c)                                                */
/* ------------------------------------------------------------------------- */

/* Yamaha AICA 4-bit ADPCM, low nibble first, decoder state reset at the start. */
void sa1a_adpcm_decode(const uint8_t *src, uint32_t num_samples, int16_t *out);

typedef struct sa1a_adx {
    uint32_t channels;          /* 1..2 */
    uint32_t rate;
    uint32_t frames;            /* samples per channel (clamped to the data present) */
    uint32_t data_off;          /* first block */
    uint32_t block;             /* bytes per block per channel (18) */
    int32_t  coef1, coef2;
    int32_t  loop;
    uint32_t loop_start, loop_end;
} sa1a_adx;

sa1_status sa1a_adx_parse(const uint8_t *d, uint32_t len, sa1a_adx *out);

typedef struct sa1a_adx_dec {
    sa1a_adx       h;
    const uint8_t *d;
    uint32_t       len;
    uint32_t       pos;             /* next frame */
    uint32_t       block;           /* block held in buf, UINT32_MAX = none */
    int32_t        hist[2][2];      /* per channel s1, s2 after `block` */
    int32_t        loop_saved;
    int32_t        loop_hist[2][2]; /* state before the loop start block */
    int16_t        buf[2 * 32];
} sa1a_adx_dec;

void     sa1a_adx_dec_init(sa1a_adx_dec *dec, const uint8_t *d, uint32_t len, const sa1a_adx *h);
uint32_t sa1a_adx_dec_read(sa1a_adx_dec *dec, int16_t *out, uint32_t frames, int loop);
void     sa1a_adx_dec_seek(sa1a_adx_dec *dec, uint32_t frame);

/* ------------------------------------------------------------------------- */
/* Banks, tones (sa1_audio.c)                                                */
/* ------------------------------------------------------------------------- */

typedef struct sa1a_pcm {
    uint32_t addr, fmt, count;      /* source: MPB offset, SA1_AUDIO_FORMAT_*, samples */
    int16_t *pcm;
    int32_t  peak;                  /* largest |sample| */
} sa1a_pcm;

#define SA1A_SILENT_PEAK 64         /* a split whose samples never exceed this is a placeholder (programs
                                       padded with 4-9 zero samples): it is left out */

typedef struct sa1a_bank {
    int32_t  loaded;
    uint32_t game_bank;             /* 0..8 */
    char     file[40];
    uint8_t *mpb;
    uint32_t mpb_size;
    uint8_t *msb;
    uint32_t msb_size;
    uint32_t num_programs;
    uint32_t num_sequences;
    uint8_t  vel_curve[128];
    sa1_vec  pcm;                   /* sa1a_pcm, decoded on demand */
    uint32_t set;                   /* the stage set whose banks this bank's sequences use (0: the object's own) */
} sa1a_bank;

/* an extra stage (stage rules): the banks a rule's range needs, read and decoded on the loader thread */
typedef struct sa1a_stage_set {
    int32_t   used;
    int32_t   stage;
    uint32_t  want;                 /* bit per game bank asked for (under the loader's m) */
    uint32_t  have;                 /* bit per game bank installed (written under the tone lock, then m) */
    uint32_t  failed;               /* bit per game bank whose read failed */
    uint32_t  reported;             /* 1: a cue met the failure and the mixer's thread logged it (once per set) */
    uint32_t  pre_first, pre_last;  /* the tones to decode once read (the union of the rules' ranges) */
    sa1a_bank banks[SA1A_GAME_BANKS];
} sa1a_stage_set;

/* One split of a layer, as played: the public view plus what the synthesizer needs. */
typedef struct sa1a_layer {
    sa1_audio_layer pub;
    float   bend_up, bend_down;     /* semitones for a full pitch bend */
    uint8_t ar, d1r, d2r, dl, rr, krs;
    uint8_t direct_level;           /* DISDL */
    uint8_t total_level;            /* TL */
    uint8_t dipan;
} sa1a_layer;

typedef struct sa1a_sound {
    int32_t     kind;               /* SA1_AUDIO_SOUND_* */
    uint32_t    bank, request, voice;
    const char *file;
    uint32_t    num_layers;
    sa1a_layer  layers[SA1A_MAX_LAYERS];
    int16_t    *rendered;           /* SEQUENCE: owned PCM */
    float       duration, radius;
    int32_t     loops;
    int32_t     stage_bank;         /* depends on a stage bank (freed by set_stage) */
} sa1a_sound;

typedef struct sa1a_range {
    int32_t  threshold;             /* tone > threshold -> bank, request = tone - threshold - 1 */
    int32_t  bank;
} sa1a_range;

/* a music number's file and what sa1_audio_music_info reports (header version 3) */
typedef struct sa1a_music {
    uint32_t lba, size;             /* SONICADV extent of the file (when on the disc: the a->music_on_disc bit) */
    int32_t  title;                 /* offset in a->titles, -1 = not in the sound test */
    int32_t  page, entry;           /* sound-test page and position, -1 = not listed */
    int32_t  have_header;           /* h and header_frames are read */
    uint32_t header_frames;         /* the frame count the header states (h.frames: the frames present) */
    sa1a_adx h;
} sa1a_music;

struct sa1_audio_mixer;
struct sa1_audio_stream;
struct sa1a_loader;

struct sa1_audio {
    sa1_context   *ctx;
    sa1_disc       disc;
    int32_t        disc_open;
    uint32_t       flags;
    int32_t        stage;
    uint32_t       characters;
    /* SONICADV directory extent (cached) */
    uint32_t       sonicadv_lba, sonicadv_size;
    uint8_t       *sonicadv_dir;
    /* tables from 1ST_READ.BIN */
    sa1a_range     ranges[SA1A_MAX_RANGES];
    uint32_t       num_ranges;
    int16_t        tone3d[64];
    int16_t        radius3d[64];
    uint32_t       num_3d;
    char           music[SA1A_MAX_MUSIC][16];
    uint32_t       num_music;
    /* the music files (sa1_audio_open: from the SONICADV directory; immutable afterwards, read by the loader thread
       too) and the sound-test titles (sa1_audio_music_info, the object's thread only) */
    uint32_t       music_on_disc[SA1A_MAX_MUSIC / 32u];   /* bit per music number */
    sa1a_music     music_files[SA1A_MAX_MUSIC];
    char          *titles;                                /* NUL-separated title strings */
    int32_t        titles_read;                           /* ADVERTISE.PRS was read (titles NULL: none accepted) */
    /* banks */
    sa1a_bank      stage_banks[SA1A_GAME_BANKS];  /* 0, 1, 2, 4, 5 used */
    sa1a_bank      player[SA1A_NUM_GROUPS];       /* bank 3 */
    sa1a_bank      voiceb[SA1A_NUM_GROUPS];       /* bank 6 */
    sa1a_sound    *sounds[SA1A_KEYS];             /* by tone key (set 0: index = tone) */
    sa1a_stage_set xset[SA1_AUDIO_MAX_STAGE_SETS]; /* the extra stages of the stage rules (set = index + 1) */
    /* voice archive */
    uint32_t       afs_lba;
    uint32_t       afs_size;
    uint32_t       num_voices;
    uint32_t      *afs_toc;                       /* offset, size per voice */
    /* children */
    struct sa1_audio_mixer  *mixers;
    struct sa1_audio_stream *streams;
    /* the loader thread, the clip cache and the locks (sa1_audio_load.c) */
    struct sa1a_loader      *loader;
};

/* Resolves (and caches) a tone key (SA1A_KEY; a plain tone = set 0); NULL with *st on error. */
const sa1a_sound *sa1a_sound_get(sa1_audio *a, uint32_t key, sa1_status *st);
/* the loaded bank of a set (0: sa1a_stage_bank); NULL if not loaded */
sa1a_bank *sa1a_set_bank(sa1_audio *a, uint32_t set, uint32_t game_bank);
/* the game bank of a tone by the tone table (0..8); immutable after open */
int32_t sa1a_tone_game_bank(const sa1_audio *a, uint32_t tone);
/* reads `stage`'s MLT and loads the game banks in `banks` (bit per bank) into out[] (no lock held, never logs: the loader
   thread calls it); returns the read's status */
sa1_status sa1a_set_read(sa1_audio *a, int32_t stage, uint32_t banks, uint32_t set, sa1a_bank *out, uint32_t *loaded);
void sa1a_bank_free(sa1_audio *a, sa1a_bank *b);
/* stage rules (sa1_audio_load.c, under the loader's lock): the set of `stage` asked to hold `banks` and to decode
   first..last (made if needed: -1 when SA1_AUDIO_MAX_STAGE_SETS others are held); and the state of a game bank of a set
   for a cue (SA1_AUDIO_STAGE_READY / LOADING / FAILED) with the set index */
int32_t    sa1a_set_use(sa1_audio *a, int32_t stage, uint32_t banks, uint32_t first, uint32_t last);
int32_t    sa1a_set_lookup(sa1_audio *a, int32_t stage, uint32_t game_bank, uint32_t *set);
/* a cue met FAILED set `set` (1..): 1 the first time (the caller, on the mixer's thread, logs it), else 0 */
int        sa1a_set_report_failed(sa1_audio *a, uint32_t set);
/* The loaded bank object for a game bank number (stage banks; 3 and 6 need a tone: NULL). */
sa1a_bank *sa1a_stage_bank(sa1_audio *a, uint32_t game_bank);
/* Fills `out` (up to SA1A_MAX_LAYERS) with the layers of program `prog` of `bank` that sound at `note`.
 * Returns the count (0 on error or empty). */
uint32_t sa1a_program_layers(sa1_audio *a, sa1a_bank *bank, uint32_t prog, int32_t note, sa1a_layer *out);
/* The game's 3D radius of a tone (0 = none). */
float sa1a_tone_radius(const sa1_audio *a, uint32_t tone);
/* Reads a whole ADX into a new buffer: voice from the AFS or SONICADV/<music name> (takes the disc lock). */
sa1_status sa1a_read_adx(sa1_audio *a, int32_t source, uint32_t index, uint8_t **data, uint32_t *size);
/* SA1_OK if sa1a_read_adx would find the clip (index in range, the archive / music present, the music file on the
 * disc: a bit computed at open); no disc access, O(1) */
sa1_status sa1a_adx_exists(const sa1_audio *a, int32_t source, uint32_t index);
/* Tells the mixers that stage-bank sounds go away. */
void sa1a_mixers_drop_stage(sa1_audio *a);
/* SA1_AUDIO_JINGLE_* of a music number: what the game plays it for as a jingle (8C107C80's call sites); OTHER for a
 * number the game never plays as a jingle (header version 4) */
int32_t sa1a_jingle_kind(uint32_t music);

/* ------------------------------------------------------------------------- */
/* Synthesis (sa1_audio_mix.c)                                               */
/* ------------------------------------------------------------------------- */

typedef struct sa1a_voice {
    const int16_t *pcm;
    uint32_t       n;
    uint32_t       loop_start;
    int32_t        loop;
    double         pos;             /* source samples */
    double         step;            /* source samples per output sample (without bend) */
    double         bend;            /* pitch factor applied to step */
    float          gain_l, gain_r;
    int32_t        stage;           /* SA1A_EG_* */
    uint32_t       delay;           /* output samples before the start */
    float          level;           /* attack: linear 0..1 */
    float          attack_inc;      /* per output sample */
    float          att;             /* attenuation in dB (decay / release) */
    float          d1, d2, rel;     /* dB per output sample */
    float          dl;              /* dB where decay2 starts */
} sa1a_voice;

#define SA1A_EG_DELAY   0
#define SA1A_EG_ATTACK  1
#define SA1A_EG_DECAY1  2
#define SA1A_EG_DECAY2  3
#define SA1A_EG_RELEASE 4
#define SA1A_EG_DONE    5

/* Prepares a voice for a layer at `semitones` from the layer's own pitch, for output rate `rate`. */
void sa1a_voice_start(sa1a_voice *v, const sa1a_layer *l, float semitones, float rate, float gain);
void sa1a_voice_release(sa1a_voice *v);
/* Adds n samples into l and r (r may be NULL: mono into l, gains averaged). */
void sa1a_voice_render(sa1a_voice *v, float *l, float *r, uint32_t n, float gl, float gr);
/* Renders sequence `seq` of bank `b` (game bank `game_bank`) into a new mono PCM16 buffer at
 * SA1A_SEQ_RATE.  Returns SA1_OK with *pcm / *count (pcm NULL if silent). */
sa1_status sa1a_render_sequence(sa1_audio *a, sa1a_bank *b, uint32_t seq, int16_t **pcm, uint32_t *count);
/* Frees every mixer of the audio object (sa1_audio_close). */
void sa1a_mixers_free_all(sa1_audio *a);
/* AICA envelope rate tables. */
float sa1a_attack_seconds(int32_t rate_index);
float sa1a_decay_db_per_second(int32_t rate_index);

/* ------------------------------------------------------------------------- */
/* Loader thread, clip cache, locks (sa1_audio_load.c)                       */
/* ------------------------------------------------------------------------- */
/*
 * Nothing the mixer does on the caller's thread waits for the disc or for decoding (G-SA1 0.3 froze for 27-430 ms
 * whenever a jingle such as TIMER.ADX, a voice or music started cold: docs/research/gsa1_ramp_freeze.md, fix 7):
 *   - clips (voices, music, jingles: whole ADX files) are read by the loader thread into a cache shared by the
 *     mixers: a few jingles pinned (preloaded at open), the rest kept by LRU within a byte budget; a mixer clip holds
 *     a reference and starts when its file is there;
 *   - tones are decoded by the loader (every tone of the loaded banks at open: the preload); a cue whose tone is not
 *     decoded yet waits in the mixer for a moment instead of decoding it there.
 * Locks: `m` (cache, queues, counters; held briefly), `tone` (a->sounds and the banks' PCM: the loader holds it for one
 * tone at a time, the mixer only tries it), `disc` (a->disc: held for one file read). The loader thread never logs and
 * never touches the context except through its allocator and file callbacks (sa1_audio_desc: SA1_AUDIO_SYNC_LOADS
 * when those are not thread-safe).
 */
#define SA1A_CLIP_WAIT    0             /* sa1a_clip_poll: queued or being read */
#define SA1A_CLIP_READY   1
#define SA1A_CLIP_FAILED  2

/* creates the loader (locks and cache; the thread unless SA1_AUDIO_SYNC_LOADS) */
sa1_status sa1a_loader_init(sa1_audio *a);
/* stops the thread (waits for the file or tone in progress) and frees the cache */
void       sa1a_loader_free(sa1_audio *a);
int        sa1a_threaded(const sa1_audio *a);
void       sa1a_disc_lock(sa1_audio *a);
void       sa1a_disc_unlock(sa1_audio *a);
void       sa1a_tone_lock(sa1_audio *a);
void       sa1a_tone_unlock(sa1_audio *a);
/* the decoded tone for the mixer without waiting: NULL and *pending = 1 when the loader still has to decode it (it
 * was asked to, first); without the thread: sa1a_sound_get */
const sa1a_sound *sa1a_sound_nowait(sa1_audio *a, uint32_t key, sa1_status *st, int *pending);
/* a clip of the cache with a reference (-1 and *st on error); queued for the loader when not cached (without the
 * thread: read now) */
int32_t    sa1a_clip_get(sa1_audio *a, int32_t source, uint32_t index, sa1_status *st);
/* SA1A_CLIP_*: READY gives the file and its parsed header (valid while the reference is held); FAILED gives *st and
 * *first = 1 the first time anyone sees the failure (for one log line) */
int32_t    sa1a_clip_poll(sa1_audio *a, int32_t slot, const uint8_t **data, uint32_t *size, const sa1a_adx **h,
                          sa1_status *st, int *first);
void       sa1a_clip_put(sa1_audio *a, int32_t slot);
/* the tones were freed (sa1_audio_set_stage, under the tone lock): a tone preload that ran starts over */
void       sa1a_tones_dropped(sa1_audio *a);
/* sa1_audio_stream_open: a copy of the file when the cache holds it (NULL otherwise) */
uint8_t   *sa1a_clip_copy(sa1_audio *a, int32_t source, uint32_t index, uint32_t *size);
/* counters: cues the mixers gave up waiting for their tone; a file read on the calling thread */
void       sa1a_count_dropped(sa1_audio *a, uint32_t n);
void       sa1a_count_caller_read(sa1_audio *a);

#endif /* SA1_AUDIO_INTERNAL_H */
