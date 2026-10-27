/*
 * libsa1 audio synthesis: AICA-like sample voices (pitch, loop, envelope, level, pan), the sequence
 * renderer for the 3D-slot tones (SMSB/SMSD), and the mixer behind sa1_audio_mixer_*.
 */
#include "sa1_audio.h"

#include <math.h>

/* ------------------------------------------------------------------------- */
/* AICA envelope rates                                                       */
/* ------------------------------------------------------------------------- */
/*
 * Time in ms for a full attack (silence -> 0 dB) and for a full decay (0 dB -> -96 dB) per rate index
 * 0..63 (2 R + key scaling), from the SCSP/AICA documentation as used by MAME's aica.cpp; indexes 0 and 1
 * never move.
 */
static const float k_attack_ms[64] = {
    1e9f, 1e9f, 8100.0f, 6900.0f, 6000.0f, 4800.0f, 4000.0f, 3400.0f, 3000.0f, 2400.0f, 2000.0f, 1700.0f, 1500.0f,
    1200.0f, 1000.0f, 860.0f, 760.0f, 600.0f, 500.0f, 430.0f, 380.0f, 300.0f, 250.0f, 220.0f, 190.0f, 150.0f, 130.0f,
    110.0f, 95.0f, 76.0f, 63.0f, 55.0f, 47.0f, 38.0f, 31.0f, 27.0f, 24.0f, 19.0f, 15.0f, 13.0f, 12.0f, 9.4f, 7.9f,
    6.8f, 6.0f, 4.7f, 3.8f, 3.4f, 3.0f, 2.4f, 2.0f, 1.8f, 1.6f, 1.3f, 1.1f, 0.93f, 0.85f, 0.65f, 0.53f, 0.44f, 0.40f,
    0.35f, 0.0f, 0.0f
};
static const float k_decay_ms[64] = {
    1e9f, 1e9f, 118200.0f, 101300.0f, 88600.0f, 70900.0f, 59100.0f, 50700.0f, 44300.0f, 35500.0f, 29600.0f,
    25300.0f, 22200.0f, 17700.0f, 14800.0f, 12700.0f, 11100.0f, 8900.0f, 7400.0f, 6300.0f, 5500.0f, 4400.0f,
    3700.0f, 3200.0f, 2800.0f, 2200.0f, 1800.0f, 1600.0f, 1400.0f, 1100.0f, 920.0f, 790.0f, 690.0f, 550.0f, 460.0f,
    390.0f, 340.0f, 270.0f, 230.0f, 200.0f, 170.0f, 140.0f, 110.0f, 98.0f, 85.0f, 68.0f, 57.0f, 49.0f, 43.0f, 34.0f,
    28.0f, 25.0f, 22.0f, 18.0f, 14.0f, 12.0f, 11.0f, 8.5f, 7.1f, 6.1f, 5.4f, 4.3f, 3.6f, 3.1f
};

float sa1a_attack_seconds(int32_t idx)
{
    if (idx < 2)
        return 1e6f;                            /* never reaches full level: silent */
    if (idx > 63)
        idx = 63;
    return k_attack_ms[idx] / 1000.0f;
}

float sa1a_decay_db_per_second(int32_t idx)
{
    if (idx < 2)
        return 0.0f;
    if (idx > 63)
        idx = 63;
    return 96000.0f / k_decay_ms[idx];
}

/* ------------------------------------------------------------------------- */
/* Voices                                                                    */
/* ------------------------------------------------------------------------- */

#define EG_SILENT_DB 96.0f

void sa1a_voice_start(sa1a_voice *v, const sa1a_layer *l, float semitones, float rate, float gain)
{
    float p = l->pub.pan, g = gain * l->pub.gain;
    memset(v, 0, sizeof *v);
    v->pcm = l->pub.pcm;
    v->n = l->pub.num_samples;
    v->loop = l->pub.loop && l->pub.loop_start < l->pub.num_samples;
    v->loop_start = l->pub.loop_start;
    v->step = (double)l->pub.sample_rate * pow(2.0, (double)semitones / 12.0) / (double)rate;
    v->bend = 1.0;
    v->gain_l = g * (p > 0.0f ? 1.0f - p : 1.0f);
    v->gain_r = g * (p < 0.0f ? 1.0f + p : 1.0f);
    v->delay = (uint32_t)(l->pub.delay * rate);
    v->dl = l->pub.decay_level;
    v->d1 = l->pub.decay1 / rate;
    v->d2 = l->pub.decay2 / rate;
    v->rel = l->pub.release / rate;
    if (l->pub.attack * rate <= 1.0f) {
        v->level = 1.0f;
        v->attack_inc = 1.0f;
    } else {
        v->attack_inc = 1.0f / (l->pub.attack * rate);
    }
    v->stage = SA1A_EG_ATTACK;
    if (!v->pcm || v->n == 0 || v->step <= 0.0)
        v->stage = SA1A_EG_DONE;
}

void sa1a_voice_release(sa1a_voice *v)
{
    if (v->stage == SA1A_EG_DONE || v->stage == SA1A_EG_RELEASE)
        return;
    if (v->stage == SA1A_EG_ATTACK || (v->stage == SA1A_EG_DECAY1 && v->att <= 0.0f)) {
        float lv = v->stage == SA1A_EG_ATTACK ? v->level : 1.0f;
        v->att = lv > 1e-5f ? -20.0f * (float)log10((double)lv) : EG_SILENT_DB;
    }
    if (v->delay) {                             /* released before it started */
        v->stage = SA1A_EG_DONE;
        return;
    }
    v->stage = v->rel > 0.0f ? SA1A_EG_RELEASE : SA1A_EG_DONE;
    if (v->rel <= 0.0f)                         /* RR 0 never fades: the AICA would hold it; cut instead */
        v->stage = SA1A_EG_DONE;
}

static float eg_amp(const sa1a_voice *v)
{
    if (v->stage == SA1A_EG_DONE)
        return 0.0f;
    if (v->stage == SA1A_EG_ATTACK)
        return v->level;
    return v->att <= 0.0f ? 1.0f : (float)pow(10.0, -(double)v->att / 20.0);
}

static void eg_advance(sa1a_voice *v, uint32_t k)
{
    while (k && v->stage != SA1A_EG_DONE) {
        switch (v->stage) {
        case SA1A_EG_ATTACK: {
            float need = (1.0f - v->level) / v->attack_inc;
            if ((float)k >= need) {
                uint32_t used = (uint32_t)need;
                v->level = 1.0f;
                v->att = 0.0f;
                v->stage = v->dl > 0.0f ? SA1A_EG_DECAY1 : SA1A_EG_DECAY2;
                k = used < k ? k - used : 0;
            } else {
                v->level += (float)k * v->attack_inc;
                k = 0;
            }
            break;
        }
        case SA1A_EG_DECAY1:
            if (v->d1 <= 0.0f) {
                k = 0;
            } else {
                float need = (v->dl - v->att) / v->d1;
                if ((float)k >= need) {
                    uint32_t used = (uint32_t)need;
                    v->att = v->dl;
                    v->stage = SA1A_EG_DECAY2;
                    k = used < k ? k - used : 0;
                } else {
                    v->att += (float)k * v->d1;
                    k = 0;
                }
            }
            break;
        case SA1A_EG_DECAY2:
            v->att += (float)k * v->d2;
            k = 0;
            break;
        case SA1A_EG_RELEASE:
            v->att += (float)k * v->rel;
            k = 0;
            break;
        default:
            k = 0;
            break;
        }
        if (v->att >= EG_SILENT_DB)
            v->stage = SA1A_EG_DONE;
    }
}

void sa1a_voice_render(sa1a_voice *v, float *l, float *r, uint32_t n, float gl, float gr)
{
    uint32_t i = 0;
    float vl = v->gain_l * gl, vr = v->gain_r * gr;
    double step = v->step * v->bend;
    if (!r) {                                   /* mono: average the two sides */
        vl = 0.5f * (vl + vr);
        vr = 0.0f;
    }
    while (i < n && v->stage != SA1A_EG_DONE) {
        uint32_t len, j;
        float a0, a1;
        if (v->delay) {
            uint32_t skip = v->delay < n - i ? v->delay : n - i;
            v->delay -= skip;
            i += skip;
            continue;
        }
        len = n - i < 32u ? n - i : 32u;
        a0 = eg_amp(v);
        eg_advance(v, len);
        a1 = eg_amp(v);
        for (j = 0; j < len; j++) {
            uint32_t idx = (uint32_t)v->pos;
            float frac = (float)(v->pos - (double)idx), s0, s1, s, amp;
            if (idx >= v->n) {
                v->stage = SA1A_EG_DONE;
                break;
            }
            s0 = (float)v->pcm[idx];
            if (idx + 1u < v->n)
                s1 = (float)v->pcm[idx + 1u];
            else
                s1 = v->loop ? (float)v->pcm[v->loop_start] : 0.0f;
            amp = a0 + (a1 - a0) * ((float)j / (float)len);
            s = (s0 + (s1 - s0) * frac) * (1.0f / 32768.0f) * amp;
            l[i + j] += s * vl;
            if (r)
                r[i + j] += s * vr;
            v->pos += step;
            if (v->pos >= (double)v->n) {
                if (v->loop) {
                    double span = (double)(v->n - v->loop_start);
                    v->pos = (double)v->loop_start + fmod(v->pos - (double)v->n, span);
                } else {
                    v->stage = SA1A_EG_DONE;
                    break;
                }
            }
        }
        i += len;
    }
}

