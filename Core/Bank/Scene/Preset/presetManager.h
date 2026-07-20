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
    PRESET_OP_GLOBALS_SAVE,
    PRESET_OP_PATTERN_LOAD,
    PRESET_OP_PATTERN_SAVE,
    PRESET_OP_ALL_LOAD,
    PRESET_OP_ALL_SAVE,
    PRESET_OP_PERFORMANCE_LOAD,
    PRESET_OP_PERFORMANCE_SAVE,
    PRESET_OP_INSTRUMENT_LOAD,
    PRESET_OP_INSTRUMENT_SAVE,
    PRESET_OP_KIT_SAVE,
    PRESET_OP_KIT_MORPH_LOAD,
    PRESET_OP_KIT_MORPH_SAVE,
    PRESET_OP_INSTRUMENT_MORPH_LOAD,
    /*
     * New-format Morph Save completions.
     *
     * Instrument Morph Save is distinct from normal Instrument Save so Menu can
     * reset the correct UI surface without implying a flat .snd file was
     * written. It does not trigger runtime apply or retained name updates.
    */
    PRESET_OP_INSTRUMENT_MORPH_SAVE,
    PRESET_OP_SCENE_LOAD,
    PRESET_OP_SCENE_SAVE,
    PRESET_OP_BANK_LOAD,
    PRESET_OP_BANK_SAVE,
    PRESET_OP_TEST_SCAN,
    PRESET_OP_TEST_FILE_LOAD,
    PRESET_OP_TEST_DIR_LOAD,
    PRESET_OP_TEST_FILE_SAVE,
    PRESET_OP_TEST_DIR_SAVE,
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
/*
 * Report whether the most recent asynchronous filesystem completion succeeded.
 *
 * Menu reads this flag before acknowledging completions so it can distinguish
 * success cleanup from a failed filesystem operation and show the
 * filesystem_errorCode() overlay.
 */
uint8_t          preset_getCompletedOk(void);
uint16_t         preset_getRequestSlot(void);
uint8_t          preset_getRequestType(void);
/*
 * Read the explicit Scene destination retained for the active Instrument load.
 *
 * Output: a valid Scene index while an Instrument request is pending/completing.
 * Client: Menu completion starts the bounded one-slot DSP apply for that exact
 * Scene instead of assuming the Scene active when encoder movement began.
 */
uint8_t          preset_getRequestScene(void);
void             preset_ackStatus(void);

/* -----------------------------------------------------------------------
** Load/save — all async, return immediately.
** ----------------------------------------------------------------------- */

/*
 * Drumset (kit).
 *
 * Load keeps the legacy isMorph compatibility flag. Save uses isMorph=1 for
 * new-format KitMrp projection: current interpolated endpoints are written to
 * both normal and morph file values.
 */
uint8_t preset_loadDrumset(uint16_t presetNr, uint8_t isMorph);
uint8_t preset_saveDrumset(uint16_t presetNr, uint8_t isMorph,
                           uint8_t source_scene);
/*
 * Load one Kit directory into an explicit set of resident Scenes.
 *
 * Inputs: direct Kit library slot 000..999 and Scene bitmask. Output: an asynchronous Kit
 * request whose filesystem phase stages, validates, and commits the Kit to
 * each selected Scene. Clients: Load menu and boot. This dedicated entry point
 * keeps scene routing at the Preset boundary instead of making Menu call the
 * filesystem directly or overloading the legacy morph compatibility API.
 */
uint8_t preset_loadKitForScenes(uint16_t presetNr, uint16_t scene_mask);
/*
 * Load root Scene library folders.
 *
 * Load inputs mirror Kit Load: root Scene library slot and destination Scene
 * bitmask. Output is an asynchronous Preset operation completed through
 * PRESET_OP_SCENE_LOAD.
 */
uint8_t preset_loadSceneForScenes(uint16_t presetNr, uint16_t scene_mask);
/*
 * Load and save root Bank folders.
 *
 * Bank Load validates bankset.bcg and may load one Bank-local two-digit Scene
 * child into the selected resident Scene mask. Empty Banks complete as
 * PRESET_OP_BANK_LOAD with no child payload; callers then run
 * preset_loadFirstAvailableSceneOrKit() for the required fallback chain.
 */
