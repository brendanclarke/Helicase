#include <math.h>
#include <string.h>
#include "TetraktVoice.h"
#include "TetraktTables.h"
#include "ColloidTables.h"      /* colEgK[]: the decay's per-block one-pole (shared, plan §1) */
#include "AlkalineControl.h"    /* alk_exp2f() (S093 P3 fast maths) */
#include "squareRootLut.h"      /* squareRootLut[]: the mixer's pan law */
#include "MidiPitch.h"          /* MIDI_DEFAULT_TRIGGER_NOTE */
#include "modulationNode.h"     /* velocityModulators[], modNode_updateValue() */
#include "InstrumentManager.h"  /* instrumentManager_applyVelocityModulationTarget() */
#include "SampleMemory.h"       /* sampleMemory_sampleEntry(), _gainFromCode(): the bind gain (§19) */

/*
 * Control constants (S096 P1). The "by ear" values (TKT_MIX_SCALE, the tone
 * damping and ranges in gen_tkt_tables.py, TKT_BIAS_MAX) are first
 * settings, set in the listening step (S12) and recorded there.
 *
 * TKT_QUIET           a decay below 1e-4 (-80 dB) ends (the
 *                     ampEnvelopeQuiet() threshold, as the Colloid's).
 * TKT_IN_SCALE        int16 read -> float.
 * TKT_OUT_SCALE       float -> int16 pack.
 * TKT_UNITY_INC       Q19.12: 4,096 is one frame per sample (1.0).
 * TKT_UNITY_HZ        the frequency that osc_userBind()'s sample law
 *                     (f x 2^22 / fs) turns into 4,096: fs / 1,024. Set so
 *                     the bind's own phaseInc is already close; the exact
 *                     one is written after it (SD5).
 * TKT_MIX_SCALE       each layer's post gain x0.5 (SD8).
 * TKT_SVF_K           the tone SVF's damping 2R, R = 0.6 (plan T-Q5).
 * TKT_TONE_W_SLOPE    the output weights rise 0 -> 1 over the first eighth
 *                     of either side (plan §5.3).
 * TKT_CLIP, TKT_CUBIC the drive's clamped cubic (the Colloid's col_drive()
 *                     law): exactly +-1.0 at +-1.5.
 * TKT_DRIVE_OCT       drive 0..127 -> gain 2^(5 d / 127): x1..x32.
 * TKT_BIAS_MAX        the asymmetry bias at full drive (plan T-Q6).
 * TKT_WET_STEPS       drive 0 is the dry signal exactly; the clip fades in
 *                     over the first 8 steps (COL_POST_WET_STEPS' rule).
 * TKT_DC_R            the bus DC blocker, a one-pole HP at 8 Hz:
 *                     1 - 2 pi 8 / fs.
 * TKT_SDT_SPAN        sdt 127 puts the right tap 50 % earlier (S096 P2 F4;
 *                     was 15 %).
 * TKT_BUS_AP          W2's one allpass coefficient on the summed right bus:
 *                     -0.6 puts its 90 degree point at 3.4 kHz (-32 degrees
 *                     at 1 kHz, -6.5 at 200 Hz, so the lows stay centred and
 *                     a mono sum loses little). S096 P2 F4: it was +0.4,
 *                     whose 90 degree point is 16 kHz (-3.5 degrees at
 *                     1 kHz), so the width barely reached the audible band.
 * TKT_FB_MAX          sfb 127 -> loop gain 0.97 (S096 P2 F4; was 0.9;
 *                     below 1 with the loop's one-pole, so bounded; the write
 *                     saturates as well).
 * TKT_VELO_SPAN       velocity offsets are in row bytes: the row's 127.
 * TKT_RING            S1's ring (S096 P1 §17): the unit's samples 1 ..
 *                     2,207; sample 0 is the guard, a copy of the ring's
 *                     last sample, so a tap's pair [a - 1], [a] never
 *                     wraps.
 * TKT_TAP_MAX         the longest tap delay: a read lags at most
 *                     TKT_RING - 1 samples (floor(tap) + 1), so it never
 *                     reaches the sample this frame writes. 50 ms at
 *                     REAL_FS (44,108 Hz) is 2,205.4, under the bound.
 */
#define TKT_QUIET           1.0e-4f
#define TKT_IN_SCALE        (1.0f / 32768.0f)
#define TKT_OUT_SCALE       32767.0f
#define TKT_UNITY_INC       4096.0f
#define TKT_UNITY_HZ        (REAL_FS / 1024.0f)
#define TKT_MIX_SCALE       0.5f
#define TKT_SVF_K           1.2f
#define TKT_TONE_W_SLOPE    8.0f
#define TKT_CLIP            1.5f
#define TKT_CUBIC           (4.0f / 27.0f)
#define TKT_DRIVE_OCT       5.0f
#define TKT_BIAS_MAX        0.25f
#define TKT_WET_STEPS       8u
#define TKT_DC_R            0.998860393f
#define TKT_SDT_SPAN        0.5f
#define TKT_FB_MAX          0.97f
#define TKT_VELO_SPAN       127.0f
#define TKT_BUS_AP          (-0.6f)
#define TKT_RING            (TKT_LINE_SAMPLES - 1u)
#define TKT_TAP_MAX         ((float)(TKT_RING - 2u) + 0.999f)

