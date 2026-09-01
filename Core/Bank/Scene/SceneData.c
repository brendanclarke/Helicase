#include "SceneData.h"
#include "Autosave.h"
#include "BankData.h"
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

/*
 * Commit one Scene-settings byte and notify its shared wire index on change.
 *
 * Inputs: owning Scene, address of its scalar byte, named Autosave parameter
 * index, and already-normalized value. Output: storage changes first and then
 * exactly that bit is marked; invalid coordinates/pointers and equal values do
 * nothing. Why: all 40 Scene fields need one future-proof mutation boundary.
 * Affiliates: Scene setters below and Autosave's Scene getter/count contract.
 */
static void scene_storeParameterByte(uint8_t scene_index,
                                     uint8_t *storage,
                                     uint8_t parameter_index,
                                     uint8_t value)
{
    if (!scene_get(scene_index) || !storage || *storage == value)
        return;
    *storage = value;
    autosave_markSceneParameterDirty(scene_index, parameter_index);
    /*
     * Option 2: a committed Scene byte invalidates that Scene's card-clean bit.
     *
     * Input: owning Scene index, after an equal-value no-op was already
     * rejected. Output: the Scene may no longer be skipped by a subsequent Bank
     * Save until a successful Bank Load/Save on this mount proves it again.
     * This is intentionally beside the Autosave marker, not folded into it:
     * Autosave tracks byte persistence for another file, while card-clean
     * tracks equality to an on-card Bank child. Affiliates:
     * bank_invalidateSdCleanScene(), filesystem Bank Save skip logic.
     */
    bank_invalidateSdCleanScene(scene_index);
}

/*
 * Commit one Kit-settings byte and notify its shared wire index on change.
 *
 * Inputs: owning Scene, Kit scalar address, named Kit parameter index, and
 * normalized byte. Output: changed storage is written before its dirty bit;
 * invalid/equal inputs are no-ops. Why: generated Kit settings must not bypass
 * the same coalescing protocol as Scene and Instrument parameters. Affiliates:
 * the two track-7 decay setters and Autosave's Kit getter/count contract.
 */
static void scene_storeKitParameterByte(uint8_t scene_index,
                                        uint8_t *storage,
                                        uint8_t parameter_index,
                                        uint8_t value)
{
    if (!scene_get(scene_index) || !storage || *storage == value)
        return;
    *storage = value;
    autosave_markKitParameterDirty(scene_index, parameter_index);
    /*
     * Option 2: a committed Kit byte invalidates the owning Scene's card-clean
     * bit, mirroring the Scene-settings funnel above. Same source/authority
     * separation from Autosave; an equal-value no-op never reaches this line.
     */
    bank_invalidateSdCleanScene(scene_index);
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

void scene_setTrackMidiChannel(uint8_t scene_index, uint8_t track,
                               uint8_t channel)
{
    scene_t *scene = scene_get(scene_index);
    /*
     * Store a track MIDI channel in Scene settings.
     *
     * Inputs: Scene index, track index, and requested channel. Output: valid
     * coordinates store a clamped 1..16 value. A changed byte then marks the
     * track's named Scene parameter; an equal final channel is a no-op. This
     * remains a Scene setting so Kit/instrument changes do not rewrite MIDI
     * assignment. Affiliate: scene_storeParameterByte().
     */
    if (!scene || track >= NUM_TRACKS)
        return;
    if (channel < 1u)
        channel = 1u;
    else if (channel > 16u)
        channel = 16u;
    scene_storeParameterByte(
        scene_index, &scene->settings.midi_channel[track],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE + track), channel);
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
    uint8_t normalized_note;
    /*
     * Store a track MIDI note in Scene settings.
     *
     * Inputs: Scene index, track index, and note value. Output: valid
     * coordinates store a 0..127 note, clamping out-of-range input to 127. A
     * changed final byte marks the track's named Scene parameter after storage;
     * an equal note is a no-op. This setting is track/Scene data, not
     * instrument-file data. Affiliate: scene_storeParameterByte().
     */
    if (!scene || track >= NUM_TRACKS)
        return;
    normalized_note = (note <= 127u) ? note : 127u;
    scene_storeParameterByte(
        scene_index, &scene->settings.midi_note[track],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_MIDI_NOTE_BASE + track),
        normalized_note);
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

void scene_setMorphAmount(uint8_t scene_index, uint8_t amount)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store the Scene's overall Morph control through its serialized owner.
     *
     * Inputs: resident Scene and 0..255 amount. Output: changed retained value
     * is written and its single named dirty bit is set; runtime interpolation
     * remains Preset-owned. Why: Preset previously assigned this field directly
     * and could bypass autosave. Affiliates: preset_morphScene() and the six
     * separate voice Morph setters.
     */
    if (!scene)
        return;
    scene_storeParameterByte(
        scene_index, &scene->settings.morph_amount,
        AUTOSAVE_SCENE_PARAM_MORPH_AMOUNT, amount);
}

