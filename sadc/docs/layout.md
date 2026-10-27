# 1ST_READ.BIN layout and toolchain notes

Facts established while building the matching asm build. Addresses are
runtime addresses (file offset + 0x8C010000).

## Overall layout

| Range | Contents |
|-------|----------|
| `8C010000-8C0100A0` | Katana startup stub (cache setup, jump to init), "Lib Handle" table |
| `8C0100A0-8C111194` | game code (SHC `P` sections of every game object, in link order) |
| `8C111194-~8C15xxxx` | game constant data (`C` sections: strings, const tables), same object order |
| `~8C15xxxx-8C5FBB60` | game initialised data (`D` sections: models, motions, tables), same object order |
| `8C5FBB60-8C66C2A8` | library code: Shinobi/Katana SDK, Ninja (`Ninja Ver 01000081 Build:Mar 04 1999`), sound driver, libc |
| `8C66C2A8-8C67C680` | library data |
| `8C67C680-...` | BSS (not in file) |

The original link therefore looked like `START P,C,D,B(8C010000)` for the
game objects with the libraries' sections placed after them. Code chunks
reference const data and initialised data that advance monotonically with
chunk address, so each source file's data can be attributed to it.

## Startup

* `8C010000`: six NOPs, sets bits in the cache control register `FF00001C`,
  jumps to `func_8C0100A0`.
* `func_8C0100A0`: fills `8C00C000-8C00F400` (the stack) with `"SEGA"`,
  calls two init routines (`func_8C0100D8`, `func_8C0100C8`), then
  `func_8C053940` (the game's main), then an exit routine in the Shinobi
  library (`func_8C5FF936`).

## SHC code generation facts used by the tools

* One literal pool is shared by all functions of a translation unit and is
  placed after the last function, or earlier when a PC-relative load would
  go out of range. Functions are not individually aligned; each object's
  `P` section is 4-aligned.
* Alignment padding inside code is `0x0009` (NOP), `0x0900` (byte-swapped
  NOP, emitted by asmsh `.ALIGN` in code) or `0x0000`.
* Dense switches compile to
  `SHLL R0; MOV R0,R1; MOVA tbl,R0; MOV.W @(R0,R1),R0; BRAF R0; NOP`
  with 16-bit entries relative to the BRAF+4 address (55 tables found).
* Calls to functions in the same file use `BSR`/`BRA` when in range;
  external calls load the address from the literal pool and `JSR`/`JMP`.
* `-optimize=1` (SHC 5.0r28) emits compare chains even for dense switches in
  small tests; jump tables in the game indicate larger/denser switches or
  different options.

## Assembler / linker behaviour

* `asmsh` treats `/` as an option switch: always pass Windows paths.
* Unaligned `.DATA.L`/`.DATA.W` are emitted where written (warning 816 only).
* Illegal delay-slot instructions are an error (150), never silently fixed.
* No `.BINCLUDE` in asmsh 4.0; data is emitted as `.DATA` directives
  (1 MB of directives assembles in ~0.15 s).
* `lnk` concatenates same-named sections in INPUT order, aligning each
  object's section to its `ALIGN`; `rof2bin -e` is exclusive.
* Label differences (`.DATA.W L2-L1`) and `sym+off` operands are supported.
