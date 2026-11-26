/*
 * TetraktVoice.h - the Tetrakt voice engine (S096 P1).
 *
 * What:       one Tetrakt voice: four sample layers (A B C D), each a
 *             one-shot read of an installed sample (bound by the
 *             platform's osc_userBind(); read inside the layer's post loop,
 *             S096 P1 §19, the osc_smpPlainBlock() law) with its own
 *             decay, tune, level, pan, drive, sample-rate reduction, 2-pole
 *             bipolar tone with an envelope amount, and space send; a layer
 *             switch row (lon, 4 bits) that decides which layers a trigger
 *             restarts; a 50 ms line in one FxBuffer voice unit read by two
 *             taps (S1); an envelope-derived width on the summed right bus
 *             (W2, trim 1); a true stereo output.
 *             Entry points follow the other engines (init / trigger / async /
 *             sync block), taking an instance pointer owned by the caller.
 * Why:        S096_P1_TETRAKT_SAMPLE_PLAYER.md (revision 7; every question
 *             decided, §12), and S096_P1_IMPLEMENTATION.md §0.
 * Inputs:     descriptor writes into the row bytes (TYPE_UINT8, SD4) and the
 *             voice's vol (TYPE_FLT); triggers with velocity and note;
 *             Tetrakt_setVelocityKind() from InstrumentManager (SD3).
 * Outputs:    per block two pre-volume int16 blocks: the left into the
 *             caller's buffer (the mixer's sampleData), the right into
 *             voice->right (read by the mixer through
 *             instrumentManager_slotRight()). Tetrakt_isQuiet().
 * Accessors:  InstrumentManager dispatchers; tools/dsp_test modules/tetrakt.c.
 * Affiliates: TetraktParameters.h (the rows), TetraktTables.h,
 *             Oscillator.h (OscInfo, osc_userBind()), SampleMemory.h (the
 *             bound sample's gain code),
 *             FxBuffer.h (the voice unit; InstrumentManager owns the claim).
 */
#ifndef TETRAKTVOICE_H_
#define TETRAKTVOICE_H_

#include <stdint.h>
#include "Oscillator.h"       /* OscInfo, OSC_TYPE_SMP, config.h (OUTPUT_DMA_SIZE) */
#include "lfo.h"

/* Four layers (plan §0). */
#define TKT_LAYERS            4u
/* The layer switch's 16 values (lon, 4 bits; MenuText.h tktLayerNames). */
#define TKT_LAYER_MASK_COUNT  16u
/* The S1 line: one FxBuffer voice unit (FXBUF_VOICE_UNIT_SAMPLES; asserted
 * in InstrumentManager.c, which includes FxBuffer.h). */
#define TKT_LINE_SAMPLES      2208u

/*
 * A layer's stage (S096 P1 SD9, plan §3.2 R1).
 *
 * What:       IDLE: silent (env 0), the read keeps running (constant cost);
 *             PLAY: decaying; FADE: a retrigger of a sounding layer, this
 *             block ramps the old read to 0; START: the next async binds the
 *             latched sample, restarts the read at its start, latches the
 *             velocity and plays at full level.
 * Why:        a restart from the start while the old read is loud would
 *             step; the fade takes one block (0.73 ms) and only for a layer
 *             that was still sounding.
 */
#define TKT_STAGE_IDLE        0u
#define TKT_STAGE_PLAY        1u
#define TKT_STAGE_FADE        2u
#define TKT_STAGE_START       3u

/*
 * The velocity groups (S096 P1 SD3, plan §4.2).
 *
 * What:       the nine per-layer row kinds a velocity destination can name
 *             for all four layers at once; TKT_KIND_NONE when the
 *             destination is not a group (off, a voice row, the own Morph:
 *             the platform node handles those).
 * Accessors:  TetraktParameters.c tetraktParameters_velocityKind();
 *             Tetrakt_setVelocityKind(); tkt_layerControl().
 */
#define TKT_KIND_TUNE         0u
#define TKT_KIND_LEVEL        1u
#define TKT_KIND_DECAY        2u
#define TKT_KIND_PAN          3u
#define TKT_KIND_DRIVE        4u
#define TKT_KIND_SRR          5u
#define TKT_KIND_SPACE        6u
#define TKT_KIND_TONE         7u
#define TKT_KIND_TONE_ENV     8u
#define TKT_KIND_COUNT        9u
#define TKT_KIND_NONE         0xFFu

