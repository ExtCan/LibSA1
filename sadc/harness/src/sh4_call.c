/*
 * sh4_call.c - host <-> guest calls.
 *
 *   sh4_invoke     run a guest function per the SHC ABI (R4-R7, FR4-FR11,
 *                  further arguments on the stack in declaration order),
 *                  re-entrant: native code called from a hook may call back
 *                  into the guest; nested calls continue on the guest stack.
 *   hooks          guest address -> host function (HLE stubs, native routing)
 *   guards         setjmp-based recovery from guest faults and from access
 *                  violations in native code
 *   native glue    guest jumps to native function pointers, native calls
 *                  through guest function pointers (DEP fault -> trampoline)
 */
#include "sh4_internal.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

sh4_guard *sh4_guard_top;
void *sh4_guard_top_get(void) { return sh4_guard_top; }
int sh4_hook_log;

/* ------------------------------------------------------------------ */
/* guards                                                              */

void sh4_guard_push(sh4_guard *g)
{
    g->prev = sh4_guard_top;
    g->depth = SH4.depth;
    g->r15 = SH4.r[15];
    g->pr = SH4.pr;
    g->pc = SH4.pc;
    sh4_guard_top = g;
}

void sh4_guard_pop(sh4_guard *g)
{
    sh4_guard_top = g->prev;
    if (SH4.depth != g->depth) {        /* unwound by a fault */
        SH4.depth = g->depth;
        SH4.r[15] = g->r15;
        SH4.pr = g->pr;
        SH4.pc = g->pc;
    }
    SH4.stop = 0;
    SH4.in_slot = 0;
}

/* ------------------------------------------------------------------ */
/* hooks                                                               */

#define HOOK_N 16384
typedef struct hook_ent {
    uint32_t off;               /* RAM offset + 1 (0 = empty) */
    uint8_t dead;
    uint8_t route;              /* installed by sh4_route_native */
    sh4_hook_fn fn;
    void *user;
    const char *name;
} hook_ent;
static hook_ent hooks[HOOK_N];
static int route_on;

static hook_ent *hook_slot(uint32_t off, int create)
{
    uint32_t h = ((off >> 1) * 2654435761u) >> 18;
    int i;
    hook_ent *tomb = NULL;
    for (i = 0; i < HOOK_N; i++) {
        hook_ent *e = &hooks[(h + (uint32_t)i) & (HOOK_N - 1)];
        if (e->off == off + 1 && !e->dead)
            return e;
        if (e->off == 0) {
            if (!create)
                return NULL;
            if (tomb)
                e = tomb;
            e->off = off + 1;
            e->dead = 0;
            return e;
        }
        if (e->dead && !tomb)
            tomb = e;
    }
    if (create && tomb) {
        tomb->off = off + 1;
        tomb->dead = 0;
        return tomb;
    }
    return NULL;
}

static void setbit(uint32_t off, int on)
{
    uint32_t hw = off >> 1;
    if (on)
        sh4_hookbits[hw >> 3] |= (uint8_t)(1u << (hw & 7));
    else
        sh4_hookbits[hw >> 3] &= (uint8_t)~(1u << (hw & 7));
}

static int hook_add_ex(uint32_t addr, sh4_hook_fn fn, void *user, const char *name, int route)
{
    hook_ent *e;
    uint32_t off;
    if (!sh4_in_ram(addr) || (addr & 1))
        return -1;
    off = addr & 0x00FFFFFFu;
    e = hook_slot(off, 1);
    if (!e)
        return -1;
    if (route && e->fn && !e->route)
        return 0;               /* an explicit HLE hook wins over routing */
    e->fn = fn;
    e->user = user;
    e->name = name;
    e->route = (uint8_t)route;
    setbit(off, 1);
    return 0;
}

int sh4_hook_add(uint32_t addr, sh4_hook_fn fn, void *user, const char *name)
{
    return hook_add_ex(addr, fn, user, name, 0);
}

void sh4_hook_remove(uint32_t addr)
{
    hook_ent *e;
    uint32_t off;
    if (!sh4_in_ram(addr))
        return;
    off = addr & 0x00FFFFFFu;
    e = hook_slot(off, 0);
    if (!e)
        return;
    e->dead = 1;
    e->fn = NULL;
    setbit(off, 0);
}

void sh4_hook_clear(void)
{
    memset(hooks, 0, sizeof hooks);
    memset(sh4_hookbits, 0, sizeof sh4_hookbits);
}

