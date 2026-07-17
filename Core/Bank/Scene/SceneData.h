#ifndef SCENE_DATA_H_
#define SCENE_DATA_H_

#include "InstrumentManager.h"
#include "PatternData.h"
#include <stdint.h>

/*
 * Resident Scene ownership.
 *
 * Why: sound endpoints, Pattern data, kit membership/settings, and Scene
 * settings must travel together. Sixteen records are resident so the Bank
 * workspace can hold one Scene per physical SEQ button and write inactive
 * Scenes during edit-mask fan-out. Clients should use bounded accessors rather
 * than indexing scenes[] directly.
 *
 * Accessing code: BankData stores 16-bit Scene masks where bit N addresses
 * scenes[N]. Filesystem Bank Load/Save iterates this count while mapping
 * Bank-local `SS Name` child folders to matching resident Scene slots. Menu
 * and ButtonHandler use the same bound when SEQ buttons select, toggle, or
 * display Scene membership.
 */
#define SCENE_COUNT 16u
/*
 * Fixed-width resident object display names.
 *
 * Kit, Scene, and Instrument browser/editor names use the LCD's eight visible
 * character cells plus a local NUL terminator for C helpers. This constant
 * keeps object identity metadata in SceneData without depending on the SD
 * storage parser's naming constants.
 */
#define SCENE_OBJECT_DISPLAY_NAME_LEN 8u
/*
 * Retained instrument source stem length.
 *
 * This is Scene-owned rather than storageTypes-owned so SceneData can retain
 * save metadata without including the SD parser header. The first 16 filename
 * stem characters survive Kit/Instrument load and later drive Kit Save member
 * filename generation; the LCD-facing display name remains eight characters.
 */
#define SCENE_INSTRUMENT_STEM_LEN 16u

