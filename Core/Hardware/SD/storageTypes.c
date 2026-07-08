/*
 * Core/Hardware/SD/storageTypes.c
 *
 * Pure storage-format helpers for the Phase 2 SD layout.
 *
 * Why this file exists: kit-directory loading needs text schemas and
 * ParameterArray mapping, while filesystem.c should only sequence SD access.
 * This split keeps format validation testable and keeps asyncfatfs state out of
 * storage-format code.
 *
 * Inputs are NUL-terminated lines/names supplied by filesystem.c. Outputs are
 * parser state and byte writes into caller-supplied parameter buffers. The only
 * runtime client today is filesystem_loadKitDirectory_tick(); the generated
 * SD_CARD/Kit data and tools/convert_legacy_kits.py are schema affiliates and
 * must remain aligned with these keys.
 */
#include "storageTypes.h"
#include "ParameterArray.h"
#include <string.h>
#include <stdint.h>

/* Instrument parser section identifiers.
 *
 * The text format starts in top-level metadata, then switches to [params] or
 * [morph]. storage_instrumentParseLine() uses these byte values in
 * storage_instrument_state_t.current_section to decide whether a mapped key
 * writes to parameter_values[] or parameters2[].
 */
#define STORAGE_SECTION_TOP     0u
#define STORAGE_SECTION_PARAMS  1u
#define STORAGE_SECTION_MORPH   2u

/* One storage key to one ParameterArray enum index.
 *
 * key is the lowercase text token emitted in an instrument file. param is the
 * enum value in ParameterArray.h that receives the parsed byte. The maps below
 * are selected by storage_paramsForInstrument(), then consumed by
 * storage_instrumentParseLine() and storage_instrumentCopyMainToMorphFallback().
 */
typedef struct {
    const char *key;
    uint16_t param;
} storage_param_map_t;

/* Parameter maps for all Phase 2 instrument types.
 *
 * These arrays are the authoritative runtime counterpart to
 * tools/convert_legacy_kits.py. Each table names only parameters that belong in
 * an instrument file: oscillator/filter/envelope/modulation settings plus
 * volume and pan. MIDI notes/channels are intentionally absent because they now
 * belong to scene settings; audio_out is absent because kitset.kcg owns it.
 *
 * Drum appears three times because the same "drm" schema lands in different
 * ParameterArray enum ranges for voice slots 1, 2, and 3. Snare, cymbal, and
 * hat are constrained to slots 4, 5, and 6 respectively by
 * storage_paramsForInstrument().
 */
static const storage_param_map_t storage_drum1_params[] = {
    { "osc_wave", PAR_OSC_WAVE_DRUM1 }, { "coarse", PAR_COARSE1 },
    { "fine", PAR_FINE1 }, { "mod_wave", PAR_MOD_WAVE_DRUM1 },
    { "filter_freq", PAR_FILTER_FREQ_1 }, { "reso", PAR_RESO_1 },
    { "velo_attack", PAR_VELOA1 }, { "velo_decay", PAR_VELOD1 },
    { "vol_slope", PAR_VOL_SLOPE1 }, { "pitch_decay", PAR_MOD_EG1 },
    { "mod_amount", PAR_MODAMNT1 }, { "pitch_slope", PAR_PITCH_SLOPE1 },
    { "fm_amount", PAR_FMAMNT1 }, { "fm_freq", PAR_FM_FREQ1 },
    { "volume", PAR_VOL1 }, { "pan", PAR_PAN1 }, { "drive", PAR_DRIVE1 },
    { "voice_decimation", PAR_VOICE_DECIMATION1 },
    { "freq_lfo", PAR_FREQ_LFO1 }, { "amount_lfo", PAR_AMOUNT_LFO1 },
    { "filter_drive", PAR_FILTER_DRIVE_1 }, { "mix_mod", PAR_MIX_MOD_1 },
    { "volume_mod_on_off", PAR_VOLUME_MOD_ON_OFF1 },
    { "velo_mod_amt", PAR_VELO_MOD_AMT_1 }, { "vel_dest", PAR_VEL_DEST_1 },
    { "wave_lfo", PAR_WAVE_LFO1 }, { "voice_lfo", PAR_VOICE_LFO1 },
    { "target_lfo", PAR_TARGET_LFO1 }, { "retrigger_lfo", PAR_RETRIGGER_LFO1 },
    { "sync_lfo", PAR_SYNC_LFO1 }, { "offset_lfo", PAR_OFFSET_LFO1 },
    { "filter_type", PAR_FILTER_TYPE_1 }, { "transient_vol", PAR_TRANS1_VOL },
    { "transient_wave", PAR_TRANS1_WAVE }, { "transient_freq", PAR_TRANS1_FREQ },
};

