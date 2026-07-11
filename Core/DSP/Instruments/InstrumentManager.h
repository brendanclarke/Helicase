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

typedef struct {
    const char *file_key;
    const char *short_name;
    const char *long_name;
    const char *category;
    uint8_t dtype;
    uint8_t flags;
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
    const char *extension;
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
instrument_type_t instrumentManager_typeFromText(const char *text);
uint8_t instrumentManager_filenameMatchesType(const char *filename,
                                               instrument_type_t type);
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
 * Validate one target for a velocity source voice.
 *
 * Inputs: Scene index, zero-based source slot, and a stored target ID. Output:
 * nonzero only for off, a modulatable descriptor on the source slot, or a
 * Scene mod target. This mixed validator exists because velocity target
 * browsing appends Scene targets after the source voice's descriptor list,
 * while LFO voice-target browsing still uses a selected target voice.
 */
uint8_t instrumentManager_targetValidForVelocitySource(
    uint8_t scene_index, uint8_t source_slot, uint16_t target_id);
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
 * Inputs: Scene index, zero-based source slot, current stored target ID or
 * INSTRUMENT_PARAM_INVALID, and signed direction. Output: the next legal
 * target in the velocity picker. The picker order is one off entry, the source
 * slot's current instrument descriptors that are valid modulation targets,
 * then Scene mod targets.
 *
 * This must stay separate from the generic descriptor stepper because velocity
 * has a mixed namespace: voice-local descriptor IDs plus Scene target IDs.
 * Menu callers need one stable traversal for encoder and knob edits, and load
 * normalization needs the same validity rule without knowing descriptor or
 * Scene-target internals.
 */
uint16_t instrumentManager_stepVelocityTargetForSource(
    uint8_t scene_index, uint8_t source_slot, uint16_t current,
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
 * Apply one LFO sample to an installed supplemental or Scene target.
 *
 * Inputs: source slot, target pair, normalized LFO value, shared polarity, and
 * that pair's amount. Output: direct ModulationNode targets have already been
 * updated by lfo_dispatchNextValue(); this function handles only installed
 * slot-decimation and Scene targets without forcing them into fake pointers.
 */
void instrumentManager_updateLfoSceneTarget(uint8_t source_slot,
                                            uint8_t target_pair,
                                            float lfo_value_0_1,
                                            uint8_t polarity,
                                            float amount);
void *instrumentManager_runtimeInstance(uint8_t slot);
uint8_t instrumentManager_writeRuntime(uint8_t slot,
                                       const ParamDescriptor *descriptor,
                                       uint16_t value);

#endif
