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

void     initRng(void);
int16_t GetRngValue(void);

#endif
