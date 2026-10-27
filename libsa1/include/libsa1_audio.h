/*
 * libsa1 audio - the game's sound effects, character voices and music, ripped at run time from the
 * user's own GD-ROM dump like everything else in libsa1 (nothing is written to disk).
 *
 * What the game does (sadc docs/research/sa1_assets.md section 8, and the sound code 8C092FA0-8C0957CE
 * of 1ST_READ.BIN):
 *   - A sound effect is a "tone" (SE number 0..1274) passed to the play functions (8C0940DA and the
 *     variants libsa1 reports as SA1_EVENT_SOUND).  The sound task maps it through the threshold table
 *     at 0x8C1C19B4 to a bank (0..8) and a request number and plays MIDI program `request` of that bank
 *     at note 60, velocity 127, on the AICA; 32 tones (the "3D slot" tones) play sequence `request` of
 *     the bank's sequence bank instead.  Tones above 1274 are voice clips (voice = tone + 455).
 *   - Banks come from .MLT files: banks 0, 1, 2, 4, 5 from the stage's MLT (EMERALDCOAST.MLT, ...),
 *     bank 3 from P_<character>.MLT (player sounds), bank 6 from V_<character>[_E].MLT (short voice
 *     sounds).  Every character has its own tone range in banks 3 and 6, so libsa1 loads all of them.
 *   - A bank (SMPB) holds programs of up to 4 layers; a layer has note-range splits whose AICA registers
 *     give the sample (4-bit Yamaha ADPCM, PCM8 or PCM16), its loop, envelope, level and pan.
 *   - Voices (8C107BA0) are CRI ADX clips: entry `voice` of SONICADV/EVENT_ADX_US.AFS (English) or
 *     EVENT_ADX.AFS (Japanese).  Music and jingles are SONICADV/<name>.ADX by music number (the name
 *     table at 0x8C5FB8A8: 55 = INVNCIBL, 85 = SPEEDUP, 96 = TIMER, 71 = ONE_UP, ...).
 *
 * Three levels of use:
 *   1. data:  sa1_audio_sound_info / sa1_audio_sound_layer give the decoded PCM16 of any tone with its
 *             rate, loop points, level, pan and envelope; sa1_audio_stream_* decode voices and music.
 *   2. cues:  sa1_audio_cue_from_event turns an sa1_event (sa1_poll_events) into what to do: play,
 *             keep alive, stop, voice, music, with position, distances, lifetime and delay.
 *   3. mixer: sa1_audio_mixer_* does all of it and renders interleaved stereo PCM16 that a host only has
 *             to hand to an audio device (listener, positional attenuation like the game, per-character
 *             2D/3D, keep-alive and stop rules, layered programs, envelopes, voices, jingles, volumes).
 *
 * Threading: an sa1_audio object and its mixers and streams are not thread-safe; use them from one
 * thread at a time (render into a ring buffer that the audio device thread drains).  They use the
 * context's allocator, log and file callbacks: close them before sa1_shutdown.
 *
 * Loading (header version 2): the object has a loader thread of its own.  It reads voices, music and
 * jingles (whole ADX files) into a cache shared by the mixers and decodes the tones, so that no mixer
 * call waits for the disc or for decoding: a clip starts when its file is there (a cold voice a few
 * milliseconds late), a cue whose tone is not decoded yet waits in the mixer for a moment.  At open it
 * preloads the jingles the characters start (TIMER, INVNCIBL, SPEEDUP, ONE_UP, RNDCLEAR, FISHGET,
 * FISHMISS: about 3 MB, kept) and decodes every tone of the loaded banks (about 50 ms of work); a mixer
 * that sees a new character asks for the same (nothing to do once it is done).  Other clips stay in an
 * LRU cache (64 MB).  The loader thread calls only the context's allocator and file callbacks (never
 * its log): they must be thread-safe (the defaults are), or pass SA1_AUDIO_SYNC_LOADS (everything on
 * the calling thread, as in header version 1).
 *
 * Music (header version 3): sa1_audio_music_info lists the 110 music numbers with the titles and pages of the
 * game's own sound test (ADVERTISE.PRS, ripped at run time like everything else) and the ADX header data, without
 * reading whole files; SA1_AUDIO_MUSIC_ONLY opens an object for music alone (a jukebox's own object: no banks, no
 * voice archive); a music file that is not on the disc (69 NIGHTS_K.ADX) is SA1_ERR_NOT_FOUND at once, for streams
 * and for MUSIC / JINGLE cues alike; sa1_audio_mixer_duck lowers the background music for a host (a jukebox playing);
 * SA1_AUDIO_LAZY_STREAMS makes a music stream's open cheap (the file is read as decoding reaches it).
 *
 * Music state (header version 4): sa1_audio_mixer_music_state tells a host what the game's music does in a mixer - the
 * background music the game asked for, the jingle that replaces it now (its music number, kind, length, time played and
 * time left), the game's music stop, and counters of the jingles started and ended with how the last one ended - so
 * that a host playing music of its own (a jukebox) can make room for the game's jingles (the drowning countdown,
 * invincibility, speed shoes, fishing).  Two mixer rules follow the game since version 4: a jingle whose file has a
 * loop (45 FISHING.ADX) loops until the game ends it, and a MUSIC cue ends a playing jingle (8C10792A's
 * ADXT_StartFname stops it).
 *
 * Stage rules (header version 5): sa1_audio_mixer_stage_rule lets the tones of a range of ONE character resolve from
 * another stage's sound banks (read and decoded on the loader thread), e.g. Super Sonic's whoosh from the Perfect Chaos
 * stage (CHAOS7B.MLT) while every other sound keeps the object's own stage; sa1_audio_stage_state tells whether those
 * banks are read. SA1_AUDIO_MIX_RESTART_REPEATS (a host option, also header version 5): a sound effect that a character
 * starts again while it still plays restarts instead of piling up (the game lets it pile up: see the flag).
 */
