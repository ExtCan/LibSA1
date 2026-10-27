/*
 * Small helpers shared by the libsa1 test tools: PNG writer (stored deflate blocks,
 * no compression library needed), monotonic timer, file helpers.
 */
#ifndef SA1_TEST_UTIL_H
#define SA1_TEST_UTIL_H

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <direct.h>
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <time.h>
#  include <unistd.h>
#endif

static inline double tu_now_ms(void)
{
#if defined(_WIN32)
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}

static inline int tu_mkdir(const char *path)
{
#if defined(_WIN32)
    return _mkdir(path) == 0 || GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
#else
    return mkdir(path, 0777) == 0 || access(path, 0) == 0 ? 0 : -1;
#endif
}

static inline int tu_rmdir(const char *path)
{
#if defined(_WIN32)
    return _rmdir(path);
#else
    return rmdir(path);
#endif
}

static uint32_t tu_crc_table[256];
static int tu_crc_ready;

static inline uint32_t tu_crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    size_t i;
    if (!tu_crc_ready) {
        uint32_t k, c;
        int j;
        for (k = 0; k < 256; k++) {
            c = k;
            for (j = 0; j < 8; j++)
                c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            tu_crc_table[k] = c;
        }
        tu_crc_ready = 1;
    }
    crc = ~crc;
    for (i = 0; i < n; i++)
        crc = tu_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

static inline void tu_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static inline int tu_png_chunk(FILE *f, const char *tag, const uint8_t *data, size_t n)
{
    uint8_t hdr[8], crcb[4];
    uint32_t crc;
    tu_be32(hdr, (uint32_t)n);
    memcpy(hdr + 4, tag, 4);
    crc = tu_crc32(0, (const uint8_t *)tag, 4);
    crc = tu_crc32(crc, data, n);
    tu_be32(crcb, crc);
    return fwrite(hdr, 1, 8, f) == 8 && (n == 0 || fwrite(data, 1, n, f) == n) && fwrite(crcb, 1, 4, f) == 4;
}

/* RGBA8 PNG with uncompressed (stored) deflate blocks. */
static inline int tu_write_png(const char *path, uint32_t w, uint32_t h, const uint8_t *rgba, uint32_t pitch_px)
{
    size_t raw_len = (size_t)h * (1u + (size_t)w * 4u), nblocks, i, pos = 0, o = 0;
    uint8_t *raw, *z, ihdr[13];
    uint32_t a = 1, b = 0, y;
    FILE *f;
    int ok;
    raw = (uint8_t *)malloc(raw_len ? raw_len : 1);
    nblocks = raw_len / 65535u + 1u;
    z = (uint8_t *)malloc(2 + raw_len + 5 * nblocks + 4);
    if (!raw || !z) {
        free(raw);
        free(z);
        return 0;
    }
    for (y = 0; y < h; y++) {
        raw[(size_t)y * (1u + w * 4u)] = 0;
        memcpy(raw + (size_t)y * (1u + w * 4u) + 1, rgba + (size_t)y * pitch_px * 4u, (size_t)w * 4u);
    }
    z[o++] = 0x78;
    z[o++] = 0x01;
    for (i = 0; i < nblocks; i++) {
        size_t n = raw_len - pos > 65535u ? 65535u : raw_len - pos;
        z[o++] = (uint8_t)(i + 1 == nblocks ? 1 : 0);
        z[o++] = (uint8_t)n;
        z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n;
        z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, raw + pos, n);
        o += n;
        pos += n;
    }
    for (i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    tu_be32(z + o, (b << 16) | a);
    o += 4;
    tu_be32(ihdr, w);
    tu_be32(ihdr + 4, h);
    ihdr[8] = 8;    /* bit depth */
    ihdr[9] = 6;    /* RGBA */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    f = fopen(path, "wb");
    ok = f != NULL;
    if (ok) {
        static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        ok = fwrite(sig, 1, 8, f) == 8 && tu_png_chunk(f, "IHDR", ihdr, 13) && tu_png_chunk(f, "IDAT", z, o) &&
             tu_png_chunk(f, "IEND", NULL, 0);
        ok = (fclose(f) == 0) && ok;
    }
    free(raw);
    free(z);
    return ok;
}

/* Reads a whole text file (NUL-terminated); NULL if missing. */
static inline char *tu_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long n;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf) {
        buf[n] = 0;
        if (len)
            *len = (size_t)n;
    }
    return buf;
}

#endif