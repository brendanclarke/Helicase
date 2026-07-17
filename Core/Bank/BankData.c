#include "BankData.h"
#include <string.h>

static char bank_display_name[BANK_DISPLAY_NAME_LEN + 1u];
static uint16_t bank_restore_bank_slot;
static uint16_t bank_scene_mask_present;
static uint16_t bank_scene_mask_voice_edit;
static uint8_t bank_active_scene_slot;
static uint8_t bank_has_resident_bank;

static uint16_t bank_sceneBit(uint8_t scene_index)
{
    /*
     * Convert a resident Scene index to its 16-bit mask bit.
     *
     * Input: zero-based resident Scene index from Menu, filesystem, or
     * bankset.bcg. Output: bit N for valid 0..15 indices, or zero when the
     * index cannot be represented on the physical SEQ-button mask. The cast
     * keeps the shift in the unsigned 16-bit domain used by all Bank Scene
     * membership fields.
     */
    if (scene_index >= BANK_SCENE_SLOT_COUNT)
        return 0u;
    return (uint16_t)(1u << scene_index);
}

static uint16_t bank_normalizeSceneMask(uint16_t mask)
{
    /*
     * Keep Scene masks inside the resident 16-Scene workspace.
     *
     * Inputs: parser/UI/filesystem masks that may contain stale high bits.
     * Output: only bits addressable by Bank-local Scene slots 00..15. The loop
     * avoids constructing `(1u << 16)` in case the compiler treats plain `int`
     * narrowly on a future target; each bit is produced through bank_sceneBit().
     */
    uint16_t valid = 0u;
    uint8_t scene_index;

    for (scene_index = 0u; scene_index < BANK_SCENE_SLOT_COUNT; scene_index++)
        valid = (uint16_t)(valid | bank_sceneBit(scene_index));
    return (uint16_t)(mask & valid);
}

static void bank_ensureActiveInVoiceEditMask(void)
{
    uint16_t active_bit = bank_sceneBit(bank_active_scene_slot);

    /*
     * Enforce the core Scene-edit invariant.
     *
     * The active Scene must always be part of scene_mask_voice_edit. Runtime
     * display, automation recording, and DSP apply all treat the active Scene
     * as the canonical audible member of a fan-out edit. If a Bank file or a
     * Scene switch leaves the active bit absent, the old mask is intentionally
     * dropped and replaced by the active Scene only.
     */
    if (active_bit == 0u)
        active_bit = 1u;
    bank_scene_mask_voice_edit =
        bank_normalizeSceneMask(bank_scene_mask_voice_edit);
    if ((bank_scene_mask_voice_edit & active_bit) == 0u)
        bank_scene_mask_voice_edit = active_bit;
}

static void bank_copyDisplayName(char dst[BANK_DISPLAY_NAME_LEN + 1u],
                                 const char src[BANK_DISPLAY_NAME_LEN])
{
    uint8_t i;

    /*
     * Copy exactly the LCD-visible Bank name cells.
     *
     * Inputs: directory-derived display bytes or an editor buffer. Output:
     * printable ASCII padded with spaces plus a NUL terminator. The fixed
     * eight-iteration loop prevents a short C string such as "Slak" from
     * leaving stale characters in cells 5..8, and keeps Bank identity aligned
     * with Scene/Kit display-name storage.
     */
    for (i = 0u; i < BANK_DISPLAY_NAME_LEN; i++) {
        char c = src ? src[i] : ' ';
        if (c < 0x20 || c > 0x7e)
            c = ' ';
        dst[i] = c;
    }
    dst[BANK_DISPLAY_NAME_LEN] = '\0';
}

void bank_init(void)
{
    /*
     * Initialize resident Bank metadata to the product default non-Bank state.
     *
     * SceneData already initializes the actual sound defaults. BankData only
     * owns container identity, so a card with no valid Bank can fall back to
     * root Scene, root Kit, or defaults without pretending a Bank is resident.
     * The visible identity is still `none`: Save:[Bank] must never seed from
     * `Empty`, which is reserved for absent library slots.
     */
    memcpy(bank_display_name, "none    ", BANK_DISPLAY_NAME_LEN);
    bank_display_name[BANK_DISPLAY_NAME_LEN] = '\0';
    bank_restore_bank_slot = 0u;
    bank_scene_mask_present = 1u;
    bank_scene_mask_voice_edit = 1u;
    bank_active_scene_slot = 0u;
    bank_has_resident_bank = 0u;
}

void bank_setDisplayName(const char name[BANK_DISPLAY_NAME_LEN])
{
    bank_copyDisplayName(bank_display_name, name);
}

const char *bank_displayName(void)
{
    return bank_display_name;
}

