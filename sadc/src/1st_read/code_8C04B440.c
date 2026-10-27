#include "sa1/sa1.h"

/*
 * 8C04B440-8C04B7BA (start of the 8C04B440 chunk): event work (taskwk.ewp,
 * eventwk in sa1/engine.h) and its action queue, the SA1 counterpart of
 * SADX's EV_SetAction / EV_SetMotion / EV_ClrAction machinery.
 *  func_8C04B440  allocate twp->ewp (CAlloc(1, 0x7C), mode = 3); PInitialize
 *  func_8C04B468  free twp->ewp and everything it owns
 *  func_8C04B48C  clear the action queue (ewp->list40, ewp->buf44, frame)
 *  func_8C04B4D2  append an NJS_ACTION to the queue (task wrapper 8C04A120)
 *  func_8C04B528  append an object + NJS_MOTION to the queue (wrapper 8C04A142)
 *  func_8C04B57C  play the queue: draw the current action (blended with the
 *                 previous one for linkframe frames), set the player's weld
 *                 pose through 8C04BF00 and advance the frame / queue
 *
 * Queue entry (CAlloc(1, 0x18)); ewp->list40 is the queue head (current
 * action), ewp->buf44 the previous action while it is blended out.
 */
typedef struct ev_action_list {
    Uint8 mode;                 /* DC 0x00 bit 0: loop (wrap) when it is the last entry */
    Uint8 linkframe;            /* DC 0x01 blend frames from the previous action */
    NJS_ACTION action;          /* DC 0x04 */
    NJS_TEXLIST *texlist;       /* DC 0x0C NULL: keep the current texlist */
    void *next;                 /* DC 0x10 struct ev_action_list * (eventwk.list40 is void *) */
    Float speed;                /* DC 0x14 frames per step */
} EV_ACTION_LIST;               /* DC 0x18 */

SA1_ASSERT_SIZE(EV_ACTION_LIST, 0x18);
SA1_ASSERT_OFFSET(EV_ACTION_LIST, action, 0x04);
SA1_ASSERT_OFFSET(EV_ACTION_LIST, texlist, 0x0C);
SA1_ASSERT_OFFSET(EV_ACTION_LIST, next, 0x10);
SA1_ASSERT_OFFSET(EV_ACTION_LIST, speed, 0x14);

extern void func_8C038CEC(Sint32 bank);          /* palette bank select (GBR+88) */
extern void func_8C04BF00(Sint32 pno, NJS_OBJECT *object, NJS_MOTION *motion, Float frame);
extern void func_8C041C92(NJS_ACTION_LINK *action, Float rate);  /* code_8C04196C.c */

void func_8C04B48C(taskwk *twp);
void func_8C04B82E(taskwk *twp);

void func_8C04B440(taskwk *twp)
{
    eventwk *ewp;

    if (twp->ewp == NULL) {
        ewp = CAlloc(1, sizeof(eventwk));
        twp->ewp = ewp;
        ewp->mode = 3;
    }
}

void func_8C04B468(taskwk *twp)
{
    if (twp->ewp != NULL) {
        func_8C04B48C(twp);
        func_8C04B82E(twp);
        MFree(twp->ewp);
        twp->ewp = NULL;
    }
}

void func_8C04B48C(taskwk *twp)
{
    eventwk *ewp;
    EV_ACTION_LIST *p;
    EV_ACTION_LIST *next;

    ewp = twp->ewp;
    p = ewp->list40;
    while (p != NULL) {
        next = p->next;
        MFree(p);
        p = next;
    }
    ewp->list40 = NULL;
    if (ewp->buf44 != NULL) {
        MFree(ewp->buf44);
        ewp->buf44 = NULL;
    }
    ewp->f3C = 0.0f;
}

