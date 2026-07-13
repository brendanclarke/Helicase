/*
 * presetManager.c — LXR-02 preset load/save (asyncfatfs version).
 *
 * All SD card operations are delegated to filesystem.c. Load/save functions
 * post a request and return immediately, setting status to
 * PRESET_LOAD_IN_PROGRESS.
 *
 * When the filesystem completes, the completion callback sets status to
 * PRESET_UPDATE_READY and records which operation completed.
 *
 * The menu main loop calls preset_getStatus(). When it sees UPDATE_READY,
 * it applies post-load logic (mod target gap index, repaint, globals
 * apply, etc.) then calls preset_ackStatus() to return to IDLE.
 *
 * This replicates the natural delay the original LXR two-MCU design
 * provided: the AVR owned the SD card and trickle-fed parameters to
 * the STM32 via UART. The STM32 never saw parameters until the transfer
 * was complete.
 */

/*
 *  Modified on: 17.05.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Modifications Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  The modifications to this file are part of the LXR02 Open-Source software.
 *  The same license and restrictions on use for the LXR software apply.
 * ------------------------------------------------------------------------------------------------------------------------
 */


#include "presetManager.h"
#include "menu.h"
#include "CcNr2Text.h"
#include "ParameterArray.h"
#include "MidiParser.h"
#include "sequencer.h"
#include "modulationNode.h"
#include "DrumVoice.h"
#include "CymbalVoice.h"
#include "HiHat.h"
#include "Snare.h"
#include "filesystem.h"
#include "presetMorphEngine.h"
#include "SceneModTargets.h"
#include "mixer.h"
#include "valueShaper.h"
#include <string.h>
#include <stdint.h>

char preset_currentName[8];

/* -----------------------------------------------------------------------
** Status state machine
** ----------------------------------------------------------------------- */
static volatile preset_status_t  pm_status = PRESET_IDLE;
static volatile preset_op_type_t pm_completed_op = PRESET_OP_NONE;
static volatile uint16_t         pm_request_slot = 0;
static volatile uint8_t          pm_request_type = SAVE_TYPE_KIT;
/* Legacy persistence requests survive temporarily for format compatibility,
 * but they no longer occupy Load/Save menu types. Keeping these out of
 * menu.h prevents invisible UI slots from being selected by encoder stepping. */
enum {
    PRESET_REQUEST_LEGACY_MORPH = NUM_SAVE_TYPES,
    PRESET_REQUEST_LEGACY_PATTERN,
    PRESET_REQUEST_LEGACY_PERFORMANCE,
    PRESET_REQUEST_LEGACY_ALL,
};
static volatile uint8_t          pm_instrument_request_slot = 0u;
static volatile uint8_t          pm_instrument_request_scene = 0u;
static volatile instrument_type_t pm_instrument_request_type = INSTRUMENT_TYPE_UNKNOWN;
static volatile uint8_t          pm_instrument_request_index = 0u;
static volatile uint16_t         pm_kit_request_scene_mask = 0u;

/* Runtime loaded-kit apply cursor.
 *
 * Why this changed: directory Kit files now parse into Scene-owned
 * kit_instrument_slot_t images, while the legacy implementation only applied
 * modulation routing from parameter_values[]. The cursor therefore owns the
 * bounded post-load bridge from Scene state to current DSP affiliates.
 *
 * Inputs: preset_startDrumsetApply() selects the active Scene and arms the
 * cursor. preset_tickDrumsetApply() advances at most one audio route,
 * non-morph runtime-cell bundle, or Morph/image application per foreground
 * pass.
 *
 * Outputs: mixer_audioRouting[] and instrument-owned runtime bindings are
 * updated from SceneData. Clients are menu.c load completion and boot-time
 * synchronous apply. Affiliates are SceneData, InstrumentManager descriptors,
 * and presetMorphEngine.
 */
static uint8_t drumset_apply_active = 0;
static uint8_t drumset_apply_voice = 0;
static uint8_t instrument_apply_active = 0u;
static uint8_t instrument_apply_scene = 0u;
static uint8_t instrument_apply_phase = 0u;
static uint8_t instrument_apply_rebind_source = 0u;
static uint8_t instrument_apply_morph_only = 0u;
enum {
    INSTRUMENT_APPLY_PHASE_MORPH_REBUILD = 0u,
    INSTRUMENT_APPLY_PHASE_TARGET_REBIND
};
static uint8_t preset_morph_initialized = 0;

preset_status_t preset_getStatus(void)
{
    return pm_status;
}

preset_op_type_t preset_getCompletedOp(void)
{
    return pm_completed_op;
}

uint16_t preset_getRequestSlot(void)
{
    return pm_request_slot;
}

uint8_t preset_getRequestType(void)
{
    return pm_request_type;
}

uint8_t preset_getRequestScene(void)
{
    /*
     * Expose the Scene captured when Instrument Load was posted.
     *
     * Output: Preset's retained destination Scene index. Client: Menu reads it
     * at async completion to apply the same Scene/slot that filesystem parsed.
     * This narrow accessor cannot be folded into preset_getRequestSlot(): slot
     * and Scene are independent coordinates, and exposing only the slot would
     * make a later menu Scene selection race the queued load's DSP follow-up.
     */
    return pm_instrument_request_scene;
}

void preset_ackStatus(void)
{
    pm_status = PRESET_IDLE;
    pm_completed_op = PRESET_OP_NONE;
}

/* -----------------------------------------------------------------------
** Completion callbacks — fired by filesystem_tick() context
** ----------------------------------------------------------------------- */
static void preset_completeFilesystemOp(preset_op_type_t completed_op)
{
    fs_status_t fs_status = filesystem_status();

    filesystem_ack();
    if (fs_status == FS_STATUS_DONE) {
        pm_completed_op = completed_op;
        pm_status = PRESET_UPDATE_READY;
    } else {
        pm_completed_op = PRESET_OP_NONE;
        pm_status = PRESET_UPDATE_READY;
    }
}

static void on_kit_load_complete(void)
{
	preset_completeFilesystemOp(PRESET_OP_KIT_LOAD);
}

static void on_kit_morph_load_complete(void)
{
    /*
     * Complete a staged new-format Kit Morph load.
     *
     * Filesystem has parsed the selected Kit/ directory into its staging
     * buffer but has not committed it to SceneData. Naming this as KitMrp lets
     * Menu/Preset run the morph-endpoint copy path instead of the normal Kit
     * replacement/apply path.
     */
    preset_completeFilesystemOp(PRESET_OP_KIT_MORPH_LOAD);
}

static void on_morph_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_MORPH_LOAD);
}

static void on_globals_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_GLOBALS_LOAD);
}

static void on_pattern_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_PATTERN_LOAD);
}

static void on_all_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_ALL_LOAD);
}

static void on_performance_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_PERFORMANCE_LOAD);
}

