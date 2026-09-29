#ifndef SCENE_DATA_H_
#define SCENE_DATA_H_

#include "InstrumentManager.h"
#include "PatternData.h"
#include "EffectTypes.h"
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
     *
     * Autosave extension rule: every new serialized Kit setting must append a
     * named Kit parameter index/getter and write through SceneData's change-
     * aware Kit store boundary. Direct assignment is reserved for boot
     * initialization or a validated whole-object commit followed by the named
     * Kit region marker. Affiliates: Autosave.h and future Kit copy/load code.
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
     * Deliberately no Bank, Scene, Kit, Instrument, filename, or file-stem
     * metadata is stored in scene_t or kit_t.
     *
     * Why: root `/.hcnames` is authoritative and filesystem keys are derived
     * from its fixed-width rows, slot coordinates, and Instrument extension at
     * the immediate SD call site. Retaining those strings here would create
     * sixteen stale copies and let a selective Bank Load overwrite names for
     * unselected resident Scenes.
     *
     * Inputs/outputs: kit_t carries playable Kit settings and Instrument
     * images only. Name clients must use the filesystem identity/cache APIs.
     *
     * Affiliates: Core/Hardware/SD/filesystem.c HCNAMES helpers and
     * Core/Menu/menu.c's operation-scoped identity session.
     */
} kit_t;

/*
 * S074 master bus compressor settings: one byte each, in wire order.
 *
 * What:       indexes scene_settings_t::bus_comp[] and, in the same order,
 *             AutoSave Scene parameters 41..44, the sceneset.scg keys
 *             bus_comp_mode/amount/time/sidechain, and the PAR_BUS_COMP_*
 *             page mirrors (PAR_BUS_COMP_MODE + field).
 * Domains:    MODE 0 off, 1 St1 (DAC1: MAIN), 2 St2 (DAC2: OUT2); AMOUNT
 *             and TIME 0..127; SIDECHAIN 0 off or voice 1..6 (track 7
 *             counts as 6).
 * Why an array: one clamp table, one default table and one setter keep the
 *             four fields on the same owner path and wire order.
 * Affiliates: BusCompressor.c, Preset, Autosave, storageTypes.c, menu.c.
 */
typedef enum {
    SCENE_BUS_COMP_MODE = 0,
    SCENE_BUS_COMP_AMOUNT,
    SCENE_BUS_COMP_TIME,
    SCENE_BUS_COMP_SIDECHAIN,
    SCENE_BUS_COMP_FIELD_COUNT
} scene_bus_comp_field_t;

#define SCENE_BUS_COMP_MODE_OFF        0u
#define SCENE_BUS_COMP_MODE_ST1        1u
#define SCENE_BUS_COMP_MODE_ST2        2u
#define SCENE_BUS_COMP_SIDECHAIN_OFF   0u
#define SCENE_BUS_COMP_DEFAULT_AMOUNT 48u
#define SCENE_BUS_COMP_DEFAULT_TIME   48u

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
     * contract. FX send is 0..127. Fader mode is 0..SCENE_FADER_SETTING_MAX
     * (0..3): pre (normal/pre-FX), pst (post-FX), fx (FX-only) and xfd (dry
     * to FX crossfade, S074), interpreted by the mixer FX path. Preset
     * setters store the values and the live mixer applies the selected mode.
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
    /*
     * Scene-level Effect Morph amount, 0..255.
     *
     * This is a Scene parameter, not part of the retained Effect record or
     * the `.fx` file. AutoSave stores it as Scene parameter 40; SceneData is
     * the sole writer so the value is always dirty-marked with its owner.
     */
    uint8_t effect_morph_amount;
    /*
     * S074 master bus compressor: cmp, cam, ctm, csc, indexed by
     * scene_bus_comp_field_t.
     *
     * These are Scene settings, not Kit or Effect data: they travel with
     * Scene and Bank save/load, sceneset.scg (bus_comp_* keys) and AutoSave
     * (Scene parameters 41..44). Written through scene_setBusCompSetting()
     * except during validated initialization/whole-Scene staging.
     * +4 B per Scene, +64 B SRAM1 in scenes[16] (approved 2026-09-29).
     */
    uint8_t bus_comp[SCENE_BUS_COMP_FIELD_COUNT];
    /*
     * Autosave extension rule for Scene settings.
     *
     * A future serialized byte is not complete until it has a named index,
     * live getter branch, and SceneData setter using the common change-aware
     * store helper. Direct assignments are limited to initialization or a
     * validated whole-Scene commit followed by the Scene region marker. Why:
     * this keeps getter order and dirty-bit order identical. Affiliates:
     * Autosave's Scene parameter enum and Preset's retained setters.
     */
} scene_settings_t;