/*
 * The shared work area (S096 P1; plan §10, T-Q1: approved).
 *
 * What:       the float L/R buses and the send bus, one copy for every
 *             Tetrakt: 384 B of .bss in SRAM1 (the linker's .bss, D-cached
 *             since S094 P2). The int16 read scratch of S096 P1 S3 (64 B)
 *             is gone: each layer reads its sample inside its post loop
 *             (S096 P1 §19), so the approved 448 B shrank.
 * Why:        the mixer renders one slot at a time (calcSlotAsync and
 *             calcSlotSyncBlock in turn), so no two Tetrakts use the area
 *             at once; inside the voice struct the buses would put it over
 *             the 1,264 B slot reserve.
 * Contract:   valid only inside one Tetrakt_calcVoiceSyncBlock() call;
 *             nothing is kept in it between blocks.
 */
typedef struct {
	float   busL[OUTPUT_DMA_SIZE];
	float   busR[OUTPUT_DMA_SIZE];
	float   send[OUTPUT_DMA_SIZE];
} TktWork;
_Static_assert(sizeof(TktWork) == 384u, "the Tetrakt work area is 384 B (448 B approved)");
static TktWork tkt_work;

/* The drive's clamped cubic (TKT_CLIP, TKT_CUBIC). */
static inline float tkt_clip(float x)
{
	x = fminf(fmaxf(x, -TKT_CLIP), TKT_CLIP);
	return x - TKT_CUBIC * x * x * x;
}

/* A float to int16 with saturation (vminnm / vmaxnm under -Ofast). */
static inline int16_t tkt_sat16(float x)
{
	return (int16_t)fminf(fmaxf(x, -32768.0f), 32767.0f);
}

/*
 * A row byte with a velocity offset (S096 P1 SD3, SD4).
 *
 * What:       b - off clamped to 0..127, as a float (the tone uses the
 *             fraction).
 * Why:        the platform's negative-polarity velocity law
 *             (modNode_updateValue(): base - amount (1 - v) width) in the
 *             row's own 0..127 domain; off is 0 for every row the velocity
 *             group does not name.
 */
static inline float tkt_eff(const uint8_t b, const float off)
{
	return fminf(fmaxf((float)b - off, 0.0f), 127.0f);
}

/* A row float 0..127 to a table index 0..127. */
static inline uint32_t tkt_idx(const float v)
{
	const uint32_t i = (uint32_t)(v + 0.5f);
	return (i > 127u) ? 127u : i;
}

/*
 * One ramp (S096 P1, the Colloid's ColSvf rule).
 *
 * What:       the next block's ramp from the last block's end (a + 32 da)
 *             to target; at init the ramp starts on its target (da 0).
 * Why:        coefficient and weight changes are spread over the block, so
 *             an envelope-swept tone does not zipper.
 */
static inline void tkt_ramp(float *a, float *da, const float target, const uint32_t init)
{
	const float start = init ? target : (*a + (float)OUTPUT_DMA_SIZE * *da);

	*a = start;
	*da = init ? 0.0f : (target - start) * (1.0f / (float)OUTPUT_DMA_SIZE);
}

/*
 * Bind one layer's catalogue item (S096 P1 SD5).
 *
 * What:       osc_userBind() on the layer's oscillator, then phase 0 for a
 *             restart; then bindGain, the bound sample's catalogue gain
 *             (1.0 with nothing bound), which the post loop's read applies
 *             (S096 P1 §19: osc_bindGain()'s law, read once per bind
 *             instead of once per block).
 * Why:        the bind is 96 instructions; it runs at init, at a START and
 *             after a catalogue install (generation change), never on a
 *             plain block. Keeping it out of line keeps the per-block
 *             control path (tkt_layerControl) free of it.
 * Accessors:  tkt_layerControl().
 */
static __attribute__((noinline)) void tkt_bindLayer(TktLayer *L, const uint32_t restart)
{
	const sample_catalogue_entry_t *entry;

	osc_userBind(&L->osc);
	if (restart)
		L->osc.phase = 0u;
	entry = (L->osc.sampleCacheLooped == OSC_BIND_SMP) ?
		sampleMemory_sampleEntry(L->osc.sampleCacheIndex) : NULL;
	L->bindGain = entry ? sampleMemory_gainFromCode(entry->gain_code) : 1.0f;
}

