# S089 P2 — Wavefolder Voice Chain: Implementation Schedule

**Plan:** `S089_P2_WAVEFOLDER_CHAIN.md`
**Baseline:** the S089 P0 tree as it passed hardware (HEAD `b4d3db8f` plus the
uncommitted P0 change: `instrumentManager_slotUsesSrr()`, the mixer gate, the
Makefile `link_budget.py` path). All line numbers below are for that tree.
**Status:** schedule only; no code has been changed.

---

## 0. Decisions applied (user, 2026-10-30)

| # | Decision | Applied as |
|---|---|---|
| D1 | Snare folds its tone oscillator | §8.2: the fold runs on `transBuf` after the osc render and `itp` blend |
| D2 | Filter drive kept for every type; the +1 over budget is accepted | every type keeps `sft`; the estimates below do not need the +1, but it stays as measurement margin for the sine family on Drum/Snare |
| D3 | DC compensation only | a per-block constant; no DC blocker, no state |
| D4 | ITCM / RAM expansion approved | `voiceFold.c` code in `.itcm.s089` (≈1.4 KB estimated); 0 B SRAM/DTCM growth (§6) |
| D5 | All 8 types | `Tri TrD Sin SnD Asn Chb TrI Fd2` |
| D6 | Fine-control depth mapping | `depth = 1 + span × vol²` (span 7, or 15 for `TrI`) |
| D7 | `frq` shown as raw 0–127 | no new display type |

## 1. Findings from the deep-dive that change the plan

The plan's intent is kept. Each change below lowers cost or risk.

1. **Integer triangle core for every fold (cheaper than planned).** The
   triangle fold is computed in Q22 integers:
   - `smlawb` (depth × sample + bias);
   - `sbfx` 24 bits (the period-4 wrap);
   - `|·|`;
   - one subtract.

   A host check against the S084 spike's float `driveShaper_triFold()`
   matched to 4.8×10⁻⁷ for every int16 input at depths 1, 2.5 and 8 and
   biases −0.3…+0.3. The estimated per-sample costs fall well under budget:

   | Loop | Types | Est. | Drum/Snare (24) | Cym/HiHat (26) |
   |---|---|---:|:-:|:-:|
   | `voiceFold_triLoop` | Tri, TrD, TrI | ≈11 | ✓ | ✓ |
   | `voiceFold_sinLoop` | Sin, SnD, Asn | ≈20 | ✓ | ✓ |
   | `voiceFold_chbLoop` | Chb | ≈14 | ✓ | ✓ |
   | `voiceFold_fd2Loop` | Fd2 | ≈17 | ✓ | ✓ |

   The per-block setup in `voiceFold_process()` is about 80 instructions per
   block (≈2.5 per sample). The transient's own per-block setup, which is
   removed, is of similar size.
2. **`Asn` is redefined so it stays inside budget.** The spike's `afd`
   applied a cubic accent *before* the fold (x + 0.05x³), which measures
   ≈+3 over `Sin`. P2 instead adds an even-order accent *after* the fold:
   `y = sinfold(t) + a·t²`. It reuses the `t²` the polynomial already
   computes, so it adds about one multiply. It is still an asymmetric sine
   fold with the asymmetry on `frq`, and its output is exactly 0 at
   silence.
