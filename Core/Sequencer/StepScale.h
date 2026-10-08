/*
 * Core/Sequencer/StepScale.h
 *
 * Shared timing-scale vocabulary for Pattern track settings and the FX
 * sequencer (Session 072 step 8; Effects plan §11.3).
 *
 * S078 P1 replaced the original 14-entry discrete scale table with a
 * 128-position continuous log curve. The retained byte is now a 7-bit CC
 * value (0..127) that is also the index into the Q8.8 tick LUT.
 */
#ifndef STEP_SCALE_H_
#define STEP_SCALE_H_

#include <stdint.h>

/*
 * 128-position continuous step-scale parameter (S078 §2.1).
 *
 * What: the full CC 0..127 range replaces the former 14-entry discrete table.
 * STEP_SCALE_DEFAULT is CC 76, the 1/16th note stop on the log curve. Why:
 * the per-track step scale is now a continuous log parameter; the old 14-entry
 * index is retired. Inputs: every caller that clamps or defaults a scale byte.
 * Outputs: storage, menu, and playback all operate on the 0..127 domain.
 * Affiliates: StepScale.c (LUT and labels), sequencer.c (DDA accumulators),
 * PatternData.c/h (default), EffectTypes.h, menu.c, copyClearService.c.
 */
#define STEP_SCALE_COUNT   128u
#define STEP_SCALE_DEFAULT 76u

/*
 * Return the Q8.8 fixed-point 96-PPQ tick interval for one CC position.
 *
 * What: looks up the 128-entry Q8.8 LUT. Input: CC 0..127 from stored
 * track_scale or FX seq_step_scale. Output: a uint16_t in Q8.8 format
 * (integer part in the high byte, fractional part in the low byte),
 * representing the number of 96-PPQ ticks per step at this scale position.
 * Out-of-range inputs return the default (CC 76, 1/16 = 24.0 ticks = 0x1800).
 * Affiliates: sequencer.c DDA accumulator, seq_fxClockTick(), seq_realign
 * paths, CrumpBitEffect.c.
 */
uint16_t stepScale_ticksQ8(uint8_t cc);

/*
 * Return the three-character compact label for one CC position.
 *
 * What: for the 14 nudge stops, returns the symbolic name (e.g. "/16",
 * "32t"). For all other CC values, returns NULL; the caller formats the raw
 * CC integer. Input: CC 0..127. Output: pointer to a static 3-char string or
 * NULL. Affiliates: menu.c MENU_TRACK_SCALE display, menuEffects.c FX Seq
 * scale display, CrumpBitParameters.c rate display.
 */
const char *stepScale_shortName(uint8_t cc);

/*
 * Return the full-width label for one CC position, or NULL when it is not a
 * nudge stop. Same contract as stepScale_shortName() with the long vocabulary
 * ("1/32T", "d 1/16", ...). Affiliates: menuEffects.c full-view display.
 */
const char *stepScale_longName(uint8_t cc);

/*
 * Return nonzero when the CC lands exactly on one of the 14 nudge stops.
 *
 * Input: CC 0..127. Output: 0 or 1. Affiliates: menu.c display decision.
 */
uint8_t stepScale_isMusicalStop(uint8_t cc);

/*
 * Write the fixed three-character display field for one CC position.
 *
 * What: nudge stops write their symbolic short name; every other CC value is
 * written as a right-justified three-character decimal ("  1".."127"). This
 * is the shared formatter for the three-character fields that would otherwise
 * each repeat the NULL fallback. Inputs: CC 0..127 and a three-byte output.
 * Outputs: exactly three characters, no terminator. Affiliates: menu.c,
 * menuEffects.c, CrumpBitParameters.c.
 */
void stepScale_formatShort(uint8_t cc, char out[3]);

#endif /* STEP_SCALE_H_ */
