/*
 * KatranVoice.h - the Katran drone / noise voice (S103 P1).
 *
 * What:       the eighth instrument type: a dual feedback core after the
 *             TORCIDO noise-synth patch (D§1). Per core: an exciter (OSC A,
 *             or an arena read) -> TORCIDO (two shaped channels) -> the loop
 *             filter F1 (TPT SVF) -> the low-cut F2 -> a 2,048-sample delay
 *             ring in one FxBuffer voice unit, whose output re-clocks the
 *             exciter (hard sync on a threshold crossing). OSC B rings each
 *             core's TORCIDO channel 1; the VCA pair gives the patch's
 *             asymmetric pair (core A = ring + AMP2(loop), core B = ring +
 *             AMP1(ring)). Then F3 (one coefficient set for both cores) and the
 *             stereo matrix: the rotation, the width, the chaos envelope CE1
 *             and the output scale in one ramped 2x2 gain (S103 P4). Stereo:
 *             buf receives L, the voice's right[] R
 *             (instrumentManager_slotRight()).
 *             S103 P2: core A runs at the full rate; core B at half rate, its
 *             output interpolated 2x (KAT_HALF_N below; S103_P2_KATRAN_DUAL_
 *             FULL-HALF_IMPLEMENTATION.md).
 *             S103 P4: the cores are A and B (they were L and R). They sound
 *             different by design, so the output stage rotates and spreads
 *             them across the field (kat_rotate, the width) and neither core
 *             belongs to one side (S103_P4_KATRAN_AB_ROTATION_CPU.md).
 * Why:        S103_KATRAN_NOISE_SYNTH.md revision 3; S103_P1_IMPLEMENTATION.md.
 *             The graph runs every block whatever the note state (constant
 *             CPU); triggers do not restart anything, they kick the loop.
 * Behaviour:  run (127 on; 0..126 a decay of 0.25..60 s), the kick (re-arm,
 *             burst, swell), the transport stop decay, boot silence until the
 *             first kick or start, and the pending-apply fade (P§3.1-§3.2).
 * RAM:        sizeof(KatranVoice) <= 1,200 B (InstrumentManager.c asserts it;
 *             the slot reserve is 1,264 B). No other RAM.
 * Affiliates: KatranOsc.h (the oscillator loops), KatranTables.h,
 *             KatranParameters.c (the rows bound into this struct),
 *             InstrumentManager.c (dispatch, units), FxBuffer.h
 *             (fxbuf_arenaView()).
 */
#ifndef KATRANVOICE_H_
#define KATRANVOICE_H_

#include <stdint.h>
#include "Oscillator.h"    /* OscInfo, OSC_TYPE_*, OUTPUT_DMA_SIZE */
#include "lfo.h"           /* Lfo (the platform LFO) */
#include "KatranTables.h"  /* the per-block law tables */

/* The two cores' delay rings (S103 P1 §4, C3). Each core owns one ring of
 * KAT_RING samples inside one FxBuffer voice unit; power of two so the wrap
 * is a mask. */
#define KAT_RING            2048u
#define KAT_RING_MASK       (KAT_RING - 1u)
/* The dry tap: one block back (K-D5, the delay's second read). */
#define KAT_TAP_DRY         32u
/*
 * The half-rate core (S103 P2, S103_P2_KATRAN_DUAL_FULL-HALF_IMPLEMENTATION.md).
 *
 * What:       core A runs at the full rate (32 samples a block); core B at
 *             half rate (KAT_HALF_N = 16 samples a block at fs / 2). Core B's
 *             ring, delay time (tNow[1], tStep[1]), dry tap (KAT_TAP_DRY_H,
 *             one half-rate block back) and loop filter run in half-rate
 *             samples; its output is interpolated to the full rate into
 *             right[] (the core B block, before the stereo matrix). Its delay
 *             time is pitch-matched: tNow[1] = tNow[0]'s
 *             law / 2, so the same tim gives the same comb pitch on both
 *             cores (spr and drift still separate them).
 * Why:        the two full-rate cores measured 534.7 instructions a sample
 *             (S103_P1_IMPLEMENTATION.md §15); a half-rate second core keeps
 *             two independent loops for about half the second core's cost
 *             (your decision, 2026-11-24). Core B's loop band stops at 11 kHz.
 */
#define KAT_HALF_N          (OUTPUT_DMA_SIZE / 2u)
#define KAT_TAP_DRY_H       16u
#define KAT_TIME_MIN_H      16.0f
/* The tim law's ends in samples (KatranTables.c katTimeSamples[]). The top
 * plus spread and drift stays inside the 2,048 ring (C3). */
