#include "sa1/sa1.h"

/*
 * E-102 Gamma (1ST_READ chunk 8C0CBE00):
 *   func_8C0CFACE  E-102 player display (tp->disp; caller E102Main):
 *                  picks the normal/laser blaster parts by equipment 0x200,
 *                  moves the two collision shapes to the body centre, draws
 *                  the body action (event action queue or the player action
 *                  with the node callback 8C0D215C), the scope (D_8C482E48)
 *                  rotated by the aim angle, the hover jet (equipment
 *                  0x400), the targeting arm, and computes the muzzle /
 *                  laser / arm points of the E-102 work; blinks the lamp
 *                  materials. Skipped while twp->wtimer & 2 (damage blink).
 */

/*
 * Line draw list of the E-102 work (same as code_8C0CFA6E.c; 0x20 bytes).
 */
typedef struct {
    NJS_POINT3 *src_p;          /* 0x00 */
    NJS_COLOR *src_col;         /* 0x04 */
    Sint16 *idx;                /* 0x08 */
    Sint16 num;                 /* 0x0C */
    Sint16 pad0E;               /* 0x0E */
    NJS_POINT3COL p3c;          /* 0x10 */
} E102_LINE;                    /* 0x20 */

/*
 * E-102 work (pwp->free.ptr[0]; E102Main 8C0CC0F2: CAlloc(1, 0x244)).
 * B_8C7A2EBC is a global copy of the same pointer (read here for +0x90 and
 * by the node callback 8C0D215C for +0x8C). Field names are descriptive
 * guesses from this function; types of the unk fields are placeholders.
 */
typedef struct {
    Sint8 mode;                 /* 0x00 3 = lock-on (scope follows the target / stick) */
    Sint8 smode;                /* 0x01 2..5: targeting arm out */
    Sint16 unk02;               /* 0x02 */
    Sint32 unk04;               /* 0x04 */
    Sint16 timer08;             /* 0x08 hover jet effect texture (timer08 >> 1 & 3) */
    Sint16 timer0A;             /* 0x0A (code_8C0D04A0.c) */
    Sint16 timer0C;             /* 0x0C (code_8C0D04A0.c) */
    Sint16 unk0E;               /* 0x0E */
    Sint16 unk10;               /* 0x10 */
    Sint16 lockon_timer;        /* 0x12 > 0: laser aimed at target_tp; decremented */
    Sint16 unk14;               /* 0x14 */
    Sint16 flag;                /* 0x16 (code_8C0D04A0.c) */
    Float hover_y;              /* 0x18 hover jet offset */
    Sint32 unk1C;               /* 0x1C */
    Angle3 jet_ang;             /* 0x20 hover jet rotation (x, z used) */
    Angle jet_phase;            /* 0x2C hover bob phase (njSin) */
    task *target_tp;            /* 0x30 locked-on target */
    NJS_POINT3 laser_pos;       /* 0x34 */
    NJS_POINT3 laser_vec;       /* 0x40 */
    Sint32 unk4C;               /* 0x4C */
    Sint32 unk50;               /* 0x50 */
    NJS_POINT3 arm_pos;         /* 0x54 */
    NJS_POINT3 arm_vec;         /* 0x60 */
    task *tp;                   /* 0x6C child task (code_8C0CFA6E.c) */
    Float arm_frame;            /* 0x70 */
    Sint32 unk74;               /* 0x74 */
    Sint32 unk78;               /* 0x78 */
    Sint32 unk7C;               /* 0x7C */
    NJS_ACTION *arm_action;     /* 0x80 targeting arm action */
    Float scope_frame;          /* 0x84 frame of D_8C482E48 */
    Angle blink_ang;            /* 0x88 lamp blink phase */
    Angle scope_ang;            /* 0x8C scope/arm yaw */
    Float body_y;               /* 0x90 (read through B_8C7A2EBC) */
    NJS_POINT3 muzzle[2];       /* 0x94 */
    Sint32 unkAC;               /* 0xAC */
    E102_LINE line0;            /* 0xB0 */
    E102_LINE line1;            /* 0xD0 */
    Sint32 unkF0[85];           /* 0xF0 */
} E102WK;                       /* 0x244 */

