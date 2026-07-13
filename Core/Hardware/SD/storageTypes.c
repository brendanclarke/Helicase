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

static uint8_t storage_formatLiteral(char *dst, uint16_t capacity,
                                     const char *text)
{
    uint16_t len = 0u;

    /*
     * Copy a known schema literal without using printf-family code.
     *
     * The firmware link does not provide heap/syscall stubs required by
     * newlib's formatted I/O path. Save writers only need tiny decimal/schema
     * lines, so these bounded helpers keep storage formatting deterministic and
     * avoid pulling in host-style stdio support.
     */
    if (!dst || capacity == 0u || len >= capacity)
        return 0u;
    if (!text)
        return 0u;
    while (text[len] != '\0')
        len++;
    if (len >= capacity)
        return 0u;
    memcpy(dst, text, len + 1u);
    return (uint8_t)len;
}

static uint8_t storage_formatAssignmentText(char *dst, uint16_t capacity,
                                            const char *key,
                                            const char *value)
{
    uint16_t len = 0u;

    /*
     * Format "key=value\n" for save files.
     *
     * Inputs are schema-owned keys and already-sanitized storage tokens, so the
     * helper only performs bounded concatenation. Returning zero on overflow
     * lets filesystem stop rather than write a truncated assignment.
     */
    if (!dst || capacity == 0u || !key || !value)
        return 0u;
    while (*key != '\0') {
        if (len + 1u >= capacity)
            return 0u;
        dst[len++] = *key++;
    }
    if (len + 1u >= capacity)
        return 0u;
    dst[len++] = '=';
    while (*value != '\0') {
        if (len + 1u >= capacity)
            return 0u;
        dst[len++] = *value++;
    }
    if (len + 1u >= capacity)
        return 0u;
    dst[len++] = '\n';
    dst[len] = '\0';
    return (uint8_t)len;
}

static uint8_t storage_formatAssignmentU16(char *dst, uint16_t capacity,
                                           const char *key,
                                           uint16_t value)
{
    char digits[6];
    uint8_t digit_count = 0u;
    uint16_t divisor = 10000u;
    uint8_t seen = 0u;

    /*
     * Format "key=<decimal>\n" for byte/range parameter values.
     *
     * Values are uint16_t because descriptor images are indexed by firmware
     * parameter storage, but current schema lines remain small enough for the
     * shared FS_TEXT_LINE_MAX buffer.
     */
    while (divisor > 0u) {
        uint8_t digit = (uint8_t)(value / divisor);
        if (digit != 0u || seen || divisor == 1u) {
            digits[digit_count++] = (char)('0' + digit);
            seen = 1u;
        }
        value = (uint16_t)(value % divisor);
        divisor = (uint16_t)(divisor / 10u);
    }
    digits[digit_count] = '\0';
    return storage_formatAssignmentText(dst, capacity, key, digits);
}

