/*
 * ColloidVoice.c - the Colloid voice engine (S095 P2; S095 P3 follow-ups).
 * See ColloidVoice.h. Behaviour: S095_P2_DRAFT_COLLOID.md §1-§5, as amended
 * by S095_P3_COLLOID_FOLLOW_UPS.md (F2-F10).
 */
#include <math.h>
#include <string.h>
#include "ColloidVoice.h"
#include "ColloidOsc.h"
#include "ColloidTables.h"
#include "AlkalineControl.h"    /* alk_log2f(), alk_exp2f(), ALK_LOG2E (S093 P3 fast maths) */
#include "AlkalinePostTables.h" /* alkOvdGain/alkOvdMakeup/alkToneC (S095 P3 F10: the Alkaline's post law) */
#include "wavetable.h"          /* sine_table, sawTable, recTable, triTable */
#include "oscPwmTables.h"       /* the S094 P4 width law: D, levels, warp W/Ra/Rb, Tri lift */
#include "MidiPitch.h"          /* midiPitch_detune(), MIDI_DEFAULT_TRIGGER_NOTE */
#include "modulationNode.h"     /* velocityModulators[], modNode_updateValue() */
#include "InstrumentManager.h"  /* instrumentManager_applyVelocityModulationTarget() */
#include "sequencer.h"          /* seq_isRunning() (read only) */
#include "random.h"             /* GetRngValue() (main loop only; col_rngSeed()) */

/*
 * Control constants (S095 P2). The "by ear" values (C-D12/C-D14/C-D15 and
 * the ranges of H.3) are first settings, set in the listening step and
 * recorded there.
 *
 * COL_LOG2_440        log2(440 Hz): the A4 anchor of the pitch formula.
 * COL_CUT_BASE_HZ     the cutoff law's 0: 20 Hz.
 * COL_CUT_ST_MAX      the cutoff law's top in semitones above 20 Hz: 115.7
 *                     = 15,963 Hz, where the Pade tangent below is still
 *                     within 0.4 % of tan() (0.82 at 18 kHz, its pole at
 *                     0.503 fs); the filter envelope is clamped to it too.
 * COL_PI_OVER_FS      pi / fs: the prewarp argument per Hz.
 * COL_SLEW            the per-block one-pole on the cutoff and the damping
 *                     (SVF_recalcFreqSlew()'s 0.28: about 3.5 blocks).
 * COL_K_SPAN          k = 2 - 1.95 res: k's floor 0.05 bounds the Q (about 20).
 * COL_FENV_ST         fam at either end moves both cutoffs 72 semitones (6
 *                     octaves) at envelope level 1.
 * COL_DRIVE_OCT       drv 0..1 -> the drive gain 2^(4 drv) = x1..x16.
 * COL_CLIP            the drive's input clamp: the cubic below is monotonic
 *                     up to 1.5, where it reaches exactly 1.0.
 * COL_CUBIC           4/27: y = x - (4/27) x^3, the division-free soft clip.
 * COL_OUT_SCALE       full scale of the int16 output stores.
 * COL_EG_ATTACK_AIM   the attack aims at 1.2 and stops at 1.0: a finite
 *                     attack with the analogue curve.
 * COL_EG_SETTLE       a decay within 0.01 of its sustain level is settled.
 * COL_EG_IDLE_LEVEL   a release below 1e-4 ends (the ampEnvelopeQuiet()
 *                     threshold).
 * COL_DETUNE_MAX_OCT  dtn 127 spreads +-250 cents (S095 P3 F5, x5 of P2's
 *                     50): 250 / 1200 octave, applied in log2 with the
 *                     slewed pitch, so it is exact at any amount.
 * COL_SPREAD_GAIN     spr's home positions x5 (S095 P3 F6).
 * COL_MOT_DEPTH_MAX   mot's depth law 3.0 x sqrt(mot): P2's 0.6 (C-D15) x5
 *                     (S095 P3 F6, Q3: the depth only, the rate unchanged).
 *                     Past +-1 the positions are shaped by the cubic edge
 *                     (Q4): they crowd the edges and keep moving.
 * COL_MOT_RATE_MIN_HZ mot's rate law 0.05 Hz x 40^mot: 0.05 .. 2 Hz.
 * COL_MOT_RATE_OCT    log2(40).
 * COL_MOT_PHASE_PER_HZ one block's motion phase step per Hz: 32 / fs x 2^32.
 * COL_INC_PER_HZ      an oscillator's phase step per Hz: 2^32 / fs.
 * COL_F_MAX           no oscillator above 0.45 fs (an interval up to +63
 *                     semitones on a high note must not wrap the step).
 * COL_W_LOG2_LO       log2(80 Hz): w(f) is 0 at and below 80 Hz (C-D14).
 * COL_W_OCTAVES       w(f) rises over 3 octaves: 1 at and above 640 Hz.
 * COL_NORM_WAV/PWM    the bus into the drive, 1 / (full scale x sqrt(N)):
 *                     detuned free-running oscillators add by power (C-D12).
 *                     S095 P3 F2: every pwm form is levelled to a 32,768 full
 *                     scale as the platform's store is (Saw and Rec by the
 *                     mean of oscPwmLevel*Q14[width] / 16,384 over the stack,
 *                     the warp forms at 1), so P2's Rec, which ran at half
 *                     (65,536), is now at the platform's level (+6 dB at
 *                     pos 0).
 * COL_HEADROOM        S095 P3 (device reports: "the oscillators are clipping
 *                     the filter stage", then "still clips"): x0.5 on the bus
 *                     into the filter drive. Free-running stacks add by power,
 *                     but their peaks run about 3.7 x their RMS; at
 *                     1 / (full scale x sqrt(N)) alone a 12-saw stack peaked
 *                     at 2.24 and 1.45 % of its samples hit the shaper's
 *                     +-1.5 clamp at drv 0. At x0.5 the peak is 1.12, inside
 *                     the clamp (-6 dB against P2). There is no make-up after
 *                     the filters: the shaper's output is at most +-1.0, full
 *                     scale, and any gain after it hard-clips a driven sound
 *                     at int16 (the first fix's x1.43 did: 49 % of a driven
 *                     patch's samples). drv (x1..x16) still drives the shaper
 *                     into saturation, which then tops out at full scale.
 * COL_PSP_GAIN/EXP    psp's curve (S095 P3 F7, Q9): k = 5 psp^1.393, so
 *                     k = 1 (P2's maximum: half a table, or 63.5 width
 *                     steps) at psp 40 and 5 at psp 127; offsets past the
 *                     range wrap (wtb) or fold (pwm).
 * COL_SLEW_STEP_MAX   slw 1: the pitch moves 1 octave per ms, as an octave
 *                     step per block (32 / fs / 1 ms) (S095 P3 F3, Q1).
 * COL_SLEW_OCT        slw 1..127 spans log2(10^4) octaves of rate: 1 ms to
 *                     10 s per octave, exponentially; slw 0 is instant.
 * COL_POST_WET_STEPS  the overdrive crossfades in over its first 8 steps
 *                     (S095 P3 F10): drive 0 is the dry signal exactly.
 * COL_P32_ZMAX        the overdrive's tanh: the Alkaline OTA's [3/2] Pade
 *                     z (27 + z^2) / (27 + 9 z^2) (DiodeLadder.c DL_P32*),
 *                     clamped at |z| = 3 where it reaches exactly 1.
 */
