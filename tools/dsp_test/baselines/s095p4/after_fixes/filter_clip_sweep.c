/* S095 P4 F4 record: needs build/gen-style rf.c = python3 tools/dsp_test/extract.py --src Core/DSPAudio/ResonantFilter.c --func svf_padeNum --func svf_padeDen --func fastTanh --func fastTan --func tanhXdX --func softClipTwo --func SVF_setReso --func SVF_directSetFilterValue --func SVF_init --func SVF_reset --func SVF_recalcFreq --func SVF_setDrive --func SVF_calcBlockZDF --out rf.c; cc -O2 -std=gnu11 -include tools/dsp_test/prelude.h. NOTE: rf.c must be extracted from the pre-F4 file (no per-type offset) for the floor sweep to mean what it says. */
#include "rf.c"
#include <stdio.h>
static const char *tn[] = {"", "LP ", "HP ", "BP ", "UBP", "Nch", "Pek", "LP2"};
static double run(int t, float drive, double *worst)
{
    const uint8_t resos[] = {0, 64, 96, 110, 120, 127};
    const float cuts[] = {0.02f, 0.05f, 0.1f, 0.2f, 0.3f, 0.4f};
    unsigned long clip = 0, tot = 0; *worst = 0;
    for (int ri = 0; ri < 6; ri++) for (int ci = 0; ci < 6; ci++) for (int sig = 0; sig < 3; sig++) {
        ResonantFilter f; memset(&f, 0, sizeof f); SVF_init(&f); SVF_reset(&f);
        SVF_setReso(&f, resos[ri] / 127.f); SVF_directSetFilterValue(&f, cuts[ci]); f.drive = drive;
        unsigned long c = 0, n = 0; double ph = 0; uint32_t s = 12345u;
        for (int b = 0; b < 400; b++) {
            int16_t buf[32];
            for (int i = 0; i < 32; i++) {
                double v;
                if (sig == 0) { ph += 110.0 / 44108.0; if (ph >= 1) ph -= 1; v = 2 * ph - 1; }
                else if (sig == 1) { double tt = (b * 32 + i) / 44108.0; v = sin(2 * M_PI * 55 * tt) * exp(-tt / 0.12); }
                else { s = s * 1664525u + 1013904223u; v = ((int32_t)s) / 2147483648.0; }
                buf[i] = (int16_t)(32767 * v);
            }
            SVF_calcBlockZDF(&f, (uint8_t)t, buf, 32);
            for (int i = 0; i < 32; i++) { n++; if (buf[i] >= 32767 || buf[i] <= -32768) c++; }
        }
        clip += c; tot += n; if ((double)c / n > *worst) *worst = (double)c / n;
    }
    return (double)clip / tot;
}
int main(void)
{
    for (int t = 1; t <= 7; t++) {
        printf("%s", tn[t]);
        for (int k = 0; k <= 12; k++) {
            double w; float d = 0.4f + 0.05f * k;
            double m = run(t, d, &w);
            printf(" | %.2f: %.3f%%/%.2f%%", d, 100 * m, 100 * w);
        }
        printf("\n");
    }
    return 0;
}
