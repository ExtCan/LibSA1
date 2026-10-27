/*
 * PRS decompression (SEGA's LZ77 variant), implemented from the bitstream description
 * in sadc/tools/prs.py:
 *
 *   control bits are read LSB first from a control byte that is fetched lazily;
 *   1            literal byte
 *   0 0 b1 b0 X  short copy: size = (b1<<1|b0) + 2, offset = X - 256
 *   0 1 LO HI    long copy:  w = LO | HI<<8; w == 0 ends the stream;
 *                offset = (w >> 3) - 0x2000; size = (w & 7) + 2, or the next
 *                byte + 1 when (w & 7) == 0
 *
 * Copies may overlap the bytes they produce.  Every read and write is bounds-checked;
 * the output is capped at max_out bytes.
 */
#include "sa1_internal.h"

typedef struct prs_in {
    const uint8_t *p;
    size_t         n, pos;
    unsigned       cmd, nbits;
} prs_in;

static int prs_bit(prs_in *r, unsigned *bit)
{
    if (r->nbits == 0) {
        if (r->pos >= r->n)
            return 0;
        r->cmd = r->p[r->pos++];
        r->nbits = 8;
    }
    *bit = r->cmd & 1u;
    r->cmd >>= 1;
    r->nbits--;
    return 1;
}

static int prs_byte(prs_in *r, unsigned *b)
{
    if (r->pos >= r->n)
        return 0;
    *b = r->p[r->pos++];
    return 1;
}

typedef struct prs_out {
    const sa1_context *ctx;
    uint8_t           *buf;
    size_t             len, cap, max;
} prs_out;

static sa1_status prs_reserve(prs_out *o, size_t extra)
{
    size_t need, ncap;
    uint8_t *nb;
    if (extra > o->max - o->len)
        return SA1_ERR_LIMIT;
    need = o->len + extra;
    if (need <= o->cap)
        return SA1_OK;
    ncap = o->cap ? o->cap : 65536u;
    while (ncap < need)
        ncap = ncap > o->max / 2 ? o->max : ncap * 2;
    nb = (uint8_t *)sa1_mem_alloc(o->ctx, ncap);
    if (!nb)
        return SA1_ERR_OUT_OF_MEMORY;
    if (o->len)
        memcpy(nb, o->buf, o->len);
    sa1_mem_free(o->ctx, o->buf);
    o->buf = nb;
    o->cap = ncap;
    return SA1_OK;
}

sa1_status sa1_prs_decompress(const sa1_context *ctx, const uint8_t *src, size_t src_len, size_t max_out,
                              uint8_t **out, uint32_t *out_len)
{
    prs_in r;
    prs_out o;
    sa1_status st = SA1_ERR_BAD_FORMAT;

    if (!ctx || !src || !out || !out_len)
        return SA1_ERR_INVALID_ARGUMENT;
    *out = NULL;
    *out_len = 0;
    if (max_out > SA1_MAX_UNPACKED_SIZE)
        max_out = SA1_MAX_UNPACKED_SIZE;
    memset(&r, 0, sizeof r);
    r.p = src;
    r.n = src_len;
    memset(&o, 0, sizeof o);
    o.ctx = ctx;
    o.max = max_out;
    {
        size_t first = src_len * 4u;
        if (first < 65536u)
            first = 65536u;
        if (first > max_out)
            first = max_out;
        if (first && (st = prs_reserve(&o, first)) != SA1_OK)
            goto fail;
        st = SA1_ERR_BAD_FORMAT;
    }

    for (;;) {
        unsigned bit, size, off;
        size_t start, i;
        if (!prs_bit(&r, &bit))
            goto fail;
        if (bit) {
            unsigned b;
            if (!prs_byte(&r, &b))
                goto fail;
            if ((st = prs_reserve(&o, 1)) != SA1_OK)
                goto fail;
            o.buf[o.len++] = (uint8_t)b;
            st = SA1_ERR_BAD_FORMAT;
            continue;
        }
        if (!prs_bit(&r, &bit))
            goto fail;
        if (bit) {
            unsigned lo, hi, w;
            if (!prs_byte(&r, &lo) || !prs_byte(&r, &hi))
                goto fail;
            w = lo | (hi << 8);
            if (w == 0)
                break;                          /* end of stream */
            off = 0x2000u - (w >> 3);           /* distance back, 1..8192 */
            size = w & 7u;
            if (size == 0) {
                unsigned nn;
                if (!prs_byte(&r, &nn))
                    goto fail;
                size = nn + 1u;
            } else {
                size += 2u;
            }
        } else {
            unsigned b1, b0, x;
            if (!prs_bit(&r, &b1) || !prs_bit(&r, &b0) || !prs_byte(&r, &x))
                goto fail;
            size = ((b1 << 1) | b0) + 2u;
            off = 256u - x;                     /* distance back, 1..256 */
        }
        if (off > o.len)
            goto fail;                          /* copy before the start of the output */
        if ((st = prs_reserve(&o, size)) != SA1_OK)
            goto fail;
        st = SA1_ERR_BAD_FORMAT;
        start = o.len - off;
        for (i = 0; i < size; i++)              /* byte-wise: overlapping copies repeat */
            o.buf[o.len + i] = o.buf[start + i];
        o.len += size;
    }
    if (o.len > 0xFFFFFFFFu) {
        st = SA1_ERR_LIMIT;
        goto fail;
    }
    *out = o.buf;
    *out_len = (uint32_t)o.len;
    return SA1_OK;

fail:
    sa1_mem_free(ctx, o.buf);
    return st;
}