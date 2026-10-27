/*
 * Discriminator d02: -aggressive=2 turns division by a float constant into a
 * multiplication by its reciprocal (a / 8.0f -> FMUL by 0.125f, even a / 3.0f).
 * Without it SHC keeps FDIV.  The original game code divides by a pool constant
 * 8.0f at 0x8C0640D0 (MOVA 8C06419C / FMOV.S @R0,FR4 / ... / FDIV FR4,FR2), so
 * SA1 was NOT compiled with -aggressive=2 (unlike SA2's makefile).
 */
float d02a(float a) { return a / 8.0f; }
float d02b(float a) { return a / 3.0f; }
