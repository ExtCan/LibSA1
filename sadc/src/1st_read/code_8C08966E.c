#include "common.h"
#include "ninja.h"

/* Path (spline) tables: like SADX pathtag / pathtbl / pathinfo. */
typedef struct {
    Sint16 slangx;
    Sint16 slangz;
    Float length;
    Float xpos;
    Float ypos;
    Float zpos;
} PATHTBL;

typedef struct {
    Uint8 slattr;
    Uint8 unk1;
    Uint16 points;
    Float totallen;
    PATHTBL *tblhead;
    void *pathctrl;
} PATHTAG;

typedef struct {
    Angle slangx;
    Angle slangz;
    Angle angx;
    Angle angz;
    Float onpathpos;
    Float xpos;
    Float ypos;
    Float zpos;
    Float xpos2;
    Float ypos2;
    Float zpos2;
    NJS_VECTOR normal;
    NJS_VECTOR normala;
    NJS_VECTOR front;
} PATHINFO;

extern Sint32 SubAngle(Sint32 a, Sint32 b);
extern Float func_8C056784(Float x, Float y);
extern Float njCos(Angle a);
extern Float njSin(Angle a);
extern Float njUnitVector(NJS_VECTOR *v);

void func_8C08966E(PATHTAG *tag, PATHINFO *info, Sint32 idx, PATHTBL *out) {
    PATHTBL tbl[3];
    Sint32 prev;
    Sint32 next;

    tbl[1] = tag->tblhead[idx];
    prev = idx - 1;
    next = idx + 1;
    switch (tag->slattr) {
    case 0:
        if (prev < 0) {
            prev = 0;
        } else if (next >= tag->points) {
            next = tag->points - 1;
        }
        break;
    case 1:
        if (prev < 0) {
            prev = tag->points - 1;
        } else if (next >= tag->points) {
            next = 0;
        }
        break;
    }
    tbl[0] = tag->tblhead[prev];
    tbl[2] = tag->tblhead[next];
    out[0].slangx = tbl[0].slangx + (SubAngle(tbl[0].slangx, tbl[1].slangx) >> 1);
    out[0].slangz = tbl[0].slangz + (SubAngle(tbl[0].slangz, tbl[1].slangz) >> 1);
    out[0].xpos = tbl[1].xpos;
    out[0].ypos = tbl[1].ypos;
    out[0].zpos = tbl[1].zpos;
    out[1].slangx = tbl[1].slangx + (SubAngle(tbl[1].slangx, tbl[2].slangx) >> 1);
    out[1].slangz = tbl[1].slangz + (SubAngle(tbl[1].slangz, tbl[2].slangz) >> 1);
    out[1].xpos = tbl[2].xpos;
    out[1].ypos = tbl[2].ypos;
    out[1].zpos = tbl[2].zpos;
}

Bool func_8C0897F8(PATHTAG *tag, PATHINFO *info) {
    NJS_VECTOR v;
    PATHTBL tbl[2];
    PATHTBL *p;
    Sint32 i;
    Float len;
    Float seg;
    Float t;

    switch (tag->slattr) {
    case 0:
        if (info->onpathpos < 0.0f || info->onpathpos > tag->totallen) {
            return FALSE;
        }
        break;
    case 1:
        info->onpathpos = func_8C056784(info->onpathpos, tag->totallen);
        break;
    }
    p = tag->tblhead;
    len = 0.0f;
    for (i = 0; i < tag->points; i++, p++) {
        if (p->length == 0.0f) {
            seg = 1.0f;
        } else {
            seg = p->length;
        }
        if (len + seg > info->onpathpos) {
            break;
        }
        len += seg;
    }
    func_8C08966E(tag, info, i, tbl);
    t = (info->onpathpos - len) / seg;
    info->slangx = p->slangx;
    info->slangz = p->slangz;
    info->angx = tbl[0].slangx + SA1_FTOI((Float)SubAngle(tbl[0].slangx, tbl[1].slangx) * t);
    info->angz = tbl[0].slangz + SA1_FTOI((Float)SubAngle(tbl[0].slangz, tbl[1].slangz) * t);
    v.x = tbl[1].xpos - tbl[0].xpos;
    v.y = tbl[1].ypos - tbl[0].ypos;
    v.z = tbl[1].zpos - tbl[0].zpos;
    info->xpos2 = SA1_FMAC(v.x, t, tbl[0].xpos);
    info->ypos2 = SA1_FMAC(v.y, t, tbl[0].ypos);
    info->zpos2 = SA1_FMAC(v.z, t, tbl[0].zpos);
    info->xpos = info->xpos2;
    info->ypos = info->ypos2;
    info->zpos = info->zpos2;
    info->normal.x = -(njSin(info->slangz) * njCos(info->slangx));
    info->normal.y = njCos(info->slangz) * njCos(info->slangx);
    info->normal.z = njSin(info->slangx);
    info->normala.x = -(njSin(info->angz) * njCos(info->angx));
    info->normala.y = njCos(info->angz) * njCos(info->angx);
    info->normala.z = njSin(info->angx);
    njUnitVector(&v);
    info->front = v;
    return TRUE;
}

Bool func_8C089A0E(Float pos, PATHTAG *tag, Uint32 *idx) {
    PATHTBL *p;
    Float len;
    Uint32 i;

    p = tag->tblhead;
    len = p->length;
    i = 0;
    while (i < tag->points && pos > len) {
        p++;
        i++;
        len += p->length;
    }
    *idx = i;
    return TRUE;
}

Bool func_8C089A40(PATHTAG *tag, Uint32 n, Float *out) {
    PATHTBL *p;
    register Uint32 i;
    Float len;

    if (n > tag->points) {
        return FALSE;
    }
    p = tag->tblhead;
    len = 0.0f;
    for (i = 0; i < n; i++) {
        len += p->length;
        p++;
    }
    *out = len;
    return TRUE;
}

Float func_8C089A6E(Float pos, PATHTAG *tag) {
    return func_8C056784(pos, tag->totallen);
}
