#ifndef BANK_DATA_H_
#define BANK_DATA_H_

#include "SceneData.h"
#include <stdint.h>

/*
 * Resident Bank identity.
 *
 * Bank is the workspace/container above Scene, but this first Bank phase still
 * has only one resident scene_t. The Bank name therefore needs its own tiny
 * SRAM owner instead of being stored in sceneset.scg, bankset.bcg, or the root
 * Bank browser cache. Inputs are directory-derived eight-cell display names;
 * outputs seed Save:[Bank] and future Bank autosave/settings state.
 */
#define BANK_DISPLAY_NAME_LEN SCENE_OBJECT_DISPLAY_NAME_LEN
#define BANK_SCENE_SLOT_COUNT 16u

void bank_init(void);
void bank_setDisplayName(const char name[BANK_DISPLAY_NAME_LEN]);
const char *bank_displayName(void);
void bank_setActiveSceneSlot(uint8_t slot);
uint8_t bank_activeSceneSlot(void);
void bank_setHasResidentBank(uint8_t present);
uint8_t bank_hasResidentBank(void);

#endif /* BANK_DATA_H_ */
