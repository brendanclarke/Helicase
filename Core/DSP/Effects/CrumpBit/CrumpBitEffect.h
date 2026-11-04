/*
 * Core/DSP/Effects/CrumpBit/CrumpBitEffect.h
 *
 *  Created on: 29.09.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#ifndef CRUMP_BIT_EFFECT_H_
#define CRUMP_BIT_EFFECT_H_

#include "EffectsManager.h"

/*
 * CrumpBit loop geometry (S074_CRUMPBIT_EFFECT.md §3.4; elastic since S091
 * P2 §5.1; retuned in S091 P3 N1).
 *
 * CRUMPBIT_DELAY_CURVE_MAX: the top of the Rate curve, 126,654 samples
 * (2.871 s at 44,108 Hz) = crumpBit_rateTable[127]: the arena less one
 * 32 B granule and the 2-sample interpolation guard. S091 P3 grew the
 * runtime by 16 B; whether `.dtcmz` then crosses a 32 B boundary (moving
 * `_sfxbuf` up one granule, 126,688 -> 126,656 B) depends on how LTO packs
 * the DTCM statics (the measured S091 P3 build absorbed it in padding). The
 * curve top is set to the smaller arena, so `rte` 127 fits either way (N1,
 * user); a 126,688 B arena leaves 32 B unused.
 * CRUMPBIT_LOOP_MAX_BYTES: the longest loop CrumpBit seats, the curve top plus
 * the guard (126,656 B, a multiple of 32). A
 * larger share is not used beyond it; a smaller share (voice units claimed
 * from the top) shortens the loop, and the Rate row and the DSP target are
 * clamped to what it holds (the loop is elastic).
 * CRUMPBIT_LOOP_MIN_SHARE_BYTES: the share left at this arena size with all
 * twelve voice units claimed (73,664 B): declared as buffer_min_bytes.
 * CrumpBit still works below it (down to CRUMPBIT_LOOP_MIN samples); the
 * field is declarative.
 * Accessors: CrumpBitEffect.c, EffectsManager.c (registry row).
 * Affiliates: FxBuffer.h FXBUF_VOICE_UNIT_*, tools/plots/gen_crumpbit_tables.py
 * (RATE_MAX), tools/diagnostic_tools/link_budget.py (FXBUF size).
 */
#define CRUMPBIT_DELAY_CURVE_MAX       126654.0f
#define CRUMPBIT_LOOP_MAX_BYTES        126656u
#define CRUMPBIT_LOOP_MIN_SHARE_BYTES                                   \
    (CRUMPBIT_LOOP_MAX_BYTES -                                          \
     FXBUF_VOICE_UNIT_COUNT * FXBUF_VOICE_UNIT_BYTES)

/*
 * Single-line tape state: `dly` and `cmb` (engine A; S091 P3 §2.2).
 *
 * What: the S091 P2 delay state, now one member of the sub-type union.
 * - length, write_pos, valid: the COMMON PREFIX of every single-line
 *   sub-type (`dly`, `cmb`, `pit`): loop length in bytes (0 = unseated), the
 *   next index to write, and the samples written since the loop was last
 *   treated as empty (reads further back are muted). It sits at offset 0 in
 *   each of them, so a switch between them keeps the recording (§2.4);
 * - delay: the gliding delay in samples (tape glide; never above
 *   length - 2);
 * - dc_x, dc_y: the in-loop DC blocker memories (feedback can exceed 1);
 * - tone_s: the one-pole state of the `ton` tilt on the feedback return.
 * Size: 28 B. Accessors: CrumpBitEffect.c only.
 */
typedef struct {
    uint32_t length;
    uint32_t write_pos;
    uint32_t valid;
    float delay;
    float dc_x;
    float dc_y;
    float tone_s;
} CrumpBitTapState;

/*
 * Pitch-shifter state: `pit` (engine B; S091 P3 §3.2).
 *
 * What: the common prefix (length, write_pos, valid, as CrumpBitTapState),
 * then phase: the rotating-head phase, an unsigned 0.32 fraction of a turn
 * that wraps by integer overflow (head A trails the write head by
 * 2 + W*phase, head B by 2 + W*(phase + 1/2), the half turn being one
 * 32-bit add), and the same feedback-return memories as the tape (dc_x,
 * dc_y, tone_s).
 * Size: 28 B. Accessors: CrumpBitEffect.c only.
 */
