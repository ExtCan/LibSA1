/*
 * 1ST_READ 8C0E66C4-8C0E6812 (chunk 8C0E6620; Big the Cat, fishing):
 *
 *  func_8C0E66C4  disp of the cast cursor task (func_8C0E6CC0 installs it
 *                 and also calls it directly): draws the target circle
 *                 (Big_Cast_Circle) 3 units above the ground height under
 *                 the cursor, tilted by the ground angles (ang z/x of the
 *                 any work) and turned by -ang.y, then the cursor model
 *                 (Big_Cast_Cursor) at twp->pos turned by ang.y. A 4-point
 *                 square (+-5 in x/z) with colours is filled into a local
 *                 NJS_POINT3COL but never drawn (leftover debug code).
 */
#include "common.h"
#include "sa1/sa1.h"

/*
 * Any work of the cursor task (task.awp, 0x10 bytes from
 * CreateElementalTask im & 8; same layout as BIGCASTWK in
 * code_8C0E6812.c). No pointers: same layout on every host.
 */
typedef struct {
    Float dist;                 /* 0x00 horizontal distance to player 0 */
    Float height;               /* 0x04 ground height under the cursor */
    Sint16 angy;                /* 0x08 cast angle */
    Sint16 unk0A;               /* 0x0A */
    Sint16 angx;                /* 0x0C ground tilt */
    Sint16 angz;                /* 0x0E */
} BIGCASTWK;                    /* 0x10 */

SA1_ASSERT_SIZE(BIGCASTWK, 0x10);
SA1_ASSERT_OFFSET(BIGCASTWK, angx, 0x0C);
SA1_ASSERT_OFFSET(BIGCASTWK, angz, 0x0E);

extern NJS_TEXLIST texlist_FISHING;     /* 8C54CB4C */
extern NJS_MODEL Big_Cast_Circle;       /* 8C555B08 basic model */
extern NJS_MODEL Big_Cast_Cursor;       /* 8C555C3C basic model */

/* the rotation is skipped for a zero angle; the angle goes through an
 * unsigned local, which SHC keeps in a stack slot (as in code_8C0876F4.c);
 * the conditional-expression form (not if) gives the original R2/R3 choice
 * (as in code_8C0DFCBC.c) */
#define ROTATEX(m, a) ((ang = (a)) ? njRotateX((m), ang) : (void)0)
#define ROTATEY(m, a) ((ang = (a)) ? njRotateY((m), ang) : (void)0)
#define ROTATEZ(m, a) ((ang = (a)) ? njRotateZ((m), ang) : (void)0)

void func_8C0E66C4(task *tp)
{
    NJS_POINT3COL p3c;
    NJS_POINT3 p[4];
    NJS_COLOR col[4];
    NJS_POINT3 unused;          /* never used; keeps the 0x60-byte frame (R15+4..+0x10) */
    Uint32 ang;
    NJS_COLOR *cp;
    taskwk *twp;
    BIGCASTWK *cwp;
    Float h;
    Sint32 i;

    twp = tp->twp;
    cwp = (BIGCASTWK *)tp->awp;
    p3c.p = p;
    p3c.col = col;
    p3c.num = 4;
    p3c.p[0].x = -5.0f;
    p3c.p[0].z = -5.0f;
    p3c.p[1].x = 5.0f;
    p3c.p[1].z = -5.0f;
    p3c.p[2].x = 5.0f;
    p3c.p[2].z = 5.0f;
    p3c.p[3].x = -5.0f;
    p3c.p[3].z = 5.0f;
    h = cwp->height;
    p3c.p[0].y = p3c.p[1].y = p3c.p[2].y = p3c.p[3].y = 0.0f;
    for (i = 0, cp = p3c.col; i < 4; i++, cp++) {
        cp->color = 0x80FFFFFF;
    }
    p3c.tex = NULL;
    njSetTexture(&texlist_FISHING);
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x, h + 3.0f, twp->pos.z);
    ROTATEZ(NULL, cwp->angz);
    ROTATEX(NULL, cwp->angx);
    ROTATEY(NULL, -twp->ang.y);
    func_8C036F12(&Big_Cast_Circle);
    njPopMatrix(1);
    for (i = 0, cp = p3c.col; i < 4; i++, cp++) {
        cp->color = 0x80FF0000;
    }
    njPushMatrix(NULL);
    njTranslate(NULL, twp->pos.x, twp->pos.y, twp->pos.z);
    ROTATEY(NULL, twp->ang.y);
    func_8C036F12(&Big_Cast_Cursor);
    njPopMatrix(1);
}