#define KAT_TIME_MIN        32.0f
#define KAT_TIME_MAX        1792.0f
/* spr above its centre spreads the two delay times by +-12 % (P§8.2). */
#define KAT_SPREAD_TIME     0.12f
/* spr above its centre spreads F1 by +-12 semitones (P§8.2). */
#define KAT_SPREAD_F1_ST    12.0f
/* spr below its centre cross-couples the cores; at 0 the cross-feedback is
 * this (P§8.2). */
#define KAT_XFB_MAX         0.6f
/* The feedback clamp: fbk alone, and feedback + cross-feedback + swell
 * together (P§5). */
#define KAT_FB_CLAMP        0.95f
#define KAT_FB_CLAMP_SUM    1.1f
/* The arena read window: one wavetable frame (P§3.4). */
#define KAT_ARENA_WIN       1024u
/* The arena read form's one-pole DC block: ~10 Hz, k = 2 pi f / fs. */
#define KAT_DC_K            0.00142466f
/* The weight of OSC B's wrap in the sync sum (P§3.4/L3). */
#define KAT_SYNC_P          0.5f
/* The src selector: which oscillators read the arena (P§8.2). */
#define KAT_SRC_OFF         0u
#define KAT_SRC_A           1u
#define KAT_SRC_B           2u
#define KAT_SRC_AB          3u
#define KAT_SRC_COUNT       4u
/* F1's type: absolute LP/BP/HP, then the same three tracking the loop's
 * pitch (P§8.2 kat_loop_type). */
#define KAT_LT_LP           0u
#define KAT_LT_BP           1u
#define KAT_LT_HP           2u
#define KAT_LT_LPT          3u
#define KAT_LT_BPT          4u
#define KAT_LT_HPT          5u
#define KAT_LT_COUNT        6u
/* The loop forms (KatranOsc.c): a stock row / wavetable frame, the two-read
 * pulse, the warped triangle, or the arena window read. */
#define KAT_FORM_PLAIN      0u
#define KAT_FORM_PWMD       1u
#define KAT_FORM_WARP       2u
#define KAT_FORM_ARENA      3u
/* A pending-apply fade request lasts this many blocks without renewal
 * (Q9; one render block is ~0.73 ms). */
#define KAT_FADE_HOLD       64u
/* gr == 0 for this many blocks before the voice reports quiet: longer than
 * one ring, so every delay holds silence (KAT_RING / 32 + margin). */
#define KAT_QUIET_BLOCKS    72u
/* The K1 re-arm ramp: ~5 ms at 32 samples a block. */
#define KAT_KICK_REARM      7u
/* The output normalisation (P§5, by ear in the host model; the loudest case
 * is a full-level read of arena garbage). */
#define KAT_OUT_NORM        0.5f
/*
 * The rotation (S103 P4, kat_rotate).
 *
 * What:       rot 0 holds the rotation still; 1..127 turn it at
 *             KAT_ROT_HZ_MIN x (KAT_ROT_HZ_MAX / KAT_ROT_HZ_MIN)^((rot - 1) / 126)
 *             Hz: 0.01 Hz (a turn in 100 s) .. 4 Hz. The chaos row jitters
 *             each turn's rate by up to 2^(+-KAT_ROT_JIT x chs').
 * Why:        the two cores sound different (A full rate, B half rate), so
 *             they move around the field instead of owning L and R.
 */
#define KAT_ROT_HZ_MIN      0.01f
#define KAT_ROT_LOG2_SPAN   8.64385619f   /* log2(4 / 0.01) */
#define KAT_ROT_JIT         0.5f
/*
 * The bipolar width (S103 P5, kat_width; stored 63 = display 0).
 *
 * What:       display 0 is full spread with no collapse as the cores cross;
 *             0 .. -KAT_WID_BLEND blends to the collapsing pan law (reached at
 *             -12); below, that law narrows to mono at -63; above 0, wider
 *             (M/S side gain to 2) on the no-collapse form
 *             (KatranVoice.c kat_stereoTargets()).
 */
#define KAT_WID_BLEND       12
/* The wavetable bind tag in osc.sampleCacheLooped (the Colloid's rule). */
#define KAT_BIND_NONE       0u
#define KAT_BIND_WTB        1u

/*
 * One TPT state-variable filter, Katran form (S103 P1).
 *
 * What:       the Colloid's ColSvf (ColloidVoice.h) without the targets t1..t3:
 *             the next block's start is a + 32 da, computed at the start of
 *             the next control. ic1, ic2 the states; a1..a3 the coefficients
 *             at the block's start; da1..da3 the per-sample steps; mH mB mL
 *             the output mix; cutSt the slewed cutoff in semitones above
 *             20 Hz; k the slewed damping.
 * Why:        three floats fewer per filter (12 B x 3 filters) for the slot
 *             budget; the arithmetic is the Colloid's.
 */
