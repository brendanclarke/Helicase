#ifndef INSTRUMENT_MANAGER_H_
#define INSTRUMENT_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>
#include "ParameterArray.h"

/*
 * Canonical instrument registry contract.
 *
 * Why: file keys, Menu metadata, routing validity, and local parameter identity
 * were previously maintained in unrelated tables. These types give every
 * client one immutable description while leaving mutable values in SceneData.
 * Inputs/outputs: lookup functions accept a type, key, local ID, or Menu cell
 * and return const registry data. Affiliates are SceneData, storageTypes,
 * Preset, Menu, modulation, automation, and the legacy kit converter.
 */
#define INSTRUMENT_SLOT_COUNT       6u
#define INSTRUMENT_PARAM_COUNT      64u
#define INSTRUMENT_VOICE_ID_COUNT   384u
#define INSTRUMENT_TOTAL_ID_COUNT   512u
#define INSTRUMENT_PARAM_INVALID    0xffffu

typedef uint16_t instrument_param_id_t;
/*
 * Byte-sized retained instrument parameter values.
 *
 * Instrument files, SceneData endpoint images, Morph interpolation, and
 * descriptor-backed menu edits all store one byte per descriptor cell. Normal
 * musical parameters use their existing 0..127 or 0..255 UI domain. Target
 * selector cells store compact local tokens instead of packed canonical target
 * IDs so the same arrays never need to widen for routing metadata.
 */
typedef uint8_t instrument_param_value_t;
/*
 * Retained modulation target token.
 *
 * Target parameter selector rows store compact byte tokens. 0xff is the sole
 * off value. Values 0..63 address local descriptor indices in the selected
 * namespace. The velocity destination row also reserves 0x40 for the source
 * voice's own Scene Morph target, preserving the LXR-02 velocity-to-Morph
 * workflow without storing a wide Scene target ID. Runtime code expands the
 * byte token into a canonical instrument_param_id_t only while installing or
 * displaying a modulation target.
 */
typedef uint8_t instrument_target_token_t;
#define INSTRUMENT_TARGET_TOKEN_OFF          0xffu
#define INSTRUMENT_TARGET_TOKEN_MAX_LOCAL    ((uint8_t)(INSTRUMENT_PARAM_COUNT - 1u))
#define INSTRUMENT_TARGET_TOKEN_VOICE_MORPH  0x40u
/*
 * LFO target namespace values stored in lfo_target_voice cells.
 *
 * Values 1..6 select instrument voices. Value 7 selects the Scene namespace
 * shown by Menu as `scn`; future values above the instrument voice range can
 * select effects or other target tables while lfo_target_param remains a local
 * byte token.
 */
#define INSTRUMENT_TARGET_VOICE_FIRST 1u
#define INSTRUMENT_TARGET_VOICE_LAST  INSTRUMENT_SLOT_COUNT
#define INSTRUMENT_TARGET_VOICE_SCENE ((uint8_t)(INSTRUMENT_SLOT_COUNT + 1u))

typedef enum {
    INSTRUMENT_TYPE_DRM = 0,
    INSTRUMENT_TYPE_SNR,
    INSTRUMENT_TYPE_CYM,
    INSTRUMENT_TYPE_HAT,
    INSTRUMENT_TYPE_UNKNOWN
} instrument_type_t;

typedef enum {
    INSTRUMENT_TARGET_MODULATION = 0,
    INSTRUMENT_TARGET_AUTOMATION
} instrument_target_use_t;

typedef enum {
    INSTRUMENT_BIND_NONE = 0,
    INSTRUMENT_BIND_INSTANCE_OFFSET,
    INSTRUMENT_BIND_SLOT_DECIMATION,
    INSTRUMENT_BIND_VELOCITY_AMOUNT,
    INSTRUMENT_BIND_VELOCITY_TARGET,
    INSTRUMENT_BIND_LFO_TARGET_VOICE,
    INSTRUMENT_BIND_LFO_TARGET_PARAM,
    /*
     * Second LFO target pair bindings.
     *
     * Voice and parameter selectors are supplemental descriptor cells whose
     * local indices can move as instrument definitions evolve. Pair 2 needs
     * its own binding identities so Menu, storage, and InstrumentManager can
     * find the matching sibling cells without hardcoded descriptor positions
     * and without sharing pair 1's SceneData storage.
     */
    INSTRUMENT_BIND_LFO_TARGET_VOICE_2,
    INSTRUMENT_BIND_LFO_TARGET_PARAM_2
} instrument_binding_kind_t;

