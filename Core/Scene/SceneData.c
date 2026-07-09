#include "SceneData.h"
#include <string.h>

scene_t scenes[SCENE_COUNT];
static uint8_t scene_active_index;

uint8_t scene_indexValid(uint8_t scene_index)
{
    return (uint8_t)(scene_index < SCENE_COUNT);
}

scene_t *scene_get(uint8_t scene_index)
{
    return scene_indexValid(scene_index) ? &scenes[scene_index] : 0;
}

const scene_t *scene_getConst(uint8_t scene_index)
{
    return scene_indexValid(scene_index) ? &scenes[scene_index] : 0;
}

uint8_t scene_getActiveIndex(void)
{
    return scene_active_index;
}

uint8_t scene_selectActive(uint8_t scene_index)
{
    /*
     * Selection changes identity, never data.
     *
     * Input is a resident Scene index; output reports acceptance. DSP apply is
     * deliberately owned by Preset so selecting a record cannot unexpectedly
     * perform a large foreground update.
     */
    if (!scene_indexValid(scene_index))
        return 0u;
    scene_active_index = scene_index;
    return 1u;
}

kit_instrument_slot_t *scene_instrumentSlot(uint8_t scene_index, uint8_t slot)
{
    scene_t *scene = scene_get(scene_index);
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0;
    return &scene->kit.instruments[slot];
}

const kit_instrument_slot_t *scene_instrumentSlotConst(uint8_t scene_index,
                                                       uint8_t slot)
{
    const scene_t *scene = scene_getConst(scene_index);
    if (!scene || slot >= INSTRUMENT_SLOT_COUNT)
        return 0;
    return &scene->kit.instruments[slot];
}

void scene_setTrackMidiChannel(uint8_t scene_index, uint8_t track,
                               uint8_t channel)
{
    scene_t *scene = scene_get(scene_index);
    if (!scene || track >= NUM_TRACKS)
        return;
    if (channel < 1u)
        channel = 1u;
    else if (channel > 16u)
        channel = 16u;
    scene->settings.midi_channel[track] = channel;
}

uint8_t scene_getTrackMidiChannel(uint8_t scene_index, uint8_t track)
{
    const scene_t *scene = scene_getConst(scene_index);
    uint8_t channel;
    if (!scene || track >= NUM_TRACKS)
        return 1u;
    channel = scene->settings.midi_channel[track];
    return (channel >= 1u && channel <= 16u)
        ? channel
        : (uint8_t)(track + 1u);
}

void scene_setTrackMidiNote(uint8_t scene_index, uint8_t track, uint8_t note)
{
    scene_t *scene = scene_get(scene_index);
    if (!scene || track >= NUM_TRACKS)
        return;
    scene->settings.midi_note[track] = (note <= 127u) ? note : 127u;
}

uint8_t scene_getTrackMidiNote(uint8_t scene_index, uint8_t track)
{
    const scene_t *scene = scene_getConst(scene_index);
    if (!scene || track >= NUM_TRACKS)
        return 0u;
    return (scene->settings.midi_note[track] <= 127u)
        ? scene->settings.midi_note[track]
        : 0u;
}

void scene_initAll(void)
{
    uint8_t scene_index;
    uint8_t track;
    static const instrument_type_t initial_types[INSTRUMENT_SLOT_COUNT] = {
        INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM, INSTRUMENT_TYPE_DRM,
        INSTRUMENT_TYPE_SNR, INSTRUMENT_TYPE_CYM, INSTRUMENT_TYPE_HAT
    };

    /*
     * Initialize each complete resident owner.
     *
     * Processing clears stale bytes, establishes safe Scene settings, and
     * resets all six slots through descriptors, then asks PatternData to apply
     * its step/track defaults inside the same Scene record.
     */
    memset(scenes, 0, sizeof(scenes));
    scene_active_index = 0u;
    for (scene_index = 0u; scene_index < SCENE_COUNT; scene_index++) {
        scenes[scene_index].settings.voice_decimation_all = 127u;
        for (track = 0u; track < NUM_TRACKS; track++)
            scenes[scene_index].settings.midi_channel[track] =
                (uint8_t)(track + 1u);
        for (track = 0u; track < INSTRUMENT_SLOT_COUNT; track++)
            instrumentManager_resetSlot(
                &scenes[scene_index].kit.instruments[track],
                initial_types[track]);
        pat_initScene(scene_index);
    }
}