typedef struct { float ic1, ic2, a1, a2, a3, da1, da2, da3, mH, mB, mL, cutSt, k; } KatSvf;

/*
 * One chaos envelope (S103 P1, P§3.3).
 *
 * What:       level 0..peak; up, down: the per-block steps of the current
 *             cycle's rise and fall (linear); peak: this cycle's top (1 minus
 *             the chaos draw); stage 0 rise, 1 fall.
 * Why:        a free-running cycling function generator; a new cycle draws
 *             new times and a new peak (chs), so the progression never
 *             repeats exactly. Never reset by a trigger (the brief).
 */
typedef struct { float level, up, down, peak; uint8_t stage; } KatCe;

/*
 * The voice (S103 P1 §2.1; the slot reserve is 1,264 B,
 * InstrumentManager.c asserts both this size and the plan's 1,200 B gate).
 *
 * Parameter members are written by the descriptor table
 * (KatranParameters.c: TYPE_UINT8 rows store the byte); everything below the
 * row bytes is the engine's own. The "now / step" pairs are the block's
 * start and its per-sample step: the async sets them, the sync loops ramp
 * them, and the sync advances "now" by 32 steps at the end of the block.
 */
typedef struct KatranVoiceStruct
{
	OscInfo  osc;                 /* OSC A pitch anchor; typ / wav / pos (platform rows) */
	Lfo      lfo;                 /* the platform LFO (as every engine) */
	int16_t  right[OUTPUT_DMA_SIZE];          /* core B's block in the render; the R output after kat_outRender() (the mixer reads it) */
	int16_t  blkD[2][OUTPUT_DMA_SIZE];        /* each core's delay output, this block ([0] A, [1] B) */
	int16_t  blkA[OUTPUT_DMA_SIZE];           /* OSC A of the core being rendered */
	int16_t  blkB[OUTPUT_DMA_SIZE];           /* OSC B; LSB = 1 on its wrap sample */
	int16_t *ring[2];             /* the two voice units (NULL: silent) */
	const int16_t *arena;         /* fxbuf_arenaView() base */
	uint32_t arenaN;              /* arena length in samples */
	uint32_t phA[2], incA[2];     /* OSC A: core A's copy, core B's copy */
	uint32_t phB, incB;           /* OSC B */
	const int16_t *rowA[2], *rowANext[2];     /* the row played; the row from the next cycle start */
	const int16_t *rowB, *rowBNext;
	uint16_t pwD[3], pwD0[3];     /* PWM D form: width at block end / start (A core A, A core B, B) */
	uint8_t  pwPos[3], pwPos0[3]; /* PWM width index, slewed (warp form) */
	/* S103 P4: dcY, f2y, f3R1 and f3R2 removed (written at init, read nowhere;
	 * 28 B, which pays for the stereo matrix and rotation state below) */
	float    dcX[3], damp[3], guard[3], dampK;   /* arena conditioning */
	float    tNow[2], tStep[2], drift[2], driftTgt[2];   /* delay times: [0] core A, full-rate samples; [1] core B, half-rate samples (S103 P2) */
	KatSvf   f1[2];               /* loop filters ([0] A, [1] B) */
	float    f2x[2], f2K;         /* loop low-cuts */
	KatSvf   f3;                  /* output filter: shared coefficients; ic1 core A's state, ic2 core B's */
	float    tGain, tGain2, tTube, tWet;   /* TORCIDO, this block */
	float    tExc, tNse;          /* the kick's effective exciter level / noise mix */
	float    kickSwell;           /* the kick's feedback swell, this block */
	float    gr, grStep;          /* run gain, now / per-sample step */
	float    kE;                  /* kick level */
	float    fb, fbStep, xfb;     /* feedback now / step; cross-feedback */
	/*
	 * S103 P4: the per-core output gains and the stereo matrix, each as now /
	 * per-sample step (the async sets the step to the block's target, the
	 * sync advances now by one block). They replace rk, g1, g2 (the ring
	 * crossfade and the VCA pair), ceg (CE1) and wid, folded with the int16
	 * scales into fewer per-sample multiplies (scale folding, S103 P2 §6 S1):
	 *   core A out = t1 (cA0 + cA1 B)   cA0 = (1 - rk + g2) 32767,
	 *                                   cA1 = rk bLvl 32767 / 32768;
	 *   core B out = t1 (cB0 + cB1 B)   cB0 = (1 - rk)(1 + g1) 32767,
	 *                                   cB1 = rk bLvl (1 + g1) 32767 / 32768;
	 *   L = mx0 a + mx1 b,  R = mx2 a + mx3 b  (a, b: the cores after F3;
	 *   mx[] the rotation, the width, CE1, KAT_OUT_NORM and 32767 / 32768;
	 *   S103 P5: four gains, was the symmetric pair mP / mQ: the no-collapse
	 *   form of the bipolar wid is not symmetric. +16 B.)
	 * rotPh is the rotation's phase in turns (0 = core A left); rotJit the
	 * current turn's chaos jitter in octaves.
	 */
	float    cA0, cA0Step, cA1, cA1Step, cB0, cB0Step, cB1, cB1Step;
	float    mx[4], mxStep[4];
	float    rotPh, rotJit;
	KatCe    ce[2];
	uint32_t rng;                 /* xorshift for CE and drift draws */
	float    lfoPh, cv, ringPrev; /* patch LFO */
	float    plfoJit;             /* the patch LFO's per-cycle chaos jitter */
	float    vol;                 /* instrument_vol (the mixer reads it) */
	uint32_t w[2];                /* each ring's write index */
	int32_t  syncPrev[2];         /* each core's last sync-test value */
	/*
	 * S103 P2: core B's last half-rate output sample, carried across
	 * blocks for the 2x linear interpolation into right[] (S103 P4: in
	 * int16 units, the output scale is folded into cB0 / cB1):
	 * right[2k] = (hPrev + o[k]) / 2,
	 * right[2k + 1] = o[k]. RAM: 4 B inside the slot reserve (the 1,200 B
	 * static assert in InstrumentManager.c still gates the size).
	 */
	float    hPrev;
	/* --- row bytes (KatranParameters.c binds them; converted in the async) --- */
	uint8_t  spreadA, source, damp_, run, kick, ce1Cycle, ce1Skew, ce1Depth,
	         ce2Cycle, ce2Skew, ce2Depth, chaos, plfoRate, plfoMod, plfoLevel,
	         vca, bCoarse, bFine, bPos, bLevel, levelA, noise, sync, drive,
	         wet, time, feedback, drift_, track, loopCut, loopRes, loopType,
	         lowCut, outCut, spread, width, pan,
	         rotate;              /* S103 P4: kat_rotate (index 57) */
	uint8_t  fxReserved[4];       /* the later insert effect's rows (P§9); not bound */
	/* --- control --- */
	uint8_t  slot, note, wasRunning, armed, fadeReq, stopDecay, rearm, driftTick;
	uint16_t silentBlocks;
} KatranVoice;

