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
#include "storageTypes.h"
#include "presetMorphEngine.h"
#include "BankData.h"
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
/*
 * Success flag for the most recent filesystem completion.
 *
 * The old menu often treats failed load/save as "no completed op", but the
 * asyncfatfs expansion tests must distinguish a real zero-byte result from a
 * failed open/read. This flag lets Menu show an explicit ERR overlay while
 * leaving the existing operation enum and musical completion paths intact.
 */
static volatile uint8_t          pm_completed_ok = 0u;
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
/* The selected Instrument row can be any of the shared cache's 1,000 entries. */
static volatile uint16_t         pm_instrument_request_index = 0u;
static volatile uint16_t         pm_kit_request_scene_mask = 0u;

static void preset_markRequestedScenesPresentOnSuccessfulLoad(void)
{
    /*
     * Promote loaded target Scenes into the resident Bank present mask.
     *
     * Inputs: pm_kit_request_scene_mask was captured when the load was accepted,
     * and filesystem_status() still reflects the just-completed async request.
     * Output: successful Kit, Scene, and Instrument loads mark their destination
     * Scenes as present so PERF switching and MODE VOICE fan-out can address
     * them immediately. Failed loads do not change BankData, preserving the last
     * known resident child map. KitMrp/InstrumentMrp intentionally do not call
     * this helper because morph-only loads do not assign an empty Scene.
     */
    if (filesystem_status() == FS_STATUS_DONE) {
        bank_setScenePresentMask((uint16_t)(bank_scenePresentMask() |
                                           pm_kit_request_scene_mask));
    }
}

/* Runtime loaded-kit apply cursor.
 *
 * Why this changed: directory Kit files now parse into Scene-owned
 * kit_instrument_slot_t images, while the legacy implementation only applied
 * modulation routing from parameter_values[]. The cursor therefore owns the
 * bounded post-load bridge from Scene state to current DSP affiliates.
 *
 * Inputs: preset_startDrumsetApply() selects the active Scene and arms a bit
 * per instrument slot. preset_tickDrumsetApply() scans that mask for one slot
 * whose currently sounding amp envelope is quiet. Trigger dispatch can also
 * force one pending slot through preset_applyDeferredSceneSlotForTrigger().
 *
 * Outputs: mixer_audioRouting[], instrument runtime type/parameters,
 * supplemental modulation targets, and Morph interpolation are updated from
 * SceneData only when a slot commits. Immediate Scene-wide settings such as
 * Morph menu mirrors and global decimation apply at Scene switch time.
 */
static uint8_t drumset_apply_active = 0;
static uint8_t drumset_apply_voice = 0;
static uint8_t drumset_apply_scene = 0u;
static uint16_t drumset_apply_pending_mask = 0u;
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

uint8_t preset_getCompletedOk(void)
{
    return pm_completed_ok;
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
     * Expose the single Scene captured for an Instrument action or Kit Save.
     *
     * Output: Preset's retained destination/source Scene index. Menu reads it
     * at async completion to apply the same Instrument coordinate or refresh
     * the same Kit Save HCNAMES block that the filesystem request captured.
     * This cannot be folded into preset_getRequestSlot(): library slot and
     * resident Scene are independent coordinates, and using later Menu state
     * would let an asynchronous completion target the wrong resident object.
     */
    return pm_instrument_request_scene;
}

uint16_t preset_getKitRequestSceneMask(void)
{
    /*
     * Expose the immutable resident Scene mask captured for a Kit-family load.
     *
     * Output: the exact mask accepted by preset_loadKitForScenes() or its Morph
     * counterpart. Menu uses it after normal full Kit Load to update one Kit
     * row plus six Instrument rows for every committed destination. Returning
     * the existing field adds no storage and avoids consulting a potentially
     * changed panel selection after the asynchronous load has completed.
     */
    return pm_kit_request_scene_mask;
}

void preset_ackStatus(void)
{
    pm_status = PRESET_IDLE;
    pm_completed_op = PRESET_OP_NONE;
    pm_completed_ok = 0u;
}

/* -----------------------------------------------------------------------
** Completion callbacks — fired by filesystem_tick() context
** ----------------------------------------------------------------------- */
static void preset_completeFilesystemOp(preset_op_type_t completed_op)
{
    fs_status_t fs_status = filesystem_status();
    uint8_t completed_ok = (uint8_t)(fs_status == FS_STATUS_DONE);
    uint8_t is_test_op = (uint8_t)(completed_op == PRESET_OP_TEST_SCAN ||
                                   completed_op == PRESET_OP_TEST_FILE_LOAD ||
                                   completed_op == PRESET_OP_TEST_DIR_LOAD ||
                                   completed_op == PRESET_OP_TEST_FILE_SAVE ||
                                   completed_op == PRESET_OP_TEST_DIR_SAVE);

    filesystem_ack();
    pm_completed_ok = completed_ok;
    if (completed_ok || is_test_op || completed_op != PRESET_OP_NONE) {
        /*
         * Preserve completion identity for operations that report failure.
         *
         * Older musical callers once collapsed failures to PRESET_OP_NONE,
         * which made the UI silently reset with no clue where filesystem.c had
         * stopped. Keep the requested op identity on error and let Menu decide
         * between success cleanup and an ERR overlay using pm_completed_ok and
         * filesystem_errorCode().
         */
        pm_completed_op = completed_op;
        pm_status = PRESET_UPDATE_READY;
    } else {
        pm_completed_op = PRESET_OP_NONE;
        pm_status = PRESET_UPDATE_READY;
    }
}

