/*
 * Disc access: path validation, .gdi parsing, raw/cooked sector reads and ISO9660
 * single-file lookup in the high-density area of a GD-ROM dump.
 *
 * Security rules (see sadc/docs/research/critique.md 5.2):
 *   - the .gdi path itself must not be a UNC path (\\server\share, //server), a
 *     \\?\ or \\.\ device-namespace path, or name a DOS device;
 *   - track files named inside the .gdi are accepted only as bare file names and are
 *     resolved in the .gdi's own directory;
 *   - all sizes, counts and LBAs read from the .gdi or the ISO9660 structures are
 *     range-checked before use.
 */
#include "sa1_internal.h"

#include <stdio.h>

#define RAW_CHUNK 64u   /* sectors per host read */

/* ------------------------------------------------------------------------- */
/* Paths                                                                     */
/* ------------------------------------------------------------------------- */

static int is_sep(char c) { return c == '/' || c == '\\'; }

static int ieq_n(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y)
            return 0;
    }
    return 1;
}

/* Is [s, s+n) (one path component) a reserved DOS device name, with or without an
 * extension?  CON, PRN, AUX, NUL, COM0-9, LPT0-9, CONIN$, CONOUT$, CLOCK$. */
static int is_device_name(const char *s, size_t n)
{
    static const char *const fixed[] = { "CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$" };
    size_t base = 0, i;
    while (base < n && s[base] != '.')
        base++;
    while (base > 0 && s[base - 1] == ' ')
        base--;
    for (i = 0; i < sizeof fixed / sizeof fixed[0]; i++)
        if (strlen(fixed[i]) == base && ieq_n(s, fixed[i], base))
            return 1;
    if (base == 4 && (ieq_n(s, "COM", 3) || ieq_n(s, "LPT", 3)) && s[3] >= '0' && s[3] <= '9')
        return 1;
    /* COM/LPT followed by a superscript digit (U+00B9, U+00B2, U+00B3 in UTF-8) */
    if (base == 5 && (ieq_n(s, "COM", 3) || ieq_n(s, "LPT", 3)) && (uint8_t)s[3] == 0xC2 &&
        ((uint8_t)s[4] == 0xB9 || (uint8_t)s[4] == 0xB2 || (uint8_t)s[4] == 0xB3))
        return 1;
    return 0;
}