/*
 * One layer (S096 P1).
 *
 * Row bytes (descriptor-bound, TYPE_UINT8, SD4): sample (the catalogue
 * index, latched at a restart), tune, level, decay, pan, drive, srr, space,
 * tone, toneEnv.
 * State: stage (TKT_STAGE_*), pendSample / pendVel (what a retrigger
 * latched), env (the decay level at this block's end) and envLast (at its
 * start), velo (the latched velocity 0..1).
 * Tone SVF (the Colloid's ColSvf form): ic1 ic2 the integrator states;
 * a1 a2 a3 the coefficients at the block's start and da1..da3 their
 * per-sample steps; mX mB mL the output mix (y = mX x + mB v1 + mL v2) and
 * dmX..dmL its steps. The block's end values are a + 32 da, so the next
 * async starts there with no stored targets (SD13).
 * Drive: dGain (x1..x32), dBias, dClipB (clip(bias)), dWet (0..1 over the
 * first 8 steps). Gains: gL, gR (the pan law x level x make-up x
 * TKT_MIX_SCALE), gS (the send). SRR: srrRate, srrCnt, srrHold.
 * Read (S096 P1 §19): osc holds the bound sample (sampleCacheData / Size /
 * Index), its Q19.12 phase and phaseInc; bindGain is the bound sample's
 * catalogue gain (sampleMemory_gainFromCode()), set by every bind, so the
 * post loop applies it with the envelope gain.
 * W2 runs on the summed right bus (S096 P1 trim 1, S096_P1_IMPLEMENTATION
 * SD12): one allpass for the voice, mixed by the level-weighted envelope, so
 * a layer carries no width state of its own.
 */
typedef struct {
	OscInfo  osc;
	uint8_t  sample, tune, level, decay, pan, drive, srr, space, tone, toneEnv;
	uint8_t  stage;
	uint8_t  pendSample;
	uint8_t  pendVel;
	/*
	 * Track-7 decay state (S104 P1), in the three padding bytes after
	 * pendVel (offsets 85..87, measured; sizeof(TktLayer) unchanged).
	 * decayAlt: the decay a note started by track 7 plays; pendDecayAlt:
	 * the pending track-7 trigger's decay; pflags: TKT_PF_PEND (a trigger is
	 * waiting for this layer's START latch), TKT_PF_PEND_ALT (that trigger
	 * is track 7's), TKT_PF_ALT (the playing note is track 7's), and the
	 * loop read form (TKT_PF_FORM_*, S104 P1 loops). Written by
	 * Tetrakt_triggerVoice() and tkt_layerControl()'s START latch; read by the
	 * decay control and the post-loop dispatch.
	 */
	uint8_t  decayAlt;
	uint8_t  pendDecayAlt;
	uint8_t  pflags;
	float    env, envLast, velo;
	/*
	 * S101 P2 A2q (S101_P2_REDUCTIONS.md §A.3 A2q; S101_P2_IMPLEMENTATION.md
	 * §5.1): the decay's per-block factor (1 - k, or 0 after a FADE) that
	 * tkt_layerCarry() applies on the three blocks between this layer's
	 * controls, so the envelope stays the exact per-block exponential.
	 * RAM: none (inside the 1,264 B slot reserve: the voice was 1,064 B).
	 */
	float    envMul;
	float    ic1, ic2;
	float    a1, a2, a3, da1, da2, da3;
	float    mX, mB, mL, dmX, dmB, dmL;
	float    dGain, dBias, dClipB, dWet;
	float    gL, gR, gS;
	float    srrRate, srrCnt, srrHold;
	float    bindGain;          /* the bound sample's gain (S096 P1 §19) */
	/*
	 * Loop points of a looped (SLP) bind (S104 P1), in Q19.12 phase:
	 * loopStartPh = loop_start << 12 and loopEndPh = loop_end << 12
	 * (loop_end exclusive; the whole sample without a `smpl` chunk,
	 * SampleInstall.c), written at every bind (tkt_bindLayer()); 0 for a
	 * one-shot bind. Why: the async's seam prediction and the sync render's
	 * seam forms read them every block of a looped layer; caching them here
	 * keeps catalogue lookups out of both (and out of the sync render's
	 * register allocation). RAM: 8 B a layer, 32 B a voice, inside the
	 * 1,264 B slot reserve (TetraktVoice 1,080 -> 1,112 B; asserted at
	 * InstrumentManager.c:360). No new allocation (S101 P2 A2q precedent).
	 */
	uint32_t loopStartPh, loopEndPh;
	/*
	 * Seam threshold (S104 P1 assessment): the first block-start
	 * phase from which this block's reads can reach the last loop frame,
	 * loopEndPh - 4,096 - 32 x phaseInc (0 when the loop is shorter than one
	 * block's reach: every block is a seam block); 0xFFFFFFFF for a one-shot
	 * (never a seam). Written by tkt_layerControl() after the bind and the
	 * step, the only places either changes; the async's seam prediction is
	 * then one load and compare per layer instead of a multiply-add.
	 * RAM: 4 B a layer, 16 B a voice, inside the 1,264 B slot reserve
	 * (TetraktVoice 1,112 -> 1,128 B; asserted at InstrumentManager.c:360).
	 */
	uint32_t seamPh;
} TktLayer;

