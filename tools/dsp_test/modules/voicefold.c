/*
 * voiceFold host tests (S089 P2).
 *
 * What:       compiles the real Core/DSPAudio/voiceFold.c and
 *             voiceFoldTables.c (ITCM sections stripped by extract.py) and
 *             checks the fold laws in S089_P2_IMPLEMENTATION.md §14.3.
 * Why:        the fold is integer/float arithmetic that the board cannot
 *             report; every type, the depth ramp, the decay envelope, the
 *             polynomial and the clamping are proved here before hardware.
 * Inputs:     build/gen/vf_transient.c, vf_snap.c, voiceFold.host.h,
 *             voiceFold.host.c, voiceFoldTables.host.c.
 * Outputs:    one line per failure (first 40); "voicefold failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test voicefold.
 * Affiliates: voiceFold.c/.h; transientGenerator.h; snapEg.h;
 *             S084 spike DriveShaper.h (the reference triFold).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "vf_transient.c"
#include "vf_snap.c"
#include "voiceFold.host.h"
#include "voiceFoldTables.host.c"
#include "voiceFold.host.c"

#define VF_TEST_SIZE 32u
#define VF_TOL_LSB   8.0f

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        if (failures < 40)
            printf("FAIL: %s\n", msg);
        failures++;
    }
}

static void expect_near(float got, float want, float tol, const char *msg)
{
    if (!(fabsf(got - want) <= tol)) {
        if (failures < 40)
            printf("FAIL: %s (got %.6f want %.6f)\n", msg, got, want);
        failures++;
    }
}

/* The S084 spike's periodic triangle fold, copied as the reference. */
static float ref_triFold(float v, float t)
{
    const float w = (v + t) * (1.0f / (4.0f * t));
    int32_t n = (int32_t)(w * 16777216.0f);

    n &= 0x00ffffff;
    {
        const float frac = (float)n * (1.0f / 16777216.0f);
        const float d = frac - 0.5f;

        return t - 4.0f * t * ((d < 0.0f) ? -d : d);
    }
}

static float clampf(float x, float lo, float hi)
{
    return (x < lo) ? lo : ((x > hi) ? hi : x);
}

static void setup(TransientGenerator *tg, SnapEg *eg, uint8_t type,
                  float vol, float frq)
{
    memset(tg, 0, sizeof(*tg));
    tg->waveform = type;
    tg->volume = vol;
    tg->pitch = 0.25f + frq * (1.0f / 33.9f);
    tg->midiFreq = 0u;
    eg->value = 1.0f;
}

