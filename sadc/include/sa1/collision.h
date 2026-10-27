#ifndef SA1_COLLISION_H
#define SA1_COLLISION_H

/*
 * Land (geometry) collision as used by the player: the per-player collision
 * status csts (playerwk.cstsp) and the gathered land-collision object list.
 * Code: land collision 8C027360-8C02CE80 (36 functions), landtable list
 * manager 8C0519C0-8C052FA0, PSetPosition core 8C0730D8. This is the seam
 * where libsa1 feeds host geometry (docs/research/critique.md 5.8).
 * The polygon buffers of the land collision module (STRIP = sa1_MESHBUF,
 * RANGE = sa1_colpoly pool, OBJECT = sa1_colobj pool, NEAR, TOUCH =
 * sa1_TOUCHINFO/sa1_TOUCHPOLY) are below. Their canonical names are still
 * defined locally by unmigrated src files, so they are named sa1_<name>
 * here and sa1/pending.h maps the names (and declares the globals and
 * functions that use them).
 */

#include "task.h"

/*
 * Collision status (playerwk.cstsp, CAlloc(1, 0x84) by PInitialize 8C06C350).
 * Passed in R4 to the land collision core: 8C028BFE (NEAR-poly list) and
 * 8C029B00 (sphere vs polygon); filled by PSetPosition 8C072B44/8C0730D8 and
 * 8C0777E0/8C078856. Field names: SADX csts (same 0x84 size and order).
 */
typedef struct csts {
    Float radius;               /* 0x00 written 8C0730D8, 8C0777E0, 8C078856; read 8C028BFE, 8C029B00, 8C02B620; * 0.5f = sweep step (8C02833A) */
    NJS_POINT3 pos;             /* 0x04 sphere centre (forcewk.pos_spd added by 8C07506E) */
    NJS_POINT3 spd;             /* 0x10 move of this frame (njScalor by 8C02833A) */
    NJS_POINT3 tnorm;           /* 0x1C read 8C06F530, 8C079000, VectorAngle third argument (8C0742D4, 8C076640) */
    Uint16 find_count;          /* 0x28 touched polygons (TOUCHINFO entries; 8C029B00, 8C02833A; read 8C075416.., MOV.W + EXTU.W) */
    Uint16 selected_nmb;        /* 0x2A index of the selected TOUCHINFO (written 8C029B00, 8C028100; read 8C0730D8, 8C0777E0 * 88) */
    Float yt;                   /* 0x2C (not seen accessed) */
    Float yb;                   /* 0x30 written by 8C02B620 (FMOV.S FR15,@(48,R14)), read by 8C028480 */
    Angle angx;                 /* 0x34 slope angle: FTRC(asin(normal2.z) * 10430.381) (8C029B00, 8C028100, 8C02B620; also written by 8C0730D8) */
    Angle angz;                 /* 0x38 -FTRC(atan2(normal2.x, normal2.y) * 10430.381) */
    NJS_POINT3 normal;          /* 0x3C triangle normal of the hit (written 8C029B00, 8C028100, 8C02B620; read 8C0751C0) */
    NJS_POINT3 normal2;         /* 0x48 hit normal facing the sphere: what the callers use (8C0730D8, 8C0747D2, 8C074AFC, 8C0763A0, 8C076640, 8C056A04, Knuckles 8C0C193A); gravity test > -0.5f = wall */
    NJS_POINT3 onpoly;          /* 0x54 centre projected on the polygon plane (average of the hits, 8C028100) */
    NJS_POINT3 pshbk;           /* 0x60 resulting (pushed-out) position (8C029B00; copied by 8C0742D4, 8C0747D2) */
    NJS_POINT3 anaspdh;         /* 0x6C resulting speed along the plane (8C076640, 8C0747D2, 8C075A40) */
    NJS_POINT3 anaspdv;         /* 0x78 speed along the normal; read/written by 8C0711D2, 8C0714B6 */
} csts;                         /* 0x84 */

