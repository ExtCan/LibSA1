# Fix list from harness verification

Findings from the SH-4 reference simulator (docs/harness_simulator.md), the per-function route matrix and the FMAC
audit. State after the verify-fix pass of 2026-10-19 (workflow `tools/workflows/verify_fix.js`, 100 files).

## Status (libsa1 configuration: `harness/build.py --non-matching --pc`)

* **All 10 sample traces are bit-identical with every native decompiled function routed** (`regress.py --route
  --exe harness/build/sa1h_nm_pc.exe`): Sonic (5 scripts, Emerald Coast 1), Tails (Speed Highway 1), Knuckles (Red
  Mountain 3), Amy (Twinkle Park 2), E-102 (Final Egg 1), Big (Emerald Coast 3); 1,370 native functions, of which every
  one the scripts execute was also checked routed alone (`harness/tools/route_matrix.py`).
* 2026-10-20, after the L5/L6/Tikal batches (514 native files): still 10/10 identical; the route matrix finds only the`n  known helper 8C02D410; five object-enabled scripts (`obj_*`: rings, springs, enemies, item boxes, the E-100 Zero chaser)`n  are identical with everything routed as well.
* Matching build unchanged (`1ST_READ.BIN: OK`); matched functions edited by the pass were re-checked with shcdiff.

## Rules adopted
* **FMAC**: libsa1 compiles with `-ffp-contract=off`; every FMAC of the original is written `SA1_FMAC(a, b, c)` or
  `SA1_FMAC_ACC(acc, a, b)` (include/common.h; the latter is exactly `acc += a * b` for SHC, so matched code keeps
  matching). `py -3.12 tools/fmac_audit.py [file]` compares per function the FMAC count of the original with the fused
  operations of the PC build. Remaining differences (31) are all explained: SHC unrolls loops (count x unroll factor),
  clang unrolls or tail-merges identical blocks, or inlines a same-file callee SHC tail-called.
* **Float literals** that round differently under SHC truncation are spelled with the RZ value (0.29999999f etc.);
  the harness's native copies rewrite all literals anyway, libsa1 must do the same (or rely on the spellings).
* **Float compares** follow the asm operand order (`!(a > b)` vs `a <= b`) so NaN takes the original branch.

## Fixed in this pass (examples)
* 110 functions: FMAC sites not fused (e.g. `func_8C06E904` air physics, `PGetAcceleration`, `func_8C076782`,
  `SonicMain` and the other character mains: the step-4 vertical-speed divergence of every character).
* `func_8C02CEF4` / `func_8C02CF20` / `func_8C02CF48` (collision-object lists): the in-use list head link is the
  negated head address (`0 - &D_8C02D548`), not NULL; libsa1 must keep that value (`COLOBJ_HEAD_LINK`).
* `func_8C02D170`: `func_8C10D0A8` is atan2-like with two floats `(Float y, Float x)` (core `8C10E6A0`).
* `func_8C0870EA` (dropped rings): FMAC, literal windows, compare order.

## Remaining
* `code_8C02D060.c` helpers `func_8C02D278` (normalises FV4 in place, FR4-FR6 in/out), `func_8C02D286` (FR0-FR8 in,
  FR9-FR11 out, clobbers FR15), `func_8C02D2B2` (returns R4/R5 advanced by one vertex), and `func_8C02D410`
  (`code_8C02D410.c`, called with R12/R13/R14): private register conventions of hand-written asm. Their C bodies
  are exact when called from C (now file-local helpers of the C callers; verified with the callers routed), but they
  cannot be routed alone. Not a libsa1 problem as long as the whole module is native.
* `func_8C0870EA`: on one path the original stores leftover FR4/FR5 of a callee (uninitialised in the source);
  C cannot reproduce it (12 mismatches in 30,000 random iterations; never in the traces).
* `func_8C089528`: uninitialised `volatile n` array bound (native stack smash).
* Native loops on random inputs (heap/list functions with garbage pointers): `MFree`, `func_8C098998/89C8/89E0/89F8`,
  `func_8C04B48C`, `func_8C04B82E`, `func_8C0B33CE` (fine on real state: traces).
* `include/sa1/engine.h`: declare `Float func_8C10D0A8(Float y, Float x);`.

## Process
* Acceptance for decompiled C entering libsa1: identical traces with the function routed alone and with everything
  routed (`regress.py --route --exe harness/build/sa1h_nm_pc.exe`), `sa1h diff <func> 40 1 --warm --ulps 0` without
  mismatches, FMAC audit consistent.
* Loop: `trace --coverage` (once per script, pure) -> `route_matrix.py` -> `tools/make_fix_units.py` (+ FMAC audit)
  -> `tools/workflows/verify_fix.js` (one agent per file, private one-file harness `--files F --tag T`).