/*
 * One layer's control for this block (S096 P1; plan §2, §3, §5, §6.2).
 *
 * What:       in order:
 *             1. a START latches the pending sample and velocity and sets
 *                the restart flag;
 *             2. the velocity offset of the voice's group (SD3) from this
 *                layer's latched velocity;
 *             3. the stage's envelope: START plays from 1.0; FADE ramps to
 *                0 and becomes START; PLAY decays by colEgK[decay] and ends
 *                below TKT_QUIET; IDLE stays 0;
 *             4. the pitch: semitones = (note - 63) + clamp(tune - 63,
 *                +-60) / 5 (plan §5.1: a whole semitone every 5 steps, +-12
 *                from +-60 to the ends); rate = 2^(semitones / 12);
 *                osc_userBind() binds the sample (or rebinds after a
 *                catalogue install), then phaseInc = round(rate x 4,096) and
 *                a restart sets the phase to 0 (SD5);
 *             5. the tone (plan §5.3): t = tone + (toneEnv - 63) x env,
 *                0..126; g from tktToneG[] (interpolated); a1 = 1 / (1 +
 *                g (g + k)), a2 = g a1, a3 = g a2; u = (t - 63) / 63, w =
 *                min(8 |u|, 1); mX = 1 - wL, mB = -wH k, mL = wL - wH (so y
 *                = x exactly at u = 0, the LP at wL = 1, the HP at wH = 1);
 *                each ramped from the last block's end;
 *             6. the drive (plan §5.4): gain 2^(5 d / 127), bias 0.25 (d /
 *                127)^2, clip(bias), wet over the first 8 steps; make-up
 *                tktDriveMakeup[d];
 *             7. the gains: post = level / 127 x make-up x TKT_MIX_SCALE;
 *                gL = squareRootLut[127 - pan] x post, gR =
 *                squareRootLut[pan] x post (the mixer's law); gS = space /
 *                127 x post;
 *             8. the SRR rate tktSrrRate[srr] (127 = 1.0 exactly).
 * Why:        all per block; the per-sample loop only reads, filters and
 *             adds (constant cost: every layer runs this every block).
 * Inputs:     voice (rows, note, veloKind, slot, width), i (the layer),
 *             init (1 from Tetrakt_initVoice(): no stage step, ramps start
 *             on their targets), bind (1 on init, a START and a catalogue
 *             generation change: osc_userBind() runs; a plain block only
 *             rewrites phaseInc, so the 96-instruction bind is not paid
 *             every block per layer, SD5).
 * Outputs:    the layer's state and per-block values.
 * Cost:       one alk_exp2f() for the pitch and one for the drive gain, one
 *             division (a1), table reads; noinline keeps it out of the
 *             async's register allocation.
 * Accessors:  Tetrakt_calcVoiceAsync(), Tetrakt_initVoice().
 */