typedef struct {
    /*
     * Descriptor-indexed instrument endpoint images for one kit slot.
     *
     * instrument_parameters[] is the main endpoint loaded from `[params]` and
     * edited in normal VOICE mode. morph_instrument_parameters[] is the Morph
     * endpoint loaded from `[morph]` and edited through SHIFT+VOICE.
     * morph_interpolation[] is the runtime byte image produced by the Morph
     * worker. Descriptor rows that select modulation destinations store compact
     * byte tokens; canonical target IDs are expanded only by InstrumentManager
     * when runtime targets are installed or displayed.
     */
    instrument_param_value_t instrument_parameters[INSTRUMENT_PARAM_COUNT];
    instrument_param_value_t morph_instrument_parameters[INSTRUMENT_PARAM_COUNT];
    instrument_param_value_t morph_interpolation[INSTRUMENT_PARAM_COUNT];
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
     * slot6_track7_amp_envelope_decay and its Morph mirror are generated
     * kit-owned endpoint values for the shared slot-6/track-7 voice pair. They
     * are used only when slot 6 hosts a non-Choke instrument with a base
     * amp_envelope_decay descriptor; Choke instruments use real `_choke`
     * descriptors inside their instrument file instead.
     *
     * Per-voice audio routing used to live here but is now Scene-owned in
     * scene_settings_t. Routing is a performance/mix setting that must survive
     * root Kit swaps, while these generated decay endpoints depend on the
     * current kit voice layout and therefore remain Kit-owned.
     */
    uint8_t slot6_track7_amp_envelope_decay;
    uint8_t slot6_track7_morph_amp_envelope_decay;
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
    /*
     * Retained Kit display name.
     *
     * What: Eight printable characters plus NUL naming the resident Kit for
     * Save editor seeding. This is storage/UI metadata, not a DSP parameter and
     * not a filesystem short alias.
     *
     * Why: Slot browsing displays on-card Kit/NNN names, while the resident
     * Kit still needs a stable internal identity for display and future
     * embedded use. Keeping that identity in SceneData prevents an empty slot
     * display from being mistaken for the Kit's internal name.
     *
     * Inputs: normal Kit Load. Morph Load must not write this field. Output
     * clients: resident display and future Scene embedded Kit naming.
     */
    char display_name[SCENE_OBJECT_DISPLAY_NAME_LEN + 1u];
    /*
     * Instrument source names are retained separately for display and save.
     *
     * instrument_display_name is the eight-character LCD field. instrument_stem
     * keeps the first 16 filename stem characters loaded from Kit/Instrument
     * files so a later Kit Save can regenerate useful member filenames. Neither
     * field is a DSP parameter, and neither is editable from the UI yet.
     */
    char instrument_display_name[INSTRUMENT_SLOT_COUNT][9];
    char instrument_stem[INSTRUMENT_SLOT_COUNT][SCENE_INSTRUMENT_STEM_LEN + 1u];
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
     * Per-voice Scene mix settings.
     *
     * audio_out is retained per instrument slot in the current mixer route
     * domain 0..5. Preset clamps it against the DSP mixer constants before
     * writing mixer_audioRouting[], keeping SceneData free of mixer includes.
     *
     * fx_send_amount and fader_setting are retained now for the Scene file/UI
     * contract. FX send is 0..127. Fader mode is 0..2, currently interpreted
     * as normal/pre-FX, post-FX, and FX-only by future mixer/FX work. Until
     * that backend exists, Preset setters store the values and intentionally
     * no-op runtime apply.
     *
     * These fields are indexed by instrument slot, not by track. Track 7
     * continues to share slot 6's voice/mix identity.
     */
    uint8_t audio_out[INSTRUMENT_SLOT_COUNT];
    uint8_t fx_send_amount[INSTRUMENT_SLOT_COUNT];
    uint8_t fader_setting[INSTRUMENT_SLOT_COUNT];
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
     * display_name is the resident Scene's own storage/UI identity. settings
     * holds Scene-level performance/settings data, pattern holds the current
     * bridge PatternSet, and kit holds the embedded six-slot Kit. The struct is
     * the unit that future Bank loading will multiply to sixteen playable
     * Scenes plus a staging/landing Scene.
     */
    /*
     * Retained Scene display name.
     *
     * What: Eight printable characters plus NUL naming the resident Scene for
     * Save editor seeding and future Bank/Scene lists. It is never emitted as
     * a self-name inside sceneset.scg.
     *
     * Why: Root Scene slot display is on-card library state. The resident Scene
     * needs its own name so runtime identity does not derive from the browser
     * sentinel for an empty or differently named slot.
     *
     * Inputs: normal Scene Load. Morph operations must not write this field.
     * Output clients: resident display and future Bank Scene lists.
     */
    char display_name[SCENE_OBJECT_DISPLAY_NAME_LEN + 1u];
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
 * Retain one Kit display name in Kit-owned storage.
 *
 * What: Copies exactly eight display cells into kit->display_name, sanitizing
 * non-printable bytes to spaces and appending NUL.
 *
 * Why: Kit name is resident data, separate from a root Kit library slot's
 * current folder name. Normal Kit Save updates this field only after the save
 * succeeds; Morph operations leave it alone.
 *
 * Inputs: caller-owned Kit pointer and fixed-width eight-character display
 * field. Outputs: kit->display_name when kit is non-NULL.
 *
 * Affiliates/clients: filesystem normal Kit Load/Save completion, Menu Save
 * editor seeding, Scene embedded Kit naming.
 */
void scene_setKitDisplayName(kit_t *kit,
                             const char name[SCENE_OBJECT_DISPLAY_NAME_LEN]);
void scene_setResidentKitDisplayName(
    uint8_t scene_index,
    const char name[SCENE_OBJECT_DISPLAY_NAME_LEN]);
/*
 * Retain one resident Scene display name.
 *
 * What: Copies exactly eight display cells into the selected resident Scene,
 * sanitizing non-printable bytes to spaces and appending NUL.
 *
 * Why: the resident Scene needs an internal name independent of the root Scene
 * slot currently highlighted by the browser. This also gives future Bank work
 * a Scene-local name field instead of overloading preset_currentName.
 *
 * Inputs: resident Scene index and fixed-width display field. Outputs:
 * scenes[index].display_name when the index is valid.
 *
 * Affiliates/clients: filesystem Scene Load/Save, Menu Save editor seeding,
 * future Bank Scene lists.
 */
void scene_setSceneDisplayName(
    uint8_t scene_index,
    const char name[SCENE_OBJECT_DISPLAY_NAME_LEN]);
const char *scene_kitDisplayName(uint8_t scene_index);
const char *scene_sceneDisplayName(uint8_t scene_index);
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
 * Retain one instrument source stem for later Kit Save.
 *
 * Inputs may be a filename with extension or a raw stem. Output updates both
 * the 16-character save stem and the eight-character LCD display name. Central
 * ownership avoids Kit load, Instrument load, and future Scene/Bank load
 * deriving subtly different names from the same file.
 */
void scene_setInstrumentSourceName(uint8_t scene_index, uint8_t slot,
                                   const char *filename_or_stem);
void scene_setKitInstrumentSourceName(kit_t *kit, uint8_t slot,
                                      const char *filename_or_stem);
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
/*
 * Scene-retained per-voice mix setting accessors.
 *
 * Inputs use resident Scene index plus zero-based instrument slot. Setters are
 * no-ops for invalid coordinates and clamp retained values to their storage
 * domains. Getters return safe defaults so callers can display/apply without
 * copying SceneData's layout or reimplementing slot bounds.
 *
 * Clients/affiliates: storageTypes sceneset parsing, Menu VOICE mix Scene
 * setting cells, Preset runtime apply, and future Scene Save/Bank copy code.
 */
void scene_setVoiceAudioOut(uint8_t scene_index, uint8_t slot,
                            uint8_t route);
uint8_t scene_getVoiceAudioOut(uint8_t scene_index, uint8_t slot);
void scene_setVoiceFxSendAmount(uint8_t scene_index, uint8_t slot,
                                uint8_t amount);
uint8_t scene_getVoiceFxSendAmount(uint8_t scene_index, uint8_t slot);
void scene_setVoiceFaderSetting(uint8_t scene_index, uint8_t slot,
                                uint8_t mode);
uint8_t scene_getVoiceFaderSetting(uint8_t scene_index, uint8_t slot);
/*
 * Kit-owned generated track-7 decay accessors.
 *
 * Inputs: resident Scene index plus 0..127 endpoint value for setters. Outputs
 * are retained main/morph values, or 0 for invalid scenes. These are specific
 * rather than a generic kit-setting accessor because the generated parameter
 * has a fixed behavioral contract: slot 6, track 7, amp envelope decay,
 * non-Choke fallback. Menu, Preset, storage, and future Scene mod targets use
 * these helpers instead of reaching into kit_settings_t directly.
 */
void scene_setSlot6Track7AmpEnvelopeDecay(uint8_t scene_index, uint8_t value);
uint8_t scene_getSlot6Track7AmpEnvelopeDecay(uint8_t scene_index);
void scene_setSlot6Track7MorphAmpEnvelopeDecay(uint8_t scene_index,
                                               uint8_t value);
uint8_t scene_getSlot6Track7MorphAmpEnvelopeDecay(uint8_t scene_index);

#endif
