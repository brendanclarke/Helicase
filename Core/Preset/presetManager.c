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

/* -----------------------------------------------------------------------
** Morph state
**
** preset_morph() captures the latest requested value. preset_morphTick()
** applies one sound-parameter slot per call. Each active pass uses a snapshot
** morph value. Request/pass generations guarantee that the newest requested
** morph value gets one complete non-cached pass before the job goes idle.
** ----------------------------------------------------------------------- */
static uint8_t  morph_active = 0;
static uint8_t  morph_target_value = 0;
static uint8_t  morph_pass_value = 0;
static uint16_t morph_request_generation = 0;
static uint16_t morph_pass_generation = 0;
static uint16_t morph_index = 0;

/* Runtime loaded-kit apply cursor.
**
** Kit/all/performance file completion used to send all six voices' velocity
** and LFO modulation destinations in one foreground call. That was small but
** bursty: every modNode_setDestination() mutates DSP graph state before the
** main loop can return to audio_check_and_render(). The menu now starts this
** cursor after audio is running and ticks one voice per foreground pass. */
static uint8_t drumset_apply_active = 0;
static uint8_t drumset_apply_voice = 0;

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

/* -----------------------------------------------------------------------
** preset_init — no-op.  Filesystem mount handled by asyncfatfs.
** ----------------------------------------------------------------------- */
void preset_init(void)
{
    pm_status = PRESET_IDLE;
    pm_completed_op = PRESET_OP_NONE;
    pm_request_slot = 0;
    pm_request_type = SAVE_TYPE_KIT;
}

void preset_applyVelocityModTarget(uint8_t voice, uint16_t targetParam)
{
    /*
     * Applies a velocity modulation destination for one drum voice.
     *
     * Callers: Menu sound-parameter edits and loaded-kit apply. This replaces
     * the old front-panel protocol packing for velocity destination opcodes.
     *
     * Why it lives in Preset: velocityModulators[] are sound/preset runtime
     * objects, not Menu or Pattern state. Menu resolves user-facing target
     * indices; Preset writes the actual modulation node destination.
     *
     * Inputs: voice is 0..5, targetParam is the resolved destination parameter
     * id from modTargets[]. Output: the voice velocity modulator destination is
     * updated. Invalid voices are ignored to preserve previous guard behavior.
     *
     * Risk: targetParam is narrowed to uint8_t by modNode_setDestination();
     * this matches the existing sound engine API and legacy target encoding.
     */
    if (voice < 6u)
        modNode_setDestination(&velocityModulators[voice], (uint8_t)targetParam);
}

