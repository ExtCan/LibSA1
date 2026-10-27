/*
 * Compiler probe t05: assorted leaf functions.
 * Original: 1ST_READ.BIN
 *   8C011042-8C011060 Find     (byte-array search loop, char param re-extended in loop)
 *   8C01B818-8C01B84E InRange  (squared distance vs radius, FMUL/FMAC/FMAC, MOVT;RTS;NOP)
 *   8C01B636-8C01B640 SetB7
 * Matches (function mode): Find = all versions; InRange = SHC >= 5.0r31;
 *   SetB7 = all versions.
 * Flags: -cpu=sh4 -division=cpu -fpu=single -endian=little -optimize=1 -speed
 *        -macsave=0 -extra=a=1800 -round=zero   (SHC 5.1r03)
 */
typedef struct { float x, y, z; } NJS_POINT3;
typedef struct {
    int pad0;
    int pad4;
    NJS_POINT3 pos;   /* +8  */
    int pad20;
    int pad24;
    float r;          /* +28 */
} OBJ;
typedef struct {
    char pad0[7];
    char b7;          /* +7 */
} GameStat;

extern char arr_8C19ACB8[16];
extern GameStat *gs_8C11204C;

/* @match Find 8C011042-8C011060 */
int Find(char a)
{
    int i;
    int r = -1;
    for (i = 0; i < 16; i++) {
        if (a == arr_8C19ACB8[i]) {
            r = i;
            break;
        }
    }
    return r;
}

/* @match InRange 8C01B818-8C01B84E */
int InRange(OBJ *o, NJS_POINT3 *p)
{
    float dx, dy, dz, d;
    dx = p->x - o->pos.x;
    dy = p->y - o->pos.y;
    dz = p->z - o->pos.z;
    d = dx * dx + dy * dy + dz * dz;
    if (d < o->r) {
        return 1;
    }
    return 0;
}

/* @match SetB7 8C01B636-8C01B640 */
void SetB7(char a)
{
    gs_8C11204C->b7 = a;
}
