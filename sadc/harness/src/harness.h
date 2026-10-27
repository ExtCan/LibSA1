/* harness.h - shared declarations of the harness command-line tools. */
#ifndef HARNESS_H
#define HARNESS_H

#include "sh4.h"

extern char g_root[1024];        /* repository root (the sadc/ directory) */
extern int g_rom_loaded;

int  harness_load_rom(void);     /* baserom/1ST_READ.BIN at 0x8C010000 + symbols */
int  harness_load_overlay(const char *name);
uint32_t harness_parse_addr(const char *s);     /* symbol name, func_XXXXXXXX or hex */
size_t harness_ram_restore(const uint8_t *snap); /* copy back the 4 KB blocks that differ */
size_t harness_ram_restore_dirty(const uint8_t *snap); /* blocks written by the interpreter only */
void harness_print_fault(const char *what);
uint32_t harness_rand(void);
void harness_srand(uint32_t seed);

/* overlay table generated from baserom/overlays.json */
typedef struct sh4_overlay { const char *name; const char *bin; uint32_t load; } sh4_overlay;
extern const sh4_overlay sh4_overlays[];
extern const int sh4_overlay_count;

/* all functions of 1ST_READ (from build/1ST_READ.xrefs.json) */
typedef struct sh4_funcinfo { uint32_t addr, size; int leaf; const char *name; } sh4_funcinfo;
extern const sh4_funcinfo sh4_functions[];
extern const int sh4_function_count;

/* HLE defaults (hle.c) */
void hle_install_defaults(int verbose);
void hle_install_bios(void);
extern int hle_verbose;

int cmd_selftest(int argc, char **argv);
int cmd_rtlib(int argc, char **argv);
int cmd_leafsmoke(int argc, char **argv);
int cmd_call(int argc, char **argv);
int cmd_diff(int argc, char **argv);
int cmd_list(int argc, char **argv);
int cmd_bench(int argc, char **argv);
int cmd_boot(int argc, char **argv);
int cmd_nativetest(int argc, char **argv);
int cmd_trace(int argc, char **argv);       /* t_sim.c: reference simulator */
int cmd_menu(int argc, char **argv);        /* t_sim.c: the ADVERTISE menu overlay (round 0.9.2, CS2) */
int boot_warm_state(int frames);      /* t_boot.c: game init for warm diff baselines */
int boot_stub_count(void);

#endif