static __attribute__((noinline)) void tkt_layerControl(TetraktVoice *voice, const uint32_t i,
                                                        const uint32_t init,
                                                        const uint32_t bind)
{
	TktLayer *const L = &voice->layer[i];
	const uint8_t kind = voice->veloKind;
	uint32_t restart = 0u;
	float off, tune, level, decay, pan, drive, srr, space, tone, tenv;

	/* 1. a START latches what the retrigger asked for */
	if (!init && L->stage == TKT_STAGE_START) {
		L->osc.waveform = L->pendSample;
		L->velo = (float)L->pendVel * (1.0f / 127.0f);
		restart = 1u;
	}
	/*
	 * 2. the velocity group's offset for this layer (SD3). Only the named
	 * group is clamped; the other eight rows are stored 0..127 already, so
	 * the block does not pay eight unused clamps.
	 */
	tune  = (float)L->tune;
	level = (float)L->level;
	decay = (float)L->decay;
	pan   = (float)L->pan;
	drive = (float)L->drive;
	srr   = (float)L->srr;
	space = (float)L->space;
	tone  = (float)L->tone;
	tenv  = (float)L->toneEnv;
	if (kind < TKT_KIND_COUNT) {
		off = velocityModulators[voice->slot].amount * (1.0f - L->velo) * TKT_VELO_SPAN;
		switch (kind) {
		case TKT_KIND_TUNE:     tune  = tkt_eff(L->tune, off);    break;
		case TKT_KIND_LEVEL:    level = tkt_eff(L->level, off);   break;
		case TKT_KIND_DECAY:    decay = tkt_eff(L->decay, off);   break;
		case TKT_KIND_PAN:      pan   = tkt_eff(L->pan, off);     break;
		case TKT_KIND_DRIVE:    drive = tkt_eff(L->drive, off);   break;
		case TKT_KIND_SRR:      srr   = tkt_eff(L->srr, off);     break;
		case TKT_KIND_SPACE:    space = tkt_eff(L->space, off);   break;
		case TKT_KIND_TONE:     tone  = tkt_eff(L->tone, off);    break;
		case TKT_KIND_TONE_ENV: tenv  = tkt_eff(L->toneEnv, off); break;
		default: break;
		}
	}

	/* 3. the stage's envelope (SD9) */
	if (!init) {
		const float k = colEgK[tkt_idx(decay)];

		switch (L->stage) {
		case TKT_STAGE_START:
			L->envLast = 1.0f;
			L->env = 1.0f - k;
			L->stage = TKT_STAGE_PLAY;
			break;
		case TKT_STAGE_FADE:
			L->envLast = L->env;
			L->env = 0.0f;
			L->stage = TKT_STAGE_START;
			break;
		case TKT_STAGE_PLAY:
			L->envLast = L->env;
			L->env -= k * L->env;
			if (L->env < TKT_QUIET) {
				L->env = 0.0f;
				L->stage = TKT_STAGE_IDLE;
			}
			break;
		default:
			L->envLast = 0.0f;
			L->env = 0.0f;
			break;
		}
	}

	/* 4. the pitch and the bind (SD5) */
	{
		const float d = fminf(fmaxf(tune - 63.0f, -60.0f), 60.0f);
		const float semis = (float)((int32_t)voice->note - (int32_t)MIDI_DEFAULT_TRIGGER_NOTE)
		                  + d * 0.2f;
		const float rate = alk_exp2f(semis * (1.0f / 12.0f));

		L->osc.freq = TKT_UNITY_HZ * rate;
		if (bind || restart)
			tkt_bindLayer(L, restart);
		L->osc.phaseInc = (uint32_t)(rate * TKT_UNITY_INC + 0.5f);
	}

	/* 5. the tone (plan §5.3) */
	{
		const float t = fminf(fmaxf(tone + (tenv - 63.0f) * L->env, 0.0f), 126.0f);
		const uint32_t ti = (uint32_t)t;
		const uint32_t tn = (ti < (TKT_TONE_POINTS - 1u)) ? ti + 1u : ti;
		const float g = tktToneG[ti] + (t - (float)ti) * (tktToneG[tn] - tktToneG[ti]);
		const float u = (t - 63.0f) * (1.0f / 63.0f);
		const float w = fminf(fabsf(u) * TKT_TONE_W_SLOPE, 1.0f);
		const float wL = (u < 0.0f) ? w : 0.0f;
		const float wH = (u > 0.0f) ? w : 0.0f;
		const float a1 = 1.0f / (1.0f + g * (g + TKT_SVF_K));
		const float a2 = g * a1;

		tkt_ramp(&L->a1, &L->da1, a1, init);
		tkt_ramp(&L->a2, &L->da2, a2, init);
		tkt_ramp(&L->a3, &L->da3, g * a2, init);
		/* SD12 trim 2: the output weights change per block (the
		 * coefficients still ramp); three fewer adds per sample per
		 * layer, and the sweep stays continuous because both weights
		 * are 0 at u = 0. */
		L->mX = 1.0f - wL;
		L->mB = -wH * TKT_SVF_K;
		L->mL = wL - wH;
		L->dmX = L->dmB = L->dmL = 0.0f;
	}

	/* 6.-8. drive, gains, SRR, W2 */
	{
		const uint32_t di = tkt_idx(drive);
		const float dn = drive * (1.0f / 127.0f);
		const float post = level * (1.0f / 127.0f) * tktDriveMakeup[di] * TKT_MIX_SCALE;
		const uint32_t pi = tkt_idx(pan);

		L->dGain = alk_exp2f(TKT_DRIVE_OCT * dn);
		L->dBias = TKT_BIAS_MAX * dn * dn;
		L->dClipB = tkt_clip(L->dBias);
		L->dWet = (di >= TKT_WET_STEPS) ? 1.0f : (float)di * (1.0f / (float)TKT_WET_STEPS);
		L->gL = squareRootLut[127u - pi] * post;
		L->gR = squareRootLut[pi] * post;
		L->gS = space * (1.0f / 127.0f) * post;
		L->srrRate = tktSrrRate[tkt_idx(srr)];
	}
}

/*
 * The space stage's control for this block (S096 P1, plan §6.1).
 *
 * What:       tapL = (1 + 49 stm / 127) ms in samples (1 .. 50 ms); tapR =
 *             tapL x (1 - 0.15 sdt / 127), at least 2 samples; fb = 0.9 sfb /
 *             127; fbK = tktFbLpK[sto]; slvGain = slv / 127. The wobble is
 *             fixed (S096 P1 trim 3, SD12); the detune offset stays.
 *             tapL is held at or below TKT_TAP_MAX (S096 P1 §17; 50 ms is
 *             under it, so no setting moves).
 * Why:        the taps' delays are constant within a block, so the loop
 *             reads them as an integer and a fraction (no per-sample
 *             division or modulo). The bound is what tkt_spaceOut()'s ring
 *             needs: a read never reaches the frame being written.
 * Accessors:  Tetrakt_calcVoiceAsync().
 */
static void tkt_spaceControl(TetraktVoice *voice)
{
	const float ms = 1.0f + 49.0f * (float)voice->spaceTime * (1.0f / 127.0f);
	const float tL = ms * (REAL_FS * 0.001f);
	const float tR = tL * (1.0f - TKT_SDT_SPAN * (float)voice->spaceDetune * (1.0f / 127.0f));
	voice->tapL = fminf(tL, TKT_TAP_MAX);
	voice->tapR = fminf(fmaxf(tR, 2.0f), TKT_TAP_MAX);
	voice->fb = TKT_FB_MAX * (float)voice->spaceFeedback * (1.0f / 127.0f);
	voice->fbK = tktFbLpK[voice->spaceTone & 127u];
	voice->slvGain = (float)voice->spaceLevel * (1.0f / 127.0f);
}

