/*
 * Cc2Text.c — mod target definitions for LXR-02.
 * Ported from original LXR AVR Cc2Text.c by Julian Schmidt / AS.
 * PROGMEM stripped. Direct array access.
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

#include "CcNr2Text.h"
#include "menu.h"
#include <stdint.h>

const ModTarg modTargets[] = {
    {TEXT_EMPTY, PAR_NONE},
};

const ModTargetVoiceOffset modTargetVoiceOffsets[6] = {
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
    {0, 0},
};

uint8_t paramToModTarget[END_OF_SOUND_PARAMETERS] = {0};

uint8_t getNumModTargets(void)
{
    return (uint8_t)(sizeof(modTargets)/sizeof(ModTarg));
}

uint8_t voiceFromModTargValue(uint8_t val)
{
    if (modTargetVoiceOffsets[3].start <= val) {
        if (modTargetVoiceOffsets[4].start <= val) {
            if (modTargetVoiceOffsets[5].start <= val) {
                if (modTargetVoiceOffsets[5].end >= val) return 6;
                else return 0;
            } else return 5;
        } else return 4;
    } else {
        if (modTargetVoiceOffsets[0].start <= val) {
            if (modTargetVoiceOffsets[1].start <= val) {
                if (modTargetVoiceOffsets[2].start <= val) return 3;
                else return 2;
            } else return 1;
        } else return 0;
    }
}

void paramToModTargetInit(void)
{
    uint8_t i;
    uint8_t n = getNumModTargets();
    for (i = 0; i < n; i++) {
        uint16_t p = modTargets[i].param;
        if (p < END_OF_SOUND_PARAMETERS)
            paramToModTarget[p] = i;
    }
}

/* Stub implementations — gap-map logic not needed without LFO targeting UI */
uint8_t getModTargetGapIndex(uint8_t modTargetIdx)
{
    (void)modTargetIdx;
    return 0xFF;
}

uint8_t getModTargetIdxFromGapIdx(uint8_t v, uint8_t gapidx)
{
    (void)v; (void)gapidx;
    return 0;
}
