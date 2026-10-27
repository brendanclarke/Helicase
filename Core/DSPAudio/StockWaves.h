/* StockWaves.h */
/*
 * Stock-wave reader (S085 C4.5).
 *
 * What:       resolves one level of one generated stock wave (wave 0..4 =
 *             sq3 sw3 sqa tra swa) into a table pointer, an index shift
 *             (index = phase >> shift) and a wrap mask; while the stock block
 *             is not valid (no install yet) it returns the 1,024-point
 *             fallback level: sq3/sqa -> recTable, sw3/swa -> sawTable,
 *             tra -> triTable.
 * Why:        plan §4.3.4: one block-rate selection keeps the render loops
 *             free of validity tests and keeps the fallback in one place.
 * Inputs:     wave, level 0..10.  Outputs: stock_level_t.
 * Accessors:  Oscillator.c calcStockWaveBlock()/calcStockWaveFmBlock(),
 *             OscillatorPwm.c (pq3/pw3).
 * Affiliates: StockWaveGen.c (same layout), SampleMemory.h
 *             (SAMPLE_STOCK_START_ADDRESS, sampleMemory_stockValid()).
 */
#ifndef STOCKWAVES_H_
#define STOCKWAVES_H_
#include <stdint.h>

#define STOCK_WAVE_COUNT            5u
#define STOCK_WAVE_LEVELS           11u
#define STOCK_WAVE_POINTS_PER_WAVE  6112u

typedef struct {
    const int16_t *tab;
    uint8_t        shift;   /* index = phase >> shift (22 for 1,024 points) */
    uint16_t       mask;    /* level length - 1 */
} stock_level_t;

/* Level offsets inside one wave (points) and phase shifts (32 - log2 L). */
extern const uint16_t stockWave_levelOffset[STOCK_WAVE_LEVELS];
extern const uint8_t  stockWave_levelShift[STOCK_WAVE_LEVELS];

stock_level_t stockWave_level(uint8_t wave, uint8_t level);

#endif /* STOCKWAVES_H_ */
