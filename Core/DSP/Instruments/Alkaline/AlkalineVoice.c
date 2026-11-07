/*
 * AlkalineVoice.c - the Alkaline (TB-303) voice engine (S093 P0).
 * See AlkalineVoice.h. Behaviour: S092_P6_DSP_EQUATIONS.md §4.6 and the
 * validated pattern driver tools/alkaline/validate.py dsp_pattern().
 */
#include <math.h>
#include "AlkalineVoice.h"
#include "AlkalineTables.h"
#include "wavetable.h"
#include "MidiPitch.h"

/* slide lag: tau = 100 k x 0.22 uF = 22 ms (S092 P0 A9); kept per block */
#define ALK_SLIDE_KEEP   0.967561f      /* e^(-0.7255 ms / 22 ms) */
/* VCO -> volts at S1 (S092 P6 §3.6): sawTable ramps +-32,625 LSB for the
 * 303 saw's 5.35 V; the square table carries ALK_SQR_SCALE_V. */
#define ALK_SAW_SCALE_V  (2.675f / 32625.0f)
#define ALK_V561         3.263f

void Alkaline_initVoice(AlkalineVoice *voice)
{
	if (!voice)
		return;
	voice->osc.waveform = SAW;
	voice->osc.freq = 65.406f;
	voice->osc.pitchMod = 1.0f;
	voice->osc.modNodeValue = 1.0f;
	voice->osc.midiFreq = (uint16_t)((36u << 8) | 63u);   /* coa 36 = C2; fine at its centre */
	voice->osc.baseNote = MIDI_DEFAULT_TRIGGER_NOTE;
	DiodeLadder_init(&voice->ladder);
	alkMeg_init(&voice->meg);
	alkAcc_init(&voice->accNet);
	alkVeg_init(&voice->veg);
	lfo_init(&voice->lfo);

	voice->cutoff = 0.5f;
	voice->reso = 0.5f;
	voice->envMod = 0.5f;
	voice->decay = 0.5f;
	voice->accent = 0.5f;
	voice->tuning = 63.0f / 127.0f;
	voice->vol = 0.8f;
	voice->pan = 63u;
	voice->wave = ALK_WAVE_SAW;
	voice->slide = 0u;

	voice->note = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->slideLatch = 0u;
	voice->gliding = 0u;
	voice->accentOn = 0u;
	voice->pitchJump = 1u;
	voice->gateLeft = 0;
	voice->accentLeft = 0;
	voice->pitchLog2 = log2f(65.406f);
	voice->v134 = ALK_V561;
	voice->iLadRaw = 0.0f;
	voice->iLadEnd = 0.0f;
	voice->iAbcEnd = 0.0f;
}

/*
 * S092 P0 §5.3 / §5.4: accent from velocity; legato when the previous note
 * was slide-flagged and its gate is still held; the slide flag is latched
 * for the next trigger; the gate is 7/12 of a step, or the whole step plus
 * the next step's gate after a slide-flagged note.
 */
void Alkaline_triggerVoice(AlkalineVoice *voice, const uint8_t velocity,
                           const uint8_t note, const uint32_t stepSamples)
{
	const int32_t gate = (int32_t)((stepSamples * ALK_GATE_NUM + ALK_GATE_DEN / 2u)
	                               / ALK_GATE_DEN);
	const uint8_t legato = (uint8_t)(voice->slideLatch && voice->gateLeft > 0);

	voice->note = note;
	voice->accentOn = (uint8_t)(velocity >= ALK_ACCENT_VELOCITY);
	voice->accentLeft = (int32_t)stepSamples;
	voice->gliding = legato;
	if (!legato) {
		voice->pitchJump = 1u;
		alkMeg_trigger(&voice->meg);
		alkVeg_trigger(&voice->veg);
	}
	voice->slideLatch = (uint8_t)(voice->slide != 0u);
	voice->gateLeft = voice->slideLatch ? (int32_t)stepSamples + gate : gate;
}

void Alkaline_calcVoiceAsync(AlkalineVoice *voice)
{
	const uint8_t gateOn = (uint8_t)(voice->gateLeft > 0);
	float n53, vW4, iAcc, dv, iLad, target, k, f;
	uint8_t lo;

	/* the lanes are high for every block that starts before their end */
	if (voice->accentLeft <= 0)
		voice->accentOn = 0u;
	voice->gateLeft = (voice->gateLeft > OUTPUT_DMA_SIZE) ? voice->gateLeft - OUTPUT_DMA_SIZE : 0;
	voice->accentLeft = (voice->accentLeft > OUTPUT_DMA_SIZE) ? voice->accentLeft - OUTPUT_DMA_SIZE : 0;

	/* pitch: coarse + note + fine from the oscillator, slide lag in log2 Hz,
	 * Tuning +-700 cents */
	osc_setBaseNote(&voice->osc, voice->note);
	target = log2f(voice->osc.freq);
	if (voice->pitchJump || !voice->gliding)
		voice->pitchLog2 = target;
	else
		voice->pitchLog2 = target + (voice->pitchLog2 - target) * ALK_SLIDE_KEEP;
	voice->pitchJump = 0u;
	voice->osc.freq = exp2f(voice->pitchLog2
	                        + (voice->tuning * 127.0f - 63.0f) * (7.0f / 12.0f / 63.0f));
	osc_setFreq(&voice->osc);

	/* control path (S092 P6 §4) */
	n53 = alkMeg_step(&voice->meg, voice->decay, voice->accentOn);
	alkAcc_step(&voice->accNet, voice->accentOn, voice->accent, voice->reso,
	            n53, voice->v134, &vW4, &iAcc);
	iLad = alk_expoCurrent(voice->cutoff, voice->envMod, n53, vW4, &dv);
	voice->v134 = ALK_V561 + dv;
	voice->iAbcEnd = alkVeg_step(&voice->veg, gateOn) + iAcc;
	voice->iLadRaw = iLad;
	voice->iLadEnd = iLad * alk_prewarp(iLad, voice->reso);

	/* Res-interpolated network for this block */
	k = fminf(fmaxf(voice->reso, 0.0f), 1.0f) * (float)(ALK_RES_POINTS - 1u);
	lo = (uint8_t)k;
	if (lo > ALK_RES_POINTS - 2u)
		lo = (uint8_t)(ALK_RES_POINTS - 2u);
	f = k - (float)lo;
	DiodeLadder_setNet(&voice->ladder, &alkResTable[lo], &alkResTable[lo + 1u], f);
}

void Alkaline_calcVoiceSyncBlock(AlkalineVoice *voice, int16_t *buf, const uint8_t size)
{
	const uint8_t square = (uint8_t)(voice->wave == ALK_WAVE_SQUARE);

	(void)size;   /* the ladder always renders OUTPUT_DMA_SIZE */
	calcWavetableOscBlock(&voice->osc, square ? alkSqrTable : sawTable, buf,
	                      OUTPUT_DMA_SIZE, 1.0f);
	DiodeLadder_renderB(&voice->ladder, buf,
	                    square ? ALK_SQR_SCALE_V : ALK_SAW_SCALE_V,
	                    voice->iLadEnd, voice->iAbcEnd);
}