#ifndef LIBSA1_AUDIO_H
#define LIBSA1_AUDIO_H

#include "libsa1.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SA1_AUDIO_HEADER_VERSION 5

typedef struct sa1_audio sa1_audio;

/* ------------------------------------------------------------------------- */
/* Open / close                                                              */
/* ------------------------------------------------------------------------- */

/* sa1_audio_desc.flags */
#define SA1_AUDIO_JAPANESE         0x01u  /* V_<c>.MLT and EVENT_ADX.AFS (default: V_<c>_E.MLT, EVENT_ADX_US.AFS) */
#define SA1_AUDIO_NO_VOICES        0x02u  /* do not index the voice archive (voices report SA1_ERR_NOT_LOADED) */
#define SA1_AUDIO_NO_MUSIC         0x04u  /* music streams report SA1_ERR_NOT_LOADED */
#define SA1_AUDIO_PRELOAD          0x08u  /* decode every program and sequence now, on the calling thread (else the
                                             loader thread does it in the background: sa1_audio_preload) */
#define SA1_AUDIO_SYNC_LOADS       0x10u  /* no loader thread: clips are read and tones decoded on the calling
                                             thread when they are first needed (header version 1's behaviour) */
#define SA1_AUDIO_NO_PRELOAD       0x20u  /* do not preload at open (jingles, tones): sa1_audio_preload later */
#define SA1_AUDIO_MUSIC_ONLY       0x40u  /* music numbers, sa1_audio_music_info and streams only: no sound banks, no
                                             voice archive, no preload (header version 3; a jukebox's own object) */
#define SA1_AUDIO_LAZY_STREAMS     0x80u  /* music streams read their file as decoding reaches it (sa1_audio_stream_open
                                             reads the header, 2 sectors; stream_read / stream_seek read on, 256 KB at a
                                             time, on their thread) instead of all of it at open (header version 3) */
#define SA1_AUDIO_ACCEPT_MODIFIED  0x100u /* accept a 1ST_READ.BIN (and an ADVERTISE.PRS: the sound-test titles) whose
                                             SHA-1 is not the supported one */

#define SA1_AUDIO_STAGE_DEFAULT    (-1)   /* = 1, Emerald Coast */

typedef struct sa1_audio_desc {
    uint32_t    struct_size;    /* sizeof(sa1_audio_desc) */
    const char *gdi_path;       /* the user's .gdi (same path rules as sa1_load_disc); kept open while the
                                   object lives (voices and music are read on demand) */
    uint32_t    flags;          /* SA1_AUDIO_* */
    int32_t     stage;          /* the game's stage number (0..42: 1 Emerald Coast, 2 Windy Valley, ...,
                                   26 Station Square, 33 Mystic Ruins) whose MLT gives banks 0, 1, 2, 4, 5 */
    uint32_t    characters;     /* (1u << SA1_CHAR_*) whose player (3) and voice (6) banks to load; 0 = all */
} sa1_audio_desc;

/* Opens the disc, checks 1ST_READ.BIN, reads the game's sound tables and the sound banks.  Returns
 * SA1_OK, SA1_PARTIAL (a bank file or the voice archive is missing; see the log) or an error. */
SA1_API sa1_status sa1_audio_open(sa1_context *ctx, const sa1_audio_desc *desc, sa1_audio **out);
/* Frees everything, including the mixers and streams of this object. */
SA1_API void       sa1_audio_close(sa1_audio *audio);
/* Replaces the stage banks (0, 1, 2, 4, 5).  Invalidates every layer pointer of those banks and stops
 * the mixers' sounds.  Reads the stage's MLT on the calling thread; the new tones are decoded in the
 * background again when the tones were preloaded.  With SA1_AUDIO_MUSIC_ONLY it only records the stage. */
SA1_API sa1_status sa1_audio_set_stage(sa1_audio *audio, int32_t stage);

/* sa1_audio_preload flags */
#define SA1_AUDIO_PRELOAD_TONES    0x1u   /* decode every tone of the loaded banks (programs, sequences) */
#define SA1_AUDIO_PRELOAD_JINGLES  0x2u   /* read the jingles the characters start and keep them */
#define SA1_AUDIO_PRELOAD_ALL      0x3u
#define SA1_AUDIO_PRELOAD_WAIT     0x100u /* return when it is done (else: queued for the loader thread) */

/* Preloads (sa1_audio_open does PRELOAD_ALL unless SA1_AUDIO_NO_PRELOAD or SA1_AUDIO_SYNC_LOADS; without the
 * loader thread the work is done here).  Cheap when it is done already. */
SA1_API sa1_status sa1_audio_preload(sa1_audio *audio, uint32_t flags);

typedef struct sa1_audio_stats {
    uint32_t struct_size;
    int32_t  loader_thread;     /* 1: clips load and tones decode on the loader thread */
    int32_t  preload_done;      /* the preloads asked for are done */
    uint32_t tones_decoded;     /* tones resolved (decoded or found empty) */
    uint32_t clips_cached;      /* ADX files held (ready, waiting or failed) */
    uint32_t clips_pinned;      /* of them, preloaded jingles */
    uint32_t clips_waiting;     /* queued or being read */
    uint64_t cache_bytes;       /* bytes of the files held */
    uint32_t clip_reads;        /* files read from the disc */
    uint32_t clip_hits;         /* clip starts that found their file in the cache */
    uint32_t read_errors;       /* reads that failed */
    uint32_t caller_reads;      /* files read on the calling thread (SA1_AUDIO_SYNC_LOADS, sa1_audio_stream_open) */
    uint32_t cues_deferred;     /* cues that waited for their tone to be decoded */
    uint32_t cues_dropped;      /* of them, given up after half a second */
    float    max_read_ms;       /* the slowest file read */
} sa1_audio_stats;

SA1_API sa1_status sa1_audio_get_stats(const sa1_audio *audio, sa1_audio_stats *out);
/* The stage MLT the game loads for a stage number ("EMERALDCOAST.MLT"); NULL if out of range.
 * flags: SA1_AUDIO_JAPANESE matters for Sky Deck. */
SA1_API const char *sa1_audio_stage_file(int32_t stage, uint32_t flags);

/* ------------------------------------------------------------------------- */
/* Sound effects (tones)                                                     */
/* ------------------------------------------------------------------------- */

#define SA1_AUDIO_SOUND_NONE      0   /* nothing to play: empty program, or its bank is not loaded */
#define SA1_AUDIO_SOUND_PROGRAM   1   /* program `request` of bank `bank` at note 60: one or more layers */
#define SA1_AUDIO_SOUND_SEQUENCE  2   /* sequence `request` of the bank (3D slot tones), rendered by libsa1
                                         into one 44.1 kHz layer */
#define SA1_AUDIO_SOUND_VOICE     3   /* tone > 1274: voice clip `voice` (sa1_audio_stream_open) */

typedef struct sa1_audio_sound {
    uint32_t    struct_size;
    uint32_t    tone;           /* the SE number asked for */
    int32_t     kind;           /* SA1_AUDIO_SOUND_* */
    uint32_t    bank;           /* the game's sound bank (0..8) */
    uint32_t    request;        /* program or sequence number in the bank */
    const char *file;           /* MLT that provides the bank, e.g. "P_SONICTAILS.MLT"; NULL if none is loaded */
    uint32_t    voice;          /* kind VOICE: the voice number */
    uint32_t    num_layers;     /* layers that sound together (sa1_audio_sound_layer 0..num_layers-1) */
    int32_t     loops;          /* 1 if a layer loops: the sound lasts until it is stopped */
    float       duration;       /* seconds until the last non-looping layer ends (delays included);
                                   looping sounds: until the first loop end */
    float       radius;         /* SA1 units: positional calls of this tone are played only within this
                                   distance of the camera (the game's 3D table); 0 = no limit */
} sa1_audio_sound;

/* Resolves a tone (decoding its samples on first use).  SA1_ERR_NOT_FOUND if the tone is outside the
 * game's table; kind NONE (and SA1_OK) if the bank is not loaded or the program is empty. */
SA1_API sa1_status sa1_audio_sound_info(sa1_audio *audio, uint32_t tone, sa1_audio_sound *out);

/* Source formats (sa1_audio_layer.format) */
#define SA1_AUDIO_FORMAT_PCM16    0
#define SA1_AUDIO_FORMAT_PCM8     1
#define SA1_AUDIO_FORMAT_ADPCM    2   /* Yamaha AICA 4-bit ADPCM */
#define SA1_AUDIO_FORMAT_ADPCM_LS 3   /* the same, "long stream" loop mode */
#define SA1_AUDIO_FORMAT_RENDERED 4   /* a sequence rendered by libsa1 */

/* sa1_audio_layer.flags: AICA features a host may want to know about (not applied to the PCM) */
#define SA1_AUDIO_LAYER_LFO       0x1u  /* pitch / amplitude LFO (vibrato, tremolo) */
#define SA1_AUDIO_LAYER_FILTER    0x2u  /* the low-pass filter / filter envelope is on */
#define SA1_AUDIO_LAYER_EFFECT    0x4u  /* sends to the DSP (reverb / echo) */

typedef struct sa1_audio_layer {
    uint32_t       struct_size;
    const int16_t *pcm;         /* mono signed 16-bit, owned by the audio object: valid until close (stage
                                   banks: until sa1_audio_set_stage) */
    uint32_t       num_samples;
    float          sample_rate; /* Hz at which the AICA plays it for this tone (44100 * 2^((60 - base_note +
                                   fine_tune / 256) / 12)); e.g. 22050 for most player sounds */
    int32_t        loop;        /* 1: after the end, playback continues from loop_start forever */
    uint32_t       loop_start;  /* samples */
    uint32_t       loop_end;    /* = num_samples */
    float          delay;       /* seconds after the start of the sound */
    float          gain;        /* linear, 1 = 0 dB (total level TL and direct send level DISDL) */
    float          pan;         /* -1 left .. +1 right (one side is attenuated, the other stays at gain) */
    /* amplitude envelope (AICA EG): attack ramp, then decay1 until decay_level, then decay2; after the
       sound is stopped (key off) the level falls at `release` */
    float          attack;      /* seconds from silence to full level */
    float          decay1;      /* dB per second (0 = none) */
    float          decay_level; /* dB below full level where decay2 takes over (0: decay2 from the start) */
    float          decay2;      /* dB per second */
    float          release;     /* dB per second; >= 10000 is an immediate cut */
    uint32_t       format;      /* SA1_AUDIO_FORMAT_* of the source data */
    uint32_t       flags;       /* SA1_AUDIO_LAYER_* */
    uint8_t        base_note;
    uint8_t        low_note;
    uint8_t        high_note;
    int8_t         fine_tune;   /* 1/256 semitone */
    uint16_t       lfo;         /* raw AICA LFO register (LFORE, LFOF, PLFOWS, PLFOS, ALFOWS, ALFOS) */
    uint16_t       reserved;
} sa1_audio_layer;

SA1_API sa1_status sa1_audio_sound_layer(sa1_audio *audio, uint32_t tone, uint32_t layer, sa1_audio_layer *out);

/* ------------------------------------------------------------------------- */
/* Voices and music (CRI ADX)                                                */
/* ------------------------------------------------------------------------- */

#define SA1_AUDIO_SOURCE_VOICE 0    /* index = voice number = entry of the voice AFS (0..count-1) */
#define SA1_AUDIO_SOURCE_MUSIC 1    /* index = music number (0..count-1): SONICADV/<sa1_audio_music_name> */

SA1_API uint32_t    sa1_audio_voice_count(const sa1_audio *audio);
SA1_API uint32_t    sa1_audio_music_count(const sa1_audio *audio);
SA1_API const char *sa1_audio_music_name(const sa1_audio *audio, uint32_t music);   /* "INVNCIBL.ADX" */

typedef struct sa1_audio_stream_info {
    uint32_t struct_size;
    uint32_t channels;          /* 1 (voices) or 2 (music) */
    uint32_t sample_rate;       /* 22050 (voices), 32000 or 44100 (music) */
    uint32_t num_frames;        /* sample frames */
    int32_t  loop;              /* the file has a loop */
    uint32_t loop_start;        /* frames */
    uint32_t loop_end;          /* frames (exclusive) */
} sa1_audio_stream_info;

typedef struct sa1_audio_stream sa1_audio_stream;

/* Reads the ADX file into memory and prepares decoding (a voice is ~30 KB, music up to ~15 MB), on the calling
 * thread (from the cache when a mixer has it).  SA1_ERR_NOT_FOUND without any read for a music file that is not
 * on the disc (69).  With SA1_AUDIO_LAZY_STREAMS a music stream reads only the header here (the buffer is still the
 * file's size) and the rest in sa1_audio_stream_read / _seek as decoding reaches it: a read error is then their
 * status. */
SA1_API sa1_status sa1_audio_stream_open(sa1_audio *audio, int32_t source, uint32_t index, sa1_audio_stream **out,
                                         sa1_audio_stream_info *info);
/* Decodes up to `frames` interleaved PCM16 frames; *got < frames only at the end (loop = 0 or no loop in
 * the file).  With loop = 1 a looping file wraps from loop_end to loop_start. */
SA1_API sa1_status sa1_audio_stream_read(sa1_audio_stream *stream, int16_t *out, uint32_t frames, int32_t loop,
                                         uint32_t *got);
SA1_API sa1_status sa1_audio_stream_seek(sa1_audio_stream *stream, uint32_t frame);
SA1_API void       sa1_audio_stream_close(sa1_audio_stream *stream);

/* ------------------------------------------------------------------------- */
/* Music list (header version 3)                                             */
/* ------------------------------------------------------------------------- */
/*
 * The game's music: 110 numbers (the name table at 0x8C5FB8A8), 109 files on the disc (69 NIGHTS_K.ADX is named but
 * absent).  The game's own sound test (the menu overlay ADVERTISE.PRS: records {char *title; s32 music} at 0x8C90F5CC,
 * six pages {record *, count} at 0x8C90F90C, read by the menu code 8C907B80) lists 104 of them with titles; the titles
 * are ripped from the user's disc on the first call (ADVERTISE.PRS read once, PRS-decompressed and accepted only when
 * its SHA-1 is the supported one unless SA1_AUDIO_ACCEPT_MODIFIED; only the title strings are kept, about 2 KB).
 */

/* sa1_audio_music.flags */
#define SA1_AUDIO_MUSIC_ON_DISC    0x1u   /* the file exists (69 NIGHTS_K.ADX does not) */
#define SA1_AUDIO_MUSIC_LISTED     0x2u   /* in the game's sound test (title, page and entry are set) */
#define SA1_AUDIO_MUSIC_JINGLE     0x4u   /* the game plays it as a jingle (8C107C80: over the background music, which
                                             restarts afterwards): 26 44 45 46 55 56 85 96 */
