#include "SceneData.h"
#include "Autosave.h"
#include "BankData.h"
#include "MidiPitch.h"
#include "MidiDefs.h"
#include "InstrumentManager.h"   /* S104 P1: the track-7 decay row mapping */
#include <string.h>

/*
 * Guard the packed target-ID boundary at the retained-data owner.
 *
 * SceneData sees InstrumentManager's voice range, PatternData's off sentinel,
 * and EffectTypes' block-7 constants. These assertions turn a future range
 * change into a build failure instead of silently retargeting stored Patterns.
 */
_Static_assert(EFFECT_TARGET_ID_BASE == INSTRUMENT_VOICE_ID_COUNT + 64u,
               "Scene block must precede the Effect block");
_Static_assert(EFFECT_TARGET_ID_BASE + EFFECT_TARGET_ID_COUNT ==
                   INSTRUMENT_TOTAL_ID_COUNT,
               "Effect block must end the 9-bit target space");
_Static_assert(PAT_AUTOMATION_TARGET_OFF ==
                   EFFECT_TARGET_ID_BASE + EFFECT_TARGET_PATTERN_LOCAL_LIMIT,
               "Effect local 63 must alias the automation-off sentinel");
_Static_assert(EFFECT_COMMON_PARAM_COUNT < EFFECT_PARAM_COUNT,
               "common Effect parameters must leave type-specific cells");

scene_t scenes[SCENE_COUNT];
static uint8_t scene_active_index;

/*
 * Scene 17: the background Scene of a running Bank load (S082 P1).
 *
 * What: one scene_t outside scenes[16] (index SCENE_BACKGROUND_INDEX), plus an
 * exists flag and the index of the Scene that was active when it was built.
 * scene_get()/scene_getConst()/scene_selectActive() address it as index 16
 * while it exists; scene_indexValid() stays 0..15, so storage, AutoSave,
 * HCNAMES and the 16-bit Bank masks never see it.
 * Why: playback continues unchanged from Scene 17 while Scenes 1-16 are
 * replaced (D1-D5); Scene 17 is otherwise a normal, editable Scene (D9, D15).
 * SRAM: 1,656 B + 2 B, SRAM1 static, owner SceneData (accepted 2026-10-17).
 * Lifetime: exists from the switch (C6) until a whole-Scene change (C8).
 * Accessors: scene_background*(), scene_get(), scene_getConst().
 * Affiliates: pat_background_region (its Pattern), seq_switchToBackgroundScene(),
 * preset_buildBackgroundScene(), the S082 P1 §5 audit (C3).
 */
static scene_t scene_background;
static uint8_t scene_backgroundFlags;
static uint8_t scene_backgroundHome;

static uint8_t scene_defaultVoiceAudioOut(uint8_t slot)
{
    /*
     * Default Scene route of a voice (S075 F2-A, user decision F2-Q1).
     *
     * What: route 0 (St1, MenuText.h route ids: 0 St1, 1 St2, 2 L1, 3 R1,
     * 4 L2, 5 R2) for every instrument slot. It replaces the old boot-Kit
     * convention (voice 1 -> L1, voice 6 -> St2), which made a cleared Scene
     * come up with voices 1 and 6 on unexpected outputs. Why: one default on
     * every path: fresh Scenes, clear scene/settings, and invalid route
     * fallbacks. Inputs: zero-based slot; kept so callers do not change.
     * Output: 0. Affiliates: filesystem_defaultVoiceAudioOut() and
     * preset_applyKitAudioRouting().
     */
    (void)slot;
    return 0u;
}

/*
 * Commit one Scene-settings byte and notify its shared wire index on change.
 *
 * Inputs: owning Scene, address of its scalar byte, named Autosave parameter
 * index, and already-normalized value. Output: storage changes first and then
 * exactly that bit is marked; invalid coordinates/pointers and equal values do
 * nothing. Why: all retained Scene fields need one future-proof mutation
 * boundary.
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

/*
 * Commit one retained Effect byte and notify its ordered AutoSave cell.
 *
 * Inputs: owning Scene, byte address, Effect-relative live index, and an
 * already-normalized value. Output: storage changes first, then exactly that
 * Effect bit and the Scene card-clean bit are updated. Invalid pointers and
 * equal values do nothing. The uint16_t index is required by the 419-cell
 * Effect wire space; it must never be folded into the Scene byte helper.
 */
static void scene_storeEffectByte(uint8_t scene_index,
                                  uint8_t *storage,
                                  uint16_t parameter_index,
                                  uint8_t value)
{
    if (!scene_get(scene_index) || !storage || *storage == value)
        return;
    *storage = value;
    autosave_markEffectParameterDirty(scene_index, parameter_index);
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
    if (scene_index == SCENE_BACKGROUND_INDEX && scene_backgroundFlags)
        return &scene_background;
    return scene_indexValid(scene_index) ? &scenes[scene_index] : 0;
}

const scene_t *scene_getConst(uint8_t scene_index)
{
    if (scene_index == SCENE_BACKGROUND_INDEX && scene_backgroundFlags)
        return &scene_background;
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
    if (scene_index == SCENE_BACKGROUND_INDEX && scene_backgroundFlags) {
        scene_active_index = scene_index;
        return 1u;
    }
    if (!scene_indexValid(scene_index))
        return 0u;
    scene_active_index = scene_index;
    return 1u;
}

uint8_t scene_backgroundExists(void)
{
    return scene_backgroundFlags;
}

uint8_t scene_backgroundHomeIndex(void)
{
    return (scene_active_index < SCENE_COUNT)
        ? scene_active_index : scene_backgroundHome;
}

