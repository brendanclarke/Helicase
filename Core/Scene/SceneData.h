#ifndef SCENE_DATA_H_
#define SCENE_DATA_H_

#include "InstrumentManager.h"
#include "PatternData.h"
#include <stdint.h>

/*
 * Resident Scene ownership.
 *
 * Why: sound endpoints, Pattern data, kit membership/settings, and Scene
 * settings must travel together. One record is allocated now; the same API is
 * intentionally indexed so the Bank implementation can raise SCENE_COUNT to
 * 17 without reintroducing globals. Clients should use bounded accessors rather
 * than indexing scenes[] directly.
 */
#define SCENE_COUNT 1u

typedef struct {
    uint16_t instrument_parameters[INSTRUMENT_PARAM_COUNT];
    uint16_t morph_instrument_parameters[INSTRUMENT_PARAM_COUNT];
    uint16_t morph_interpolation[INSTRUMENT_PARAM_COUNT];
} instrument_parameter_images_t;

typedef struct kit_instrument_slot {
    instrument_type_t type;
    instrument_parameter_images_t parameter_images;
} kit_instrument_slot_t;

typedef struct {
    uint8_t audio_out[INSTRUMENT_SLOT_COUNT];
} kit_settings_t;

typedef struct {
    kit_settings_t settings;
    kit_instrument_slot_t instruments[INSTRUMENT_SLOT_COUNT];
} kit_t;

typedef struct {
    uint8_t morph_amount;
    uint8_t voice_decimation_all;
    uint8_t midi_channel[NUM_TRACKS];
    uint8_t midi_note[NUM_TRACKS];
} scene_settings_t;

typedef struct {
    scene_settings_t settings;
    PatternSet pattern;
    kit_t kit;
} scene_t;

extern scene_t scenes[SCENE_COUNT];

void scene_initAll(void);
uint8_t scene_indexValid(uint8_t scene_index);
scene_t *scene_get(uint8_t scene_index);
const scene_t *scene_getConst(uint8_t scene_index);
uint8_t scene_getActiveIndex(void);
uint8_t scene_selectActive(uint8_t scene_index);
kit_instrument_slot_t *scene_instrumentSlot(uint8_t scene_index, uint8_t slot);
const kit_instrument_slot_t *scene_instrumentSlotConst(uint8_t scene_index,
                                                       uint8_t slot);
void scene_setTrackMidiChannel(uint8_t scene_index, uint8_t track,
                               uint8_t channel);
uint8_t scene_getTrackMidiChannel(uint8_t scene_index, uint8_t track);
void scene_setTrackMidiNote(uint8_t scene_index, uint8_t track, uint8_t note);
uint8_t scene_getTrackMidiNote(uint8_t scene_index, uint8_t track);

#endif