static uint8_t storage_formatSlotHeader(char *dst, uint16_t capacity,
                                        uint8_t one_based_slot)
{
    uint16_t len = 0u;

    /*
     * Format "[slotN]\n" without stdio.
     *
     * Kit slot count is six today, but the helper accepts any single decimal
     * digit so the call site documents the on-card section grammar directly.
     */
    if (!dst || capacity < 9u || one_based_slot == 0u ||
        one_based_slot > 9u) {
        return 0u;
    }
    dst[len++] = '[';
    dst[len++] = 's';
    dst[len++] = 'l';
    dst[len++] = 'o';
    dst[len++] = 't';
    dst[len++] = (char)('0' + one_based_slot);
    dst[len++] = ']';
    dst[len++] = '\n';
    dst[len] = '\0';
    return (uint8_t)len;
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

const char *storage_instrumentTypeToText(storage_instrument_type_t type)
{
    /*
     * Convert runtime instrument type to storage schema text.
     *
     * Inputs: registered instrument type. Output: lowercase token used by both
     * kitset.kcg and instrument metadata. Returning NULL for unknown types lets
     * filesystem reject an impossible save before it writes a partial file.
     */
    switch (type) {
    case STORAGE_INSTRUMENT_DRM: return "drm";
    case STORAGE_INSTRUMENT_SNR: return "snr";
    case STORAGE_INSTRUMENT_CYM: return "cym";
    case STORAGE_INSTRUMENT_HAT: return "hat";
    default: return NULL;
    }
}

const char *storage_instrumentTypeExtension(storage_instrument_type_t type)
{
    /*
     * Convert runtime instrument type to the 8.3 file extension.
     *
     * The extension is kept beside the type text because save and load must
     * agree on the same storage-level type vocabulary.
     */
    return storage_instrumentTypeToText(type);
}

static char storage_filenameChar(char c)
{
    if (c >= 'a' && c <= 'z')
        return c;
    if (c >= 'A' && c <= 'Z')
        return (char)(c + ('a' - 'A'));
    if (c >= '0' && c <= '9')
        return c;
    if (c == '_')
        return c;
    return '_';
}

void storage_makeSavedInstrumentFilename(
    char dst[STORAGE_KIT_FILENAME_MAX],
    const char *stem,
    storage_instrument_type_t type,
    uint8_t one_based_voice,
    uint8_t force_voice_suffix)
{
    const char *ext = storage_instrumentTypeExtension(type);
    char base[9];
    uint8_t i = 0u;
    uint8_t base_limit = force_voice_suffix ? 6u : 8u;

    /*
     * Build an 8.3-safe saved instrument filename from Scene metadata.
     *
     * The retained stem may be 16 characters, while some compatibility callers
     * still need an explicit short alias. When requested, the last two basename
     * cells become v1..v6 so duplicate stems cannot collide inside one saved
     * Kit folder. LFN save paths use the display-name helper below instead.
     */
    if (!ext)
        ext = "drm";
    if (!stem || stem[0] == '\0')
        stem = "inst";
    memset(base, 0, sizeof(base));
    while (stem[i] != '\0' && stem[i] != '.' && i < base_limit) {
        base[i] = storage_filenameChar(stem[i]);
        i++;
    }
    if (i == 0u)
        base[i++] = 'i';
    if (force_voice_suffix) {
        while (i < 6u)
            base[i++] = '_';
        base[i++] = 'v';
        base[i++] = (one_based_voice >= 1u && one_based_voice <= 9u)
            ? (char)('0' + one_based_voice) : 'x';
    }
    i = 0u;
    while (base[i] != '\0' && i < 8u) {
        dst[i] = base[i];
        i++;
    }
    dst[i++] = '.';
    while (*ext != '\0' && i < (STORAGE_KIT_FILENAME_MAX - 1u))
        dst[i++] = *ext++;
    dst[i] = '\0';
}

static char storage_displayFilenameChar(char c)
{
    /*
     * Sanitize one character for a FAT long filename display component.
     *
     * Unlike storage_filenameChar(), this helper intentionally preserves case
     * and spaces. It only replaces control bytes and FAT-forbidden punctuation,
     * so firmware-created LFNs can round-trip the human-facing stem as closely
     * as the current ASCII UI allows.
     */
    if (c < 0x20 || c > 0x7e)
        return '_';
    switch (c) {
    case '"':
    case '*':
    case '/':
    case ':':
    case '<':
    case '>':
    case '?':
    case '\\':
    case '|':
    case 0x7f:
        return '_';
    default:
        return c;
    }
}

void storage_makeSavedInstrumentDisplayFilename(char *dst,
                                                uint8_t capacity,
                                                const char *stem,
                                                storage_instrument_type_t type,
                                                uint8_t one_based_voice,
                                                uint8_t force_voice_suffix)
{
    const char *ext = storage_instrumentTypeExtension(type);
    uint8_t pos = 0u;
    uint8_t last_meaningful = 0u;

    /*
     * Build the visible LFN component for one saved Kit member file.
     *
     * The old 8.3 helper remains the compatibility fallback and alias generator
     * input. This helper is for the new asyncfatfs LFN create path: it keeps
     * the retained Scene stem's spaces/case, trims unsafe trailing spaces/dots,
     * optionally appends a duplicate-breaking voice suffix, and finally appends
     * the descriptor-owned extension.
     */
    if (!dst || capacity == 0u)
        return;
    if (!ext)
        ext = "drm";
    if (!stem || stem[0] == '\0')
        stem = "inst";
    while (stem[pos] != '\0' && stem[pos] != '.' &&
           pos + 1u < capacity && pos < SCENE_INSTRUMENT_STEM_LEN) {
        char c = storage_displayFilenameChar(stem[pos]);
        dst[pos] = c;
        if (c != ' ' && c != '.')
            last_meaningful = (uint8_t)(pos + 1u);
        pos++;
    }
    if (last_meaningful == 0u) {
        const char *fallback = "inst";
        pos = 0u;
        while (fallback[pos] != '\0' && pos + 1u < capacity) {
            dst[pos] = fallback[pos];
            pos++;
        }
        last_meaningful = pos;
    }
    pos = last_meaningful;
    if (force_voice_suffix && pos + 3u < capacity) {
        dst[pos++] = '_';
        dst[pos++] = 'v';
        dst[pos++] = (one_based_voice >= 1u && one_based_voice <= 9u)
            ? (char)('0' + one_based_voice) : 'x';
    }
    if (pos + 1u < capacity)
        dst[pos++] = '.';
    while (*ext != '\0' && pos + 1u < capacity)
        dst[pos++] = *ext++;
    dst[pos] = '\0';
}

uint8_t storage_formatKitsetLine(char *dst, uint16_t capacity,
                                 const kit_t *kit,
                                 const char file_names[STORAGE_KIT_SLOT_COUNT]
                                                      [STORAGE_KIT_FILENAME_MAX],
                                 uint16_t line_index)
{
    uint8_t slot;
    uint8_t field;
    const char *type_text;

    /*
     * Emit one kitset.kcg line in the same schema accepted by the parser.
     *
     * Filesystem owns async writes and calls this with monotonically advancing
     * line indices. storageTypes owns the key order, type text, generated
     * track-7 endpoint names, and per-slot file/audio fields.
     */
    if (!dst || capacity == 0u || !kit || !file_names)
        return 0u;
    if (line_index == 0u)
        return storage_formatLiteral(dst, capacity,
                                     "format=helicase.kitset\n");
    if (line_index == 1u)
        return storage_formatLiteral(dst, capacity, "version=1\n");
    if (line_index == 2u)
        return storage_formatAssignmentU16(
            dst, capacity, "slot6_track7_amp_envelope_decay",
            kit->settings.slot6_track7_amp_envelope_decay);
    if (line_index == 3u)
        return storage_formatAssignmentU16(
            dst, capacity, "slot6_track7_morph_amp_envelope_decay",
            kit->settings.slot6_track7_morph_amp_envelope_decay);
    if (line_index == 4u)
        return storage_formatLiteral(dst, capacity, "\n");
    line_index = (uint16_t)(line_index - 5u);
    slot = (uint8_t)(line_index / 5u);
    field = (uint8_t)(line_index % 5u);
    if (slot >= STORAGE_KIT_SLOT_COUNT)
        return 0u;
    type_text = storage_instrumentTypeToText(kit->instruments[slot].type);
    if (!type_text)
        return 0u;
    switch (field) {
    case 0u:
        return storage_formatSlotHeader(dst, capacity,
                                        (uint8_t)(slot + 1u));
    case 1u:
        return storage_formatAssignmentText(dst, capacity, "type",
                                            type_text);
    case 2u:
        return storage_formatAssignmentText(dst, capacity, "file",
                                            file_names[slot]);
    case 3u:
        return storage_formatAssignmentU16(dst, capacity, "audio_out",
                                           kit->settings.audio_out[slot]);
    default:
        return storage_formatLiteral(dst, capacity, "\n");
    }
}

static uint8_t storage_descriptorWritableInSection(
    const ParamDescriptor *descriptor,
    uint8_t morph_section)
{
    if (!descriptor || !descriptor->file_key)
        return 0u;
    if (!morph_section)
        return 1u;
    return (uint8_t)((descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE) != 0u);
}

static uint16_t storage_descriptorValueForSection(
    const kit_instrument_slot_t *instrument,
    uint8_t descriptor_index,
    uint8_t morph_section)
{
    return morph_section
        ? instrument->parameter_images.morph_instrument_parameters[descriptor_index]
        : instrument->parameter_images.instrument_parameters[descriptor_index];
}

uint8_t storage_formatInstrumentLine(char *dst, uint16_t capacity,
                                     const kit_instrument_slot_t *instrument,
                                     storage_instrument_type_t type,
                                     uint8_t one_based_voice,
                                     uint16_t line_index)
{
    const instrument_registry_entry_t *entry =
        instrumentManager_registryEntry(type);
    uint16_t descriptor_ordinal;
    uint8_t i;
    const char *type_text = storage_instrumentTypeToText(type);

    /*
     * Emit one complete instrument-file line from descriptor-owned Scene images.
     *
     * The writer produces metadata, one [params] block, then one [morph] block
     * in a single monotonically-indexed sequence. Keeping the section switch
     * here instead of in filesystem.c prevents async write phases from knowing
     * descriptor counts, and it guarantees the file header is emitted exactly
     * once for each saved Instrument.
     */
    if (!dst || capacity == 0u || !instrument || !entry || !type_text)
        return 0u;
    if (line_index == 0u)
        return storage_formatLiteral(dst, capacity,
                                     "format=helicase.instrument\n");
    if (line_index == 1u)
        return storage_formatLiteral(dst, capacity, "version=1\n");
    if (line_index == 2u)
        return storage_formatAssignmentText(dst, capacity, "type",
                                            type_text);
    if (line_index == 3u)
        return storage_formatLiteral(dst, capacity, "\n");
    if (line_index == 4u)
        return storage_formatLiteral(dst, capacity, "[params]\n");

    descriptor_ordinal = (uint16_t)(line_index - 5u);
    for (i = 0u; i < entry->descriptor_count; i++) {
        const ParamDescriptor *descriptor = &entry->descriptors[i];
        if (!storage_descriptorWritableInSection(descriptor, 0u))
            continue;
        if (descriptor_ordinal > 0u) {
            descriptor_ordinal--;
            continue;
        }
        if ((descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_VOICE ||
             descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_VOICE_2) &&
            storage_descriptorValueForSection(instrument, i, 0u) ==
                one_based_voice) {
            /*
             * Save the file-only LFO self token at the storage boundary.
             *
             * SceneData stores ordinary numeric voice selectors. When a
             * selector points back to the instrument's own one-based slot,
             * writing "self" preserves the intent across future loads into a
             * different slot. The token is never stored in SceneData.
             */
            return storage_formatAssignmentText(dst, capacity,
                                                descriptor->file_key,
                                                "self");
        }
        return storage_formatAssignmentU16(dst, capacity,
            descriptor->file_key,
            storage_descriptorValueForSection(instrument, i, 0u));
    }
    if (descriptor_ordinal == 0u)
        return storage_formatLiteral(dst, capacity, "\n");
    descriptor_ordinal--;
    if (descriptor_ordinal == 0u)
        return storage_formatLiteral(dst, capacity, "[morph]\n");
    descriptor_ordinal--;
    for (i = 0u; i < entry->descriptor_count; i++) {
        const ParamDescriptor *descriptor = &entry->descriptors[i];
        if (!storage_descriptorWritableInSection(descriptor, 1u))
            continue;
        if (descriptor_ordinal > 0u) {
            descriptor_ordinal--;
            continue;
        }
        /*
         * Morph storage writes only morphable endpoint descriptors.
         *
         * Target/routing descriptors are intentionally omitted here because the
         * loader treats them as single-endpoint setup cells. That keeps saved
         * morph data aligned with the existing parser contract: [morph] changes
         * values that can interpolate, while [params] owns instrument routing.
         */
        return storage_formatAssignmentU16(dst, capacity,
            descriptor->file_key,
            storage_descriptorValueForSection(instrument, i, 1u));
    }
    if (descriptor_ordinal == 0u)
        return storage_formatLiteral(dst, capacity, "\n");
    return 0u;
}

/* See storageTypes.h for the public contract.
 *
 * Folder numbers are one-based on the SD card because they are user-visible:
 * 001 Name through 999 Name. The returned slot is zero-based because preset,
 * menu, and ParameterArray code already use zero-based indices internally. The
 * separator may be space or underscore for compatibility with older generated
 * folders, but display-name copying starts after any separator run so internal
 * spaces in names such as "Moch to" are preserved.
 */
uint8_t storage_parseNumberedFolder(const char *name,
                                    uint16_t *zero_based_slot,
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

    *zero_based_slot = (uint16_t)(number - 1u);
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
        scene_setKitInstrumentSourceName(target_kit, parsed, value);
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

        if (descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_VOICE ||
            descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_VOICE_2) {
            /*
             * Resolve file-only self targets at the storage boundary.
             *
             * Inputs: lfo_target_voice/lfo_target_voice_2 value text and the
             * parser's one-based destination slot. Output: the ordinary
             * numeric selector stored in SceneData. The self token is not a
             * Menu value, descriptor sentinel, or DSP runtime state; Preset's
             * LFO pair normalization intentionally receives only numeric voice
             * selectors and packed parameter IDs. Numeric parsing keeps the
             * legacy clamp because old converted files can contain zero here.
             */
            if (storage_streq(value, "self")) {
                if (state->expected_slot < 1u ||
                    state->expected_slot > STORAGE_KIT_SLOT_COUNT) {
                    return STORAGE_STATUS_BAD_SLOT;
                }
                parsed = state->expected_slot;
            } else {
                st = storage_parseU8(value, &parsed);
                if (st != STORAGE_STATUS_OK)
                    return st;
                if (parsed < 1u)
                    parsed = 1u;
                else if (parsed > STORAGE_KIT_SLOT_COUNT)
                    parsed = STORAGE_KIT_SLOT_COUNT;
            }
        } else {
            st = storage_parseU8(value, &parsed);
            if (st != STORAGE_STATUS_OK)
                return st;
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