uint8_t scene_playableIndexValid(uint8_t scene_index)
{
    if (scene_index < SCENE_COUNT)
        return 1u;
    return (uint8_t)(scene_index == SCENE_BACKGROUND_INDEX &&
                     scene_backgroundFlags);
}

void scene_backgroundBegin(uint8_t home_index)
{
    scene_backgroundHome = home_index;
    scene_backgroundFlags = 1u;
}

void scene_backgroundRelease(void)
{
    scene_backgroundFlags = 0u;
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

/*
 * Compare the retained Effect and six Instrument types used by edit masks.
 *
 * Inputs: two resident Scene indices. Output: nonzero only when both records
 * exist and their local parameter/target layouts match. This read-only
 * predicate is the shared rule for Menu's mask-selection gate and BankData's
 * post-load/type-change repair; it intentionally does not mark AutoSave.
 */
uint8_t scene_editLayoutMatches(uint8_t scene_a, uint8_t scene_b)
{
    const scene_t *a = scene_getConst(scene_a);
    const scene_t *b = scene_getConst(scene_b);
    uint8_t slot;

    if (!a || !b)
        return 0u;
    if (scene_a == scene_b)
        return 1u;
    if (a->effect.type != b->effect.type)
        return 0u;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        if (a->kit.instruments[slot].type != b->kit.instruments[slot].type)
            return 0u;
    }
    return 1u;
}

void scene_setTrackMidiChannel(uint8_t scene_index, uint8_t track,
                               uint8_t channel)
{
    scene_t *scene = scene_get(scene_index);
    /*
     * Store a track MIDI channel in Scene settings (S083).
     *
     * Inputs: Scene index, track, channel 0..17: 0 = `glb` (the track
     * follows G), 1..16, MIDI_TRACK_CHANNEL_OFF (17) = `off` (no MIDI
     * channel at all; S083 follow-up). Values above 17 clamp to `off`.
     * Output: a changed byte marks the track's AutoSave Scene cell. Affiliates: scene_storeParameterByte(),
     * menu_parseGlobalParam(PAR_TRACK_MIDI_CHAN), AutoSave restore.
     */
    if (!scene || track >= NUM_TRACKS)
        return;
    if (channel > MIDI_TRACK_CHANNEL_OFF)
        channel = MIDI_TRACK_CHANNEL_OFF;
    scene_storeParameterByte(
        scene_index, &scene->settings.midi_channel[track],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_MIDI_CHANNEL_BASE + track), channel);
}

uint8_t scene_getTrackMidiChannel(uint8_t scene_index, uint8_t track)
{
    const scene_t *scene = scene_getConst(scene_index);
    uint8_t channel;
    /*
     * Read a track MIDI channel (S083).
     *
     * Output: 1..16; 0 when the track is unassigned (`glb`, it follows G),
     * the coordinates are invalid or the byte is out of range;
     * MIDI_TRACK_CHANNEL_OFF (17) for `off`. The pre-S083 fallback to
     * track+1 is gone. Accessors: MidiNoteMap loader (maps `off` to "no
     * channel"), menu (menu-order conversion).
     */
    if (!scene || track >= NUM_TRACKS)
        return 0u;
    channel = scene->settings.midi_channel[track];
    return (channel <= MIDI_TRACK_CHANNEL_OFF) ? channel : 0u;
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
    /* S083 follow-up: 0 = `on`, 1..127 root, 128 = `off`; above clamps off. */
    normalized_note = (note <= MIDI_TRACK_NOTE_OFF) ? note : MIDI_TRACK_NOTE_OFF;
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
     * Inputs: Scene index and track index. Output: 0 = `on` (no root; also
     * for invalid/stale data), 1..127 = root, MIDI_TRACK_NOTE_OFF (128) =
     * `off` (S083 follow-up). Menu and MIDI callers use this to keep the
     * Scene storage policy centralized.
     */
    if (!scene || track >= NUM_TRACKS)
        return 0u;
    return (scene->settings.midi_note[track] <= MIDI_TRACK_NOTE_OFF)
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
     * bit is marked. Runtime FX send is intentionally not written here; the
     * mixer pulls the effective Scene/Preset value at the next block boundary.
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

void scene_setVoiceFxSendMorph(uint8_t scene_index, uint8_t slot,
                               uint8_t amount)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one FX-send Morph endpoint (contract in SceneData.h, S075 F2-H).
     *
     * Clamp, then use scene_storeParameterByte() so the byte is written before
     * AutoSave cell FX_SEND_MORPH_BASE + slot and card-clean invalidation. A
     * mixer runtime push is unnecessary: the next block reads both endpoints.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (amount > 127u)
        amount = 127u;
    scene_storeParameterByte(
        scene_index, &scene->settings.fx_send_morph[slot],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_FX_SEND_MORPH_BASE + slot), amount);
}

uint8_t scene_getVoiceFxSendMorph(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    /* Contract in SceneData.h: retained 0..127, or 0 for invalid storage. */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT ||
        scene->settings.fx_send_morph[slot] > 127u)
        return 0u;
    return scene->settings.fx_send_morph[slot];
}