#define COL_LOG2_440          8.78135971f
#define COL_CUT_BASE_HZ       20.0f
#define COL_CUT_ST_MAX        115.7f
#define COL_PI_OVER_FS        (3.14159265f / REAL_FS)
#define COL_SLEW              0.28f
#define COL_K_SPAN            1.95f
#define COL_FENV_ST           72.0f
#define COL_DRIVE_OCT         4.0f
#define COL_CLIP              1.5f
#define COL_CUBIC             (4.0f / 27.0f)
#define COL_OUT_SCALE         32767.0f
#define COL_EG_ATTACK_AIM     1.2f
#define COL_EG_SETTLE         0.01f
#define COL_EG_IDLE_LEVEL     1.0e-4f
#define COL_DETUNE_MAX_OCT    0.208333333f
#define COL_SPREAD_GAIN       5.0f
#define COL_MOT_DEPTH_MAX     3.0f
#define COL_MOT_RATE_MIN_HZ   0.05f
#define COL_MOT_RATE_OCT      5.32192809f
#define COL_MOT_PHASE_PER_HZ  ((float)OUTPUT_DMA_SIZE * 4294967296.0f / REAL_FS)
#define COL_INC_PER_HZ        (4294967296.0f / REAL_FS)
#define COL_F_MAX             (0.45f * REAL_FS)
#define COL_W_LOG2_LO         6.32192809f
#define COL_W_OCTAVES         3.0f
#if COL_OSC_COUNT == 12u
#define COL_INV_SQRT_COUNT    0.288675135f    /* 1 / sqrt(12) */
#elif COL_OSC_COUNT == 10u
#define COL_INV_SQRT_COUNT    0.316227766f    /* 1 / sqrt(10) */
#else
#error "COL_OSC_COUNT: add its 1 / sqrt(N) here"
#endif
#define COL_NORM_WAV          (COL_INV_SQRT_COUNT / 32768.0f)
#define COL_NORM_PWM          (0.408248290f / 32768.0f)   /* 1 / sqrt(6) */
_Static_assert(COL_PWM_COUNT == 6u, "COL_NORM_PWM is 1 / sqrt(6)");
#define COL_HEADROOM          0.5f
#define COL_PSP_GAIN          5.0f
#define COL_PSP_EXP           1.39308169f
#define COL_SLEW_STEP_MAX     (((float)OUTPUT_DMA_SIZE / REAL_FS) / 0.001f)
#define COL_SLEW_OCT          13.2877124f
#define COL_POST_WET_STEPS    8u
#define COL_P32_ZMAX          3.0f

/* x clamped to 0..1 (descriptor floats are byte / 127, but an LFO or
 * velocity overlay may push them past either end). */
/*
 * The voice's draw generator (S095 P2, device fix).
 *
 * What:       col_rngSeed() mixes one hardware RNG read with a state that
 *             differs per call (the caller's free-running phase, or the voice's
 *             address at init) into a non-zero 32-bit seed; col_rngNext() is
 *             xorshift32 (x ^= x << 13; x ^= x >> 17; x ^= x << 5), one value
 *             per oscillator.
 * Why:        GetRngValue() returns RNG_DR as it is, without waiting for a new
 *             value (random.c: one every ~833 ns). Back-to-back calls in a
 *             loop return the same value many times, so the twelve per-
 *             oscillator draws of a trigger (frame and width spread) and the
 *             start phases at init were nearly identical: no spread, a
 *             phase-aligned stack. One hardware read seeds the trigger; the
 *             generator makes the twelve values distinct. These are control
 *             draws, not audio noise (the S073 rule that audio-rate noise uses
 *             the hardware RNG is untouched).
 * Inputs:     salt (any 32-bit value that differs between calls).
 * Accessors:  Colloid_initVoice(), Colloid_triggerVoice() (main loop).
 * Affiliates: tools/dsp_test modules/colloid.c law CO-RND.
 */
static inline uint32_t col_rngSeed(const uint32_t salt)
{
	const uint32_t s = (((uint32_t)(uint16_t)GetRngValue() << 16) | 0x5A5Au) ^ salt
	                 ^ 0x9E3779B9u;

	return s ? s : 0x9E3779B9u;   /* xorshift's one fixed point is 0 */
}

static inline uint32_t col_rngNext(uint32_t *s)
{
	uint32_t x = *s;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*s = x;
	return x;
}

static inline float col_clamp01(const float x)
{
	return fminf(fmaxf(x, 0.0f), 1.0f);
}

/*
 * tan(x) by the [3/2] Pade form x (15 - x^2) / (15 - 6 x^2) (S095 P2).
 *
 * What:       the same rational form as ResonantFilter.c fastTan(), one VDIV.
 * Why:        the prewarp g = tan(pi f / fs) per filter per block. fastTan()
 *             is an ITCM function with no header declaration; a local copy
 *             keeps the Colloid's control path in its own file.
 * Range:      x <= pi x 15,963 / fs = 1.137 (COL_CUT_ST_MAX): 0.4 % low there;
 *             the pole (x^2 = 2.5) is never reached.
 */