static void on_name_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_NAME_LOAD);
}

static void on_instrument_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_LOAD);
}

static void on_instrument_morph_load_complete(void)
{
    /*
     * Complete a staged Instrument Morph load.
     *
     * The Instrument/ file was parsed by the normal single-instrument loader.
     * Preset will copy only same-type morphable normal endpoint values into the
     * resident destination slot, preserving slot identity and modulation
     * bindings.
     */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_MORPH_LOAD);
}

static void on_kit_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_KIT_SAVE);
}

static void on_scene_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_SCENE_LOAD);
}

static void on_scene_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_SCENE_SAVE);
}

static void on_morph_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_MORPH_SAVE);
}

static void on_globals_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_GLOBALS_SAVE);
}

static void on_pattern_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_PATTERN_SAVE);
}

static void on_all_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_ALL_SAVE);
}

static void on_performance_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_PERFORMANCE_SAVE);
}

static fs_file_type_t preset_fileTypeFromSaveType(uint8_t what, uint8_t *hasName)
{
    if (hasName) *hasName = 1;

    switch (what) {
    case SAVE_TYPE_KIT:         return FS_FILE_KIT;
    case SAVE_TYPE_KIT_MORPH:   return FS_FILE_KIT;
    case SAVE_TYPE_SCENE:       return FS_FILE_SCENE;
    case SAVE_TYPE_GLO:
        if (hasName) *hasName = 0;
        return FS_FILE_GLOBALS;
    case SAVE_TYPE_SAMPLES:
    default:
        if (hasName) *hasName = 0;
        return FS_FILE_SAMPLES;
    }
}

/* Ensure the Scene Morph worker exists before any compatibility wrapper uses it.
 *
 * Why this helper exists: preset_init() is present but the current boot path
 * does not call it. Lazy initialization keeps the new Morph worker valid for
 * boot kit load, menu Morph edits, MIDI Morph edits, and tests without making
 * this bug fix depend on a separate main.c initialization reorder.
 *
 * Inputs: none. Output: presetMorph_init() has run exactly once. Clients are
 * preset_init(), preset_morph(), and preset_startDrumsetApply().
 */
static void preset_ensureMorphInitialized(void)
{
    if (preset_morph_initialized)
        return;
    presetMorph_init();
    preset_morph_initialized = 1u;
}

static void preset_syncSceneMorphMirrors(const scene_t *scene)
{
    uint8_t slot;

    /*
     * Mirror retained Scene Morph settings into the flat Menu buffer.
     *
     * Inputs: Scene settings record. Outputs: parameter_values[] entries used
     * by PERF `mrp` and `1vm..6vm` are updated for display/edit baselines.
     * This helper is intentionally Preset-local because Preset is the boundary
     * between SceneData's retained settings and legacy flat menu mirrors; Menu,
     * MIDI, and future Scene-file load should not each duplicate the mirror
     * rules.
     */
    if (!scene)
        return;
    parameter_values[PAR_MORPH] = scene->settings.morph_amount;
    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        parameter_values[PAR_VOICE1_MORPH + slot] =
            scene->settings.voice_morph_amount[slot];
}

void preset_applyVoiceDecimationAllRuntime(uint8_t value)
{
    /*
     * Apply the Scene-wide decimation multiplier to the mixer runtime.
     *
     * Inputs: retained PERF `srt` value in the existing 0..127 menu domain.
     * Output: mixer_decimation_rate[6] receives the same tapered value used by
     * the legacy VOICE_DECIMATION_ALL MIDI CC path. This stays separate from
     * preset_setVoiceDecimationAll() so Scene apply can mirror already-retained
     * settings without pretending the user edited the parameter again.
     */
    if (value > 127u)
        value = 127u;
    mixer_decimation_rate[INSTRUMENT_SLOT_COUNT] =
        valueShaperI2F(value, -0.7f);
}

/* -----------------------------------------------------------------------
** preset_init — reset Preset async state and initialize Scene-owned morph
** application helpers. Filesystem mount is still handled by asyncfatfs.
** ----------------------------------------------------------------------- */
void preset_init(void)
{
    pm_status = PRESET_IDLE;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = 0;
    pm_request_type = SAVE_TYPE_KIT;
    preset_ensureMorphInitialized();
}

void preset_applySoundParameter(uint16_t paramNr, uint8_t value,
                                uint8_t recordAutomation)
{
    MidiMsg msg = {0};
    uint8_t automationDest;

    /*
     * Applies one sound parameter directly to stored preset state and DSP.
     *
     * Callers: Menu edits, morph interpolation, reset-lock restore, and loaded
     * kit/performance apply. This replaces SET_P1/SET_P2/front-panel parser
     * packing for sound parameters.
     *
     * Why it lives in Preset: parameter_values[] and the sound-engine CC
     * application path are Preset/sound state. Menu decides what the user
     * changed; Preset applies that change and optionally records automation.
     *
     * Inputs:
     *   - paramNr: canonical sound parameter id, before any MIDI_CC packing.
     *   - value: 0..127 menu/DSP value.
     *   - recordAutomation: non-zero records this edit into the active Pattern
     *     via Sequencer automation capture.
     *
     * Outputs: parameter_values[paramNr] is updated, midiParser_ccHandler()
     * applies the value to the DSP voice objects, and seq_recordAutomation()
     * records the edit when requested.
     *
     * Risk: param 127 remains forbidden because the legacy CC encoding maps it
     * to CC0 and then midiParser_ccHandler() underflows while recovering the
     * parameter id. Keeping that guard preserves old behavior until the DSP CC
     * path is replaced with a true typed sound-parameter API.
     */
    if (paramNr == 127u)
        return;
    if (paramNr >= END_OF_SOUND_PARAMETERS)
        return;

    parameter_values[paramNr] = value;

    if (paramNr < 128u) {
        msg.status = MIDI_CC;
        msg.data1 = (uint8_t)((paramNr + 1u) & 0x7fu);
        automationDest = msg.data1;
    } else {
        msg.status = MIDI_CC2;
        msg.data1 = (uint8_t)(paramNr - 128u);
        automationDest = (uint8_t)paramNr;
    }
    msg.data2 = value;
    midiParser_ccHandler(msg, 1);

    if (recordAutomation)
        seq_recordAutomation(menu_getActiveVoice(), automationDest, value);
}