static const storage_param_map_t storage_drum2_params[] = {
    { "osc_wave", PAR_OSC_WAVE_DRUM2 }, { "coarse", PAR_COARSE2 },
    { "fine", PAR_FINE2 }, { "mod_wave", PAR_MOD_WAVE_DRUM2 },
    { "filter_freq", PAR_FILTER_FREQ_2 }, { "reso", PAR_RESO_2 },
    { "velo_attack", PAR_VELOA2 }, { "velo_decay", PAR_VELOD2 },
    { "vol_slope", PAR_VOL_SLOPE2 }, { "pitch_decay", PAR_MOD_EG2 },
    { "mod_amount", PAR_MODAMNT2 }, { "pitch_slope", PAR_PITCH_SLOPE2 },
    { "fm_amount", PAR_FMAMNT2 }, { "fm_freq", PAR_FM_FREQ2 },
    { "volume", PAR_VOL2 }, { "pan", PAR_PAN2 }, { "drive", PAR_DRIVE2 },
    { "voice_decimation", PAR_VOICE_DECIMATION2 },
    { "freq_lfo", PAR_FREQ_LFO2 }, { "amount_lfo", PAR_AMOUNT_LFO2 },
    { "filter_drive", PAR_FILTER_DRIVE_2 }, { "mix_mod", PAR_MIX_MOD_2 },
    { "volume_mod_on_off", PAR_VOLUME_MOD_ON_OFF2 },
    { "velo_mod_amt", PAR_VELO_MOD_AMT_2 }, { "vel_dest", PAR_VEL_DEST_2 },
    { "wave_lfo", PAR_WAVE_LFO2 }, { "voice_lfo", PAR_VOICE_LFO2 },
    { "target_lfo", PAR_TARGET_LFO2 }, { "retrigger_lfo", PAR_RETRIGGER_LFO2 },
    { "sync_lfo", PAR_SYNC_LFO2 }, { "offset_lfo", PAR_OFFSET_LFO2 },
    { "filter_type", PAR_FILTER_TYPE_2 }, { "transient_vol", PAR_TRANS2_VOL },
    { "transient_wave", PAR_TRANS2_WAVE }, { "transient_freq", PAR_TRANS2_FREQ },
};

