#include "BankData.h"
#include <string.h>

static char bank_display_name[BANK_DISPLAY_NAME_LEN + 1u];
static uint8_t bank_active_scene_slot;
static uint8_t bank_has_resident_bank;

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
}

uint8_t bank_activeSceneSlot(void)
{
    return bank_active_scene_slot;
}

void bank_setHasResidentBank(uint8_t present)
{
    bank_has_resident_bank = present ? 1u : 0u;
}

uint8_t bank_hasResidentBank(void)
{
    return bank_has_resident_bank;
}