typedef struct {
    instrument_binding_kind_t kind;
    uint16_t offset;
    uint8_t parameter_type;
} instrument_runtime_binding_t;

#define INSTRUMENT_PARAM_FLAG_MORPHABLE       0x01u
#define INSTRUMENT_PARAM_FLAG_MODULATABLE     0x02u
#define INSTRUMENT_PARAM_FLAG_AUTOMATABLE     0x04u

#define INSTRUMENT_FLAG_BASIC                 0x01u
#define INSTRUMENT_FLAG_ADVANCED              0x02u
#define INSTRUMENT_FLAG_CHOKE                 0x04u

#define INSTRUMENT_MOD_DOMAIN_NONE            0x00u
#define INSTRUMENT_MOD_DOMAIN_CONTINUOUS      0x01u
#define INSTRUMENT_MOD_DOMAIN_INTEGER         0x02u
#define INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX     0x04u

typedef struct {
    instrument_param_value_t min_value;
    instrument_param_value_t max_value;
    uint8_t flags;
} instrument_mod_domain_t;

typedef struct {
    const char *file_key;
    const char *short_name;
    const char *long_name;
    const char *category;
    uint8_t dtype;
    uint8_t flags;
    /*
     * Parameter-domain modulation contract.
     *
     * Inputs: instrument parameter files declare this beside each descriptor
     * row. Output: InstrumentManager uses it to decide whether transient LFO
     * overlays may target the row and which descriptor-space value range is
     * legal. The domain deliberately describes the value before owner-specific
     * DSP shaping: envelope, pitch, filter, transient, distortion, and LFO-rate
     * conversion still lives in instrumentManager_writeRuntime() and the DSP
     * owner setters. This prevents ModulationNode from guessing from C scalar
     * type or learning a hardcoded parameter list.
     */
    instrument_mod_domain_t mod_domain;
    instrument_runtime_binding_t runtime;
} ParamDescriptor;

#define INSTRUMENT_MENU_EMPTY 0xffu
#define INSTRUMENT_MENU_SKIP  0xfeu
#define INSTRUMENT_MENU_PAGE_CELLS 16u

typedef struct {
    /*
     * Instrument-owned voice sub-page cells.
     *
     * Static pages still use the old Page top/bottom arrays with eight cells.
     * Instrument pages are wider so one SELECT sub-page can expose up to four
     * four-parameter screens. Menu owns the current screen index and resolves
     * absolute positions here; descriptor arrays remain the storage/file-key
     * source of truth.
     */
    uint8_t descriptor_index[INSTRUMENT_MENU_PAGE_CELLS];
} instrument_menu_page_t;

typedef struct {
    instrument_type_t type;
    const char *type_text;
    const char *display_label;
    const char *extension;
    /*
     * Storage directory owned by this registry row.
     *
     * Inputs: none; this is immutable product metadata. Output: the exact
     * display component used below /Instrument/ for this instrument type.
     * Filesystem clients use instrumentManager_storageDirectory() instead of
     * duplicating Drum/Snare/Cymbal/HiHat folder names in storage state
     * machines. Keeping the directory beside the extension makes type
     * classification and storage navigation change together.
     */
    const char *storage_directory;
    uint8_t type_flags;
    const ParamDescriptor *descriptors;
    uint8_t descriptor_count;
    const instrument_menu_page_t *menu_pages;
    uint8_t menu_page_count;
} instrument_registry_entry_t;

instrument_param_id_t instrumentParam_make(uint8_t slot, uint8_t descriptor_index);
uint8_t instrumentParam_isVoiceParameter(instrument_param_id_t id);
uint8_t instrumentParam_slot(instrument_param_id_t id);
uint8_t instrumentParam_local(instrument_param_id_t id);

const instrument_registry_entry_t *instrumentManager_registryEntry(
    instrument_type_t type);
uint8_t instrumentManager_registryCount(void);
const instrument_registry_entry_t *instrumentManager_registryEntryAt(
    uint8_t index);
instrument_type_t instrumentManager_typeFromText(const char *text);
const char *instrumentManager_typeDisplayLabel(instrument_type_t type);
uint8_t instrumentManager_typeFlags(instrument_type_t type);
uint8_t instrumentManager_advancedCountForScene(uint8_t scene_index,
                                                uint8_t ignore_slot);
