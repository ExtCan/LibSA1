# libsa1 `gen/`: build-time generators and the boundary stub policy

Everything here is used by `LIB/build.py` (see the "Building" section of `LIB/README.md`); nothing is
compiled from this directory directly except through the generators.

| File | Role |
|---|---|
| `policy/stubs.tsv` | The boundary stub policy (sadc `docs/libsa1_integration.md` 10.5, decision D8): one line per function that pclower stubs, with its policy and the evidence for it |
| `gen_stubs.py` | `stubs.tsv` + pclower's `report.json` -> `build/gen/sa1g_stubs_policy_*.c` (the stubs; a base stub at an overlay-slot address first dispatches to the running world's overlay image, see "Overlay images") and `build/gen/sa1g_rtglue_*.c` (runtime functions without a DC address that neither the lowered runtime nor the world core define: `njFraction`, `UNK_PcWrite`) |
| `stub_evidence.py` | Measures the evidence column: calls of the original functions in the reference scripts, static reach, rand use; rewrites the measured part of each line |
| `binsyms.py` | COFF / ELF / PE symbol and section tables for the link test and the size report |
| `linktest/link_test.c` | The link test program (static `--whole-archive` and shared variants; checks the code map) |

## Stub policy

pclower (`--stub-unresolved`) lists every function the lowered code calls or takes the address of that no
lowered translation unit defines (217 on 2026-10-20, 3 of them in the STG05 overlay image: overlay code not
decompiled yet, camera, display, sound, game flow, boot / VMU / menu code and SDK functions the runtime does not
implement). The design's default is
**fatal**: the stub records the DC address and longjmps to the tick, which returns `SA1_ERR_NOT_IMPLEMENTED` and
queues `SA1_EVENT_UNIMPLEMENTED`. Every other policy must cite evidence. The format of a line and the policy
grammar are documented in the header of `policy/stubs.tsv`:

