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
#include "frontPanelParser.h"
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

/* -----------------------------------------------------------------------
** preset_sendDrumsetParameters — send loaded parameters to DSP.
** Called from menu when it sees UPDATE_READY for a kit load.
** ----------------------------------------------------------------------- */
// void preset_sendDrumsetParameters(void)
// {
//     uint8_t i;

//     for (i = 0; i < 6; i++) {
//         uint16_t value = modTargets[parameter_values[PAR_VEL_DEST_1 + i]].param;
//         uint8_t upper = (uint8_t)(((value & 0x80u) >> 7) | ((i & 0x3fu) << 1));
//         uint8_t lower = (uint8_t)(value & 0x7fu);
//         frontPanel_sendData(CC_VELO_TARGET, upper, lower);

//         if (parameter_values[PAR_VOICE_LFO1 + i] < 1 ||
//             parameter_values[PAR_VOICE_LFO1 + i] > 6)
//             parameter_values[PAR_VOICE_LFO1 + i] = 1;

//         value = modTargets[parameter_values[PAR_TARGET_LFO1 + i]].param;
//         upper = (uint8_t)(((value & 0x80u) >> 7) | ((i & 0x3fu) << 1));
//         lower = (uint8_t)(value & 0x7fu);
//         frontPanel_sendData(CC_LFO_TARGET, upper, lower);
//     }

//     preset_morph(parameter_values[PAR_MORPH]);
// }
static void preset_sendModTarget(uint8_t status, uint8_t upper, uint8_t lower)
{
    switch(status)
    {
        case CC_VELO_TARGET:
        {
            uint8_t value = ((upper&0x01)<<7) | lower;
            uint8_t velModNr = (upper&0xfe)>>1;
			modNode_setDestination(&velocityModulators[velModNr], value);
            break;
        }
        case CC_LFO_TARGET:
        {
            uint8_t value = ((upper&0x01)<<7) | lower;
            uint8_t lfoNr = (upper&0xfe)>>1;
            switch(lfoNr)
            {
            case 0:
            case 1:
            case 2:	modNode_setDestination(&voiceArray[lfoNr].lfo.modTarget, value);break;
            case 3:	modNode_setDestination(&snareVoice.lfo.modTarget,value);		break;
            case 4:	modNode_setDestination(&cymbalVoice.lfo.modTarget, value);		break;
            case 5:	modNode_setDestination(&hatVoice.lfo.modTarget, value);			break;
            default:
                break;
            }
        }   
    }
}

void preset_sendDrumsetParameters()
{
	uint8_t i;
	//special case mod targets
	for(i=0;i<6;i++)
	{
		//**VELO load drumkit. translate to param value before sending
		// parameter_values[PAR_VEL_DEST_1+i] is an index into modTargets, we need to send
		// a parameter number
		uint8_t value = (uint8_t)(modTargets[parameter_values[PAR_VEL_DEST_1+i]].param);
		uint8_t upper,lower;
		upper = (uint8_t)(((value&0x80)>>7) | (((i)&0x3f)<<1));
		lower = value&0x7f;
		preset_sendModTarget(CC_VELO_TARGET,upper,lower);

		// ensure target voice # is valid
		if(parameter_values[PAR_VOICE_LFO1+i] < 1 || parameter_values[PAR_VOICE_LFO1+i] > 6 )
			parameter_values[PAR_VOICE_LFO1+i]=1;

		// **LFO par_target_lfo will be an index into modTargets, but we need a parameter number to send
		value = (uint8_t)(modTargets[parameter_values[PAR_TARGET_LFO1+i]].param);

		upper = (uint8_t)(((value&0x80)>>7) | (((i)&0x3f)<<1));
		lower = value&0x7f;
		// frontPanel_sendData(CC_LFO_TARGET,upper,lower);
        preset_sendModTarget(CC_LFO_TARGET, upper, lower);



	// PAR_VEL_DEST_1,
	// PAR_VEL_DEST_2,
	// PAR_VEL_DEST_3,
	// PAR_VEL_DEST_4,
	// PAR_VEL_DEST_5,
	// PAR_VEL_DEST_6,

    // PAR_VOICE_LFO1,
	// PAR_VOICE_LFO2,
	// PAR_VOICE_LFO3,
	// PAR_VOICE_LFO4,
	// PAR_VOICE_LFO5,
	// PAR_VOICE_LFO6,

	// PAR_TARGET_LFO1,
	// PAR_TARGET_LFO2,
	// PAR_TARGET_LFO3,
	// PAR_TARGET_LFO4,
	// PAR_TARGET_LFO5,
	// PAR_TARGET_LFO6,
    // LFO destinations aren't grabbed from parameters
    // they must be assigned manually
    
	}

	// --AS todo will this morph (and fuck up) our modulation targets?
	// send parameters (possibly combined with morph parameters) to back
	preset_morph(parameter_values[PAR_MORPH]);



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
    if (index < 128u)
        frontPanel_sendData(MIDI_CC, (uint8_t)index, value);
    else
        frontPanel_sendData(CC_2, (uint8_t)(index - 128u), value);
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

    /* Skip index 127: frontPanel_sendData(MIDI_CC, 127, ...) encodes data1
    ** as (127+1) & 0x7f = 0. midiParser_ccHandler then computes paramNr as
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