3. **5th-order sine polynomial**, fitted with p(1) = 1:
   `1.5703368 t − 0.6421070 t³ + 0.0717703 t⁵`. The maximum error is
   1.04×10⁻⁴ (the spike's 7th-order was 1.57×10⁻⁴), and |p| ≤ 1, so it can
   never overflow.
4. **`Tri`, `TrD` and `TrI` share one loop.** `TrI` differs by its depth
   range (×1–×16 instead of ×1–×8), so it is denser. `TrD` has no bias.
5. **Snare normalisation.** Snare renders the tone oscillator already scaled
   by `(1 − mix)`. The fold gets `inGain = 1/(1 − mix)` (capped at 16) and
   `outGain = (1 − mix)`, so fold depth does not change with the noise mix.
   Both factors fold into per-block constants, so they cost nothing per
   sample.
6. **Cymbal/HiHat `pitchMod`.** The `Snp` path *assigns*
   `osc.pitchMod = 1 + snap` and nothing else resets it after init. Under
   `WFd` the render writes `pitchMod = 1.0f` every block, so a chain switch
   during a snap cannot leave the pitch stuck. (The same latent stuck-pitch
   exists in `T+D` when switching from `Snp` to another transient mid-snap.
   It is recorded in §16 and left unchanged, to keep `T+D` S0.)
7. **The `chn` selector uses short names only (`T+D`, `WFd`), like
   `osc1_run`.** The P1 plan's 8-character chain long names are dropped: no
   tagged on/off cell has a long-name path, and adding one would touch the
   single-parameter painter for no functional gain. The single-parameter
   view shows the category and long name `Voice / Chain` with the value on
   the right.
8. **Engine struct growth.** `chainType` goes directly after `ampRampArm`:
   - Drum (offset 571), Snare (419) and Cymbal (471) use existing padding,
     so they stay 632, 452 and 508 B.
   - HiHat grows 516 → 520 B, still under the 632 B DrumVoice that sizes
     every slot.
   - `runtime_slots` stays **7,584 B**, so no SRAM or DTCM data moves.
9. **ITCM ordering is pinned.** Today `.itcm.*` subsections are laid out in
   LTRANS partition order. The linker script is changed (§2) so the
   existing `.itcm.s085` and `.itcm.s087` keep their addresses and
   `.itcm.s089` always comes last. That keeps the S085 rule that existing
   loops never move.
10. **The host test law 6** ("`osc1_run` is the last Drum row") changes
    meaning: `voice_chain` becomes the last row (§14).
11. **Step-automation max.** `menu_automationValueMax()` has no slot, so the
    chain-aware maximum is applied at the one caller that knows the slot
    (`menu.c:13972`). Values that reach the engine any other way are
    clamped when the fold renders.

---

## 2. Order of work and gates

| Step | What | Gate after the step |
|---|---|---|
| 1 | Linker: pin the ITCM order (§2) | `build/lxr02.bin` byte-identical to the P0 build |
| 2 | `voiceFold.h/.c`, the tables, the generator, the Makefile (§3–§5) | builds; the new code is unreferenced, so it is dropped by `--gc-sections` and the image is identical |
| 3 | Engine fields and init (§6–§7) | builds; `runtime_slots` 7,584 B; Drum/Snare/Cymbal sizes unchanged |
| 4 | `voicePost_gainDist()` (§9) | builds |
| 5 | Engine guards and dispatch (§8) | builds; with every `chainType` 0, every existing mixer hot loop is unchanged (P0 §4.2 gates 1–4) |
| 6 | Descriptors, MenuText, InstrumentManager, menu (§10–§13) | builds; special tags OK; the host `instrument_manager` test passes |
| 7 | Host tests (§14) and the cost gate (§15) | every law passes; each fold loop is within §1's table |
| 8 | Hardware (§15) | the matrix passes |

---

## 3. Linker script: pin the ITCM subsection order

**File:** `STM32F765VIHx_FLASH.ld`, **line 150. MODIFY.**

```diff
-        *(.itcm) *(.itcm.*)
+        /*
+         * ITCM input order (S089 P2).
+         *
+         * What:       .itcm first, then the named S085 and S087 subsections
+         *             (and their linker-generated .itcm.s087.* stubs) in
+         *             their current order, then every other .itcm.*
+         *             subsection, which from S089 P2 is .itcm.s089.
+         * Why:        a bare *(.itcm.*) lays subsections out in LTRANS
+         *             partition order, so a new subsection in another
+         *             partition could land before .itcm.s085 and move
+         *             every existing ITCM loop, changing their short (b.n)
+         *             tail branches. That is the S085 failure this rule
+         *             exists to prevent (Oscillator.c S085 ITCM note).
+         *             Naming the existing subsections keeps today's
+         *             addresses exactly; new code is always appended.
+         * Check:      with no other change the image must be byte-identical
+         *             to the previous build (S089 P2 schedule step 1).
+         * Affiliates: Oscillator.c OSC_S085_SECTION / OSC_S087_SECTION;
+         *             voiceFold.c VOICE_FOLD_SECTION; the ASSERT below.
+         */
+        *(.itcm) *(.itcm.s085) *(.itcm.s087 .itcm.s087.*) *(.itcm.*)
```

---

## 4. New file: `Core/DSPAudio/voiceFold.h`

**ADD (new file).**

```c
/*
 * Core/DSPAudio/voiceFold.h
 *
 *  Created on: 30.10.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * Wavefolder voice chain (S089 P2, S089_P2_WAVEFOLDER_CHAIN.md).
 *
 * What:       the voice-chain selector values, the eight fold types and the
 *             one entry point the four engines call once per block when their
 *             chainType is VOICE_CHAIN_WAVEFOLDER.
 * Why:        the WFd chain replaces the transient stage with a wavefolder
 *             ahead of the filter (Drum, Cymbal, HiHat) or on the tone
 *             oscillator (Snare). The fold reuses the transient page's
 *             controls and state, so the chain adds no RAM:
 *               wav  (transGen.waveform) = fold type,
 *               vol  (transGen.volume)   = depth,
 *               frq  (transGen.pitch)    = per-type second control,
 *               transGen.midiFreq        = last block's depth (Q11),
 *               snapEg.value             = depth-decay envelope (TrD, SnD).
 * Inputs:     the engine's TransientGenerator and SnapEg, the block, and the
 *             engine's input/output gain normalisation.
 * Outputs:    the block folded in place.
 * Accessors:  Drum_calcVoiceSyncBlock(), Snare_calcSyncBlockVoice(),
 *             Cymbal_calcSyncBlockVoice(), HiHat_calcSyncBlockVoice();
 *             InstrumentManager (VOICE_CHAIN_COUNT clamp, fold-type accessor);
 *             menu.c (name-table asserts).
 * Affiliates: voiceFold.c, voiceFoldTables.c, MenuText.h (voiceChainNames,
 *             foldTypeNames, foldTypeLongNames), the four engines' chainType.
 */
#ifndef VOICEFOLD_H_
#define VOICEFOLD_H_

#include <stdint.h>
#include "transientGenerator.h"
#include "snapEg.h"

/*
 * Voice chain selector values (S089 P2; stored by the voice_chain row).
 *
 * What:       0 T+D = the default chain (transient + filter drive +
 *             distortion), bit-identical to S089 P0; 1 WFd = the wavefolder
 *             in place of the transient. VOICE_CHAIN_COUNT is the number of
 *             stored values.
 * Why:        one selector, one chain, one budget (P1 §7). A file without
 *             the key loads 0, so every existing sound keeps T+D.
 * Accessors:  the IM_SPECIAL_VOICE_CHAIN writer (clamps to COUNT-1); the
 *             engines; instrumentManager_slotVoiceChain(); menu.c.
 * Affiliates: voiceChainNames (MenuText.h), whose count must equal
 *             VOICE_CHAIN_COUNT (asserted in menu.c).
 */
#define VOICE_CHAIN_DEFAULT     0u
#define VOICE_CHAIN_WAVEFOLDER  1u
#define VOICE_CHAIN_COUNT       2u

/*
 * Fold types: the transient_wave byte under WFd (S089 P2 §3).
 *
 * What:       wav value -> fold. A stored value >= VOICE_FOLD_COUNT (left
 *             over from a T+D click selection) renders as the last type and
 *             is never rewritten, so switching back to T+D restores the click.
 *               0 Tri  triangle fold,          frq = bias
 *               1 TrD  triangle fold, decay,   frq = depth-decay time
 *               2 Sin  sine fold,              frq = bias
 *               3 SnD  sine fold, decay,       frq = depth-decay time
 *               4 Asn  asymmetric sine fold,   frq = even-harmonic accent
 *               5 Chb  Chebyshev T3,           frq = dry -> T3 blend
 *               6 TrI  triangle fold x1-x16,   frq = bias
 *               7 Fd2  two cascaded folds,     frq = second-stage ratio 1-4
 * Why:        plan §3 with the S089 P2 implementation §1 refinements.
 * Accessors:  voiceFold_process(); instrumentManager_slotFoldType(); menu.c.
 * Affiliates: foldTypeNames / foldTypeLongNames (MenuText.h).
 */
#define VOICE_FOLD_TRI      0u
#define VOICE_FOLD_TRD      1u
#define VOICE_FOLD_SIN      2u
#define VOICE_FOLD_SND      3u
#define VOICE_FOLD_ASN      4u
#define VOICE_FOLD_CHB      5u
#define VOICE_FOLD_TRI_INT  6u
#define VOICE_FOLD_FD2      7u
#define VOICE_FOLD_COUNT    8u

/*
 * Largest input gain an engine may pass (S089 P2 §1 item 5).
 *
 * What:       Snare passes 1/(1 - mix) to undo the tone oscillator's render
 *             gain; above 16 (mix > 0.9375) the tone is near silence, so the
 *             gain is capped instead of growing without bound.
 * Why:        keeps depth x inGain <= 128, so the Q23 depth word stays
 *             <= 2^30 and the smlawb product cannot overflow (voiceFold.c).
 */
#define VOICE_FOLD_IN_GAIN_MAX  16.0f

/*
 * Fold one block in place (S089 P2).
 *
 * What:       reads the fold type, depth and frq control from the
 *             TransientGenerator, advances the TrD/SnD decay envelope in
 *             SnapEg, ramps the depth per sample from the previous block's
 *             value (transGen.midiFreq), and runs the type's loop.
 * Why:        one shared, non-inlined copy for all four engines, so 4 x 8
 *             loops are not inlined into mixer_calcNextSampleBlock() (S089 P0
 *             F3: that function's LTO inlining budget is at its limit).
 * Inputs:     tg (fold controls and the depth word), eg (the decay
 *             envelope; SnapEg_trigger() sets it to 1 on every hit), buf/size
 *             (one block, size <= OUTPUT_DMA_SIZE), inGain (input level
 *             normalisation, >= 1, capped at VOICE_FOLD_IN_GAIN_MAX) and
 *             outGain (output level, 0..1).
 * Outputs:    buf folded in place; tg->midiFreq (this block's end depth);
 *             eg->value (decay types only).
 * Cost:       constant per type, never data-dependent (CPU mandate): the
 *             loops have no data branches. See S089_P2_IMPLEMENTATION.md §1.
 * Accessors:  the four engine sync renders (WFd only).
 * Affiliates: voiceFold_decayStep[] (voiceFoldTables.c).
 */
void voiceFold_process(TransientGenerator *tg, SnapEg *eg, int16_t *buf,
                       uint8_t size, float inGain, float outGain);

/*
 * Depth-decay step per block, indexed by the frq row value (S089 P2).
 *
 * What:       entry v = (32 / 44108.07) / T(v), with T(v) = 5 ms x 400^(v/127):
 *             5 ms at 0 up to 2 s at 127. The decay subtracts the step from
 *             snapEg.value once per block, and the depth follows value², the
 *             SnapEg shape, at the TrD/SnD time scale.
 * Why:        SnapEg's own rate (0.2 x pitch per block) lasts at most about
 *             15 ms, which is click length. A table costs no per-block
 *             transcendental (an expf per block per voice would cost about 2
 *             instructions per sample).
 * Inputs:     generated by tools/plots/gen_fold_tables.py.
 * Accessors:  voiceFold_process().
 */
extern const float voiceFold_decayStep[128];

#endif /* VOICEFOLD_H_ */
```

---

## 5. New file: `Core/DSPAudio/voiceFold.c`

**ADD (new file).** It is compiled `-Ofast` by the existing
`$(BUILD)/Core/DSPAudio/%.o` rule (Makefile line 199) once listed in
`DSP_SRCS` (§5.3).

### 5.1 File head, placement and helpers

```c
/*
 * Core/DSPAudio/voiceFold.c
 *
 *  Created on: 30.10.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 *
 * Wavefolder voice chain DSP (S089 P2). Contract in voiceFold.h.
 * Every loop runs the same instructions for every sample, whatever the
 * value (CPU mandate: constant, predictable cost). Compiled with -Ofast.
 */
#include "voiceFold.h"
#include "config.h"

/*
 * S089 P2 ITCM placement.
 *
 * What:       .itcm.s089 for every function in this file; the loops are also
 *             noinline/noclone.
 * Why:        D4 (user): the fold code runs from zero-wait ITCM like the S087
 *             sample readers. The linker script places .itcm.s089 after
 *             .itcm.s085/.itcm.s087 (S089 P2 §3), so no existing ITCM loop
 *             moves. noinline/noclone keeps one copy of each loop, so the
 *             engines' calls stay a single veneer call instead of inlined
 *             bodies inside mixer_calcNextSampleBlock() (S089 P0 F3).
 *             The section attribute is written as its own macro so that
 *             tools/dsp_test/extract.py --body can strip it for the host
 *             build (it removes __attribute__((section("...")))).
 * Accessors:  every function below.
 * Affiliates: STM32F765VIHx_FLASH.ld .itcm; Oscillator.c OSC_S087_SECTION.
 */
#define VOICE_FOLD_SECTION __attribute__((section(".itcm.s089")))
#define VOICE_FOLD_LOOP    VOICE_FOLD_SECTION __attribute__((noinline, noclone))

/*
 * Fixed-point and mapping constants (S089 P2 §3).
 *
 * VF_Q22         1.0 in the triangle fold's Q22 output (2^22).
 * VF_D_ONE       depth 1.0 in the Q23 depth word that smlawb multiplies by
 *                the int16 sample: (D x s) >> 16 = s x depth x 2^7, which is
 *                x = s/32768 x depth expressed in Q22 (2^22 / 2^15 = 2^7).
 * VF_DEPTH_Q     the Q11 scale of transGen.midiFreq (max 31.99; TrI peaks at
 *                16, which a Q12 uint16 could not hold).
 * VF_DI_MAX      cap on depth x inGain, so the Q23 word stays <= 2^30.
 * VF_SPAN        depth span above 1: depth = 1 + span x vol^2 (D6
 *                fine-control mapping). 7 gives x1-x8 (the S084 Fld range);
 *                TrI uses 15 (x1-x16).
 * VF_BIAS_MAX    largest fold bias, +-0.3 (S084 Fld symmetry range).
 * VF_ASYM_MAX    largest Asn even-harmonic accent a (y += a t^2).
 * VF_RATIO_MAX   largest Fd2 second-stage gain (1 = single fold).
 * VF_FRQ_*       inverse of the IM_SPECIAL_TRANSIENT_FREQ writer
 *                pitch = 1 + (v/33.9 - 0.75)  =>  v = (pitch - 0.25) x 33.9.
 * VF_SIN_A*      sin(pi/2 t) ~ A1 t + A3 t^3 + A5 t^5 on [-1, 1], least
 *                squares with A1 + A3 + A5 = 1 exactly, so p(+-1) = +-1 and
 *                |p| <= 1 (no overflow). Max error 1.04e-4 (S089 P2 §1 item 3;
 *                the S084 7th-order polynomial was 1.57e-4).
 */
#define VF_Q22          4194304
#define VF_D_ONE        8388608.0f
#define VF_DEPTH_Q      2048.0f
#define VF_DI_MAX       128.0f
#define VF_SPAN         7.0f
#define VF_SPAN_INT     15.0f
#define VF_BIAS_MAX     0.3f
#define VF_ASYM_MAX     0.5f
#define VF_RATIO_MAX    4.0f
#define VF_FRQ_OFFSET   0.25f
#define VF_FRQ_SCALE    33.9f
#define VF_SIN_A1       1.5703368f
#define VF_SIN_A3       (-0.6421070f)
#define VF_SIN_A5       0.0717703f

/*
 * (d x s) >> 16 + acc with s the low 16 bits (SMLAWB) (S089 P2).
 *
 * What:       the triangle fold's front end in one instruction:
 *             w = depth_Q23 x sample >> 16 + bias_Q22.
 * Why:        GCC does not reliably select SMLAWB from the C expression; the
 *             inline form makes the per-sample count deterministic. The host
 *             build (no DSP extension) uses the exact C equivalent: the 48-bit
 *             product's top 32 bits plus the accumulator.
 * Inputs:     d (|d| <= 2^30), s (int16 in an int32), acc.
 * Outputs:    the Q22 fold argument (|w| < 2^29 within VF_DI_MAX).
 * Accessors:  the triangle, sine and Fd2 loops.
 */
static inline int32_t vf_smlawb(int32_t d, int32_t s, int32_t acc)
{
#if defined(__ARM_FEATURE_DSP)
    int32_t r;
    __asm__ ("smlawb %0, %1, %2, %3" : "=r"(r) : "r"(d), "r"(s), "r"(acc));
    return r;
#else
    return (int32_t)(((int64_t)d * (int16_t)s) >> 16) + acc;
#endif
}

/*
 * Periodic triangle fold core: |sign-extend(w, 24 bits)| (S089 P2).
 *
 * What:       w is (x + b + 1) x 2^22, where x is the scaled sample and b
 *             the bias. One fold period of the triangle (4 units of x) is
 *             2^24 counts of w. Sign-extending the low 24 bits (SBFX) maps w
 *             onto [-2^23, 2^23); its absolute value a gives the fold:
 *               tri(x + b) x 2^22 = a - 2^22.
 *             This is the spike's driveShaper_triFold(v, 1) (wrap,
 *             frac - 0.5, 1 - 4|d|) rearranged; the host check matched it to
 *             4.8e-7 (S089 P2 §1 item 1).
 * Why:        three integer instructions (sbfx, eor, sub) instead of the
 *             float version's two conversions, a mask and three float ops.
 * Inputs:     w.  Outputs: a in [0, 2^23].
 * Accessors:  the triangle, sine and Fd2 loops.
 */
static inline int32_t vf_foldAbs(int32_t w)
{
    /* Shift left as unsigned (no signed-overflow UB), then arithmetic shift
     * right: GCC emits a single SBFX #0, #24. */
    const int32_t f = (int32_t)((uint32_t)w << 8) >> 8;

    return (f < 0) ? -f : f;   /* EOR/SUB with ASR #31: no branch */
}

/*
 * High word of a 32 x 32 product (SMMUL) (S089 P2).
 *
 * What:       (a x g) >> 32: scales a Q22 fold output to int16 with one
 *             instruction, g = 32767 x outputGain x 2^10.
 * Accessors:  the triangle and Fd2 loops.
 */
static inline int32_t vf_smmul(int32_t a, int32_t g)
{
    return (int32_t)(((int64_t)a * g) >> 32);
}
```

### 5.2 The four loops

```c
/*
 * Triangle fold loop: Tri, TrD, TrI (S089 P2).
 *
 * What:       per sample, with the depth word ramped from d to d + size x
 *             dStep:
 *               w = (d x s) >> 16 + b        x + bias + 1, Q22 (SMLAWB)
 *               a = |sbfx24(w)|              fold argument wrapped
 *               y = a - k1                   tri(x + b) - b, Q22
 *               out = (y x g) >> 32          int16 (SMMUL)
 *             k1 = b, which is (1 + bias) x 2^22: the "- 2^22" of the
 *             fold identity and the DC compensation "- tri(bias)" combine
 *             into one constant (tri(bias) = bias for |bias| <= 1), so the
 *             output is exactly 0 for a silent input (D3).
 * Why:        the cheapest fold (≈11 per sample). The depth ramp is a
 *             per-sample integer add, so vol edits, Morph, LFO, velocity and
 *             the TrD envelope never step.
 * Inputs:     buf/size; d, dStep (Q23 depth word and per-sample step); b
 *             (Q22 bias + 1); k1 (= b); g (output scale).
 * Outputs:    buf in place. |y| <= (1 + |bias|) x 2^22 and g includes
 *             1/(1 + |bias|), so the int16 result cannot overflow.
 * Accessors:  voiceFold_process().
 */
static VOICE_FOLD_LOOP void voiceFold_triLoop(int16_t *buf, uint8_t size,
        int32_t d, const int32_t dStep, const int32_t b, const int32_t k1,
        const int32_t g)
{
    uint8_t i;

    for (i = 0u; i < size; i++) {
        const int32_t w = vf_smlawb(d, buf[i], b);   /* scaled input + bias */
        const int32_t y = vf_foldAbs(w) - k1;        /* folded, DC removed */

        d += dStep;                                  /* depth ramp */
        buf[i] = (int16_t)vf_smmul(y, g);            /* to int16 */
    }
}

/*
 * Sine-fold loop: Sin, SnD, Asn (S089 P2).
 *
 * What:       the triangle fold as above, then a smooth sine shape and the
 *             Asn accent, in float:
 *               t' = a - 2^22                tri(x + b) in Q22 (as float)
 *               t2 = t' x t'
 *               P  = k1 + t2 (k3 + k5 t2)    the 5th-order polynomial
 *               y  = t' P + (kA t2 - kC)     Sin: kA = 0, kC = DC term
 *                                            Asn: kA = accent, kC = 0
 *               out = (int16) y
 *             The Q22 scale (2^-22 per power of t) and the output scale S
 *             are folded into k1 = A1 S 2^-22, k3 = A3 S 2^-66,
 *             k5 = A5 S 2^-110 and kA = a S 2^-44, so the loop has no
 *             conversion multiply. The smallest constant (k5 ≈ 1.8e-30) stays
 *             a normal float, so -Ofast flush-to-zero does not apply.
 * Why:        the sine fold rounds every corner of the triangle (fewer high
 *             partials, less aliasing). Asn's even-order term reuses t2 and
 *             replaces the spike's pre-fold cubic, which cost ≈3 more (S089
 *             P2 §1 item 2).
 * Inputs:     buf/size; d, dStep, b (as the triangle loop); k1, k3, k5, kA,
 *             kC (per-block constants from voiceFold_process()).
 * Outputs:    buf in place; |y| <= 32767 x outGain (S includes the
 *             1/(1 + |kC/S| + a) normalisation).
 * Accessors:  voiceFold_process().
 */
static VOICE_FOLD_LOOP void voiceFold_sinLoop(int16_t *buf, uint8_t size,
        int32_t d, const int32_t dStep, const int32_t b, const float k1,
        const float k3, const float k5, const float kA, const float kC)
{
    uint8_t i;

    for (i = 0u; i < size; i++) {
        const int32_t w = vf_smlawb(d, buf[i], b);
        const float t = (float)(vf_foldAbs(w) - VF_Q22);  /* tri, Q22 units */
        const float t2 = t * t;
        const float p = k1 + t2 * (k3 + k5 * t2);         /* odd polynomial */

        d += dStep;
        buf[i] = (int16_t)(int32_t)(t * p + (kA * t2 - kC));
    }
}

/*
 * Chebyshev loop: Chb (S089 P2).
 *
 * What:       no periodic fold; the third Chebyshev polynomial with a blend:
 *               x  = s x dk                  0 < |x| <= 1 (dk ramps)
 *               y  = x (c1 - c3 x^2)         = S' [(1 - m) x + m T3(x)]
 *             with T3(x) = 3x - 4x^3, m the frq blend, c1 = (1 + 2m) S' and
 *             c3 = 4m S'. S' = 32767 x outGain / ((1 + 2m) xpk), where xpk is
 *             the block's peak |x|, so the small-signal gain is 1 for every m
 *             and |y| <= 32767 x outGain.
 * Why:        the most tonal option: a sine at full drive becomes exactly its
 *             third harmonic. dk is capped so |x| <= 1 (T3 grows without
 *             bound beyond it), so no clamp instruction is needed.
 * Inputs:     buf/size; dk, dkStep (float per-sample input scale); c1, c3.
 * Outputs:    buf in place.
 * Accessors:  voiceFold_process().
 */
static VOICE_FOLD_LOOP void voiceFold_chbLoop(int16_t *buf, uint8_t size,
        float dk, const float dkStep, const float c1, const float c3)
{
    uint8_t i;

    for (i = 0u; i < size; i++) {
        const float x = (float)buf[i] * dk;          /* drive into T3 */

        dk += dkStep;
        buf[i] = (int16_t)(int32_t)(x * (c1 - c3 * (x * x)));
    }
}

/*
 * Double fold loop: Fd2 (S089 P2).
 *
 * What:       the triangle fold twice:
 *               y1 = |sbfx24(w)| - 2^22               first fold, Q22 in +-2^22
 *               w2 = (y1 x r6) >> 6 + 2^22            stage-2 gain r (Q6)
 *               y2 = |sbfx24(w2)| - 2^22              second fold
 *               out = (y2 x g) >> 32
 *             At r = 1 the second fold is the identity (|y1| <= 1 lies inside
 *             one fold segment), so frq 0 is the plain triangle; r = 4 folds
 *             each first-stage segment four more times.
 * Why:        a very dense spectrum from integer instructions only (≈17).
 *             y1 x r6 <= 2^22 x 256 = 2^30, so the product cannot overflow.
 * Inputs:     buf/size; d, dStep (Q23 depth word); r6 (Q6 ratio, 64..256); g.
 * Outputs:    buf in place; |y2| <= 2^22, so the result fits int16 with
 *             g = 32767 x outGain x 2^10.
 * Accessors:  voiceFold_process().
 */
static VOICE_FOLD_LOOP void voiceFold_fd2Loop(int16_t *buf, uint8_t size,
        int32_t d, const int32_t dStep, const int32_t r6, const int32_t g)
{
    uint8_t i;

    for (i = 0u; i < size; i++) {
        const int32_t y1 = vf_foldAbs(vf_smlawb(d, buf[i], VF_Q22)) - VF_Q22;
        const int32_t w2 = ((y1 * r6) >> 6) + VF_Q22;   /* stage-2 gain */

        d += dStep;
        buf[i] = (int16_t)vf_smmul(vf_foldAbs(w2) - VF_Q22, g);
    }
}
```

### 5.3 The entry point

```c
/*
 * One WFd block (S089 P2). Contract in voiceFold.h.
 *
 * What:       1. resolves the fold type (clamped, never rewritten);
 *             2. recovers the frq row value v (0..127) and p = v/127;
 *             3. maps vol to the peak depth, 1 + span x vol² (D6);
 *             4. for TrD/SnD, steps snapEg.value down by the table entry for
 *                v and scales the depth above 1 by value² (SnapEg's shape);
 *             5. takes the previous block's depth from transGen.midiFreq
 *                (Q11; 0 = none yet, so the first block does not ramp from 0)
 *                and stores this block's end depth there;
 *             6. builds the type's per-block constants and calls its loop.
 * Why:        all control-rate work happens once per block here, so the loops
 *             only carry the per-sample instructions.
 * Inputs/Outputs: see voiceFold.h.
 * Accessors:  the four engine sync renders under VOICE_CHAIN_WAVEFOLDER.
 * Affiliates: IM_SPECIAL_TRANSIENT_FREQ (the pitch mapping it inverts);
 *             SnapEg_trigger() (sets value = 1 at every hit);
 *             IM_SPECIAL_VOICE_CHAIN (clears midiFreq on a chain change).
 */
VOICE_FOLD_LOOP void voiceFold_process(TransientGenerator *tg, SnapEg *eg,
        int16_t *buf, uint8_t size, float inGain, float outGain)
{
    /* 1. Type: a stored byte past the list plays the last type. */
    const uint8_t type = (tg->waveform < VOICE_FOLD_COUNT)
        ? tg->waveform : (uint8_t)(VOICE_FOLD_COUNT - 1u);
    /* 2. frq: invert the transient pitch writer. An LFO may give fractions,
     *    so v is continuous; the decay table uses its rounded index. */
    float v = (tg->pitch - VF_FRQ_OFFSET) * VF_FRQ_SCALE;
    float vol = tg->volume;
    float depth1;
    float depth0;
    float ig = inGain;
    int32_t d0;
    int32_t d1;
    int32_t dStep;

    if (v < 0.0f) v = 0.0f;
    else if (v > 127.0f) v = 127.0f;
    if (vol < 0.0f) vol = 0.0f;
    else if (vol > 1.0f) vol = 1.0f;
    if (ig < 1.0f) ig = 1.0f;
    else if (ig > VOICE_FOLD_IN_GAIN_MAX) ig = VOICE_FOLD_IN_GAIN_MAX;

    /* 3. Fine-control depth (D6): squared, so the first half of vol stays
     *    within x1-x2.75 (x1-x4.75 for TrI). */
    depth1 = 1.0f + ((type == VOICE_FOLD_TRI_INT) ? VF_SPAN_INT : VF_SPAN) *
             vol * vol;

    /* 4. Decay types: value falls linearly per block (table, 5 ms .. 2 s)
     *    and the depth above 1 follows value², ending at a clean fold
     *    (Tri) or gentle saturation (Sin). SnapEg_trigger() restarts it. */
    if (type == VOICE_FOLD_TRD || type == VOICE_FOLD_SND) {
        float e = eg->value - voiceFold_decayStep[(uint8_t)(v + 0.5f)];

        if (e < 0.0f) e = 0.0f;
        eg->value = e;
        depth1 = 1.0f + (depth1 - 1.0f) * e * e;
    }

    /* 5. Ramp start: last block's end depth; 0 = none (slot reset/chain
     *    change), which starts flat at this block's depth. */
    depth0 = tg->midiFreq ? (float)tg->midiFreq * (1.0f / VF_DEPTH_Q) : depth1;
    tg->midiFreq = (uint16_t)(depth1 * VF_DEPTH_Q + 0.5f);

    /* Integer depth words, with depth x inGain capped (VF_DI_MAX) so
     * |d| <= 2^30 and the smlawb product stays in range. */
    {
        float a0 = depth0 * ig;
        float a1 = depth1 * ig;

        if (a0 > VF_DI_MAX) a0 = VF_DI_MAX;
        if (a1 > VF_DI_MAX) a1 = VF_DI_MAX;
        d0 = (int32_t)(a0 * VF_D_ONE);
        d1 = (int32_t)(a1 * VF_D_ONE);
        dStep = (d1 - d0) / (int32_t)size;   /* size is 32: an ASR */
    }

    /* 6. Per-type constants and loop. */
    switch (type) {
    case VOICE_FOLD_TRI:
    case VOICE_FOLD_TRD:
    case VOICE_FOLD_TRI_INT: {
        /* Bias: frq 64 is exactly symmetric; 0 / 127 are -0.3 / +0.3.
         * TrD uses frq for decay, so it has no bias. */
        float bias = (type == VOICE_FOLD_TRD) ? 0.0f
            : VF_BIAS_MAX * (v - 64.0f) * (1.0f / 63.0f);
        float ab;
        int32_t b;

        if (bias > VF_BIAS_MAX) bias = VF_BIAS_MAX;   /* v = 0 -> -0.305 */
        if (bias < -VF_BIAS_MAX) bias = -VF_BIAS_MAX;
        ab = (bias < 0.0f) ? -bias : bias;
        b = (int32_t)((1.0f + bias) * (float)VF_Q22);
        /* g = 32767 x outGain / (1 + |bias|) x 2^10 (see vf_smmul()). */
        voiceFold_triLoop(buf, size, d0, dStep, b, b,
            (int32_t)(32767.0f * 1024.0f * outGain / (1.0f + ab)));
        break; }

    case VOICE_FOLD_SIN:
    case VOICE_FOLD_SND:
    case VOICE_FOLD_ASN: {
        /* Sin: bias as Tri. SnD: none. Asn: frq is the accent a, no bias. */
        float bias = (type == VOICE_FOLD_SIN)
            ? VF_BIAS_MAX * (v - 64.0f) * (1.0f / 63.0f) : 0.0f;
        const float acc = (type == VOICE_FOLD_ASN)
            ? VF_ASYM_MAX * v * (1.0f / 127.0f) : 0.0f;
        float c;
        float ac;
        float s;

        if (bias > VF_BIAS_MAX) bias = VF_BIAS_MAX;
        if (bias < -VF_BIAS_MAX) bias = -VF_BIAS_MAX;
        /* DC term: the sine fold of the bias alone (tri(b) = b, |b| <= 1). */
        c = bias * (VF_SIN_A1 + bias * bias *
                    (VF_SIN_A3 + VF_SIN_A5 * bias * bias));
        ac = (c < 0.0f) ? -c : c;
        /* Output scale: the largest |y| is 1 + |c| (Sin) or 1 + a (Asn). */
        s = 32767.0f * outGain / (1.0f + ac + acc);
        voiceFold_sinLoop(buf, size, d0, dStep,
            (int32_t)((1.0f + bias) * (float)VF_Q22),
            VF_SIN_A1 * s * 2.38418579e-7f,     /* x 2^-22  */
            VF_SIN_A3 * s * 1.35525272e-20f,    /* x 2^-66  */
            VF_SIN_A5 * s * 7.70371978e-34f,    /* x 2^-110 */
            acc * s * 5.68434189e-14f,          /* x 2^-44  */
            c * s);
        break; }

    case VOICE_FOLD_CHB: {
        /* x = s x depth x inGain / 8 / 32768: vol 0 drives the curve at 1/8
         * (nearly linear), vol 127 at full scale (a pure third harmonic for
         * a sine). Capped at |x| <= 1. */
        const float m = v * (1.0f / 127.0f);
        float k0 = depth0 * ig * 0.125f;
        float k1 = depth1 * ig * 0.125f;
        float xpk;
        float sc;

        if (k0 > 1.0f) k0 = 1.0f;
        if (k1 > 1.0f) k1 = 1.0f;
        /* Normalise by the block's larger peak, so a falling ramp cannot
         * exceed int16 within the block. */
        xpk = (k0 > k1) ? k0 : k1;
        sc = 32767.0f * outGain / ((1.0f + 2.0f * m) * xpk);
        voiceFold_chbLoop(buf, size, k0 * (1.0f / 32768.0f),
            (k1 - k0) * (1.0f / 32768.0f) / (float)size,
            (1.0f + 2.0f * m) * sc, 4.0f * m * sc);
        break; }

    case VOICE_FOLD_FD2:
    default:
        /* Stage-2 ratio 1..4 in Q6 (64..256). */
        voiceFold_fd2Loop(buf, size, d0, dStep,
            (int32_t)((1.0f + (VF_RATIO_MAX - 1.0f) * v * (1.0f / 127.0f)) *
                      64.0f + 0.5f),
            (int32_t)(32767.0f * 1024.0f * outGain));
        break;
    }
}
```

### 5.4 New file: `tools/plots/gen_fold_tables.py`

**ADD (new file).** This follows the `tools/plots/gen_stock_tables.py`
pattern: it writes the committed table, and `--check` verifies it.

```python
#!/usr/bin/env python3
"""Generate the WFd depth-decay table (S089 P2).

What:       writes Core/DSPAudio/voiceFoldTables.c with
            const float voiceFold_decayStep[128], entry v =
            (OUTPUT_DMA_SIZE / REAL_FS) / T(v), T(v) = 0.005 s x 400^(v/127)
            (5 ms at frq 0 to 2 s at frq 127, exponential, so equal frq steps
            are equal time ratios).
Why:        voiceFold_process() subtracts the entry from snapEg.value once per
            block for the TrD/SnD types; a table avoids an expf per block per
            voice (S089_P2_IMPLEMENTATION.md §4).
Checks:     C1 entry 0 = 0.1450982 (6.9 blocks) and entry 127 = 0.0003627
            (2,757 blocks) to 1e-6; C2 strictly decreasing; C3 --check:
            regenerate in memory and compare with the committed file byte for
            byte (exit 1 on any failure).
Inputs:     the constants below (they must match config.h OUTPUT_DMA_SIZE and
            REAL_FS).
Outputs:    Core/DSPAudio/voiceFoldTables.c, or a pass/fail line with --check.
Accessors:  tools/dsp_test/Makefile fold_tables target.
Affiliates: voiceFold.h voiceFold_decayStep; voiceFold.c.
"""
import sys

OUTPUT_DMA_SIZE = 32
REAL_FS = 44108.07291666667
T_MIN, T_RATIO = 0.005, 400.0
PATH = "Core/DSPAudio/voiceFoldTables.c"


def table():
    blk = OUTPUT_DMA_SIZE / REAL_FS
    return [blk / (T_MIN * T_RATIO ** (v / 127.0)) for v in range(128)]


def render(vals):
    lines = ["/* Generated by tools/plots/gen_fold_tables.py (S089 P2). Do not edit. */",
             '#include "voiceFold.h"',
             "",
             "/* Per-block depth-decay step, v = frq 0..127: 5 ms .. 2 s (voiceFold.h). */",
             "const float voiceFold_decayStep[128] = {"]
    for i in range(0, 128, 4):
        lines.append("    " + ", ".join("%.9ef" % x for x in vals[i:i + 4]) +
                     ",  /* %3d..%3d */" % (i, i + 3))
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    vals = table()
    ok = abs(vals[0] - 0.1450982) < 1e-6 and abs(vals[127] - 0.0003627) < 1e-6
    ok = ok and all(a > b for a, b in zip(vals, vals[1:]))
    text = render(vals)
    if "--check" in sys.argv:
        ok = ok and open(PATH).read() == text
        print("fold tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("fold tables: C1/C2 failed")
        return 1
    open(PATH, "w").write(text)
    print("wrote", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

**ADD (generated):** `Core/DSPAudio/voiceFoldTables.c`. Run
`python3 tools/plots/gen_fold_tables.py` from the repository root. Its
header comment is in the generator's output.

### 5.5 Makefile: build the new sources

**File:** `Makefile`, **lines 170–171 (the end of `DSP_SRCS`). MODIFY.**

```diff
   Core/DSPAudio/transientTables.c \
+  Core/DSPAudio/voiceFold.c \
+  Core/DSPAudio/voiceFoldTables.c \
   Core/DSPAudio/wavetable.c \
   Core/DSPAudio/wavetableStock.c
```

Add this comment above `DSP_SRCS` (line 145):

```make
# S089 P2: voiceFold.c (WFd chain DSP, .itcm.s089) and its generated table
# use the Core/DSPAudio -Ofast rule below like every other voice DSP file.
```

---

## 6. Engine structs: `chainType`

In each header, **ADD directly after the `ampRampArm` member.** That puts it
in existing padding for Drum, Snare and Cymbal; HiHat grows 4 B (§1 item 8).

| File | Line (after) |
|---|---|
| `Core/DSP/Instruments/Drum/DrumVoice.h` | 245 (`uint8_t ampRampArm;`) |
| `Core/DSP/Instruments/Snare/Snare.h` | 131 |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.h` | 120 |
| `Core/DSP/Instruments/HiHat/HiHat.h` | 129 |

Text, identical in all four, with the engine name substituted:

```c
	/*
	 * Voice chain selector (S089 P2).
	 *
	 * What:       VOICE_CHAIN_DEFAULT (0, T+D: transient + filter drive +
	 *             distortion, S089 P0 behaviour) or VOICE_CHAIN_WAVEFOLDER
	 *             (1, WFd: a wavefolder in place of the transient). Written by
	 *             IM_SPECIAL_VOICE_CHAIN through the voice_chain row, which
	 *             clamps to VOICE_CHAIN_COUNT - 1.
	 * Why:        the sync render, the trigger and the async calc branch on it
	 *             once per block/trigger; under WFd the transient_* rows and
	 *             transGen/snapEg state belong to the fold (voiceFold.h). A
	 *             selector: never morphed, no LFO/velocity target, one
	 *             step-automation value per step.
	 * Inputs:     instrumentManager_writeSpecialRuntime() via the descriptor.
	 * Outputs:    read by <Engine>_triggerVoice(), <Engine>_calcAsyncVoice(),
	 *             <Engine>_calcSyncBlockVoice(); instrumentManager_slotVoiceChain().
	 * Layout:     after ampRampArm, in existing alignment padding (Drum 571,
	 *             Snare 419, Cymbal 471); HiHat grows 516 -> 520 B, still
	 *             under the 2 x sizeof(DrumVoice) slot reserve, so
	 *             runtime_slots is unchanged (7,584 B).
	 * Affiliates: VOICE_CHAIN_* (voiceFold.h); the voice_chain rows;
	 *             InstrumentManager.c slot-reserve _Static_asserts.
	 */
	uint8_t chainType;
```

---

## 7. Engine init

**ADD after the `runMode = OSC_RUN_RST;` line in each `*_initVoice()`.**

| File | Line (after) |
|---|---|
| `Core/DSP/Instruments/Drum/DrumVoice.c` | 174 |
| `Core/DSP/Instruments/Snare/Snare.c` | 116 |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.c` | 133 |
| `Core/DSP/Instruments/HiHat/HiHat.c` | 145 |

```c
	/* S089 P2: default chain (T+D). The slot reset memset already cleared it;
	 * the explicit store documents the default like runMode above. Under WFd
	 * transGen.midiFreq (the fold's last depth) relies on the same memset
	 * being 0 = "no previous depth" (voiceFold_process()). */
	voice->chainType = VOICE_CHAIN_DEFAULT;
```

**ADD the include.** In each engine `.c`, after its `#include "voicePostChain.h"`
(Drum line 48, Snare 43, Cymbal 42, HiHat 55):

```c
#include "voiceFold.h"   /* S089 P2: VOICE_CHAIN_*, voiceFold_process() */
```

---

## 8. Engines: guards and the chain dispatch

### 8.1 Drum — `Core/DSP/Instruments/Drum/DrumVoice.c`

**Line 310. MODIFY** (inside `#ifdef USE_AMP_FILTER`, which is undefined in
this build; guarded so an amp-filter build behaves the same):

```diff
-		 (voice->transGen.waveform == 1)) ? 1u : 0u;
+		 (voice->chainType == VOICE_CHAIN_DEFAULT &&
+		  voice->transGen.waveform == 1)) ? 1u : 0u;
```

**Line 317. MODIFY:**

```diff
-		if(voice->transGen.waveform==1) //offset mode
+		/*
+		 * Ofs transient (offset mode) belongs to T+D only (S089 P2).
+		 * What: under WFd transGen.waveform is a fold type, and value 1 is TrD,
+		 *       not "offset"; the start offset must not follow transient_vol
+		 *       (which is fold depth there).
+		 * Why:  plan §1: the shared stored byte means different things per
+		 *       chain. Affiliates: VOICE_CHAIN_*; voiceFold.h.
+		 */
+		if(voice->chainType == VOICE_CHAIN_DEFAULT &&
+		   voice->transGen.waveform==1) //offset mode
```

**Line 462. MODIFY** (`Drum_calcVoiceAsync()`):

```diff
-	//calc snap EG if transient sample 0 is activated
-	if(voice->transGen.waveform == 0)
+	/*
+	 * Snp transient (snap pitch envelope) belongs to T+D only (S089 P2).
+	 * What: under WFd value 0 is the Tri fold, and snapEg.value is the TrD/SnD
+	 *       depth envelope stepped by voiceFold_process(); SnapEg_calc() must
+	 *       not also decay it or bend the pitch. Drum reassigns pitchMod every
+	 *       block above, so skipping the += leaves no stale bend.
+	 * Affiliates: voiceFold_process(); SnapEg_trigger() in Drum_triggerVoice().
+	 */
+	if(voice->chainType == VOICE_CHAIN_DEFAULT && voice->transGen.waveform == 0)
```

**Lines 813–817. MODIFY** (`Drum_calcVoiceSyncBlock()`):

```diff
-	//calc transient sample
-	transient_calcBlock(&voice->transGen,modBuf,size);
-
-	//Mix with transient buffer
-	bufferTool_addBuffersSaturating(buf,modBuf,size);
+	/*
+	 * Stage ahead of the filter, by voice chain (S089 P2).
+	 *
+	 * What:       T+D: the transient click into modBuf and its saturating
+	 *             mix-in, exactly as before (S0). WFd: the oscillator section
+	 *             (including the itp blend above) is folded in place; no
+	 *             transient, no mix-in. The filter drive (sft), the filter and
+	 *             the post-chain below are unchanged in both chains.
+	 * Why:        the WFd chain spends the click's budget (22 + 2 = 24 per
+	 *             sample, P1 §1.1) on the fold. modBuf is not read after this
+	 *             point, so leaving it unwritten under WFd is safe.
+	 * Inputs:     voice->chainType; buf (oscillator section); transGen,
+	 *             snapEg (fold controls/state under WFd).
+	 * Outputs:    buf (pre-filter block).
+	 * Accessors:  this render.
+	 * Affiliates: voiceFold_process() (gains 1/1: the Drum oscillator is
+	 *             rendered at full scale); transient_calcBlock().
+	 */
+	if (voice->chainType == VOICE_CHAIN_WAVEFOLDER) {
+		voiceFold_process(&voice->transGen, &voice->snapEg, buf, size,
+		                  1.0f, 1.0f);
+	} else {
+		//calc transient sample
+		transient_calcBlock(&voice->transGen,modBuf,size);
+
+		//Mix with transient buffer
+		bufferTool_addBuffersSaturating(buf,modBuf,size);
+	}
```

**Lines 614–615. MODIFY** (comment accuracy):

```diff
- *             modBuf is still fully rewritten by transient_calcBlock()
- *             below, so its contents in snc do not matter.
+ *             modBuf is still fully rewritten by transient_calcBlock()
+ *             below under T+D, and is not read after the oscillator section
+ *             under WFd (S089 P2), so its contents in snc do not matter.
```

### 8.2 Snare — `Core/DSP/Instruments/Snare/Snare.c`

**Line 213. MODIFY** (`Snare_triggerVoice()`, offset mode): the same guard
and comment as Drum line 317.

```diff
-	if(voice->transGen.waveform==1) //offset mode
+	/* Ofs belongs to T+D only (S089 P2; see DrumVoice.c offset guard). */
+	if(voice->chainType == VOICE_CHAIN_DEFAULT &&
+	   voice->transGen.waveform==1) //offset mode
```

**Line 309. MODIFY** (`Snare_calcAsyncVoice()`, snap; Snare reassigns
`pitchMod` each block, as Drum does):

```diff
-	if(voice->transGen.waveform == 0)
+	/* Snp belongs to T+D only; under WFd snapEg is the fold's decay (S089 P2). */
+	if(voice->chainType == VOICE_CHAIN_DEFAULT && voice->transGen.waveform == 0)
```

**Lines 346–348. MODIFY** (the transient on the filtered noise):

```diff
-	//calc transient sample
-	transient_calcBlock(&voice->transGen,transBuf,size);
-	bufferTool_addBuffersSaturating(buf,transBuf,size);
+	/*
+	 * Transient (T+D only) (S089 P2).
+	 * What: T+D adds the click to the filtered noise as before (S0). WFd has
+	 *       no transient: its fold runs on the tone oscillator below (D1).
+	 * Why:  Snare's only pre-filter signal is noise; folding it does little.
+	 */
+	if (voice->chainType != VOICE_CHAIN_WAVEFOLDER) {
+		//calc transient sample
+		transient_calcBlock(&voice->transGen,transBuf,size);
+		bufferTool_addBuffersSaturating(buf,transBuf,size);
+	}
```

**After line 392** (after the `itp` `oscSection_renderOldCarrier(..., transBuf,
size);`, before the post-chain comment at 394). **ADD:**

```c
	/*
	 * WFd: fold the tone oscillator (S089 P2, D1).
	 *
	 * What:       folds transBuf (the tone oscillator, already blended by
	 *             itp) in place before the post-chain mixes it with the noise.
	 * Why:        the tone oscillator is Snare's tonal, unfiltered body. It is
	 *             rendered at gain (1 - mix), which would make the fold depth
	 *             follow the noise mix; inGain = 1/(1 - mix) restores full
	 *             scale into the fold (capped at VOICE_FOLD_IN_GAIN_MAX = 16,
	 *             i.e. mix > 0.9375, where the tone is near silence) and
	 *             outGain = (1 - mix) restores the mix level after it. Both
	 *             are per-block constants inside voiceFold_process(), so they
	 *             cost nothing per sample.
	 * Inputs:     transBuf, voice->mix (0..1), transGen/snapEg (fold state).
	 * Outputs:    transBuf folded; read by voicePost_mixAddGainDist() below.
	 * Affiliates: voiceFold_process(); the osc render above (1 - mix gain).
	 */
	if (voice->chainType == VOICE_CHAIN_WAVEFOLDER) {
		const float tone = 1.0f - voice->mix;

		voiceFold_process(&voice->transGen, &voice->snapEg, transBuf, size,
		                  (tone > (1.0f / VOICE_FOLD_IN_GAIN_MAX))
		                      ? (1.0f / tone) : VOICE_FOLD_IN_GAIN_MAX,
		                  tone);
	}
```

### 8.3 Cymbal — `Core/DSP/Instruments/Cymbal/CymbalVoice.c`

**Line 233. MODIFY** (offset mode): the same guard as Drum line 317.

**Line 336. MODIFY** (`Cymbal_calcAsyncVoice()`, snap):

```diff
-	//calc snap EG if transient sample 0 is activated
-	if(voice->transGen.waveform == 0)
-	{
-		const float snapVal = SnapEg_calc(&voice->snapEg,voice->transGen.pitch);
-		voice->osc.pitchMod = 1 + snapVal*voice->transGen.volume;
-	}
+	/*
+	 * Snap pitch envelope (T+D Snp only) (S089 P2).
+	 *
+	 * What:       T+D: unchanged (S0). WFd: no snap; pitchMod is held at
+	 *             1.0 and snapEg.value is left to voiceFold_process() (the
+	 *             TrD/SnD decay).
+	 * Why:        unlike Drum/Snare, Cymbal *assigns* pitchMod only here and
+	 *             nothing else resets it after init. Without the else, a chain
+	 *             change during a snap would leave the carrier detuned
+	 *             indefinitely (S089 P2 §1 item 6). The T+D Snp-to-click
+	 *             variant of that latent issue is recorded, not changed (§16).
+	 * Affiliates: Cymbal_initVoice() (pitchMod = 1); voiceFold_process().
+	 */
+	if (voice->chainType == VOICE_CHAIN_WAVEFOLDER)
+		voice->osc.pitchMod = 1.0f;
+	else if(voice->transGen.waveform == 0)
+	{
+		const float snapVal = SnapEg_calc(&voice->snapEg,voice->transGen.pitch);
+		voice->osc.pitchMod = 1 + snapVal*voice->transGen.volume;
+	}
```

**After line 423** (`oscSection_renderOldCarrier(...)`), before
`SVF_calcBlockZDF` at 424. **ADD:**

```c
	/*
	 * WFd: fold the FM carrier ahead of the filter (S089 P2).
	 * What: the carrier block (with the itp blend) is folded in place; the
	 *       filter drive (sft), the filter and the distortion run unchanged.
	 * Why:  plan §1: the fold takes the transient's place and budget
	 *       (22 + 4 = 26 per sample with the no-add post loop below).
	 * Affiliates: voiceFold_process() (gains 1/1: the carrier renders at
	 *       gain 1.0).
	 */
	if (voice->chainType == VOICE_CHAIN_WAVEFOLDER)
		voiceFold_process(&voice->transGen, &voice->snapEg, buf, size,
		                  1.0f, 1.0f);
```

**Lines 426–427. MODIFY:**

```diff
-	//calc transient sample
-	transient_calcBlock(&voice->transGen,mod,size);
+	/* Transient (T+D only, S089 P2). Under WFd mod still holds the FM
+	 * modulator signal, so the post loop below must not add it. */
+	if (voice->chainType != VOICE_CHAIN_WAVEFOLDER)
+		transient_calcBlock(&voice->transGen,mod,size);
```

**Lines 451–452. MODIFY** (the post-chain call):

```diff
-		voicePost_addGainDist(buf, mod, ampGain, lastAmpGain,
-		                      &voice->distortion, size);
+		/*
+		 * Post-chain by voice chain (S089 P2).
+		 * T+D: transient add + amp ramp + distortion, unchanged (S0).
+		 * WFd: the no-add twin. mod is the FM modulator here, not a
+		 *      transient, and must not reach the output (plan §1 hazard).
+		 * Affiliates: voicePost_gainDist() (voicePostChain.h).
+		 */
+		if (voice->chainType == VOICE_CHAIN_WAVEFOLDER)
+			voicePost_gainDist(buf, ampGain, lastAmpGain,
+			                   &voice->distortion, size);
+		else
+			voicePost_addGainDist(buf, mod, ampGain, lastAmpGain,
+			                      &voice->distortion, size);
```

### 8.4 HiHat — `Core/DSP/Instruments/HiHat/HiHat.c`

The same four changes as Cymbal, at:
- line 243: offset-mode guard;
- line 355: snap, with the WFd `pitchMod = 1.0f` else;
- after line 441: fold before `SVF_calcBlockZDF` (line 443);
- lines 445–446: transient (T+D only); comment "`mod1` holds the modulator
  sum";
- lines 470–471: post-chain (`voicePost_gainDist` under WFd).

The text is identical apart from `mod` → `mod1` and the function names in the
`Affiliates` lines (`HiHat_initVoice()`).

---

## 9. `Core/DSPAudio/voicePostChain.h`: the no-add post loop

**ADD after line 118** (the end of `voicePost_addGainDist()`), before
`#endif` at 120:

```c
/*
 * Cymbal/HiHat amp gain ramp -> distortion, no add (S089 P2).
 *
 * What:       voicePost_addGainDist() without the saturating add: the same
 *             per-sample amp ramp (bufferTool_interpolatedGain()) and the
 *             same float-through distortion, sample for sample.
 * Why:        under the WFd chain Cymbal and HiHat render no transient, and
 *             their transient buffer (mod / mod1) still holds the FM
 *             modulator signal, so the add must not run. Removing it also
 *             frees the add's ≈4 per sample, which is part of the fold's
 *             budget (26 for Cymbal/HiHat, S089 P2 plan §2). The ramp rule
 *             is unchanged: equal endpoints unless the trigger armed it.
 * Inputs:     filtered buf, current/previous amp gain, distortion state, block
 *             size.
 * Outputs:    buf overwritten in place.
 * Accessors:  Cymbal_calcSyncBlockVoice(), HiHat_calcSyncBlockVoice() (WFd).
 * Affiliates: voicePost_addGainDist() (its T+D twin);
 *             bufferTool_interpolatedGain(); distortion_curveSampleF().
 */
static inline void voicePost_gainDist(int16_t *buf,
		const float ampGain, const float lastAmpGain,
		const Distortion *dist, const uint8_t size)
{
	uint8_t j;
	/* size is OUTPUT_DMA_SIZE in the mixer; see voicePost_drum(). */
	const float inv_size = (size > 1u) ? 1.f/(size-1.f) : 0.f;
	for(j=0;j<size;j++)
	{
		const float ag = bufferTool_interpolatedGain(j, inv_size,
		                                              ampGain, lastAmpGain);
		buf[j] = distortion_curveSampleF(dist, (float)buf[j] * ag);
	}
}
```

---

## 10. Descriptor files: the `voice_chain` row

The same four edits in each engine. Line numbers per engine:

| Edit | Drum | Snare | Cymbal | HiHat |
|---|---|---|---|---|
| `.h` `*_PARAM_DESCRIPTOR_COUNT` | 14 (42→43) | 14 (41→42) | 14 (42→43) | 14 (42→43) |
| enum: add after `*_PARAM_OSC1_RUN,` | 217 | 211 | 210 | 213 |
| row: add after the `osc1_run` row | 353 | 333 | 332 | 336 |
| transient page: cell 3 | 377 | 355 | 353 | 359 |

**10.1 `*Parameters.h` line 14. MODIFY** (Drum shown):

```diff
-#define DRUM_PARAM_DESCRIPTOR_COUNT 42u /* S088: + osc1_run. */
+#define DRUM_PARAM_DESCRIPTOR_COUNT 43u /* S088: + osc1_run. S089 P2: + voice_chain. */
```

**10.2 Enum. ADD after `*_PARAM_OSC1_RUN,`:**

```c
    DRUM_PARAM_VOICE_CHAIN, /* S089 P2: appended; preserve all prior IDs. */
```

**10.3 Row. ADD after the `osc1_run` row** (it must be one physical line for
`check_special_tags.py`, S088 deviation 4):

```c
    /*
     * voice_chain: chn, the voice chain selector (S089 P2).
     *
     * What:       0 T+D (transient + filter drive + distortion, the S089 P0
     *             chain), 1 WFd (wavefolder in place of the transient),
     *             stored in the engine chainType field through
     *             IM_SPECIAL_VOICE_CHAIN (clamped to VOICE_CHAIN_COUNT - 1).
     *             Shown on the transient page as the fourth cell
     *             (wav vol frq chn); under WFd those three cells drive the fold
     *             (voiceFold.h) and the single-parameter view relabels them.
     * Why:        S089 P1/P2: one selector, one chain, one budget. A selector:
     *             never morphed, Normal image only, applied before the Morph
     *             image by preset_applyKitVoiceSelectors(), step-automatable
     *             one value per step, not an LFO or velocity target. Appended
     *             last, so every existing descriptor index, automation ID,
     *             token, NRPN ID and Learn row is unchanged; a file without the
     *             key loads 0 (T+D, today's sound).
     * Inputs:     menu edit, Kit/Scene/Instrument apply, step automation, MIDI.
     * Outputs:    chainType (the engine struct field).
     * Accessors:  instrumentManager_writeSpecialRuntime().
     * Affiliates: VOICE_CHAIN_* (voiceFold.h); voiceChainNames (MenuText.h);
     *             ONOFF_TAG_VOICE_CHAIN; menu_onoffTagNames() in menu.c.
     */
    ROW_SELECT_ONOFF_SPECIAL("voice_chain", "Voice", "Chain", "chn", ONOFF_TAG_VOICE_CHAIN, chainType, TYPE_UINT8, IM_SPECIAL_VOICE_CHAIN),
```

**10.4 Transient page. MODIFY cell 3** (index 3, today `INSTRUMENT_MENU_EMPTY`):

```diff
-    {{ DRUM_PARAM_TRANSIENT_WAVE, DRUM_PARAM_TRANSIENT_VOL, DRUM_PARAM_TRANSIENT_FREQ, INSTRUMENT_MENU_EMPTY, ...
+    /* S089 P2: the transient page is wav vol frq chn (the chain selector). */
+    {{ DRUM_PARAM_TRANSIENT_WAVE, DRUM_PARAM_TRANSIENT_VOL, DRUM_PARAM_TRANSIENT_FREQ, DRUM_PARAM_VOICE_CHAIN, ...
```

Only cell 3 changes; cells 4–15 stay `INSTRUMENT_MENU_EMPTY`.

---

## 11. `Core/Menu/MenuText.h`: tag and name tables

**ADD after line 199** (the end of `oscRunNames`):

```c
/*
 * DTYPE_ON_OFF tag for the voice chain selector (S089 P2).
 *
 * What:       ONOFF_TAG_VOICE_CHAIN = 2u: the voice_chain rows' dtype byte is
 *             DTYPE_ON_OFF | (2 << 4) = 0x26. Tag 1 stays osc1_run.
 * Why:        S088 D12 reserved further tags as 2, 3, ... for tagged on/off
 *             selectors; menu.c resolves the name table per tag through
 *             menu_onoffTagNames().
 * Accessors:  ROW_SELECT_ONOFF_SPECIAL (the four voice_chain rows).
 * Affiliates: voiceChainNames below; ONOFF_TAG_OSC_RUN above.
 */
#define ONOFF_TAG_VOICE_CHAIN   2u
/*
 * Voice chain names (S089 P2).
 *
 * What:       [0] = count (2), then T+D, WFd. Same shape as oscRunNames.
 * Why:        display, clamp and automation max of the chn cell; the count
 *             must equal VOICE_CHAIN_COUNT (asserted in menu.c).
 * Accessors:  menu_onoffTagNames() (menu.c).
 * Affiliates: VOICE_CHAIN_* (voiceFold.h).
 */
static const char voiceChainNames[][4] = {
    {2}, {"T+D"},{"WFd"},
};
/*
 * Wavefolder type names: the wav cell under WFd (S089 P2).
 *
 * What:       foldTypeNames: [0] = count (8), then the 3-character codes;
 *             foldTypeLongNames: the same order, 8 characters for the
 *             single-parameter view (left field).
 * Why:        under WFd the transient_wave byte is a fold type
 *             (VOICE_FOLD_*); the counts must equal VOICE_FOLD_COUNT
 *             (asserted in menu.c).
 * Accessors:  menu_cellIsFoldWave() users in menu.c.
 * Affiliates: VOICE_FOLD_* (voiceFold.h).
 */
static const char foldTypeNames[][4] = {
    {8}, {"Tri"},{"TrD"},{"Sin"},{"SnD"},{"Asn"},{"Chb"},{"TrI"},{"Fd2"},
};
static const char foldTypeLongNames[][9] = {
    {8}, {"TriFold "},{"TriDecay"},{"SinFold "},{"SinDecay"},
         {"AsymFold"},{"Chebyshv"},{"IntFold "},{"Fold2x  "},
};
```

---

## 12. InstrumentManager

### 12.1 `Core/DSP/Instruments/InstrumentManager.h`

**After line 175** (`IM_SPECIAL_OSC_RUN,`), before
`IM_SPECIAL_WRITER_COUNT`. **ADD:**

```c
    /*
     * S089 P2: voice_chain writer. Clamps the value to 0..VOICE_CHAIN_COUNT-1
     * and stores it in the engine's chainType field through the descriptor's
     * instance offset. On a change of chain it clears transGen.midiFreq so
     * the fold's depth ramp starts flat (voiceFold_process()).
     *
     * What:       IM_SPECIAL_VOICE_CHAIN = 22 (next after IM_SPECIAL_OSC_RUN,
     *             21); IM_SPECIAL_WRITER_COUNT becomes 23 (<= 32, the 5-bit
     *             writer field).
     * Why:        a selector must pass through the special writer so menu
     *             edits, Kit/Scene/Instrument apply, step automation and MIDI
     *             writes all clamp and store identically.
     * Inputs:     slot, byte value.  Outputs: chainType; transGen.midiFreq.
     * Accessors:  instrumentManager_writeSpecialRuntime() switch.
     * Affiliates: VOICE_CHAIN_* (voiceFold.h); voiceChainNames (MenuText.h);
     *             the four voice_chain rows; tools/dsp_test/check_special_tags.py.
     */
    IM_SPECIAL_VOICE_CHAIN,
```

**After line 748** (`uint8_t instrumentManager_osc1Type(uint8_t slot);`).
**ADD:**

```c
/*
 * A slot's current voice chain (S089 P2).
 *
 * What:       the chainType of the engine occupying the slot
 *             (VOICE_CHAIN_*); VOICE_CHAIN_DEFAULT for an unknown type or slot.
 * Why:        Menu relabels and re-lists the transient cells by chain without
 *             owning the tagged runtime mapping (as instrumentManager_osc1Type()).
 * Inputs:     slot.  Outputs: VOICE_CHAIN_*.
 * Accessors:  menu.c (menu_cellIsFoldWave(), menu_instrumentCellLabels(),
 *             the step-automation max); instrumentManager_slotUsesSrr().
 * Affiliates: the engines' chainType; IM_SPECIAL_VOICE_CHAIN.
 */
uint8_t instrumentManager_slotVoiceChain(uint8_t slot);
/*
 * A slot's fold type under WFd (S089 P2).
 *
 * What:       transGen.waveform clamped to VOICE_FOLD_COUNT - 1 (the type the
 *             fold actually plays); VOICE_FOLD_TRI for an unknown slot.
 * Why:        the single-parameter view names frq by fold type ("Bias",
 *             "Decay", ...).
 * Inputs:     slot.  Outputs: VOICE_FOLD_*.
 * Accessors:  menu_instrumentCellLabels() (menu.c).
 * Affiliates: voiceFold_process() (the same clamp).
 */
uint8_t instrumentManager_slotFoldType(uint8_t slot);
```

### 12.2 `Core/DSP/Instruments/InstrumentManager.c`

**After line 14** (`#include "HiHat.h"`). **ADD:**

```c
#include "voiceFold.h"   /* S089 P2: VOICE_CHAIN_* / VOICE_FOLD_* clamps */
```

**After line 1806** (the end of `instrumentManager_slotRunMode()`). **ADD:**

```c
/*
 * Read one slot's voice chain (S089 P2). Contract in InstrumentManager.h.
 *
 * What:       voice->chainType of the engine in the slot; VOICE_CHAIN_DEFAULT
 *             for an unknown type or a missing instance.
 * Why:        same shape as instrumentManager_slotRunMode(), so the chain
 *             decision is read one way everywhere.
 */
uint8_t instrumentManager_slotVoiceChain(uint8_t slot)
{
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *voice = instrumentManager_drumRuntime(slot);
        return voice ? voice->chainType : (uint8_t)VOICE_CHAIN_DEFAULT; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *voice = instrumentManager_snareRuntime(slot);
        return voice ? voice->chainType : (uint8_t)VOICE_CHAIN_DEFAULT; }
    case INSTRUMENT_TYPE_CYM: {
        CymbalVoice *voice = instrumentManager_cymbalRuntime(slot);
        return voice ? voice->chainType : (uint8_t)VOICE_CHAIN_DEFAULT; }
    case INSTRUMENT_TYPE_HAT: {
        HiHatVoice *voice = instrumentManager_hihatRuntime(slot);
        return voice ? voice->chainType : (uint8_t)VOICE_CHAIN_DEFAULT; }
    default:
        return (uint8_t)VOICE_CHAIN_DEFAULT;
    }
}
```

**`instrumentManager_slotUsesSrr()`, comment at lines 2006–2010. MODIFY** (no
code change):

```diff
-     * Each type's default voice chain ends in SRR (S089 P0). P1 adds the
-     * chain selector here: a type returns 1 only when its selected chain
-     * includes SRR. Unknown/empty slots and new types default to 0.
+     * Each type's default voice chain ends in SRR (S089 P0). Both chains of
+     * the four types (T+D and the S089 P2 WFd chain, which keeps the full
+     * post-chain) include SRR, so the answer does not depend on chainType
+     * yet; a future chain without SRR adds its test here
+     * (instrumentManager_slotVoiceChain()). Unknown/empty slots and new types
+     * default to 0.
```

**After line 2383** (the end of the static `instrumentManager_transient()`).
**ADD:**

```c
/*
 * A slot's fold type (S089 P2). Contract in InstrumentManager.h.
 *
 * What:       the transient generator's stored waveform byte, clamped exactly
 *             as voiceFold_process() clamps it, so the label names the type
 *             that plays.
 */
uint8_t instrumentManager_slotFoldType(uint8_t slot)
{
    const TransientGenerator *transient = instrumentManager_transient(slot);

    if (!transient)
        return (uint8_t)VOICE_FOLD_TRI;
    return (transient->waveform < VOICE_FOLD_COUNT)
        ? transient->waveform : (uint8_t)(VOICE_FOLD_COUNT - 1u);
}
```

**After line 3762** (the end of `case IM_SPECIAL_OSC_RUN`), before
`case IM_SPECIAL_NONE:` at 3763. **ADD:**

```c
    case IM_SPECIAL_VOICE_CHAIN: {
        /*
         * voice_chain writer (S089 P2).
         *
         * What:       clamps byteValue to 0..VOICE_CHAIN_COUNT-1 (0 T+D,
         *             1 WFd) and stores it in the engine's chainType through
         *             the descriptor's instance offset (generic TYPE_UINT8
         *             store). When the stored chain changes, clears
         *             transGen.midiFreq so the WFd depth ramp does not start
         *             from a depth left by an earlier WFd period.
         * Why:        every voice_chain write (menu, Kit/Scene/Instrument
         *             apply, step automation, MIDI) must clamp and store the
         *             same way; corrupt storage or an out-of-range MIDI write
         *             becomes WFd (1). The engines read chainType at their own
         *             pace (next block / next trigger), so there is no other
         *             side effect. A switch mid-note changes the stage ahead
         *             of the filter at the next block boundary, like any
         *             selector (step automation applies at steps).
         * Inputs:     slot, descriptor (instance offset), byteValue.
         * Outputs:    chainType; transGen.midiFreq (on change).
         * Accessors:  instrumentManager_writeRuntimeInternal() via the tag.
         * Affiliates: VOICE_CHAIN_* (voiceFold.h); the four voice_chain rows;
         *             voiceFold_process() (midiFreq 0 = no previous depth).
         */
        void *instance = instrumentManager_runtimeInstance(slot);
        TransientGenerator *transient = instrumentManager_transient(slot);
        uint8_t chain = (byteValue >= VOICE_CHAIN_COUNT)
            ? (uint8_t)(VOICE_CHAIN_COUNT - 1u) : byteValue;
        uint8_t *field;

        if (!instance || !descriptor) return 0u;
        field = (uint8_t *)instance + descriptor->runtime.offset;
        if (*field != chain && transient)
            transient->midiFreq = 0u;
        instrumentManager_writeParameter(
            (Parameter){ (void *)field, descriptor->runtime.parameter_type },
            chain);
        return 1u; }
```

**After line 3836** (the classifier's `osc1_run` line). **ADD:**

```c
    /*
     * S089 P2: voice_chain. Not an oscillator row (no IM_SPECIAL_OSC* bits),
     * so it is classified by key alone, like transient_freq; the boot
     * diagnostic compares every descriptor tag against this rule.
     */
    if (strcmp(key, "voice_chain") == 0) return IM_SPECIAL_VOICE_CHAIN;
```

---

## 13. `Core/Menu/menu.c`

**ADD the include after line 68** (`#include "transientGenerator.h"`):

```c
#include "voiceFold.h"   /* S089 P2: VOICE_CHAIN_* / VOICE_FOLD_* (name asserts) */
```

**ADD forward declarations after line 2252** (after the existing
`menu_formatWaveValue3()` prototype). The three new helpers are defined
beside the table helpers (§13.1, §13.2), but they are first used at lines
3362, 3376 and 6290, so they need prototypes in the file's existing
forward-reference style:

```c
/* S089 P2: tagged on/off tables and the WFd transient-cell rules; defined
 * beside getMenuItemNameForValue() / menu_formatTransientName(). */
static const char (*menu_onoffTagNames(uint8_t tag))[4];
static uint8_t menu_cellIsFoldWave(const menu_cell_t *cell);
static void menu_instrumentCellLabels(uint8_t slot,
                                      const ParamDescriptor *descriptor,
                                      const char **cat, const char **lng);
```

### 13.1 One tag → name-table helper, used by the six `DTYPE_ON_OFF` sites

**ADD the definition before `getMenuItemNameForValue()`** (line 11804), next
to the other table helpers (prototype: §13 head):

```c
/*
 * Name table of a tagged DTYPE_ON_OFF cell (S088 D12; S089 P2).
 *
 * What:       returns the 3-character name table for a dtype high-nibble tag:
 *             ONOFF_TAG_VOICE_CHAIN -> voiceChainNames (T+D, WFd); every other
 *             non-zero tag -> oscRunNames (rst, fre, itp), exactly as before.
 *             Entry [0][0] is the count; names start at [1].
 * Why:        the six tagged on/off sites in this file used oscRunNames
 *             directly ("tag != 0 means run mode"); the second tag needs its
 *             own table, and one helper keeps display, clamp, automation max
 *             and the step-edit line in agreement.
 * Inputs:     tag (dtype >> 4, non-zero).  Outputs: the table.
 * Accessors:  menu_formatCellValue3(), va_formatValue3(), menu_clampCellValue(),
 *             menu_automationValueMax(), menu_formatAutomationValue3(), the
 *             single-parameter step-edit display.
 * Affiliates: ONOFF_TAG_* / oscRunNames / voiceChainNames (MenuText.h).
 */
static const char (*menu_onoffTagNames(uint8_t tag))[4]
{
    return (tag == ONOFF_TAG_VOICE_CHAIN) ? voiceChainNames : oscRunNames;
}
```

**MODIFY the six sites.** In each tagged branch, replace `oscRunNames` with
the table the helper returns. The plain on/off path (high nibble 0) is
untouched.

| Site | Lines | Tag source |
|---|---|---|
| `menu_formatCellValue3()` | 3362–3365 | `menu_cellDtype(cell) >> 4` |
| `va_formatValue3()` | 6518–6521 | `menu_cellDtype(cell) >> 4` |
| `menu_clampCellValue()` | 6802–6804 | `menu_cellDtype(cell) >> 4` |
| `menu_automationValueMax()` | 13318–13319 | `descriptor->dtype >> 4` |
| `menu_formatAutomationValue3()` | 13362–13365 | `descriptor->dtype >> 4` |
| step-edit display | 14490–14493 | `menu_cellDtype(&cell) >> 4` |

Pattern (`menu_formatCellValue3()` shown; the others are the same shape):

```diff
-        if ((uint8_t)(menu_cellDtype(cell) >> 4) != 0u) {
-            const uint8_t idx = (value < (uint8_t)oscRunNames[0][0])
-                ? (uint8_t)value : 0u;
-            memcpy(out, oscRunNames[1 + idx], 3);
+        if ((uint8_t)(menu_cellDtype(cell) >> 4) != 0u) {
+            /* S089 P2: the table follows the tag (run mode or voice chain). */
+            const char (*names)[4] = menu_onoffTagNames(
+                (uint8_t)(menu_cellDtype(cell) >> 4));
+            const uint8_t idx = (value < (uint8_t)names[0][0])
+                ? (uint8_t)value : 0u;
+            memcpy(out, names[1 + idx], 3);
```

Update each site's existing comment `Affiliates:` line to add
`menu_onoffTagNames(), voiceChainNames`.

### 13.2 The fold-wave cell: name, count and long name

**ADD the definitions before the comment block that opens
`menu_formatTransientName()`** (line 11778; prototypes: §13 head):

```c
/*
 * Whether a cell is the transient wav cell of a WFd slot (S089 P2).
 *
 * What:       1 for an instrument cell whose descriptor is transient_wave on a
 *             slot whose chain is VOICE_CHAIN_WAVEFOLDER; else 0.
 * Why:        under WFd the wav byte selects a fold type, so its names
 *             (foldTypeNames) and count (VOICE_FOLD_COUNT) replace the
 *             transient list's; every other cell keeps the MENU_TRANS path.
 * Inputs:     cell (may be NULL).  Outputs: 0/1.
 * Accessors:  menu_formatCellValue3(), va_formatValue3(), menu_clampCellValue(),
 *             the single-parameter edit painter, menu_instrumentCellLabels().
 * Affiliates: instrumentManager_slotVoiceChain(); foldTypeNames.
 */
static uint8_t menu_cellIsFoldWave(const menu_cell_t *cell)
{
    return (uint8_t)(cell && cell->kind == MENU_CELL_INSTRUMENT &&
        cell->descriptor &&
        strcmp(cell->descriptor->file_key, "transient_wave") == 0 &&
        instrumentManager_slotVoiceChain(cell->slot) ==
            VOICE_CHAIN_WAVEFOLDER);
}

/*
 * Category and long name of an instrument cell, by chain (S089 P2).
 *
 * What:       the descriptor's category/long name, except for the three
 *             transient rows of a WFd slot, which read "WavFldr" and:
 *               transient_wave  "FoldType"
 *               transient_vol   "Depth"
 *               transient_freq  by fold type: "Bias" (Tri, Sin, TrI),
 *                               "Decay" (TrD, SnD), "Asymmtry" (Asn),
 *                               "Harmonic" (Chb), "Ratio" (Fd2).
 *             The 3-character short names (wav vol frq) are unchanged.
 * Why:        plan §5 step 5: the same three cells mean different things per
 *             chain; only the labels change, never the stored values.
 * Inputs:     slot, descriptor, cat/lng (outputs, never NULL on return).
 * Outputs:    *cat, *lng.
 * Accessors:  the single-parameter view header (menu.c:14452-14456), the
 *             step-automation target line (14168-14182) and the modulation
 *             target full display (6290-6291).
 * Affiliates: instrumentManager_slotVoiceChain(), instrumentManager_slotFoldType().
 */
static void menu_instrumentCellLabels(uint8_t slot,
                                      const ParamDescriptor *descriptor,
                                      const char **cat, const char **lng)
{
    static const char *const frqNames[VOICE_FOLD_COUNT] = {
        "Bias", "Decay", "Bias", "Decay", "Asymmtry", "Harmonic", "Bias",
        "Ratio",
    };

    *cat = descriptor->category;
    *lng = descriptor->long_name;
    if (instrumentManager_slotVoiceChain(slot) != VOICE_CHAIN_WAVEFOLDER)
        return;
    if (strcmp(descriptor->file_key, "transient_wave") == 0) {
        *cat = "WavFldr";
        *lng = "FoldType";
    } else if (strcmp(descriptor->file_key, "transient_vol") == 0) {
        *cat = "WavFldr";
        *lng = "Depth";
    } else if (strcmp(descriptor->file_key, "transient_freq") == 0) {
        *cat = "WavFldr";
        *lng = frqNames[instrumentManager_slotFoldType(slot)];
    }
}
```

**MODIFY `menu_formatCellValue3()`, `DTYPE_MENU` case, lines 3376–3379. ADD
before the `MENU_WAVEFORM` test:**

```c
        /* S089 P2: a WFd slot's wav cell shows the fold type. */
        if (menu_cellIsFoldWave(cell)) {
            memcpy(out, foldTypeNames[1u + ((value < VOICE_FOLD_COUNT)
                                            ? value : VOICE_FOLD_COUNT - 1u)], 3);
            break;
        }
```

**MODIFY `va_formatValue3()`, `DTYPE_MENU` case, lines 6531–6534:** the same
insertion, with `valueAsText` in place of `out`.

**MODIFY `menu_clampCellValue()`, `DTYPE_MENU` case, after line 6827** (after
the `MENU_WAVEFORM` count override):

```c
        /* S089 P2: a WFd wav cell holds a fold type (0..VOICE_FOLD_COUNT-1). */
        if (menu_cellIsFoldWave(cell))
            n = (uint8_t)VOICE_FOLD_COUNT;
```

**MODIFY the single-parameter step-edit display, `DTYPE_MENU`, lines
14513–14525. ADD before the `MENU_TRANS` user-name branch:**

```c
                /* S089 P2: a WFd wav cell: the long fold name on the left, the
                 * 3-character code on the right. */
                if (menu_cellIsFoldWave(&cell)) {
                    const uint8_t t = (value < VOICE_FOLD_COUNT)
                        ? (uint8_t)value : (uint8_t)(VOICE_FOLD_COUNT - 1u);

                    memcpy(&editDisplayBuffer[1][0], foldTypeLongNames[1u + t], 8);
                    memcpy(&editDisplayBuffer[1][13], foldTypeNames[1u + t], 3);
                    break;
                }
```

**MODIFY the label sites:**

- **Lines 14452–14456** (single-parameter view header):

  ```diff
  -                menu_copyPaddedField(&editDisplayBuffer[0][0],
  -                                     cell.descriptor->category, 8u);
  -                menu_copyPaddedField(&editDisplayBuffer[0][8],
  -                                     cell.descriptor->long_name, 8u);
  +                /* S089 P2: WFd relabels the transient cells. */
  +                const char *cat = cell.descriptor->category;
  +                const char *lng = cell.descriptor->long_name;
  +
  +                if (cell.kind == MENU_CELL_INSTRUMENT)
  +                    menu_instrumentCellLabels(cell.slot, cell.descriptor,
  +                                              &cat, &lng);
  +                menu_copyPaddedField(&editDisplayBuffer[0][0], cat, 8u);
  +                menu_copyPaddedField(&editDisplayBuffer[0][8], lng, 8u);
  ```

  The `else if` body already has braces (line 14450), so the new locals go
  at the top of that block.
- **Lines 14168–14182** (step-automation target line): take `cat`/`lng`
  from `menu_instrumentCellLabels(slot, descriptor, ...)` and use them in
  place of `descriptor->category` / `descriptor->long_name` in the two copy
  loops.
- **Lines 6290–6291** (modulation/automation target full display): the same
  change, using the function's existing `slot` local.

### 13.3 Automation max and display under WFd

**After line 13972** (`max_val = menu_automationValueMax(desc);` in the
step-automation encoder). **ADD:**

```c
        /*
         * S089 P2: under WFd the transient wav byte is a fold type, so step
         * automation stops at the last fold type. menu_automationValueMax()
         * has no slot; this caller knows it (s). Values that arrive another
         * way are clamped by voiceFold_process() at render.
         */
        if (desc && instrumentParam_isVoiceParameter(vt) &&
            strcmp(desc->file_key, "transient_wave") == 0 &&
            instrumentManager_slotVoiceChain(instrumentParam_slot(vt)) ==
                VOICE_CHAIN_WAVEFOLDER)
            max_val = (uint8_t)(VOICE_FOLD_COUNT - 1u);
```

**MODIFY `menu_formatVoiceAutomationValue3()`, after line 13422** (after the
`MENU_WAVEFORM` branch's `return;`). **ADD:**

```c
    /* S089 P2: a WFd slot's automated wav value names its fold type. */
    if (descriptor && instrumentParam_isVoiceParameter(target) &&
        strcmp(descriptor->file_key, "transient_wave") == 0 &&
        instrumentManager_slotVoiceChain(instrumentParam_slot(target)) ==
            VOICE_CHAIN_WAVEFOLDER) {
        memcpy(buf, foldTypeNames[1u + ((value < VOICE_FOLD_COUNT)
                                        ? value : VOICE_FOLD_COUNT - 1u)], 3);
        return;
    }
```

### 13.4 Name-table asserts

**ADD after line 11941** (after the existing `transientNames`
`_Static_assert`):

```c
/* S089 P2: the chain and fold name tables must list every stored value. */
_Static_assert(voiceChainNames[0][0] == VOICE_CHAIN_COUNT,
               "voiceChainNames must list every VOICE_CHAIN_* value");
_Static_assert(foldTypeNames[0][0] == VOICE_FOLD_COUNT &&
               foldTypeLongNames[0][0] == VOICE_FOLD_COUNT,
               "fold name tables must list every VOICE_FOLD_* type");
```

---

## 14. Host tests

### 14.1 `tools/dsp_test/check_special_tags.py`

**Line 52** (the `simple` dict). **MODIFY:**

```diff
         'instrument_drive': 'INSTRUMENT_DRIVE', 'lfo_rate': 'LFO_RATE',
+        # S089 P2: the voice chain selector has its own clamping writer.
+        'voice_chain': 'VOICE_CHAIN',
```

### 14.2 `tools/dsp_test/modules/instrument_manager.c` and `Makefile`

- **Makefile line 110.** Also extract the new tag:
  `--macro ONOFF_TAG_OSC_RUN --macro ONOFF_TAG_VOICE_CHAIN`.
- **`instrument_manager.c` line 45.** ADD `#define IM_SPECIAL_VOICE_CHAIN 22u`.
- **After line 125.** ADD the row built from the real macro:

  ```c
  /* S089 P2: the voice chain row, from the real tag macro. */
  static const ParamDescriptor g_chn_row = ROW_SELECT_ONOFF_SPECIAL(
      "voice_chain", "Voice", "Chain", "chn", ONOFF_TAG_VOICE_CHAIN,
      chainType, TYPE_UINT8, IM_SPECIAL_VOICE_CHAIN);
  ```

- **Lines 197–200. MODIFY law 6:**

  ```c
      /* 6: osc1_run after osc1_type; S089 P2: voice_chain is now last. */
      expect(DRUM_PARAM_OSC1_RUN == DRUM_PARAM_OSC1_TYPE + 1u,
             "law 6: osc1_run follows osc1_type");
      expect(DRUM_PARAM_VOICE_CHAIN == DRUM_PARAM_OSC1_RUN + 1u,
             "law 6: voice_chain follows osc1_run");
      expect(DRUM_PARAM_VOICE_CHAIN + 1u == DRUM_PARAM_COUNT,
             "law 6: voice_chain is the last Drum row");
  ```

- **After law 7.** ADD law 8 (and list it in the file header comment):

  ```c
      /* 8: the voice chain row (S089 P2): tag 2, selector, no ramp. */
      expect((g_chn_row.dtype & 0x0fu) == DTYPE_ON_OFF,
             "law 8: chn dtype low nibble is DTYPE_ON_OFF");
      expect((g_chn_row.dtype >> 4) == ONOFF_TAG_VOICE_CHAIN,
             "law 8: chn dtype high nibble is ONOFF_TAG_VOICE_CHAIN");
      expect(ONOFF_TAG_VOICE_CHAIN != ONOFF_TAG_OSC_RUN,
             "law 8: chn and run tags differ");
      expect(g_chn_row.flags ==
                 (INSTRUMENT_PARAM_FLAG_AUTOMATABLE |
                  INSTRUMENT_PARAM_FLAG_SELECTOR),
             "law 8: chn is automatable+selector, never morph/mod");
      expect(seq_rampDtypeOk(&g_chn_row) == 0u, "law 8: chn never ramps");
  ```

### 14.3 New module: `tools/dsp_test/modules/voicefold.c` and target

**Makefile. ADD** the target (append `voicefold fold_tables` to `.PHONY` and
`all`):

```make
# S089 P2: the real voiceFold.c (ITCM sections stripped) against reference
# models; break proof: dropping the bias DC term must fail law F3.
voicefold: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSPAudio/transientGenerator.h \
	  --typedef TransientGenerator --out $(GEN)/vf_transient.c
	$(EX) --src $(ROOT)/Core/DSPAudio/snapEg.h --typedef SnapEg --out $(GEN)/vf_snap.c
	$(EX) --src $(ROOT)/Core/DSPAudio/voiceFold.h --body --out $(GEN)/voiceFold.host.h
	$(EX) --src $(ROOT)/Core/DSPAudio/voiceFold.c --body --out $(GEN)/voiceFold.host.c
	$(EX) --src $(ROOT)/Core/DSPAudio/voiceFoldTables.c --body \
	  --out $(GEN)/voiceFoldTables.host.c
	$(HOSTCC) $(HOSTFLAGS) -Wno-unknown-attributes modules/voicefold.c \
	  -o build/voicefold -lm
	./build/voicefold
	mkdir -p build/break_vf
	sed 's/(kA \* t2 - kC)/(kA * t2)/' $(GEN)/voiceFold.host.c \
	  > build/break_vf/voiceFold.host.c
	$(HOSTCC) -Ibuild/break_vf $(HOSTFLAGS) -Wno-unknown-attributes \
	  modules/voicefold.c -o build/voicefold_break -lm
	@if ./build/voicefold_break >/dev/null 2>&1; then \
	  echo "voicefold_break: FAILED (DC-term break not caught)"; exit 1; \
	else echo "voicefold_break: caught (law F3 fails as expected)"; fi

fold_tables:
	cd $(ROOT) && python3 tools/plots/gen_fold_tables.py --check
```

**`modules/voicefold.c`:**
- **Header comment:** in the house style (What/Why/Inputs/Outputs/
  Accessors/Affiliates).
- **Includes:** `vf_transient.c`, `vf_snap.c`, `voiceFold.host.h`,
  `voiceFold.host.c` and `voiceFoldTables.host.c`, after a local
  `#include "config.h"` stub (`OUTPUT_DMA_SIZE` comes from the prelude).
- **Laws:**

| Law | Check |
|---|---|
| F1 | Triangle core: `vf_foldAbs()` path vs the spike's `driveShaper_triFold()` (copied into the test as the reference) for every int16 at depths 1, 2.5, 8 and bias −0.3, 0, 0.17, 0.3: `|err| ≤ 2⁻²⁰`. |
| F2 | Silence → exactly 0 for every type, every `frq`, every depth (`D3` DC compensation). |
| F3 | Bias DC: `Tri`/`Sin` with bias ≠ 0 and a zero input give 0 (the break build drops `kC` and must fail). |
| F4 | Bounds: random full-scale input, all types, `vol`/`frq` corners, Snare gains (`inGain` 16, `outGain` 1/16 and 1): every output stays inside ±32767 with no wrap (the int32 value before the int16 store is checked). |
| F5 | Ramp continuity: two blocks with `vol` stepped 0 → 1: the depth word increases monotonically across the boundary, and `midiFreq` holds the end depth (Q11). |
| F6 | Decay: `TrD` with `snapEg.value = 1` and `frq` 0: the depth reaches 1.0 after ≤ 7 blocks (`voiceFold_decayStep[0]` = 0.1451); `frq` 127: still above 1 after 2,700 blocks. |
| F7 | Polynomial: p(1) = 1 within 1e-6; max \|p(t) − sin(πt/2)\| ≤ 1.1e-4. |
| F8 | Type clamp: waveform 9 renders exactly as type 7 (`Fd2`); the byte is not modified. |
| F9 | `Chb`, m = 1, `vol` = 1, a full-scale sine of period 64: the output's 3rd harmonic exceeds the fundamental by ≥ 40 dB. |
| F10 | `Fd2` at `frq` 0 equals `Tri` at bias 0 (the same input and depth), sample for sample. |

---

## 15. Verification

**Build.** `make all && make img`, with the `Written: … OK` line.

**Static gates.** These compare against the S089 P0 build; keep a copy of
its ELF first.

1. **Step 1** (linker only): `build/lxr02.bin` is byte-identical to P0.
2. **ITCM:**
   - the addresses of `osc_setFreq`, every `calc*Block` and every
     `osc_*Block` symbol are unchanged;
   - `.itcm.s089` follows `.itcm.s087.__stub` in the map;
   - ITCM ≤ 16,384 B, expected ≈14.0 KB of 16 KB.
3. **RAM:** `runtime_slots` 7,584 B; `.bss`/`.data` sizes unchanged; only
   one data symbol is new (none expected).
4. **Mixer (P0 §4.2):**
   - **Inlined set:** identical to P0. No function may leave or join the
     set inlined into the mixer; in particular `busComp_processBlock` stays
     standalone (as in P0) and `mixer_decimateBlock` stays inlined (as in
     P0). `mixer_calcNextSampleBlock` itself grows, by the two chain branches
     and `voicePost_gainDist`. (Wording corrected by assessment F5: the
     first version said `busComp_processBlock` must not be standalone, but
     it already is in P0.)
   - **Hot loops:** every P0 hot loop is unchanged or layout-only, and one
     new ≈21-instruction loop (`voicePost_gainDist`) appears.
5. **Fold costs:** `elf_loopdump.py --sym voiceFold_triLoop
   --sym voiceFold_sinLoop --sym voiceFold_chbLoop --sym voiceFold_fd2Loop`.
   - **Limits:** each hot loop ≤ 24 instructions (≤ 25 accepted for
     `voiceFold_sinLoop` on Drum/Snare, D2).
   - **Recording:** record the measured counts in this document and in
     `CPU_USE_DSP_AUDIT.md`.
6. **Host tests:**
   - pass: `instrument_manager` (laws 3–8), `special_tags`, `voicefold`
     (F1–F10 plus the break), `fold_tables`;
   - unchanged: `run_modes`, which still has its S088 gap.

**Hardware.** Matrix H1–H9:

| # | Check |
|---|---|
| H1 | `T+D` everywhere: existing kits sound the same as P0, including `Snp`/`Ofs` transients and run modes; the `cpu` widget reads the same. |
| H2 | `chn` cell: `T+D`/`WFd` names, the encoder clamps at `WFd`, the step-automation bar has 2 values. |
| H3 | Each fold type on Drum: depth sweep (no zipper), `frq` sweep, `frq` 0/64/127; the single-parameter labels read `WavFldr / FoldType / Depth / <frq name>`. |
| H4 | The same on Snare (tone oscillator), with `mix` 0, 0.5, 0.9: the fold character does not change with `mix`. |
| H5 | The same on Cymbal/HiHat: no FM-modulator bleed in the output (the no-add post loop). |
| H6 | `Filter drv` 0 and 127 with every type. |
| H7 | `TrD`/`SnD`: short and long decays; retriggers in `rst`/`fre`/`itp` without clicks. |
| H8 | LFO and velocity → `vol` (depth) and → `frq`; Morph between two depths. |
| H9 | Worst-case six-slot Scene, all `WFd` (dearest type `Asn`/`Sin` on all slots): the `cpu` reading is at or below the all-`T+D` reading. |

---

## 16. Documentation (after hardware passes)

| Document | Change |
|---|---|
| `INSTRUMENTS_DSP_REFERENCE.md` | the WFd chain, the fold types, the control mapping, the guards |
| `CPU_USE_DSP_AUDIT.md` | Session 089 P2: measured fold loop counts; `voicePost_gainDist`; ITCM growth |
| `STORAGE_SRAM_MANIFEST.md` | HiHatVoice 520 B; ITCM total; `runtime_slots` unchanged |
| `FILESYSTEM_SPEC.md` | `voice_chain` key (0 T+D, 1 WFd, SELECTOR) |
| `MIDI_SPEC.md` §5.5 | `voice_chain` appended after `osc1_run` |
| `S089_P1_VOICE_CHAIN_PLAN.md` | mark §2–§5 phase 1 as delivered by P2, with the §1 deviations here |
| `MEMORY.md` | Binding Rules / DSP: "WFd chain (S089 P2): the transient rows and `transGen`/`snapEg` state belong to the fold under WFd; every `transGen.waveform` read outside the generator must test `chainType`." |
| Open item | T+D latent: Cymbal/HiHat `pitchMod` keeps the last snap value when `wav` leaves `Snp` mid-snap (pre-existing; not changed in P2) |

---

## 17. Implementation progress (this session, 2026-10-30)

Working notes, appended as the schedule is executed. All figures are from this
tree; the P0 baseline ELF/BIN/map are kept at `/tmp/s089p2_baseline/`.

### Step 1 — linker ITCM order: PASS

- `STM32F765VIHx_FLASH.ld` now names `.itcm`, `.itcm.s085`,
  `.itcm.s087 .itcm.s087.*` in that order, then the catch-all `.itcm.*`.
- `make all` relinked. `build/lxr02.bin` is byte-identical to the P0 build
  (SHA-256 `680fed3f93f9313c08eb6c50f81825b60cb89e37199079b157b2542910d064fb`),
  and so is `build/lxr02.elf`
  (`40959fcbf4222a6c8c4d8337358aa2f6cc0b16ed195d43f8b44320c1d1f45f05`).
  ITCM stays 12,656 / 16,384 B.

### Step 2 — voiceFold.h/.c, tables, generator, Makefile: PASS

- Added `Core/DSPAudio/voiceFold.h`, `voiceFold.c`,
  `voiceFoldTables.c` (generated) and `tools/plots/gen_fold_tables.py`;
  listed both sources in `DSP_SRCS`.
- `gen_fold_tables.py`: C1 (entry 0 = 0.1450982, entry 127 = 0.0003627) and
  C2 (strictly decreasing) pass, and `--check` reproduces the committed file
  byte for byte.
- Deviation from the schedule's expectation: with the new (unreferenced)
  sources in the build, the image is **not** byte-identical. The only
  differences are the link-order of LTO-generated `__*_veneer` stubs (same
  sizes, same code addresses). Step 1, the linker-only gate, *was*
  byte-identical, so the rule itself is proved.

### Step 3 — engine fields and init: PASS

- `chainType` added after `ampRampArm` in all four headers; default set in
  all four `*_initVoice()`; `voiceFold.h` included by each engine.
- Probe sizes: Drum 632, Snare 452, Cymbal 508 B unchanged; HiHat 516 -> 520 B.
  `runtime_slots` = 7,584 B (0x1da0) unchanged. `.bss` 412,884 B unchanged.
  `.data` is 420 -> 424 B, but the named data total is 403 B in both: LTO
  moved one small local between partitions (+4 B in ltrans7, -1 B in
  ltrans6) and the section end padded to 4 B. No new data object.

### Step 4 — `voicePost_gainDist()`: PASS

- Added as the no-add twin in `voicePostChain.h`, adjacent to
  `voicePost_addGainDist()`.

### Step 5 — engine guards and dispatch: PASS

- All eight `transGen.waveform` reads outside the generator now test
  `chainType == VOICE_CHAIN_DEFAULT`: Drum 317/333, Snare 221/317, Cymbal
  241/358, HiHat 251/377.
- Drum folds `buf` after the `itp` blend; Snare folds `transBuf` (D1) with
  `1/(1-mix)` input normalisation; Cymbal/HiHat fold `buf` before the ZDF
  filter and use `voicePost_gainDist()` under WFd.
- Cymbal/HiHat also write `pitchMod = 1.0f` every WFd block, so a chain
  switch during a snap cannot leave the carrier detuned.
- The schedule's Cymbal/HiHat fold call uses gains 1/1. Note for hardware H5:
  HiHat renders its FM carrier at gain 0.5 (unchanged), so for the same `vol`
  its fold drive is 6 dB below Cymbal's. The `vol` range still spans the full
  fold depth; this is called out so it is judged by ear, not mistaken for a
  wiring error.

### Step 6 — descriptors, MenuText, InstrumentManager, menu: PASS

- Counts 43/42/43/43; `*_PARAM_VOICE_CHAIN` appended; `voice_chain` row
  uses `ROW_SELECT_ONOFF_SPECIAL` with tag 2 and
  `IM_SPECIAL_VOICE_CHAIN`; the transient page is `wav vol frq chn` on all
  four engines.
- `IM_SPECIAL_VOICE_CHAIN = 22`, `IM_SPECIAL_WRITER_COUNT = 23`; the writer
  clamps and clears `transGen.midiFreq` on a chain change; the diagnostic
  classifier returns the tag by key alone.
- Menu: `menu_onoffTagNames()` replaces the six direct `oscRunNames` reads;
  `menu_cellIsFoldWave()` / `menu_instrumentCellLabels()` name the fold
  type, depth and per-type frq; the step-automation max and display use
  `VOICE_FOLD_COUNT` under WFd. Name-table `_Static_assert`s added.
- `special tags OK (171 rows, 0 mismatches)`;
  `instrument_manager failures: 0` (laws 3–8, all three break builds caught).

### Step 7 — host tests and cost gate: PASS

- `tools/dsp_test/modules/voicefold.c` runs F1–F10 against the real
  `voiceFold.c`; `voicefold failures: 0`; the DC-term break build is
  caught by F3.
- `fold_tables OK`.
- `elf_loopdump.py` per-sample counts (this build):

  | Loop | Types | Instructions | Limit |
  |---|---|---:|---:|
  | `voiceFold_triLoop` | Tri, TrD, TrI | 12 | 24 |
  | `voiceFold_chbLoop` | Chb | 14 | 24 |
  | `voiceFold_fd2Loop` | Fd2 | 20 | 24 |
  | `voiceFold_sinLoop` | Sin, SnD, Asn | 23 | 24 (25 accepted on Drum/Snare) |

- Mixer hot-path gate (`elf_hotloops.py`, P0 mixer vs this mixer):
  **31 equal, 6 shrank, 0 grew**. The 23-instruction add post loops appear as
  two 21-instruction no-add twins (Cymbal and HiHat inlined separately), as
  expected.
- `busComp_processBlock` is standalone in both P0 and P2;
  `mixer_decimateBlock` is inlined in both, so the inlined set is unchanged
  for those two. New standalone symbols are the fold code, the voice-chain
  accessor and the name tables.
- ITCM: 12,656 -> 13,960 B (+1,304 B), within D4. No existing ITCM function
  moved; only the linker-generated veneer stubs moved to after
  `.itcm.s089` (they are stubs, not loops). The S085 rule is intact.
- `run_modes` still fails to compile at its pre-existing S088 gap
  (`distortion_curveSampleF` vs the `...16` stub in the module); it is
  unchanged and out of P2 scope.
- Firmware: `make all && make img` -> `Written:build/LXRV2_lxr02.img
  (681512b) OK`. Flash headroom 72,152 B; DTCM statics 4,372 B; FX arena
  margin 3,808 B.

---

## 18. Post-implementation assessment (2026-10-30)

An independent review of the tree against §3–§14. It was rebuilt and
re-measured here and compared with the hardware-passed S089 P0 ELF.

### 18.1 Verdict

The implementation **follows the schedule and is ready for hardware
testing.**
- `voiceFold.h` and `voiceFold.c` match §4–§5 exactly; a diff shows blank
  lines between blocks only.
- Every engine, descriptor, InstrumentManager, MenuText, menu, linker,
  Makefile and host-test change is present at the scheduled site, with its
  comment block.
- No defect blocks testing.

One finding (F1, a gain error that came from the schedule itself) should be
fixed before the HiHat hardware checks.

### 18.2 Verified

| Area | Result |
|---|---|
| Build | `make all` exit 0; `make img` → `Written:build/LXRV2_lxr02.img (681512b) OK`. All 12 changed C files recompiled syntax-only with their own Makefile flags: **0 warnings**. |
| Host tests | `instrument_manager` 0 failures (all three break builds caught); `voicefold` 0 failures, F1–F10 present, DC-term break caught by F3; `fold_tables OK`; `special tags OK (171 rows, 0 mismatches)`. |
| Fold loops (linked ELF) | `triLoop` 12, `chbLoop` 14, `fd2Loop` 20, `sinLoop` 23: all ≤ 24, matching §17. Every loop is branch-free; the absolute value compiles to `cmp; it lt; neglt` (constant time), and `smmul` to `smull`. |
| Mixer hot loops vs P0 | 31 equal, 6 shrank, 0 grew. The P0/P2 loop multisets differ in 8 loops, and **in every one only integer/move instructions differ; the float operations are identical in count and order**, so the T+D audio arithmetic is unchanged (S0 by inspection; H1 confirms). One new 21-instruction loop ×2 is `voicePost_gainDist()` (Cymbal and HiHat inlined separately). |
| Inlined set | No DSP function joined or left the mixer's inlined set. `busComp_processBlock` is standalone in P0 and P2; `mixer_decimateBlock` is inlined in both. The other symbol changes are menu/UI LTO partitioning (`checkScrollSign`, `sa_applyTrackMarkers`, `va_*`) and the new fold/menu helpers. `mixer_calcNextSampleBlock` 13,848 → 14,412 B. |
| ITCM | 12,656 → 13,960 B. All 35 existing ITCM functions keep their address and size. Only the three linker veneers (`__osc_wtbPlanBlock…`, `__memset`, `__osc_userBind`) moved, to after `.itcm.s089`. The byte differences inside the old range are the `b.w`/`bl` targets of those veneers and the SRAM addresses in literal pools (§18.3 F3); no loop changed shape. |
| RAM | Struct sizes as scheduled (HiHat 520 B); `runtime_slots` 7,584 B; `.bss` size 412,884 B; DTCM statics 4,372 B. **But see F3**: `.data` 420 → 424 B. |
| `pitchMod` safety | The only `osc.pitchMod` writers are init, the pitch envelope (Drum/Snare reassign it every block) and the snap; the LFO acts through `modNodeValue`. The Cymbal/HiHat `pitchMod = 1.0f` under WFd therefore cannot override anything. |
| Descriptor space | 43 rows per type against `INSTRUMENT_PARAM_COUNT` 64 (firmware); the host test's 240-ID stub is unrelated. |

### 18.3 Findings

| # | Severity | Finding | Recommendation |
|---|---|---|---|
| F1 | **Medium** (sound; a schedule error) | HiHat renders its FM carrier at gain **0.5** (`HiHat.c` `osc_renderTypedFm(..., 0.5f)` / `calcNextOscSampleFmBlock(..., 0.5f)`), but §8.4 copied Cymbal's gains 1/1. So HiHat folds at half Cymbal's depth for the same `vol`. Worse, at high depth the folded output reaches full scale, **+6 dB above the T+D HiHat level**. §17 noted the gain difference but left it to be judged by ear. | In `HiHat_calcSyncBlockVoice()`, pass `inGain = 2.0f, outGain = 0.5f`, with a comment naming the 0.5 carrier gain. The fold then sees full scale (the same character as Cymbal) and its output returns to the carrier's level (vol 0 stays identity at the T+D level). There is no per-sample cost: both factors are per-block constants. Drum (full scale in every mode) and Cymbal (1.0) are correct. |
| F2 | Low (ITCM use) | `voiceFold_process()` is **1,016 B** of the +1,304 B ITCM (§1 estimated the whole file at ≈1.4 KB, but the per-block setup alone at ~250 B). It runs once per voice per block, and the per-sample work is all in the 290 B of loops. ITCM headroom is now 2,424 B. | Optional: drop `VOICE_FOLD_SECTION` from `voiceFold_process()` (keep `noinline, noclone`) so the setup runs from flash and only the loops stay in ITCM. That recovers ≈1 KB. The call from the engines then goes flash → flash, and the loops are reached through ITCM veneers. Re-run the §15 gates. |
| F3 | Low (layout; a gate not met as written) | §15 gate 3 expected "no data symbol moved". LTO partition order put one 1-byte `.data` object (`pm_instrument_request_type`) on a 4-byte boundary, so `.data` grows 4 B and `.bss` (8-byte aligned) starts at `0x20020dc8` instead of `0x20020dc0`. **851 SRAM1 data symbols move by +1…+8 B.** No object was added or resized, and DTCM did not move. This is the layout-shift pattern S089 P0 F4 named as a possible latent-fault trigger. | No code fix: the shift comes from LTO placement, not from P2 data. Treat it as a hardware discriminator: if anything misbehaves on hardware with every slot on `T+D`, suspect layout (P0 F4) before the fold. Record the shift in `STORAGE_SRAM_MANIFEST.md`. |
| F4 | Info (cost) | `sinLoop` measured 23 against the 20 estimate: three `vmov.f32` accumulator copies before the `vfma`s, plus one instruction for the IT-block absolute value. | None needed; it is within 24 on every engine. If headroom is wanted later, the polynomial can be reordered so two of the copies disappear. |
| F5 | Info (schedule wording) | §15 gate 4 said `busComp_processBlock` "must not appear as a standalone symbol". It is standalone in P0 as well; the gate means **unchanged from P0**, which holds. | Read gate 4 as "the inlined set is identical to P0". |

### 18.4 Corrections to §17 and `CPU_USE_DSP_AUDIT.md`

- **§17 Step 7 and audit item 58** say the no-add loops replace
  "23-instruction add versions". The Cymbal/HiHat add post loops are **25**
  instructions in both P0 and P2, so `voicePost_gainDist()` saves 4 per
  sample, as planned. The 23 → 22 loops that "shrank" are unrelated loops
  (a signed-byte table read with a conditional add) that changed by
  register allocation only.
- **Audit item 60** says "Text 679,088 → 681,088 B". The P0 text was
  **676,600 B**, so text grew **+4,488 B**. 681,088 is correct.
- **§17 Step 3 RAM bullet:** add that the 4 B `.data` growth moves the start
  of `.bss` by 8 B (F3). The *size* of `.bss` is unchanged, but its layout
  is not.

### 18.5 Hardware notes (§15 matrix)

- **H1 first:** every slot on `T+D`. A pass also clears the F3 layout shift.
- **H5 (Cymbal/HiHat):** run after the F1 fix. Without it, HiHat `WFd`
  sounds weaker in fold depth than Cymbal at the same `vol`, and up to 6 dB
  louder than its own `T+D` level at high depth.
- **H9 (CPU):** the dearest WFd loop is `sinLoop` at 23, against the
  click's 22 + 2, so an all-`Asn`/`Sin` Scene should read at or below
  all-`T+D` on Drum/Snare and clearly below on Cymbal/HiHat (no-add post
  loop −4).

---

## 19. Assessment fixes F1–F5 (2026-10-30)

All five §18.3 findings are resolved. **Hardware re-test passed (user,
2026-10-30).** The worst-case Scene reaches 100 % on the `cpu` widget,
which is now a binding ceiling (`S089_P2_WAVEFOLDER_CHAIN.md` §8).

### 19.1 Changes

| # | File | Change |
|---|---|---|
| F1 | `Core/DSP/Instruments/HiHat/HiHat.c` (`HiHat_calcSyncBlockVoice()`, the WFd fold call) | `voiceFold_process(..., 1.0f, 1.0f)` → **`(..., 2.0f, 0.5f)`**. The comment names the 0.5 carrier render gain: `inGain` 2 restores full scale into the fold (the same depth per `vol` as Cymbal), and `outGain` 0.5 returns the folded block to the T+D carrier level (`vol` 0 is still the identity there). Both are per-block constants, so there is no per-sample cost. Drum and Cymbal are unchanged (full-scale oscillator sections). |
| F2 | `Core/DSPAudio/voiceFold.c` | New `VOICE_FOLD_ENTRY` (`noinline, noclone`, no section). `voiceFold_process()` uses it and now runs from flash; the loops keep `VOICE_FOLD_LOOP` (`.itcm.s089`). The placement comment is updated. |
| F3 | `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md` | No code change (the shift comes from LTO placement). New S089 P2 entry and "current as of" figures: struct sizes, ITCM, text, and the `.data` 420 → 424 B / `.bss` start +8 B layout shift, with the P0 F4 hardware note. |
| F4 | `Core/DSPAudio/voiceFold.c`, `tools/dsp_test/Makefile` | The sine loop is split. `voiceFold_sinLoop()` (Sin, SnD): `y = t·P − kC`. **New `voiceFold_asnLoop()`** (Asn): `y = t·P + kA·t²`, with no bias. The combined `kA·t² − kC` term had cost one extra accumulator copy per sample. `voiceFold_process()` dispatches by type. The `voicefold` break build now removes `kC` from `t * p - kC`, and still fails law F3 as intended. |
| F5 | this document, §15 gate 4 | The gate now reads "inlined set identical to P0: `busComp_processBlock` stays standalone, `mixer_decimateBlock` stays inlined". |
| — | `CPU_USE_DSP_AUDIT.md` items 57, 58, 60 | §18.4 corrections applied (25-instruction add loops, P0 text 676,600 B), plus the F2/F4 figures. |
| — | repository root | Removed 12 stray `*.d` dependency files left by the §18 syntax-only recompiles (`-MMD` without `-o`). They were git-ignored, but not created by the implementation. |

### 19.2 Measured (rebuilt; compared with P0 and with P2 as first delivered)

| Check | Result |
|---|---|
| Build | `make all` exit 0, no warnings in the changed files; `make img` → `Written:build/LXRV2_lxr02.img (681656b) OK` |
| Host tests | `instrument_manager` 0 failures (3 breaks caught); `voicefold` 0 failures, and the break is caught by F3; `fold tables OK`; `special tags OK (171 rows, 0 mismatches)` |
| Fold loops | `triLoop` 12, `chbLoop` 14, `fd2Loop` 20, **`sinLoop` 22**, **`asnLoop` 22** (was 23 combined) |
| ITCM | **13,032 / 16,384 B** (was 13,960; −928 B). Only the loops are in `.itcm.s089`; `voiceFold_process()` (1,036 B) is in flash. Every P0 ITCM function keeps its address and size; only the three linker veneers moved, as before. Headroom 3,352 B. |
| Flash | text 681,088 → 681,232 B (+144: the entry's flash veneers to the five loops); headroom 72,008 B |
| Mixer | `elf_hotloops.py`: vs P0, 31 equal, 6 shrank, **0 grew**; vs P2 as first delivered, **39 equal, 0 shrank, 0 grew** (the F1 constant change and F2/F4 do not touch the inlined renders). |
| RAM layout | no data symbol moved relative to P2 as first delivered; DTCM unchanged from P0 (F3 shift as recorded) |

### 19.3 Re-test focus (§15 matrix)

- **H1:** every slot on `T+D`, first. A pass also clears the F3 layout
  shift.
- **H5:** HiHat now folds like Cymbal for the same `vol`, and its WFd level
  matches its T+D level at `vol` 0. At full depth, HiHat stays at its
  carrier level instead of +6 dB.
- **H3/H4:** `Sin`, `SnD` and `Asn` now come from two loops; they must
  sound as before (the arithmetic is the same, minus one register copy).
- **H9:** the dearest WFd loop is now 22 (`Sin`/`SnD`/`Asn`), equal to the
  click it replaces, plus the click's mix-in 2 saved.