static void on_kit_load_complete(void)
{
    preset_markRequestedScenesPresentOnSuccessfulLoad();
	preset_completeFilesystemOp(PRESET_OP_KIT_LOAD);
}

static void on_kit_save_complete(void)
{
    /*
     * Report one completed normal Kit directory save.
     *
     * filesystem.c has already handled slot cleanup, directory creation,
     * member-file streaming, kitset emission, and final flush/error reporting.
     * Preset keeps this callback intentionally small: Menu only needs the
     * operation identity so it can clear Save busy state and repaint at the
     * top-row type selector.
     */
    preset_completeFilesystemOp(PRESET_OP_KIT_SAVE);
}

static void on_kit_morph_save_complete(void)
{
    /*
     * Report one completed KitMrp directory save.
     *
     * KitMrp uses the same directory writer as normal Kit Save, but filesystem
     * serializes the Morph Save projection: current interpolated values are
     * written into both normal and morph endpoint sections. Resident Scene kit
     * names and instrument stems are not changed by this export.
     */
    preset_completeFilesystemOp(PRESET_OP_KIT_MORPH_SAVE);
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
    preset_markRequestedScenesPresentOnSuccessfulLoad();
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_LOAD);
}

static void on_instrument_save_complete(void)
{
    /*
     * Complete one root Instrument Save.
     *
     * Filesystem has already written Instrument/<name.ext>, closed it, and
     * updated its Instrument browser cache. Preset reports a save completion
     * rather than an apply completion because no SceneData or DSP runtime state
     * changes when exporting a resident instrument.
     */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_SAVE);
}

static void on_instrument_temp_save_complete(void)
{
    /*
     * Report completion of the hidden normal Instrument Load source write.
     *
     * Input: filesystem has closed `.hctmp.<ext>` in one type directory.
     * Output: Menu may now load that type's `.hcindex` and permit the `kit`
     * row. No Scene/DSP/HCNAMES work occurs here. Affiliates: Menu entry
     * sequencing and filesystem_requestSaveInstrumentTemp().
     */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_TEMP_SAVE);
}

static void on_instrument_morph_save_complete(void)
{
    /*
     * Complete one root Instrument Morph Save.
     *
     * Filesystem has written Instrument/<name.ext> with the Morph Save
     * projection, where [params] and [morph] both hold the current
     * interpolated endpoint. Preset reports this as a separate completion so
     * Menu can reset nested Save UI without treating it as a normal Instrument
     * export or a legacy flat Morph file.
     */
    preset_completeFilesystemOp(PRESET_OP_INSTRUMENT_MORPH_SAVE);
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

static void on_scene_load_complete(void)
{
    preset_markRequestedScenesPresentOnSuccessfulLoad();
    preset_completeFilesystemOp(PRESET_OP_SCENE_LOAD);
}

static void on_scene_save_complete(void)
{
    /*
     * Complete one root Scene directory save.
     *
     * Filesystem has already serialized sceneset.scg, the embedded Kit
     * directory, pattern stub, and effect placeholder. Preset reports only the
     * operation identity so Menu can clear Save busy state without starting any
     * runtime sound-apply work.
     */
    preset_completeFilesystemOp(PRESET_OP_SCENE_SAVE);
}

static void on_bank_load_complete(void)
{
    /*
     * Complete one root Bank load.
     *
     * Filesystem has validated bankset.bcg and either loaded a Bank-local
     * Scene child or reported a valid empty Bank through
     * filesystem_lastBankLoadLoadedScene(). Menu/boot decide whether to run
     * the fallback chain after reading that bit.
     */
    preset_completeFilesystemOp(PRESET_OP_BANK_LOAD);
}

static void on_bank_save_complete(void)
{
    /*
     * Complete one root Bank save.
     *
     * The save writes bankset.bcg plus the selected Bank-local Scene children.
     * It does not imply runtime DSP changes, so Menu can share the ordinary
     * Save completion cleanup used by Scene Save.
     */
    preset_completeFilesystemOp(PRESET_OP_BANK_SAVE);
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

/*
 * Retired generic File/Dir diagnostics.
 *
 * Their menu types and filesystem caches were removed to keep SRAM within the
 * musical-name contract. Keep the old completion adapters out of the build so
 * no Preset path can issue a request for the retired diagnostic API.
 */
#if 0
static void on_test_scan_complete(void)
{
    /*
     * Complete a generic File/Dir test scan.
     *
     * No SceneData or DSP state is touched; Menu will read the filesystem
     * scan cache directly and repaint the temporary browser list.
     */
    preset_completeFilesystemOp(PRESET_OP_TEST_SCAN);
}

static void on_test_file_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_TEST_FILE_LOAD);
}

