/*
 * KatranOsc.h - Katran's oscillator loops (S103 P1).
 *
 * What:       eight loops, each one block (32 samples):
 *               kat_bPlain / kat_bPwmD / kat_bWarp / kat_bArena  OSC B into
 *                 blkB; each sample's LSB is 1 on the sample where the phase
 *                 wrapped (the carry of p + inc), and on that sample the row
 *                 switches to rowNext (K-D10);
 *               kat_aPlain / kat_aPwmD / kat_aWarp / kat_aArena  one copy of
 *                 OSC A into blkA, with the hard sync: per sample
 *                 s = P x wrap(B[n]) x 32767 + D[n]; a reset when s crosses
 *                 the threshold upward (cur = s > th; reset = cur & !prev):
 *                 phase = 0 and row = rowNext; a natural wrap also takes
 *                 rowNext.
 *             The arena loops read row[ph >> 22] (no interpolation: the rough
 *             read, P§3.4), then the DC block (one-pole HP at ~10 Hz), the
 *             damping one-pole (dampK), and the guard gain, into int16.
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

/* One copy of OSC A, one block into blkA (c 0 = L, 1 = R; th the sync
 * threshold). */
void kat_aPlain(KatranVoice *v, uint32_t c, int32_t th);
void kat_aPwmD(KatranVoice *v, uint32_t c, int32_t th, int32_t sign);
void kat_aWarp(KatranVoice *v, uint32_t c, int32_t th);
void kat_aArena(KatranVoice *v, uint32_t c, int32_t th);

#endif /* KATRANOSC_H_ */
