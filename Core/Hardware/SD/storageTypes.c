/*
 * Core/Hardware/SD/storageTypes.c
 *
 * Pure storage-format helpers for the Phase 2 SD layout.
 *
 * Why this file exists: kit-directory loading needs text schemas and
 * descriptor-based parameter routing, while filesystem.c should only sequence
 * SD access.
 * This split keeps format validation testable and keeps asyncfatfs state out of
 * storage-format code.
 *
 * Inputs are NUL-terminated lines/names supplied by filesystem.c. Outputs are
 * parser state and writes into caller-supplied Scene kit records. The only
 * runtime client today is filesystem_loadKitDirectory_tick(); the generated
 * SD_CARD/Kit data and tools/convert_legacy_kits.py are schema affiliates and
 * must remain aligned with the descriptor keys.
 */
#include "storageTypes.h"
#include <string.h>
#include <stdint.h>

/* Instrument parser section identifiers.
 *
 * The text format starts in top-level metadata, then switches to [params] or
 * [morph]. storage_instrumentParseLine() uses these byte values in
 * storage_instrument_state_t.current_section to decide whether a descriptor
 * writes to the main or morph image in a kit instrument slot.
 */
#define STORAGE_SECTION_TOP     0u
#define STORAGE_SECTION_PARAMS  1u
#define STORAGE_SECTION_MORPH   2u

/*
 * Longest legacy descriptor key is "amp_envelope_decay_closed" at 25 bytes.
 * Keep parser scratch wider than the descriptor namespace so storage lookup
 * fails only for genuinely unknown keys, not because a valid key was truncated
 * before instrumentManager_descriptorIndexByKey() sees it.
 */
#define STORAGE_INSTRUMENT_KEY_MAX 32u

static const char *storage_canonicalInstrumentKey(
    storage_instrument_type_t type, const char *key)
{
    /*
     * Translate legacy on-card keys into current descriptor keys.
     *
     * Inputs: expected instrument type and the parsed assignment key. Output:
     * canonical descriptor key to pass to InstrumentManager. This helper keeps
     * compatibility at the storage boundary: descriptor tables expose only the
     * current names, while older `.hat` files using closed/open decay keys keep
     * loading without duplicate legacy descriptor rows.
     *
     * Clients: storage_instrumentParseLine(). Affiliates:
     * HiHatParameters.c's canonical amp_envelope_decay and
     * amp_envelope_decay_choke descriptors plus generated SD_CARD data.
     */
    if (type == STORAGE_INSTRUMENT_HAT) {
        if (strcmp(key, "amp_envelope_decay_closed") == 0)
            return "amp_envelope_decay";
        if (strcmp(key, "amp_envelope_decay_open") == 0)
            return "amp_envelope_decay_choke";
    }
    return key;
}

/* Exact string comparison helper.
 *
 * Inputs: two NUL-terminated strings. Output: nonzero only for byte-for-byte
 * equality. Clients are all schema-token checks where case is fixed by the
 * storage format.
 */
static uint8_t storage_streq(const char *a, const char *b)
{
    return (uint8_t)(strcmp(a, b) == 0);
}

/* Skip leading spaces and tabs in a parser line/value.
 *
 * Input: NUL-terminated text. Output: pointer into the same string at the
 * first non-space/tab byte. Clients: both line parsers and numeric parsing.
 */
