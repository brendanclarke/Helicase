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
/*
 * Byte-sized retained instrument parameter values.
 *
 * Instrument files, SceneData endpoint images, Morph interpolation, and
 * descriptor-backed menu edits all store one byte per descriptor cell. Normal
 * musical parameters use their existing 0..127 or 0..255 UI domain. Target
 * selector cells store compact local tokens instead of packed canonical target
 * IDs so the same arrays never need to widen for routing metadata.
 */
typedef uint8_t instrument_param_value_t;
/*
 * Retained modulation target token.
 *
 * Target parameter selector rows store compact byte tokens. 0xff is the sole
 * off value. Values 0..63 address local descriptor indices in the selected
 * namespace. The velocity destination row also reserves 0x40 for the source
 * voice's own Scene Morph target, preserving the LXR-02 velocity-to-Morph
 * workflow without storing a wide Scene target ID. Runtime code expands the
 * byte token into a canonical instrument_param_id_t only while installing or
 * displaying a modulation target.
 */
typedef uint8_t instrument_target_token_t;
#define INSTRUMENT_TARGET_TOKEN_OFF          0xffu
#define INSTRUMENT_TARGET_TOKEN_MAX_LOCAL    ((uint8_t)(INSTRUMENT_PARAM_COUNT - 1u))
#define INSTRUMENT_TARGET_TOKEN_VOICE_MORPH  0x40u
/*
 * LFO target namespace values stored in lfo_target_voice cells.
 *
 * Values 1..6 select instrument voices. Value 7 selects the Scene namespace
 * shown by Menu as `scn`. Value 8 selects the active Scene's Effect namespace
 * shown as `fx`; lfo_target_param is then an Effect-local byte token.
 */
#define INSTRUMENT_TARGET_VOICE_FIRST 1u
#define INSTRUMENT_TARGET_VOICE_LAST  INSTRUMENT_SLOT_COUNT
#define INSTRUMENT_TARGET_VOICE_SCENE ((uint8_t)(INSTRUMENT_SLOT_COUNT + 1u))
#define INSTRUMENT_TARGET_VOICE_EFFECT ((uint8_t)(INSTRUMENT_SLOT_COUNT + 2u))
#define INSTRUMENT_TARGET_VOICE_NAMESPACE_LAST INSTRUMENT_TARGET_VOICE_EFFECT

typedef enum {
    INSTRUMENT_TYPE_DRM = 0,
    INSTRUMENT_TYPE_SNR,
    INSTRUMENT_TYPE_CYM,
    INSTRUMENT_TYPE_HAT,
    /*
     * S093 P4: the Alkaline (TB-303) type, before UNKNOWN.
     *
     * What:       value 4; UNKNOWN moves from 4 to 5.
     * Why:        UNKNOWN is the count of real types: every type-sized array
     *             (menu_instrumentLoadIndex[], menu.c l. 1728: +2 B, R1) and
     *             every `type >= INSTRUMENT_TYPE_UNKNOWN` guard grow with it.
     *             A new type goes here, before UNKNOWN, with a registry row
     *             at the same index (InstrumentManager.c static assert and
     *             check_special_tags.py registry_check()).
     * Stored data: no stored value changes meaning. Types are stored as text
     *             tokens (AutoSave, .hcnames, instrument files, midilrn.cfg);
     *             the numeric value lives only in RAM. The .hcnames header
     *             lists the tokens in enum order; a header written before a
     *             type was appended is a leading part of the current list
     *             and is accepted (filesystem_validateHcnamesHeader()), so
     *             the register and AutoSave survive the addition. A
     *             rejected header would regenerate /.hcnames with every row
     *             flagged R and discard the AutoSave payload (S093 P4 field
     *             report). New types are appended only.
     */
    INSTRUMENT_TYPE_ALK,
    /*
     * S095 P2: the Colloid type (token "col"), before UNKNOWN.
     *
     * What:       value 5; UNKNOWN moves from 5 to 6.
     * Why:        appended (the standing rule, as the Alkaline in S093 P4):
     *             no stored value changes meaning; every type-sized array
     *             (menu_instrumentLoadIndex[], menu.c: +2 B .bss) and every
     *             `type >= INSTRUMENT_TYPE_UNKNOWN` guard follow; the
     *             .hcnames header written before the addition is a leading
     *             part of the new list and is accepted.
     */
    INSTRUMENT_TYPE_COL,
    /*
     * S096 P1: the Tetrakt type (token "tkt"), before UNKNOWN.
     *
     * What:       value 6; UNKNOWN moves from 6 to 7.
     * Why:        appended (the standing rule, as the Colloid in S095 P2): no
     *             stored value changes meaning; menu_instrumentLoadIndex[]
     *             (menu.c: +2 B .bss) and every `type >= UNKNOWN` guard
     *             follow; a .hcnames header written before the addition is
     *             a leading part of the new list and is accepted.
     */
    INSTRUMENT_TYPE_TKT,
    /*
     * S099 P1: the Advanced Colloid (token "clx"), before UNKNOWN.
     *
     * What:       value 7; UNKNOWN moves from 7 to 8. The type runs the
     *             Colloid engine (ColloidVoice, the same union member) and the
     *             BBD chorus (BbdChorus.h) in one FxBuffer voice unit while
     *             its cho row is not off; its registry row points at the
     *             Colloid's descriptor table with 64 rows (the Colloid's 59,
     *             then the five chorus rows).
     * Why:        appended (the standing rule): no stored value changes
     *             meaning; menu_instrumentLoadIndex[] (menu.c: +2 B .bss) and
     *             every `type >= UNKNOWN` guard follow; a .hcnames header
     *             written before the addition is a leading part of the new
     *             list and is accepted. Engine-level code tests the engine
     *             (instrumentManager_typeEngine(): clx -> col), not the type.
     */
    INSTRUMENT_TYPE_CLX,
    /*
     * S099 P1 (P-E): the Advanced Alkaline (token "alx"), before UNKNOWN.
     *
     * What:       value 8; UNKNOWN moves from 8 to 9. The type runs the
     *             Alkaline engine (AlkalineVoice) and the BBD chorus in its
     *             mono-in form (the Alpha Juno's own topology) with a 12 dB
     *             wet, a BBD drive into a hard clip, a tone and a width; while
     *             the chorus runs the slot is stereo (instrumentManager_
     *             slotRight() returns the voice's right block).
     * Why:        appended, as clx; menu_instrumentLoadIndex[] +2 B .bss.
     */
    INSTRUMENT_TYPE_ALX,
    /*
     * S103 P1: Katran, the drone / noise synth (token "kat"), before UNKNOWN.
     *
     * What:       value 9; UNKNOWN moves from 9 to 10. Its own engine
     *             (KatranVoice, a union member of its own); Basic.
     * Why:        appended (the standing rule): no stored value changes
     *             meaning; menu_instrumentLoadIndex[] (menu.c: +2 B .bss,
     *             signed off) and every type >= UNKNOWN guard follow; a
     *             .hcnames header written before the addition is a leading
     *             part of the new list and is accepted.
     */
    INSTRUMENT_TYPE_KAT,
    INSTRUMENT_TYPE_UNKNOWN
} instrument_type_t;

