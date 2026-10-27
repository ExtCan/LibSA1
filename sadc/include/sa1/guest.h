/*
 * sa1/guest.h - the GUEST-ARENA (GA) dialect that tools/pclower.py lowers decompiled game C to
 * (docs/libsa1_integration.md sections 2.1-2.4 and 3).
 *
 * Every game struct keeps its DC layout on every target: a pointer stored in memory (struct field,
 * global, array element, pointee) is a 32-bit guest word, the same value the Dreamcast stores.
 * Native host pointers exist only in locals, parameters and return values.
 *
 *   sa1_gp   data pointer held in guest memory            (R1)
 *   sa1_gfn  function pointer: a DC code address          (R1, R5; in memory AND in values)
 *
 * The lowered text is the same for both profiles; only the definitions below differ:
 *
 *   ga64 (default)   libsa1. The world's 16 MB "DC RAM" arena can be anywhere in the host address
 *                    space. decode() goes through the per-process segment table sa1g_seg[32]
 *                    (copied from the world at tick entry), encode() maps an arena address back to
 *                    the DC convention (P1 0x8C.. below 0x900000, P0 0x0C.. for the overlays) and
 *                    every other address through sa1g_encode_slow().
 *   ga32 (-DSA1_GA32) the 32-bit harness (sa1h, LARGE_ADDRESS_AWARE). DC RAM is mapped at
 *                    0x8C000000 with the P0/P2 mirrors, so a guest word is its own host address:
 *                    decode and encode are the identity (exact mirrors are preserved); pcdata and
 *                    stack objects are plain host memory the interpreter reaches by pass-through.
 *
 * Guest segments ((g >> 24) & 0x1F, design 2.1): 0x00 NULL; 0x08-0x0B the static collision segment
 * (64 MB, shared by the worlds of a context); 0x0C the arena; 0x13 trap (negated arena pointers,
 * COLOBJ_HEAD_LINK); 0x18 the per-world extension segment (R4 pcdata at its start, then the world's
 * own objects); 0x1D the host-stack window (registered at tick entry: base = rsp_entry - 16 MB);
 * every other segment a trap reservation. sa1g_encode_segment() below is the canonical encoding of
 * the non-arena segments; the world's sa1g_encode_slow() uses it with its span table.
 *
 * The world (LIB/src/world) or the harness provides: sa1g_seg, sa1g_ram_base, sa1g_encode_slow, the
 * three fault entry points and the runtime functions/data of the "rt" section. pclower generates
 * the code map (sa1g_fntab.c: sa1_fn, sa1_fn_addr, sa1_fn_name, sa1g_fn_lookup) and the pcdata
 * placement (sa1g_pcdata.c: sa1g_pcd_size, sa1g_pcd_init).
 *
 * This header is included first by every lowered file and must not depend on common.h.
 */
#ifndef SA1_GUEST_H
#define SA1_GUEST_H

#include <stddef.h>
#include <stdint.h>

typedef struct sa1_gp {
    uint32_t v;                 /* guest data address (0 = NULL) */
} sa1_gp;

typedef struct sa1_gfn {
    uint32_t v;                 /* DC code address (0 = NULL) */
} sa1_gfn;

_Static_assert(sizeof(sa1_gp) == 4, "sa1_gp must be one guest word");
_Static_assert(sizeof(sa1_gfn) == 4, "sa1_gfn must be one guest word");
_Static_assert(_Alignof(sa1_gp) == 4, "sa1_gp must have the alignment of a guest word");

#if defined(__GNUC__) || defined(__clang__)
#define SA1G_NORETURN __attribute__((noreturn))
#define SA1G_INLINE static inline __attribute__((always_inline, unused))
#else
#define SA1G_NORETURN
#define SA1G_INLINE static inline
#endif

