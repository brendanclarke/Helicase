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
    /* Hidden `.hctmp.<ext>` save that prepares Menu's reversible `kit` row. */
    PRESET_OP_INSTRUMENT_TEMP_SAVE,
    /* Hidden Morph-only `.hctmp` save/load for InstrumentMrp's reversible row. */
    PRESET_OP_INSTRUMENT_MORPH_TEMP_SAVE,
    PRESET_OP_KIT_SAVE,
    PRESET_OP_KIT_MORPH_LOAD,
    PRESET_OP_KIT_MORPH_SAVE,
    PRESET_OP_INSTRUMENT_MORPH_LOAD,
    PRESET_OP_INSTRUMENT_MORPH_TEMP_LOAD,
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
 * Read the explicit single Scene retained for an Instrument action or Kit Save.
 *
 * Output: the request-time destination/source Scene while the asynchronous
 * operation is pending or completing. Instrument completion uses it for the
 * exact one-slot DSP apply; Kit Save completion uses it for the matching Kit
 * plus six-Instrument HCNAMES block after the Save UI has reset its selection.
 */
uint8_t          preset_getRequestScene(void);
/*
 * Read the immutable multi-Scene mask retained for a Kit-family load.
 *
 * Menu uses this exact accepted mask after a normal full Kit commit to refresh
 * HCNAMES for every destination, instead of trusting later panel state. This is
 * a read-only view of existing Preset request state and adds no SRAM storage.
 */
uint16_t         preset_getKitRequestSceneMask(void);
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
/*
 * Bank requests use settings.cfg only for the boot restore slot.  Successful
 * Bank loads publish their own HCNAMES Bank/child provenance through the
 * filesystem close gate; neither this interface nor settings.cfg owns Scene
 * source state.
 */
uint8_t preset_loadBank(uint16_t presetNr, uint16_t scene_mask);
uint8_t preset_saveBank(uint16_t presetNr, uint16_t scene_mask,
                        uint8_t force_save);
uint8_t preset_completedBankLoadedScene(void);
uint16_t preset_bankLoadFailedSceneMask(void);
uint8_t preset_loadFirstAvailableSceneOrKit(void);
/*
 * Save the active resident Scene into the root Scene library.
 *
 * Inputs: direct root Scene slot and the current eight-cell preset_currentName
 * edited by the Save page. Output: asynchronous Scene directory write and a
 * PRESET_OP_SCENE_SAVE completion. Saving does not alter the retained HCNAMES
 * source provenance; failure likewise preserves it. This is separate from
 * preset_saveDrumset() because Scene Save serializes Scene settings, embedded
 * Kit, Pattern, and Effect placeholder, not only the Kit payload.
 * Affiliates: filesystem's HCNAMES identity/source register.
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
/*
 * Post one explicit Settings request.
 *
 * Inputs: current settings storage authority. Output: one only when the
 * filesystem accepted the asynchronous request; zero leaves Preset idle. Menu
 * uses this accepted/rejected result to enter its `...` confirmation state
 * only for work that will actually complete.
 */
uint8_t preset_loadGlobals(void);
uint8_t preset_saveGlobals(void);

/* Pattern — async direct serializer in filesystem.c. */
uint8_t preset_loadPattern(uint16_t presetNr);
uint8_t preset_loadPatternForScenes(uint16_t presetNr, uint16_t scene_mask);
uint8_t preset_savePattern(uint16_t presetNr);

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
 * Restore the original one-voice normal Instrument Load image from the
 * filesystem staging preview.
 *
 * Inputs: the Menu session's unchanged Scene/voice. Output: nonzero when a
 * valid preview image was committed and the ordinary bounded runtime apply was
 * armed; no file is opened and no name/key is stored in Preset. The preview is
 * intentionally available only until Menu changes load mode/type, instrument
 * type, Scene, voice, or leaves nested Instrument Load.
 *
 * Affiliates: filesystem_instrumentLoadPreviewOriginal(),
 * preset_tickInstrumentApply(), and Core/Menu/menu.c's `kit` browser row.
 */
/*
 * Start the hidden reversible `kit` save/load operations for Instrument Load.
 * Inputs: Menu's selected Scene/voice/type. Output: ordinary asynchronous
 * completion; temporary files do not alter HCNAMES or `.hcindex`. Affiliates:
 * filesystem temp APIs and Menu's one nine-byte kit-name lifetime.
 */
uint8_t preset_saveInstrumentTemp(uint8_t source_scene, uint8_t source_slot);
uint8_t preset_loadInstrumentTemp(uint8_t destination_scene,
                                  uint8_t destination_slot,
                                  instrument_type_t type);
