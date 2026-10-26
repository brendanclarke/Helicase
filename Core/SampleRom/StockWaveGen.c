/*
 * StockWaveGen.c — additive stock-wave generator (S084 VX2 VX2-4.3).
 *
 * Runs only inside the modal Load:[Samples] install, with audio suspended.
 * Each 32-bit word is programmed as soon as its two points exist, so the
 * generator needs no scratch buffer.
 *
 *  Modified on: 25.10.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#include "StockWaveGen.h"
#include "StockWaves.h"
#include "sampleFlash.h"
#include "wavetable.h"

/* Level geometry, shared with StockWaves.c (S084 VX2 VX2-4.3). */
static const uint16_t stock_gen_length[STOCK_WAVE_LEVELS] = {
    1024u, 1024u, 1024u, 1024u, 1024u, 512u,
    256u, 128u, 64u, 16u, 16u,
};
static const uint16_t stock_gen_harmonics[STOCK_WAVE_LEVELS] = {
    511u, 511u, 337u, 168u, 84u, 42u, 21u, 10u, 5u, 2u, 1u,
};

static int32_t stockWaveGen_point(uint8_t wave, uint8_t level, uint32_t m)
{
    const uint32_t length = stock_gen_length[level];
    const uint16_t harmonics = stock_gen_harmonics[level];
    const int16_t *a = stockWaveRecipeA[wave];
    const int16_t *b = stockWaveRecipeB[wave];
    float sum = 0.0f;
    uint16_t k;

    for (k = 1u; k <= harmonics; k++) {
        const uint32_t t =
            ((uint32_t)k * m * 4096u) / length & 4095u;
        /* The firmware sine_table stores -cos(2*pi*i/4096). */
        const float c = -(float)sine_table[t];
        const float s = (float)sine_table[(t + 1024u) & 4095u];

        sum += (float)a[k - 1u] * c + (float)b[k - 1u] * s;
    }
    sum *= stockWaveLevelScale[wave][level];
    if (sum >= 32767.0f)
        return 32767;
    if (sum <= -32768.0f)
        return -32768;
    return (int32_t)(sum + (sum >= 0.0f ? 0.5f : -0.5f));
}

/*
 * Program the whole stock block (S084 VX2 VX2-4.3).
 *
 * What:       all five waves and eleven levels, two points per programmed
 *             word, at base + (wave * 6,112 + level offset + m) * 2.
 * Why:        W4; about 8.4 M multiply-adds and 15,280 word programs,
 *             roughly 0.4 s at 216 MHz, all modal.
 * Inputs:     base (the erased stock block).  Outputs: 0 or the flash status.
 * Accessors:  sampleInstall_writeStockBlock().
 * Affiliates: stockWave_gen_point(), stockWave_levelOffset[].
 */
int stockWaveGen_programAll(uint32_t base)
{
    uint8_t wave;
    uint8_t level;

    for (wave = 0u; wave < STOCK_WAVE_COUNT; wave++) {
        for (level = 0u; level < STOCK_WAVE_LEVELS; level++) {
            const uint32_t length = stock_gen_length[level];
            const uint32_t origin = base +
                ((uint32_t)wave * STOCK_WAVE_POINTS_PER_WAVE +
                 stockWave_levelOffset[level]) * 2u;
            uint32_t m;

            for (m = 0u; m < length; m += 2u) {
                const int32_t lo = stockWaveGen_point(wave, level, m);
                const int32_t hi = stockWaveGen_point(wave, level, m + 1u);
                const uint32_t word =
                    ((uint32_t)(uint16_t)(int16_t)lo) |
                    ((uint32_t)(uint16_t)(int16_t)hi << 16);
                int rc = sampleFlash_writeWord(
                    origin + (m * 2u), word);

                if (rc != 0)
                    return rc;
            }
        }
    }
    return 0;
}
