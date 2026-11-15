/*
 * tools/dsp_test/modules/distortion.c — the voice distortion curve and its
 * makeup (S095 P4 C3).
 *
 * Laws:
 *   DI1 drv 0: distortion_curveSampleF() returns the S073 identity,
 *       (int16)(int32)(x / 32767 * 32767), bit for bit, over a float sweep
 *       of -40000..40000 (step 0.37) and every int16 through
 *       distortion_curveSample16();
 *   DI2 every row, inputs -32767..32767: the output stays within +-32767
 *       (the makeup only lowers the curve) and the curve is odd
 *       (f(-x) == -f(x)); -32768 is left out, the identity at drv 0 passes
 *       it through unchanged, as before;
 *   DI3 the table: entry 0 exactly 1, non-increasing, all in (0, 1];
 *   DI4 every row: out_gain == distortion_makeup[s] * (1 + shape) exactly
 *       (setDistortionShape() is the only writer).
 * Outputs: one line per failure; "distortion failures: N"; exit 1 if N > 0.
 * Accessors: make -C tools/dsp_test distortion.
 * Affiliates: distortion.h/.c, distortionMakeup.c, gen_dist_makeup.py.
 */
#include <stdint.h>
#include <stdio.h>
#include <math.h>
/* prelude.h (Distortion, INITCM_EFFECT) is force-included by HOSTFLAGS. */

#define DISTORTION_MAKEUP_COUNT 128u
/*
 * Plain prototypes, as distortion.h gives the firmware: with them the
 * extracted `__inline` definition of setDistortionShape() is an external
 * definition (C99 6.7.4), so the host link never misses it.
 */
void setDistortionShape(Distortion *dist, uint8_t shape);
float distortion_calcSampleFloat(const Distortion *dist, float x);
#include "dist_table.c"     /* the real distortion_makeup[] */
#include "dist_curve.c"     /* the real curve helpers (distortion.h) */
#include "dist_shape.c"     /* the real setDistortionShape() */

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        printf("FAIL %s\n", msg);
        failures++;
    }
}

int main(void)
{
    Distortion d;
    float x;
    int32_t i;
    uint32_t s;
    int ok;

    /* DI1 */
    setDistortionShape(&d, 0u);
    ok = 1;
    for (x = -40000.0f; x <= 40000.0f; x += 0.37f) {
        const float ref = x * (1.f / 32767.f);

        ok = ok && distortion_curveSampleF(&d, x) ==
                   (int16_t)(int32_t)((1.0f * ref / (1.0f + 0.0f * fabsf(ref))) * 32767);
    }
    for (i = -32768; i <= 32767; i++) {
        const float ref = (int16_t)i / 32767.f;

        ok = ok && distortion_curveSample16(&d, (int16_t)i) ==
                   (int16_t)(int32_t)((1.0f * ref / (1.0f + 0.0f * fabsf(ref))) * 32767);
    }
    expect(ok, "DI1 drv 0 is not the identity");
    /* DI2, DI4 */
    for (s = 0u; s < 128u; s++) {
        char msg[80];

        setDistortionShape(&d, (uint8_t)s);
        ok = d.out_gain == distortion_makeup[s] * (1.0f + d.shape);
        for (i = -32767; i <= 32767 && ok; i += 7) {
            const int16_t y = distortion_curveSample16(&d, (int16_t)i);

            ok = (y <= 32767 && y >= -32767) &&
                 distortion_curveSample16(&d, (int16_t)-i) == (int16_t)-y;
        }
        snprintf(msg, sizeof msg, "DI2/DI4 row %u", (unsigned)s);
        expect(ok, msg);
    }
    /* DI3 */
    ok = distortion_makeup[0] == 1.0f;
    for (s = 1u; s < 128u; s++)
        ok = ok && distortion_makeup[s] <= distortion_makeup[s - 1u] &&
             distortion_makeup[s] > 0.0f;
    expect(ok, "DI3 the makeup table");
    printf("distortion failures: %d\n", failures);
    return failures ? 1 : 0;
}
