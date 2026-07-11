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
    /*
     * Descriptor-indexed instrument endpoint images for one kit slot.
     *
     * instrument_parameters[] is the main endpoint loaded from `[params]` and
     * edited in normal VOICE mode. morph_instrument_parameters[] is the Morph
     * endpoint loaded from `[morph]` and edited through SHIFT+VOICE. The Morph
     * worker writes morph_interpolation[] as runtime-derived state. Inputs to
     * these arrays are descriptor indices local to the slot's current
     * instrument type; callers must use InstrumentManager to interpret them.
     * Output values are 16-bit so supplemental target IDs and future wider
     * value domains can be stored without narrowing at the Scene boundary.
     */
    uint16_t instrument_parameters[INSTRUMENT_PARAM_COUNT];
    uint16_t morph_instrument_parameters[INSTRUMENT_PARAM_COUNT];
    uint16_t morph_interpolation[INSTRUMENT_PARAM_COUNT];
} instrument_parameter_images_t;

typedef struct kit_instrument_slot {
    /*
     * One swappable instrument slot inside a Kit.
     *
     * type selects which descriptor table gives meaning to the generic image
     * arrays. parameter_images stores the descriptor-indexed data for that
     * type. This struct deliberately does not store the instrument filename or
     * kit name: those are file/container metadata owned by kitset.kcg and the
     * folder name, not runtime Scene state.
     */
    instrument_type_t type;
    instrument_parameter_images_t parameter_images;
} kit_instrument_slot_t;

typedef struct {
    /*
     * Kit-level settings that are not instrument parameter images.
     *
     * audio_out[] is indexed by instrument slot and comes from kitset.kcg.
     * It remains Kit-owned because routing follows a Kit voice assignment, not
     * an individual instrument file and not Scene-wide performance settings.
     */
    uint8_t audio_out[INSTRUMENT_SLOT_COUNT];
} kit_settings_t;

typedef struct {
    /*
     * Complete Kit payload embedded in a Scene.
     *
     * settings owns per-slot kit settings such as audio routing. instruments[]
     * owns the six swappable instrument slots and their descriptor images. Kit
     * loading replaces this structure inside the active Scene; future Scene and
     * Bank loading will embed/copy the same shape.
     */
    kit_settings_t settings;
    kit_instrument_slot_t instruments[INSTRUMENT_SLOT_COUNT];
} kit_t;

typedef struct {
    /*
     * Scene-level global Morph amount, 0..255.
     *
     * This is the visible PERF "mrp" value and the last bulk-set amount. Runtime
     * Morph is applied from voice_morph_amount[] below; setting this field
     * through Preset also writes all six per-voice values.
     */
    uint8_t morph_amount;
    /*
     * Per-voice Morph amounts, 0..255.
     *
     * These are Scene settings, not Kit or instrument-file data. The six values
     * select how far each swappable instrument slot is interpolated between its
     * main endpoint image and morph endpoint image. The overall PERF Morph
     * field remains as the visible bulk-set value, but runtime Morph is always
     * applied from these per-slot amounts.
     *
     * Clients: PERF per-voice Morph controls, MIDI CC1 on a voice channel,
     * preset_morph() bulk-set, preset_setInstrumentParameter() endpoint refresh,
     * and future sceneset.scg load/save. The values are indexed by slot, not by
     * instrument type, so changing the instrument in a slot does not require a
     * hardcoded parameter map.
     */
    uint8_t voice_morph_amount[INSTRUMENT_SLOT_COUNT];
    /*
     * Scene-level global sample-rate decimation, 0..127.
     *
     * This is PERF `srt`, stored once per Scene. It is intentionally separate
     * from voice-local instrument_decimation descriptor rows, which are stored
     * inside each instrument slot's descriptor images.
     */
    uint8_t voice_decimation_all;
    /*
     * Per-track MIDI assignment settings retained with the Scene.
     *
     * The current bridge stores one MIDI channel and note per sequencer track.
     * Channels are 1..16 in accessors; a future MIDI cleanup may add 0 as an
     * off sentinel. These fields belong to Scene settings, not kitset.kcg or
     * instrument files, because changing a Kit should not rewrite track MIDI
     * input identity.
     */
    uint8_t midi_channel[NUM_TRACKS];
    uint8_t midi_note[NUM_TRACKS];
} scene_settings_t;

typedef struct {
    /*
     * Resident Scene record.
     *
     * settings holds Scene-level performance/settings data, pattern holds the
     * current bridge PatternSet, and kit holds the embedded six-slot Kit. The
     * struct is the unit that future Bank loading will multiply to sixteen
     * playable Scenes plus a staging/landing Scene.
     */
    scene_settings_t settings;
    PatternSet pattern;
    kit_t kit;
} scene_t;

extern scene_t scenes[SCENE_COUNT];

