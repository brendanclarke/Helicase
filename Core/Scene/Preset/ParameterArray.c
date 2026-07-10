/*
 * ParameterArray.c
 *
 * The old file defined every sound parameter pointer here under Preset. That
 * made Preset the owner of what a drum/snare/cymbal/hat file key meant.
 *
 * This file is now deliberately boring: it preserves the legacy flat array for
 * non-instrument callers that still compile against PAR_* ids, but instrument
 * slot storage and runtime meaning have moved to Core/DSP/Instruments.
 */

#include "ParameterArray.h"
#include <string.h>

Parameter parameterArray[END_OF_SOUND_PARAMETERS];

void paramArray_setParameter(uint16_t idx, ptrValue newValue)
{
    if (idx >= END_OF_SOUND_PARAMETERS || parameterArray[idx].ptr == 0)
        return;

    switch (parameterArray[idx].type) {
    case TYPE_UINT8:
        *((uint8_t *)parameterArray[idx].ptr) = (uint8_t)newValue.itg;
        break;
    case TYPE_UINT32:
        *((uint32_t *)parameterArray[idx].ptr) = newValue.itg;
        break;
    case TYPE_FLT:
    case TYPE_SPECIAL_F:
    case TYPE_SPECIAL_P:
    case TYPE_SPECIAL_FILTER_F:
        *((float *)parameterArray[idx].ptr) = newValue.flt;
        break;
    default:
        break;
    }
}

void parameterArray_init(void)
{
    memset(parameterArray, 0, sizeof(parameterArray));
}