/* Save/load only the current Morphable Morph endpoints for the reversible
 * InstrumentMrp `kit` row. */
uint8_t preset_saveInstrumentMorphTemp(uint8_t source_scene,
                                       uint8_t source_slot);
uint8_t preset_loadInstrumentMorphTemp(uint8_t destination_scene,
                                       uint8_t destination_slot,
                                       instrument_type_t type);
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
 * Retired File/Dir diagnostic compatibility requests.
 *
 * Inputs are ignored and output is always zero: the Menu no longer lists these
 * types, and these declarations prevent stale developer-only code from
 * rebuilding the removed filesystem diagnostic cache. Affiliates: matching
 * zero-work filesystem compatibility APIs in filesystem.h/.c.
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
 * Retained mutation contract: both public endpoint setters compare and commit
 * through one generic Preset store boundary, then mark the matching normal or
 * Morph Autosave descriptor cell only when its final byte changed. A future
 * registry descriptor automatically follows this path when edited through
 * these setters; direct endpoint assignments are restricted to validated
 * whole-object/load paths that must use the appropriate region marker. Derived
 * morph_interpolation[] never represents autosave data.
 *
 * Accessors/clients:
 * - storageTypes/filesystem populate Scene slots directly during load.
 * - menu.c load completion calls preset_startDrumsetApply(), which uses these
 *   functions in bounded foreground chunks.
 * - presetMorphEngine calls preset_applyInstrumentRuntimeValue() after it
 *   rebuilds morph_interpolation[].
 * - future Menu/MIDI descriptor editors should call the setters instead of
 *   touching SceneData arrays directly.
 *
 * Runtime contract (S075 F3): an endpoint edit on the active Scene applies
 * only the edited parameter's interpolation at the voice's resolved Morph
 * amount (presetMorph_applyParameterNow()), never the raw value, and never
 * over a parameter held by step automation (automation always wins; the next
 * trigger applies the new interpolation).
 */
uint8_t preset_setInstrumentParameter(uint8_t scene_index, uint8_t slot,
                                      uint8_t descriptor_index,
                                      instrument_image_select_t image,
                                      uint8_t value);
uint8_t preset_setSupplementalParameter(uint8_t scene_index, uint8_t slot,
                                        uint8_t descriptor_index,
                                        instrument_param_value_t value);
/*
 * Enter one instrument parameter from external MIDI (S075 F3, user P1:
 * MIDI takes the lowest priority).
 *
 * What: stores `value` as the active Scene's Normal endpoint of one
 * descriptor and queues that voice for the Morph sweep. Nothing is written
 * to the runtime here: the sweep applies the new interpolation whenever it
 * reaches the parameter, and skips it while step automation holds it.
 * Non-morphable parameters (never automatable, never swept) go through
 * preset_setSupplementalParameter(), which stores and applies them as a menu
 * edit does.
 * Why: automation wins, then menu edits, then MIDI. A CC is an endpoint entry,
 * not a runtime override, so it can override neither automation nor the Morph
 * interpolation.
 * Inputs: slot 0..5, descriptor-local index for the active Scene's slot type,
 * and a value already clamped to the descriptor domain
 * (menu_clampInstrumentValue()). Output: 1 when stored/queued, 0 for an
 * invalid slot/descriptor. Retention: a changed byte marks its AutoSave
 * Normal cell and clears the Scene's card-clean bit, like a menu edit.
 * Active Scene only (no edit-mask fan-out). Caller: MidiParser.c
 * midiParser_enterTaggedParameter(). Affiliates:
 * preset_storeInstrumentEndpoint(), presetMorph_requestVoice(),
 * seq_automationHoldsParameter().
 */
uint8_t preset_setInstrumentParameterFromMidi(uint8_t slot,
                                              uint8_t descriptor_index,
                                              uint8_t value);
uint8_t preset_applyInstrumentRuntimeValue(uint8_t scene_index,
                                           instrument_param_id_t id,
                                           instrument_param_value_t value);
/*
 * Runtime apply that ignores the active-Scene write guard (S077 P2 §3.4).
 *
 * Inputs: Scene index, slot/descriptor-index instrument ID, and descriptor
 * image value. Output: the live runtime is written even when the Scene is not
 * the active Scene. Client: presetMorph_writeRuntimeBaseEx() when force is set
 * by the per-track single-voice apply. Affiliate: the guarded public wrapper.
 */
uint8_t preset_applyInstrumentRuntimeValueForced(uint8_t scene_index,
                                                 instrument_param_id_t id,
                                                 instrument_param_value_t value);
