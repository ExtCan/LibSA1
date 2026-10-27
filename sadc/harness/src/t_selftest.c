/*
 * t_selftest.c - instruction-level tests of the interpreter against the
 * documented SH-4 semantics (computed independently in C).  Needs no ROM.
 */
#include "sh4.h"
#include "harness.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- encoders (field layout as in tools/sh4/isa.py) ---- */
#define NM(base, m, n) ((uint16_t)((base) | ((n) << 8) | ((m) << 4)))
#define N_(base, n)    ((uint16_t)((base) | ((n) << 8)))
#define MOVI(i, n)  ((uint16_t)(0xE000 | ((n) << 8) | ((i) & 0xFF)))
#define MOV(m, n)   NM(0x6003, m, n)
#define ADD(m, n)   NM(0x300C, m, n)
#define ADDI(i, n)  ((uint16_t)(0x7000 | ((n) << 8) | ((i) & 0xFF)))
#define ADDC(m, n)  NM(0x300E, m, n)
#define ADDV(m, n)  NM(0x300F, m, n)
#define SUBC(m, n)  NM(0x300A, m, n)
#define SUBV(m, n)  NM(0x300B, m, n)
#define NEGC(m, n)  NM(0x600A, m, n)
#define DIV0U       0x0019
#define DIV0S(m, n) NM(0x2007, m, n)
#define DIV1(m, n)  NM(0x3004, m, n)
#define ROTCL(n)    N_(0x4024, n)
#define ROTCR(n)    N_(0x4025, n)
#define ROTL(n)     N_(0x4004, n)
#define ROTR(n)     N_(0x4005, n)
#define SHAD(m, n)  NM(0x400C, m, n)
#define SHLD(m, n)  NM(0x400D, m, n)
#define SHAR(n)     N_(0x4021, n)
#define SHLR(n)     N_(0x4001, n)
#define MACL(m, n)  NM(0x000F, m, n)
#define MACW(m, n)  NM(0x400F, m, n)
#define DMULS(m, n) NM(0x300D, m, n)
#define DMULU(m, n) NM(0x3005, m, n)
#define MULL(m, n)  NM(0x0007, m, n)
#define MULSW(m, n) NM(0x200F, m, n)
#define MULUW(m, n) NM(0x200E, m, n)
#define STSMACH(n)  N_(0x000A, n)
#define STSMACL(n)  N_(0x001A, n)
#define CLRMAC      0x0028
#define SETS        0x0058
#define CLRS        0x0048
#define SETT_       0x0018
#define CLRT        0x0008
#define MOVT(n)     N_(0x0029, n)
#define CMPSTR(m, n) NM(0x200C, m, n)
#define CMPEQ(m, n) NM(0x3000, m, n)
#define TAS(n)      N_(0x401B, n)
#define XTRCT(m, n) NM(0x200D, m, n)
#define SWAPB(m, n) NM(0x6008, m, n)
#define SWAPW(m, n) NM(0x6009, m, n)
#define EXTSB(m, n) NM(0x600E, m, n)
#define EXTUW(m, n) NM(0x600D, m, n)
#define BTS(d)      ((uint16_t)(0x8D00 | ((d) & 0xFF)))
#define BFS(d)      ((uint16_t)(0x8F00 | ((d) & 0xFF)))
#define BT(d)       ((uint16_t)(0x8900 | ((d) & 0xFF)))
#define BF(d)       ((uint16_t)(0x8B00 | ((d) & 0xFF)))
#define BRA(d)      ((uint16_t)(0xA000 | ((d) & 0xFFF)))
#define BSR(d)      ((uint16_t)(0xB000 | ((d) & 0xFFF)))
#define BRAF(n)     N_(0x0023, n)
#define BSRF(n)     N_(0x0003, n)
#define JMP(n)      N_(0x402B, n)
#define JSR(n)      N_(0x400B, n)
#define RTS         0x000B
#define NOP         0x0009
#define STSPR(n)    N_(0x002A, n)
#define LDSPR(n)    N_(0x402A, n)
#define STSLPR(n)   N_(0x4022, n)
#define LDSLPR(n)   N_(0x4026, n)
#define MOVA(d)     ((uint16_t)(0xC700 | ((d) & 0xFF)))
#define MOVLPC(d, n) ((uint16_t)(0xD000 | ((n) << 8) | ((d) & 0xFF)))
#define MOVWPC(d, n) ((uint16_t)(0x9000 | ((n) << 8) | ((d) & 0xFF)))
#define MOVLLD(m, n)  NM(0x6002, m, n)
#define MOVLLDP(m, n) NM(0x6006, m, n)
#define MOVLST(m, n)  NM(0x2002, m, n)
#define MOVLSTM(m, n) NM(0x2006, m, n)
#define MOVBLDP(m, n) NM(0x6004, m, n)
#define LDCGBR(n)   N_(0x401E, n)
#define ANDB(i)     ((uint16_t)(0xCD00 | (i)))
#define ORB(i)      ((uint16_t)(0xCF00 | (i)))
#define XORB(i)     ((uint16_t)(0xCE00 | (i)))
#define TSTB(i)     ((uint16_t)(0xCC00 | (i)))
#define MOVLGBRLD(d) ((uint16_t)(0xC600 | (d)))
#define MOVWGBRST(d) ((uint16_t)(0xC100 | (d)))
#define LDCSR(n)    N_(0x400E, n)
#define STCSR(n)    N_(0x0002, n)
#define STCRB(b, n) ((uint16_t)(0x0082 | ((n) << 8) | ((b) << 4)))
#define LDCRB(m, b) ((uint16_t)(0x408E | ((m) << 8) | ((b) << 4)))
#define DT(n)       N_(0x4010, n)
#define FADD(m, n)  NM(0xF000, m, n)
#define FSUB(m, n)  NM(0xF001, m, n)
#define FMUL(m, n)  NM(0xF002, m, n)
#define FDIV(m, n)  NM(0xF003, m, n)
#define FCMPEQ(m, n) NM(0xF004, m, n)
#define FCMPGT(m, n) NM(0xF005, m, n)
#define FMOV(m, n)  NM(0xF00C, m, n)
#define FMOVLD(m, n) NM(0xF008, m, n)
#define FMOVST(m, n) NM(0xF00A, m, n)
#define FMOVSTM(m, n) NM(0xF00B, m, n)
#define FMOVLDP(m, n) NM(0xF009, m, n)
#define FMAC(m, n)  NM(0xF00E, m, n)
#define FLDS(m)     N_(0xF01D, m)
#define FSTS(n)     N_(0xF00D, n)
#define FLOAT(n)    N_(0xF02D, n)
#define FTRC(m)     N_(0xF03D, m)
#define FNEG(n)     N_(0xF04D, n)
#define FABS(n)     N_(0xF05D, n)
#define FSQRT(n)    N_(0xF06D, n)
#define FSRRA(n)    N_(0xF07D, n)
#define FCNVSD(n)   N_(0xF0AD, n)
#define FCNVDS(m)   N_(0xF0BD, m)
#define FIPR(vm, vn) ((uint16_t)(0xF0ED | (((vn) / 4) << 10) | (((vm) / 4) << 8)))
#define FTRV(vn)    ((uint16_t)(0xF1FD | (((vn) / 4) << 10)))
#define FSCA(n)     N_(0xF0FD, n)
#define FSCHG       0xF3FD
#define FRCHG       0xFBFD
#define LDSFPSCR(m) N_(0x406A, m)
#define STSFPSCR(n) N_(0x006A, n)
#define LDSFPUL(m)  N_(0x405A, m)
#define STSFPUL(n)  N_(0x005A, n)