#define SA1_AUDIO_MUSIC_TRUNCATED  0x8u   /* the file holds fewer frames than its header says (87 SSRACING.ADX) */

typedef struct sa1_audio_music {
    uint32_t    struct_size;
    uint32_t    music;          /* the music number */
    const char *file;           /* "ECOAST1.ADX" (owned by the audio object, valid until close) */
    const char *title;          /* the sound-test title ("Emerald Coast:  Type 1", ASCII, as the game spells it); NULL
                                   when not listed or when ADVERTISE.PRS is not the supported one */
    int32_t     page;           /* sound-test page 0..5; -1 when not listed */
    int32_t     entry;          /* position on the page, 0-based; -1 when not listed */
    uint32_t    flags;          /* SA1_AUDIO_MUSIC_* */
    uint32_t    channels;       /* ON_DISC only: the ADX header, as sa1_audio_stream_open reports it */
    uint32_t    sample_rate;
    uint32_t    num_frames;     /* the frames present in the file */
    int32_t     loop;
    uint32_t    loop_start;     /* frames */
    uint32_t    loop_end;       /* frames (exclusive) */
    uint32_t    file_size;      /* bytes */
} sa1_audio_music;

/* Describes music number `music`: SA1_ERR_NOT_FOUND for music >= sa1_audio_music_count; SA1_OK for a number whose file
 * is not on the disc (flags without ON_DISC, header fields 0).  On the calling thread: the first call reads
 * ADVERTISE.PRS (87 sectors), the first call for a number reads its ADX header (the first 4,096 bytes of the file:
 * 2 sectors); later calls read nothing.  A header that cannot be read gives that status, with the fields that do
 * not need it filled. */
