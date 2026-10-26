/*
 * InsertTypeCommon.h — private helpers shared by the S084 insert types.
 *
 * What: keeps the fixed-point conversion, wet blend and D+S sample-and-hold
 * operation identical across type files. Why: each public type remains a
 * small self-contained registry row while the common arithmetic is audited
 * once. Inputs/outputs: int16 samples and bounded float controls.
 */
#ifndef INSERT_TYPE_COMMON_H_
#define INSERT_TYPE_COMMON_H_

#if !defined(INSERT_HOST_TEST)
#include "stm32f4xx.h"
#endif
#include "config.h"
#include <stdint.h>

static inline int16_t insert_to16(float x)
{
    return (int16_t)__SSAT((int32_t)x, 16);
}

static inline int16_t insert_wet16(int16_t dry, int16_t wet, float amount)
{
    return insert_to16((float)dry + amount * ((float)wet - (float)dry));
}

static inline int16_t insert_decimate(float *counter, int16_t *held,
                                      int16_t sample, float rate)
{
    *counter += rate;
    if (*counter >= 1.0f) {
        *counter -= 1.0f;
        *held = sample;
    }
    return *held;
}

#endif /* INSERT_TYPE_COMMON_H_ */