int sh4_hook_find(uint32_t addr, sh4_hook_fn *fn, void **user, const char **name)
{
    hook_ent *e;
    if (!sh4_in_ram(addr))
        return 0;
    e = hook_slot(addr & 0x00FFFFFFu, 0);
    if (!e || !e->fn)
        return 0;
    if (fn) *fn = e->fn;
    if (user) *user = e->user;
    if (name) *name = e->name;
    return 1;
}

void sh4_hle_ret0(sh4_cpu *c, void *u) { (void)u; c->r[0] = 0; c->fr.f[0] = 0.0f; }
void sh4_hle_ret1(sh4_cpu *c, void *u) { (void)u; c->r[0] = 1; }
void sh4_hle_retu(sh4_cpu *c, void *u) { c->r[0] = (uint32_t)(uintptr_t)u; }

uint32_t sh4_stack_arg(sh4_cpu *c, int i)
{
    return sh4_rd32(c, c->r[15] + (uint32_t)i * 4);
}

/* ------------------------------------------------------------------ */
/* native registry                                                     */

const sh4_native_fn *sh4_native_by_guest(uint32_t guest)
{
    int i;
    if (!guest)
        return NULL;
    for (i = 0; i < sh4_native_fn_count; i++)
        if ((sh4_native_fns[i].guest & 0x1FFFFFFFu) == (guest & 0x1FFFFFFFu))
            return &sh4_native_fns[i];
    return NULL;
}

const sh4_native_fn *sh4_native_by_host(const void *host)
{
    int i;
    for (i = 0; i < sh4_native_fn_count; i++)
        if (sh4_native_fns[i].host == host)
            return &sh4_native_fns[i];
    return NULL;
}

const sh4_thunk_info *sh4_thunk_by_host(const void *host)
{
    int i;
    for (i = 0; i < sh4_thunk_count; i++)
        if (sh4_thunks[i].host == host)
            return &sh4_thunks[i];
    return NULL;
}

const sh4_thunk_info *sh4_thunk_by_guest(uint32_t guest)
{
    int i;
    for (i = 0; i < sh4_thunk_count; i++)
        if ((sh4_thunks[i].guest & 0x1FFFFFFFu) == (guest & 0x1FFFFFFFu))
            return &sh4_thunks[i];
    return NULL;
}

uint32_t sh4_canon_code_ptr(uint32_t v)
{
    const sh4_thunk_info *t;
    const sh4_native_fn *nf;
    if (sizeof(void *) != 4 || v == 0)
        return v;
    t = sh4_thunk_by_host((const void *)(uintptr_t)v);
    if (t)
        return t->guest;
    nf = sh4_native_by_host((const void *)(uintptr_t)v);
    if (nf && nf->guest)
        return nf->guest;
    return v;
}

static void route_hook(sh4_cpu *c, void *user)
{
    const sh4_native_fn *nf = (const sh4_native_fn *)user;
    nf->adapter(c);
}

void sh4_route_native(int on)
{
    int i;
    route_on = on;
    for (i = 0; i < sh4_native_fn_count; i++) {
        const sh4_native_fn *nf = &sh4_native_fns[i];
        hook_ent *e;
        if (!nf->guest || !nf->adapter)
            continue;
        if (on) {
            hook_add_ex(nf->guest, route_hook, (void *)nf, nf->name, 1);
        } else {
            e = hook_slot(nf->guest & 0x00FFFFFFu, 0);
            if (e && e->route) {
                e->dead = 1;
                e->fn = NULL;
                setbit(nf->guest & 0x00FFFFFFu, 0);
            }
        }
    }
}

int sh4_route_native_enabled(void) { return route_on; }

/* routing is on and this native function's hook is still installed (the trace
 * command's --route-only/--route-except remove the hooks of the others) */
static int routed(const sh4_native_fn *nf)
{
    void *user = NULL;
    sh4_hook_fn fn = NULL;
    return route_on && nf->guest && sh4_hook_find(nf->guest, &fn, &user, NULL) &&
           fn == route_hook && user == (void *)nf;
}

/* ------------------------------------------------------------------ */
/* special hooks: synthetic code addresses outside main RAM            */

#define SPECIAL_N 8
static struct {
    uint32_t key;               /* addr & 0x1FFFFFFF */
    sh4_hook_fn fn;
    void *user;
    const void *native;
    const char *name;
} specials[SPECIAL_N];
static int n_specials;