/*
 * Runtime LFO-node visitor contract.
 *
 * InstrumentManager owns the tagged engine storage while ModulationNode owns
 * operations on its target records.  Forward declaring the record keeps that
 * boundary narrow: clients can synchronously visit the two nodes belonging to
 * each live slot LFO without learning any engine layout or retaining a pointer
 * across a Scene/type replacement.
 */
struct ModulatorStruct;
typedef void (*instrument_runtime_lfo_node_visitor_t)(
    struct ModulatorStruct *primary, struct ModulatorStruct *secondary,
    void *context);

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

/*
 * Descriptor special-writer tags (S073 Step 2).
 *
 * What:       names the fixed DSP setter associated with a descriptor row;
 *             bits 5-6 select its oscillator where applicable.
 * Why:        replaces repeated file_key string searches in the LFO, Morph,
 *             velocity and Scene activation paths with a flash-stored tag.
 * Inputs:     compile-time table constants.
 * Outputs:    instrument_runtime_binding_t.special values.
 * Accessors:  the four instrument parameter tables and the runtime writer.
 * Affiliates: tools/dsp_test/check_special_tags.py and the diagnostic
 *             self-check; Effect rows explicitly use IM_SPECIAL_NONE.
 */
typedef enum {
    IM_SPECIAL_NONE = 0u,
    IM_SPECIAL_NOISE_FREQ,
    IM_SPECIAL_PITCH_COARSE,
    IM_SPECIAL_PITCH_FINE,
    IM_SPECIAL_FILTER_FREQ,
    IM_SPECIAL_FILTER_RESO,
    IM_SPECIAL_FILTER_DRIVE,
    IM_SPECIAL_FILTER_TYPE,
    IM_SPECIAL_AMP_ATTACK,
    IM_SPECIAL_AMP_DECAY,
    IM_SPECIAL_HAT_DECAY_CHOKE,
    IM_SPECIAL_AMP_SLOPE,
    IM_SPECIAL_PITCH_EG_DECAY,
    IM_SPECIAL_PITCH_EG_SLOPE,
    IM_SPECIAL_PITCH_EG_AMOUNT,
    IM_SPECIAL_TRANSIENT_WAVE,
    IM_SPECIAL_TRANSIENT_FREQ,
    IM_SPECIAL_INSTRUMENT_DRIVE,
    IM_SPECIAL_LFO_RATE,
    /*
     * S076 P2: lfo_offset writer. Scales the 0..127 descriptor byte onto the
     * full 32-bit phase accumulator range before writing lfo.phaseOffset,
     * instead of the raw TYPE_UINT32 store that made 127 effectively zero.
     */
    IM_SPECIAL_LFO_OFFSET,
    /*
     * S087 B: osc1_type writer. Calls osc_setType() on the tagged oscillator,
     * then refreshes the wave rows' modulation domains for the slot
     * (instrumentManager_waveDomainChanged()).
     */
    IM_SPECIAL_OSC_TYPE,
    /*
     * S088 P0: osc1_run writer (run mode). Clamps the value to 0..2
     * (OSC_RUN_RST..OSC_RUN_ITP) and stores it in the engine's runMode
     * field through the descriptor's instance offset. No other side
     * effects: the engine trigger and render functions read runMode at
     * their own pace, so the mode takes effect on the next trigger.
     *
     * What:       IM_SPECIAL_OSC_RUN = 22 (next after IM_SPECIAL_OSC_TYPE).
     * Why:        run mode is a selector, not a continuous parameter. It must
     *             pass through the special writer so menu edits, Kit/Scene/
     *             Instrument apply, step automation and MIDI writes all clamp
     *             and store identically. Corrupt storage or an out-of-range
     *             MIDI write becomes itp (2).
     * Inputs:     slot, descriptor tag (IM_SPECIAL_OSC1), byte value.
     * Outputs:    the engine struct's runMode field.
     * Accessors:  instrumentManager_writeSpecialRuntime() switch.
     * Affiliates: OSC_RUN_* (Oscillator.h); oscRunNames (MenuText.h); the four
     *             osc1_run rows; the engine trigger/render functions.
     */
    IM_SPECIAL_OSC_RUN,
    /*
     * S089 P2: voice_chain writer. Clamps the value to 0..VOICE_CHAIN_COUNT-1
     * and stores it in the engine's chainType field through the descriptor's
     * instance offset. On a change of chain it clears transGen.midiFreq so
     * the fold's depth ramp starts flat (voiceFold_process()).
     *
     * What:       IM_SPECIAL_VOICE_CHAIN = 22 (next after IM_SPECIAL_OSC_RUN,
     *             21); IM_SPECIAL_WRITER_COUNT becomes 23 (<= 32, the 5-bit
     *             writer field).
     * Why:        a selector must pass through the special writer so menu
     *             edits, Kit/Scene/Instrument apply, step automation and MIDI
     *             writes all clamp and store identically.
     * Inputs:     slot, byte value.  Outputs: chainType; transGen.midiFreq.
     * Accessors:  instrumentManager_writeSpecialRuntime() switch.
     * Affiliates: VOICE_CHAIN_* (voiceFold.h); voiceChainNames (MenuText.h);
     *             the four voice_chain rows; tools/dsp_test/check_special_tags.py.
    */
    IM_SPECIAL_VOICE_CHAIN,
    /*
     * S094 P1 S1: lfo_sync writer.
     *
     * What:       IM_SPECIAL_LFO_SYNC = 23 (next after IM_SPECIAL_VOICE_CHAIN,
     *             22); IM_SPECIAL_WRITER_COUNT becomes 24 (<= 32, the 5-bit
     *             writer field, asserted below). The writer calls
     *             lfo_setSync(), which stores the byte and recomputes the
     *             LFO's phase step.
     * Why:        the plain ROW_MENU store left phaseInc at the previous
     *             mode's rate until a frq or tempo change (S094 bug report),
     *             and a Kit/Scene/Instrument load applied lfo_rate (an earlier
     *             row) with the previous sound's sync byte.
     * Inputs:     slot, byte value (0..11; MENU_SYNC_RATES).
     * Outputs:    the slot LFO's sync and phaseInc.
     * Accessors:  instrumentManager_writeSpecialRuntime() switch.
     * Affiliates: lfo_setSync() (lfo.c); the five lfo_sync rows;
     *             tools/dsp_test/check_special_tags.py; the diagnostic
     *             classifier.
     */
    IM_SPECIAL_LFO_SYNC,
    /*
     * S099 P1: the Advanced types' effect-type writer (S099 P2: the fx row,
     *             Effect Type, was the chorus's cho row).
     *
     * What:       IM_SPECIAL_FX_TYPE = 24 (next after IM_SPECIAL_LFO_SYNC,
     *             23); IM_SPECIAL_WRITER_COUNT becomes 25 (<= 32, the 5-bit
     *             writer field, asserted below). The writer clamps the byte to
     *             ADV_FX_COUNT - 1, stores it in the voice, makes the slot
     *             own one FxBuffer voice unit while it is not off (none while
     *             off), binds and clears the unit on a first bind, and
     *             refreshes the slot's right-block pointer.
     * Why:        the brief: the effect's 50 ms unit is held only while its
     *             type is selected. The row is a selector that is neither
     *             automatable, morphable nor a modulation target, so every
     *             write comes from the foreground (FxBuffer's share callback
     *             is foreground-only).
     * Inputs:     slot, byte value (ADV_FX_*; larger values become the last).
     * Outputs:    the voice's fxType and choUnit; the slot's units.
     * Accessors:  instrumentManager_writeSpecialRuntime() switch.
     * Affiliates: ADV_FX_* (below); the fx_type rows;
     *             tools/dsp_test/check_special_tags.py; the diagnostic
     *             classifier.
     */
    IM_SPECIAL_FX_TYPE,
    IM_SPECIAL_WRITER_COUNT
} instrument_special_writer_t;

