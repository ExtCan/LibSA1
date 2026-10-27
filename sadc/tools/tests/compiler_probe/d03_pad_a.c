/*
 * Padding test object A (linked together with d03_pad_b.c by run_all.py).
 * With -extra=a=1800 the P section is ALIGN=32 and every literal pool is preceded
 * by .ALIGN 32.  Observed fill bytes:
 *   - .ALIGN fill inside an object, -code=machinecode:
 *       SHC 5.0r26 .. 5.1r03 : 00 09 00 09 ...  (big-endian NOP -- matches 1ST_READ.BIN)
 *       SHC 5.1r04 .. 5.1r13 : 09 00 09 00 ...  (little-endian NOP)
 *   - .ALIGN fill via -code=asmcode + asmsh (any asmsh version): 09 00 (LE NOP)
 *   - gap between objects (lnk of 5.1r03 and 5.1r08 tested; rof2bin -v= does not change it): 00 00
 */
extern int ext_8C6B7078;
int pa(int x) { return x + ext_8C6B7078; }
int pb(int x) { return x * 3 + ext_8C6B7078; }