static const storage_param_map_t storage_drum3_params[] = {
    { "osc_wave", PAR_OSC_WAVE_DRUM3 }, { "coarse", PAR_COARSE3 },
    { "fine", PAR_FINE3 }, { "mod_wave", PAR_MOD_WAVE_DRUM3 },
    { "filter_freq", PAR_FILTER_FREQ_3 }, { "reso", PAR_RESO_3 },
    { "velo_attack", PAR_VELOA3 }, { "velo_decay", PAR_VELOD3 },
    { "vol_slope", PAR_VOL_SLOPE3 }, { "pitch_decay", PAR_MOD_EG3 },
    { "mod_amount", PAR_MODAMNT3 }, { "pitch_slope", PAR_PITCH_SLOPE3 },
    { "fm_amount", PAR_FMAMNT3 }, { "fm_freq", PAR_FM_FREQ3 },
    { "volume", PAR_VOL3 }, { "pan", PAR_PAN3 }, { "drive", PAR_DRIVE3 },
    { "voice_decimation", PAR_VOICE_DECIMATION3 },
    { "freq_lfo", PAR_FREQ_LFO3 }, { "amount_lfo", PAR_AMOUNT_LFO3 },
    { "filter_drive", PAR_FILTER_DRIVE_3 }, { "mix_mod", PAR_MIX_MOD_3 },
    { "volume_mod_on_off", PAR_VOLUME_MOD_ON_OFF3 },
    { "velo_mod_amt", PAR_VELO_MOD_AMT_3 }, { "vel_dest", PAR_VEL_DEST_3 },
    { "wave_lfo", PAR_WAVE_LFO3 }, { "voice_lfo", PAR_VOICE_LFO3 },
    { "target_lfo", PAR_TARGET_LFO3 }, { "retrigger_lfo", PAR_RETRIGGER_LFO3 },
    { "sync_lfo", PAR_SYNC_LFO3 }, { "offset_lfo", PAR_OFFSET_LFO3 },
    { "filter_type", PAR_FILTER_TYPE_3 }, { "transient_vol", PAR_TRANS3_VOL },
    { "transient_wave", PAR_TRANS3_WAVE }, { "transient_freq", PAR_TRANS3_FREQ },
};

static const storage_param_map_t storage_snare_params[] = {
    { "osc_wave", PAR_OSC_WAVE_SNARE }, { "coarse", PAR_COARSE4 },
    { "fine", PAR_FINE4 }, { "noise_freq", PAR_NOISE_FREQ1 },
    { "mix", PAR_MIX1 }, { "filter_freq", PAR_FILTER_FREQ_4 },
    { "reso", PAR_RESO_4 }, { "velo_attack", PAR_VELOA4 },
    { "velo_decay", PAR_VELOD4 }, { "vol_slope", PAR_VOL_SLOPE4 },
    { "repeat", PAR_REPEAT4 }, { "pitch_decay", PAR_MOD_EG4 },
    { "mod_amount", PAR_MODAMNT4 }, { "pitch_slope", PAR_PITCH_SLOPE4 },
    { "volume", PAR_VOL4 }, { "pan", PAR_PAN4 },
    { "drive", PAR_SNARE_DISTORTION }, { "voice_decimation", PAR_VOICE_DECIMATION4 },
    { "freq_lfo", PAR_FREQ_LFO4 }, { "amount_lfo", PAR_AMOUNT_LFO4 },
    { "filter_drive", PAR_FILTER_DRIVE_4 },
    { "volume_mod_on_off", PAR_VOLUME_MOD_ON_OFF4 },
    { "velo_mod_amt", PAR_VELO_MOD_AMT_4 }, { "vel_dest", PAR_VEL_DEST_4 },
    { "wave_lfo", PAR_WAVE_LFO4 }, { "voice_lfo", PAR_VOICE_LFO4 },
    { "target_lfo", PAR_TARGET_LFO4 }, { "retrigger_lfo", PAR_RETRIGGER_LFO4 },
    { "sync_lfo", PAR_SYNC_LFO4 }, { "offset_lfo", PAR_OFFSET_LFO4 },
    { "filter_type", PAR_FILTER_TYPE_4 }, { "transient_vol", PAR_TRANS4_VOL },
    { "transient_wave", PAR_TRANS4_WAVE }, { "transient_freq", PAR_TRANS4_FREQ },
};