typedef struct {
    uint32_t length;
    uint32_t write_pos;
    uint32_t valid;
    uint32_t phase;
    float dc_x;
    float dc_y;
    float tone_s;
} CrumpBitPitchState;

/*
 * Granular state: `grn` (engine B'; S091 P3 §3.3).
 *
 * What: the share is split into a record line (first half) and a delay line
 * (second half) of `length` samples each, written at the same write_pos.
 * - write_pos, length, valid: 16-bit, because a line is at most 63,328
 *   samples (half the arena);
 * - age: the samples since grain A started (grain B is half a grain later);
 * - pos_a, pos_b: the grains' read positions in the record line, 16.16 fixed
 *   point (integer part = sample index; drop-sample reads, user G6);
 * - inc_a, inc_b: their pitch steps, signed Q3.12 (ratio x 4096; negative =
 *   reversed); pos += inc << 4 per sample;
 * - dc_x, dc_y: the delay line's feedback DC blocker;
 * - amp_a, amp_b, pan_a, pan_b: each grain's level and pan (0..255), drawn
 *   at its restart from `amp` and `wid`.
 * The delay-line tap has no state: it reads exactly D = R behind the write
 * head, with no tape glide (user, 2026-11-04: not wanted in `grn`); a
 * change of R jumps.
 * Size: 32 B (the union's largest member). Accessors: CrumpBitEffect.c.
 */
typedef struct {
    uint16_t write_pos;
    uint16_t length;
    uint16_t valid;
    uint16_t age;
    uint32_t pos_a;
    uint32_t pos_b;
    int16_t inc_a;
    int16_t inc_b;
    float dc_x;
    float dc_y;
    uint8_t amp_a;
    uint8_t amp_b;
    uint8_t pan_a;
    uint8_t pan_b;
} CrumpBitGrainState;

/*
 * Reverb state: `rvb` (engine C; S091 P3 §3.4).
 *
 * What: the share is used as 32-bit words, one per sample, each holding the
 * four FDN lines' 8-bit values (one lane per line). write_pos and length are
 * in words; valid counts words written since the loop was treated as empty
 * (the wet path and the line feedback open per block once it covers the
 * longest line); damp[4] are the per-line one-pole states of the `ton`
 * damping.
 * Size: 28 B. Accessors: CrumpBitEffect.c only.
 */
typedef struct {
    uint32_t write_pos;
    uint32_t length;
    uint32_t valid;
    float damp[4];
} CrumpBitReverbState;

/*
 * CrumpBit runtime (88 B since S091 P3, a member of EffectsManager's DTCM
 * runtime union; user-approved growth up to +32 B, 2026-11-04).
 *
 * What: a SHARED part used by every sub-type (stages 1, 3 and 4 and all row
 * bytes), then a UNION of the sub-engines' DSP states (stage 2).
 * Shared part (56 B):
 * - ac_y_*, ac_c_*: stage 1 AC coupling (outputs; previous codes, int16,
 *   seeded 128 by init);
 * - svf_*: the output filter's two TPT integrators per channel (stage 4);
 * - row bytes, written by write_param() whatever the sub-type: their
 *   meaning changes per sub-type, their storage does not (plan §2.2);
 * - subtype: the resolved `sub` row; active_subtype: the engine that ran
 *   last block. A difference triggers crumpBit_enter() (plan §2.4);
 * - *_prev: the raw rows the previous block ramped to (ramp origins);
 * - primed: 0 until the first block of a tape sub-type starts its glide on
 *   the target;
 * - drv_reserved: the deferred output-filter drive row's byte (plan OQ6).
 * Union (32 B): one of the five states above.
 * Why here: EffectsManager zeroes the union and calls init() when the type
 * becomes active. Accessors: CrumpBitEffect.c only; no other module may keep
 * a pointer. Affiliates: effects_runtime_t, the size guard in
 * CrumpBitEffect.c, STORAGE_SRAM_MANIFEST.md.
 */
