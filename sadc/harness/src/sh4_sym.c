/* sh4_sym.c - name <-> address lookup from build/1ST_READ.symbols.txt
 * ("name = 0xADDR;" lines). */
#include "sh4.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *name; uint32_t addr; } sym_ent;
static sym_ent *syms;
static int nsyms, capsyms;
static sym_ent **by_name;    /* hash table */
static int hcap;

static uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s)
        h = (h ^ (uint8_t)*s++) * 16777619u;
    return h;
}

int sh4_symbols_load(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    int i;
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char name[256];
        unsigned int addr;
        if (sscanf(line, " %255[A-Za-z0-9_$.] = 0x%x", name, &addr) != 2)
            continue;
        if (nsyms == capsyms) {
            capsyms = capsyms ? capsyms * 2 : 4096;
            syms = (sym_ent *)realloc(syms, sizeof(sym_ent) * (size_t)capsyms);
        }
        syms[nsyms].name = strdup(name);
        syms[nsyms].addr = addr;
        nsyms++;
    }
    fclose(f);
    hcap = 1;
    while (hcap < nsyms * 2)
        hcap <<= 1;
    by_name = (sym_ent **)calloc((size_t)hcap, sizeof(sym_ent *));
    for (i = 0; i < nsyms; i++) {
        uint32_t h = hash_str(syms[i].name) & (uint32_t)(hcap - 1);
        while (by_name[h])
            h = (h + 1) & (uint32_t)(hcap - 1);
        by_name[h] = &syms[i];
    }
    return nsyms;
}

uint32_t sh4_sym_addr(const char *name)
{
    uint32_t h;
    char *end;
    unsigned long v;
    if (!name || !*name)
        return 0;
    if (by_name) {
        h = hash_str(name) & (uint32_t)(hcap - 1);
        while (by_name[h]) {
            if (!strcmp(by_name[h]->name, name))
                return by_name[h]->addr;
            h = (h + 1) & (uint32_t)(hcap - 1);
        }
    }
    /* func_8Cxxxxxx / D_ / B_ / raw hex */
    {
        const char *p = strrchr(name, '_');
        p = p ? p + 1 : name;
        if (!strncmp(p, "0x", 2) || !strncmp(p, "0X", 2))
            p += 2;
        v = strtoul(p, &end, 16);
        if (*end == 0 && end - p == 8)
            return (uint32_t)v;
    }
    return 0;
}

static sym_ent *by_addr;    /* sorted copy */

static int cmp_addr(const void *a, const void *b)
{
    uint32_t x = ((const sym_ent *)a)->addr, y = ((const sym_ent *)b)->addr;
    return x < y ? -1 : x > y;
}

static void sort_addr(void)
{
    if (by_addr || !nsyms)
        return;
    by_addr = (sym_ent *)malloc(sizeof(sym_ent) * (size_t)nsyms);
    memcpy(by_addr, syms, sizeof(sym_ent) * (size_t)nsyms);
    qsort(by_addr, (size_t)nsyms, sizeof(sym_ent), cmp_addr);
}

const char *sh4_sym_near(uint32_t addr, uint32_t *off)
{
    int lo = 0, hi;
    sort_addr();
    if (!by_addr)
        return NULL;
    hi = nsyms - 1;
    if (addr < by_addr[0].addr)
        return NULL;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (by_addr[mid].addr <= addr)
            lo = mid;
        else
            hi = mid - 1;
    }
    if (off)
        *off = addr - by_addr[lo].addr;
    return by_addr[lo].name;
}

const char *sh4_sym_name(uint32_t addr)
{
    uint32_t off;
    const char *n = sh4_sym_near(addr, &off);
    return (n && off == 0) ? n : NULL;
}