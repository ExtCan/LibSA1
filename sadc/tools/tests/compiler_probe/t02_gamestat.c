/*
 * Compiler probe t02: game-status setters + a float distance check with a
 * small switch and a tail call to a same-file function (BRA tail call).
 * Original: 1ST_READ.BIN 0x8C01B3E0-0x8C01B4D2 (pool at 0x8C01B4E0)
 *   8C01B3E0 SetSub, 8C01B406 GetSub, 8C01B40E GetB6, 8C01B416 SetB6,
 *   8C01B436 Check
 * Matches (function mode): all five on SHC 5.1r03/r04/r08/r11/r13.
 *   - 5.0r26/r28: pool-load scheduling differs in SetSub/SetB6.
 *   - 5.0r31/r32: Check spills the float to the stack instead of FR15.
 *   - 5.1r01:     switch compare chain sorted by case value (0,1,2,..) instead
 *                 of source order (1,0,2,3,4).
 * Flags: -cpu=sh4 -division=cpu -fpu=single -endian=little -optimize=1 -speed
 *        -macsave=0 -extra=a=1800 -round=zero   (SHC 5.1r03)
 * Float literal 0x3C23D708: SHC converts decimal literals with truncation
 * (-round=zero, default), so a literal slightly *above* the wanted float is used.
 */
typedef struct { int a; int b; } Ent8;
typedef struct { int first; int pad[3]; } Ent16;
typedef struct {
    char pad0[4];
    short w4;    /* +4  */
    char b6;     /* +6  */
    char b7;     /* +7  */
    char mode;   /* +8  */
    char sub;    /* +9  */
    char pad10[2];
    int f12;     /* +12 */
    int f16;     /* +16 */
    int f20;     /* +20 */
} GameStat;
typedef struct {
    char pad[20];
    int ang[3];                     /* +20 */
    struct { float x, y, z; } pos;  /* +32 */
} Obj;

extern GameStat *gs_8C11204C;
extern GameStat *gs2_8C112050;
extern Ent8 *tbl_8C6B705C;
extern Ent16 *tbl_8C6B7054;
extern int DiffAng_8C055D5A(int, int);

/* @match SetSub 8C01B3E0-8C01B406 */
void SetSub(char a)
{
    gs_8C11204C->sub = a;
    gs_8C11204C->f16 = tbl_8C6B705C[gs_8C11204C->sub].b;
    gs2_8C112050->f20 = 0;
}

/* @match GetSub 8C01B406-8C01B40E */
char GetSub(void)
{
    return gs_8C11204C->sub;
}

/* @match GetB6 8C01B40E-8C01B416 */
char GetB6(void)
{
    return gs_8C11204C->b6;
}

/* @match SetB6 8C01B416-8C01B436 */
void SetB6(int a)
{
    gs_8C11204C->w4 = -1;
    gs_8C11204C->b6 = a;
    gs_8C11204C->f12 = tbl_8C6B7054[a].first;
}

/* @match Check 8C01B436-8C01B4D2 */
void Check(Obj *a, Obj *b)
{
    float dx, dy, dz, d;
    dx = a->pos.x - b->pos.x;
    dy = a->pos.y - b->pos.y;
    dz = a->pos.z - b->pos.z;
    d = dx * dx + dy * dy + dz * dz;
    if (d < 0.0099999986f
        && DiffAng_8C055D5A(a->ang[0], b->ang[0]) < 91
        && DiffAng_8C055D5A(a->ang[1], b->ang[1]) < 91
        && DiffAng_8C055D5A(a->ang[2], b->ang[2]) < 91) {
        switch (gs_8C11204C->sub) {
        case 1:
            SetSub(3);
            break;
        case 0:
        case 2:
        case 3:
        case 4:
            SetSub(0);
            break;
        }
    }
}