/* ------------------------------------------------------------------------- */
/* Sequences (SMSD)                                                          */
/* ------------------------------------------------------------------------- */
/*
 * "SMSD", u32 0x88, u32 tempo, then events, each followed by its delta time in ticks (480 per quarter
 * note; the tempo is in ms per quarter note: 800 for most):
 *   0x00-0x7F  note: channel = low nibble; note, velocity, gate (1 byte, 2 BE if 0x20), delta (1 byte, 2 BE
 *              if 0x10)
 *   0xB0-0xBF  control change: controller (bit 7: the delta is 2 bytes), value, delta
 *   0xC0, 0xE0 program change / pitch bend (7-bit, 64 = centre): value (bit 7: 2-byte delta), delta
 *   0x81       reference: u16 BE offset in the sequence, u8 count of events to play there
 *   0x82       rest: x (bit 7: 2-byte delta), delta
 *   0x83       end;  0x84 tempo: u16 BE, delta
 * Byte layout verified over all 5,596 sequences of the disc (12 use 0x81 or 0x88-0x8F, rare commands that
 * are skipped here); the time units are the most plausible reading, not verified against hardware.
 */
#define SEQ_MAX_EVENTS   8192u
#define SEQ_MAX_VOICES   32u
#define SEQ_TICKS        480.0

typedef struct seq_ev {
    double   time;                  /* output samples */
    uint8_t  kind, ch, a, b;        /* kind: 1 note, 2 cc, 3 program, 4 bend */
    double   gate;                  /* samples */
} seq_ev;

typedef struct seq_ch {
    uint32_t bank, program;
    float    vol, bend_semis;
    int32_t  sustain;
} seq_ch;

typedef struct seq_voice {
    int32_t    used, ch, held, keyed_off;
    double     off_time;
    float      bend_up, bend_down;
    sa1a_voice v;
} seq_voice;

typedef struct seq_parse {
    const uint8_t *m;
    uint32_t       size, start;
    double         spt;             /* samples per tick */
    double         now;
    seq_ev        *ev;
    uint32_t       nev;
    int32_t        ended;
} seq_parse;

static uint32_t seq_delta(seq_parse *p, uint32_t *q, int big)
{
    uint32_t d;
    if (big) {
        if (*q + 2u > p->size)
            return (p->ended = 1), 0u;
        d = ((uint32_t)p->m[*q] << 8) | p->m[*q + 1u];
        *q += 2u;
    } else {
        if (*q + 1u > p->size)
            return (p->ended = 1), 0u;
        d = p->m[*q];
        *q += 1u;
    }
    return d;
}

static void seq_push(seq_parse *p, uint8_t kind, uint8_t ch, uint8_t a, uint8_t b, double gate)
{
    seq_ev *e;
    if (p->nev >= SEQ_MAX_EVENTS) {
        p->ended = 1;
        return;
    }
    e = &p->ev[p->nev++];
    e->time = p->now;
    e->kind = kind;
    e->ch = ch;
    e->a = a;
    e->b = b;
    e->gate = gate;
}

/* Parses events from `pos`; `limit` events at most (0 = until the end).  Returns the position after. */
static uint32_t seq_run(seq_parse *p, uint32_t pos, uint32_t limit, int depth)
{
    uint32_t count = 0;
    while (!p->ended && pos < p->size && (limit == 0 || count < limit)) {
        uint8_t s = p->m[pos];
        uint32_t q = pos + 1u, d;
        count++;
        if (s < 0x80u) {
            uint32_t gate;
            if (q + 2u > p->size)
                break;
            {
                uint8_t note = p->m[q], vel = p->m[q + 1u];
                q += 2u;
                gate = seq_delta(p, &q, (s & 0x20u) != 0);
                d = seq_delta(p, &q, (s & 0x10u) != 0);
                seq_push(p, 1, s & 15u, note, vel, (double)gate * p->spt);
            }
        } else if ((s & 0xF0u) == 0xB0u) {
            uint8_t c, v;
            if (q + 2u > p->size)
                break;
            c = p->m[q];
            v = p->m[q + 1u];
            q += 2u;
            d = seq_delta(p, &q, (c & 0x80u) != 0);
            seq_push(p, 2, s & 15u, c & 0x7Fu, v, 0.0);
        } else if ((s & 0xF0u) == 0xC0u || (s & 0xF0u) == 0xE0u || (s & 0xF0u) == 0xA0u || (s & 0xF0u) == 0xD0u) {
            uint8_t v;
            if (q + 1u > p->size)
                break;
            v = p->m[q++];
            d = seq_delta(p, &q, (v & 0x80u) != 0);
            if ((s & 0xF0u) == 0xC0u)
                seq_push(p, 3, s & 15u, v & 0x7Fu, 0, 0.0);
            else if ((s & 0xF0u) == 0xE0u)
                seq_push(p, 4, s & 15u, v & 0x7Fu, 0, 0.0);
        } else if (s == 0x82u) {
            uint8_t x;
            if (q + 1u > p->size)
                break;
            x = p->m[q++];
            d = seq_delta(p, &q, (x & 0x80u) != 0);
        } else if (s == 0x84u) {
            uint32_t t;
            if (q + 2u > p->size)
                break;
            t = ((uint32_t)p->m[q] << 8) | p->m[q + 1u];
            q += 2u;
            d = seq_delta(p, &q, 0);
            if (t >= 50u && t <= 10000u)
                p->spt = (double)SA1A_SEQ_RATE * ((double)t / 1000.0) / SEQ_TICKS;
        } else if (s == 0x81u) {
            uint32_t off, n;
            if (q + 3u > p->size)
                break;
            off = ((uint32_t)p->m[q] << 8) | p->m[q + 1u];
            n = p->m[q + 2u];
            q += 3u;
            d = 0;
            if (depth < 4 && n && off < p->size - p->start)
                (void)seq_run(p, p->start + off, n, depth + 1);
        } else if (s >= 0x88u && s <= 0x8Fu) {
            d = 0;                              /* gate / step extension commands: skipped */
        } else {
            p->ended = 1;                       /* 0x83 end, or an unknown command */
            break;
        }
        p->now += (double)d * p->spt;
        pos = q;
    }
    return pos;
}

static sa1a_bank *seq_bank(sa1_audio *a, sa1a_bank *own, uint32_t game_bank)
{
    if (game_bank == own->game_bank)
        return own;
    return sa1a_set_bank(a, own->set, game_bank);   /* the same stage set as the sequence's own bank */
}

