/*
 * sh4_cpu.c - SH-4 instruction interpreter (integer + FPU, user and the
 * privileged instructions the game executes).
 *
 * Decoding uses the generated table sh4_optab.h (derived from
 * tools/sh4/isa.py).  Every handler extracts its fields from the opcode word.
 *
 * Delay slots: a delayed branch computes its target (and PR) first, then
 * executes the slot instruction, then jumps.  BT/S and BF/S test T before the
 * slot.  Instructions that may not sit in a slot (branches, TRAPA, LDC,
 * PC-relative loads) fault there.
 *
 * Floating point: single precision ops run on the host SSE unit with MXCSR
 * set from FPSCR (RM=1 -> round toward zero, DN=1 -> FTZ+DAZ), which gives
 * IEEE results identical to the SH-4 for FADD/FSUB/FMUL/FDIV/FSQRT/FLOAT/FCMP.
 * FMAC is fused (exact product in double, one sum, truncation nests correctly
 * under RZ).  FIPR/FTRV are computed in double and rounded once (hardware has
 * its own internal precision: approximate).  FSRRA and FSCA are approximate.
 * These models live in include/pc/sh4_fpmodel.h (shared with the libsa1
 * runtime); FSCA uses the generated table libsa1/src/rt/rt_fsca_q.c.  NaN
 * results are the SH-4 default qNaN 0x7FBFFFFF.  FPU exception flags/cause
 * bits are not maintained.
 */
#include "sh4_internal.h"
#include "sh4_optab.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#define HAVE_MXCSR 1
#else
#include <fenv.h>
#endif

sh4_cpu SH4;

/* ------------------------------------------------------------------ */
/* host floating point environment                                     */

#define MXCSR_MASKALL 0x1F80u
#define MXCSR_RZ      0x6000u
#define MXCSR_FTZ     0x8000u
#define MXCSR_DAZ     0x0040u

uint32_t sh4_host_mxcsr_get(void)
{
#ifdef HAVE_MXCSR
    return _mm_getcsr();
#else
    return (uint32_t)fegetround();
#endif
}

void sh4_host_mxcsr_set(uint32_t v)
{
#ifdef HAVE_MXCSR
    _mm_setcsr(v);
#else
    fesetround((int)v);
#endif
}

static uint32_t mxcsr_for(uint32_t fpscr)
{
#ifdef HAVE_MXCSR
    uint32_t x = MXCSR_MASKALL;
    if ((fpscr & FPSCR_RM) != 0)          /* 1 = RZ; 2/3 are reserved: treat as RZ */
        x |= MXCSR_RZ;
    if (fpscr & FPSCR_DN)
        x |= MXCSR_FTZ | MXCSR_DAZ;
    return x;
#else
    return (fpscr & FPSCR_RM) ? FE_TOWARDZERO : FE_TONEAREST;
#endif
}

void sh4_fpu_sync_host(sh4_cpu *c)
{
    sh4_host_mxcsr_set(mxcsr_for(c->fpscr));
}

void sh4_host_fp_game_mode(void)
{
    sh4_host_mxcsr_set(mxcsr_for(SH4_DEFAULT_FPSCR));
}

/* ------------------------------------------------------------------ */
/* FPU models: include/pc/sh4_fpmodel.h, shared with the libsa1 runtime   */
/* (docs/libsa1_integration.md 8.2).  SH4_FPMODEL_EXTERN: this file holds  */
/* the external definitions that sh4.h declares (sh4_fipr, sh4_ftrv,       */
/* sh4_fsrra, sh4_fmac, sh4_fsqrt, sh4_ftrc).                              */

#define SH4_FPMODEL_EXTERN 1
#include "pc/sh4_fpmodel.h"

#define f2u    sh4_f2u
#define u2f    sh4_u2f
#define canon  sh4_canon
#define canond sh4_canond

/* FSCA reads the generated quarter-wave table sh4_fsca_q[]
 * (libsa1/src/rt/rt_fsca_q.c, compiled into the harness).  Until 2026-10-20
 * the interpreter built a full table at start-up with fsca_build(); that
 * function now lives in sh4_fsca_ref.c, and the selftest checks that the
 * loaded table is exactly what it computes. */
float sh4_fsca_sin(uint32_t angle)
{
    return sh4_fsca_sin16(angle);
}

float sh4_fsca_cos(uint32_t angle)
{
    return sh4_fsca_sin16(angle + 0x4000u);
}

static uint32_t ftrc_d(double d)
{
    if (d != d)
        return 0x80000000u;
    if (d >= 2147483648.0)
        return 0x7FFFFFFFu;
    if (d <= -2147483649.0)
        return 0x80000000u;
    return (uint32_t)(int32_t)d;
}

/* ------------------------------------------------------------------ */
/* faults                                                              */

static const char *const fault_names[SH4_FAULT_COUNT] = {
    "ok", "illegal instruction", "illegal slot instruction", "unmapped memory access",
    "misaligned access", "bad branch target", "instruction budget exhausted",
    "TRAPA", "SLEEP", "undefined FPU operation", "native access violation", "hook fault",
};

const char *sh4_fault_name(int kind)
{
    if (kind < 0 || kind >= SH4_FAULT_COUNT)
        return "?";
    return fault_names[kind];
}

void sh4_fault(sh4_cpu *c, int kind, uint32_t addr, const char *fmt, ...)
{
    va_list ap;
    c->fault.kind = kind;
    c->fault.pc = c->cur_pc;
    c->fault.op = c->cur_op;
    c->fault.addr = addr;
    va_start(ap, fmt);
    vsnprintf(c->fault.msg, sizeof c->fault.msg, fmt, ap);
    va_end(ap);
    c->in_slot = 0;
    if (sh4_guard_top) {
        longjmp(sh4_guard_top->jb, 1);
    }
    fprintf(stderr, "sh4: unguarded fault: %s at pc=%08X op=%04X addr=%08X: %s\n",
            sh4_fault_name(kind), c->fault.pc, c->fault.op, addr, c->fault.msg);
    abort();
}

