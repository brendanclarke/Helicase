/* StockWaveGen.c */
#include "StockWaveGen.h"
#include "StockWaves.h"
#include "sampleFlash.h"
#include "wavetable.h"

/*
 * StockWaveGen.c (S085 C4.4).
 *
 * What:       the install-time additive generator for sq3 sw3 sqa tra swa.
 * Why:        61,120 B of tables from about 10 KB of recipes; the host
 *             generator and this code accumulate in the same float32 order.
 * Accessors:  sampleInstall_writeStockBlock().
 * Affiliates: StockWaveRecipes.c, wavetable.c sine_table.
 */

/* Level geometry (S085 C4.4): lengths and harmonic counts; offsets are
** stockWave_levelOffset[] in StockWaves.c. */
static const uint16_t stock_gen_length[STOCK_WAVE_LEVELS] = {
    1024u, 1024u, 1024u, 1024u, 1024u, 512u, 256u, 128u, 64u, 16u, 16u,
};
static const uint16_t stock_gen_harmonics[STOCK_WAVE_LEVELS] = {
    511u, 511u, 337u, 168u, 84u, 42u, 21u, 10u, 5u, 2u, 1u,
};

/*
 * One generated point (S085 C4.4).
 *
 * What:       the additive sum of C4.4's contract for (wave, level, m), in
 *             float32 and in k order (the script emulates exactly this).
 * Outputs:    the rounded, saturated int16 value.
 * Accessors:  stockWaveGen_programAll().
 */
static int32_t stockWaveGen_point(uint8_t wave, uint8_t level, uint32_t m)
{
    const uint32_t length = stock_gen_length[level];
    const uint16_t harmonics = stock_gen_harmonics[level];
    const int16_t *a = stockWaveRecipeA[wave];
    const int16_t *b = stockWaveRecipeB[wave];
    float sum = 0.0f;
    uint16_t k;

    for (k = 1u; k <= harmonics; k++) {
        const uint32_t t = (((uint32_t)k * m * 4096u) / length) & 4095u;
        const float c = -(float)sine_table[t];
        const float s = (float)sine_table[(t + 1024u) & 4095u];

        sum += (float)a[k - 1u] * c + (float)b[k - 1u] * s;
    }
    sum *= stockWaveLevelScale[wave][level];
    if (sum >= 32767.0f)
        return 32767;
    if (sum <= -32768.0f)
        return -32768;
    return (int32_t)(sum + ((sum >= 0.0f) ? 0.5f : -0.5f));
}

/*
 * Program the whole stock block (S085 C4.4).
 *
 * What:       for every wave and level, point m = sum over k of
 *             A[k]*cos(t) + B[k]*sin(t), t = (k*m*4096/L) & 4095, read from
 *             sine_table (cos = -sine_table[t], sin = sine_table[t+1024]),
 *             times the level scale, rounded and saturated to int16; two
 *             points per programmed word at
 *             base + (wave*6112 + levelOffset + m)*2.
 * Why:        plan §4.3.4; the device output equals the script's reference
 *             within 1 LSB (float32 in the same order).
 * Inputs:     base (the erased stock block address).
 * Outputs:    61,120 B programmed; 0 or a sampleFlash status.
 * Accessors:  sampleInstall_writeStockBlock().
 * Affiliates: StockWaveRecipes.c, StockWaves.c (the same layout),
 *             sampleFlash_writeWord(), wavetable.c sine_table.
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
                const uint32_t word = (uint32_t)(uint16_t)(int16_t)lo |
                    ((uint32_t)(uint16_t)(int16_t)hi << 16);
                const int rc = sampleFlash_writeWord(origin + m * 2u, word);

                if (rc != 0)
                    return rc;
            }
        }
    }
    return 0;
}
