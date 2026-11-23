/*
 * Core/DSPAudio/voiceSpace.h
 *
 *  Created on: 22.11.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * The Spc voice chain's spatializer and the Cymbal/HiHat low-pass
 * (S101 P3, S101_P3_BASIC_CHAIN_SPATIALIZER.md revision 5).
 *
 * What:       1. voiceSpace_process() / voiceSpace_processMix(): the stereo
 *                spatializer the Spc chain (VOICE_CHAIN_SPACE) runs in place
 *                of the transient, the post-chain drive and the SRR on Drum,
 *                Snare, Cymbal and HiHat. It is the Tetrakt's S1 law in
 *                integer form: two interpolated taps into one 50 ms voice
 *                unit, feedback through the Tetrakt's tone pole, a wet level,
 *                the dry at unity; the engine's amp (x velocity) ramp is
 *                fused into the same loop.
 *             2. voiceSpace_lp1(): the TPT low-pass Cymbal and HiHat play for
 *                the LP filter type in every chain (the ZDF LP's division and
 *                branches gone; shown as LP, as on Drum and Snare).
 * Why:        the measured costs of the S101 P3 proposal: the space stage
 *             costs less than the three stages it replaces only in this
 *             integer, mirrored-ring form, and the ZDF LP (101 instructions,
 *             3 VDIV a sample) was the Cymbal/HiHat worst case.
 * Accessors:  Drum_calcVoiceSyncBlock(), Snare_calcSyncBlockVoice(),
 *             Cymbal_calcSyncBlockVoice(), HiHat_calcSyncBlockVoice();
 *             InstrumentManager.c (unit bind, the row-byte stores).
 * Affiliates: voiceFold.h (VOICE_CHAIN_*), FxBuffer.h (the voice unit),
 *             TetraktTables.h (tktFbLpK[], the tone law), ResonantFilter.h.
 */
#ifndef VOICESPACE_H_
#define VOICESPACE_H_

#include <stdint.h>
#include "config.h"
#include "ResonantFilter.h"

/*
 * The voice unit's layout (S101 P3 §4.1).
 *
 * What:       VSPACE_RING samples of ring (68 x 32) and VSPACE_MIRROR samples
 *             after it holding a copy of ring[0..31]: 2,176 + 32 = 2,208, one
 *             FxBuffer voice unit (asserted in voiceSpace.c).
 *             The write position advances one block (32) a block, so it wraps
 *             only at a block boundary; a read block [start - 1, start + 31],
 *             start in [1, VSPACE_RING], never leaves the unit; the mirror is
 *             copied after the one block in 68 that writes ring[0..31].
 * Why:        no wrap test in the frame loop and no segment logic per block
 *             (the Tetrakt pays up to four segments a block).
 * Condition:  every tap is at least VSPACE_TAP_MIN samples, so no read in the
 *             block that writes ring[0..31] reaches a mirror sample not yet
 *             copied (host law SP-RING).
 */
#define VSPACE_RING        2176u
#define VSPACE_MIRROR      32u
#define VSPACE_TAP_MIN     32u     /* the spread tap's floor */
#define VSPACE_TIME_MIN    44u     /* the time tap's floor: 1 ms, the Tetrakt's */
#define VSPACE_TIME_MAX    2174u   /* 49.3 ms: a tap + 1 never reaches the write */

/*
 * The spread tap's form (S101 P3 §5.4, the device fallback).
 *
 * What:       1 (built): both taps interpolated, the Tetrakt's S1 law (form
 *             E). 0: the spread (right) tap reads whole samples (form H): the
 *             time stays interpolated, the spread moves in 22.7 us steps;
 *             about 5 instructions a sample less (prototype 46 -> 41).
 * Why:        the decided form is E; H is the measured fallback if the device
 *             run puts Cymbal/HiHat over the Tetrakt's 18.6k with E (the
 *             Step 0 rule, S101_P3_BASIC_CHAIN_SPATIALIZER.md §5.4). Build
 *             with -DVSPACE_SPREAD_INTERP=0.
 */
#ifndef VSPACE_SPREAD_INTERP
#define VSPACE_SPREAD_INTERP 1
#endif