uint8_t preset_loadBank(uint16_t presetNr, uint16_t scene_mask);
uint8_t preset_saveBank(uint16_t presetNr, uint16_t scene_mask);
uint8_t preset_completedBankLoadedScene(void);
uint8_t preset_loadFirstAvailableSceneOrKit(void);
/*
 * Save the active resident Scene into the root Scene library.
 *
 * Inputs: direct root Scene slot and the current eight-cell preset_currentName
 * edited by the Save page. Output: asynchronous Scene directory write and a
 * PRESET_OP_SCENE_SAVE completion. This is separate from preset_saveDrumset()
 * because Scene Save serializes Scene settings, embedded Kit, Pattern stub,
 * and Effect placeholder, not only the Kit payload.
 */
uint8_t preset_saveScene(uint16_t presetNr, uint8_t source_scene);
/*
 * Load a new-format Kit directory into the selected Scenes' morph endpoints.
 *
 * Inputs: direct Kit library slot 000..999 and Scene bitmask. Output: an asynchronous Kit/
 * directory request whose filesystem phase only stages the source kit; Preset
 * then copies source normal endpoints into resident morph endpoints for slots
 * whose instrument types match. Mismatched source/destination slot types are
 * deliberately no-change so morph load remains a per-instrument operation.
 */
uint8_t preset_loadKitMorphForScenes(uint16_t presetNr, uint16_t scene_mask);
/* Settings — keyed root settings.cfg file. */
void    preset_loadGlobals(void);
void    preset_saveGlobals(void);

/* Pattern — async direct serializer in filesystem.c. */
uint8_t preset_loadPattern(uint8_t presetNr);
void    preset_savePattern(uint8_t presetNr);

/* All / Performance — async container serializers in filesystem.c. */
void    preset_saveAll(uint8_t presetNr, uint8_t isAll);
uint8_t preset_loadAll(uint8_t presetNr, uint8_t isAll);

/* Read 8-byte preset name from file header (any type). */
char*   preset_loadName(uint16_t presetNr, uint8_t what);
void    preset_applyLoadedName(void);
/*
 * Load one row from the active Instrument type's shared name cache.
 *
 * browser_index is 16-bit because the single cache exposes rows 0..999; using
 * uint8_t here would wrap selection at row 255 before filesystem validation.
 */
uint8_t preset_loadInstrument(uint8_t destination_scene,
                              uint8_t destination_slot,
                              instrument_type_t type,
                              uint16_t browser_index);
uint8_t preset_loadInstrumentForScenes(uint16_t destination_scene_mask,
                                       uint8_t destination_slot,
                                       instrument_type_t type,
                                       uint16_t browser_index);
/*
 * Save one resident kit voice into the root Instrument/ pool.
 *
 * Inputs: source Scene, zero-based kit voice slot, and the visible stem from
 * nested Save:[Instrument] editing. Output: nonzero only when filesystem
 * accepts the asynchronous write. This is not a numbered library-slot save;
 * the slot coordinate selects one of the six resident kit instruments, while
 * asyncfatfs creates or overwrites Instrument/<stem.ext> by exact case.
 */
uint8_t preset_saveInstrument(uint8_t source_scene,
                              uint8_t source_slot,
                              const char *display_name);
/*
 * Save one resident Instrument through the InstrumentMrp projection.
 *
 * Inputs: source Scene/slot plus edited root Instrument stem. Output:
 * asynchronous Instrument/<stem.ext> save using Morph Save endpoint mapping.
 * Completion does not rename the resident slot or apply runtime state.
 */
uint8_t preset_saveInstrumentMorph(uint8_t source_scene,
                                   uint8_t source_slot,
                                   const char *display_name);
/*
 * Load one Instrument/ file into the destination slot's morph endpoint.
 *
 * Inputs mirror preset_loadInstrument(), but the requested type must match the
 * slot's currently loaded type. Output: the file is parsed through the normal
 * Instrument loader, then only same-type morphable normal endpoint values are
 * copied into the resident morph image. Type mismatches are rejected/no-change.
 */
uint8_t preset_loadInstrumentMorph(uint8_t destination_scene,
                                   uint8_t destination_slot,
                                   instrument_type_t type,
                                   uint16_t browser_index);
/*
 * Generic File/Dir asyncfatfs expansion test requests.
 *
 * These operations deliberately bypass musical preset state. Inputs are exact
 * root-level display names from the temporary Load/Save menus. Outputs are only
 * Preset completion events; Menu reads scan caches and four-byte/Dir results
 * from filesystem.h after PRESET_OP_TEST_* completes.
 */
