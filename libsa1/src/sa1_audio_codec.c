/*
 * Audio codecs: Yamaha AICA 4-bit ADPCM (the Dreamcast sound chip's sample format) and CRI ADX
 * (voices and music).  Both decoders take untrusted data and never read outside the given buffers.
 */
#include "sa1_audio.h"

#include <math.h>

/* ------------------------------------------------------------------------- */
/* AICA ADPCM                                                                */
/* ------------------------------------------------------------------------- */
/*
 * The AICA's ADPCM (the same algorithm as the YMZ280B): per nibble d (low nibble of each byte first),
 *   x = step * (2 * (d & 7) + 1) / 8, negated if d & 8; sample = clamp16(sample + x);
 *   step = clamp(step * Q[d & 7] / 256, 0x7F, 0x6000), Q = 0.8984375 x4, 1.19921875, 1.59765625, 2.0, 2.3984375.
 * The hardware restores the state saved at the loop start when it loops (PCMS 2), so decoding once from the
 * start and replaying [LSA, LEA) is exact.
 */
static const int32_t k_adpcm_scale[8] = { 0xE6, 0xE6, 0xE6, 0xE6, 0x133, 0x199, 0x200, 0x266 };

void sa1a_adpcm_decode(const uint8_t *src, uint32_t num_samples, int16_t *out)
{
    int32_t sig = 0, step = 0x7F;
    uint32_t i;
    for (i = 0; i < num_samples; i++) {
        uint32_t d = (i & 1u) ? (uint32_t)(src[i >> 1] >> 4) : (uint32_t)(src[i >> 1] & 15u);
        int32_t x = (step * (int32_t)((d & 7u) * 2u + 1u)) >> 3;
        if (x > 0x7FFF)
            x = 0x7FFF;
        if (d & 8u)
            x = -x;
        sig += x;
        if (sig > 32767)
            sig = 32767;
        else if (sig < -32768)
            sig = -32768;
        step = (step * k_adpcm_scale[d & 7u]) >> 8;
        if (step < 0x7F)
            step = 0x7F;
        else if (step > 0x6000)
            step = 0x6000;
        out[i] = (int16_t)sig;
    }
}

/* ------------------------------------------------------------------------- */
/* CRI ADX                                                                   */
/* ------------------------------------------------------------------------- */
/*
 * Header (big-endian): 00 u16 0x8000; 02 u16 offset of "(c)CRI" (data at offset + 4); 04 u8 encoding (3 =
 * fixed linear prediction); 05 u8 block size (18); 06 u8 bits (4); 07 u8 channels; 08 u32 rate; 0C u32
 * samples; 10 u16 high-pass cutoff; 12 u8 version (3, 4); 13 u8 flags (encryption, must be 0).  Version 3
 * with a header of at least 0x2C bytes: 18 u32 loop flag, 1C loop start sample, 24 loop end sample;
 * version 4: the same 0x10 bytes later.  Blocks: per channel, u16 scale + 32 signed nibbles (high first);
 * s = nibble * scale + (c1 * s1 + c2 * s2) >> 12 with c1, c2 from the cutoff (as FFmpeg's adx decoder).
 */
static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

sa1_status sa1a_adx_parse(const uint8_t *d, uint32_t len, sa1a_adx *out)
{
    uint32_t coff, ver, cutoff, blocks;
    double a, b, c;
    memset(out, 0, sizeof *out);
    if (!d || len < 0x20 || be16(d) != 0x8000)
        return SA1_ERR_BAD_FORMAT;
    coff = be16(d + 2);
    if (coff < 0x10 || (uint64_t)coff + 4u > len || memcmp(d + coff - 2, "(c)CRI", 6) != 0)
        return SA1_ERR_BAD_FORMAT;
    if (d[4] != 3 || d[5] != 18 || d[6] != 4 || d[7] < 1 || d[7] > 2 || d[0x13] != 0)
        return SA1_ERR_UNSUPPORTED;            /* other encodings / encryption are not used by SA1 */
    out->channels = d[7];
    out->rate = be32(d + 8);
    out->frames = be32(d + 12);
    cutoff = be16(d + 16);
    ver = d[18];
    if (out->rate < 1000 || out->rate > 96000 || cutoff == 0)
        return SA1_ERR_BAD_FORMAT;
    out->data_off = coff + 4u;
    out->block = 18;
    /* clamp the sample count to the blocks that are present */
    blocks = (len - out->data_off) / (out->block * out->channels);
    if ((uint64_t)blocks * 32u < out->frames)
        out->frames = blocks * 32u;
    a = sqrt(2.0) - cos(2.0 * 3.14159265358979323846 * (double)cutoff / (double)out->rate);
    b = sqrt(2.0) - 1.0;
    c = (a - sqrt((a + b) * (a - b))) / b;
    out->coef1 = (int32_t)floor(c * 8192.0 + 0.5);
    out->coef2 = (int32_t)floor(-(c * c) * 4096.0 + 0.5);
    {
        uint32_t lo = 0;
        if (ver == 3 && coff + 4u >= 0x2C)
            lo = 0x18;
        else if (ver == 4 && coff + 4u >= 0x3C)
            lo = 0x28;
        if (lo && be32(d + lo) != 0) {
            uint32_t ls = be32(d + lo + 4), le = be32(d + lo + 12);
            if (ls < le && le <= out->frames) {
                out->loop = 1;
                out->loop_start = ls;
                out->loop_end = le;
            }
        }
    }
    if (!out->loop) {
        out->loop_start = 0;
        out->loop_end = out->frames;
    }
    return SA1_OK;
}