SA1_API sa1_status sa1_audio_music_info(sa1_audio *audio, uint32_t music, sa1_audio_music *out);

/* ------------------------------------------------------------------------- */
/* Cues: what an event asks for                                              */
/* ------------------------------------------------------------------------- */

#define SA1_AUDIO_CUE_NONE          0   /* not a sound event, or nothing to do */
#define SA1_AUDIO_CUE_PLAY          1   /* start `tone` (instances may overlap) */
#define SA1_AUDIO_CUE_KEEP          2   /* start `tone` unless the character still plays it from a KEEP cue:
                                           then only refresh its lifetime and position (sent every frame) */
#define SA1_AUDIO_CUE_STOP          3   /* stop every instance of `tone` of the character; tone 0: every
                                           OWNED sound of the character */
#define SA1_AUDIO_CUE_VOICE         4   /* play voice `index` (a new voice of a character replaces its last) */
#define SA1_AUDIO_CUE_MUSIC         5   /* change the background music to music `index` (the old one stops first, as
                                           the game's ADXT_StartFname does: for a file not on the disc the music
                                           stops and the mixer returns SA1_ERR_NOT_FOUND); since header version 4 it
                                           also ends a playing jingle, as in the game */
#define SA1_AUDIO_CUE_JINGLE        6   /* play music `index` instead of the background music, then resume it (a file
                                           with a loop, 45 FISHING.ADX, loops until the game ends it: header version 4) */
