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
 * reach DSP parameter application. Inputs are real legacy sound parameter IDs.
 * Outputs update DSP/menu parameter state and optionally record automation.
 * Risk: parameter 127 is rejected because the old MIDI_CC packing underflowed.
 */
void    preset_applySoundParameter(uint16_t paramNr, uint8_t value,
                                   uint8_t recordAutomation);

/*
 * Scene-owned instrument mutation/apply API.
 *
 * Why these functions exist: root Kit directories now parse into
 * scene_t.kit.instruments rather than the old flat parameter_values[] buffer.
 * Preset is the boundary that knows how to turn that Scene-owned data into the
 * current DSP runtime bindings without leaking legacy PAR_* IDs into SceneData,
 * storageTypes, or InstrumentManager. The per-slot storage cell is the
 * descriptor array index for that instrument type.
 *
 * Accessors/clients:
 * - storageTypes/filesystem populate Scene slots directly during load.
 * - menu.c load completion calls preset_startDrumsetApply(), which uses these
 *   functions in bounded foreground chunks.
 * - presetMorphEngine calls preset_applyInstrumentRuntimeValue() after it
 *   rebuilds morph_interpolation[].
 * - future Menu/MIDI descriptor editors should call the setters instead of
 *   touching SceneData arrays directly.
 */
uint8_t preset_setInstrumentParameter(uint8_t scene_index, uint8_t slot,
                                      uint8_t descriptor_index,
                                      instrument_image_select_t image,
                                      uint8_t value,
                                      uint8_t record_automation);
uint8_t preset_setSupplementalParameter(uint8_t scene_index, uint8_t slot,
                                        uint8_t descriptor_index, uint16_t value);
uint8_t preset_applyInstrumentRuntimeValue(uint8_t scene_index,
                                           instrument_param_id_t id,
                                           uint16_t value);
uint8_t preset_applyKitAudioRouting(uint8_t scene_index, uint8_t slot);
void preset_applySceneSettings(uint8_t scene_index);

/* Runtime chunked sound apply. preset_tickDrumsetApply() applies one Scene kit
** slot's audio routing and non-morph runtime cells per pass, then advances the
** presetMorphEngine parameter image dump until idle. The menu owns
** operation-specific UI/global follow-up after the tick function reports idle. */
void    preset_startDrumsetApply(void);
uint8_t preset_tickDrumsetApply(void);

/*
 * Scene Morph and Scene performance settings.
 *
 * preset_morph() is now the overall Morph bulk-set operation: it writes the
 * Scene global mirror and all six per-slot Morph amounts. preset_morphVoice()
 * changes one slot only. preset_rebuildMorph() requeues the descriptor-driven
 * worker from retained Scene values without changing any Morph amounts, which
 * is required after endpoint loads/edits. preset_setVoiceDecimationAll()
 * retains and applies the Scene-wide decimation multiplier used by PERF "srt".
 */
void    preset_morph(uint8_t morph);
void    preset_morphVoice(uint8_t slot, uint8_t morph);
void    preset_rebuildMorph(void);
void    preset_setVoiceDecimationAll(uint8_t scene_index, uint8_t value);
/*
 * Apply Scene-wide decimation to runtime without changing retained Scene/Menu
 * state.
 *
 * Inputs: value in the 0..127 PERF `srt` domain. Output:
 * mixer_decimation_rate[6] receives the shaped multiplier. LFO modulation uses
 * this runtime-only path so Scene Decimation can be a Scene mod target without
 * causing the displayed PERF setting to move every LFO block.
 */
void    preset_applyVoiceDecimationAllRuntime(uint8_t value);
void    preset_morphTick(void);
uint8_t preset_getMorphValue(uint16_t index, uint8_t morph);

#endif