/* Per-type float reference for one sample, used by law F4. */
static float model(uint8_t type, float vol, float frq, float ig,
                   float outGain, float s)
{
    const float v = clampf(frq, 0.0f, 127.0f);
    float peak = 1.0f + ((type == VOICE_FOLD_TRI_INT)
        ? VF_SPAN_INT : VF_SPAN) * vol * vol;

    /* TrD/SnD step snapEg.value once before using it (S089 P2 §5.3). */
    if (type == VOICE_FOLD_TRD || type == VOICE_FOLD_SND) {
        const float e = clampf(1.0f -
            voiceFold_decayStep[(uint8_t)(v + 0.5f)], 0.0f, 1.0f);

        peak = 1.0f + (peak - 1.0f) * e * e;
    }
    const float depth = fminf(peak * clampf(ig, 1.0f,
                                           VOICE_FOLD_IN_GAIN_MAX),
                              VF_DI_MAX);
    const float x = (s / 32768.0f) * depth;

    switch (type) {
    case VOICE_FOLD_TRD:
    case VOICE_FOLD_TRI:
    case VOICE_FOLD_TRI_INT: {
        const float bias = (type == VOICE_FOLD_TRD) ? 0.0f
            : clampf(VF_BIAS_MAX * (v - 64.0f) * (1.0f / 63.0f),
                     -VF_BIAS_MAX, VF_BIAS_MAX);

        return (ref_triFold(x + bias, 1.0f) - bias) *
               32767.0f * outGain / (1.0f + fabsf(bias));
    }
    case VOICE_FOLD_SIN:
    case VOICE_FOLD_SND:
    case VOICE_FOLD_ASN: {
        const float bias = (type == VOICE_FOLD_SIN)
            ? clampf(VF_BIAS_MAX * (v - 64.0f) * (1.0f / 63.0f),
                     -VF_BIAS_MAX, VF_BIAS_MAX) : 0.0f;
        const float acc = (type == VOICE_FOLD_ASN)
            ? VF_ASYM_MAX * v * (1.0f / 127.0f) : 0.0f;
        const float c = bias * (VF_SIN_A1 + bias * bias *
                                (VF_SIN_A3 + VF_SIN_A5 * bias * bias));
        const float t = ref_triFold(x + bias, 1.0f);
        const float t2 = t * t;
        const float sc = 32767.0f * outGain / (1.0f + fabsf(c) + acc);

        return sc * (t * (VF_SIN_A1 + t2 * (VF_SIN_A3 + VF_SIN_A5 * t2)) +
                     acc * t2 - c);
    }
    case VOICE_FOLD_CHB: {
        const float m = v * (1.0f / 127.0f);
        const float k = fminf(depth * 0.125f, 1.0f);
        const float sc = 32767.0f * outGain / ((1.0f + 2.0f * m) * k);
        const float xx = (s / 32768.0f) * k;

        return xx * ((1.0f + 2.0f * m) * sc - 4.0f * m * sc * xx * xx);
    }
    case VOICE_FOLD_FD2:
    default: {
        const int32_t r6 = (int32_t)((1.0f + (VF_RATIO_MAX - 1.0f) * v *
                                      (1.0f / 127.0f)) * 64.0f + 0.5f);
        const float y1 = ref_triFold(x, 1.0f);

        return 32767.0f * outGain *
               ref_triFold(y1 * ((float)r6 * (1.0f / 64.0f)), 1.0f);
    }
    }
}

static void law_f1_triangle_core(void)
{
    static const float depths[3] = { 1.0f, 2.5f, 8.0f };
    static const float biases[4] = { -0.3f, 0.0f, 0.17f, 0.3f };
    uint8_t di, bi;

    for (di = 0u; di < 3u; di++) {
        for (bi = 0u; bi < 4u; bi++) {
            const int32_t d = (int32_t)(depths[di] * VF_D_ONE);
            const int32_t b = (int32_t)((1.0f + biases[bi]) * (float)VF_Q22);
            int32_t s;

            for (s = -32768; s <= 32767; s++) {
                const int32_t w = vf_smlawb(d, (int32_t)s, b);
                const int32_t y = vf_foldAbs(w) - b;
                const float got = (float)y * (1.0f / (float)VF_Q22);
                const float want = ref_triFold(
                    ((float)s * (1.0f / 32768.0f)) * depths[di] + biases[bi],
                    1.0f) - biases[bi];

                if (fabsf(got - want) > 9.53674316e-7f) { /* 2^-20 */
                    expect_near(got, want, 9.53674316e-7f,
                                "F1: triangle core vs spike triFold");
                    return;
                }
            }
        }
    }
}

static void law_f2_silence(void)
{
    static const float vols[3] = { 0.0f, 0.5f, 1.0f };
    static const float frqs[3] = { 0.0f, 64.0f, 127.0f };
    uint8_t type, vi, fi;

    for (type = 0u; type < VOICE_FOLD_COUNT; type++) {
        for (vi = 0u; vi < 3u; vi++) {
            for (fi = 0u; fi < 3u; fi++) {
                TransientGenerator tg;
                SnapEg eg;
                int16_t buf[VF_TEST_SIZE];
                uint8_t i;

                setup(&tg, &eg, type, vols[vi], frqs[fi]);
                memset(buf, 0, sizeof(buf));
                voiceFold_process(&tg, &eg, buf, VF_TEST_SIZE, 1.0f, 1.0f);
                for (i = 0u; i < VF_TEST_SIZE; i++)
                    if (buf[i] != 0)
                        expect(0, "F2: silence must fold to exactly 0");
            }
        }
    }
}