uint8_t preset_applyKitAudioRouting(uint8_t scene_index, uint8_t slot);
/*
 * Apply one voice's output route without retaining it in SceneData.
 *
 * Inputs: zero-based instrument slot and mixer route enum value. Output: the
 * active mixer route is updated without AutoSave or Bank-clean side effects.
 * Scene-target step automation uses this transient path; restore calls
 * preset_applyKitAudioRouting() so the retained Scene route remains the base.
 */
void preset_applyVoiceAudioOutRuntime(uint8_t slot, uint8_t route);
/*
 * Runtime-only Scene-setting step overlays used by the live VOICE superpage.
 *
 * Inputs: setters receive a zero-based voice slot and route/amount. Outputs:
 * effective getters return the transient step value while active, otherwise
 * retained SceneData. These APIs never mark AutoSave or alter retained Scene
 * settings; audio-out DSP restore remains owned by preset_applyKitAudioRouting.
 * The audio/FX tables are six-entry runtime state and are cleared at transport
 * restore and preset_init(). The mixer pulls the effective FX-send getter each
 * block; it reads the step override, otherwise the Normal/Morph endpoints
 * interpolated by the voice's resolved Morph amount (S075 F2-H). The display
 * getter returns the step override or Normal endpoint, never interpolation.
 */
void preset_setAudioOutStepOverride(uint8_t slot, uint8_t route);
void preset_clearAllAudioOutStepOverrides(uint8_t scene_index);
uint8_t preset_getEffectiveAudioOut(uint8_t scene_index, uint8_t slot);
void preset_setFxSendStepOverride(uint8_t slot, uint8_t amount);
void preset_clearAllFxSendStepOverrides(void);
uint8_t preset_getEffectiveFxSendAmount(uint8_t scene_index, uint8_t slot);
uint8_t preset_getFxSendDisplayAmount(uint8_t scene_index, uint8_t slot);
void preset_applySceneSettings(uint8_t scene_index);
/*
 * Scene-owned per-voice mix setting setters.
 *
 * Inputs: resident Scene index, zero-based instrument slot, and a value in the
 * UI/storage domain. Outputs: retained SceneData updates; audio_out also
 * applies the active Scene's mixer route immediately. FX send and fader mode
 * (pre/pst/fx/xfd) have no push step: the mixer reads both every block.
 *
 * Clients: VOICE mix Scene-setting cells, sceneset load/apply follow-up, and
 * future MIDI/Bank Scene setting mutation. FX send has two endpoints: the
 * Normal setter below and the Morph setter (S075 F2-H); both are retained in
 * SceneData.
 */
uint8_t preset_setVoiceAudioOut(uint8_t scene_index, uint8_t slot,
                                uint8_t route);
uint8_t preset_setVoiceFxSendAmount(uint8_t scene_index, uint8_t slot,
                                    uint8_t amount);