sa1_status sa1a_render_sequence(sa1_audio *a, sa1a_bank *b, uint32_t seq, int16_t **pcm, uint32_t *count)
{
    seq_parse p;
    seq_ch ch[16];
    seq_voice *vo = NULL;
    float *buf = NULL;
    uint32_t off, tempo, i, k, total, cap, e;
    double cur = 0.0, end_time = 0.0;
    sa1_status st = SA1_OK;
    *pcm = NULL;
    *count = 0;
    if (!b->msb || seq >= b->num_sequences)
        return SA1_OK;
    off = sa1_rd32(b->msb + 16u + seq * 4u);
    if (off > b->msb_size || b->msb_size - off < 12u || memcmp(b->msb + off, "SMSD", 4) != 0)
        return SA1_OK;
    tempo = sa1_rd32(b->msb + off + 8u);
    if (tempo < 50u || tempo > 10000u)
        tempo = 500u;
    memset(&p, 0, sizeof p);
    p.m = b->msb;
    p.size = b->msb_size;
    p.start = off;
    p.spt = (double)SA1A_SEQ_RATE * ((double)tempo / 1000.0) / SEQ_TICKS;
    p.ev = (seq_ev *)sa1_mem_array(a->ctx, SEQ_MAX_EVENTS, sizeof(seq_ev));
    if (!p.ev)
        return SA1_ERR_OUT_OF_MEMORY;
    (void)seq_run(&p, off + 12u, 0, 0);
    if (p.nev == 0) {
        sa1_mem_free(a->ctx, p.ev);
        return SA1_OK;
    }
    for (e = 0; e < p.nev; e++) {
        double t = p.ev[e].time + (p.ev[e].kind == 1 ? p.ev[e].gate : 0.0);
        if (t > end_time)
            end_time = t;
    }
    cap = SA1A_SEQ_MAX_SECONDS * SA1A_SEQ_RATE;
    total = end_time + 2.0 * SA1A_SEQ_RATE < (double)cap ? (uint32_t)(end_time + 2.0 * SA1A_SEQ_RATE) : cap;
    buf = (float *)sa1_mem_array(a->ctx, total ? total : 1u, sizeof(float));
    vo = (seq_voice *)sa1_mem_array(a->ctx, SEQ_MAX_VOICES * SA1A_MAX_LAYERS, sizeof(seq_voice));
    if (!buf || !vo) {
        st = SA1_ERR_OUT_OF_MEMORY;
        goto done;
    }
    for (i = 0; i < 16; i++) {
        ch[i].bank = b->game_bank;
        ch[i].program = 0;
        ch[i].vol = 1.0f;
        ch[i].bend_semis = 0.0f;
        ch[i].sustain = 0;
    }
    e = 0;
    while (cur < (double)total) {
        /* the next thing to happen: an event or a key off */
        double next = (double)total;
        uint32_t from = (uint32_t)cur, to;
        if (e < p.nev && p.ev[e].time < next)
            next = p.ev[e].time;
        for (k = 0; k < SEQ_MAX_VOICES * SA1A_MAX_LAYERS; k++)
            if (vo[k].used && !vo[k].keyed_off && vo[k].off_time < next)
                next = vo[k].off_time;
        to = next < (double)total ? (uint32_t)next : total;
        if (to > from) {
            for (k = 0; k < SEQ_MAX_VOICES * SA1A_MAX_LAYERS; k++) {
                seq_voice *sv = &vo[k];
                if (!sv->used)
                    continue;
                sa1a_voice_render(&sv->v, buf + from, NULL, to - from, ch[sv->ch].vol, ch[sv->ch].vol);
                if (sv->v.stage == SA1A_EG_DONE)
                    sv->used = 0;
            }
        }
        cur = next > cur ? next : cur;
        /* key offs due now */
        for (k = 0; k < SEQ_MAX_VOICES * SA1A_MAX_LAYERS; k++) {
            seq_voice *sv = &vo[k];
            if (sv->used && !sv->keyed_off && sv->off_time <= cur) {
                sv->keyed_off = 1;
                if (ch[sv->ch].sustain)
                    sv->held = 1;
                else
                    sa1a_voice_release(&sv->v);
            }
        }
        /* events due now */
        while (e < p.nev && p.ev[e].time <= cur) {
            const seq_ev *ev = &p.ev[e++];
            seq_ch *c = &ch[ev->ch];
            if (ev->kind == 1 && ev->b > 0) {
                sa1a_layer layers[SA1A_MAX_LAYERS];
                sa1a_bank *pb = seq_bank(a, b, c->bank);
                uint32_t nl = pb ? sa1a_program_layers(a, pb, c->program, ev->a, layers) : 0u, li;
                float vg = pb ? (float)pb->vel_curve[ev->b & 127u] / 127.0f : 0.0f;
                for (li = 0; li < nl; li++) {
                    for (k = 0; k < SEQ_MAX_VOICES * SA1A_MAX_LAYERS && vo[k].used; k++)
                        ;
                    if (k == SEQ_MAX_VOICES * SA1A_MAX_LAYERS)
                        break;
                    memset(&vo[k], 0, sizeof vo[k]);
                    vo[k].used = 1;
                    vo[k].ch = ev->ch;
                    vo[k].off_time = cur + (ev->gate > 1.0 ? ev->gate : 1.0);
                    vo[k].bend_up = layers[li].bend_up > 0.0f ? layers[li].bend_up : 2.0f;
                    vo[k].bend_down = layers[li].bend_down > 0.0f ? layers[li].bend_down : 2.0f;
                    sa1a_voice_start(&vo[k].v, &layers[li], 0.0f, (float)SA1A_SEQ_RATE, vg * vg);
                    vo[k].v.bend = pow(2.0, (double)c->bend_semis / 12.0);
                }
            } else if (ev->kind == 2) {
                if (ev->a == 32)
                    c->bank = ev->b;
                else if (ev->a == 7)
                    c->vol = ((float)ev->b / 127.0f) * ((float)ev->b / 127.0f);
                else if (ev->a == 64) {
                    c->sustain = ev->b >= 64;
                    if (!c->sustain) {
                        for (k = 0; k < SEQ_MAX_VOICES * SA1A_MAX_LAYERS; k++)
                            if (vo[k].used && vo[k].held && vo[k].ch == ev->ch) {
                                vo[k].held = 0;
                                sa1a_voice_release(&vo[k].v);
                            }
                    }
                } else if (ev->a == 121) {
                    c->vol = 1.0f;
                    c->bend_semis = 0.0f;
                    c->sustain = 0;
                }
            } else if (ev->kind == 3) {
                c->program = ev->a;
            } else if (ev->kind == 4) {
                float x = ((float)ev->a - 64.0f) / 64.0f;
                for (k = 0; k < SEQ_MAX_VOICES * SA1A_MAX_LAYERS; k++) {
                    if (vo[k].used && vo[k].ch == ev->ch) {
                        float semis = x >= 0.0f ? x * vo[k].bend_up : x * vo[k].bend_down;
                        vo[k].v.bend = pow(2.0, (double)semis / 12.0);
                    }
                }
                c->bend_semis = x * 2.0f;
            }
        }
        if (e >= p.nev) {
            int any = 0;
            for (k = 0; k < SEQ_MAX_VOICES * SA1A_MAX_LAYERS; k++)
                any |= vo[k].used;
            if (!any)
                break;
            /* sustained notes at the end are released when the sequence ends */
            if (cur >= end_time)
                for (k = 0; k < SEQ_MAX_VOICES * SA1A_MAX_LAYERS; k++)
                    if (vo[k].used && vo[k].held) {
                        vo[k].held = 0;
                        sa1a_voice_release(&vo[k].v);
                    }
        }
        if (next >= (double)total)
            break;
    }
    {
        uint32_t len = (uint32_t)cur < total ? (uint32_t)cur : total, last = 0;
        int16_t *out;
        for (i = 0; i < len; i++)
            if (buf[i] > 1e-4f || buf[i] < -1e-4f)
                last = i + 1u;
        if (last == 0)
            goto done;
        out = (int16_t *)sa1_mem_array(a->ctx, last, sizeof(int16_t));
        if (!out) {
            st = SA1_ERR_OUT_OF_MEMORY;
            goto done;
        }
        for (i = 0; i < last; i++) {
            float x = buf[i] * 32767.0f;
            out[i] = (int16_t)(x > 32767.0f ? 32767 : (x < -32768.0f ? -32768 : (int32_t)lrintf(x)));
        }
        *pcm = out;
        *count = last;
    }
done:
    sa1_mem_free(a->ctx, vo);
    sa1_mem_free(a->ctx, buf);
    sa1_mem_free(a->ctx, p.ev);
    return st;
}

/* ------------------------------------------------------------------------- */
/* Mixer                                                                     */
/* ------------------------------------------------------------------------- */

#define MIX_BLOCK       256u
#define MIX_CHARS       64u
#define MIX_CLIPS       16u
#define MIX_PENDING     32u
#define PENDING_MAX_AGE 0.5f            /* seconds a cue waits for its tone to be decoded */
#define DUCK_RAMP       0.5f            /* seconds of a full duck swing (sa1_audio_mixer_duck) */
#define CLIP_VOICE      0
#define CLIP_BGM        1
#define CLIP_JINGLE     2
#define DUCK_BGM        0               /* mixer duck[]: background music, jingles */
#define DUCK_JINGLE     1

typedef struct mix_inst {
    int32_t          used;
    uint32_t         serial;
    sa1_character_id character;
    uint32_t         tone;
    uint32_t         flags;         /* SA1_AUDIO_CUE_* flags */
    int32_t          keep;
    int32_t          released;
    int32_t          loops;
    float            pos[3];
    float            min_d, max_d;
    float            volume;
    float            delay;
    float            life;          /* seconds left, < 0: unlimited */
    uint32_t         nv;
    sa1a_voice       v[SA1A_MAX_LAYERS];
} mix_inst;

typedef struct mix_clip {
    int32_t          used;
    int32_t          kind;          /* CLIP_* */
    sa1_character_id character;
    uint32_t         index;
    int32_t          cache;         /* the file in the audio object's clip cache: slot + 1 (a reference) */
    int32_t          waiting;       /* the loader has not read the file yet: silent until it has */
    sa1a_adx_dec     dec;
    int32_t          loop;
    int32_t          ended;         /* the decoder has no more frames */
    int32_t          primed;        /* cur and nxt hold the first two frames */
    int32_t          tail;          /* nxt is the silence after the last frame */
    float            delay;
    float            fade;          /* > 0: fading out, gain per output sample */
    float            gain;
    int32_t          reported;      /* JINGLE: the jingle sa1_audio_mixer_music_state reports (started, not fading out) */
    uint64_t         played;        /* output samples rendered after the delay (header version 4: the time heard) */
    double           frac, step;
    int16_t          cur[2], nxt[2];
    int16_t          buf[2 * 256];
    uint32_t         buf_n, buf_pos;
} mix_clip;

typedef struct mix_rule {
    int32_t          used;
    int32_t          stage;
    uint32_t         first, last;
} mix_rule;

typedef struct mix_char {
    sa1_character_id id;
    float            pos[3];
    int32_t          local;
    mix_rule         rules[SA1_AUDIO_MAX_STAGE_RULES];  /* stage rules (header version 5) */
} mix_char;

/* a PLAY / KEEP cue whose tone the loader thread is still decoding (sa1a_sound_nowait) */
typedef struct mix_pend {
    int32_t          used;
    sa1_audio_cue    cue;
    float            age;           /* seconds since the cue */
} mix_pend;

