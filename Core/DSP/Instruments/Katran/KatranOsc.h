/*
 * KatranOsc.h - Katran's oscillator loops (S103 P1; S103 P4: the two cores
 * are named A and B).
 *
 * What:       twelve loops (S103 P2: OSC A in a full-rate and a half-rate
 *             set, see below), each one block (32 samples, 16 at half rate):
 *               kat_bPlain / kat_bPwmD / kat_bWarp / kat_bArena  OSC B into
 *                 blkB; each sample's LSB is 1 on the sample where the phase
 *                 wrapped (the carry of p + inc), and on that sample the row
 *                 switches to rowNext (K-D10);
 *               kat_aPlain / kat_aPwmD / kat_aWarp / kat_aArena, each as
 *                 *CoreA (core A, full rate) and *CoreB (core B, half rate;
 *                 S103 P4, formerly *L / *H): one copy of
 *                 OSC A into blkA, with the hard sync: per sample
 *                 s = P x wrap(B[n]) x 32767 + D[n]; a reset when s crosses
 *                 the threshold upward (cur = s > th; reset = cur & !prev):
 *                 phase = 0 and row = rowNext; a natural wrap also takes
 *                 rowNext.
 *             The arena loops read row[ph >> 22] (no interpolation: the rough
 *             read, P§3.4), then the DC block (one-pole HP at ~10 Hz), the
 *             damping one-pole (dampK), and the guard gain, into int16.
 *             S105 P1 A7: the OSC B loops read at the phase offset by their
 *             own last output (bFbK, Ring/FM Sideband).
 * Why:        the Colloid's loops sum pairs into buses and never reset;
 *             Katran's write blocks and take a reset per sample. One loop per
 *             form keeps every form a fixed cost; the dearest (PWM D or warp)
 *             is the one budgeted, and gate G-CPU2 keeps the arena form under
 *             it.
 * Inputs:     the voice (rows, phases, increments, widths, conditioning
 *             state), the threshold th (sth: 127 = never), D and B blocks
 *             for A.
 * Outputs:    blkB or blkA; phases, rows and conditioning state updated.
 * Accessors:  Katran_calcVoiceSyncBlock().
 * Affiliates: ColloidOsc.c (the read and PWM arithmetic), oscPwmTables.h,
 *             oscPwmWarp.h (osc_pwmWarp()), tools/dsp_test/modules/katran.c
 *             (laws KA-OSC, KA-CYC, KA-SYNC, KA-ARENA).
 */
#ifndef KATRANOSC_H_
#define KATRANOSC_H_

#include <stdint.h>
#include "KatranVoice.h"

/* A 1,024-sample row of zeros: the arena form's row when the arena view is
 * shorter than one window (defensive; the arena is ~63,000 samples). */
extern const int16_t katZeroRow[KAT_ARENA_WIN];

/* OSC B, one block into blkB (the wrap flag in each sample's LSB). */
void kat_bPlain(KatranVoice *v);
void kat_bPwmD(KatranVoice *v, int32_t sign);       /* -1 Rec, +1 Saw */
void kat_bWarp(KatranVoice *v);
void kat_bArena(KatranVoice *v);

/*
 * One copy of OSC A, one block into blkA (S103 P2: full + half rate).
 *
 * What:       the *CoreA loops render core A's copy at the full rate (32
 *             samples, its sync from blkD[0][n] and blkB[n]); the *CoreB loops
 *             render core B's copy at half rate (16 samples, phase step
 *             2 x incA[1], its
 *             sync from blkD[1][k] and the OR of OSC B's wrap bits over the
 *             two full-rate samples blkB[2k], blkB[2k + 1]). th is the sync
 *             threshold; sign the PWM D form's -1 Rec / +1 Saw.
 * Why:        core B runs at half rate (S103_P2_KATRAN_DUAL_FULL-HALF_
 *             IMPLEMENTATION.md §2). One noinline function per form and rate
 *             keeps each a fixed cost that katran_cpu.py can count by name;
 *             all eight share one always_inline body (KatranOsc.c), so the
 *             two rates cannot drift apart.
 * Accessors:  Katran_calcVoiceSyncBlock().
 */
/* S103 P4: the cores are A and B, not L and R. They sound different by
 * design (A full rate, B half rate) and the output stage rotates them through
 * the stereo field (KatranVoice.c kat_stereoControl()), so neither is tied to
 * a side. The function names say which core, not which output. */
void kat_aPlainCoreA(KatranVoice *v, int32_t th);
void kat_aPwmDCoreA(KatranVoice *v, int32_t th, int32_t sign);
void kat_aWarpCoreA(KatranVoice *v, int32_t th);
void kat_aArenaCoreA(KatranVoice *v, int32_t th);
void kat_aPlainCoreB(KatranVoice *v, int32_t th);
void kat_aPwmDCoreB(KatranVoice *v, int32_t th, int32_t sign);
void kat_aWarpCoreB(KatranVoice *v, int32_t th);
void kat_aArenaCoreB(KatranVoice *v, int32_t th);

#endif /* KATRANOSC_H_ */
