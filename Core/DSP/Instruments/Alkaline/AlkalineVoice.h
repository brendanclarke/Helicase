/*
 * AlkalineVoice.h - the Alkaline (TB-303) voice engine (S093 P0; B2R, drive
 * and the output make-up since S093 P3).
 *
 * What:       one Alkaline voice: the VCO (the fused table loop on sawTable,
 *             the D8 square table or, since S093 P8, a wavetable frame's
 *             1,024-point row), the diode-ladder VCF with its
 *             network and the OTA (DiodeLadder), and the control path (MEG,
 *             accent network, expo converter, VEG; AlkalineControl), with the
 *             303 sequencing rules: accent at velocity >= ALK_ACCENT_VELOCITY (101),
 *             slide taken from the slide-flagged note, legato notes neither
 *             strike the MEG nor trigger the VEG, gate 3.5/6 of a step.
 *             Entry points follow the other engines (init / trigger / async /
 *             sync block), taking an instance pointer owned by the caller.
 * Why:        S092 P0 §5, S092 P6 §4.6; S093 P0 §2.
 * Inputs:     descriptor writes into the parameter members (0..1 floats via
 *             TYPE_FLT, bytes via TYPE_UINT8); triggers with velocity, note
 *             and the track's step length in samples (the gate closes at the
 *             first block boundary at or after 7/12 of it).
 * Outputs:    one mono pre-volume int16 block per call (the mixer applies
 *             vol and pan, S072): the B2R ladder (DiodeLadder_renderB2), its
 *             input scaled by the drive (x1..x16) and its OTA current by the
 *             output make-up gain (S093_P2 §5, B-2 option (b)).
 *             Alkaline_isQuiet() for instrumentManager_ampEnvelopeQuiet().
 * Accessors:  InstrumentManager dispatchers (integration, S093 P0 §6);
 *             tools/dsp_test modules/alkaline.c.
 * Affiliates: DiodeLadder.h, AlkalineControl.h, AlkalineTables.h.
 */
#ifndef ALKALINEVOICE_H_
#define ALKALINEVOICE_H_

#include <stdint.h>
#include "Oscillator.h"
#include "lfo.h"
#include "DiodeLadder.h"
#include "AlkalineControl.h"

/* D1 (user, S093): velocity 101 or more is an accent (the default step
 * velocity, PAT_DEFAULT_VELOCITY, is 100, so un-edited steps are not). */
#define ALK_ACCENT_VELOCITY   101u
/* D11 (user, S092): gate = 3.5 of 6 clocks = 7/12 of a step. */
#define ALK_GATE_NUM          7u
#define ALK_GATE_DEN          12u
/* Output make-up (S093_P2 §5, B-2 option (b), user S093): a gain on the OTA
 * current that falls as drive rises, 2^(ALK_MAKEUP_OCT x (1 - drive)):
 * +6 dB at drive x1, 0 dB at x16, so the level is closer to the other engines
 * at the 303's own drive and the 7 V full scale (D17) keeps its headroom
 * when driven. Set by ear in the device listening (S093 P4). */
#define ALK_MAKEUP_OCT        1.0f
/* S093 P3: 1 = the wav oscillator read fused into the B2R loop
 * (DiodeLadder_renderB2Osc); 0 = calcWavetableOscBlock() then
 * DiodeLadder_renderB2 for the wav reference build. Typ wtb stays fused in
 * both builds and uses the same table loop. Bit-identical wav output (AL-X). */
#ifndef ALK_FUSED_OSC
#define ALK_FUSED_OSC         1
#endif
/* osc.waveform under typ wav (S093 P8: row osc1_wave; was the
 * AlkalineVoice.wave byte of row alk_wave). Under typ wtb the same byte is
 * the wavetable index. */
#define ALK_WAVE_SAW          0u
#define ALK_WAVE_SQUARE       1u
/*
 * The typ byte (row osc1_type, S093 P8).
 *
 * What:       the stored and displayed value of the Alkaline typ cell:
 *             0 wav (Saw/Sqr, the 303's VCO), 1 wtb (a wavetable frame);
 *             ALK_TYP_COUNT entries (alkTypeNames, MenuText.h).
 *             Alkaline_setOscType() maps it to OSC_TYPE_WAV / OSC_TYPE_WTB in
 *             osc.oscType.
 * Why:        S093 P8: wavetables only. Looped samples are over the Basic
 *             ceiling on their loop alone (P8 assessment §6); one-shot samples
 *             fit (loop 215) but their render grew two mixer loops of other
 *             engines by 1 (P8 schedule §0), so they are left out (user,
 *             S093 P8). osc.oscType keeps the platform's values so the shared
 *             wave-cell helpers (instrumentManager_osc1Type(), osc_userCount(),
 *             menu_formatWaveCell()) need no Alkaline map.
 * Accessors:  Alkaline_setOscType(); AlkalineParameters.c osc1_type row.
 */
