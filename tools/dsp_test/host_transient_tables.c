/*
 * Host build of the transient PCM ROM (S084 VX3 remediation §18 T1).
 *
 * What:       includes config.h first, then clears INCCM so the firmware's
 *             section attribute (an ELF section name the host linker rejects)
 *             does not apply, then compiles the real transientTables.c.
 * Why:        test_insert_types needs the real stock `Clk` PCM to check that
 *             Plk is seeded from it (VX3-8.2).
 * Accessors:  the test_insert_types Makefile rule.
 */
#include "config.h"
#undef INCCM
#define INCCM
#include "../../Core/DSPAudio/transientTables.c"