uint8_t instrumentManager_typeSelectableForSceneSlot(
    uint8_t scene_index, uint8_t destination_slot, instrument_type_t candidate);
uint8_t instrumentManager_filenameMatchesType(const char *filename,
                                               instrument_type_t type);
/*
 * Return the registry-owned Instrument storage directory.
 *
 * Inputs: one registered instrument type. Output: the immutable directory
 * component below /Instrument/, or NULL for INSTRUMENT_TYPE_UNKNOWN. Clients:
 * filesystem boot index creation, per-type index loading, Instrument scan,
 * and Instrument Save. This accessor is the only cross-module path to the
 * registry's folder metadata, so filesystem.c cannot grow a second hardcoded
 * type-to-directory table.
 */
const char *instrumentManager_storageDirectory(instrument_type_t type);
const ParamDescriptor *instrumentManager_descriptor(instrument_type_t type,
                                                     uint8_t descriptor_index);
const ParamDescriptor *instrumentManager_descriptorByKey(instrument_type_t type,
                                                          const char *file_key);
const ParamDescriptor *instrumentManager_descriptorIndexByKey(
    instrument_type_t type, const char *file_key, uint8_t *index_out);
const ParamDescriptor *instrumentManager_menuDescriptor(instrument_type_t type,
                                                         uint8_t page,
                                                         uint8_t position);
const ParamDescriptor *instrumentManager_menuDescriptorIndex(
    instrument_type_t type, uint8_t page, uint8_t position, uint8_t *index_out);
const ParamDescriptor *instrumentManager_voicePageDescriptorIndex(
    instrument_type_t type, uint8_t voice_page, uint8_t page, uint8_t position,
    uint8_t *index_out);
uint8_t instrumentManager_chokeDescriptorIndexForBase(
    instrument_type_t type, uint8_t base_index, uint8_t *choke_index_out);
/*
 * Find an instrument descriptor by runtime binding kind.
 *
 * Inputs: an instrument type, the binding kind to search for, and an optional
 * output pointer for the descriptor index. Output: the matching descriptor, or
 * NULL when the type is unknown or the binding is not exposed by that
 * instrument. The index output is written only on success.
 *
 * Why this exists outside Menu: Menu needs to relate sibling descriptor-backed
 * cells such as lfo_target_voice and lfo_target_param, but those cells are
 * owned by each instrument registry entry and can move as instrument
 * definitions evolve. Keeping the scan here preserves InstrumentManager as the
 * source of descriptor structure while avoiding hardcoded menu lists.
 *
 * Common clients: Menu's coupled LFO target picker, storage/load
 * normalization, and future dynamic instrument-slot replacement. Affiliate
 * data: instrument_registry_entry_t::descriptors and ParamDescriptor::runtime.
 */
const ParamDescriptor *instrumentManager_descriptorIndexForBinding(
    instrument_type_t type, instrument_binding_kind_t kind, uint8_t *index_out);
struct kit_instrument_slot;
void instrumentManager_resetSlot(struct kit_instrument_slot *slot,
                                 instrument_type_t type);
uint8_t instrumentManager_targetValid(uint8_t scene_index,
                                      instrument_param_id_t id,
                                      instrument_target_use_t use);
/*
 * Validate one local descriptor index on a specific target slot.
 *
 * Inputs: Scene index, zero-based target slot, local descriptor index, and the
 * requested target use. Output: nonzero only when that target slot currently
 * contains an instrument whose descriptor exists and carries the required
 * target flag. This is a convenience wrapper around canonical target IDs.
 *
 * Why this is separate from instrumentManager_targetValid(): the LFO target
 * picker navigates a local descriptor list for one selected voice slot. It
 * should not duplicate canonical ID packing rules or validity checks inside
 * Menu. Existing instrumentManager_targetValid() remains the canonical-ID
 * validator for storage/runtime paths; this helper is the local-index adapter
 * for UI enumeration.
 *
 * Common clients: Menu target pickers, load-time pair normalization, and future
 * automation/modulation browsers. Affiliates: instrumentParam_make(),
 * SceneData's active kit slots, and descriptor capability flags.
 */
uint8_t instrumentManager_targetLocalValid(uint8_t scene_index,
                                           uint8_t target_slot,
                                           uint8_t local,
                                           instrument_target_use_t use);
