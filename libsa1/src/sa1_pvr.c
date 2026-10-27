/*
 * PVM archives and PVR textures (PowerVR2 / Dreamcast), decoded to RGBA8.
 *
 * Format notes (sadc/docs/research/sa1_assets.md section 6, verified on the disc):
 *   PVM:  "PVMH", u32 header size (data at 8 + size), u16 flags, u16 count, then
 *         per entry: u16 id, [28-byte name if flags&1], [u16 format if flags&2],
 *         [u16 dims if flags&4], [u32 GBIX if flags&8]; then one PVRT chunk per
 *         entry (optionally preceded by a GBIX chunk).
 *   PVRT: "PVRT", u32 size (from offset 8, includes padding), u8 pixel format,
 *         u8 data format, u16 0, u16 width, u16 height, data.
 *   Pixel formats: 0 ARGB1555, 1 RGB565, 2 ARGB4444 (the characters use these), 3 YUV422.
 *   Data formats:  1 twiddled, 2 twiddled + mips, 3 VQ, 4 VQ + mips, 9 rectangle,
 *                  0x0B stride, 0x0D rectangle twiddled, 0x10 small VQ, 0x11 small VQ + mips,
 *                  0x12 twiddled + mips (alternative padding).  Palettised (5-8) and bump: unsupported.
 *   Twiddled index of (x, y): y bits on even positions, x bits on odd positions;
 *   non-square textures are consecutive square blocks.
 *   Mips are stored smallest first.  16-bit twiddled: the 1x1 level occupies as much
 *   as two texels (format 2: 2 bytes of padding before it; 0x12: as much as a 2x2
 *   level, 6 bytes).  Chunk sizes include trailing alignment padding (to 32 bytes),
 *   so the layout never depends on the chunk length.  (Until 2026-10-20 this decoder
 *   took 6 bytes whenever the chunk was long enough, which misplaced the full-size
 *   level of every mipmapped character texture by 4 bytes = 2 texels in twiddled
 *   order: the "noisy" textures of the first in-game test.)
 *   VQ: 2x2-texel codebook (256 entries x 8 bytes, entry texels in twiddled order),
 *   then one byte per 2x2 block, twiddled; VQ mips put 1 byte (the 1x1 level) before
 *   the index levels.  Small VQ has fewer codebook entries, fixed by the width: 16 up to
 *   16 texels, then 32 / 128 / 256 (64 / 256 with mips).
 *   Verified texel for texel against an independent decoder (tests/texdump_ref.py, which
 *   agrees with SA Tools' ArchiveTool on all 782 textures of the character, effect and
 *   common archives) by tests/texdump.c.
 */
#include "sa1_internal.h"

#include <stdio.h>

sa1_status sa1_pvm_parse(const uint8_t *d, uint32_t size, sa1_pvm_entry *entries, uint32_t max_entries,
                         uint32_t *count)
{
    uint32_t hsize, flags, n, esize, i, pos;
    if (!d || !entries || !count)
        return SA1_ERR_INVALID_ARGUMENT;
    *count = 0;
    if (size < 12 || memcmp(d, "PVMH", 4) != 0)
        return SA1_ERR_BAD_FORMAT;
    hsize = sa1_rd32(d + 4);
    flags = sa1_rd16(d + 8);
    n = sa1_rd16(d + 10);
    if (n > max_entries || n > SA1_MAX_TEXTURES)
        return SA1_ERR_LIMIT;
    esize = 2u + ((flags & 1u) ? 28u : 0u) + ((flags & 2u) ? 2u : 0u) + ((flags & 4u) ? 2u : 0u) +
            ((flags & 8u) ? 4u : 0u);
    if (hsize > size - 8u || 0xCu + (uint64_t)esize * n > 8u + (uint64_t)hsize)
        return SA1_ERR_BAD_FORMAT;
    for (i = 0; i < n; i++) {
        const uint8_t *e = d + 0xCu + esize * i + 2u;
        sa1_pvm_entry *o = &entries[i];
        memset(o, 0, sizeof *o);
        if (flags & 1u) {
            uint32_t k;
            for (k = 0; k < 28 && e[k]; k++)
                o->name[k] = (e[k] >= 0x20 && e[k] < 0x7F) ? (char)e[k] : '_';
            o->name[k] = 0;
            e += 28;
        } else {
            snprintf(o->name, sizeof o->name, "%03u", (unsigned)i);
        }
        e += ((flags & 2u) ? 2u : 0u) + ((flags & 4u) ? 2u : 0u);
        o->gbix = (flags & 8u) ? sa1_rd32(e) : 0xFFFFFFFFu;
    }
    pos = 8u + hsize;
    for (i = 0; i < n; i++) {
        uint32_t csize;
        for (;;) {
            /* resync on the next chunk magic (alignment padding between chunks) */
            while (pos + 4u <= size && memcmp(d + pos, "PVRT", 4) != 0 && memcmp(d + pos, "GBIX", 4) != 0)
                pos++;
            if (pos + 8u > size)
                return SA1_ERR_BAD_FORMAT;
            if (memcmp(d + pos, "GBIX", 4) == 0) {
                uint32_t gs = sa1_rd32(d + pos + 4);
                if (gs > size - pos - 8u)
                    return SA1_ERR_BAD_FORMAT;
                if (gs >= 4 && entries[i].gbix == 0xFFFFFFFFu)
                    entries[i].gbix = sa1_rd32(d + pos + 8);
                pos += 8u + gs;
                continue;
            }
            break;
        }
        csize = sa1_rd32(d + pos + 4);
        entries[i].offset = pos;
        entries[i].length = csize > size - pos - 8u ? size - pos : 8u + csize;
        pos += entries[i].length;
    }
    *count = n;
    return SA1_OK;
}

