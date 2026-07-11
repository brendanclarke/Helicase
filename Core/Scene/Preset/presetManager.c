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
#include "mixer.h"
#include <string.h>
#include <stdint.h>

char preset_currentName[8];

/* -----------------------------------------------------------------------
** Status state machine
** ----------------------------------------------------------------------- */
static volatile preset_status_t  pm_status = PRESET_IDLE;
static volatile preset_op_type_t pm_completed_op = PRESET_OP_NONE;
static volatile uint8_t          pm_request_slot = 0;
static volatile uint8_t          pm_request_type = SAVE_TYPE_KIT;

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
static uint8_t preset_morph_initialized = 0;

preset_status_t preset_getStatus(void)
{
    return pm_status;
}

preset_op_type_t preset_getCompletedOp(void)
{
    return pm_completed_op;
}

uint8_t preset_getRequestSlot(void)
{
    return pm_request_slot;
}

uint8_t preset_getRequestType(void)
{
    return pm_request_type;
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

static void on_kit_save_complete(void)
{
    preset_completeFilesystemOp(PRESET_OP_KIT_SAVE);
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
    case SAVE_TYPE_PATTERN:     return FS_FILE_PATTERN;
    case SAVE_TYPE_MORPH:       return FS_FILE_MORPH;
    case SAVE_TYPE_PERFORMANCE: return FS_FILE_PERFORMANCE;
    case SAVE_TYPE_ALL:         return FS_FILE_ALL;
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
                                           uint8_t value)
{
    /*
     * Public runtime apply for one canonical instrument parameter.
     *
     * Inputs: Scene index, slot/descriptor-index instrument ID, and byte value.
     * Output: active Scene values are written through instrument-owned runtime
     * bindings;
     * inactive Scene calls validate but do not mutate live DSP state. Accessors
     * and affiliates: SceneData supplies slot/type lookup, InstrumentManager
     * supplies descriptor lookup and runtime instance binding.
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
            scene && scene->settings.morph_amount == 0u) {
            (void)preset_applyInstrumentRuntimeValueInternal(
                scene_index,
                instrumentParam_make(slot, descriptor_index),
                value,
                1u);
        }
        presetMorph_request(scene_index,
                            scene ? scene->settings.morph_amount : 0u);
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

void preset_applySceneSettings(uint8_t scene_index)
{
    const scene_t *scene = scene_getConst(scene_index);
    if (!scene || scene_index != scene_getActiveIndex())
        return;

    /*
     * Apply Scene-wide settings that still have legacy mirrors.
     *
     * Morph amount is still a Menu-visible Scene setting. Instrument/runtime
     * settings no longer mirror through flat sound parameter ids.
     */
    parameter_values[PAR_MORPH] = scene->settings.morph_amount;
    preset_morph(scene->settings.morph_amount);
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
 * synchronous boot apply and preset_tickDrumsetApply().
 */
static void preset_applyDrumsetVoice(uint8_t voice)
{
    uint8_t scene_index = scene_getActiveIndex();
    const kit_instrument_slot_t *instrument =
        scene_instrumentSlotConst(scene_index, voice);
    const instrument_registry_entry_t *entry;
    uint8_t i;

    if (!instrument || voice >= INSTRUMENT_SLOT_COUNT)
        return;

    (void)preset_applyKitAudioRouting(scene_index, voice);
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
        preset_applyDrumsetVoice(voice);

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
    presetMorph_rebuildScene(scene_getActiveIndex());
    drumset_apply_active = 1u;
    drumset_apply_voice = 0u;
}

uint8_t preset_tickDrumsetApply(void)
{
    if (!drumset_apply_active)
        return 0u;

    if (drumset_apply_voice < INSTRUMENT_SLOT_COUNT) {
        preset_applyDrumsetVoice(drumset_apply_voice);
        drumset_apply_voice++;
        return 1u;
    }

    if (presetMorph_tick())
        return 1u;

    drumset_apply_active = 0u;
    return 0u;
}
/* -----------------------------------------------------------------------
** preset_loadDrumset — post async kit load request.
** ----------------------------------------------------------------------- */
uint8_t preset_loadDrumset(uint8_t presetNr, uint8_t isMorph)
{
    fs_file_type_t type = isMorph ? FS_FILE_MORPH : FS_FILE_KIT;
    fs_completion_cb_t cb = isMorph ? on_morph_load_complete : on_kit_load_complete;

    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = isMorph ? SAVE_TYPE_MORPH : SAVE_TYPE_KIT;
    if (filesystem_requestLoad(type, presetNr, cb))
        return 1;
    pm_status = PRESET_IDLE;
    return 0;
}

/* -----------------------------------------------------------------------
** preset_saveDrumset — post async kit save request.
** ----------------------------------------------------------------------- */
void preset_saveDrumset(uint8_t presetNr, uint8_t isMorph)
{
    fs_file_type_t type = isMorph ? FS_FILE_MORPH : FS_FILE_KIT;
    fs_completion_cb_t cb = isMorph ? on_morph_save_complete : on_kit_save_complete;

    filesystem_ack();
    pm_status = PRESET_LOAD_IN_PROGRESS;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = presetNr;
    pm_request_type = isMorph ? SAVE_TYPE_MORPH : SAVE_TYPE_KIT;
    if (!filesystem_requestSave(type, presetNr, cb))
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
char* preset_loadName(uint8_t presetNr, uint8_t what)
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
    pm_request_type = SAVE_TYPE_PATTERN;
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
    pm_request_type = SAVE_TYPE_PATTERN;
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
    pm_request_type = isAll ? SAVE_TYPE_ALL : SAVE_TYPE_PERFORMANCE;
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
    pm_request_type = isAll ? SAVE_TYPE_ALL : SAVE_TYPE_PERFORMANCE;
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
     * Compatibility wrapper for existing Menu/MIDI Morph callers.
     *
     * The old implementation walked flat parameter_values[]/parameters2[].
     * Directory Kits now store two Scene-owned endpoint images, so Morph
     * requests are forwarded to presetMorphEngine. That worker rebuilds
     * morph_interpolation[] and applies active-Scene image values through
     * preset_applyInstrumentRuntimeValue().
     */
    preset_ensureMorphInitialized();
    parameter_values[PAR_MORPH] = morph;
    presetMorph_request(scene_getActiveIndex(), morph);
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