void scene_setVoiceDecimationAll(uint8_t scene_index, uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store Scene-wide decimation through its serialized owner boundary.
     *
     * Inputs: resident Scene and already-clamped 0..127 value. Output: changed
     * retained state and the decimation dirty bit; DSP/mirror apply stays in
     * Preset. Why: direct Preset assignment was the other Scene scalar hole.
     * Affiliate: preset_setVoiceDecimationAll().
     */
    if (!scene)
        return;
    scene_storeParameterByte(
        scene_index, &scene->settings.voice_decimation_all,
        AUTOSAVE_SCENE_PARAM_DECIMATION_ALL, value);
}

void scene_setVoiceMorphAmount(uint8_t scene_index, uint8_t slot,
                               uint8_t amount)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one Scene-retained per-slot Morph amount.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and 0..255
     * amount. Output: only a changed selected-slot setting is updated, followed
     * by its named Scene dirty marker. Clients are Preset's PERF/MIDI Morph
     * setters and future sceneset.scg load. This
     * cannot be folded into those callers because SceneData owns validity and
     * indexing for the retained Scene record.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    scene_storeParameterByte(
        scene_index, &scene->settings.voice_morph_amount[slot],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_VOICE_MORPH_BASE + slot), amount);
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
     * second Morph engine or a derived average. Calling the scalar owner setter
     * for each slot preserves per-byte comparison and dirty notification.
     */
    if (!scene)
        return;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        scene_setVoiceMorphAmount(scene_index, slot, amount);
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
     * changes only for valid coordinates; a changed final route is stored then
     * marked at its named Scene index. DSP-route validation happens in Preset
     * because SceneData intentionally does not include mixer.h.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (route > 5u)
        route = scene_defaultVoiceAudioOut(slot);
    scene_storeParameterByte(
        scene_index, &scene->settings.audio_out[slot],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_AUDIO_OUT_BASE + slot), route);
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
     * Output: a changed retained 0..127 amount is stored before its named Scene
     * bit is marked. Runtime FX send is intentionally not applied here; Preset
     * owns runtime side effects when the FX bus exists.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (amount > 127u)
        amount = 127u;
    scene_storeParameterByte(
        scene_index, &scene->settings.fx_send_amount[slot],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_FX_SEND_BASE + slot), amount);
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
     * Output: a changed retained mode is stored before its named Scene bit is
     * marked. Runtime behavior is intentionally deferred to Preset/future mixer
     * code rather than being hidden in SceneData.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (mode > 2u)
        mode = 2u;
    scene_storeParameterByte(
        scene_index, &scene->settings.fader_setting[slot],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_FADER_BASE + slot), mode);
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
     * file and has no descriptor index. A changed normalized byte is stored
     * before the named Kit parameter is marked; equal values do nothing.
     */
    if (!scene)
        return;
    if (value > 127u)
        value = 127u;
    scene_storeKitParameterByte(
        scene_index,
        &scene->kit.settings.slot6_track7_amp_envelope_decay,
        AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_DECAY, value);
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
     * the interpolation amount rather than an endpoint. A changed normalized
     * byte is stored before the named Kit parameter is marked; equal values do
     * nothing.
     */
    if (!scene)
        return;
    if (value > 127u)
        value = 127u;
    scene_storeKitParameterByte(
        scene_index,
        &scene->kit.settings.slot6_track7_morph_amp_envelope_decay,
        AUTOSAVE_KIT_PARAM_SLOT6_TRACK7_MORPH_DECAY, value);
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

static void scene_resetOneInternal(uint8_t scene_index)
{
    uint8_t track;
    static const instrument_type_t initial_types[INSTRUMENT_SLOT_COUNT] = {
        INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM,
        INSTRUMENT_TYPE_SNR, INSTRUMENT_TYPE_CYM, INSTRUMENT_TYPE_HAT
    };

    memset(&scenes[scene_index], 0, sizeof(scenes[scene_index]));
    scenes[scene_index].settings.voice_decimation_all = 127u;
    for (track = 0u; track < NUM_TRACKS; track++)
        scenes[scene_index].settings.midi_channel[track] =
            (uint8_t)(track + 1u);
    for (track = 0u; track < INSTRUMENT_SLOT_COUNT; track++) {
        /* Scene-owned mix defaults remain independent of Instrument reset. */
        scenes[scene_index].settings.audio_out[track] =
            scene_defaultVoiceAudioOut(track);
        scenes[scene_index].settings.fx_send_amount[track] = 0u;
        scenes[scene_index].settings.fader_setting[track] = 0u;
    }
    for (track = 0u; track < INSTRUMENT_SLOT_COUNT; track++)
        instrumentManager_resetSlot(
            &scenes[scene_index].kit.instruments[track],
            initial_types[track]);
    pat_initScene(scene_index);
}

void scene_resetOne(uint8_t scene_index)
{
    /* Boot reconstruction uses this isolated default reset after one source
     * component fails; no Bank presence or active-scene state is touched. */
    if (scene_index >= SCENE_COUNT)
        return;
    scene_resetOneInternal(scene_index);
}

void scene_initAll(void)
{
    uint8_t scene_index;

    /* Keep whole-array initialization byte-identical while sharing the
     * single-Scene reset needed by HCNAMES reconstruction. */
    memset(scenes, 0, sizeof(scenes));
    scene_active_index = 0u;
    for (scene_index = 0u; scene_index < SCENE_COUNT; scene_index++)
        scene_resetOneInternal(scene_index);
}