static inline float col_tanf(const float x)
{
	const float x2 = x * x;

	return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

/*
 * The drive: x' = clamp(x, +-1.5), y = x' - (4/27) x'^3 (draft §3).
 * A cubic soft clip with no division; 1.0 at the clamp. S095 P3 F6 (Q4)
 * also uses it as the stereo positions' edge: slope 1 at the centre, +-1
 * from |x| >= 1.5.
 */
static inline float col_drive(const float x)
{
	const float c = fminf(fmaxf(x, -COL_CLIP), COL_CLIP);

	return c - COL_CUBIC * c * c * c;
}

/*
 * The psp curve (S095 P3 F7, Q9).
 *
 * What:       k = 5 psp^1.393 (psp 0..1; 0 at 0): 1 at psp 40/127 (P2's
 *             maximum), 5 at psp 127.
 * Why:        the user's notes: "x5 at max at least", answered with the curve:
 *             the useful range keeps its resolution below P2's maximum and
 *             the top of the knob reaches five times further.
 * Accessors:  col_wtbTrigger(), col_pwmScale() (main loop and control rate).
 *             noinline: one copy of the log2/exp2 polynomials, not one per
 *             caller (flash; it runs at most once per block).
 */
static __attribute__((noinline)) float col_pspK(const float psp)
{
	const float x = col_clamp01(psp);

	return (x > 0.0f) ? COL_PSP_GAIN * alk_exp2f(COL_PSP_EXP * alk_log2f(x)) : 0.0f;
}

/*
 * The post-filter overdrive (S095 P3 F10; the Alkaline's law).
 *
 * What:       z = clamp(y x g, +-3), sat = z (27 + z^2) / (27 + 9 z^2) (the
 *             OTA tanh of DiodeLadder.c), then y + wet (mk x sat - y): the
 *             gain g = alkOvdGain[drv] (x1..x32), the make-up mk =
 *             alkOvdMakeup[drv] (2^(-2.5 drv / 127)), wet 0 at drive 0 rising
 *             to 1 at drive 8.
 * Why:        "the same post-filter drive as for Alkaline": the same tables
 *             and saturator (Q11). wet 0 returns y exactly (0 x a finite value
 *             is 0), so drive 0 is today's output to the bit (law CO-POST).
 *             One VDIV per call, every sample whatever the drive (constant
 *             cost).
 * Inputs:     y (a filter output, about +-1), g, mk, wet (per block).
 * Accessors:  col_renderPar() (left and right), col_renderSer() (once).
 */
static inline float col_post(const float y, const float g, const float mk, const float wet)
{
	const float z = fminf(fmaxf(y * g, -COL_P32_ZMAX), COL_P32_ZMAX);
	const float z2 = z * z;
	const float sat = z * (27.0f + z2) / (27.0f + 9.0f * z2);

	return y + wet * (mk * sat - y);
}

/*
 * The octave row of a stock table for frequency f (S095 P2).
 *
 * What:       0..10: the number of octave edges (16.3516 Hz x 2^k) at or
 *             below f, from f's float bits: the exponent minus 4, plus 1 when
 *             the mantissa reaches the edges' mantissa 0x02D013, clamped.
 * Why:        osc_setTableFreq()'s law (Oscillator.c, S093 P3) for each
 *             oscillator's own frequency, so an interval set spanning two
 *             octaves stays band-limited on every member (draft §2.2). Ten
 *             compares become a few integer operations.
 * Inputs:     f > 0 (the caller's frequencies are positive).
 */
static inline uint32_t col_octave(const float f)
{
	union { float f; int32_t i; } v = { f };
	int32_t k = ((v.i >> 23) & 0xFF) - (127 + 4)
	          + (int32_t)((v.i & 0x007FFFFF) >= 0x0002D013);

	return (uint32_t)((k < 0) ? 0 : ((k > 10) ? 10 : k));
}

/*
 * Step one envelope by one block (S095 P2, draft §4.1).
 *
 * What:       attack: level += K[a] (1.2 - level), at 1.0 -> decay;
 *             decay: level += K[d] (s - level), within 0.01 of s -> sustain;
 *             sustain: keeps following s (K[d]); when the effective sustain
 *             switch is off -> release;
 *             release: level += K[r] (0 - level), below 1e-4 -> idle at 0.
 *             K = colEgK[byte & 127].
 * Why:        a lightweight ADSR: one multiply-add per block (C-D8 the switch,
 *             C-D19 its effective value). With susOn 0 the envelope runs
 *             attack, decay, release; with susOn 1 it holds at s. Switching
 *             susOn off while held (sustain stage) starts the release on that
 *             block; a decay still under way releases once it settles.
 * Inputs:     e, the time bytes a, d, r, the level s (0..1 float), susOn.
 * Outputs:    e->level, e->stage.
 * Accessors:  Colloid_calcVoiceAsync() (both envelopes, with the same susOn).
 */
static void col_egStep(ColEg *e, const uint8_t a, const uint8_t d, const float s,
                       const uint8_t r, const uint8_t susOn)
{
	const float sl = col_clamp01(s);

	switch (e->stage) {
	case COL_EG_ATTACK:
		e->level += colEgK[a & 127u] * (COL_EG_ATTACK_AIM - e->level);
		if (e->level >= 1.0f) {
			e->level = 1.0f;
			e->stage = COL_EG_DECAY;
		}
		break;
	case COL_EG_DECAY:
		e->level += colEgK[d & 127u] * (sl - e->level);
		if (fabsf(e->level - sl) <= COL_EG_SETTLE)
			e->stage = COL_EG_SUSTAIN;
		break;
	case COL_EG_SUSTAIN:
		/* the level keeps following slv, so a sustain-level change is smooth */
		e->level += colEgK[d & 127u] * (sl - e->level);
		if (!susOn)
			e->stage = COL_EG_RELEASE;
		break;
	case COL_EG_RELEASE:
		e->level -= colEgK[r & 127u] * e->level;
		if (e->level < COL_EG_IDLE_LEVEL) {
			e->level = 0.0f;
			e->stage = COL_EG_IDLE;
		}
		break;
	default:
		break;
	}
}

/*
 * One filter's output mix for this block (S095 P2, draft §3).
 *
 * What:       mH, mB, mL from the type and this block's k: LP (0, 0, 1),
 *             BP (0, 1, 0), HP (1, -k, -1), notch (1, -k, 0), peak (1, -k, -2);
 *             any other byte is LP.
 * Why:        every type is the same loop with other mix constants (constant
 *             cost). The mix steps once per block; k is slewed, so its steps
 *             are small.
 */
static void col_svfMix(ColSvf *f, const uint8_t type)
{
	switch (type) {
	case COL_FT_BP:    f->mH = 0.0f; f->mB = 1.0f;  f->mL = 0.0f;  break;
	case COL_FT_HP:    f->mH = 1.0f; f->mB = -f->k; f->mL = -1.0f; break;
	case COL_FT_NOTCH: f->mH = 1.0f; f->mB = -f->k; f->mL = 0.0f;  break;
	case COL_FT_PEAK:  f->mH = 1.0f; f->mB = -f->k; f->mL = -2.0f; break;
	default:           f->mH = 0.0f; f->mB = 0.0f;  f->mL = 1.0f;  break;
	}
}

/*
 * One filter's coefficients for this block (S095 P2, draft §3, C-D9).
 *
 * What:       1. the ramp of the previous block ended on its targets, so
 *                this block starts there: a = t;
 *             2. the cutoff target in semitones above 20 Hz,
 *                st = clamp(cut x 115.7 + envSt, 0, 115.7), and the damping
 *                target kT = 2 - 1.95 res; both slewed by 0.28 per block;
 *             3. hz = 20 x 2^(cutSt / 12); g = tan(pi hz / fs) (one VDIV);
 *                t1 = 1 / (1 + g (g + k)) (one VDIV), t2 = g t1, t3 = g t2;
 *             4. da = (t - a) / 32: the output loop adds da once per sample,
 *                so the coefficients reach t exactly at the block's end;
 *             5. the output mix (col_svfMix()).
 * Why:        smooth sweeps (the brief): the TPT filter is stable under the
 *             per-sample ramp, and the slew removes block steps from knob,
 *             LFO and envelope moves. 2 VDIV per filter per block, none per
 *             sample.
 * Inputs:     f, cut and res (0..1 floats), type (COL_FT_*), envSt (the
 *             filter envelope's offset in semitones).
 * Outputs:    f's a, t, da, cutSt, k and mix.
 * Accessors:  Colloid_calcVoiceAsync(), col_svfInit().
 */
static void col_svfControl(ColSvf *f, const float cut, const float res,
                           const uint8_t type, const float envSt)
{
	const float st = fminf(fmaxf(col_clamp01(cut) * COL_CUT_ST_MAX + envSt, 0.0f),
	                       COL_CUT_ST_MAX);
	const float kT = 2.0f - COL_K_SPAN * col_clamp01(res);
	float g;

	f->a1 = f->t1;
	f->a2 = f->t2;
	f->a3 = f->t3;
	f->cutSt += COL_SLEW * (st - f->cutSt);
	f->k += COL_SLEW * (kT - f->k);
	g = col_tanf(COL_CUT_BASE_HZ * alk_exp2f(f->cutSt * (1.0f / 12.0f)) * COL_PI_OVER_FS);
	f->t1 = 1.0f / (1.0f + g * (g + f->k));
	f->t2 = g * f->t1;
	f->t3 = g * f->t2;
	f->da1 = (f->t1 - f->a1) * (1.0f / (float)OUTPUT_DMA_SIZE);
	f->da2 = (f->t2 - f->a2) * (1.0f / (float)OUTPUT_DMA_SIZE);
	f->da3 = (f->t3 - f->a3) * (1.0f / (float)OUTPUT_DMA_SIZE);
	col_svfMix(f, type);
}

/*
 * A filter at rest on its knob settings (S095 P2).
 *
 * What:       clears the integrators, puts the slewed cutoff and damping on
 *             their targets, computes the coefficients, and makes the first
 *             block start on them (a = t, da = 0).
 * Accessors:  Colloid_initVoice().
 */
static void col_svfInit(ColSvf *f, const float cut, const float res, const uint8_t type)
{
	f->ic1 = 0.0f;
	f->ic2 = 0.0f;
	f->cutSt = col_clamp01(cut) * COL_CUT_ST_MAX;
	f->k = 2.0f - COL_K_SPAN * col_clamp01(res);
	col_svfControl(f, cut, res, type, 0.0f);
	f->a1 = f->t1;
	f->a2 = f->t2;
	f->a3 = f->t3;
	f->da1 = 0.0f;
	f->da2 = 0.0f;
	f->da3 = 0.0f;
}

/*
 * Bind the wavetable (S095 P2; the Alkaline's alk_wtbBind() rule).
 *
 * What:       records the key (COL_BIND_WTB, the table = osc.waveform clamped
 *             to the last installed table, the catalogue generation), then
 *             sampleCacheSize = the table's frame count and sampleCacheData =
 *             its first 1,024-point row (frame f at + f x 1,024). With no
 *             table installed: one frame and NULL (the async then sets the
 *             input gain to 0: silence, with the filters still running).
 * Why:        the Colloid binds its own tables (draft §2.3): osc_userBind() is
 *             never called on a Colloid oscillator. It runs only when the key
 *             changes; noinline keeps its registers out of the async's path.
 * Accessors:  Colloid_calcVoiceAsync(), col_wtbTrigger().
 * Affiliates: SampleMemory.h sampleMemory_wavetableEntry(),
 *             sampleMemory_wavetableRows().
 */
static __attribute__((noinline)) void col_wtbBind(ColloidVoice *voice)
{
	OscInfo *const osc = &voice->osc;
	const uint8_t count = sampleMemory_getNumWavetables();
	const uint8_t index = (osc->waveform < count) ? osc->waveform
	                    : (uint8_t)(count ? count - 1u : 0u);
	const wavetable_catalogue_entry_t *e = sampleMemory_wavetableEntry(index);

	osc->sampleCacheLooped = COL_BIND_WTB;
	osc->sampleCacheIndex = index;
	osc->sampleCacheGeneration = sampleMemory_getGeneration();
	osc->sampleCacheSize = e ? e->frame_count : 1u;
	osc->sampleCacheData = e ? sampleMemory_wavetableRows(e) : 0;
}

/* Whether the bound table no longer matches (alk_wtbKeyChanged()'s rule). */
static inline uint8_t col_wtbKeyChanged(const OscInfo *osc)
{
	return (uint8_t)(osc->sampleCacheLooped != COL_BIND_WTB ||
	                 osc->waveform != osc->sampleCacheIndex ||
	                 sampleMemory_getGeneration() != osc->sampleCacheGeneration);
}

/*
 * Latch every oscillator's frame at a trigger (S095 P2, C-D4/C-D5; S095 P3).
 *
 * What:       binds first when the key changed; then, with F frames,
 *             centre = round(pos x (F - 1) / 127), and for each oscillator
 *             frame = wrap(centre + round(r x k x F / 2)), r = rnd / 32768
 *             (-1..+1), k = col_pspK(psp) (S095 P3 F7: 0..5). psp 0 puts every
 *             oscillator on the centre frame; from psp 40 the offsets pass
 *             half a table and wrap (a modulo: up to 2.5 tables either side).
 *             Every trigger latches (S095 P3 F3: no legato).
 * Why:        "a spread that picks a random index further from the centre as
 *             the spread rises" (the brief), within the selected table (C-D5);
 *             latched, so a held note never steps between frames (C-D4).
 * Accessors:  Colloid_triggerVoice() (main loop).
 */
static void col_wtbTrigger(ColloidVoice *voice)
{
	OscInfo *const osc = &voice->osc;
	uint32_t frames, centre, i;
	float half;

	if (col_wtbKeyChanged(osc))
		col_wtbBind(voice);
	frames = osc->sampleCacheSize;
	centre = ((uint32_t)(osc->position & 127u) * (frames - 1u) + 63u) / 127u;
	half = col_pspK(voice->posSpread) * (float)frames * (0.5f / 32768.0f);
	/* main loop only: kept rolled (flash, not cycles) */
#pragma GCC unroll 1
	for (i = 0u; i < COL_OSC_MAX; i++) {
		const float off = (float)voice->rnd[i] * half;
		int32_t fr = ((int32_t)centre + (int32_t)(off + ((off >= 0.0f) ? 0.5f : -0.5f)))
		           % (int32_t)frames;

		if (fr < 0)
			fr += (int32_t)frames;
		voice->frame[i] = (uint8_t)fr;
	}
}

/*
 * One oscillator's pwm width target (S095 P2, draft §2.4, U.4; S095 P3 F7).
 *
 * What:       pos + rnd x scale in pos space, scale = k x 63.5 / 32768 (so
 *             the offset is r x k x 63.5, r = rnd / 32768 in -1..+1, k =
 *             col_pspK(psp) 0..5), folded back at 0 and at 127 as a triangle
 *             of period 254 (S095 P3: |offset| reaches 317, so a modulo
 *             replaces P2's single fold).
 * Why:        every oscillator has its own width around the centre pos; the
 *             width law itself is the shipped oscPwmDRec[] (pos 0 = 50 %,
 *             127 = 0.39 %). pos and scale are passed in, computed once per
 *             call site: read through voice-> inside the loop they were
 *             reloaded for every oscillator (the loop stores into the voice).
 * Inputs:     pos (osc.position & 127), rnd (the oscillator's draw), scale.
 * Accessors:  col_oscControl(), Colloid_triggerVoice(), Colloid_setOscType().
 */
static inline uint8_t col_pwmTarget(const int32_t pos, const int16_t rnd, const float scale)
{
	int32_t v = (pos + (int32_t)((float)rnd * scale)) % 254;

	if (v < 0)
		v += 254;
	if (v > 127)
		v = 254 - v;
	return (uint8_t)v;
}

/* The pwm target's scale: k x 63.5 / 32768, k = col_pspK(psp) (col_pwmTarget()). */
static inline float col_pwmScale(const ColloidVoice *voice)
{
	return col_pspK(voice->posSpread) * (63.5f / 32768.0f);
}

/*
 * Every oscillator's pitch, increment, row, width and stereo position for
 * this block (S095 P2, draft §2.2, §2.6; S095 P3 F2-F6).
 *
 * What:       for each of the `count` oscillators o, group g = o mod 4:
 *             - the target pitch, log2 Hz: base + iv[g] + c_o x dOct, where
 *               base is the note/coa/fin pitch, iv[0] = 0 and iv[1..3] =
 *               (in1..in3 byte - 63) / 12 (F4), c_o = colDetC[o] and dOct =
 *               dtn^2 x 250 / 1200 (F5);
 *             - the slew (F3, Q1): pitchCur moves toward the target by at
 *               most `step` octaves this block (a constant rate), or lands
 *               on it when within a step or when `jump` is set (slw 0, or
 *               the voice's first note);
 *             - f = 2^pitchCur x osc.modNodeValue (the LFO / velocity pitch
 *               multiplier after the slew, Q2), clamped to 0.45 fs;
 *               inc = f x 2^32 / fs;
 *             - row: wtb, the latched frame of the bound table (the last
 *               frame if the table shrank); Sin, the 1,024-point sine; pwm,
 *               by form: Sin the sine, Tri the triTable octave row lifted by
 *               oscPwmTriLift[width], Saw/Rec the sawTable octave row;
 *               Saw/Rec/Tri under wav, the octave row of that table;
 *             - pwm: the width index slews toward col_pwmTarget() by at most
 *               OSC_PWM_SLEW_STEPS (2) per block; Saw/Rec take pwD =
 *               oscPwmDSaw / oscPwmDRec[index] and add their level
 *               oscPwmLevel*Q14[index] to the stack's sum;
 *             - the position: the motion phase advances by rate x colMotK[o];
 *               m = 4 t (1 - |t|) (a parabolic sine, t = phase / 2^31);
 *               w = clamp((pitchCur - log2 80) / 3, 0, 1);
 *               p = w x edge(5 spr x colHome[o] + 3 sqrt(mot) x m) (F6, the
 *               cubic edge col_drive(), Q4); the pair's two Q15 positions
 *               packed into posPair[o / 2].
 * Why:        all per block: the per-sample loops only read rows and add.
 *             Every oscillator slews on its own (a bigger jump takes longer),
 *             so interval and note changes arrive staggered (the user's
 *             "lazily get to their destination pitch at the slew rate").
 *             Lower notes sit nearer the centre and move less (w, C-D14).
 * Inputs:     voice (parameters, osc, the bind, rnd, frame, pitchCur), count
 *             (COL_OSC_COUNT or COL_PWM_COUNT), base (log2 Hz), step (octaves
 *             per block), jump, isPwm, isWtb.
 * Outputs:    pitchCur[], inc[], row[], pwPos[], pwD[], motPhase[],
 *             posPair[]. Returns the pwm level factor: under Saw/Rec the mean
 *             of oscPwmLevel*Q14[width] / 16,384, else 1.
 * Cost:       one alk_exp2f() per oscillator per block (it replaces P2's
 *             colRatio[] lookup and first-order detune); no per-sample work.
 *             noinline and the loop kept rolled: -Ofast otherwise inlined and
 *             unrolled it into the async (+1.9 KB of flash for a control-rate
 *             loop, measured S095 P3).
 * Accessors:  Colloid_calcVoiceAsync().
 */
static __attribute__((noinline)) float col_oscControl(ColloidVoice *voice, const uint32_t count, const float base,
                            const float step, const uint8_t jump,
                            const uint8_t isPwm, const uint8_t isWtb)
{
	const OscInfo *const osc = &voice->osc;
	const float dtn = col_clamp01(voice->detune);
	const float dOct = dtn * dtn * COL_DETUNE_MAX_OCT;
	const float spr = col_clamp01(voice->spread) * COL_SPREAD_GAIN;
	const float mot = col_clamp01(voice->motion);
	/* mot 0 is still: no depth, no movement */
	const float depth = COL_MOT_DEPTH_MAX * __builtin_sqrtf(mot);
	const float motStep = (mot > 0.0f)
	    ? COL_MOT_RATE_MIN_HZ * alk_exp2f(COL_MOT_RATE_OCT * mot) * COL_MOT_PHASE_PER_HZ
	    : 0.0f;
	const float modMul = osc->modNodeValue;
	const uint8_t wave = osc->waveform;
	/* the pwm form (the platform's order; past Rec plays Rec) */
	const uint8_t form = (wave < COL_PWM_WAVE_COUNT) ? wave : COL_PWM_REC;
	/* under wav the list; under pwm Saw/Rec read the saw rows, Tri the tri rows */
	const int16_t (*const stock)[1024] = isPwm
	    ? ((form == COL_PWM_TRI) ? (const int16_t (*)[1024])triTable
	                             : (const int16_t (*)[1024])sawTable)
	    : ((wave == COL_WAVE_REC) ? (const int16_t (*)[1024])recTable
	    : ((wave == COL_WAVE_TRI) ? (const int16_t (*)[1024])triTable
	                              : (const int16_t (*)[1024])sawTable));
	const uint8_t isSin = (uint8_t)(isPwm ? (form == COL_PWM_SIN)
	                                      : (!isWtb && wave == COL_WAVE_SIN));
	const uint8_t isPwmD = (uint8_t)(isPwm && form >= COL_PWM_SAW);
	const uint16_t *const dTab = (form == COL_PWM_SAW) ? oscPwmDSaw : oscPwmDRec;
	const int16_t *const lvTab = (form == COL_PWM_SAW) ? oscPwmLevelSawQ14 : oscPwmLevelRecQ14;
	const int16_t *const wtbBase = osc->sampleCacheData;
	const uint32_t frames = osc->sampleCacheSize ? osc->sampleCacheSize : 1u;
	/* pwm: the centre width and the spread scale, once per block */
	const int32_t pwPos0 = (int32_t)(osc->position & 127u);
	const float pwScale = col_pwmScale(voice);
	float iv[COL_GROUPS];
	int32_t qEven = 0;
	int32_t levelSum = 0;
	uint32_t i;

	/* the groups' offsets in octaves: the root, then in1..in3 (63 = 0) */
	iv[0] = 0.0f;
	for (i = 0u; i < COL_INTERVALS; i++)
		iv[i + 1u] = ((float)voice->interval[i] - 63.0f) * (1.0f / 12.0f);

#pragma GCC unroll 1
	for (i = 0u; i < count; i++) {
		const float target = base + iv[COL_GROUP(i)] + colDetC[i] * dOct;
		const float d = target - voice->pitchCur[i];
		float f;

		/* the slew: a constant rate in log pitch; land within one step */
		if (jump || fabsf(d) <= step)
			voice->pitchCur[i] = target;
		else
			voice->pitchCur[i] += (d > 0.0f) ? step : -step;
		f = fminf(alk_exp2f(voice->pitchCur[i]) * modMul, COL_F_MAX);
		voice->inc[i] = (uint32_t)(f * COL_INC_PER_HZ);
		if (isPwm) {
			int32_t dw = (int32_t)col_pwmTarget(pwPos0, voice->rnd[i], pwScale) - (int32_t)voice->pwPos[i];

			if (dw > OSC_PWM_SLEW_STEPS)
				dw = OSC_PWM_SLEW_STEPS;
			else if (dw < -OSC_PWM_SLEW_STEPS)
				dw = -OSC_PWM_SLEW_STEPS;
			voice->pwPos[i] = (uint8_t)((int32_t)voice->pwPos[i] + dw);
			if (isPwmD) {
				voice->pwD[i] = dTab[voice->pwPos[i] & 127u];
				levelSum += lvTab[voice->pwPos[i] & 127u];
			}
		}
		if (isWtb) {
			const uint32_t fr = (voice->frame[i] < frames) ? voice->frame[i] : (frames - 1u);

			voice->row[i] = wtbBase ? wtbBase + fr * WAVETABLE_ROW_POINTS : sawTable[0];
		} else if (isSin) {
			voice->row[i] = sine_table;
		} else if (isPwm && form == COL_PWM_TRI) {
			/* the octave row lifted for the compressed half-cycle (osc_pwmPlan()) */
			uint32_t r = col_octave(f) + oscPwmTriLift[voice->pwPos[i] & 127u];

			voice->row[i] = stock[(r > 10u) ? 10u : r];
		} else {
			voice->row[i] = stock[col_octave(f)];
		}
		voice->motPhase[i] += (uint32_t)(motStep * colMotK[i]);
		{
			const float t = (float)(int32_t)voice->motPhase[i] * (1.0f / 2147483648.0f);
			const float m = 4.0f * t * (1.0f - fabsf(t));
			const float w = col_clamp01((voice->pitchCur[i] - COL_W_LOG2_LO)
			                            * (1.0f / COL_W_OCTAVES));
			const float p = col_drive(spr * colHome[i] + depth * m) * w;
			const int32_t q = (int32_t)(p * 32767.0f);

			if (i & 1u)
				voice->posPair[i >> 1] = ((uint32_t)qEven & 0xFFFFu) | ((uint32_t)q << 16);
			else
				qEven = q;
		}
	}
	return isPwmD ? (float)levelSum * (1.0f / 16384.0f) / (float)count : 1.0f;
}

void Colloid_initVoice(ColloidVoice *voice)
{
	uint32_t i;
	uint32_t rng;

	if (!voice)
		return;
	/* the pitch anchor: typ wav, Saw, coa 48 (C3 at the default note 63),
	 * fin at its centre 63 (init.col osc1_type 0, osc1_wave 0) */
	voice->osc.oscType = OSC_TYPE_WAV;
	voice->osc.waveform = COL_WAVE_SAW;
	voice->osc.position = 0u;
	voice->osc.freq = 130.813f;
	voice->osc.pitchMod = 1.0f;
	voice->osc.modNodeValue = 1.0f;
	voice->osc.midiFreq = (uint16_t)((48u << 8) | 63u);
	voice->osc.baseNote = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->osc.sampleCacheLooped = COL_BIND_NONE;
	voice->osc.sampleCacheSize = 1u;
	voice->osc.sampleCacheData = 0;
	lfo_init(&voice->lfo);

	/* the bytes of SD_CARD/Instrument/Colloid/init.col, as the descriptor
	 * writers store them (TYPE_FLT byte / 127, TYPE_UINT8 the byte), so a
	 * voice before its first apply and a freshly loaded init.col match */
	voice->posSpread = 0.0f;
	voice->sustainLevel = 90.0f / 127.0f;
	voice->fSustain = 64.0f / 127.0f;
	voice->fAmount = 63.0f / 127.0f;
	/* S095 P3: the x5 ranges' gentle start: dtn +-6 cents at the outer pair,
	 * spr 0.79 of the field, mot depth 0.84 at 0.07 Hz */
	voice->detune = 20.0f / 127.0f;
	voice->spread = 20.0f / 127.0f;
	voice->motion = 10.0f / 127.0f;
	voice->cut = 90.0f / 127.0f;
	voice->res = 20.0f / 127.0f;
	voice->cutOfs = 63.0f / 127.0f;    /* the offsets at 0 (S095 P3 F9) */
	voice->resOfs = 63.0f / 127.0f;
	voice->envOfs = 63.0f / 127.0f;
	voice->driveOfs = 63.0f / 127.0f;
	voice->drive = 0.0f;
	voice->vol = 100.0f / 127.0f;
	voice->attack = 40u;
	voice->decay = 70u;
	voice->release = 70u;
	voice->fAttack = 0u;
	voice->fDecay = 70u;
	voice->fRelease = 70u;
	voice->slew = 0u;                  /* instant (S095 P3 F3) */
	voice->pan = 63u;
	voice->sustain = 1u;
	for (i = 0u; i < COL_INTERVALS; i++)
		voice->interval[i] = 63u;      /* every group on the root (F4) */
	voice->filtType = COL_FT_LP;
	voice->route = COL_RTE_STR;
	voice->vcaDrive = 0u;              /* no overdrive, the tone open (F10) */
	voice->tone = 127u;

	/* free-running from a random start: the stack never starts phase-aligned
	 * (one hardware RNG read, salted with the voice's address so the six
	 * slots differ, then col_rngNext() per value; main loop or boot, never
	 * audio) */
	rng = col_rngSeed((uint32_t)(uintptr_t)voice);
#pragma GCC unroll 1
	for (i = 0u; i < COL_OSC_MAX; i++) {
		voice->phase[i] = col_rngNext(&rng);
		voice->motPhase[i] = col_rngNext(&rng);
		voice->pitchCur[i] = 0.0f;   /* set on the first block (pitchJump) */
		voice->inc[i] = 0u;
		voice->row[i] = sawTable[0];
		voice->rnd[i] = 0;
		voice->frame[i] = 0u;
		voice->pwPos[i] = 0u;
		voice->pwD[i] = oscPwmDRec[0];
	}
	for (i = 0u; i < COL_PAIR_MAX; i++)
		voice->posPair[i] = 0u;
	memset(voice->busM, 0, sizeof(voice->busM));
	memset(voice->busS, 0, sizeof(voice->busS));
	memset(voice->right, 0, sizeof(voice->right));
	voice->ampEg.level = 0.0f;
	voice->ampEg.stage = COL_EG_IDLE;
	voice->fltEg.level = 0.0f;
	voice->fltEg.stage = COL_EG_IDLE;
	/* both filters on the knobs (the offsets are 0) */
	col_svfInit(&voice->svfA, voice->cut, voice->res, voice->filtType);
	col_svfInit(&voice->svfB, voice->cut, voice->res, voice->filtType);
	voice->note = MIDI_DEFAULT_TRIGGER_NOTE;
	/* the pitch lands on its targets until the first note has sounded one
	 * block (S095 P3 F3): a load never sweeps in from 0 Hz */
	voice->pitchJump = 1u;
	voice->ampLast = 0.0f;
	voice->inGain = COL_NORM_WAV;
	voice->inGainR = COL_NORM_WAV;
	voice->postGain = 1.0f;
	voice->postMakeup = 1.0f;
	voice->postWet = 0.0f;
	voice->toneC = 0.0f;
	voice->toneZL = 0.0f;
	voice->toneZR = 0.0f;
}

void Colloid_triggerVoice(ColloidVoice *voice, const uint8_t source_slot,
                          const uint8_t velocity, const uint8_t note)
{
	/* the velocity as the modulation system's 0..1 value (Drum: vol/127.f) */
	const float vel = (float)velocity * (1.0f / 127.0f);
	uint32_t i;
	uint32_t rng;

	/* --- platform part (as Drum/Alkaline): LFO and velocity, per slot --- */
	lfo_retrigger(source_slot);
	modNode_updateValue(&velocityModulators[source_slot], vel);
	instrumentManager_applyVelocityModulationTarget(source_slot, vel);

	/*
	 * S095 P3 F3: no legato and no glide mode. The note becomes every
	 * oscillator's new target and the slew carries them there (slw); the
	 * voice's first note after init lands at once (pitchJump 1 -> 2: the next
	 * block jumps, then the flag clears).
	 */
	voice->note = note;
	if (voice->pitchJump)
		voice->pitchJump = 2u;
	/* both attacks restart from their current levels */
	voice->ampEg.stage = COL_EG_ATTACK;
	voice->fltEg.stage = COL_EG_ATTACK;
	/* one hardware read per trigger, salted with a free-running phase (it
	 * differs at every trigger), then one generator value per oscillator:
	 * its top 16 bits are the Q15 draw (-1..+1) */
	rng = col_rngSeed(voice->phase[0] ^ (voice->phase[1] << 7));
#pragma GCC unroll 1
	for (i = 0u; i < COL_OSC_MAX; i++)
		voice->rnd[i] = (int16_t)(col_rngNext(&rng) >> 16);
	if (voice->osc.oscType == OSC_TYPE_WTB) {
		col_wtbTrigger(voice);
	} else if (voice->osc.oscType == OSC_TYPE_PWM) {
		/* the widths start on their targets (no slew across a new note) */
		const int32_t pos = (int32_t)(voice->osc.position & 127u);
		const float scale = col_pwmScale(voice);

#pragma GCC unroll 1
		for (i = 0u; i < COL_OSC_MAX; i++)
			voice->pwPos[i] = col_pwmTarget(pos, voice->rnd[i], scale);
	}
}

void Colloid_calcVoiceAsync(ColloidVoice *voice)
{
	OscInfo *const osc = &voice->osc;
	const uint8_t isPwm = (uint8_t)(osc->oscType == OSC_TYPE_PWM);
	const uint8_t isWtb = (uint8_t)(osc->oscType == OSC_TYPE_WTB);
	const uint32_t count = isPwm ? COL_PWM_COUNT : COL_OSC_COUNT;
	/*
	 * The effective sustain switch (C-D8, C-D19).
	 *
	 * What:       sus on and the transport running. Read once per block.
	 * Why:        nothing tells an engine that a note has ended. With the
	 *             transport stopped, a pad, a MIDI note or the preview plays
	 *             attack, decay, release; a transport stop releases a held
	 *             note on the next block (the sustain stage sees 0), with no
	 *             edge flag and no ISR-to-engine call.
	 */
	const uint8_t susOn = (uint8_t)(voice->sustain != 0u && seq_isRunning() != 0u);
	float base, step, level, famL, famR, cutR, resR, driveR, norm;
	uint8_t typeB;

	/* envelopes: the amp ramp starts on the previous block's end */
	voice->ampLast = voice->ampEg.level;
	col_egStep(&voice->ampEg, voice->attack, voice->decay, voice->sustainLevel,
	           voice->release, susOn);
	col_egStep(&voice->fltEg, voice->fAttack, voice->fDecay, voice->fSustain,
	           voice->fRelease, susOn);

	/*
	 * Pitch (the Alkaline's formula, S093 P3): log2 of the note plus coarse
	 * minus the default note 63, from 440 Hz at note 69, plus the fine byte's
	 * detune (midiPitch_detune() is 1 + x, |x| <= 0.03: log2 by its second
	 * order). osc.baseNote is kept for the coarse writer's osc_recalcFreq().
	 */
	{
		int16_t n = (int16_t)(osc->midiFreq >> 8) + (int16_t)voice->note
		          - (int16_t)MIDI_DEFAULT_TRIGGER_NOTE;
		const float x = midiPitch_detune((uint8_t)(osc->midiFreq & 0xffu)) - 1.0f;

		n = (n < 0) ? 0 : ((n > 127) ? 127 : n);
		osc->baseNote = voice->note;
		base = COL_LOG2_440 + ((float)n - 69.0f) * (1.0f / 12.0f)
		     + x * (ALK_LOG2E - 0.5f * ALK_LOG2E * x);
	}
	/*
	 * The pitch slew (S095 P3 F3, Q1): slw 0 is instant; slw 1..127 is a
	 * constant rate of 1 octave per ms down to 1 octave per 10 s
	 * (exponential in slw). Applied per oscillator in col_oscControl(); the
	 * LFO / velocity multiplier (osc.modNodeValue) acts after it (Q2).
	 */
	step = (voice->slew == 0u) ? 1.0e9f
	     : COL_SLEW_STEP_MAX * alk_exp2f(-COL_SLEW_OCT * (float)((voice->slew & 127u) - 1u)
	                                     * (1.0f / 126.0f));

	/* wtb: bind on a key change (an install, or a wav change by the menu,
	 * Morph, an LFO or automation) */
	if (isWtb && col_wtbKeyChanged(osc))
		col_wtbBind(voice);
	level = col_oscControl(voice, count, base, step, (uint8_t)(voice->pitchJump == 1u ||
	                                                          voice->pitchJump == 2u),
	                       isPwm, isWtb);
	/* the first note's block has jumped: from here the pitch slews */
	if (voice->pitchJump == 2u)
		voice->pitchJump = 0u;

	/*
	 * The right channel / second stage from the offsets (S095 P3 F9).
	 *
	 * What:       each offset is a PM63 byte stored / 127 (63/127 = 0), added
	 *             in its cell's own units and clamped to the cell's range:
	 *             cutR = cut + fro, resR = res + reo, driveR = drv + dro; the
	 *             envelope amount in fam's byte units, famR = fam + eno; the
	 *             second filter's type is typ under str, else rte's type.
	 * Why:        one filter shown with offsets; at zero offsets the right
	 *             filter equals the left (law CO-RTE).
	 */
	cutR = col_clamp01(voice->cut + voice->cutOfs - 63.0f / 127.0f);
	resR = col_clamp01(voice->res + voice->resOfs - 63.0f / 127.0f);
	driveR = col_clamp01(voice->drive + voice->driveOfs - 63.0f / 127.0f);
	typeB = (voice->route == COL_RTE_STR || voice->route >= COL_RTE_COUNT)
	      ? voice->filtType : COL_RTE_TYPE(voice->route);

	/*
	 * The bus into the drive (C-D12): 1 / (full scale x sqrt(N)) x
	 * COL_HEADROOM (S095 P3: the stack's peaks stay inside the shaper at drv
	 * 0) times the drive gain 2^(4 drv); under pwm Saw/Rec times the stack's
	 * mean level (S095 P3 F2). wtb with no table installed is silenced here (the rows
	 * point at a stock row so the loops still read valid memory). str: the
	 * right channel's own gain from driveR. Mono: inGainR is the gain into
	 * the second filter, 2^(4 driveR) (Q7; 1 at drv 0, dro 0, as P2's
	 * shaper-only stage), folded into the first filter's output mix by the
	 * render loop.
	 */
	norm = COL_HEADROOM * (isPwm ? COL_NORM_PWM * level
	                    : ((isWtb && !osc->sampleCacheData) ? 0.0f : COL_NORM_WAV));
	voice->inGain = norm * alk_exp2f(COL_DRIVE_OCT * col_clamp01(voice->drive));
	voice->inGainR = ((voice->route == COL_RTE_STR || voice->route >= COL_RTE_COUNT) ? norm : 1.0f)
	               * alk_exp2f(COL_DRIVE_OCT * driveR);

	/*
	 * Filters (draft §3, §4.2): fam (63 = none) x the filter envelope x 72
	 * semitones; the left / first filter on fam, the right / second on
	 * fam + eno (S095 P3 F9; was C-D17's one amount on both).
	 */
	famL = fminf(fmaxf((voice->fAmount * 127.0f - 63.0f) * (1.0f / 63.0f), -1.0f), 1.0f);
	famR = fminf(fmaxf((voice->fAmount * 127.0f + voice->envOfs * 127.0f - 126.0f)
	                   * (1.0f / 63.0f), -1.0f), 1.0f);
	col_svfControl(&voice->svfA, voice->cut, voice->res, voice->filtType,
	               famL * COL_FENV_ST * voice->fltEg.level);
	col_svfControl(&voice->svfB, cutR, resR, typeB, famR * COL_FENV_ST * voice->fltEg.level);

	/*
	 * The post-filter overdrive and tone (S095 P3 F10): the Alkaline's tables
	 * by the bytes (AlkalineVoice.c: alkOvdGain/alkOvdMakeup[drv],
	 * alkToneC[ton]); the overdrive crossfades in over its first 8 steps so
	 * drive 0 is exact.
	 */
	{
		const uint32_t oi = (uint32_t)voice->vcaDrive & 127u;

		voice->postGain = alkOvdGain[oi];
		voice->postMakeup = alkOvdMakeup[oi];
		voice->postWet = (oi >= COL_POST_WET_STEPS) ? 1.0f
		               : (float)oi * (1.0f / (float)COL_POST_WET_STEPS);
		voice->toneC = alkToneC[(uint32_t)voice->tone & 127u];
	}
}

/*
 * The stereo (rte str) filter and output loop (S095 P2, draft §3, §5;
 * S095 P3 F9, F10).
 *
 * What:       per sample n:
 *               L = M + 2S, R = M - 2S (the side bus carries s p / 2);
 *               xl = drive(L x inGain), xr = drive(R x inGainR) (the right
 *               channel's drive is drv + dro, S095 P3);
 *               filter A on xl, filter B on xr (the TPT step:
 *                 v3 = x - ic2; v1 = a1 ic1 + a2 v3; v2 = ic2 + a2 ic1 + a3 v3;
 *                 ic1 = 2 v1 - ic1; ic2 = 2 v2 - ic2;
 *                 y = mH x + mB v1 + mL v2);
 *               the overdrive col_post() on each, x amp (amp ramping
 *               linearly from the previous block's envelope level to this
 *               block's, x 32,767); the tone tz = o + c (tz - o) per
 *               channel;
 *               outL[n], right[n] = SSAT16(tz) (S095 P3 F10);
 *               every coefficient += its per-sample step.
 *             The integrator states are written back; the coefficients are
 *             not (the next async starts the ramp on the targets).
 * Why:        the draft's measured f5_par form (72 per sample, no VDIV),
 *             plus the post stage: two VDIVs per sample (one per channel) and
 *             the two tone states (S095 P3, measured on the device).
 *             noinline: one call per block, kept out of the sync block's and
 *             the mixer's register allocation.
 * Accessors:  Colloid_calcVoiceSyncBlock() (rte str).
 */
static __attribute__((noinline)) void col_renderPar(ColloidVoice *voice, int16_t *outL)
{
	ColSvf *const A = &voice->svfA;
	ColSvf *const B = &voice->svfB;
	const int32_t *const busM = voice->busM;
	const int32_t *const busS = voice->busS;
	int16_t *const outR = voice->right;
	const float gin = voice->inGain;
	const float ginR = voice->inGainR;
	const float pg = voice->postGain, pmk = voice->postMakeup, pwet = voice->postWet;
	const float tc = voice->toneC;
	const float ampStep = (voice->ampEg.level - voice->ampLast)
	                    * (COL_OUT_SCALE / (float)OUTPUT_DMA_SIZE);
	const float da1A = A->da1, da2A = A->da2, da3A = A->da3;
	const float da1B = B->da1, da2B = B->da2, da3B = B->da3;
	const float mHA = A->mH, mBA = A->mB, mLA = A->mL;
	const float mHB = B->mH, mBB = B->mB, mLB = B->mL;
	float a1A = A->a1, a2A = A->a2, a3A = A->a3, ic1A = A->ic1, ic2A = A->ic2;
	float a1B = B->a1, a2B = B->a2, a3B = B->a3, ic1B = B->ic1, ic2B = B->ic2;
	float amp = voice->ampLast * COL_OUT_SCALE;
	float tzL = voice->toneZL, tzR = voice->toneZR;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t m = busM[n];
		const int32_t s2 = busS[n] * 2;
		const float xl = col_drive((float)(m + s2) * gin);
		const float xr = col_drive((float)(m - s2) * ginR);
		float ol, orr;
		float v1, v2, v3, yl, yr;

		v3 = xl - ic2A;
		v1 = a1A * ic1A + a2A * v3;
		v2 = ic2A + a2A * ic1A + a3A * v3;
		ic1A = 2.0f * v1 - ic1A;
		ic2A = 2.0f * v2 - ic2A;
		yl = mHA * xl + mBA * v1 + mLA * v2;

		v3 = xr - ic2B;
		v1 = a1B * ic1B + a2B * v3;
		v2 = ic2B + a2B * ic1B + a3B * v3;
		ic1B = 2.0f * v1 - ic1B;
		ic2B = 2.0f * v2 - ic2B;
		yr = mHB * xr + mBB * v1 + mLB * v2;

		/* S095 P3 F10: overdrive, the amp, then the tone (exact when open) */
		ol = col_post(yl, pg, pmk, pwet) * amp;
		orr = col_post(yr, pg, pmk, pwet) * amp;
		tzL = ol + tc * (tzL - ol);
		tzR = orr + tc * (tzR - orr);
		outL[n] = (int16_t)__SSAT((int32_t)tzL, 16);
		outR[n] = (int16_t)__SSAT((int32_t)tzR, 16);
		a1A += da1A; a2A += da2A; a3A += da3A;
		a1B += da1B; a2B += da2B; a3B += da3B;
		amp += ampStep;
	}
	A->ic1 = ic1A; A->ic2 = ic2A;
	B->ic1 = ic1B; B->ic2 = ic2B;
	voice->toneZL = tzL;
	voice->toneZR = tzR;
}