typedef struct {
    float ac_y_l;              /* stage 1 */
    float ac_y_r;              /* stage 1 */
    float svf_l1;              /* stage 4: TPT band integrator, left */
    float svf_l2;              /* stage 4: TPT low integrator, left */
    float svf_r1;              /* stage 4: TPT band integrator, right */
    float svf_r2;              /* stage 4: TPT low integrator, right */
    int16_t ac_c_l;            /* stage 1: previous code, left */
    int16_t ac_c_r;            /* stage 1: previous code, right */
    uint8_t bit_off;           /* row 3, shared */
    uint8_t bit_invert;        /* row 4, shared */
    uint8_t mix_raw;           /* row 5, shared: the mix stage */
    uint8_t feedback_raw;      /* row 6, sub-type row */
    uint8_t rate;              /* row 7, sub-type row */
    uint8_t subtype;           /* row 8, shared: resolved sub-type */
    uint8_t sync;              /* row 9, sub-type row */
    uint8_t pan_raw;           /* row 10, sub-type row */
    uint8_t tone_raw;          /* row 11, sub-type row */
    uint8_t filter_type;       /* row 12, shared */
    uint8_t filter_freq;       /* row 13, shared */
    uint8_t filter_reso;       /* row 14, shared */
    uint8_t aux_raw;           /* row 15, sub-type row (pitch) */
    uint8_t grain_st2;         /* row 16, grn */
    uint8_t grain_st3;         /* row 17, grn */
    uint8_t grain_spread;      /* row 18, grn */
    uint8_t grain_density;     /* row 19, grn */
    uint8_t grain_amp;         /* row 20, grn */
    uint8_t grain_width;       /* row 21, grn */
    uint8_t active_subtype;    /* the engine that ran last block */
    uint8_t mix_prev;          /* mix stage ramp origin */
    uint8_t feedback_prev;     /* sub-type ramp origin */
    uint8_t pan_prev;          /* sub-type ramp origin */
    uint8_t filter_type_prev;  /* stage 4 ramp origin */
    uint8_t filter_reso_prev;  /* stage 4 ramp origin */
    uint8_t primed;            /* tape glide primed */
    uint8_t drv_reserved;      /* deferred drive row (plan OQ6) */
    uint8_t pad;               /* alignment of the union */
    union {
        CrumpBitTapState tap;      /* dly, cmb */
        CrumpBitPitchState pit;    /* pit */
        CrumpBitGrainState grn;    /* grn */
        CrumpBitReverbState rvb;   /* rvb */
    } u;
} CrumpBitRuntime;

/* DSP operations for the registry row (init, export, write, process, share). */
extern const effect_type_ops_t crumpBit_ops;

/*
 * Free-running delay for one Rate value (reversed and extended in S091 P2).
 *
 * What: crumpBit_rateTable[rate], t = 20 ms * (2.872 s / 20 ms)^(rate/127)
 * in samples: rate 0 = 882 (20 ms), rate 127 = 126,686 (2.872 s), about 18
 * steps per octave of time. Higher rate now means a LONGER delay (plan Q2):
 * a shrinking share then removes the top of the range, which the top-down
 * effective_max clamp can express (plan N1). The value is not clamped to a
 * share here. Why shared: the DSP, the share clamp and the Effect-page Sync
 * label must use one curve. Input: rate 0..127 (larger values are treated as
 * 127). Output: delay in samples.
 * Accessors: crumpBit_syncDivision(), CrumpBitEffect.c per-block target.
 * Affiliates: crumpBit_rateTable, CRUMPBIT_DELAY_CURVE_MAX, crumpBit_rateLimit().
 */
float crumpBit_rateSamples(uint8_t rate);

/*
 * Longest delay a share holds (S091 P2).
 *
 * What: min(share bytes, CRUMPBIT_LOOP_MAX_BYTES) - 2 samples (the
 * interpolation guard), as a float; 0 when there is no share or it is
 * smaller than the smallest usable loop. It is exactly the clamp
 * crumpBit_process() applies to its target for the same share.
 * Input: share (may be NULL). Output: samples.
 * Accessors: crumpBit_rateLimit(), crumpBit_uiFormatValue3() (Sync label),
 * crumpBit_process() (target clamp).
 */
float crumpBit_shareMaxSamples(const fx_share_t *share);