/* ------------------------------------------------------------------ */
/* state                                                               */

const char *sh4_op_name(uint16_t op) { return sh4_opname[sh4_optab[op]]; }
int sh4_op_valid(uint16_t op) { return sh4_optab[op] != OP_ILLEGAL; }

static uint8_t slot_illegal[OP_COUNT];
static int tables_ready;

static void init_tables(void)
{
    static const uint8_t bad[] = {
        OP_BF, OP_BF_S, OP_BT, OP_BT_S, OP_BRA, OP_BSR, OP_BRAF_R, OP_BSRF_R, OP_JMP_AR,
        OP_JSR_AR, OP_RTS, OP_RTE, OP_TRAPA_I, OP_MOVA_PC_R, OP_MOV_W_PC_R, OP_MOV_L_PC_R,
        OP_LDC_R_SR, OP_LDC_R_GBR, OP_LDC_R_VBR, OP_LDC_R_SSR, OP_LDC_R_SPC, OP_LDC_R_DBR,
        OP_LDC_R_Rb, OP_LDC_L_RP_SR, OP_LDC_L_RP_GBR, OP_LDC_L_RP_VBR, OP_LDC_L_RP_SSR,
        OP_LDC_L_RP_SPC, OP_LDC_L_RP_DBR, OP_LDC_L_RP_Rb,
    };
    size_t i;
    for (i = 0; i < sizeof bad; i++)
        slot_illegal[bad[i]] = 1;
    tables_ready = 1;
}

void sh4_reset(sh4_cpu *c)
{
    uint32_t top = c->stack_top ? c->stack_top : SH4_DEFAULT_STACK;
    uint64_t ic = c->icount;
    int trace = c->trace;
    memset(c, 0, sizeof *c);
    c->icount = ic;
    c->trace = trace;
    c->sr = SR_MD | SR_IMASK;               /* privileged, interrupts masked, bank 0 */
    c->fpscr = SH4_DEFAULT_FPSCR;
    c->stack_top = top;
    c->r[15] = top;
    c->pr = SH4_SENTINEL;
    c->vbr = 0x8C000000u;
    if (!tables_ready)
        init_tables();
}

void sh4_set_sr(sh4_cpu *c, uint32_t v)
{
    v &= SR_MASK;
    if ((v ^ c->sr) & SR_RB) {
        int i;
        for (i = 0; i < 8; i++) {
            uint32_t t = c->r[i];
            c->r[i] = c->rbank[i];
            c->rbank[i] = t;
        }
    }
    c->sr = v;
}

static void swap_fbanks(sh4_cpu *c)
{
    int i;
    for (i = 0; i < 16; i++) {
        uint32_t t = c->fr.u[i];
        c->fr.u[i] = c->xf.u[i];
        c->xf.u[i] = t;
    }
}

void sh4_set_fpscr(sh4_cpu *c, uint32_t v)
{
    v &= FPSCR_MASK;
    if ((v ^ c->fpscr) & FPSCR_FR)
        swap_fbanks(c);
    if ((v ^ c->fpscr) & (FPSCR_RM | FPSCR_DN)) {
        c->fpscr = v;
        sh4_fpu_sync_host(c);
    }
    c->fpscr = v;
}

/* ------------------------------------------------------------------ */
/* trace                                                               */

static void trace_insn(sh4_cpu *c, uint32_t pc, uint16_t op)
{
    fprintf(stderr, "%08X %04X %-22s r0=%08X r1=%08X r2=%08X r3=%08X r4=%08X r5=%08X r15=%08X T=%u\n",
            pc, op, sh4_opname[sh4_optab[op]], c->r[0], c->r[1], c->r[2], c->r[3], c->r[4],
            c->r[5], c->r[15], c->sr & SR_T);
}
/* ------------------------------------------------------------------ */
/* the interpreter                                                     */

#define REGN c->r[n]
#define REGM c->r[m]
#define TBIT (c->sr & SR_T)
#define SETT(x) (c->sr = (c->sr & ~SR_T) | ((x) ? SR_T : 0u))
#define FR(i)  c->fr.f[i]
#define FRU(i) c->fr.u[i]
#define PRMODE (c->fpscr & FPSCR_PR)
#define SZMODE (c->fpscr & FPSCR_SZ)
#define SEXT8(x)  ((uint32_t)(int32_t)(int8_t)(x))
#define SEXT16(x) ((uint32_t)(int32_t)(int16_t)(x))

static inline double get_dr(sh4_cpu *c, int n)
{
    uint64_t v = ((uint64_t)c->fr.u[n] << 32) | c->fr.u[n + 1];
    double d;
    memcpy(&d, &v, 8);
    return d;
}

static inline void set_dr(sh4_cpu *c, int n, double d)
{
    uint64_t v;
    memcpy(&v, &d, 8);
    c->fr.u[n] = (uint32_t)(v >> 32);
    c->fr.u[n + 1] = (uint32_t)v;
}

/* register pair for SZ=1 moves: even = DRn (FR bank), odd = XDn (XF bank).
 * Little-endian pair moves: the even register <-> the lower address. */
static inline uint32_t *pair_reg(sh4_cpu *c, int r)
{
    return (r & 1) ? &c->xf.u[r & 14] : &c->fr.u[r & 14];
}

static inline uint16_t fetch(sh4_cpu *c, uint32_t addr)
{
    uint8_t *p = sh4_page[addr >> 20];
    if (SH4_LIKELY(p != NULL && (addr & 1) == 0)) {
        uint16_t v;
        memcpy(&v, p + (addr & 0xFFFFFu), 2);
        return v;
    }
    c->cur_pc = addr;
    c->cur_op = 0;
    sh4_fault(c, SH4_FAULT_BRANCH, addr, "instruction fetch from %08X", addr);
    return 0;
}

static void delay_slot(sh4_cpu *c, uint32_t slot_pc)
{
    c->in_slot = 1;
    c->pc = slot_pc;
    sh4_step(c);
    c->in_slot = 0;
}