#define ALK_TYP_WAV           0u
#define ALK_TYP_WTB           1u
#define ALK_TYP_COUNT         2u
/*
 * The Alkaline's wavetable bind state (S093 P8), kept in
 * osc.sampleCacheLooped.
 *
 * What:       ALK_BIND_NONE (nothing bound: the next block or trigger binds),
 *             ALK_BIND_WTB (a table's 1,024-point rows are bound). With
 *             osc.sampleCacheIndex (the bound table) and
 *             osc.sampleCacheGeneration (the catalogue generation) they are
 *             the key alk_wtbKeyChanged() compares each block.
 * Why:        the Alkaline binds its own tables (P8 §7): osc_userBind() is
 *             never called on an Alkaline oscillator (its callers are
 *             osc_setFreq(), osc_setType() and osc_typedTriggerPhase(), none of
 *             which an Alkaline reaches), so these values never meet the
 *             OSC_BIND_* ones in the same field.
 * Accessors:  AlkalineVoice.c (alk_wtbBind(), alk_wtbKeyChanged(),
 *             Alkaline_setOscType()).
 */
#define ALK_BIND_NONE         0u
#define ALK_BIND_WTB          1u

typedef struct AlkalineVoiceStruct
{
	/* VCO (S093 P8): oscType (typ), waveform (wav: Saw/Sqr; wtb: the
	 * table), position (pos), phase/phaseInc; under wtb the bind state in
	 * the sampleCache* fields: sampleCacheData = the 1,024-point row of the
	 * frame being played, sampleCacheSize = the table's frame count;
	 * positionApplied = the frame being played */
	OscInfo      osc;
	DiodeLadder  ladder;
	AlkMeg       meg;
	AlkAccentNet accNet;
	AlkVeg       veg;
	Lfo          lfo;          /* platform LFO pair 1 (as every engine) */

	/* --- parameters (descriptor bindings) --- */
	float   cutoff;            /* 0..1, VR3 */
	float   reso;              /* 0..1 knob, VR4: feedback k_eff = ALK_RES_KMAX x knob (S093 P6: x1.5 at 1; the 303's max at 1/1.5) */
	float   envMod;            /* 0..1, VR5 */
	float   decay;             /* 0..1, VR6 */
	float   accent;            /* 0..1, VR7 */
	float   tuning;            /* 0..1, centre 63/127: +-700 cents ([SN] p1) */
	float   drive;             /* 0..1: input gain 16^drive, x1..x16 (D16, E2; S093 P6 x16) */
	float   attack;            /* 0..1 (S093 P6, alk_attack): VEG attack as the CPU trigger width, 0 = 0.70 ms (today), 1 = 0.55 ms */
	float   vol;               /* 0..1, read by the mixer only */
	uint8_t pan;
	uint8_t slide;             /* 0/1, latched at each trigger */

	/* --- sequencing state --- */
	uint8_t note;              /* last trigger note */
	uint8_t slideLatch;        /* slide of the previous note (S092 P0 §5.3) */
	uint8_t gliding;           /* this note glides (legato) */
	uint8_t accentOn;          /* ACCENT lane */
	uint8_t pitchJump;         /* set the pitch to the target at the next block */
	int32_t gateLeft;          /* samples of gate left (counted per block) */
	int32_t accentLeft;        /* samples of ACCENT left (one step) */
	float   pitchLog2;         /* the slide-lagged pitch, log2 Hz */

	/* --- control results for the render --- */
	float   v134;              /* expo node (Q10 base) of the last block */
	float   iLadRaw;           /* ladder current before the prewarp (cutoff) */
	float   iLadEnd;           /* prewarped ladder current at the block end */
	float   iAbcEnd;           /* OTA control current at the block end */
	float   driveGain;         /* 16^drive, this block */
	float   makeup;            /* 2^(ALK_MAKEUP_OCT (1 - drive)), this block */
} AlkalineVoice;

