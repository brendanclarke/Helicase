#include "DrumParameters.h"
#include "DrumVoice.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>

/*
 * Drum parameter source of truth.
 *
 * ParameterArray/SceneData decide that every kit slot has generic storage.
 * This file defines what each storage cell means when the slot contains a drum:
 * file key, menu text, dtype, morph/mod flags, and runtime offset from the
 * slot's DrumVoice instance.
 */
#define FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)
#define BIND(member_, type_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(DrumVoice, member_), type_ }
#define ROW(key_, short_, long_, cat_, dtype_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, BIND(member_, type_) }
#define ROW_MENU(key_, short_, long_, cat_, menu_, member_, type_) \
    ROW(key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), member_, type_)
#define ROW_NOBIND(key_, short_, long_, cat_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, { bind_kind_, 0u, 0u } }

const ParamDescriptor drum_param_descriptors[] = {
    ROW_MENU("osc1_wave", "wav", "Waveform", "Oscilltr", MENU_WAVEFORM, osc.waveform, TYPE_UINT8),
    ROW("osc1_pitch_coarse", "coa", "Coarse", "Oscilltr", DTYPE_0B127, osc.modNodeValue, TYPE_SPECIAL_F),
    ROW("osc1_pitch_fine", "fin", "Fine", "Oscilltr", DTYPE_PM63, osc.modNodeValue, TYPE_SPECIAL_F),
    ROW_MENU("osc2_wave", "wav", "Waveform", "FM", MENU_WAVEFORM, modOsc.waveform, TYPE_UINT8),
    ROW("osc2_pitch_coarse", "frq", "Frequncy", "FM", DTYPE_0B127, modOsc.modNodeValue, TYPE_SPECIAL_F),
    ROW("osc2_mod_amount", "amt", "Amount", "FM", DTYPE_0B127, fmModAmount, TYPE_FLT),
    ROW("osc2_mod_type", "mod", "Mode", "FM", DTYPE_MIX_FM, mixOscs, TYPE_UINT8),
    ROW("filter_freq", "frq", "Frequncy", "Filter", DTYPE_0B127, filter.f, TYPE_FLT),
    ROW("filter_reso", "res", "Resnance", "Filter", DTYPE_0B127, filter.q, TYPE_FLT),
    ROW("filter_drive", "drv", "Overdriv", "Filter", DTYPE_0B127, filter.drive, TYPE_FLT),
    ROW_MENU("filter_type", "typ", "Type", "Filter", MENU_FILTER, filterType, TYPE_UINT8),
    ROW("amp_envelope_attack", "atk", "Attack", "Veloc EG", DTYPE_0B127, oscVolEg.attack, TYPE_FLT),
    ROW("amp_envelope_decay", "dec", "Decay", "Veloc EG", DTYPE_0B127, oscVolEg.decay, TYPE_FLT),
    ROW("amp_envelope_slope", "slp", "Slope", "Veloc EG", DTYPE_0B127, oscVolEg.slope, TYPE_FLT),
    ROW("pitch_envelope_decay", "dec", "Decay", "PitchMod", DTYPE_0B127, oscPitchEg.decay, TYPE_FLT),
    ROW("pitch_envelope_amount", "amt", "Amount", "PitchMod", DTYPE_0B127, egPitchModAmount, TYPE_FLT),
    ROW("pitch_envelope_slope", "slp", "Slope", "PitchMod", DTYPE_0B127, oscPitchEg.slope, TYPE_FLT),
    ROW("instrument_vol", "vol", "Volume", "Voice", DTYPE_0B127, vol, TYPE_FLT),
    ROW("instrument_pan", "pan", "Panning", "Voice", DTYPE_PM63, pan, TYPE_UINT8),
    ROW("instrument_drive", "drv", "Overdriv", "Voice", DTYPE_0B127, distortion.shape, TYPE_FLT),
    ROW_NOBIND("instrument_decimation", "srt", "SampleRt", "Voice", DTYPE_0B127, INSTRUMENT_BIND_SLOT_DECIMATION),
    ROW("lfo_rate", "frq", "Frequncy", "LFO", DTYPE_0B127, lfo.modNodeValue, TYPE_SPECIAL_F),
    ROW("lfo_amount", "amt", "Amount", "LFO", DTYPE_0B127, lfo.modTarget.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", "wav", "Waveform", "LFO", MENU_LFO_WAVES, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", "rtg", "Retriggr", "LFO", MENU_RETRIGGER, lfo.retrigger, TYPE_UINT8),
    ROW_MENU("lfo_sync", "snc", "ClockSnc", "LFO", MENU_SYNC_RATES, lfo.sync, TYPE_UINT8),
    ROW("lfo_offset", "ofs", "Offset", "LFO", DTYPE_0B127, lfo.phaseOffset, TYPE_UINT32),
    ROW("velo_vol_on_off", "vel", "Vol mod", "Velocity", DTYPE_ON_OFF, volumeMod, TYPE_UINT8),
    ROW_NOBIND("velo_mod_amount", "amt", "Amount", "Velocity", DTYPE_0B127, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_NOBIND("velo_mod_dest", "dst", "DstParam", "Velocity", DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    ROW_NOBIND("lfo_target_voice", "voi", "DstVoice", "LFO", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", "dst", "DstParam", "LFO", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_MENU("transient_wave", "wav", "Waveform", "Transnt", MENU_TRANS, transGen.waveform, TYPE_UINT8),
    ROW("transient_vol", "vol", "Volume", "Transnt", DTYPE_0B127, transGen.volume, TYPE_FLT),
    ROW("transient_freq", "frq", "Frequncy", "Transnt", DTYPE_0B127, transGen.pitch, TYPE_FLT),
};

const uint8_t drum_param_descriptor_count =
    (uint8_t)(sizeof(drum_param_descriptors) / sizeof(drum_param_descriptors[0]));

#undef FLAGS_IMAGE
#undef BIND
#undef ROW
#undef ROW_MENU
#undef ROW_NOBIND
