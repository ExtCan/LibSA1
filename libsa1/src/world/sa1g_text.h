/*
 * sa1g_text.h - the "game text" section of libsa1 (sadc docs/libsa1_integration.md 6.9).
 *
 * The world's fault handlers (Windows VEH, Linux SIGSEGV) only turn an access violation into a world
 * fault when the faulting instruction is inside game or runtime code. That code is collected in one
 * section whose bounds the world knows at run time:
 *
 *   Windows (COFF)  ".text$sa1g_m": the linker groups ".text$..." sections by suffix, so the world's
 *                   markers in ".text$sa1g_a" / ".text$sa1g_z" bracket it (sa1_world.c);
 *   ELF             "sa1g_text": a C-identifier section name gets __start_sa1g_text /
 *                   __stop_sa1g_text from the linker.
 *
 * Usage:
 *   - LIB/build.py compiles every lowered game / runtime object with
 *         -DSA1G_TEXT_PRAGMA -include src/world/sa1g_text.h
 *     so all functions of those translation units (static helpers and the inline helpers of
 *     sa1/guest.h included) land in the section;
 *   - other code that may fault on purpose inside a world (tests) marks single functions with
 *     SA1G_GAME_TEXT.
 */
#ifndef SA1G_TEXT_H
#define SA1G_TEXT_H

#if defined(_WIN32)
#define SA1G_TEXT_SECTION ".text$sa1g_m"
#else
#define SA1G_TEXT_SECTION "sa1g_text"
#endif

/* game-like code: no UBSan checks (a debug build must reach the world's fault handling, not a trap) */
#if defined(__clang__)
#define SA1G_GAME_TEXT __attribute__((section(SA1G_TEXT_SECTION), noinline, no_sanitize("undefined")))
#elif defined(__GNUC__)
#define SA1G_GAME_TEXT __attribute__((section(SA1G_TEXT_SECTION), noinline))
#else
#define SA1G_GAME_TEXT
#endif

#endif /* SA1G_TEXT_H */

/* force-included into game / runtime objects (outside the include guard on purpose) */
#if defined(SA1G_TEXT_PRAGMA) && defined(__clang__)
#if defined(_WIN32)
#pragma clang section text = ".text$sa1g_m"
#else
#pragma clang section text = "sa1g_text"
#endif
#endif