int sh4_special_hook_add(uint32_t addr, sh4_hook_fn fn, void *user, const void *native, const char *name)
{
    int k;
    uint32_t key = addr & 0x1FFFFFFFu;
    if (sh4_in_ram(addr) || !fn)
        return -1;
    for (k = 0; k < n_specials && specials[k].key != key; k++)
        ;
    if (k == n_specials) {
        if (n_specials == SPECIAL_N)
            return -1;
        n_specials++;
    }
    specials[k].key = key;
    specials[k].fn = fn;
    specials[k].user = user;
    specials[k].native = native;
    specials[k].name = name;
    return 0;
}

static int special_of(uint32_t addr)
{
    int k;
    for (k = 0; k < n_specials; k++)
        if (specials[k].key == (addr & 0x1FFFFFFFu))
            return k;
    return -1;
}

/* ------------------------------------------------------------------ */
/* special branch targets                                              */

static void log_hook(sh4_cpu *c, uint32_t addr)
{
    sh4_event e;
    int i;
    memset(&e, 0, sizeof e);
    e.type = SH4_EV_HOOK;
    e.addr = addr;
    e.val = c->r[4];
    for (i = 0; i < 3; i++)
        e.a[i] = c->r[5 + i];
    for (i = 0; i < 4; i++)
        e.f[i] = c->fr.u[4 + i];
    sh4_evlog_add(&e);
}

void sh4_branch_special(sh4_cpu *c)
{
    for (;;) {
        uint32_t t = c->pc;
        if (t == SH4_SENTINEL) {
            c->stop = 1;
            return;
        }
        if (sh4_in_ram(t)) {
            hook_ent *e = hook_slot(t & 0x00FFFFFFu, 0);
            uint32_t ret;
            if (!e || !e->fn)
                return;
            ret = c->pr;
            if (sh4_hook_log)
                log_hook(c, t);
            e->fn(c, e->user);
            if (sh4_cstack_on)
                sh4_cstack_ret(ret);
            c->pc = ret;
            continue;
        }
        if (n_specials) {
            int k = special_of(t);
            if (k >= 0) {               /* a synthetic code address (sh4_special_hook_add) */
                uint32_t ret = c->pr;
                if (sh4_hook_log)
                    log_hook(c, t);
                specials[k].fn(c, specials[k].user);
                if (sh4_cstack_on)
                    sh4_cstack_ret(ret);
                c->pc = ret;
                continue;
            }
        }
        if (sizeof(void *) == 4) {
            const void *h = (const void *)(uintptr_t)t;
            const sh4_thunk_info *ti = sh4_thunk_by_host(h);
            const sh4_native_fn *nf;
            if (ti) {                   /* native thunk pointer stored in guest memory */
                c->pc = ti->guest;
                continue;
            }
            nf = sh4_native_by_host(h);
            if (nf) {
                uint32_t ret = c->pr;
                if (!nf->guest || routed(nf)) {
                    if (sh4_hook_log)
                        log_hook(c, nf->guest);
                    nf->adapter(c);
                    c->pc = ret;
                } else {
                    c->pc = nf->guest;  /* pure mode: run the original */
                }
                continue;
            }
        }
        return;                         /* fetch will fault */
    }
}

/* ------------------------------------------------------------------ */
/* calls                                                               */

int sh4_invoke(uint32_t addr, const sh4_arg *args, int nargs, uint32_t *r0, float *fr0)
{
    sh4_cpu *c = &SH4;
    sh4_guard g;
    int own = 0, ni = 0, nf = 0, ns = 0, i;
    uint32_t stackv[64];
    uint32_t save_pc = c->pc, save_pr = c->pr, save_r15 = c->r[15];
    uint32_t sp;

    if (c->depth == 0 && !sh4_guard_top) {
        own = 1;
        sh4_guard_push(&g);
        if (setjmp(g.jb)) {
            sh4_guard_pop(&g);
            c->pc = save_pc;
            c->pr = save_pr;
            c->r[15] = save_r15;
            if (r0) *r0 = 0;
            if (fr0) *fr0 = 0.0f;
            return c->fault.kind;
        }
    }
    if (c->depth == 0) {
        sh4_fpu_sync_host(c);
        sp = c->stack_top;
    } else {
        sp = c->r[15];
    }
    for (i = 0; i < nargs; i++) {
        if (args[i].kind == 'f') {
            if (nf < 8) c->fr.u[4 + nf++] = args[i].v;
            else if (ns < 64) stackv[ns++] = args[i].v;
        } else {
            if (ni < 4) c->r[4 + ni++] = args[i].v;
            else if (ns < 64) stackv[ns++] = args[i].v;
        }
    }
    sp = (sp - 4u * (uint32_t)ns) & ~3u;
    for (i = 0; i < ns; i++)
        sh4_wr32(c, sp + 4u * (uint32_t)i, stackv[i]);
    c->r[15] = sp;
    c->pr = SH4_SENTINEL;
    c->depth++;
    {
        int cdepth = sh4_cstack_depth;
        if (sh4_cstack_on)
            sh4_cstack_push(addr, SH4_SENTINEL);
        sh4_branch(c, addr);
        sh4_run(c);
        sh4_cstack_depth = cdepth;
    }
    c->depth--;
    if (r0) *r0 = c->r[0];
    if (fr0) *fr0 = c->fr.f[0];
    c->pc = save_pc;
    c->pr = save_pr;
    c->r[15] = save_r15;
    if (own)
        sh4_guard_pop(&g);
    return SH4_OK;
}

