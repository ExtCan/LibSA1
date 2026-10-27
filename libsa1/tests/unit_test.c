/*
 * unit_test: self-contained tests of the libsa1 building blocks (SHA-1, PRS, paths,
 * .gdi parser, PVM/PVR decoders, coordinates, collision input, API argument checks,
 * allocation balance), plus robustness checks that run when a .gdi is given:
 *
 *   unit_test [game.gdi [fuzz_runs]]
 *
 * With a .gdi: corrupted 1ST_READ.BIN must be rejected by its hash, and the Ninja
 * deserializers are fed hundreds of randomly corrupted copies of the real image
 * (hash check bypassed) and must fail cleanly (build with --debug for UBSan traps).
 * Uses internal headers (links the static library).
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#  define _POSIX_C_SOURCE 200809L
#endif
#include "sa1_internal.h"
#include "test_util.h"

#include "guard_alloc.c"       /* SA1_GUARD_ALLOC: tests/guard_alloc.h */

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

static uint32_t g_rng = 12345u;
static uint32_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

/* counting allocator */
static long g_allocs, g_frees;
static void *count_alloc(void *u, size_t n) { (void)u; g_allocs++; return ga_malloc(n); }
static void count_free(void *u, void *p) { (void)u; if (p) g_frees++; ga_free(p); }

static sa1_context *new_ctx(void)
{
    sa1_host h;
    sa1_context *ctx = NULL;
    sa1_host_defaults(&h);
    h.alloc = count_alloc;
    h.free = count_free;
    h.log_level = SA1_LOG_ERROR + 1;    /* silent */
    if (sa1_init(&h, &ctx) != SA1_OK)
        return NULL;
    return ctx;
}

/* ------------------------------------------------------------------------- */