/*
 * The mono (rte = a filter type) filter and output loop (S095 P2, draft §3,
 * C-D16; S095 P3 F9, F10).
 *
 * What:       per sample: x = drive(M x inGain) -> filter A -> drive(g2 x
 *             y_A) -> filter B -> y; the overdrive col_post(y) x amp, the
 *             tone; outL[n] = right[n] = SSAT16(tz). g2 = inGainR (2^(4
 *             (drv + dro)), Q7) is folded into A's output mix (mH, mB, mL x
 *             g2) before the loop, so it costs nothing per sample; at drv 0,
 *             dro 0 it is 1, P2's shaper-only stage. The side bus is not
 *             read: the voice is mono and the positions have no effect (spr
 *             and mot show "---").
 * Why:        the second shaper bounds the first filter's resonant peak into
 *             the second filter; dro sets how hard (the user's note: the
 *             second stage is the first plus offsets).
 * Accessors:  Colloid_calcVoiceSyncBlock() (rte LP..Pek).
 */
static __attribute__((noinline)) void col_renderSer(ColloidVoice *voice, int16_t *outL)
{
	ColSvf *const A = &voice->svfA;
	ColSvf *const B = &voice->svfB;
	const int32_t *const busM = voice->busM;
	int16_t *const outR = voice->right;
	const float gin = voice->inGain;
	const float g2 = voice->inGainR;
	const float pg = voice->postGain, pmk = voice->postMakeup, pwet = voice->postWet;
	const float tc = voice->toneC;
	const float ampStep = (voice->ampEg.level - voice->ampLast)
	                    * (COL_OUT_SCALE / (float)OUTPUT_DMA_SIZE);
	const float da1A = A->da1, da2A = A->da2, da3A = A->da3;
	const float da1B = B->da1, da2B = B->da2, da3B = B->da3;
	/* the inter-stage gain folded into A's mix (exact at g2 = 1) */
	const float mHA = A->mH * g2, mBA = A->mB * g2, mLA = A->mL * g2;
	const float mHB = B->mH, mBB = B->mB, mLB = B->mL;
	float a1A = A->a1, a2A = A->a2, a3A = A->a3, ic1A = A->ic1, ic2A = A->ic2;
	float a1B = B->a1, a2B = B->a2, a3B = B->a3, ic1B = B->ic1, ic2B = B->ic2;
	float amp = voice->ampLast * COL_OUT_SCALE;
	float tz = voice->toneZL;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const float x = col_drive((float)busM[n] * gin);
		float v1, v2, v3, y;
		int16_t o;

		v3 = x - ic2A;
		v1 = a1A * ic1A + a2A * v3;
		v2 = ic2A + a2A * ic1A + a3A * v3;
		ic1A = 2.0f * v1 - ic1A;
		ic2A = 2.0f * v2 - ic2A;
		y = col_drive(mHA * x + mBA * v1 + mLA * v2);

		v3 = y - ic2B;
		v1 = a1B * ic1B + a2B * v3;
		v2 = ic2B + a2B * ic1B + a3B * v3;
		ic1B = 2.0f * v1 - ic1B;
		ic2B = 2.0f * v2 - ic2B;
		y = mHB * y + mBB * v1 + mLB * v2;

		/* S095 P3 F10: overdrive, the amp, then the tone (once: mono) */
		y = col_post(y, pg, pmk, pwet) * amp;
		tz = y + tc * (tz - y);
		o = (int16_t)__SSAT((int32_t)tz, 16);
		outL[n] = o;
		outR[n] = o;
		a1A += da1A; a2A += da2A; a3A += da3A;
		a1B += da1B; a2B += da2B; a3B += da3B;
		amp += ampStep;
	}
	A->ic1 = ic1A; A->ic2 = ic2A;
	B->ic1 = ic1B; B->ic2 = ic2B;
	/* both channels' tone states follow, so a change to str starts smooth */
	voice->toneZL = tz;
	voice->toneZR = tz;
}