SA1_ASSERT_SIZE(csts, 0x84);
SA1_ASSERT_OFFSET(csts, pos, 0x04);
SA1_ASSERT_OFFSET(csts, spd, 0x10);
SA1_ASSERT_OFFSET(csts, tnorm, 0x1C);
SA1_ASSERT_OFFSET(csts, find_count, 0x28);
SA1_ASSERT_OFFSET(csts, selected_nmb, 0x2A);
SA1_ASSERT_OFFSET(csts, yt, 0x2C);
SA1_ASSERT_OFFSET(csts, yb, 0x30);
SA1_ASSERT_OFFSET(csts, angx, 0x34);
SA1_ASSERT_OFFSET(csts, angz, 0x38);
SA1_ASSERT_OFFSET(csts, normal, 0x3C);
SA1_ASSERT_OFFSET(csts, normal2, 0x48);
SA1_ASSERT_OFFSET(csts, onpoly, 0x54);
SA1_ASSERT_OFFSET(csts, pshbk, 0x60);
SA1_ASSERT_OFFSET(csts, anaspdh, 0x6C);
SA1_ASSERT_OFFSET(csts, anaspdv, 0x78);

/*
 * Land collision object list entry (SADX _OBJ_LANDCOLL). The gather
 * 8C0522C0 copies {attr, object, task} (stride 12) from the source list into
 * the per-query list; 8C02B620 is called with (buffer, entry.pObject,
 * entry.slAttribute); 8C069220 reads pTask. 0x10000000 in slAttribute =
 * UseRotation (the gather then builds a matrix from pObject->ang).
 */
typedef struct _OBJ_LANDCOLL {
    Uint32 slAttribute;         /* 0x00 SA1 ColFlags */
    NJS_OBJECT *pObject;        /* 0x04 */
    struct task *pTask;         /* 0x08 owner of a dynamic (moving) collision object, else NULL */
} _OBJ_LANDCOLL;                /* 0x0C */

SA1_ASSERT_SIZE(_OBJ_LANDCOLL, 0x0C);
SA1_ASSERT_OFFSET(_OBJ_LANDCOLL, pObject, 0x04);
SA1_ASSERT_OFFSET(_OBJ_LANDCOLL, pTask, 0x08);

/*
 * Touch record (B_8C6BACF0[16], stride 0x58 = MUL.L #88): one per polygon
 * the sphere touches. Written by 8C02770E (and 8C029B00 via TOUCHPOLY);
 * 8C028100 picks the entry with the smallest |dist2| and copies every field
 * except dist/dist2 into the csts field of the same name, averaging
 * onpoly/pshbk (asm 8C02A4B0-8C02A536). Read by 8C0751C0, 8C075416,
 * 8C075686, 8C075860, 8C075A40, 8C075F8C, 8C076782 (copies the first 0x50
 * bytes), 8C0730D8/8C0777E0 (onpoly via csts.selected_nmb), 8C0C7600.
 * No pointers. Canonical name TOUCHINFO (COLHIT, CL_HITPOLY in two files):
 * sa1/pending.h.
 */
typedef struct {
    Angle angx;                 /* 0x00 FTRC(asin(normal2.z) * 10430.381) */
    Angle angz;                 /* 0x04 -FTRC(atan2(normal2.x, normal2.y) * 10430.381) */
    NJS_VECTOR normal;          /* 0x08 triangle normal */
    NJS_VECTOR normal2;         /* 0x14 normal facing the sphere (flipped for double-sided materials) */
    NJS_POINT3 onpoly;          /* 0x20 centre projected on the plane */
    NJS_POINT3 pshbk;           /* 0x2C centre pushed out to the radius (code_8C02770E.c: onpoly2) */
    NJS_VECTOR anaspdh;         /* 0x38 speed along the plane */
    NJS_VECTOR anaspdv;         /* 0x44 speed along the normal */
    Float dist;                 /* 0x50 height of the centre above the plane before the move */
    Float dist2;                /* 0x54 height after the move (selection key) */
} sa1_TOUCHINFO;                /* 0x58 */