static void on_test_dir_load_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_TEST_DIR_LOAD);
}

static void on_test_file_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_TEST_FILE_SAVE);
}

static void on_test_dir_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_TEST_DIR_SAVE);
}

#endif

static fs_file_type_t preset_fileTypeFromSaveType(uint8_t what, uint8_t *hasName)
{
    if (hasName) *hasName = 1;

    switch (what) {
    case SAVE_TYPE_KIT:         return FS_FILE_KIT;
    case SAVE_TYPE_KIT_MORPH:   return FS_FILE_KIT;
    case SAVE_TYPE_SCENE:       return FS_FILE_SCENE;
    case SAVE_TYPE_BANK:        return FS_FILE_BANK;
    case SAVE_TYPE_GLO:
        if (hasName) *hasName = 0;
        return FS_FILE_SETTINGS;
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
                                                          instrument_param_value_t value,
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
                                           instrument_param_value_t value)
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
     * The public value is byte-sized because SceneData, files, Menu, and Morph
     * all retain descriptor values in byte space. Target selector bytes are
     * expanded by InstrumentManager only while applying runtime modulation
     * destinations.
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
                                        uint8_t descriptor_index,
                                        instrument_param_value_t value)
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

    /*
     * Supplemental target cells are retained as byte tokens.
     *
     * Velocity target rows store a self-scoped byte token, and LFO target rows
     * store a local token interpreted through their paired target-voice row.
     * Preset retains the byte value unchanged; InstrumentManager expands it to
     * a canonical target ID only while applying the active Scene's runtime
     * modulation graph.
     */
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
     * Apply one Scene-owned per-voice routing byte.
     *
     * Input is a Scene/slot pair; output is the active mixer routing and
     * parameter_values[] compatibility byte. The mixer array remains the DSP
     * affiliate until routing is absorbed into a typed Scene apply backend.
     * Clients are the bounded loaded-kit/Scene apply cursor and VOICE mix
     * Scene-setting UI edits. Routing moved out of kit_settings_t so root Kit
     * swaps do not overwrite a Scene's output assignment.
     */
    route = scene_getVoiceAudioOut(scene_index, slot);
    if (route > MIXER_ROUTING_DAC2_R)
        route = MIXER_ROUTING_DAC1_STEREO;
    if (scene_index == scene_getActiveIndex()) {
        mixer_audioRouting[slot] = route;
    }
    return 1u;
}

uint8_t preset_setVoiceAudioOut(uint8_t scene_index, uint8_t slot,
                                uint8_t route)
{
    /*
     * Retain and apply one Scene per-voice output route.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and raw route
     * from menu/storage. Output: SceneData stores the route and active Scene
     * runtime routing is refreshed immediately. Clamping here uses mixer.h's
     * route enum, keeping SceneData independent of DSP headers.
     */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (route > MIXER_ROUTING_DAC2_R)
        route = MIXER_ROUTING_DAC1_STEREO;
    scene_setVoiceAudioOut(scene_index, slot, route);
    return preset_applyKitAudioRouting(scene_index, slot);
}

uint8_t preset_setVoiceFxSendAmount(uint8_t scene_index, uint8_t slot,
                                    uint8_t amount)
{
    /*
     * Retain one Scene FX-send amount without runtime side effects yet.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and 0..127
     * amount. Output: SceneData retains the value. The eventual FX bus should
     * attach its active-scene runtime write here so Menu/storage callers keep
     * one owner boundary.
     */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (amount > 127u)
        amount = 127u;
    scene_setVoiceFxSendAmount(scene_index, slot, amount);
    return 1u;
}

uint8_t preset_setVoiceFaderSetting(uint8_t scene_index, uint8_t slot,
                                    uint8_t mode)
{
    /*
     * Retain one Scene fader mode without runtime side effects yet.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and 0..2 mode.
     * Output: SceneData retains the mode. Future fader topology code should
     * add active-scene apply here instead of teaching Menu about mixer internals.
     */
    if (!scene_get(scene_index) || slot >= INSTRUMENT_SLOT_COUNT)
        return 0u;
    if (mode > 2u)
        mode = 2u;
    scene_setVoiceFaderSetting(scene_index, slot, mode);
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
     * Apply immediate Scene-wide settings that still have legacy mirrors.
     *
     * Inputs: active Scene index. Outputs: flat PERF mirrors are synchronized
     * from retained Scene settings and global decimation is applied. Instrument
     * runtime parameters, voice LFO slots/targets, audio out, future FX sends,
     * and fader assignments are deliberately excluded; the deferred slot worker
     * commits those per-instrument affiliates only when the old envelope is quiet
     * or when the new Scene pattern triggers that slot.
     *
     * This must not call preset_morph(), because preset_morph() is now the
     * user-facing bulk-set operation and would overwrite distinct per-voice Morph
     * values loaded from future sceneset.scg data.
     */
    preset_ensureMorphInitialized();
    preset_syncSceneMorphMirrors(scene);
    parameter_values[PAR_VOICE_DECIMATION_ALL] =
        scene->settings.voice_decimation_all;
    preset_applyVoiceDecimationAllRuntime(scene->settings.voice_decimation_all);
}

