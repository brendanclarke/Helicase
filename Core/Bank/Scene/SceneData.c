#include "SceneData.h"
#include <string.h>

scene_t scenes[SCENE_COUNT];
static uint8_t scene_active_index;

static uint8_t scene_defaultVoiceAudioOut(uint8_t slot)
{
    /*
     * Preserve the current boot-kit routing defaults without making SceneData
     * depend on DSP mixer headers.
     *
     * Inputs: zero-based resident instrument slot. Output: the retained route
     * byte used when Scene storage has no explicit audio_out line. Values match
     * the current default Slak routing convention: voice 1 routes to output
     * entry 2, voices 2..5 to entry 0, and voice 6 to entry 1.
     */
    if (slot == 0u)
        return 2u;
    if (slot == 5u)
        return 1u;
    return 0u;
}

uint8_t scene_indexValid(uint8_t scene_index)
{
    /*
     * Validate a resident Scene index.
     *
     * Input: candidate index. Output: nonzero when the index addresses the
     * current scenes[] allocation. Keeping this helper even while SCENE_COUNT
     * is one lets future Bank work expand the allocation without rewriting
     * callers that currently only need bounds safety.
     */
    return (uint8_t)(scene_index < SCENE_COUNT);
}

scene_t *scene_get(uint8_t scene_index)
{
    /*
     * Borrow mutable Scene storage.
     *
     * Input: resident Scene index. Output: pointer to scenes[index] or NULL for
     * invalid input. Affiliates are Preset, filesystem load/apply, PatternData,
     * and future Scene/Bank file code; all use this central bounds check rather
     * than indexing the global array directly.
     */
    return scene_indexValid(scene_index) ? &scenes[scene_index] : 0;
}

const scene_t *scene_getConst(uint8_t scene_index)
{
    /*
     * Borrow read-only Scene storage.
     *
     * Input: resident Scene index. Output: const pointer or NULL. Menu,
     * InstrumentManager, and runtime apply code use this when they need current
     * Scene metadata without taking ownership of retained data writes.
     */
    return scene_indexValid(scene_index) ? &scenes[scene_index] : 0;
}

uint8_t scene_getActiveIndex(void)
{
    /*
     * Return the active Scene index.
     *
     * Inputs: none. Output: current active resident Scene index. This remains
     * an accessor rather than a public global so Phase 3/4 Bank work can change
     * scene selection/apply policy behind one boundary.
     */
    return scene_active_index;
}

uint8_t scene_selectActive(uint8_t scene_index)
{
    /*
     * Selection changes identity, never data.
     *
     * Input is a resident Scene index; output reports acceptance. DSP apply is
     * deliberately owned by Preset so selecting a record cannot unexpectedly
     * perform a large foreground update.
     */
    if (!scene_indexValid(scene_index))
        return 0u;
    scene_active_index = scene_index;
    return 1u;
}

kit_instrument_slot_t *scene_instrumentSlot(uint8_t scene_index, uint8_t slot)
{
    scene_t *scene = scene_get(scene_index);
    /*
     * Borrow one mutable Kit instrument slot.
     *
     * Inputs: Scene index and zero-based slot. Output: pointer to the slot or
     * NULL for invalid Scene/slot. Filesystem Kit load, Preset setters, and
     * future instrument-swap operations use this as the safe write boundary for
     * swappable instrument storage.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0;
    return &scene->kit.instruments[slot];
}

const kit_instrument_slot_t *scene_instrumentSlotConst(uint8_t scene_index,
                                                       uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);
    /*
     * Borrow one read-only Kit instrument slot.
     *
     * Inputs: Scene index and zero-based slot. Output: const pointer or NULL.
     * Menu and InstrumentManager use this to resolve current slot type and
     * descriptor images without mutating Scene data.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0;
    return &scene->kit.instruments[slot];
}

static void scene_copyDisplayName(
    char dst[SCENE_OBJECT_DISPLAY_NAME_LEN + 1u],
    const char name[SCENE_OBJECT_DISPLAY_NAME_LEN])
{
    uint8_t i;

    /*
     * Normalize a retained object display name.
     *
     * Inputs are fixed-width Save/Load display bytes, not NUL-terminated C
     * strings. Output is the same eight-cell field plus NUL for resident
     * SceneData storage. Callers deliberately pass the product default
     * "none    " for uninitialized objects; the UI sentinel `Empty` remains
     * reserved for absent library slots reported by filesystem/menu scan
     * caches.
     */
    if (!dst)
        return;
    for (i = 0u; i < SCENE_OBJECT_DISPLAY_NAME_LEN; i++) {
        char c = name ? name[i] : ' ';
        dst[i] = (c >= 0x20 && c <= 0x7e) ? c : ' ';
    }
    dst[SCENE_OBJECT_DISPLAY_NAME_LEN] = '\0';
}