typedef struct {
    /*
     * Resident Scene record.
     *
     * settings holds Scene-level performance/settings data and kit holds the
     * embedded six-slot Kit. Pattern data is owned by PatternData.c's static
     * Scene-indexed address/pool/bitmap regions; it is not embedded in
     * scene_t.
     *
     * A Scene display name is intentionally absent. It is card-resident
     * metadata owned by fixed rows 1..16 of root `/.hcnames`, rather than
     * playable Scene state. The former sixteen nine-byte mirrors wasted
     * nonvolatile SRAM and could diverge after a mask-selective Bank Load.
     * Menu now borrows only the one row required by a Scene Load/Save
     * operation; Bank operations borrow the existing general-purpose cache for
     * their complete 129-row transaction.
     *
     * Inputs: filesystem loaders copy validated settings and Kit payload;
     * PatternData owns the separate live Pattern region. Outputs: Menu and Bank name writers use filesystem HCNAMES
     * helpers. Affiliates: filesystem.c and Core/Menu/menu.c.
    */
    scene_settings_t settings;
    /*
     * Scene-retained Effect (Session 072, Effects Phase 5 step 3).
     *
     * The Effect belongs to this Scene, never to its Kit. SceneData setters
     * and whole-record commits own all retained writes; each scalar setter
     * marks its ordered AutoSave Effect cell and whole commits mark the full
     * live Effect region. Readers use scene_effectConst().
     */
    effect_record_t effect;
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
 * Report whether two resident Scenes share one edit layout (plan §7.4).
 *
 * Inputs: two Scene indices. Output: nonzero when both exist, their Effect
 * record types are equal, and all six Kit instrument slot types are equal
 * slot by slot.
 *
 * Why: a VOICE edit-mask fan-out writes the same descriptor or lane index into
 * every masked Scene. That index names the same parameter only when layouts
 * match, so this is the rule behind the selection gate and re-validation
 * (S072 Step 10, A44, F5). The raw Effect type byte is compared; SceneData
 * stays independent of EffectsManager. Clients: Menu and BankData. Read-only;
 * no AutoSave side effect.
 */
uint8_t scene_editLayoutMatches(uint8_t scene_a, uint8_t scene_b);
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
 * Store the two Scene-wide scalar settings through their retained owner.
 *
 * Inputs: resident Scene plus 0..255 Morph or normalized 0..127 decimation.
 * Outputs: changed storage is committed before its named Autosave bit; equal
 * values and invalid Scenes do nothing. Runtime Morph/decimation apply remains
 * Preset-owned. Why: callers must not directly assign these serialized fields.
 * Affiliates: preset_morphScene() and preset_setVoiceDecimationAll().
 */
void scene_setMorphAmount(uint8_t scene_index, uint8_t amount);
void scene_setVoiceDecimationAll(uint8_t scene_index, uint8_t value);
/*
 * Scene-retained per-slot Morph accessors.
 *
 * Inputs use resident Scene index and zero-based instrument slot. Outputs are
 * bounded 0..255 values or no-op/0 for invalid coordinates. Changed setter
 * values also notify their named Autosave cells; identical values do not.
 * Preset uses these to keep per-voice Morph amount ownership in SceneData while
 * the Morph worker remains an apply-only engine.
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
 * All setters commit normalized retained bytes before change-aware Autosave
 * notification; getters and runtime apply never produce dirty work.
 */
void scene_setVoiceAudioOut(uint8_t scene_index, uint8_t slot,
                            uint8_t route);
uint8_t scene_getVoiceAudioOut(uint8_t scene_index, uint8_t slot);
void scene_setVoiceFxSendAmount(uint8_t scene_index, uint8_t slot,
                                uint8_t amount);
uint8_t scene_getVoiceFxSendAmount(uint8_t scene_index, uint8_t slot);
/*
 * Largest stored fader mode (S074): 0 pre, 1 pst, 2 fx, 3 xfd.
 *
 * What: the single domain limit for fader_setting[]. The SceneData setter
 * and getter, Preset's setter, the sceneset.scg parser and the Menu clamp
 * all use it. mixer.c asserts that it equals MIXER_FADER_XFD, so SceneData
 * stays free of mixer includes while the two cannot drift. A future mode
 * raises this value and adds a mixer branch and a Menu label.
 */
#define SCENE_FADER_SETTING_MAX 3u
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
 * these helpers instead of reaching into kit_settings_t directly. Changed
 * final values mark their named Kit autosave cells after storage; equal values
 * do not schedule work.
 */
void scene_setSlot6Track7AmpEnvelopeDecay(uint8_t scene_index, uint8_t value);
uint8_t scene_getSlot6Track7AmpEnvelopeDecay(uint8_t scene_index);
void scene_setSlot6Track7MorphAmpEnvelopeDecay(uint8_t scene_index,
                                               uint8_t value);
uint8_t scene_getSlot6Track7MorphAmpEnvelopeDecay(uint8_t scene_index);

/*
 * Scene Effect accessors (Session 072, Effects Phase 5 step 3).
 *
 * These declarations define the single mutation boundary for the retained
 * Effect record and its Scene-level Morph amount. Setters normalize input,
 * store first, then mark exactly the matching AutoSave cell and invalidate
 * the card-clean bit; equal values and invalid coordinates are no-ops.
 */
const effect_record_t *scene_effectConst(uint8_t scene_index);
void scene_effectRecordDefaults(effect_record_t *record);
uint8_t scene_commitEffectRecord(uint8_t scene_index,
                                 const effect_record_t *record);
/*
 * In-place whole-record commit pair (Session 072, Effects Phase 5 step 4).
 *
 * EffectsManager obtains the mutable retained record, rewrites it, and must
 * immediately close the pair with scene_finishEffectWholeCommit(). The close
 * normalizes sequence fields, marks the type token plus all live Effect cells,
 * and invalidates the Scene card-clean bit without putting a 420-byte copy on
 * the caller's stack. No other writer may use the mutable pointer.
 */
effect_record_t *scene_effectRecordForWholeCommit(uint8_t scene_index);
void scene_finishEffectWholeCommit(uint8_t scene_index);
void scene_setEffectNormalParameter(uint8_t scene_index, uint8_t index,
                                    uint8_t value);
void scene_setEffectMorphParameter(uint8_t scene_index, uint8_t index,
                                   uint8_t value);
void scene_setEffectSeqRunMode(uint8_t scene_index, uint8_t mode);
void scene_setEffectSeqLength(uint8_t scene_index, uint8_t length);
void scene_setEffectSeqStepScale(uint8_t scene_index, uint8_t scale);
void scene_setEffectSeqLaneValue(uint8_t scene_index, uint8_t step,
                                 uint8_t lane, uint8_t value);
void scene_setEffectSeqLaneLocked(uint8_t scene_index, uint8_t step,
                                  uint8_t lane, uint8_t locked);
void scene_setEffectMorphAmount(uint8_t scene_index, uint8_t amount);
uint8_t scene_getEffectMorphAmount(uint8_t scene_index);

/*
 * S074 master bus compressor accessors (cmp, cam, ctm, csc).
 *
 * scene_busCompDefaults() writes off, 48, 48, off into a settings image for
 * every fresh/staged/emptied Scene path. scene_busCompClamp() applies the
 * field domain (mode 0..2, amount/time 0..127, sidechain 0..6), returning 0
 * for an invalid field. scene_setBusCompSetting() clamps and commits through
 * the change-aware Scene store; scene_getBusCompSetting() returns the retained
 * byte or 0 for an invalid Scene/field. No runtime push is performed: the
 * compressor reads the active Scene every block.
 * Affiliates: Preset, Autosave, storageTypes.c, BusCompressor.c, menu.c.
 */
void scene_busCompDefaults(scene_settings_t *settings);
uint8_t scene_busCompClamp(uint8_t field, uint8_t value);
void scene_setBusCompSetting(uint8_t scene_index, uint8_t field,
                             uint8_t value);
uint8_t scene_getBusCompSetting(uint8_t scene_index, uint8_t field);

#endif
