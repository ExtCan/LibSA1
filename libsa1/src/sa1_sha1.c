/*
 * SHA-1 (FIPS 180-4), written from the specification.  Used to identify the disc
 * version and to check every file read from it; not used for security.
 */
#include "sa1_internal.h"

static uint32_t rol(uint32_t x, unsigned n) { return (x << n) | (x >> (32u - n)); }

static void block(sa1_sha1 *s, const uint8_t *p)
{
    uint32_t w[80], a, b, c, d, e, t;
    unsigned i;
    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (i = 16; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = s->h[0]; b = s->h[1]; c = s->h[2]; d = s->h[3]; e = s->h[4];
    for (i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }
        t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e;
}

void sa1_sha1_init(sa1_sha1 *s)
{
    s->h[0] = 0x67452301u;
    s->h[1] = 0xEFCDAB89u;
    s->h[2] = 0x98BADCFEu;
    s->h[3] = 0x10325476u;
    s->h[4] = 0xC3D2E1F0u;
    s->length = 0;
    s->used = 0;
}

void sa1_sha1_update(sa1_sha1 *s, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    s->length += (uint64_t)len;
    while (len) {
        size_t take = 64u - s->used;
        if (take > len)
            take = len;
        memcpy(s->block + s->used, p, take);
        s->used += (uint32_t)take;
        p += take;
        len -= take;
        if (s->used == 64u) {
            block(s, s->block);
            s->used = 0;
        }
    }
}

void sa1_sha1_final(sa1_sha1 *s, uint8_t digest[20])
{
    uint64_t bits = s->length * 8u;
    uint8_t pad = 0x80;
    uint8_t lenb[8];
    unsigned i;
    sa1_sha1_update(s, &pad, 1);
    pad = 0;
    while (s->used != 56u)
        sa1_sha1_update(s, &pad, 1);
    for (i = 0; i < 8; i++)
        lenb[i] = (uint8_t)(bits >> (56 - 8 * i));
    sa1_sha1_update(s, lenb, 8);
    for (i = 0; i < 5; i++) {
        digest[4 * i] = (uint8_t)(s->h[i] >> 24);
        digest[4 * i + 1] = (uint8_t)(s->h[i] >> 16);
        digest[4 * i + 2] = (uint8_t)(s->h[i] >> 8);
        digest[4 * i + 3] = (uint8_t)s->h[i];
    }
}

void sa1_sha1_hex(const void *data, size_t len, char hex[41])
{
    static const char digits[] = "0123456789abcdef";
    sa1_sha1 s;
    uint8_t d[20];
    unsigned i;
    sa1_sha1_init(&s);
    sa1_sha1_update(&s, data, len);
    sa1_sha1_final(&s, d);
    for (i = 0; i < 20; i++) {
        hex[2 * i] = digits[d[i] >> 4];
        hex[2 * i + 1] = digits[d[i] & 15];
    }
    hex[40] = 0;
}