/*
 * Largest Rate whose free-running delay fits a share (S091 P2).
 *
 * What: the highest rate r with crumpBit_rateTable[r] <= the share's longest
 * delay, by a fixed 7-step binary search of the ascending table (the same
 * shape as the Sync search, constant cost); 0 if even the shortest delay does
 * not fit. With the whole arena it is 127. Why: the manager clamps the
 * resolved `rte` to it before write_param() (effective_max), the page clamps
 * edits to it, and the label shows the clamped value, so a voice-unit
 * reservation never plays or shows a delay the loop cannot hold.
 * Input: share (may be NULL). Output: 0..127.
 * Accessors: crumpBit_ops.effective_max, crumpBit_uiFormatValue3().
 * Affiliates: crumpBit_shareMaxSamples(), crumpBit_rateTable.
 */
uint8_t crumpBit_rateLimit(const fx_share_t *share);

/*
 * Step-scale division that Sync snaps a Rate to (Q7; live maximum S091 P2).
 *
 * What: among the shared StepScale CC positions (0..127, 96 PPQ ticks) whose
 * length at the given tempo fits max_samples, returns the CC nearest in log
 * time to crumpBit_rateSamples(rate). It uses the bracketing pair and the
 * geometric-mean rule: no logarithms, and a fixed 7-step binary search over
 * the 128 ascending positions (S091 R1). If no division fits it returns 0,
 * and the DSP clamps that length to the maximum. Since S091 P2 the limit is
 * the live share (crumpBit_shareMaxSamples()), not a constant, so `1br` at
 * 120 BPM (2.0 s) is offered while the share holds it.
 * Why shared: the Effect-page label must show exactly the division the DSP
 * plays. Inputs: rate 0..127 (the caller passes the share-clamped value),
 * bpm from seq_getBpm() (0 is treated as 1), max_samples from
 * crumpBit_shareMaxSamples() for the same share the DSP uses.
 * Output: a StepScale CC 0..127.
 * Accessors: crumpBit_uiFormatValue3() (label), CrumpBitEffect.c (target).
 * Affiliates: stepScale_ticksQ8(), stepScale_formatShort().
 */
uint8_t crumpBit_syncDivision(uint8_t rate, uint16_t bpm, float max_samples);

/*
 * Sub-engine helpers shared with the page (S091 P3).
 *
 * crumpBit_subtypeRateLimit(): the largest `rte` whose length fits the share
 * under one sub-type's law (delay curve, comb table, pitch window, a `grn`
 * half line, the `rvb` word buffer); the effective_max op and the Rate label
 * use it. crumpBit_grainMaxSamples(): the longest `grn` time, one half line
 * less the guard. crumpBit_combNote(): the MIDI note a `cmb` `rte` plays
 * (112 - rte, clamped at 96), for the note-name label.
 * Inputs: sub-type id, share (may be NULL), or the raw `rte`. Outputs: as
 * named. Accessors: CrumpBitEffect.c, crumpBit_uiFormatValue3().
 */
uint8_t crumpBit_subtypeRateLimit(uint8_t subtype, const fx_share_t *share);
float crumpBit_grainMaxSamples(const fx_share_t *share);
uint8_t crumpBit_combNote(uint8_t rate);

/*
 * Per-sub-type FX lane maps (S091 P3 §2.9).
 *
 * What: the 16-entry lane -> row table for one `sub` value. Nine lanes are
 * shared by every map (0 Morph, 1 bof, 2 biv, 3 mix, 7 vol, 8 pan, 11 typ,
 * 12 frq, 13 res; lane 3 `mix` is never freeable); the seven-lane pool
 * (4, 5, 6, 9, 10, 14, 15) is assigned per sub-type (user L1/L2). The `dly`
 * map is the S091 P2 table, so `.fx` files round-trip unchanged.
 * Input: the `sub` value (anything above the last id reads the last map).
 * Output: a const 16-entry table in flash.
 * Accessors: crumpBit_ops.lane_map (EffectsManager's effects_laneTable());
 * defined in CrumpBitEffect.c beside the ops table (host-tested, CB9).
 */
const uint8_t *crumpBit_laneMap(uint8_t variant);

#endif /* CRUMP_BIT_EFFECT_H_ */