/*
 * Initialize every resident Scene record.
 *
 * Inputs: none. Output: scenes[] is cleared, the active Scene index is reset,
 * safe default Scene settings are established, each kit slot is reset through
 * InstrumentManager, and PatternData initializes per-Scene pattern defaults.
 * This is called at boot before filesystem-loaded Kit data is applied.
 */
void scene_initAll(void);
/*
 * Validate a resident Scene index.
 *
 * Input: candidate Scene index. Output: nonzero when it is within the current
 * SCENE_COUNT allocation. Bank work will increase SCENE_COUNT; callers should
 * use this helper rather than hardcoding the current single-Scene bridge.
 */
uint8_t scene_indexValid(uint8_t scene_index);
/*
 * Borrow a mutable Scene record.
 *
 * Input: resident Scene index. Output: pointer to the Scene or NULL for an
 * invalid index. Mutating callers are Preset, filesystem apply, PatternData,
 * and future Bank/Scene loaders; DSP code should prefer owner APIs rather than
 * editing Scene storage directly.
 */
scene_t *scene_get(uint8_t scene_index);
/*
 * Borrow a read-only Scene record.
 *
 * Input: resident Scene index. Output: const pointer to the Scene or NULL for
 * invalid index. Clients use this for display, validation, and runtime apply
 * decisions that must not mutate retained Scene data.
 */
const scene_t *scene_getConst(uint8_t scene_index);
/*
 * Read the active Scene index.
 *
 * Inputs: none. Output: current resident Scene index selected for menu/runtime
 * apply. With SCENE_COUNT=1 this returns 0, but keeping the accessor preserves
 * the future Bank scene-switch boundary.
 */
uint8_t scene_getActiveIndex(void);
/*
 * Select the active resident Scene record.
 *
 * Input: resident Scene index. Output: nonzero on success. This changes the
 * identity only; Preset owns any runtime DSP apply so selecting a record cannot
 * unexpectedly perform a large foreground update.
 */
uint8_t scene_selectActive(uint8_t scene_index);
/*
 * Borrow a mutable instrument slot from a Scene's embedded Kit.
 *
 * Inputs: Scene index and zero-based instrument slot. Output: pointer to the
 * slot or NULL for invalid coordinates. Filesystem load and Preset setters use
 * this to write descriptor images while keeping slot bounds centralized.
 */
kit_instrument_slot_t *scene_instrumentSlot(uint8_t scene_index, uint8_t slot);
/*
 * Borrow a read-only instrument slot from a Scene's embedded Kit.
 *
 * Inputs: Scene index and zero-based instrument slot. Output: const pointer to
 * the slot or NULL for invalid coordinates. Menu, InstrumentManager, and target
 * browsers use this to resolve the slot's current instrument type and images.
 */
const kit_instrument_slot_t *scene_instrumentSlotConst(uint8_t scene_index,
                                                       uint8_t slot);
/*
 * Store one track's MIDI channel setting.
 *
 * Inputs: Scene index, track index, and requested channel. Output: the channel
 * is clamped to the current 1..16 bridge domain and stored when coordinates
 * are valid. Future MIDI rework may extend this with an off sentinel.
 */
void scene_setTrackMidiChannel(uint8_t scene_index, uint8_t track,
                               uint8_t channel);
/*
 * Read one track's MIDI channel setting.
 *
 * Inputs: Scene index and track index. Output: stored valid channel, or the
 * track+1 fallback for unset/stale values, or 1 for invalid coordinates. This
 * keeps legacy boot defaults stable while storage moves under SceneData.
 */
uint8_t scene_getTrackMidiChannel(uint8_t scene_index, uint8_t track);
/*
 * Store one track's MIDI note setting.
 *
 * Inputs: Scene index, track index, and note. Output: valid coordinates store a
 * 0..127 note, clamping out-of-range input to 127. The setting belongs to Scene
 * track configuration rather than instrument files.
 */
void scene_setTrackMidiNote(uint8_t scene_index, uint8_t track, uint8_t note);
/*
 * Read one track's MIDI note setting.
 *
 * Inputs: Scene index and track index. Output: stored 0..127 note, or 0 for
 * invalid/stale data. MIDI and menu code use this rather than reading the
 * settings array directly.
 */
uint8_t scene_getTrackMidiNote(uint8_t scene_index, uint8_t track);
/*
 * Scene-retained per-slot Morph accessors.
 *
 * Inputs use resident Scene index and zero-based instrument slot. Outputs are
 * bounded 0..255 values or no-op/0 for invalid coordinates. Preset uses these
 * to keep per-voice Morph amount ownership in SceneData while the Morph worker
 * remains an apply-only engine.
 */
void scene_setVoiceMorphAmount(uint8_t scene_index, uint8_t slot,
                               uint8_t amount);
uint8_t scene_getVoiceMorphAmount(uint8_t scene_index, uint8_t slot);
void scene_setAllVoiceMorphAmounts(uint8_t scene_index, uint8_t amount);

#endif