void Tetrakt_initVoice(TetraktVoice *voice, const uint8_t slot, int16_t *line)
{
	uint32_t i;

	if (!voice)
		return;
	/* the caller cleared the slot (memset); every field is set here anyway */
	voice->slot = slot;
	voice->line = line;
	voice->bindGen = sampleMemory_getGeneration();   /* the catalogue this voice binds to (SD5) */
	if (line)
		memset(line, 0, TKT_LINE_SAMPLES * sizeof(line[0]));   /* the FxBuffer owner clears (SD11) */
	voice->wi = 0u;
	voice->layerOn = 1u;              /* A only (plan T-Q7) */
	voice->veloVol = 1u;              /* the Drum's default (DrumVoice.c:134) */
	voice->pan = 63u;
	voice->vol = 100.0f / 127.0f;
	voice->spaceTime = 40u;
	voice->spaceFeedback = 40u;
	voice->spaceDetune = 30u;
	voice->spaceTone = 100u;
	voice->spaceLevel = 100u;
	voice->width = 0u;                /* SD15 */
	voice->note = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->veloKind = TKT_KIND_NONE;
	voice->fbLp = 0.0f;
	voice->busWMix = 0.0f;
	voice->busApZ = 0.0f;
	voice->dcXL = voice->dcYL = voice->dcXR = voice->dcYR = 0.0f;
	for (i = 0u; i < TKT_LAYERS; i++) {
		TktLayer *const L = &voice->layer[i];

		L->osc.oscType = OSC_TYPE_SMP;
		L->osc.waveform = (uint8_t)i;
		L->osc.pitchMod = 1.0f;
		L->osc.modNodeValue = 1.0f;
		L->osc.sampleCacheLooped = OSC_BIND_NONE;
		L->sample = (uint8_t)i;       /* samples 0..3 (clamped to the installed count by the bind) */
		L->tune = 63u;
		L->level = 100u;
		L->decay = 70u;
		L->pan = 63u;
		L->drive = 0u;
		L->srr = 127u;
		L->space = 0u;
		L->tone = 63u;
		L->toneEnv = 63u;
		L->stage = TKT_STAGE_IDLE;
		L->pendSample = (uint8_t)i;
		L->pendVel = 127u;
		L->env = L->envLast = 0.0f;
		L->velo = 1.0f;
		L->ic1 = L->ic2 = 0.0f;
		L->srrCnt = L->srrHold = 0.0f;
		L->bindGain = 1.0f;           /* the bind below sets the sample's own (§19) */
		tkt_layerControl(voice, i, 1u, 1u);
	}
	tkt_spaceControl(voice);
	lfo_init(&voice->lfo);
}

void Tetrakt_triggerVoice(TetraktVoice *voice, const uint8_t source_slot,
                          const uint8_t velocity, const uint8_t note)
{
	const float vel = (float)velocity * (1.0f / 127.0f);
	uint32_t i;

	/* --- platform part (as every engine): LFO and velocity, per slot --- */
	lfo_retrigger(source_slot);
	modNode_updateValue(&velocityModulators[source_slot], vel);
	instrumentManager_applyVelocityModulationTarget(source_slot, vel);

	/*
	 * The layer switch (plan §3.1, §4.3).
	 *
	 * What:       every layer whose bit is set in layerOn latches the trigger's
	 *             velocity and its current sample row, then fades (if it is
	 *             still sounding, SD9) or starts on the next block. A layer
	 *             whose bit is clear is untouched and plays out. A retrigger
	 *             during a FADE or a START only replaces what was latched.
	 * Why:        layerOn is read only here: a step lock on it applies to
	 *             its own step because the automation drain runs before the
	 *             triggers (voiceControl_processPending()).
	 */
	voice->note = note;
	for (i = 0u; i < TKT_LAYERS; i++) {
		TktLayer *const L = &voice->layer[i];

		if (!(voice->layerOn & (1u << i)))
			continue;
		L->pendSample = L->sample;
		L->pendVel = velocity;
		if (L->stage == TKT_STAGE_PLAY && L->env >= TKT_QUIET)
			L->stage = TKT_STAGE_FADE;
		else if (L->stage != TKT_STAGE_FADE)
			L->stage = TKT_STAGE_START;
	}
}

void Tetrakt_calcVoiceAsync(TetraktVoice *voice)
{
	const uint32_t gen = sampleMemory_getGeneration();
	const uint32_t rebind = (uint32_t)(gen != voice->bindGen);
	float num = 0.0f, den = 0.0f, envW;
	uint32_t i;

	voice->bindGen = gen;
	for (i = 0u; i < TKT_LAYERS; i++) {
		const float lv = (float)voice->layer[i].level;

		tkt_layerControl(voice, i, 0u, rebind);
		num += lv * voice->layer[i].env;
		den += lv;
	}
	/* W2 on the summed right bus (trim 1): the level-weighted envelope. */
	envW = (den > 0.0f) ? (num / den) : 0.0f;
	voice->busWMix = (float)voice->width * (1.0f / 127.0f) *
	                 (1.0f - envW) * (1.0f - envW);
	tkt_spaceControl(voice);
}