sa1_status sa1_path_check_host(const char *path)
{
    size_t len, i, start;
    if (!path || !*path)
        return SA1_ERR_BAD_PATH;
    len = strlen(path);
    if (len > 4000)
        return SA1_ERR_BAD_PATH;
    if (is_sep(path[0]) && is_sep(path[1]))
        return SA1_ERR_BAD_PATH;            /* UNC, \\?\, \\.\, //server */
    if (path[0] == '\\' && path[1] == '?' && path[2] == '?')
        return SA1_ERR_BAD_PATH;            /* \??\ NT object path */
    for (i = 0; i < len; i++) {
        uint8_t c = (uint8_t)path[i];
        if (c < 0x20 || c == 0x7F)
            return SA1_ERR_BAD_PATH;
        if (c == ':' && i != 1)
            return SA1_ERR_BAD_PATH;        /* only a drive letter colon; no ADS */
    }
    if (path[1] == ':' && !((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')))
        return SA1_ERR_BAD_PATH;
    /* no component may be a device name */
    start = (len >= 2 && path[1] == ':') ? 2 : 0;
    for (i = start; i <= len; i++) {
        if (i == len || is_sep(path[i])) {
            if (i > start && is_device_name(path + start, i - start))
                return SA1_ERR_BAD_PATH;
            start = i + 1;
        }
    }
    return SA1_OK;
}

sa1_status sa1_path_check_bare(const char *name)
{
    size_t len, i;
    int only_dots = 1;
    if (!name || !*name)
        return SA1_ERR_BAD_PATH;
    len = strlen(name);
    if (len > 255)
        return SA1_ERR_BAD_PATH;
    for (i = 0; i < len; i++) {
        uint8_t c = (uint8_t)name[i];
        if (c < 0x20 || c >= 0x7F)
            return SA1_ERR_BAD_PATH;        /* ASCII only */
        if (c == '/' || c == '\\' || c == ':' || c == '"' || c == '<' || c == '>' || c == '|' ||
            c == '?' || c == '*')
            return SA1_ERR_BAD_PATH;
        if (c != '.' && c != ' ')
            only_dots = 0;
    }
    if (only_dots)
        return SA1_ERR_BAD_PATH;            /* ".", "..", "...", " " */
    if (name[len - 1] == '.' || name[len - 1] == ' ')
        return SA1_ERR_BAD_PATH;            /* Windows would strip these */
    if (is_device_name(name, len))
        return SA1_ERR_BAD_PATH;
    return SA1_OK;
}

/* ------------------------------------------------------------------------- */
/* .gdi parsing                                                              */
/* ------------------------------------------------------------------------- */

typedef struct gdi_lexer {
    const char *p, *end;
} gdi_lexer;

/* Next token on the current line; returns 0 at end of line/text.  Quoted tokens
 * ("track 03.bin") may contain spaces. */
static int gdi_token(gdi_lexer *lx, char *out, size_t cap)
{
    size_t n = 0;
    while (lx->p < lx->end && (*lx->p == ' ' || *lx->p == '\t'))
        lx->p++;
    if (lx->p >= lx->end || *lx->p == '\r' || *lx->p == '\n')
        return 0;
    if (*lx->p == '"') {
        lx->p++;
        while (lx->p < lx->end && *lx->p != '"' && *lx->p != '\r' && *lx->p != '\n') {
            if (n + 1 >= cap)
                return -1;
            out[n++] = *lx->p++;
        }
        if (lx->p >= lx->end || *lx->p != '"')
            return -1;
        lx->p++;
    } else {
        while (lx->p < lx->end && *lx->p != ' ' && *lx->p != '\t' && *lx->p != '\r' && *lx->p != '\n') {
            if (n + 1 >= cap)
                return -1;
            out[n++] = *lx->p++;
        }
    }
    out[n] = 0;
    return 1;
}

static void gdi_next_line(gdi_lexer *lx)
{
    while (lx->p < lx->end && *lx->p != '\n')
        lx->p++;
    if (lx->p < lx->end)
        lx->p++;
}

static int gdi_blank_line(const gdi_lexer *lx)
{
    const char *q = lx->p;
    while (q < lx->end && (*q == ' ' || *q == '\t' || *q == '\r'))
        q++;
    return q < lx->end && *q == '\n';
}

static int parse_u32(const char *s, uint32_t *out)
{
    uint64_t v = 0;
    if (!*s)
        return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return 0;
        v = v * 10u + (uint64_t)(*s - '0');
        if (v > 0xFFFFFFFFu)
            return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

sa1_status sa1_gdi_parse(const char *text, size_t len, sa1_gdi_track *tracks, uint32_t max_tracks,
                         uint32_t *num_tracks)
{
    gdi_lexer lx;
    char tok[260];
    uint32_t count, i, prev_num = 0;
    if (!text || !tracks || !num_tracks)
        return SA1_ERR_INVALID_ARGUMENT;
    *num_tracks = 0;
    lx.p = text;
    lx.end = text + len;
    if (len >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF)
        lx.p += 3;
    while (lx.p < lx.end && gdi_blank_line(&lx))
        gdi_next_line(&lx);
    if (gdi_token(&lx, tok, sizeof tok) != 1 || !parse_u32(tok, &count) || count == 0 ||
        count > SA1_MAX_TRACKS || count > max_tracks)
        return SA1_ERR_BAD_FORMAT;
    gdi_next_line(&lx);
    for (i = 0; i < count; i++) {
        sa1_gdi_track *t = &tracks[i];
        uint32_t offset = 0;
        int r;
        while (lx.p < lx.end && gdi_blank_line(&lx))
            gdi_next_line(&lx);
        memset(t, 0, sizeof *t);
        if (gdi_token(&lx, tok, sizeof tok) != 1 || !parse_u32(tok, &t->number))
            return SA1_ERR_BAD_FORMAT;
        if (gdi_token(&lx, tok, sizeof tok) != 1 || !parse_u32(tok, &t->lba))
            return SA1_ERR_BAD_FORMAT;
        if (gdi_token(&lx, tok, sizeof tok) != 1 || !parse_u32(tok, &t->type))
            return SA1_ERR_BAD_FORMAT;
        if (gdi_token(&lx, tok, sizeof tok) != 1 || !parse_u32(tok, &t->sector_size))
            return SA1_ERR_BAD_FORMAT;
        if (gdi_token(&lx, t->name, sizeof t->name) != 1)
            return SA1_ERR_BAD_FORMAT;
        r = gdi_token(&lx, tok, sizeof tok);
        if (r < 0 || (r == 1 && !parse_u32(tok, &offset)))
            return SA1_ERR_BAD_FORMAT;
        if (r == 1 && gdi_token(&lx, tok, sizeof tok) != 0)
            return SA1_ERR_BAD_FORMAT;      /* trailing garbage */
        gdi_next_line(&lx);
        if (t->number == 0 || t->number > SA1_MAX_TRACKS || t->number <= prev_num)
            return SA1_ERR_BAD_FORMAT;
        prev_num = t->number;
        if (t->type != 0 && t->type != 4)
            return SA1_ERR_BAD_FORMAT;
        if (t->sector_size != 2048 && t->sector_size != 2352)
            return SA1_ERR_UNSUPPORTED;
        if (t->lba > 100000000u)
            return SA1_ERR_BAD_FORMAT;
        if (sa1_path_check_bare(t->name) != SA1_OK)
            return SA1_ERR_BAD_PATH;
        (void)offset;   /* the last column is a byte offset that dumps always set to 0 */
    }
    *num_tracks = count;
    return SA1_OK;
}

/* ------------------------------------------------------------------------- */
/* Disc                                                                      */
/* ------------------------------------------------------------------------- */

void sa1_disc_close(sa1_disc *d)
{
    uint32_t i;
    if (!d || !d->ctx)
        return;
    for (i = 0; i < d->num_tracks; i++)
        sa1_file_close(d->ctx, d->tracks[i].file);
    sa1_mem_free(d->ctx, d->raw);
    sa1_mem_free(d->ctx, d->dir);
    memset(d, 0, sizeof *d);
}

static const sa1_disc_track *find_track(const sa1_disc *d, uint32_t lba)
{
    uint32_t i;
    for (i = 0; i < d->num_tracks; i++) {
        const sa1_disc_track *t = &d->tracks[i];
        if (t->file && lba >= t->info.lba && lba - t->info.lba < t->num_sectors)
            return t;
    }
    return NULL;
}

sa1_status sa1_disc_read_sectors(sa1_disc *d, uint32_t lba, uint32_t count, uint8_t *out)
{
    while (count) {
        const sa1_disc_track *t = find_track(d, lba);
        uint32_t rel, n, i, ss;
        sa1_status st;
        if (!t)
            return SA1_ERR_BAD_FORMAT;          /* LBA outside every data track */
        ss = t->info.sector_size;
        rel = lba - t->info.lba;
        n = t->num_sectors - rel;
        if (n > count)
            n = count;
        if (n > RAW_CHUNK)
            n = RAW_CHUNK;
        st = sa1_file_read(d->ctx, t->file, (uint64_t)rel * ss, d->raw, (size_t)n * ss);
        if (st != SA1_OK)
            return st;
        for (i = 0; i < n; i++) {
            const uint8_t *s = d->raw + (size_t)i * ss;
            uint32_t off = 0;
            if (ss == 2352) {
                /* Mode 1 user data at +16; Mode 2 Form 1 at +24 (sync + header + subheader) */
                static const uint8_t sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
                off = 16;
                if (memcmp(s, sync, 12) == 0 && s[15] == 2)
                    off = 24;
            }
            memcpy(out + (size_t)i * 2048u, s + off, 2048);
        }
        out += (size_t)n * 2048u;
        lba += n;
        count -= n;
    }
    return SA1_OK;
}

sa1_status sa1_disc_open(sa1_context *ctx, const char *gdi_path, sa1_disc *d)
{
    void *f;
    uint64_t fsize;
    char *text = NULL;
    sa1_gdi_track parsed[SA1_MAX_TRACKS];
    uint32_t ntracks = 0, i, best_lba = 0;
    int have_hd = 0;
    size_t dirlen = 0;
    sa1_status st;
    uint8_t pvd[2048];

    memset(d, 0, sizeof *d);
    d->ctx = ctx;
    if (sa1_path_check_host(gdi_path) != SA1_OK)
        return sa1_fail(ctx, SA1_ERR_BAD_PATH, "rejected .gdi path (UNC, device or invalid path)");

    f = sa1_file_open(ctx, gdi_path);
    if (!f)
        return sa1_fail(ctx, SA1_ERR_IO, "cannot open .gdi file");
    st = sa1_file_size(ctx, f, &fsize);
    if (st == SA1_OK && (fsize == 0 || fsize > SA1_MAX_GDI_SIZE))
        st = SA1_ERR_BAD_FORMAT;
    if (st == SA1_OK) {
        text = (char *)sa1_mem_alloc(ctx, (size_t)fsize + 1u);
        st = text ? sa1_file_read(ctx, f, 0, text, (size_t)fsize) : SA1_ERR_OUT_OF_MEMORY;
    }
    sa1_file_close(ctx, f);
    if (st == SA1_OK)
        st = sa1_gdi_parse(text, (size_t)fsize, parsed, SA1_MAX_TRACKS, &ntracks);
    sa1_mem_free(ctx, text);
    if (st != SA1_OK)
        return sa1_fail(ctx, st, "cannot parse .gdi file (%s)", sa1_status_string(st));

    /* directory of the .gdi, including the trailing separator */
    {
        size_t k = strlen(gdi_path);
        while (k > 0 && !is_sep(gdi_path[k - 1]) && !(k == 2 && gdi_path[1] == ':'))
            k--;
        dirlen = k;
    }

    d->raw = (uint8_t *)sa1_mem_alloc(ctx, (size_t)RAW_CHUNK * 2352u);
    if (!d->raw)
        return SA1_ERR_OUT_OF_MEMORY;

    for (i = 0; i < ntracks; i++) {
        sa1_disc_track *t = &d->tracks[i];
        char path[4400];
        size_t nl = strlen(parsed[i].name);
        t->info = parsed[i];
        d->num_tracks = i + 1;
        if (parsed[i].type != 4)
            continue;                           /* audio tracks are never needed */
        if (dirlen + nl + 1 > sizeof path) {
            sa1_disc_close(d);
            return sa1_fail(ctx, SA1_ERR_BAD_PATH, "track path too long");
        }
        memcpy(path, gdi_path, dirlen);
        memcpy(path + dirlen, parsed[i].name, nl + 1);
        t->file = sa1_file_open(ctx, path);
        if (!t->file) {
            st = sa1_fail(ctx, SA1_ERR_IO, "cannot open track %u (%s)", (unsigned)t->info.number, t->info.name);
            sa1_disc_close(d);
            return st;
        }
        if (sa1_file_size(ctx, t->file, &fsize) != SA1_OK) {
            sa1_disc_close(d);
            return sa1_fail(ctx, SA1_ERR_IO, "cannot get the size of track %u", (unsigned)t->info.number);
        }
        if (fsize / t->info.sector_size > 0xFFFFFFFFu - t->info.lba) {
            sa1_disc_close(d);
            return sa1_fail(ctx, SA1_ERR_BAD_FORMAT, "track %u is too large", (unsigned)t->info.number);
        }
        t->num_sectors = (uint32_t)(fsize / t->info.sector_size);
        if (fsize % t->info.sector_size)
            sa1_log(ctx, SA1_LOG_WARN, "track %u size is not a multiple of %u", (unsigned)t->info.number,
                    (unsigned)t->info.sector_size);
        if (!have_hd || t->info.lba >= best_lba) {
            have_hd = 1;
            best_lba = t->info.lba;
            d->hd = i;
        }
    }
    if (!have_hd) {
        sa1_disc_close(d);
        return sa1_fail(ctx, SA1_ERR_BAD_FORMAT, "the .gdi has no data track");
    }

    st = sa1_disc_read_sectors(d, d->tracks[d->hd].info.lba + 16u, 1, pvd);
    if (st != SA1_OK || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0 || pvd[156] < 34) {
        sa1_disc_close(d);
        return sa1_fail(ctx, st != SA1_OK ? st : SA1_ERR_BAD_FORMAT,
                        "no ISO9660 primary volume descriptor in the high-density area");
    }
    d->root_lba = sa1_rd32(pvd + 156 + 2);
    d->root_size = sa1_rd32(pvd + 156 + 10);
    return SA1_OK;
}

/* ------------------------------------------------------------------------- */
/* ISO9660                                                                   */
/* ------------------------------------------------------------------------- */

static sa1_status load_dir(sa1_disc *d, uint32_t lba, uint32_t size)
{
    uint32_t nsec;
    sa1_status st;
    if (d->dir && d->dir_lba == lba && d->dir_size == size)
        return SA1_OK;
    if (size == 0 || size > SA1_MAX_DIR_SIZE)
        return SA1_ERR_BAD_FORMAT;
    nsec = (size + 2047u) / 2048u;
    sa1_mem_free(d->ctx, d->dir);
    d->dir = (uint8_t *)sa1_mem_alloc(d->ctx, (size_t)nsec * 2048u);
    d->dir_size = 0;
    if (!d->dir)
        return SA1_ERR_OUT_OF_MEMORY;
    st = sa1_disc_read_sectors(d, lba, nsec, d->dir);
    if (st != SA1_OK) {
        sa1_mem_free(d->ctx, d->dir);
        d->dir = NULL;
        return st;
    }
    d->dir_lba = lba;
    d->dir_size = size;
    return SA1_OK;
}

/* Finds `name` (case-insensitive, ";1" version and trailing '.' ignored) in the cached
 * directory.  Returns SA1_OK with the record fields, or SA1_ERR_NOT_FOUND. */
static sa1_status dir_find(const sa1_disc *d, const char *name, size_t nlen, uint32_t *lba, uint32_t *size,
                           uint8_t *flags)
{
    uint32_t pos = 0;
    while (pos < d->dir_size) {
        const uint8_t *r = d->dir + pos;
        uint32_t rlen = r[0], nl, k;
        if (rlen == 0) {
            pos = (pos / 2048u + 1u) * 2048u;   /* records do not span sectors */
            continue;
        }
        if (rlen < 34 || pos + rlen > d->dir_size)
            return SA1_ERR_BAD_FORMAT;
        nl = r[32];
        if (33u + nl > rlen)
            return SA1_ERR_BAD_FORMAT;
        for (k = 0; k < nl && r[33 + k] != ';'; k++)
            ;
        while (k > 0 && r[33 + k - 1] == '.')
            k--;
        if (!(nl == 1 && (r[33] == 0 || r[33] == 1)) && k == nlen && ieq_n((const char *)r + 33, name, nlen)) {
            *lba = sa1_rd32(r + 2);
            *size = sa1_rd32(r + 10);
            *flags = r[25];
            if (r[26] != 0 || r[27] != 0 || (r[25] & 0x80))
                return SA1_ERR_UNSUPPORTED;     /* interleaved or multi-extent file */
            return SA1_OK;
        }
        pos += rlen;
    }
    return SA1_ERR_NOT_FOUND;
}

sa1_status sa1_disc_read_file(sa1_disc *d, const char *path, uint8_t **data, uint32_t *size)
{
    uint32_t lba = d->root_lba, fsize = d->root_size, nsec;
    uint8_t flags = 2;
    const char *p = path;
    uint8_t *buf;
    sa1_status st;

    *data = NULL;
    *size = 0;
    while (*p) {
        const char *e = p;
        size_t n, i;
        while (*e && *e != '/')
            e++;
        n = (size_t)(e - p);
        if (n == 0 || n > 64 || (p[0] == '.' && (n == 1 || (n == 2 && p[1] == '.'))))
            return SA1_ERR_BAD_PATH;
        for (i = 0; i < n; i++) {
            char c = p[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
                  c == '.' || c == '-'))
                return SA1_ERR_BAD_PATH;
        }
        if (!(flags & 2))
            return SA1_ERR_NOT_FOUND;           /* a file used as a directory */
        st = load_dir(d, lba, fsize);
        if (st != SA1_OK)
            return st;
        st = dir_find(d, p, n, &lba, &fsize, &flags);
        if (st != SA1_OK)
            return st;
        p = *e ? e + 1 : e;
    }
    if (flags & 2)
        return SA1_ERR_NOT_FOUND;               /* a directory, not a file */
    if (fsize > SA1_MAX_FILE_SIZE)
        return SA1_ERR_LIMIT;
    nsec = (fsize + 2047u) / 2048u;
    buf = (uint8_t *)sa1_mem_alloc(d->ctx, nsec ? (size_t)nsec * 2048u : 1u);
    if (!buf)
        return SA1_ERR_OUT_OF_MEMORY;
    if (nsec && (st = sa1_disc_read_sectors(d, lba, nsec, buf)) != SA1_OK) {
        sa1_mem_free(d->ctx, buf);
        return st;
    }
    *data = buf;
    *size = fsize;
    return SA1_OK;
}