static void law_f3_bias_dc(void)
{
    static const float frqs[4] = { 0.0f, 32.0f, 96.0f, 127.0f };
    uint8_t fi;
    uint8_t type;

    for (type = VOICE_FOLD_TRI; type <= VOICE_FOLD_SIN; type += 2u) {
        for (fi = 0u; fi < 4u; fi++) {
            TransientGenerator tg;
            SnapEg eg;
            int16_t buf[VF_TEST_SIZE];
            uint8_t i;

            setup(&tg, &eg, type, 1.0f, frqs[fi]);
            memset(buf, 0, sizeof(buf));
            voiceFold_process(&tg, &eg, buf, VF_TEST_SIZE, 1.0f, 1.0f);
            for (i = 0u; i < VF_TEST_SIZE; i++)
                expect(buf[i] == 0, "F3: biased fold DC compensation");
        }
    }
}

static void law_f4_bounds(void)
{
    static const uint8_t types[VOICE_FOLD_COUNT] = {
        VOICE_FOLD_TRI, VOICE_FOLD_TRD, VOICE_FOLD_SIN, VOICE_FOLD_SND,
        VOICE_FOLD_ASN, VOICE_FOLD_CHB, VOICE_FOLD_TRI_INT, VOICE_FOLD_FD2,
    };
    static const float vols[2] = { 0.0f, 1.0f };
    static const float frqs[3] = { 0.0f, 64.0f, 127.0f };
    static const float gains[3][2] = {
        { 1.0f, 1.0f }, { 16.0f, 1.0f / 16.0f }, { 16.0f, 1.0f },
    };
    uint8_t ti, vi, fi, gi, i;
    uint32_t rng = 0x12345678u;

    for (ti = 0u; ti < VOICE_FOLD_COUNT; ti++) {
        for (vi = 0u; vi < 2u; vi++) {
            for (fi = 0u; fi < 3u; fi++) {
                for (gi = 0u; gi < 3u; gi++) {
                    TransientGenerator tg;
                    SnapEg eg;
                    int16_t buf[VF_TEST_SIZE];
                    int16_t src[VF_TEST_SIZE];

                    setup(&tg, &eg, types[ti], vols[vi], frqs[fi]);
                    for (i = 0u; i < VF_TEST_SIZE; i++) {
                        rng ^= rng << 13;
                        rng ^= rng >> 17;
                        rng ^= rng << 5;
                        src[i] = (int16_t)rng;
                        if (i == 0u) src[i] = (int16_t)-32768;
                        if (i == 1u) src[i] = (int16_t)32767;
                    }
                    memcpy(buf, src, sizeof(buf));
                    voiceFold_process(&tg, &eg, buf, VF_TEST_SIZE,
                                      gains[gi][0], gains[gi][1]);
                    for (i = 0u; i < VF_TEST_SIZE; i++) {
                        const float want = model(types[ti], vols[vi],
                                                 frqs[fi], gains[gi][0],
                                                 gains[gi][1], (float)src[i]);
                        if (!(fabsf((float)buf[i] - want) <= VF_TOL_LSB)) {
                            if (failures < 20)
                                printf("F4 t=%u vol=%.2f frq=%.0f g=%.3f i=%u"
                                       " s=%d got=%d want=%.3f\n",
                                       (unsigned)types[ti], (double)vols[vi],
                                       (double)frqs[fi], (double)gains[gi][0],
                                       (unsigned)i, (int)src[i],
                                       (int)buf[i], (double)want);
                            failures++;
                        }
                        if (!(fabsf(want) <= 32767.5f)) {
                            if (failures < 30)
                                printf("F4 bound t=%u vol=%.2f frq=%.0f"
                                       " g=%.3f i=%u s=%d want=%.6f\n",
                                       (unsigned)types[ti], (double)vols[vi],
                                       (double)frqs[fi], (double)gains[gi][0],
                                       (unsigned)i, (int)src[i], (double)want);
                            failures++;
                        }
                    }
                }
            }
        }
    }
}