void scene_setKitDisplayName(
    kit_t *kit,
    const char name[SCENE_OBJECT_DISPLAY_NAME_LEN])
{
    /*
     * Store a resident Kit name without touching sound data.
     *
     * Inputs: a Kit record and eight display bytes. Output: kit->display_name
     * updates only when the Kit pointer is valid. This is used after normal
     * load/save completion; Morph paths intentionally do not call it.
     */
    if (!kit)
        return;
    scene_copyDisplayName(kit->display_name, name);
}

void scene_setResidentKitDisplayName(
    uint8_t scene_index,
    const char name[SCENE_OBJECT_DISPLAY_NAME_LEN])
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store the active resident Kit identity by Scene coordinate.
     *
     * Inputs: resident Scene index plus eight display bytes. Output: the
     * embedded Kit's retained name changes without modifying instrument slots,
     * routing, or Morph endpoints.
     */
    if (!scene)
        return;
    scene_setKitDisplayName(&scene->kit, name);
}

void scene_setSceneDisplayName(
    uint8_t scene_index,
    const char name[SCENE_OBJECT_DISPLAY_NAME_LEN])
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one resident Scene identity.
     *
     * Inputs: resident Scene index plus eight display bytes. Output:
     * scene->display_name updates only for valid coordinates. This separates
     * resident Scene naming from root Scene slot occupancy display.
     */
    if (!scene)
        return;
    scene_copyDisplayName(scene->display_name, name);
}

const char *scene_kitDisplayName(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Borrow the retained Kit name for Save editor seeding.
     *
     * Output is always an eight-character NUL-terminated string. Invalid Scene
     * indices return a blank resident name, not the library-slot word `Empty`.
     */
    return scene ? scene->kit.display_name : "        ";
}

const char *scene_sceneDisplayName(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Borrow the retained Scene name for Save editor seeding.
     *
     * Output is always an eight-character NUL-terminated string. Invalid Scene
     * indices return a blank resident name because absent-slot text belongs to
     * filesystem scan caches, not SceneData.
     */
    return scene ? scene->display_name : "        ";
}

static void scene_copyInstrumentSourceName(kit_t *kit, uint8_t slot,
                                           const char *filename_or_stem)
{
    uint8_t i = 0u;

    /*
     * Normalize one instrument source name into Scene-owned metadata.
     *
     * Inputs may be a filename with extension or a raw stem. Output updates the
     * 16-character save stem and the eight-character LCD field together. The
     * copy stops before '.' so `.drm`/`.snr`/`.cym`/`.hat` extensions do not
     * become part of either retained name.
     */
    if (!kit || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (!filename_or_stem)
        filename_or_stem = "";
    memset(kit->instrument_stem[slot], 0,
           sizeof(kit->instrument_stem[slot]));
    memset(kit->instrument_display_name[slot], ' ', 8u);
    while (filename_or_stem[i] != '\0' &&
           filename_or_stem[i] != '.' &&
           i < SCENE_INSTRUMENT_STEM_LEN) {
        char c = filename_or_stem[i];
        if (c < 32 || c > 126)
            c = '_';
        kit->instrument_stem[slot][i] = c;
        if (i < 8u)
            kit->instrument_display_name[slot][i] = c;
        i++;
    }
    if (i == 0u) {
        /*
         * Empty source names serialize as the product default, not as a debug
         * placeholder.
         *
         * Inputs: NULL, empty, or extension-only stems. Output: the retained
         * stem becomes "none"; save filename formatting later pads that stem
         * to character 8 and appends the voice number, producing names such as
         * `none   1.drm`.
         */
        memcpy(kit->instrument_stem[slot], "none", 4u);
        memcpy(kit->instrument_display_name[slot], "none    ", 8u);
    }
    kit->instrument_stem[slot][SCENE_INSTRUMENT_STEM_LEN] = '\0';
    kit->instrument_display_name[slot][8] = '\0';
}

void scene_setKitInstrumentSourceName(kit_t *kit, uint8_t slot,
                                      const char *filename_or_stem)
{
    /*
     * Retain Kit-member source metadata while staging or editing a Kit.
     *
     * Inputs: caller-owned Kit storage, zero-based slot, and source filename or
     * stem. Output: both display and save names are updated inside that Kit.
     * Filesystem uses this while parsing into staging before a Scene exists.
     */
    scene_copyInstrumentSourceName(kit, slot, filename_or_stem);
}

void scene_setInstrumentSourceName(uint8_t scene_index, uint8_t slot,
                                   const char *filename_or_stem)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Retain one resident instrument source stem for later Kit Save.
     *
     * Inputs: resident Scene/slot plus filename or raw stem. Output: Scene kit
     * metadata updates only when coordinates are valid. The DSP instrument
     * images are not touched because source names are storage/UI metadata.
     */
    if (!scene)
        return;
    scene_copyInstrumentSourceName(&scene->kit, slot, filename_or_stem);
}