sa1_status sa1_pvr_header(const uint8_t *c, uint32_t len, uint32_t *w, uint32_t *h, uint32_t *pf, uint32_t *df)
{
    if (!c || len < 16 || memcmp(c, "PVRT", 4) != 0)
        return SA1_ERR_BAD_FORMAT;
    *pf = c[8];
    *df = c[9];
    *w = sa1_rd16(c + 12);
    *h = sa1_rd16(c + 14);
    if (*w == 0 || *h == 0 || *w > SA1_MAX_TEX_DIM || *h > SA1_MAX_TEX_DIM)
        return SA1_ERR_BAD_FORMAT;
    return SA1_OK;
}

static int is_pow2(uint32_t x) { return x && !(x & (x - 1u)); }

/* x bits -> odd positions, y bits -> even positions */
static uint32_t twiddle(uint32_t x, uint32_t y)
{
    uint32_t r = 0, b;
    for (b = 0; b < 11; b++) {
        r |= ((y >> b) & 1u) << (2u * b);
        r |= ((x >> b) & 1u) << (2u * b + 1u);
    }
    return r;
}

static void put_color(uint32_t pf, uint32_t v, uint8_t *o)
{
    switch (pf) {
    case 0: /* ARGB1555 */
        o[0] = (uint8_t)(((v >> 10) & 31u) * 255u / 31u);
        o[1] = (uint8_t)(((v >> 5) & 31u) * 255u / 31u);
        o[2] = (uint8_t)((v & 31u) * 255u / 31u);
        o[3] = (uint8_t)((v & 0x8000u) ? 255u : 0u);
        break;
    case 1: /* RGB565 */
        o[0] = (uint8_t)(((v >> 11) & 31u) * 255u / 31u);
        o[1] = (uint8_t)(((v >> 5) & 63u) * 255u / 63u);
        o[2] = (uint8_t)((v & 31u) * 255u / 31u);
        o[3] = 255;
        break;
    default: /* 2: ARGB4444 */
        o[0] = (uint8_t)(((v >> 8) & 15u) * 17u);
        o[1] = (uint8_t)(((v >> 4) & 15u) * 17u);
        o[2] = (uint8_t)((v & 15u) * 17u);
        o[3] = (uint8_t)(((v >> 12) & 15u) * 17u);
        break;
    }
}

/* YUV422: two texels share U (low byte of the first word) and V (low byte of the second); Y in the high bytes */
static void put_yuv_pair(uint32_t w0, uint32_t w1, uint8_t *o0, uint8_t *o1)
{
    int32_t yy[2], u = (int32_t)(w0 & 255u) - 128, v = (int32_t)(w1 & 255u) - 128, k;
    uint8_t *o[2];
    yy[0] = (int32_t)(w0 >> 8);
    yy[1] = (int32_t)(w1 >> 8);
    o[0] = o0;
    o[1] = o1;
    for (k = 0; k < 2; k++) {
        double c[3];
        int32_t j;
        c[0] = yy[k] + 1.375 * v;
        c[1] = yy[k] - 0.34375 * u - 0.6875 * v;
        c[2] = yy[k] + 1.71875 * u;
        for (j = 0; j < 3; j++) {
            double cv = c[j] < 0.0 ? 0.0 : c[j] > 255.0 ? 255.0 : c[j];
            o[k][j] = (uint8_t)cv;
        }
        o[k][3] = 255;
    }
}

/* texel number of (x, y) in a twiddled w x h texture: Morton order inside squares of the smaller side, the squares
 * one after another */
static uint32_t twiddled_index(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t s = w < h ? w : h;
    return (x / s + y / s) * s * s + twiddle(x % s, y % s);
}

/* Bytes before the full-size level of a mipmapped 16-bit twiddled texture: the smaller levels, smallest first (1x1,
 * 2x2, ... w/2 x w/2 at 2 bytes per texel), after `pad` bytes: the 1x1 level occupies as much as two texels in data
 * format 2 (pad 2) and as much as a 2x2 level in format 0x12 (pad 6). Chunks end with alignment padding, so the
 * chunk length does not tell the layout. */