static const storage_param_map_t storage_cymbal_params[] = {
    { "wave1", PAR_WAVE1_CYM }, { "coarse", PAR_COARSE5 },
    { "fine", PAR_FINE5 }, { "mod_osc1_freq", PAR_MOD_OSC_F1_CYM },
    { "mod_osc2_freq", PAR_MOD_OSC_F2_CYM },
    { "mod_osc1_gain", PAR_MOD_OSC_GAIN1_CYM },
    { "mod_osc2_gain", PAR_MOD_OSC_GAIN2_CYM },
    { "wave2", PAR_WAVE2_CYM }, { "wave3", PAR_WAVE3_CYM },
    { "filter_freq", PAR_FILTER_FREQ_5 }, { "reso", PAR_RESO_5 },
    { "velo_attack", PAR_VELOA5 }, { "velo_decay", PAR_VELOD5 },
    { "vol_slope", PAR_VOL_SLOPE5 }, { "repeat", PAR_REPEAT5 },
    { "volume", PAR_VOL5 }, { "pan", PAR_PAN5 },
    { "drive", PAR_CYMBAL_DISTORTION },
    { "voice_decimation", PAR_VOICE_DECIMATION5 },
    { "freq_lfo", PAR_FREQ_LFO5 }, { "amount_lfo", PAR_AMOUNT_LFO5 },
    { "filter_drive", PAR_FILTER_DRIVE_5 },
    { "volume_mod_on_off", PAR_VOLUME_MOD_ON_OFF5 },
    { "velo_mod_amt", PAR_VELO_MOD_AMT_5 }, { "vel_dest", PAR_VEL_DEST_5 },
    { "wave_lfo", PAR_WAVE_LFO5 }, { "voice_lfo", PAR_VOICE_LFO5 },
    { "target_lfo", PAR_TARGET_LFO5 }, { "retrigger_lfo", PAR_RETRIGGER_LFO5 },
    { "sync_lfo", PAR_SYNC_LFO5 }, { "offset_lfo", PAR_OFFSET_LFO5 },
    { "filter_type", PAR_FILTER_TYPE_5 }, { "transient_vol", PAR_TRANS5_VOL },
    { "transient_wave", PAR_TRANS5_WAVE }, { "transient_freq", PAR_TRANS5_FREQ },
};

static const storage_param_map_t storage_hat_params[] = {
    { "wave1", PAR_WAVE1_HH }, { "coarse", PAR_COARSE6 },
    { "fine", PAR_FINE6 }, { "mod_osc1_freq", PAR_MOD_OSC_F1 },
    { "mod_osc2_freq", PAR_MOD_OSC_F2 },
    { "mod_osc1_gain", PAR_MOD_OSC_GAIN1 },
    { "mod_osc2_gain", PAR_MOD_OSC_GAIN2 },
    { "wave2", PAR_WAVE2_HH }, { "wave3", PAR_WAVE3_HH },
    { "filter_freq", PAR_FILTER_FREQ_6 }, { "reso", PAR_RESO_6 },
    { "velo_attack", PAR_VELOA6 }, { "decay_closed", PAR_VELOD6_CLOSED },
    { "decay_open", PAR_VELOD6_OPEN }, { "vol_slope", PAR_VOL_SLOPE6 },
    { "volume", PAR_VOL6 }, { "pan", PAR_PAN6 },
    { "drive", PAR_HAT_DISTORTION }, { "voice_decimation", PAR_VOICE_DECIMATION6 },
    { "freq_lfo", PAR_FREQ_LFO6 }, { "amount_lfo", PAR_AMOUNT_LFO6 },
    { "filter_drive", PAR_FILTER_DRIVE_6 },
    { "volume_mod_on_off", PAR_VOLUME_MOD_ON_OFF6 },
    { "velo_mod_amt", PAR_VELO_MOD_AMT_6 }, { "vel_dest", PAR_VEL_DEST_6 },
    { "wave_lfo", PAR_WAVE_LFO6 }, { "voice_lfo", PAR_VOICE_LFO6 },
    { "target_lfo", PAR_TARGET_LFO6 }, { "retrigger_lfo", PAR_RETRIGGER_LFO6 },
    { "sync_lfo", PAR_SYNC_LFO6 }, { "offset_lfo", PAR_OFFSET_LFO6 },
    { "filter_type", PAR_FILTER_TYPE_6 }, { "transient_vol", PAR_TRANS6_VOL },
    { "transient_wave", PAR_TRANS6_WAVE }, { "transient_freq", PAR_TRANS6_FREQ },
};

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

