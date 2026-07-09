/*
 * presetManager.h — LXR-02 preset load/save (asyncfatfs version).
 *
 * All load/save functions are asynchronous — they post a request to filesystem
 * and return immediately. The caller must poll preset_getStatus() to know
 * when the operation completes.
 *
 * Status lifecycle:
 *   PRESET_IDLE → preset_loadDrumset() → PRESET_LOAD_IN_PROGRESS
 *     → filesystem completes read → PRESET_UPDATE_READY
 *     → menu calls preset_applyPending() → PRESET_IDLE
 *
 * The menu main loop calls preset_pollStatus() each iteration. When
 * UPDATE_READY is seen, it applies post-load logic (mod target gap index,
 * repaint, etc.) and clears the status back to IDLE.
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

#ifndef PRESETMANAGER_H_
#define PRESETMANAGER_H_
#include <stdint.h>
#include "SceneData.h"

/* -----------------------------------------------------------------------
** Async operation status
** ----------------------------------------------------------------------- */
typedef enum {
    PRESET_IDLE             = 0,
    PRESET_LOAD_IN_PROGRESS = 1,
    PRESET_UPDATE_READY     = 2,
} preset_status_t;

/* Which type of load completed — tells the menu what post-load work to do */
typedef enum {
    PRESET_OP_NONE,
    PRESET_OP_KIT_LOAD,
    PRESET_OP_MORPH_LOAD,
    PRESET_OP_GLOBALS_LOAD,
    PRESET_OP_NAME_LOAD,
    PRESET_OP_KIT_SAVE,
    PRESET_OP_MORPH_SAVE,
    PRESET_OP_GLOBALS_SAVE,
    PRESET_OP_PATTERN_LOAD,
    PRESET_OP_PATTERN_SAVE,
    PRESET_OP_ALL_LOAD,
    PRESET_OP_ALL_SAVE,
    PRESET_OP_PERFORMANCE_LOAD,
    PRESET_OP_PERFORMANCE_SAVE,
} preset_op_type_t;

extern char preset_currentName[8];

/*
 * Selects one of the two persisted endpoint images.
 *
 * The interpolation image is intentionally absent: only presetMorphEngine may
 * write that runtime cache. Clients are Menu edits, external MIDI translation,
 * storage/load follow-up, and tests.
 */
typedef enum {
    INSTRUMENT_IMAGE_MAIN = 0,
    INSTRUMENT_IMAGE_MORPH
} instrument_image_select_t;

void    preset_init(void);

/* -----------------------------------------------------------------------
** Status polling — call from main loop (or menu tick) each iteration.
** Returns current status. When UPDATE_READY, caller should do post-load
** work then call preset_ackStatus() to clear back to IDLE.
** ----------------------------------------------------------------------- */
preset_status_t  preset_getStatus(void);
preset_op_type_t preset_getCompletedOp(void);
uint8_t          preset_getRequestSlot(void);
uint8_t          preset_getRequestType(void);
void             preset_ackStatus(void);

/* -----------------------------------------------------------------------
** Load/save — all async, return immediately.
** ----------------------------------------------------------------------- */

/* Drumset (kit). isMorph=1 reads/writes the morph kit buffer/path. */
uint8_t preset_loadDrumset(uint8_t presetNr, uint8_t isMorph);
void    preset_saveDrumset(uint8_t presetNr, uint8_t isMorph);

/* Globals — single GLO.CFG file. */
void    preset_loadGlobals(void);
void    preset_saveGlobals(void);

/* Pattern — async direct serializer in filesystem.c. */
uint8_t preset_loadPattern(uint8_t presetNr);
void    preset_savePattern(uint8_t presetNr);

/* All / Performance — async container serializers in filesystem.c. */
void    preset_saveAll(uint8_t presetNr, uint8_t isAll);
uint8_t preset_loadAll(uint8_t presetNr, uint8_t isAll);

/* Read 8-byte preset name from file header (any type). */
char*   preset_loadName(uint8_t presetNr, uint8_t what);
void    preset_applyLoadedName(void);

/* Send loaded parameters to DSP synchronously. Use this before audio starts;
** runtime load completion should use the chunked apply API below so it cannot
** monopolize one foreground pass. */
void    preset_sendDrumsetParameters(void);

/*
 * Direct sound-apply helpers.
 * Why: local UI/preset code should not pack fake front-panel protocol bytes to
 * reach DSP parameter application. Inputs are real parameter IDs/targets.
 * Outputs update DSP/menu parameter state and optionally record automation.
 * Risk: parameter 127 is rejected because the old MIDI_CC packing underflowed.
 */
void    preset_applySoundParameter(uint16_t paramNr, uint8_t value,
                                   uint8_t recordAutomation);
void    preset_applyVelocityModTarget(uint8_t voice, uint16_t targetParam);
void    preset_applyLfoModTarget(uint8_t lfo, uint16_t targetParam);

uint8_t preset_setInstrumentParameter(uint8_t scene_index, uint8_t slot,
                                      uint8_t local_param,
                                      instrument_image_select_t image,
                                      uint8_t value,
                                      uint8_t record_automation);
uint8_t preset_setSupplementalParameter(uint8_t scene_index, uint8_t slot,
                                        uint8_t local_param, uint16_t value);
uint8_t preset_applyInstrumentRuntimeValue(uint8_t scene_index,
                                           instrument_param_id_t id,
                                           uint8_t value);
uint8_t preset_applyKitAudioRouting(uint8_t scene_index, uint8_t slot);
void preset_applySceneSettings(uint8_t scene_index);

/* Runtime chunked sound apply. preset_tickDrumsetApply() performs at most one
** voice of modulation-routing work and returns non-zero while that pass did
** foreground work. The menu owns operation-specific UI/global follow-up after
** the tick function reports idle. */
void    preset_startDrumsetApply(void);
uint8_t preset_tickDrumsetApply(void);

/* Morph — rate-limited front-panel CC parameter dump. */
void    preset_morph(uint8_t morph);
void    preset_morphTick(void);
uint8_t preset_getMorphValue(uint16_t index, uint8_t morph);

#endif