/*
 * Convert between retained target tokens and canonical runtime target IDs.
 *
 * Retained storage keeps only byte-sized target tokens. Runtime modulation,
 * automation, and display helpers still need canonical IDs because installed
 * nodes share one target namespace. These helpers are the only place that may
 * combine a target slot, Scene namespace, or source-voice Morph token with a
 * local byte token to build a wider id.
 */
uint8_t instrumentManager_targetTokenValidForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t token,
    instrument_target_use_t use);
instrument_param_id_t instrumentManager_targetIdFromTokenForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t token,
    instrument_target_use_t use);
instrument_target_token_t instrumentManager_targetTokenFromIdForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_param_id_t id,
    instrument_target_use_t use);
instrument_target_token_t instrumentManager_stepTargetTokenForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t current,
    int8_t direction,
    instrument_target_use_t use);
uint8_t instrumentManager_lfoTargetVoiceValid(uint8_t voice);
instrument_param_id_t instrumentManager_lfoTargetIdFromToken(
    uint8_t scene_index,
    uint8_t source_slot,
    uint8_t target_voice,
    instrument_target_token_t token,
    instrument_target_use_t use);
instrument_target_token_t instrumentManager_lfoTargetTokenFromId(
    uint8_t scene_index,
    uint8_t target_voice,
    instrument_param_id_t id,
    instrument_target_use_t use);
instrument_target_token_t instrumentManager_stepLfoTargetToken(
    uint8_t scene_index,
    uint8_t target_voice,
    instrument_target_token_t current,
    int8_t direction,
    instrument_target_use_t use);
uint8_t instrumentManager_targetValidForVelocitySource(
    uint8_t scene_index, uint8_t source_slot, instrument_target_token_t token);
/*
 * Step through valid targets for one target slot.
 *
 * Inputs: Scene index, zero-based target slot, the current canonical target or
 * INSTRUMENT_PARAM_INVALID for off, signed direction, and target use. Output:
 * the next canonical target in descriptor order, or INSTRUMENT_PARAM_INVALID
 * for the single off position. Positive direction moves from off to the first
 * valid descriptor and then forward; negative direction moves backward and
 * returns off before the first valid descriptor. Non-modulatable descriptors
 * are skipped rather than surfaced as repeated off entries.
 *
 * Why this cannot live inside menu_encoderChangeParameter(): encoder, endless
 * knobs, file normalization, and future UI browsers all need the same
 * registry-driven traversal. The traversal belongs beside descriptor validity
 * so callers do not know or cache per-instrument target lists.
 *
 * Common clients: Menu's LFO target parameter editor and future target-picker
 * UIs. Affiliates: SceneData slot type lookup, instrument registry descriptor
 * order, instrumentManager_targetValid(), and INSTRUMENT_PARAM_INVALID.
 */
instrument_param_id_t instrumentManager_stepTargetForSlot(
    uint8_t scene_index, uint8_t target_slot, instrument_param_id_t current,
    int8_t direction, instrument_target_use_t use);
/*
 * Walk the velocity target list for one source voice.
 *
 * Inputs: Scene index, zero-based source slot, current retained byte token,
 * and signed direction. Output: the next legal token in the velocity picker.
 * The picker order is one off entry, the source slot's current instrument
 * descriptors that are valid modulation targets, then the source voice's own
 * Scene Morph target.
 *
 * This must stay separate from the generic descriptor stepper because velocity
 * has one byte token outside descriptor range for own-voice Morph. Menu callers
 * need one stable traversal for encoder and knob edits, and load normalization
 * needs the same validity rule without knowing descriptor or Scene-target
 * internals.
 */
instrument_target_token_t instrumentManager_stepVelocityTargetForSource(
    uint8_t scene_index, uint8_t source_slot, instrument_target_token_t current,
    int8_t direction);
/*
 * Apply a velocity-triggered supplemental or Scene modulation target.
 *
 * Inputs: source_slot is the voice that was triggered, and velocity_0_1 is the
 * normalized trigger velocity already used by velocityModulators[source_slot].
 * Output: direct descriptor targets are ignored because ModulationNode already
 * handled them; installed slot-decimation and Scene targets are converted into
 * owner-specific runtime or retained set operations.
 *
 * This function must stay separate from modNode_updateValue() because velocity
 * Scene targets intentionally update retained PERF/Menu base values, while
 * slot decimation writes through InstrumentManager's supplemental binding and
 * direct descriptor targets are transient pointer writes.
 */
void instrumentManager_applyVelocityModulationTarget(uint8_t source_slot,
                                                     float velocity_0_1);