int sh4_invoke_varargs(uint32_t addr, const uint32_t *words, int nwords, uint32_t *r0)
{
    sh4_arg a[64];
    int i;
    /* SHC passes every argument of a variadic call on the stack: emulate by
     * marking all as overflow (more than 4 ints) */
    if (nwords > 60)
        nwords = 60;
    for (i = 0; i < 4; i++) a[i] = sh4_ai(0);           /* dummies fill R4-R7 */
    for (i = 0; i < nwords; i++) a[4 + i] = sh4_ai(words[i]);
    return sh4_invoke(addr, a, 4 + nwords, r0, NULL);
}

int sh4_call_regs(uint32_t addr)
{
    return sh4_invoke(addr, NULL, 0, NULL, NULL);
}

uint32_t sh4_call(uint32_t addr, const uint32_t args[4], const float fargs[8])
{
    sh4_arg a[12];
    int n = 0, i;
    uint32_t r0 = 0;
    float f0;
    if (args)
        for (i = 0; i < 4; i++) a[n++] = sh4_ai(args[i]);
    if (fargs)
        for (i = 0; i < 8; i++) a[n++] = sh4_af(fargs[i]);
    sh4_invoke(addr, a, n, &r0, &f0);
    return r0;
}

float sh4_call_f(uint32_t addr, const uint32_t args[4], const float fargs[8])
{
    sh4_call(addr, args, fargs);
    return SH4.fr.f[0];
}
/* ------------------------------------------------------------------ */
/* signatures ("ret:arg,arg"; v i8 u8 i16 u16 i32 u32 p f32 f64 ...)    */

int sh4_sig_parse(const char *sig, char ret[8], char params[24][8])
{
    int n = 0, k = 0;
    const char *s = sig;
    if (!sig)
        return -1;
    while (*s && *s != ':' && k < 7)
        ret[k++] = *s++;
    ret[k] = 0;
    if (*s != ':')
        return -1;
    s++;
    while (*s && n < 24) {
        k = 0;
        while (*s && *s != ',' && k < 7)
            params[n][k++] = *s++;
        params[n][k] = 0;
        n++;
        if (*s == ',')
            s++;
    }
    return n;
}

uint32_t sh4_extend(const char *t, uint32_t v)
{
    if (!strcmp(t, "i8")) return (uint32_t)(int32_t)(int8_t)v;
    if (!strcmp(t, "u8")) return v & 0xFFu;
    if (!strcmp(t, "i16")) return (uint32_t)(int32_t)(int16_t)v;
    if (!strcmp(t, "u16")) return v & 0xFFFFu;
    return v;
}

#if defined(_WIN32) && defined(__i386__)

/* Native code called a guest address (DEP fault -> sh4_indirect_tramp).
 * argp points at the cdecl arguments.  Signature from the thunk/native
 * tables when the address is known, else four integer arguments. */