static SH4_NOINLINE void undef_fpu(sh4_cpu *c)
{
    sh4_fault(c, SH4_FAULT_UNDEF_FPU, c->cur_pc, "%s undefined with FPSCR=%08X",
              sh4_opname[sh4_optab[c->cur_op]], c->fpscr);
}

static void div1(sh4_cpu *c, int m, int n)
{
    uint32_t tmp0, tmp2 = REGM;
    uint32_t old_q = (c->sr & SR_Q) ? 1 : 0;
    uint32_t q = (REGN & 0x80000000u) ? 1 : 0;
    uint32_t mm = (c->sr & SR_M) ? 1 : 0;
    uint32_t tmp1;
    REGN = (REGN << 1) | TBIT;
    if (!old_q) {
        if (!mm) {
            tmp0 = REGN; REGN -= tmp2; tmp1 = REGN > tmp0;
            q = q ? (tmp1 == 0) : tmp1;
        } else {
            tmp0 = REGN; REGN += tmp2; tmp1 = REGN < tmp0;
            q = q ? tmp1 : (tmp1 == 0);
        }
    } else {
        if (!mm) {
            tmp0 = REGN; REGN += tmp2; tmp1 = REGN < tmp0;
            q = q ? (tmp1 == 0) : tmp1;
        } else {
            tmp0 = REGN; REGN -= tmp2; tmp1 = REGN > tmp0;
            q = q ? tmp1 : (tmp1 == 0);
        }
    }
    c->sr = (c->sr & ~(SR_Q | SR_T)) | (q ? SR_Q : 0u) | ((q == mm) ? SR_T : 0u);
}

static void mac_l(sh4_cpu *c, int m, int n)
{
    int32_t vn = (int32_t)sh4_rd32(c, REGN);
    int32_t vm;
    int64_t mac, res;
    REGN += 4;
    vm = (int32_t)sh4_rd32(c, REGM);
    REGM += 4;
    mac = (int64_t)(((uint64_t)c->mach << 32) | c->macl);
    res = mac + (int64_t)vn * (int64_t)vm;
    if (c->sr & SR_S) {
        if (res > (int64_t)0x00007FFFFFFFFFFFll)
            res = 0x00007FFFFFFFFFFFll;
        else if (res < -(int64_t)0x0000800000000000ll)
            res = -(int64_t)0x0000800000000000ll;
    }
    c->mach = (uint32_t)((uint64_t)res >> 32);
    c->macl = (uint32_t)res;
}

static void mac_w(sh4_cpu *c, int m, int n)
{
    int32_t vn = (int16_t)sh4_rd16(c, REGN);
    int32_t vm;
    int64_t prod;
    REGN += 2;
    vm = (int16_t)sh4_rd16(c, REGM);
    REGM += 2;
    prod = (int64_t)vn * vm;
    if (c->sr & SR_S) {
        int64_t s = (int64_t)(int32_t)c->macl + prod;
        if (s > 0x7FFFFFFFll) {
            c->macl = 0x7FFFFFFFu;
            c->mach |= 1;
        } else if (s < -0x80000000ll) {
            c->macl = 0x80000000u;
            c->mach |= 1;
        } else {
            c->macl = (uint32_t)s;
        }
    } else {
        int64_t mac = (int64_t)(((uint64_t)c->mach << 32) | c->macl);
        mac += prod;
        c->mach = (uint32_t)((uint64_t)mac >> 32);
        c->macl = (uint32_t)mac;
    }
}

static inline uint32_t shad(uint32_t vn, uint32_t vm)
{
    int32_t s = (int32_t)vm;
    if (s >= 0)
        return vn << (vm & 31);
    if ((vm & 31) == 0)
        return ((int32_t)vn < 0) ? 0xFFFFFFFFu : 0;
    return (uint32_t)((int32_t)vn >> ((~vm & 31) + 1));
}

static inline uint32_t shld(uint32_t vn, uint32_t vm)
{
    int32_t s = (int32_t)vm;
    if (s >= 0)
        return vn << (vm & 31);
    if ((vm & 31) == 0)
        return 0;
    return vn >> ((~vm & 31) + 1);
}

void (*sh4_trap_handler)(sh4_cpu *c, uint32_t imm);
void (*sh4_sleep_handler)(sh4_cpu *c);

sh4_frame sh4_cstack[SH4_CSTACK_MAX];
int sh4_cstack_depth;
int sh4_cstack_on;
uint8_t *sh4_cov;               /* executed-instruction bitmap (1 bit per 16-bit word), or NULL */

void sh4_cstack_push(uint32_t target, uint32_t ret)
{
    if (sh4_cstack_on == 2)     /* call trace */
        fprintf(stderr, "%*scall %08X (%08X, %08X, %08X, %08X | %g, %g)\n", (sh4_cstack_depth % 32) * 2, "", target,
                SH4.r[4], SH4.r[5], SH4.r[6], SH4.r[7], (double)SH4.fr.f[4], (double)SH4.fr.f[5]);
    if (sh4_cstack_depth < SH4_CSTACK_MAX) {
        sh4_cstack[sh4_cstack_depth].target = target;
        sh4_cstack[sh4_cstack_depth].ret = ret;
    }
    sh4_cstack_depth++;
}

void sh4_cstack_ret(uint32_t to)
{
    int i = sh4_cstack_depth < SH4_CSTACK_MAX ? sh4_cstack_depth : SH4_CSTACK_MAX;
    while (i > 0) {
        i--;
        if (sh4_cstack[i].ret == to) {
            if (sh4_cstack_on == 2)
                fprintf(stderr, "%*sret  %08X -> R0=%08X FR0=%g\n", (i % 32) * 2, "", sh4_cstack[i].target, SH4.r[0],
                        (double)SH4.fr.f[0]);
            sh4_cstack_depth = i;
            return;
        }
    }
}

