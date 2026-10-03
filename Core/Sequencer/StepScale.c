/*
 * Core/Sequencer/StepScale.c
 *
 * Shared 96-PPQ step-scale table for Pattern display/storage and the FX
 * sequencer clock (Session 072 step 8; Effects plan §11.3).
 */

#include "StepScale.h"

/*
 * One table owns both timing and display meaning. Keeping the labels beside
 * the tick values prevents the Pattern UI and FX clock from acquiring
 * independent index orders.
 */
static const uint16_t stepScale_tickTable[STEP_SCALE_COUNT] = {
    6u, 8u, 12u, 16u, 24u, 32u, 36u,
    48u, 64u, 72u, 96u, 192u, 384u, 768u
};

static const char stepScale_shortTable[STEP_SCALE_COUNT][4] = {
    "/64", "32t", "/32", "16t", "/16", "/8t", "16.",
    "/8 ", "/4t", "/8.", "/4 ", "/2 ", "1br", "2br"
};

static const char *const stepScale_longTable[STEP_SCALE_COUNT] = {
    "1/64", "1/32T", "1/32", "1/16T", "1/16", "1/8T", "1/16.",
    "1/8", "1/4T", "1/8.", "1/4", "1/2", "1 bar", "2 bars"
};

uint16_t stepScale_ticks(uint8_t index)
{
    /* Malformed retained values use the new default for playback safety. */
    return (index < STEP_SCALE_COUNT)
        ? stepScale_tickTable[index] : stepScale_tickTable[STEP_SCALE_DEFAULT];
}

const char *stepScale_shortName(uint8_t index)
{
    /* Stale card values display as the default without rewriting storage. */
    return (index < STEP_SCALE_COUNT)
        ? stepScale_shortTable[index] : stepScale_shortTable[STEP_SCALE_DEFAULT];
}

const char *stepScale_longName(uint8_t index)
{
    /* Stale card values display as the default without rewriting storage. */
    return (index < STEP_SCALE_COUNT)
        ? stepScale_longTable[index] : stepScale_longTable[STEP_SCALE_DEFAULT];
}