void bank_setRestoreBankSlot(uint16_t slot)
{
    /*
     * Store the Bank slot used by boot restore and Save:[Bank] entry.
     *
     * Inputs: a root Bank library slot number from settings.cfg, successful
     * Bank Load, or successful Bank Save. Output: bounded 0..999 retained
     * value. Slot 000 is a real slot and is also the default when malformed
     * input is supplied.
     */
    bank_restore_bank_slot = (slot < BANK_RESTORE_SLOT_COUNT) ? slot : 0u;
}

uint16_t bank_restoreBankSlot(void)
{
    return bank_restore_bank_slot;
}

void bank_setScenePresentMask(uint16_t mask)
{
    /*
     * Retain which resident Scene slots currently contain valid data.
     *
     * Inputs: Bank Load child-discovery masks, Scene/Kit fallback setup, or
     * save/load completion code. Output: a normalized 16-bit occupancy mask
     * used by Load/Save SEQ LEDs and source-selection guards. An empty mask is
     * allowed for a valid empty Bank; selection callers decide when at least
     * one Scene is required.
     */
    bank_scene_mask_present = bank_normalizeSceneMask(mask);
}

uint16_t bank_scenePresentMask(void)
{
    return bank_scene_mask_present;
}

uint8_t bank_scenePresent(uint8_t scene_index)
{
    return (uint8_t)((bank_scene_mask_present & bank_sceneBit(scene_index)) !=
                     0u);
}

void bank_setActiveSceneSlot(uint8_t slot)
{
    /*
     * Clamp Bank-local Scene identity to the product's 16 slots.
     *
     * Bank-local folders are numbered 00..15. Values outside that range can
     * appear only through malformed bankset.bcg data or future UI mistakes, so
     * they fall back to slot 00 instead of indexing past the resident Bank
     * workspace.
     */
    bank_active_scene_slot = (slot < BANK_SCENE_SLOT_COUNT) ? slot : 0u;
    bank_ensureActiveInVoiceEditMask();
}

uint8_t bank_activeSceneSlot(void)
{
    return bank_active_scene_slot;
}

void bank_selectActiveSceneForEditMask(uint8_t slot)
{
    /*
     * Select a new active Scene while preserving the edit-mask invariant.
     *
     * Inputs: PERF-mode Scene switch or Bank Load active_scene field. Output:
     * bank_active_scene_slot changes to the bounded Scene index. If the new
     * active Scene was already inside scene_mask_voice_edit, the multi-Scene
     * edit set is preserved; if not, the previous mask is dropped and replaced
     * by the new active Scene bit.
     */
    bank_active_scene_slot = (slot < BANK_SCENE_SLOT_COUNT) ? slot : 0u;
    bank_ensureActiveInVoiceEditMask();
}

void bank_setSceneMaskVoiceEdit(uint16_t mask)
{
    /*
     * Set the Scene fan-out mask used by VOICE-mode edits.
     *
     * This is named scene_mask_voice_edit deliberately: the bits address
     * Scenes, not voices. Inputs come from bankset.bcg and VOICE-held SEQ
     * toggles. Outputs are consumed by Menu edit commits and LED refresh. The
     * active Scene bit is enforced here so no caller can create an inaudible
     * active Scene outside the edit set.
     */
    bank_scene_mask_voice_edit = bank_normalizeSceneMask(mask);
    bank_ensureActiveInVoiceEditMask();
}

uint16_t bank_sceneMaskVoiceEdit(void)
{
    bank_ensureActiveInVoiceEditMask();
    return bank_scene_mask_voice_edit;
}

uint8_t bank_sceneInVoiceEditMask(uint8_t scene_index)
{
    return (uint8_t)((bank_sceneMaskVoiceEdit() & bank_sceneBit(scene_index)) !=
                     0u);
}

void bank_toggleSceneMaskVoiceEdit(uint8_t scene_index)
{
    uint16_t bit = bank_sceneBit(scene_index);

    /*
     * Toggle one resident Scene in the VOICE edit fan-out set.
     *
     * Inputs: physical SEQ button index while VOICE is held. Output:
     * scene_mask_voice_edit gains or loses that Scene bit, except the active
     * Scene can never be removed because bank_ensureActiveInVoiceEditMask()
     * immediately restores it. Menu owns compatibility checks before allowing
     * a Scene to be toggled on.
     */
    if (bit == 0u)
        return;
    bank_scene_mask_voice_edit =
        (uint16_t)(bank_scene_mask_voice_edit ^ bit);
    bank_ensureActiveInVoiceEditMask();
}

void bank_setHasResidentBank(uint8_t present)
{
    bank_has_resident_bank = present ? 1u : 0u;
}

uint8_t bank_hasResidentBank(void)
{
    return bank_has_resident_bank;
}