static void test_sha1(void)
{
    char hex[41];
    char *big = (char *)malloc(1000000);
    sa1_sha1_hex("abc", 3, hex);
    CHECK(strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
    sa1_sha1_hex("", 0, hex);
    CHECK(strcmp(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0);
    sa1_sha1_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, hex);
    CHECK(strcmp(hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1") == 0);
    if (big) {
        memset(big, 'a', 1000000);
        sa1_sha1_hex(big, 1000000, hex);
        CHECK(strcmp(hex, "34aa973cd4c4daa4f61eeb2bdbad27316534016f") == 0);
        free(big);
    }
}

static void test_prs(sa1_context *ctx)
{
    static const uint8_t lit[] = { 0x05, 'A', 0x00, 0x00 };
    static const uint8_t shortcp[] = { 0x93, 'A', 'B', 0xFE, 0x00, 0x00 };
    /* literal 'X', then long copy w = ((0x2000-1)<<3) | 0 with extended size byte 9 -> 10 x 'X' */
    static const uint8_t longcp[] = { 0x15, 'X', 0xF8, 0xFF, 0x09, 0x00, 0x00 };
    static const uint8_t trunc[] = { 0x05, 'A' };
    static const uint8_t before[] = { 0x00, 0xFF, 0x00, 0x00 };
    uint8_t *out = NULL, buf[512];
    uint32_t n = 0, i, it;

    CHECK(sa1_prs_decompress(ctx, lit, sizeof lit, 1u << 20, &out, &n) == SA1_OK && n == 1 && out[0] == 'A');
    sa1_mem_free(ctx, out);
    CHECK(sa1_prs_decompress(ctx, shortcp, sizeof shortcp, 1u << 20, &out, &n) == SA1_OK && n == 6 &&
          memcmp(out, "ABABAB", 6) == 0);
    sa1_mem_free(ctx, out);
    CHECK(sa1_prs_decompress(ctx, longcp, sizeof longcp, 1u << 20, &out, &n) == SA1_OK && n == 11 &&
          memcmp(out, "XXXXXXXXXXX", 11) == 0);
    sa1_mem_free(ctx, out);
    CHECK(sa1_prs_decompress(ctx, trunc, sizeof trunc, 1u << 20, &out, &n) == SA1_ERR_BAD_FORMAT && out == NULL);
    CHECK(sa1_prs_decompress(ctx, before, sizeof before, 1u << 20, &out, &n) == SA1_ERR_BAD_FORMAT);
    /* output cap */
    CHECK(sa1_prs_decompress(ctx, longcp, sizeof longcp, 5, &out, &n) == SA1_ERR_LIMIT);
    /* random streams never crash */
    for (it = 0; it < 20000; it++) {
        uint32_t len = rnd() % sizeof buf;
        sa1_status st;
        for (i = 0; i < len; i++)
            buf[i] = (uint8_t)rnd();
        st = sa1_prs_decompress(ctx, buf, len, 1u << 16, &out, &n);
        if (st == SA1_OK)
            sa1_mem_free(ctx, out);
    }
    CHECK(1);
}

static void test_paths(void)
{
    static const char *const bad_host[] = {
        "\\\\server\\share\\sa1.gdi", "//server/share/sa1.gdi", "\\\\?\\C:\\sa1.gdi", "\\\\.\\PhysicalDrive0",
        "\\??\\C:\\sa1.gdi", "C:\\games\\CON", "NUL.gdi", "C:\\a\\sa1.gdi:stream", "C:\\a\\com1\\x.gdi", "",
        "a\x01.gdi", "1:\\x.gdi",
    };
    static const char *const good_host[] = {
        "C:\\games\\sa1.gdi", "games/sa1.gdi", "/home/user/sa1.gdi", "sa1.gdi",
        "G:\\Games (Dreamcast)\\Sonic Adventure v1.005 (1999)(Sega)(US)(M5)[!][%51000-A].gdi",
        "D:/console/CONSOLE.gdi",
    };
    static const char *const bad_bare[] = {
        "../track01.bin", "..", ".", "a/b.bin", "a\\b.bin", "C:x.bin", "CON", "com1.bin", "track01.bin.",
        "track01.bin ", "LPT9", "\\\\server\\x", "", "x\x01", "aux.txt", "trä.bin", "a:b", "*.bin",
    };
    static const char *const good_bare[] = { "track01.bin", "track 03.bin", "Track03.RAW", "console.bin", "..a.bin" };
    size_t i;
    for (i = 0; i < sizeof bad_host / sizeof bad_host[0]; i++)
        CHECK(sa1_path_check_host(bad_host[i]) == SA1_ERR_BAD_PATH);
    for (i = 0; i < sizeof good_host / sizeof good_host[0]; i++)
        CHECK(sa1_path_check_host(good_host[i]) == SA1_OK);
    for (i = 0; i < sizeof bad_bare / sizeof bad_bare[0]; i++)
        CHECK(sa1_path_check_bare(bad_bare[i]) == SA1_ERR_BAD_PATH);
    for (i = 0; i < sizeof good_bare / sizeof good_bare[0]; i++)
        CHECK(sa1_path_check_bare(good_bare[i]) == SA1_OK);
}

static sa1_status gdi(const char *text, sa1_gdi_track *t, uint32_t *n)
{
    return sa1_gdi_parse(text, strlen(text), t, SA1_MAX_TRACKS, n);
}

static void test_gdi(void)
{
    sa1_gdi_track t[SA1_MAX_TRACKS];
    uint32_t n = 0;
    CHECK(gdi("3\r\n1 0 4 2352 track01.bin 0\r\n2 14714 0 2352 track02.raw 0\r\n3 45000 4 2352 track03.bin 0\r\n", t, &n) ==
          SA1_OK && n == 3 && t[2].lba == 45000 && t[2].type == 4 && strcmp(t[2].name, "track03.bin") == 0);
    CHECK(gdi("\xEF\xBB\xBF" "2\n1 0 4 2048 \"track 01.iso\" 0\n\n2 600 4 2352 \"t2.bin\"\n", t, &n) == SA1_OK && n == 2 &&
          strcmp(t[0].name, "track 01.iso") == 0 && t[0].sector_size == 2048 && t[1].lba == 600);
    CHECK(gdi("3\n1 0 4 2352 track01.bin 0\n2 14714 0 2352 track02.raw 0\n", t, &n) == SA1_ERR_BAD_FORMAT);
    CHECK(gdi("1\n1 0 4 2336 track01.bin 0\n", t, &n) == SA1_ERR_UNSUPPORTED);
    CHECK(gdi("1\n1 0 4 2352 ../track01.bin 0\n", t, &n) == SA1_ERR_BAD_PATH);
    CHECK(gdi("1\n1 0 4 2352 \"C:\\x\\track01.bin\" 0\n", t, &n) == SA1_ERR_BAD_PATH);
    CHECK(gdi("1\n1 0 4 2352 \\\\srv\\share\\t.bin 0\n", t, &n) == SA1_ERR_BAD_PATH);
    CHECK(gdi("1\n1 0 2 2352 track01.bin 0\n", t, &n) == SA1_ERR_BAD_FORMAT);
    CHECK(gdi("2\n2 0 4 2352 a.bin 0\n1 9 4 2352 b.bin 0\n", t, &n) == SA1_ERR_BAD_FORMAT);
    CHECK(gdi("1\n1 0 4 2352 a.bin 0 junk\n", t, &n) == SA1_ERR_BAD_FORMAT);
    CHECK(gdi("1\n1 99999999999 4 2352 a.bin 0\n", t, &n) == SA1_ERR_BAD_FORMAT);
    CHECK(gdi("100\n", t, &n) == SA1_ERR_BAD_FORMAT);
    CHECK(gdi("1\n1 0 4 2352 \"unterminated 0\n", t, &n) == SA1_ERR_BAD_FORMAT);
    CHECK(gdi("", t, &n) == SA1_ERR_BAD_FORMAT);
    {
        char buf[300];
        uint32_t it, i;
        for (it = 0; it < 20000; it++) {
            uint32_t len = rnd() % (sizeof buf - 1);
            static const char alphabet[] = "0123456789 \t\r\n\"abc.\\/:";
            for (i = 0; i < len; i++)
                buf[i] = alphabet[rnd() % (sizeof alphabet - 1)];
            buf[len] = 0;
            (void)sa1_gdi_parse(buf, len, t, SA1_MAX_TRACKS, &n);
        }
        CHECK(1);
    }
}

/* 16-bit texel stream helper */
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v); put16(p + 2, v >> 16); }

static size_t make_pvr(uint8_t *c, uint32_t pf, uint32_t df, uint32_t w, uint32_t h, const uint8_t *data, size_t n)
{
    memcpy(c, "PVRT", 4);
    put32(c + 4, (uint32_t)(8 + n));
    c[8] = (uint8_t)pf;
    c[9] = (uint8_t)df;
    put16(c + 10, 0);
    put16(c + 12, w);
    put16(c + 14, h);
    memcpy(c + 16, data, n);
    return 16 + n;
}

static void test_pvr(void)
{
    uint8_t chunk[4096], data[4096], rgba[64 * 64 * 4];
    size_t len;
    uint32_t x, y, i, ok;

    /* 4x4 twiddled RGB565 where texel value = twiddled index * 0x0801 (distinct colours) */
    for (i = 0; i < 16; i++)
        put16(data + 2 * i, (i << 11) | (i << 5) | i);
    len = make_pvr(chunk, 1, 1, 4, 4, data, 32);
    CHECK(sa1_pvr_decode(chunk, (uint32_t)len, rgba, sizeof rgba) == SA1_OK);
    ok = 1;
    for (y = 0; y < 4; y++)
        for (x = 0; x < 4; x++) {
            /* twiddled index: y bits even, x bits odd */
            uint32_t ti = ((y & 1) | ((x & 1) << 1) | ((y & 2) << 1) | ((x & 2) << 2));
            if (rgba[4 * (y * 4 + x)] != (uint8_t)(ti * 255 / 31) || rgba[4 * (y * 4 + x) + 3] != 255)
                ok = 0;
        }
    CHECK(ok);
    /* truncated */
    CHECK(sa1_pvr_decode(chunk, 16 + 10, rgba, sizeof rgba) == SA1_ERR_BAD_FORMAT);

    /* rectangle 2x2 ARGB4444 */
    put16(data, 0xF00F); put16(data + 2, 0x0F00); put16(data + 4, 0x00F0); put16(data + 6, 0x8888);
    len = make_pvr(chunk, 2, 9, 2, 2, data, 8);
    CHECK(sa1_pvr_decode(chunk, (uint32_t)len, rgba, sizeof rgba) == SA1_OK);
    CHECK(rgba[0] == 0 && rgba[1] == 0 && rgba[2] == 255 && rgba[3] == 255);     /* 0xF00F: a15 r0 g0 b15 */
    CHECK(rgba[4] == 255 && rgba[7] == 0);                                         /* 0x0F00: r15, a0 */
    CHECK(rgba[12] == 136 && rgba[15] == 136);

    /* ARGB1555 alpha bit */
    put16(data, 0x8000 | (31 << 10)); put16(data + 2, 31);
    len = make_pvr(chunk, 0, 9, 2, 1, data, 4);
    CHECK(sa1_pvr_decode(chunk, (uint32_t)len, rgba, sizeof rgba) == SA1_OK && rgba[0] == 255 && rgba[3] == 255 &&
          rgba[6] == 255 && rgba[7] == 0);

    /* VQ 4x4: 256-entry codebook, entry k = 4 texels of value k; index map (2x2 blocks) = 1,2,3,4 */
    memset(data, 0, sizeof data);
    for (i = 0; i < 256; i++) {
        uint32_t t;
        for (t = 0; t < 4; t++)
            put16(data + 8 * i + 2 * t, ((i & 31) << 11) | t);
    }
    data[2048] = 1; data[2049] = 2; data[2050] = 3; data[2051] = 4;
    len = make_pvr(chunk, 1, 3, 4, 4, data, 2052);
    CHECK(sa1_pvr_decode(chunk, (uint32_t)len, rgba, sizeof rgba) == SA1_OK);
    /* block (bx,by) uses code idx[twiddle]; (0,0)->1, (0,1)->2, (1,0)->3, (1,1)->4 */
    CHECK(rgba[0] == (uint8_t)(1 * 255 / 31));                         /* (0,0) block (0,0) */
    CHECK(rgba[4 * (2 * 4 + 0)] == (uint8_t)(2 * 255 / 31));           /* (0,2) block (0,1) */
    CHECK(rgba[4 * (0 * 4 + 2)] == (uint8_t)(3 * 255 / 31));           /* (2,0) block (1,0) */
    CHECK(rgba[4 * (3 * 4 + 3)] == (uint8_t)(4 * 255 / 31));
    /* codebook texel order inside a block: t=1 is (x0, y1) */
    CHECK((rgba[4 * (1 * 4 + 0) + 2]) == (uint8_t)(1 * 255 / 31));      /* blue = t = 1 */
    CHECK(sa1_pvr_decode(chunk, 16 + 2000, rgba, sizeof rgba) == SA1_ERR_BAD_FORMAT);

    /* unsupported formats */
    len = make_pvr(chunk, 5, 1, 4, 4, data, 32);
    CHECK(sa1_pvr_decode(chunk, (uint32_t)len, rgba, sizeof rgba) == SA1_ERR_UNSUPPORTED);
    len = make_pvr(chunk, 1, 0x0E, 4, 4, data, 32);
    CHECK(sa1_pvr_decode(chunk, (uint32_t)len, rgba, sizeof rgba) == SA1_ERR_UNSUPPORTED);
    /* too large */
    len = make_pvr(chunk, 1, 1, 2048, 4, data, 32);
    CHECK(sa1_pvr_decode(chunk, (uint32_t)len, rgba, sizeof rgba) == SA1_ERR_BAD_FORMAT);

    /* random chunks never crash */
    {
        uint32_t it;
        for (it = 0; it < 20000; it++) {
            uint32_t n = 16 + rnd() % 600, w = 1u << (rnd() % 6), h = 1u << (rnd() % 6);
            for (i = 0; i < n; i++)
                chunk[i] = (uint8_t)rnd();
            memcpy(chunk, "PVRT", 4);
            chunk[8] = (uint8_t)(rnd() % 4);
            chunk[9] = (uint8_t)((rnd() & 1) ? (rnd() % 18) : (rnd() % 256));
            put16(chunk + 12, w);
            put16(chunk + 14, h);
            (void)sa1_pvr_decode(chunk, n, rgba, sizeof rgba);
        }
        CHECK(1);
    }
}

static void test_pvm(void)
{
    uint8_t buf[2048];
    sa1_pvm_entry e[8];
    uint32_t n = 0, i, it;
    size_t pos;
    memset(buf, 0, sizeof buf);
    memcpy(buf, "PVMH", 4);
    put32(buf + 4, 0x60 - 8);       /* data at 0x60 (after the 2 x 34-byte table) */
    put16(buf + 8, 0x0009);         /* names + GBIX */
    put16(buf + 10, 2);
    put16(buf + 12, 0);
    memcpy(buf + 14, "first", 5);
    put32(buf + 14 + 28, 1000);
    put16(buf + 46, 1);
    memcpy(buf + 48, "second", 6);
    put32(buf + 48 + 28, 1001);
    pos = 0x60 + 3;                 /* misaligned: the parser resyncs on the magic */
    memcpy(buf + pos, "PVRT", 4);
    put32(buf + pos + 4, 8 + 8);
    put16(buf + pos + 12, 2);
    put16(buf + pos + 14, 2);
    pos += 24;
    memcpy(buf + pos, "GBIX", 4);
    put32(buf + pos + 4, 8);
    pos += 16;
    memcpy(buf + pos, "PVRT", 4);
    put32(buf + pos + 4, 8 + 8);
    put16(buf + pos + 12, 2);
    put16(buf + pos + 14, 2);
    pos += 24;
    CHECK(sa1_pvm_parse(buf, (uint32_t)pos, e, 8, &n) == SA1_OK && n == 2 && strcmp(e[0].name, "first") == 0 &&
          e[1].gbix == 1001 && e[0].offset == 0x63 && e[1].length == 24);
    CHECK(sa1_pvm_parse(buf, (uint32_t)pos - 30, e, 8, &n) == SA1_ERR_BAD_FORMAT);
    CHECK(sa1_pvm_parse(buf, (uint32_t)pos, e, 1, &n) == SA1_ERR_LIMIT);
    for (it = 0; it < 20000; it++) {
        uint32_t len = 12 + rnd() % 500;
        for (i = 12; i < len; i++)
            buf[i] = (uint8_t)((rnd() & 3) ? rnd() : "PVRTGBIX"[rnd() % 8]);
        put32(buf + 4, rnd() % 600);
        put16(buf + 8, rnd() & 0xF);
        put16(buf + 10, rnd() % 10);
        if (sa1_pvm_parse(buf, len, e, 8, &n) == SA1_OK)
            for (i = 0; i < n; i++)
                CHECK(e[i].offset + e[i].length <= len);
    }
}

static void test_coords_collision(void)
{
    sa1_context *ctx = new_ctx();
    sa1_coords c, bad;
    float p[3] = { 1.0f, 2.0f, 3.0f }, h[3], back[3];
    sa1_collision_tri tri[2];
    uint32_t id = 0, id2 = 0, st_tris = 0, objs = 0, otris = 0;
    float rot[9] = { 0, -1, 0, 1, 0, 0, 0, 0, 1 }, badrot[9] = { 2, 0, 0, 0, 1, 0, 0, 0, 1 };
    if (!ctx) {
        CHECK(0);
        return;
    }
    CHECK(sa1_coords_preset(SA1_COORDS_PRESET_SOURCE, &c) == SA1_OK && c.scale == 4.0f);
    sa1_to_host_point(ctx, p, h);           /* default Source: 4 * (x, -z, y) */
    CHECK(h[0] == 4.0f && h[1] == -12.0f && h[2] == 8.0f);
    sa1_from_host_point(ctx, h, back);
    CHECK(fabsf(back[0] - 1) < 1e-6f && fabsf(back[1] - 2) < 1e-6f && fabsf(back[2] - 3) < 1e-6f);
    bad = c;
    bad.axis[0] = 2.0f;
    CHECK(sa1_set_coords(ctx, &bad) == SA1_ERR_INVALID_ARGUMENT);
    bad = c;
    bad.scale = 0.0f;
    CHECK(sa1_set_coords(ctx, &bad) == SA1_ERR_INVALID_ARGUMENT);
    bad = c;
    bad.flags = 0x80;
    CHECK(sa1_set_coords(ctx, &bad) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_coords_preset(SA1_COORDS_PRESET_NATIVE, &c) == SA1_OK && sa1_set_coords(ctx, &c) == SA1_OK);

    memset(tri, 0, sizeof tri);
    tri[0].v[1][0] = 1.0f;
    tri[0].v[2][1] = 1.0f;
    tri[1] = tri[0];
    CHECK(sa1_collision_set_static(ctx, tri, 2) == SA1_OK);
    CHECK(sa1_collision_object_add(ctx, tri, 1, p, rot, &id) == SA1_OK && id != 0);
    CHECK(sa1_collision_object_add(ctx, tri, 1, p, badrot, &id2) == SA1_ERR_INVALID_ARGUMENT && id2 == 0);
    CHECK(sa1_collision_object_move(ctx, id, p, NULL) == SA1_OK);
    CHECK(sa1_collision_stats(ctx, &st_tris, &objs, &otris) == SA1_OK && st_tris == 2 && objs == 1 && otris == 1);
    tri[1].v[0][0] = NAN;
    CHECK(sa1_collision_set_static(ctx, tri, 2) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_collision_object_remove(ctx, id) == SA1_OK && sa1_collision_object_remove(ctx, id) == SA1_ERR_NOT_FOUND);
    CHECK(sa1_collision_set_static(ctx, NULL, 0) == SA1_OK);
    sa1_shutdown(ctx);
}

static void test_api(void)
{
    sa1_host h;
    sa1_context *ctx = NULL;
    sa1_character_id id = 0;
    sa1_character_info ci;
    sa1_load_options lo;
    uint32_t cnt = 1;

    sa1_host_defaults(&h);
    h.api_version = (SA1_VERSION_MAJOR + 1) << 16;
    CHECK(sa1_init(&h, &ctx) == SA1_ERR_VERSION && ctx == NULL);
    sa1_host_defaults(&h);
    h.file_open = sa1_stdio_open;               /* incomplete set of file callbacks */
    CHECK(sa1_init(&h, &ctx) == SA1_ERR_INVALID_ARGUMENT);
    sa1_host_defaults(&h);
    h.alloc = count_alloc;                      /* alloc without free */
    CHECK(sa1_init(&h, &ctx) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_init(NULL, NULL) == SA1_ERR_INVALID_ARGUMENT);

    ctx = new_ctx();
    if (!ctx) {
        CHECK(0);
        return;
    }
    CHECK(sa1_character_create(ctx, SA1_CHAR_SONIC, NULL, 0.0f, &id) == SA1_ERR_NOT_LOADED && id == 0);
    CHECK(sa1_character_create(ctx, 42, NULL, 0.0f, &id) == SA1_ERR_INVALID_ARGUMENT);
    CHECK(sa1_character_destroy(ctx, 12345) == SA1_ERR_NOT_FOUND);
    CHECK(sa1_load_disc(ctx, "\\\\evil-server\\share\\sa1.gdi", NULL) == SA1_ERR_BAD_PATH);
    CHECK(sa1_load_disc(ctx, "//evil-server/share/sa1.gdi", NULL) == SA1_ERR_BAD_PATH);
    CHECK(sa1_load_disc(ctx, "\\\\.\\PhysicalDrive0", NULL) == SA1_ERR_BAD_PATH);
    CHECK(sa1_load_disc(ctx, "this_file_does_not_exist.gdi", NULL) == SA1_ERR_IO);
    memset(&lo, 0, sizeof lo);
    CHECK(sa1_load_disc(ctx, "x.gdi", &lo) == SA1_ERR_INVALID_ARGUMENT);      /* struct_size not set */
    memset(&ci, 0, sizeof ci);
    ci.struct_size = sizeof ci;
    CHECK(sa1_character_info_get(ctx, SA1_CHAR_SONIC, &ci) == SA1_ERR_NOT_LOADED && ci.loaded == 0 &&
          strcmp(ci.name, "sonic") == 0);
    /* a caller compiled against a smaller struct only gets its prefix */
    {
        union {
            sa1_character_info full;
            uint32_t           words[3];
        } u;
        memset(&u, 0, sizeof u);
        u.words[0] = 8;
        u.words[1] = 0xAAAAAAAAu;
        u.words[2] = 0xBBBBBBBBu;
        sa1_character_info_get(ctx, SA1_CHAR_TAILS, &u.full);
        CHECK(u.words[0] == 8 && u.words[1] == SA1_CHAR_TAILS && u.words[2] == 0xBBBBBBBBu);
    }
    CHECK(sa1_poll_sound_events(ctx, NULL, 0, &cnt) == SA1_OK && cnt == 0);
    CHECK(strcmp(sa1_status_string(SA1_ERR_NOT_IMPLEMENTED), "not implemented") == 0);
    CHECK(sa1_character_kind_from_name("knuckles") == SA1_CHAR_KNUCKLES && sa1_character_kind_from_name("x") == -1);
    CHECK(sa1_version() == ((SA1_VERSION_MAJOR << 16) | (SA1_VERSION_MINOR << 8) | SA1_VERSION_PATCH));
    sa1_shutdown(ctx);
}

/* ------------------------------------------------------------------------- */
/* Synthetic discs: 2048-byte, 2352-byte Mode 1 and Mode 2 Form 1 sectors    */
/* ------------------------------------------------------------------------- */

static void iso_dirrec(uint8_t *p, uint32_t *pos, const char *name, uint32_t lba, uint32_t size, int dir)
{
    uint32_t nl = (uint32_t)strlen(name), len;
    uint8_t *r = p + *pos;
    if (name[0] == 0 || name[0] == 1)
        nl = 1;
    len = 33 + nl + ((33 + nl) & 1);
    memset(r, 0, len);
    r[0] = (uint8_t)len;
    put32(r + 2, lba);
    put32(r + 10, size);
    r[25] = (uint8_t)(dir ? 2 : 0);
    r[32] = (uint8_t)nl;
    memcpy(r + 33, name, nl);
    *pos += len;
}

/* 25 user-data sectors of a tiny ISO9660 volume whose extents start at `base` */
static void build_iso(uint8_t *img, uint32_t base)
{
    static const uint8_t prs[6] = { 0x93, 'A', 'B', 0xFE, 0, 0 };
    uint32_t pos, i;
    memset(img, 0, 25 * 2048);
    img[16 * 2048] = 1;
    memcpy(img + 16 * 2048 + 1, "CD001", 5);
    img[16 * 2048 + 6] = 1;
    pos = 0;
    iso_dirrec(img + 16 * 2048 + 156, &pos, "", base + 20, 2048, 1);
    pos = 0;
    iso_dirrec(img + 20 * 2048, &pos, "", base + 20, 2048, 1);
    iso_dirrec(img + 20 * 2048, &pos, "\1", base + 20, 2048, 1);
    iso_dirrec(img + 20 * 2048, &pos, "1ST_READ.BIN;1", base + 22, 3000, 0);
    iso_dirrec(img + 20 * 2048, &pos, "SONICADV", base + 21, 2048, 1);
    pos = 0;
    iso_dirrec(img + 21 * 2048, &pos, "", base + 21, 2048, 1);
    iso_dirrec(img + 21 * 2048, &pos, "\1", base + 20, 2048, 1);
    iso_dirrec(img + 21 * 2048, &pos, "TEST.PRS;1", base + 24, 6, 0);
    for (i = 0; i < 3000; i++)
        img[22 * 2048 + i] = (uint8_t)(i * 7 + 3);
    memcpy(img + 24 * 2048, prs, sizeof prs);
}

static int write_track(const char *path, const uint8_t *img, uint32_t nsec, uint32_t ss, int mode2)
{
    FILE *f = fopen(path, "wb");
    uint8_t raw[2352];
    uint32_t i;
    int ok = 1;
    if (!f)
        return 0;
    for (i = 0; i < nsec && ok; i++) {
        if (ss == 2048) {
            ok = fwrite(img + (size_t)i * 2048, 1, 2048, f) == 2048;
        } else {
            memset(raw, 0x55, sizeof raw);
            raw[0] = 0;
            memset(raw + 1, 0xFF, 10);
            raw[11] = 0;
            raw[15] = (uint8_t)(mode2 ? 2 : 1);
            memcpy(raw + (mode2 ? 24 : 16), img + (size_t)i * 2048, 2048);
            ok = fwrite(raw, 1, 2352, f) == 2352;
        }
    }
    return (fclose(f) == 0) && ok;
}

static int write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    int ok;
    if (!f)
        return 0;
    ok = fwrite(text, 1, strlen(text), f) == strlen(text);
    return (fclose(f) == 0) && ok;
}