/*
 * The Advanced types' effect (S099 P2, user 2026-11-19).
 *
 * What:       the fx row (Effect Type, the first cell of the Advanced MIX
 *             page's last screen: fx dpt rte mix): ADV_FX_OFF 0 (no unit, no
 *             effect work), ADV_FX_CHORUS 1 (chr, the BBD chorus,
 *             BbdChorus.h). dpt (Effect Depth), rte (Effect Rate) and mix
 *             (Effect Mix) are the effect's three common rows; the chorus
 *             reads dpt as its mode and depth (BBD_DPT_*).
 * Why:        more effects may follow: each takes the next value and gives
 *             the three rows its own meaning, and the screen stays the same
 *             four cells on every Advanced type.
 * Accessors:  instrumentManager_writeFxType(), instrumentManager_unitsWanted(),
 *             the clx / alx async and sync cases; Colloid/AlkalineParameters.c
 *             (the rows, the inactive cells); MenuText.h fxTypeNames.
 */
#define ADV_FX_OFF        0u
#define ADV_FX_CHORUS     1u
#define ADV_FX_COUNT      2u

#define IM_SPECIAL_WRITER_MASK  0x1Fu
#define IM_SPECIAL_OSC_MASK     0x60u
#define IM_SPECIAL_OSC1         0x00u
#define IM_SPECIAL_OSC2         0x20u
#define IM_SPECIAL_OSC3         0x40u
#define IM_SPECIAL_OSC_NOISE    0x60u
_Static_assert(IM_SPECIAL_WRITER_COUNT <= (IM_SPECIAL_WRITER_MASK + 1u),
               "special writer IDs must fit bits 0-4");

typedef struct {
    instrument_binding_kind_t kind;
    uint16_t offset;
    uint8_t parameter_type;
    /*
     * Special DSP writer tag (S073 Step 2).
     *
     * What:       IM_SPECIAL_* writer ID | IM_SPECIAL_OSC_* selector, or 0
     *             for generic offset writes.
     * Why:        stores the fixed row-to-setter mapping once and avoids the
     *             runtime key-string search.
     * Inputs:     BIND_SPECIAL() in the instrument tables.
     * Outputs:    read by instrumentManager_writeSpecialRuntime().
     * Accessors:  InstrumentManager.c.
     * Affiliates: the layout guards below and EffectParamRows.h.
     */
    uint8_t special;
} instrument_runtime_binding_t;

#define INSTRUMENT_PARAM_FLAG_MORPHABLE       0x01u
#define INSTRUMENT_PARAM_FLAG_MODULATABLE     0x02u
#define INSTRUMENT_PARAM_FLAG_AUTOMATABLE     0x04u
/*
 * Selector descriptor flag (S087 stage B).
 *
 * What:       0x08 marks a row that chooses what an oscillator is (osc1_type)
 *             rather than tuning it. Such a row is never morphable (its
 *             in-between values are other choices), is stored single-ended
 *             (Normal image only), is applied before the Morph image on every
 *             voice apply, and may be step-automated as one value per step.
 * Why:        S087 P0 §12.5: the automation gate admitted only morphable rows,
 *             and Morph interpolates every morphable row linearly; a separate
 *             flag lets typ be automated without being morphed.
 * Inputs:     the ROW_SELECT_SPECIAL rows.  Outputs: flag bit.
 * Accessors:  instrumentManager_targetValid(), preset_setSupplementalParameter-
 *             Internal(), preset_applyKitVoiceSelectors(),
 *             preset_applyKitVoiceSupplementalInternal().
 * Affiliates: INSTRUMENT_PARAM_FLAG_MORPHABLE/AUTOMATABLE; seq_rampDtypeOk()
 *             (ramps are excluded by dtype, not by this flag).
 */
#define INSTRUMENT_PARAM_FLAG_SELECTOR        0x08u

#define INSTRUMENT_FLAG_BASIC                 0x01u
#define INSTRUMENT_FLAG_ADVANCED              0x02u
#define INSTRUMENT_FLAG_CHOKE                 0x04u

#define INSTRUMENT_MOD_DOMAIN_NONE            0x00u
#define INSTRUMENT_MOD_DOMAIN_CONTINUOUS      0x01u
#define INSTRUMENT_MOD_DOMAIN_INTEGER         0x02u
#define INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX     0x04u

typedef struct {
    instrument_param_value_t min_value;
    instrument_param_value_t max_value;
    uint8_t flags;
} instrument_mod_domain_t;

typedef struct {
    const char *file_key;
    /*
     * Display labels as indexes into the MenuText.h pools (S097 P1 item 2):
     * menuLabel_short(short_id), menuLabel_long(long_id),
     * menuLabel_cat(cat_id); ids are enum shortNamesEnum / longNamesEnum /
     * catNamesEnum (menu.h). file_key stays the storage key.
     */
    uint8_t short_id;
    uint8_t long_id;
    uint8_t cat_id;
    uint8_t dtype;
    uint8_t flags;
    /*
     * Parameter-domain modulation contract.
     *
     * Inputs: instrument parameter files declare this beside each descriptor
     * row. Output: InstrumentManager uses it to decide whether transient LFO
     * overlays may target the row and which descriptor-space value range is
     * legal. The domain deliberately describes the value before owner-specific
     * DSP shaping: envelope, pitch, filter, transient, distortion, and LFO-rate
     * conversion still lives in instrumentManager_writeRuntime() and the DSP
     * owner setters. This prevents ModulationNode from guessing from C scalar
     * type or learning a hardcoded parameter list.
     */
    instrument_mod_domain_t mod_domain;
    instrument_runtime_binding_t runtime;
} ParamDescriptor;

/*
 * Descriptor layout guards (S073 Step 2).
 *
 * What:       pins the binding and descriptor sizes so the new tag consumes
 *             existing padding rather than growing every flash table.
 * Why:        a silent table-size increase would invalidate the S0/flash
 *             budget claim.
 * Inputs:     compiler ABI layout.
 * Outputs:    compile-time errors when the layout changes.
 * Accessors:  the compiler.
 * Affiliates: instrument_runtime_binding_t.special.
 */
_Static_assert(sizeof(instrument_runtime_binding_t) == 6u,
               "instrument_runtime_binding_t must stay 6 bytes");