#define TKT_PF_PEND       0x01u
#define TKT_PF_PEND_ALT   0x02u
#define TKT_PF_ALT        0x04u
/* S104 P1 assessment: the pending trigger's row value names a looped
 * sample; Tetrakt_triggerVoice() maps the value, the START latch only copies. */
#define TKT_PF_PEND_SLP   0x20u

/* S104 P1: the post-loop form of this block (Tetrakt_calcVoiceAsync()). */
#define TKT_PF_FORM_SHIFT 3u
#define TKT_PF_FORM_MASK  0x18u
#define TKT_FORM_PLAIN    0u   /* no seam this block: the one-shot loop */
#define TKT_FORM_A        1u   /* seam, interpolation slot held: exact */
#define TKT_FORM_B        2u   /* seam, no slot: early wrap */

/* The host build's OscInfo differs, so the firmware layout is the one pinned. */
#if !defined(TKT_HOST)
_Static_assert(sizeof(TktLayer) == 216u,
               "S104 P1: 204 B + the two loop points + the seam threshold");
#endif

/*
 * The voice (S096 P1).
 *
 * Rows (descriptor-bound): layerOn (lon, bit 0 = A .. bit 3 = D), veloVol
 * (vel: velocity scales each restarted layer), pan (the mixer's slot pan:
 * the L-R balance), vol (read by the mixer), spaceTime, spaceFeedback,
 * spaceDetune, spaceTone, spaceLevel (S1), width (W2).
 * State: slot (for velocityModulators[]), note (the last trigger note),
 * veloKind (TKT_KIND_*), line (the voice unit or NULL, SD11), wi (its write
 * index), tapL / tapR (the taps' delays in samples, this block), fb, fbK,
 * slvGain, fbLp (S1's feedback LP state), busWMix / busApZ (W2 on the summed
 * right bus), bindGen (the catalogue generation the layers are bound to),
 * the DC blockers' states, right[] (the right output block).
 */
typedef struct TetraktVoiceStruct
{
	TktLayer layer[TKT_LAYERS];
	Lfo      lfo;
	int16_t  right[OUTPUT_DMA_SIZE];
	int16_t *line;
	float    vol;
	float    tapL, tapR, fb, fbK, slvGain, fbLp;
	float    busWMix, busApZ;
	float    dcXL, dcYL, dcXR, dcYR;
	uint32_t bindGen;          /* the catalogue generation the samples are bound to (SD5) */
	uint16_t wi;
	uint8_t  layerOn, veloVol, pan;
	uint8_t  spaceTime, spaceFeedback, spaceDetune, spaceTone, spaceLevel, width;
	uint8_t  slot, note, veloKind;
	/* S101 P2 A2q: the layer-control schedule, 0..TKT_CTL_BLOCKS - 1 (layer i
	 * runs its control when (ctlPhase + i) % 4 is 0; RAM: none, inside the
	 * slot reserve) */
	uint8_t  ctlPhase;
} TetraktVoice;

/*
 * Entry points (S096 P1).
 *
 * Tetrakt_initVoice()        the init.tkt values; slot and the voice unit
 *                            (or NULL) from InstrumentManager; clears the
 *                            unit (main loop or boot).
 * Tetrakt_triggerVoice()     LFO retrigger, the platform velocity node;
 *                            arms the layers set in layerOn, or in `layers`
 *                            when it is nonzero for this trigger only
 *                            (S096 P3) (main loop, sequencer); altDecay is
 *                            track 7's four decays, NULL for track 6
 *                            (S104 P1).
 * Tetrakt_calcVoiceAsync()   the block's control work (render context).
 * Tetrakt_calcVoiceSyncBlock() the block's audio: left into buf, right into
 *                            voice->right (render context).
 * Tetrakt_isQuiet()          every layer IDLE.
 * Tetrakt_setVelocityKind()  the velocity group (SD3) or TKT_KIND_NONE.
 * The two render entries are noinline so LTO keeps them out of
 * mixer_calcNextSampleBlock() (S086 F1).
 */
void Tetrakt_initVoice(TetraktVoice *voice, uint8_t slot, int16_t *line)
	__attribute__((noinline, noclone));
void Tetrakt_triggerVoice(TetraktVoice *voice, uint8_t source_slot,
                          uint8_t velocity, uint8_t note, uint8_t layers,
                          const uint8_t *altDecay);
void Tetrakt_calcVoiceAsync(TetraktVoice *voice) __attribute__((noinline));
void Tetrakt_calcVoiceSyncBlock(TetraktVoice *voice, int16_t *buf, uint8_t size)
	__attribute__((noinline));
uint8_t Tetrakt_isQuiet(const TetraktVoice *voice);
void Tetrakt_setVelocityKind(TetraktVoice *voice, uint8_t kind);

#endif /* TETRAKTVOICE_H_ */