void Colloid_calcVoiceSyncBlock(ColloidVoice *voice, int16_t *buf, uint8_t size)
{
	/*
	 * One block (S095 P2; S095 P3 F2, F9).
	 *
	 * What:       the oscillator loops, the first storing the buses and every
	 *             further one adding:
	 *               typ pwm, wav Sin or Tri: col_warpStore/Add per oscillator
	 *                 (COL_PWM_COUNT), W/ra/rb from oscPwmSin* or oscPwmTri*
	 *                 at the oscillator's width index;
	 *               typ pwm, wav Saw or Rec: col_pwmPairStore/Add per pair,
	 *                 sign +1 (Saw) or -1 (Rec);
	 *               otherwise: col_oscPairStore/Add per pair (COL_OSC_COUNT);
	 *             then the filter/output loop of the route (rte str stereo,
	 *             any other value mono).
	 * Why:        one branch per block for the form and one for the route,
	 *             outside every loop; the count depends on typ only (constant
	 *             cost). Both routes run both filters every block.
	 * Inputs:     voice (the async ran this block), buf (the left output).
	 * Outputs:    buf, voice->right; phases, buses, filter and tone states.
	 */
	const uint8_t isPwm = (uint8_t)(voice->osc.oscType == OSC_TYPE_PWM);
	const uint8_t form = (voice->osc.waveform < COL_PWM_WAVE_COUNT)
	                   ? voice->osc.waveform : COL_PWM_REC;
	uint32_t j;

	(void)size;   /* the voice always renders OUTPUT_DMA_SIZE */
	if (isPwm && form <= COL_PWM_TRI) {
		const uint32_t *const tW  = (form == COL_PWM_SIN) ? oscPwmSinW  : oscPwmTriW;
		const uint32_t *const tRa = (form == COL_PWM_SIN) ? oscPwmSinRa : oscPwmTriRa;
		const uint32_t *const tRb = (form == COL_PWM_SIN) ? oscPwmSinRb : oscPwmTriRb;

		for (j = 0u; j < COL_PWM_COUNT; j++) {
			const uint32_t k = voice->pwPos[j] & 127u;
			/* the oscillator's Q15 position into the low half */
			const uint32_t pq = (j & 1u) ? (voice->posPair[j >> 1] >> 16)
			                             : (voice->posPair[j >> 1] & 0xFFFFu);

			if (j == 0u)
				col_warpStore(voice->row[0], &voice->phase[0], voice->inc[0],
				              tW[k], tRa[k], tRb[k], pq, voice->busM, voice->busS);
			else
				col_warpAdd(voice->row[j], &voice->phase[j], voice->inc[j],
				            tW[k], tRa[k], tRb[k], pq, voice->busM, voice->busS);
		}
	} else if (isPwm) {
		const int32_t sign = (form == COL_PWM_SAW) ? 1 : -1;

		col_pwmPairStore(&voice->row[0], &voice->phase[0], &voice->inc[0], &voice->pwD[0],
		                 voice->posPair[0], voice->busM, voice->busS, sign);
		for (j = 1u; j < COL_PWM_COUNT / 2u; j++)
			col_pwmPairAdd(&voice->row[2u * j], &voice->phase[2u * j], &voice->inc[2u * j],
			               &voice->pwD[2u * j], voice->posPair[j], voice->busM, voice->busS,
			               sign);
	} else {
		col_oscPairStore(&voice->row[0], &voice->phase[0], &voice->inc[0],
		                 voice->posPair[0], voice->busM, voice->busS);
		for (j = 1u; j < COL_OSC_COUNT / 2u; j++)
			col_oscPairAdd(&voice->row[2u * j], &voice->phase[2u * j], &voice->inc[2u * j],
			               voice->posPair[j], voice->busM, voice->busS);
	}
	if (voice->route == COL_RTE_STR || voice->route >= COL_RTE_COUNT)
		col_renderPar(voice, buf);
	else
		col_renderSer(voice, buf);
}