void scene_setVoiceFaderSetting(uint8_t scene_index, uint8_t slot,
                                uint8_t mode)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one future per-voice fader mode.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and mode in
     * the current Scene file domain: 0 normal/pre-FX, 1 post-FX, 2 FX-only,
     * 3 xfd dry/FX crossfade (S074); larger values clamp to
     * SCENE_FADER_SETTING_MAX. Output: a changed retained mode is stored
     * before its named Scene bit is marked. Runtime behavior is applied by
     * Preset and the mixer rather than being hidden in SceneData. AutoSave
     * restore also enters here, so a restored byte is always in domain.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return;
    if (mode > SCENE_FADER_SETTING_MAX)
        mode = SCENE_FADER_SETTING_MAX;
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
     * retained 0..SCENE_FADER_SETTING_MAX mode (0..3, S074 adds xfd), or 0
     * for invalid coordinates/stale storage.
     */
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT ||
        scene->settings.fader_setting[slot] > SCENE_FADER_SETTING_MAX) {
        return 0u;
    }
    return scene->settings.fader_setting[slot];
}

void scene_setSlot6Track7Decay(uint8_t scene_index, uint8_t layer, uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one generated track-7 Normal decay endpoint (S104 P1: per layer).
     *
     * Inputs: resident Scene index, layer 0..3 and a 0..127 menu-domain
     * value. Output: Kit settings retain the value the track-7 trigger uses
     * for that layer (InstrumentManager: the amp EG of a generic slot-6
     * instrument for layer 0; a Tetrakt's layer decays). This cannot be
     * folded into the descriptor-image setters because the value is not part
     * of any instrument file and has no descriptor index. A changed
     * normalized byte is stored before its named Kit parameter
     * (AUTOSAVE_KIT_PARAM_T7(layer, 0)) is marked; equal values do nothing.
     */
    if (!scene || layer >= SCENE_TRACK7_DECAY_LAYERS)
        return;
    if (value > 127u)
        value = 127u;
    scene_storeKitParameterByte(
        scene_index, &scene->kit.settings.slot6_track7_decay[layer],
        AUTOSAVE_KIT_PARAM_T7(layer, 0u), value);
}

uint8_t scene_getSlot6Track7Decay(uint8_t scene_index, uint8_t layer)
{
    const scene_t *scene = scene_getConst(scene_index);

    /* Retained 0..127 Normal endpoint; 0 for an invalid Scene or layer.
     * Clients: Menu display, Preset apply, storage, the track-7 trigger. */
    return (scene && layer < SCENE_TRACK7_DECAY_LAYERS)
        ? scene->kit.settings.slot6_track7_decay[layer] : 0u;
}

void scene_setSlot6Track7MorphDecay(uint8_t scene_index, uint8_t layer,
                                    uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one generated track-7 Morph decay endpoint (S104 P1: per layer).
     * Same contract as the Normal setter; the AutoSave cell is
     * AUTOSAVE_KIT_PARAM_T7(layer, 1). Kept separate from Scene
     * voice_morph_amount[], which is the interpolation amount.
     */
    if (!scene || layer >= SCENE_TRACK7_DECAY_LAYERS)
        return;
    if (value > 127u)
        value = 127u;
    scene_storeKitParameterByte(
        scene_index, &scene->kit.settings.slot6_track7_morph_decay[layer],
        AUTOSAVE_KIT_PARAM_T7(layer, 1u), value);
}

uint8_t scene_getSlot6Track7MorphDecay(uint8_t scene_index, uint8_t layer)
{
    const scene_t *scene = scene_getConst(scene_index);

    return (scene && layer < SCENE_TRACK7_DECAY_LAYERS)
        ? scene->kit.settings.slot6_track7_morph_decay[layer] : 0u;
}

/*
 * Seed the track-7 decays a load did not carry (S104 P1; user Q24: "a load
 * sets T7 decays the same as T6 if they aren't present in the scene or kit").
 *
 * What: for each layer that slot 6's type has a track-7 decay for
 * (instrumentManager_track7DecayIndex(): a Tetrakt has A..D at its
 * tkt_decay_a..d rows; any other type with amp_envelope_decay has layer 0),
 * an endpoint whose bit is clear in seen_mask (bit 2 x layer = Normal,
 * bit 2 x layer + 1 = Morph; storage_kitset_t.seen_t7_mask) takes slot 6's
 * own value of that row from the same image (Normal from
 * instrument_parameters[], Morph from morph_instrument_parameters[]).
 * scene_seedTrack7Decays() writes a staged kit_t directly (the commit that
 * follows copies the whole Kit and marks it); scene_seedResidentTrack7Decays()
 * writes a resident Scene through the setters (AutoSave-marked), for an
 * Instrument Load into slot 6, which never carries these values.
 * Why: an older or hand-made Kit file, or a newly loaded slot-6 instrument,
 * would otherwise leave track 7 at 0 (an instant decay) or at another
 * type's value. Inputs: the Kit or Scene, the seen mask. Outputs: unseen
 * endpoints. Accessors: filesystem.c Kit/Scene/boot commits,
 * presetManager.c Instrument commit. Affiliates: scene_setSlot6Track7Decay().
 */
void scene_seedTrack7Decays(kit_t *kit, uint8_t seen_mask)
{
    const kit_instrument_slot_t *slot6;
    uint8_t layer;

    if (!kit)
        return;
    slot6 = &kit->instruments[INSTRUMENT_SLOT_COUNT - 1u];
    for (layer = 0u; layer < SCENE_TRACK7_DECAY_LAYERS; layer++) {
        uint8_t index;

        if (!instrumentManager_track7DecayIndex(slot6->type, layer, &index))
            continue;
        if ((seen_mask & (uint8_t)(1u << (2u * layer))) == 0u)
            kit->settings.slot6_track7_decay[layer] =
                slot6->parameter_images.instrument_parameters[index];
        if ((seen_mask & (uint8_t)(1u << (2u * layer + 1u))) == 0u)
            kit->settings.slot6_track7_morph_decay[layer] =
                slot6->parameter_images.morph_instrument_parameters[index];
    }
}

void scene_seedResidentTrack7Decays(uint8_t scene_index)
{
    const kit_instrument_slot_t *slot6 =
        scene_instrumentSlotConst(scene_index, INSTRUMENT_SLOT_COUNT - 1u);
    uint8_t layer;

    if (!slot6)
        return;
    for (layer = 0u; layer < SCENE_TRACK7_DECAY_LAYERS; layer++) {
        uint8_t index;

        if (!instrumentManager_track7DecayIndex(slot6->type, layer, &index))
            continue;
        scene_setSlot6Track7Decay(scene_index, layer,
            slot6->parameter_images.instrument_parameters[index]);
        scene_setSlot6Track7MorphDecay(scene_index, layer,
            slot6->parameter_images.morph_instrument_parameters[index]);
    }
}

const effect_record_t *scene_effectConst(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);

    /* Read-only Effect view; the retained-record contract is in SceneData.h. */
    return scene ? &scene->effect : 0;
}