_Static_assert(sizeof(ParamDescriptor) == 20u,
               "ParamDescriptor must stay 20 bytes (S097 P1 item 2: 28 -> 20)");

#define INSTRUMENT_MENU_EMPTY 0xffu
#define INSTRUMENT_MENU_SKIP  0xfeu
/*
 * Bit cells of a page (S096 P1 SD1).
 *
 * What:       a page cell code 0xF0 + b (b 0..3) is "bit b of the type's
 *             switch row" (the registry row's switch_row_plus1).
 *             instrumentManager_menuDescriptorIndex() resolves it to the
 *             switch row's descriptor and instrumentManager_menuCellBitPlus1()
 *             returns b; the menu shows and edits that one bit as an on/off
 *             cell and commits the whole row.
 * Why:        the Tetrakt's four layer switches are one 4-bit row (plan
 *             §4.3, user R4) shown as four controls. Descriptor indices are
 *             0..63 and 0xFE / 0xFF are SKIP / EMPTY, so 0xF0..0xF3 are
 *             free.
 * Accessors:  TetraktParameters.c (tetrakt_menu_pages[]); menu.c
 *             menu_resolveCellAbsolute().
 */
#define INSTRUMENT_MENU_BIT_FIRST  0xf0u
#define INSTRUMENT_MENU_BIT_COUNT  4u
#define INSTRUMENT_MENU_BIT(b)     ((uint8_t)(INSTRUMENT_MENU_BIT_FIRST + (b)))
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
    const char *display_label;
    const char *extension;
    /*
     * Storage directory owned by this registry row.
     *
     * Inputs: none; this is immutable product metadata. Output: the exact
     * display component used below /Instrument/ for this instrument type.
     * Filesystem clients use instrumentManager_storageDirectory() instead of
     * duplicating Drum/Snare/Cymbal/HiHat folder names in storage state
     * machines. Keeping the directory beside the extension makes type
     * classification and storage navigation change together.
     */
    const char *storage_directory;
    uint8_t type_flags;
    const ParamDescriptor *descriptors;
    uint8_t descriptor_count;
    const instrument_menu_page_t *menu_pages;
    uint8_t menu_page_count;
    /*
     * The switch row behind the page's bit cells (S096 P1 SD1).
     *
     * What:       the descriptor index + 1 of the row that
     *             INSTRUMENT_MENU_BIT(b) cells edit; 0 = the type has no
     *             bit cells.
     * Why:        +1 so every positional registry row written before this
     *             field (zero-initialised) means "none".
     */
    uint8_t switch_row_plus1;
} instrument_registry_entry_t;

instrument_param_id_t instrumentParam_make(uint8_t slot, uint8_t descriptor_index);
uint8_t instrumentParam_isVoiceParameter(instrument_param_id_t id);
uint8_t instrumentParam_slot(instrument_param_id_t id);
uint8_t instrumentParam_local(instrument_param_id_t id);

const instrument_registry_entry_t *instrumentManager_registryEntry(
    instrument_type_t type);
uint8_t instrumentManager_registryCount(void);
const instrument_registry_entry_t *instrumentManager_registryEntryAt(
    uint8_t index);
instrument_type_t instrumentManager_typeFromText(const char *text);
const char *instrumentManager_typeDisplayLabel(instrument_type_t type);
uint8_t instrumentManager_typeFlags(instrument_type_t type);
uint8_t instrumentManager_advancedCountForScene(uint8_t scene_index,
                                                uint8_t ignore_slot);
uint8_t instrumentManager_typeSelectableForSceneSlot(
    uint8_t scene_index, uint8_t destination_slot, instrument_type_t candidate);
uint8_t instrumentManager_filenameMatchesType(const char *filename,
                                               instrument_type_t type);
/*
 * Return the registry-owned Instrument storage directory.
 *
 * Inputs: one registered instrument type. Output: the immutable directory
 * component below /Instrument/, or NULL for INSTRUMENT_TYPE_UNKNOWN. Clients:
 * filesystem boot index creation, per-type index loading, Instrument scan,
 * and Instrument Save. This accessor is the only cross-module path to the
 * registry's folder metadata, so filesystem.c cannot grow a second hardcoded
 * type-to-directory table.
 */
const char *instrumentManager_storageDirectory(instrument_type_t type);
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
/*
 * The bit of a page cell (S096 P1 SD1).
 *
 * What:       b + 1 when the cell is INSTRUMENT_MENU_BIT(b) on a type with a
 *             switch row; 0 for every other cell.
 * Inputs:     type, page (sub-page), position (0..15).
 * Accessors:  menu.c menu_resolveCellAbsolute().
 */
uint8_t instrumentManager_menuCellBitPlus1(instrument_type_t type, uint8_t page,
                                           uint8_t position);
const ParamDescriptor *instrumentManager_voicePageDescriptorIndex(
    instrument_type_t type, uint8_t voice_page, uint8_t page, uint8_t position,
    uint8_t *index_out);
uint8_t instrumentManager_chokeDescriptorIndexForBase(
    instrument_type_t type, uint8_t base_index, uint8_t *choke_index_out);
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
 * Convert between retained target tokens and canonical runtime target IDs.
 *
 * Retained storage keeps only byte-sized target tokens. Runtime modulation,
 * automation, and display helpers still need canonical IDs because installed
 * nodes share one target namespace. These helpers are the only place that may
 * combine a target slot, Scene namespace, or source-voice Morph token with a
 * local byte token to build a wider id.
 */
uint8_t instrumentManager_targetTokenValidForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t token,
    instrument_target_use_t use);
instrument_param_id_t instrumentManager_targetIdFromTokenForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t token,
    instrument_target_use_t use);
instrument_target_token_t instrumentManager_targetTokenFromIdForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_param_id_t id,
    instrument_target_use_t use);
instrument_target_token_t instrumentManager_stepTargetTokenForSlot(
    uint8_t scene_index,
    uint8_t target_slot,
    instrument_target_token_t current,
    int8_t direction,
    instrument_target_use_t use);
uint8_t instrumentManager_lfoTargetVoiceValid(uint8_t voice);
instrument_param_id_t instrumentManager_lfoTargetIdFromToken(
    uint8_t scene_index,
    uint8_t source_slot,
    uint8_t target_voice,
    instrument_target_token_t token,
    instrument_target_use_t use);
instrument_target_token_t instrumentManager_lfoTargetTokenFromId(
    uint8_t scene_index,
    uint8_t target_voice,
    instrument_param_id_t id,
    instrument_target_use_t use);
instrument_target_token_t instrumentManager_stepLfoTargetToken(
    uint8_t scene_index,
    uint8_t target_voice,
    instrument_target_token_t current,
    int8_t direction,
    instrument_target_use_t use);
uint8_t instrumentManager_targetValidForVelocitySource(
    uint8_t scene_index, uint8_t source_slot, instrument_target_token_t token);
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
 * Inputs: Scene index, zero-based source slot, current retained byte token,
 * and signed direction. Output: the next legal token in the velocity picker.
 * The picker order is one off entry, the source slot's current instrument
 * descriptors that are valid modulation targets, then the source voice's own
 * Scene Morph target.
 *
 * This must stay separate from the generic descriptor stepper because velocity
 * has one byte token outside descriptor range for own-voice Morph. Menu callers
 * need one stable traversal for encoder and knob edits, and load normalization
 * needs the same validity rule without knowing descriptor or Scene-target
 * internals.
 */