struct sa1_audio_mixer {
    sa1_audio               *audio;
    struct sa1_audio_mixer  *next;
    uint32_t                 rate;
    uint32_t                 flags;
    uint32_t                 max_voices;
    uint32_t                 serial;
    float                    vol[4];
    float                    lpos[3], right[3];
    int32_t                  have_listener;
    float                    scale;         /* host units per SA1 unit */
    mix_inst                *inst;
    uint32_t                 ninst;
    mix_clip                 clips[MIX_CLIPS];
    mix_char                 chars[MIX_CHARS];
    uint32_t                 bgm_index;
    int32_t                  bgm_set;
    int32_t                  ring_right;    /* D_8C1C19AC: the next ring sound goes right */
    float                   *mix_l, *mix_r;
    mix_pend                 pend[MIX_PENDING];
    int32_t                  in_retry;      /* retry_pending is re-issuing cues */
    float                    duck[2];       /* the host's music duck (DUCK_*): current gain, ramped to duck_to */
    float                    duck_to[2];
    /* the game's music state (header version 4: sa1_audio_mixer_music_state) */
    int32_t                  stopped;       /* MUSIC_STOP (8C107ADA) came; cleared by MUSIC, JINGLE, MUSIC_RESTORE */
    uint32_t                 jingles_started;
    uint32_t                 jingles_ended;
    int32_t                  last_jingle;   /* the last jingle that ended, -1 none */
    int32_t                  last_end;      /* SA1_AUDIO_JINGLE_END_* */
};

/* the reported jingle of a clip ends (header version 4): counted, with how (a no-op for any other clip) */
static void jingle_over(sa1_audio_mixer *m, mix_clip *c, int32_t how)
{
    if (!c->used || c->kind != CLIP_JINGLE || !c->reported)
        return;
    c->reported = 0;
    m->jingles_ended++;
    m->last_jingle = (int32_t)c->index;
    m->last_end = how;
}

/* frees a clip; a jingle still reported here has played to its end (the other ways out say how before) */
static void clip_free(sa1_audio_mixer *m, mix_clip *c)
{
    jingle_over(m, c, SA1_AUDIO_JINGLE_END_PLAYED);
    if (c->cache > 0)
        sa1a_clip_put(m->audio, c->cache - 1);
    memset(c, 0, sizeof *c);
}

static const mix_char *find_char(const sa1_audio_mixer *m, sa1_character_id id)
{
    uint32_t i;
    for (i = 0; i < MIX_CHARS; i++)
        if (m->chars[i].id == id && id != 0)
            return &m->chars[i];
    return NULL;
}

/* stage rules (header version 5): the tone key of a PLAY / KEEP cue. 0: play *key; 1: the rule's banks are being read
 * (the cue waits like a tone being decoded); 2: silent (the rule's stage could not be read or held: the first cue that
 * meets a failed stage logs it here, on the mixer's thread; the loader thread only recorded it) */
static int cue_key(sa1_audio_mixer *m, const sa1_audio_cue *cue, uint32_t *key)
{
    const mix_char *c;
    uint32_t i, set = 0;
    *key = cue->tone <= SA1A_MAX_TONE ? cue->tone : SA1A_KEYS;
    if (cue->tone > SA1A_LAST_SE || (c = find_char(m, cue->character)) == NULL)
        return 0;
    for (i = 0; i < SA1_AUDIO_MAX_STAGE_RULES; i++) {
        const mix_rule *r = &c->rules[i];
        int32_t gb, st;
        if (!r->used || cue->tone < r->first || cue->tone > r->last)
            continue;
        gb = sa1a_tone_game_bank(m->audio, cue->tone);
        if (gb != 0 && gb != 1 && gb != 2 && gb != 4 && gb != 5)
            return 0;                   /* not a stage bank (3, 6, 7, 8): the rule does not apply */
        st = sa1a_set_lookup(m->audio, r->stage, (uint32_t)gb, &set);
        if (st == SA1_AUDIO_STAGE_READY) {
            *key = SA1A_KEY(set, cue->tone);
            return 0;
        }
        if (st == SA1_AUDIO_STAGE_FAILED && set != 0 && sa1a_set_report_failed(m->audio, set)) {
            const char *mlt = sa1_audio_stage_file(r->stage, m->audio->flags);
            sa1_log(m->audio->ctx, SA1_LOG_WARN, "audio: stage rule: SONICADV/%s (stage %d) bank %d could not be read: "
                    "tones %u-%u of character %d stay silent", mlt ? mlt : "?", (int)r->stage, (int)gb,
                    (unsigned)r->first, (unsigned)r->last, (int)cue->character);
        }
        return st == SA1_AUDIO_STAGE_LOADING ? 1 : 2;
    }
    return 0;
}

sa1_status sa1_audio_mixer_create(sa1_audio *a, const sa1_audio_mixer_desc *desc, sa1_audio_mixer **out)
{
    sa1_audio_mixer *m;
    sa1_coords co;
    uint32_t rate = 44100, maxv = 64;
    if (out)
        *out = NULL;
    if (!a || !out || (desc && desc->struct_size < sizeof(sa1_audio_mixer_desc)))
        return SA1_ERR_INVALID_ARGUMENT;
    if (desc && desc->sample_rate)
        rate = desc->sample_rate;
    if (desc && desc->max_voices)
        maxv = desc->max_voices;
    if (rate < 8000u || rate > 192000u || maxv > 256u)
        return SA1_ERR_INVALID_ARGUMENT;
    m = (sa1_audio_mixer *)sa1_mem_alloc(a->ctx, sizeof *m);
    if (!m)
        return SA1_ERR_OUT_OF_MEMORY;
    m->audio = a;
    m->rate = rate;
    m->flags = desc ? desc->flags : 0u;
    m->max_voices = maxv;
    m->ninst = maxv;
    m->vol[0] = m->vol[1] = m->vol[2] = m->vol[3] = 1.0f;
    m->duck[DUCK_BGM] = m->duck[DUCK_JINGLE] = m->duck_to[DUCK_BGM] = m->duck_to[DUCK_JINGLE] = 1.0f;
    m->last_jingle = -1;
    m->right[1] = -1.0f;                     /* until a listener is set: Source-style +X forward, +Z up */
    m->scale = 1.0f;
    co.struct_size = sizeof co;
    if (sa1_get_coords(a->ctx, &co) == SA1_OK && co.scale > 0.0f)
        m->scale = co.scale;
    m->inst = (mix_inst *)sa1_mem_array(a->ctx, m->ninst, sizeof(mix_inst));
    m->mix_l = (float *)sa1_mem_array(a->ctx, MIX_BLOCK, sizeof(float));
    m->mix_r = (float *)sa1_mem_array(a->ctx, MIX_BLOCK, sizeof(float));
    if (!m->inst || !m->mix_l || !m->mix_r) {
        sa1_mem_free(a->ctx, m->inst);
        sa1_mem_free(a->ctx, m->mix_l);
        sa1_mem_free(a->ctx, m->mix_r);
        sa1_mem_free(a->ctx, m);
        return SA1_ERR_OUT_OF_MEMORY;
    }
    m->next = a->mixers;
    a->mixers = m;
    *out = m;
    return SA1_OK;
}

void sa1_audio_mixer_destroy(sa1_audio_mixer *m)
{
    sa1_audio_mixer **pp;
    uint32_t i;
    sa1_context *ctx;
    if (!m)
        return;
    ctx = m->audio->ctx;
    for (pp = &m->audio->mixers; *pp; pp = &(*pp)->next) {
        if (*pp == m) {
            *pp = m->next;
            break;
        }
    }
    for (i = 0; i < MIX_CLIPS; i++)
        clip_free(m, &m->clips[i]);
    sa1_mem_free(ctx, m->inst);
    sa1_mem_free(ctx, m->mix_l);
    sa1_mem_free(ctx, m->mix_r);
    sa1_mem_free(ctx, m);
}

void sa1a_mixers_free_all(sa1_audio *a)
{
    while (a->mixers)
        sa1_audio_mixer_destroy(a->mixers);
}

void sa1a_mixers_drop_stage(sa1_audio *a)
{
    sa1_audio_mixer *m;
    for (m = a->mixers; m; m = m->next) {
        memset(m->inst, 0, (size_t)m->ninst * sizeof(mix_inst));
        memset(m->pend, 0, sizeof m->pend);
    }
}

static uint32_t active_voices(const sa1_audio_mixer *m)
{
    uint32_t i, n = 0;
    for (i = 0; i < m->ninst; i++)
        if (m->inst[i].used)
            n += m->inst[i].nv;
    return n;
}

static mix_inst *alloc_inst(sa1_audio_mixer *m, uint32_t need)
{
    uint32_t i, best = UINT32_MAX;
    for (;;) {
        if (active_voices(m) + need <= m->max_voices)
            for (i = 0; i < m->ninst; i++)
                if (!m->inst[i].used)
                    return &m->inst[i];
        /* steal: the oldest released one, else the oldest that is not kept alive */
        best = UINT32_MAX;
        for (i = 0; i < m->ninst; i++)
            if (m->inst[i].used && m->inst[i].released &&
                (best == UINT32_MAX || m->inst[i].serial < m->inst[best].serial))
                best = i;
        if (best == UINT32_MAX)
            for (i = 0; i < m->ninst; i++)
                if (m->inst[i].used && !m->inst[i].keep &&
                    (best == UINT32_MAX || m->inst[i].serial < m->inst[best].serial))
                    best = i;
        if (best == UINT32_MAX)
            return NULL;
        memset(&m->inst[best], 0, sizeof m->inst[best]);
    }
}