/*
 * The spatializer state, one per drum-family voice (S101 P3 §6.8).
 *
 * What:       line   the voice unit (FxBuffer) or NULL while the slot holds
 *                    none: then the render is the dry ramp only, mono.
 *             wi     the write position, a multiple of 32, < VSPACE_RING.
 *             time, tone, level
 *                    the row bytes of transient_freq, instrument_decimation
 *                    and instrument_drive, stored by their writers in every
 *                    chain (InstrumentManager.c) so a switch to Spc finds
 *                    them current. Spread (transient_wave) and feedback
 *                    (transient_vol) are read from the engine's transGen,
 *                    whose writers store them exactly (byte, byte / 127).
 *             lp     the tone pole's state, Q15 x 2 (the taps' mean << 15).
 *             right  the right output block, which the mixer reads through
 *                    instrumentManager_slotRight() while the unit is held.
 * Size:       80 B (asserted in voiceSpace.c), inside the pinned 1,264 B
 *             slot reserve (InstrumentManager.c).
 */
typedef struct {
	int16_t *line;
	uint16_t wi;
	uint8_t  time;
	uint8_t  tone;
	uint8_t  level;
	uint8_t  pad[3];
	int32_t  lp;
	int16_t  right[OUTPUT_DMA_SIZE];
} VoiceSpace;

/*
 * Reset the state (S101 P3).
 * What:       line NULL, wi 0, lp 0, the right block silent; the row bytes
 *             take their defaults (time 64, tone 127, level 0: the row
 *             writers overwrite them on every apply).
 * Accessors:  the four engines' *_initVoice().
 */
void voiceSpace_init(VoiceSpace *sp);

/*
 * Bind or release the voice unit (S101 P3 §6.5).
 * What:       unit != line: the unit's 2,208 samples are cleared (FxBuffer
 *             owners clear what they claim), wi and lp restart; unit NULL
 *             releases (the render falls back to the dry ramp).
 *             unit == line: nothing (a re-apply keeps the tail).
 * Context:    foreground only (the chain writer), between render blocks.
 */
void voiceSpace_bind(VoiceSpace *sp, int16_t *unit);

/*
 * One block of the spatializer, one input (Drum, Cymbal, HiHat).
 *
 * What:       x = buf[n] x g (g ramped linearly g0 -> g1 over the block, the
 *             post-chain's ramp without its drive); two taps (time; the
 *             spread tap up to 50 % earlier), each interpolated; their mean
 *             through the tone pole into the feedback; ring = sat16(x + fb
 *             lp); buf[n] = sat16(x + level tapL), right[n] = sat16(x +
 *             level tapR).
 * Inputs:     sp (bound or not), buf (the filtered block, in place), g0, g1
 *             (the ramp's endpoints, 0 .. < 2), spread (0..127, the
 *             transient_wave byte), feedback (transient_vol / 127; 63/127 is
 *             0, 127/127 is +0.97, 0 is -0.955).
 * Outputs:    buf (left), sp->right, the ring, wi, lp.
 * Cost:       constant: one loop, every block, at every setting; the mirror
 *             copy in one block in 68.
 */
void voiceSpace_process(VoiceSpace *sp, int16_t *buf, float g0, float g1,
                        uint8_t spread, float feedback);

/*
 * One block of the spatializer, the Snare's two inputs.
 *
 * What:       as voiceSpace_process() with x = sat16(buf[n] x mix + add[n])
 *             x g: the Snare post-chain's mix and saturating add, without
 *             its drive.
 * Inputs:     add (the tone oscillator block), mix (0..1); the rest as above.
 */
void voiceSpace_processMix(VoiceSpace *sp, int16_t *buf, const int16_t *add,
                           float mix, float g0, float g1,
                           uint8_t spread, float feedback);

/*
 * The Cymbal/HiHat low-pass (S101 P3 §6.2; shown as LP).
 *
 * What:       a TPT state-variable low-pass on the ZDF's own coefficients:
 *             g = filter->g, damping 2R with R = (f >= 0.4499) ? 1 : q (the
 *             ZDF's stability rule), the input drive filter->drive. The
 *             ZDF LP's two saturators are kept as division-free cubics with
 *             the same small-signal slope and limits: the input 2 tanh(u/2)
 *             as u - u^3/27 (|u| <= 3, limit 2), the output fastTanh() as
 *             c (0.967 - 0.1340 c^2) (|c| <= 1.551, limit 1). State in
 *             filter->s1 / s2 (the ZDF's, cleared when the type changes to or
 *             from LP).
 * Inputs:     filter (coefficients, state), buf (in place).
 * Cost:       one division per block, none per sample, no branch.
 */
void voiceSpace_lp1(ResonantFilter *filter, int16_t *buf);

#endif /* VOICESPACE_H_ */
