/*
 * Stock-wave generation check (S085 C17.3).
 *
 * What:       runs the device's stockWaveGen_point() for every wave and level
 *             point and compares with the generator's reference binary.
 * Why:        G2 gate: the install-time tables must match the recipes within
 *             the approved +/-1 LSB.
 * Inputs:     build/gen/stock_gen.c and frozen/stock_ref.bin.
 * Outputs:    mismatch count and the worst absolute difference.
 * Accessors:  make -C tools/dsp_test stock_gen.
 * Affiliates: tools/gen_stock_recipes.py, StockWaveGen.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int32_t host_stockWaveGen_point(uint8_t wave, uint8_t level, uint32_t m);

static const uint16_t level_len[11] = {
    1024u, 1024u, 1024u, 1024u, 1024u, 512u, 256u, 128u, 64u, 16u, 16u,
};
static const uint16_t level_off[11] = {
    0u, 1024u, 2048u, 3072u, 4096u, 5120u, 5632u, 5888u, 6016u, 6080u, 6096u,
};

int main(void)
{
    FILE *f = fopen("frozen/stock_ref.bin", "rb");
    int16_t *ref;
    int failures = 0;
    int worst = 0;
    if (!f) {
        printf("FAIL cannot open frozen/stock_ref.bin\n");
        return 1;
    }
    ref = malloc(5u * 6112u * sizeof(int16_t));
    if (!ref || fread(ref, sizeof(int16_t), 5u * 6112u, f) != 5u * 6112u) {
        printf("FAIL cannot read the reference\n");
        fclose(f);
        free(ref);
        return 1;
    }
    fclose(f);
    for (uint8_t w = 0u; w < 5u; w++) {
        for (uint8_t lv = 0u; lv < 11u; lv++) {
            for (uint16_t m = 0u; m < level_len[lv]; m++) {
                const int32_t got = host_stockWaveGen_point(w, lv, m);
                const int32_t want = ref[w * 6112u + level_off[lv] + m];
                const int32_t d = got > want ? got - want : want - got;

                if (d > 1) {
                    if (failures < 8)
                        printf("FAIL wave %u level %u point %u: %d != %d\n",
                               w, lv, m, (int)got, (int)want);
                    failures++;
                }
                if (d > worst)
                    worst = (int)d;
            }
        }
    }
    free(ref);
    printf("worst %d LSB\n%d failures\n", worst, failures);
    return failures ? 1 : 0;
}