static void check_synthetic(sa1_context *ctx, const char *gdi_path, const char *what)
{
    sa1_disc d;
    uint8_t *data = NULL, *out = NULL;
    uint32_t size = 0, n = 0, i, ok = 1;
    sa1_status st = sa1_disc_open(ctx, gdi_path, &d);
    printf("  synthetic disc (%s): open %s\n", what, sa1_status_string(st));
    CHECK(st == SA1_OK);
    if (st != SA1_OK)
        return;
    CHECK(sa1_disc_read_file(&d, "1st_read.bin", &data, &size) == SA1_OK && size == 3000);
    for (i = 0; data && i < size; i++)
        if (data[i] != (uint8_t)(i * 7 + 3))
            ok = 0;
    CHECK(data && ok);
    sa1_mem_free(ctx, data);
    data = NULL;
    CHECK(sa1_disc_read_file(&d, "SONICADV/TEST.PRS", &data, &size) == SA1_OK && size == 6);
    CHECK(data && sa1_prs_decompress(ctx, data, size, 1024, &out, &n) == SA1_OK && n == 6 && memcmp(out, "ABABAB", 6) == 0);
    sa1_mem_free(ctx, out);
    sa1_mem_free(ctx, data);
    CHECK(sa1_disc_read_file(&d, "SONICADV/MISSING.PRS", &data, &size) == SA1_ERR_NOT_FOUND);
    sa1_disc_close(&d);
}