static void preset_storeSupplementalCell(kit_instrument_slot_t *instrument,
                                         uint8_t index,
                                         instrument_param_value_t value)
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
    instrument_param_value_t voice;
    instrument_target_token_t token;

    /*
     * Reconcile one retained LFO voice/parameter pair before runtime install.
     *
     * Inputs: Scene/source slot plus pair-specific binding kinds. Output: the
     * retained token is checked against the selected one-based target voice.
     * Voice namespaces store descriptor-local tokens; the Scene namespace
     * stores Scene target-table indices. Invalid pairs are changed to explicit
     * byte off before preset_applyKitVoiceSupplemental() installs the result.
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
    token = instrument->parameter_images.instrument_parameters[param_index];
    if (!instrumentManager_lfoTargetVoiceValid(voice)) {
        voice = 1u;
        token = INSTRUMENT_TARGET_TOKEN_OFF;
    } else {
        instrument_param_id_t id =
            instrumentManager_lfoTargetIdFromToken(
                scene_index, source_slot, voice, token,
                INSTRUMENT_TARGET_MODULATION);
        token = instrumentManager_lfoTargetTokenFromId(
            scene_index, voice, id, INSTRUMENT_TARGET_MODULATION);
    }
    preset_storeSupplementalCell(instrument, voice_index, voice);
    preset_storeSupplementalCell(instrument, param_index, token);
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
        instrument_target_token_t target =
            instrument->parameter_images.instrument_parameters[velocity_index];
        if (!instrumentManager_targetValidForVelocitySource(
                scene_index, source_slot, target)) {
            target = INSTRUMENT_TARGET_TOKEN_OFF;
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

    /*
     * Rebind the DSP runtime type at the same time as parameter commit.
     *
     * Input: active Scene slot selected by the deferred apply worker. Output:
     * InstrumentManager's runtime type shadow and the concrete DSP voice object
     * are reset immediately before descriptor/audio/supplemental values are
     * copied in. Inactive Scene retained writes must not call this path because
     * the runtime shadow always belongs to the audible Scene.
     */
    if (scene_index == scene_getActiveIndex())
        instrumentManager_resetRuntimeSlot(voice);
    (void)preset_applyKitAudioRouting(scene_index, voice);
    preset_applyKitVoiceSupplemental(scene_index, voice);
    presetMorph_applyVoiceNow(scene_index, voice);
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
    drumset_apply_scene = scene_getActiveIndex();
    drumset_apply_pending_mask =
        (uint16_t)((1u << INSTRUMENT_SLOT_COUNT) - 1u);
    drumset_apply_active = 1u;
    drumset_apply_voice = 0u;
}

uint8_t preset_tickDrumsetApply(void)
{
    uint8_t checked;

    if (!drumset_apply_active)
        return 0u;

    if (drumset_apply_scene != scene_getActiveIndex()) {
        /*
         * A newer Scene switch supersedes this worker.
         *
         * Input: active Scene changed since the pending mask was armed. Output:
         * stale slot bits are discarded so the old Scene can never apply over
         * the current Scene. preset_startDrumsetApply() has already armed the
         * replacement worker for the new active Scene.
         */
        drumset_apply_active = 0u;
        drumset_apply_pending_mask = 0u;
        return 0u;
    }

    for (checked = 0u; checked < INSTRUMENT_SLOT_COUNT; checked++) {
        uint8_t voice = drumset_apply_voice;
        uint16_t bit = (uint16_t)(1u << voice);

        /*
         * Scan the pending bitmask round-robin.
         *
         * Inputs: drumset_apply_voice is the next slot to test, preserving fair
         * progress when several envelopes are ringing. Output: at most one quiet
         * slot is committed per foreground pass; non-quiet slots remain pending
         * for a later tick or trigger-time force-apply.
         */
        drumset_apply_voice++;
        if (drumset_apply_voice >= INSTRUMENT_SLOT_COUNT)
            drumset_apply_voice = 0u;
        if ((drumset_apply_pending_mask & bit) == 0u)
            continue;
        if (!instrumentManager_ampEnvelopeQuiet(voice))
            continue;

        preset_applyKitVoice(drumset_apply_scene, voice);
        drumset_apply_pending_mask =
            (uint16_t)(drumset_apply_pending_mask & ~bit);
        if (drumset_apply_pending_mask == 0u)
            drumset_apply_active = 0u;
        return 1u;
    }

    if (drumset_apply_pending_mask == 0u)
        drumset_apply_active = 0u;
    return 0u;
}

