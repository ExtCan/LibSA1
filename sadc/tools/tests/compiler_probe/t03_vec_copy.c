/*
 * Compiler probe t03: 12-byte struct copies (NJS_POINT3 = NJS_POINT3) and
 * field-wise float arithmetic.  These prove -speed: without -speed SHC calls
 * the runtime block-move helper (R1=dst, R2=src, R0=size) instead of the
 * inline MOV.L load/store pairs seen in the original.
 * Original: 1ST_READ.BIN
 *   8C01B500-8C01B554 F  (address of playertwp[] array CSE'd into a stack slot)
 *   8C01B554-8C01B596 G
 * Matches (function mode): SHC 5.1r03/r04/r08/r11/r13 with -speed only.
 *   5.0r3x/5.1r01 differ in register allocation/scheduling of the copy.
 * Flags: -cpu=sh4 -division=cpu -fpu=single -endian=little -optimize=1 -speed
 *        -macsave=0 -extra=a=1800 -round=zero   (SHC 5.1r03)
 */
typedef struct { float x, y, z; } NJS_POINT3;
typedef struct {
    short w0;
    short w2;
    short w4;           /* +4  */
    char pad[26];
    NJS_POINT3 pos;     /* +32 */
} TWK;

extern TWK *playertwp_8C78C608[];
extern NJS_POINT3 v_8C6B709C;
extern int flag_8C6B6E9C;
extern void sub_8C63A7E4(NJS_POINT3 *, NJS_POINT3 *);
extern float sub_8C63A75C(NJS_POINT3 *);
extern TWK *pp_8C18B7EC;
extern NJS_POINT3 old_8C6B70A8;
extern NJS_POINT3 spd_8C6B6EA0;

/* @match F 8C01B500-8C01B554 */
void F(void)
{
    sub_8C63A7E4(&v_8C6B709C, &playertwp_8C78C608[0]->pos);
    if (sub_8C63A75C(&v_8C6B709C) < 0.1f && (playertwp_8C78C608[0]->w4 & 1)) {
        flag_8C6B6E9C = 1;
    } else {
        flag_8C6B6E9C = 0;
    }
    v_8C6B709C = playertwp_8C78C608[0]->pos;
}

/* @match G 8C01B554-8C01B596 */
void G(void)
{
    spd_8C6B6EA0.x = pp_8C18B7EC->pos.x - old_8C6B70A8.x;
    spd_8C6B6EA0.y = pp_8C18B7EC->pos.y - old_8C6B70A8.y;
    spd_8C6B6EA0.z = pp_8C18B7EC->pos.z - old_8C6B70A8.z;
    old_8C6B70A8 = pp_8C18B7EC->pos;
}