void scene_effectRecordDefaults(effect_record_t *record)
{
    /*
     * Build the retained `off` Effect defaults without dirty marking.
     *
     * Inputs: caller-owned record. Output: zeroed steps/type-specific cells,
     * fwd/16/1-16 sequence settings, and common out/vol/pan defaults in both
     * endpoint images. Used during Scene initialization and future type/load
     * transactions before the record belongs to a resident Scene.
     */
    if (!record)
        return;
    memset(record, 0, sizeof(*record));
    record->type = EFFECT_TYPE_OFF;
    record->seq_run_mode = EFFECT_SEQ_RUN_FWD;
    record->seq_length = EFFECT_SEQ_LENGTH_DEFAULT;
    record->seq_step_scale = EFFECT_SEQ_SCALE_DEFAULT;
    record->normal[EFFECT_COMMON_PARAM_AUDIO_OUT] =
        EFFECT_COMMON_DEFAULT_AUDIO_OUT;
    record->normal[EFFECT_COMMON_PARAM_LEVEL] = EFFECT_COMMON_DEFAULT_LEVEL;
    record->normal[EFFECT_COMMON_PARAM_PAN] = EFFECT_COMMON_DEFAULT_PAN;
    record->morph[EFFECT_COMMON_PARAM_AUDIO_OUT] =
        EFFECT_COMMON_DEFAULT_AUDIO_OUT;
    record->morph[EFFECT_COMMON_PARAM_LEVEL] = EFFECT_COMMON_DEFAULT_LEVEL;
    record->morph[EFFECT_COMMON_PARAM_PAN] = EFFECT_COMMON_DEFAULT_PAN;
}

uint8_t scene_commitEffectRecord(uint8_t scene_index,
                                 const effect_record_t *record)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Replace a complete Effect record (contract in SceneData.h).
     *
     * Inputs: resident Scene and caller-validated record. Output: the record
     * is copied, sequence settings are normalized, and all live Effect cells
     * are marked. Type validation belongs to the future registry owner; this
     * boundary only prevents malformed sequence bytes from reaching readers.
     */
    if (!scene || !record)
        return 0u;
    scene->effect = *record;
    if (scene->effect.seq_run_mode >= EFFECT_SEQ_RUN_MODE_COUNT)
        scene->effect.seq_run_mode = EFFECT_SEQ_RUN_FWD;
    if (scene->effect.seq_length < EFFECT_SEQ_LENGTH_MIN)
        scene->effect.seq_length = EFFECT_SEQ_LENGTH_MIN;
    if (scene->effect.seq_length > EFFECT_SEQ_LENGTH_MAX)
        scene->effect.seq_length = EFFECT_SEQ_LENGTH_MAX;
    if (scene->effect.seq_step_scale >= EFFECT_SEQ_SCALE_COUNT)
        scene->effect.seq_step_scale = EFFECT_SEQ_SCALE_DEFAULT;
    autosave_markEffectDirty(scene_index);
    bank_invalidateSdCleanScene(scene_index);
    return 1u;
}

effect_record_t *scene_effectRecordForWholeCommit(uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);

    /* Return the mutable retained record; the paired close owns marking. */
    return scene ? &scene->effect : 0;
}

void scene_finishEffectWholeCommit(uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);

    /* Close the in-place commit with the same normalization as the copy path. */
    if (!scene)
        return;
    if (scene->effect.seq_run_mode >= EFFECT_SEQ_RUN_MODE_COUNT)
        scene->effect.seq_run_mode = EFFECT_SEQ_RUN_FWD;
    if (scene->effect.seq_length < EFFECT_SEQ_LENGTH_MIN)
        scene->effect.seq_length = EFFECT_SEQ_LENGTH_MIN;
    if (scene->effect.seq_length > EFFECT_SEQ_LENGTH_MAX)
        scene->effect.seq_length = EFFECT_SEQ_LENGTH_MAX;
    if (scene->effect.seq_step_scale >= EFFECT_SEQ_SCALE_COUNT)
        scene->effect.seq_step_scale = EFFECT_SEQ_SCALE_DEFAULT;
    autosave_markEffectDirty(scene_index);
    bank_invalidateSdCleanScene(scene_index);
}

void scene_setEffectNormalParameter(uint8_t scene_index, uint8_t index,
                                    uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /* Store one normal endpoint cell at its explicit AutoSave index. */
    if (!scene || index >= EFFECT_PARAM_COUNT)
        return;
    scene_storeEffectByte(
        scene_index, &scene->effect.normal[index],
        (uint16_t)(AUTOSAVE_EFFECT_PARAM_NORMAL_BASE + index), value);
}

