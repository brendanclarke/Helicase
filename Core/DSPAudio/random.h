/*
 * random.h — STM32F765 hardware RNG.
 * Ported from original LXR DSPAudio/random.h.
 *
 * Public API matches original verbatim (initRng, GetRngValue) so DSP
 * source files using these compile unchanged.
 */

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef RANDOM_H_
#define RANDOM_H_

#include <stdint.h>

/*
 * The RNG data register's address (S096 P3 assessment F8).
 *
 * What:       RNG_DR of the STM32F765 (RNG base 0x50060800 + 0x08). The one
 *             definition read by GetRngValue() (random.c) and by the
 *             Colloid's inline noise read (ColloidVoice.c col_rngRead(),
 *             which avoids the out-of-line call in its per-sample loop).
 * Why:        the tree has no CMSIS RNG struct; random.c addresses the RNG by
 *             literal, and a second literal copy could drift.
 */
#define RNG_DR_ADDR  0x50060808UL

void     initRng(void);
int16_t GetRngValue(void);

#endif