static void test_synthetic_discs(void)
{
    sa1_context *ctx = new_ctx();
    uint8_t *img = (uint8_t *)malloc(25 * 2048), *decoy = (uint8_t *)calloc(30, 2048);
    sa1_load_options lo;
    if (!ctx || !img || !decoy) {
        CHECK(0);
        free(img);
        free(decoy);
        sa1_shutdown(ctx);
        return;
    }
    tu_mkdir("unit_test_tmp");
    /* 1. single 2048-byte data track at LBA 0 */
    build_iso(img, 0);
    CHECK(write_track("unit_test_tmp/t1.iso", img, 25, 2048, 0));
    CHECK(write_text("unit_test_tmp/a.gdi", "1\r\n1 0 4 2048 t1.iso 0\r\n"));
    check_synthetic(ctx, "unit_test_tmp/a.gdi", "2048-byte sectors");
    /* 2. GD-ROM layout: data track 1, audio track 2 (never opened, file absent),
     *    the filesystem in the LAST data track at LBA 1000 with absolute extents */
    build_iso(img, 1000);
    CHECK(write_track("unit_test_tmp/track01.bin", decoy, 30, 2352, 0));
    CHECK(write_track("unit_test_tmp/track03.bin", img, 25, 2352, 0));
    CHECK(write_text("unit_test_tmp/b.gdi",
                     "3\n1 0 4 2352 track01.bin 0\n2 600 0 2352 track02.raw 0\n3 1000 4 2352 \"track03.bin\" 0\n"));
    check_synthetic(ctx, "unit_test_tmp/b.gdi", "2352-byte Mode 1, high-density area at LBA 1000");
    /* 3. Mode 2 Form 1 raw sectors */
    build_iso(img, 0);
    CHECK(write_track("unit_test_tmp/m2.bin", img, 25, 2352, 1));
    CHECK(write_text("unit_test_tmp/c.gdi", "1\n1 0 4 2352 m2.bin 0\n"));
    check_synthetic(ctx, "unit_test_tmp/c.gdi", "2352-byte Mode 2 Form 1");
    /* 4. a missing data track and a non-ISO data track */
    CHECK(write_text("unit_test_tmp/d.gdi", "1\n1 0 4 2352 nothere.bin 0\n"));
    CHECK(sa1_load_disc(ctx, "unit_test_tmp/d.gdi", NULL) == SA1_ERR_IO);
    CHECK(write_text("unit_test_tmp/e.gdi", "1\n1 0 4 2352 track01.bin 0\n"));
    CHECK(sa1_load_disc(ctx, "unit_test_tmp/e.gdi", NULL) == SA1_ERR_BAD_FORMAT);
    /* 5. the full loader refuses a disc whose 1ST_READ.BIN is not US v1.005 */
    memset(&lo, 0, sizeof lo);
    lo.struct_size = sizeof lo;
    CHECK(sa1_load_disc(ctx, "unit_test_tmp/a.gdi", &lo) == SA1_ERR_BAD_HASH);
    CHECK(sa1_disc_file_count(ctx) == 1);
    remove("unit_test_tmp/t1.iso");
    remove("unit_test_tmp/track01.bin");
    remove("unit_test_tmp/track03.bin");
    remove("unit_test_tmp/m2.bin");
    remove("unit_test_tmp/a.gdi");
    remove("unit_test_tmp/b.gdi");
    remove("unit_test_tmp/c.gdi");
    remove("unit_test_tmp/d.gdi");
    remove("unit_test_tmp/e.gdi");
    tu_rmdir("unit_test_tmp");
    free(img);
    free(decoy);
    sa1_shutdown(ctx);
}