void scene_setEffectMorphParameter(uint8_t scene_index, uint8_t index,
                                   uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /* Store one Morph endpoint cell at its explicit AutoSave index. */
    if (!scene || index >= EFFECT_PARAM_COUNT)
        return;
    scene_storeEffectByte(
        scene_index, &scene->effect.morph[index],
        (uint16_t)(AUTOSAVE_EFFECT_PARAM_MORPH_BASE + index), value);
}

void scene_setEffectSeqRunMode(uint8_t scene_index, uint8_t mode)
{
    scene_t *scene = scene_get(scene_index);

    /* Run mode is an enum; invalid values are ignored instead of remapped. */
    if (!scene || mode >= EFFECT_SEQ_RUN_MODE_COUNT)
        return;
    scene_storeEffectByte(scene_index, &scene->effect.seq_run_mode,
                          AUTOSAVE_EFFECT_PARAM_SEQ_RUN_MODE, mode);
}

void scene_setEffectSeqLength(uint8_t scene_index, uint8_t length)
{
    scene_t *scene = scene_get(scene_index);

    /* Sequence length is saturating in the retained 1..16 domain. */
    if (!scene)
        return;
    if (length < EFFECT_SEQ_LENGTH_MIN)
        length = EFFECT_SEQ_LENGTH_MIN;
    if (length > EFFECT_SEQ_LENGTH_MAX)
        length = EFFECT_SEQ_LENGTH_MAX;
    scene_storeEffectByte(scene_index, &scene->effect.seq_length,
                          AUTOSAVE_EFFECT_PARAM_SEQ_LENGTH, length);
}

void scene_setEffectSeqStepScale(uint8_t scene_index, uint8_t scale)
{
    scene_t *scene = scene_get(scene_index);

    /* Shared track/Effect scale index; invalid indices are ignored. */
    if (!scene || scale >= EFFECT_SEQ_SCALE_COUNT)
        return;
    scene_storeEffectByte(scene_index, &scene->effect.seq_step_scale,
                          AUTOSAVE_EFFECT_PARAM_SEQ_STEP_SCALE, scale);
}

void scene_setEffectSeqLaneValue(uint8_t scene_index, uint8_t step,
                                 uint8_t lane, uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /* Store one lane value without changing that lane's lock bit. */
    if (!scene || step >= EFFECT_SEQ_STEP_COUNT ||
        lane >= EFFECT_SEQ_LANE_COUNT)
        return;
    scene_storeEffectByte(
        scene_index, &scene->effect.steps[step].value[lane],
        (uint16_t)(AUTOSAVE_EFFECT_PARAM_STEPS_BASE +
                   step * AUTOSAVE_EFFECT_STEP_BYTES +
                   AUTOSAVE_EFFECT_STEP_VALUES_OFFSET + lane), value);
}

void scene_setEffectSeqLaneLocked(uint8_t scene_index, uint8_t step,
                                  uint8_t lane, uint8_t locked)
{
    scene_t *scene = scene_get(scene_index);
    effect_seq_step_t *entry;
    uint16_t bit;
    uint16_t updated;

    /*
     * Update one serialized little-endian lock-mask byte.
     *
     * Only the low or high mask byte is marked, preserving the one-cell dirty
     * contract even though the retained value is a 16-bit field.
     */
    if (!scene || step >= EFFECT_SEQ_STEP_COUNT ||
        lane >= EFFECT_SEQ_LANE_COUNT)
        return;
    entry = &scene->effect.steps[step];
    bit = (uint16_t)(1u << lane);
    updated = locked ? (uint16_t)(entry->lock_mask | bit)
                     : (uint16_t)(entry->lock_mask & (uint16_t)~bit);
    if (updated == entry->lock_mask)
        return;
    entry->lock_mask = updated;
    autosave_markEffectParameterDirty(
        scene_index,
        (uint16_t)(AUTOSAVE_EFFECT_PARAM_STEPS_BASE +
                   step * AUTOSAVE_EFFECT_STEP_BYTES +
                   ((lane < 8u) ? AUTOSAVE_EFFECT_STEP_MASK_LO_OFFSET
                                : AUTOSAVE_EFFECT_STEP_MASK_HI_OFFSET)));
    bank_invalidateSdCleanScene(scene_index);
}

void scene_setEffectMorphAmount(uint8_t scene_index, uint8_t amount)
{
    scene_t *scene = scene_get(scene_index);

    /* Store the Scene parameter through the existing scalar owner funnel. */
    if (!scene)
        return;
    scene_storeParameterByte(scene_index, &scene->settings.effect_morph_amount,
                             AUTOSAVE_SCENE_PARAM_EFFECT_MORPH, amount);
}

uint8_t scene_getEffectMorphAmount(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);

    /* Return retained Effect Morph amount, or zero for an invalid Scene. */
    return scene ? scene->settings.effect_morph_amount : 0u;
}

/*
 * S074 bus compressor domain/default tables, in scene_bus_comp_field_t order.
 *
 * What:       maxima (St2, 127, 127, voice 6) and defaults (off, 0, 0,
 *             off; S075 F2-B). Why: parser, menu, AutoSave restore and Scene setter all
 *             use the same clamp boundary, so no path stores an unreachable
 *             value. Affiliates: storageTypes.c, presetManager.c, menu.c.
 */