void scene_setTrackMidiChannel(uint8_t scene_index, uint8_t track,
                               uint8_t channel)
{
    scene_t *scene = scene_get(scene_index);
    /*
     * Store a track MIDI channel in Scene settings.
     *
     * Inputs: Scene index, track index, and requested channel. Output: valid
     * coordinates store a clamped 1..16 value. This remains a Scene setting so
     * Kit/instrument changes do not rewrite MIDI assignment.
     */
    if (!scene || track >= NUM_TRACKS)
        return;
    if (channel < 1u)
        channel = 1u;
    else if (channel > 16u)
        channel = 16u;
    scene->settings.midi_channel[track] = channel;
}

uint8_t scene_getTrackMidiChannel(uint8_t scene_index, uint8_t track)
{
    const scene_t *scene = scene_getConst(scene_index);
    uint8_t channel;
    /*
     * Read a track MIDI channel from Scene settings.
     *
     * Inputs: Scene index and track index. Output: valid stored 1..16 channel,
     * track+1 fallback for unset/stale values, or 1 for invalid coordinates.
     * The fallback preserves current bridge defaults until MIDI off/channel
     * policy is redesigned in Phase 5.
     */
    if (!scene || track >= NUM_TRACKS)
        return 1u;
    channel = scene->settings.midi_channel[track];
    return (channel >= 1u && channel <= 16u)
        ? channel
        : (uint8_t)(track + 1u);
}

void scene_setTrackMidiNote(uint8_t scene_index, uint8_t track, uint8_t note)
{
    scene_t *scene = scene_get(scene_index);
    /*
     * Store a track MIDI note in Scene settings.
     *
     * Inputs: Scene index, track index, and note value. Output: valid
     * coordinates store a 0..127 note, clamping out-of-range input to 127. This
     * setting is track/Scene data, not instrument-file data.
     */
    if (!scene || track >= NUM_TRACKS)
        return;
    scene->settings.midi_note[track] = (note <= 127u) ? note : 127u;
}

uint8_t scene_getTrackMidiNote(uint8_t scene_index, uint8_t track)
{
    const scene_t *scene = scene_getConst(scene_index);
    /*
     * Read a track MIDI note from Scene settings.
     *
     * Inputs: Scene index and track index. Output: stored 0..127 note or 0 for
     * invalid/stale data. Menu and MIDI callers use this to keep the current
     * Scene storage policy centralized.
     */
    if (!scene || track >= NUM_TRACKS)
        return 0u;
    return (scene->settings.midi_note[track] <= 127u)
        ? scene->settings.midi_note[track]
        : 0u;
}

void scene_setVoiceMorphAmount(uint8_t scene_index, uint8_t slot,
                               uint8_t amount)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one Scene-retained per-slot Morph amount.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and 0..255
     * amount. Output: only the selected slot's setting is updated. Clients are
     * Preset's PERF/MIDI Morph setters and future sceneset.scg load. This
     * cannot be folded into those callers because SceneData owns validity and
     * indexing for the retained Scene record.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    scene->settings.voice_morph_amount[slot] = amount;
}