/* ---- segments (2.1) -------------------------------------------------------------------------- */
#define SA1G_SEG_COLLISION  0x08u       /* 0x08-0x0B: guest 0x88000000-0x8BFFFFFF */
#define SA1G_SEG_ARENA      0x0Cu       /* guest 0x0C/0x8C/0xAC000000 */
#define SA1G_SEG_NEGTRAP    0x13u       /* 0 - (arena address) */
#define SA1G_SEG_EXT        0x18u       /* guest 0x98000000-: pcdata + world objects */
#define SA1G_SEG_STACK      0x1Du       /* guest 0x9D000000-: host-stack window */
#define SA1G_PCDATA_GUEST   0x98000000u /* R4 pcdata starts the extension segment */

/* canonical guest word of a host address inside a non-arena segment s whose host base is base and
 * whose mapped span is span bytes (<= 16 MB; the collision segment is four consecutive segments of
 * one 64 MB block): 0x80000000 | s << 24 | offset (0x88.., 0x98.., 0x9D..); 0 when outside */
SA1G_INLINE uint32_t sa1g_encode_segment(const void *p, uint32_t s, uintptr_t base, uintptr_t span)
{
    uintptr_t off = (uintptr_t)p - base;
    if (span == 0u || off >= span) {
        return 0u;
    }
    return 0x80000000u + (s << 24) + (uint32_t)off;
}

/* ---- faults (world faults: they never return; 6.9) ------------------------------------------- */
SA1G_NORETURN void sa1g_fault_encode(const void *host);      /* encode of a host address in no segment */
SA1G_NORETURN void sa1g_fault_fn(uint32_t g, uint32_t sig);  /* code-map miss / signature mismatch */
SA1G_NORETURN void sa1g_unimpl(uint32_t g, const char *name);/* fatal boundary stub (10.5) */

/* ---- decode / encode (2.2) ------------------------------------------------------------------- */
#if defined(SA1_GA32)

SA1G_INLINE void *sa1_decode(uint32_t g)
{
    return (void *)(uintptr_t)g;
}

SA1G_INLINE uint32_t sa1_encode(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}

/* address of a global: the DC address itself */
#define SA1_GV(a) ((void *)(uintptr_t)(uint32_t)(a))

#else /* ga64 */

extern uintptr_t sa1g_seg[32];      /* host base of each 16 MB guest segment ((g >> 24) & 31) */
extern uint8_t *sa1g_ram_base;      /* host address of guest 0x8C000000 (arena offset 0) */
/* any host address outside the arena: the collision segment, the extension segment, the stack
 * window (sa1g_encode_segment) -- anything else is a world fault (sa1g_fault_encode) */
uint32_t sa1g_encode_slow(const void *p);

/* NULL stays NULL; every other word is seg base + 24-bit offset (a cmov, no branch). Words of
 * unmapped segments land in trap reservations and fault on the first access, not here. */
SA1G_INLINE void *sa1_decode(uint32_t g)
{
    uintptr_t h = sa1g_seg[(g >> 24) & 31u] + (g & 0xFFFFFFu);
    return g ? (void *)h : (void *)0;
}

SA1G_INLINE uint32_t sa1_encode(const void *p)
{
    uintptr_t off;
    if (p == (const void *)0) {
        return 0u;
    }
    off = (uintptr_t)p - (uintptr_t)sa1g_ram_base;
    if (off < 0x1000000u) {
        /* the DC convention: P1 for 1ST_READ, BSS and heap, P0 for the overlay slots */
        return (off < 0x900000u ? 0x8C000000u : 0x0C000000u) | (uint32_t)off;
    }
    return sa1g_encode_slow(p);
}

/* address of a global: ram_base + (a & 0xFFFFFF) (P0/P1/P2 aliases land on the same byte) */
#define SA1_GV(a) ((void *)(sa1g_ram_base + ((uint32_t)(a) & 0xFFFFFFu)))

#endif /* profile */

/* ---- loads and stores through memory pointer slots (R2) -------------------------------------- */
/* load: the slot's guest word decoded to a native pointer */
#define SA1_LD(T, lv) ((T *)sa1_decode((lv).v))
#define SA1_LDP(PT, lv) ((PT)sa1_decode((lv).v))

/* store a native pointer (the slot address is evaluated once: *p++ = q); returns the value */
SA1G_INLINE void *sa1_st(sa1_gp *slot, const void *p)
{
    slot->v = sa1_encode(p);
    return (void *)(uintptr_t)p;
}

