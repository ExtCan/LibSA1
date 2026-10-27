# Matching tips for SHC 5.1r03 (Sonic Adventure DC)

Curated from matched functions. Read before decompiling; see also
`docs/compiler.md` (how the compiler/flags were identified).

## Tooling

* `py -3.12 tools/funcinfo.py <func>`: asm, pool constants, callers/callees, Ghidra draft.
* `py -3.12 tools/shcdiff.py <func> <file.c> [-v]`: compile + compare (C file must be under the project root).
* `--flags=-define=NON_MATCHING` compiles your `#ifdef NON_MATCHING` bodies so you can keep testing them.
* Integer `/` and `%` call the runtime helpers `__divls/__divlu/__modls/__modlu`;
  large struct copies call `__quick_mvn`. These resolve automatically.

## Code generation rules observed

* **Stack locals are laid out in reverse declaration order**: the variable declared
  last gets the lowest stack address. Order declarations to reproduce the
  original `@(disp,R15)` offsets.
* **Pre-decrement stores** (`ADD #12,Rn` then `FMOV.S FRx,@-Rn` x3) come only from
  explicit source like `v += 3; *--v = z; *--v = y; *--v = x;`.
* A pointer to a global used several times is kept in a callee-saved register only
  if the source assigns it to a local at the start (`p = &B_8Cxxxxxx;`).
* `EXTS.W` after a call result usually means the result was assigned to a `Sint16`
  temporary before being used; loading a parameter after the call (instead of
  before) can depend on whether an expression is inline or in a temporary.
* Loops over arrays counting down (`for (i = 255; i >= 0; i--)`) may be unrolled
  2x into `MOV.B Rx,@-Rn` stores.
* `(Uint8)x` vs `(x & 0xFF00) >> 8` into a `Uint8` local select different code;
  the game often stored a whole value in an int local then extracted bytes.
* Float literals are **truncated** (`-round=zero`): `0.1f` gives `0x3DCCCCCC`.
  If the original has a constant one ULP higher, write a slightly larger decimal.
  Known: `10430.381f` = `0x4622F986` (radians to Angle, not the exact 65536/2pi).
* 16-bit constants are built with `MOV #imm,Rn; SHLL8 Rn; ADD #imm,Rn` instead of a
  `MOV.W` pool load (because of `-extra=a=1800`); nothing to do in C.
* `-speed`: 12-byte struct copies are inlined as `MOV.L` pairs; bigger ones call
  `__quick_mvn`.
* Register-allocation ties (two values swapping R8/R9) can depend on constant
  values and are sometimes not reproducible from source alone; after a dozen
  attempts, leave such a function as NON_MATCHING with a note.

## Engine facts

* `task` (include/task.h): `twp` at +0x20; `taskwk.counter.b[1]` holds the character id.
* Players: `B_8C78C608[pno]` is the array of player `taskwk*` (see code_8C043A20.c).
* `func_8C63A970`/`func_8C63A9B8` behave like `njSin`/`njCos` (Angle in R4, Float return).
* `func_8C10D058` (likely asinf/acosf), `func_8C10D0F8` (likely atan2f) take/return Float.
* Collision work (`taskwk.cwp`, +0x38) in SA1 DC differs from SADX: `0x00 Uint16 id`,
  `0x0C info` (array of 0x34-byte shapes, attr at +0), `0x10 task *mytask`,
  `0x16 Sint16 hit_num`, `0x18 hit_cwp`.
* `func_8C04F7E0` returns `Uint16` `(level << 8) | act`.
* `func_8C60EDF0` takes a `{Float a, r, g, b}` (njSetConstantMaterial-like);
  `func_8C63CE24` is njDrawLine3D-like `(NJS_POINT3COL *, Sint32 n, Uint32 attr)`.