static const uint8_t scene_busCompMax[SCENE_BUS_COMP_FIELD_COUNT] = {
    SCENE_BUS_COMP_MODE_ST2, 127u, 127u, INSTRUMENT_SLOT_COUNT
};
static const uint8_t scene_busCompDefault[SCENE_BUS_COMP_FIELD_COUNT] = {
    SCENE_BUS_COMP_MODE_OFF, SCENE_BUS_COMP_DEFAULT_AMOUNT,
    SCENE_BUS_COMP_DEFAULT_TIME, SCENE_BUS_COMP_SIDECHAIN_OFF
};

void scene_busCompDefaults(scene_settings_t *settings)
{
    uint8_t field;

    /* Initialization/staging may seed the complete settings image directly. */
    if (!settings)
        return;
    for (field = 0u; field < SCENE_BUS_COMP_FIELD_COUNT; field++)
        settings->bus_comp[field] = scene_busCompDefault[field];
}

uint8_t scene_busCompClamp(uint8_t field, uint8_t value)
{
    /* Contract in SceneData.h; invalid fields normalize to the safe off byte. */
    if (field >= SCENE_BUS_COMP_FIELD_COUNT)
        return 0u;
    return (value > scene_busCompMax[field]) ? scene_busCompMax[field]
                                             : value;
}

void scene_setBusCompSetting(uint8_t scene_index, uint8_t field,
                             uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Store one bus compressor byte through the scalar owner funnel.
     *
     * Inputs: resident Scene, field, and any byte. Output: clamped storage is
     * written before AutoSave parameter 41+field and card-clean invalidation;
     * equal values and invalid coordinates are no-ops.
     */
    if (!scene || field >= SCENE_BUS_COMP_FIELD_COUNT)
        return;
    scene_storeParameterByte(
        scene_index, &scene->settings.bus_comp[field],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE + field),
        scene_busCompClamp(field, value));
}

uint8_t scene_getBusCompSetting(uint8_t scene_index, uint8_t field)
{
    const scene_t *scene = scene_getConst(scene_index);

    /* Return the retained byte, or off for an invalid Scene/field. */
    if (!scene || field >= SCENE_BUS_COMP_FIELD_COUNT)
        return 0u;
    return scene->settings.bus_comp[field];
}

/*
 * Per-track Morph endpoints (S078 §5.4).
 *
 * What: store or read one track's Morph endpoint for length, scale, or shuffle.
 * Setters clamp to the parameter domain and commit through
 * scene_storeParameterByte(), so a changed byte marks exactly its AutoSave
 * Scene cell (51..71) and the card-clean bit. Getters return the retained
 * value, or the parameter default for an invalid Scene/track. Why: the effective
 * track getters in presetMorphEngine.c interpolate the Pattern Normal value
 * against these endpoints at the voice Morph amount. Inputs: resident Scene
 * index, track 0..6, value. Outputs: retained Scene data updated. Affiliates:
 * Autosave Scene cells, filesystem.c sceneset writer, presetMorphEngine.c.
 */
void scene_setTrackMorphLength(uint8_t scene_index, uint8_t track,
                               uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || track >= NUM_TRACKS)
        return;
    if (value < 1u)
        value = 1u;
    else if (value > NUM_STEPS)
        value = NUM_STEPS;
    scene_storeParameterByte(
        scene_index, &scene->settings.track_morph_length[track],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_TRACK_MORPH_LENGTH_BASE + track),
        value);
}

uint8_t scene_getTrackMorphLength(uint8_t scene_index, uint8_t track)
{
    const scene_t *scene = scene_getConst(scene_index);

    if (!scene || track >= NUM_TRACKS ||
        scene->settings.track_morph_length[track] < 1u ||
        scene->settings.track_morph_length[track] > NUM_STEPS)
        return NUM_STEPS_PER_BAR;
    return scene->settings.track_morph_length[track];
}

void scene_setTrackMorphScale(uint8_t scene_index, uint8_t track,
                              uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || track >= NUM_TRACKS)
        return;
    if (value >= STEP_SCALE_COUNT)
        value = STEP_SCALE_DEFAULT;
    scene_storeParameterByte(
        scene_index, &scene->settings.track_morph_scale[track],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SCALE_BASE + track),
        value);
}

uint8_t scene_getTrackMorphScale(uint8_t scene_index, uint8_t track)
{
    const scene_t *scene = scene_getConst(scene_index);

    if (!scene || track >= NUM_TRACKS ||
        scene->settings.track_morph_scale[track] >= STEP_SCALE_COUNT)
        return STEP_SCALE_DEFAULT;
    return scene->settings.track_morph_scale[track];
}

void scene_setTrackMorphShuffle(uint8_t scene_index, uint8_t track,
                                uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || track >= NUM_TRACKS)
        return;
    if (value > 127u)
        value = 127u;
    scene_storeParameterByte(
        scene_index, &scene->settings.track_morph_shuffle[track],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_TRACK_MORPH_SHUFFLE_BASE + track),
        value);
}

uint8_t scene_getTrackMorphShuffle(uint8_t scene_index, uint8_t track)
{
    const scene_t *scene = scene_getConst(scene_index);

    if (!scene || track >= NUM_TRACKS ||
        scene->settings.track_morph_shuffle[track] > 127u)
        return 0u;
    return scene->settings.track_morph_shuffle[track];
}

/*
 * Scene chain accessors (S080 G2; contract in SceneData.h).
 *
 * What: named setters/getters for the two chain pairs, clamped to the table
 * sizes and committed through scene_storeParameterByte() with AutoSave cells
 * AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE/NEXT_BASE + slot. Why: the
 * AutoSave extension rule requires a named index, getter and change-aware
 * setter for every retained Scene byte. Inputs: Scene 0..15, slot 0..1,
 * value. Outputs: scene.settings.chain_condition[]/chain_next[].
 * Affiliates: autosave_getSceneParameter()/autosave_applyScenePayload().
 */