/*
 * Land polygon node (0x40). The RANGE-poly pool B_8C6BE27C ("RANGE-poly
 * buf", free list D_8C02D540, built by 8C028E60/8C02CE80), linked per land
 * object (8C02D00E) and into the z buckets D_8C02D560 (8C02CFC0, sorted by
 * center.z); the NEAR list B_8C73E280 points at them (8C028BFE).
 * code_8C028620.c, code_8C029B00.c, code_8C02CFC0.c: full; NEARPOLY,
 * FDPOLY, LCPOLY, LANDPOLY are prefix views. Canonical name colpoly
 * (COLPOLY in two files): sa1/pending.h.
 */
typedef struct sa1_colpoly {
    struct sa1_colpoly *next;   /* 0x00 free list / bucket list (increasing center.z) / per-object list */
    struct sa1_colpoly *prev;   /* 0x04 (8C02D00E, 8C02CFC0) */
    NJS_POINT3 center;          /* 0x08 bounding sphere centre (8C0289D6..E6; NEAR test in 8C028BFE) */
    Float r;                    /* 0x14 bounding radius squared; 0 = free slot (8C028620, 8C028E60 clears it) */
    NJS_POINT3 v[3];            /* 0x18 triangle (8C0288E0..910; 8C029B00 subtracts v[0] from cp->pos) */
    Uint32 attr;                /* 0x3C NJS_MATERIAL.attrflags of the meshset (8C028A3C); 0x00800000 = collide from both sides (8C029D90, 8C02A680) */
} sa1_colpoly;                  /* 0x40 */

/*
 * Land object node (0x40): the OBJECT pool B_8C6BB27C[192] ("OBJECT buf",
 * free list D_8C02D544, in-use list D_8C02D548). prev holds the previous
 * entry, or the negated address of the list head for the first entry
 * (hand-written 8C02CF20 tests it with CMP/PZ): keep it a void *.
 * code_8C028E60.c, code_8C02CEA0.c, code_8C02CF20.c, code_8C02CFC0.c: full;
 * NEARLIST, FDOBJ, LCOBJ, LANDPOLYLIST are prefix views. Canonical name
 * colobj: sa1/pending.h.
 */
typedef struct sa1_colobj {
    struct sa1_colobj *next;    /* 0x00 free list / in-use list */
    void *prev;                 /* 0x04 previous entry or -(list head address) */
    NJS_OBJECT *object;         /* 0x08 key, compared with _OBJ_LANDCOLL.pObject (8C028C60, 8C029B58, 8C029400); NULL = free */
    Uint32 attr;                /* 0x0C _OBJ_LANDCOLL.slAttribute (copied to xssunit.objatt) */
    sa1_colpoly *polys;         /* 0x10 first polygon, lowest center.z (8C02D00E; NEARLIST.head) */
    sa1_colpoly *last;          /* 0x14 last polygon (NEARLIST.tail) */
    Sint32 unk18;               /* 0x18 */
    sa1_colpoly **near;         /* 0x1C first entry in the NEAR-poly buffer B_8C73E280, NULL if none (8C028BFE, 8C029BB2) */
    Sint32 unk20;               /* 0x20 */
    Sint32 near_num;            /* 0x24 NEAR-poly entries of this object (8C029BAE) */
    Float zr;                   /* 0x28 z half extent (B_8C73EDF0 after 8C028B00; 8C02D060) */
    Float unk2C;                /* 0x2C B_8C73EDF4 after 8C028B00 */
    NJS_POINT3 center;          /* 0x30 model centre in view space (njCalcPoint) */
    Sint32 unk3C;               /* 0x3C */
} sa1_colobj;                   /* 0x40 */

/* z bucket of the polygon sort (D_8C02D560[128]; 8C02CFA4 clears, 8C02CFC0
 * pushes). Canonical name colbucket: sa1/pending.h. */
typedef struct {
    sa1_colpoly *head;          /* 0x00 */
    sa1_colpoly *tail;          /* 0x04 set when the bucket was empty */
} sa1_colbucket;                /* 0x08 */

/* TOUCH-poly entry (B_8C73E404[16], count B_8C73E400; 8C029B00 only;
 * B_8C73E448 = &B_8C73E404[0].info). Canonical name TOUCHPOLY: pending.h. */
