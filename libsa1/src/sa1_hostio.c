/*
 * Default host services: stdio file access with 64-bit offsets (GD-ROM track files
 * are larger than 2 GB in some dumps) and a monotonic clock.
 */
#if !defined(_WIN32)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef _FILE_OFFSET_BITS
#    define _FILE_OFFSET_BITS 64
#  endif
#endif

#include "sa1_internal.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/types.h>
#  include <time.h>
#endif

void *sa1_stdio_open(void *user, const char *path)
{
    (void)user;
    if (!path || !*path)
        return NULL;
#if defined(_WIN32)
    {
        /* UTF-8 -> UTF-16 so that non-ASCII paths work */
        wchar_t wbuf[1024];
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wbuf, (int)(sizeof wbuf / sizeof wbuf[0]));
        if (n <= 0)
            return NULL;
        return (void *)_wfopen(wbuf, L"rb");
    }
#else
    return (void *)fopen(path, "rb");
#endif
}

static int seek64(FILE *f, uint64_t off, int whence)
{
#if defined(_WIN32)
    return _fseeki64(f, (__int64)off, whence);
#else
    return fseeko(f, (off_t)off, whence);
#endif
}

int32_t sa1_stdio_size(void *user, void *file, uint64_t *size)
{
    FILE *f = (FILE *)file;
    (void)user;
    if (!f || !size || seek64(f, 0, SEEK_END) != 0)
        return -1;
#if defined(_WIN32)
    {
        __int64 p = _ftelli64(f);
        if (p < 0)
            return -1;
        *size = (uint64_t)p;
    }
#else
    {
        off_t p = ftello(f);
        if (p < 0)
            return -1;
        *size = (uint64_t)p;
    }
#endif
    return 0;
}

int32_t sa1_stdio_read(void *user, void *file, uint64_t offset, void *buffer, size_t size)
{
    FILE *f = (FILE *)file;
    (void)user;
    if (!f || (!buffer && size))
        return -1;
    if (offset > (uint64_t)INT64_MAX || seek64(f, offset, SEEK_SET) != 0)
        return -1;
    return fread(buffer, 1, size, f) == size ? 0 : -1;
}

void sa1_stdio_close(void *user, void *file)
{
    (void)user;
    if (file)
        fclose((FILE *)file);
}

double sa1_now_ms(void)
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