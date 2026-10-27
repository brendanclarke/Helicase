/* StockWaves.c */
#include "StockWaves.h"
#include "SampleMemory.h"
#include "wavetable.h"

/*
 * Stock-wave level table (S085 C4.5).
 *
 * What:       offsets inside one 6,112-point wave and the phase shift for
 *             each of the 11 octave levels.
 * Why:        the install-time layout and the renderer share one geometry.
 * Inputs:     level 0..10.  Outputs: table pointer, shift, mask.
 * Accessors:  Oscillator.c stock renderers, OscillatorPwm.c.
 * Affiliates: StockWaveGen.c.
 */

const uint16_t stockWave_levelOffset[STOCK_WAVE_LEVELS] = {
    0u, 1024u, 2048u, 3072u, 4096u, 5120u, 5632u, 5888u, 6016u, 6080u, 6096u,
};
const uint8_t stockWave_levelShift[STOCK_WAVE_LEVELS] = {
    22u, 22u, 22u, 22u, 22u, 23u, 24u, 25u, 26u, 28u, 28u,
};

/*
 * Resolve one stock level (S085 C4.5).
 *
 * What:       the generated level inside sample flash while the catalogue
 *             says the stock block is valid; otherwise the 1,024-point
 *             Rec/Saw/Tri fallback.
 * Why:        one block-rate selection keeps the render loops free of
 *             validity tests.
 * Inputs:     wave 0..4, level 0..10.  Outputs: stock_level_t.
 * Accessors:  Oscillator.c, OscillatorPwm.c.
 * Affiliates: SampleMemory_stockValid(), StockWaveGen.c.
 */
stock_level_t stockWave_level(uint8_t wave, uint8_t level)
{
    stock_level_t out;

    if (wave >= STOCK_WAVE_COUNT)
        wave = 0u;
    if (level >= STOCK_WAVE_LEVELS)
        level = (uint8_t)(STOCK_WAVE_LEVELS - 1u);
    if (sampleMemory_stockValid()) {
        out.tab = (const int16_t *)SAMPLE_FLASH_PTR(
            SAMPLE_STOCK_START_ADDRESS +
            ((uint32_t)wave * STOCK_WAVE_POINTS_PER_WAVE +
             stockWave_levelOffset[level]) * 2u);
        out.shift = stockWave_levelShift[level];
        out.mask = (uint16_t)((1u << (32u - out.shift)) - 1u);
        return out;
    }
    out.tab = (wave == 1u || wave == 4u) ? sawTable[level]
            : (wave == 3u)               ? triTable[level]
                                         : recTable[level];
    out.shift = 22u;
    out.mask = 1023u;
    return out;
}