typedef struct {
    Sint32 unk00;               /* 0x00 not accessed */
    sa1_colpoly poly;           /* 0x04 copy of the triangle */
    sa1_TOUCHINFO info;         /* 0x44 */
} sa1_TOUCHPOLY;                /* 0x9C */

/*
 * Vertical probe result (SADX xssunit / zxsdwstr / xyyzzxsdwstr).
 * zxsdwstr: ground below / ceiling above pos (8C02AD22, 8C02BC60, 8C02CA00
 * death zones, 8C02D060, 8C02D410; callers 8C07BAC0, 8C0BD8B2 keep it on
 * the stack). xyyzzxsdwstr: six axis probes +x,-x,+y,-y,+z,-z (8C029400,
 * 8C02C100; code_8C016C60.c UNK_8C016C60_info). No pointers. Canonical
 * names: sa1/pending.h.
 */
typedef struct {
    Sint32 findflag;            /* 0x00 1 = polygon found */
    Sint32 objatt;              /* 0x04 attribute of the land object */
    Angle angx;                 /* 0x08 */
    Angle angz;                 /* 0x0C */
    Float onpos;                /* 0x10 coordinate of the hit on the probe axis (ground height for zxsdwstr) */
    NJS_VECTOR normal;          /* 0x14 unit polygon normal */
} sa1_xssunit;                  /* 0x20 */

typedef struct {
    NJS_POINT3 pos;             /* 0x00 */
    sa1_xssunit lower;          /* 0x0C highest polygon at or below pos.y */
    sa1_xssunit upper;          /* 0x2C lowest polygon above pos.y */
} sa1_zxsdwstr;                 /* 0x4C */

typedef struct {
    NJS_POINT3 pos;             /* 0x00 */
    sa1_xssunit hit[6];         /* 0x0C +x, -x, +y, -y, +z, -z */
} sa1_xyyzzxsdwstr;             /* 0xCC */

/*
 * Strip/vertex gather buffer of the land collision ("STRIP buf": 8C0273A2,
 * 8C0274B8, 8C0275E0; on the stack of 8C02770E at sp+0xB4, 8C02B620,
 * 8C02BC60, 8C02C100, 8C02CA00, 8C028620). Native work memory.
 * Canonical name MESHBUF (STRIPBUF, CL_STRIPBUF in two files): pending.h.
 */
typedef struct {
    Sint32 type;                /* 0x00 NJS_MESHSET.type_matId & 0xC000 */
    Sint32 n;                   /* 0x04 vertex count: 3, 4, or the strip length & 0x7FFF capped at 0x400 */
    Sint16 *meshes;             /* 0x08 read cursor in NJS_MESHSET.meshes */
    NJS_MODEL *model;           /* 0x0C points source */
    NJS_POINT3 buf[0x400];      /* 0x10 vertices through the current matrix */
} sa1_MESHBUF;                  /* 0x3010 */

/* Hit gathering context of 8C02833A (stack sp+4), filled by 8C02770E and
 * resolved by 8C028100. Canonical name COLHITCTX (CL_HITLIST in
 * code_8C02770E.c): sa1/pending.h. */
typedef struct {
    Sint32 hit;                 /* 0x00 set to 1 by 8C02770E on a hit; the result */
    Sint32 nbHit;               /* 0x04 entries used in list (max 16) */
    Sint32 nbCheck;             /* 0x08 polygons that passed the bounding test (incremented by 8C02770E) */
    Float range2;               /* 0x0C D_8C112460[] indexed by ((attr & 0x60000000) >> 13) * 4 bytes (SHLD #-13 + SHLL2 at 8C02835C; reads past the 4-entry table, see code_8C028100.c) */
    Float spd;                  /* 0x10 njScalor(&cp->spd) */
    Float halfr;                /* 0x14 cp->radius * 0.5f (sweep step) */
    sa1_TOUCHINFO *list;        /* 0x18 B_8C6BACF0 */
} sa1_COLHITCTX;                /* 0x1C */

/* Segment cast against the land objects (8C02B1A0/8C02B2D6; R4). Canonical
 * name LANDRAY: sa1/pending.h. */
