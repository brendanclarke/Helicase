#include "BankData.h"
#include "Autosave.h"
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

static uint8_t bank_ensureActiveInVoiceEditMask(void)
{
    uint16_t active_bit = bank_sceneBit(bank_active_scene_slot);
    uint16_t previous_mask = bank_scene_mask_voice_edit;

    /*
     * Enforce the core Scene-edit invariant.
     *
     * The active Scene must always be part of scene_mask_voice_edit. Runtime
     * display, automation recording, and DSP apply all treat the active Scene
     * as the canonical audible member of a fan-out edit. If a Bank file or a
     * Scene switch leaves the active bit absent, the old mask is intentionally
     * dropped and replaced by the active Scene only. Output additionally
     * reports whether that final serialized mask differs from entry state so
     * callers can issue the VOICE-mask Autosave notification after storage.
     */
    if (active_bit == 0u)
        active_bit = 1u;
    bank_scene_mask_voice_edit =
        bank_normalizeSceneMask(bank_scene_mask_voice_edit);
    if ((bank_scene_mask_voice_edit & active_bit) == 0u)
        bank_scene_mask_voice_edit = active_bit;
    return (uint8_t)(bank_scene_mask_voice_edit != previous_mask);
}

static uint8_t bank_copyDisplayName(char dst[BANK_DISPLAY_NAME_LEN + 1u],
                                    const char src[BANK_DISPLAY_NAME_LEN])
{
    uint8_t i;
    uint8_t changed = 0u;

    /*
     * Copy exactly the LCD-visible Bank name cells.
     *
     * Inputs: directory-derived display bytes or an editor buffer. Output:
     * printable ASCII padded with spaces plus a NUL terminator, and a return
     * flag reports whether any serialized display cell changed. The fixed
     * eight-iteration loop prevents a short C string such as "Slak" from
     * leaving stale characters in cells 5..8, and keeps Bank identity aligned
     * with Scene/Kit display-name storage.
     */
    for (i = 0u; i < BANK_DISPLAY_NAME_LEN; i++) {
        char c = src ? src[i] : ' ';
        if (c < 0x20 || c > 0x7e)
            c = ' ';
        if (dst[i] != c)
            changed = 1u;
        dst[i] = c;
    }
    dst[BANK_DISPLAY_NAME_LEN] = '\0';
    return changed;
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
    /*
     * Store the normalized serialized name before notifying Autosave.
     *
     * Input: eight display cells. Output: printable/space-padded BankData name;
     * only a changed final eight-byte value marks the full Bank-name range.
     * Why: identical setters must not schedule SD work, and the writer must get
     * the already-committed value. Affiliates: bank_copyDisplayName() and the
     * format-owned AUTOSAVE_BANK_FIELD_DISPLAY_NAME mapping.
     */
    if (bank_copyDisplayName(bank_display_name, name))
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_DISPLAY_NAME);
}

const char *bank_displayName(void)
{
    return bank_display_name;
}

void bank_setRestoreBankSlot(uint16_t slot)
{
    uint16_t normalized_slot;

    /*
     * Store the Bank slot used by boot restore and Save:[Bank] entry.
     *
     * Inputs: a root Bank library slot number from settings.cfg, successful
     * Bank Load, or successful Bank Save. Output: bounded 0..999 retained
     * value. Slot 000 is a real slot and is also the default when malformed
     * input is supplied. A changed normalized final value is stored before the
     * two-byte restore-slot Autosave field is marked; an identical value is a
     * no-op. Affiliate: autosave_getLivePayloadByte()'s little-endian getter.
     */
    normalized_slot = (slot < BANK_RESTORE_SLOT_COUNT) ? slot : 0u;
    if (normalized_slot != bank_restore_bank_slot) {
        bank_restore_bank_slot = normalized_slot;
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_RESTORE_SLOT);
    }
}

uint16_t bank_restoreBankSlot(void)
{
    return bank_restore_bank_slot;
}