void func_8C04B4D2(taskwk *twp, NJS_ACTION *action, NJS_TEXLIST *texlist, Float speed,
                   Sint32 mode, Sint32 linkframe)
{
    eventwk *ewp;
    void **pp;
    EV_ACTION_LIST *node;

    ewp = twp->ewp;
    pp = &ewp->list40;
    while (*pp != NULL) {
        pp = &((EV_ACTION_LIST *)*pp)->next;
    }
    *pp = node = CAlloc(1, sizeof(EV_ACTION_LIST));
    node->mode = mode;
    node->linkframe = linkframe;
    node->action = *action;
    node->texlist = texlist;
    node->speed = speed;
}

void func_8C04B528(taskwk *twp, NJS_OBJECT *object, NJS_MOTION *motion, NJS_TEXLIST *texlist,
                   Float speed, Sint32 mode, Sint32 linkframe)
{
    eventwk *ewp;
    void **pp;
    EV_ACTION_LIST *node;

    ewp = twp->ewp;
    pp = &ewp->list40;
    while (*pp != NULL) {
        pp = &((EV_ACTION_LIST *)*pp)->next;
    }
    node = CAlloc(1, sizeof(EV_ACTION_LIST));
    *pp = node;
    node->mode = mode;
    node->linkframe = linkframe;
    node->action.object = object;
    node->action.motion = motion;
    node->texlist = texlist;
    node->speed = speed;
}

void func_8C04B57C(taskwk *twp)
{
    eventwk *ewp;
    EV_ACTION_LIST *node;
    EV_ACTION_LIST *prev;
    Float nbframe;
    Float frame;
    Sint32 i;
    NJS_MOTION_LINK link;
    NJS_ACTION_LINK actlink;

    ewp = twp->ewp;
    if (ewp == NULL) {
        return;
    }
    node = ewp->list40;
    prev = ewp->buf44;
    if (node == NULL) {
        return;
    }
    nbframe = (Float)node->action.motion->nbFrame;
    frame = ewp->f3C;
    if (node->texlist != NULL) {
        njSetTexture(node->texlist);
    }
    func_8C038CEC(2);
    if (prev != NULL && node->linkframe) {
        actlink.motionlink = &link;
        actlink.object = node->action.object;
        link.motion[0] = prev->action.motion;
        link.motion[1] = node->action.motion;
        if (prev->mode & 1) {
            link.frame[0] = 0.0f;
        } else {
            link.frame[0] = (Float)(prev->action.motion->nbFrame - 1);
        }
        link.frame[1] = 0.0f;
        for (i = 0; i < 8; i++) {
            if (twp == playertwp[i]) {
                if (func_8C10D198(frame) * 2.0f < (Float)node->linkframe) {
                    func_8C04BF00(i, node->action.object, link.motion[0], link.frame[0]);
                } else {
                    func_8C04BF00(i, node->action.object, link.motion[1], link.frame[1]);
                }
                break;
            }
        }
        func_8C041C92(&actlink, (frame + 1.0f) / (Float)(node->linkframe + 1));
        frame += 1.0f;
        if (!(func_8C10D198(frame) < (Float)node->linkframe)) {
            MFree(prev);
            ewp->buf44 = NULL;
            frame = 0.0f;
        }
    } else {
        for (i = 0; i < 8; i++) {
            if (twp == playertwp[i]) {
                func_8C04BF00(i, node->action.object, node->action.motion, ewp->f3C);
                break;
            }
        }
        func_8C040942(&node->action, ewp->f3C);
        frame += node->speed;
        if (node->mode & 1) {
            if (frame >= nbframe) {
                if (node->next == NULL) {
                    frame -= nbframe;
                    goto end;
                }
            } else {
                goto end;
            }
        } else {
            if (frame < nbframe - 1.0f) {
                goto end;
            }
        }
        frame = 0.0f;
        ewp->buf44 = node;
        ewp->list40 = node->next;
    }
end:
    func_8C038CEC(0);
    ewp->f3C = frame;
}

ASM_STUB(void func_8C04B82E(taskwk *twp))