void sa1a_adx_dec_init(sa1a_adx_dec *dec, const uint8_t *d, uint32_t len, const sa1a_adx *h)
{
    memset(dec, 0, sizeof *dec);
    dec->h = *h;
    dec->d = d;
    dec->len = len;
    dec->block = UINT32_MAX;
}

static void adx_decode_block(sa1a_adx_dec *dec, uint32_t blk)
{
    uint32_t ch, i, nch = dec->h.channels;
    if (dec->h.loop && !dec->loop_saved && blk == dec->h.loop_start / 32u) {
        memcpy(dec->loop_hist, dec->hist, sizeof dec->hist);
        dec->loop_saved = 1;
    }
    for (ch = 0; ch < nch; ch++) {
        uint64_t off = (uint64_t)dec->h.data_off + ((uint64_t)blk * nch + ch) * dec->h.block;
        int32_t s1 = dec->hist[ch][0], s2 = dec->hist[ch][1];
        if (off + dec->h.block > dec->len) {
            for (i = 0; i < 32; i++)
                dec->buf[i * nch + ch] = 0;
            continue;
        }
        {
            const uint8_t *p = dec->d + off;
            int32_t scale = (int32_t)be16(p);
            for (i = 0; i < 32; i++) {
                int32_t nib = (i & 1u) ? (p[2 + (i >> 1)] & 15) : (p[2 + (i >> 1)] >> 4);
                int32_t s;
                if (nib & 8)
                    nib -= 16;
                s = nib * scale + ((dec->h.coef1 * s1 + dec->h.coef2 * s2) >> 12);
                if (s > 32767)
                    s = 32767;
                else if (s < -32768)
                    s = -32768;
                s2 = s1;
                s1 = s;
                dec->buf[i * nch + ch] = (int16_t)s;
            }
        }
        dec->hist[ch][0] = s1;
        dec->hist[ch][1] = s2;
    }
    dec->block = blk;
}

void sa1a_adx_dec_seek(sa1a_adx_dec *dec, uint32_t frame)
{
    uint32_t target, b, start;
    if (frame > dec->h.frames)
        frame = dec->h.frames;
    target = frame / 32u;
    dec->pos = frame;
    if (dec->block != UINT32_MAX && dec->block == target)
        return;
    if (dec->h.loop && dec->loop_saved && target == dec->h.loop_start / 32u) {
        memcpy(dec->hist, dec->loop_hist, sizeof dec->hist);
        adx_decode_block(dec, target);
        return;
    }
    if (dec->block == UINT32_MAX || dec->block > target) {
        memset(dec->hist, 0, sizeof dec->hist);
        start = 0;
    } else {
        start = dec->block + 1u;
    }
    for (b = start; b <= target; b++)
        adx_decode_block(dec, b);
}

uint32_t sa1a_adx_dec_read(sa1a_adx_dec *dec, int16_t *out, uint32_t frames, int loop)
{
    uint32_t done = 0, nch = dec->h.channels;
    int do_loop = loop && dec->h.loop;
    while (done < frames) {
        uint32_t end = do_loop ? dec->h.loop_end : dec->h.frames;
        uint32_t blk, in_blk, n;
        if (dec->pos >= end) {
            if (!do_loop || dec->h.loop_end <= dec->h.loop_start)
                break;
            sa1a_adx_dec_seek(dec, dec->h.loop_start);
            continue;
        }
        blk = dec->pos / 32u;
        if (blk != dec->block) {
            if (dec->block != UINT32_MAX && blk == dec->block + 1u)
                adx_decode_block(dec, blk);
            else
                sa1a_adx_dec_seek(dec, dec->pos);
        }
        in_blk = dec->pos % 32u;
        n = 32u - in_blk;
        if (n > frames - done)
            n = frames - done;
        if (n > end - dec->pos)
            n = end - dec->pos;
        memcpy(out + (size_t)done * nch, dec->buf + (size_t)in_blk * nch, (size_t)n * nch * sizeof(int16_t));
        done += n;
        dec->pos += n;
    }
    return done;
}
