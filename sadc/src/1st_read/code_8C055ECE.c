#include "common.h"
#include "ninja.h"
#include "sa1/ninja_funcs.h"

/*
 * Model morphing helpers (8C055ECE-8C05646C).
 *
 *   func_8C055ECE  morph a basic model: dst->points = blend(a->points,
 *                  b->points, sin(t * 90 deg)), then rebuild the polygon
 *                  normals (func_8C056000) and vertex normals (func_8C05646C).
 *   func_8C055EFE  dst[i] = a[i] * (1 - t) + b[i] * t over n points (linear)
 *   func_8C055F2E  same with s = njSin(t * 0x4000) (ease out)
 *   func_8C055F8C  same with s = t * t (ease in)
 *   func_8C056000  polygon (face) normals of every meshset: triangles,
 *                  quads (vertices 0, 1, 3) and strips (winding flag 0x8000,
 *                  length 0x3FFF); an N-gon meshset (0x8000) ends the walk.
 *
 * The blend loops walk the point arrays as n * 3 consecutive floats. The
 * local copies of the arguments (pd, pa, pb, s, cnt) are what the matched
 * code needs: they set SHC's register/stack allocation (FMAC with the
 * (1 - s) factor in FR0).
 */

void func_8C055EFE(Float t, NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *dst, Sint32 n);
void func_8C055F2E(Float t, NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *dst, Sint32 n);
void func_8C055F8C(Float t, NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *dst, Sint32 n);
void func_8C056000(NJS_MODEL *model);
void func_8C05646C(NJS_MODEL *model);

#ifdef NON_MATCHING
/*
 * 3 instructions differ: SHC gives the four argument temporaries the stack
 * slots pa@12, n@8, pb@0, pd@4; the original has pb@4, pd@0. No declaration
 * order, assignment order, type or register-keyword variant tried changes it.
 */
void func_8C055ECE(NJS_MODEL *a, NJS_MODEL *b, NJS_MODEL *dst, Float t)
{
    Sint32 n;
    NJS_POINT3 *pa;
    NJS_POINT3 *pb;
    NJS_POINT3 *pd;

    n = a->nbPoint;
    pa = a->points;
    pb = b->points;
    pd = dst->points;
    func_8C055F2E(t, pa, pb, pd, n);
    func_8C056000(dst);
    func_8C05646C(dst);
}
#else
ASM_STUB(void func_8C055ECE(NJS_MODEL *a, NJS_MODEL *b, NJS_MODEL *dst, Float t))
#endif

void func_8C055EFE(Float t, NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *dst, Sint32 n)
{
    Float *pd;
    Float *pa;
    Float *pb;
    Float s;
    Sint32 cnt;
    Uint32 i;

    pd = &dst->x;
    pa = &a->x;
    pb = &b->x;
    s = t;
    cnt = n;
    for (i = cnt * 3; i > 0; i--) {
        *pd++ = SA1_FMAC(*pa++, 1.0f - s, *pb++ * s);
    }
}

void func_8C055F2E(Float t, NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *dst, Sint32 n)
{
    Float *pd;
    Float *pa;
    Float *pb;
    Float s;
    Sint32 cnt;
    register Uint32 i;

    pd = &dst->x;
    pa = &a->x;
    pb = &b->x;
    s = njSin(SA1_FTOI(t * 16384.0f));
    cnt = n;
    for (i = cnt * 3; i > 0; i--) {
        *pd++ = SA1_FMAC(*pa++, 1.0f - s, *pb++ * s);
    }
}

void func_8C055F8C(Float t, NJS_POINT3 *a, NJS_POINT3 *b, NJS_POINT3 *dst, Sint32 n)
{
    Float *pd;
    Float *pa;
    Float *pb;
    Float s;
    Sint32 cnt;
    Uint32 i;

    pd = &dst->x;
    pa = &a->x;
    pb = &b->x;
    s = t * t;
    cnt = n;
    for (i = cnt * 3; i > 0; i--) {
        *pd++ = SA1_FMAC(*pa++, 1.0f - s, *pb++ * s);
    }
}

#ifdef NON_MATCHING
/*
 * Same instruction sequence shape (538 vs 537 instructions) but SHC's
 * register/stack assignment differs: the triangle/quad loops spill the
 * second vertex index instead of the first, the R8-R11 copies of the
 * hoisted &v[0]/&v[1] are permuted, and several spill slots are swapped
 * (normals/len-2, nbMesh/k, index/normal induction variables).
 * Each case has its own NJS_VECTOR v[2]: the original hoists three separate
 * copies of the array address (R9/R11 triangles, R10/R8 quads, R13/R14
 * strips), which a single function-scope array does not reproduce.
 */