instrument_target_token_t instrumentManager_stepVelocityTargetForSource(
    uint8_t scene_index, uint8_t source_slot, instrument_target_token_t current,
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
 * Update every InstrumentManager-owned LFO destination backend for one source
 * pair.
 *
 * Inputs: source slot, target pair, normalized LFO sample, shared polarity, and
 * normalized pair amount. Output: descriptor adapters, slot decimation, and
 * Scene targets receive owner-specific runtime writes. Direct ModulationNode
 * pointers are no longer used for descriptor instrument targets because those
 * targets must be shaped in descriptor space and applied through the normal DSP
 * owner writer rather than through raw runtime pointers.
 */
void instrumentManager_updateLfoAdapters(uint8_t source_slot,
                                         uint8_t target_pair,
                                         float lfo_value_0_1,
                                         uint8_t polarity,
                                         float amount);
/*
 * Set/clear the generated slot-6 track-7 decay step overlay.
 *
 * Inputs: a seven-bit per-step decay value. Output: the alternate trigger
 * path uses it without retaining Kit settings; clearing restores priority to
 * any LFO overlay, then to the retained Kit value. Sequencer owns the call
 * boundary and clears it at transport/Pattern restore.
 */
void instrumentManager_setSlot6Track7StepDecayOverride(uint8_t layer, uint8_t value);
void instrumentManager_clearSlot6Track7StepDecayLayer(uint8_t layer);
void instrumentManager_clearSlot6Track7StepDecayOverride(void);
/* S104 P1: the track-7 decay row mapping (contract in InstrumentManager.c). */
uint8_t instrumentManager_track7DecayIndex(instrument_type_t type, uint8_t layer,
                                           uint8_t *index_out);
uint8_t instrumentManager_track7DecayLayerOf(instrument_type_t type, uint8_t local);
uint8_t instrumentManager_track7DecayLayer(uint8_t scene_index,
                                           instrument_param_id_t id);
uint64_t instrumentManager_track7DecayLocalMask(uint8_t scene_index);
/*
 * LFO phase Scene-change handoff (S076 P2).
 *
 * What:       captureLfoPhases() snapshots the running phase of every slot's
 *             LFO before the deferred Scene worker starts.
 *             restoreLfoPhaseIfNeeded(slot) checks the incoming LFO's
 *             retrigger field after descriptor values are applied and decides:
 *             scn retrigger -> phase = phaseOffset (reset to start);
 *             otherwise -> phase = captured snapshot (continue cycle).
 * Why:        instrumentManager_resetRuntimeSlot() unconditionally zeroes the
 *             LFO phase via memset + lfo_init. Without capture/restore, the
 *             running phase is always lost on Scene change.
 * Inputs:     captureLfoPhases() reads from the current runtime LFOs.
 *             restoreLfoPhaseIfNeeded(slot) reads the handoff struct and the
 *             incoming LFO's retrigger field (already written by descriptor
 *             apply).
 * Outputs:    captureLfoPhases() fills the static handoff struct
 *             (28 bytes: 6 x uint32_t phase + 1 uint8_t valid, padded).
 *             restoreLfoPhaseIfNeeded(slot) writes lfo->phase.
 * Callers:    preset_startDrumsetApply() (capture),
 *             preset_resetAndApplyKitVoiceImage() (restore).
 * Affiliates: instrumentManager_resetRuntimeSlot() (the reset that destroys
 *             the phase), presetMorph_applyVoiceNow() (descriptor apply that
 *             writes retrigger before restore is called).
 */
void instrumentManager_captureLfoPhases(void);
void instrumentManager_restoreLfoPhaseIfNeeded(uint8_t slot);
/*
 * Capture one slot's LFO phase before a single-voice apply (S077 P2 §2.3).
 *
 * Inputs: slot 0..5. Output: the slot's running phase is snapshotted into the
 * shared handoff struct and marked valid, so
 * instrumentManager_restoreLfoPhaseIfNeeded() can continue the cycle after the
 * slot reset. Client: preset_startSingleVoiceApply(). Affiliate:
 * instrumentManager_captureLfoPhases() (the all-slot variant).
 */
void instrumentManager_captureLfoPhaseForSlot(uint8_t slot);
/*
 * Dynamic instrument runtime dispatcher.
 *
 * Inputs: logical slot/track numbers from SceneData, MIDI, mixer, and LFO
 * timing. Outputs: the current slot type selects the correct voice engine
 * instance for initialization, trigger, control-rate updates, audio-block
 * rendering, pan lookup, filter refresh, and LFO dispatch/retrigger. These
 * functions exist as a family because folding every case into callers would
 * recreate the fixed Drum/Snare/Cymbal/HiHat slot table in multiple modules.
 *
 * Slots are zero-based storage/render voices 0..5. Trigger tracks are
 * zero-based 0..6, where track 6 is the slot-6 choke/alternate trigger.
 */
void instrumentManager_runtimeInit(void);
/*
 * Clear the complete current modulation target graph before a slot replacement.
 *
 * Inputs: the active Scene's six pre-commit slot identities. Output: both LFO
 * target pairs and the velocity target for every current source are restored,
 * cleared, and removed from InstrumentManager's supplemental-target records.
 * Client: Preset's staged Instrument commit, before SceneData changes type.
 *
 * This cannot be folded into instrumentManager_resetRuntimeSlot(): clearing
 * must resolve source nodes through the outgoing Scene identities, while reset
 * intentionally resolves the incoming identity after commit. Keeping the two
 * phases explicit prevents a type swap from orphaning a tagged-slot LFO.
 */
void instrumentManager_clearAllRuntimeModulationTargets(void);
/*
 * Clear runtime modulation targets for one slot only (S077 P2 §2.2).
 *
 * Inputs: slot 0..5. Output: this source slot's two LFO pair destinations and
 * velocity source are detached, leaving every other slot's modulation graph
 * live. Client: preset_startSingleVoiceApply(). Affiliate:
 * instrumentManager_clearAllRuntimeModulationTargets() (the all-slot variant).
 */
void instrumentManager_clearRuntimeModulationTargetsForSlot(uint8_t slot);
/*
 * Prepare the modulation graph for a Scene switch that changes some slots
 * (S098 P1).
 *
 * Inputs: changed_slots (bit s = slot s is replaced), effect_changed.
 * Output: the bindings the change makes stale are detached; returns the
 * source mask the Scene worker's rebind must revisit (the changed slots plus
 * every kept source whose binding points into one of them, or into the
 * changed Effect). Kept sources with bindings inside kept slots are left
 * running. Client: preset_startSceneSwitchApply().
 */
uint8_t instrumentManager_prepareModulationForSlotChange(uint8_t changed_slots,
                                                         uint8_t effect_changed);
/*
 * Quiet test for deferred Scene-slot replacement.
 *
 * Inputs: zero-based instrument slot. Output: nonzero when the slot has no
 * active amp envelope or its envelope value is below the Scene-switch quiet
 * floor. Preset uses this before committing a newly selected Scene's instrument
 * runtime into a slot that may still be ringing from the previous Scene.
 *
 * The threshold is intentionally based on the runtime envelope value rather
 * than the envelope phase alone: SlopeEg2 can remain in EG_D after it has
 * decayed to silence, so a value floor is the stable "basically zero" test.
 */
uint8_t instrumentManager_ampEnvelopeQuiet(uint8_t slot);
/*
 * Reinitialize one committed slot's incoming DSP runtime instance.
 *
 * Input: zero-based slot whose new type is already resident in active
 * SceneData. Output: exactly that type/slot runtime object is returned to its
 * engine defaults before descriptor images are applied. Client: Preset's
 * staged Instrument transaction. The manager clears the complete tagged slot,
 * installs the incoming runtime type, then initializes its one live engine
 * member before Preset rebuilds descriptor and modulation state.
 *
 * This remains separate from instrumentManager_runtimeInit(), which initializes
 * every boot-resident tagged slot once and must not reset unrelated sounding
 * voices during one Instrument load.
 */
void instrumentManager_resetRuntimeSlot(uint8_t slot);
/*
 * Reinitialize one slot's runtime from a caller-selected Scene (S077 P2 §2.1).
 *
 * Inputs: slot 0..5 and a resident Scene index. Output: that slot's tagged
 * runtime member is reset to the Scene's type and engine defaults. Why: the
 * single-voice per-track apply commits a slot whose played Scene may differ
 * from the active Scene. Affiliate: instrumentManager_resetRuntimeSlot()
 * (the active-Scene wrapper).
 */
void instrumentManager_resetRuntimeSlotFromScene(uint8_t slot,
                                                 uint8_t scene_index);
/*
 * S101 P2 A1: dispatch every slot LFO, stepping its phase by `steps` blocks
 * (1 or 2: the staggered control runs every other block, so each run steps
 * the blocks it owes and the rate stays exact); clears the dirty sources.
 * instrumentManager_reapplyDirtyLfos(): on the block between, re-apply at
 * its last value (no phase step) each source LFO whose descriptor target an
 * ordinary write refreshed since its last dispatch, in slot order.
 * Accessors: modNode_controlBlock().
 */
void instrumentManager_dispatchRuntimeLfos(uint8_t steps);
void instrumentManager_reapplyDirtyLfos(void);
void instrumentManager_recalcRuntimeLfoSync(void);
void instrumentManager_retriggerRuntimeLfos(uint8_t trigger_track);
void instrumentManager_recalcSlotFilter(uint8_t slot);
/*
 * itp crossfade slot queries (S088 P0 §3 Layer 3b).
 *
 * What:       instrumentManager_slotHasActiveItpFade() reports whether the
 *             given render slot's current engine instance holds an active itp
 *             carrier crossfade (snapshot.active);
 *             instrumentManager_activeItpFadeCount() counts them over all six
 *             slots (mixer block-start reserve);
 *             instrumentManager_itpFadeSlotAvailable(exclude) reports 1 when a
 *             new fade may start (the fades held by other slots are fewer than
 *             OSC_WAVE_INTERP_MAX_ACTIVE). exclude is the triggering voice's
 *             own slot, because re-snapshotting an existing fade reuses its
 *             slot instead of taking another.
 * Why:        the two interpolation slots are shared with wavetable frame
 *             blending (S087 E); ongoing itp fades are pre-claimed at the
 *             block start so modNode_interpClaim() cannot hand their slot to
 *             a wavetable blend. Claim order per block: ongoing itp, then wtb,
 *             then new itp. A refused itp trigger falls back to fre.
 * Inputs:     slot (0..5), exclude slot (0..5).
 * Outputs:    0/1, or the fade count 0..OSC_WAVE_INTERP_MAX_ACTIVE.
 * Accessors:  mixer_calcNextSampleBlock() (count), the four engine triggers
 *             (available).
 * Affiliates: OscCarrierSnapshot (Oscillator.h); modNode_setItpReserved()
 *             (modulationNode.h); OSC_WAVE_INTERP_MAX_ACTIVE (config.h).
 */
uint8_t instrumentManager_slotHasActiveItpFade(uint8_t slot);
uint8_t instrumentManager_activeItpFadeCount(void);
uint8_t instrumentManager_itpFadeSlotAvailable(uint8_t exclude_slot);

/*
 * Refresh the modulation domains of a slot's wave rows (S087 stage B).
 *
 * What:       for every active LFO adapter on `slot` whose descriptor has
 *             INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX, recompute adapter->domain
 *             with instrumentManager_descriptorModDomain(slot, ...) and clamp
 *             base_value into it; for a direct ModulationNode target on the
 *             same id, call modNode_directOriginalValueChanged() with the new
 *             range.
 * Why:        a typ change (or an install, C-31) changes how many entries the
 *             wave row has; an LFO set up for 8 stock waves must not keep
 *             sweeping 0..7 over a 3-sample list, nor stop short of a
 *             100-sample list.
 * Inputs:     slot 0..INSTRUMENT_SLOT_COUNT-1.  Outputs: refreshed adapter
 *             domains and ranges.
 * Accessors:  IM_SPECIAL_OSC_TYPE writer (B-8); the post-install refresh
 *             (C-31).
 * Affiliates: instrumentManager_updateLfoAdapters(),
 *             instrumentManager_resolveModulationTarget().
 */
void instrumentManager_waveDomainChanged(uint8_t slot);

/*
 * Number of legal values of an oscillator wave row (S087 stage B).
 *
 * What:       instrumentManager_waveDomainMax(slot, descriptor) + 1, i.e. the
 *             menu's entry count for osc1_wave under its current type or for a
 *             modulator's flat list.
 * Why:        menu_clampCellValue() and the typ-switch clamp need the count
 *             without owning the descriptor-domain rule.
 * Inputs:     slot (INSTRUMENT_SLOT_COUNT = unknown), descriptor.
 * Outputs:    count, at least 1.
 * Accessors:  menu.c (cell clamp, typ-switch clamp).
 * Affiliates: instrumentManager_waveDomainMax().
 */
uint8_t instrumentManager_waveCellCount(uint8_t slot,
                                        const ParamDescriptor *descriptor);

/*
 * osc1's current type for a slot (S087 stage D).
 *
 * What:       returns the runtime osc1 OSC_TYPE_* value, or OSC_TYPE_WAV for
 *             an invalid slot.
 * Why:        Menu formats osc1_wave by the type in force without owning the
 *             tagged runtime mapping.
 * Inputs:     slot.  Outputs: type.
 * Accessors:  menu_formatWaveCell().
 * Affiliates: osc_setType(), OSC_TYPE_*.
 */
uint8_t instrumentManager_osc1Type(uint8_t slot);
/*
 * A slot's current voice chain (S089 P2).
 *
 * What:       the chainType of the engine occupying the slot
 *             (VOICE_CHAIN_*); VOICE_CHAIN_DEFAULT for an unknown type or slot.
 * Why:        Menu relabels and re-lists the transient cells by chain without
 *             owning the tagged runtime mapping (as instrumentManager_osc1Type()).
 * Inputs:     slot.  Outputs: VOICE_CHAIN_*.
 * Accessors:  menu.c (menu_cellIsFoldWave(), menu_instrumentCellLabels(),
 *             the step-automation max); instrumentManager_slotUsesSrr().
 * Affiliates: the engines' chainType; IM_SPECIAL_VOICE_CHAIN.
 */
uint8_t instrumentManager_slotVoiceChain(uint8_t slot);
/*
 * A slot's fold type under WFd (S089 P2).
 *
 * What:       transGen.waveform clamped to VOICE_FOLD_COUNT - 1 (the type the
 *             fold actually plays); VOICE_FOLD_TRI for an unknown slot.
 * Why:        the single-parameter view names frq by fold type ("Bias",
 *             "Decay", ...).
 * Inputs:     slot.  Outputs: VOICE_FOLD_*.
 * Accessors:  menu_instrumentCellLabels() (menu.c).
 * Affiliates: voiceFold_process() (the same clamp).
 */
uint8_t instrumentManager_slotFoldType(uint8_t slot);
/*
 * Whether a slot holds a Colloid in a mono route (S103 P6).
 *
 * What:       1 when the slot's engine is the Colloid (col or clx) and its
 *             rte is a filter type (the mono route), else 0.
 * Why:        under a mono route the Colloid's spr / mot rows set the filter
 *             mix instead of the stack's placement (S103_P6_COLLOID_TWEAKS.md
 *             §1), so Menu relabels their long view without owning the
 *             tagged runtime mapping (as instrumentManager_slotVoiceChain()).
 * Inputs:     slot.  Outputs: 0 / 1.
 * Accessors:  menu.c menu_instrumentCellLabels().
 */
uint8_t instrumentManager_slotColloidMono(uint8_t slot);
void instrumentManager_calcSlotAsync(uint8_t slot);
void instrumentManager_calcSlotSyncBlock(uint8_t slot, int16_t *buf,
                                         uint8_t size);
/*
 * The slot's right output block, for a stereo engine (S095 P2, draft §5).
 *
 * What:       the stereo voice's right block (32 int16, written by its
 *             sync block in the same call that fills the mixer's left
 *             buffer): the Colloid's and Advanced Colloid's voice->right, the
 *             Tetrakt's (S096 P1), and (S099 P1, P-E) the Advanced
 *             Alkaline's while its chorus runs; NULL for every other type, an
 *             Advanced Alkaline with the chorus off, and an empty slot.
 *             S099 P1: read from a per-slot pointer table set where the
 *             runtime type or the chorus's unit changes
 *             (instrumentManager_slotRightUpdate()).
 * Why:        the mixer renders one mono stack block per slot (sampleData);
 *             the right block lives inside the voice (the slot reserve), so a
 *             stereo voice costs no new RAM. noinline keeps the call out of
 *             mixer_calcNextSampleBlock()'s register allocation; the table
 *             read is shorter than the type tests it replaces.
 * Inputs:     slot 0..5. Outputs: a borrowed pointer valid until the next
 *             render of the slot, or NULL.
 * Accessors:  mixer_calcNextSampleBlock() (once per slot per block, after
 *             instrumentManager_calcSlotSyncBlock()).
 */
const int16_t *instrumentManager_slotRight(uint8_t slot) __attribute__((noinline));
/*
 * Whether an instrument cell has no effect in the slot's current mode
 * (S095 P2, C-D16, C-D20).
 *
 * What:       for a Colloid slot, colloidParameters_cellInactive() (pos and
 *             psp under typ wav; spr and mot in fmd ser); 0 for every other
 *             type.
 * Why:        the compact cell shows "---" (menu_formatCellValue3()).
 * Inputs:     slot 0..5, the cell's descriptor.
 * Accessors:  menu.c menu_formatCellValue3().
 */
uint8_t instrumentManager_cellInactive(uint8_t slot, const ParamDescriptor *descriptor);
/*
 * Whether a target is a Tetrakt velocity group (S096 P1 SD3).
 *
 * What:       1 when target is a voice parameter on a Tetrakt slot whose
 *             local index is a layer-A row of one of the nine groups
 *             (tetraktParameters_velocityKind() != TKT_KIND_NONE); else 0.
 * Why:        the velocity destination cell marks a group "all four layers"
 *             with a '*' (tn* for tnA; plan §4.2).
 * Accessors:  menu.c menu_formatCellValue3(), the single-parameter painter.
 */
uint8_t instrumentManager_targetIsVelocityGroup(uint16_t target);
uint8_t instrumentManager_runtimePan(uint8_t slot);
/*
 * Read the current slot instrument's channel volume for the mixer.
 *
 * Inputs: zero-based render slot 0..5. Output: the tagged runtime member's
 * vol field (0..1, descriptor instrument_vol / 127, including any live LFO,
 * Morph, or step-automation value already written to it), or 0.0f for an
 * unknown/empty slot type, which renders silence anyway.
 *
 * Why (Session 072, Effects Phase 5 step 2): the voice engines no longer
 * apply volume; the mixer applies it after decimation so that the FX send can
 * tap the decimated, pre-volume signal. Keeping the type switch here, beside
 * instrumentManager_runtimePan(), preserves InstrumentManager as the only
 * module that knows which engine struct occupies a slot.
 *
 * Clients: mixer.c mixer_init() and mixer_calcNextSampleBlock() (once per
 * slot per 32-frame block). Foreground render context only. Affiliates:
 * DrumVoice/SnareVoice/CymbalVoice/HiHatVoice vol members.
 */
float instrumentManager_runtimeVolume(uint8_t slot);
/*
 * Report whether a slot's voice chain includes sample-rate reduction (S089 P0).
 *
 * What: 1 when the instrument type that owns the slot's live runtime runs a
 * voice chain whose last stage is the mixer's S&H decimator
 * (mixer_decimateBlock()); 0 otherwise. SRR is off by default: an unknown or
 * empty slot, and any type or chain that does not list it, returns 0. Drum,
 * Snare, Cymbal and HiHat return 1 under their default chain.
 *
 * Why: lets an instrument give the S&H cost to another chain stage
 * (S089 P1) without moving the decimator state out of the mixer's DTCM
 * arrays. Constant CPU: the answer depends only on the runtime type (and,
 * from P1, the chain selector), never on the srt value or voice activity.
 *
 * Inputs: zero-based render slot 0..5. Output: 0 or 1. Clients:
 * mixer_calcNextSampleBlock(), once per slot per 32-frame block, foreground
 * render context only. Affiliates: instrumentManager_slotType(),
 * INSTRUMENT_BIND_SLOT_DECIMATION (still writes mixer_decimation_rate[] for
 * every type, so the srt value is kept across chain changes).
 */
uint8_t instrumentManager_slotUsesSrr(uint8_t slot);
/*
 * Trigger the instrument assigned to a visible track.
 * Inputs: trigger_track 0..6, note (internal), velocity, reset (the
 *   trigger's reset flag: 1 for a first hit, 0 for a roll retrigger) and
 *   layers (a Tetrakt layer mask; 0 = the `lon` row, S096 P3). Output: one
 *   engine trigger; a Colloid gets the track's step length and the gate.
 */
void instrumentManager_triggerTrack(uint8_t trigger_track, uint8_t note,
                                    uint8_t velocity, uint8_t reset,
                                    uint8_t layers);

/*
 * Arm a fast gain fade-out on a track's voice (S079 P2 fix 4).
 *
 * What: maps a visible trigger track to its storage slot and sets that slot's
 * duck request. Why: a per-step roll's fill boundary must duck the sounding
 * voice with a short output fade instead of altering the voice's decay
 * parameter (which was the P1 bug). Inputs: visible trigger track 0..6.
 * Output: the slot's duck request is set; the audio path arms the fade at the
 * next rendered block. Context: called from the TIM3 sequencer ISR; a single
 * byte store is atomic on Cortex-M7. Affiliates: seq_processRollTicks(),
 * instrumentManager_calcSlotAsync().
 */
void instrumentManager_requestGateDuck(uint8_t trigger_track);

/*
 * Current per-slot roll fill gate gain, 1.0 = open, 0.0 = fully ducked
 * (S079 P2 fix 4).
 *
 * What: returns the live fade multiplier for one slot. Why: the mixer folds it
 * into the slot's dry and send gains. Inputs: zero-based slot. Output:
 * 0.0..1.0. Affiliates: mixer_faderGains().
 */
float instrumentManager_gateGain(uint8_t slot);

/*
 * Gate requests and hooks for engines with a note gate (S096 P3 C1).
 *
 * instrumentManager_requestGateRelease(track, brk): close the track's slot
 *   gate at its next async; brk 1 also breaks the effective sustain (a
 *   forced release). Context: TIM3 or foreground.
 * instrumentManager_renewGate(track): a roll carried into a new step renews
 *   a Colloid's gate by one step of the track. Foreground.
 * instrumentManager_changeArm(track) / changeDecide(track): the Scene/track
 *   change rule (plan §2.1 rule 6). Foreground, from
 *   voiceControl_processPending() with seq_takeChangeMasks().
 * instrumentManager_automationWritten(slot, descriptor, value): told of every
 *   value the automation drain or a ramp writes; a Colloid's sus row with
 *   value 1 marks the change decision's susSeen. Foreground.
 * Only INSTRUMENT_TYPE_COL acts; every other slot type ignores the calls.
 * The full description is the comment block in InstrumentManager.c.
 */
void instrumentManager_requestGateRelease(uint8_t trigger_track, uint8_t brk);
void instrumentManager_renewGate(uint8_t trigger_track);
void instrumentManager_changeArm(uint8_t trigger_track);
void instrumentManager_changeDecide(uint8_t trigger_track);
void instrumentManager_automationWritten(uint8_t slot,
                                         const ParamDescriptor *descriptor,
                                         instrument_param_value_t value);
/*
 * Set or clear one slot's MIDI Learn delta overlay (S096 P3 C4): a voice
 * row's non-destructive pressure/note/velocity delta on top of its base.
 * delta 0 frees it and restores the base. Returns the effective value.
 * Full contract in InstrumentManager.c.
 */
uint8_t instrumentManager_deltaOverlaySet(uint8_t slot, uint8_t local,
                                          int8_t delta);
/* The paraphonic hold (S096 P3 C3): on a Colloid slot, sus holds with the
** transport stopped while MIDI keys are down. Other types ignore it. */
void instrumentManager_setMidiHold(uint8_t slot, uint8_t on);
/*
 * Return the type that currently owns a slot's live tagged runtime member.
 *
 * Inputs: zero-based slot. Output: the runtime tag, not merely the selected
 * Scene's stored type, so callers such as legacy MIDI can reject a type-specific
 * field write while a deferred Scene replacement is still pending.
 */
instrument_type_t instrumentManager_runtimeType(uint8_t slot);
/*
 * The engine behind a type (S099 P1).
 *
 * What:       the type whose engine (union member, entry points, engine
 *             rules) a registry type runs: INSTRUMENT_TYPE_CLX -> COL (P-E:
 *             ALX -> ALK); every other type is its own engine; UNKNOWN and
 *             out-of-range values give UNKNOWN.
 *             instrumentManager_runtimeEngine(slot) is the same for the slot's
 *             live runtime type.
 * Why:        the Advanced types run the Basic engines unchanged; code that
 *             asks "is this a Colloid voice" (menu wave names, the MIDI
 *             note modes, the engine branches in InstrumentManager.c) tests
 *             the engine, and registry lookups keep the type.
 * Accessors:  InstrumentManager.c; menu.c menu_formatWaveCell();
 *             MidiNoteModes.c.
 */
instrument_type_t instrumentManager_typeEngine(instrument_type_t type);
instrument_type_t instrumentManager_runtimeEngine(uint8_t slot);
/*
 * Visit the two modulation destinations owned by every live runtime LFO.
 *
 * Inputs: a synchronous visitor and optional context. Output: one callback per
 * tagged slot that currently exposes an LFO; pointers are borrowed for the
 * callback only and must not survive a Scene/type commit. This keeps external
 * modulation maintenance independent of historical fixed engine globals.
 */
void instrumentManager_visitRuntimeLfoNodes(
    instrument_runtime_lfo_node_visitor_t visitor, void *context);
/*
 * Borrow the instance selected by a slot's current runtime tag.
 *
 * Inputs: zero-based slot. Output: an immediate-use descriptor/runtime pointer
 * to that tagged member, or NULL. Callers must not retain it across a Scene or
 * Instrument replacement because reset clears and reinitializes the slot.
 */
void *instrumentManager_runtimeInstance(uint8_t slot);
/*
 * Apply one retained descriptor value to its runtime owner.
 *
 * Inputs: active Scene slot, descriptor registry entry, and descriptor-domain
 * byte value. Output: the corresponding DSP/runtime owner is updated. For
 * paired LFO target selectors, writing either the voice namespace or parameter
 * token also reinstalls the pair from the current sibling cell; this clears
 * stale Scene-target, decimation, or Morph contributions when the namespace
 * changes without requiring a second UI edit. The caller must have stored the
 * retained value in SceneData before invoking this active-runtime path.
 * Clients: Preset supplemental apply, Menu edits, Kit/Scene activation, and
 * bounded runtime automation.
 */
uint8_t instrumentManager_writeRuntime(uint8_t slot,
                                       const ParamDescriptor *descriptor,
                                       instrument_param_value_t value);

/*
 * Diagnostic proof of special-writer tags (S073 Step 2).
 *
 * What:       compares every descriptor tag with the original key classifier.
 * Why:        proves the flash tags reproduce the pre-S073 writer mapping.
 * Inputs:     the immutable instrument registry.
 * Outputs:    mismatch count, clamped to 9; zero is pass.
 * Accessors:  main.c's existing DEV_MODE_DIAGNOSTIC boot screen.
 * Affiliates: check_special_tags.py and the tagged parameter tables.
 */
uint8_t instrumentManager_specialTagSelfCheck(void);

#endif