void preset_applyDeferredSceneSlotForTrigger(uint8_t trigger_track)
{
    uint8_t voice;
    uint16_t bit;

    /*
     * Force a pending Scene slot to commit immediately before it triggers.
     *
     * Inputs: visible trigger track 0..6 from the sequencer/MIDI trigger queue.
     * Output: if that track maps to a pending instrument slot for the active
     * Scene, the slot's runtime type, parameters, LFO slots/targets, audio out,
     * and future per-instrument mix affiliates are applied synchronously before
     * InstrumentManager receives the note trigger.
     */
    if (!drumset_apply_active ||
        drumset_apply_scene != scene_getActiveIndex()) {
        return;
    }
    if (trigger_track > INSTRUMENT_SLOT_COUNT)
        return;
    voice = (trigger_track >= INSTRUMENT_SLOT_COUNT)
        ? (INSTRUMENT_SLOT_COUNT - 1u)
        : trigger_track;
    bit = (uint16_t)(1u << voice);
    if ((drumset_apply_pending_mask & bit) == 0u)
        return;

    presetMorph_prioritizeVoice(drumset_apply_scene, voice);
    preset_applyKitVoice(drumset_apply_scene, voice);
    drumset_apply_pending_mask =
        (uint16_t)(drumset_apply_pending_mask & ~bit);
    if (drumset_apply_pending_mask == 0u)
        drumset_apply_active = 0u;
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

static void preset_startInstrumentApplyImage(const kit_instrument_slot_t *staged,
                                             uint16_t destination_mask,
                                             uint8_t slot,
                                             instrument_type_t expected_type)
{
    uint8_t target_scene_index;
    uint8_t active_scene_touched = 0u;

    /*
     * Commit one validated Instrument image and arm the bounded runtime apply.
     *
     * Inputs: an immutable typed stage, exact destination Scene mask, voice
     * slot, and expected type. Output: every selected Scene receives audio
     * parameters and the active Scene receives the existing ordered
     * modulation-clear/reset/routing/Morph apply. The public normal loader and
     * the reversible `kit` restore both use this one lifecycle path, so a
     * restore cannot leave stale runtime targets or differ from a file load.
     *
     * Affiliates: preset_startInstrumentApply(),
     * preset_loadInstrumentTemp(), filesystem_loadedInstrumentSlot(),
     * and filesystem_instrumentLoadPreviewOriginal().
     */
    instrument_apply_active = 0u;
    if (!staged || slot >= INSTRUMENT_SLOT_COUNT ||
        staged->type >= INSTRUMENT_TYPE_UNKNOWN ||
        staged->type != expected_type) {
        return;
    }
    if ((destination_mask & (uint16_t)(1u << scene_getActiveIndex())) != 0u)
        instrumentManager_clearAllRuntimeModulationTargets();
    for (target_scene_index = 0u;
         target_scene_index < SCENE_COUNT && target_scene_index < 16u;
         target_scene_index++) {
        scene_t *scene;

        if ((destination_mask & (uint16_t)(1u << target_scene_index)) == 0u)
            continue;
        scene = scene_get(target_scene_index);
        if (!scene)
            continue;
        scene->kit.instruments[slot] = *staged;
        /*
         * Do not attach a filename or display-name copy to the resident slot.
         *
         * Why: filesystem has already placed the successful eight-cell name in
         * the operation identity block for its targeted HCNAMES update. Inputs:
         * validated staged descriptor and destination coordinates. Output:
         * audio data only; name authority remains `/.hcnames`.
         * Affiliates: filesystem_loadedInstrumentSlot(), HCNAMES Instrument
         * writer, and Menu's Instrument session exit.
         */
        if (target_scene_index == scene_getActiveIndex())
            active_scene_touched = 1u;
    }

    if (!active_scene_touched)
        return;

    instrumentManager_resetRuntimeSlot(slot);
    (void)preset_applyKitAudioRouting(scene_getActiveIndex(), slot);
    preset_ensureMorphInitialized();
    presetMorph_requestAll(scene_getActiveIndex());
    instrument_apply_active = 1u;
    instrument_apply_scene = scene_getActiveIndex();
    instrument_apply_phase = INSTRUMENT_APPLY_PHASE_MORPH_REBUILD;
    instrument_apply_rebind_source = 0u;
    instrument_apply_morph_only = 0u;
}

void preset_startInstrumentApply(uint8_t scene_index, uint8_t slot)
{
    const kit_instrument_slot_t *staged =
        (const kit_instrument_slot_t *)filesystem_loadedInstrumentSlot();
    uint16_t destination_mask = pm_kit_request_scene_mask
        ? pm_kit_request_scene_mask
        : (uint16_t)(1u << scene_index);

    /*
     * Apply the validated filesystem Instrument candidate through the shared
     * image commit helper.
     *
     * Inputs: callback-captured Scene/slot plus Preset's immutable request
     * type/mask. Output: the normal Instrument file result commits exactly as
     * before. Keeping this wrapper preserves the filesystem completion API
     * while allowing the `kit` preview restore to reuse the same DSP ordering.
     * Affiliates: on_instrument_load_complete() and Menu completion polling.
     */
    preset_startInstrumentApplyImage(staged, destination_mask, slot,
                                     (instrument_type_t)pm_instrument_request_type);
}

uint8_t preset_saveInstrumentTemp(uint8_t source_scene, uint8_t source_slot)
{
    /*
     * Save one voice as the hidden reversible normal-Load source.
     *
     * Inputs: Menu's entry Scene/voice. Output: Preset holds the immutable
     * coordinates until filesystem writes `.hctmp.<ext>`; completion is a
     * dedicated UI sequencing event, not an Instrument Save or name update.
     * Affiliates: on_instrument_temp_save_complete() and Menu entry handling.
     */
    filesystem_ack();
    if (!filesystem_requestSaveInstrumentTemp(source_scene, source_slot,
                                              on_instrument_temp_save_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = source_slot;
    pm_instrument_request_scene = source_scene;
    pm_instrument_request_slot = source_slot;
    return 1u;
}

uint8_t preset_loadInstrumentTemp(uint8_t destination_scene,
                                  uint8_t destination_slot,
                                  instrument_type_t type)
{
    /*
     * Route the direct `kit` row through the normal Instrument apply path.
     *
     * Inputs: the exact temporary-file Scene/voice/type context retained by
     * Menu. Output: the regular Instrument completion applies parsed data to
     * that voice, while filesystem suppresses HCNAMES publication for the
     * hidden file. Affiliates: on_instrument_load_complete() and Menu cursor.
     */
    filesystem_ack();
    if (!filesystem_requestLoadInstrumentTemp(destination_scene,
                                              destination_slot,
                                              type,
                                              on_instrument_load_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = destination_slot;
    pm_instrument_request_scene = destination_scene;
    pm_instrument_request_slot = destination_slot;
    pm_instrument_request_type = type;
    return 1u;
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
    pm_kit_request_scene_mask = (uint16_t)(1u << scene_getActiveIndex());
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
     *
     * pm_kit_request_scene_mask is captured before the asynchronous request so
     * on_kit_load_complete() can promote exactly the loaded destination Scenes
     * into BankData's resident-present mask after filesystem success.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_KIT;
    pm_kit_request_scene_mask = scene_mask;
    if (filesystem_requestLoadKitForScenes(presetNr, scene_mask,
                                           on_kit_load_complete))
        return 1u;
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_saveDrumset(uint16_t presetNr, uint8_t isMorph,
                           uint8_t source_scene)
{
    /*
     * Post normal Kit Save or KitMrp Save to the shared directory writer.
     *
     * Inputs are captured here because filesystem work is asynchronous: the
     * target slot, selected source Scene, editable eight-cell display name, and
     * Morph projection flag must remain fixed even if the user moves the panel
     * before the SD operation completes. The Morph flag is not a legacy flat
     * .SND request; it selects the new-format snapshot-twice projection inside
     * storage/filesystem.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = isMorph ? SAVE_TYPE_KIT_MORPH : SAVE_TYPE_KIT;
    /*
     * Reuse the existing single-Scene request coordinate for Kit Save.
     *
     * Input: source_scene accepted by the Save request. Output: Menu can read
     * the same immutable source after the physical Kit and `/Kit/.hcindex` are
     * durable, even though menu_resetSaveParameters() has already reset its UI
     * Scene selection. No additional request-state byte is introduced.
     */
    pm_instrument_request_scene = source_scene;
    if (filesystem_requestSaveKitDirectory(presetNr,
                                           source_scene,
                                           preset_currentName,
                                           isMorph,
                                           isMorph ? on_kit_morph_save_complete
                                                   : on_kit_save_complete)) {
        return 1u;
    }
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

uint8_t preset_saveScene(uint16_t presetNr, uint8_t source_scene)
{
    /*
     * Post a root Scene Save request.
     *
     * Inputs: target root Scene slot plus selected source Scene and edited
     * preset_currentName captured here. Output: filesystem-owned asynchronous
     * directory write. Scene Save is not a runtime mutation, so completion only
     * repaints/clears UI busy state.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_SCENE;
    if (filesystem_requestSaveSceneDirectory(presetNr,
                                             source_scene,
                                             preset_currentName,
                                             on_scene_save_complete)) {
        return 1u;
    }
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_loadBank(uint16_t presetNr, uint16_t scene_mask)
{
    /*
     * Post an explicit root Bank Load request.
     *
     * Inputs: root Bank library slot and destination resident Scene mask.
     * Output: nonzero when filesystem accepts the Bank container load. A
     * successful completion may still have no Scene child; callers inspect
     * preset_completedBankLoadedScene() before deciding whether to apply sound
     * or start fallback.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_BANK;
    pm_kit_request_scene_mask = scene_mask;
    if (filesystem_requestLoadBank(presetNr, scene_mask,
                                   on_bank_load_complete))
        return 1u;
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_saveBank(uint16_t presetNr, uint16_t scene_mask)
{
    uint8_t source_scene = scene_getActiveIndex();

    /*
     * Post a multi-Scene Bank Save.
     *
     * Inputs: root Bank slot plus edited preset_currentName as the Bank
     * directory name and the caller-selected resident Scene mask. Output:
     * filesystem writes bankset.bcg and one Bank-local child folder per
     * selected bit. source_scene remains the active Scene for compatibility
     * validation, but the mask determines the actual child set.
     */
    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = SAVE_TYPE_BANK;
    if (filesystem_requestSaveBank(presetNr,
                                   source_scene,
                                   preset_currentName,
                                   scene_mask,
                                   on_bank_save_complete)) {
        return 1u;
    }
    pm_status = PRESET_IDLE;
    return 0u;
}

uint8_t preset_completedBankLoadedScene(void)
{
    return filesystem_lastBankLoadLoadedScene();
}

uint8_t preset_loadFirstAvailableSceneOrKit(void)
{
    uint16_t slot;

    /*
     * Start the Bank empty/failure fallback chain.
     *
     * Order is root Scene, then root Kit, then SRAM defaults. The helpers
     * return STORAGE_*_MAX_SLOTS as the absent sentinel because slot 000 is a
     * valid library slot and cannot mean "none".
     */
    slot = filesystem_firstSceneSlot();
    if (slot < STORAGE_SCENE_MAX_SLOTS)
        return preset_loadSceneForScenes(slot, 1u);
    slot = filesystem_firstKitSlot();
    if (slot < STORAGE_KIT_MAX_SLOTS)
        return preset_loadKitForScenes(slot, 1u);
    return 0u;
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
    if (!filesystem_requestLoad(FS_FILE_SETTINGS, 0, on_globals_load_complete))
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
    if (!filesystem_requestSave(FS_FILE_SETTINGS, 0, on_globals_save_complete))
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
                              uint16_t browser_index)
{
    return preset_loadInstrumentForScenes((uint16_t)(1u << destination_scene),
                                          destination_slot,
                                          type,
                                          browser_index);
}

uint8_t preset_loadInstrumentForScenes(uint16_t destination_scene_mask,
                                       uint8_t destination_slot,
                                       instrument_type_t type,
                                       uint16_t browser_index)
{
    uint8_t scene_index;
    uint8_t request_scene = 0u;
    uint16_t valid_mask = 0u;

    /*
     * Post one immutable staged-Instrument request for one or more Scenes.
     *
     * Inputs: destination Scene mask, slot, selected type, and browser cache
     * index. Output: one filesystem read from Instrument/; after completion
     * preset_startInstrumentApply() commits the staged slot to every valid
     * masked Scene. The representative request_scene is only the staging
     * coordinate needed by the parser and compatibility completion accessors.
     */
    for (scene_index = 0u; scene_index < SCENE_COUNT && scene_index < 16u;
         scene_index++) {
        if ((destination_scene_mask & (uint16_t)(1u << scene_index)) != 0u &&
            scene_instrumentSlotConst(scene_index, destination_slot)) {
            valid_mask = (uint16_t)(valid_mask | (uint16_t)(1u << scene_index));
            if (valid_mask == (uint16_t)(1u << scene_index))
                request_scene = scene_index;
        }
    }
    if (valid_mask == 0u)
        return 0u;
    filesystem_ack();
    if (!filesystem_requestLoadInstrument(request_scene, destination_slot,
                                          type, browser_index,
                                          on_instrument_load_complete)) {
        return 0u;
    }
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = destination_slot;
    pm_request_type = SAVE_TYPE_KIT;
    pm_instrument_request_scene = request_scene;
    pm_instrument_request_slot = destination_slot;
    pm_instrument_request_type = type;
    pm_instrument_request_index = browser_index;
    pm_kit_request_scene_mask = valid_mask;
    return 1u;
}

uint8_t preset_saveInstrument(uint8_t source_scene,
                              uint8_t source_slot,
                              const char *display_name)
{
    /*
     * Post one immutable root Instrument Save request.
     *
     * Inputs: resident source Scene/voice and the user-edited file stem. Output
     * is nonzero only after filesystem accepts the write. Completion context
     * retains the source coordinates for UI bookkeeping, but no runtime apply
     * follows because saving does not mutate resident SceneData.
     */
    filesystem_ack();
    if (!filesystem_requestSaveInstrument(source_scene,
                                          source_slot,
                                          display_name,
                                          on_instrument_save_complete)) {
        return 0u;
    }
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = source_slot;
    pm_request_type = SAVE_TYPE_KIT;
    pm_instrument_request_scene = source_scene;
    pm_instrument_request_slot = source_slot;
    pm_instrument_request_type = INSTRUMENT_TYPE_UNKNOWN;
    pm_instrument_request_index = 0u;
    return 1u;
}

uint8_t preset_saveInstrumentMorph(uint8_t source_scene,
                                   uint8_t source_slot,
                                   const char *display_name)
{
    /*
     * Post one immutable root Instrument Morph Save request.
     *
     * Inputs: resident source Scene/voice plus the edited Instrument stem.
     * Output: nonzero only when filesystem accepts the write. The request type
     * is KitMrp because the nested Instrument Save surface is reached from the
     * Kit/KitMrp Load-Save context, but completion stays instrument-specific so
     * Menu can clear nested busy state exactly like normal Instrument Save.
     * Affiliates: filesystem_requestSaveInstrumentMorph(), storageTypes'
     * Morph Save write view, and menu_instrumentSaveRequestSelection().
     */
    filesystem_ack();
    if (!filesystem_requestSaveInstrumentMorph(
            source_scene,
            source_slot,
            display_name,
            on_instrument_morph_save_complete)) {
        return 0u;
    }
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = source_slot;
    pm_request_type = SAVE_TYPE_KIT_MORPH;
    pm_instrument_request_scene = source_scene;
    pm_instrument_request_slot = source_slot;
    pm_instrument_request_type = INSTRUMENT_TYPE_UNKNOWN;
    pm_instrument_request_index = 0u;
    return 1u;
}

uint8_t preset_loadInstrumentMorph(uint8_t destination_scene,
                                   uint8_t destination_slot,
                                   instrument_type_t type,
                                   uint16_t browser_index)
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

/* Retired File/Dir diagnostic request adapters; see disabled callbacks above. */
#if 0
uint8_t preset_scanTestFiles(void)
{
    /*
     * Post the temporary Load:[File] root scan.
     *
     * This is a storage diagnostic operation, not a musical preset action.
     * Preset participates only so Menu can reuse the existing async completion
     * poll and avoid calling filesystem.c directly from encoder handlers.
     */
    filesystem_ack();
    if (!filesystem_requestScanTestFiles(on_test_scan_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_FILE;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_scanTestDirs(void)
{
    filesystem_ack();
    if (!filesystem_requestScanTestDirs(on_test_scan_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_DIR;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_loadTestFile(const char *display_name)
{
    filesystem_ack();
    if (!filesystem_requestLoadTestFile(display_name,
                                        on_test_file_load_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_FILE;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_loadTestDir(const char *display_name)
{
    filesystem_ack();
    if (!filesystem_requestLoadTestDir(display_name,
                                       on_test_dir_load_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_DIR;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_saveTestFile(const char *display_name)
{
    filesystem_ack();
    if (!filesystem_requestSaveTestFile(display_name,
                                        on_test_file_save_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_FILE;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_saveTestDir(const char *display_name)
{
    filesystem_ack();
    if (!filesystem_requestSaveTestDir(display_name,
                                       on_test_dir_save_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_DIR;
    pm_request_slot = 0u;
    return 1u;
}

uint8_t preset_saveTestSimpleDir(const char *display_name)
{
    filesystem_ack();
    if (!filesystem_requestSaveTestSimpleDir(display_name,
                                             on_test_dir_save_complete))
        return 0u;
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_type = SAVE_TYPE_SIMPLE_DIR;
    pm_request_slot = 0u;
    return 1u;
}

#endif

/*
 * Compatibility stubs for retired File/Dir diagnostics.
 *
 * Inputs are intentionally ignored and output is always "not started": the
 * Load/Save chooser no longer exposes these types, and retaining callers must
 * not allocate or repopulate the removed filesystem diagnostic caches.
 */
uint8_t preset_scanTestFiles(void) { return 0u; }
uint8_t preset_scanTestDirs(void) { return 0u; }
uint8_t preset_loadTestFile(const char *name) { (void)name; return 0u; }
uint8_t preset_loadTestDir(const char *name) { (void)name; return 0u; }
uint8_t preset_saveTestFile(const char *name) { (void)name; return 0u; }
uint8_t preset_saveTestDir(const char *name) { (void)name; return 0u; }
uint8_t preset_saveTestSimpleDir(const char *name) { (void)name; return 0u; }

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
    preset_morphScene(scene_getActiveIndex(), morph);
}

void preset_morphScene(uint8_t scene_index, uint8_t morph)
{
    scene_t *scene;

    /*
     * Set overall Morph for a specific resident Scene.
     *
     * Inputs: Scene index from active UI, MIDI, or scene_mask_voice_edit fan-
     * out and 0..255 Morph amount. Outputs: retained Scene global Morph and
     * all six per-voice Morph amounts update. Runtime/PERF mirrors are updated
     * only for the active Scene, because inactive Scenes are stored state until
     * selected.
     */
    preset_ensureMorphInitialized();
    scene = scene_get(scene_index);
    if (!scene)
        return;
    scene->settings.morph_amount = morph;
    scene_setAllVoiceMorphAmounts(scene_index, morph);
    if (scene_index == scene_getActiveIndex()) {
        preset_syncSceneMorphMirrors(scene);
        presetMorph_requestAll(scene_index);
    }
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
    preset_morphVoiceScene(scene_getActiveIndex(), slot, morph);
}

void preset_morphVoiceScene(uint8_t scene_index, uint8_t slot, uint8_t morph)
{
    /*
     * Set one Scene/voice Morph amount.
     *
     * Inputs: resident Scene index, zero-based instrument slot, and 0..255
     * Morph amount. Outputs: retained per-voice Morph state updates for any
     * valid Scene; only the active Scene updates flat PERF mirrors and queues
     * descriptor interpolation.
     */
    if (slot >= INSTRUMENT_SLOT_COUNT || !scene_get(scene_index))
        return;
    preset_ensureMorphInitialized();
    scene_setVoiceMorphAmount(scene_index, slot, morph);
    if (scene_index == scene_getActiveIndex()) {
        parameter_values[PAR_VOICE1_MORPH + slot] = morph;
        presetMorph_requestVoice(scene_index, slot);
    }
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