SA1_ASSERT_SIZE(E102_LINE, 0x20);
SA1_ASSERT_SIZE(E102WK, 0x244);
SA1_ASSERT_OFFSET(E102WK, lockon_timer, 0x12);
SA1_ASSERT_OFFSET(E102WK, hover_y, 0x18);
SA1_ASSERT_OFFSET(E102WK, jet_ang, 0x20);
SA1_ASSERT_OFFSET(E102WK, jet_phase, 0x2C);
SA1_ASSERT_OFFSET(E102WK, target_tp, 0x30);
SA1_ASSERT_OFFSET(E102WK, laser_pos, 0x34);
SA1_ASSERT_OFFSET(E102WK, laser_vec, 0x40);
SA1_ASSERT_OFFSET(E102WK, arm_pos, 0x54);
SA1_ASSERT_OFFSET(E102WK, arm_vec, 0x60);
SA1_ASSERT_OFFSET(E102WK, tp, 0x6C);
SA1_ASSERT_OFFSET(E102WK, arm_frame, 0x70);
SA1_ASSERT_OFFSET(E102WK, arm_action, 0x80);
SA1_ASSERT_OFFSET(E102WK, scope_frame, 0x84);
SA1_ASSERT_OFFSET(E102WK, blink_ang, 0x88);
SA1_ASSERT_OFFSET(E102WK, scope_ang, 0x8C);
SA1_ASSERT_OFFSET(E102WK, body_y, 0x90);
SA1_ASSERT_OFFSET(E102WK, muzzle, 0x94);
SA1_ASSERT_OFFSET(E102WK, line0, 0xB0);

/* E-102 model data (1ST_READ, extern only). Nodes whose fields are written
 * are arrays so SHC keeps sym + displacement (see sa1/player.h). */
extern NJS_OBJECT D_8C429324[];         /* blaster attach node (sibling swapped) */
extern NJS_OBJECT D_8C42D9C0[];         /* second blaster attach node */
extern NJS_OBJECT E_102_Gamma_Normal_Blaster;
extern NJS_OBJECT E_102_Gamma_Normal_Blaster_A;
extern NJS_OBJECT E_102_Gamma_Laser_Blaster;
extern NJS_OBJECT E_102_Gamma_Laser_Blaser_A;
extern NJS_OBJECT E_102_Gamma_Hover_Jet[];
extern NJS_OBJECT E_102_Gamma_Hover_Jet_Effect[];
extern NJS_TEXLIST texlist_E102;
extern NJS_TEXLIST texlist_E102EFFECT;
extern NJS_ACTION D_8C482E48;           /* scope action */
extern NJS_OBJECT *D_8C422C88[];        /* NULL-terminated node list hidden while the arm is out */
extern NJS_MATERIAL D_8C426934[];       /* lamp materials ([6] blinks) */
extern NJS_MATERIAL D_8C4265E8[];
extern NJS_MATERIAL D_8C42616C[];
extern NJS_MATERIAL D_8C42A994[];
extern NJS_MATERIAL D_8C42BD14[];
extern NJS_POINT3 D_8C15C240;           /* eye point in the head matrix */
extern Float D_8C422CB0;
extern NJS_POINT3 D_8C422CB4;           /* scratch point for njCalcPoint/njCalcVector */
#if defined(TARGET_PC)
#define D_8C422CBC (D_8C422CB4.z)
#else
extern Float D_8C422CBC;                /* = D_8C422CB4.z (separate symbol in the pools) */
#endif
/* node matrices saved by the body action (8C058434 / 8C0583A0) */
extern NJS_MATRIX B_8C7A2EC8;
extern NJS_MATRIX B_8C7A2F08;
extern NJS_MATRIX B_8C7A2F48;
extern NJS_MATRIX B_8C7A2F88;
extern NJS_MATRIX B_8C7A2FC8;
extern NJS_MATRIX B_8C7A3008;
extern E102WK *B_8C7A2EBC;              /* copy of pwp->free.ptr[0] */
extern Sint8 B_8C7A2EA9;                /* hover jet effect enable */

