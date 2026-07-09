#include "SnareParameters.h"

/* Canonical Snare keys and their stable local/storage identities. */
#define IMAGE(key_, id_) \
    { id_, key_, 0u, 0u, 0u, 0u, 0u, 127u, true, true, \
      INSTRUMENT_VALUE_PARAMETER_IMAGE, 0xffu, 0xffu, \
      INSTRUMENT_DTYPE_UNSIGNED, { INSTRUMENT_DTYPE_SOURCE_NONE, { { 0, 0 } } } }
#define SUPP(key_, id_, owner_) \
    { id_, key_, 0u, 0u, 0u, 0u, 0u, 127u, false, false, owner_, \
      0xffu, 0xffu, INSTRUMENT_DTYPE_UNSIGNED, \
      { INSTRUMENT_DTYPE_SOURCE_NONE, { { 0, 0 } } } }

const ParamDescriptor snare_param_descriptors[] = {
    IMAGE("osc1_wave", INST_PARAM_OSC1_WAVE),
    IMAGE("osc1_pitch_coarse", INST_PARAM_OSC1_PITCH_COARSE),
    IMAGE("osc1_pitch_fine", INST_PARAM_OSC1_PITCH_FINE),
    IMAGE("noise_freq", INST_PARAM_OSC3_WAVE_OR_NOISE_FREQ),
    IMAGE("osc1_noise_mix", INST_PARAM_OSC3_PITCH_OR_NOISE_MIX),
    IMAGE("filter_freq", INST_PARAM_FILTER_FREQ),
    IMAGE("filter_reso", INST_PARAM_FILTER_RESO),
    IMAGE("filter_drive", INST_PARAM_FILTER_DRIVE),
    IMAGE("filter_type", INST_PARAM_FILTER_TYPE),
    IMAGE("amp_envelope_attack", INST_PARAM_AMP_ENV_ATTACK),
    IMAGE("amp_envelope_decay", INST_PARAM_AMP_ENV_DECAY),
    IMAGE("amp_envelope_slope", INST_PARAM_AMP_ENV_SLOPE),
    IMAGE("amp_attack_repeat", INST_PARAM_AMP_ATTACK_REPEAT),
    IMAGE("pitch_envelope_decay", INST_PARAM_PITCH_ENV_DECAY),
    IMAGE("pitch_envelope_amount", INST_PARAM_PITCH_ENV_AMOUNT),
    IMAGE("pitch_envelope_slope", INST_PARAM_PITCH_ENV_SLOPE),
    IMAGE("instrument_vol", INST_PARAM_INSTRUMENT_VOL),
    IMAGE("instrument_pan", INST_PARAM_INSTRUMENT_PAN),
    IMAGE("instrument_drive", INST_PARAM_INSTRUMENT_DRIVE),
    IMAGE("instrument_decimation", INST_PARAM_INSTRUMENT_DECIMATION),
    IMAGE("lfo_rate", INST_PARAM_LFO_RATE),
    IMAGE("lfo_amount", INST_PARAM_LFO_AMOUNT),
    IMAGE("lfo_wave", INST_PARAM_LFO_WAVE),
    IMAGE("lfo_retrigger_voice", INST_PARAM_LFO_RETRIGGER),
    IMAGE("lfo_sync", INST_PARAM_LFO_SYNC),
    IMAGE("lfo_offset", INST_PARAM_LFO_OFFSET),
    IMAGE("velo_vol_on_off", INST_PARAM_VELO_VOL_ON_OFF),
    SUPP("velo_mod_amount", INST_PARAM_VELO_MOD_AMOUNT,
         INSTRUMENT_VALUE_VELOCITY_AMOUNT),
    SUPP("velo_mod_dest", INST_PARAM_VELO_MOD_DEST,
         INSTRUMENT_VALUE_VELOCITY_TARGET),
    SUPP("lfo_target_voice", INST_PARAM_LFO_TARGET_VOICE,
         INSTRUMENT_VALUE_LFO_TARGET_VOICE),
    SUPP("lfo_target_param", INST_PARAM_LFO_TARGET_PARAM,
         INSTRUMENT_VALUE_LFO_TARGET_PARAM),
    IMAGE("transient_vol", INST_PARAM_TRANSIENT_VOL),
    IMAGE("transient_wave", INST_PARAM_TRANSIENT_WAVE),
    IMAGE("transient_freq", INST_PARAM_TRANSIENT_FREQ),
};

const uint8_t snare_param_descriptor_count =
    (uint8_t)(sizeof(snare_param_descriptors) / sizeof(snare_param_descriptors[0]));

#undef IMAGE
#undef SUPP