void func_8C056000(NJS_MODEL *model)
{
    NJS_POINT3 *points;
    NJS_MESHSET *mesh;
    NJS_VECTOR *normals;
    Sint16 *meshes;
    Sint32 i;
    Sint32 k;
    Sint32 j;
    Sint32 s;
    Uint16 nbMesh;
    Uint16 n0;
    Uint16 n1;
    Uint16 n2;
    Sint16 hdr;
    Sint16 len;
    Sint32 flip;

    points = model->points;
    mesh = model->meshsets;
    for (i = 0; model->nbMeshset > i; mesh++, i++) {
        normals = mesh->normals;
        nbMesh = mesh->nbMesh;
        meshes = mesh->meshes;
        switch (mesh->type_matId & 0xC000) {
        case 0x0000: {
            NJS_VECTOR v[2];

            for (k = 0; k < nbMesh; k++) {
                n0 = meshes[k * 3];
                n1 = meshes[k * 3 + 1];
                n2 = meshes[k * 3 + 2];
                v[0].x = points[n1].x - points[n0].x;
                v[0].y = points[n1].y - points[n0].y;
                v[0].z = points[n1].z - points[n0].z;
                v[1].x = points[n2].x - points[n0].x;
                v[1].y = points[n2].y - points[n0].y;
                v[1].z = points[n2].z - points[n0].z;
                njUnitVector(&v[0]);
                njUnitVector(&v[1]);
                normals[k].x = v[0].y * v[1].z - v[0].z * v[1].y;
                normals[k].y = v[0].z * v[1].x - v[0].x * v[1].z;
                normals[k].z = v[0].x * v[1].y - v[0].y * v[1].x;
            }
            break;
        }
        case 0x4000: {
            NJS_VECTOR v[2];

            for (k = 0; k < nbMesh; k++) {
                n0 = meshes[k * 4];
                n1 = meshes[k * 4 + 1];
                n2 = meshes[k * 4 + 3];
                v[0].x = points[n1].x - points[n0].x;
                v[0].y = points[n1].y - points[n0].y;
                v[0].z = points[n1].z - points[n0].z;
                v[1].x = points[n2].x - points[n0].x;
                v[1].y = points[n2].y - points[n0].y;
                v[1].z = points[n2].z - points[n0].z;
                njUnitVector(&v[0]);
                njUnitVector(&v[1]);
                normals[k].x = v[0].y * v[1].z - v[0].z * v[1].y;
                normals[k].y = v[0].z * v[1].x - v[0].x * v[1].z;
                normals[k].z = v[0].x * v[1].y - v[0].y * v[1].x;
            }
            break;
        }
        case 0xC000: {
            NJS_VECTOR v[2];

            for (s = 0; s < nbMesh; s++) {
                hdr = *meshes++;
                if (hdr & 0x8000) {
                    flip = -1;
                } else {
                    flip = 0;
                }
                n0 = *meshes++;
                n1 = *meshes++;
                len = hdr & 0x3FFF;
                n2 = *meshes++;
                /* every strip of the meshset restarts at normals[0] (original behaviour) */
                for (j = 0; j < len - 2; j++) {
                    v[0].x = points[n1].x - points[n0].x;
                    v[0].y = points[n1].y - points[n0].y;
                    v[0].z = points[n1].z - points[n0].z;
                    v[1].x = points[n2].x - points[n0].x;
                    v[1].y = points[n2].y - points[n0].y;
                    v[1].z = points[n2].z - points[n0].z;
                    njUnitVector(&v[0]);
                    njUnitVector(&v[1]);
                    if (flip >= 0) {
                        normals[j].x = v[0].y * v[1].z - v[0].z * v[1].y;
                        normals[j].y = v[0].z * v[1].x - v[0].x * v[1].z;
                        normals[j].z = v[0].x * v[1].y - v[0].y * v[1].x;
                    } else {
                        normals[j].x = v[1].y * v[0].z - v[1].z * v[0].y;
                        normals[j].y = v[1].z * v[0].x - v[1].x * v[0].z;
                        normals[j].z = v[1].x * v[0].y - v[1].y * v[0].x;
                    }
                    flip = ~flip;
                    n0 = n1;
                    n1 = n2;
                    n2 = *meshes++;
                }
                meshes--;
            }
            break;
        }
        default:
            return;
        }
    }
}
#else
ASM_STUB(void func_8C056000(NJS_MODEL *model))
#endif

ASM_STUB(void func_8C05646C(NJS_MODEL *model))
