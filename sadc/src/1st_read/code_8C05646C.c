#include "common.h"
#include "ninja.h"
#include "sa1/engine.h"

/*
 * Model / object-tree helpers (1ST_READ 8C05646C-8C056824).
 *
 * func_8C05646C  recompute a basic model's vertex normals as the average of
 *                the per-polygon normals (meshset->normals) of every polygon
 *                that uses the vertex (triangles, quads, strips).
 *                Callers: 8C055ECE, 8C0BDC80 (Tails tail morph models).
 * func_8C056712  index of an object in a hierarchy (depth-first, child before
 *                sibling), -1 if it is not part of it (PJoinVertexes).
 * func_8C056738  recursive search for func_8C056712.
 * func_8C056784  floor-based fmod: a - f(a / b) * b with f = func_8C10D198
 *                (floorf-like libm wrapper); 0 when b == 0. Used to wrap a
 *                path position into [0, totallen).
 * func_8C0567B0  flatten an object hierarchy into a pointer array (depth-first),
 *                returns the number of objects.
 * func_8C0567D4  recursive part of func_8C0567B0.
 */

NJS_OBJECT *func_8C056738(NJS_OBJECT *obj, NJS_OBJECT *target, Sint32 *n);
Sint32 func_8C0567D4(NJS_OBJECT **list, Sint32 *n);

/*
 * NON_MATCHING: 12 instructions differ, register allocation only; code shape,
 * stack layout and all other registers match.
 *  - Prologue (5): nbPoint/vn are loaded into R3/R2 instead of R2/R3. Not a
 *    source difference: SHC chooses scratch registers from usage state carried
 *    over from the previous function of the translation unit (func_8C056000
 *    in the original). Compiled after a function whose last scratch use is R3
 *    the prologue matches; a file that starts here cannot reproduce it.
 *  - Strip loop (7): the inner counter n and the first vertex a get R9/R8
 *    instead of R8/R9. Tried: declaration orders, block-scoped and renamed
 *    strip variables, explicit index and len-2 variables, pointer walks for
 *    the normals, comma/while loop forms, statement orders, Sint16 vertices.
 * The CSE of &vn[i] for the zeroing followed by p = &vn[i] (and the final
 * division through vn[i] again) is what the original does.
 */
#ifdef NON_MATCHING
void func_8C05646C(NJS_MODEL *model)
{
    Uint32 nbPoint;
    NJS_VECTOR *vn;
    Uint32 i;
    NJS_VECTOR *p;
    Float count;
    NJS_MESHSET *ms;
    Uint32 j;
    NJS_VECTOR *fn;
    Uint16 nbMesh;
    Sint16 *meshes;
    Uint32 k;
    Uint16 a;
    Uint16 b;
    Uint16 c;
    Uint16 d;
    Sint16 len;
    Sint32 n;

    nbPoint = model->nbPoint;
    vn = model->normals;
    for (i = 0; i < nbPoint; i++) {
        vn[i].x = vn[i].y = vn[i].z = 0.0f;
        p = &vn[i];
        count = 0.0f;
        ms = model->meshsets;
        for (j = 0; model->nbMeshset > j; ms++, j++) {
            fn = ms->normals;
            nbMesh = ms->nbMesh;
            meshes = ms->meshes;
            switch (ms->type_matId & 0xC000) {
            case 0x0000:
                for (k = 0; k < nbMesh; k++) {
                    a = meshes[k * 3];
                    b = meshes[k * 3 + 1];
                    c = meshes[k * 3 + 2];
                    if (i == a || i == b || i == c) {
                        count += 1.0f;
                        p->x += fn[k].x;
                        p->y += fn[k].y;
                        p->z += fn[k].z;
                    }
                }
                break;
            case 0x4000:
                for (k = 0; k < nbMesh; k++) {
                    a = meshes[k * 4];
                    b = meshes[k * 4 + 1];
                    c = meshes[k * 4 + 2];
                    d = meshes[k * 4 + 3];
                    if (i == a || i == b || i == c || i == d) {
                        count += 1.0f;
                        p->x += fn[k].x;
                        p->y += fn[k].y;
                        p->z += fn[k].z;
                    }
                }
                break;
            case 0xC000:
                /* strips: the per-polygon normal index restarts at 0 in
                 * every strip (original behaviour) */
                for (k = 0; k < nbMesh; k++) {
                    len = *meshes++;
                    len &= 0x3FFF;
                    a = *meshes++;
                    b = *meshes++;
                    c = *meshes++;
                    for (n = 0; n < len - 2; n++) {
                        if (i == a || i == b || i == c) {
                            count += 1.0f;
                            p->x += fn[n].x;
                            p->y += fn[n].y;
                            p->z += fn[n].z;
                        }
                        a = b;
                        b = c;
                        c = *meshes++;
                    }
                    meshes--;
                }
                break;
            default:
                return;
            }
        }
        vn[i].x /= count;
        vn[i].y /= count;
        vn[i].z /= count;
    }
}
#else
ASM_STUB(void func_8C05646C(NJS_MODEL *model))
#endif

/*
 * Matches only when compiled right after the real func_8C05646C (build with
 * -define=NON_MATCHING): SHC picks the scratch register for n = 0 (R2 vs R3)
 * from register-usage state carried over from the previous function in the
 * translation unit, and the original TU had func_8C05646C's code before it.
 * The original keeps a copy of root in the stack frame next to n; taking the
 * copy's address (*&top) reproduces it.
 */
#ifdef NON_MATCHING
Sint32 func_8C056712(NJS_OBJECT *root, NJS_OBJECT *target)
{
    NJS_OBJECT *top;
    Sint32 n;
    NJS_OBJECT *found;

    n = 0;
    top = root;
    found = func_8C056738(*&top, target, &n);
    if (found == NULL) {
        return -1;
    }
    return n;
}
#else
ASM_STUB(Sint32 func_8C056712(NJS_OBJECT *root, NJS_OBJECT *target))
#endif

NJS_OBJECT *func_8C056738(NJS_OBJECT *obj, NJS_OBJECT *target, Sint32 *n)
{
    NJS_OBJECT *o;

    if (obj == target) {
        return target;
    }
    (*n)++;
    if ((o = obj->child) != NULL) {
        o = func_8C056738(o, target, n);
        if (o == target) {
            return target;
        }
    }
    if ((o = obj->sibling) != NULL) {
        o = func_8C056738(o, target, n);
        if (o == target) {
            return target;
        }
    }
    return NULL;
}

Float func_8C056784(Float a, Float b)
{
    if (b == 0.0f) {
        return 0.0f;
    }
    return a - func_8C10D198(a / b) * b;
}

Sint32 func_8C0567B0(NJS_OBJECT *obj, NJS_OBJECT **list)
{
    Sint32 n;

    n = 0;
    if (list != NULL) {
        list[n] = obj;
    }
    return func_8C0567D4(list, &n);
}

/* list[*n] is read even when list is NULL (the NULL checks only guard the
 * stores); every caller passes a buffer. */
Sint32 func_8C0567D4(NJS_OBJECT **list, Sint32 *n)
{
    NJS_OBJECT *obj;
    NJS_OBJECT *next;

    obj = list[*n];
    (*n)++;
    next = obj->child;
    if (next != NULL) {
        if (list != NULL) {
            list[*n] = next;
        }
        func_8C0567D4(list, n);
    }
    next = obj->sibling;
    if (next != NULL) {
        if (list != NULL) {
            list[*n] = next;
        }
        func_8C0567D4(list, n);
    }
    return *n;
}