#define SA1_AUDIO_CUE_MUSIC_RESTORE 7   /* end a jingle / return to the saved background music */
#define SA1_AUDIO_CUE_MUSIC_STOP    8   /* stop the music */

/* sa1_audio_cue.flags */
#define SA1_AUDIO_CUE_POSITIONAL    0x1u    /* the game attenuates it by the distance from the camera */
#define SA1_AUDIO_CUE_OWNED         0x2u    /* bound to the character (stopped by STOP with tone 0) */

typedef struct sa1_audio_cue {
    uint32_t         struct_size;
    sa1_character_id character;
    int32_t          type;          /* SA1_AUDIO_CUE_* */
    uint32_t         flags;         /* SA1_AUDIO_CUE_* flags */
    uint32_t         tone;          /* PLAY, KEEP, STOP */
    uint32_t         index;         /* VOICE: voice number; MUSIC, JINGLE: music number */
    float            position[3];   /* host space (the event's position; the character for owned sounds) */
    float            min_distance;  /* host units: full volume within this distance of the listener */
    float            max_distance;  /* host units: silent from here on (the game: 600 SA1 units) */
    float            start_radius;  /* host units: the game does not start a positional sound farther than
                                       this from the camera (the tone's 3D radius); 0 = no limit */
    float            volume;        /* linear, 1 = the game's normal SE level */
    float            delay;         /* seconds to wait before starting */
    float            lifetime;      /* seconds after which the game stops it; < 0: until stopped (or the end
                                       of a non-looping sample); KEEP: stop when not refreshed for this long */
    uint32_t         source;        /* the DC function behind the event (sa1_event.arg) */
} sa1_audio_cue;

/* Fills `out` for any event; non-sound events give type SA1_AUDIO_CUE_NONE and SA1_OK. */
SA1_API sa1_status sa1_audio_cue_from_event(sa1_audio *audio, const sa1_event *event, sa1_audio_cue *out);

/* ------------------------------------------------------------------------- */
/* Mixer                                                                     */
/* ------------------------------------------------------------------------- */

typedef struct sa1_audio_mixer sa1_audio_mixer;