typedef struct {
    NJS_POINT3 pos;             /* 0x00 start */
    NJS_VECTOR vec;             /* 0x0C start to end */
    NJS_POINT3 hit;             /* 0x18 hit point (out; an overlay reads +0x1C) */
    NJS_VECTOR normal;          /* 0x24 polygon normal, not normalised (out) */
    Angle angx;                 /* 0x30 slope angles (8C066732 2nd/3rd arguments) */
    Angle angz;                 /* 0x34 */
    Uint32 attr;                /* 0x38 in: attribute for obj; out: slAttribute of the object hit */
    Float t;                    /* 0x3C hit parameter along vec, 0..1 (nearest hit) */
} sa1_LANDRAY;                  /* 0x40 */

/* Ray query block of 8C02A680. Canonical name LINECOLLI: pending.h. */
typedef struct {
    Float len;                  /* 0x00 ray length; forced to 400 outside [0.01, 400] */
    NJS_POINT3 pos;             /* 0x04 origin */
    NJS_VECTOR vec;             /* 0x10 direction */
    Float dist;                 /* 0x1C distance of the nearest hit (out) */
    NJS_POINT3 hitpos;          /* 0x20 hit point (out) */
    Sint32 hit;                 /* 0x2C 1 when hit (out; cleared on entry) */
    Uint32 attr;                /* 0x30 slAttribute of the hit object (out) */
    Angle angx;                 /* 0x34 (out) */
    Angle angz;                 /* 0x38 (out) */
    Sint32 unk3C;               /* 0x3C not written by 8C02A680 */
    NJS_VECTOR normal;          /* 0x40 unit polygon normal (out) */
} sa1_LINECOLLI;                /* 0x4C */

/* Death zone entry (DeathZoneTable 8C1BE404 [level][act] -> list; 8C07BAC0
 * walks it with ADD #8 until object == NULL). Game data. Canonical name
 * DEATHZONE: sa1/pending.h. */
typedef struct {
    Uint32 flags;               /* 0x00 character bits (1 << GetPlayerNumber()) */
    NJS_OBJECT *object;         /* 0x04 */
} sa1_DEATHZONE;                /* 0x08 */