/*
 * Update every InstrumentManager-owned LFO destination backend for one source
 * pair.
 *
 * Inputs: source slot, target pair, normalized LFO sample, shared polarity, and
 * normalized pair amount. Output: descriptor adapters, slot decimation, and
 * Scene targets receive owner-specific runtime writes. Direct ModulationNode
 * pointers are no longer used for descriptor instrument targets because those
 * targets must be shaped in descriptor space and applied through the normal DSP
 * owner writer rather than through raw runtime pointers.
 */
void instrumentManager_updateLfoAdapters(uint8_t source_slot,
                                         uint8_t target_pair,
                                         float lfo_value_0_1,
                                         uint8_t polarity,
                                         float amount);
/*
 * Dynamic instrument runtime dispatcher.
 *
 * Inputs: logical slot/track numbers from SceneData, MIDI, mixer, and LFO
 * timing. Outputs: the current slot type selects the correct voice engine
 * instance for initialization, trigger, control-rate updates, audio-block
 * rendering, pan lookup, filter refresh, and LFO dispatch/retrigger. These
 * functions exist as a family because folding every case into callers would
 * recreate the fixed Drum/Snare/Cymbal/HiHat slot table in multiple modules.
 *
 * Slots are zero-based storage/render voices 0..5. Trigger tracks are
 * zero-based 0..6, where track 6 is the slot-6 choke/alternate trigger.
 */
void instrumentManager_runtimeInit(void);
/*
 * Clear the complete current modulation target graph before a slot replacement.
 *
 * Inputs: the active Scene's six pre-commit slot identities. Output: both LFO
 * target pairs and the velocity target for every current source are restored,
 * cleared, and removed from InstrumentManager's supplemental-target records.
 * Client: Preset's staged Instrument commit, before SceneData changes type.
 *
 * This cannot be folded into instrumentManager_resetRuntimeSlot(): clearing
 * must resolve source nodes through the outgoing Scene identities, while reset
 * intentionally resolves the incoming identity after commit. Keeping the two
 * phases explicit prevents a type swap from orphaning a dynamic-pool LFO.
 */
void instrumentManager_clearAllRuntimeModulationTargets(void);
/*
 * Quiet test for deferred Scene-slot replacement.
 *
 * Inputs: zero-based instrument slot. Output: nonzero when the slot has no
 * active amp envelope or its envelope value is below the Scene-switch quiet
 * floor. Preset uses this before committing a newly selected Scene's instrument
 * runtime into a slot that may still be ringing from the previous Scene.
 *
 * The threshold is intentionally based on the runtime envelope value rather
 * than the envelope phase alone: SlopeEg2 can remain in EG_D after it has
 * decayed to silence, so a value floor is the stable "basically zero" test.
 */
uint8_t instrumentManager_ampEnvelopeQuiet(uint8_t slot);
/*
 * Reinitialize one committed slot's incoming DSP runtime instance.
 *
 * Input: zero-based slot whose new type is already resident in active
 * SceneData. Output: exactly that type/slot runtime object is returned to its
 * engine defaults before descriptor images are applied. Client: Preset's
 * staged Instrument transaction. Affiliates are the per-type runtime pools and
 * the preserved native Drum/Snare/Cymbal/HiHat globals.
 *
 * This remains separate from instrumentManager_runtimeInit(), which initializes
 * every non-native pool once at boot and must not reset unrelated sounding
 * voices during one Instrument load.
 */
void instrumentManager_resetRuntimeSlot(uint8_t slot);
void instrumentManager_dispatchRuntimeLfos(void);
void instrumentManager_recalcRuntimeLfoSync(void);
void instrumentManager_retriggerRuntimeLfos(uint8_t trigger_track);
void instrumentManager_recalcSlotFilter(uint8_t slot);
void instrumentManager_calcSlotAsync(uint8_t slot);
void instrumentManager_calcSlotSyncBlock(uint8_t slot, int16_t *buf,
                                         uint8_t size);
uint8_t instrumentManager_runtimePan(uint8_t slot);
void instrumentManager_triggerTrack(uint8_t trigger_track, uint8_t note,
                                    uint8_t velocity);
void *instrumentManager_runtimeInstance(uint8_t slot);
uint8_t instrumentManager_writeRuntime(uint8_t slot,
                                       const ParamDescriptor *descriptor,
                                       instrument_param_value_t value);

#endif