void scene_setChainCondition(uint8_t scene_index, uint8_t slot, uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || slot >= SCENE_CHAIN_SLOT_COUNT)
        return;
    if (value > SCENE_CHAIN_CONDITION_MAX)
        value = SCENE_CHAIN_CONDITION_MAX;
    scene_storeParameterByte(
        scene_index, &scene->settings.chain_condition[slot],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_CHAIN_CONDITION_BASE + slot), value);
}

uint8_t scene_getChainCondition(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    if (!scene || slot >= SCENE_CHAIN_SLOT_COUNT ||
        scene->settings.chain_condition[slot] > SCENE_CHAIN_CONDITION_MAX)
        return 0u;
    return scene->settings.chain_condition[slot];
}

void scene_setChainNext(uint8_t scene_index, uint8_t slot, uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || slot >= SCENE_CHAIN_SLOT_COUNT)
        return;
    if (value > SCENE_CHAIN_NEXT_MAX)
        value = SCENE_CHAIN_NEXT_MAX;
    scene_storeParameterByte(
        scene_index, &scene->settings.chain_next[slot],
        (uint8_t)(AUTOSAVE_SCENE_PARAM_CHAIN_NEXT_BASE + slot), value);
}

uint8_t scene_getChainNext(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);

    if (!scene || slot >= SCENE_CHAIN_SLOT_COUNT ||
        scene->settings.chain_next[slot] > SCENE_CHAIN_NEXT_MAX)
        return 0u;
    return scene->settings.chain_next[slot];
}

/*
 * Default Instrument types of a fresh or emptied Scene (DRM, DRM, DRM, SNR,
 * CYM, HAT). Shared by scene_initAll() and scene_resetKitToDefaults() (S075)
 * so both produce the same Kit. filesystem_initSceneStage() keeps its own
 * identical table for the Scene Load stage.
 */
static const instrument_type_t
    scene_initialInstrumentTypes[INSTRUMENT_SLOT_COUNT] = {
        INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM,
        INSTRUMENT_TYPE_SNR, INSTRUMENT_TYPE_CYM, INSTRUMENT_TYPE_HAT
    };

/*
 * Whole-settings defaults and commit for copy/clear (S075).
 *
 * Contract in SceneData.h. scene_settingsDefaults() reproduces the Scene Load
 * stage defaults (filesystem_initSceneStage()). scene_commitSettings() writes
 * every field through its change-aware setter, so each changed byte marks its
 * own AutoSave cell and the card-clean bit; the before/after comparison runs
 * on a stack copy (sizeof(scene_settings_t), about 80 B) because the setters
 * do not report change. `src` may be another Scene's live settings.
 */
void scene_settingsDefaults(scene_settings_t *out)
{
    uint8_t i;

    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    for (i = 0u; i < NUM_TRACKS; i++) {
        /* S083: tracks follow G and have no note root until assigned. */
        out->midi_channel[i] = MIDI_CHANNEL_NONE;
        out->midi_note[i] = 0u;
        /*
         * Track Morph endpoint defaults (S078 §5.4).
         *
         * What: fresh Scenes start with Morph endpoints matching the Normal
         * defaults (length 16, scale 76/1/16, shuffle 0), so Morph amount
         * 0..255 produces no change until the endpoints differ.
         */
        out->track_morph_length[i] = NUM_STEPS_PER_BAR;
        out->track_morph_scale[i] = STEP_SCALE_DEFAULT;
        out->track_morph_shuffle[i] = 0u;
    }
    for (i = 0u; i < INSTRUMENT_SLOT_COUNT; i++)
        out->audio_out[i] = scene_defaultVoiceAudioOut(i);
    scene_busCompDefaults(out);
}

uint8_t scene_commitSettings(uint8_t scene_index, const scene_settings_t *src)
{
    scene_t *scene = scene_get(scene_index);
    scene_settings_t before;
    scene_settings_t image;
    uint8_t i;

    if (!scene || !src)
        return 0u;
    image = *src;
    before = scene->settings;
    scene_setMorphAmount(scene_index, image.morph_amount);
    for (i = 0u; i < INSTRUMENT_SLOT_COUNT; i++) {
        scene_setVoiceMorphAmount(scene_index, i, image.voice_morph_amount[i]);
        scene_setVoiceAudioOut(scene_index, i, image.audio_out[i]);
        scene_setVoiceFxSendAmount(scene_index, i, image.fx_send_amount[i]);
        /* S075 F2-H: copy/clear carry both FX-send endpoints. */
        scene_setVoiceFxSendMorph(scene_index, i, image.fx_send_morph[i]);
        scene_setVoiceFaderSetting(scene_index, i, image.fader_setting[i]);
    }
    for (i = 0u; i < NUM_TRACKS; i++) {
        scene_setTrackMidiChannel(scene_index, i, image.midi_channel[i]);
        scene_setTrackMidiNote(scene_index, i, image.midi_note[i]);
        /* S078 §5.4: copy/clear carry the three track Morph endpoints. */
        scene_setTrackMorphLength(scene_index, i, image.track_morph_length[i]);
        scene_setTrackMorphScale(scene_index, i, image.track_morph_scale[i]);
        scene_setTrackMorphShuffle(scene_index, i,
                                   image.track_morph_shuffle[i]);
    }
    scene_setEffectMorphAmount(scene_index, image.effect_morph_amount);
    for (i = 0u; i < SCENE_BUS_COMP_FIELD_COUNT; i++)
        scene_setBusCompSetting(scene_index, i, image.bus_comp[i]);
    /*
     * S080 G3: copy/clear carry the Scene chain (defaults 0 = no chaining via
     * scene_settingsDefaults()' memset). Not part of the VOICE edit-mask
     * fan-out: only this Scene changes.
     */
    for (i = 0u; i < SCENE_CHAIN_SLOT_COUNT; i++) {
        scene_setChainCondition(scene_index, i, image.chain_condition[i]);
        scene_setChainNext(scene_index, i, image.chain_next[i]);
    }
    return (uint8_t)(memcmp(&before, &scene->settings, sizeof(before)) != 0);
}

