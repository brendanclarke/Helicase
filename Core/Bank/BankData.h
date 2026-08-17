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
 *
 * Serialized-owner rule: after boot Autosave tracking is enabled, each setter
 * below compares its normalized final value with retained storage, stores
 * first, and notifies the format-owned Bank-field marker only on change. New
 * serialized Bank fields must follow the same setter/getter/marker pattern;
 * initialization and the has-resident lifecycle flag are intentionally not
 * payload mutations. Affiliates: Autosave's Bank field identifiers and live
 * getter, Menu Scene selection, and filesystem Bank metadata completion.
 */
#define BANK_DISPLAY_NAME_LEN SCENE_OBJECT_DISPLAY_NAME_LEN
#define BANK_SCENE_SLOT_COUNT 16u
#define BANK_RESTORE_SLOT_COUNT 1000u

/*
 * Initialize ownership without producing autosave mutations, then expose
 * change-aware serialized Bank setters and read accessors.
 *
 * Inputs/outputs: setters accept existing normalized domains (name, 0..999
 * slot, 16-bit Scene masks, 0..15 active Scene) and update BankData; ordinary
 * post-boot final-value changes mark their corresponding autosave range.
 * `bank_setHasResidentBank()` controls whether a Bank session exists and never
 * maps to a payload bit. Why: all Bank mutation paths must converge here while
 * boot setup remains quiet. Affiliates: BankData.c invariant normalization and
 * Core/Bank/Scene/Autosave.h.
 */
void bank_init(void);
void bank_setDisplayName(const char name[BANK_DISPLAY_NAME_LEN]);
const char *bank_displayName(void);
void bank_setRestoreBankSlot(uint16_t slot);
uint16_t bank_restoreBankSlot(void);
/* Return nonzero when the normalized resident mask changed and was marked. */
uint8_t bank_setScenePresentMask(uint16_t mask);
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