/*
 * One layer's per-sample chain (S096 P1; plan §2, §5, §6.2; the read
 * folded in by §19).
 *
 * What:       per sample n:
 *               read: the bound sample at the Q19.12 phase, linearly
 *                 interpolated in integers, s = (a << 12) + frac (b - a),
 *                 a = data[ph >> 12], b = the next frame, frac = ph & 4095;
 *                 ph = min(ph + phaseInc, end) (osc_smpPlainBlock()'s law:
 *                 a finished sample reads the two installed zero frames);
 *               x = s x the ramped decay x velocity (veloVol) x bindGain /
 *                 4,096 x 1 / 32,768 (one gain, ramped per sample);
 *               drive: d = clip(x dGain + dBias) - dClipB; x += dWet (d - x);
 *               SRR: cnt += rate; on cnt >= 1 the hold takes x; x = hold;
 *               tone: the TPT step (v3 = x - ic2; v1 = a1 ic1 + a2 v3;
 *                 v2 = ic2 + a2 ic1 + a3 v3; ic1 = 2 v1 - ic1;
 *                 ic2 = 2 v2 - ic2), y = mX x + mB v1 + mL v2, every
 *                 coefficient and weight += its step;
 *               pan: l = y gL; r = y gR;
 *               busL[n] (=|+=) l; busR[n] (=|+=) r; send[n] (=|+=) y gS.
 *             The first layer stores (first = 1), the others add, so the
 *             buses need no clearing. The integrator and SRR states are
 *             written back; the ramps are not (SD13).
 * Why:        the order of plan §2 (decay and velocity before the drive;
 *             the tone last). Every stage always runs: drive 0, srr 127, a
 *             flat tone and wid 0 are the input exactly through the same
 *             arithmetic.
 *             The read is here, not in osc_smpPlainBlock() (S096 P1 §19):
 *             the platform read stored an int16 block that this loop
 *             loaded, converted and scaled again; reading here saves ≈ 13
 *             instructions per layer per sample and the four dispatches.
 *             The interpolation is exact in integers (|s| < 2^29) and is
 *             no longer rounded to int16 before the gain, so the result is
 *             the platform read's to within its rounding (host law
 *             TK-READ). With phaseInc 4,096 (no fraction) and bindGain 1 it
 *             equals the old scratch path bit for bit (x 2^12 then
 *             x 2^-12 is exact).
 * Inputs:     L (its bound sample, phase and phaseInc from the async), vg
 *             (the velocity gain: velo or 1), first.
 * Outputs:    the buses; L's phase, tone and SRR states.
 * Accessors:  tkt_layerPostStore(), tkt_layerPostAdd().
 */
static inline __attribute__((always_inline)) void tkt_layerPost(TktLayer *L, const float vg,
                                                                  const uint32_t first)
{
	/* osc_renderTyped()'s rule: only an SMP bind with data reads; anything
	 * else (OSC_BIND_NONE: no sample installed) reads a zero pair at phase
	 * 0, so the loop is the same and the layer is silent */
	static const int16_t tkt_zeroPair[2] = { 0, 0 };
	const uint32_t live = (L->osc.sampleCacheLooped == OSC_BIND_SMP &&
	                       L->osc.sampleCacheData && L->osc.sampleCacheSize != 0u);
	const int16_t *const data = live ? (const int16_t *)L->osc.sampleCacheData : tkt_zeroPair;
	const uint32_t endPh = live ? (L->osc.sampleCacheSize << OSC_SAMPLE_PHASE_SHIFT) : 0u;
	const uint32_t inc = L->osc.phaseInc;
	uint32_t ph = live ? L->osc.phase : 0u;
	float *const bL = tkt_work.busL;
	float *const bR = tkt_work.busR;
	float *const bS = tkt_work.send;
	/* the read's Q12 scale and the bound sample's gain join the envelope
	 * gain; with bindGain 1 the factor is exactly 2^-12 (§19) */
	const float rs = L->bindGain * (1.0f / 4096.0f);
	float g = L->envLast * vg * TKT_IN_SCALE * rs;
	const float gStep = (L->env - L->envLast) * vg * TKT_IN_SCALE * (1.0f / (float)OUTPUT_DMA_SIZE) * rs;
	const float dG = L->dGain, dB = L->dBias, dCB = L->dClipB, dW = L->dWet;
	const float rate = L->srrRate;
	const float gL = L->gL, gR = L->gR, gS = L->gS;
	const float da1 = L->da1, da2 = L->da2, da3 = L->da3;
	float a1 = L->a1, a2 = L->a2, a3 = L->a3;
	float mX = L->mX, mB = L->mB, mL = L->mL;
	float ic1 = L->ic1, ic2 = L->ic2;
	float cnt = L->srrCnt, hold = L->srrHold;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const uint32_t idx = ph >> OSC_SAMPLE_PHASE_SHIFT;
		const int32_t a = data[idx];
		float x = (float)((a << OSC_SAMPLE_PHASE_SHIFT) +
		                  (int32_t)(ph & 0xFFFu) * ((int32_t)data[idx + 1u] - a)) * g;

		ph += inc;
		ph = (ph > endPh) ? endPh : ph;
		float d, v1, v2, v3, y, r;

		g += gStep;
		d = tkt_clip(x * dG + dB) - dCB;
		x += dW * (d - x);
		cnt += rate;
		if (cnt >= 1.0f) {
			cnt -= 1.0f;
			hold = x;
		}
		x = hold;
		v3 = x - ic2;
		v1 = a1 * ic1 + a2 * v3;
		v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.0f * v1 - ic1;
		ic2 = 2.0f * v2 - ic2;
		y = mX * x + mB * v1 + mL * v2;
		a1 += da1; a2 += da2; a3 += da3;
		r = y * gR;
		if (first) {
			bL[n] = y * gL;
			bR[n] = r;
			bS[n] = y * gS;
		} else {
			bL[n] += y * gL;
			bR[n] += r;
			bS[n] += y * gS;
		}
	}
	L->ic1 = ic1;
	L->ic2 = ic2;
	L->srrCnt = cnt;
	L->srrHold = hold;
	if (live)
		L->osc.phase = ph;
}

