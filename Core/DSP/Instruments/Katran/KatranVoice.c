/*
 * KatranVoice.c - the Katran drone / noise voice (S103 P1).
 * Contract: KatranVoice.h; behaviour and laws: S103_KATRAN_NOISE_SYNTH.md
 * revision 3 and S103_P1_IMPLEMENTATION.md.
 */
#include <math.h>
#include <stddef.h>
#include <string.h>
#include "KatranVoice.h"
#include "KatranOsc.h"
#include "KatranTables.h"
#include "AlkalineControl.h"    /* alk_exp2f(), alk_log2f(), ALK_LOG2E */
#include "wavetable.h"          /* sine_table, sawTable, recTable, triTable */
#include "oscPwmTables.h"       /* the S094 P4 width law */
#include "MidiPitch.h"          /* midiPitch_detune(), MIDI_DEFAULT_TRIGGER_NOTE */
#include "modulationNode.h"     /* velocityModulators[], modNode_updateValue() */
#include "InstrumentManager.h"  /* instrumentManager_applyVelocityModulationTarget() */
#include "sequencer.h"          /* seq_isRunning() (read only) */
#include "random.h"             /* GetRngValue(), RNG_DR_ADDR */
#include "FxBuffer.h"           /* fxbuf_arenaView() */
#include "SampleMemory.h"       /* WAVETABLE_ROW_POINTS, the bind */

/* Control constants (S103 P1). Values marked "by ear" are set in the host
 * model (P§15 step 1) and recorded in tools/dsp_test/baselines/s103/.
 *
 * KAT_LOG2_440     log2(440 Hz): the A4 anchor of the pitch formula.
 * KAT_CUT_BASE_HZ  the cutoff law's 0: 20 Hz.
 * KAT_CUT_ST_MAX   the cutoff law's top in semitones above 20 Hz (15,963 Hz).
 * KAT_PI_OVER_FS   pi / fs, the prewarp argument per Hz.
 * KAT_SLEW         the per-block one-pole on cutoff and damping (the Colloid's
 *                  0.28).
 * KAT_K_SPAN       k = 2 - 1.95 res: k's floor 0.05 bounds the Q.
 * KAT_CLIP         the drive's input clamp; the cubic is monotonic to 1.5.
 * KAT_CUBIC        4/27, the division-free soft clip.
 * KAT_TOR_LV1/LV2  the patch's fixed channel levels (60, 64; P§8.3).
 * KAT_INC_PER_HZ   an oscillator's phase step per Hz: 2^32 / fs.
 * KAT_F_MAX        no table oscillator above 0.45 fs (the arena form is
 *                  uncapped, P§3.4).
 * KAT_OUT_SCALE    full scale of the int16 stores.
 * KAT_RUN_RISE     run 127's ~20 ms rise (0.10 a block: 95 % in 27 blocks).
 * KAT_DRIFT_STEP   the delay drift's +-1.5 % per draw (P§8.2 kat_drift).
 */
#define KAT_LOG2_440          8.78135971f
#define KAT_CUT_BASE_HZ       20.0f
#define KAT_CUT_ST_MAX        115.7f
#define KAT_PI_OVER_FS        (3.14159265f / REAL_FS)
#define KAT_SLEW              0.28f
#define KAT_K_SPAN            1.95f
#define KAT_CLIP              1.5f
#define KAT_CUBIC             (4.0f / 27.0f)
#define KAT_TOR_LV1           (60.0f / 127.0f)
#define KAT_TOR_LV2           (64.0f / 127.0f)
#define KAT_INC_PER_HZ        (4294967296.0f / REAL_FS)
#define KAT_F_MAX             (0.45f * REAL_FS)
#define KAT_OUT_SCALE         32767.0f
#define KAT_RUN_RISE          0.10f
#define KAT_REARM_STEP        0.15f   /* 1/7 a block: the K1 5 ms re-arm */
#define KAT_DRIFT_STEP        0.015f
#define KAT_SIN_2PI           6.28318531f
/* The output filter's fully open mix (out_cut 63): pass the input exactly.
 * It is not a stored selector value, only kat_svfControl()'s mix argument. */
#define KAT_MIX_OPEN          6u
/*
 * The half-rate core's laws (S103 P2).
 *
 * KAT_PI_OVER_FS_H   pi / (fs / 2): core R's loop filter prewarp.
 * KAT_CUT_ST_MAX_H   core R's cutoff ceiling, 104 semitones above 20 Hz
 *                    (8.1 kHz): tan()'s [3/2] Pade form stays accurate and
 *                    finite there (argument 1.15 rad; its pole is 1.58 rad,
 *                    which 15.96 kHz at fs / 2 would pass).
 * KAT_F_MAX_H        core R's table oscillator ceiling, 0.45 (fs / 2).
 */
#define KAT_PI_OVER_FS_H      (2.0f * KAT_PI_OVER_FS)
#define KAT_CUT_ST_MAX_H      104.0f
#define KAT_F_MAX_H           (0.5f * KAT_F_MAX)

/*
 * The voice's xorshift and the per-sample RNG read (S103 P1).
 *
 * What:       kat_rngNext() advances the per-voice xorshift32 (seeded from
 *             GetRngValue() at init, salted by the slot) for the per-block
 *             draws: CE cycle times and peaks, the drift walk, the patch LFO
 *             jitter. kat_rngRead() is the Colloid's col_rngRead()
 *             (ColloidVoice.c:286): one inline read of RNG_DR per sample on
 *             the device, GetRngValue() on the host.
 * Why:        per-block draws need no hardware read (cheaper, and the host
 *             model is repeatable with a seed); the audio noise keeps the
 *             accepted hardware character (MEMORY.md DSP/RNG). An
 *             out-of-line call in the loop spilled every FP register in the
 *             Colloid (+47 a sample), hence the inline register load.
 */
static inline uint32_t kat_rngSeed(const uint32_t salt)
{
	const uint32_t s = (((uint32_t)(uint16_t)GetRngValue() << 16) | 0x5A5Au) ^ salt
	                 ^ 0x9E3779B9u;

	return s ? s : 0x9E3779B9u;
}

static inline uint32_t kat_rngNext(uint32_t *s)
{
	uint32_t x = *s;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	*s = x;
	return x;
}

static inline int16_t kat_rngRead(void)
{
#if defined(__ARM_ARCH_7EM__)
	return (int16_t)(*(volatile uint32_t *)RNG_DR_ADDR);
#else
	return GetRngValue();
#endif
}