/*
 * Reset one Scene's Kit to the fresh-Scene Kit (contract in SceneData.h).
 *
 * Whole-Kit commit: each slot is reset through its descriptors, every slot-6 /
 * track-7 decay layer returns to 0 through its own setters, then the Kit
 * region marker and the card-clean bit are set (the direct slot assignment is
 * the validated whole-commit exception of the AutoSave extension rule).
 */
void scene_resetKitToDefaults(uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);
    uint8_t slot;

    if (!scene)
        return;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        instrumentManager_resetSlot(&scene->kit.instruments[slot],
                                    scene_initialInstrumentTypes[slot]);
    /* S104 P1: every track-7 decay layer, both endpoints, to 0 (a reset is
     * not a load: the load seeding rule does not apply here). */
    for (slot = 0u; slot < SCENE_TRACK7_DECAY_LAYERS; slot++) {
        scene_setSlot6Track7Decay(scene_index, slot, 0u);
        scene_setSlot6Track7MorphDecay(scene_index, slot, 0u);
    }
    bank_invalidateSdCleanScene(scene_index);
    autosave_markKitDirty(scene_index);
}

/*
 * Reset one resident Scene's retained data to the empty Scene (S082 B3).
 *
 * What: settings (bus compressor, MIDI, per-voice routing/send/fader), the six
 * Kit slots (default types) and the Effect record are reset to the values a
 * fresh boot uses. Pattern, HCNAMES, present bit and edit masks belong to
 * their owners and are handled by the caller.
 * Why: one data definition of "empty Scene" for the boot reader (Case 3) and
 * the S082 Replace Bank Load (R7); before S082 the boot reader owned a private
 * copy.
 * Inputs: resident Scene index 0..15 (Scene 17 is never emptied).
 * Outputs: scenes[index] rewritten; no AutoSave mark (callers mark).
 * Accessors: filesystem_bootReaderEmptyScene(), filesystem_bankEmptyScenes().
 * Affiliates: scene_busCompDefaults(), instrumentManager_resetSlot(),
 * scene_effectRecordDefaults(), pat_initScene() (caller).
 */
void scene_resetResidentToEmpty(uint8_t scene_index)
{
    scene_t *scene = scene_get(scene_index);
    uint8_t slot;

    if (!scene)
        return;
    memset(scene, 0, sizeof(*scene));
    scene_busCompDefaults(&scene->settings);
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
        scene->settings.audio_out[slot] = scene_defaultVoiceAudioOut(slot);
        scene->settings.fx_send_amount[slot] = 0u;
        scene->settings.fx_send_morph[slot] = 0u;
        scene->settings.fader_setting[slot] = 0u;
        instrumentManager_resetSlot(&scene->kit.instruments[slot],
                                    scene_initialInstrumentTypes[slot]);
    }
    scene_effectRecordDefaults(&scene->effect);
}

/*
 * Replace one Scene's whole Kit (contract in SceneData.h).
 *
 * Whole-Kit commit: the six slots and the Kit settings are copied, then the
 * Kit region marker and the card-clean bit are set. `kit` may be another
 * Scene's live Kit (never this Scene's own; that is a no-op).
 */
uint8_t scene_commitKit(uint8_t scene_index, const kit_t *kit)
{
    scene_t *scene = scene_get(scene_index);

    if (!scene || !kit)
        return 0u;
    if (&scene->kit == kit)
        return 0u;
    scene->kit = *kit;
    bank_invalidateSdCleanScene(scene_index);
    autosave_markKitDirty(scene_index);
    return 1u;
}

void scene_initAll(void)
{
    uint8_t scene_index;
    uint8_t track;

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
        /* S074/S075 F2-B: bus compressor defaults are off, 0, 0, off. */
        scene_busCompDefaults(&scenes[scene_index].settings);
        for (track = 0u; track < INSTRUMENT_SLOT_COUNT; track++) {
            /*
             * Initialize Scene-owned per-voice mix settings before any SD
             * load. These are separate from instrument reset because changing
             * the instrument type in a slot must not reset Scene mix state.
             */
            scenes[scene_index].settings.audio_out[track] =
                scene_defaultVoiceAudioOut(track);
            scenes[scene_index].settings.fx_send_amount[track] = 0u;
            /* S075 F2-H: a fresh Scene's Morph endpoint is also 0. */
            scenes[scene_index].settings.fx_send_morph[track] = 0u;
            scenes[scene_index].settings.fader_setting[track] = 0u;
        }
        /* Seed the Scene-owned Effect before any file/runtime apply begins. */
        scene_effectRecordDefaults(&scenes[scene_index].effect);
        for (track = 0u; track < INSTRUMENT_SLOT_COUNT; track++)
            instrumentManager_resetSlot(
                &scenes[scene_index].kit.instruments[track],
                scene_initialInstrumentTypes[track]);
        pat_initScene(scene_index);
    }
}