/* store a raw guest word (memory-to-memory pointer copies, NULL, integer -> pointer casts whose
 * result goes straight to memory: R2/R6b). Keeps the exact DC value, e.g. COLOBJ_HEAD_LINK. */
SA1G_INLINE uint32_t sa1_st_raw(sa1_gp *slot, uint32_t g)
{
    slot->v = g;
    return g;
}

/* p += n / ++p / --p on a pointer held in memory: raw guest arithmetic (the DC adds to the word
 * it holds, mirrors included); returns the new value decoded */
SA1G_INLINE void *sa1_gp_add(sa1_gp *slot, ptrdiff_t bytes)
{
    slot->v += (uint32_t)bytes;
    return sa1_decode(slot->v);
}

/* p++ / p-- on a pointer held in memory: returns the old value decoded */
SA1G_INLINE void *sa1_gp_postadd(sa1_gp *slot, ptrdiff_t bytes)
{
    uint32_t old = slot->v;
    slot->v = old + (uint32_t)bytes;
    return sa1_decode(old);
}

/* ---- function pointers (R5, section 3) ------------------------------------------------------- */
#define SA1_GFN_C(a) ((sa1_gfn){ (uint32_t)(a) })
#define SA1_GFN_NULL ((sa1_gfn){ 0u })
#define SA1_GFN_EQ(a, b) ((a).v == (b).v)
#define SA1_GFN_NE(a, b) ((a).v != (b).v)
#define SA1_GFN_NZ(a) ((a).v != 0u)

/* the generated code map (sa1g_fntab.c): a perfect hash on addr & 0x1FFFFFFF */
typedef struct sa1g_fnent {
    uint32_t key;               /* addr & 0x1FFFFFFF */
    uint32_t addr;              /* DC code address */
    uint32_t sig;               /* pclower signature id of the definition */
    void (*fn)(void);           /* the native function (any signature) */
    const char *name;
} sa1g_fnent;
const sa1g_fnent *sa1g_fn_lookup(uint32_t g);
extern const size_t sa1g_fntab_count;

/* DC code address -> native function. sig is the pclower signature id of the call site (0 =
 * unchecked). A miss or a mismatch is a world fault. (A host that routes calls itself, like the
 * harness, compiles sa1g_fntab.c with -DSA1G_FN_HOST and defines sa1_fn.) */
void *sa1_fn(sa1_gfn g, uint32_t sig);
/* native function -> DC code address (traces, debug output); 0 if unknown */
uint32_t sa1_fn_addr(const void *native);
/* name of the code-map entry of a DC code address, or NULL */
const char *sa1_fn_name(uint32_t g);

/* ---- R4 pcdata (C-defined data without a DC address) ------------------------------------------ */
/* Each lowered TU with pcdata ends with SA1G_PCD_UNIT(): its block type, size and initialiser.
 * sa1g_pcdata.c (generated) places the units: ga64 in segment 0x18 from SA1G_PCDATA_GUEST on,
 * ga32 in each unit's static storage. Lowered code reaches object k of its TU through
 * sa1_decode(sa1g_pcdo[k]) (guest addresses set by the initialiser), and an exported object NAME
 * of another TU through sa1_decode(sa1g_pcdv_NAME). */
typedef struct sa1g_pcd_unit {
    uint32_t size;
    uint32_t align;
    void (*init)(uint32_t gbase);       /* sets the TU's guest addresses, writes the initial bytes */
    const char *name;
    void *storage;                      /* ga32: the unit's static block; ga64: NULL (segment 0x18) */
} sa1g_pcd_unit;

#if defined(SA1_GA32)
#define SA1G_PCD_UNIT(tu, T, initfn) \
    static T sa1g_pcdstore_##tu; \
    const sa1g_pcd_unit sa1g_pcdunit_##tu = { (uint32_t)sizeof(T), (uint32_t)_Alignof(T), initfn, #tu, \
                                              (void *)&sa1g_pcdstore_##tu };
#else
#define SA1G_PCD_UNIT(tu, T, initfn) \
    const sa1g_pcd_unit sa1g_pcdunit_##tu = { (uint32_t)sizeof(T), (uint32_t)_Alignof(T), initfn, #tu, \
                                              (void *)0 };