/*
 * Entry points (S103 P1).
 *
 * Katran_initVoice()         the init.kat values; slot; the two voice units
 *                            (core A's ring, core B's ring; S103 P4 names)
 *                            (either may be NULL: the voice then renders
 *                            silence); clears both rings; reads the arena view.
 * Katran_triggerVoice()      the kick: platform LFO retrigger and velocity
 *                            node, then K1 re-arm, K2 burst, the note.
 * Katran_calcVoiceAsync()    the block's control (render context).
 * Katran_calcVoiceSyncBlock() the block's audio: L into buf, R into right[]
 *                            (core A and core B through the stereo matrix).
 * Katran_isQuiet()           gr has been 0 for KAT_QUIET_BLOCKS blocks.
 * Katran_requestFade()       a deferred Scene/Kit apply is waiting: fade gr
 *                            to 0 over ~20 ms and hold it there while renewed.
 * Katran_setOscType()        the typ writer (wav / wtb / pwm).
 * The two render entries are noinline so LTO keeps them out of
 * mixer_calcNextSampleBlock() (S086 F1; S101 P3 §4).
 */
void Katran_initVoice(KatranVoice *voice, uint8_t slot, int16_t *ringA, int16_t *ringB)
	__attribute__((noinline, noclone));
void Katran_triggerVoice(KatranVoice *voice, uint8_t source_slot, uint8_t velocity, uint8_t note);
void Katran_calcVoiceAsync(KatranVoice *voice) __attribute__((noinline));
void Katran_calcVoiceSyncBlock(KatranVoice *voice, int16_t *buf, uint8_t size)
	__attribute__((noinline));
uint8_t Katran_isQuiet(const KatranVoice *voice);
void Katran_requestFade(KatranVoice *voice);
void Katran_setOscType(KatranVoice *voice, uint8_t typ);

#endif /* KATRANVOICE_H_ */