static uint32_t mip_offset16(uint32_t w, uint32_t pad)
{
    uint32_t off = pad, s;
    for (s = 1; s < w; s *= 2u)
        off += 2u * s * s;
    return off;
}

/* VQ mipmaps: one byte for the 1x1 level, then one index byte per 2x2 block of the levels 2x2 ... w/2 x w/2 */
static uint32_t mip_offset_vq(uint32_t w)
{
    uint32_t off = 1, s;
    for (s = 2; s < w; s *= 2u)
        off += (s / 2u) * (s / 2u);
    return off;
}

/* codebook entries of small VQ (data formats 0x10 / 0x11): fixed by the width */
static uint32_t small_vq_entries(uint32_t w, int mips)
{
    if (!mips)
        return w <= 16u ? 16u : w == 32u ? 32u : w == 64u ? 128u : 256u;
    return w <= 16u ? 16u : w == 32u ? 64u : 256u;
}

sa1_status sa1_pvr_decode(const uint8_t *c, uint32_t len, uint8_t *rgba, size_t rgba_size)
{
    uint32_t w, h, pf, df, size, blen, x, y;
    const uint8_t *body;
    sa1_status st = sa1_pvr_header(c, len, &w, &h, &pf, &df);
    if (st != SA1_OK)
        return st;
    if (!rgba || rgba_size < (size_t)w * h * 4u)
        return SA1_ERR_INVALID_ARGUMENT;
    if (pf > 3)
        return SA1_ERR_UNSUPPORTED;     /* bump and palettised formats are not used by the characters */
    size = sa1_rd32(c + 4);
    blen = (size < 8u || 8u + (uint64_t)size > len) ? len : 8u + size;
    if (blen < 16)
        return SA1_ERR_BAD_FORMAT;
    body = c + 16;
    blen -= 16;

    /* 16-bit texels: twiddled (1), twiddled + mips (2, 0x12), rectangle twiddled (0x0D), rectangle / stride (9, 0x0B) */
    if (df == 1 || df == 2 || df == 0x12 || df == 0x0D || df == 9 || df == 0x0B) {
        uint32_t off = 0, need = w * h * 2u;
        int linear = (df == 9 || df == 0x0B);
        if (!linear) {
            if (!is_pow2(w) || !is_pow2(h))
                return SA1_ERR_UNSUPPORTED;
            if (df == 2 || df == 0x12) {
                if (w != h)
                    return SA1_ERR_UNSUPPORTED;
                off = mip_offset16(w, df == 2 ? 2u : 6u);
            }
        }
        if (pf == 3 && (w & 1u))
            return SA1_ERR_UNSUPPORTED;
        if ((uint64_t)off + need > blen)
            return SA1_ERR_BAD_FORMAT;
        body += off;
        if (pf == 3) {
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x += 2u) {
                    uint32_t i0 = linear ? y * w + x : twiddled_index(x, y, w, h);
                    uint32_t i1 = linear ? y * w + x + 1u : twiddled_index(x + 1u, y, w, h);
                    put_yuv_pair(sa1_rd16(body + 2u * i0), sa1_rd16(body + 2u * i1), rgba + 4u * (y * w + x),
                                 rgba + 4u * (y * w + x + 1u));
                }
            return SA1_OK;
        }
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                uint32_t idx = linear ? y * w + x : twiddled_index(x, y, w, h);
                put_color(pf, sa1_rd16(body + 2u * idx), rgba + 4u * (y * w + x));
            }
        return SA1_OK;
    }

    /* VQ: codebook of 2x2-texel entries (texels in twiddled order), then one index byte per 2x2 block, twiddled */
    if (df == 3 || df == 4 || df == 0x10 || df == 0x11) {
        int mip = (df == 4 || df == 0x11);
        uint32_t bw = w / 2u, bh = h / 2u, ncodes, off;
        const uint8_t *idx;
        if (pf == 3 || !is_pow2(w) || !is_pow2(h) || w < 2 || h < 2)
            return SA1_ERR_UNSUPPORTED;
        if (mip && w != h)
            return SA1_ERR_UNSUPPORTED;
        ncodes = (df == 3 || df == 4) ? 256u : small_vq_entries(w, mip);
        off = 8u * ncodes + (mip ? mip_offset_vq(w) : 0u);
        if ((uint64_t)off + (uint64_t)bw * bh > blen)
            return SA1_ERR_BAD_FORMAT;
        idx = body + off;
        for (y = 0; y < bh; y++)
            for (x = 0; x < bw; x++) {
                uint32_t code = idx[twiddled_index(x, y, bw, bh)], t;
                if (code >= ncodes)     /* not on the disc; a malformed index reads the last entry */
                    code = ncodes - 1u;
                for (t = 0; t < 4; t++) {
                    uint32_t px = 2u * x + (t >> 1), py = 2u * y + (t & 1u);
                    put_color(pf, sa1_rd16(body + 8u * code + 2u * t), rgba + 4u * (py * w + px));
                }
            }
        return SA1_OK;
    }
    return SA1_ERR_UNSUPPORTED;
}