/* sa1_audio_mixer_desc.flags */
#define SA1_AUDIO_MIX_MUSIC        0x1u   /* follow MUSIC cues (background music); default: jingles only */
#define SA1_AUDIO_MIX_NO_JINGLES   0x2u
#define SA1_AUDIO_MIX_NO_VOICES    0x4u
#define SA1_AUDIO_MIX_3D_VOICES    0x8u   /* voices of non-local characters come from their position */
#define SA1_AUDIO_MIX_RESTART_REPEATS 0x10u /* header version 5, a host option (default off = the game): a one-shot sound
                                             effect (a PLAY cue, neither POSITIONAL nor OWNED: 8C0940DA, 8C04EF22) that a
                                             character starts while it still plays the same tone from such a cue restarts:
                                             the older instance is keyed off at once (faded out within about 4 ms) and the
                                             new one starts, so a fast ring line (tone 7) stays at about one ring's level.
                                             The game lets them pile up: 8C0940DA gives every call its own slot of the
                                             sound queue (8C093F02: the next of 32 in turn) and every slot is its own MIDI
                                             channel (8C093140 / 8C093360), so 10 rings 50 ms apart sound together (sadc
                                             docs/research/super2_plan.md 10.3). Other tones, other characters, KEEP,
                                             OWNED and positional sounds overlap as without the flag */

typedef struct sa1_audio_mixer_desc {
    uint32_t struct_size;
    uint32_t sample_rate;       /* output rate, 8000..192000 (0 = 44100) */
    uint32_t flags;             /* SA1_AUDIO_MIX_* */
    uint32_t max_voices;        /* simultaneous sample voices, 1..256 (0 = 64, like the AICA) */
} sa1_audio_mixer_desc;

/* Volume channels (sa1_audio_mixer_volume) */
#define SA1_AUDIO_VOL_MASTER 0
#define SA1_AUDIO_VOL_SFX    1
#define SA1_AUDIO_VOL_VOICE  2
#define SA1_AUDIO_VOL_MUSIC  3

SA1_API sa1_status sa1_audio_mixer_create(sa1_audio *audio, const sa1_audio_mixer_desc *desc, sa1_audio_mixer **out);
SA1_API void       sa1_audio_mixer_destroy(sa1_audio_mixer *mixer);
/* sa1_audio_cue_from_event + sa1_audio_mixer_cue; events without sound are ignored (SA1_OK). */
SA1_API sa1_status sa1_audio_mixer_event(sa1_audio_mixer *mixer, const sa1_event *event);
SA1_API sa1_status sa1_audio_mixer_cue(sa1_audio_mixer *mixer, const sa1_audio_cue *cue);
/* The ears (host space): position and the forward and up directions (need not be normalized). */
SA1_API sa1_status sa1_audio_mixer_listener(sa1_audio_mixer *mixer, const float position[3], const float forward[3],
                                            const float up[3]);
/* Where a character is (host space).  local = 1: its non-positional sounds and voices play like in the
 * game (2D, the player's own character); local = 0: they come from `position`.  Call it every tick for
 * every character with sound; unknown characters are treated as local. */
SA1_API sa1_status sa1_audio_mixer_character(sa1_audio_mixer *mixer, sa1_character_id character,
                                             const float position[3], int32_t local);
/* 0..4 (linear gain), per SA1_AUDIO_VOL_* channel. */
SA1_API sa1_status sa1_audio_mixer_volume(sa1_audio_mixer *mixer, int32_t channel, float volume);

/* sa1_audio_mixer_duck flags (header version 3) */
#define SA1_AUDIO_DUCK_JINGLES     0x1u   /* duck the jingles too (default: they keep their level, so that the drowning
                                             countdown 96 stays audible) */
#define SA1_AUDIO_DUCK_NOW         0x2u   /* jump to the gain (default: a linear ramp, 0.5 s from 1 to 0) */
/* A host's duck of the music (e.g. while a jukebox plays): the background music, and with SA1_AUDIO_DUCK_JINGLES the
 * jingles, are multiplied by `gain` (0..1, on top of SA1_AUDIO_VOL_MUSIC); 1 restores them.  The music keeps playing
 * (ducked to 0 it goes on silently). */
SA1_API sa1_status sa1_audio_mixer_duck(sa1_audio_mixer *mixer, float gain, uint32_t flags);
/* Changes the SA1_AUDIO_MIX_* flags (e.g. from a host's options menu); switching voices, jingles or the
 * background music off stops what is playing of them. */
SA1_API sa1_status sa1_audio_mixer_flags(sa1_audio_mixer *mixer, uint32_t flags);
/* Stops every sound of a character (0 = everything, music included), e.g. when it is destroyed. */
SA1_API sa1_status sa1_audio_mixer_stop(sa1_audio_mixer *mixer, sa1_character_id character);
/* Renders `frames` interleaved stereo PCM16 frames (out holds 2 * frames samples) and advances time. */
SA1_API sa1_status sa1_audio_mixer_render(sa1_audio_mixer *mixer, int16_t *out, uint32_t frames);
/* Playing sample voices + voice clips + music streams. */
SA1_API uint32_t   sa1_audio_mixer_active(const sa1_audio_mixer *mixer);

