#ifndef INSTRUMENT_MANAGER_H_
#define INSTRUMENT_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>

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
    INSTRUMENT_VALUE_PARAMETER_IMAGE = 0,
    INSTRUMENT_VALUE_VELOCITY_TARGET,
    INSTRUMENT_VALUE_VELOCITY_AMOUNT,
    INSTRUMENT_VALUE_LFO_TARGET_VOICE,
    INSTRUMENT_VALUE_LFO_TARGET_PARAM
} instrument_value_owner_t;

typedef enum {
    INSTRUMENT_TARGET_MODULATION = 0,
    INSTRUMENT_TARGET_AUTOMATION
} instrument_target_use_t;

typedef enum {
    INSTRUMENT_DTYPE_UNSIGNED = 0,
    INSTRUMENT_DTYPE_SIGNED_CENTER,
    INSTRUMENT_DTYPE_ON_OFF,
    INSTRUMENT_DTYPE_STATIC_LIST,
    INSTRUMENT_DTYPE_RUNTIME_LIST,
    INSTRUMENT_DTYPE_LFO_TARGET,
    INSTRUMENT_DTYPE_VELOCITY_TARGET
} instrument_dtype_t;

typedef uint8_t (*instrument_dtype_count_fn)(uint8_t scene_index,
                                              uint8_t slot);
typedef uint8_t (*instrument_dtype_format_fn)(uint8_t scene_index,
                                               uint8_t slot,
                                               uint8_t value,
                                               char out[3]);

typedef enum {
    INSTRUMENT_DTYPE_SOURCE_NONE = 0,
    INSTRUMENT_DTYPE_SOURCE_STATIC,
    INSTRUMENT_DTYPE_SOURCE_RUNTIME
} instrument_dtype_source_kind_t;

typedef struct {
    instrument_dtype_source_kind_t kind;
    union {
        struct {
            const char (*names)[4];
            uint8_t count;
        } static_list;
        struct {
            instrument_dtype_count_fn count;
            instrument_dtype_format_fn format;
        } runtime;
    } source;
} instrument_dtype_source_t;

typedef struct {
    uint8_t local_param;
    const char *file_key;
    uint8_t category;
    uint8_t short_name;
    uint8_t long_name;
    uint8_t default_value;
    uint8_t min_value;
    uint8_t max_value;
    bool is_modulatable;
    bool is_step_automatable;
    instrument_value_owner_t value_owner;
    uint8_t menu_page;
    uint8_t menu_position;
    instrument_dtype_t dtype;
    instrument_dtype_source_t dtype_source;
} ParamDescriptor;

typedef struct {
    instrument_type_t type;
    const char *type_text;
    const char *extension;
    const ParamDescriptor *descriptors;
    uint8_t descriptor_count;
} instrument_registry_entry_t;

/*
 * Stable local IDs are explicit and module-aligned. Gaps are intentional:
 * compact descriptor array position is never a parameter identity.
 */
enum InstrumentLocalParam {
    INST_PARAM_OSC1_WAVE = 0,
    INST_PARAM_OSC1_PITCH_COARSE = 1,
    INST_PARAM_OSC1_PITCH_FINE = 2,
    INST_PARAM_OSC2_WAVE = 3,
    INST_PARAM_OSC2_PITCH_COARSE = 4,
    INST_PARAM_OSC2_MOD_AMOUNT = 5,
    INST_PARAM_OSC3_WAVE_OR_NOISE_FREQ = 6,
    INST_PARAM_OSC3_PITCH_OR_NOISE_MIX = 7,
    INST_PARAM_OSC3_MOD_AMOUNT = 8,
    INST_PARAM_OSC2_MOD_TYPE = 9,
    INST_PARAM_FILTER_FREQ = 12,
    INST_PARAM_FILTER_RESO = 13,
    INST_PARAM_FILTER_DRIVE = 14,
    INST_PARAM_FILTER_TYPE = 15,
    INST_PARAM_AMP_ENV_ATTACK = 16,
    INST_PARAM_AMP_ENV_DECAY = 17,
    INST_PARAM_AMP_ENV_DECAY_OPEN = 18,
    INST_PARAM_AMP_ENV_SLOPE = 19,
    INST_PARAM_AMP_ATTACK_REPEAT = 20,
    INST_PARAM_PITCH_ENV_DECAY = 21,
    INST_PARAM_PITCH_ENV_AMOUNT = 22,
    INST_PARAM_PITCH_ENV_SLOPE = 23,
    INST_PARAM_INSTRUMENT_VOL = 24,
    INST_PARAM_INSTRUMENT_PAN = 25,
    INST_PARAM_INSTRUMENT_DRIVE = 26,
    INST_PARAM_INSTRUMENT_DECIMATION = 27,
    INST_PARAM_LFO_RATE = 32,
    INST_PARAM_LFO_AMOUNT = 33,
    INST_PARAM_LFO_WAVE = 34,
    INST_PARAM_LFO_RETRIGGER = 35,
    INST_PARAM_LFO_SYNC = 36,
    INST_PARAM_LFO_OFFSET = 37,
    INST_PARAM_VELO_VOL_ON_OFF = 40,
    INST_PARAM_VELO_MOD_AMOUNT = 41,
    INST_PARAM_VELO_MOD_DEST = 42,
    INST_PARAM_LFO_TARGET_VOICE = 43,
    INST_PARAM_LFO_TARGET_PARAM = 44,
    INST_PARAM_TRANSIENT_VOL = 48,
    INST_PARAM_TRANSIENT_WAVE = 49,
    INST_PARAM_TRANSIENT_FREQ = 50
};

instrument_param_id_t instrumentParam_make(uint8_t slot, uint8_t local_param);
uint8_t instrumentParam_isVoiceParameter(instrument_param_id_t id);
uint8_t instrumentParam_slot(instrument_param_id_t id);
uint8_t instrumentParam_local(instrument_param_id_t id);

const instrument_registry_entry_t *instrumentManager_registryEntry(
    instrument_type_t type);
instrument_type_t instrumentManager_typeFromText(const char *text);
uint8_t instrumentManager_filenameMatchesType(const char *filename,
                                               instrument_type_t type);
const ParamDescriptor *instrumentManager_descriptor(instrument_type_t type,
                                                     uint8_t local_param);
const ParamDescriptor *instrumentManager_descriptorByKey(instrument_type_t type,
                                                          const char *file_key);
const ParamDescriptor *instrumentManager_menuDescriptor(instrument_type_t type,
                                                         uint8_t page,
                                                         uint8_t position);
struct kit_instrument_slot;
void instrumentManager_resetSlot(struct kit_instrument_slot *slot,
                                 instrument_type_t type);
uint8_t instrumentManager_targetValid(uint8_t scene_index,
                                      instrument_param_id_t id,
                                      instrument_target_use_t use);

#endif
