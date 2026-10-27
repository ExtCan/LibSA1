/*
 * Compiler probe t06: call sequences (JSR via literal pool, BSR to a function
 * defined in the same file, tail calls via JMP with LDS.L in the delay slot,
 * switch with fall-through, float parameter spills).
 * Original: 1ST_READ.BIN
 *   8C036F12-8C036F26 CallSeq  (MOV.L R4,@-R15 in JSR delay slot; BSR 8C036BC0; JMP tail call)
 *   8C01B640-8C01B684 Start    (switch 0/2 + fall-through, BSR 8C01AD2C, int/float spills)
 * The two "stub" functions below only stand in for the real same-file callees
 * (they must be defined in the file so SHC emits BSR, and must be too large to
 * be inlined by -speed); they are not compared.
 * Matches (function mode): CallSeq = SHC >= 5.0r31 (5.0r26/r28 use ADD #-4,R15 +
 *   MOV.L R4,@R15 instead of the pre-decrement push); Start = SHC >= 5.1r03.
 * Flags: -cpu=sh4 -division=cpu -fpu=single -endian=little -optimize=1 -speed
 *        -macsave=0 -extra=a=1800 -round=zero   (SHC 5.1r03)
 */
extern void e1(int);
extern void f_8C605DAC(int);
extern void f_8C605E0A(void);
extern int state_8C6B706C;
extern int arg_8C6B7070;
extern float farg_8C6B7074;
extern void func_8C024582(void);

/* stand-in for the real same-file function at 8C036BC0 (not compared) */
void f_8C036BC0(int a)
{
    int i;
    for (i = 0; i < a; i++) {
        e1(i); e1(a); e1(i + a); e1(i * a);
    }
}

/* stand-in for the real same-file function at 8C01AD2C (not compared) */
void sub_8C01AD2C(void (*fn)(void), int a, int b)
{
    int i;
    for (i = 0; i < a; i++) {
        e1(i); e1(b); e1(a); e1(i + b);
        state_8C6B706C = a + b;
        fn();
    }
}

/* @match CallSeq 8C036F12-8C036F26 */
void CallSeq(int p)
{
    f_8C605DAC(p);
    f_8C036BC0(p);
    f_8C605E0A();
}

/* @match Start 8C01B640-8C01B684 */
int Start(int a, float f)
{
    switch (state_8C6B706C) {
    case 0:
        sub_8C01AD2C(func_8C024582, 0, 0);
    case 2:
        state_8C6B706C = 1;
        arg_8C6B7070 = a;
        farg_8C6B7074 = f;
        return 0;
    }
    return 1;
}
