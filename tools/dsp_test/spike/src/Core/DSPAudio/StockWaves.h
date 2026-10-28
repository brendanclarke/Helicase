/*
 * Stock-wave level access (S084 VX2 VX2-4.4).
 *
 * What:       maps (wave 0..4, level 0..10) to the level's flash pointer and
 *             length shift (32 - log2 L) inside the stock block, or to the
 *             1,024-point fallback table (sq3/sqa -> recTable, sw3/swa ->
 *             sawTable, tra -> triTable, shift 22) while the stock block is
 *             not valid (before the first install).
 * Why:        plan §4.1.2: until the first install the new waves fall back
 *             to Rec/Saw/Tri; one selection point keeps the readers simple.
 * Inputs:     wave, level; sampleMemory_stockValid().
 * Outputs:    stock_level_t {const int16_t *tab; uint8_t shift; uint16_t
 *             mask}.
 * Accessors:  Oscillator.c calcStockWaveBlock(_Fm), OscillatorPwm.c (pq3,
 *             pw3), transientGenerator.c (bursts).
 * Affiliates: StockWaveGen.c (the same offsets), SampleMemory.h.
 */
#ifndef STOCKWAVES_H_
#define STOCKWAVES_H_

#include <stdint.h>

#define STOCK_WAVE_COUNT            5u
#define STOCK_WAVE_LEVELS           11u
#define STOCK_WAVE_POINTS_PER_WAVE  6112u

typedef struct {
    const int16_t *tab;
    uint8_t        shift;   /* index = phase >> shift */
    uint16_t       mask;    /* L - 1 */
} stock_level_t;

extern const uint16_t stockWave_levelOffset[STOCK_WAVE_LEVELS];
    /* 0, 1024, 2048, 3072, 4096, 5120, 5632, 5888, 6016, 6080, 6096 */
extern const uint8_t  stockWave_levelShift[STOCK_WAVE_LEVELS];
    /* 22, 22, 22, 22, 22, 23, 24, 25, 26, 28, 28 */

/*
 * Resolve one stock-wave level (S084 VX2 VX2-4.4).
 *
 * What:       selects the generated level when the stock block is valid, else
 *             the 1,024-point fallback table of the same wave.
 * Why:        one block-rate selection keeps the render loops free of
 *             validity tests and the fallback rules in one place.
 * Inputs:     wave 0..4, level 0..10.  Outputs: the level descriptor.
 * Accessors:  calcStockWaveBlock(), calcStockWaveFmBlock(), oscPwm_prepare()
 *             via pwm_familyLevel(), transient_calcBlock().
 * Affiliates: SAMPLE_STOCK_START_ADDRESS, stockWave_levelOffset[].
 */
stock_level_t stockWave_level(uint8_t wave, uint8_t level);

#endif /* STOCKWAVES_H_ */