SA1_ASSERT_SIZE(sa1_TOUCHINFO, 0x58);
SA1_ASSERT_OFFSET(sa1_TOUCHINFO, normal, 0x08);
SA1_ASSERT_OFFSET(sa1_TOUCHINFO, normal2, 0x14);
SA1_ASSERT_OFFSET(sa1_TOUCHINFO, onpoly, 0x20);
SA1_ASSERT_OFFSET(sa1_TOUCHINFO, pshbk, 0x2C);
SA1_ASSERT_OFFSET(sa1_TOUCHINFO, anaspdh, 0x38);
SA1_ASSERT_OFFSET(sa1_TOUCHINFO, anaspdv, 0x44);
SA1_ASSERT_OFFSET(sa1_TOUCHINFO, dist, 0x50);
SA1_ASSERT_OFFSET(sa1_TOUCHINFO, dist2, 0x54);
SA1_ASSERT_SIZE(sa1_colpoly, 0x40);
SA1_ASSERT_OFFSET(sa1_colpoly, center, 0x08);
SA1_ASSERT_OFFSET(sa1_colpoly, r, 0x14);
SA1_ASSERT_OFFSET(sa1_colpoly, v, 0x18);
SA1_ASSERT_OFFSET(sa1_colpoly, attr, 0x3C);
SA1_ASSERT_SIZE(sa1_colobj, 0x40);
SA1_ASSERT_OFFSET(sa1_colobj, object, 0x08);
SA1_ASSERT_OFFSET(sa1_colobj, attr, 0x0C);
SA1_ASSERT_OFFSET(sa1_colobj, polys, 0x10);
SA1_ASSERT_OFFSET(sa1_colobj, last, 0x14);
SA1_ASSERT_OFFSET(sa1_colobj, near, 0x1C);
SA1_ASSERT_OFFSET(sa1_colobj, near_num, 0x24);
SA1_ASSERT_OFFSET(sa1_colobj, zr, 0x28);
SA1_ASSERT_OFFSET(sa1_colobj, center, 0x30);
SA1_ASSERT_SIZE(sa1_colbucket, 0x08);
SA1_ASSERT_OFFSET(sa1_colbucket, tail, 0x04);
SA1_ASSERT_SIZE(sa1_TOUCHPOLY, 0x9C);
SA1_ASSERT_OFFSET(sa1_TOUCHPOLY, poly, 0x04);
SA1_ASSERT_OFFSET(sa1_TOUCHPOLY, info, 0x44);
SA1_ASSERT_SIZE(sa1_xssunit, 0x20);
SA1_ASSERT_OFFSET(sa1_xssunit, objatt, 0x04);
SA1_ASSERT_OFFSET(sa1_xssunit, onpos, 0x10);
SA1_ASSERT_OFFSET(sa1_xssunit, normal, 0x14);
SA1_ASSERT_SIZE(sa1_zxsdwstr, 0x4C);
SA1_ASSERT_OFFSET(sa1_zxsdwstr, lower, 0x0C);
SA1_ASSERT_OFFSET(sa1_zxsdwstr, upper, 0x2C);
SA1_ASSERT_SIZE(sa1_xyyzzxsdwstr, 0xCC);
SA1_ASSERT_OFFSET(sa1_xyyzzxsdwstr, hit, 0x0C);
SA1_ASSERT_SIZE(sa1_MESHBUF, 0x3010);
SA1_ASSERT_OFFSET(sa1_MESHBUF, meshes, 0x08);
SA1_ASSERT_OFFSET(sa1_MESHBUF, model, 0x0C);
SA1_ASSERT_OFFSET(sa1_MESHBUF, buf, 0x10);
SA1_ASSERT_SIZE(sa1_COLHITCTX, 0x1C);
SA1_ASSERT_OFFSET(sa1_COLHITCTX, range2, 0x0C);
SA1_ASSERT_OFFSET(sa1_COLHITCTX, halfr, 0x14);
SA1_ASSERT_OFFSET(sa1_COLHITCTX, list, 0x18);
SA1_ASSERT_SIZE(sa1_LANDRAY, 0x40);
SA1_ASSERT_OFFSET(sa1_LANDRAY, hit, 0x18);
SA1_ASSERT_OFFSET(sa1_LANDRAY, normal, 0x24);
SA1_ASSERT_OFFSET(sa1_LANDRAY, attr, 0x38);
SA1_ASSERT_OFFSET(sa1_LANDRAY, t, 0x3C);
SA1_ASSERT_SIZE(sa1_LINECOLLI, 0x4C);
SA1_ASSERT_OFFSET(sa1_LINECOLLI, dist, 0x1C);
SA1_ASSERT_OFFSET(sa1_LINECOLLI, hit, 0x2C);
SA1_ASSERT_OFFSET(sa1_LINECOLLI, angx, 0x34);
SA1_ASSERT_OFFSET(sa1_LINECOLLI, normal, 0x40);
SA1_ASSERT_SIZE(sa1_DEATHZONE, 0x08);
SA1_ASSERT_OFFSET(sa1_DEATHZONE, object, 0x04);

extern Sint16 B_8C754EF0;               /* number of entries in B_8C754EF4 (MOV.W; 37 readers) */
extern _OBJ_LANDCOLL B_8C754EF4[];      /* per-query list (gather output); up to 0x3000/12 = 1024 entries before B_8C757EF4 */
extern _OBJ_LANDCOLL B_8C757EF4[];      /* source list (landtable + dynamic), count D_8C19E964 */
extern Sint32 D_8C19E964;               /* number of entries in B_8C757EF4 */