#endif

/* bytes of segment 0x18 the pcdata needs (ga64) */
uint32_t sa1g_pcd_size(void);
/* place and initialise every unit. ga64: after the world mapped sa1g_pcd_size() bytes at the
 * start of segment 0x18 and entered (decode/encode must work); once per world creation.
 * ga32: once per process, before any lowered code runs. */
void sa1g_pcd_init(void);

/* ---- R7: the game's own C-library functions, renamed so they never bind to the host CRT --------
 * (rand/srand on the arena state D_8C5FBB08, the SHC memset/memcpy/strlen/strcpy, the atan2f wrapper
 * 8C10D0F8); implemented by libsa1's rt (rt_misc.c, rt_libm.c), entries of the code map at their DC
 * addresses */
int sa1g_rand(void);
void sa1g_srand(unsigned int seed);
void *sa1g_memset(void *dst, int c, size_t n);
void *sa1g_memcpy(void *dst, const void *src, size_t n);
size_t sa1g_strlen(const char *s);
char *sa1g_strcpy(char *dst, const char *src);
float sa1g_atan2f(float y, float x);

/* ---- R7: runtime functions and data without a DC address (rt symbols) -------------------------
 * The SHC intrinsics (_builtin_*) are renamed to these; the models are the interpreter's
 * (include/pc/sh4_fpmodel.h); ftrv reads XMTRX through sa1_rt_xmtrx. njFraction, sa1_tmu_tcnt1 and
 * UNK_PcWrite keep their names (declared by the game sources). */
float sa1g_fabsf(float x);
float sa1g_sqrtf(float x);
float sa1g_fsrra(float x);
float sa1g_fipr(float *a, float *b);
void sa1g_ftrv(float *v, float *out);
void sa1g_fsca(int32_t ang, float *s, float *c);
void sa1g_set_cr(int cr);
int sa1g_get_cr(void);
void sa1g_set_imask(int mask);
int sa1g_get_imask(void);
void sa1g_set_gbr(void *p);
void *sa1g_get_gbr(void);
extern float *sa1_rt_xmtrx;         /* XMTRX of the ticking world (harness: the interpreter's XF bank) */

/* ---- numerics (R6) --------------------------------------------------------------------------- */
/* SHC __divls/__divlu/__modls/__modlu (8C10CA5C..8C10CC80): divisor 0 -> 0, no trap;
 * INT_MIN / -1 wraps, x % -1 = 0 */
SA1G_INLINE int32_t sa1g_div_s32(int32_t a, int32_t b)
{
    if (b == 0) {
        return 0;
    }
    if (b == -1) {
        return (int32_t)(0u - (uint32_t)a);
    }
    return a / b;
}

SA1G_INLINE int32_t sa1g_mod_s32(int32_t a, int32_t b)
{
    if (b == 0 || b == -1) {
        return 0;
    }
    return a % b;
}

SA1G_INLINE uint32_t sa1g_div_u32(uint32_t a, uint32_t b)
{
    return b != 0u ? a / b : 0u;
}

SA1G_INLINE uint32_t sa1g_mod_u32(uint32_t a, uint32_t b)
{
    return b != 0u ? a % b : 0u;
}

/* FTRC: truncate; saturate to 0x7FFFFFFF / 0x80000000; NaN -> 0x80000000 */
SA1G_INLINE int32_t sa1g_ftrc(float f)
{
    if (f != f) {
        return (int32_t)0x80000000u;
    }
    if (f >= 2147483648.0f) {
        return 0x7FFFFFFF;
    }
    if (f <= -2147483648.0f) {
        return (int32_t)0x80000000u;
    }
    return (int32_t)f;
}

/* FMAC under SA1_GA (8.1): the product is exact in double, and under round-toward-zero a nested
 * truncation equals the single truncation of the fused result (harness sh4_fmac) */
#define SA1G_FMAC(a, b, c) ((float)((double)(a) * (double)(b) + (double)(c)))

#endif /* SA1_GUEST_H */
