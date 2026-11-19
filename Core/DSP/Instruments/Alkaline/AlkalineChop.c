/*
 * AlkalineChop.c - the Alkaline's chopped pwm wave R-S (S099 P1; S099 P2:
 * REx dropped, the widths Rec's).
 * Contract: AlkalineVoice.h (Alkaline_renderChop()). Behaviour:
 * DRAFT_S-R_PWM_WAVE.md §4.3 and §6.4; the tables and checks: oscChopTables.h
 * (tools/plots/gen_pwm_tables.py T10-T15).
 *
 * Why a separate file: compiled -fno-lto, so the chop render cannot shift
 * LTO's partitioning of the other engines' loops (the S095 P2 rule; measured
 * in S099_P1_IMPLEMENTATION.md §13: in the LTO unit, ten Drum loops changed
 * and two grew by 1). Its only entry is a noinline call from alk_renderPwm().
 */
#include "AlkalineVoice.h"
#include "DiodeLadder.h"
#include "oscPwmTables.h"     /* the widths: oscPwmDRec[pos] (S099 P2) */
#include "oscChopTables.h"    /* S099 P1: the R-S curve, gate gains, mean and level */

/*
 * The chopped waves' input scale (S099 P1).
 *
 * What:       volts per unit of the chop's normalised wave (peak after its
 *             mean = 1 x level): 2.675 V / 32,767 x the table units, so the
 *             chop's positive peak reaches the 303 saw's 2.675 V.
 * Why:        the chop is unipolar around its mean (the ramp's floor is the
 *             gate's floor); keeping its peak at the saw's peak drives the
 *             ladder about as hard at pos 0. By ear in the B-G5 listening.
 * Accessors:  Alkaline_renderChop().
 */
#define ALK_CHOP_SCALE_V  (2.675f / 32767.0f)

/*
 * One block of typ pwm R-S (S099 P1, fused mode 5).
 *
 * What:       k = positionApplied (0..127: 0 the gate at 50 %, the Alpha
 *             Juno's Saw 2; 127 the narrowest, 0.39 %: S099 P2, user, Rec's
 *             widths, was Saw's 2.54 %); the gate row r =
 *             min(tableOffset + OSC_CHOP_LIFT, 10) (the gate plays an octave
 *             up; lift 2 halves its band again, Q-15); d = oscPwmDRec[k];
 *             then per block, with span = oscChopSpanF[r]:
 *               chC    = d / 1,024 x span     (the gate's floor at 0)
 *               chDc   = oscChopDcRsQ15[k] x span (the wave's mean)
 *               sScale = ALK_CHOP_SCALE_V x oscChopLevelRs[k] x drive
 *                        x oscChopInvSpanF[r]
 *             into DiodeLadder_renderB2OscChop() with the R-S curve table
 *             (S099 P2: REx, waveform 4, dropped; a stored 4 plays R-S).
 * Why:        noinline, as the other renders: each type's render keeps its
 *             registers to itself. Every value is a table read or a product:
 *             no VDIV.
 * Inputs:     voice (the async ran this block: phaseInc, tableOffset,
 *             positionApplied, driveGain, makeup); buf (output).
 * Outputs:    buf; osc.phase; the ladder state.
 * Accessors:  alk_renderPwm() (AlkalineVoice.c).
 * Affiliates: oscChopTables.h; DiodeLadder_renderB2OscChop(); bench law AL-CH.
 */
void Alkaline_renderChop(AlkalineVoice *voice, int16_t *buf)
{
	OscInfo *const osc = &voice->osc;
	const uint32_t k = (uint32_t)(osc->positionApplied & 127u);
	uint32_t r = (uint32_t)osc->tableOffset + OSC_CHOP_LIFT;

	if (r > 10u)
		r = 10u;
	{
		const uint32_t d = oscPwmDRec[k];
		const float span = oscChopSpanF[r];
		const float chC = (float)d * (1.0f / 1024.0f) * span;
		const float chDc = (float)oscChopDcRsQ15[k] * span;
		const float level = oscChopLevelRs[k];

		DiodeLadder_renderB2OscChop(&voice->ladder, buf, sawTable[r],
		                            oscChopCurveRs,
		                            &osc->phase, osc->phaseInc, d, chC, chDc,
		                            ALK_CHOP_SCALE_V * level * voice->driveGain
		                                * oscChopInvSpanF[r],
		                            voice->iLadEnd, voice->iAbcEnd * voice->makeup);
	}
}