uint32_t sh4_indirect_dispatch(uint32_t target, const uint32_t *argp, float *fret, int *isf)
{
    const char *sig = NULL;
    const sh4_thunk_info *ti = sh4_thunk_by_guest(target);
    const sh4_native_fn *nf;
    char ret[8], params[24][8];
    sh4_arg a[24];
    int np, i, w = 0, n = 0;
    uint32_t r0 = 0;
    float f0 = 0.0f;
    if (ti)
        sig = ti->sig;
    else if ((nf = sh4_native_by_guest(target)) != NULL)
        sig = nf->sig;
    np = sig ? sh4_sig_parse(sig, ret, params) : -1;
    if (np < 0 || (np > 0 && !strcmp(params[np - 1], "..."))) {
        if (np < 0)
            strcpy(ret, "u32");
        for (i = 0; i < 4; i++)
            a[n++] = sh4_ai(argp[i]);
    } else {
        for (i = 0; i < np; i++) {
            if (!strcmp(params[i], "f32")) {
                a[n].kind = 'f';
                a[n++].v = argp[w++];
            } else if (!strcmp(params[i], "f64")) {
                double d;
                memcpy(&d, &argp[w], 8);
                w += 2;
                a[n++] = sh4_af((float)d);
            } else {
                a[n++] = sh4_ai(sh4_extend(params[i], argp[w++]));
            }
        }
    }
    sh4_invoke(target, a, n, &r0, &f0);
    if (!strcmp(ret, "f32") || !strcmp(ret, "f64")) {
        *isf = 1;
        *fret = f0;
        return 0;
    }
    return sh4_extend(ret, r0);
}

void sh4_indirect_tramp(void);
__asm__(
    ".text\n"
    ".globl _sh4_indirect_tramp\n"
    "_sh4_indirect_tramp:\n"
    "    pushl %ebp\n"
    "    movl %esp, %ebp\n"
    "    subl $24, %esp\n"
    "    movl $0, -8(%ebp)\n"
    "    leal -8(%ebp), %edx\n"
    "    movl %edx, 12(%esp)\n"
    "    leal -4(%ebp), %ecx\n"
    "    movl %ecx, 8(%esp)\n"
    "    leal 8(%ebp), %ecx\n"
    "    movl %ecx, 4(%esp)\n"
    "    movl %eax, 0(%esp)\n"
    "    call _sh4_indirect_dispatch\n"
    "    cmpl $0, -8(%ebp)\n"
    "    je 1f\n"
    "    flds -4(%ebp)\n"
    "1:\n"
    "    movl %ebp, %esp\n"
    "    popl %ebp\n"
    "    ret\n");

static volatile uint32_t nf_addr, nf_eip, nf_kind, nf_code;

static void native_fault_stub(void)
{
    sh4_fault(&SH4, SH4_FAULT_NATIVE, nf_addr, "native exception %08X (%s %08X) at EIP %08X",
              nf_code, nf_kind == 8 ? "execute" : nf_kind == 1 ? "write" : "read", nf_addr, nf_eip);
}

static LONG CALLBACK sh4_veh(EXCEPTION_POINTERS *ep)
{
    EXCEPTION_RECORD *er = ep->ExceptionRecord;
    CONTEXT *ctx = ep->ContextRecord;
    DWORD code = er->ExceptionCode;
    if (code == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
        uint32_t kind = (uint32_t)er->ExceptionInformation[0];
        uint32_t addr = (uint32_t)er->ExceptionInformation[1];
        if (kind == 8 && ctx->Eip == addr && sh4_in_ram(addr)) {
            const sh4_native_fn *nf = route_on ? sh4_native_by_guest(addr) : NULL;
            if (nf && routed(nf)) {
                ctx->Eip = (DWORD)(uintptr_t)nf->host;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
            ctx->Eax = addr;
            ctx->Eip = (DWORD)(uintptr_t)sh4_indirect_tramp;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (ctx->Eip == addr && n_specials) {      /* an unmapped address may report kind 0 or 8 */
            int k = special_of(addr);
            if (k >= 0 && specials[k].native) {     /* native call of a synthetic code address: its native function */
                ctx->Eip = (DWORD)(uintptr_t)specials[k].native;
                return EXCEPTION_CONTINUE_EXECUTION;
            }
        }
        if (sh4_guard_top) {
            nf_addr = addr;
            nf_kind = kind;
            nf_eip = (uint32_t)ctx->Eip;
            nf_code = code;
            ctx->Eip = (DWORD)(uintptr_t)native_fault_stub;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    if (sh4_guard_top && (code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_INT_OVERFLOW ||
                          code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_PRIV_INSTRUCTION)) {
        nf_addr = 0;
        nf_kind = 0;
        nf_eip = (uint32_t)ctx->Eip;
        nf_code = code;
        ctx->Eip = (DWORD)(uintptr_t)native_fault_stub;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void sh4_native_init(void)
{
    static int done;
    if (done)
        return;
    done = 1;
    SetProcessDEPPolicy(1 /* PROCESS_DEP_ENABLE */);
    AddVectoredExceptionHandler(1, sh4_veh);
}

#else

void sh4_native_init(void)
{
}

#endif