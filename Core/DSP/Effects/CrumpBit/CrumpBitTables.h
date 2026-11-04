/*
 * Core/DSP/Effects/CrumpBit/CrumpBitTables.h
 *
 *  Created on: 04.11.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#ifndef CRUMP_BIT_TABLES_H_
#define CRUMP_BIT_TABLES_H_

/*
 * CrumpBit control tables (S091 P2; generated, CrumpBitTables.c).
 *
 * What: three 128-entry float tables indexed by a 0..127 row value.
 * - crumpBit_rateTable[rte]: delay length in samples, 882 (20 ms) at 0 to
 *   126,686 (2.872 s, the whole arena less the 2-sample guard) at 127,
 *   exponential and strictly increasing. Higher `rte` = longer delay.
 * - crumpBit_toneK[ton]: the one-pole coefficient of the feedback-return
 *   tilt. 0..62 low-pass (150 Hz .. 18 kHz), 63 exactly 1.0 (flat),
 *   64..127 high-pass (20 Hz .. 2 kHz).
 * - crumpBit_filterG[frq]: the TPT pre-warped cutoff g of the output filter,
 *   the voice cutoff law (fastTan(pi * 0.45 * valueShaperF2F(frq/127,
 *   FILTER_SHAPER))). 0 at frq 0 (closed), strictly increasing.
 * Why tables: CrumpBit's per-block work runs inside the render
 * (mixer_calcNextSampleBlock()); a lookup replaces an expf, a fastTan
 * division and a valueShaper division per block, and the Rate table makes the
 * share clamp and the DSP use bit-identical lengths.
 * Regenerate with `python3 tools/plots/gen_crumpbit_tables.py` from the
 * repository root; `make -C tools/dsp_test crumpbit_tables` checks that the
 * committed file matches the generator byte for byte.
 * Accessors: CrumpBitEffect.c only.
 * Affiliates: tools/plots/gen_crumpbit_tables.py.
 */
#define CRUMPBIT_TABLE_SIZE 128u

extern const float crumpBit_rateTable[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_toneK[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_filterG[CRUMPBIT_TABLE_SIZE];

#endif /* CRUMP_BIT_TABLES_H_ */