uint8_t Colloid_isQuiet(const ColloidVoice *voice)
{
	return (uint8_t)(voice->ampEg.stage == COL_EG_IDLE);
}

void Colloid_setOscType(ColloidVoice *voice, const uint8_t typ)
{
	OscInfo *const osc = &voice->osc;
	/* the byte -> the platform type: 0 wav, 1 wtb, 2 and above pwm */
	const uint8_t type = (typ == COL_TYP_WAV) ? OSC_TYPE_WAV
	                   : ((typ == COL_TYP_WTB) ? OSC_TYPE_WTB : OSC_TYPE_PWM);
	/* the new list's length: Saw..Sin, Sin..Rec (S095 P3 F2), or the
	 * installed tables */
	const uint8_t count = (type == OSC_TYPE_WTB) ? sampleMemory_getNumWavetables()
	                    : ((type == OSC_TYPE_PWM) ? (uint8_t)COL_PWM_WAVE_COUNT
	                                              : (uint8_t)COL_WAVE_COUNT);
	const uint8_t last = count ? (uint8_t)(count - 1u) : 0u;
	uint32_t i;

	if (type == osc->oscType)
		return;
	osc->oscType = type;
	/* the stored byte is used as it is, clamped to an entry that exists
	 * (the platform's typ rule, S087 P0 r8) */
	if (osc->waveform > last)
		osc->waveform = last;
	osc->sampleCacheLooped = COL_BIND_NONE;
	if (type == OSC_TYPE_PWM) {
		const int32_t pos = (int32_t)(osc->position & 127u);
		const float scale = col_pwmScale(voice);

#pragma GCC unroll 1
		for (i = 0u; i < COL_OSC_MAX; i++)
			voice->pwPos[i] = col_pwmTarget(pos, voice->rnd[i], scale);
	}
}