static uint8_t preset_applyInstrumentRuntimeValueInternal(uint8_t scene_index,
                                                          instrument_param_id_t id,
                                                          uint16_t value,
                                                          uint8_t recordAutomation)
{
    const kit_instrument_slot_t *instrument;
    const ParamDescriptor *descriptor;
    uint8_t slot;
    uint8_t index;

    (void)recordAutomation;

    if (!instrumentParam_isVoiceParameter(id))
        return 0u;
    slot = instrumentParam_slot(id);
    index = instrumentParam_local(id);
    instrument = scene_instrumentSlotConst(scene_index, slot);
    if (!instrument)
        return 0u;
    descriptor = instrumentManager_descriptor(instrument->type, index);
    if (!descriptor)
        return 0u;

    /*
     * Runtime apply is now descriptor-owned. The generic storage cell is
     * identified by descriptor index; the descriptor supplies the instrument
     * instance offset and parameter type. ParameterArray/PAR_* is no longer the
     * instrument meaning layer.
     */
    if (scene_index != scene_getActiveIndex())
        return 1u;
    return instrumentManager_writeRuntime(slot, descriptor, value);
}

uint8_t preset_applyInstrumentRuntimeValue(uint8_t scene_index,
                                           instrument_param_id_t id,
                                           uint16_t value)
{
    /*
     * Public runtime apply for one canonical instrument parameter.
     *
     * Inputs: Scene index, slot/descriptor-index instrument ID, and descriptor
     * image value. Output: active Scene values are written through
     * instrument-owned runtime bindings; inactive Scene calls validate but do
     * not mutate live DSP state. Accessors and affiliates: SceneData supplies
     * slot/type lookup, InstrumentManager supplies descriptor lookup and runtime
     * instance binding, and presetMorphEngine calls this after building
     * morph_interpolation[].
     *
     * The public value is uint16_t because Scene descriptor images and
     * InstrumentManager_writeRuntime() are already uint16_t. Keeping this
     * boundary wide prevents Morph from baking in today's byte-domain menu
     * values and avoids a lossy cast before TYPE_SPECIAL_F-style descriptors can
     * re-establish their own min/max contracts.
     */
    return preset_applyInstrumentRuntimeValueInternal(scene_index, id, value, 0u);
}

