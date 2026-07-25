#ifndef BANK_DATA_H_
#define BANK_DATA_H_

#include "SceneData.h"
#include <stdint.h>

/*
 * Resident Bank identity.
 *
 * Bank is the workspace/container above Scene. The Bank name, restore slot,
 * resident child mask, active child Scene, and VOICE-mode edit mask need one
 * SRAM owner instead of being stored in sceneset.scg or the root Bank browser
 * cache. Inputs are directory-derived eight-cell display names, settings.cfg
 * restore slot values, and bankset.bcg control fields. Outputs seed Save:[Bank],
 * boot restore, Scene switching, and multi-Scene edit fan-out.
 */
#define BANK_DISPLAY_NAME_LEN SCENE_OBJECT_DISPLAY_NAME_LEN
#define BANK_SCENE_SLOT_COUNT 16u
#define BANK_RESTORE_SLOT_COUNT 1000u

void bank_init(void);
void bank_setDisplayName(const char name[BANK_DISPLAY_NAME_LEN]);
const char *bank_displayName(void);
void bank_setRestoreBankSlot(uint16_t slot);
uint16_t bank_restoreBankSlot(void);
void bank_setScenePresentMask(uint16_t mask);
uint16_t bank_scenePresentMask(void);
uint8_t bank_scenePresent(uint8_t scene_index);
void bank_setActiveSceneSlot(uint8_t slot);
uint8_t bank_activeSceneSlot(void);
void bank_selectActiveSceneForEditMask(uint8_t slot);
void bank_setSceneMaskVoiceEdit(uint16_t mask);
uint16_t bank_sceneMaskVoiceEdit(void);
uint8_t bank_sceneInVoiceEditMask(uint8_t scene_index);
void bank_toggleSceneMaskVoiceEdit(uint8_t scene_index);
void bank_setHasResidentBank(uint8_t present);
uint8_t bank_hasResidentBank(void);

#endif /* BANK_DATA_H_ */