void preset_applyLfoModTarget(uint8_t lfo, uint16_t targetParam)
{
    /*
     * Applies an LFO modulation destination for one drum voice.
     *
     * Callers: Menu sound-parameter edits and loaded-kit apply. The removed
     * parser used to forward high/low packed target bytes; the direct API takes
     * the already resolved parameter id.
     *
     * Why it lives in Preset: voiceArray/snare/cymbal/hat LFO nodes are sound
     * engine objects owned by Preset/voice state. Menu should not know those
     * object addresses.
     *
     * Inputs: lfo selects voice/LFO 0..5, targetParam is the resolved mod target
     * parameter id. Output: the chosen LFO modTarget destination is updated.
     * Invalid lfo indices are ignored.
     */
    uint8_t value = (uint8_t)targetParam;
    switch(lfo)
    {
    case 0:
    case 1:
    case 2:	modNode_setDestination(&voiceArray[lfo].lfo.modTarget, value);break;
    case 3:	modNode_setDestination(&snareVoice.lfo.modTarget,value);		break;
    case 4:	modNode_setDestination(&cymbalVoice.lfo.modTarget, value);		break;
    case 5:	modNode_setDestination(&hatVoice.lfo.modTarget, value);			break;
    default:
        break;
    }
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

/* Apply one loaded kit voice's modulation routing.
**
** This helper is deliberately one voice wide so runtime kit/all/performance
** load completion can spread DSP graph mutations across main-loop passes.
** The synchronous wrapper below still calls all six voices before audio starts,
** preserving boot behavior and legacy call sites.
**
** FrontPanelParser removal note: this function used to prepare high/low packed
** protocol fragments for velocity/LFO target changes. Those locals remain only
** to preserve the exact translation points while the direct Preset helpers are
** introduced; the actual state change is now preset_applyVelocityModTarget()
** and preset_applyLfoModTarget(). Once the sound engine has a fully typed
** parameter API, the unused upper/lower compatibility calculations can be
** removed in a separate cleanup. */
static void preset_applyDrumsetVoice(uint8_t voice)
{
    uint8_t value;
    uint8_t upper;
    uint8_t lower;

    if (voice >= 6u)
        return;

    /* Loaded VELO destinations are stored as modTargets[] indices. The DSP
    ** mod node needs the destination parameter number packed in the legacy
    ** high/low protocol shape, so translate once at apply time. */
    value = (uint8_t)(modTargets[parameter_values[PAR_VEL_DEST_1 + voice]].param);
    upper = (uint8_t)(((value & 0x80u) >> 7) | ((voice & 0x3fu) << 1));
    lower = (uint8_t)(value & 0x7fu);
    (void)upper;
    (void)lower;
    preset_applyVelocityModTarget(voice, value);

    /* Old or malformed files can hold an invalid target-voice number. Clamp
    ** before resolving the LFO destination so the later UI and mod-routing
    ** paths agree on the repaired value. */
    if (parameter_values[PAR_VOICE_LFO1 + voice] < 1u ||
        parameter_values[PAR_VOICE_LFO1 + voice] > 6u) {
        parameter_values[PAR_VOICE_LFO1 + voice] = 1u;
    }

    value = (uint8_t)(modTargets[parameter_values[PAR_TARGET_LFO1 + voice]].param);
    upper = (uint8_t)(((value & 0x80u) >> 7) | ((voice & 0x3fu) << 1));
    lower = (uint8_t)(value & 0x7fu);
    (void)upper;
    (void)lower;
    preset_applyLfoModTarget(voice, value);
}

/* Synchronous loaded-kit apply.
**
** Safe before audio starts and retained for boot-time loads. Runtime load
** completion should prefer preset_startDrumsetApply()/preset_tickDrumsetApply()
** so the six mod-target updates do not run as one foreground burst. */
void preset_sendDrumsetParameters(void)
{
    uint8_t voice;

    for (voice = 0; voice < 6u; voice++)
        preset_applyDrumsetVoice(voice);

    /* Morph itself is already rate-limited by preset_morphTick(); this call
    ** only records the requested value and arms a pass if needed. */
    preset_morph(parameter_values[PAR_MORPH]);
}

void preset_startDrumsetApply(void)
{
    drumset_apply_active = 1u;
    drumset_apply_voice = 0u;
}

uint8_t preset_tickDrumsetApply(void)
{
    if (!drumset_apply_active)
        return 0u;

    if (drumset_apply_voice < 6u) {
        preset_applyDrumsetVoice(drumset_apply_voice);
        drumset_apply_voice++;
        return 1u;
    }

    /* Final tick only arms morph; the actual morph parameter walk remains
    ** bounded by preset_morphTick() elsewhere in the main loop. */
    preset_morph(parameter_values[PAR_MORPH]);
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

static uint8_t preset_morphShouldSkip(uint16_t index)
{
    if (index == 127u)
        return 1;
    if (index >= PAR_VEL_DEST_1 && index <= PAR_VEL_DEST_6)
        return 1;
    if (index >= PAR_VOICE_LFO1 && index <= PAR_VOICE_LFO6)
        return 1;
    if (index >= PAR_TARGET_LFO1 && index <= PAR_TARGET_LFO6)
        return 1;
    return 0;
}

static void preset_morphSendParameter(uint16_t index, uint8_t value)
{
    /*
     * Sends one morph-interpolated sound parameter through Preset's direct
     * sound apply helper.
     *
     * Callers: preset_morphTick() only. Morph is rate-limited there, so this
     * helper intentionally does no pacing of its own.
     *
     * Inputs: index is the sound parameter id, value is the interpolated 0..127
     * value between the current kit and morph kit. Output: the sound parameter
     * is applied and automation is recorded, matching the old morph CC path.
     *
     * Risk: preset_morphShouldSkip() must filter unsupported indices before
     * this call. In particular, parameter 127 must not reach the legacy CC
     * application path.
     */
    preset_applySoundParameter(index, value, 1);
}

void preset_morph(uint8_t morph)
{
    if (morph_target_value != morph || !morph_active)
        morph_request_generation++;

    morph_target_value = morph;
    if (!morph_active) {
        morph_active = 1;
        morph_pass_value = morph;
        morph_pass_generation = morph_request_generation;
        morph_index = 0;
    }
}

void preset_morphTick(void)
{
    uint16_t index;
    uint8_t value;

    if (!morph_active)
        return;

    if (morph_index >= END_OF_SOUND_PARAMETERS) {
        if (morph_pass_generation != morph_request_generation) {
            morph_pass_value = morph_target_value;
            morph_pass_generation = morph_request_generation;
            morph_index = 0;
        } else {
            morph_active = 0;
        }
        return;
    }

    index = morph_index++;

    /* Skip index 127: legacy MIDI_CC packing encoded data1 as
    ** (127+1) & 0x7f = 0. midiParser_ccHandler then computes paramNr as
    ** data1-1, underflowing to 65535. Mod-target slots are also intentionally
    ** not morphed, matching the original morph-save behavior. */
    if (preset_morphShouldSkip(index))
        return;

    value = preset_getMorphValue(index, morph_pass_value);
    preset_morphSendParameter(index, value);
}

uint8_t preset_getMorphValue(uint16_t index, uint8_t morph)
{
    if (index >= END_OF_SOUND_PARAMETERS)
        return 0;
    return preset_interpolate(parameter_values[index], parameters2[index], morph);
}