/* The two instances (the Colloid's store / add pattern): one loop each. */
static __attribute__((noinline)) void tkt_layerPostStore(TktLayer *L, const float vg)
{
	tkt_layerPost(L, vg, 1u);
}
static __attribute__((noinline)) void tkt_layerPostAdd(TktLayer *L, const float vg)
{
	tkt_layerPost(L, vg, 0u);
}

/*
 * The output of one frame (S096 P1, plan §5.4, §7 step 5; split out in §17).
 *
 * What:       l = busL + tapL slvGain, r = busR + tapR slvGain; r passes
 *             W2's one allpass (mixed by busWMix, trim 1), then each side
 *             the 8 Hz DC blocker (y = x - x1 + R y1); *oL = sat16(l x
 *             32,767), *oR = sat16(r x 32,767).
 * Why:        the same arithmetic for the ring path and the no-unit path
 *             of tkt_spaceOut(); always inlined, so the states stay in
 *             registers.
 */
typedef struct {
	float az, xl1, yl1, xr1, yr1;
} TktOutState;

static inline __attribute__((always_inline)) void tkt_outFrame(TktOutState *st,
                                                                const float l,
                                                                float r,
                                                                const float wm,
                                                                int16_t *oL,
                                                                int16_t *oR)
{
	const float ap = TKT_BUS_AP * r + st->az;
	float yl, yr;

	st->az = r - TKT_BUS_AP * ap;
	r += wm * (ap - r);
	yl = l - st->xl1 + TKT_DC_R * st->yl1;
	yr = r - st->xr1 + TKT_DC_R * st->yr1;
	st->xl1 = l; st->yl1 = yl;
	st->xr1 = r; st->yr1 = yr;
	*oL = tkt_sat16(yl * TKT_OUT_SCALE);
	*oR = tkt_sat16(yr * TKT_OUT_SCALE);
}

/*
 * The space stage and the output (S096 P1, plan §6.1, §5.4, §7 steps 4-5;
 * the ring reworked in S096 P1 §17).
 *
 * What:       the unit is [guard][ring of TKT_RING]. Per frame: the two
 *             taps read the ring at the pair [a - 1], [a], a = wi -
 *             floor(tap), with linear interpolation (integer part and
 *             fraction fixed for the block): tap = [a] + f ([a - 1] - [a]);
 *             their mid feeds the feedback one-pole (fbLp += fbK (mid -
 *             fbLp)); the ring takes sat16((send + fb fbLp) x 32,767) at
 *             wi; then tkt_outFrame(). With no voice unit (SD11) the taps
 *             are 0 and nothing is written.
 *             The block runs in segments: a segment ends where the write
 *             pointer or either read pointer reaches the ring's end (each
 *             wraps at most once in 32 frames), so at most four segments a
 *             block. When the write pointer wraps, the guard takes a copy of
 *             the ring's last sample. Within a segment the three pointers
 *             only step, so the frame loop holds no index arithmetic.
 * Why:        the per-frame wrap selects of the first form (four indices,
 *             each a compare and a select, plus the write wrap) were most
 *             of this loop's cost beyond the plan's estimate (S096 P1 §16
 *             F3). The guard is the Space tape's rule (SpaceEffect.c:235):
 *             the pair that would straddle the wrap (a = 0) reads the copy
 *             as its older sample. The copy is always current when read: a
 *             = 0 is read floor(tap) >= 2 frames after the ring's last
 *             sample was written, and the copy is made at the end of the
 *             segment that wrote it. The arithmetic and its order per frame
 *             are those of the first form, so the output is the same (host
 *             law TK-RING).
 * Inputs:     voice (tapL/tapR <= TKT_TAP_MAX, from tkt_spaceControl();
 *             wi < TKT_RING), tkt_work's buses.
 * Outputs:    outL, voice->right, the ring and its guard, wi, the S1 / W2 /
 *             DC states.
 * Cost:       the frame loop per sample; per segment (<= 4 a block) its
 *             limits and wraps (tools/dsp_test/tetrakt_cpu.py counts both).
 * Accessors:  Tetrakt_calcVoiceSyncBlock().
 */
