/*
 * Core/Sequencer/StepScale.h
 *
 * Shared timing-scale vocabulary for Pattern track settings and the FX
 * sequencer (Session 072 step 8; Effects plan §11.3).
 */
#ifndef STEP_SCALE_H_
#define STEP_SCALE_H_

#include <stdint.h>

/*
 * One shared scale index is used by retained Pattern and Effect records.
 * Values are 96-PPQ ticks, ordered from shortest to longest duration.
 * Index 4 is the new/default 1/16 entry; the retained byte is not rewritten
 * when an older card contains an index that is now outside this table.
 */
#define STEP_SCALE_COUNT   14u
#define STEP_SCALE_DEFAULT 4u

/* Return the 96-PPQ duration represented by one retained scale index. */
uint16_t stepScale_ticks(uint8_t index);

/* Return the three-character compact label for one retained scale index. */
const char *stepScale_shortName(uint8_t index);

/* Return the bounded long label for one retained scale index. */
const char *stepScale_longName(uint8_t index);

#endif /* STEP_SCALE_H_ */