/* A draw in -1..+1 from the per-voice generator. */
static inline float kat_rngBipolar(uint32_t *s)
{
	return (float)(kat_rngNext(s) >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

static inline float kat_clamp01(const float x)
{
	return fminf(fmaxf(x, 0.0f), 1.0f);
}

/* tan(x) by the [3/2] Pade form x (15 - x^2) / (15 - 6 x^2), the Colloid's
 * col_tanf(); one VDIV a block per filter, none a sample. */
static inline float kat_tanf(const float x)
{
	const float x2 = x * x;

	return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

/* The bare cubic: c = clamp(x, +-1.5), y = c - (4/27) c^3 (TORCIDO ch2). */
static inline float kat_cubic(const float x)
{
	const float c = fminf(fmaxf(x, -KAT_CLIP), KAT_CLIP);

	return c - KAT_CUBIC * c * c * c;
}

/* TORCIDO ch1: the cubic with the tube's even-order term folded in (C2):
 * x' = clamp(x, +-1.5); y = x' - (4/27) x'^3 + tube x'^2. x'^2 is already
 * there for the cube, so the tube is one multiply-add. */
static inline float kat_tube(const float x, const float tube)
{
	const float c = fminf(fmaxf(x, -KAT_CLIP), KAT_CLIP);
	const float c2 = c * c;

	return c - KAT_CUBIC * c * c2 + tube * c2;
}

/*
 * The saturating int16 store value (S103 P1; S103 P2: one SSAT).
 *
 * What:       x clamped to -32,768..32,767.
 * Why:        S103 P2: every core and output sample takes two of these. The
 *             two-sided compare cost about three instructions each; SSAT is
 *             one (inline asm on the device, so no CMSIS header is needed; the
 *             host build keeps the compare).
 */
static inline int16_t kat_sat16(const int32_t x)
{
#if defined(__ARM_ARCH_7EM__)
	int32_t r;

	__asm__("ssat %0, #16, %1" : "=r"(r) : "r"(x));
	return (int16_t)r;
#else
	return (int16_t)((x > 32767) ? 32767 : ((x < -32768) ? -32768 : x));
#endif
}

static inline int16_t kat_sat16f(const float x)
{
	/* truncated, not rounded: lrintf() is an out-of-line libm call in the
	 * per-sample loops (measured; the Colloid stores by truncation too) */
	return kat_sat16((int32_t)x);
}

/* The octave row of a stock table for f (the Colloid's col_octave()). */
static inline uint32_t kat_octave(const float f)
{
	union { float f; int32_t i; } v = { f };
	int32_t k = ((v.i >> 23) & 0xFF) - (127 + 4)
	          + (int32_t)((v.i & 0x007FFFFF) >= 0x0002D013);

	return (uint32_t)((k < 0) ? 0 : ((k > 10) ? 10 : k));
}

/* A frequency to a phase increment; the arena form is uncapped, so a value
 * past 2^32 is pinned rather than converted (C's cast is undefined there). */
static inline uint32_t kat_inc(const float f)
{
	const float x = f * KAT_INC_PER_HZ;

	if (!(x > 0.0f))
		return 0u;
	return (x >= 4294967040.0f) ? 0xFFFFFFFFu : (uint32_t)x;
}

/*
 * One loop or output filter's coefficients for this block (S103 P1).
 *
 * What:       col_svfControl() (ColloidVoice.c:550) for KatSvf: the block's
 *             start a = a + 32 da (where the last ramp ended); the cutoff
 *             target st (semitones above 20 Hz, clamped 0..115.7) and the
 *             damping target kT = 2 - 1.95 res, both slewed by 0.28; g =
 *             tan(pi 20 x 2^(st/12) / fs) (col_tanf form), a1 = 1 / (1 + g (g +
 *             k)), a2 = g a1, a3 = g a2 as the targets; da = (target - a) / 32;
 *             then the output mix for the type: LP (0, 0, 1), BP (0, 1, 0),
 *             HP (1, -k, -1).
 * Why:        the TPT form is stable under the per-sample ramp, and the slew
 *             removes block steps from LFO, CE2 and tracking moves; two VDIV
 *             a block per filter, none per sample. A local copy, not a shared
 *             one: col_svfControl() is static, and moving it would change the
 *             Colloid's object (gate: Colloid bit-identical).
 * Inputs:     f, st (semitones), res (0..1), mix (KAT_LT_* % 3); S103 P2:
 *             piOverFs (pi / the core's rate), stMax (the core's cutoff
 *             ceiling), count (the core's samples a block: 32 full rate, 16
 *             half rate). The ramp spans `count` samples, so the half-rate
 *             core's coefficients land on their targets at its block end too.
 * Cost:       about 99 a block (the Colloid's measured figure).
 * Accessors:  Katran_calcVoiceAsync() (F1 x2), Katran_initVoice() (through
 *             kat_svfInit()).
 */
static void kat_svfControl(KatSvf *f, const float st, const float res, const uint8_t mix,
                           const float piOverFs, const float stMax, const uint32_t count)
{
	const float stc = fminf(fmaxf(st, 0.0f), stMax);
	const float kT = 2.0f - KAT_K_SPAN * kat_clamp01(res);
	const float inv = 1.0f / (float)count;
	float g, t1, t2, t3;

	f->a1 += (float)count * f->da1;
	f->a2 += (float)count * f->da2;
	f->a3 += (float)count * f->da3;
	f->cutSt += KAT_SLEW * (stc - f->cutSt);
	f->k += KAT_SLEW * (kT - f->k);
	g = kat_tanf(KAT_CUT_BASE_HZ * alk_exp2f(f->cutSt * (1.0f / 12.0f)) * piOverFs);
	t1 = 1.0f / (1.0f + g * (g + f->k));
	t2 = g * t1;
	t3 = g * t2;
	f->da1 = (t1 - f->a1) * inv;
	f->da2 = (t2 - f->a2) * inv;
	f->da3 = (t3 - f->a3) * inv;
	switch (mix) {
	case KAT_LT_BP: f->mH = 0.0f; f->mB = 1.0f;  f->mL = 0.0f;  break;
	case KAT_LT_HP: f->mH = 1.0f; f->mB = -f->k; f->mL = -1.0f; break;
	case KAT_MIX_OPEN: f->mH = 1.0f; f->mB = 0.0f; f->mL = 0.0f; break;
	default:        f->mH = 0.0f; f->mB = 0.0f;  f->mL = 1.0f;  break;
	}
}

/* A filter at rest on its settings: the first block starts on its targets.
 * S103 P2: piOverFs and stMax are the core's (full or half rate), as
 * kat_svfControl(). */
static void kat_svfInit(KatSvf *f, const float st, const float res, const uint8_t mix,
                        const float piOverFs, const float stMax)
{
	float g;

	memset(f, 0, sizeof(*f));
	f->cutSt = fminf(fmaxf(st, 0.0f), stMax);
	f->k = 2.0f - KAT_K_SPAN * kat_clamp01(res);
	g = kat_tanf(KAT_CUT_BASE_HZ * alk_exp2f(f->cutSt * (1.0f / 12.0f)) * piOverFs);
	f->a1 = 1.0f / (1.0f + g * (g + f->k));
	f->a2 = g * f->a1;
	f->a3 = g * f->a2;
	if (mix == KAT_LT_BP) { f->mH = 0.0f; f->mB = 1.0f;  f->mL = 0.0f; }
	else if (mix == KAT_LT_HP) { f->mH = 1.0f; f->mB = -f->k; f->mL = -1.0f; }
	else if (mix == KAT_MIX_OPEN) { f->mH = 1.0f; f->mB = 0.0f; f->mL = 0.0f; }
	else { f->mH = 0.0f; f->mB = 0.0f; f->mL = 1.0f; }
}

/*
 * Bind the shared wavetable (S103 P1; col_wtbBind()'s rule, ColloidVoice.c:668).
 *
 * What:       records the key (KAT_BIND_WTB, the table = osc.waveform clamped
 *             to the installed count, the catalogue generation); the frame
 *             count and the first frame's row. No table: one frame and NULL,
 *             and the async then plays sawTable[1] at the oscillator's level
 *             (silence is not used: the loop must keep an exciter).
 * Why:        one table for both oscillators (K-D8); each picks its own frame
 *             from its own pos. osc_userBind() is never called on Katran.
 */
static __attribute__((noinline)) void kat_wtbBind(KatranVoice *v)
{
	OscInfo *const osc = &v->osc;
	const uint8_t count = sampleMemory_getNumWavetables();
	const uint8_t index = (osc->waveform < count) ? osc->waveform
	                    : (uint8_t)(count ? count - 1u : 0u);
	const wavetable_catalogue_entry_t *e = sampleMemory_wavetableEntry(index);

	osc->sampleCacheLooped = KAT_BIND_WTB;
	osc->sampleCacheIndex = index;
	osc->sampleCacheGeneration = sampleMemory_getGeneration();
	osc->sampleCacheSize = e ? e->frame_count : 1u;
	osc->sampleCacheData = e ? sampleMemory_wavetableRows(e) : 0;
}

static inline uint8_t kat_wtbKeyChanged(const OscInfo *osc)
{
	return (uint8_t)(osc->sampleCacheLooped != KAT_BIND_WTB ||
	                 osc->waveform != osc->sampleCacheIndex ||
	                 sampleMemory_getGeneration() != osc->sampleCacheGeneration);
}

/*
 * Start a chaos envelope's new cycle (S103 P1, P§3.3).
 *
 * What:       total = katCeBlocks[cycle] x 2^(chs' x r) blocks (r uniform
 *             +-1); rise share = skew' (0 instant .. 1 all rise; each stage
 *             at least 3 blocks); peak = 1 - chs' x |r2| / 2; up/down the
 *             per-block steps. The level stays where it is (a new cycle
 *             starts from 0 through kat_ceStep()).
 * Why:        a free-running cycling envelope with a chaotic progression;
 *             never reset by a trigger (the brief).
 */
static void kat_ceStart(KatranVoice *v, const uint32_t i)
{
	KatCe *const e = &v->ce[i];
	const uint8_t cyc = (i == 0u) ? v->ce1Cycle : v->ce2Cycle;
	const uint8_t skw = (i == 0u) ? v->ce1Skew : v->ce2Skew;
	const float chs = (float)(v->chaos & 127u) * (1.0f / 127.0f);
	const float r = kat_rngBipolar(&v->rng);
	const float r2 = kat_rngBipolar(&v->rng);
	float total = (float)katCeBlocks[cyc & 127u] * alk_exp2f(chs * r);
	uint32_t n;
	uint32_t rise;

	if (!(total >= 2.0f))
		total = 2.0f;
	n = (uint32_t)(total + 0.5f);
	if (n < 2u)
		n = 2u;
	rise = (uint32_t)((float)n * ((float)(skw & 127u) * (1.0f / 127.0f)) + 0.5f);
	if (rise < 3u)
		rise = 3u;
	if (rise > n - 3u)
		rise = (n > 6u) ? (n - 3u) : (n / 2u);
	if (rise < 1u)
		rise = 1u;
	e->peak = 1.0f - chs * fabsf(r2) * 0.5f;
	if (e->peak < 0.05f)
		e->peak = 0.05f;
	e->up = e->peak / (float)rise;
	e->down = e->peak / (float)(n - rise);
	e->stage = 0u;
}

/*
 * One chaos envelope's block (S103 P1, P§3.3).
 *
 * What:       rise: level += up until level >= peak, then fall; fall: level
 *             -= down until level <= 0, then a new cycle (kat_ceStart()).
 *             Above chs 64 the rise rate is also scaled by
 *             (1 + 2 (chs' - 0.5) min(1, 4 ringPrev)): a loud loop hurries
 *             its own envelope.
 * Why:        repeating and chaotic, never retriggered (the brief).
 */
static void kat_ceStep(KatranVoice *v, const uint32_t i)
{
	KatCe *const e = &v->ce[i];
	const float chs = (float)(v->chaos & 127u) * (1.0f / 127.0f);

	if (e->stage == 0u) {
		float up = e->up;

		if (chs > 0.5f) {
			const float lp = fminf(1.0f, 4.0f * v->ringPrev);

			up *= 1.0f + 2.0f * (chs - 0.5f) * lp;
		}
		e->level += up;
		if (e->level >= e->peak) {
			e->level = e->peak;
			e->stage = 1u;
		}
	} else {
		e->level -= e->down;
		if (e->level <= 0.0f) {
			e->level = 0.0f;
			kat_ceStart(v, i);
		}
	}
}

/*
 * The patch LFO's block (S103 P1, D§5.7; lfm folds lfw).
 *
 * What:       rate = katPlfoHz[lfr] x 2^(lfm' x (1.5 ringPrev + inv) + jitter)
 *             (one alk_exp2f()); the phase advances by rate x 32 / fs; the
 *             shape is the sine of the phase warped by
 *             w = 0.5 + 0.45 lfm' x inv (lfm 0 is a plain sine; rising lfm
 *             skews it); a chaos jitter of chs' x 0.1 on the rate, drawn once
 *             per cycle. cv = shape x lfl'; inv = -cv.
 * Why:        the patch's unstable LFO: its rate follows the output and its
 *             own inverted output. ringPrev is the last block's mean |r|.
 */
static void kat_plfoStep(KatranVoice *v)
{
	const float lfm = (float)(v->plfoMod & 127u) * (1.0f / 127.0f);
	const float lfl = (float)(v->plfoLevel & 127u) * (1.0f / 127.0f);
	const float chs = (float)(v->chaos & 127u) * (1.0f / 127.0f);
	const float inv = -v->cv;
	float rate;
	float w;
	float ph;
	float q;
	uint32_t idx;

	rate = katPlfoHz[v->plfoRate & 127u]
	     * alk_exp2f(lfm * (1.5f * v->ringPrev + inv) + v->plfoJit);
	v->lfoPh += rate * ((float)OUTPUT_DMA_SIZE / REAL_FS);
	if (v->lfoPh >= 1.0f) {
		v->lfoPh -= floorf(v->lfoPh);
		v->plfoJit = chs * 0.1f * kat_rngBipolar(&v->rng);
	}
	w = fminf(fmaxf(0.5f + 0.45f * lfm * inv, 0.05f), 0.95f);
	ph = v->lfoPh;
	q = (ph < w) ? (0.5f * ph / w) : (0.5f + 0.5f * (ph - w) / (1.0f - w));
	idx = (uint32_t)(q * 1024.0f) & 0x3FFu;
	v->cv = ((float)sine_table[idx] * (1.0f / 32768.0f)) * lfl;
}

/*
 * The run gain gr for this block (S103 P1, P§3.1; decisions Q2, Q3, Q9).
 *
 * What:       the target and the per-sample step of gr, from, in order:
 *             1. the pending-apply fade (fadeReq > 0): gr falls to 0 by
 *                katFadeK per block (~20 ms), fadeReq counts down; at 0 the
 *                normal rule resumes (the apply was cancelled);
 *             2. not armed (boot, Scene load, before a kick or a start): 0;
 *             3. the transport stop edge (wasRunning && !seq_isRunning()):
 *                stopDecay = 1, which decays at katRunK[126] even when run is
 *                127; a start edge or a kick clears it;
 *             4. a kick's K1 re-arm (rearm > 0): gr reaches 1 over that many
 *                blocks;
 *             5. run == 127 (and no stopDecay): gr rises to 1 over ~20 ms;
 *             6. run 0..126: gr x= katRunK[run] per block (-60 dB at T(run),
 *                0.25 .. 60 s); below 1e-4 it is 0.
 *             grStep = (new - gr) / 32: the loops ramp it per sample.
 *             silentBlocks counts blocks with gr == 0 (saturating).
 * Why:        one gain on every delay input ends the sound naturally with its
 *             character (K-D9, G1); the transport and boot rules are Q3; the
 *             fade is Q9 (a Scene change over a drone without a click).
 * Accessors:  Katran_calcVoiceAsync().
 */
static void kat_runControl(KatranVoice *v)
{
	const uint8_t running = seq_isRunning();
	float newGr;

	if (running && !v->wasRunning) {
		v->armed = 1u;
		v->stopDecay = 0u;
	}
	if (!running && v->wasRunning && v->armed)
		v->stopDecay = 1u;
	v->wasRunning = running;

	if (v->fadeReq) {
		newGr = v->gr * katFadeK;
		v->fadeReq--;
	} else if (!v->armed) {
		newGr = 0.0f;
	} else if (v->rearm) {
		newGr = v->gr + KAT_REARM_STEP;
		v->rearm--;
	} else if (v->stopDecay) {
		newGr = v->gr * katRunK[126u];
	} else if ((v->run & 127u) >= 127u) {
		newGr = v->gr + KAT_RUN_RISE * (1.0f - v->gr);
	} else {
		newGr = v->gr * katRunK[v->run & 127u];
	}
	/*
	 * S103 P2, bug B4: the K1 re-arm adds KAT_REARM_STEP a block with no
	 * ceiling, and grStep was computed from the unclamped value, so a kick
	 * into a sounding drone (gr > 0.85) ramped the loop gain above 1 for one
	 * block (only the sync's end-of-block clamp pulled gr back). The ceiling
	 * here keeps the per-sample ramp inside 0..1, so the loop gain never
	 * passes unity on a kick.
	 */
	if (newGr > 1.0f)
		newGr = 1.0f;
	if (newGr < 1.0e-4f)
		newGr = 0.0f;
	v->grStep = (newGr - v->gr) * (1.0f / (float)OUTPUT_DMA_SIZE);
	v->gr = newGr;
	if (v->gr == 0.0f) {
		if (v->silentBlocks < 0xFFFFu)
			v->silentBlocks++;
	} else {
		v->silentBlocks = 0u;
	}
}

/*
 * The kick's per-block part (S103 P1, P§3.2).
 *
 * What:       kE decays by katKickK (~80 ms to -60 dB); burst = min(1, 2 kik')
 *             and swell = max(0, 2 kik' - 1) (the base/offset split of one
 *             row). This block: the exciter level += burst x kE x 0.8, the
 *             noise mix += burst x kE x 0.5 (toward noise), the feedback +=
 *             swell x kE x 0.3; all inside the shared feedback clamp.
 *             tExc/tNse are the block's effective exciter level and noise
 *             mix; the target records are unchanged.
 * Why:        the kick adds energy through gains the loops already apply, so
 *             it costs nothing per sample.
 */
static void kat_kickControl(KatranVoice *v)
{
	const float kik = (float)(v->kick & 127u) * (1.0f / 127.0f);
	const float burst = fminf(1.0f, 2.0f * kik);
	const float swell = fmaxf(0.0f, 2.0f * kik - 1.0f);
	const float bE = burst * v->kE;

	v->kE *= katKickK;
	v->tExc = kat_clamp01((float)(v->levelA & 127u) * (1.0f / 127.0f) + bE * 0.8f);
	v->tNse = kat_clamp01((float)(v->noise & 127u) * (1.0f / 127.0f) + bE * 0.5f);
	v->kickSwell = swell * v->kE * 0.3f;
}

/* The detune ratio 2^d for |d| <= 25 cents, by its second-order expansion
 * (C4: one less alk_exp2f() a block; error below 1e-6 at 25 cents). */
static inline float kat_detuneRatio(const float d)
{
	const float l2 = 0.69314718f;

	return 1.0f + l2 * d + 0.5f * l2 * l2 * d * d;
}

/* The arena window's first sample for a position in 0..127 units. */
static inline uint32_t kat_arenaStart(const KatranVoice *v, const float pos)
{
	const float span = (float)((v->arenaN > KAT_ARENA_WIN)
	                         ? (v->arenaN - KAT_ARENA_WIN) : 0u);
	const float p = fminf(fmaxf(pos, 0.0f), 127.0f);

	return (uint32_t)(p * (span / 127.0f) + 0.5f);
}

/*
 * The three oscillators' control for this block (S103 P1, P§3.4, K-D10).
 *
 * What:       1. pitch: A = the note + osc.midiFreq (coarse, fine) as the
 *                Colloid (ColloidVoice.c:1388-1395), log2 Hz; B = the note +
 *                (bCoarse - 48) semitones + (bFine - 63) / 63 x 0.5 semitone
 *                + the patch LFO's inv x lfl x 0.3 semitone (D§5.7: inv to
 *                OSC B fine). A's L/R: one alk_exp2f() for A, then the
 *                second-order 2^(+-d) detune ratio for d = dtn'^2 x 25 cents
 *                (C4). Increments inc = f x 2^32 / fs, f capped at 0.45 fs
 *                for the table forms; for the arena form, uncapped (P§3.4).
 *             2. form per oscillator: src selects ARENA for A (L and R) and/or
 *                B; otherwise the typ form: wav -> PLAIN (Saw/Rec/Tri rows by
 *                octave, Sin the sine row), wtb -> PLAIN on the frame row,
 *                pwm -> PWMD (Saw, Rec) or WARP (Tri).
 *             3. the next row (applied by the loop at the oscillator's next
 *                cycle start, K-D10): wav/pwm the octave row for this block's
 *                pitch; wtb the frame round(pos' x (F - 1)) (A L: pos - psp,
 *                A R: pos + psp, psp = dtn' x 16 steps; B: bPos + the patch
 *                LFO's cv x lfl x 24 steps); ARENA the window start
 *                S = round(pos' x (arenaN - 1,024)) with the same offsets.
 *             4. PWM widths: the S094 P4 position law (oscPwmDRec/DSaw,
 *                oscPwmLevel*, oscPwmTriLift) from each oscillator's pos,
 *                slewed by OSC_PWM_SLEW_STEPS, glide start/end (pwD0/pwD).
 * Why:        the loops only read rows, phases and increments; every choice
 *             is made here once per block. The row is swapped at the cycle
 *             start so a moving pos never cuts a cycle (K-D10), for frames
 *             and arena windows alike.
 * Cost:       about 320 a block (P§1.4).
 * Accessors:  Katran_calcVoiceAsync().
 */
static __attribute__((noinline)) void kat_oscControl(KatranVoice *v)
{
	OscInfo *const osc = &v->osc;
	const uint8_t src = (uint8_t)(v->source & 3u);
	const uint8_t aArena = (uint8_t)(src == KAT_SRC_A || src == KAT_SRC_AB);
	const uint8_t bArena = (uint8_t)(src == KAT_SRC_B || src == KAT_SRC_AB);
	const uint8_t isPwm = (uint8_t)(osc->oscType == OSC_TYPE_PWM);
	const uint8_t isWtb = (uint8_t)(osc->oscType == OSC_TYPE_WTB);
	const uint8_t wave = osc->waveform;
	const uint8_t pwmForm = (wave == OSC_PWM_TRI) ? KAT_FORM_WARP : KAT_FORM_PWMD;
	const uint16_t *const dTab = (wave == OSC_PWM_SAW) ? oscPwmDSaw : oscPwmDRec;
	const float dtn = (float)(v->spreadA & 127u) * (1.0f / 127.0f);
	const float psp = dtn * 16.0f;
	const float dOct = dtn * dtn * (25.0f / 1200.0f);
	const float ratio = kat_detuneRatio(dOct);
	const float lfl = (float)(v->plfoLevel & 127u) * (1.0f / 127.0f);
	const float pos = (float)(osc->position & 127u);
	const float posL = fminf(fmaxf(pos - psp, 0.0f), 127.0f);
	const float posR = fminf(fmaxf(pos + psp, 0.0f), 127.0f);
	const float posB = fminf(fmaxf((float)(v->bPos & 127u) + v->cv * lfl * 24.0f,
	                               0.0f), 127.0f);
	const int16_t *rowL;
	const int16_t *rowR;
	const int16_t *rowB;
	float base, log2B;
	int16_t n;
	float x;
	float fA, fB;
	float fAR, fAR2;     /* S103 P2: core R's frequency, and twice it (its rows) */
	float posW[3];
	uint32_t c;

	/* the note's pitch, the Alkaline / Colloid formula */
	n = (int16_t)(osc->midiFreq >> 8) + (int16_t)v->note - (int16_t)MIDI_DEFAULT_TRIGGER_NOTE;
	n = (n < 0) ? 0 : ((n > 127) ? 127 : n);
	osc->baseNote = v->note;
	x = midiPitch_detune((uint8_t)(osc->midiFreq & 0xffu)) - 1.0f;
	base = KAT_LOG2_440 + ((float)n - 69.0f) * (1.0f / 12.0f)
	     + x * (ALK_LOG2E - 0.5f * ALK_LOG2E * x);
	log2B = base + ((float)(v->bCoarse & 127u) - 48.0f
	              + ((float)(v->bFine & 127u) - 63.0f) * (0.5f / 63.0f)
	              + v->cv * lfl * 0.3f) * (1.0f / 12.0f);
	fA = alk_exp2f(base) * osc->modNodeValue;
	fB = alk_exp2f(log2B) * osc->modNodeValue;
	if (!aArena)
		fA = fminf(fA, KAT_F_MAX);
	if (!bArena)
		fB = fminf(fB, KAT_F_MAX);
	/*
	 * S103 P2: core R's copy of OSC A runs at half rate (KatranOsc.c *H
	 * loops step 2 x incA[1]). incA[1] keeps the full-rate law, so both copies
	 * follow the same pitch; its table forms are capped at 0.45 (fs / 2), the
	 * half rate's Nyquist margin (the arena form stays uncapped, P§3.4), and
	 * fAR2 = 2 fAR picks its octave rows below, one octave further up, so the
	 * band-limited rows suit the half rate.
	 */
	fAR = aArena ? (fA * ratio) : fminf(fA * ratio, KAT_F_MAX_H);
	fAR2 = 2.0f * fAR;
	v->incA[0] = kat_inc(fA / ratio);
	v->incA[1] = kat_inc(fAR);
	v->incB = kat_inc(fB);

	/* the rows from the form and the position */
	if (aArena) {
		const int16_t *const arena = v->arena ? v->arena : katZeroRow;

		rowL = (v->arenaN >= KAT_ARENA_WIN) ? arena + kat_arenaStart(v, posL) : katZeroRow;
		rowR = (v->arenaN >= KAT_ARENA_WIN) ? arena + kat_arenaStart(v, posR) : katZeroRow;
	} else if (isWtb) {
		const uint32_t frames = osc->sampleCacheSize ? osc->sampleCacheSize : 1u;
		const uint32_t frL = (uint32_t)(posL * (float)(frames - 1u) / 127.0f + 0.5f);
		const uint32_t frR = (uint32_t)(posR * (float)(frames - 1u) / 127.0f + 0.5f);
		const int16_t *const data = osc->sampleCacheData;

		rowL = data ? data + ((frL < frames) ? frL : frames - 1u) * WAVETABLE_ROW_POINTS
		            : sawTable[1];
		rowR = data ? data + ((frR < frames) ? frR : frames - 1u) * WAVETABLE_ROW_POINTS
		            : sawTable[1];
	} else if (isPwm && pwmForm == KAT_FORM_WARP) {
		/* S103 P2: rowR from fAR2, core R's half rate (see above) */
		const uint32_t o = kat_octave(fA);
		const uint32_t oR = kat_octave(fAR2);

		rowL = triTable[(o < 10u) ? o : 10u];
		rowR = triTable[(oR < 10u) ? oR : 10u];
	} else if (isPwm) {
		const uint32_t o = kat_octave(fA);
		const uint32_t oR = kat_octave(fAR2);

		rowL = sawTable[(o == 0u) ? 1u : o];
		rowR = sawTable[(oR == 0u) ? 1u : oR];
	} else if (!isWtb && wave == 3u) {
		rowL = sine_table;
		rowR = rowL;
	} else {
		const uint32_t o = kat_octave(fA);
		const uint32_t oR = kat_octave(fAR2);
		const int16_t (*const stock)[1024] = (wave == 1u)
		    ? (const int16_t (*)[1024])recTable
		    : ((wave == 2u) ? (const int16_t (*)[1024])triTable
		                    : (const int16_t (*)[1024])sawTable);
		const uint32_t lvl1 = (uint32_t)(stock != (const int16_t (*)[1024])triTable);

		rowL = stock[((o == 0u) && lvl1) ? 1u : o];
		rowR = stock[((oR == 0u) && lvl1) ? 1u : oR];
	}
	if (bArena) {
		const int16_t *const arena = v->arena ? v->arena : katZeroRow;

		rowB = (v->arenaN >= KAT_ARENA_WIN) ? arena + kat_arenaStart(v, posB) : katZeroRow;
	} else if (isWtb) {
		const uint32_t frames = osc->sampleCacheSize ? osc->sampleCacheSize : 1u;
		const uint32_t fr = (uint32_t)(posB * (float)(frames - 1u) / 127.0f + 0.5f);
		const int16_t *const data = osc->sampleCacheData;

		rowB = data ? data + ((fr < frames) ? fr : frames - 1u) * WAVETABLE_ROW_POINTS
		            : sawTable[1];
	} else if (isPwm && pwmForm == KAT_FORM_WARP) {
		const uint32_t o = kat_octave(fB);

		rowB = triTable[(o < 10u) ? o : 10u];
	} else if (isPwm) {
		const uint32_t o = kat_octave(fB);

		rowB = sawTable[(o == 0u) ? 1u : o];
	} else if (!isWtb && wave == 3u) {
		rowB = sine_table;
	} else {
		const uint32_t o = kat_octave(fB);
		const int16_t (*const stock)[1024] = (wave == 1u)
		    ? (const int16_t (*)[1024])recTable
		    : ((wave == 2u) ? (const int16_t (*)[1024])triTable
		                    : (const int16_t (*)[1024])sawTable);

		rowB = stock[((o == 0u) && stock != (const int16_t (*)[1024])triTable) ? 1u : o];
	}
	v->rowANext[0] = rowL;
	v->rowANext[1] = rowR;
	v->rowBNext = rowB;

	/* the pwm widths, slewed from the last block's end */
	posW[0] = posL;
	posW[1] = posR;
	posW[2] = posB;
	for (c = 0u; c < 3u; c++) {
		const int32_t target = (int32_t)posW[c] - (int32_t)v->pwPos[c];
		int32_t dw = target;
		const uint8_t prev = v->pwPos[c];

		if (!isPwm) {
			v->pwPos[c] = (uint8_t)posW[c];
		} else {
			if (dw > OSC_PWM_SLEW_STEPS)
				dw = OSC_PWM_SLEW_STEPS;
			else if (dw < -OSC_PWM_SLEW_STEPS)
				dw = -OSC_PWM_SLEW_STEPS;
			v->pwPos[c] = (uint8_t)((int32_t)prev + dw);
		}
		v->pwPos0[c] = prev;
		v->pwD0[c] = v->pwD[c];
		v->pwD[c] = dTab[v->pwPos[c] & 127u];
	}
}

/*
 * The two delay times for this block (S103 P1, D§5.3, P§8.2 tim/dtk/drf/spr).
 *
 * What:       base T = katTimeSamples[tim] (log, 32 .. 1,792 samples) x
 *             2^(-dtk' x (B's pitch offset in octaves)) (one alk_exp2f():
 *             the delay follows OSC B's pitch, so the comb tracks it);
 *             spread s = max(0, (spr - 63) / 64): T_L = T (1 + 0.12 s),
 *             T_R = T (1 - 0.12 s); drift: each core's driftTgt takes a new
 *             draw in +-1.5 % x drf' every 16 blocks and drift slews to it by
 *             1/16 a block; clamp 32 .. 2,040. The per-sample step is
 *             tStep = (target - tNow) / 32 (the time ramps per sample: a time
 *             change bends the pitch like tape, D§5.3).
 *             loopSt[] is the loop's pitch in semitones above 20 Hz, for F1's
 *             tracked types.
 * Why:        one exp2 for both cores (C4); the clamp keeps the fractional
 *             read inside the 2,048 ring with one sample of guard.
 */
static void kat_delayControl(KatranVoice *v, float loopSt[2])
{
	const float baseT = katTimeSamples[v->time & 127u];
	const float toff = ((float)(v->bCoarse & 127u) - 48.0f
	                  + ((float)(v->bFine & 127u) - 63.0f) * (0.5f / 63.0f)) * (1.0f / 12.0f);
	const float trk = (float)(v->track & 127u) * (1.0f / 127.0f);
	const float T = baseT * alk_exp2f(-trk * toff);
	const float spr = (v->spread > 63u) ? ((float)(v->spread - 63u) * (1.0f / 64.0f)) : 0.0f;
	const float dr = (float)(v->drift_ & 127u) * (1.0f / 127.0f);
	uint32_t c;

	if (++v->driftTick >= 16u) {
		v->driftTick = 0u;
		for (c = 0u; c < 2u; c++)
			v->driftTgt[c] = kat_rngBipolar(&v->rng) * KAT_DRIFT_STEP * dr;
	}
	for (c = 0u; c < 2u; c++) {
		float target;
		const float sp = (c == 0u) ? (1.0f + KAT_SPREAD_TIME * spr)
		                           : (1.0f - KAT_SPREAD_TIME * spr);

		v->drift[c] += (v->driftTgt[c] - v->drift[c]) * (1.0f / 16.0f);
		target = T * sp * (1.0f + v->drift[c]);
		target = fminf(fmaxf(target, KAT_TIME_MIN), 2040.0f);
		loopSt[c] = 12.0f * alk_log2f(REAL_FS / target) - 51.8f;
		/*
		 * S103 P2: core R's delay runs in half-rate samples. The target above
		 * is in full-rate samples for both cores (one law, one spread, one
		 * drift, one loop pitch for F1's tracking); core R takes half of it,
		 * so the same tim gives the same comb pitch on both cores, and ramps
		 * over its 16 half-rate samples. Its minimum (16 half-rate samples,
		 * KAT_TIME_MIN_H) is one half-rate block: the whole block's output is
		 * still in the ring before the block writes (the staged read rule).
		 */
		if (c == 1u)
			v->tStep[1] = (fmaxf(0.5f * target, KAT_TIME_MIN_H) - v->tNow[1])
			            * (1.0f / (float)KAT_HALF_N);
		else
			v->tStep[0] = (target - v->tNow[0]) * (1.0f / (float)OUTPUT_DMA_SIZE);
	}
}

/*
 * One core's delay outputs for the block (S103 P1, D§4 L1; S103 P2: the
 * fixed-point read, bug B1, and the full / half-rate pair).
 *
 * What:       per sample n of `count` (32 for core L, 16 for core R): the
 *             timed tap at w + n - T(n), linearly interpolated, and the dry
 *             tap one block back (KAT_TAP_DRY full rate, KAT_TAP_DRY_H half
 *             rate); D = (timed + dry) / 2 (dmx fixed at 64, P§8.3), stored as
 *             int16 into blkD[c]. T ramps by tStep per sample; tNow then
 *             advances by one block's ramp.
 *             The read position is Q16 fixed point:
 *               pos = ((w + n + KAT_RING) << 16) - Tq,  Tq = T x 65,536;
 *               i0 = (pos >> 16) & mask, frac = the top 15 bits of pos's
 *               fraction, timed = a + ((b - a) x frac >> 15).
 * Why:        bug B1: the P1 form computed idx = (w + n) - t in float and took
 *             (int32_t)idx, which truncates toward zero. Whenever the write
 *             index was behind the delay time (most of the ring cycle at long
 *             times) idx was negative, the integer part came out as the
 *             ceiling, frac was negative, and the read extrapolated along the
 *             next sample's slope instead of interpolating. In Q16 the
 *             position is unsigned and wraps modulo 2^32, which is a multiple
 *             of the ring, so (pos >> 16) & mask is always the floor sample
 *             and frac is always 0..1 (the + KAT_RING keeps it positive
 *             anyway, for clarity). It is also integer only: no
 *             int <-> float conversion per sample (the P1 read measured 53
 *             instructions a sample). The 15-bit fraction keeps (b - a) x frac
 *             inside int32 (|b - a| <= 65,535).
 *             With T >= one block (32 full-rate, 16 half-rate samples), the
 *             whole block's output is in the ring before this block writes,
 *             so the exciters' resets (L3) and the cross-feedback (L4) can
 *             read it first.
 * Inputs:     v, c, half (constants per wrapper).
 * Outputs:    blkD[c][0..count-1]; tNow[c].
 */
static inline __attribute__((always_inline)) void kat_delayBody(KatranVoice *v, const uint32_t c,
                                                                const uint32_t half)
{
	const uint32_t count = half ? KAT_HALF_N : OUTPUT_DMA_SIZE;
	const uint32_t dry = half ? KAT_TAP_DRY_H : KAT_TAP_DRY;
	const int16_t *const ring = v->ring[c];
	int16_t *const out = v->blkD[c];
	const uint32_t w = v->w[c];
	uint32_t tq = (uint32_t)(v->tNow[c] * 65536.0f);
	const uint32_t sq = (uint32_t)(int32_t)(v->tStep[c] * 65536.0f);
	uint32_t n;

	for (n = 0u; n < count; n++) {
		const uint32_t pos = ((w + n + KAT_RING) << 16) - tq;
		const uint32_t i0 = (pos >> 16) & KAT_RING_MASK;
		const uint32_t i1 = (i0 + 1u) & KAT_RING_MASK;
		const int32_t a = ring[i0];
		const int32_t fr = (int32_t)((pos >> 1) & 0x7FFFu);
		const int32_t y = a + (((ring[i1] - a) * fr) >> 15);
		const int32_t dr = ring[(w + n - dry) & KAT_RING_MASK];

		out[n] = (int16_t)((y + dr) >> 1);
		tq += sq;
	}
	v->tNow[c] += (float)count * v->tStep[c];
}

/* Core L's delay read (full rate) and core R's (half rate): one noinline
 * wrapper each, so katran_cpu.py counts each by name (the P1 two-core outer
 * loop left it unclear whether the count was one core's sample or both). */
static __attribute__((noinline)) void kat_delayReadL(KatranVoice *v)
{
	kat_delayBody(v, 0u, 0u);
}

static __attribute__((noinline)) void kat_delayReadH(KatranVoice *v)
{
	kat_delayBody(v, 1u, 1u);
}

/*
 * One core's audio for the block (S103 P1, P§4 L4; S103 P2: the full-rate
 * core L and the half-rate core R).
 *
 * What:       per sample n of `count` (32 core L, 16 core R):
 *             t1 = TOR1(D[n]): x = D x tGain, x' = clamp(x, +-1.5),
 *             y = x' - (4/27) x'^3 + tTube x'^2 (C2), t1 = lv1 (tWet y +
 *             (1 - tWet) D); t2 = lv2 x cubic(tGain2 x exc ((1 - nse) A[n] +
 *             nse noise)); u = F2(F1(t1 + t2)); ring[w] = SSAT16(gr (u + xfb
 *             D_other + fb D) x 32768); r = t1 ((1 - rk) + rk bLvl B);
 *             core L: buf[n] = SSAT16((r + g2 t1) x 32767);
 *             core R: o = r (1 + g1); right[2n] = SSAT16((hPrev + o) / 2 x
 *             32767), right[2n + 1] = SSAT16(o x 32767), hPrev = o (the 2x
 *             linear interpolation to the full rate, half a full-rate sample
 *             of delay).
 *             The other core's delay output: core L reads core R's 16-sample
 *             block held (blkD[1][n >> 1]); core R reads core L's decimated
 *             (blkD[0][2n]). OSC B is full rate: core R reads blkB[2n].
 *             Core R's ramps (gr, fb, rk, g1) step twice as far per sample,
 *             so both cores reach the block's end values; its F1
 *             coefficients come from kat_svfControl() at fs / 2 with a
 *             16-sample ramp; its F2 coefficient is f2K (2 - f2K), the same
 *             one-pole corner at half the rate (1 - (1 - k)^2).
 * Why:        S103 P2 (your decision): core R at half rate keeps two
 *             independent loops for about half the second core's cost (its
 *             loop band stops at 11 kHz). Core L and core R are separate
 *             noinline functions with constant c and half: the P1 single
 *             function chose its output per sample ((c == 0u) ? ...) and
 *             ramped both VCA gains in both cores; here each core ramps only
 *             its own. One RNG read per core sample: 1.5 a full-rate sample
 *             (was 2).
 * Inputs:     v, c (0 L, 1 R), out (buf for L, right for R), half.
 */
static inline __attribute__((always_inline)) void kat_coreBody(KatranVoice *v, const uint32_t c,
                                                               int16_t *out, const uint32_t half)
{
	const uint32_t count = half ? KAT_HALF_N : OUTPUT_DMA_SIZE;
	const float rs = half ? 2.0f : 1.0f;
	const uint32_t other = c ^ 1u;
	int16_t *const ring = v->ring[c];
	const int16_t *const dSelf = v->blkD[c];
	const int16_t *const dOther = v->blkD[other];
	const int16_t *const blkA = v->blkA;
	const int16_t *const blkB = v->blkB;
	const float bLvl = (float)(v->bLevel & 127u) * (1.0f / 127.0f);
	const float exc = v->tExc;
	const float nse = v->tNse;
	const float xfb = v->xfb;
	const float tg = v->tGain, tg2 = v->tGain2, tube = v->tTube, twet = v->tWet;
	const float grS = rs * v->grStep, fbS = rs * v->fbStep, rkS = rs * v->rkStep;
	const float gS = half ? (rs * v->g1Step) : v->g2Step;
	KatSvf *const f1 = &v->f1[c];
	const float mH = f1->mH, mB = f1->mB, mL = f1->mL;
	const float f2k = half ? (v->f2K * (2.0f - v->f2K)) : v->f2K;
	float gr = v->gr, fb = v->fb, rk = v->rk;
	float gv = half ? v->g1 : v->g2;
	float a1 = f1->a1, a2 = f1->a2, a3 = f1->a3;
	const float da1 = f1->da1, da2 = f1->da2, da3 = f1->da3;
	float ic1 = f1->ic1, ic2 = f1->ic2;
	float f2z = v->f2x[c];
	float hPrev = v->hPrev;
	uint32_t w = v->w[c];
	uint32_t n;

	for (n = 0u; n < count; n++) {
		const uint32_t bi = half ? (n << 1) : n;
		const uint32_t oi = half ? (n << 1) : (n >> 1);
		const float D = (float)dSelf[n] * (1.0f / 32768.0f);
		const float A = (float)blkA[n] * (1.0f / 32768.0f);
		const float Bv = (float)(blkB[bi] & (int16_t)0xFFFE) * (1.0f / 32768.0f);
		const float Do = (float)dOther[oi] * (1.0f / 32768.0f);
		const float nz = (float)kat_rngRead() * (1.0f / 32768.0f);
		const float y = kat_tube(D * tg, tube);
		const float t1 = KAT_TOR_LV1 * (twet * y + (1.0f - twet) * D);
		const float e = exc * ((1.0f - nse) * A + nse * nz);
		const float t2 = KAT_TOR_LV2 * kat_cubic(tg2 * e);
		const float xin = t1 + t2;
		const float v3 = xin - ic2;
		const float v1 = a1 * ic1 + a2 * v3;
		const float v2 = ic2 + a2 * ic1 + a3 * v3;
		float yf, z, u, r, wr;

		ic1 = 2.0f * v1 - ic1;
		ic2 = 2.0f * v2 - ic2;
		yf = mH * xin + mB * v1 + mL * v2;
		z = f2z;
		u = yf - z;
		z += f2k * (yf - z);
		f2z = z;
		r = t1 * ((1.0f - rk) + rk * (bLvl * Bv));
		wr = gr * (u + xfb * Do + fb * D);
		ring[w & KAT_RING_MASK] = kat_sat16f(wr * 32768.0f);
		if (half) {
			const float o = r * (1.0f + gv);

			out[n << 1] = kat_sat16f(0.5f * (hPrev + o) * KAT_OUT_SCALE);
			out[(n << 1) + 1u] = kat_sat16f(o * KAT_OUT_SCALE);
			hPrev = o;
		} else {
			out[n] = kat_sat16f((r + gv * t1) * KAT_OUT_SCALE);
		}
		w++;
		gr += grS;
		fb += fbS;
		rk += rkS;
		gv += gS;
		a1 += da1;
		a2 += da2;
		a3 += da3;
	}
	v->w[c] = w & KAT_RING_MASK;
	f1->ic1 = ic1;
	f1->ic2 = ic2;
	v->f2x[c] = f2z;
	if (half)
		v->hPrev = hPrev;
}

/* Core L (full rate, into buf) and core R (half rate, into right[]): one
 * noinline function each (katran_cpu.py counts them by name). */
static __attribute__((noinline)) void kat_coreRenderL(KatranVoice *v, int16_t *out)
{
	kat_coreBody(v, 0u, out, 0u);
}

static __attribute__((noinline)) void kat_coreRenderH(KatranVoice *v)
{
	kat_coreBody(v, 1u, v->right, 1u);
}

/*
 * The output stage, in place on buf and right (S103 P1, P§4 L5).
 *
 * What:       per sample: L, R from the blocks (x 1/32768); F3 on each with
 *             the shared ramped coefficients (C1; the R side keeps its own
 *             integrator states f3R1/f3R2); width: m = (L + R) / 2,
 *             s = (L - R) / 2 x wid; L = m + s, R = m - s; x the CE1 gain
 *             (ramped); x KAT_OUT_NORM; SSAT16 stores.
 * Why:        one coefficient ramp for both sides (C1); the width before the
 *             level so wid 0 is true mono.
 */
static __attribute__((noinline)) void kat_outRender(KatranVoice *v, int16_t *outL)
{
	KatSvf *const f3 = &v->f3;
	const float hp = (f3->mL < 0.0f) ? 1.0f : 0.0f;
	const float da = f3->da1;
	float a = f3->a1;
	float zl = f3->ic1, zr = f3->ic2;
	const float wid = v->wid;
	const float cegS = v->cegStep;
	float ceg = v->ceg;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const float inL = (float)outL[n] * (1.0f / 32768.0f);
		const float inR = (float)v->right[n] * (1.0f / 32768.0f);
		float yl, yr, m, s;

		/* L1: the one-pole LP / HP tilt, the same coefficient on both */
		zl += a * (inL - zl);
		zr += a * (inR - zr);
		yl = zl + hp * (inL - 2.0f * zl);
		yr = zr + hp * (inR - 2.0f * zr);
		m = 0.5f * (yl + yr);
		s = 0.5f * (yl - yr) * wid;
		yl = (m + s) * ceg * KAT_OUT_NORM;
		yr = (m - s) * ceg * KAT_OUT_NORM;
		outL[n] = kat_sat16f(yl * KAT_OUT_SCALE);
		v->right[n] = kat_sat16f(yr * KAT_OUT_SCALE);
		ceg += cegS;
		a += da;
	}
	v->f3.ic1 = zl;
	v->f3.ic2 = zr;
	v->ceg = ceg;
}

/*
 * The guard's peak on every 8th sample of one read block (S103 P1, P§3.4
 * item 3): a fast attack to min(1, 0.7 / peak), released by 1 % a block.
 *
 * S103 P2: `count` is the block's valid length: 32 for OSC B and core L's
 * OSC A, 16 for core R's (half-rate) OSC A. blkA is shared by the two
 * copies, so after core R's 16-sample render, samples 16..31 still hold
 * core L's copy; reading them would let core L's level set core R's guard.
 */
static __attribute__((noinline)) void kat_guardUpdate(KatranVoice *v, const uint32_t c,
                                                      const int16_t *blk, const uint32_t count)
{
	int32_t peak = 0;
	uint32_t i;

	for (i = 0u; i < count; i += 8u) {
		const int32_t x = (c < 2u) ? (int32_t)blk[i] : (int32_t)(blk[i] & (int16_t)0xFFFE);
		const int32_t a = (x < 0) ? -x : x;

		if (a > peak)
			peak = a;
	}
	if (peak > 0) {
		float tgt = 0.7f * 32768.0f / (float)peak;

		tgt = fminf(tgt, 1.0f);
		if (tgt < v->guard[c])
			v->guard[c] = tgt;
		else
			v->guard[c] = fminf(1.0f, v->guard[c] * 1.01f);
	}
}

void Katran_initVoice(KatranVoice *voice, const uint8_t slot,
                      int16_t *ringL, int16_t *ringR)
{
	const uint8_t typ = 2u;   /* init.kat: pwm */
	const uint8_t wave = 2u;  /* init.kat: Rec */
	uint32_t i;
	uint32_t rng;

	if (!voice)
		return;
	memset(voice, 0, sizeof(*voice));
	voice->slot = slot;
	voice->ring[0] = ringL;
	voice->ring[1] = ringR;
	if (ringL)
		memset(ringL, 0, KAT_RING * sizeof(ringL[0]));
	if (ringR)
		memset(ringR, 0, KAT_RING * sizeof(ringR[0]));
	fxbuf_arenaView(&voice->arena, &voice->arenaN);

	/* the pitch anchor: typ pwm, Rec, coa 36 (32'), fin 63 */
	voice->osc.oscType = OSC_TYPE_PWM;
	voice->osc.waveform = wave;
	voice->osc.position = 46u;
	voice->osc.pitchMod = 1.0f;
	voice->osc.modNodeValue = 1.0f;
	voice->osc.midiFreq = (uint16_t)((36u << 8) | 63u);
	voice->osc.baseNote = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->osc.sampleCacheLooped = KAT_BIND_NONE;
	voice->osc.sampleCacheSize = 1u;
	voice->osc.sampleCacheData = 0;
	lfo_init(&voice->lfo);

	/* the rows of SD_CARD/Instrument/Katran/init.kat (P§11) */
	voice->spreadA = 0u;
	voice->source = KAT_SRC_OFF;
	voice->damp_ = 40u;
	voice->run = 127u;
	voice->kick = 40u;
	voice->ce1Cycle = 90u;
	voice->ce1Skew = 63u;
	voice->ce1Depth = 0u;
	voice->ce2Cycle = 100u;
	voice->ce2Skew = 63u;
	voice->ce2Depth = 63u;
	voice->chaos = 20u;
	voice->plfoRate = 74u;
	voice->plfoMod = 57u;
	voice->plfoLevel = 51u;
	voice->vca = 63u;
	voice->bCoarse = 48u;
	voice->bFine = 64u;
	voice->bPos = 60u;
	voice->bLevel = 55u;
	voice->levelA = 50u;
	voice->noise = 0u;
	voice->sync = 60u;
	voice->drive = 56u;
	voice->wet = 57u;
	voice->time = 74u;
	voice->feedback = 61u;
	voice->drift_ = 0u;
	voice->track = 0u;
	voice->loopCut = 53u;
	voice->loopRes = 30u;
	voice->loopType = KAT_LT_LP;
	voice->lowCut = 20u;
	voice->outCut = 63u;
	voice->spread = 63u;
	voice->width = 64u;
	voice->pan = 63u;
	voice->vol = 100.0f / 127.0f;
	voice->note = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->wasRunning = seq_isRunning();
	voice->armed = 0u;          /* silent until a kick or a start (Q3) */
	voice->fadeReq = 0u;
	voice->stopDecay = 0u;
	voice->rearm = 0u;
	voice->silentBlocks = 0u;
	voice->driftTick = 0u;
	voice->kE = 0.0f;
	voice->kickSwell = 0.0f;
	voice->gr = 0.0f;
	voice->grStep = 0.0f;
	voice->ringPrev = 0.0f;
	voice->plfoJit = 0.0f;
	voice->dampK = katDampK[voice->damp_ & 127u];
	voice->guard[0] = voice->guard[1] = voice->guard[2] = 1.0f;
	voice->dcX[0] = voice->dcX[1] = voice->dcX[2] = 0.0f;
	voice->dcY[0] = voice->dcY[1] = voice->dcY[2] = 0.0f;
	voice->damp[0] = voice->damp[1] = voice->damp[2] = 0.0f;
	voice->tGain = katDriveGain[voice->drive & 127u];
	voice->tGain2 = voice->tGain;
	voice->tTube = 0.25f * (float)(voice->drive & 127u) * (1.0f / 127.0f);
	voice->tWet = (float)(voice->wet & 127u) * (1.0f / 127.0f);
	voice->tExc = (float)(voice->levelA & 127u) * (1.0f / 127.0f);
	voice->tNse = (float)(voice->noise & 127u) * (1.0f / 127.0f);
	voice->xfb = (voice->spread < 63u)
	    ? KAT_XFB_MAX * (float)(63u - voice->spread) * (1.0f / 63.0f) : 0.0f;
	voice->fb = (float)(voice->feedback & 127u) * (1.0f / 127.0f) * KAT_FB_CLAMP;
	voice->fbStep = 0.0f;
	voice->rk = (voice->bLevel > 0u) ? 1.0f : 0.0f;
	voice->rkStep = 0.0f;
	voice->g1 = 0.0f;
	voice->g2 = 0.0f;
	voice->g1Step = 0.0f;
	voice->g2Step = 0.0f;
	voice->ceg = 1.0f;
	voice->cegStep = 0.0f;
	voice->wid = (float)(voice->width & 127u) * (1.0f / 64.0f);
	voice->f2K = katLowCutK[voice->lowCut & 127u];
	voice->f2x[0] = voice->f2x[1] = 0.0f;
	voice->f2y[0] = voice->f2y[1] = 0.0f;
	/* S103 P2: core R's delay time is in half-rate samples (the same comb
	 * pitch as core L's; kat_delayControl()), and its interpolation state
	 * starts at silence */
	voice->tNow[0] = katTimeSamples[voice->time & 127u];
	voice->tNow[1] = 0.5f * katTimeSamples[voice->time & 127u];
	voice->tStep[0] = voice->tStep[1] = 0.0f;
	voice->drift[0] = voice->drift[1] = 0.0f;
	voice->driftTgt[0] = voice->driftTgt[1] = 0.0f;
	voice->w[0] = voice->w[1] = 0u;
	voice->syncPrev[0] = voice->syncPrev[1] = -1;
	voice->hPrev = 0.0f;

	/* S103 P2: core L's loop filter at the full rate, core R's at half rate
	 * (its prewarp and its 8.1 kHz ceiling, KAT_CUT_ST_MAX_H) */
	kat_svfInit(&voice->f1[0], (float)(voice->loopCut & 127u) * (KAT_CUT_ST_MAX / 127.0f),
	            (float)(voice->loopRes & 127u) * (1.0f / 127.0f), KAT_LT_LP,
	            KAT_PI_OVER_FS, KAT_CUT_ST_MAX);
	kat_svfInit(&voice->f1[1], (float)(voice->loopCut & 127u) * (KAT_CUT_ST_MAX / 127.0f),
	            (float)(voice->loopRes & 127u) * (1.0f / 127.0f), KAT_LT_LP,
	            KAT_PI_OVER_FS_H, KAT_CUT_ST_MAX_H);
	/* L1 (the CPU ladder): F3 is a one-pole LP/HP tilt, one coefficient set
	 * for both sides; a = 1 (open) at out_cut 63 */
	memset(&voice->f3, 0, sizeof(voice->f3));
	voice->f3.cutSt = KAT_CUT_ST_MAX;
	voice->f3.a1 = 1.0f;
	voice->f3.mL = 1.0f;

	/* the rows the loops read before the first control */
	voice->rowA[0] = voice->rowANext[0] = sawTable[1];
	voice->rowA[1] = voice->rowANext[1] = sawTable[1];
	voice->rowB = voice->rowBNext = sawTable[1];
	for (i = 0u; i < 3u; i++) {
		voice->pwPos[i] = (uint8_t)((i == 2u) ? voice->bPos : voice->osc.position);
		voice->pwPos0[i] = voice->pwPos[i];
		voice->pwD[i] = oscPwmDRec[voice->pwPos[i] & 127u];
		voice->pwD0[i] = voice->pwD[i];
	}

	/* free-running phases and the per-voice draws, salted by the slot */
	rng = kat_rngSeed(((uint32_t)slot << 24) ^ (uint32_t)(uintptr_t)voice);
	voice->rng = rng ? rng : 0x9E3779B9u;
	voice->phA[0] = kat_rngNext(&voice->rng);
	voice->phA[1] = kat_rngNext(&voice->rng);
	voice->phB = kat_rngNext(&voice->rng);
	voice->incA[0] = 0u;
	voice->incA[1] = 0u;
	voice->incB = 0u;
	voice->lfoPh = (float)(kat_rngNext(&voice->rng) >> 8) * (1.0f / 16777216.0f);
	voice->cv = 0.0f;
	kat_ceStart(voice, 0u);
	kat_ceStart(voice, 1u);
	voice->ce[0].level = voice->ce[0].peak;   /* the two never start aligned */
	voice->ce[0].stage = 1u;
	voice->ce[1].level = 0.0f;
	voice->ce[1].stage = 0u;
	voice->ceg = 1.0f - (float)(voice->ce1Depth & 127u) * (1.0f / 127.0f)
	           + (float)(voice->ce1Depth & 127u) * (1.0f / 127.0f) * voice->ce[0].level;
	voice->cegStep = 0.0f;
	voice->tGain = katDriveGain[voice->drive & 127u];
	(void)typ;
}

/*
 * The block's control (S103 P1). Contract: KatranVoice.h.
 *
 * What:       in order: the transport edges and kat_runControl();
 *             kat_kickControl(); kat_plfoStep(); kat_ceStep() x2;
 *             the wtb bind check (kat_wtbBind() when the key changed);
 *             kat_oscControl(); kat_delayControl(); F1: per core st =
 *             (tracked types: the loop pitch in semitones + frq' x 48;
 *             absolute: frq' x 115.7) + CE2 (dp2 bipolar) x 48 + cv x lfl x 12
 *             + (core L +, R -) spread x 12, res, type % 3 ->
 *             kat_svfControl(); F2: f2K = katLowCutK[lct]; F3: one
 *             kat_svfControl() for c = (cut - 63) / 63: c < 0 LP at
 *             115.7 + c x 77 semitones, c > 0 HP at c x 80 semitones, c = 0 LP
 *             fully open; res fixed 0.35; + CE2 x 24; TORCIDO: tGain =
 *             katDriveGain[drv], tTube = 0.25 drv' + 0.15 cv x lfl', tWet =
 *             wet', tGain2 = tGain; the feedback target fb = min(fbk' x 0.95 +
 *             0.2 x inv x lfl' + swell, KAT_FB_CLAMP) with xfb from spr below
 *             63, the sum clamped at 1.1; the ring crossfade target rk =
 *             (bLevel > 0); the VCA targets g1 = min(1, 0.67 (1 + v)) x
 *             clamp(0.42 + cv, 0, 1), g2 = min(1, 0.38 (1 - v)) x
 *             clamp(0.42 + inv, 0, 1), v = (vca - 63) / 63; the CE1 gain
 *             target 1 - dp1' + dp1' x CE1.level; wid = width / 64; the arena
 *             damp coefficient katDampK[dmp]. Every gain gets now/step for
 *             the per-sample ramp.
 * Why:        every per-sample loop reads only ramps and coefficients; all
 *             laws are per block (constant CPU: no branch here changes what
 *             the loops cost).
 */
void Katran_calcVoiceAsync(KatranVoice *voice)
{
	OscInfo *const osc = &voice->osc;
	const uint8_t isWtb = (uint8_t)(osc->oscType == OSC_TYPE_WTB);
	const float lfl = (float)(voice->plfoLevel & 127u) * (1.0f / 127.0f);
	const float inv = -voice->cv;
	const float dp2 = ((float)(voice->ce2Depth & 127u) - 63.0f) * (1.0f / 63.0f);
	const float sp = (voice->spread > 63u)
	    ? ((float)(voice->spread - 63u) * (1.0f / 64.0f)) : 0.0f;
	const float vcaN = ((float)(voice->vca & 127u) - 63.0f) * (1.0f / 63.0f);
	const float dp1 = (float)(voice->ce1Depth & 127u) * (1.0f / 127.0f);
	float loopSt[2];
	float fbT, g1T, g2T, cegT;
	uint32_t c;

	kat_runControl(voice);
	kat_kickControl(voice);
	kat_plfoStep(voice);
	kat_ceStep(voice, 0u);
	kat_ceStep(voice, 1u);
	if (isWtb && kat_wtbKeyChanged(osc))
		kat_wtbBind(voice);
	kat_oscControl(voice);
	kat_delayControl(voice, loopSt);

	for (c = 0u; c < 2u; c++) {
		const uint8_t type = (uint8_t)(voice->loopType % KAT_LT_COUNT);
		float st;

		if (type >= KAT_LT_LPT)
			st = loopSt[c] + (float)(voice->loopCut & 127u) * (48.0f / 127.0f);
		else
			st = (float)(voice->loopCut & 127u) * (KAT_CUT_ST_MAX / 127.0f);
		st += dp2 * 48.0f + voice->cv * lfl * 12.0f
		    + ((c == 0u) ? sp : -sp) * KAT_SPREAD_F1_ST;
		/* S103 P2: core L at the full rate (32-sample ramp), core R at half
		 * rate (its prewarp, its 8.1 kHz ceiling, a 16-sample ramp) */
		kat_svfControl(&voice->f1[c], st,
		               (float)(voice->loopRes & 127u) * (1.0f / 127.0f),
		               (uint8_t)(type % 3u),
		               (c == 0u) ? KAT_PI_OVER_FS : KAT_PI_OVER_FS_H,
		               (c == 0u) ? KAT_CUT_ST_MAX : KAT_CUT_ST_MAX_H,
		               (c == 0u) ? OUTPUT_DMA_SIZE : KAT_HALF_N);
	}
	voice->f2K = katLowCutK[voice->lowCut & 127u];
	{
		const float cf = ((float)(voice->outCut & 127u) - 63.0f) * (1.0f / 63.0f);
		float st;
		float g, a;
		uint8_t open = 0u;

		if (cf < 0.0f) {
			st = KAT_CUT_ST_MAX + cf * 77.0f;
			voice->f3.mL = 1.0f;    /* LP */
		} else if (cf > 0.0f) {
			st = cf * 80.0f;
			voice->f3.mL = -1.0f;   /* HP */
		} else {
			st = KAT_CUT_ST_MAX;
			voice->f3.mL = 1.0f;    /* open: a = 1 passes exactly */
			open = 1u;
		}
		st += dp2 * 24.0f;
		st = fminf(fmaxf(st, 0.0f), KAT_CUT_ST_MAX);
		voice->f3.cutSt += KAT_SLEW * (st - voice->f3.cutSt);
		g = kat_tanf(KAT_CUT_BASE_HZ * alk_exp2f(voice->f3.cutSt * (1.0f / 12.0f))
		             * KAT_PI_OVER_FS);
		a = open ? 1.0f : g / (1.0f + g);
		voice->f3.a1 += (float)OUTPUT_DMA_SIZE * voice->f3.da1;
		voice->f3.da1 = (a - voice->f3.a1) * (1.0f / (float)OUTPUT_DMA_SIZE);
	}

	voice->tGain = katDriveGain[voice->drive & 127u];
	voice->tGain2 = voice->tGain;
	voice->tTube = 0.25f * (float)(voice->drive & 127u) * (1.0f / 127.0f)
	             + 0.15f * voice->cv * lfl;
	voice->tWet = (float)(voice->wet & 127u) * (1.0f / 127.0f);
	voice->xfb = (voice->spread < 63u)
	    ? KAT_XFB_MAX * (float)(63u - voice->spread) * (1.0f / 63.0f) : 0.0f;
	fbT = (float)(voice->feedback & 127u) * (1.0f / 127.0f) * KAT_FB_CLAMP
	    + 0.2f * inv * lfl + voice->kickSwell;
	fbT = fminf(fbT, KAT_FB_CLAMP);
	if (fbT < 0.0f)
		fbT = 0.0f;
	if (fbT + voice->xfb > KAT_FB_CLAMP_SUM)
		fbT = fmaxf(0.0f, KAT_FB_CLAMP_SUM - voice->xfb);
	voice->fbStep = (fbT - voice->fb) * (1.0f / (float)OUTPUT_DMA_SIZE);
	voice->rkStep = (((voice->bLevel > 0u) ? 1.0f : 0.0f) - voice->rk)
	              * (1.0f / (float)OUTPUT_DMA_SIZE);
	g1T = fminf(1.0f, 0.67f * (1.0f + vcaN)) * kat_clamp01(0.42f + voice->cv);
	g2T = fminf(1.0f, 0.38f * (1.0f - vcaN)) * kat_clamp01(0.42f + inv);
	voice->g1Step = (g1T - voice->g1) * (1.0f / (float)OUTPUT_DMA_SIZE);
	voice->g2Step = (g2T - voice->g2) * (1.0f / (float)OUTPUT_DMA_SIZE);
	cegT = 1.0f - dp1 + dp1 * voice->ce[0].level;
	voice->cegStep = (cegT - voice->ceg) * (1.0f / (float)OUTPUT_DMA_SIZE);
	voice->wid = (float)(voice->width & 127u) * (1.0f / 64.0f);
	voice->dampK = katDampK[voice->damp_ & 127u];
}

/*
 * The block's audio (S103 P1; S103 P2: core L full rate, core R half rate).
 * Contract: KatranVoice.h.
 *
 * What:       no ring (a unit was not granted): memset buf and right, return.
 *             Otherwise, in order:
 *             1. kat_delayReadL() (32 samples into blkD[0]) and
 *                kat_delayReadH() (16 half-rate samples into blkD[1]): both
 *                before any ring write, so each core's cross-feedback can read
 *                the other's output;
 *             2. OSC B's loop for its form into blkB (full rate), its guard;
 *             3. core L: OSC A's full-rate copy (kat_a*L) into blkA, then
 *                kat_coreRenderL() into buf, then its guard (32 samples);
 *             4. core R: OSC A's half-rate copy (kat_a*H) into blkA[0..15],
 *                then kat_coreRenderH() into right[] (interpolated to the
 *                full rate), then its guard (16 samples);
 *             5. the shared ramps' ends; ringPrev from each core's last
 *                sample (blkD[0][31], blkD[1][15]); kat_outRender().
 * Why:        the staged order of D§4; every loop runs every block
 *             (constant CPU); the form only picks which equal-cost loop.
 *             S103 P2: core R's loops are separate half-rate functions
 *             (KatranOsc.h, kat_coreRenderH()), not the full-rate ones with a
 *             core index.
 */
void Katran_calcVoiceSyncBlock(KatranVoice *voice, int16_t *buf, uint8_t size)
{
	const uint8_t src = (uint8_t)(voice->source & 3u);
	const uint8_t isPwm = (uint8_t)(voice->osc.oscType == OSC_TYPE_PWM);
	const uint8_t wave = voice->osc.waveform;
	const uint8_t pwmForm = (wave == OSC_PWM_TRI) ? KAT_FORM_WARP : KAT_FORM_PWMD;
	const uint8_t tableForm = isPwm ? pwmForm : KAT_FORM_PLAIN;
	const uint8_t bArena = (uint8_t)(src == KAT_SRC_B || src == KAT_SRC_AB);
	const uint8_t aArena = (uint8_t)(src == KAT_SRC_A || src == KAT_SRC_AB);
	const int32_t sign = (wave == OSC_PWM_SAW) ? 1 : -1;
	const int32_t th = ((voice->sync & 127u) >= 127u)
	    ? 0x7FFFFFFF : (int32_t)(voice->sync & 127u) * 258;

	(void)size;
	if (!voice->ring[0] || !voice->ring[1]) {
		memset(buf, 0, OUTPUT_DMA_SIZE * sizeof(buf[0]));
		memset(voice->right, 0, sizeof(voice->right));
		return;
	}
	kat_delayReadL(voice);
	kat_delayReadH(voice);

	if (bArena) {
		kat_bArena(voice);
	} else if (tableForm == KAT_FORM_WARP) {
		kat_bWarp(voice);
	} else if (tableForm == KAT_FORM_PWMD) {
		kat_bPwmD(voice, sign);
	} else {
		kat_bPlain(voice);
	}
	kat_guardUpdate(voice, 2u, voice->blkB, OUTPUT_DMA_SIZE);

	/* core L: full rate */
	if (aArena)
		kat_aArenaL(voice, th);
	else if (tableForm == KAT_FORM_WARP)
		kat_aWarpL(voice, th);
	else if (tableForm == KAT_FORM_PWMD)
		kat_aPwmDL(voice, th, sign);
	else
		kat_aPlainL(voice, th);
	kat_coreRenderL(voice, buf);
	kat_guardUpdate(voice, 0u, voice->blkA, OUTPUT_DMA_SIZE);

	/* core R: half rate */
	if (aArena)
		kat_aArenaH(voice, th);
	else if (tableForm == KAT_FORM_WARP)
		kat_aWarpH(voice, th);
	else if (tableForm == KAT_FORM_PWMD)
		kat_aPwmDH(voice, th, sign);
	else
		kat_aPlainH(voice, th);
	kat_coreRenderH(voice);
	kat_guardUpdate(voice, 1u, voice->blkA, KAT_HALF_N);

	/* the block's shared ramps reach their ends for the next block */
	voice->gr += (float)OUTPUT_DMA_SIZE * voice->grStep;
	voice->fb += (float)OUTPUT_DMA_SIZE * voice->fbStep;
	voice->rk += (float)OUTPUT_DMA_SIZE * voice->rkStep;
	voice->g1 += (float)OUTPUT_DMA_SIZE * voice->g1Step;
	voice->g2 += (float)OUTPUT_DMA_SIZE * voice->g2Step;
	if (voice->gr > 1.0f)
		voice->gr = 1.0f;
	if (voice->gr < 0.0f)
		voice->gr = 0.0f;
	/* S103 P2: core R's block is 16 half-rate samples; its last is [15] */
	voice->ringPrev = 0.5f * (fabsf((float)voice->blkD[0][OUTPUT_DMA_SIZE - 1u])
	                        + fabsf((float)voice->blkD[1][KAT_HALF_N - 1u]))
	                * (1.0f / 32768.0f);
	kat_outRender(voice, buf);
}

/*
 * Quiet, the fade request, the type writer (S103 P1).
 *
 * Katran_isQuiet():     silentBlocks >= KAT_QUIET_BLOCKS: gr has been 0
 *                       for longer than one ring, so every delay holds zeros
 *                       and the output is silent.
 * Katran_requestFade(): fadeReq = KAT_FADE_HOLD. Called by
 *                       instrumentManager_ampEnvelopeQuiet() while a deferred
 *                       apply polls a non-quiet Katran (Q9); the request lapses
 *                       if the polls stop (the apply was cancelled) and the
 *                       voice returns to its run state.
 * Katran_setOscType():  Colloid_setOscType()'s shape (ColloidVoice.c:2194):
 *                       byte 0 wav, 1 wtb, 2 pwm -> OSC_TYPE_WAV / _WTB / _PWM;
 *                       clamp osc.waveform to the new list (wav: 4, pwm: the
 *                       platform's three, wtb: the installed tables); drop
 *                       the bind.
 */
uint8_t Katran_isQuiet(const KatranVoice *voice)
{
	return (uint8_t)(voice && voice->silentBlocks >= KAT_QUIET_BLOCKS);
}

void Katran_requestFade(KatranVoice *voice)
{
	if (voice)
		voice->fadeReq = KAT_FADE_HOLD;
}

void Katran_triggerVoice(KatranVoice *voice, const uint8_t source_slot,
                         const uint8_t velocity, const uint8_t note)
{
	const float vel = (float)velocity * (1.0f / 127.0f);

	if (!voice)
		return;
	lfo_retrigger(source_slot);
	modNode_updateValue(&velocityModulators[source_slot], vel);
	instrumentManager_applyVelocityModulationTarget(source_slot, vel);
	voice->note = note;
	voice->armed = 1u;
	voice->stopDecay = 0u;
	voice->rearm = KAT_KICK_REARM;
	voice->kE = vel;
}

void Katran_setOscType(KatranVoice *voice, const uint8_t typ)
{
	OscInfo *osc;
	const uint8_t type = (typ == 0u) ? OSC_TYPE_WAV
	                   : ((typ == 1u) ? OSC_TYPE_WTB : OSC_TYPE_PWM);
	const uint8_t count = (type == OSC_TYPE_WTB) ? sampleMemory_getNumWavetables()
	                    : ((type == OSC_TYPE_PWM) ? (uint8_t)OSC_PWM_WAVE_COUNT
	                                              : (uint8_t)4u);
	const uint8_t last = count ? (uint8_t)(count - 1u) : 0u;

	if (!voice)
		return;
	osc = &voice->osc;
	if (type == osc->oscType)
		return;
	osc->oscType = type;
	if (osc->waveform > last)
		osc->waveform = last;
	osc->sampleCacheLooped = KAT_BIND_NONE;
}
