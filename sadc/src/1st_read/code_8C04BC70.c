/*
 * Event work (taskwk.ewp) face/mouth sequence player and the event-work test
 * task. Chunk asm/1st_read/text/8C04B440.src, functions 8C04BC70-8C04BEB6.
 */
#include "common.h"
#include "ninja.h"
#include "task.h"
#include "sa1/ninja_types.h"
#include "sa1/ninja_funcs.h"
#include "sa1/engine.h"
#include "sa1/ccl.h"

/* face sequences: 2-byte steps {time, face}, ended by a step with time 0;
 * [0] and [1] are the two random idle sequences (game data) */
extern const char *D_8C19C32C[];
/* collision shape of the event-work test task (game data) */
extern CCL_INFO D_8C19C1C4[];

/* shadow at pos tilted by ang, size r (8C091C20 chunk: R4 = ang, rotations
 * Z then X; R5 = pos, njTranslateV; FR4 = r) */
extern void func_8C0922EE(Angle3 *ang, NJS_POINT3 *pos, Float r);

/* same-chunk neighbours still in assembly (reached with BSR) */
ASM_STUB(void func_8C04B440(taskwk *twp))
ASM_STUB(void func_8C04B57C(taskwk *twp))
ASM_STUB(void func_8C04B9E0(taskwk *twp))
ASM_STUB(Sint32 func_8C04BC20(taskwk *twp))

void func_8C04BD60(taskwk *twp);
void func_8C04BD9E(task *tp);

/*
 * Face block of eventwk (0x5C-0x6B). The game reaches it through a pointer
 * to ewp + 0x5C (8C04BC20, 8C04BC70, 8C04BCC8 and 8C04BD60 all do
 * "ADD #92" and then use small displacements), i.e. a nested struct in the
 * original eventwk; a flat eventwk compiles to "MOV #104,R0" indexing.
 * Until include/sa1/engine.h has the nested struct, SHC uses this view and
 * the PC build uses the flat eventwk fields of the same names.
 */
#if defined(TARGET_PC)
typedef eventwk facewk;
#define FACEWK(ewp) (ewp)
#else
typedef struct facewk {
    Uint8 face;                 /* 0x00 = eventwk 0x5C */
    Uint8 face_next;            /* 0x01 */
    Sint16 face_time;           /* 0x02 */
    Sint16 face_count;          /* 0x04 */
    Uint8 pad06[2];
    const char *face_script;    /* 0x08 = eventwk 0x64 */
    const char *face_seq;       /* 0x0C = eventwk 0x68 */
} facewk;
#define FACEWK(ewp) ((facewk *)&(ewp)->face)
#endif

#ifdef NON_MATCHING
/*
 * Start face sequence n (n < 0: stop; 0 or an empty sequence: one of the two
 * idle sequences at random, 75% / 25%). Functionally complete; only the
 * scratch registers differ: the original tests *seq in R3 and calls rand
 * through R1, this compiles to R2 / R3.
 */
void func_8C04BC70(taskwk *twp, Sint32 n)
{
    eventwk *ewp;
    facewk *fwp;
    const char *seq;

    if (n < 0) {
        func_8C04BD60(twp);
        return;
    }
    ewp = (eventwk *)twp->ewp;
    fwp = FACEWK(ewp);
    seq = D_8C19C32C[n];
    if (n == 0 || seq == NULL || *seq == 0) {
        if ((Float)rand() * 3.0517579e-05f < 0.75f) {
            seq = D_8C19C32C[0];
        } else {
            seq = D_8C19C32C[1];
        }
    }
    fwp->face_seq = seq;
}
#else
ASM_STUB(void func_8C04BC70(taskwk *twp, Sint32 n))
#endif

/* advance the face sequence by one frame; at the end of a sequence the next
 * one comes from the face script (8C04BC20) */
void func_8C04BCC8(taskwk *twp)
{
    facewk *fwp;
    const char *p;

    fwp = FACEWK((eventwk *)twp->ewp);
    if (fwp->face_time != 0) {
        if (++fwp->face_count >= fwp->face_time) {
            fwp->face = fwp->face_next;
            fwp->face_count = 0;
            if (fwp->face_seq == NULL || *fwp->face_seq == 0) {
                func_8C04BC70(twp, func_8C04BC20(twp));
                if (fwp->face_time == 0) {
                    return;
                }
            }
            p = (fwp->face_seq += 2) - 2;
            fwp->face_time = p[0];
            fwp->face_next = p[1];
        }
    }
}

/* stop the face script and sequence */
void func_8C04BD60(taskwk *twp)
{
    facewk *fwp;

    fwp = FACEWK((eventwk *)twp->ewp);
    fwp->face_script = NULL;
    fwp->face_seq = NULL;
    fwp->face_time = 0;
}

/* create the event-work test task (called by 8C049DAA) */
task *func_8C04BD70(void)
{
    task *tp;

    tp = CreateElementalTask(2, 5, func_8C04BD9E);
    func_8C04B440(tp->twp);
    CCL_Init(tp, D_8C19C1C4, 4, 1, 1);
    return tp;
}

/*
 * Exec of the event-work test task: face, look-at (8C04B9E0), ground
 * snapping (ewp->mode bit 0: height, bit 1: tilt), shadow, draw (8C04B57C)
 * and collision. The rotations are conditional expressions (not if
 * statements), as in func_8C058100: SHC then keeps the angle in a stack
 * temporary like the original.
 */
void func_8C04BD9E(task *tp)
{
    NJS_POINT3 pos;
    Angle3 shang;
    taskwk *twp;
    eventwk *ewp;
    Sint32 mode;
    Float x;
    Float y;
    Float z;

    twp = tp->twp;
    ewp = (eventwk *)twp->ewp;
    func_8C04BCC8(twp);
    if (((eventwk *)twp->ewp)->list4C != NULL) {
        func_8C04B9E0(twp);
    }
    if (((eventwk *)twp->ewp)->list40 != NULL) {
        mode = (Uint8)ewp->mode;
        x = twp->pos.x;
        y = twp->pos.y;
        z = twp->pos.z;
        pos.y = func_8C091C20(x, y, z, &shang);
        if (mode & 1) {
            twp->pos.y = pos.y;
        }
        if (mode & 2) {
            twp->ang.x = shang.x;
            twp->ang.z = shang.z;
        }
        if (ewp->f78 > 0.0f) {
            pos.x = x;
            pos.y += 0.29999999f;
            pos.z = z;
            func_8C0922EE(&shang, &pos, ewp->f78);
        }
        njPushMatrix(NULL);
        njTranslateV(NULL, &twp->pos);
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
        /* eventwk 0x6C: NJS_POINT3 offset (pad6C in the header) */
        njTranslateV(NULL, (NJS_VECTOR *)ewp->pad6C);
        func_8C04B57C(twp);
        njPopMatrix(1);
        if (CWP(twp)->info->a > 0.0f) {
            CCL_Entry(twp);
        }
    }
}