void sh4_step(sh4_cpu *c)
{
    uint32_t pc = c->pc;
    uint16_t op = fetch(c, pc);
    int n = (op >> 8) & 15, m = (op >> 4) & 15;
    uint8_t id = sh4_optab[op];
    uint32_t t, ea;

    c->cur_pc = pc;
    if (SH4_UNLIKELY(sh4_cov != NULL))
        sh4_cov[(pc & 0x00FFFFFFu) >> 4] |= (uint8_t)(1u << ((pc >> 1) & 7));
    c->cur_op = op;
    c->pc = pc + 2;
    c->icount++;
    if (SH4_UNLIKELY(c->trace))
        trace_insn(c, pc, op);
    if (SH4_UNLIKELY(c->in_slot) && slot_illegal[id])
        sh4_fault(c, SH4_FAULT_SLOT, pc, "%s in delay slot", sh4_opname[id]);

    switch (id) {
    /* ---------------- data transfer ---------------- */
    case OP_MOV_I_R: REGN = SEXT8(op); break;
    case OP_MOV_W_PC_R: REGN = SEXT16(sh4_rd16(c, pc + 4 + (op & 0xFF) * 2)); break;
    case OP_MOV_L_PC_R: REGN = sh4_rd32(c, ((pc + 4) & ~3u) + (op & 0xFF) * 4); break;
    case OP_MOVA_PC_R: c->r[0] = ((pc + 4) & ~3u) + (op & 0xFF) * 4; break;
    case OP_MOV_R_R: REGN = REGM; break;
    case OP_MOV_B_R_AR: sh4_wr8(c, REGN, REGM); break;
    case OP_MOV_W_R_AR: sh4_wr16(c, REGN, REGM); break;
    case OP_MOV_L_R_AR: sh4_wr32(c, REGN, REGM); break;
    case OP_MOV_B_AR_R: REGN = SEXT8(sh4_rd8(c, REGM)); break;
    case OP_MOV_W_AR_R: REGN = SEXT16(sh4_rd16(c, REGM)); break;
    case OP_MOV_L_AR_R: REGN = sh4_rd32(c, REGM); break;
    case OP_MOV_B_R_MR: ea = REGN - 1; sh4_wr8(c, ea, REGM); REGN = ea; break;
    case OP_MOV_W_R_MR: ea = REGN - 2; sh4_wr16(c, ea, REGM); REGN = ea; break;
    case OP_MOV_L_R_MR: ea = REGN - 4; sh4_wr32(c, ea, REGM); REGN = ea; break;
    case OP_MOV_B_RP_R: t = SEXT8(sh4_rd8(c, REGM)); if (n != m) REGM += 1; REGN = t; break;
    case OP_MOV_W_RP_R: t = SEXT16(sh4_rd16(c, REGM)); if (n != m) REGM += 2; REGN = t; break;
    case OP_MOV_L_RP_R: t = sh4_rd32(c, REGM); if (n != m) REGM += 4; REGN = t; break;
    /* MOV.B R0,@(d,Rm) / MOV.W R0,@(d,Rm): base register in the m field */
    case OP_MOV_B_R_DR: sh4_wr8(c, REGM + (op & 15), c->r[0]); break;
    case OP_MOV_W_R_DR: sh4_wr16(c, REGM + (op & 15) * 2, c->r[0]); break;
    /* MOV.L Rm,@(d,Rn) */
    case OP_MOV_L_R_DR: sh4_wr32(c, REGN + (op & 15) * 4, REGM); break;
    /* MOV.B @(d,Rm),R0 / MOV.W @(d,Rm),R0 / MOV.L @(d,Rm),Rn */
    case OP_MOV_B_DR_R: c->r[0] = SEXT8(sh4_rd8(c, REGM + (op & 15))); break;
    case OP_MOV_W_DR_R: c->r[0] = SEXT16(sh4_rd16(c, REGM + (op & 15) * 2)); break;
    case OP_MOV_L_DR_R: REGN = sh4_rd32(c, REGM + (op & 15) * 4); break;
    case OP_MOV_B_R_R0R: sh4_wr8(c, REGN + c->r[0], REGM); break;
    case OP_MOV_W_R_R0R: sh4_wr16(c, REGN + c->r[0], REGM); break;
    case OP_MOV_L_R_R0R: sh4_wr32(c, REGN + c->r[0], REGM); break;
    case OP_MOV_B_R0R_R: REGN = SEXT8(sh4_rd8(c, REGM + c->r[0])); break;
    case OP_MOV_W_R0R_R: REGN = SEXT16(sh4_rd16(c, REGM + c->r[0])); break;
    case OP_MOV_L_R0R_R: REGN = sh4_rd32(c, REGM + c->r[0]); break;
    case OP_MOV_B_R_DGBR: sh4_wr8(c, c->gbr + (op & 0xFF), c->r[0]); break;
    case OP_MOV_W_R_DGBR: sh4_wr16(c, c->gbr + (op & 0xFF) * 2, c->r[0]); break;
    case OP_MOV_L_R_DGBR: sh4_wr32(c, c->gbr + (op & 0xFF) * 4, c->r[0]); break;
    case OP_MOV_B_DGBR_R: c->r[0] = SEXT8(sh4_rd8(c, c->gbr + (op & 0xFF))); break;
    case OP_MOV_W_DGBR_R: c->r[0] = SEXT16(sh4_rd16(c, c->gbr + (op & 0xFF) * 2)); break;
    case OP_MOV_L_DGBR_R: c->r[0] = sh4_rd32(c, c->gbr + (op & 0xFF) * 4); break;
    case OP_MOVT_R: REGN = TBIT; break;
    case OP_SWAP_B_R_R: REGN = (REGM & 0xFFFF0000u) | ((REGM & 0xFF) << 8) | ((REGM >> 8) & 0xFF); break;
    case OP_SWAP_W_R_R: REGN = (REGM >> 16) | (REGM << 16); break;
    case OP_XTRCT_R_R: REGN = (REGN >> 16) | (REGM << 16); break;
    case OP_MOVCA_L_R_AR: sh4_wr32(c, REGN, c->r[0]); break;

    /* ---------------- arithmetic ---------------- */
    case OP_ADD_R_R: REGN += REGM; break;
    case OP_ADD_I_R: REGN += SEXT8(op); break;
    case OP_ADDC_R_R: {
        uint32_t tmp1 = REGN + REGM, tmp0 = REGN;
        REGN = tmp1 + TBIT;
        SETT(tmp0 > tmp1 || tmp1 > REGN);
        break;
    }
    case OP_ADDV_R_R: {
        uint32_t r = REGN + REGM;
        SETT(((~(REGN ^ REGM)) & (REGN ^ r)) >> 31);
        REGN = r;
        break;
    }
    case OP_CMP_EQ_I_R: SETT(c->r[0] == SEXT8(op)); break;
    case OP_CMP_EQ_R_R: SETT(REGN == REGM); break;
    case OP_CMP_HS_R_R: SETT(REGN >= REGM); break;
    case OP_CMP_GE_R_R: SETT((int32_t)REGN >= (int32_t)REGM); break;
    case OP_CMP_HI_R_R: SETT(REGN > REGM); break;
    case OP_CMP_GT_R_R: SETT((int32_t)REGN > (int32_t)REGM); break;
    case OP_CMP_PZ_R: SETT((int32_t)REGN >= 0); break;
    case OP_CMP_PL_R: SETT((int32_t)REGN > 0); break;
    case OP_CMP_STR_R_R: {
        uint32_t x = REGN ^ REGM;
        SETT(!(x & 0xFF000000u) || !(x & 0x00FF0000u) || !(x & 0x0000FF00u) || !(x & 0x000000FFu));
        break;
    }
    case OP_DIV0S_R_R: {
        uint32_t q = REGN >> 31, mm = REGM >> 31;
        c->sr = (c->sr & ~(SR_Q | SR_M | SR_T)) | (q ? SR_Q : 0u) | (mm ? SR_M : 0u) | ((q ^ mm) ? SR_T : 0u);
        break;
    }
    case OP_DIV0U: c->sr &= ~(SR_Q | SR_M | SR_T); break;
    case OP_DIV1_R_R: div1(c, m, n); break;
    case OP_DMULS_L_R_R: {
        int64_t r = (int64_t)(int32_t)REGN * (int64_t)(int32_t)REGM;
        c->mach = (uint32_t)((uint64_t)r >> 32);
        c->macl = (uint32_t)r;
        break;
    }
    case OP_DMULU_L_R_R: {
        uint64_t r = (uint64_t)REGN * (uint64_t)REGM;
        c->mach = (uint32_t)(r >> 32);
        c->macl = (uint32_t)r;
        break;
    }
    case OP_DT_R: REGN -= 1; SETT(REGN == 0); break;
    case OP_EXTS_B_R_R: REGN = SEXT8(REGM); break;
    case OP_EXTS_W_R_R: REGN = SEXT16(REGM); break;
    case OP_EXTU_B_R_R: REGN = REGM & 0xFF; break;
    case OP_EXTU_W_R_R: REGN = REGM & 0xFFFF; break;
    case OP_MAC_L_RP_RP: mac_l(c, m, n); break;
    case OP_MAC_W_RP_RP: mac_w(c, m, n); break;
    case OP_MUL_L_R_R: c->macl = REGN * REGM; break;
    case OP_MULS_W_R_R: c->macl = (uint32_t)((int32_t)(int16_t)REGN * (int32_t)(int16_t)REGM); break;
    case OP_MULU_W_R_R: c->macl = (uint32_t)(REGN & 0xFFFF) * (REGM & 0xFFFF); break;
    case OP_NEG_R_R: REGN = 0u - REGM; break;
    case OP_NEGC_R_R: {
        uint32_t tmp = 0u - REGM;
        REGN = tmp - TBIT;
        SETT(0 < tmp || tmp < REGN);
        break;
    }
    case OP_SUB_R_R: REGN -= REGM; break;
    case OP_SUBC_R_R: {
        uint32_t tmp1 = REGN - REGM, tmp0 = REGN;
        REGN = tmp1 - TBIT;
        SETT(tmp0 < tmp1 || tmp1 < REGN);
        break;
    }
    case OP_SUBV_R_R: {
        uint32_t r = REGN - REGM;
        SETT(((REGN ^ REGM) & (REGN ^ r)) >> 31);
        REGN = r;
        break;
    }
    /* ---------------- logic ---------------- */
    case OP_AND_R_R: REGN &= REGM; break;
    case OP_AND_I_R: c->r[0] &= (op & 0xFF); break;
    case OP_AND_B_I_R0GBR: ea = c->gbr + c->r[0]; sh4_wr8(c, ea, sh4_rd8(c, ea) & (op & 0xFF)); break;
    case OP_NOT_R_R: REGN = ~REGM; break;
    case OP_OR_R_R: REGN |= REGM; break;
    case OP_OR_I_R: c->r[0] |= (op & 0xFF); break;
    case OP_OR_B_I_R0GBR: ea = c->gbr + c->r[0]; sh4_wr8(c, ea, sh4_rd8(c, ea) | (op & 0xFF)); break;
    case OP_TAS_B_AR: t = sh4_rd8(c, REGN); SETT(t == 0); sh4_wr8(c, REGN, t | 0x80); break;
    case OP_TST_R_R: SETT((REGN & REGM) == 0); break;
    case OP_TST_I_R: SETT((c->r[0] & (op & 0xFF)) == 0); break;
    case OP_TST_B_I_R0GBR: SETT((sh4_rd8(c, c->gbr + c->r[0]) & (op & 0xFF)) == 0); break;
    case OP_XOR_R_R: REGN ^= REGM; break;
    case OP_XOR_I_R: c->r[0] ^= (op & 0xFF); break;
    case OP_XOR_B_I_R0GBR: ea = c->gbr + c->r[0]; sh4_wr8(c, ea, sh4_rd8(c, ea) ^ (op & 0xFF)); break;

    /* ---------------- shifts ---------------- */
    case OP_ROTL_R: t = REGN >> 31; REGN = (REGN << 1) | t; SETT(t); break;
    case OP_ROTR_R: t = REGN & 1; REGN = (REGN >> 1) | (t << 31); SETT(t); break;
    case OP_ROTCL_R: t = REGN >> 31; REGN = (REGN << 1) | TBIT; SETT(t); break;
    case OP_ROTCR_R: t = REGN & 1; REGN = (REGN >> 1) | (TBIT << 31); SETT(t); break;
    case OP_SHAD_R_R: REGN = shad(REGN, REGM); break;
    case OP_SHAL_R: SETT(REGN >> 31); REGN <<= 1; break;
    case OP_SHAR_R: SETT(REGN & 1); REGN = (uint32_t)((int32_t)REGN >> 1); break;
    case OP_SHLD_R_R: REGN = shld(REGN, REGM); break;
    case OP_SHLL_R: SETT(REGN >> 31); REGN <<= 1; break;
    case OP_SHLR_R: SETT(REGN & 1); REGN >>= 1; break;
    case OP_SHLL2_R: REGN <<= 2; break;
    case OP_SHLR2_R: REGN >>= 2; break;
    case OP_SHLL8_R: REGN <<= 8; break;
    case OP_SHLR8_R: REGN >>= 8; break;
    case OP_SHLL16_R: REGN <<= 16; break;
    case OP_SHLR16_R: REGN >>= 16; break;

    /* ---------------- branches ---------------- */
    case OP_BF:
        if (!TBIT)
            sh4_branch(c, pc + 4 + SEXT8(op) * 2);
        break;
    case OP_BT:
        if (TBIT)
            sh4_branch(c, pc + 4 + SEXT8(op) * 2);
        break;
    case OP_BF_S:
        if (!TBIT) {
            t = pc + 4 + SEXT8(op) * 2;
            delay_slot(c, pc + 2);
            sh4_branch(c, t);
        } else {
            delay_slot(c, pc + 2);
        }
        break;
    case OP_BT_S:
        if (TBIT) {
            t = pc + 4 + SEXT8(op) * 2;
            delay_slot(c, pc + 2);
            sh4_branch(c, t);
        } else {
            delay_slot(c, pc + 2);
        }
        break;
    case OP_BRA:
        t = pc + 4 + (uint32_t)(((int32_t)((uint32_t)op << 20)) >> 19);
        delay_slot(c, pc + 2);
        sh4_branch(c, t);
        break;
    case OP_BSR:
        t = pc + 4 + (uint32_t)(((int32_t)((uint32_t)op << 20)) >> 19);
        c->pr = pc + 4;
        delay_slot(c, pc + 2);
        if (SH4_UNLIKELY(sh4_cstack_on))
            sh4_cstack_push(t, pc + 4);
        sh4_branch(c, t);
        break;
    case OP_BRAF_R:
        t = pc + 4 + REGN;
        delay_slot(c, pc + 2);
        sh4_branch(c, t);
        break;
    case OP_BSRF_R:
        t = pc + 4 + REGN;
        c->pr = pc + 4;
        delay_slot(c, pc + 2);
        if (SH4_UNLIKELY(sh4_cstack_on))
            sh4_cstack_push(t, pc + 4);
        sh4_branch(c, t);
        break;
    case OP_JMP_AR:
        t = REGN;
        delay_slot(c, pc + 2);
        sh4_branch(c, t);
        break;
    case OP_JSR_AR:
        t = REGN;
        c->pr = pc + 4;
        delay_slot(c, pc + 2);
        if (SH4_UNLIKELY(sh4_cstack_on))
            sh4_cstack_push(t, pc + 4);
        sh4_branch(c, t);
        break;
    case OP_RTS:
        t = c->pr;
        delay_slot(c, pc + 2);
        if (SH4_UNLIKELY(sh4_cstack_on))
            sh4_cstack_ret(t);
        sh4_branch(c, t);
        break;
    case OP_RTE:
        t = c->spc;
        sh4_set_sr(c, c->ssr);
        delay_slot(c, pc + 2);
        sh4_branch(c, t);
        break;

    /* ---------------- system ---------------- */
    case OP_NOP: break;
    case OP_CLRMAC: c->mach = c->macl = 0; break;
    case OP_CLRS: c->sr &= ~SR_S; break;
    case OP_SETS: c->sr |= SR_S; break;
    case OP_CLRT: c->sr &= ~SR_T; break;
    case OP_SETT: c->sr |= SR_T; break;
    case OP_LDTLB: break;
    case OP_PREF_AR:
        if ((REGN & 0xFC000000u) == 0xE0000000u)
            sh4_sq_pref(c, REGN);
        break;
    case OP_OCBI_AR: case OP_OCBP_AR: case OP_OCBWB_AR: break;
    case OP_SLEEP:
        if (sh4_sleep_handler)
            sh4_sleep_handler(c);
        else
            sh4_fault(c, SH4_FAULT_SLEEP, pc, "SLEEP");
        break;
    case OP_TRAPA_I:
        if (sh4_trap_handler)
            sh4_trap_handler(c, op & 0xFF);
        else
            sh4_fault(c, SH4_FAULT_TRAP, op & 0xFF, "TRAPA #%u", op & 0xFF);
        break;

    case OP_LDC_R_SR: sh4_set_sr(c, REGN); break;
    case OP_LDC_R_GBR: c->gbr = REGN; break;
    case OP_LDC_R_VBR: c->vbr = REGN; break;
    case OP_LDC_R_SSR: c->ssr = REGN; break;
    case OP_LDC_R_SPC: c->spc = REGN; break;
    case OP_LDC_R_DBR: c->dbr = REGN; break;
    case OP_LDC_R_Rb: c->rbank[m & 7] = REGN; break;
    case OP_LDC_L_RP_SR: t = sh4_rd32(c, REGN); REGN += 4; sh4_set_sr(c, t); break;
    case OP_LDC_L_RP_GBR: c->gbr = sh4_rd32(c, REGN); REGN += 4; break;
    case OP_LDC_L_RP_VBR: c->vbr = sh4_rd32(c, REGN); REGN += 4; break;
    case OP_LDC_L_RP_SSR: c->ssr = sh4_rd32(c, REGN); REGN += 4; break;
    case OP_LDC_L_RP_SPC: c->spc = sh4_rd32(c, REGN); REGN += 4; break;
    case OP_LDC_L_RP_DBR: c->dbr = sh4_rd32(c, REGN); REGN += 4; break;
    case OP_LDC_L_RP_Rb: c->rbank[m & 7] = sh4_rd32(c, REGN); REGN += 4; break;
    case OP_STC_SR_R: REGN = c->sr; break;
    case OP_STC_GBR_R: REGN = c->gbr; break;
    case OP_STC_VBR_R: REGN = c->vbr; break;
    case OP_STC_SSR_R: REGN = c->ssr; break;
    case OP_STC_SPC_R: REGN = c->spc; break;
    case OP_STC_SGR_R: REGN = c->sgr; break;
    case OP_STC_DBR_R: REGN = c->dbr; break;
    case OP_STC_Rb_R: REGN = c->rbank[m & 7]; break;
    case OP_STC_L_SR_MR: ea = REGN - 4; sh4_wr32(c, ea, c->sr); REGN = ea; break;
    case OP_STC_L_GBR_MR: ea = REGN - 4; sh4_wr32(c, ea, c->gbr); REGN = ea; break;
    case OP_STC_L_VBR_MR: ea = REGN - 4; sh4_wr32(c, ea, c->vbr); REGN = ea; break;
    case OP_STC_L_SSR_MR: ea = REGN - 4; sh4_wr32(c, ea, c->ssr); REGN = ea; break;
    case OP_STC_L_SPC_MR: ea = REGN - 4; sh4_wr32(c, ea, c->spc); REGN = ea; break;
    case OP_STC_L_SGR_MR: ea = REGN - 4; sh4_wr32(c, ea, c->sgr); REGN = ea; break;
    case OP_STC_L_DBR_MR: ea = REGN - 4; sh4_wr32(c, ea, c->dbr); REGN = ea; break;
    case OP_STC_L_Rb_MR: ea = REGN - 4; sh4_wr32(c, ea, c->rbank[m & 7]); REGN = ea; break;

    case OP_LDS_R_MACH: c->mach = REGN; break;
    case OP_LDS_R_MACL: c->macl = REGN; break;
    case OP_LDS_R_PR: c->pr = REGN; break;
    case OP_LDS_R_FPUL: c->fpul = REGN; break;
    case OP_LDS_R_FPSCR: sh4_set_fpscr(c, REGN); break;
    case OP_LDS_L_RP_MACH: c->mach = sh4_rd32(c, REGN); REGN += 4; break;
    case OP_LDS_L_RP_MACL: c->macl = sh4_rd32(c, REGN); REGN += 4; break;
    case OP_LDS_L_RP_PR: c->pr = sh4_rd32(c, REGN); REGN += 4; break;
    case OP_LDS_L_RP_FPUL: c->fpul = sh4_rd32(c, REGN); REGN += 4; break;
    case OP_LDS_L_RP_FPSCR: t = sh4_rd32(c, REGN); REGN += 4; sh4_set_fpscr(c, t); break;
    case OP_STS_MACH_R: REGN = c->mach; break;
    case OP_STS_MACL_R: REGN = c->macl; break;
    case OP_STS_PR_R: REGN = c->pr; break;
    case OP_STS_FPUL_R: REGN = c->fpul; break;
    case OP_STS_FPSCR_R: REGN = c->fpscr; break;
    case OP_STS_L_MACH_MR: ea = REGN - 4; sh4_wr32(c, ea, c->mach); REGN = ea; break;
    case OP_STS_L_MACL_MR: ea = REGN - 4; sh4_wr32(c, ea, c->macl); REGN = ea; break;
    case OP_STS_L_PR_MR: ea = REGN - 4; sh4_wr32(c, ea, c->pr); REGN = ea; break;
    case OP_STS_L_FPUL_MR: ea = REGN - 4; sh4_wr32(c, ea, c->fpul); REGN = ea; break;
    case OP_STS_L_FPSCR_MR: ea = REGN - 4; sh4_wr32(c, ea, c->fpscr); REGN = ea; break;
    /* ---------------- FPU: moves ---------------- */
    case OP_FMOV_F_F:
        if (!SZMODE) {
            FRU(n) = FRU(m);
        } else {
            uint32_t *s = pair_reg(c, m), *d = pair_reg(c, n);
            uint32_t s0 = s[0], s1 = s[1];
            d[0] = s0;
            d[1] = s1;
        }
        break;
    case OP_FMOV_S_AR_F:
        if (!SZMODE) {
            FRU(n) = sh4_rd32(c, REGM);
        } else {
            uint32_t *d = pair_reg(c, n);
            uint32_t v0 = sh4_rd32(c, REGM), v1 = sh4_rd32(c, REGM + 4);
            d[0] = v0;
            d[1] = v1;
        }
        break;
    case OP_FMOV_S_RP_F:
        if (!SZMODE) {
            FRU(n) = sh4_rd32(c, REGM);
            REGM += 4;
        } else {
            uint32_t *d = pair_reg(c, n);
            uint32_t v0 = sh4_rd32(c, REGM), v1 = sh4_rd32(c, REGM + 4);
            d[0] = v0;
            d[1] = v1;
            REGM += 8;
        }
        break;
    case OP_FMOV_S_R0R_F:
        ea = REGM + c->r[0];
        if (!SZMODE) {
            FRU(n) = sh4_rd32(c, ea);
        } else {
            uint32_t *d = pair_reg(c, n);
            uint32_t v0 = sh4_rd32(c, ea), v1 = sh4_rd32(c, ea + 4);
            d[0] = v0;
            d[1] = v1;
        }
        break;
    case OP_FMOV_S_F_AR:
        if (!SZMODE) {
            sh4_wr32(c, REGN, FRU(m));
        } else {
            uint32_t *s = pair_reg(c, m);
            sh4_wr32(c, REGN, s[0]);
            sh4_wr32(c, REGN + 4, s[1]);
        }
        break;
    case OP_FMOV_S_F_MR:
        if (!SZMODE) {
            ea = REGN - 4;
            sh4_wr32(c, ea, FRU(m));
        } else {
            uint32_t *s = pair_reg(c, m);
            ea = REGN - 8;
            sh4_wr32(c, ea, s[0]);
            sh4_wr32(c, ea + 4, s[1]);
        }
        REGN = ea;
        break;
    case OP_FMOV_S_F_R0R:
        ea = REGN + c->r[0];
        if (!SZMODE) {
            sh4_wr32(c, ea, FRU(m));
        } else {
            uint32_t *s = pair_reg(c, m);
            sh4_wr32(c, ea, s[0]);
            sh4_wr32(c, ea + 4, s[1]);
        }
        break;
    case OP_FLDI0_F: if (PRMODE) undef_fpu(c); FRU(n) = 0x00000000u; break;
    case OP_FLDI1_F: if (PRMODE) undef_fpu(c); FRU(n) = 0x3F800000u; break;
    case OP_FLDS_F_FPUL: c->fpul = FRU(n); break;
    case OP_FSTS_FPUL_F: FRU(n) = c->fpul; break;

    /* ---------------- FPU: arithmetic ---------------- */
    case OP_FADD_F_F:
        if (!PRMODE) FR(n) = canon(FR(n) + FR(m));
        else set_dr(c, n & 14, canond(get_dr(c, n & 14) + get_dr(c, m & 14)));
        break;
    case OP_FSUB_F_F:
        if (!PRMODE) FR(n) = canon(FR(n) - FR(m));
        else set_dr(c, n & 14, canond(get_dr(c, n & 14) - get_dr(c, m & 14)));
        break;
    case OP_FMUL_F_F:
        if (!PRMODE) FR(n) = canon(FR(n) * FR(m));
        else set_dr(c, n & 14, canond(get_dr(c, n & 14) * get_dr(c, m & 14)));
        break;
    case OP_FDIV_F_F:
        if (!PRMODE) FR(n) = canon(FR(n) / FR(m));
        else set_dr(c, n & 14, canond(get_dr(c, n & 14) / get_dr(c, m & 14)));
        break;
    case OP_FMAC_F_F_F:
        if (PRMODE) undef_fpu(c);
        FR(n) = sh4_fmac(FR(0), FR(m), FR(n));
        break;
    case OP_FCMP_EQ_F_F:
        if (!PRMODE) SETT(FR(n) == FR(m));
        else SETT(get_dr(c, n & 14) == get_dr(c, m & 14));
        break;
    case OP_FCMP_GT_F_F:
        if (!PRMODE) SETT(FR(n) > FR(m));
        else SETT(get_dr(c, n & 14) > get_dr(c, m & 14));
        break;
    case OP_FNEG_F:
        if (!PRMODE) FRU(n) ^= 0x80000000u;
        else FRU(n & 14) ^= 0x80000000u;
        break;
    case OP_FABS_F:
        if (!PRMODE) FRU(n) &= 0x7FFFFFFFu;
        else FRU(n & 14) &= 0x7FFFFFFFu;
        break;
    case OP_FSQRT_F:
        if (!PRMODE) FR(n) = sh4_fsqrt(FR(n));
        else set_dr(c, n & 14, canond(__builtin_sqrt(get_dr(c, n & 14))));
        break;
    case OP_FLOAT_FPUL_F:
        if (!PRMODE) FR(n) = (float)(int32_t)c->fpul;
        else set_dr(c, n & 14, (double)(int32_t)c->fpul);
        break;
    case OP_FTRC_F_FPUL:
        if (!PRMODE) c->fpul = sh4_ftrc(FR(n));
        else c->fpul = ftrc_d(get_dr(c, n & 14));
        break;
    case OP_FCNVSD_FPUL_D:
        if (!PRMODE) undef_fpu(c);
        set_dr(c, n & 14, (double)u2f(c->fpul));
        break;
    case OP_FCNVDS_D_FPUL:
        if (!PRMODE) undef_fpu(c);
        c->fpul = f2u(canon((float)get_dr(c, n & 14)));
        break;
    case OP_FIPR_V_V: {
        int vm = ((op >> 8) & 3) * 4, vn = ((op >> 10) & 3) * 4;
        if (PRMODE) undef_fpu(c);
        FR(vn + 3) = sh4_fipr(&FR(vm), &FR(vn));
        break;
    }
    case OP_FTRV_X_V: {
        int vn = ((op >> 10) & 3) * 4;
        if (PRMODE) undef_fpu(c);
        sh4_ftrv(c->xf.f, &FR(vn), &FR(vn));
        break;
    }
    case OP_FSCA_FPUL_D:
        if (PRMODE) undef_fpu(c);
        sh4_fsca(c->fpul, &FR(n & 14), &FR((n & 14) + 1));
        break;
    case OP_FSRRA_F:
        if (PRMODE) undef_fpu(c);
        FR(n) = sh4_fsrra(FR(n));
        break;
    case OP_FRCHG:
        if (PRMODE) undef_fpu(c);
        sh4_set_fpscr(c, c->fpscr ^ FPSCR_FR);
        break;
    case OP_FSCHG:
        if (PRMODE) undef_fpu(c);
        c->fpscr ^= FPSCR_SZ;
        break;

    case OP_ILLEGAL:
    default:
        sh4_fault(c, SH4_FAULT_ILLEGAL, pc, "illegal instruction %04X at %08X", op, pc);
        break;
    }
}

void sh4_run(sh4_cpu *c)
{
    while (!c->stop) {
        sh4_step(c);
        if (SH4_UNLIKELY(c->ilimit != 0 && c->icount >= c->ilimit))
            sh4_fault(c, SH4_FAULT_TIMEOUT, c->pc, "instruction budget exhausted (%llu)",
                      (unsigned long long)c->icount);
    }
    c->stop = 0;
}