uint8_t preset_setInstrumentParameter(uint8_t scene_index, uint8_t slot,
                                      uint8_t descriptor_index,
                                      instrument_image_select_t image,
                                      uint8_t value,
                                      uint8_t record_automation)
{
    kit_instrument_slot_t *instrument = scene_instrumentSlot(scene_index, slot);
    const ParamDescriptor *descriptor;
    if (!instrument)
        return 0u;

    descriptor = instrumentManager_descriptor(instrument->type, descriptor_index);
    if (!descriptor ||
        !(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
        return 0u;
    }

    /*
     * Typed endpoint setter.
     *
     * Why this exists: Menu, MIDI translation, tests, and future instrument
     * import should not write the Scene arrays directly. The setter enforces
     * descriptor ownership/range, chooses the persisted endpoint, and schedules
     * Morph interpolation so the runtime image and DSP backend follow the
     * Scene state. Affiliate code: presetMorphEngine owns
     * morph_interpolation[], while preset_applyInstrumentRuntimeValueInternal()
     * owns the temporary legacy DSP mirror.
     */
    if (image == INSTRUMENT_IMAGE_MORPH) {
        instrument->parameter_images.morph_instrument_parameters[descriptor_index] = value;
    } else {
        instrument->parameter_images.instrument_parameters[descriptor_index] = value;
    }

    if (scene_index == scene_getActiveIndex()) {
        scene_t *scene = scene_get(scene_index);
        preset_ensureMorphInitialized();
        if (record_automation && image == INSTRUMENT_IMAGE_MAIN &&
            scene && scene_getVoiceMorphAmount(scene_index, slot) == 0u) {
            (void)preset_applyInstrumentRuntimeValueInternal(
                scene_index,
                instrumentParam_make(slot, descriptor_index),
                value,
                1u);
        }
        /*
         * Endpoint edits are slot-local under per-voice Morph.
         *
         * Inputs: the edited slot/descriptor endpoint and the retained
         * per-slot Morph amount in SceneData. Output: only that slot is queued
         * for interpolation; other voices keep their current Morph positions.
         * This preserves dynamic instrument membership because the worker will
         * still ask InstrumentManager which descriptors are morphable for the
         * slot's current type.
         */
        if (scene)
            presetMorph_requestVoice(scene_index, slot);
    }
    return 1u;
}

uint8_t preset_setSupplementalParameter(uint8_t scene_index, uint8_t slot,
                                        uint8_t descriptor_index, uint16_t value)
{
    kit_instrument_slot_t *instrument = scene_instrumentSlot(scene_index, slot);
    const ParamDescriptor *descriptor;
    if (!instrument)
        return 0u;

    descriptor = instrumentManager_descriptor(instrument->type, descriptor_index);
    if (!descriptor)
        return 0u;

    if (descriptor->runtime.kind == INSTRUMENT_BIND_INSTANCE_OFFSET ||
        (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
        return 0u;
    }

    instrument->parameter_images.instrument_parameters[descriptor_index] = value;
    if (scene_index == scene_getActiveIndex())
        return instrumentManager_writeRuntime(slot, descriptor, value);
    return 1u;
}

uint8_t preset_applyKitAudioRouting(uint8_t scene_index, uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);
    uint8_t route;
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;

    /*
     * Apply one Scene-owned kit routing byte.
     *
     * Input is a Scene/slot pair; output is the active mixer routing and
     * parameter_values[] compatibility byte. The mixer array remains the DSP
     * affiliate until routing is absorbed into a typed Scene apply backend.
     * Clients are the bounded loaded-kit apply cursor and future kit-setting
     * UI edits.
     */
    route = scene->kit.settings.audio_out[slot];
    if (route > MIXER_ROUTING_DAC2_R)
        route = MIXER_ROUTING_DAC1_STEREO;
    if (scene_index == scene_getActiveIndex()) {
        mixer_audioRouting[slot] = route;
    }
    return 1u;
}

uint8_t preset_setSlot6Track7AmpEnvelopeDecay(uint8_t scene_index,
                                              instrument_image_select_t image,
                                              uint8_t value,
                                              uint8_t record_automation)
{
    (void)record_automation;

    /*
     * Retain the generated non-Choke track-7 decay endpoint.
     *
     * Inputs: Scene index, endpoint image selector, 0..127 value, and the
     * normal automation-recording flag used by Menu edits. Output: the
     * generated Kit setting is updated in SceneData. This function exists
     * beside descriptor setters because the generated value has no descriptor
     * index and must never be saved into an instrument file. Future Scene mod
     * target work can extend this boundary with runtime apply/automation
     * capture without teaching Menu the kit_settings_t layout.
     */
    if (!scene_get(scene_index))
        return 0u;
    if (value > 127u)
        value = 127u;
    if (image == INSTRUMENT_IMAGE_MORPH)
        scene_setSlot6Track7MorphAmpEnvelopeDecay(scene_index, value);
    else
        scene_setSlot6Track7AmpEnvelopeDecay(scene_index, value);
    return 1u;
}

void preset_applySceneSettings(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    if (!scene || scene_index != scene_getActiveIndex())
        return;

    /*
     * Apply Scene-wide settings that still have legacy mirrors.
     *
     * Inputs: active Scene index. Outputs: flat PERF mirrors are synchronized
     * from retained Scene settings, global decimation is applied, and the Morph
     * worker is queued from per-voice Morph amounts. This must not call
     * preset_morph(), because preset_morph() is now the user-facing bulk-set
     * operation and would overwrite distinct per-voice Morph values loaded from
     * future sceneset.scg data.
     */
    preset_ensureMorphInitialized();
    preset_syncSceneMorphMirrors(scene);
    parameter_values[PAR_VOICE_DECIMATION_ALL] =
        scene->settings.voice_decimation_all;
    preset_applyVoiceDecimationAllRuntime(scene->settings.voice_decimation_all);
    presetMorph_rebuildScene(scene_index);
}

static void preset_storeSupplementalCell(kit_instrument_slot_t *instrument,
                                         uint8_t index, uint16_t value)
{
    /*
     * Keep all three generic images coherent for a non-morphable selector.
     *
     * Inputs: retained slot, descriptor index, and normalized selector value.
     * Output: main, Morph, and interpolation cells agree. This helper is kept
     * separate from preset_setSupplementalParameter() because load-time
     * normalization must repair retained storage before that public setter
     * applies the active runtime binding; calling the setter three times would
     * repeat DSP side effects and still leave image ownership ambiguous.
     */
    if (!instrument || index >= INSTRUMENT_PARAM_COUNT)
        return;
    instrument->parameter_images.instrument_parameters[index] = value;
    instrument->parameter_images.morph_instrument_parameters[index] = value;
    instrument->parameter_images.morph_interpolation[index] = value;
}

static void preset_normalizeLfoTargetPair(uint8_t scene_index,
                                          uint8_t source_slot,
                                          instrument_binding_kind_t voice_kind,
                                          instrument_binding_kind_t param_kind)
{
    kit_instrument_slot_t *instrument =
        scene_instrumentSlot(scene_index, source_slot);
    uint8_t voice_index;
    uint8_t param_index;
    uint16_t voice;
    uint16_t target;

    /*
     * Reconcile one retained LFO voice/parameter pair before runtime install.
     *
     * Inputs: Scene/source slot plus pair-specific binding kinds. Output: the
     * canonical target is rebuilt from the selected one-based target voice and
     * the stored local descriptor index, or changed to explicit off when that
     * descriptor is not modulatable on the committed target type. Scene target
     * voice 7 preserves only registered LFO Scene IDs. All selector images are
     * repaired together before preset_applyKitVoiceSupplemental() installs the
     * result.
     *
     * This cannot be folded into Menu normalization: files and type swaps reach
     * runtime without visiting Menu, and runtime must never follow a canonical
     * slot that contradicts the adjacent retained target-voice cell.
     */
    if (!instrument ||
        !instrumentManager_descriptorIndexForBinding(instrument->type,
                                                     voice_kind,
                                                     &voice_index) ||
        !instrumentManager_descriptorIndexForBinding(instrument->type,
                                                     param_kind,
                                                     &param_index)) {
        return;
    }
    voice = instrument->parameter_images.instrument_parameters[voice_index];
    target = instrument->parameter_images.instrument_parameters[param_index];
    if (voice < 1u || voice > (uint16_t)(INSTRUMENT_SLOT_COUNT + 1u)) {
        voice = 1u;
        target = INSTRUMENT_PARAM_INVALID;
    } else if (target != INSTRUMENT_PARAM_INVALID) {
        if (voice == (uint16_t)(INSTRUMENT_SLOT_COUNT + 1u)) {
            if (!sceneModTarget_valid(target, SCENE_MOD_TARGET_USE_LFO))
                target = INSTRUMENT_PARAM_INVALID;
        } else if (instrumentParam_isVoiceParameter(target)) {
            instrument_param_id_t candidate = instrumentParam_make(
                (uint8_t)(voice - 1u), instrumentParam_local(target));
            target = instrumentManager_targetValid(
                         scene_index, candidate,
                         INSTRUMENT_TARGET_MODULATION)
                ? candidate : INSTRUMENT_PARAM_INVALID;
        } else {
            target = INSTRUMENT_PARAM_INVALID;
        }
    }
    preset_storeSupplementalCell(instrument, voice_index, voice);
    preset_storeSupplementalCell(instrument, param_index, target);
}

static void preset_normalizeSlotModulationTargets(uint8_t scene_index,
                                                  uint8_t source_slot)
{
    kit_instrument_slot_t *instrument =
        scene_instrumentSlot(scene_index, source_slot);
    uint8_t velocity_index;

    /*
     * Normalize every supplemental destination owned by one source slot.
     *
     * Inputs: committed Scene and source slot. Output: both LFO pairs obey their
     * coupled voice/parameter contract, and velocity is either a valid target
     * for this source or off. Client: the all-source rebind loop after staged
     * Instrument commit and ordinary Kit-slot supplemental apply.
     */
    if (!instrument)
        return;
    preset_normalizeLfoTargetPair(scene_index, source_slot,
        INSTRUMENT_BIND_LFO_TARGET_VOICE,
        INSTRUMENT_BIND_LFO_TARGET_PARAM);
    preset_normalizeLfoTargetPair(scene_index, source_slot,
        INSTRUMENT_BIND_LFO_TARGET_VOICE_2,
        INSTRUMENT_BIND_LFO_TARGET_PARAM_2);
    if (instrumentManager_descriptorIndexForBinding(
            instrument->type, INSTRUMENT_BIND_VELOCITY_TARGET,
            &velocity_index)) {
        uint16_t target =
            instrument->parameter_images.instrument_parameters[velocity_index];
        if (!instrumentManager_targetValidForVelocitySource(
                scene_index, source_slot, target)) {
            target = INSTRUMENT_PARAM_INVALID;
        }
        preset_storeSupplementalCell(instrument, velocity_index, target);
    }
}

static void preset_applyKitVoiceSupplemental(uint8_t scene_index, uint8_t voice)
{
    const kit_instrument_slot_t *instrument =
        scene_instrumentSlotConst(scene_index, voice);
    const instrument_registry_entry_t *entry;
    uint8_t i;

    /*
     * Apply one source slot's normalized non-image runtime bindings.
     *
     * Inputs: resident Scene/slot after its Morph runtime image is current.
     * Output: target selectors and other supplemental cells are installed in
     * descriptor order. Clients are Kit apply and Instrument's six-source
     * graph rebuild. This is separate from audio routing so rebind can revisit
     * all sources without rewriting unrelated mixer routes.
     */
    if (!instrument || voice >= INSTRUMENT_SLOT_COUNT)
        return;
    preset_normalizeSlotModulationTargets(scene_index, voice);
    entry = instrumentManager_registryEntry(instrument->type);
    if (!entry)
        return;
    for (i = 0u; i < entry->descriptor_count; i++) {
        const ParamDescriptor *descriptor = &entry->descriptors[i];
        if (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)
            continue;
        (void)preset_setSupplementalParameter(
            scene_index, voice, i,
            instrument->parameter_images.instrument_parameters[i]);
    }
}

/* Apply one loaded Scene kit slot's non-image affiliates.
 *
 * Why this helper changed: the directory Kit loader now stores audio routing
 * and non-morph runtime cells in SceneData, while morphable image bytes are
 * handled by the Morph worker. A one-slot helper keeps post-load foreground
 * work bounded.
 *
 * Inputs: active Scene index and zero-based slot. Outputs: mixer route,
 * non-morph runtime cells are applied through typed Preset setters. Clients are
 * synchronous boot apply, preset_tickDrumsetApply(), and the one-slot
 * Instrument Load apply cursor.
 */
static void preset_applyKitVoice(uint8_t scene_index, uint8_t voice)
{
    if (!scene_instrumentSlotConst(scene_index, voice) ||
        voice >= INSTRUMENT_SLOT_COUNT) {
        return;
    }

    (void)preset_applyKitAudioRouting(scene_index, voice);
    preset_applyKitVoiceSupplemental(scene_index, voice);
}

/* Synchronous loaded-kit apply.
**
** Safe before audio starts and retained for boot-time loads. Runtime load
** completion should prefer preset_startDrumsetApply()/preset_tickDrumsetApply()
** so the six mod-target updates do not run as one foreground burst. */
void preset_sendDrumsetParameters(void)
{
    uint8_t voice;

    preset_applySceneSettings(scene_getActiveIndex());
    for (voice = 0; voice < 6u; voice++)
        preset_applyKitVoice(scene_getActiveIndex(), voice);

    while (presetMorph_tick()) {
        /* Boot-time synchronous path: audio has not started, so drain the
         * Scene Morph worker immediately after applying routing. Runtime load
         * completion uses preset_tickDrumsetApply() to perform the same work
         * one foreground pass at a time. */
    }
}

void preset_startDrumsetApply(void)
{
    preset_ensureMorphInitialized();
    preset_applySceneSettings(scene_getActiveIndex());
    drumset_apply_active = 1u;
    drumset_apply_voice = 0u;
}

uint8_t preset_tickDrumsetApply(void)
{
    if (!drumset_apply_active)
        return 0u;

    if (drumset_apply_voice < INSTRUMENT_SLOT_COUNT) {
        preset_applyKitVoice(scene_getActiveIndex(), drumset_apply_voice);
        drumset_apply_voice++;
        return 1u;
    }

    if (presetMorph_tick())
        return 1u;

    drumset_apply_active = 0u;
    return 0u;
}

static uint8_t preset_copyInstrumentNormalToMorphIfSameType(
    kit_instrument_slot_t *destination,
    const kit_instrument_slot_t *source)
{
    const instrument_registry_entry_t *entry;
    uint8_t index;
    uint8_t copied = 0u;

    /*
     * Copy one staged source normal endpoint into a resident morph endpoint.
     *
     * Inputs: destination resident slot and staged source slot. Output:
     * morphable descriptor values are copied by descriptor index only when the
     * instrument types match. A mismatch is a complete no-change for that slot,
     * which keeps KitMrp/InstrumentMrp per-instrument and avoids inventing
     * cross-type parameter mapping in the loader.
     */
    if (!destination || !source || destination->type != source->type)
        return 0u;
    entry = instrumentManager_registryEntry(destination->type);
    if (!entry)
        return 0u;
    for (index = 0u; index < entry->descriptor_count; index++) {
        const ParamDescriptor *descriptor = &entry->descriptors[index];
        if (!(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
            continue;
        destination->parameter_images.morph_instrument_parameters[index] =
            source->parameter_images.instrument_parameters[index];
        copied = 1u;
    }
    return copied;
}

static uint8_t preset_commitStagedInstrumentNormalToMorph(
    uint8_t scene_index,
    uint8_t slot)
{
    const kit_instrument_slot_t *staged =
        (const kit_instrument_slot_t *)filesystem_loadedInstrumentSlot();
    kit_instrument_slot_t *destination = scene_instrumentSlot(scene_index, slot);

    /*
     * Commit a staged Instrument file as a morph endpoint update.
     *
     * Unlike preset_startInstrumentApply(), this path does not replace the slot,
     * copy the display name, reset runtime, or clear/rebind modulation. If the
     * staged type no longer matches the resident destination type, the operation
     * is no-change. Otherwise only morphable normal endpoint bytes are copied,
     * and the Morph worker reapplies the current morph amount.
     */
    if (!destination || !staged || slot >= INSTRUMENT_SLOT_COUNT ||
        staged->type != destination->type ||
        staged->type != (instrument_type_t)pm_instrument_request_type) {
        return 0u;
    }
    return preset_copyInstrumentNormalToMorphIfSameType(destination, staged);
}

static uint8_t preset_commitStagedKitNormalToMorph(void)
{
    const kit_t *source = filesystem_loadedKit();
    uint8_t scene_index;
    uint8_t active_queued = 0u;

    /*
     * Commit staged Kit normal endpoints into resident Kit morph endpoints.
     *
     * This deliberately preserves destination kit slot types, display names,
     * audio routing, and supplemental modulation bindings. For each slot, a
     * matching source/destination instrument type copies morphable descriptor
     * values by index; a mismatch skips the whole slot as a no-change. The
     * generated slot-6/track-7 morph setting follows the same matching-slot
     * rule.
     */
    if (!source)
        return 0u;
    for (scene_index = 0u;
         scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        scene_t *scene;
        uint8_t slot;
        if ((pm_kit_request_scene_mask &
             (uint16_t)(1u << scene_index)) == 0u) {
            continue;
        }
        scene = scene_get(scene_index);
        if (!scene)
            continue;
        for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++) {
            if (preset_copyInstrumentNormalToMorphIfSameType(
                    &scene->kit.instruments[slot],
                    &source->instruments[slot]) &&
                scene_index == scene_getActiveIndex()) {
                presetMorph_requestVoice(scene_index, slot);
                active_queued = 1u;
            }
        }
        if (INSTRUMENT_SLOT_COUNT > 5u &&
            scene->kit.instruments[5].type == source->instruments[5].type) {
            scene->kit.settings.slot6_track7_morph_amp_envelope_decay =
                source->settings.slot6_track7_amp_envelope_decay;
            if (scene_index == scene_getActiveIndex()) {
                presetMorph_requestVoice(scene_index, 5u);
                active_queued = 1u;
            }
        }
    }
    return active_queued;
}

void preset_startKitMorphApply(void)
{
    /*
     * Commit staged KitMrp endpoints and arm a morph-only runtime refresh.
     *
     * KitMrp changes morph endpoint storage only. It must not use the normal
     * drumset apply cursor because that cursor reapplies Scene settings,
     * routing, and supplemental target bindings for a replaced kit. Here the
     * resident kit identity is preserved and only the Morph worker is drained.
     */
    instrument_apply_active = 0u;
    preset_ensureMorphInitialized();
    if (preset_commitStagedKitNormalToMorph()) {
        instrument_apply_active = 1u;
        instrument_apply_scene = scene_getActiveIndex();
        instrument_apply_phase = INSTRUMENT_APPLY_PHASE_MORPH_REBUILD;
        instrument_apply_rebind_source = 0u;
        instrument_apply_morph_only = 1u;
    }
}

void preset_startInstrumentMorphApply(uint8_t scene_index, uint8_t slot)
{
    /*
     * Commit staged InstrumentMrp endpoints and arm a morph-only refresh.
     *
     * InstrumentMrp preserves the destination slot identity. It copies no
     * display name, performs no routing apply, and does not clear/rebind
     * modulation targets. A type mismatch is a no-change operation; the cursor
     * remains inactive and Menu will simply unlock on the next poll.
     */
    instrument_apply_active = 0u;
    preset_ensureMorphInitialized();
    if (preset_commitStagedInstrumentNormalToMorph(scene_index, slot) &&
        scene_index == scene_getActiveIndex()) {
        presetMorph_requestVoice(scene_index, slot);
        instrument_apply_active = 1u;
        instrument_apply_scene = scene_index;
        instrument_apply_phase = INSTRUMENT_APPLY_PHASE_MORPH_REBUILD;
        instrument_apply_rebind_source = 0u;
        instrument_apply_morph_only = 1u;
    }
}

void preset_startInstrumentApply(uint8_t scene_index, uint8_t slot)
{
    const kit_instrument_slot_t *staged =
        (const kit_instrument_slot_t *)filesystem_loadedInstrumentSlot();
    scene_t *scene = scene_get(scene_index);

    /*
     * Commit a staged Instrument and arm its bounded runtime transaction.
     *
     * Inputs: immutable request Scene/slot plus filesystem's validated staging
     * image/name. Output: inactive Scenes receive only retained state. For the
     * active Scene, all outgoing modulation owners are cleared before copying
     * the new slot; then the incoming runtime is reset, routing is applied, and
     * a six-slot Morph rebuild is queued before target reinstallation.
     *
     * Clear and reset cannot be one operation: clear must resolve old Scene
     * types, while reset must resolve the newly committed type. Filesystem
     * cannot own either phase because parsing must remain off-scene and cannot
     * know DSP lifecycle order.
     */
    instrument_apply_active = 0u;
    if (!scene || !staged || slot >= INSTRUMENT_SLOT_COUNT ||
        staged->type >= INSTRUMENT_TYPE_UNKNOWN ||
        staged->type != (instrument_type_t)pm_instrument_request_type) {
        return;
    }
    if (scene_index == scene_getActiveIndex())
        instrumentManager_clearAllRuntimeModulationTargets();
    scene->kit.instruments[slot] = *staged;
    /*
     * Commit Instrument source-name metadata with the staged payload.
     *
     * Filesystem captures the selected Instrument filename stem beside the
     * parsed descriptor image. SceneData derives both the 16-character save stem
     * and the eight-character LCD field only after this commit succeeds, so
     * failed loads cannot rename the resident slot.
     */
    scene_setInstrumentSourceName(scene_index, slot,
                                  filesystem_loadedInstrumentStem());

    if (scene_index != scene_getActiveIndex())
        return;

    instrumentManager_resetRuntimeSlot(slot);
    (void)preset_applyKitAudioRouting(scene_index, slot);
    preset_ensureMorphInitialized();
    presetMorph_requestAll(scene_index);
    instrument_apply_active = 1u;
    instrument_apply_scene = scene_index;
    instrument_apply_phase = INSTRUMENT_APPLY_PHASE_MORPH_REBUILD;
    instrument_apply_rebind_source = 0u;
    instrument_apply_morph_only = 0u;
}

uint8_t preset_tickInstrumentApply(void)
{
    /*
     * Advance one bounded unit of the staged Instrument runtime transaction.
     *
     * Inputs are captured by preset_startInstrumentApply(). First, one Morph
     * descriptor per tick is reapplied across all six slots so no stale direct
     * runtime value survives graph clearing. Second, one source slot per tick
     * normalizes and reinstalls supplemental targets against the committed
     * descriptor graph. Output remains nonzero until both phases finish, which
     * keeps Menu's destination-changing controls locked throughout.
     */
    if (!instrument_apply_active)
        return 0u;
    if (instrument_apply_phase == INSTRUMENT_APPLY_PHASE_MORPH_REBUILD) {
        if (presetMorph_tick())
            return 1u;
        if (instrument_apply_morph_only) {
            instrument_apply_active = 0u;
            instrument_apply_morph_only = 0u;
            return 0u;
        }
        instrument_apply_phase = INSTRUMENT_APPLY_PHASE_TARGET_REBIND;
        instrument_apply_rebind_source = 0u;
        return 1u;
    }
    if (instrument_apply_rebind_source < INSTRUMENT_SLOT_COUNT) {
        /*
         * Rebuild every source rather than only the replaced destination.
         *
         * Any LFO/velocity owner may address the committed slot. Processing
         * one source per foreground pass bounds work while ensuring no stale
         * cross-slot pointer or mismatched loaded target pair survives.
         */
        preset_applyKitVoiceSupplemental(instrument_apply_scene,
                                         instrument_apply_rebind_source);
        instrument_apply_rebind_source++;
        return 1u;
    }
    instrument_apply_active = 0u;
    instrument_apply_morph_only = 0u;
    return 0u;
}
/* -----------------------------------------------------------------------
** preset_loadDrumset — post async kit load request.
** ----------------------------------------------------------------------- */
uint8_t preset_loadDrumset(uint16_t presetNr, uint8_t isMorph)
{
    fs_file_type_t type = isMorph ? FS_FILE_MORPH : FS_FILE_KIT;
    fs_completion_cb_t cb = isMorph ? on_morph_load_complete : on_kit_load_complete;

    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = isMorph ? PRESET_REQUEST_LEGACY_MORPH : SAVE_TYPE_KIT;
    if ((!isMorph && filesystem_requestLoadKitForScenes(
                         presetNr,
                         (uint16_t)(1u << scene_getActiveIndex()), cb)) ||
        (isMorph && filesystem_requestLoad(type, presetNr, cb)))
        return 1;
    pm_status = PRESET_IDLE;
    return 0;
}

uint8_t preset_loadKitForScenes(uint16_t presetNr, uint16_t scene_mask)
{
    /*
     * Post the Scene-targeted Kit directory load used by current Load UI.
     *
     * Inputs: Kit browser slot and selected Scene bitmask. Output: asynchronous
     * filesystem work plus Preset completion context. This is separate from
     * preset_loadDrumset() because that legacy public function still accepts a
     * morph compatibility flag; callers that select Scenes need an explicit,
     * unambiguous mask rather than an overloaded boolean.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_KIT;
    if (filesystem_requestLoadKitForScenes(presetNr, scene_mask,
                                           on_kit_load_complete))
        return 1u;
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_loadKitMorphForScenes(uint16_t presetNr, uint16_t scene_mask)
{
    /*
     * Post a new-format Kit Morph load.
     *
     * Inputs: Kit browser slot and selected Scene mask. Output: filesystem
     * parses the same Kit/ directory used by normal Kit Load but stages only;
     * Preset later copies staged normal endpoint values into resident morph
     * endpoints for matching instrument types. Mismatched slots are no-change
     * by design, so KitMrp remains per-instrument instead of replacing kit
     * membership.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_KIT_MORPH;
    pm_kit_request_scene_mask = scene_mask;
    if (filesystem_requestLoadKitMorphForScenes(presetNr, scene_mask,
                                                on_kit_morph_load_complete))
        return 1u;
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_loadSceneForScenes(uint16_t presetNr, uint16_t scene_mask)
{
    /*
     * Post an explicit root Scene Load request.
     *
     * Inputs: root Scene library slot and destination Scene mask from Menu.
     * Output: nonzero only when filesystem accepts the staged multi-file
     * Scene load. Scene Load is separate from Kit Load because its completion
     * may replace settings, pattern, effect state, and the embedded Kit.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_SCENE;
    pm_kit_request_scene_mask = scene_mask;
    if (filesystem_requestLoadSceneForScenes(presetNr, scene_mask,
                                             on_scene_load_complete))
        return 1u;
    pm_status = PRESET_IDLE;
    return 0u;
}

/* -----------------------------------------------------------------------
** preset_saveDrumset — post async kit save request.
** ----------------------------------------------------------------------- */
void preset_saveDrumset(uint16_t presetNr, uint8_t isMorph)
{
    fs_file_type_t type = isMorph ? FS_FILE_MORPH : FS_FILE_KIT;
    fs_completion_cb_t cb = isMorph ? on_morph_save_complete : on_kit_save_complete;

    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = isMorph ? PRESET_REQUEST_LEGACY_MORPH : SAVE_TYPE_KIT;
    /*
     * Normal Kit Save now targets the directory Kit format.
     *
     * The old flat .SND writer remains only for legacy morph compatibility.
     * Saving SAVE_TYPE_KIT streams the active Scene kit through the new storage
     * schema so [params] and [morph] endpoints round-trip with the directory
     * Kit loader.
     */
    if ((!isMorph && !filesystem_requestSaveKitDirectory(presetNr, cb)) ||
        (isMorph && !filesystem_requestSave(type, presetNr, cb)))
        pm_status = PRESET_IDLE;
}

void preset_saveScene(uint16_t presetNr, uint8_t source_scene,
                      const char display_name[8])
{
    /*
     * Post a root Scene directory save.
     *
     * Inputs are captured from the Save UI: target library slot,
     * lowest-numbered selected resident Scene, and the display name used for
     * the numbered folder plus sceneset.scg. Output is an async filesystem
     * writer; completion is reported as PRESET_OP_SCENE_SAVE so Menu can clear
     * busy state and repaint without applying DSP runtime.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_SCENE;
    if (!filesystem_requestSaveSceneDirectory(presetNr, source_scene,
                                              display_name,
                                              on_scene_save_complete))
        pm_status = PRESET_IDLE;
}

/* -----------------------------------------------------------------------
** preset_loadGlobals — post async globals load request.
** ----------------------------------------------------------------------- */
void preset_loadGlobals(void)
{
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = 0;
    pm_request_type = SAVE_TYPE_GLO;
    if (!filesystem_requestLoad(FS_FILE_GLOBALS, 0, on_globals_load_complete))
        pm_status = PRESET_IDLE;
}

/* -----------------------------------------------------------------------
** preset_saveGlobals — post async globals save request.
** ----------------------------------------------------------------------- */
void preset_saveGlobals(void)
{
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = 0;
    pm_request_type = SAVE_TYPE_GLO;
    if (!filesystem_requestSave(FS_FILE_GLOBALS, 0, on_globals_save_complete))
        pm_status = PRESET_IDLE;
}

/* -----------------------------------------------------------------------
** preset_loadName — post async name load request.
** ----------------------------------------------------------------------- */
char* preset_loadName(uint16_t presetNr, uint8_t what)
{
    uint8_t hasName = 0;
    fs_file_type_t type = preset_fileTypeFromSaveType(what, &hasName);

    pm_request_slot = presetNr;
    pm_request_type = what;

    if (!hasName) {
        memcpy(preset_currentName, "        ", 8);
        return preset_currentName;
    }

    memcpy(preset_currentName, "        ", 8);
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    if (!filesystem_requestLoadName(type, presetNr, on_name_load_complete))
        pm_status = PRESET_IDLE;
    return preset_currentName;
}

void preset_applyLoadedName(void)
{
    memcpy(preset_currentName, filesystem_loadedName(), 8);
}

uint8_t preset_loadInstrument(uint8_t destination_scene,
                              uint8_t destination_slot,
                              instrument_type_t type,
                              uint8_t browser_index)
{
    /*
     * Post one immutable staged-Instrument request.
     *
     * Inputs: destination Scene/slot, selected type, and browser cache index.
     * Output: nonzero only when filesystem accepts the operation; Preset
     * publishes completion coordinates and busy state after acceptance. A
     * rejected second request therefore cannot overwrite the Scene/slot used by
     * an in-flight operation's callback. This remains separate from Kit load,
     * whose staged payload and completion apply all six slots.
     */
    filesystem_ack();
    if (!filesystem_requestLoadInstrument(destination_scene, destination_slot,
                                          type, browser_index,
                                          on_instrument_load_complete)) {
        return 0u;
    }
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = destination_slot;
    pm_request_type = SAVE_TYPE_KIT;
    pm_instrument_request_scene = destination_scene;
    pm_instrument_request_slot = destination_slot;
    pm_instrument_request_type = type;
    pm_instrument_request_index = browser_index;
    return 1u;
}

uint8_t preset_loadInstrumentMorph(uint8_t destination_scene,
                                   uint8_t destination_slot,
                                   instrument_type_t type,
                                   uint8_t browser_index)
{
    const kit_instrument_slot_t *destination =
        scene_instrumentSlotConst(destination_scene, destination_slot);

    /*
     * Post an Instrument Morph load for the destination slot's current type.
     *
     * The type equality check is intentional. InstrumentMrp is an endpoint
     * update for the currently loaded slot, not a type-changing operation. The
     * parser stages a full Instrument file through the normal loader, but the
     * later commit copies only same-type morphable normal endpoint values into
     * the resident morph image.
     */
    if (!destination || destination_slot >= INSTRUMENT_SLOT_COUNT ||
        type >= INSTRUMENT_TYPE_UNKNOWN || destination->type != type) {
        return 0u;
    }

    filesystem_ack();
    if (!filesystem_requestLoadInstrument(destination_scene, destination_slot,
                                          type, browser_index,
                                          on_instrument_morph_load_complete)) {
        return 0u;
    }
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = destination_slot;
    pm_request_type = SAVE_TYPE_KIT_MORPH;
    pm_instrument_request_scene = destination_scene;
    pm_instrument_request_slot = destination_slot;
    pm_instrument_request_type = type;
    pm_instrument_request_index = browser_index;
    return 1u;
}

/* =======================================================================
** Pattern / all / performance entry points
** ======================================================================= */

#define FILE_VERSION 2

uint8_t preset_loadPattern(uint8_t presetNr)
{
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = PRESET_REQUEST_LEGACY_PATTERN;
    if (filesystem_requestLoad(FS_FILE_PATTERN, presetNr, on_pattern_load_complete))
        return 1;
    pm_status = PRESET_IDLE;
    return 0;
}

void preset_savePattern(uint8_t presetNr)
{
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = PRESET_REQUEST_LEGACY_PATTERN;
    if (!filesystem_requestSave(FS_FILE_PATTERN, presetNr, on_pattern_save_complete))
        pm_status = PRESET_IDLE;
}

void preset_saveAll(uint8_t presetNr, uint8_t isAll)
{
    fs_file_type_t type = isAll ? FS_FILE_ALL : FS_FILE_PERFORMANCE;
    fs_completion_cb_t cb = isAll ? on_all_save_complete : on_performance_save_complete;

    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = isAll ? PRESET_REQUEST_LEGACY_ALL :
                              PRESET_REQUEST_LEGACY_PERFORMANCE;
    if (!filesystem_requestSave(type, presetNr, cb))
        pm_status = PRESET_IDLE;
}

uint8_t preset_loadAll(uint8_t presetNr, uint8_t isAll)
{
    fs_file_type_t type = isAll ? FS_FILE_ALL : FS_FILE_PERFORMANCE;
    fs_completion_cb_t cb = isAll ? on_all_load_complete : on_performance_load_complete;

    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = isAll ? PRESET_REQUEST_LEGACY_ALL :
                              PRESET_REQUEST_LEGACY_PERFORMANCE;
    if (filesystem_requestLoad(type, presetNr, cb))
        return 1;
    pm_status = PRESET_IDLE;
    return 0;
}

static uint8_t preset_interpolate(uint8_t a, uint8_t b, uint8_t x)
{
    uint16_t fixedPointValue = (uint16_t)(((a * 256) + (b - a) * x));
    uint8_t result = (uint8_t)(fixedPointValue / 256);
    return (uint8_t)((fixedPointValue & 0xff) < 0x7f ? result : result + 1);
}

void preset_morph(uint8_t morph)
{
    /*
     * Set overall Scene Morph by writing all per-voice Morph amounts.
     *
     * Inputs: user-facing 0..255 Morph amount from PERF, global MIDI CC1, or
     * Scene-load fallback. Outputs: the Scene global mirror and all six
     * per-slot Morph amounts are retained, flat PERF menu mirrors are updated,
     * and the Morph worker is queued for every instrument slot. Runtime Morph
     * is still per voice; this function is only the bulk-set operation that
     * gives the user one overall control.
     */
    scene_t *scene;
    uint8_t scene_index = scene_getActiveIndex();

    preset_ensureMorphInitialized();
    scene = scene_get(scene_index);
    if (!scene)
        return;
    scene->settings.morph_amount = morph;
    scene_setAllVoiceMorphAmounts(scene_index, morph);
    preset_syncSceneMorphMirrors(scene);
    presetMorph_requestAll(scene_index);
}

void preset_morphVoice(uint8_t slot, uint8_t morph)
{
    /*
     * Set one Scene voice's Morph amount.
     *
     * Inputs: zero-based instrument slot and 0..255 Morph amount. Output: only
     * that slot's Scene Morph amount and PERF mirror are updated, and only that
     * slot is queued for descriptor Morph interpolation. This function exists
     * separately from preset_morph() because global Morph is a bulk-set
     * operation while MIDI CC1 on a voice channel and PERF 1vm..6vm edits must
     * preserve the other five slot amounts.
     */
    uint8_t scene_index = scene_getActiveIndex();

    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    preset_ensureMorphInitialized();
    scene_setVoiceMorphAmount(scene_index, slot, morph);
    parameter_values[PAR_VOICE1_MORPH + slot] = morph;
    presetMorph_requestVoice(scene_index, slot);
}

void preset_rebuildMorph(void)
{
    /*
     * Requeue Morph from retained Scene values without changing them.
     *
     * Inputs: none; the active Scene supplies all six per-slot Morph amounts.
     * Output: the descriptor-driven Morph worker rebuilds the runtime
     * interpolation image. Clients are endpoint-load/edit refresh paths that
     * must not call preset_morph(), because that would overwrite distinct
     * per-voice Morph amounts with the global bulk-set amount.
     */
    uint8_t scene_index = scene_getActiveIndex();

    preset_ensureMorphInitialized();
    presetMorph_rebuildScene(scene_index);
}

void preset_setVoiceDecimationAll(uint8_t scene_index, uint8_t value)
{
    scene_t *scene = scene_get(scene_index);

    /*
     * Retain and apply Scene global decimation.
     *
     * Inputs: Scene index and PERF `srt` value in the 0..127 menu domain.
     * Outputs: scene_settings_t::voice_decimation_all is retained,
     * parameter_values[] is mirrored for the PERF page, and the active Scene's
     * mixer global decimation multiplier is updated. This function is separate
     * from the MIDI CC handler so future sceneset.scg load/save has one owner
     * for the retained setting and runtime side effect.
     */
    if (!scene)
        return;
    if (value > 127u)
        value = 127u;
    scene->settings.voice_decimation_all = value;
    parameter_values[PAR_VOICE_DECIMATION_ALL] = value;
    if (scene_index == scene_getActiveIndex())
        preset_applyVoiceDecimationAllRuntime(value);
}

void preset_morphTick(void)
{
    /*
     * Compatibility tick called from main.c.
     *
     * Output is intentionally ignored by the caller; the worker itself limits
     * work to one descriptor-backed image value per call. Loaded-kit apply also
     * ticks the same worker while a load completion is active, so both boot and
     * runtime loads share one Morph path.
     */
    (void)presetMorph_tick();
}

uint8_t preset_getMorphValue(uint16_t index, uint8_t morph)
{
    if (index >= END_OF_SOUND_PARAMETERS)
        return 0;
    return preset_interpolate(parameter_values[index], parameters2[index], morph);
}