/* ------------------------------------------------------------------------- */
/* Music state (header version 4)                                            */
/* ------------------------------------------------------------------------- */
/*
 * The game's music system (8C107xxx, sadc docs/research/sa1_sound.md section 4): 8C107AC6 asks for background music
 * (and saves it); 8C107C80 plays a jingle *instead of* it without saving it, and its task 8C107CC2 asks for the saved
 * track again when the jingle has ended; 8C107B26 goes back to the saved track (nothing when none is saved);
 * 8C107ADA stops the music and forgets the saved track.  The drowning countdown is jingle 96 (requested in two
 * consecutive frames), the drown stops the music (8C107ADA) and the countdown task's destructor sends 8C107B26 one
 * frame after the restart request.  The 1-up is a sound effect (tone 704, 8C08A120), not a jingle: no game call asks
 * for 71 ONE_UP.ADX with a constant.
 */

/* sa1_audio_music_state.jingle_kind: what the game plays the jingle for (the call sites of 8C107C80, all with a
   constant music number; these eight are SA1_AUDIO_MUSIC_JINGLE) */
#define SA1_AUDIO_JINGLE_NONE          0   /* no jingle */
#define SA1_AUDIO_JINGLE_OTHER         1   /* a music number the game never plays as a jingle (a host's own JINGLE cue) */
#define SA1_AUDIO_JINGLE_COUNTDOWN     2   /* 96 TIMER.ADX: the drowning countdown (8C07BE6E) */
#define SA1_AUDIO_JINGLE_INVINCIBLE    3   /* 55 INVNCIBL.ADX: the invincibility item (8C07D4A0) */
#define SA1_AUDIO_JINGLE_SPEED_UP      4   /* 85 SPEEDUP.ADX: the speed shoes (8C07D2DA) */
#define SA1_AUDIO_JINGLE_FISHING       5   /* 45 FISHING.ADX: Big's fishing (8C0EB244); its file loops */
#define SA1_AUDIO_JINGLE_FISH_CAUGHT   6   /* 44 FISHGET.ADX: Big landed a fish (8C0E6446) */
#define SA1_AUDIO_JINGLE_FISH_LOST     7   /* 46 FISHMISS.ADX: Big's line broke (8C0E8562) */
#define SA1_AUDIO_JINGLE_ITEM          8   /* 56 ITEM1.ADX: an item found in an adventure field (ADV00 overlay) */
#define SA1_AUDIO_JINGLE_CONTINUE      9   /* 26 CONTINUE.ADX: the continue / game over screen (8C04CA40, 8C04DA20, ...) */

/* sa1_audio_music_state.last_end: how the last jingle ended */
#define SA1_AUDIO_JINGLE_END_NONE      0   /* none has ended yet */
#define SA1_AUDIO_JINGLE_END_PLAYED    1   /* it played to its end (the game then asks for the saved track: 8C107CC2) */
#define SA1_AUDIO_JINGLE_END_RESTORE   2   /* MUSIC_RESTORE (8C107B26): e.g. the countdown cancelled because the character
                                              got air, or Big's fishing over */
#define SA1_AUDIO_JINGLE_END_STOP      3   /* MUSIC_STOP (8C107ADA): e.g. the character drowned */
#define SA1_AUDIO_JINGLE_END_MUSIC     4   /* a MUSIC cue (8C107AC6 / 8C04EF52: new background music) */
#define SA1_AUDIO_JINGLE_END_REPLACED  5   /* a JINGLE cue (another jingle, or the same one again: the game restarts it) */
#define SA1_AUDIO_JINGLE_END_HOST      6   /* the host: sa1_audio_mixer_stop(mixer, 0) or SA1_AUDIO_MIX_NO_JINGLES */
#define SA1_AUDIO_JINGLE_END_FAILED    7   /* its file could not be read */

typedef struct sa1_audio_music_state {
    uint32_t struct_size;       /* sizeof(sa1_audio_music_state); a smaller (older) struct gets what fits */
    int32_t  jingle;            /* the music number of the jingle this mixer plays or is about to play (its file being
                                   read, its delay running); -1 none (always -1 with SA1_AUDIO_MIX_NO_JINGLES) */
    float    jingle_left;       /* seconds until it ends by itself, its delay included; -1 while it loops (jingle_loops:
                                   until the game ends it) or while its length is not known (jingle_length 0: its file
                                   is being read); 0 without a jingle */
    int32_t  stopped;           /* 1 after the game's music stop (MUSIC_STOP, 8C107ADA: the drown) until the next MUSIC,
                                   JINGLE or MUSIC_RESTORE cue (the countdown's destructor sends MUSIC_RESTORE one frame
                                   after the restart request: a host that waits for the respawn holds on its own) */
    int32_t  bgm;               /* the background music the game asked for (MUSIC cues: 8C107AC6's saved track), -1 none
                                   or after a music stop; whether or not this mixer plays it (SA1_AUDIO_MIX_MUSIC) */
    int32_t  jingle_kind;       /* SA1_AUDIO_JINGLE_*: what the game plays it for */
    int32_t  jingle_waiting;    /* 1: not heard yet (its file is being read or its delay runs) */
    int32_t  jingle_loops;      /* 1: its file has a loop (45 FISHING.ADX): it plays until the game ends it */
    float    jingle_length;     /* seconds of its file (the frames present / the rate); 0 while the file is being read */
    float    jingle_time;       /* seconds of it heard so far (loops included) */
    uint32_t jingles_started;   /* jingles this mixer started since it was created (JINGLE cues it played) */
    uint32_t jingles_ended;     /* of them, those that ended: jingles_started - jingles_ended is 1 while `jingle` is set */
    int32_t  last_jingle;       /* the music number of the last jingle that ended, -1 none */
    int32_t  last_end;          /* how it ended: SA1_AUDIO_JINGLE_END_* */
    int32_t  bgm_playing;       /* 1: this mixer plays the background music and it is heard now (SA1_AUDIO_MIX_MUSIC, its
                                   file read, not replaced by a jingle; a host duck does not count) */
} sa1_audio_music_state;

