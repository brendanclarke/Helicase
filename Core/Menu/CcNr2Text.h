/*
 * CcNr2Text.h — mod target declarations for LXR-02.
 * Ported from original LXR AVR CcNr2Text.h / Cc2Text.c.
 * PROGMEM stripped, direct array access.
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

#ifndef CCNR2TEXT_H_
#define CCNR2TEXT_H_

#include <stdint.h>
#include "menu.h"

/* Returns total number of entries in modTargets[] */
uint8_t getNumModTargets(void);

/* Initialise paramToModTarget[] reverse-lookup array */
void paramToModTargetInit(void);

/* Returns 1-based voice number for a modTargets index, 0 if invalid */
uint8_t voiceFromModTargValue(uint8_t val);

/* Gap-map helpers for LFO target voice switching */
uint8_t getModTargetGapIndex(uint8_t modTargetIdx);
uint8_t getModTargetIdxFromGapIdx(uint8_t v, uint8_t gapidx);

#endif /* CCNR2TEXT_H_ */