static void law_f5_ramp(void)
{
    TransientGenerator tg;
    SnapEg eg;
    int16_t buf[VF_TEST_SIZE];
    uint8_t i;

    setup(&tg, &eg, VOICE_FOLD_TRI, 0.0f, 64.0f);
    for (i = 0u; i < VF_TEST_SIZE; i++) buf[i] = 1000;
    voiceFold_process(&tg, &eg, buf, VF_TEST_SIZE, 1.0f, 1.0f);
    expect(tg.midiFreq == 2048u, "F5: vol 0 stores depth 1.0 in Q11");
    {
        const int16_t first = buf[0];
        const int16_t last = buf[VF_TEST_SIZE - 1u];

        for (i = 0u; i < VF_TEST_SIZE; i++)
            expect(buf[i] == first, "F5: flat block stays flat");
        tg.volume = 1.0f;
        for (i = 0u; i < VF_TEST_SIZE; i++) buf[i] = 1000;
        voiceFold_process(&tg, &eg, buf, VF_TEST_SIZE, 1.0f, 1.0f);
        expect(tg.midiFreq == 16384u, "F5: vol 1 stores depth 8.0 in Q11");
        expect(buf[0] >= last, "F5: the boundary does not step down");
        expect(buf[1] > buf[0], "F5: the depth ramp continues upward");
        for (i = 1u; i < VF_TEST_SIZE; i++)
            expect(buf[i] >= buf[i - 1u], "F5: the depth ramp only rises");
    }
}

static void law_f6_decay(void)
{
    TransientGenerator tg;
    SnapEg eg;
    int16_t buf[VF_TEST_SIZE];
    uint16_t blocks;

    setup(&tg, &eg, VOICE_FOLD_TRD, 1.0f, 0.0f);
    for (blocks = 0u; blocks < 8u && eg.value > 0.0f; blocks++) {
        memset(buf, 0, sizeof(buf));
        voiceFold_process(&tg, &eg, buf, VF_TEST_SIZE, 1.0f, 1.0f);
    }
    expect(blocks <= 7u, "F6: frq 0 decays to clean within 7 blocks");
    expect(tg.midiFreq == 2048u, "F6: frq 0 ends at depth 1.0");

    setup(&tg, &eg, VOICE_FOLD_TRD, 1.0f, 127.0f);
    for (blocks = 0u; blocks < 2700u; blocks++) {
        memset(buf, 0, sizeof(buf));
        voiceFold_process(&tg, &eg, buf, VF_TEST_SIZE, 1.0f, 1.0f);
    }
    expect(eg.value > 0.0f, "F6: frq 127 still decaying after 2700 blocks");
    expect(tg.midiFreq > 2048u, "F6: frq 127 stays above depth 1.0");
}

static void law_f7_polynomial(void)
{
    float t;
    float max_err = 0.0f;

    expect_near(VF_SIN_A1 + VF_SIN_A3 + VF_SIN_A5, 1.0f, 1e-6f,
                "F7: p(1) = 1");
    for (t = -1.0f; t <= 1.0f; t += 0.0001f) {
        const float t2 = t * t;
        const float p = t * (VF_SIN_A1 + t2 * (VF_SIN_A3 + VF_SIN_A5 * t2));
        const float err = fabsf(p - sinf((float)M_PI * 0.5f * t));

        if (err > max_err) max_err = err;
    }
    expect(max_err <= 1.1e-4f, "F7: polynomial error <= 1.1e-4");
}