/* ASCII-only lowercasing for filename extension checks.
 *
 * Input: one character. Output: lowercase A-Z, otherwise unchanged. This avoids
 * libc locale behavior and matches the FAT/8.3 ASCII filenames used by
 * filesystem.c.
 */
static char storage_lower(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c + ('a' - 'A'));
    return c;
}

/* Case-insensitive ASCII string comparison.
 *
 * Inputs: two NUL-terminated strings. Output: nonzero when equal after
 * storage_lower(). Client: storage_instrumentFilenameMatchesType(), so users
 * can keep uppercase or lowercase 8.3 extensions.
 */
static uint8_t storage_streqNoCase(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (storage_lower(*a) != storage_lower(*b))
            return 0u;
        a++;
        b++;
    }
    return (uint8_t)(*a == '\0' && *b == '\0');
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
    if (storage_streq(text, "drm")) return STORAGE_INSTRUMENT_DRM;
    if (storage_streq(text, "snr")) return STORAGE_INSTRUMENT_SNR;
    if (storage_streq(text, "cym")) return STORAGE_INSTRUMENT_CYM;
    if (storage_streq(text, "hat")) return STORAGE_INSTRUMENT_HAT;
    return STORAGE_INSTRUMENT_UNKNOWN;
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
    const char *ext = NULL;
    uint8_t len = 0u;

    switch (type) {
    case STORAGE_INSTRUMENT_DRM: ext = ".drm"; break;
    case STORAGE_INSTRUMENT_SNR: ext = ".snr"; break;
    case STORAGE_INSTRUMENT_CYM: ext = ".cym"; break;
    case STORAGE_INSTRUMENT_HAT: ext = ".hat"; break;
    default: return 0u;
    }

    while (filename[len] != '\0')
        len++;
    if (len < 4u)
        return 0u;
    return storage_streqNoCase(filename + len - 4u, ext);
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
                                         uint8_t *target_values)
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
        kit->seen_file_mask = (uint8_t)(kit->seen_file_mask | (1u << parsed));
    } else if (storage_streq(key, "audio_out")) {
        st = storage_parseU8(value, &target_values[PAR_AUDIO_OUT1 + parsed]);
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

/* Return the key-to-ParameterArray map for one concrete voice slot.
 *
 * Inputs: instrument type from the file and one-based voice slot from kitset.
 * Outputs: *count receives the number of entries; return value is the matching
 * static map or NULL for an impossible type/slot pairing. Clients are the
 * instrument line parser and the morph fallback copier.
 */
static const storage_param_map_t *storage_paramsForInstrument(storage_instrument_type_t type,
                                                              uint8_t slot,
                                                              uint8_t *count)
{
    switch (type) {
    case STORAGE_INSTRUMENT_DRM:
        if (slot == 1u) {
            *count = (uint8_t)(sizeof(storage_drum1_params) / sizeof(storage_drum1_params[0]));
            return storage_drum1_params;
        }
        if (slot == 2u) {
            *count = (uint8_t)(sizeof(storage_drum2_params) / sizeof(storage_drum2_params[0]));
            return storage_drum2_params;
        }
        if (slot == 3u) {
            *count = (uint8_t)(sizeof(storage_drum3_params) / sizeof(storage_drum3_params[0]));
            return storage_drum3_params;
        }
        break;
    case STORAGE_INSTRUMENT_SNR:
        if (slot == 4u) {
            *count = (uint8_t)(sizeof(storage_snare_params) / sizeof(storage_snare_params[0]));
            return storage_snare_params;
        }
        break;
    case STORAGE_INSTRUMENT_CYM:
        if (slot == 5u) {
            *count = (uint8_t)(sizeof(storage_cymbal_params) / sizeof(storage_cymbal_params[0]));
            return storage_cymbal_params;
        }
        break;
    case STORAGE_INSTRUMENT_HAT:
        if (slot == 6u) {
            *count = (uint8_t)(sizeof(storage_hat_params) / sizeof(storage_hat_params[0]));
            return storage_hat_params;
        }
        break;
    default:
        break;
    }
    *count = 0u;
    return NULL;
}

/* See storageTypes.h for the public contract.
 *
 * The expected type/slot are captured from the already-validated kitset before
 * the file is opened. storage_instrumentParseLine() then checks the file header
 * against these values so a manually swapped instrument is rejected cleanly.
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
 * Metadata lines prove the file type and version. Parameter lines are accepted
 * only inside [params] or [morph]. For known keys, parsed bytes are written
 * directly to the caller's active or morph ParameterArray buffer. Unknown keys
 * are skipped for forward compatibility, so future instrument saves can carry
 * extra fields without making old firmware reject the whole file.
 */
storage_status_t storage_instrumentParseLine(storage_instrument_state_t *state,
                                             const char *line,
                                             uint8_t *target_values,
                                             uint8_t *morph_values)
{
    char key[24];
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
        } else if (storage_streq(key, "slot")) {
            st = storage_parseU8(value, &parsed);
            if (st != STORAGE_STATUS_OK)
                return st;
            if (parsed != state->expected_slot)
                return STORAGE_STATUS_BAD_SLOT;
            state->seen_slot = 1u;
        }
        return STORAGE_STATUS_OK;
    }

    {
        uint8_t count;
        const storage_param_map_t *map =
            storage_paramsForInstrument(state->expected_type,
                                        state->expected_slot,
                                        &count);
        if (map == NULL)
            return STORAGE_STATUS_BAD_SLOT;

        st = storage_parseU8(value, &parsed);
        if (st != STORAGE_STATUS_OK)
            return st;

        for (uint8_t i = 0u; i < count; i++) {
            if (storage_streq(key, map[i].key)) {
                if (state->current_section == STORAGE_SECTION_MORPH) {
                    morph_values[map[i].param] = parsed;
                    state->seen_morph_data = 1u;
                } else {
                    target_values[map[i].param] = parsed;
                    state->seen_param_count++;
                }
                return STORAGE_STATUS_OK;
            }
        }
    }

    return STORAGE_STATUS_OK;
}