void func_8C05695C(Uint32 a, Uint32 b);                 /* code_8C056860.c */
void func_8C056D5E(NJS_OBJECT **objs, Uint32 flag);     /* code_8C056A04.c: evalflags |= flag */
void func_8C056D76(NJS_OBJECT **objs, Uint32 flag);     /* code_8C056A04.c: evalflags &= ~flag */
Float func_8C09222C(taskwk *twp, Float r);              /* code_8C017360.c */
void func_8C0D215C(NJS_OBJECT *obj);                    /* node callback (chunk 8C0D2080) */

/*
 * func_8C60D1F8 (Ninja, 8C60D1F8: MOV.L R4,@B_8C890074) sets the per-node
 * motion callback B_8C890074 (sa1/motion.h; NULL clears it), but
 * sa1/ninja_funcs.h declares it (Uint32 v). Until the header takes an
 * sa1_MOTION_FUNC, the PC build does the store itself instead of passing
 * a truncated address.
 */
#if defined(TARGET_PC)
#define E102_SetNodeCallback(fn) (B_8C890074 = (fn))
#else
#define E102_SetNodeCallback(fn) func_8C60D1F8((Uint32)(fn))
#endif

#define CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

/*
 * Matching notes: dx/dz must be separate statements (keeps the target_tp
 * CSE on the stack at sp+4 and ->twp in R4); the pops of the lock-on
 * if/else are written in both branches (tail-merged; this makes njPopMatrix
 * R11 and njPushMatrix R10); the 46/52 test is an if with && (reqaction
 * spilled to sp+0, func_8C04FE40 called through R3). The hover bob offset
 * and the reqaction 69 fade go through Float temporaries (bob: njTranslate's
 * MOV #0,R4 in the delay slot; fade: the FMOV FR1,FR4 before the FMUL), the
 * 69 test is a nested if (FR0/FR1 temporaries), and the other four
 * func_8C04FE40 calls go through a function pointer local loaded before the
 * wtimer test (keeps R8 = func_8C04FE40 against the B_8C7A2EC8 address).
 */