static __attribute__((noinline)) void tkt_spaceOut(TetraktVoice *voice, int16_t *outL)
{
	int16_t *const line = voice->line;
	const float *const bL = tkt_work.busL;
	const float *const bR = tkt_work.busR;
	const float *const bS = tkt_work.send;
	int16_t *const outR = voice->right;
	const float wm = voice->busWMix;
	TktOutState st;
	uint32_t n;

	st.az = voice->busApZ;
	st.xl1 = voice->dcXL; st.yl1 = voice->dcYL;
	st.xr1 = voice->dcXR; st.yr1 = voice->dcYR;
	if (!line) {
		for (n = 0u; n < OUTPUT_DMA_SIZE; n++)
			tkt_outFrame(&st, bL[n], bR[n], wm, &outL[n], &outR[n]);
	} else {
		int16_t *const ring = line + 1;
		const int16_t *const end = ring + TKT_RING;
		const uint32_t iL = (uint32_t)voice->tapL;
		const uint32_t iR = (uint32_t)voice->tapR;
		const float fL = voice->tapL - (float)iL;
		const float fR = voice->tapR - (float)iR;
		const float fb = voice->fb, fbK = voice->fbK, slv = voice->slvGain;
		float lp = voice->fbLp;
		const uint32_t wi = voice->wi;
		int16_t *w = ring + wi;
		const int16_t *rl = ring + ((wi >= iL) ? wi - iL : wi + TKT_RING - iL);
		const int16_t *rr = ring + ((wi >= iR) ? wi - iR : wi + TKT_RING - iR);

		n = 0u;
		while (n < OUTPUT_DMA_SIZE) {
			uint32_t run = OUTPUT_DMA_SIZE - n;
			const uint32_t ew = (uint32_t)(end - w);
			const uint32_t el = (uint32_t)(end - rl);
			const uint32_t er = (uint32_t)(end - rr);
			uint32_t stop;

			run = (run < ew) ? run : ew;
			run = (run < el) ? run : el;
			run = (run < er) ? run : er;
			stop = n + run;
			for (; n < stop; n++) {
				const float tl = ((float)rl[0] + fL * (float)(rl[-1] - rl[0])) * TKT_IN_SCALE;
				const float tr = ((float)rr[0] + fR * (float)(rr[-1] - rr[0])) * TKT_IN_SCALE;

				rl++;
				rr++;
				lp += fbK * (0.5f * (tl + tr) - lp);
				*w++ = tkt_sat16((bS[n] + fb * lp) * TKT_OUT_SCALE);
				tkt_outFrame(&st, bL[n] + tl * slv, bR[n] + tr * slv, wm,
				             &outL[n], &outR[n]);
			}
			if (w == end) {
				line[0] = ring[TKT_RING - 1u];   /* the guard */
				w = ring;
			}
			rl = (rl == end) ? ring : rl;
			rr = (rr == end) ? ring : rr;
		}
		voice->wi = (uint16_t)(w - ring);
		voice->fbLp = lp;
	}
	voice->busApZ = st.az;
	voice->dcXL = st.xl1; voice->dcYL = st.yl1;
	voice->dcXR = st.xr1; voice->dcYR = st.yr1;
}

void Tetrakt_calcVoiceSyncBlock(TetraktVoice *voice, int16_t *buf, uint8_t size)
{
	/*
	 * One block (S096 P1, plan §7).
	 *
	 * What:       per layer, the layer's post loop, which reads its bound
	 *             sample itself (A stores the buses, B..D add); then the
	 *             space stage and the output pack.
	 * Why:        S096 P1 §19: the platform read (osc_renderTyped() ->
	 *             osc_smpPlainBlock() into an int16 scratch) is folded into
	 *             the post loop, which saves the scratch round trip and the
	 *             four dispatches; the bind is still the platform's
	 *             (osc_userBind(), SD5). The order of the four layers and
	 *             the loops never depends on a setting (constant cost).
	 * Inputs:     voice (the async ran this block), buf (the left output).
	 * Outputs:    buf, voice->right; every state the loops keep.
	 */
	uint32_t i;

	(void)size;   /* the voice always renders OUTPUT_DMA_SIZE */
	for (i = 0u; i < TKT_LAYERS; i++) {
		TktLayer *const L = &voice->layer[i];
		const float vg = voice->veloVol ? L->velo : 1.0f;

		if (i == 0u)
			tkt_layerPostStore(L, vg);
		else
			tkt_layerPostAdd(L, vg);
	}
	tkt_spaceOut(voice, buf);
}

uint8_t Tetrakt_isQuiet(const TetraktVoice *voice)
{
	/* SD10: every layer idle; a still-ringing S1 tail does not hold the swap */
	uint32_t i;

	for (i = 0u; i < TKT_LAYERS; i++)
		if (voice->layer[i].stage != TKT_STAGE_IDLE)
			return 0u;
	return 1u;
}

void Tetrakt_setVelocityKind(TetraktVoice *voice, const uint8_t kind)
{
	/* SD3: the group a velocity destination names, or TKT_KIND_NONE */
	if (voice)
		voice->veloKind = (kind < TKT_KIND_COUNT) ? kind : TKT_KIND_NONE;
}
