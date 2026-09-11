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

static uint8_t storage_formatAssignmentU8(char *dst, uint16_t capacity,
                                          const char *key,
                                          instrument_param_value_t value)
{
    /*
     * Format one byte-domain Instrument value.
     *
     * Instrument descriptor rows now serialize exactly the byte retained in
     * SceneData. Target selector rows are compact tokens, not canonical IDs, so
     * the writer deliberately uses this byte helper instead of the wider U16
     * formatter used by non-parameter metadata such as active_scene.
     */
    return storage_formatAssignmentU16(dst, capacity, key, value);
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
    return (key[0] == '\0') ? STORAGE_STATUS_INVALID_FORMAT
                            : STORAGE_STATUS_OK;
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

static storage_status_t storage_parseCsvU8(const char *text,
                                           uint8_t *values,
                                           uint8_t expected_count,
                                           uint8_t max_value)
{
    uint8_t index = 0u;

    /*
     * Parse a fixed-count comma-separated byte list.
     *
     * Inputs: value text such as "1,2,3", caller-owned output array, required
     * item count, and maximum accepted value for each cell. Output: the array
     * is filled only as each token validates; callers treat any BAD_VALUE
     * result as a rejected line/file. The loop is intentionally strict about
     * count and separators so a truncated sceneset like "1,2" cannot silently
     * leave old settings in later Scene tracks.
     */
    if (!values || expected_count == 0u)
        return STORAGE_STATUS_BAD_VALUE;
    while (index < expected_count) {
        uint8_t parsed = 0u;
        storage_status_t st;
        const char *start = text;

        while (*text != '\0' && *text != ',')
            text++;
        if (*text == ',') {
            char token[4];
            uint8_t len = (uint8_t)(text - start);
            if (len == 0u || len >= sizeof(token))
                return STORAGE_STATUS_BAD_VALUE;
            memcpy(token, start, len);
            token[len] = '\0';
            st = storage_parseU8(token, &parsed);
            text++;
        } else {
            st = storage_parseU8(start, &parsed);
        }
        if (st != STORAGE_STATUS_OK || parsed > max_value)
            return STORAGE_STATUS_BAD_VALUE;
        values[index++] = parsed;
        if (*(text - 1) != ',' && *text == '\0')
            break;
    }
    if (index != expected_count || *storage_trimLeft(text) != '\0')
        return STORAGE_STATUS_BAD_VALUE;
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

void storage_copyKitMemberFilename(
    char dst[STORAGE_KIT_MEMBER_FILENAME_MAX],
    const char *src)
{
    uint8_t i;

    /*
     * Preserve a Kit member's visible filename exactly as kitset.kcg names it.
     *
     * What: Copies one NUL-terminated component into the LFN-sized kit member
     * field, truncating only at the storage component limit.
     *
     * Why: Kit member filenames are not merely reopen aliases; they carry the
     * on-card naming convention where stem character eight is the voice
     * number. Short-alias copying would erase padding spaces and can produce
     * misleading references such as `slakd11.drm`.
     *
     * Inputs: src from a parsed `file=` line. Output: dst is a safe
     * NUL-terminated component for kitset storage and later
     * afatfs_fopen_lfn() lookup.
     *
     * Affiliates/clients: storage_kitsetParseLine() and filesystem Kit/Scene
     * load.
     */
    if (!src)
        src = "";
    for (i = 0u;
         i < (STORAGE_KIT_MEMBER_FILENAME_MAX - 1u) && src[i] != '\0';
         i++) {
        dst[i] = src[i];
    }
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
        stem = "none";
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

void storage_scenesetInit(storage_sceneset_t *state)
{
    /*
     * Clear sceneset parser guard bits before the first line.
     *
     * Inputs/outputs: caller-owned parser state. Filesystem initializes the
     * staged Scene separately so missing optional settings preserve SceneData's
     * defaults while required guard bits are tracked here.
     */
    if (state)
        memset(state, 0, sizeof(*state));
}

storage_status_t storage_scenesetParseLine(
    storage_sceneset_t *state,
    const char *line,
    scene_settings_t *target_settings,
    char display[STORAGE_SCENE_DISPLAY_NAME_LEN])
{
    char key[32];
    const char *value;
    storage_status_t st;
    uint8_t parsed;

    /*
     * Parse one sceneset.scg line.
     *
     * Required metadata validates the folder only. Object identity is never
     * owned by this file: the Scene name comes from "Scene/NN Name", and the
     * embedded Kit name comes from "Kit <name>". Optional settings write
     * directly into the staged Scene settings. The display argument is retained only so
     * older call sites keep their signature while name= compatibility below
     * deliberately ignores the value.
     */
    if (!state || !line)
        return STORAGE_STATUS_BAD_VALUE;
    (void)display;
    line = storage_trimLeft(line);
    if (*line == '\0' || *line == '#')
        return STORAGE_STATUS_OK;
    st = storage_splitKeyValue(line, key, sizeof(key), &value);
    if (st != STORAGE_STATUS_OK)
        return st;

    if (storage_streq(key, "format")) {
        if (!storage_streq(value, "helicase.sceneset"))
            return STORAGE_STATUS_INVALID_FORMAT;
        state->seen_format = 1u;
    } else if (storage_streq(key, "version")) {
        st = storage_parseU8(value, &parsed);
        if (st != STORAGE_STATUS_OK)
            return st;
        if (parsed != 1u)
            return STORAGE_STATUS_UNSUPPORTED_VERSION;
        state->seen_version = 1u;
    } else if (storage_streq(key, "name")) {
        /*
         * Compatibility-only ignore for early Scene fixtures.
         *
         * Inputs: a legacy name= line, if present. Output: no parser state and
         * no resident display field changes. Writers never emit this key
         * because object names are universally owned by directories/files, not
         * by the files' own payloads.
         */
        return STORAGE_STATUS_OK;
    } else if (storage_streq(key, "morph_amount")) {
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseU8(value, &parsed);
        if (st != STORAGE_STATUS_OK)
            return st;
        target_settings->morph_amount = parsed;
    } else if (storage_streq(key, "voice_morph_amount")) {
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        return storage_parseCsvU8(value,
                                  target_settings->voice_morph_amount,
                                  INSTRUMENT_SLOT_COUNT,
                                  255u);
    } else if (storage_streq(key, "voice_decimation_all")) {
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseU8(value, &parsed);
        if (st != STORAGE_STATUS_OK)
            return st;
        target_settings->voice_decimation_all =
            (parsed > 127u) ? 127u : parsed;
    } else if (storage_streq(key, "midi_channel")) {
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseCsvU8(value,
                                target_settings->midi_channel,
                                NUM_TRACKS,
                                16u);
        if (st != STORAGE_STATUS_OK)
            return st;
        for (parsed = 0u; parsed < NUM_TRACKS; parsed++) {
            if (target_settings->midi_channel[parsed] < 1u)
                return STORAGE_STATUS_BAD_VALUE;
        }
    } else if (storage_streq(key, "midi_note")) {
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        return storage_parseCsvU8(value,
                                  target_settings->midi_note,
                                  NUM_TRACKS,
                                  127u);
    } else if (storage_streq(key, "audio_out")) {
        /*
         * Parse Scene-owned per-voice output routing.
         *
         * Inputs: exactly six comma-separated route values in the current
         * 0..5 mixer/menu domain. Output: staged Scene settings are written
         * directly and seen_audio_out records that legacy embedded kitset
         * routing should not be imported later.
         */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseCsvU8(value,
                                target_settings->audio_out,
                                INSTRUMENT_SLOT_COUNT,
                                5u);
        if (st != STORAGE_STATUS_OK)
            return st;
        state->seen_audio_out = 1u;
    } else if (storage_streq(key, "fx_send_amount")) {
        /*
         * Parse retained future FX-send amounts.
         *
         * Inputs: six comma-separated 0..127 values, one per instrument slot.
         * Output: staged Scene settings update; runtime FX routing is not
         * applied by storage.
         */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseCsvU8(value,
                                target_settings->fx_send_amount,
                                INSTRUMENT_SLOT_COUNT,
                                127u);
        if (st != STORAGE_STATUS_OK)
            return st;
        state->seen_fx_send_amount = 1u;
    } else if (storage_streq(key, "fader_setting")) {
        /*
         * Parse retained future fader topology modes.
         *
         * Inputs: six comma-separated values in the current 0..2 mode domain.
         * Output: staged Scene settings update; runtime behavior is future
         * mixer/FX work.
         */
        if (!target_settings)
            return STORAGE_STATUS_BAD_VALUE;
        st = storage_parseCsvU8(value,
                                target_settings->fader_setting,
                                INSTRUMENT_SLOT_COUNT,
                                2u);
        if (st != STORAGE_STATUS_OK)
            return st;
        state->seen_fader_setting = 1u;
    }
    return STORAGE_STATUS_OK;
}

storage_status_t storage_scenesetFinalize(const storage_sceneset_t *state)
{
    /*
     * Convert streamed sceneset bits into a load/no-load decision.
     *
     * Required fields are intentionally small: format/version protect against
     * loading a wrong text file. Scene and Kit names are not required here
     * because filesystem.c already captured them from directory names before
     * parsing sceneset.scg. Optional settings may be absent for
     * forward/backward compatibility because SceneData defaults are safe.
     */
    if (!state || !state->seen_format || !state->seen_version) {
        return STORAGE_STATUS_MISSING_REQUIRED;
    }
    return STORAGE_STATUS_OK;
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
     * Instrument leaves are eight characters or fewer plus extension.
     *
     * Why: no loader/save path retains long stems; a forced Kit-member voice
     * suffix reserves the eighth cell, while a root Instrument leaf may use
     * all eight authoritative HCNAMES cells. Inputs: save mode and identity
     * row. Output: bounded component formatting. Affiliates: filesystem.c.
     */
    uint8_t stem_limit = force_voice_suffix
        ? 7u
        : STORAGE_KIT_DISPLAY_NAME_LEN;

    /*
     * Build the visible LFN component for one saved Instrument file.
     *
     * The old 8.3 helper remains the compatibility fallback and alias generator
     * input. This helper is for the asyncfatfs LFN create path: it keeps the
     * retained Scene stem's spaces/case, trims unsafe trailing spaces/dots for
     * standalone files, writes an optional voice number into character 8, and
     * finally appends the descriptor-owned extension.
     */
    if (!dst || capacity == 0u)
        return;
    if (!ext)
        ext = "drm";
    if (!stem || stem[0] == '\0')
        stem = "inst";
    while (stem[pos] != '\0' && stem[pos] != '.' &&
           pos + 1u < capacity && pos < stem_limit) {
        char c = storage_displayFilenameChar(stem[pos]);
        dst[pos] = c;
        if (c != ' ' && c != '.')
            last_meaningful = (uint8_t)(pos + 1u);
        pos++;
    }
    if (last_meaningful == 0u) {
        const char *fallback = "none";
        /*
         * Fall back to the universal uninitialized stem.
         *
         * Inputs: an empty or all-space retained stem. Output: the same
         * filename convention used by initialized defaults, so force-suffix
         * saves produce `none   N.ext` instead of `inst   N.ext`.
         */
        pos = 0u;
        while (fallback[pos] != '\0' && pos + 1u < capacity) {
            dst[pos] = fallback[pos];
            pos++;
        }
        last_meaningful = pos;
    }
    pos = last_meaningful;
    if (force_voice_suffix && capacity > 9u) {
        while (pos < 7u)
            dst[pos++] = ' ';
        dst[pos++] = (one_based_voice >= 1u && one_based_voice <= 9u)
            ? (char)('0' + one_based_voice) : 'x';
    }
    if (pos + 1u < capacity)
        dst[pos++] = '.';
    while (*ext != '\0' && pos + 1u < capacity)
        dst[pos++] = *ext++;
    dst[pos] = '\0';
}

static instrument_param_value_t storage_interpolateMorphEndpoint(
    instrument_param_value_t normal,
    instrument_param_value_t morph,
    uint8_t amount)
{
    int32_t numerator;

    /*
     * Interpolate retained Morph endpoints for file-save projection.
     *
     * What: Computes the same 0..255 endpoint interpolation used by the runtime
     * Morph worker, but only from retained SceneData values supplied in the
     * write view.
     *
     * Why: Morph Save writes a portable file image; it must not include the
     * hidden LFO Morph overlay or depend on presetMorphEngine's bounded worker
     * state. Duplicating the tiny arithmetic here keeps storageTypes
     * self-contained while matching the runtime endpoint contract.
     *
     * Inputs: normal endpoint, morph endpoint, and retained per-voice Morph
     * amount. Output: rounded descriptor byte value; exact amount 0 and 255
     * return exact endpoints.
     *
     * Affiliates/clients: storage_valueForInstrumentSaveSection() and
     * presetMorphEngine.c interpolation contract.
     */
    if (amount == 0u)
        return normal;
    if (amount == 255u)
        return morph;

    numerator = (int32_t)normal * 255 +
                ((int32_t)morph - (int32_t)normal) * amount;
    numerator += 127;
    if (numerator < 0)
        return 0u;
    return (instrument_param_value_t)(numerator / 255);
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

static instrument_param_value_t storage_valueForInstrumentSaveSection(
    const storage_instrument_write_view_t *view,
    const ParamDescriptor *descriptor,
    uint8_t descriptor_index,
    uint8_t morph_section)
{
    const kit_instrument_slot_t *instrument = view ? view->instrument : NULL;
    instrument_param_value_t normal;
    instrument_param_value_t morph;

    /*
     * Resolve one descriptor value for the selected save view.
     *
     * What: Chooses the value written into either [params] or [morph] for one
     * descriptor-indexed storage key.
     *
     * Why: Morph Save is not a new file format. It is a projection of the
     * resident endpoint images into the existing normal Instrument schema:
     * morphable [params] and [morph] both become the current interpolated
     * value. Saving the same value twice makes a loaded Morph Save sound the
     * same at either morph endpoint instead of creating an inverted pair.
     * Non-morphable setup cells remain single-ended.
     *
     * Inputs: write view, descriptor metadata, descriptor index, and section
     * flag. Output: byte-domain descriptor value to serialize. Target selector
     * rows are emitted as compact tokens, never packed runtime target IDs.
     * Section eligibility is still enforced by
     * storage_descriptorWritableInSection().
     *
     * Affiliates/clients: storage_formatInstrumentLineView(),
     * InstrumentMrp Save, and normal Instrument Save wrapper.
     */
    if (!instrument)
        return 0u;
    normal = instrument->parameter_images.instrument_parameters[descriptor_index];
    morph =
        instrument->parameter_images.morph_instrument_parameters[descriptor_index];
    if (view->mode != STORAGE_INSTRUMENT_SAVE_MORPH) {
        return morph_section ? morph : normal;
    }
    if (descriptor &&
        (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
        return storage_interpolateMorphEndpoint(normal, morph,
                                                view->morph_amount);
    }
    return normal;
}

uint8_t storage_formatInstrumentLineView(
    char *dst,
    uint16_t capacity,
    const storage_instrument_write_view_t *view,
    uint16_t line_index)
{
    const instrument_registry_entry_t *entry =
        view ? instrumentManager_registryEntry(view->type) : NULL;
    uint16_t descriptor_ordinal;
    uint8_t i;
    const char *type_text = view ? storage_instrumentTypeToText(view->type) : NULL;

    /*
     * Emit one complete Instrument file line from a selected save view.
     *
     * What: Streams metadata, [params], and [morph] lines for one resident
     * Instrument slot. The write view decides whether morphable descriptor
     * values are written as normal Save endpoints or Morph Save's
     * interpolated-to-both-endpoints projection.
     *
     * Why: Filesystem state machines must stay descriptor-agnostic. This
     * formatter owns descriptor iteration, section ordering, morphability
     * filtering, and the file-only `self` token for own-slot LFO voice
     * selectors.
     *
     * Inputs: destination line buffer, capacity, immutable write view, and
     * monotonic line index. Output: byte count for one line, or zero when the
     * file is complete.
     *
     * Affiliates/clients: filesystem_writeTextLine(),
     * filesystem_nextInstrumentLine(), root Instrument Save, and
     * InstrumentMrp Save.
     */
    if (!dst || capacity == 0u || !view || !view->instrument ||
        !entry || !type_text)
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

    if (view->mode == STORAGE_INSTRUMENT_SAVE_MORPH_SNAPSHOT) {
        /*
         * Emit the smallest valid Instrument container for a reversible
         * InstrumentMrp baseline.
         *
         * What: retain one ordinary [params] anchor for the existing parser,
         * then stream every Morphable endpoint into [morph]. Why: the hidden
         * `kit` row needs only the entry Morph image; writing the complete
         * normal Instrument image would make InstrumentMrp depend on the
         * normal-load snapshot and could accidentally restore type/Normal
         * state. Inputs: the resident slot and its descriptor registry. Output:
         * an ordinary parseable Instrument file whose only meaningful payload
         * is the Morph endpoint domain. No RAM image is created.
         */
        if (line_index == 5u) {
            for (i = 0u; i < entry->descriptor_count; i++) {
                const ParamDescriptor *descriptor = &entry->descriptors[i];
                if (storage_descriptorWritableInSection(descriptor, 0u)) {
                    if ((descriptor->runtime.kind ==
                             INSTRUMENT_BIND_LFO_TARGET_VOICE ||
                         descriptor->runtime.kind ==
                             INSTRUMENT_BIND_LFO_TARGET_VOICE_2) &&
                        storage_valueForInstrumentSaveSection(
                            view, descriptor, i, 0u) == view->one_based_voice) {
                        return storage_formatAssignmentText(dst, capacity,
                                                            descriptor->file_key,
                                                            "self");
                    }
                    return storage_formatAssignmentU8(
                        dst, capacity, descriptor->file_key,
                        storage_valueForInstrumentSaveSection(
                            view, descriptor, i, 0u));
                }
            }
            return 0u;
        }
        if (line_index == 6u)
            return storage_formatLiteral(dst, capacity, "\n");
        if (line_index == 7u)
            return storage_formatLiteral(dst, capacity, "[morph]\n");

        descriptor_ordinal = (uint16_t)(line_index - 8u);
        for (i = 0u; i < entry->descriptor_count; i++) {
            const ParamDescriptor *descriptor = &entry->descriptors[i];
            if (!storage_descriptorWritableInSection(descriptor, 1u))
                continue;
            if (descriptor_ordinal > 0u) {
                descriptor_ordinal--;
                continue;
            }
            return storage_formatAssignmentU8(
                dst, capacity, descriptor->file_key,
                storage_valueForInstrumentSaveSection(
                    view, descriptor, i, 1u));
        }
        if (descriptor_ordinal == 0u)
            return storage_formatLiteral(dst, capacity, "\n");
        return 0u;
    }

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
            storage_valueForInstrumentSaveSection(view, descriptor, i, 0u) ==
                view->one_based_voice) {
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
        return storage_formatAssignmentU8(dst, capacity,
            descriptor->file_key,
            storage_valueForInstrumentSaveSection(view, descriptor, i, 0u));
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
        return storage_formatAssignmentU8(dst, capacity,
            descriptor->file_key,
            storage_valueForInstrumentSaveSection(view, descriptor, i, 1u));
    }
    if (descriptor_ordinal == 0u)
        return storage_formatLiteral(dst, capacity, "\n");
    return 0u;
}

uint8_t storage_formatInstrumentLine(char *dst, uint16_t capacity,
                                     const kit_instrument_slot_t *instrument,
                                     storage_instrument_type_t type,
                                     uint8_t one_based_voice,
                                     uint16_t line_index)
{
    storage_instrument_write_view_t view = {
        instrument,
        type,
        one_based_voice,
        0u,
        STORAGE_INSTRUMENT_SAVE_NORMAL
    };

    /*
     * Preserve the normal-save public formatter contract.
     *
     * What: Adapts the existing call sites to the new write-view formatter with
     * STORAGE_INSTRUMENT_SAVE_NORMAL.
     *
     * Why: Phase 3 adds Morph Save projection without forcing every normal
     * writer call site to construct a view. The old symbol remains the stable
     * normal-save API for Kit Save, Instrument Save, and tests.
     *
     * Affiliates/clients: filesystem_nextInstrumentLine(), generated fixtures,
     * future unit tests that compare normal Instrument output.
     */
    return storage_formatInstrumentLineView(dst, capacity, &view, line_index);
}

void storage_effectStateInit(storage_effect_state_t *state)
{
    /*
     * Clear placeholder effect validation bits.
     *
     * Effect files have no runtime payload yet, but Scene load still requires
     * a guarded .fx file so the folder contract stays stable before Phase 6.
     */
    if (state)
        memset(state, 0, sizeof(*state));
}

storage_status_t storage_effectParseLine(storage_effect_state_t *state,
                                         const char *line)
{
    char key[24];
    const char *value;
    storage_status_t st;
    uint8_t parsed;

    /*
     * Parse one placeholder .fx line.
     *
     * Unknown keys are ignored for forward compatibility with future effect
     * schemas, but the v1 placeholder guard must be present for this first
     * implementation to accept the file as a valid Scene effect.
     */
    if (!state || !line)
        return STORAGE_STATUS_BAD_VALUE;
    line = storage_trimLeft(line);
    if (*line == '\0' || *line == '#')
        return STORAGE_STATUS_OK;
    st = storage_splitKeyValue(line, key, sizeof(key), &value);
    if (st != STORAGE_STATUS_OK)
        return st;
    if (storage_streq(key, "format")) {
        if (!storage_streq(value, "helicase.effect"))
            return STORAGE_STATUS_INVALID_FORMAT;
        state->seen_format = 1u;
    } else if (storage_streq(key, "version")) {
        st = storage_parseU8(value, &parsed);
        if (st != STORAGE_STATUS_OK)
            return st;
        if (parsed != 1u)
            return STORAGE_STATUS_UNSUPPORTED_VERSION;
        state->seen_version = 1u;
    } else if (storage_streq(key, "placeholder")) {
        st = storage_parseU8(value, &parsed);
        if (st != STORAGE_STATUS_OK)
            return st;
        if (parsed != 1u)
            return STORAGE_STATUS_BAD_VALUE;
        state->seen_placeholder = 1u;
    }
    return STORAGE_STATUS_OK;
}

storage_status_t storage_effectFinalize(const storage_effect_state_t *state)
{
    /*
     * Validate the first-pass effect file.
     *
     * Until real FX data exists, requiring placeholder=1 prevents arbitrary
     * .fx files from being mistaken for loadable effect state.
     */
    if (!state || !state->seen_format || !state->seen_version ||
        !state->seen_placeholder) {
        return STORAGE_STATUS_MISSING_REQUIRED;
    }
    return STORAGE_STATUS_OK;
}


/* Bankset's hexadecimal mask parser remains shared with the Bank schema. */
static int8_t storage_patternHex(char c)
{
    if (c >= '0' && c <= '9') return (int8_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
    return -1;
}

/* The effect and Bank directory schemas remain independent of Step storage. */
uint8_t storage_formatEffectPlaceholderLine(char *dst, uint16_t capacity,
                                            uint16_t line_index)
{
    if (line_index == 0u) return storage_formatLiteral(dst, capacity, "format=helicase.effect\n");
    if (line_index == 1u) return storage_formatLiteral(dst, capacity, "version=1\n");
    if (line_index == 2u) return storage_formatLiteral(dst, capacity, "placeholder=1\n");
    return 0u;
}

void storage_banksetInit(storage_bankset_t *state)
{
    if (state) memset(state, 0, sizeof(*state));
}
storage_status_t storage_banksetParseLine(storage_bankset_t *state, const char *line)
{
    char key[32]; const char *value; storage_status_t st;
    if (!state || !line) return STORAGE_STATUS_BAD_VALUE;
    st = storage_splitKeyValue(line, key, sizeof(key), &value); if (st != STORAGE_STATUS_OK) return st;
    if (storage_streq(key,"format")) { if (!storage_streq(value,"helicase.bankset")) return STORAGE_STATUS_INVALID_FORMAT; state->seen_format=1u; }
    else if (storage_streq(key,"version")) { uint8_t v; st=storage_parseU8(value,&v); if(st!=STORAGE_STATUS_OK||v!=2u) return STORAGE_STATUS_UNSUPPORTED_VERSION; state->seen_version=1u; }
    else if (storage_streq(key,"active_scene")) { st=storage_parseU8(value,&state->active_scene); if(st!=STORAGE_STATUS_OK) return st; state->seen_active_scene=1u; }
    else if (storage_streq(key,"scene_mask_voice_edit")) {
        uint16_t value16 = 0u; uint8_t n = 0u; uint8_t digits = 0u;
        if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))
            n = 2u;
        while (value[n] != '\0' && digits < 4u) {
            int8_t digit = storage_patternHex(value[n++]);
            if (digit < 0) return STORAGE_STATUS_BAD_VALUE;
            value16 = (uint16_t)((value16 << 4u) | (uint8_t)digit);
            digits++;
        }
        if (value[n] != '\0' || digits == 0u) return STORAGE_STATUS_BAD_VALUE;
        state->scene_mask_voice_edit = value16;
        state->seen_scene_mask_voice_edit = 1u;
    }
    return STORAGE_STATUS_OK;
}
storage_status_t storage_banksetFinalize(const storage_bankset_t *state)
{ return (!state || !state->seen_format || !state->seen_version) ? STORAGE_STATUS_MISSING_REQUIRED : STORAGE_STATUS_OK; }
uint8_t storage_formatBanksetLine(char *dst,uint16_t capacity,const storage_bankset_t *state,uint16_t line_index)
{
    if (!state) return 0u;
    if(line_index==0u) return storage_formatLiteral(dst,capacity,"format=helicase.bankset\n");
    if(line_index==1u) return storage_formatLiteral(dst,capacity,"version=2\n");
    if(line_index==2u) return storage_formatAssignmentU16(dst,capacity,"active_scene",state->active_scene);
    if(line_index==3u) {
        static const char hex[]="0123456789abcdef";
        if (capacity < 28u) return 0u;
        memcpy(dst,"scene_mask_voice_edit=",22u);
        dst[22]=hex[(state->scene_mask_voice_edit >> 12u)&15u];
        dst[23]=hex[(state->scene_mask_voice_edit >> 8u)&15u];
        dst[24]=hex[(state->scene_mask_voice_edit >> 4u)&15u];
        dst[25]=hex[state->scene_mask_voice_edit&15u]; dst[26]='\n'; dst[27]='\0'; return 27u;
    }
    return 0u;
}

/* See storageTypes.h for the public contract.
 *
 * Folder numbers are literal library slots on the SD card:
 * 000 Name through 999 Name. Slot 000 is a real slot, not an empty sentinel, so
 * the returned slot is the parsed number itself. The separator may be space or
 * underscore for compatibility with older generated folders, but display-name
 * copying starts after any separator run so internal spaces in names such as
 * "Moch to" are preserved.
 */
uint8_t storage_parseNumberedFolder(const char *name,
                                    uint16_t *slot,
                                    char display[STORAGE_KIT_DISPLAY_NAME_LEN])
{
    uint16_t number;
    const char *display_start;

    if (!name || !slot || !display)
        return 0u;

    if (name[0] < '0' || name[0] > '9' ||
        name[1] < '0' || name[1] > '9' ||
        name[2] < '0' || name[2] > '9' ||
        (name[3] != '_' && name[3] != ' ')) {
        return 0u;
    }

    number = (uint16_t)((uint16_t)(name[0] - '0') * 100u +
                        (uint16_t)(name[1] - '0') * 10u +
                        (uint16_t)(name[2] - '0'));
    /*
     * Compare against the array count, not the highest display number.
     *
     * With STORAGE_KIT_MAX_SLOTS == 1000, valid prefixes are exactly
     * 000..999. This avoids the previous one-based `number - 1` mapping that
     * made 000 impossible and shifted every folder by one slot.
     */
    if (number >= STORAGE_KIT_MAX_SLOTS)
        return 0u;

    display_start = name + 4u;

    *slot = number;
    /*
     * Accept a blank numbered-folder display name.
     *
     * What: `NNN ` is a valid numbered folder component. The parsed display
     * field becomes eight spaces, matching a retained internal blank name.
     *
     * Why: Blank is a real user-entered name, not an empty-slot sentinel. Slot
     * occupancy comes from the numeric prefix and validated folder contents;
     * the UI string `Empty` is only the display for an absent slot.
     *
     * Inputs: FAT display component beginning with three digits and a space or
     * underscore. Outputs: slot receives 000..999 directly; display receives
     * the post-separator text padded to the fixed LCD width, or all spaces
     * when blank.
     *
     * Affiliates/clients: filesystem_recordKitDirectory(),
     * filesystem_recordSceneDirectory(), and retained-name storage.
     */
    storage_copyDisplayName(display, display_start);
    return 1u;
}

uint8_t storage_parseBankSceneFolder(
    const char *name,
    uint8_t *slot,
    char display[STORAGE_SCENE_DISPLAY_NAME_LEN])
{
    uint8_t number;

    /*
     * Parse the Bank-local two-digit Scene namespace.
     *
     * Inputs: visible child directory component inside one Bank. Output:
     * direct Bank Scene slot 0..15 plus the post-separator display name. The
     * arithmetic is deliberately two digits only: `(tens * 10) + ones`.
     * Accepting the root three-digit parser here would make `Scene/000` and
     * `Bank/000/00` look like the same kind of object, which they are not.
     */
    if (!name || !slot || !display)
        return 0u;
    if (name[0] < '0' || name[0] > '9' ||
        name[1] < '0' || name[1] > '9' ||
        (name[2] != '_' && name[2] != ' ')) {
        return 0u;
    }
    number = (uint8_t)((uint8_t)(name[0] - '0') * 10u +
                       (uint8_t)(name[1] - '0'));
    if (number >= STORAGE_BANK_SCENE_MAX_SLOTS)
        return 0u;
    *slot = number;
    storage_copyDisplayName(display, name + 3u);
    return 1u;
}

void storage_formatBankSceneDir(
    char *dst,
    uint16_t capacity,
    uint8_t slot,
    const char display[STORAGE_SCENE_DISPLAY_NAME_LEN])
{
    uint8_t i;

    /*
     * Format `SS <name>` for a Bank-local Scene child folder.
     *
     * Inputs: direct slot 0..15 and eight display cells. Output: a
     * NUL-terminated FAT display component. The digit math is the inverse of
     * storage_parseBankSceneFolder(): tens is slot / 10, ones is slot % 10.
     * Out-of-range inputs clamp to slot 00 so callers cannot generate a
     * syntactically valid but product-invalid child such as "99 Name".
     */
    if (!dst || capacity < (uint16_t)(3u + STORAGE_SCENE_DISPLAY_NAME_LEN + 1u))
        return;
    if (slot >= STORAGE_BANK_SCENE_MAX_SLOTS)
        slot = 0u;
    dst[0] = (char)('0' + (slot / 10u));
    dst[1] = (char)('0' + (slot % 10u));
    dst[2] = ' ';
    for (i = 0u; i < STORAGE_SCENE_DISPLAY_NAME_LEN; i++)
        dst[3u + i] = display ? display[i] : ' ';
    dst[3u + STORAGE_SCENE_DISPLAY_NAME_LEN] = '\0';
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
 * [slot1]..[slot6] sections collect instrument type and filename for each
 * voice. Legacy audio_out lines are accepted into side storage only; routing
 * is now Scene-owned and lives in sceneset.scg. The kit display name is owned
 * by the folder name, and performance controls such as voice_decimation_all
 * are not stored in kitset.kcg.
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
        storage_copyKitMemberFilename(kit->instrument_file[parsed], value);
        /*
         * Keep `file=` only in the transient kitset parser state.
         *
         * Why: the member name is an on-card key, not audio state. Once this
         * parser finishes, save/open paths derive their leaf from the
         * authoritative HCNAMES row, slot, and Instrument extension rather
         * than retaining a second stem inside kit_t.
         *
         * Inputs: current kitset member value. Output: parser-local filename
         * used by the immediately following load only. Affiliates:
         * filesystem.c Kit/Scene loaders and HCNAMES targeted updates.
         */
        kit->seen_file_mask = (uint8_t)(kit->seen_file_mask | (1u << parsed));
    } else if (storage_streq(key, "audio_out")) {
        /*
         * Accept old kitset routing without making it Kit-owned.
         *
         * Inputs: per-slot audio_out from pre-Scene-routing kitset files.
         * Output: compatibility side data in storage_kitset_t. Root Kit Load
         * ignores this field; Scene Load may import it only when sceneset.scg
         * lacks its own audio_out line.
         */
        st = storage_parseU8(value, &kit->legacy_audio_out[parsed]);
        if (st != STORAGE_STATUS_OK)
            return st;
        kit->seen_audio_out_mask = (uint8_t)(kit->seen_audio_out_mask | (1u << parsed));
    }

    return STORAGE_STATUS_OK;
}

uint8_t storage_kitsetHasCompleteLegacyAudioOut(const storage_kitset_t *kit)
{
    const uint8_t all_slots = (uint8_t)((1u << STORAGE_KIT_SLOT_COUNT) - 1u);

    /*
     * Report whether an old kitset supplied routing for every slot.
     *
     * Inputs: completed kitset parse state. Output: nonzero when all six
     * legacy audio_out bits are present. The bit expression deliberately
     * mirrors storage_kitsetFinalize() so partial hand-edited files never
     * import mixed default/legacy routes into a Scene.
     */
    return (uint8_t)(kit && kit->seen_audio_out_mask == all_slots);
}

const uint8_t *storage_kitsetLegacyAudioOut(const storage_kitset_t *kit)
{
    /*
     * Borrow the retained legacy routing array.
     *
     * Input: completed kitset parse state. Output: six route bytes or NULL.
     * Caller must first check storage_kitsetHasCompleteLegacyAudioOut() unless
     * it intentionally handles partial legacy data.
     */
    return kit ? kit->legacy_audio_out : 0;
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
        kit->seen_file_mask != all_slots) {
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
        if (descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_VOICE ||
            descriptor->runtime.kind == INSTRUMENT_BIND_LFO_TARGET_VOICE_2) {
            /*
             * Resolve file-only self targets at the storage boundary.
             *
             * Inputs: lfo_target_voice/lfo_target_voice_2 value text and the
             * parser's one-based destination slot. Output: the ordinary
             * numeric selector stored in SceneData. The self token is not a
             * Menu value, descriptor sentinel, or DSP runtime state. Numeric
             * value 7 is retained as the Scene namespace displayed by Menu as
             * `scn`; values outside the supported namespace clamp to a valid
             * byte before Preset normalizes the paired target token.
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
                if (parsed < INSTRUMENT_TARGET_VOICE_FIRST)
                    parsed = INSTRUMENT_TARGET_VOICE_FIRST;
                else if (parsed > INSTRUMENT_TARGET_VOICE_SCENE)
                    parsed = INSTRUMENT_TARGET_VOICE_SCENE;
            }
        } else {
            /*
             * Parse one retained byte-domain descriptor value.
             *
             * Normal sound parameters, velocity target tokens, and LFO target
             * parameter tokens all share the same file domain: one byte.
             * Target rows store compact local tokens such as 255 for off; they
             * never store packed canonical runtime IDs.
             */
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