/* ------------------------------------------------------------------------- */
/* Disc-backed robustness tests                                              */
/* ------------------------------------------------------------------------- */

/* host file layer that corrupts one byte at a given absolute file offset */
static uint64_t g_corrupt_at = (uint64_t)-1;
static int32_t corrupt_read(void *u, void *f, uint64_t off, void *buf, size_t n)
{
    int32_t r = sa1_stdio_read(u, f, off, buf, n);
    if (r == 0 && g_corrupt_at >= off && g_corrupt_at < off + n)
        ((uint8_t *)buf)[g_corrupt_at - off] ^= 0x5A;
    return r;
}

static void test_disc(const char *gdi_path, uint32_t runs)
{
    sa1_context *ctx = new_ctx();
    sa1_disc disc;
    uint8_t *first = NULL, *smot_raw = NULL, *smot = NULL;
    uint32_t size = 0, smot_raw_size = 0, smot_size = 0, it;
    char hex[41];
    sa1_load_options lo;
    sa1_host h;
    sa1_context *c2 = NULL;
    double t0;

    if (!ctx) {
        CHECK(0);
        return;
    }
    CHECK(sa1_disc_open(ctx, gdi_path, &disc) == SA1_OK);
    CHECK(sa1_disc_read_file(&disc, "1ST_READ.BIN", &first, &size) == SA1_OK && size == 6735488);
    CHECK(sa1_disc_read_file(&disc, "SONICADV/S_MOT.PRS", &smot_raw, &smot_raw_size) == SA1_OK);
    CHECK(sa1_disc_read_file(&disc, "SONICADV/NOPE.PRS", &smot, &smot_size) == SA1_ERR_NOT_FOUND);
    CHECK(sa1_disc_read_file(&disc, "SONICADV/../1ST_READ.BIN", &smot, &smot_size) == SA1_ERR_BAD_PATH);
    CHECK(sa1_disc_read_file(&disc, "SONICADV", &smot, &smot_size) == SA1_ERR_NOT_FOUND);
    sa1_disc_close(&disc);
    if (!first || !smot_raw) {
        sa1_shutdown(ctx);
        return;
    }
    sa1_sha1_hex(first, size, hex);
    CHECK(strcmp(hex, SA1_FIRST_READ_SHA1) == 0);
    CHECK(sa1_prs_decompress(ctx, smot_raw, smot_raw_size, SA1_MAX_UNPACKED_SIZE, &smot, &smot_size) == SA1_OK &&
          smot_size == 488608);

    /* Ninja deserializers on randomly corrupted images: must never crash */
    t0 = tu_now_ms();
    {
        uint8_t *img = (uint8_t *)malloc(size), *img2 = (uint8_t *)malloc(smot_size);
        uint32_t loaded = 0, failed = 0;
        for (it = 0; it < runs && img && img2; it++) {
            sa1_charset cs;
            sa1_image im[2];
            sa1_view v;
            sa1_status st;
            uint32_t k, flips = 1u + rnd() % 64u;
            int32_t obj;
            memcpy(img, first, size);
            memcpy(img2, smot, smot_size);
            for (k = 0; k < flips; k++) {
                /* model/motion/weld data of Sonic (0x8C1C0000..0x8C260000) and S_MOT */
                uint32_t off = 0x1B0000u + rnd() % 0xA0000u;
                img[off] = (uint8_t)rnd();
                img2[rnd() % smot_size] = (uint8_t)rnd();
            }
            memset(&cs, 0, sizeof cs);
            cs.arena.ctx = ctx;
            cs.models.elem = sizeof(sa1_model);
            cs.objects.elem = sizeof(sa1_object);
            cs.motions.elem = sizeof(sa1_motion);
            memset(im, 0, sizeof im);
            im[0].data = img; im[0].size = size; im[0].load_addr = 0x8C010000u; im[0].phys = 0x0C010000u; im[0].id = 0;
            im[1].data = img2; im[1].size = smot_size; im[1].load_addr = 0x0CC00000u; im[1].phys = 0x0CC00000u; im[1].id = 1;
            v.n = 2;
            v.img[0] = &im[0];
            v.img[1] = &im[1];
            obj = sa1_ninja_object(ctx, &cs, &v, 0x8C1CE76Cu, &st);
            if (obj >= 0) {
                loaded++;
                for (k = 0; k < 142; k++) {
                    const uint8_t *e = sa1_view_get(&v, 0x8C1C55A0u + 16u * k, 16, NULL), *pa;
                    int32_t o2;
                    if (!e || !(pa = sa1_view_get(&v, sa1_rd32(e), 8, NULL)))
                        continue;
                    o2 = sa1_ninja_object(ctx, &cs, &v, sa1_rd32(pa), &st);
                    if (o2 >= 0)
                        (void)sa1_ninja_motion(ctx, &cs, &v, sa1_rd32(pa + 4),
                                               SA1_VEC(cs.objects, sa1_object)[o2].num_nodes, &st);
                }
                (void)sa1_ninja_welds(ctx, &cs, &v, 0x8C1C5FA4u);
            } else {
                failed++;
            }
            sa1_charset_free(ctx, &cs);
        }
        printf("  corrupted-image fuzz: %u runs, %u main trees loaded, %u rejected (%.0f ms)\n", (unsigned)runs,
               (unsigned)loaded, (unsigned)failed, tu_now_ms() - t0);
        CHECK(loaded + failed == runs);
        free(img);
        free(img2);
    }
    sa1_mem_free(ctx, first);
    sa1_mem_free(ctx, smot_raw);
    sa1_mem_free(ctx, smot);

    /* a corrupted 1ST_READ.BIN is rejected by its hash (LBA 545711, track03 at LBA 45000) */
    sa1_host_defaults(&h);
    h.file_open = sa1_stdio_open;
    h.file_size = sa1_stdio_size;
    h.file_read = corrupt_read;
    h.file_close = sa1_stdio_close;
    h.log_level = SA1_LOG_ERROR + 1;
    if (sa1_init(&h, &c2) == SA1_OK) {
        memset(&lo, 0, sizeof lo);
        lo.struct_size = sizeof lo;
        lo.characters = 1u << SA1_CHAR_SONIC;
        g_corrupt_at = (uint64_t)(545711u + 100u - 45000u) * 2352u + 16u + 77u;
        CHECK(sa1_load_disc(c2, gdi_path, &lo) == SA1_ERR_BAD_HASH);
        g_corrupt_at = (uint64_t)-1;
        CHECK(sa1_load_disc(c2, gdi_path, &lo) == SA1_OK);
        sa1_shutdown(c2);
    }
    sa1_shutdown(ctx);
}

int main(int argc, char **argv)
{
    sa1_context *ctx;
    long a0, f0;
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("libsa1 %s unit tests\n", sa1_version_string());
    printf("- %s\n", "test_sha1");
    test_sha1();
    printf("- %s\n", "test_paths");
    test_paths();
    printf("- %s\n", "test_gdi");
    test_gdi();
    printf("- %s\n", "test_pvr");
    test_pvr();
    printf("- %s\n", "test_pvm");
    test_pvm();
    a0 = g_allocs;
    f0 = g_frees;
    ctx = new_ctx();
    if (ctx) {
        printf("- test_prs\n");
        test_prs(ctx);
        sa1_shutdown(ctx);
    }
    printf("- %s\n", "test_coords_collision");
    test_coords_collision();
    printf("- %s\n", "test_api");
    test_api();
    printf("- %s\n", "test_synthetic_discs");
    test_synthetic_discs();
    if (argc > 1)
        test_disc(argv[1], argc > 2 ? (uint32_t)atoi(argv[2]) : 300u);
    printf("allocations %ld, frees %ld\n", g_allocs - a0, g_frees - f0);
    CHECK(g_allocs - a0 == g_frees - f0);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}