uint8_t scene_getVoiceMorphAmount(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Read one Scene-retained per-slot Morph amount.
     *
     * Inputs: resident Scene index and zero-based instrument slot. Output:
     * stored 0..255 amount, or 0 for invalid coordinates so a bad caller cannot
     * index outside the Scene settings. Clients are Preset endpoint refresh and
     * the Morph worker's pass snapshot.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    return scene->settings.voice_morph_amount[slot];
}

void scene_setAllVoiceMorphAmounts(uint8_t scene_index, uint8_t amount)
{
    scene_t *scene = scene_get(scene_index);
    uint8_t slot;

    /*
     * Bulk-store the six per-slot Morph amounts for overall PERF Morph.
     *
     * Inputs: resident Scene index and 0..255 amount. Output: every instrument
     * slot's Morph setting is updated while instrument endpoint images remain
     * untouched. This exists separately from the single-slot setter because the
     * global Morph control is semantically a six-slot set operation, not a
     * second Morph engine or a derived average.
     */
    if (!scene)
        return;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        scene->settings.voice_morph_amount[slot] = amount;
}

void scene_setVoiceAudioOut(uint8_t scene_index, uint8_t slot,
                            uint8_t route)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one Scene-retained output route.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and route byte
     * in the current 0..5 mixer menu domain. Output: retained Scene mix state
     * changes only for valid coordinates. DSP-route validation happens in
     * Preset because SceneData intentionally does not include mixer.h.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (route > 5u)
        route = scene_defaultVoiceAudioOut(slot);
    scene->settings.audio_out[slot] = route;
}

uint8_t scene_getVoiceAudioOut(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Read one Scene-retained output route.
     *
     * Inputs: resident Scene index and zero-based instrument slot. Output:
     * stored 0..5 route, or the default route for invalid/uninitialized data.
     * The fallback keeps UI display and Preset apply stable while old Scene
     * files without audio_out lines are still accepted.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT)
        return scene_defaultVoiceAudioOut(0u);
    if (!scene || scene->settings.audio_out[slot] > 5u)
        return scene_defaultVoiceAudioOut(slot);
    return scene->settings.audio_out[slot];
}

void scene_setVoiceFxSendAmount(uint8_t scene_index, uint8_t slot,
                                uint8_t amount)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one future FX-send amount in Scene settings.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and amount.
     * Output: retained 0..127 amount. Runtime FX send is intentionally not
     * applied here; Preset owns runtime side effects when the FX bus exists.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (amount > 127u)
        amount = 127u;
    scene->settings.fx_send_amount[slot] = amount;
}

uint8_t scene_getVoiceFxSendAmount(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Read one future FX-send amount.
     *
     * Inputs: resident Scene index and zero-based instrument slot. Output:
     * retained 0..127 value, or 0 for invalid coordinates/stale storage.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT ||
        scene->settings.fx_send_amount[slot] > 127u) {
        return 0u;
    }
    return scene->settings.fx_send_amount[slot];
}

void scene_setVoiceFaderSetting(uint8_t scene_index, uint8_t slot,
                                uint8_t mode)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one future per-voice fader mode.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and mode in
     * the current Scene file domain: 0 normal/pre-FX, 1 post-FX, 2 FX-only.
     * Output: retained mode. Runtime behavior is intentionally deferred to
     * Preset/future mixer code rather than being hidden in SceneData.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (mode > 2u)
        mode = 2u;
    scene->settings.fader_setting[slot] = mode;
}

uint8_t scene_getVoiceFaderSetting(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Read one retained fader mode.
     *
     * Inputs: resident Scene index and zero-based instrument slot. Output:
     * retained 0..2 mode, or 0 for invalid coordinates/stale storage.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT ||
        scene->settings.fader_setting[slot] > 2u) {
        return 0u;
    }
    return scene->settings.fader_setting[slot];
}

void scene_setSlot6Track7AmpEnvelopeDecay(uint8_t scene_index, uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store the generated slot-6 track-7 base decay endpoint.
     *
     * Inputs: resident Scene index and a 0..127 menu-domain decay value.
     * Output: Kit settings retain the value used when track 7 triggers a
     * non-Choke instrument assigned to slot 6. This cannot be folded into the
     * descriptor-image setters because the value is not part of any instrument
     * file and has no descriptor index.
     */
    if (!scene)
        return;
    if (value > 127u)
        value = 127u;
    scene->kit.settings.slot6_track7_amp_envelope_decay = value;
}