/* The game's music state in this mixer (see above), e.g. for a jukebox that pauses while the game plays a jingle and
 * stays quiet after the game's music stop.  No disc access, no lock, no allocation: call it every frame (on the
 * mixer's thread, like every mixer call).  SA1_ERR_INVALID_ARGUMENT without a mixer, without `out` or when
 * out->struct_size < 4. */
SA1_API sa1_status sa1_audio_mixer_music_state(const sa1_audio_mixer *mixer, sa1_audio_music_state *out);

/* ------------------------------------------------------------------------- */
/* Stage rules (header version 5)                                            */
/* ------------------------------------------------------------------------- */
/*
 * Stage rules (header version 5; sadc docs/research/super2_sound.md 6). The game loads ONE stage MLT (8C09340C: banks 0,
 * 1, 2, 4, 5), and every stage-bank tone resolves against it. A tone of another stage's range therefore plays whatever
 * that request holds in the loaded bank: Super Sonic's whoosh 1035 / 1036 is CHAOS7B.MLT bank 5; the board's snow 237 is
 * ICECAP.MLT bank 1 and its sand 691 is SANDHILL.MLT bank 1.
 * A stage rule makes the tones first..last of ONE character resolve as on `stage`. Their bank comes from that stage's MLT:
 * only the banks the range needs are read, and only the range's tones are decoded, both on the loader thread. Every other
 * tone and every other character keeps the object's own stage (sa1_audio_set_stage, which keeps the rules and their banks).
 * Setting or clearing a rule never stops a sound and never reads the disc on the caller's thread (SA1_AUDIO_SYNC_LOADS:
 * it does).
 * Until the banks are read, the rule's tones wait like a tone being decoded (at most half a second). If the read fails,
 * they stay silent: never the object's own stage's program. The first cue that meets such a failed stage logs one
 * SA1_LOG_WARN on the mixer's thread naming the stage, its MLT and the bank (the loader thread only records the failure).
 * The rules G-SA1 uses: 1035-1036 -> 19 (Perfect Chaos: the aura's whoosh); 208-238 -> 8 (Ice Cap: the board's ramp,
 * crash and snow); 690-698 -> 38 (Sand Hill: the board's sand).
 * Memory: a rule holds the needed banks of its stage and the range's decoded tones until sa1_audio_close (measured: the
 * whoosh rule +0.31 MB; the three rules above together +2.3 MB).
 */
#define SA1_AUDIO_HAS_STAGE_RULES  1
#define SA1_AUDIO_MAX_STAGE_SETS   4    /* other stages an audio object holds at once (besides its own); held until close */
#define SA1_AUDIO_MAX_STAGE_RULES  4    /* rules per character in a mixer */

/* sa1_audio_stage_state */
#define SA1_AUDIO_STAGE_ABSENT     0    /* no rule asked for this stage */
#define SA1_AUDIO_STAGE_LOADING    1    /* its banks are being read / the rules' tones decoded */
#define SA1_AUDIO_STAGE_READY      2
#define SA1_AUDIO_STAGE_FAILED     3    /* its MLT (or a needed bank of it) could not be read: those tones stay silent */

/* slot 0..SA1_AUDIO_MAX_STAGE_RULES-1; the first matching slot wins. stage -1 clears the slot (first / last ignored). The
 * character's mixer entry is made if needed; sa1_audio_mixer_stop(mixer, character) forgets it with its rules. A rule
 * applies to PLAY and KEEP cues of that character whose tone is within first..last and whose bank (the game's tone table)
 * is a stage bank (0, 1, 2, 4, 5); STOP cues match by tone as before.
 * SA1_ERR_LIMIT: the object already holds SA1_AUDIO_MAX_STAGE_SETS other stages.
 * SA1_ERR_INVALID_ARGUMENT: a bad stage, range (last > 1274, first > last) or slot.
 * SA1_ERR_UNSUPPORTED: SA1_AUDIO_MUSIC_ONLY. */
SA1_API sa1_status sa1_audio_mixer_stage_rule(sa1_audio_mixer *mixer, sa1_character_id character, uint32_t slot,
                                              int32_t stage, uint32_t first_tone, uint32_t last_tone);
/* SA1_AUDIO_STAGE_* of an extra stage (the object's own stage: READY); on the mixer's thread, no disc access */
SA1_API int32_t    sa1_audio_stage_state(sa1_audio *audio, int32_t stage);

#ifdef __cplusplus
}
#endif
#endif /* LIBSA1_AUDIO_H */
