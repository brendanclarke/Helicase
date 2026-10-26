/*
 * Stock-wave generator (S084 VX2 VX2-10.3).
 *
 * What:       compiles the real StockWaveGen.c and StockWaveRecipes.c against
 *             a fake sector-7 image and compares every point with
 *             frozen/stock_ref.bin (the reference produced by
 *             tools/gen_stock_recipes.py).
 * Why:        proves the install-time additive generator matches the tool and
 *             that each level peaks at full scale.
 * Inputs:     frozen/stock_ref.bin, the generated recipes and sine table.
 * Outputs:    failure count; exits nonzero on any failure.
 * Accessors:  make -C tools/dsp_test stock_gen.
 * Affiliates: tools/gen_stock_recipes.py, Core/SampleRom/StockWaveGen.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STOCK_BASE 0x080C0000u
#define STOCK_BYTES 61120u
#define STOCK_WAVE_LEVELS 11u
#define TABLESIZE 4096

#include "stock_levels.c"
#include "stock_sine.c"
#include "StockWaveGen.c"
#include "StockWaveRecipes.c"

static uint8_t fake_flash[STOCK_BYTES];
static uint32_t programmed[STOCK_BYTES / 4];
static unsigned failures;

int sampleFlash_writeWord(uint32_t address, uint32_t data)
{
    const uint32_t offset = address - STOCK_BASE;

    if (offset % 4u != 0u || offset + 4u > STOCK_BYTES)
        return -1;
    if (programmed[offset / 4u] != 0u)
        return -2;      /* double programming, as on the chip */
    programmed[offset / 4u] = 1u;
    fake_flash[offset + 0u] = (uint8_t)(data & 0xffu);
    fake_flash[offset + 1u] = (uint8_t)((data >> 8) & 0xffu);
    fake_flash[offset + 2u] = (uint8_t)((data >> 16) & 0xffu);
    fake_flash[offset + 3u] = (uint8_t)((data >> 24) & 0xffu);
    return 0;
}

static int16_t flashPoint(uint32_t point)
{
    const uint32_t offset = point * 2u;

    return (int16_t)((uint16_t)fake_flash[offset] |
                     ((uint16_t)fake_flash[offset + 1u] << 8));
}

int main(void)
{
    int16_t reference[STOCK_BYTES / 2u];
    FILE *fh;
    uint8_t wave;
    uint8_t level;
    unsigned i;

    memset(fake_flash, 0xff, sizeof(fake_flash));
    if (stockWaveGen_programAll(STOCK_BASE) != 0) {
        printf("stock_gen FAILED: generator status\n");
        return 1;
    }

    fh = fopen("frozen/stock_ref.bin", "rb");
    if (!fh) {
        printf("stock_gen FAILED: frozen/stock_ref.bin missing\n");
        return 1;
    }
    if (fread(reference, sizeof(int16_t), STOCK_BYTES / 2u, fh) !=
        STOCK_BYTES / 2u) {
        printf("stock_gen FAILED: short reference\n");
        fclose(fh);
        return 1;
    }
    fclose(fh);

    for (i = 0u; i < STOCK_BYTES / 2u; i++) {
        const int diff = (int)flashPoint(i) - (int)reference[i];

        if (diff > 1 || diff < -1) {
            if (failures < 8u)
                printf("FAIL: point %u differs by %d\n", i, diff);
            failures++;
        }
    }

    for (wave = 0u; wave < 5u; wave++) {
        for (level = 0u; level < 11u; level++) {
            const uint32_t length = 1u << (32u - stockWave_levelShift[level]);
            const uint32_t offset =
                (uint32_t)wave * 6112u + stockWave_levelOffset[level];
            int peak = 0;
            uint32_t m;

            for (m = 0u; m < length; m++) {
                const int v = flashPoint(offset + m);
                const int a = (v < 0) ? -v : v;

                if (a > peak)
                    peak = a;
            }
            if (peak < 32766 || peak > 32768) {
                printf("FAIL: wave %u level %u peak %d\n", wave, level, peak);
                failures++;
            }
        }
    }

    printf("stock_gen %s (%u failures)\n", failures ? "FAILED" : "OK",
           failures);
    return failures ? 1 : 0;
}