uint8_t scene_getSlot6Track7AmpEnvelopeDecay(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Read the generated slot-6 track-7 base decay endpoint.
     *
     * Input: resident Scene index. Output: retained 0..127 value, or 0 for an
     * invalid Scene. Clients are Menu display, Preset apply, storage, and the
     * track-7 trigger path.
     */
    return scene ? scene->kit.settings.slot6_track7_amp_envelope_decay : 0u;
}

void scene_setSlot6Track7MorphAmpEnvelopeDecay(uint8_t scene_index,
                                               uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store the generated slot-6 track-7 Morph decay endpoint.
     *
     * Inputs: resident Scene index and 0..127 value. Output: Kit settings
     * retain the Morph-side endpoint for the generated non-Choke track-7 decay
     * parameter. It stays separate from Scene voice_morph_amount[], which is
     * the interpolation amount rather than an endpoint.
     */
    if (!scene)
        return;
    if (value > 127u)
        value = 127u;
    scene->kit.settings.slot6_track7_morph_amp_envelope_decay = value;
}

uint8_t scene_getSlot6Track7MorphAmpEnvelopeDecay(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);

    /*
     * Read the generated slot-6 track-7 Morph decay endpoint.
     *
     * Input: resident Scene index. Output: retained 0..127 Morph endpoint, or
     * 0 for invalid scenes. Preset/Morph code uses this with the slot-6 voice
     * Morph amount to derive the runtime generated decay.
     */
    return scene ? scene->kit.settings.slot6_track7_morph_amp_envelope_decay
                 : 0u;
}

void scene_initAll(void)
{
    uint8_t scene_index;
    uint8_t track;
    static const instrument_type_t initial_types[INSTRUMENT_SLOT_COUNT] = {
        INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM,
        INSTRUMENT_TYPE_SNR, INSTRUMENT_TYPE_CYM, INSTRUMENT_TYPE_HAT
    };

    /*
     * Initialize each complete resident owner.
     *
     * Processing clears stale bytes, establishes safe Scene settings, and
     * resets all six slots through descriptors, then asks PatternData to apply
     * its step/track defaults inside the same Scene record.
     */
    memset(scenes, 0, sizeof(scenes));
    scene_active_index = 0u;
    for (scene_index = 0u; scene_index < SCENE_COUNT; scene_index++) {
        scenes[scene_index].settings.voice_decimation_all = 127u;
        for (track = 0u; track < NUM_TRACKS; track++)
            scenes[scene_index].settings.midi_channel[track] =
                (uint8_t)(track + 1u);
        for (track = 0u; track < INSTRUMENT_SLOT_COUNT; track++) {
            /*
             * Initialize Scene-owned per-voice mix settings before any SD
             * load. These are separate from instrument reset because changing
             * the instrument type in a slot must not reset Scene mix state.
             */
            scenes[scene_index].settings.audio_out[track] =
                scene_defaultVoiceAudioOut(track);
            scenes[scene_index].settings.fx_send_amount[track] = 0u;
            scenes[scene_index].settings.fader_setting[track] = 0u;
        }
        /*
         * Initialize retained object names to the universal uninitialized name.
         *
         * "none" is the resident object default. The visible word `Empty`
         * belongs only to missing library slots reported by filesystem/menu
         * scan caches, so saving from SRAM defaults produces `Scene none`,
         * `Kit none`, and `none   N.ext` rather than blank or ad hoc names.
         */
        scene_setSceneDisplayName(scene_index, "none    ");
        scene_setResidentKitDisplayName(scene_index, "none    ");
        for (track = 0u; track < INSTRUMENT_SLOT_COUNT; track++)
            instrumentManager_resetSlot(
                &scenes[scene_index].kit.instruments[track],
                initial_types[track]);
        /*
         * Keep source metadata defined before any SD load.
         *
         * Defaults use the same `none` stem for every voice.
         *
         * The save filename writer owns the voice suffix, so a single stem
         * produces `none   1.drm` through `none   6.hat` without duplicating
         * slot math here.
         */
        for (track = 0u; track < INSTRUMENT_SLOT_COUNT; track++) {
            char fallback[5] = { 'n', 'o', 'n', 'e', '\0' };
            scene_setKitInstrumentSourceName(&scenes[scene_index].kit,
                                             track, fallback);
        }
        pat_initScene(scene_index);
    }
}
