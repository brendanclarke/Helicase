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
 * What:       .itcm.s089 for the four per-sample loops (VOICE_FOLD_LOOP);
 *             the per-block entry voiceFold_process() stays in flash
 *             (VOICE_FOLD_ENTRY). All five are noinline/noclone.
 * Why:        D4 (user): the per-sample fold code runs from zero-wait ITCM
 *             like the S087 sample readers. The entry is ~1 KB of control
 *             code that runs once per voice per block, so it gains almost
 *             nothing from ITCM; keeping it in flash saves ~1 KB of the 16 KB
 *             ITCM (S089 P2 assessment F2). The linker script places .itcm.s089 after
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
/* The per-block entry: one out-of-line copy in flash (.text), not ITCM. */
#define VOICE_FOLD_ENTRY   __attribute__((noinline, noclone))

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
 * Sine-fold loop: Sin, SnD (S089 P2).
 *
 * What:       the triangle fold as above, then a smooth sine shape, in float:
 *               t' = a - 2^22                tri(x + b) in Q22 (as float)
 *               t2 = t' x t'
 *               P  = k1 + t2 (k3 + k5 t2)    the 5th-order polynomial
 *               y  = t' P - kC               kC = the bias DC term (D3)
 *               out = (int16) y
 *             The Q22 scale (2^-22 per power of t) and the output scale S
 *             are folded into k1 = A1 S 2^-22, k3 = A3 S 2^-66 and
 *             k5 = A5 S 2^-110, so the loop has no conversion multiply. The
 *             smallest constant (k5 ≈ 1.8e-30) stays a normal float, so
 *             -Ofast flush-to-zero does not apply.
 * Why:        the sine fold rounds every corner of the triangle (fewer high
 *             partials, less aliasing). Sin/SnD and Asn are separate loops
 *             (S089 P2 assessment F4): each vfma overwrites its accumulator,
 *             so a combined "kA t2 - kC" term cost one more accumulator copy
 *             per sample than either type needs on its own.
 * Inputs:     buf/size; d, dStep, b (as the triangle loop); k1, k3, k5, kC
 *             (per-block constants from voiceFold_process()).
 * Outputs:    buf in place; |y| <= 32767 x outGain (S includes the
 *             1/(1 + |kC/S|) normalisation).
 * Accessors:  voiceFold_process().
 * Affiliates: voiceFold_asnLoop() (the same front end and polynomial).
 */
static VOICE_FOLD_LOOP void voiceFold_sinLoop(int16_t *buf, uint8_t size,
        int32_t d, const int32_t dStep, const int32_t b, const float k1,
        const float k3, const float k5, const float kC)
{
    uint8_t i;

    for (i = 0u; i < size; i++) {
        const int32_t w = vf_smlawb(d, buf[i], b);
        const float t = (float)(vf_foldAbs(w) - VF_Q22);  /* tri, Q22 units */
        const float t2 = t * t;
        const float p = k1 + t2 * (k3 + k5 * t2);         /* odd polynomial */

        d += dStep;
        buf[i] = (int16_t)(int32_t)(t * p - kC);          /* DC removed */
    }
}

/*
 * Asymmetric sine-fold loop: Asn (S089 P2).
 *
 * What:       voiceFold_sinLoop() without bias (b = 2^22, so no DC term), plus
 *             the even-order accent on the folded value:
 *               y  = t' P + kA t2            kA = a S 2^-44
 *             At silence t' = 0, so the output is exactly 0 (D3).
 * Why:        the accent reuses t2 and replaces the spike's pre-fold cubic,
 *             which cost ≈3 more (S089 P2 §1 item 2); its own loop saves the
 *             accumulator copy a combined Sin/Asn loop needed (assessment F4).
 * Inputs:     buf/size; d, dStep; k1, k3, k5 (as voiceFold_sinLoop()); kA.
 * Outputs:    buf in place; |y| <= 32767 x outGain (S includes 1/(1 + a)).
 * Accessors:  voiceFold_process().
 * Affiliates: voiceFold_sinLoop().
 */
static VOICE_FOLD_LOOP void voiceFold_asnLoop(int16_t *buf, uint8_t size,
        int32_t d, const int32_t dStep, const float k1, const float k3,
        const float k5, const float kA)
{
    uint8_t i;

    for (i = 0u; i < size; i++) {
        const int32_t w = vf_smlawb(d, buf[i], VF_Q22);
        const float t = (float)(vf_foldAbs(w) - VF_Q22);  /* tri, Q22 units */
        const float t2 = t * t;
        const float p = k1 + t2 * (k3 + k5 * t2);         /* odd polynomial */

        d += dStep;
        buf[i] = (int16_t)(int32_t)(t * p + kA * t2);     /* even accent */
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
VOICE_FOLD_ENTRY void voiceFold_process(TransientGenerator *tg, SnapEg *eg,
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
        /* Sin/SnD and Asn run separate loops (assessment F4); the Q22
         * powers of 2 are folded into the constants (x 2^-22/-66/-110/-44). */
        if (type == VOICE_FOLD_ASN)
            voiceFold_asnLoop(buf, size, d0, dStep,
                VF_SIN_A1 * s * 2.38418579e-7f,     /* x 2^-22  */
                VF_SIN_A3 * s * 1.35525272e-20f,    /* x 2^-66  */
                VF_SIN_A5 * s * 7.70371978e-34f,    /* x 2^-110 */
                acc * s * 5.68434189e-14f);         /* x 2^-44  */
        else
            voiceFold_sinLoop(buf, size, d0, dStep,
                (int32_t)((1.0f + bias) * (float)VF_Q22),
                VF_SIN_A1 * s * 2.38418579e-7f,
                VF_SIN_A3 * s * 1.35525272e-20f,
                VF_SIN_A5 * s * 7.70371978e-34f,
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