void bank_setScenePresentMask(uint16_t mask)
{
    uint16_t normalized_mask;

    /*
     * Retain which resident Scene slots currently contain valid data.
     *
     * Inputs: Bank Load child-discovery masks, Scene/Kit fallback setup, or
     * save/load completion code. Output: a normalized 16-bit occupancy mask
     * used by Load/Save SEQ LEDs and source-selection guards. An empty mask is
     * allowed for a valid empty Bank; selection callers decide when at least
     * one Scene is required. A changed normalized final mask is stored before
     * the two-byte Scene-present Autosave field is marked; equal input creates
     * no writer work. Affiliate: Scene marker presence validation.
     */
    normalized_mask = bank_normalizeSceneMask(mask);
    if (normalized_mask != bank_scene_mask_present) {
        bank_scene_mask_present = normalized_mask;
        autosave_markBankFieldDirty(
            AUTOSAVE_BANK_FIELD_SCENE_PRESENT_MASK);
    }
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
    uint8_t normalized_slot;
    uint8_t mask_changed;

    /*
     * Clamp Bank-local Scene identity to the product's 16 slots.
     *
     * Bank-local folders are numbered 00..15. Values outside that range can
     * appear only through malformed bankset.bcg data or future UI mistakes, so
     * they fall back to slot 00 instead of indexing past the resident Bank
     * workspace. Output marks the active-Scene byte only if its normalized
     * value changes, and independently marks the VOICE mask if invariant repair
     * changes it. Both writes precede notification. Affiliates: Autosave Bank
     * field mapping and bank_ensureActiveInVoiceEditMask().
     */
    normalized_slot = (slot < BANK_SCENE_SLOT_COUNT) ? slot : 0u;
    if (normalized_slot != bank_active_scene_slot) {
        bank_active_scene_slot = normalized_slot;
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_ACTIVE_SCENE);
    }
    mask_changed = bank_ensureActiveInVoiceEditMask();
    if (mask_changed)
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK);
}

uint8_t bank_activeSceneSlot(void)
{
    return bank_active_scene_slot;
}

void bank_selectActiveSceneForEditMask(uint8_t slot)
{
    uint8_t normalized_slot;
    uint8_t mask_changed;

    /*
     * Select a new active Scene while preserving the edit-mask invariant.
     *
     * Inputs: PERF-mode Scene switch or Bank Load active_scene field. Output:
     * bank_active_scene_slot changes to the bounded Scene index. If the new
     * active Scene was already inside scene_mask_voice_edit, the multi-Scene
     * edit set is preserved; if not, the previous mask is dropped and replaced
     * by the new active Scene bit. Changed final active/mask fields are stored
     * before their independent Autosave notifications; unchanged selection
     * produces neither bit. Affiliates: PERF Scene switching and VOICE fan-out.
     */
    normalized_slot = (slot < BANK_SCENE_SLOT_COUNT) ? slot : 0u;
    if (normalized_slot != bank_active_scene_slot) {
        bank_active_scene_slot = normalized_slot;
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_ACTIVE_SCENE);
    }
    mask_changed = bank_ensureActiveInVoiceEditMask();
    if (mask_changed)
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK);
}

void bank_setSceneMaskVoiceEdit(uint16_t mask)
{
    uint16_t previous_mask = bank_scene_mask_voice_edit;

    /*
     * Set the Scene fan-out mask used by VOICE-mode edits.
     *
     * This is named scene_mask_voice_edit deliberately: the bits address
     * Scenes, not voices. Inputs come from bankset.bcg and VOICE-held SEQ
     * toggles. Outputs are consumed by Menu edit commits and LED refresh. The
     * active Scene bit is enforced here so no caller can create an inaudible
     * active Scene outside the edit set. Only the final post-invariant value is
     * compared with entry state and marked, avoiding a false mutation when an
     * invalid intermediate mask normalizes back to the retained value.
     */
    bank_scene_mask_voice_edit = bank_normalizeSceneMask(mask);
    (void)bank_ensureActiveInVoiceEditMask();
    if (bank_scene_mask_voice_edit != previous_mask)
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK);
}

uint16_t bank_sceneMaskVoiceEdit(void)
{
    /*
     * Preserve the existing self-repairing getter without hiding a mutation.
     *
     * Inputs: retained active Scene and VOICE mask. Output: invariant-safe
     * mask; if repair changes its serialized value after tracking is enabled,
     * that field is marked. Why: this getter historically owns a normalization
     * path which cannot silently bypass Autosave. Affiliate: edit-mask users.
     */
    if (bank_ensureActiveInVoiceEditMask())
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK);
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
    uint16_t previous_mask = bank_scene_mask_voice_edit;

    /*
     * Toggle one resident Scene in the VOICE edit fan-out set.
     *
     * Inputs: physical SEQ button index while VOICE is held. Output:
     * scene_mask_voice_edit gains or loses that Scene bit, except the active
     * Scene can never be removed because bank_ensureActiveInVoiceEditMask()
     * immediately restores it. Menu owns compatibility checks before allowing
     * a Scene to be toggled on. Output compares the final invariant-safe mask
     * with entry state, stores first, and marks its Autosave field only when the
     * toggle survives normalization.
     */
    if (bit == 0u)
        return;
    bank_scene_mask_voice_edit =
        (uint16_t)(bank_scene_mask_voice_edit ^ bit);
    (void)bank_ensureActiveInVoiceEditMask();
    if (bank_scene_mask_voice_edit != previous_mask)
        autosave_markBankFieldDirty(AUTOSAVE_BANK_FIELD_VOICE_EDIT_MASK);
}

void bank_setHasResidentBank(uint8_t present)
{
    bank_has_resident_bank = present ? 1u : 0u;
}

uint8_t bank_hasResidentBank(void)
{
    return bank_has_resident_bank;
}