`fatal`, `void`, `ret <value>`, `null`, `address-only`, `freetask` (a task exec that frees its own task, design
10.5), `event <KIND> [id=] [bank=] [pos=] [arg=] [ret=]` (sound, voice, music, rumble, restart, respawn into the
world's event queue, design 7.4).

How they were decided (2026-10-20), in order of strength:

1. **Reference call log.** `stub_evidence.py` hooks every stub's original at its DC address in a private pure
   interpreter build and runs the 15 reference scripts (each hooked run is checked identical to its reference
   trace). "ref: N calls in steps in K/15 scripts, returns ..." is what the original did during logic steps:
   72 stubs are called during logic steps (8C605480 only in frame code), and every constant policy (`ret`) uses the value the original returned there
   (8C04C960 always 2, 8C099500 always 1 ...; 8C038D00 returned 0 in all 144,447 calls of the references of that
   day, which had the zero screen - it is the runtime's exact copy of the original now, src/rt/rt_draw.c section 6).
2. **Static reach** from the character mains and the libsa1 step functions (the particle manager 8C09DF64 among
   them since the step runs it) over `build/1ST_READ.xrefs.json`. Pointers in data tables are not followed: two stubs called through local function
   pointers (8C09E474, 0C900080: snow / sand trail effects whenever the floor has attr 8 / 0x10) were found by
   reading the callers.
3. **Callers' code** (the decompiled C): NULL checks before using an allocator's result (`null`), results
   ignored, outputs pre-zeroed (8C046F48).
4. The design's seed table (10.5) and `sadc/build/verify/missing_triage.md` categories.

Totals (217): fatal 107 (boot / VMU / menus / stage and object code that no character path reaches, the B_CHAOS7
/ STG08 overlays, the callees of the power-up tasks, the Red Mountain stage-restart path, the GD file system behind
the file loaders, math helpers whose results a constant cannot replace), void 64 (draws, fog, light and palette
state, camera hooks, texture loads, the STG05 tasks without C, Big's fishing HUD), event 20, ret 11, freetask 9,
address-only 5 (camera functions), null 1 (syMalloc).

Changes of 2026-10-21 (Wave A landing of the G-SA1 rounds plan, sadc docs/research/gsa1_rounds_plan.md section 7):
8 lines removed as stale, the functions are lowered now: the enemy work initialiser 8C047040, the follower AI's
8C049160 / 8C049A5A / 8C0585BA / 8C0585DC, and the fishing rope sim's 8C0EBFA8 / 8C0EC0A8 / 8C0EC60C (stale since its
decompilation); the alias line `B_CHAOS7_exec_0C900480` went with the alias (code_8C0B07E4.c calls `func_0C900480`).
1 line added: 8C083A1E (`fatal`, the boss-AI creator tail-called by the newly lowered wrapper 8C0585CA). The
B_CHAOS7 base stubs 0C900480 / 0C9004B6 / 0C9004D4 / 0C9004F2 stay (sadc docs/research/super_sonic.md Review R.2.3):
B_CHAOS7 has C now and is lowered as its own image, so they first run the world's B_CHAOS7 image and are fatal only
in a world without it. `sa1g_fatal` (CCL_Init's PC_ONLY exit) is a runtime function the world core defines
(`WORLD_RUNTIME` in gen_stubs.py: no glue). The measured evidence of every line was re-run against the screen
references (`stub_evidence.py --update-tsv`): 23 lines changed their counts. The particle execs 8C046480 /
8C0464EE / 8C046960 are lowered sadc C now (`src/rt/rt_effect.c` is gone).

Changes of 2026-10-20 (particle system, Big's fishing displays, 8C04C5E0 ...: integration of that decompilation
round): 10 lines removed as stale: the lowered file loader 8C09D9C6, the particle spawners 8C09E3E8 / 8C09E474, Big's
fishing displays 8C0E66C4 / 8C0E7720 / 8C0E78C0, the game's sprite draw 8C09E778 (defined by `src/rt/rt_draw.c`)
and the particle execs 8C046480 / 8C0464EE / 8C046960 (`src/rt/rt_effect.c`: the step runs the particle manager,
which calls them, so an address-only no-op would never end a particle). 7 lines added for the callees of the new C:
8C0E746C (`void`: the fishing line length readout, 2D HUD), 8C0EC60C (`ret 128`: the line's vertex alpha, the
original's own value without a camera), the cache control 8C5FC0C2 (`void`), gdFsReqRd32 8C601084 / gdFsGetStat
8C6013A4 (`fatal`, like the other gdFs functions: only the file loaders call them, after gdFsOpen), syMalloc
8C6021D6 (`null`) and syFree 8C602288 (`void`). 8C63EBC8 (a 2D polygon draw) went from `fatal` to `void`: Big's
tension gauge 8C0E72DA calls it while the lure is out, which would have ended his world when he cast. The gdFs
lines name their new callers. `py -3.12 build.py --strict-stubs` proves every stub has a line.

Changes of 2026-10-20 (stage tasks, sadc docs/libsa1_integration.md "Status"): 26 lines were removed as stale, the
functions are lowered now: the sprite particle spawners 8C0465E0 / 8C046A08 and the particle allocator 8C09DDFC /
8C09DE34 / 8C09DE3A (their rand draws are the game's again), the E-102 effect system 8C04FF58 / 8C050190 / 8C0502D8 /
8C050914, 8C066660 / 8C066732, 8C091C00 / 8C091C10, the power-up tasks 8C043C9E / 8C044060 / 8C044284, Big's lure
reset 8C0E91A0 (its C has a TARGET_PC body now), and the draws and the visibility test 8C038D00 that the runtime
implements for the display capture (src/rt/rt_draw.c: 8C03700C, 8C037098, 8C038CEC, 8C038D00, 8C60E080, 8C60E49A,
8C6215D6, njDrawModel, njDrawObject). 24 lines were added: the callees of the newly lowered code (the power-up
helpers, the effect execs 8C0464EE / 8C046960, 8C099538 `ret 1` as in all 80,317 reference calls, njDrawTexture
8C63ED08) and of the lowered stage overlays (the fog functions 8C63FB48 / 8C63FE20 / 8C63FE48, the light task
8C046120, the Red Mountain restart path, and the three STG05 image stubs below). The `pos=aN@twp` of an event reads
arena pointers only: STG05's act setup passes the owner -1 to 8C0941FC as an integer.

**FIDELITY lines** mark stubs whose original has side effects a constant cannot reproduce, mostly `rand` draws.
After the 2026-10-20 decompilations one is left: D_8C046B60 (an effect spawner that creates effect tasks, code not
disassembled); no reference trace depends on it (all ten replay strictly). The particle system is set up by the
world's boot like the stage start does (8C09DDDA / 8C09E2DA), and the step runs the particle manager 8C09DF64 where
the original step does, so finished particles give their nodes back (before 2026-10-20 the manager had no C and a
world stopped spawning after 512 particles).

### Overlay images

pclower gives every overlay image (`sadc/src/overlays/<dir>`: STG01, STG05, TIKAL_PROG) its own code map and
renames its functions `<IMAGE>__<name>` (sadc docs/libsa1_integration.md 2.5). A stub an image's TU needs at an
address of the image's code range is an image stub (`STG05__func_0C9131AA`): its line names it that way, and it is
reached through that image's map only (when the world holds STG05.PRS). Their evidence is measured like the others
(hooked at the address, in the scripts of that stage: the knux scripts). A base stub at an overlay-slot address
(0x0C900000-0x0CFFFFFF: 1ST_READ calls 0C907998, 0C900080 ... directly) first asks the running world's images
(`sa1g_fn_image_lookup`) and calls the image's function when there is one (8 such stubs today), else applies its
policy.

### Updating

```
cd <sadc> && py -3.12 harness/build.py --files none --tag <tag>          # private pure interpreter
cd <libsa1> && py -3.12 build.py --target x86_64-windows                # lowers the game: build/pclower/report.json
py -3.12 gen/stub_evidence.py --exe <sadc>/harness/build/sa1h_<tag>.exe --update-tsv [--add-missing]
```

`build.py` regenerates the C on every build. A stub without a line gets `fatal` and a warning (`--strict-stubs`
makes it an error); a line whose function is not a stub any more (decompiled and lowered since) is reported as
stale and ignored: a defined function is never stubbed. A signature change reported by pclower is printed; a
policy that does not fit the signature is an error.