static float dist3(const float a[3], const float b[3])
{
    float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

/* the game's distance attenuation (8C0947BA + 8C093BE0): volofs = -127 * (d - near) / (far - near), CC7 =
 * 106 + volofs, level (CC7 / 106)^2; plus a soft pan from the listener's right */
static void spatial(const sa1_audio_mixer *m, const float pos[3], float min_d, float max_d, float *gl, float *gr)
{
    float d, g = 1.0f, p = 0.0f;
    if (!m->have_listener) {
        *gl = *gr = 1.0f;
        return;
    }
    d = dist3(pos, m->lpos);
    if (d > min_d) {
        float f = max_d > min_d ? (d - min_d) / (max_d - min_d) : 1.0f, cc;
        if (f > 1.0f)
            f = 1.0f;
        cc = 106.0f - 127.0f * f;
        g = cc > 0.0f ? (cc / 106.0f) * (cc / 106.0f) : 0.0f;
    }
    if (d > 1e-3f) {
        p = ((pos[0] - m->lpos[0]) * m->right[0] + (pos[1] - m->lpos[1]) * m->right[1] +
             (pos[2] - m->lpos[2]) * m->right[2]) / d;
        p *= 0.6f;
    }
    *gl = g * (p > 0.0f ? 1.0f - p : 1.0f);
    *gr = g * (p < 0.0f ? 1.0f + p : 1.0f);
}

/* SA1_AUDIO_MIX_RESTART_REPEATS (header version 5, a host option): a one-shot PLAY of a tone that the character still
 * plays from such a cue keys the older instances off at once - each layer released at its own rate or within 4 ms
 * (96 dB at REPEAT_CUT_DB_PER_S), whichever is faster - before the new one starts. The game lets them pile up: every
 * 8C0940DA call takes the next free / one-shot slot of the sound queue (8C093F02, 32 slots in turn) and every slot is
 * its own MIDI channel (8C093140, note on 8C093360), so nothing of the game restarts a tone (super2_plan.md 10.3). */
#define REPEAT_CUT_DB_PER_S 24000.0f

static int restarts_repeats(const sa1_audio_mixer *m, const sa1_audio_cue *cue)
{
    return (m->flags & SA1_AUDIO_MIX_RESTART_REPEATS) && cue->type == SA1_AUDIO_CUE_PLAY &&
           !(cue->flags & (SA1_AUDIO_CUE_POSITIONAL | SA1_AUDIO_CUE_OWNED));
}

static void restart_repeats(sa1_audio_mixer *m, const sa1_audio_cue *cue)
{
    const float cut = REPEAT_CUT_DB_PER_S / (float)m->rate;
    uint32_t i, k;
    for (i = 0; i < m->ninst; i++) {
        mix_inst *in = &m->inst[i];
        if (!in->used || in->released || in->keep || in->character != cue->character || in->tone != cue->tone ||
            (in->flags & (SA1_AUDIO_CUE_POSITIONAL | SA1_AUDIO_CUE_OWNED)))
            continue;
        in->released = 1;
        for (k = 0; k < in->nv; k++) {
            if (in->v[k].rel < cut)
                in->v[k].rel = cut;
            sa1a_voice_release(&in->v[k]);
        }
    }
}

static void release_where(sa1_audio_mixer *m, sa1_character_id ch, uint32_t tone, int owned_only)
{
    uint32_t i, k;
    for (i = 0; i < m->ninst; i++) {
        mix_inst *in = &m->inst[i];
        if (!in->used || in->released || (ch && in->character != ch))
            continue;
        if (owned_only ? !(in->flags & SA1_AUDIO_CUE_OWNED) : (tone && in->tone != tone))
            continue;
        in->released = 1;
        for (k = 0; k < in->nv; k++)
            sa1a_voice_release(&in->v[k]);
    }
}

/* A waiting clip whose file the loader has read starts its decoder: 1 playing, 0 still waiting, -1 failed (freed; the
 * first failure of a file is logged here, on the mixer's thread). */
static int clip_ready(sa1_audio_mixer *m, mix_clip *c)
{
    const uint8_t *data = NULL;
    const sa1a_adx *h = NULL;
    uint32_t size = 0;
    sa1_status st = SA1_OK;
    int first = 0, r;
    if (!c->waiting)
        return 1;
    r = sa1a_clip_poll(m->audio, c->cache - 1, &data, &size, &h, &st, &first);
    if (r == SA1A_CLIP_WAIT)
        return 0;
    if (r != SA1A_CLIP_READY) {
        if (first)
            sa1_log(m->audio->ctx, SA1_LOG_WARN, "audio: cannot load %s %u (%s)",
                    c->kind == CLIP_VOICE ? "voice" : "music", (unsigned)c->index, sa1_status_string(st));
        jingle_over(m, c, SA1_AUDIO_JINGLE_END_FAILED);
        clip_free(m, c);
        return -1;
    }
    sa1a_adx_dec_init(&c->dec, data, size, h);
    c->step = (double)h->rate / (double)m->rate;
    c->waiting = 0;
    return 1;
}

/* Starts a clip from the audio object's cache: never reads the disc here (the loader thread does; the clip is silent
 * until its file is there, a cold voice a few milliseconds). */
static sa1_status start_clip(sa1_audio_mixer *m, int32_t kind, sa1_character_id ch, int32_t source, uint32_t index,
                             float delay, int32_t loop)
{
    mix_clip *c = NULL;
    uint32_t i;
    int32_t slot;
    sa1_status st = sa1a_adx_exists(m->audio, source, index);   /* a file that is not there: at once, nothing queued */
    if (st != SA1_OK)
        return st;
    for (i = 0; i < MIX_CLIPS; i++)
        if (!m->clips[i].used) {
            c = &m->clips[i];
            break;
        }
    if (!c)
        return SA1_ERR_LIMIT;
    slot = sa1a_clip_get(m->audio, source, index, &st);
    if (slot < 0)
        return st;
    memset(c, 0, sizeof *c);
    c->used = 1;
    c->kind = kind;
    c->character = ch;
    c->index = index;
    c->cache = slot + 1;
    c->waiting = 1;
    c->loop = loop;
    c->delay = delay;
    c->gain = 1.0f;
    if (clip_ready(m, c) < 0)
        return SA1_ERR_IO;
    if (kind == CLIP_JINGLE) {                  /* the jingle sa1_audio_mixer_music_state reports from now on */
        c->reported = 1;
        m->jingles_started++;
    }
    return SA1_OK;
}

/* a cue whose tone is being decoded waits (a KEEP cue refreshes its waiting copy) */
static void defer_cue(sa1_audio_mixer *m, const sa1_audio_cue *cue)
{
    uint32_t i, free_i = MIX_PENDING, old = 0;
    for (i = 0; i < MIX_PENDING; i++) {
        mix_pend *p = &m->pend[i];
        if (!p->used) {
            if (free_i == MIX_PENDING)
                free_i = i;
            continue;
        }
        if (cue->type == SA1_AUDIO_CUE_KEEP && p->cue.type == SA1_AUDIO_CUE_KEEP && p->cue.character == cue->character &&
            p->cue.tone == cue->tone) {
            p->cue.lifetime = cue->lifetime;
            memcpy(p->cue.position, cue->position, sizeof p->cue.position);
            return;
        }
        if (m->pend[i].age > m->pend[old].age)
            old = i;
    }
    if (free_i == MIX_PENDING) {
        free_i = old;   /* full: the oldest gives way */
        sa1a_count_dropped(m->audio, 1);
    }
    m->pend[free_i].used = 1;
    m->pend[free_i].cue = *cue;
    m->pend[free_i].age = 0.0f;
}

/* the waiting cues whose tones are decoded now start (their delay shortened by the wait); after PENDING_MAX_AGE they
 * are given up */
static void retry_pending(sa1_audio_mixer *m, float dt)
{
    uint32_t i, dropped = 0;
    if (m->in_retry)
        return;
    m->in_retry = 1;
    for (i = 0; i < MIX_PENDING; i++) {
        mix_pend *p = &m->pend[i];
        const sa1a_sound *s;
        sa1_status st;
        int pending = 0;
        if (!p->used)
            continue;
        {
            uint32_t key;
            int how = cue_key(m, &p->cue, &key);
            s = NULL;
            if (how == 0)
                s = sa1a_sound_nowait(m->audio, key, &st, &pending);
            else
                pending = how == 1;     /* a rule's banks still being read: keep waiting; failed: give up */
        }
        if (s) {
            sa1_audio_cue c = p->cue;
            p->used = 0;
            c.delay = c.delay > p->age ? c.delay - p->age : 0.0f;
            (void)sa1_audio_mixer_cue(m, &c);
            continue;
        }
        p->age += dt;
        if (!pending || p->age > PENDING_MAX_AGE) {
            p->used = 0;
            dropped++;
        }
    }
    m->in_retry = 0;
    sa1a_count_dropped(m->audio, dropped);
}

/* fades out (50 ms) the clips of a kind (voices: of one character); a jingle ends as `how` (SA1_AUDIO_JINGLE_END_*) */
static void fade_clips(sa1_audio_mixer *m, int32_t kind, sa1_character_id ch, int32_t how)
{
    uint32_t i;
    for (i = 0; i < MIX_CLIPS; i++) {
        mix_clip *c = &m->clips[i];
        if (c->used && c->kind == kind && (kind != CLIP_VOICE || c->character == ch) && c->fade <= 0.0f) {
            jingle_over(m, c, how);
            if (c->waiting)
                clip_free(m, c);    /* never heard: nothing to fade */
            else
                c->fade = 1.0f / (0.05f * (float)m->rate);
        }
    }
}

static int jingle_playing(const sa1_audio_mixer *m)
{
    uint32_t i;
    for (i = 0; i < MIX_CLIPS; i++)
        if (m->clips[i].used && m->clips[i].kind == CLIP_JINGLE && m->clips[i].fade <= 0.0f)
            return 1;
    return 0;
}

sa1_status sa1_audio_mixer_cue(sa1_audio_mixer *m, const sa1_audio_cue *cue)
{
    sa1_status st = SA1_OK;
    if (!m || !cue || cue->struct_size < sizeof(sa1_audio_cue))
        return SA1_ERR_INVALID_ARGUMENT;
    switch (cue->type) {
    case SA1_AUDIO_CUE_PLAY:
    case SA1_AUDIO_CUE_KEEP: {
        const sa1a_sound *s;
        mix_inst *in;
        uint32_t i;
        if (cue->type == SA1_AUDIO_CUE_KEEP) {
            for (i = 0; i < m->ninst; i++) {
                in = &m->inst[i];
                if (in->used && in->keep && !in->released && in->character == cue->character && in->tone == cue->tone) {
                    in->life = cue->lifetime;
                    memcpy(in->pos, cue->position, sizeof in->pos);
                    return SA1_OK;
                }
            }
        }
        {
            int pending = 0;
            uint32_t key;
            int how = cue_key(m, cue, &key);
            if (how == 2)
                return SA1_OK;          /* a stage rule whose stage could not be read: silent, never the own stage's sound */
            if (how == 1) {
                if (!m->in_retry)
                    defer_cue(m, cue);
                return SA1_OK;
            }
            s = sa1a_sound_nowait(m->audio, key, &st, &pending);
            if (!s) {
                if (pending && !m->in_retry)
                    defer_cue(m, cue);
                return pending ? SA1_OK : st;
            }
        }
        if (s->kind == SA1_AUDIO_SOUND_VOICE) {
            sa1_audio_cue v = *cue;
            v.type = SA1_AUDIO_CUE_VOICE;
            v.index = s->voice;
            return sa1_audio_mixer_cue(m, &v);
        }
        if (s->num_layers == 0)
            return SA1_OK;
        if ((cue->flags & SA1_AUDIO_CUE_POSITIONAL) && cue->start_radius > 0.0f && m->have_listener &&
            dist3(cue->position, m->lpos) > cue->start_radius)
            return SA1_OK;                      /* 8C09413A: out of the tone's 3D radius */
        if (restarts_repeats(m, cue))
            restart_repeats(m, cue);            /* the host's option: the same one-shot restarts instead of piling up */
        in = alloc_inst(m, s->num_layers);
        if (!in)
            return SA1_OK;
        memset(in, 0, sizeof *in);
        in->used = 1;
        in->serial = ++m->serial;
        in->character = cue->character;
        in->tone = cue->tone;
        in->flags = cue->flags;
        in->keep = cue->type == SA1_AUDIO_CUE_KEEP;
        in->loops = s->loops;
        memcpy(in->pos, cue->position, sizeof in->pos);
        in->min_d = cue->min_distance > 0.0f ? cue->min_distance : SA1A_NEAR * m->scale;
        in->max_d = cue->max_distance > in->min_d ? cue->max_distance : SA1A_FAR * m->scale;
        in->volume = cue->volume > 0.0f ? cue->volume : 0.0f;
        in->delay = cue->delay;
        /* the game's 120-frame timer of one-shot calls only matters for looping samples here: a non-looping
           sample plays to its end */
        in->life = (cue->type == SA1_AUDIO_CUE_PLAY && !s->loops) ? -1.0f : cue->lifetime;
        in->nv = s->num_layers;
        for (i = 0; i < s->num_layers; i++)
            sa1a_voice_start(&in->v[i], &s->layers[i], 0.0f, (float)m->rate, 1.0f);
        if (s->kind == SA1_AUDIO_SOUND_PROGRAM && s->bank == 0 && s->request == 7) {
            /* 8C093140: bank 0 request 7 (the ring) alternates MIDI pan 34 / 94 */
            float pan = m->ring_right ? (94.0f - 64.0f) / 64.0f : (34.0f - 64.0f) / 64.0f;
            m->ring_right = !m->ring_right;
            for (i = 0; i < in->nv; i++) {
                in->v[i].gain_l *= pan > 0.0f ? 1.0f - pan : 1.0f;
                in->v[i].gain_r *= pan < 0.0f ? 1.0f + pan : 1.0f;
            }
        }
        break;
    }
    case SA1_AUDIO_CUE_STOP:
        release_where(m, cue->character, cue->tone, cue->tone == 0);
        break;
    case SA1_AUDIO_CUE_VOICE:
        if (m->flags & SA1_AUDIO_MIX_NO_VOICES)
            break;
        fade_clips(m, CLIP_VOICE, cue->character, 0);
        st = start_clip(m, CLIP_VOICE, cue->character, SA1_AUDIO_SOURCE_VOICE, cue->index, cue->delay, 0);
        break;
    case SA1_AUDIO_CUE_MUSIC:
        m->bgm_index = cue->index;
        m->bgm_set = 1;
        m->stopped = 0;
        /* 8C107AC6 -> 8C10792A -> ADXT_StartFname stops the current track (8C644468) before it opens the file: a
           music file that is not on the disc (69) leaves silence, and the cue says SA1_ERR_NOT_FOUND at once.  The
           current track can be a jingle: it ends (its task 8C107CC2 frees itself, the saved track has changed), whether
           or not this mixer plays the background music (header version 4) */
        fade_clips(m, CLIP_JINGLE, 0, SA1_AUDIO_JINGLE_END_MUSIC);
        if (!(m->flags & SA1_AUDIO_MIX_MUSIC)) {
            if (sa1a_adx_exists(m->audio, SA1_AUDIO_SOURCE_MUSIC, cue->index) == SA1_ERR_NOT_FOUND)
                st = SA1_ERR_NOT_FOUND;
            break;
        }
        fade_clips(m, CLIP_BGM, 0, 0);
        st = start_clip(m, CLIP_BGM, 0, SA1_AUDIO_SOURCE_MUSIC, cue->index, cue->delay, 1);
        break;
    case SA1_AUDIO_CUE_JINGLE:
        m->stopped = 0;
        if (m->flags & SA1_AUDIO_MIX_NO_JINGLES)
            break;
        fade_clips(m, CLIP_JINGLE, 0, SA1_AUDIO_JINGLE_END_REPLACED);
        /* the game's ADXT loops a file that has a loop in its header for as long as nothing else is asked for (header
           version 4; of the jingles only 45 FISHING.ADX has one: it lasts until the fishing ends it), the others play
           once */
        st = start_clip(m, CLIP_JINGLE, 0, SA1_AUDIO_SOURCE_MUSIC, cue->index, cue->delay, 1);
        break;
    case SA1_AUDIO_CUE_MUSIC_RESTORE:
        /* 8C107B26: back to the saved track (none after a music stop: the music stays off, bgm -1) */
        m->stopped = 0;
        fade_clips(m, CLIP_JINGLE, 0, SA1_AUDIO_JINGLE_END_RESTORE);
        break;
    case SA1_AUDIO_CUE_MUSIC_STOP:
        /* 8C107ADA: ADXT_Stop and the saved track forgotten */
        fade_clips(m, CLIP_JINGLE, 0, SA1_AUDIO_JINGLE_END_STOP);
        fade_clips(m, CLIP_BGM, 0, 0);
        m->bgm_set = 0;
        m->stopped = 1;
        break;
    default:
        break;
    }
    return st;
}

sa1_status sa1_audio_mixer_event(sa1_audio_mixer *m, const sa1_event *ev)
{
    sa1_audio_cue cue;
    sa1_status st;
    if (!m || !ev)
        return SA1_ERR_INVALID_ARGUMENT;
    cue.struct_size = sizeof cue;
    st = sa1_audio_cue_from_event(m->audio, ev, &cue);
    if (st != SA1_OK || cue.type == SA1_AUDIO_CUE_NONE)
        return st;
    return sa1_audio_mixer_cue(m, &cue);
}

sa1_status sa1_audio_mixer_listener(sa1_audio_mixer *m, const float position[3], const float forward[3],
                                    const float up[3])
{
    float r[3], len, sign = 1.0f;
    sa1_coords co;
    if (!m || !position || !forward || !up)
        return SA1_ERR_INVALID_ARGUMENT;
    r[0] = forward[1] * up[2] - forward[2] * up[1];
    r[1] = forward[2] * up[0] - forward[0] * up[2];
    r[2] = forward[0] * up[1] - forward[1] * up[0];
    len = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
    if (!(len > 1e-6f) || !sa1_finite(position[0]) || !sa1_finite(position[1]) || !sa1_finite(position[2]))
        return SA1_ERR_INVALID_ARGUMENT;
    /* forward x up is the right side in a right-handed host frame; a mirrored axis matrix flips it */
    co.struct_size = sizeof co;
    if (sa1_get_coords(m->audio->ctx, &co) == SA1_OK) {
        const float *x = co.axis;
        float det = x[0] * (x[4] * x[8] - x[5] * x[7]) - x[1] * (x[3] * x[8] - x[5] * x[6]) +
                    x[2] * (x[3] * x[7] - x[4] * x[6]);
        if (det < 0.0f)
            sign = -1.0f;
        if (co.scale > 0.0f)
            m->scale = co.scale;
    }
    m->right[0] = sign * r[0] / len;
    m->right[1] = sign * r[1] / len;
    m->right[2] = sign * r[2] / len;
    memcpy(m->lpos, position, sizeof m->lpos);
    m->have_listener = 1;
    return SA1_OK;
}

sa1_status sa1_audio_mixer_character(sa1_audio_mixer *m, sa1_character_id character, const float position[3],
                                     int32_t local)
{
    uint32_t i, slot = UINT32_MAX;
    if (!m || character <= 0 || !position)
        return SA1_ERR_INVALID_ARGUMENT;
    for (i = 0; i < MIX_CHARS; i++) {
        if (m->chars[i].id == character) {
            slot = i;
            break;
        }
        if (slot == UINT32_MAX && m->chars[i].id == 0)
            slot = i;
    }
    if (slot == UINT32_MAX)
        return SA1_ERR_LIMIT;
    if (m->chars[slot].id != character && !(m->audio->flags & (SA1_AUDIO_NO_PRELOAD | SA1_AUDIO_MUSIC_ONLY))) {
        /* a new character: its jingles and sound banks preloaded (queued for the loader thread; nothing to do once the
           preload of sa1_audio_open is done) */
        (void)sa1_audio_preload(m->audio, SA1_AUDIO_PRELOAD_ALL);
    }
    m->chars[slot].id = character;
    memcpy(m->chars[slot].pos, position, sizeof m->chars[slot].pos);
    m->chars[slot].local = local != 0;
    return SA1_OK;
}

sa1_status sa1_audio_mixer_stage_rule(sa1_audio_mixer *m, sa1_character_id character, uint32_t slot, int32_t stage,
                                      uint32_t first, uint32_t last)
{
    uint32_t i, at = UINT32_MAX, banks = 0, t;
    mix_rule *r;
    if (!m || character <= 0 || slot >= SA1_AUDIO_MAX_STAGE_RULES)
        return SA1_ERR_INVALID_ARGUMENT;
    if (stage >= 0 && (!sa1_audio_stage_file(stage, m->audio->flags) || first > last || last > SA1A_LAST_SE))
        return SA1_ERR_INVALID_ARGUMENT;
    if (m->audio->flags & SA1_AUDIO_MUSIC_ONLY)
        return SA1_ERR_UNSUPPORTED;
    for (i = 0; i < MIX_CHARS; i++) {
        if (m->chars[i].id == character) {
            at = i;
            break;
        }
        if (at == UINT32_MAX && m->chars[i].id == 0)
            at = i;
    }
    if (at == UINT32_MAX)
        return SA1_ERR_LIMIT;
    if (m->chars[at].id != character) {
        memset(&m->chars[at], 0, sizeof m->chars[at]);
        m->chars[at].id = character;
        m->chars[at].local = 1;         /* until sa1_audio_mixer_character says where it is */
    }
    r = &m->chars[at].rules[slot];
    if (stage < 0) {
        memset(r, 0, sizeof *r);        /* sounds that play go on: their layers stay valid until close */
        return SA1_OK;
    }
    for (t = first; t <= last; t++) {
        int32_t gb = sa1a_tone_game_bank(m->audio, t);
        if (gb == 0 || gb == 1 || gb == 2 || gb == 4 || gb == 5)
            banks |= 1u << gb;
    }
    if (banks && sa1a_set_use(m->audio, stage, banks, first, last) < 0)
        return SA1_ERR_LIMIT;           /* SA1_AUDIO_MAX_STAGE_SETS other stages held */
    r->used = 1;
    r->stage = stage;
    r->first = first;
    r->last = last;
    return SA1_OK;
}

sa1_status sa1_audio_mixer_volume(sa1_audio_mixer *m, int32_t channel, float volume)
{
    if (!m || channel < 0 || channel > 3 || !(volume >= 0.0f && volume <= 4.0f))
        return SA1_ERR_INVALID_ARGUMENT;
    m->vol[channel] = volume;
    return SA1_OK;
}

sa1_status sa1_audio_mixer_duck(sa1_audio_mixer *m, float gain, uint32_t flags)
{
    if (!m || !(gain >= 0.0f && gain <= 1.0f) || (flags & ~(SA1_AUDIO_DUCK_JINGLES | SA1_AUDIO_DUCK_NOW)))
        return SA1_ERR_INVALID_ARGUMENT;
    m->duck_to[DUCK_BGM] = gain;
    m->duck_to[DUCK_JINGLE] = (flags & SA1_AUDIO_DUCK_JINGLES) ? gain : 1.0f;
    if (flags & SA1_AUDIO_DUCK_NOW) {
        m->duck[DUCK_BGM] = m->duck_to[DUCK_BGM];
        m->duck[DUCK_JINGLE] = m->duck_to[DUCK_JINGLE];
    }
    return SA1_OK;
}

sa1_status sa1_audio_mixer_flags(sa1_audio_mixer *m, uint32_t flags)
{
    uint32_t i;
    if (!m || (flags & ~(SA1_AUDIO_MIX_MUSIC | SA1_AUDIO_MIX_NO_JINGLES | SA1_AUDIO_MIX_NO_VOICES |
                         SA1_AUDIO_MIX_3D_VOICES | SA1_AUDIO_MIX_RESTART_REPEATS)))
        return SA1_ERR_INVALID_ARGUMENT;
    m->flags = flags;
    for (i = 0; i < MIX_CLIPS; i++) {
        mix_clip *c = &m->clips[i];
        if (!c->used)
            continue;
        if ((c->kind == CLIP_VOICE && (flags & SA1_AUDIO_MIX_NO_VOICES)) ||
            (c->kind == CLIP_JINGLE && (flags & SA1_AUDIO_MIX_NO_JINGLES)) ||
            (c->kind == CLIP_BGM && !(flags & SA1_AUDIO_MIX_MUSIC))) {
            jingle_over(m, c, SA1_AUDIO_JINGLE_END_HOST);
            clip_free(m, c);
        }
    }
    if ((flags & SA1_AUDIO_MIX_MUSIC) && m->bgm_set) {
        int playing = 0;
        for (i = 0; i < MIX_CLIPS; i++)
            playing |= m->clips[i].used && m->clips[i].kind == CLIP_BGM;
        if (!playing)
            (void)start_clip(m, CLIP_BGM, 0, SA1_AUDIO_SOURCE_MUSIC, m->bgm_index, 0.0f, 1);
    }
    return SA1_OK;
}

sa1_status sa1_audio_mixer_stop(sa1_audio_mixer *m, sa1_character_id character)
{
    uint32_t i;
    if (!m)
        return SA1_ERR_INVALID_ARGUMENT;
    for (i = 0; i < m->ninst; i++)
        if (m->inst[i].used && (character == 0 || m->inst[i].character == character))
            memset(&m->inst[i], 0, sizeof m->inst[i]);
    for (i = 0; i < MIX_CLIPS; i++) {
        mix_clip *c = &m->clips[i];
        if (c->used && (character == 0 || (c->kind == CLIP_VOICE && c->character == character))) {
            jingle_over(m, c, SA1_AUDIO_JINGLE_END_HOST);
            clip_free(m, c);
        }
    }
    if (character == 0) {
        m->bgm_set = 0;
        m->stopped = 0;                         /* the game's music state is forgotten too (header version 4) */
    } else {
        for (i = 0; i < MIX_CHARS; i++)
            if (m->chars[i].id == character)
                memset(&m->chars[i], 0, sizeof m->chars[i]);
    }
    return SA1_OK;
}

sa1_status sa1_audio_mixer_music_state(const sa1_audio_mixer *m, sa1_audio_music_state *out)
{
    sa1_audio_music_state s;
    const mix_clip *j = NULL, *b = NULL;
    uint32_t i;
    if (!m || !out || out->struct_size < sizeof(uint32_t))
        return SA1_ERR_INVALID_ARGUMENT;
    memset(&s, 0, sizeof s);
    s.struct_size = sizeof s;
    s.jingle = -1;
    s.stopped = m->stopped;
    s.bgm = m->bgm_set ? (int32_t)m->bgm_index : -1;
    s.jingles_started = m->jingles_started;
    s.jingles_ended = m->jingles_ended;
    s.last_jingle = m->last_jingle;
    s.last_end = m->last_end;
    for (i = 0; i < MIX_CLIPS; i++) {
        const mix_clip *c = &m->clips[i];
        if (!c->used)
            continue;
        if (c->kind == CLIP_JINGLE && c->reported && !j)
            j = c;                              /* at most one: a JINGLE cue ends the one before */
        else if (c->kind == CLIP_BGM && c->fade <= 0.0f && !b)
            b = c;
    }
    if (j) {
        const sa1a_adx *h = NULL;
        float delay = j->delay > 0.0f ? j->delay : 0.0f;
        if (!j->waiting)
            h = &j->dec.h;
        else if (j->index < SA1A_MAX_MUSIC && m->audio->music_files[j->index].have_header)
            h = &m->audio->music_files[j->index].h;  /* sa1_audio_music_info read it (on this thread) */
        s.jingle = (int32_t)j->index;
        s.jingle_kind = sa1a_jingle_kind(j->index);
        s.jingle_waiting = j->waiting || delay > 0.0f;
        s.jingle_time = (float)((double)j->played / (double)m->rate);
        s.jingle_left = -1.0f;
        if (h && h->rate) {
            s.jingle_length = (float)((double)h->frames / (double)h->rate);
            /* the decoder's rule (sa1a_adx_dec_read): a loop only when the header has a non-empty one */
            s.jingle_loops = h->loop && h->loop_end > h->loop_start;
            if (!s.jingle_loops) {
                float left = s.jingle_length - s.jingle_time;
                s.jingle_left = delay + (left > 0.0f ? left : 0.0f);
            }
        }
    }
    s.bgm_playing = b && (m->flags & SA1_AUDIO_MIX_MUSIC) && !b->waiting && b->delay <= 0.0f && !jingle_playing(m);
    sa1_copy_out(out, &s, sizeof s);
    return SA1_OK;
}

uint32_t sa1_audio_mixer_active(const sa1_audio_mixer *m)
{
    uint32_t i, n;
    if (!m)
        return 0;
    n = active_voices(m);
    for (i = 0; i < MIX_CLIPS; i++)
        n += m->clips[i].used ? 1u : 0u;
    return n;
}

/* next source frame of a clip into c->nxt; 0 at the end */
static int clip_fetch(mix_clip *c)
{
    uint32_t nch = c->dec.h.channels;
    if (c->buf_pos >= c->buf_n) {
        c->buf_n = c->ended ? 0u : sa1a_adx_dec_read(&c->dec, c->buf, 256u, c->loop);
        c->buf_pos = 0;
        if (c->buf_n == 0) {
            c->ended = 1;
            return 0;
        }
    }
    c->nxt[0] = c->buf[c->buf_pos * nch];
    c->nxt[1] = nch > 1u ? c->buf[c->buf_pos * nch + 1u] : c->nxt[0];
    c->buf_pos++;
    return 1;
}

/* gl, gr: the clip's gains; duck0 + i * dduck: the host's duck at output sample i of the block */
static void render_clip(sa1_audio_mixer *m, mix_clip *c, uint32_t n, float gl, float gr, float duck0, float dduck)
{
    uint32_t i;
    if (c->waiting) {
        int r = clip_ready(m, c);
        if (r < 0)
            return;
        if (r == 0) {
            /* still being read: its delay runs meanwhile */
            c->delay -= (float)n / (float)m->rate;
            if (c->delay < 0.0f)
                c->delay = 0.0f;
            return;
        }
    }
    for (i = 0; i < n; i++) {
        float s0, s1, f;
        if (c->delay > 0.0f) {
            c->delay -= 1.0f / (float)m->rate;
            continue;
        }
        if (!c->primed) {
            if (!clip_fetch(c)) {
                clip_free(m, c);
                return;
            }
            c->cur[0] = c->nxt[0];
            c->cur[1] = c->nxt[1];
            if (!clip_fetch(c)) {
                c->nxt[0] = c->nxt[1] = 0;
                c->tail = 1;
            }
            c->frac = 0.0;
            c->primed = 1;
        }
        while (c->frac >= 1.0) {
            if (c->tail) {
                clip_free(m, c);
                return;
            }
            c->cur[0] = c->nxt[0];
            c->cur[1] = c->nxt[1];
            c->frac -= 1.0;
            if (!clip_fetch(c)) {
                c->nxt[0] = c->nxt[1] = 0;
                c->tail = 1;
            }
        }
        f = (float)c->frac;
        s0 = ((float)c->cur[0] + ((float)c->nxt[0] - (float)c->cur[0]) * f) * (1.0f / 32768.0f);
        s1 = ((float)c->cur[1] + ((float)c->nxt[1] - (float)c->cur[1]) * f) * (1.0f / 32768.0f);
        {
            float g = c->gain * (duck0 + dduck * (float)i);
            m->mix_l[i] += s0 * gl * g;
            m->mix_r[i] += s1 * gr * g;
        }
        c->played++;
        if (c->fade > 0.0f) {
            c->gain -= c->fade;
            if (c->gain <= 0.0f) {
                clip_free(m, c);
                return;
            }
        }
        c->frac += c->step;
    }
}

sa1_status sa1_audio_mixer_render(sa1_audio_mixer *m, int16_t *out, uint32_t frames)
{
    uint32_t done = 0, i, k;
    if (!m || (!out && frames))
        return SA1_ERR_INVALID_ARGUMENT;
    retry_pending(m, (float)frames / (float)m->rate);
    while (done < frames) {
        uint32_t n = frames - done < MIX_BLOCK ? frames - done : MIX_BLOCK;
        float dt = (float)n / (float)m->rate, master = m->vol[SA1_AUDIO_VOL_MASTER];
        int jingle = jingle_playing(m);
        float duck0[2], dduck[2];
        /* the host's duck moves linearly towards its target, sample by sample (DUCK_RAMP for a full swing) */
        for (k = 0; k < 2; k++) {
            float step = dt / DUCK_RAMP, d = m->duck_to[k] - m->duck[k];
            duck0[k] = m->duck[k];
            m->duck[k] = d > step ? m->duck[k] + step : (d < -step ? m->duck[k] - step : m->duck_to[k]);
            dduck[k] = (m->duck[k] - duck0[k]) / (float)n;
        }
        memset(m->mix_l, 0, n * sizeof(float));
        memset(m->mix_r, 0, n * sizeof(float));
        for (i = 0; i < m->ninst; i++) {
            mix_inst *in = &m->inst[i];
            float gl = 1.0f, gr = 1.0f, g;
            uint32_t alive = 0;
            const mix_char *mc;
            if (!in->used)
                continue;
            if (in->delay > 0.0f) {
                in->delay -= dt;
                continue;
            }
            mc = find_char(m, in->character);
            if (in->flags & SA1_AUDIO_CUE_POSITIONAL)
                spatial(m, in->pos, in->min_d, in->max_d, &gl, &gr);
            else if (mc && !mc->local)
                spatial(m, mc->pos, in->min_d, in->max_d, &gl, &gr);
            g = in->volume * m->vol[SA1_AUDIO_VOL_SFX] * master;
            for (k = 0; k < in->nv; k++) {
                if (in->v[k].stage == SA1A_EG_DONE)
                    continue;
                sa1a_voice_render(&in->v[k], m->mix_l, m->mix_r, n, gl * g, gr * g);
                alive += in->v[k].stage != SA1A_EG_DONE;
            }
            if (!alive) {
                memset(in, 0, sizeof *in);
                continue;
            }
            if (in->life >= 0.0f && !in->released) {
                in->life -= dt;
                if (in->life <= 0.0f) {
                    in->released = 1;
                    for (k = 0; k < in->nv; k++)
                        sa1a_voice_release(&in->v[k]);
                }
            }
        }
        for (i = 0; i < MIX_CLIPS; i++) {
            mix_clip *c = &m->clips[i];
            float gl = 1.0f, gr = 1.0f, g, d0 = 1.0f, dd = 0.0f;
            if (!c->used)
                continue;
            if (c->kind == CLIP_VOICE) {
                const mix_char *mc = find_char(m, c->character);
                if (mc && !mc->local && (m->flags & SA1_AUDIO_MIX_3D_VOICES))
                    spatial(m, mc->pos, SA1A_NEAR * m->scale, SA1A_FAR * m->scale, &gl, &gr);
                g = m->vol[SA1_AUDIO_VOL_VOICE] * master;
            } else {
                g = m->vol[SA1_AUDIO_VOL_MUSIC] * master;
                if (c->kind == CLIP_BGM && jingle)
                    continue;                   /* the jingle replaces the background music */
                k = c->kind == CLIP_BGM ? DUCK_BGM : DUCK_JINGLE;
                d0 = duck0[k];
                dd = dduck[k];
            }
            render_clip(m, c, n, gl * g, gr * g, d0, dd);
        }
        for (i = 0; i < n; i++) {
            float l = m->mix_l[i] * 32767.0f, r = m->mix_r[i] * 32767.0f;
            out[(done + i) * 2u] = (int16_t)(l > 32767.0f ? 32767 : (l < -32768.0f ? -32768 : (int32_t)lrintf(l)));
            out[(done + i) * 2u + 1u] = (int16_t)(r > 32767.0f ? 32767 : (r < -32768.0f ? -32768 : (int32_t)lrintf(r)));
        }
        done += n;
    }
    return SA1_OK;
}