static const char *storage_trimLeft(const char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

/* Remove trailing spaces and tabs from a mutable key buffer.
 *
 * Input/output: NUL-terminated scratch string. The function edits in place and
 * has no separate return. Client: storage_splitKeyValue(), so "key = value"
 * and "key=value" are treated the same.
 */
static void storage_trimRight(char *s)
{
    uint8_t len = 0u;

    while (s[len] != '\0')
        len++;
    while (len > 0u && (s[len - 1u] == ' ' || s[len - 1u] == '\t')) {
        len--;
        s[len] = '\0';
    }
}

/* Split one schema assignment into a normalized key and value pointer.
 *
 * Inputs: full text line, caller-owned key buffer, key buffer size, and an
 * output value pointer. Outputs: key is copied and right-trimmed; *value points
 * into line after '=' with leading spaces/tabs skipped. Clients are kitset and
 * instrument parsers. A missing '=' or empty key returns INVALID_FORMAT.
 */
static storage_status_t storage_splitKeyValue(const char *line,
                                              char *key,
                                              uint8_t key_max,
                                              const char **value)
{
    const char *eq = line;
    uint8_t i = 0u;

    while (*eq != '\0' && *eq != '=')
        eq++;
    if (*eq != '=')
        return STORAGE_STATUS_INVALID_FORMAT;

    while (line < eq && i < (uint8_t)(key_max - 1u)) {
        key[i++] = *line++;
    }
    key[i] = '\0';
    storage_trimRight(key);
    *value = storage_trimLeft(eq + 1u);
    return (key[0] == '\0') ? STORAGE_STATUS_INVALID_FORMAT : STORAGE_STATUS_OK;
}

/* Parse an unsigned byte from decimal text.
 *
 * Inputs: NUL-terminated value text and output pointer. Outputs: *out receives
 * 0..255 on OK; BAD_VALUE is returned for empty text, overflow, or trailing
 * non-space content. Clients parse versions, slots, audio outputs, and all
 * instrument parameter values.
 */
static storage_status_t storage_parseU8(const char *text, uint8_t *out)
{
    uint16_t value = 0u;
    uint8_t digits = 0u;

    while (*text >= '0' && *text <= '9') {
        value = (uint16_t)((value * 10u) + (uint8_t)(*text - '0'));
        if (value > 255u)
            return STORAGE_STATUS_BAD_VALUE;
        text++;
        digits++;
    }
    if (digits == 0u || *storage_trimLeft(text) != '\0')
        return STORAGE_STATUS_BAD_VALUE;
    *out = (uint8_t)value;
    return STORAGE_STATUS_OK;
}

static storage_status_t storage_parseU16(const char *text, uint16_t *out)
{
    uint32_t value = 0u;
    uint8_t digits = 0u;
    while (*text >= '0' && *text <= '9') {
        value = value * 10u + (uint8_t)(*text - '0');
        if (value > 65535u)
            return STORAGE_STATUS_BAD_VALUE;
        text++;
        digits++;
    }
    if (digits == 0u || *storage_trimLeft(text) != '\0')
        return STORAGE_STATUS_BAD_VALUE;
    *out = (uint16_t)value;
    return STORAGE_STATUS_OK;
}

/* See storageTypes.h for the public contract.
 *
 * Implementation detail: display names are fixed-width LCD fields, not C
 * strings. Non-printable bytes are replaced with spaces so a corrupt card name
 * cannot write control characters into preset_currentName or editDisplayBuffer.
 */
void storage_copyDisplayName(char dst[STORAGE_KIT_DISPLAY_NAME_LEN],
                             const char *src)
{
    uint8_t i;

    for (i = 0u; i < STORAGE_KIT_DISPLAY_NAME_LEN; i++) {
        char c = src[i];
        if (c == '\0')
            break;
        dst[i] = (c >= 0x20 && c <= 0x7e) ? c : ' ';
    }
    for (; i < STORAGE_KIT_DISPLAY_NAME_LEN; i++)
        dst[i] = ' ';
}

/* See storageTypes.h for the public contract.
 *
 * Implementation detail: asyncfatfs opens short names in the current directory,
 * so filesystem.c caches these copied strings from either kitset.kcg or scanned
 * FAT entries and passes them back to afatfs_fopen().
 */
void storage_copyFilename(char dst[STORAGE_KIT_FILENAME_MAX],
                          const char *src)
{
    uint8_t i;

    for (i = 0u; i < (STORAGE_KIT_FILENAME_MAX - 1u) && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* See storageTypes.h for the public contract.
 *
 * This is deliberately strict and lowercase because the schema tokens are
 * generated by firmware/tools, not free-form user labels. Filename extensions
 * are the user-facing compatibility point and are checked case-insensitively
 * by storage_instrumentFilenameMatchesType().
 */
storage_instrument_type_t storage_instrumentTypeFromText(const char *text)
{
    return instrumentManager_typeFromText(text);
}

/* See storageTypes.h for the public contract.
 *
 * This validator ties kitset.kcg slot metadata to the actual instrument file
 * extension before filesystem.c opens the file. It prevents simple mistakes
 * like declaring type=snr but listing a .drm file.
 */
uint8_t storage_instrumentFilenameMatchesType(const char *filename,
                                              storage_instrument_type_t type)
{
    return instrumentManager_filenameMatchesType(filename, type);
}

/* See storageTypes.h for the public contract.
 *
 * Folder numbers are one-based on the SD card because they are user-visible:
 * 001 Name through 128 Name. The returned slot is zero-based because preset,
 * menu, and ParameterArray code already use zero-based indices internally. The
 * separator may be space or underscore for compatibility with older generated
 * folders, but display-name copying starts after any separator run so internal
 * spaces in names such as "Moch to" are preserved.
 */
uint8_t storage_parseNumberedFolder(const char *name,
                                    uint8_t *zero_based_slot,
                                    char display[STORAGE_KIT_DISPLAY_NAME_LEN])
{
    uint16_t number;
    const char *display_start;

    if (name[0] < '0' || name[0] > '9' ||
        name[1] < '0' || name[1] > '9' ||
        name[2] < '0' || name[2] > '9' ||
        (name[3] != '_' && name[3] != ' ')) {
        return 0u;
    }

    number = (uint16_t)((uint16_t)(name[0] - '0') * 100u +
                        (uint16_t)(name[1] - '0') * 10u +
                        (uint16_t)(name[2] - '0'));
    if (number == 0u || number > STORAGE_KIT_MAX_SLOTS)
        return 0u;

    display_start = name + 3u;
    while (*display_start == '_' || *display_start == ' ')
        display_start++;
    if (*display_start == '\0')
        return 0u;

    *zero_based_slot = (uint8_t)(number - 1u);
    storage_copyDisplayName(display, display_start);
    return 1u;
}

/* See storageTypes.h for the public contract.
 *
 * UNKNOWN instrument types make missing [slotN] type lines distinguishable from
 * a real type value during storage_kitsetFinalize().
 */
void storage_kitsetInit(storage_kitset_t *kit)
{
    memset(kit, 0, sizeof(*kit));
    for (uint8_t i = 0u; i < STORAGE_KIT_SLOT_COUNT; i++)
        kit->instrument_type[i] = STORAGE_INSTRUMENT_UNKNOWN;
}

/*
 * Derive the fixed LCD display stem retained with a Kit member.
 *
 * Inputs: a validated kitset `file` value and a nine-byte SceneData destination.
 * Output: at most eight filename characters before the extension, padded with
 * spaces and NUL terminated. Client: storage_kitsetParseLine(). This belongs
 * next to kitset parsing rather than filesystem scanning because Kit files can
 * reference filenames outside the root Instrument scan cache; retaining the
 * stem here keeps the Scene display correct for either source.
 */
static void storage_copyInstrumentDisplayStem(char destination[9],
                                              const char *filename)
{
    uint8_t i = 0u;

    memset(destination, ' ', 8u);
    while (filename[i] != '\0' && filename[i] != '.' && i < 8u) {
        destination[i] = filename[i];
        i++;
    }
    destination[8] = '\0';
}

/* See storageTypes.h for the public contract.
 *
 * The parser is intentionally incremental because filesystem.c reads from SD in
 * small chunks. Top-level fields validate only the file guard/version.
 * [slot1]..[slot6] sections collect instrument type, filename, and audio_out
 * for each voice. The kit display name is owned by the folder name, and
 * performance controls such as voice_decimation_all are not stored in
 * kitset.kcg.
 */
storage_status_t storage_kitsetParseLine(storage_kitset_t *kit,
                                         const char *line,
                                         kit_t *target_kit)
{
    char key[24];
    const char *value;
    storage_status_t st;
    uint8_t parsed;

    line = storage_trimLeft(line);
    if (*line == '\0' || *line == '#')
        return STORAGE_STATUS_OK;

    if (line[0] == '[') {
        if (line[1] == 's' && line[2] == 'l' && line[3] == 'o' &&
            line[4] == 't' && line[5] >= '1' && line[5] <= '6' &&
            line[6] == ']' && line[7] == '\0') {
            kit->current_slot = (uint8_t)(line[5] - '0');
            return STORAGE_STATUS_OK;
        }
        return STORAGE_STATUS_INVALID_FORMAT;
    }

    st = storage_splitKeyValue(line, key, sizeof(key), &value);
    if (st != STORAGE_STATUS_OK)
        return st;

    if (kit->current_slot == 0u) {
        if (storage_streq(key, "format")) {
            if (!storage_streq(value, "helicase.kitset"))
                return STORAGE_STATUS_INVALID_FORMAT;
            kit->seen_format = 1u;
        } else if (storage_streq(key, "version")) {
            st = storage_parseU8(value, &parsed);
            if (st != STORAGE_STATUS_OK)
                return st;
            if (parsed != 1u)
                return STORAGE_STATUS_UNSUPPORTED_VERSION;
            kit->seen_version = 1u;
        } else if (storage_streq(key, "slot6_track7_amp_envelope_decay")) {
            /*
             * Optional generated non-Choke track-7 decay endpoint.
             *
             * Inputs: top-level kitset key in the normal 0..127 parameter
             * domain. Output: target_kit retains the generated main endpoint
             * used when track 7 triggers a non-Choke instrument assigned to
             * slot 6. This is kit-owned metadata, not an instrument-file
             * descriptor, so it is parsed from kitset.kcg rather than any
             * `.drm`/`.snr`/`.cym`/`.hat` file.
             */
            if (!target_kit)
                return STORAGE_STATUS_BAD_VALUE;
            st = storage_parseU8(value, &parsed);
            if (st != STORAGE_STATUS_OK)
                return st;
            if (parsed > 127u)
                parsed = 127u;
            target_kit->settings.slot6_track7_amp_envelope_decay = parsed;
        } else if (storage_streq(key,
                    "slot6_track7_morph_amp_envelope_decay")) {
            /*
             * Optional generated non-Choke track-7 Morph endpoint.
             *
             * Inputs: top-level kitset key. Output: target_kit retains the
             * Morph-side endpoint for the same generated track-7 decay
             * parameter. It is optional for backward compatibility; missing
             * keys leave SceneData defaults in place.
             */
            if (!target_kit)
                return STORAGE_STATUS_BAD_VALUE;
            st = storage_parseU8(value, &parsed);
            if (st != STORAGE_STATUS_OK)
                return st;
            if (parsed > 127u)
                parsed = 127u;
            target_kit->settings.slot6_track7_morph_amp_envelope_decay =
                parsed;
        }
        return STORAGE_STATUS_OK;
    }

    if (kit->current_slot > STORAGE_KIT_SLOT_COUNT)
        return STORAGE_STATUS_BAD_SLOT;

    parsed = (uint8_t)(kit->current_slot - 1u);
    if (storage_streq(key, "type")) {
        storage_instrument_type_t type = storage_instrumentTypeFromText(value);
        if (type == STORAGE_INSTRUMENT_UNKNOWN)
            return STORAGE_STATUS_BAD_TYPE;
        kit->instrument_type[parsed] = type;
        kit->seen_type_mask = (uint8_t)(kit->seen_type_mask | (1u << parsed));
    } else if (storage_streq(key, "file")) {
        storage_copyFilename(kit->instrument_file[parsed], value);
        if (!target_kit)
            return STORAGE_STATUS_BAD_VALUE;
        storage_copyInstrumentDisplayStem(
            target_kit->instrument_display_name[parsed], value);
        kit->seen_file_mask = (uint8_t)(kit->seen_file_mask | (1u << parsed));
    } else if (storage_streq(key, "audio_out")) {
        if (!target_kit)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseU8(value, &target_kit->settings.audio_out[parsed]);
        if (st != STORAGE_STATUS_OK)
            return st;
        kit->seen_audio_out_mask = (uint8_t)(kit->seen_audio_out_mask | (1u << parsed));
    }

    return STORAGE_STATUS_OK;
}

/* See storageTypes.h for the public contract.
 *
 * Finalization is where "unknown but harmless while streaming" becomes "this is
 * not a loadable kit." filesystem.c treats any non-OK status here as an invalid
 * kit directory and shows the '-' preset name sentinel.
 */
storage_status_t storage_kitsetFinalize(const storage_kitset_t *kit)
{
    const uint8_t all_slots = (uint8_t)((1u << STORAGE_KIT_SLOT_COUNT) - 1u);

    if (!kit->seen_format || !kit->seen_version) {
        return STORAGE_STATUS_MISSING_REQUIRED;
    }
    if (kit->seen_type_mask != all_slots ||
        kit->seen_file_mask != all_slots ||
        kit->seen_audio_out_mask != all_slots) {
        return STORAGE_STATUS_MISSING_REQUIRED;
    }
    for (uint8_t i = 0u; i < STORAGE_KIT_SLOT_COUNT; i++) {
        if (!storage_instrumentFilenameMatchesType(kit->instrument_file[i],
                                                   kit->instrument_type[i])) {
            return STORAGE_STATUS_BAD_TYPE;
        }
    }
    return STORAGE_STATUS_OK;
}

/* See storageTypes.h for the public contract.
 *
 * The expected type/slot are captured from the already-validated kitset before
 * the file is opened. storage_instrumentParseLine() checks the file header type
 * and retains the kitset-owned slot for validation/diagnostics.
 */
void storage_instrumentStateInit(storage_instrument_state_t *state,
                                 storage_instrument_type_t type,
                                 uint8_t slot)
{
    memset(state, 0, sizeof(*state));
    state->expected_type = type;
    state->expected_slot = slot;
}

/* See storageTypes.h for the public contract.
 *
 * Metadata lines prove the file format, version, and instrument type.
 * Parameter lines are accepted only inside [params] or [morph]. For known keys,
 * parsed bytes are written through descriptor ownership into the caller's
 * Scene-owned slot. Unknown keys are skipped for forward compatibility, so
 * future instrument saves can carry extra fields without making old firmware
 * reject the whole file.
 */
storage_status_t storage_instrumentParseLine(storage_instrument_state_t *state,
                                             const char *line,
                                             kit_instrument_slot_t *slot)
{
    char key[STORAGE_INSTRUMENT_KEY_MAX];
    const char *value;
    storage_status_t st;
    uint8_t parsed;

    line = storage_trimLeft(line);
    if (*line == '\0' || *line == '#')
        return STORAGE_STATUS_OK;

    if (line[0] == '[') {
        if (storage_streq(line, "[params]")) {
            state->current_section = STORAGE_SECTION_PARAMS;
            return STORAGE_STATUS_OK;
        }
        if (storage_streq(line, "[morph]")) {
            state->current_section = STORAGE_SECTION_MORPH;
            return STORAGE_STATUS_OK;
        }
        return STORAGE_STATUS_INVALID_FORMAT;
    }

    st = storage_splitKeyValue(line, key, sizeof(key), &value);
    if (st != STORAGE_STATUS_OK)
        return st;

    if (state->current_section == STORAGE_SECTION_TOP) {
        if (storage_streq(key, "format")) {
            if (!storage_streq(value, "helicase.instrument"))
                return STORAGE_STATUS_INVALID_FORMAT;
            state->seen_format = 1u;
        } else if (storage_streq(key, "version")) {
            st = storage_parseU8(value, &parsed);
            if (st != STORAGE_STATUS_OK)
                return st;
            if (parsed != 1u)
                return STORAGE_STATUS_UNSUPPORTED_VERSION;
            state->seen_version = 1u;
        } else if (storage_streq(key, "type")) {
            if (storage_instrumentTypeFromText(value) != state->expected_type)
                return STORAGE_STATUS_BAD_TYPE;
            state->seen_type = 1u;
        }
        return STORAGE_STATUS_OK;
    }

    {
        uint8_t index;
        const char *canonical_key =
            storage_canonicalInstrumentKey(state->expected_type, key);
        const ParamDescriptor *descriptor =
            instrumentManager_descriptorIndexByKey(state->expected_type,
                                                   canonical_key, &index);
        if (!descriptor)
            return STORAGE_STATUS_OK;
        if (!slot || slot->type != state->expected_type)
            return STORAGE_STATUS_BAD_TYPE;

        /*
         * Descriptor index is the storage cell for this instrument type.
         * ParameterArray only allocates the generic slot storage; the instrument
         * descriptor defines what the cell means and how runtime application
         * should interpret it. Non-morphable runtime bindings are ignored in
         * [morph] so routing/target values keep a single endpoint.
         */
        if (descriptor->runtime.kind == INSTRUMENT_BIND_VELOCITY_TARGET ||
            descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_PARAM ||
            descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_PARAM_2) {
            uint16_t parsed16;
            /*
             * Canonical descriptor targets are 16-bit values.
             *
             * Inputs: velocity target, LFO destination 1, and LFO destination 2
             * file keys store either INSTRUMENT_PARAM_INVALID for off or a
             * packed slot/local descriptor id. Output: only the main endpoint
             * stores the parsed selector; [morph] ignores these supplemental
             * routing cells. This must not use the normal u8 parser because
             * valid target ids can exceed 255, and zero is a valid target id
             * rather than an off sentinel.
             */
            st = storage_parseU16(value, &parsed16);
            if (st != STORAGE_STATUS_OK)
                return st;
            if (state->current_section == STORAGE_SECTION_MORPH) {
                return STORAGE_STATUS_OK;
            }
            slot->parameter_images.instrument_parameters[index] = parsed16;
            state->seen_param_count++;
            return STORAGE_STATUS_OK;
        }

        st = storage_parseU8(value, &parsed);
        if (st != STORAGE_STATUS_OK)
            return st;

        if (descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_VOICE ||
            descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_VOICE_2) {
                /*
                 * Legacy converted kits can contain zero here because the old
                 * flat LFO target voice byte was repaired only during apply.
                 * SceneData now owns the generic storage cell, so clamp both
                 * LFO target voice selector pairs while parsing. Clients are
                 * filesystem_loadKitDirectory_tick() and Preset's later
                 * instrument-runtime apply.
                 */
                if (parsed < 1u)
                    parsed = 1u;
                else if (parsed > STORAGE_KIT_SLOT_COUNT)
                    parsed = STORAGE_KIT_SLOT_COUNT;
        }

        if (state->current_section == STORAGE_SECTION_MORPH) {
            if (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE) {
                slot->parameter_images.morph_instrument_parameters[index] = parsed;
                state->seen_morph_count++;
            }
            return STORAGE_STATUS_OK;
        }

        slot->parameter_images.instrument_parameters[index] = parsed;
        state->seen_param_count++;
    }

    return STORAGE_STATUS_OK;
}

/* See storageTypes.h for the public contract.
 *
 * A missing [morph] section is not an error. filesystem.c checks
 * seen_morph_count after this succeeds and calls
 * storage_instrumentCopyMainToMorphFallback() when needed.
 */
storage_status_t storage_instrumentFinalize(const storage_instrument_state_t *state)
{
    if (!state->seen_format || !state->seen_version ||
        !state->seen_type ||
        state->seen_param_count == 0u) {
        return STORAGE_STATUS_MISSING_REQUIRED;
    }
    return STORAGE_STATUS_OK;
}

/* See storageTypes.h for the public contract.
 *
     * The fallback copies only descriptors flagged as morphable. Routing/target
     * cells are single-endpoint runtime values and Scene MIDI settings live
     * outside the kit, so neither class is touched here.
 */
void storage_instrumentCopyMainToMorphFallback(storage_instrument_type_t type,
                                               uint8_t slot,
                                               kit_instrument_slot_t *instrument)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    uint8_t i;
    (void)slot;
    if (!entry || !instrument)
        return;
    for (i = 0u; i < entry->descriptor_count; i++) {
        const ParamDescriptor *descriptor = &entry->descriptors[i];
        if (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE) {
            instrument->parameter_images.morph_instrument_parameters[i] =
                instrument->parameter_images.instrument_parameters[i];
        }
    }
}
