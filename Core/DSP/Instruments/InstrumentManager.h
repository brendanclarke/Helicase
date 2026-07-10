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
    INSTRUMENT_BIND_LFO_TARGET_PARAM
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

typedef struct {
    instrument_type_t type;
    const char *type_text;
    const char *extension;
    const ParamDescriptor *descriptors;
    uint8_t descriptor_count;
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
struct kit_instrument_slot;
void instrumentManager_resetSlot(struct kit_instrument_slot *slot,
                                 instrument_type_t type);
uint8_t instrumentManager_targetValid(uint8_t scene_index,
                                      instrument_param_id_t id,
                                      instrument_target_use_t use);
void *instrumentManager_runtimeInstance(uint8_t slot);
uint8_t instrumentManager_writeRuntime(uint8_t slot,
                                       const ParamDescriptor *descriptor,
                                       uint16_t value);

#endif