uint8_t preset_setVoiceFxSendMorph(uint8_t scene_index, uint8_t slot,
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
 * preset_startDrumsetApply() first detaches the outgoing all-source modulation
 * graph, swaps immediate Scene settings, and arms one bit per instrument slot.
 * preset_tickDrumsetApply() commits at most one quiet slot's tagged runtime
 * type, routing, and descriptor image per pass. Once every member is valid,
 * it reuses the existing Instrument apply cursor to normalize/rebind every
 * source slot's velocity plus both LFO pairs. The pre-audio boot counterpart
 * establishes only a safe temporary image; main.c calls
 * preset_startDrumsetApply() after audio startup so boot uses this entire
 * worker, including its clear/image/rebind order, rather than a partial
 * boot-only target-install sequence. Thus target tokens are resolved only
 * against the complete incoming type layout, not a stale physical-slot
 * assumption. A return value of 1 means one bounded unit was performed; 0 can
 * mean either fully idle or waiting for ringing slots, so callers may poll it
 * from the ordinary foreground loop.
 *
 * preset_applyDeferredSceneSlotForTrigger() is the trigger-time escape hatch:
 * when the newly selected Scene pattern fires a pending slot, it synchronously
 * applies that slot's type, descriptor image, and audio out before the note
 * trigger is dispatched. Cross-slot LFO/velocity bindings remain detached
 * until all pending members finish, when the common rebind cursor installs
 * them. The one pre-worker teardown is required because a tagged runtime
 * replacement overwrites outgoing engine bytes to which old modulation nodes
 * may point. This lifecycle adds no retained apply state.
 */
void    preset_startDrumsetApply(void);
uint8_t preset_tickDrumsetApply(void);
/*
 * Per-track single-voice deferred apply (S077 P2 §2.1, §3.4).
 *
 * Inputs: slot 0..5, source Scene index. Output: the drumset worker's bit for
 * this slot is cleared (the single-voice apply supersedes it), the slot's LFO
 * phase is snapshotted, its outgoing modulation graph is cleared, the source
 * Scene's audio routing is applied, and the slot is armed for a deferred
 * quiet-wait commit from its played Scene. Client: the buttonHandler PERF
 * hold-VOICE+press-SEQ gesture. Affiliate: preset_tickDrumsetApply() polls the
 * single-voice worker before the Scene worker.
 */
void    preset_startSingleVoiceApply(uint8_t slot, uint8_t source_scene);
/*
 * Resolve one slot's played Scene for per-track playback (S077 P2 §3.5).
 *
 * Inputs: slot 0..5. Output: the resident Scene playback reads for that slot's
 * track (seq_activePattern when no override is set). Client: mixer_faderGains()
 * resolves per-voice FX send / fader mode / Morph amount through this.
 */
uint8_t preset_getSlotPlayedScene(uint8_t slot);
/*
 * Report whether the Scene and Instrument apply workers are idle (S075).
 *
 * Output: nonzero when neither the Scene (drumset) worker nor the Instrument
 * apply worker is active. Why: a Scene-level paste or clear touching the
 * active Scene waits at the head of the copy/clear queue until the previous
 * apply (for example after a PERF Scene switch) has finished, so it never
 * starts a second apply over a running one. Client: copyClearService.c.
 */
uint8_t preset_applyWorkersIdle(void);
void    preset_applyDeferredSceneSlotForTrigger(uint8_t trigger_track);
/*
 * Commit and start bounded runtime application for one staged Instrument slot.
 *
 * Inputs: immutable request Scene/slot, filesystem's validated staging payload,
 * and a root-pool versus hidden-temporary persistence decision that Menu reads
 * from filesystem's immutable completed-request flag. Output: inactive Scenes receive retained
 * state only. A root-pool commit marks its complete type/Normal/Morph payload
 * for AutoSave immediately at each retained Scene destination; hidden `kit`
 * restore is deliberately non-marking. Active Scene commits clear all outgoing
 * modulation owners, replace/reset the incoming runtime, rebuild all six Morph
 * images, and rebind one normalized source per tick. Client: Menu's Instrument
 * Load completion handler.
 *
 * This remains separate from the Kit cursor because Instrument commit must
 * preserve the outgoing slot identity until targets are cleared, whereas Kit
 * loading has already atomically replaced a fully staged six-slot payload.
 */
void    preset_startInstrumentApply(uint8_t scene_index,
                                    uint8_t slot,
                                    uint8_t mark_autosave_whole_instrument);
/*
 * Commit one resident Instrument slot onto a slot in a set of Scenes (S075).
 *
 * What: copies type, Normal and Morph images from a resident source slot to
 * dst_slot of every Scene in dst_mask through the same commit path as
 * Instrument Load (Bank-present publication, whole-Instrument AutoSave
 * marker, card-clean invalidation, runtime modulation clear and bounded
 * apply when the active Scene is touched), with `self` LFO selectors moved
 * from the source slot to the destination slot. Why: `copy instrument` (with
 * edit-mask fan-out, user F3) must behave exactly like a load of that
 * Instrument. Inputs: source Scene/slot, destination mask (the caller has
 * checked the Advanced limit) and slot. Output: none; when the active Scene
 * is in dst_mask, drive preset_tickInstrumentApply() until it returns 0.
 * The slot-6/track-7 Kit decay pair is not part of the image (the caller
 * copies it only slot 6 -> slot 6). Client: copyOps.c.
 */
void    preset_startInstrumentCopy(uint8_t src_scene, uint8_t src_slot,
                                   uint16_t dst_mask, uint8_t dst_slot);
/*
 * Commit staged KitMrp or InstrumentMrp endpoints and drain the bounded Morph
 * worker without replacing identity, Normal images, routing, or modulation
 * ownership.
 *
 * Inputs: filesystem-owned validated staging plus the immutable selected
 * destination coordinates. Outputs: InstrumentMrp marks only the committed
 * destination's Morphable Morph descriptors; KitMrp marks the same
 * Morphable-only descriptor domain for each successfully type-compatible
 * selected slot and commits its generated slot-6 Morph-decay setting through
 * SceneData's named Kit setter. Mismatched slots, types, Normal endpoints,
 * HCNAMES identity/source, and routing remain unchanged. Why: each loader
 * imports endpoint data without becoming a normal Kit/Instrument replacement.
 * The active Scene alone receives the existing deferred runtime refresh; an
 * inactive selected Scene can publish only if it is already Bank-present.
 * Affiliates: preset_commitStagedKitNormalToMorph(),
 * preset_commitStagedInstrumentNormalToMorph(),
 * autosave_markInstrumentMorphDirty(), and
 * scene_setSlot6Track7MorphAmpEnvelopeDecay().
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
 * is required after endpoint loads/edits. The former global `srt` control was
 * retired in S075; the PERF slot now mirrors Effect Morph.
 *
 * Serialized-owner rule: overall Morph is committed through
 * SceneData's change-aware setters before runtime mirrors/work are updated.
 * Inputs and runtime outputs remain unchanged; equal retained values create no
 * autosave mutation. Affiliates: SceneData's named Scene parameter boundary
 * and Autosave's scalar marker.
 */
void    preset_morph(uint8_t morph);
void    preset_morphVoice(uint8_t slot, uint8_t morph);
void    preset_morphScene(uint8_t scene_index, uint8_t morph);
void    preset_morphVoiceScene(uint8_t scene_index, uint8_t slot,
                               uint8_t morph);
void    preset_rebuildMorph(void);
/*
 * Endpoint-only morph helpers for copy/clear (S076 P3).
 *
 * What:       preset_resetSlotMorphToNormal() equalises one instrument slot's
 *             Morph endpoint to its current Normal endpoint; it iterates the
 *             slot's instrument type descriptor table and, for every
 *             descriptor whose flags include INSTRUMENT_PARAM_FLAG_MORPHABLE,
 *             copies instrument_parameters[i] onto
 *             morph_instrument_parameters[i]. Non-morphable descriptors are
 *             untouched.
 *             preset_copySlotNormalToMorph() copies one source Scene/slot's
 *             Normal endpoints onto another Scene/slot's Morph endpoints,
 *             requiring the two instrument types to match; a mismatch is a
 *             complete no-change for that slot.
 * Why:        the "reset morph" clear operations (track- and Scene-level) and
 *             the "inst -> morph"/"scene -> morph" copy operations share this
 *             morphable-descriptor endpoint loop. Keeping the byte writes and
 *             the descriptor iteration inside the Preset owner path matches
 *             the KitMrp/InstrumentMrp endpoint commits and avoids firing
 *             preset_setInstrumentParameter()'s per-byte runtime and fan-out
 *             work for a batch the caller will rebuild once.
 * Inputs:     reset: scene_index (0..15), slot (0..5); copy: src_scene/src_slot
 *             (Normal source) and dst_scene/dst_slot (Morph target), all valid.
 * Outputs:    the count of Morph bytes changed. Zero for invalid coordinates or
 *             a type mismatch. On change the destination slot's Morph scope and
 *             the owning Scene's card-clean bit are marked. The helpers do NOT
 *             queue the Morph worker; the caller does that after all writes.
 * Accessors:  scene_instrumentSlot(), instrumentManager_registryEntry(),
 *             autosave_markInstrumentMorphDirty(), bank_invalidateSdCleanScene().
 * Affiliates: ccClear_runResetMorphTrack(), ccClear_runResetSceneMorph(),
 *             ccCopy_runMorphTrack(), ccCopy_runSceneMorph(),
 *             preset_rebuildMorph().
 */
uint8_t preset_resetSlotMorphToNormal(uint8_t scene_index, uint8_t slot);
uint8_t preset_copySlotNormalToMorph(uint8_t src_scene, uint8_t src_slot,
                                     uint8_t dst_scene, uint8_t dst_slot);
/*
 * S074 master bus compressor Scene settings (cmp, cam, ctm, csc).
 *
 * preset_setBusCompSetting() clamps and commits one field of one Scene through
 * SceneData, then refreshes that field's active-page mirror. The setter is
 * used by the VOICE edit-mask fan-out; preset_syncBusCompMirrors() copies the
 * active Scene's four retained values into parameter_values[]. Neither path
 * pushes runtime DSP state because BusCompressor reads SceneData per block.
 * Affiliates: menu.c and preset_applySceneSettings().
 */
void    preset_setBusCompSetting(uint8_t scene_index, uint8_t field,
                                 uint8_t value);
void    preset_syncBusCompMirrors(void);
/* Refresh the PERF `fxm` mirror from the active Scene's Effect Morph (S075). */
void    preset_syncEffectMorphMirror(void);
void    preset_morphTick(void);
uint8_t preset_getMorphValue(uint16_t index, uint8_t morph);

#endif