void Alkaline_initVoice(AlkalineVoice *voice);
/*
 * Trigger the voice (S092 P0 §5.3/§5.4; the platform part S093 P4).
 *
 * What:       the LFO retrigger and the two velocity modulation paths for
 *             source_slot, as Drum_triggerVoice() (DrumVoice.c l. 242-255);
 *             then the 303 rules: accent at velocity >= ALK_ACCENT_VELOCITY
 *             (101); legato when the previous note was slide-flagged and its
 *             gate is still open (no MEG strike, no VEG trigger, the pitch
 *             glides); the slide flag latched for the next trigger; the gate
 *             7/12 of the step, or the step plus 7/12 after a slide-flagged
 *             note; the ACCENT lane one step long.
 * Why:        the LFO and the velocity nodes belong to the slot the voice
 *             occupies, not to the voice instance (InstrumentManager's tagged
 *             runtime), so the caller names the slot.
 * Inputs:     voice (the slot's runtime member, non-NULL: the caller's slot
 *             is always < INSTRUMENT_SLOT_COUNT); source_slot 0..5; velocity
 *             0..127; note (the step/MIDI note, 63 = the default); stepSamples
 *             (seq_trackStepSamples() of the triggering track, >= 1).
 * Outputs:    the voice's sequencing state; the slot's LFO phase and velocity
 *             nodes.
 * Accessors:  instrumentManager_triggerTrack() (the single trigger funnel:
 *             sequencer, MIDI, live and preview, InstrumentManager.c l. 2129).
 * Affiliates: Drum_triggerVoice(); lfo_retrigger() (lfo.h l. 141),
 *             modNode_updateValue() and velocityModulators[]
 *             (modulationNode.h), instrumentManager_applyVelocityModulation-
 *             Target() (InstrumentManager.h l. 543).
 */
void Alkaline_triggerVoice(AlkalineVoice *voice, uint8_t source_slot, uint8_t velocity,
                           uint8_t note, uint32_t stepSamples);
/* Both are called from inside mixer_calcNextSampleBlock(): noinline keeps
 * LTO from inlining them there (S092 P0 §1.3, the S086 F1 register-pressure
 * risk to the other engines' inlined loops). */
void Alkaline_calcVoiceAsync(AlkalineVoice *voice) __attribute__((noinline));
void Alkaline_calcVoiceSyncBlock(AlkalineVoice *voice, int16_t *buf, uint8_t size)
	__attribute__((noinline));
/*
 * Whether the voice is silent enough to swap (S093_P2 §5).
 *
 * What:       1 when the gate is closed, the ACCENT lane is low and the OTA
 *             current (before the make-up gain) is below 1e-7 A, so the OTA
 *             passes nothing audible; else 0.
 * Why:        instrumentManager_ampEnvelopeQuiet() otherwise returns 1 for a
 *             type without a SlopeEg2, which would let a Scene swap cut a
 *             sounding note.
 */
uint8_t Alkaline_isQuiet(const AlkalineVoice *voice);

/*
 * Set the oscillator type from the typ byte (S093 P8).
 *
 * What:       maps ALK_TYP_* to osc.oscType (0 wav, 1 and above wtb); clamps
 *             osc.waveform to the new type's list (wav: Saw/Sqr; wtb: the
 *             installed tables); clears the bind so the next block (or
 *             trigger) binds; re-anchors positionApplied to pos. The phase
 *             runs on (both types are 2^32 per cycle). An unchanged type is a
 *             no-op.
 * Why:        the platform's osc_setType() binds through Drum's generic
 *             osc_userBind() and reads the byte as OSC_TYPE_* (1 = smp); the
 *             Alkaline binds its own tables (P8 §7) and offers wav and wtb.
 * Inputs:     voice (non-NULL), typ (0..255).
 * Outputs:    osc.oscType, waveform, positionApplied, sampleCacheLooped.
 * Accessors:  InstrumentManager.c instrumentManager_writeSpecialRuntime()
 *             (IM_SPECIAL_OSC_TYPE, an Alkaline slot); the host bench.
 * Affiliates: ALK_TYP_*; alk_wtbBind() (AlkalineVoice.c).
 */
void Alkaline_setOscType(AlkalineVoice *voice, uint8_t typ);

#endif /* ALKALINEVOICE_H_ */