/* See storageTypes.h for the public contract.
 *
 * A missing [morph] section is not an error. filesystem.c checks
 * seen_morph_data after this succeeds and calls
 * storage_instrumentCopyMainToMorphFallback() when needed.
 */
storage_status_t storage_instrumentFinalize(const storage_instrument_state_t *state)
{
    if (!state->seen_format || !state->seen_version ||
        !state->seen_type || !state->seen_slot ||
        state->seen_param_count == 0u) {
        return STORAGE_STATUS_MISSING_REQUIRED;
    }
    return STORAGE_STATUS_OK;
}

/* See storageTypes.h for the public contract.
 *
 * The fallback copies only mapped instrument parameters, leaving unrelated
 * ParameterArray bytes untouched. That is important while MIDI note/channel
 * storage is moving to scenes and while morph persistence is deferred to the
 * future save-function pass.
 */
void storage_instrumentCopyMainToMorphFallback(storage_instrument_type_t type,
                                               uint8_t slot,
                                               const uint8_t *main_values,
                                               uint8_t *morph_values)
{
    uint8_t count;
    const storage_param_map_t *map = storage_paramsForInstrument(type, slot, &count);

    if (map == NULL)
        return;

    for (uint8_t i = 0u; i < count; i++)
        morph_values[map[i].param] = main_values[map[i].param];
}