void func_8C0CFACE(task *tp)
{
    taskwk *twp;
    motionwk *mwp;
    playerwk *pwp;
    E102WK *ewk;
    CCL_INFO *info;
    Sint32 act;
    Float zero;
    Float r;
    Sint32 d;
    Sint32 a;
    NJS_POINT3 ofs2;
    NJS_POINT3 ofs;
    NJS_POINT3 pos;
    Angle target;
    Float dx;
    Float dz;
    Float bob;
    Float fade;
    Sint32 (*getmode)(void);

    twp = tp->twp;
    mwp = tp->mwp;
    pwp = mwp->work.pwp;
    ewk = (E102WK *)pwp->free.ptr[0];
    if (pwp->equipment & 0x200) {
        D_8C429324->sibling = &E_102_Gamma_Laser_Blaster;
        D_8C42D9C0->sibling = &E_102_Gamma_Laser_Blaser_A;
    } else {
        D_8C429324->sibling = &E_102_Gamma_Normal_Blaster;
        D_8C42D9C0->sibling = &E_102_Gamma_Normal_Blaster_A;
    }
    GammaWeldList[0].srcobj = D_8C429324->sibling->child;
    func_8C038CEC(2);
    act = pwp->mj.reqaction;
    if (pwp->mj.mtnmode == 2) {
        act = pwp->mj.action;
    }
    getmode = func_8C04FE40;    /* see matching notes */
    if (!(twp->wtimer & 2)) {
        njSetTexture(&texlist_E102);
        func_8C056926();
        func_8C05695C(0, 0x80000);
        njPushMatrix(NULL);
        zero = 0.0f;
        ofs.x = ofs.z = zero;
        ofs.y = pwp->p.center_height;
        njPushMatrix(D_8C67C640);
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        -twp->ang.y ? njRotateY(NULL, -twp->ang.y) : (void)0;
        njCalcVector(NULL, &ofs, &pos);
        njPopMatrix(1);
        ofs2 = pos;
        pos.x += twp->pos.x;
        pos.y += twp->pos.y;
        pos.z += twp->pos.z;
        njTranslateV(NULL, &pos);
        info = CWP(twp)->info;
        info->center = pos;
        info++;
        info->center.x = pos.x;
        info->center.y = twp->pos.y + 5.0f;
        info->center.z = pos.z;
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
        if ((pwp->mj.reqaction == 46 || pwp->mj.reqaction == 52) && !func_8C04FE40()) {
            r = CLAMP(_builtin_fabsf(pwp->spd.x + pwp->spd.z) - 0.79999997f, 0.0f, 2.0f);
            ewk->jet_phase += SA1_FTOI(r * 1536.0f) + 0x600;
            bob = njSin(ewk->jet_phase) * 1.8f;
            njTranslate(NULL, 0.0f, bob, 0.0f);
            SA1_FTOI(r * 2048.0f) ? njRotateZ(NULL, SA1_FTOI(r * 2048.0f)) : (void)0;
        } else {
            ewk->jet_phase = 0;
        }
        if (EWP(twp)->list40 != NULL) {
            func_8C04B57C(twp);
            func_8C058434(54, B_8C7A2EC8);
            func_8C058434(30, B_8C7A2F08);
        } else {
            E102_SetNodeCallback(func_8C0D215C);
            if (pwp->mj.mtnmode != 2) {
                func_8C040942(pwp->mj.plactptr[act].actptr, pwp->mj.nframe);
            } else {
                func_8C040942(pwp->mj.actwkptr, pwp->mj.nframe);
            }
            E102_SetNodeCallback(NULL);
        }
        njPushMatrix(NULL);
        njTranslate(NULL, zero, -(B_8C7A2EBC->body_y * 2.0f), zero);
        njMultiMatrix(NULL, B_8C7A2EC8);
        ewk->scope_ang ? njRotateY(NULL, ewk->scope_ang) : (void)0;
        func_8C040942(&D_8C482E48, ewk->scope_frame);
        func_8C0583A0(D_8C67C640, &D_8C482E48, ewk->scope_frame, 2, B_8C7A2F48);
        njPopMatrix(1);
        if (pwp->equipment & 0x400) {
            njPushMatrix(NULL);
            njMultiMatrix(NULL, B_8C7A2F08);
            njRotateZ(NULL, 0x8000);
            njTranslate(NULL, 5.0f, ewk->hover_y + 5.0f, zero);
            ewk->jet_ang.z ? njRotateZ(NULL, ewk->jet_ang.z) : (void)0;
            ewk->jet_ang.x ? njRotateX(NULL, ewk->jet_ang.x) : (void)0;
            func_8C036F12((NJS_MODEL *)E_102_Gamma_Hover_Jet);
            if (B_8C7A2EA9) {
                func_8C63FAE8();
                njSetTexture(&texlist_E102EFFECT);
                E_102_Gamma_Hover_Jet_Effect->model->mats->attr_texId = ((ewk->timer08 >> 1) & 3) + 8;
                func_8C036F12((NJS_MODEL *)E_102_Gamma_Hover_Jet_Effect);
                njSetTexture(&texlist_E102);
                func_8C63FB18();
                njColorBlendingMode(0, 8);
                njColorBlendingMode(1, 6);
            }
            njPopMatrix(1);
        }
        if ((ewk->smode >= 2 && ewk->smode <= 5) || (ewk->mode >= 2 && ewk->mode <= 3)) {
            func_8C056D5E(D_8C422C88, 8);
            njPushMatrix(NULL);
            njTranslate(NULL, zero, -(B_8C7A2EBC->body_y * 2.0f), zero);
            njMultiMatrix(NULL, B_8C7A2F88);
            func_8C040942(ewk->arm_action, ewk->arm_frame);
            njPopMatrix(1);
            func_8C0583A0(D_8C67C640, ewk->arm_action, ewk->arm_frame, 6, B_8C7A2FC8);
        } else {
            func_8C056D76(D_8C422C88, 8);
        }
        njPushMatrix(D_8C67C640);
        njTranslateV(NULL, &twp->pos);
        njTranslateV(NULL, &ofs2);
        twp->ang.z ? njRotateZ(NULL, twp->ang.z) : (void)0;
        twp->ang.x ? njRotateX(NULL, twp->ang.x) : (void)0;
        0x8000 - twp->ang.y ? njRotateY(NULL, 0x8000 - twp->ang.y) : (void)0;
        njPushMatrix(NULL);
        njMultiMatrix(NULL, B_8C7A3008);
        D_8C422CB4.x = zero;
        D_8C422CBC = 6.0f;
        njCalcPoint(NULL, &D_8C422CB4, &ewk->muzzle[0]);
        D_8C422CBC = -D_8C422CBC;
        njCalcPoint(NULL, &D_8C422CB4, &ewk->muzzle[1]);
        D_8C422CBC = zero;
        njPopMatrix(1);
        njPushMatrix(NULL);
        if (ewk->mode == 3) {
            if (ewk->lockon_timer > 0) {
                dx = ewk->target_tp->twp->pos.x - ewk->laser_pos.x;
                dz = ewk->target_tp->twp->pos.z - ewk->laser_pos.z;
                target = twp->ang.y - SA1_FTOI(atan2f(dz, dx) * 10430.381f);
                ewk->scope_ang = target;
            } else if (B_8C754DCC[(Uint8)twp->counter.b[0]].stroke != 0.0f) {
                target = twp->ang.y - B_8C754DCC[(Uint8)twp->counter.b[0]].ang;
            } else {
                target = 0;
            }
        } else {
            target = 0;
        }
        if (!getmode()) {
            d = SubAngle(ewk->scope_ang, target);
            ewk->scope_ang += CLAMP(d, -0x5B0, 0x5B0);
        }
        njMultiMatrix(NULL, B_8C7A2EC8);
        ewk->scope_ang ? njRotateY(NULL, ewk->scope_ang) : (void)0;
        njMultiMatrix(NULL, B_8C7A2F48);
        njCalcPoint(NULL, &D_8C422CB4, &ewk->laser_pos);
        if (ewk->lockon_timer > 0) {
            ewk->laser_vec = ewk->target_tp->twp->pos;
            njSubVector(&ewk->laser_vec, &ewk->laser_pos);
            njUnitVector(&ewk->laser_vec);
            if (!getmode()) {
                ewk->lockon_timer--;
            }
            njPopMatrix(1);
        } else {
            D_8C422CB4.x = -1.0f;
            njCalcVector(NULL, &D_8C422CB4, &ewk->laser_vec);
            njPopMatrix(1);
        }
        njPushMatrix(NULL);
        njMultiMatrix(NULL, B_8C7A2FC8);
        D_8C422CB4.x = zero;
        njCalcPoint(NULL, &D_8C422CB4, &ewk->arm_pos);
        D_8C422CB4.x = 1.0f;
        njCalcVector(NULL, &D_8C422CB4, &ewk->arm_vec);
        njUnitVector(&ewk->arm_vec);
        njPopMatrix(1);
        njPopMatrix(1);
        njPushMatrix(B_8C7A2EC8);
        ofs = D_8C15C240;
        njCalcPoint(NULL, &ofs, &pos);
        pwp->p.height = D_8C422CB0 + pos.y;
        njPopMatrix(1);
        njPopMatrix(1);
        if (!getmode()) {
            ewk->blink_ang += 0x400;
        }
        a = SA1_FTOI(_builtin_fabsf(njSin(ewk->blink_ang) * 255.0f));
        D_8C426934[6].diffuse.argb.a = a;
        if (pwp->mj.reqaction == 69) {
            /* lamps fade out over frames 40..80 */
            if (pwp->mj.nframe >= 40.0f) {
                fade = (80.0f - pwp->mj.nframe) * 6.375f;
                a = SA1_FTOI(fade);
                D_8C426934[6].diffuse.argb.a = a;
                D_8C4265E8[0].diffuse.argb.a = a;
                D_8C42616C[0].diffuse.argb.a = a;
                D_8C42A994[0].diffuse.argb.a = a;
                D_8C42BD14[0].diffuse.argb.a = a;
            }
        }
        func_8C056938();
    }
    func_8C038CEC(0);
    if (getmode()) {
        func_8C09222C(twp, 1.5f);
    }
}
