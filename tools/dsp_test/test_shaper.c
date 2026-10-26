/*
 * Filter drive shapers (S084 VX2 VX2-10.7).
 *
 * What:       feeds the (u, s) pairs exported by
 *             tools/plots/plot_waveshapers.py --export-ref through the real
 *             driveShaper_apply() and compares with the script's expected
 *             level-trimmed output.
 * Why:        plan §4.4: the script is the reference for every type, and the
 *             voice filter must match it.
 * Inputs:     the frozen/shaper reference files.
 * Outputs:    failure count; exits nonzero on any failure.
 * Accessors:  make -C tools/dsp_test shaper.
 * Affiliates: Core/DSPAudio/DriveShaper.h/.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "DriveShaper.h"

static unsigned failures;

static const char *signals[] = { "sine40", "sine80", "sine127", "kick110" };
static const char *types[] = { "sft", "rct", "art", "thf", "fdc", "bsw",
                               "tsw", "lsw", "lsq", "sfd", "afd", "asf",
                               "chb", "rsb" };

static float *readFloatFile(const char *path, unsigned *count)
{
    FILE *fh = fopen(path, "rb");
    float *data;
    long bytes;

    *count = 0u;
    if (!fh)
        return NULL;
    fseek(fh, 0, SEEK_END);
    bytes = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    if (bytes <= 0 || (bytes % 4) != 0) {
        fclose(fh);
        return NULL;
    }
    data = (float *)malloc((size_t)bytes);
    if (!data) {
        fclose(fh);
        return NULL;
    }
    if (fread(data, 1, (size_t)bytes, fh) != (size_t)bytes) {
        free(data);
        fclose(fh);
        return NULL;
    }
    fclose(fh);
    *count = (unsigned)(bytes / 4);
    return data;
}

static int typeIndex(const char *name)
{
    unsigned i;

    for (i = 0u; i < DRIVE_TYPE_COUNT; i++)
        if (strcmp(types[i], name) == 0)
            return (int)i;
    return -1;
}

int main(void)
{
    unsigned s;
    unsigned t;

    for (s = 0u; s < sizeof(signals) / sizeof(signals[0]); s++) {
        for (t = 0u; t < DRIVE_TYPE_COUNT; t++) {
            char base[96];
            char path[128];
            unsigned count_u = 0u;
            unsigned count_s = 0u;
            unsigned count_o = 0u;
            float *u;
            float *sv;
            float *want;
            DriveShaperState st;
            unsigned i;
            double tol;
            const int type = typeIndex(types[t]);

            snprintf(base, sizeof(base), "frozen/shaper/%s_%s",
                     signals[s], types[t]);
            snprintf(path, sizeof(path), "%s_u.bin", base);
            u = readFloatFile(path, &count_u);
            snprintf(path, sizeof(path), "%s_s.bin", base);
            sv = readFloatFile(path, &count_s);
            snprintf(path, sizeof(path), "%s_out.bin", base);
            want = readFloatFile(path, &count_o);
            if (!u || !sv || !want || count_u != count_s ||
                count_u != count_o) {
                printf("FAIL: cannot read %s\n", base);
                failures++;
                free(u); free(sv); free(want);
                continue;
            }
            /*
             * Memoryless types are compared against the script's float64
             * output through the sine-fold polynomial; the stateful types are
             * tighter because their state is integer/float32-exact.
             */
            tol = (type == DRIVE_TYPE_RCT || type == DRIVE_TYPE_ART ||
                   type == DRIVE_TYPE_FDC || type == DRIVE_TYPE_TSW ||
                   type == DRIVE_TYPE_LSW || type == DRIVE_TYPE_LSQ ||
                   type == DRIVE_TYPE_RSB) ? 1e-4 : 2e-3;
            driveShaper_reset(&st);
            {
            float inc = 0.0f;

            for (i = 0u; i < count_u; i++) {
                float got;

                if ((i % 32u) == 0u)
                    inc = driveShaper_blockInc(&st);
                got = driveShaper_apply((uint8_t)type, &st, u[i], sv[i], inc);
                if (fabs((double)got - (double)want[i]) > tol) {
                    printf("FAIL: %s sample %u got %.6f want %.6f\n",
                           base, i, (double)got, (double)want[i]);
                    failures++;
                    break;
                }
            }
            }
            free(u);
            free(sv);
            free(want);
        }
    }

    printf("shaper %s (%u failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
