/*
 * StockWaves.c — stock-wave level selection (S084 VX2 VX2-4.4).
 *
 * The generated block lives at the floor of sample flash; each wave holds 11
 * levels at the offsets below. Before the first Load:[Samples] the block is
 * absent (or holds an older recipe), so the readers use the 1,024-point
 * Rec/Saw/Tri tables as the documented fallback.
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

#include "StockWaves.h"
#include "SampleMemory.h"
#include "Oscillator.h"
#include "wavetable.h"

/* Level offsets inside one wave's 6,112-point block (S084 VX2 VX2-4.4). */
const uint16_t stockWave_levelOffset[STOCK_WAVE_LEVELS] = {
    0u, 1024u, 2048u, 3072u, 4096u, 5120u,
    5632u, 5888u, 6016u, 6080u, 6096u,
};

/* 32 - log2(level length), the phase-to-index shift. */
const uint8_t stockWave_levelShift[STOCK_WAVE_LEVELS] = {
    22u, 22u, 22u, 22u, 22u, 23u, 24u, 25u, 26u, 28u, 28u,
};

/*
 * Resolve one stock-wave level (S084 VX2 VX2-4.4).
 *
 * What:       generated level when sampleMemory_stockValid(), otherwise the
 *             fallback table for the same wave at 1,024 points.
 * Why:        plan §4.1.2: until the first install sq3/sqa read Rec, sw3/swa
 *             read Saw and tra reads Tri.
 * Inputs:     wave 0..4, level 0..10.  Outputs: the level descriptor.
 * Accessors:  calcStockWaveBlock(), calcStockWaveFmBlock(), pwm_familyLevel(),
 *             transient_calcBlock().
 * Affiliates: sampleMemory_stockValid(), stockWave_levelOffset[].
 */
stock_level_t stockWave_level(uint8_t wave, uint8_t level)
{
    stock_level_t out;

    if (wave >= STOCK_WAVE_COUNT)
        wave = 0u;
    if (level >= STOCK_WAVE_LEVELS)
        level = (uint8_t)(STOCK_WAVE_LEVELS - 1u);

    if (sampleMemory_stockValid()) {
        out.tab = (const int16_t *)
            SAMPLE_FLASH_PTR(SAMPLE_STOCK_START_ADDRESS +
                             ((uint32_t)wave * STOCK_WAVE_POINTS_PER_WAVE +
                              stockWave_levelOffset[level]) * 2u);
        out.shift = stockWave_levelShift[level];
        out.mask = (uint16_t)((1u << (32u - out.shift)) - 1u);
        return out;
    }

    switch (wave) {
    case 1u: /* sw3 */
    case 4u: /* swa */
        out.tab = sawTable[level];
        break;
    case 3u: /* tra */
        out.tab = triTable[level];
        break;
    case 0u: /* sq3 */
    case 2u: /* sqa */
    default:
        out.tab = recTable[level];
        break;
    }
    out.shift = 22u;
    out.mask = 1023u;
    return out;
}