#define CODE 0x8C700000u
#define DATA 0x8C710000u

static int npass, nfail;
static uint32_t rng = 0x12345678u;

static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static uint32_t rnd_interesting(void)
{
    static const uint32_t edge[] = { 0, 1, 2, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFEu,
                                     0x80000001u, 0x0000FFFFu, 0xFFFF0000u, 31, 32, 0xFFFFFFE0u };
    uint32_t r = rnd();
    if ((r & 3) == 0)
        return edge[(r >> 2) % (sizeof edge / sizeof edge[0])];
    return rnd();
}

#define CHECK(cond, ...) do { \
    if (cond) npass++; \
    else { nfail++; printf("  FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static uint32_t f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float u2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static int run_n(const uint16_t *code, int n)
{
    int i, k;
    for (i = 0; i < n; i++)
        sh4_write16(CODE + 2u * (uint32_t)i, code[i]);
    k = sh4_call_regs(CODE);
    return k;
}
#define RUN(arr) run_n(arr, (int)(sizeof(arr) / sizeof(arr[0])))
#define T() (SH4.sr & SR_T)
#define SET_T(x) (SH4.sr = (SH4.sr & ~SR_T) | ((x) ? SR_T : 0))
/* ---------------------------------------------------------------- */
static void t_addsub(void)
{
    static const uint16_t c_addc[] = { ADDC(5, 4), MOVT(0), RTS, NOP };
    static const uint16_t c_addv[] = { ADDV(5, 4), MOVT(0), RTS, NOP };
    static const uint16_t c_subc[] = { SUBC(5, 4), MOVT(0), RTS, NOP };
    static const uint16_t c_subv[] = { SUBV(5, 4), MOVT(0), RTS, NOP };
    static const uint16_t c_negc[] = { NEGC(5, 4), MOVT(0), RTS, NOP };
    int i;
    for (i = 0; i < 3000; i++) {
        uint32_t a = rnd_interesting(), b = rnd_interesting(), t = rnd() & 1;
        uint64_t s = (uint64_t)a + b + t;
        int64_t sv = (int64_t)(int32_t)a + (int32_t)b;
        int64_t dv = (int64_t)(int32_t)a - (int32_t)b;
        SH4.r[4] = a; SH4.r[5] = b; SET_T(t);
        RUN(c_addc);
        CHECK(SH4.r[4] == (uint32_t)s && SH4.r[0] == (uint32_t)(s >> 32), "ADDC %08X+%08X+%u", a, b, t);
        SH4.r[4] = a; SH4.r[5] = b;
        RUN(c_addv);
        CHECK(SH4.r[4] == a + b && SH4.r[0] == (uint32_t)(sv > 0x7FFFFFFFll || sv < -0x80000000ll), "ADDV %08X %08X", a, b);
        SH4.r[4] = a; SH4.r[5] = b; SET_T(t);
        RUN(c_subc);
        CHECK(SH4.r[4] == a - b - t && SH4.r[0] == (uint32_t)((uint64_t)a < (uint64_t)b + t), "SUBC %08X-%08X-%u", a, b, t);
        SH4.r[4] = a; SH4.r[5] = b;
        RUN(c_subv);
        CHECK(SH4.r[4] == a - b && SH4.r[0] == (uint32_t)(dv > 0x7FFFFFFFll || dv < -0x80000000ll), "SUBV %08X %08X", a, b);
        SH4.r[5] = b; SET_T(t);
        RUN(c_negc);
        CHECK(SH4.r[4] == 0u - b - t && SH4.r[0] == (uint32_t)((uint64_t)b + t > 0), "NEGC %08X %u", b, t);
    }
}

static void t_div(void)
{
    uint16_t cu[80], cs[80];
    int n = 0, i;
    /* unsigned: R1 / R0 -> R1 (R2 = 0) as in __divlu */
    cu[n++] = MOVI(0, 2);
    cu[n++] = DIV0U;
    for (i = 0; i < 32; i++) { cu[n++] = ROTCL(1); cu[n++] = DIV1(0, 2); }
    cu[n++] = ROTCL(1);
    cu[n++] = RTS;
    cu[n++] = NOP;
    for (i = 0; i < 3000; i++) {
        uint32_t a = rnd_interesting(), b = rnd_interesting();
        if (b == 0) b = 7;
        SH4.r[1] = a; SH4.r[0] = b;
        run_n(cu, n);
        CHECK(SH4.r[1] == a / b, "DIV1 unsigned %08X/%08X -> %08X", a, b, SH4.r[1]);
    }
    /* signed, the __divls sequence */
    n = 0;
    cs[n++] = MOVI(0, 2);
    cs[n++] = DIV0S(2, 1);
    cs[n++] = SUBC(3, 3);
    cs[n++] = SUBC(2, 1);
    cs[n++] = DIV0S(0, 3);
    for (i = 0; i < 32; i++) { cs[n++] = ROTCL(1); cs[n++] = DIV1(0, 3); }
    cs[n++] = ROTCL(1);
    cs[n++] = ADDC(2, 1);
    cs[n++] = RTS;
    cs[n++] = MOV(1, 0);
    for (i = 0; i < 3000; i++) {
        int32_t a = (int32_t)rnd_interesting(), b = (int32_t)rnd_interesting();
        if (b == 0) b = -3;
        if (a == (int32_t)0x80000000 && b == -1) b = 5;
        SH4.r[1] = (uint32_t)a; SH4.r[0] = (uint32_t)b;
        run_n(cs, n);
        CHECK((int32_t)SH4.r[0] == a / b, "DIV1 signed %d/%d -> %d", a, b, (int32_t)SH4.r[0]);
    }
}

static uint32_t ref_shad(uint32_t v, uint32_t s)
{
    if ((int32_t)s >= 0) return v << (s & 31);
    if ((s & 31) == 0) return (int32_t)v < 0 ? 0xFFFFFFFFu : 0;
    return (uint32_t)((int32_t)v >> (32 - (s & 31)));
}

static uint32_t ref_shld(uint32_t v, uint32_t s)
{
    if ((int32_t)s >= 0) return v << (s & 31);
    if ((s & 31) == 0) return 0;
    return v >> (32 - (s & 31));
}

static void t_shift(void)
{
    static const uint16_t c_shad[] = { SHAD(5, 4), RTS, NOP };
    static const uint16_t c_shld[] = { SHLD(5, 4), RTS, NOP };
    static const uint16_t c_rot[] = { ROTCL(4), MOVT(0), ROTCR(5), MOVT(1), ROTL(6), MOVT(2), ROTR(7), MOVT(3), RTS, NOP };
    static const uint16_t c_shar[] = { SHAR(4), MOVT(0), SHLR(5), MOVT(1), RTS, NOP };
    int i;
    for (i = 0; i < 3000; i++) {
        uint32_t v = rnd_interesting(), s = rnd_interesting(), t = rnd() & 1;
        if (rnd() & 1) s = (rnd() % 64) - 32;
        SH4.r[4] = v; SH4.r[5] = s;
        RUN(c_shad);
        CHECK(SH4.r[4] == ref_shad(v, s), "SHAD %08X by %08X", v, s);
        SH4.r[4] = v; SH4.r[5] = s;
        RUN(c_shld);
        CHECK(SH4.r[4] == ref_shld(v, s), "SHLD %08X by %08X", v, s);
        SH4.r[4] = v; SH4.r[5] = v; SH4.r[6] = v; SH4.r[7] = v; SET_T(t);
        RUN(c_rot);
        CHECK(SH4.r[4] == ((v << 1) | t) && SH4.r[0] == (v >> 31), "ROTCL %08X", v);
        CHECK(SH4.r[5] == ((v >> 1) | ((v >> 31) << 31)) && SH4.r[1] == (v & 1), "ROTCR %08X", v);
        CHECK(SH4.r[6] == ((v << 1) | (v >> 31)) && SH4.r[2] == (v >> 31), "ROTL %08X", v);
        CHECK(SH4.r[7] == ((v >> 1) | (v << 31)) && SH4.r[3] == (v & 1), "ROTR %08X", v);
        SH4.r[4] = v; SH4.r[5] = v;
        RUN(c_shar);
        CHECK(SH4.r[4] == (uint32_t)((int32_t)v >> 1) && SH4.r[0] == (v & 1), "SHAR %08X", v);
        CHECK(SH4.r[5] == v >> 1 && SH4.r[1] == (v & 1), "SHLR %08X", v);
    }
}

static void t_mul(void)
{
    static const uint16_t c_dmuls[] = { DMULS(5, 4), STSMACH(0), STSMACL(1), RTS, NOP };
    static const uint16_t c_dmulu[] = { DMULU(5, 4), STSMACH(0), STSMACL(1), RTS, NOP };
    static const uint16_t c_mul[] = { MULL(5, 4), STSMACL(0), MULSW(5, 4), STSMACL(1), MULUW(5, 4), STSMACL(2), RTS, NOP };
    static const uint16_t c_macw[] = { MACW(5, 4), MACW(5, 4), STSMACH(0), STSMACL(1), RTS, NOP };
    static const uint16_t c_macl[] = { MACL(5, 4), MACL(5, 4), STSMACH(0), STSMACL(1), RTS, NOP };
    int i;
    for (i = 0; i < 2000; i++) {
        uint32_t a = rnd_interesting(), b = rnd_interesting();
        int64_t ps = (int64_t)(int32_t)a * (int32_t)b;
        uint64_t pu = (uint64_t)a * b;
        SH4.r[4] = a; SH4.r[5] = b;
        RUN(c_dmuls);
        CHECK(SH4.r[0] == (uint32_t)((uint64_t)ps >> 32) && SH4.r[1] == (uint32_t)ps, "DMULS %08X*%08X", a, b);
        RUN(c_dmulu);
        CHECK(SH4.r[0] == (uint32_t)(pu >> 32) && SH4.r[1] == (uint32_t)pu, "DMULU %08X*%08X", a, b);
        RUN(c_mul);
        CHECK(SH4.r[0] == a * b, "MUL.L");
        CHECK(SH4.r[1] == (uint32_t)((int32_t)(int16_t)a * (int16_t)b), "MULS.W %08X %08X", a, b);
        CHECK(SH4.r[2] == (uint32_t)((a & 0xFFFF) * (b & 0xFFFF)), "MULU.W");
    }
    /* MAC.W / MAC.L, S=0 and S=1, two accumulations each */
    for (i = 0; i < 2000; i++) {
        int s = rnd() & 1;
        uint32_t mach0 = (rnd() & 1) ? rnd() : 0, macl0 = rnd_interesting();
        int16_t w[4];
        int32_t l[4];
        int k;
        for (k = 0; k < 4; k++) {
            w[k] = (int16_t)rnd_interesting();
            l[k] = (int32_t)rnd_interesting();
            sh4_write16(DATA + 2u * (uint32_t)k, (uint16_t)w[k]);
            sh4_write32(DATA + 16 + 4u * (uint32_t)k, (uint32_t)l[k]);
        }
        /* MAC.W @R5+,@R4+ : R4 -> w[0], w[1]; R5 -> w[2], w[3] */
        SH4.r[4] = DATA; SH4.r[5] = DATA + 4;
        SH4.mach = s ? (mach0 & 0xFFFF) * 0 : mach0;
        SH4.macl = macl0;
        SH4.sr = (SH4.sr & ~SR_S) | (s ? SR_S : 0);
        RUN(c_macw);
        {
            uint32_t emach = SH4.mach, emacl;
            if (!s) {
                int64_t acc = (int64_t)(((uint64_t)mach0 << 32) | macl0);
                acc += (int64_t)w[0] * w[2];
                acc += (int64_t)w[1] * w[3];
                emach = (uint32_t)((uint64_t)acc >> 32);
                emacl = (uint32_t)acc;
            } else {
                int64_t acc = (int32_t)macl0;
                uint32_t ov = 0;
                int j;
                for (j = 0; j < 2; j++) {
                    acc += (int64_t)w[j] * w[j + 2];
                    if (acc > 0x7FFFFFFFll) { acc = 0x7FFFFFFFll; ov = 1; }
                    if (acc < -0x80000000ll) { acc = -0x80000000ll; ov = 1; }
                }
                emacl = (uint32_t)acc;
                emach = ov;
            }
            CHECK(SH4.r[0] == emach && SH4.r[1] == emacl && SH4.r[4] == DATA + 4 && SH4.r[5] == DATA + 8,
                  "MAC.W S=%d mac=%08X:%08X got %08X:%08X want %08X:%08X", s, mach0, macl0, SH4.r[0], SH4.r[1], emach, emacl);
        }
        /* MAC.L @R5+,@R4+ */
        SH4.r[4] = DATA + 16; SH4.r[5] = DATA + 24;
        SH4.mach = s ? (uint32_t)(int32_t)(int16_t)(mach0 & 0xFFFF) : mach0;
        SH4.macl = macl0;
        {
            int64_t acc = (int64_t)(((uint64_t)SH4.mach << 32) | macl0);
            int j;
            RUN(c_macl);
            for (j = 0; j < 2; j++) {
                acc += (int64_t)l[j] * l[j + 2];
                if (s) {
                    if (acc > 0x00007FFFFFFFFFFFll) acc = 0x00007FFFFFFFFFFFll;
                    if (acc < -0x0000800000000000ll) acc = -0x0000800000000000ll;
                }
            }
            CHECK(SH4.r[0] == (uint32_t)((uint64_t)acc >> 32) && SH4.r[1] == (uint32_t)acc, "MAC.L S=%d", s);
        }
    }
    SH4.sr &= ~SR_S;
}

static void t_misc(void)
{
    static const uint16_t c_str[] = { CMPSTR(5, 4), MOVT(0), RTS, NOP };
    static const uint16_t c_x[] = { XTRCT(5, 4), SWAPB(5, 1), SWAPW(5, 2), EXTSB(5, 3), EXTUW(5, 6), RTS, NOP };
    static const uint16_t c_tas[] = { TAS(4), MOVT(0), RTS, NOP };
    static const uint16_t c_dt[] = { DT(4), MOVT(0), RTS, NOP };
    int i;
    for (i = 0; i < 2000; i++) {
        uint32_t a = rnd_interesting(), b = rnd_interesting(), x;
        if (rnd() & 1) b = (b & ~(0xFFu << ((rnd() & 3) * 8))) | (a & (0xFFu << ((rnd() & 3) * 8)));
        x = a ^ b;
        SH4.r[4] = a; SH4.r[5] = b;
        RUN(c_str);
        CHECK(SH4.r[0] == (uint32_t)(!(x & 0xFF) || !(x & 0xFF00) || !(x & 0xFF0000) || !(x & 0xFF000000u)), "CMP/STR");
        SH4.r[4] = a; SH4.r[5] = b;
        RUN(c_x);
        CHECK(SH4.r[4] == ((a >> 16) | (b << 16)), "XTRCT");
        CHECK(SH4.r[1] == ((b & 0xFFFF0000u) | ((b & 0xFF) << 8) | ((b >> 8) & 0xFF)), "SWAP.B");
        CHECK(SH4.r[2] == ((b >> 16) | (b << 16)), "SWAP.W");
        CHECK(SH4.r[3] == (uint32_t)(int32_t)(int8_t)b && SH4.r[6] == (b & 0xFFFF), "EXTS.B/EXTU.W");
    }
    sh4_write8(DATA, 0);
    SH4.r[4] = DATA;
    RUN(c_tas);
    CHECK(SH4.r[0] == 1 && sh4_read8(DATA) == 0x80, "TAS.B 0");
    RUN(c_tas);
    CHECK(SH4.r[0] == 0 && sh4_read8(DATA) == 0x80, "TAS.B 80");
    SH4.r[4] = 1;
    RUN(c_dt);
    CHECK(SH4.r[4] == 0 && SH4.r[0] == 1, "DT 1");
    SH4.r[4] = 0;
    RUN(c_dt);
    CHECK(SH4.r[4] == 0xFFFFFFFFu && SH4.r[0] == 0, "DT 0");
}
static void t_branch(void)
{
    /* BT/S taken: slot executes, insn after slot skipped */
    static const uint16_t c1[] = { SETT_, BTS(1), MOVI(5, 0), MOVI(7, 1), RTS, NOP };
    /* BF/S not taken (T=1): slot executes and falls through */
    static const uint16_t c2[] = { SETT_, BFS(1), MOVI(5, 0), MOVI(7, 1), RTS, NOP };
    /* condition is sampled before the slot (slot clears T) */
    static const uint16_t c3[] = { SETT_, BTS(1), CLRT, MOVI(7, 1), RTS, NOP };
    /* JSR target is read before the slot modifies the register */
    static const uint16_t c4[] = { STSLPR(15), JSR(1), ADDI(4, 1), LDSLPR(15), RTS, NOP,
                                   RTS, MOVI(42, 0), RTS, MOVI(99, 0) };
    /* BSR sets PR = address after the slot */
    static const uint16_t c5[] = { STSLPR(15), BSR(3), NOP, LDSLPR(15), RTS, NOP, STSPR(2), RTS, NOP };
    /* BRAF: target = PC + 4 + Rn */
    static const uint16_t c6[] = { BRAF(1), MOVI(1, 0), MOVI(2, 0), MOVI(3, 0), RTS, ADDI(10, 0) };
    /* MOVA / MOV.L PC-relative at a PC that is 2 mod 4 */
    static const uint16_t c7[] = { NOP, MOVA(2), MOVLPC(1, 1), RTS, NOP, NOP, 0x5678, 0x1234 };
    /* MOV.W PC-relative */
    static const uint16_t c8[] = { MOVWPC(1, 2), RTS, NOP, 0x8001 };
    /* MOV.L @R1+,R1: no increment; MOV.L R3,@-R3 stores the original value */
    static const uint16_t c9[] = { MOVLLDP(1, 1), MOVLSTM(3, 3), RTS, NOP };
    /* GBR logic */
    static const uint16_t c10[] = { LDCGBR(1), ANDB(0x0F), ORB(0x40), XORB(0x01), TSTB(0x80), MOVT(2), RTS, NOP };
    /* loop with DT/BF: 10 iterations */
    static const uint16_t c11[] = { MOVI(10, 4), MOVI(0, 0), ADDI(3, 0), DT(4), BF(-4), RTS, NOP };

    SH4.r[1] = 0;
    RUN(c1);
    CHECK(SH4.r[0] == 5 && SH4.r[1] == 0, "BT/S taken r0=%u r1=%u", SH4.r[0], SH4.r[1]);
    SH4.r[1] = 0;
    RUN(c2);
    CHECK(SH4.r[0] == 5 && SH4.r[1] == 7, "BF/S not taken");
    SH4.r[1] = 0;
    RUN(c3);
    CHECK(SH4.r[1] == 0, "BT/S samples T before the slot");
    SH4.r[1] = CODE + 12;
    RUN(c4);
    CHECK(SH4.r[0] == 42 && SH4.r[1] == CODE + 16, "JSR target before slot: r0=%u r1=%08X", SH4.r[0], SH4.r[1]);
    RUN(c5);
    CHECK(SH4.r[2] == CODE + 6, "BSR PR=%08X", SH4.r[2]);
    SH4.r[1] = 4;   /* PC(0)+4+4 = index 4 */
    RUN(c6);
    CHECK(SH4.r[0] == 11, "BRAF r0=%u", SH4.r[0]);
    RUN(c7);
    CHECK(SH4.r[0] == CODE + 12 && SH4.r[1] == 0x12345678u, "MOVA %08X / MOV.L pc %08X", SH4.r[0], SH4.r[1]);
    RUN(c8);
    CHECK(SH4.r[2] == 0xFFFF8001u, "MOV.W pc %08X", SH4.r[2]);
    sh4_write32(DATA, 0xCAFEF00Du);
    SH4.r[1] = DATA;
    SH4.r[3] = DATA + 0x20;
    RUN(c9);
    CHECK(SH4.r[1] == 0xCAFEF00Du, "MOV.L @R1+,R1");
    CHECK(SH4.r[3] == DATA + 0x1C && sh4_read32(DATA + 0x1C) == DATA + 0x20, "MOV.L R3,@-R3");
    sh4_write8(DATA + 5, 0xF3);
    SH4.r[1] = DATA;
    SH4.r[0] = 5;
    RUN(c10);
    CHECK(sh4_read8(DATA + 5) == 0x42 && SH4.r[2] == 1, "GBR ops %02X", sh4_read8(DATA + 5));
    RUN(c11);
    CHECK(SH4.r[0] == 30 && SH4.r[4] == 0, "DT loop r0=%u", SH4.r[0]);
}

static void t_banks(void)
{
    static const uint16_t c1[] = { STCRB(0, 3), LDCRB(4, 1), STCSR(5), RTS, NOP };
    SH4.r[0] = 111;
    SH4.rbank[0] = 222;
    SH4.r[4] = 333;
    RUN(c1);
    CHECK(SH4.r[3] == 222 && SH4.rbank[1] == 333, "STC/LDC Rn_BANK");
    CHECK((SH4.r[5] & SR_MD) != 0, "SR.MD set");
    sh4_set_sr(&SH4, SH4.sr | SR_RB);
    CHECK(SH4.r[0] == 222 && SH4.r[1] == 333 && SH4.rbank[0] == 111, "RB swap");
    sh4_set_sr(&SH4, SH4.sr & ~SR_RB);
    CHECK(SH4.r[0] == 111, "RB swap back");
}

static uint32_t rz_ref(double d)
{
    float r = (float)d;
    if (fabs((double)r) > fabs(d))
        r = nextafterf(r, 0.0f);
    return f2u(r);
}

static float rnd_float(void)
{
    uint32_t e = 127 - 10 + rnd() % 21;
    uint32_t u = (rnd() & 0x807FFFFFu) | (e << 23);
    return u2f(u);
}

static void t_fpu(void)
{
    static const uint16_t c_add[] = { FADD(2, 1), RTS, NOP };
    static const uint16_t c_sub[] = { FSUB(2, 1), RTS, NOP };
    static const uint16_t c_mul[] = { FMUL(2, 1), RTS, NOP };
    static const uint16_t c_div[] = { FDIV(2, 1), RTS, NOP };
    static const uint16_t c_sqrt[] = { FSQRT(1), RTS, NOP };
    static const uint16_t c_float[] = { LDSFPUL(4), FLOAT(1), RTS, NOP };
    static const uint16_t c_ftrc[] = { FTRC(1), STSFPUL(0), RTS, NOP };
    static const uint16_t c_fmac[] = { FMAC(1, 2), RTS, NOP };
    static const uint16_t c_fipr[] = { FIPR(0, 4), RTS, NOP };
    static const uint16_t c_ftrv[] = { FTRV(0), RTS, NOP };
    static const uint16_t c_fsca[] = { LDSFPUL(4), FSCA(2), RTS, NOP };
    static const uint16_t c_fsrra[] = { FSRRA(1), RTS, NOP };
    static const uint16_t c_cmp[] = { FCMPEQ(2, 1), MOVT(0), FCMPGT(2, 1), MOVT(1), FCMPGT(1, 2), MOVT(2), RTS, NOP };
    static const uint16_t c_negabs[] = { FNEG(1), FABS(2), FLDS(1), STSFPUL(3), RTS, NOP };
    static const uint16_t c_pair[] = { FSCHG, FMOVLD(4, 2), FMOVLD(4, 3), FMOVST(2, 5), FMOV(3, 8), FSCHG, RTS, NOP };
    static const uint16_t c_frchg[] = { FRCHG, RTS, NOP };
    static const uint16_t c_dbl[] = { LDSFPUL(4), FLOAT(0), FADD(0, 0), FCNVDS(0), STSFPUL(1), RTS, NOP };
    static const uint16_t c_fpscr[] = { STSFPSCR(0), RTS, NOP };
    int i;

    /* specific RZ cases */
    SH4.fr.f[1] = 1.0f; SH4.fr.u[2] = 0x33C00000u;
    RUN(c_add);
    CHECK(SH4.fr.u[1] == 0x3F800000u, "FADD RZ %08X", SH4.fr.u[1]);
    SH4.fpscr = 0x00040000u;    /* RN */
    SH4.fr.f[1] = 1.0f; SH4.fr.u[2] = 0x33C00000u;
    RUN(c_add);
    CHECK(SH4.fr.u[1] == 0x3F800001u, "FADD RN %08X", SH4.fr.u[1]);
    SH4.fpscr = SH4_DEFAULT_FPSCR;
    SH4.fr.f[1] = 1.0f; SH4.fr.f[2] = 3.0f;
    RUN(c_div);
    CHECK(SH4.fr.u[1] == 0x3EAAAAAAu, "FDIV 1/3 RZ %08X", SH4.fr.u[1]);
    SH4.fr.f[1] = 2.0f; SH4.fr.f[2] = 3.0f;
    RUN(c_div);
    CHECK(SH4.fr.u[1] == 0x3F2AAAAAu, "FDIV 2/3 RZ %08X", SH4.fr.u[1]);
    SH4.fr.f[1] = 5.0f;
    RUN(c_sqrt);
    CHECK(SH4.fr.u[1] == 0x400F1BBCu, "FSQRT 5 RZ %08X", SH4.fr.u[1]);
    SH4.r[4] = 0x7FFFFFFFu;
    RUN(c_float);
    CHECK(SH4.fr.u[1] == 0x4EFFFFFFu, "FLOAT 7FFFFFFF RZ %08X", SH4.fr.u[1]);
    SH4.r[4] = 16777219;
    RUN(c_float);
    CHECK(SH4.fr.u[1] == 0x4B800001u, "FLOAT 16777219 RZ %08X", SH4.fr.u[1]);
    {
        static const uint32_t in[] = { 0x4F32D05Eu /*3e9*/, 0xCF32D05Eu, 0x7FC00000u, 0xFFC00000u, 0xBFC00000u /*-1.5*/,
                                       0x4EFFFFFFu, 0x4F000000u, 0xCF000000u, 0x7F800000u, 0xFF800000u, 0x3F7FFFFFu };
        static const uint32_t out[] = { 0x7FFFFFFFu, 0x80000000u, 0x80000000u, 0x80000000u, 0xFFFFFFFFu,
                                        2147483520u, 0x7FFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 0x80000000u, 0 };
        for (i = 0; i < (int)(sizeof in / sizeof in[0]); i++) {
            SH4.fr.u[1] = in[i];
            RUN(c_ftrc);
            CHECK(SH4.r[0] == out[i], "FTRC %08X -> %08X want %08X", in[i], SH4.r[0], out[i]);
        }
    }
    /* random RZ add/sub/mul against an independent reference */
    for (i = 0; i < 5000; i++) {
        float a = rnd_float(), b = rnd_float();
        SH4.fr.f[1] = a; SH4.fr.f[2] = b;
        RUN(c_add);
        CHECK(SH4.fr.u[1] == rz_ref((double)a + (double)b), "FADD %08X %08X", f2u(a), f2u(b));
        SH4.fr.f[1] = a; SH4.fr.f[2] = b;
        RUN(c_sub);
        CHECK(SH4.fr.u[1] == rz_ref((double)a - (double)b), "FSUB %08X %08X", f2u(a), f2u(b));
        SH4.fr.f[1] = a; SH4.fr.f[2] = b;
        RUN(c_mul);
        CHECK(SH4.fr.u[1] == rz_ref((double)a * (double)b), "FMUL %08X %08X", f2u(a), f2u(b));
    }
    /* FMAC is fused */
    SH4.fr.u[0] = 0x3F800800u; SH4.fr.u[1] = 0x3F800800u; SH4.fr.u[2] = 0xBF801000u;
    RUN(c_fmac);
    CHECK(SH4.fr.u[2] == 0x33800000u, "FMAC fused %08X", SH4.fr.u[2]);
    /* FIPR / FTRV */
    for (i = 0; i < 4; i++) { SH4.fr.f[i] = (float)(i + 1); SH4.fr.f[4 + i] = (float)(i + 5); }
    RUN(c_fipr);
    CHECK(SH4.fr.f[7] == 70.0f, "FIPR %f", (double)SH4.fr.f[7]);
    for (i = 0; i < 16; i++)
        SH4.xf.f[(i / 4) + 4 * (i % 4)] = (float)(i + 1);  /* M[i][j] = 4i+j+1 at XF[i+4j] */
    for (i = 0; i < 4; i++) SH4.fr.f[i] = 1.0f;
    RUN(c_ftrv);
    CHECK(SH4.fr.f[0] == 10.0f && SH4.fr.f[1] == 26.0f && SH4.fr.f[2] == 42.0f && SH4.fr.f[3] == 58.0f,
          "FTRV %g %g %g %g", (double)SH4.fr.f[0], (double)SH4.fr.f[1], (double)SH4.fr.f[2], (double)SH4.fr.f[3]);
    /* FSCA */
    SH4.r[4] = 0x4000;
    RUN(c_fsca);
    CHECK(SH4.fr.u[2] == 0x3F800000u && (SH4.fr.u[3] & 0x7FFFFFFFu) == 0, "FSCA 4000 %08X %08X", SH4.fr.u[2], SH4.fr.u[3]);
    SH4.r[4] = 0x18000;
    RUN(c_fsca);
    CHECK((SH4.fr.u[2] & 0x7FFFFFFFu) == 0 && SH4.fr.u[3] == 0xBF800000u, "FSCA 8000 %08X %08X", SH4.fr.u[2], SH4.fr.u[3]);
    SH4.r[4] = 0x2000;
    RUN(c_fsca);
    CHECK(SH4.fr.u[2] == 0x3F3504F3u && SH4.fr.u[3] == 0x3F3504F3u, "FSCA 2000 %08X %08X", SH4.fr.u[2], SH4.fr.u[3]);
    {   /* the FSCA table loaded from libsa1/src/rt/rt_fsca_q.c is exactly what fsca_build() computes
         * (all 65,536 angles, sine and cosine; sh4_fsca_ref.c) */
        extern int sh4_fsca_selfcheck(uint32_t *first_bad);
        uint32_t bad_at = 0;
        int nbad = sh4_fsca_selfcheck(&bad_at);
        CHECK(nbad == 0, "FSCA table: %d mismatches with fsca_build(), first at %05X "
              "(regenerate with harness/tools/dump_fsca.py)", nbad, bad_at);
    }
    SH4.fr.f[1] = 4.0f;
    RUN(c_fsrra);
    CHECK(SH4.fr.f[1] == 0.5f, "FSRRA 4");
    /* denormals (DN=1) */
    SH4.fr.f[1] = 1e-20f; SH4.fr.f[2] = 1e-20f;
    RUN(c_mul);
    CHECK(SH4.fr.u[1] == 0, "FMUL underflow flushes %08X", SH4.fr.u[1]);
    SH4.fr.u[1] = 0x00000001u; SH4.fr.u[2] = 0;
    RUN(c_add);
    CHECK(SH4.fr.u[1] == 0, "denormal input is zero %08X", SH4.fr.u[1]);
    /* compares with NaN */
    SH4.fr.u[1] = 0x7FBFFFFFu; SH4.fr.f[2] = 1.0f;
    RUN(c_cmp);
    CHECK(SH4.r[0] == 0 && SH4.r[1] == 0 && SH4.r[2] == 0, "FCMP NaN");
    SH4.fr.f[1] = 2.0f; SH4.fr.f[2] = 1.0f;
    RUN(c_cmp);
    CHECK(SH4.r[0] == 0 && SH4.r[1] == 1 && SH4.r[2] == 0, "FCMP 2>1");
    SH4.fr.u[1] = 0x3F800000u; SH4.fr.u[2] = 0xC0000000u;
    RUN(c_negabs);
    CHECK(SH4.fr.u[1] == 0xBF800000u && SH4.fr.u[2] == 0x40000000u && SH4.r[3] == 0xBF800000u, "FNEG/FABS/FLDS");
    /* NaN result is the SH-4 default qNaN */
    SH4.fr.u[1] = 0x7F800000u; SH4.fr.u[2] = 0xFF800000u;
    RUN(c_add);
    CHECK(SH4.fr.u[1] == 0x7FBFFFFFu, "inf-inf = %08X", SH4.fr.u[1]);
    /* pair moves: even register <-> lower address */
    sh4_write32(DATA, 0x11111111u);
    sh4_write32(DATA + 4, 0x22222222u);
    SH4.r[4] = DATA;
    SH4.r[5] = DATA + 8;
    RUN(c_pair);
    CHECK(SH4.fr.u[2] == 0x11111111u && SH4.fr.u[3] == 0x22222222u, "FMOV @R4,DR2");
    CHECK(SH4.xf.u[2] == 0x11111111u && SH4.xf.u[3] == 0x22222222u, "FMOV @R4,XD2");
    CHECK(sh4_read32(DATA + 8) == 0x11111111u && sh4_read32(DATA + 12) == 0x22222222u, "FMOV DR2,@R5");
    CHECK(SH4.fr.u[8] == 0x11111111u && SH4.fr.u[9] == 0x22222222u, "FMOV XD2,DR8");
    CHECK((SH4.fpscr & FPSCR_SZ) == 0, "SZ restored");
    SH4.fr.f[0] = 1.0f; SH4.xf.f[0] = 2.0f;
    RUN(c_frchg);
    CHECK(SH4.fr.f[0] == 2.0f && SH4.xf.f[0] == 1.0f && (SH4.fpscr & FPSCR_FR), "FRCHG");
    RUN(c_frchg);
    CHECK(SH4.fr.f[0] == 1.0f && !(SH4.fpscr & FPSCR_FR), "FRCHG back");
    /* double precision */
    SH4.fpscr = SH4_DEFAULT_FPSCR | FPSCR_PR;
    SH4.r[4] = 3;
    RUN(c_dbl);
    CHECK(SH4.fr.u[0] == 0x40180000u && SH4.fr.u[1] == 0 && SH4.r[1] == 0x40C00000u, "double FLOAT/FADD/FCNVDS %08X %08X %08X",
          SH4.fr.u[0], SH4.fr.u[1], SH4.r[1]);
    SH4.fpscr = SH4_DEFAULT_FPSCR;
    RUN(c_fpscr);
    CHECK(SH4.r[0] == SH4_DEFAULT_FPSCR, "STS FPSCR");
}

static int hook_calls;
static void hook_double(sh4_cpu *c, void *u)
{
    (void)u;
    hook_calls++;
    c->r[0] = c->r[4] * 2;
}

static void hook_nested(sh4_cpu *c, void *u)
{
    uint32_t args[4] = { 0, 0, 0, 0 };
    (void)u;
    args[0] = c->r[4] + 1;
    c->r[0] = sh4_call(CODE + 0x200, args, NULL) + 1;   /* 2*(r4+1)+1 */
}

static void t_faults_hooks(void)
{
    static const uint16_t c_ill[] = { 0xFFFF, RTS, NOP };
    static const uint16_t c_slot[] = { BRA(1), BRA(1), NOP, RTS, NOP };
    static const uint16_t c_align[] = { MOVLLD(4, 0), RTS, NOP };
    static const uint16_t c_loop[] = { BRA(-2), NOP };
    static const uint16_t c_call[] = { STSLPR(15), JSR(1), NOP, LDSLPR(15), RTS, NOP };
    static const uint16_t c_sub[] = { MOV(4, 0), RTS, ADD(0, 0) };
    int k, i;
    int save_pt = sh4_host_passthrough;

    k = RUN(c_ill);
    CHECK(k == SH4_FAULT_ILLEGAL && SH4.fault.pc == CODE, "illegal -> %s", sh4_fault_name(k));
    k = RUN(c_slot);
    CHECK(k == SH4_FAULT_SLOT, "slot -> %s", sh4_fault_name(k));
    SH4.r[4] = DATA + 2;
    k = RUN(c_align);
    CHECK(k == SH4_FAULT_ALIGN, "align -> %s", sh4_fault_name(k));
    sh4_host_passthrough = 0;
    SH4.r[4] = 0x40000000u;
    k = RUN(c_align);
    CHECK(k == SH4_FAULT_MEM, "unmapped -> %s", sh4_fault_name(k));
    sh4_host_passthrough = save_pt;
    SH4.ilimit = SH4.icount + 100000;
    k = RUN(c_loop);
    CHECK(k == SH4_FAULT_TIMEOUT, "budget -> %s", sh4_fault_name(k));
    SH4.ilimit = 0;
    CHECK(SH4.depth == 0, "depth after faults %d", SH4.depth);

    /* hook reached by JSR */
    sh4_hook_add(CODE + 0x100, hook_double, NULL, "double");
    for (i = 8; i < 15; i++) SH4.r[i] = 0x1000u + (uint32_t)i;
    SH4.r[1] = CODE + 0x100;
    SH4.r[4] = 21;
    hook_calls = 0;
    k = RUN(c_call);
    CHECK(k == 0 && SH4.r[0] == 42 && hook_calls == 1, "hook via JSR r0=%u", SH4.r[0]);
    /* hook as the entry point of sh4_call */
    {
        uint32_t args[4] = { 50, 0, 0, 0 };
        CHECK(sh4_call(CODE + 0x100, args, NULL) == 100, "hook at entry");
    }
    sh4_hook_remove(CODE + 0x100);
    /* nested: hook calls back into the guest */
    for (i = 0; i < 3; i++)
        sh4_write16(CODE + 0x200 + 2u * (uint32_t)i, c_sub[i]);
    sh4_hook_add(CODE + 0x100, hook_nested, NULL, "nested");
    SH4.r[4] = 10;
    k = RUN(c_call);
    CHECK(k == 0 && SH4.r[0] == 23, "nested call r0=%u", SH4.r[0]);
    for (i = 8; i < 15; i++)
        CHECK(SH4.r[i] == 0x1000u + (uint32_t)i, "R%d preserved", i);
    CHECK(SH4.r[15] == SH4.stack_top, "R15 restored %08X", SH4.r[15]);
    sh4_hook_remove(CODE + 0x100);
}

int cmd_selftest(int argc, char **argv)
{
    (void)argc; (void)argv;
    npass = nfail = 0;
    sh4_reset(&SH4);
    printf("selftest: integer ALU\n");  t_addsub();
    printf("selftest: DIV0/DIV1\n");    t_div();
    printf("selftest: shifts\n");       t_shift();
    printf("selftest: multiply/MAC\n"); t_mul();
    printf("selftest: misc\n");         t_misc();
    printf("selftest: branches\n");     t_branch();
    printf("selftest: banks\n");        t_banks();
    printf("selftest: FPU\n");          t_fpu();
    printf("selftest: faults/hooks\n"); t_faults_hooks();
    printf("selftest: %d passed, %d failed\n", npass, nfail);
    sh4_host_fp_game_mode();
    return nfail ? 1 : 0;
}