/* land collision module globals */
extern Sint8 tcrf_CollisionDisplayVar;  /* 8C752BDC debug display (MOV.B, CMP/PZ) */
extern Sint32 B_8C73E27C;               /* NEAR-poly entries (max 96, 8C028BFE) */
extern Sint32 B_8C73E400;               /* TOUCH-poly entries (8C029B00) */
extern Float D_8C112460[];              /* squared ranges {220^2, 202^2, 101^2, 30.3^2} by (attr & 0x60000000) */
extern Float B_8C73EDEC;                /* z origin of the polygon buckets */
extern Float B_8C73EDF0;                /* max z extent (8C028620) */
extern Float B_8C73EDF4;
extern NJS_OBJECT *B_8C73EDC4;          /* compared with _OBJ_LANDCOLL.pObject (8C028E60) */
extern Sint32 D_8C1BE84C;               /* PSetPosition: hit object with slAttribute 0x4000 (8C0730D8) */
extern Sint32 D_8C196044;
extern Sint32 D_8C02D54C;               /* found flag of the hand-written FD queries (8C02D060/8C02D2E0 return it) */
extern Float D_8C02D964;                /* 10430.0f (0x4622F800, not 10430.381f): radians to Angle in the hand-written module */
extern Float D_8C1BE850;                /* 5.2f (Amy) collision offset (8C0742D4) */
extern Float D_8C1BE854;                /* 4.0f */
extern NJS_MODEL Debug_Sphere;          /* 8C199518 */
extern NJS_MATRIX B_8C73EDF8;           /* 8C029B00 */
extern NJS_POINT3 B_8C73EDC8[3];        /* debug triangle (8C029B00) */
extern NJS_MATRIX B_8C788C44[];         /* node matrices written by 8C056DC0/8C056E6C, read by 8C056EB2 */

/* landtable gather around a sphere (8C0522C0, 19 callers; FR4-FR7) */
void func_8C0522C0(Float x, Float y, Float z, Float r);
/* NEAR-poly list for the player sphere ("WARNING: NEAR-poly is FULL"):
 * returns the NEAR-poly count B_8C73E27C (96 when full; every exit goes
 * through 8C028E1A MOV.L @R13,R0) */
Sint32 func_8C028BFE(csts *cp);
/* main sphere vs polygon routine ("TOUCH-poly buf is FULL"): R5 =
 * _OBJ_LANDCOLL.pObject (compared with colobj.object at 8C029B58), R6 =
 * .slAttribute (not read); every caller passes the list entry
 * (8C056B1C, 8C07335E, 8C073662, 8C0746C2, 8C0779A2) */
Sint32 func_8C029B00(csts *cp, NJS_OBJECT *object, Uint32 attr);
/* sphere vs one land object for an external csts (8C028480 passes a stack
 * csts, entry.pObject, entry.slAttribute; attr unused) */
Sint32 func_8C02B620(csts *cp, NJS_OBJECT *obj, Uint32 attr);
Sint32 func_8C02833A(csts *cp, NJS_OBJECT *obj, Uint32 attr);           /* sweep test; writes find_count */
/* vertical ground query (death zones 8C07BAC0): really
 * Sint32 func_8C02CA00(zxsdwstr *zssp, NJS_OBJECT *obj) (code_8C02CA00.c);
 * kept void * until code_8C07BAC0.c uses the shared zxsdwstr */
Sint32 func_8C02CA00(void *a, void *b);
void func_8C066660(NJS_VECTOR *a, NJS_VECTOR *b, NJS_VECTOR *out);      /* cross product */
void func_8C066732(NJS_VECTOR *v, Angle *angx, Angle *angz);            /* slope angles of a normal */
Float func_8C028BC0(NJS_POINT3 *a, NJS_POINT3 *b);
Float func_8C02B074(NJS_POINT3 *a, NJS_POINT3 *b);
Float func_8C02B08E(NJS_POINT3 *a, NJS_POINT3 *b);
void func_8C02D528(NJS_POINT3 *p);
/* MSetPosition core (code_8C056A04.c, NON_MATCHING only for its struct copy helper): the object sphere (pos, r)
 * moved by spd against the land collision list, entries with a bit of attr skipped; pos / spd / ang (optional) get
 * the nearest hit's push-back; -> 1 floor | 2 wall or ceiling, 0 no hit. Wrappers 8C0569E0 / 8C0569F0 / 8C056A00;
 * the movable-object steps 8C05872A / 8C058A1A */
Sint32 func_8C056A04(NJS_POINT3 *pos, NJS_POINT3 *spd, Angle3 *ang, Uint32 attr, Float r);

#define ListGroundForCollision func_8C0522C0

#endif