uint8_t preset_scanTestFiles(void);
uint8_t preset_scanTestDirs(void);
uint8_t preset_loadTestFile(const char *display_name);
uint8_t preset_loadTestDir(const char *display_name);
uint8_t preset_saveTestFile(const char *display_name);
uint8_t preset_saveTestDir(const char *display_name);
uint8_t preset_saveTestSimpleDir(const char *display_name);

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
                                        uint8_t descriptor_index,
                                        instrument_param_value_t value);
uint8_t preset_applyInstrumentRuntimeValue(uint8_t scene_index,
                                           instrument_param_id_t id,
                                           instrument_param_value_t value);
uint8_t preset_applyKitAudioRouting(uint8_t scene_index, uint8_t slot);
void preset_applySceneSettings(uint8_t scene_index);
/*
 * Scene-owned per-voice mix setting setters.
 *
 * Inputs: resident Scene index, zero-based instrument slot, and a value in the
 * UI/storage domain. Outputs: retained SceneData updates; audio_out also
 * applies the active Scene's mixer route immediately. FX send and fader mode
 * deliberately have no runtime output until the FX/fader backend exists.
 *
 * Clients: VOICE mix Scene-setting cells, sceneset load/apply follow-up, and
 * future MIDI/Bank Scene setting mutation.
 */
uint8_t preset_setVoiceAudioOut(uint8_t scene_index, uint8_t slot,
                                uint8_t route);
uint8_t preset_setVoiceFxSendAmount(uint8_t scene_index, uint8_t slot,
                                    uint8_t amount);
uint8_t preset_setVoiceFaderSetting(uint8_t scene_index, uint8_t slot,
                                    uint8_t mode);
uint8_t preset_setSlot6Track7AmpEnvelopeDecay(uint8_t scene_index,
                                              instrument_image_select_t image,
                                              uint8_t value,
                                              uint8_t record_automation);

/*
 * Deferred active-Scene sound apply.
 *
 * preset_startDrumsetApply() swaps immediate Scene settings and arms one bit per
 * instrument slot. preset_tickDrumsetApply() commits at most one pending slot
 * whose old amp envelope is below the quiet threshold. A return value of 1 means
 * one bounded unit was performed; 0 can mean either fully idle or waiting for
 * ringing slots, so callers may poll it from the ordinary foreground loop.
 *
 * preset_applyDeferredSceneSlotForTrigger() is the trigger-time escape hatch:
 * when the newly selected Scene pattern fires a pending slot, it synchronously
 * applies that slot's instrument parameters, LFO slot/targets, audio out, and
 * future per-instrument mix affiliates before the note trigger is dispatched.
 */
void    preset_startDrumsetApply(void);
uint8_t preset_tickDrumsetApply(void);
void    preset_applyDeferredSceneSlotForTrigger(uint8_t trigger_track);
/*
 * Commit and start bounded runtime application for one staged Instrument slot.
 *
 * Inputs: immutable request Scene/slot and filesystem's validated staging
 * payload. Output: inactive Scenes receive retained state only. Active Scene
 * commits clear all outgoing modulation owners, replace/reset the incoming
 * runtime, rebuild all six Morph images, and rebind one normalized source per
 * tick. Client: Menu's Instrument Load completion handler.
 *
 * This remains separate from the Kit cursor because Instrument commit must
 * preserve the outgoing slot identity until targets are cleared, whereas Kit
 * loading has already atomically replaced a fully staged six-slot payload.
 */
void    preset_startInstrumentApply(uint8_t scene_index, uint8_t slot);
/*
 * Commit staged morph-load endpoints and drain the Morph worker.
 *
 * KitMrp and InstrumentMrp change endpoint values only. They must not clear
 * modulation, reset instrument runtime objects, replace display names, or
 * apply routing. These starters preserve slot identity and reuse the bounded
 * Morph worker so active-scene interpolation is refreshed safely.
 */
void    preset_startKitMorphApply(void);
void    preset_startInstrumentMorphApply(uint8_t scene_index, uint8_t slot);
uint8_t preset_tickInstrumentApply(void);

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
void    preset_morphScene(uint8_t scene_index, uint8_t morph);
void    preset_morphVoiceScene(uint8_t scene_index, uint8_t slot,
                               uint8_t morph);
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