static void law_f8_type_clamp(void)
{
    TransientGenerator tg7, tg9;
    SnapEg eg7, eg9;
    int16_t a[VF_TEST_SIZE], b[VF_TEST_SIZE];
    uint8_t i;

    for (i = 0u; i < VF_TEST_SIZE; i++) a[i] = b[i] = (int16_t)(i * 900 - 14000);
    setup(&tg7, &eg7, VOICE_FOLD_FD2, 0.6f, 90.0f);
    setup(&tg9, &eg9, 9u, 0.6f, 90.0f);
    voiceFold_process(&tg7, &eg7, a, VF_TEST_SIZE, 1.0f, 1.0f);
    voiceFold_process(&tg9, &eg9, b, VF_TEST_SIZE, 1.0f, 1.0f);
    expect(memcmp(a, b, sizeof(a)) == 0,
           "F8: waveform 9 renders exactly as Fd2");
    expect(tg9.waveform == 9u, "F8: the stored byte is not rewritten");
}

static void law_f9_chebyshev(void)
{
    TransientGenerator tg;
    SnapEg eg;
    int16_t buf[64];
    float re1 = 0.0f, im1 = 0.0f, re3 = 0.0f, im3 = 0.0f;
    float mag1, mag3;
    uint8_t n;

    for (n = 0u; n < 64u; n++)
        buf[n] = (int16_t)(32767.0 * sin(2.0 * M_PI * (double)n / 64.0));
    setup(&tg, &eg, VOICE_FOLD_CHB, 1.0f, 127.0f);
    voiceFold_process(&tg, &eg, buf, 64u, 1.0f, 1.0f);
    for (n = 0u; n < 64u; n++) {
        const float ph = (float)(2.0 * M_PI * (double)n / 64.0);

        re1 += (float)buf[n] * cosf(ph);
        im1 -= (float)buf[n] * sinf(ph);
        re3 += (float)buf[n] * cosf(3.0f * ph);
        im3 -= (float)buf[n] * sinf(3.0f * ph);
    }
    mag1 = sqrtf(re1 * re1 + im1 * im1);
    mag3 = sqrtf(re3 * re3 + im3 * im3);
    expect(20.0f * log10f(mag3 / (mag1 > 1e-6f ? mag1 : 1e-6f)) >= 40.0f,
           "F9: Chb m=1 makes the 3rd harmonic dominate by >= 40 dB");
}

static void law_f10_fd2_is_tri(void)
{
    TransientGenerator tgT, tgF;
    SnapEg egT, egF;
    int16_t a[VF_TEST_SIZE], b[VF_TEST_SIZE];
    uint8_t i;

    for (i = 0u; i < VF_TEST_SIZE; i++) a[i] = b[i] = (int16_t)(i * 1000 - 12000);
    setup(&tgT, &egT, VOICE_FOLD_TRI, 0.5f, 64.0f);
    setup(&tgF, &egF, VOICE_FOLD_FD2, 0.5f, 0.0f);
    voiceFold_process(&tgT, &egT, a, VF_TEST_SIZE, 1.0f, 1.0f);
    voiceFold_process(&tgF, &egF, b, VF_TEST_SIZE, 1.0f, 1.0f);
    expect(memcmp(a, b, sizeof(a)) == 0,
           "F10: Fd2 at frq 0 equals Tri at bias 0");
}

int main(void)
{
    law_f1_triangle_core();
    law_f2_silence();
    law_f3_bias_dc();
    law_f4_bounds();
    law_f5_ramp();
    law_f6_decay();
    law_f7_polynomial();
    law_f8_type_clamp();
    law_f9_chebyshev();
    law_f10_fd2_is_tri();
    printf("voicefold failures: %d\n", failures);
    return failures ? 1 : 0;
}
