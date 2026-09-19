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

/*
 * Session-scoped card-verified clean-Scene authority (Option 2).
 *
 * What: four volatile bytes retained for the current boot and current mounted
 * card only. `bank_scene_sd_clean_mask` holds one bit per resident Scene that
 * has been proven equal to that Scene's child folder inside one identified
 * root Bank; `bank_scene_sd_clean_slot` is that root Bank slot, with
 * `BANK_SD_CLEAN_SLOT_NONE` (0xffff) meaning no card authority exists. This
 * state is never serialized to settings, HCNAMES, a Bank, or Autosave.
 *
 * Why: firmware cannot know whether a removable card was edited while power
 * was off, so a Scene is clean only when I/O on the current mounted card
 * established the equality during this powered session. Skipping writes for
 * these bits is therefore a correctness-preserving speedup, not a cached
 * assumption that survives a remount.
 *
 * Inputs/outputs: bank_init() and every fresh mount clear all authority;
 * mutation funnels invalidate one affected Scene bit; Bank Load/Save publish
 * only the completed effective child mask after the final sync/index chain.
 * Affiliates: filesystem Bank Load/Save request and completion paths,
 * SceneData/PatternData/Preset mutation boundaries, and
 * filesystem_initAfterCardReady().
 */
#define BANK_SD_CLEAN_SLOT_NONE 0xffffu
void bank_clearSdCleanAuthority(void);
void bank_invalidateSdCleanScene(uint8_t scene_index);
void bank_publishSdCleanAuthority(uint16_t slot, uint16_t proven_mask);
uint16_t bank_sdCleanMask(void);
uint16_t bank_sdCleanSlot(void);
uint8_t bank_sdCleanSlotIsValid(void);
/*
 * Option 2 operation-scoped mutation window helpers.
 *
 * The filesystem Bank Save owns a mutation window from request acceptance to
 * final index completion. Any resident-data mutation in that window sets a
 * per-Scene bit through bank_invalidateSdCleanScene(); filesystem resets the
 * window at request, clears one Scene's bit immediately before that child's
 * writer starts, and reads the surviving mask at completion so a Scene edited
 * while its candidate write was in flight stays non-clean.
 */
void bank_resetSdSaveMutationWindow(void);
void bank_resetSdSaveMutationScene(uint8_t scene_index);
uint16_t bank_sdSaveMutatedMask(void);

#endif /* BANK_DATA_H_ */
