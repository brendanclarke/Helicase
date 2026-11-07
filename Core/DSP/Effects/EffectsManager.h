/*
 * Core/DSP/Effects/EffectsManager.h
 *
 *  Created on: 27.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  brendanpaulclarke@gmail.com
 *  https://www.brendanclarke.com
 * ------------------------------------------------------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ------------------------------------------------------------------------------------------------------------------------
 *  Redistribution and use of the LXR02 Open-Source, hardware driver code, or any derivative works are permitted
 *  provided that the following conditions are met:
 *
 *       - The code may not be sold, nor may it be used in a commercial product or activity.
 *
 *       - Redistributions that are modified from the original source must include the complete
 *         source code, including the source code for all components used by a binary built
 *         from the modified sources. However, as a special exception, the source code distributed
 *         need not include anything that is normally distributed (in either source or binary form)
 *         with the major components (compiler, kernel, and so on) of the operating system on which
 *         the executable runs, unless that component itself accompanies the executable.
 *
 *       - Redistributions must reproduce the above copyright notice, this list of conditions and the
 *         following disclaimer in the documentation and/or other materials provided with the distribution.
 * ------------------------------------------------------------------------------------------------------------------------
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *   USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ------------------------------------------------------------------------------------------------------------------------
 */

#ifndef EFFECTS_MANAGER_H_
#define EFFECTS_MANAGER_H_

#include <stdint.h>
#include "EffectTypes.h"
#include "InstrumentManager.h"
#include "FxBuffer.h"

/*
 * Effect registry and runtime manager (Session 072, Effects Phase 5).
 *
 * What: owns the immutable Effect registry and the active Scene's runtime
 * state. It resolves retained endpoint images, selects type operations,
 * switches types through FxBuffer's handoff contract, and publishes the
 * common return settings consumed by the mixer's FX return.
 * Why: only this module knows a type's descriptor layout; SceneData owns the
 * retained bytes and FxBuffer owns the shared audio arena independently.
 * Context: foreground only. It is called by boot, Preset, and the render
 * block; it must not be called from an ISR.
 * Affiliates: SceneData, FxBuffer, Preset, mixer, AutoSave, Menu, Sequencer
 * (FX latch, automation drain), InstrumentManager (LFO adapters), and the
 * mixer FX bus.
 */

/*
 * Registry ids are append-only; persisted identity is the three-byte token.
 * EFFECT_TYPE_CRUMPBIT (S074): the first buffer-using type (`cbt`, 8-bit
 * data lines and an 8-bit tape delay in the FxBuffer share).
 */
#define EFFECT_TYPE_STEREO_FILTER        1u
#define EFFECT_TYPE_CRUMPBIT             2u
#define EFFECT_TYPE_COUNT                3u

/* Extra descriptor capability flags used by Effect-specific owners. */
#define EFFECT_PARAM_FLAG_WIDE8              0x01u
#define EFFECT_PARAM_FLAG_BUFFER_DEPENDENT   0x02u

/* Sequencer lane sentinel values. Lane zero is always the Morph source. */
#define EFFECT_LANE_MORPH_SOURCE         0xFEu
#define EFFECT_LANE_NONE                 0xFFu

/* Runtime I/O shape flags; the mixer sizes the FX bus from them (0 = off). */
#define EFFECT_IO_MONO_IN                0x01u
#define EFFECT_IO_STEREO_IN              0x02u
#define EFFECT_IO_MONO_OUT               0x04u
#define EFFECT_IO_STEREO_OUT             0x08u

/* Convert a Pattern's seven-bit value to an Effect's stored domain. */
typedef uint8_t (*effect_expand7_fn)(uint8_t value7);
uint8_t effect_expand7Linear(uint8_t value7);

/*
 * One Effect parameter descriptor.
 *
 * base is the shared ParamDescriptor contract used by Menu and target
 * selection. default_value is the single type-change default for both normal
 * and Morph images. max_value is the inclusive retained/runtime clamp.
 * effect_flags and expand7 describe Effect-only automation rules.
 */
typedef struct {
    ParamDescriptor base;
    uint8_t default_value;
    uint8_t max_value;
    uint8_t effect_flags;
    effect_expand7_fn expand7;
} effect_param_descriptor_t;

/*
 * One block passed to an Effect process operation.
 *
 * Samples are normalized floats where 1.0 is int16 full scale. l is always
 * present; r is supplied when the type has stereo input or stereo output and
 * may be NULL for a mono-input/mono-output block. For a mono-input/stereo-
 * output type, r is supplied as a zeroed output channel and channels remains
 * the input count. frames is the current render-block length, and share is
 * the current FxBuffer allocation for any buffer-using type.
 */
typedef struct {
    float *l;
    float *r;
    uint8_t frames;
    uint8_t channels;
    const fx_share_t *share;
} effect_io_t;

/*
 * Optional per-type Effect-page hooks (Session 072 step 7; plan §13.6;
 * extended in S074 for CrumpBit).
 *
 * What: lets a type take over SELECT, TRACK or BAR gestures, add LED
 * rendering, paint a type-owned top row, and label its own values on the
 * Effect page.
 * - select/track/bar(button, shift, pressed) return nonzero when handled;
 *   zero falls back to the default page behaviour. select may return
 *   EFFECT_UI_SHOW_HOME: handled, and the page shows the layout's home
 *   screen. Any nonzero select return abandons an open `typ` browse (S074).
 * - render_leds() runs after the page has drawn its own LEDs. With
 *   EFFECT_UI_FLAG_OWNS_SELECT_LEDS it is also the only writer of the SELECT
 *   LED row while the type is on the page: the page's active SELECT LED is
 *   suppressed, and menuEffects_renderSelectLeds() calls this instead.
 * - paint_row0(sub_page, screen, row0) writes columns 0..14 of the compact
 *   top row for screens flagged in select_layout->custom_row0; column 15
 *   keeps the page's scroll marker. Those screens show no automation name
 *   markers.
 * - format_value3(index, value, out) may replace the 3-character value text
 *   of a type row on the Effect page only: compact cells, the full view, and
 *   held-step values. Storage and the STEP automation page show raw values.
 * - flags (S074): EFFECT_UI_FLAG_*.
 * Rules: hooks run in foreground, must not block, must write retained data
 * only through the EffectsManager edit API (menuEffects_editParam() for
 * held-aware writes), and never touch the filesystem. Any member may be NULL.
 * Affiliates: menuEffects_hook*(), _renderLeds(), _renderSelectLeds(),
 * _paintRow0(), _formatParamValue3(), _liveRefreshWanted(), buttonHandler FX
 * branches, and the registry's ui field.
 */
#define EFFECT_UI_HANDLED                 1u
#define EFFECT_UI_SHOW_HOME               2u
#define EFFECT_UI_FLAG_OWNS_SELECT_LEDS   0x01u

/*
 * S091 P3 hooks (any may be NULL):
 * - labels(index, variant, &short3, &category, &long_name) (S091 P6; it
 *   replaced S091 P3's name3, which named a row on the Effect page only):
 *   the row's names for one variant of the type (CrumpBit: the `sub`
 *   value), used on every page that names an Effect row: Effect page
 *   cells and the full view, STEP automation and modulation/LFO targets.
 *   The caller sets the three pointers to the descriptor's names first; the
 *   hook overwrites any it renames and returns nonzero if it renamed any.
 *   variant is the type's lane_variant_row value in the record shown
 *   (effects_rowLabels()). Strings are static, NUL-terminated, at most 3
 *   (short) and 8 (category, long name) characters.
 * - layout(): the select layout to show now (CrumpBit: per shown sub-type);
 *   NULL return or no hook = the registry's select_layout.
 * - format_target3(index, value, out): value text for automation targets
 *   outside the Effect page (the STEP automation page, Pattern value entry);
 *   only rows whose text needs no page context (CrumpBit: `sub` names).
 *   Nonzero when out[0..2] was written.
 */
struct effect_select_layout;

struct effect_ui_hooks {
    uint8_t (*select)(uint8_t button, uint8_t shift, uint8_t pressed);
    uint8_t (*track)(uint8_t track, uint8_t shift, uint8_t pressed);
    uint8_t (*bar)(uint8_t bar, uint8_t shift, uint8_t pressed);
    void (*render_leds)(void);
    void (*paint_row0)(uint8_t sub_page, uint8_t screen, char *row0);
    uint8_t (*format_value3)(uint8_t index, uint8_t value, char *out);
    uint8_t flags;
    uint8_t (*labels)(uint8_t index, uint8_t variant, const char **short3,
                      const char **category, const char **long_name);
    const struct effect_select_layout *(*layout)(void);
    uint8_t (*format_target3)(uint8_t index, uint8_t value, char *out);
};
typedef struct effect_ui_hooks effect_ui_hooks_t;

/*
 * Optional per-type SELECT layout for the Effect page (NULL = default, §13.2;
 * extended in S074).
 *
 * screen_count[b], cells[b][screen][column]: the screens behind SELECT b+1
 * (up to 4). A cell holds a descriptor index, EFFECT_LANE_NONE (empty), or
 * EFFECT_LAYOUT_CELL_MORPH (the manager `mrp` cell). Index 0 (SELECT 1) is
 * always manager-owned and ignored here.
 * custom_row0[b]: bit s set = screen s of SELECT b+1 has a type-painted top
 * row (ui->paint_row0) instead of names.
 * home_sub_page/home_screen: the screen a hooked SELECT shows when it returns
 * EFFECT_UI_SHOW_HOME.
 * Affiliates: menuEffects_cellAt(), _screenCount(), _screenHasCustomRow0(),
 * _home(), and crumpBit_layout.
 */
#define EFFECT_LAYOUT_CELL_MORPH         0xFDu

typedef struct effect_select_layout {
    uint8_t screen_count[8];
    uint8_t cells[8][4][4];
    uint8_t custom_row0[8];
    uint8_t home_sub_page;
    uint8_t home_screen;
} effect_select_layout_t;

/*
 * DSP operations of one Effect type.
 *
 * init/export_handoff own type-specific arena adoption. write_param receives
 * only type-specific indices (3..63); EffectsManager owns common indices 0..2.
 * process handles one normalized block. buffer_changed and effective_max are
 * optional hooks for types affected by a changing FxBuffer share.
 * S091 P3:
 * - effective_max receives the live runtime (NULL when the caller has none,
 *   e.g. a page edit of a Scene whose type is not live), so a type with
 *   sub-engines can clamp by the engine it runs;
 * - lane_map (optional) returns the FX lane -> row table for one variant;
 *   the variant is the record's stored value of row lane_variant_row
 *   (0..lane_variant_count-1). NULL keeps the registry's fixed lanes[].
 *   Every lane reader goes through effects_laneTable(record).
 */
typedef struct {
    void (*init)(void *rt, const fxbuf_handoff_t *handoff);
    void (*export_handoff)(const void *rt, fxbuf_handoff_t *out);
    void (*write_param)(void *rt, uint8_t index, uint8_t value);
    void (*process)(void *rt, effect_io_t *io);
    void (*buffer_changed)(void *rt, const fx_share_t *share);
    uint8_t (*effective_max)(const void *rt, uint8_t index,
                             const fx_share_t *share);
    const uint8_t *(*lane_map)(uint8_t variant);
    uint8_t lane_variant_row;
    uint8_t lane_variant_count;
} effect_type_ops_t;

/*
 * Immutable registry row.
 *
 * token3 is the persisted three-character identity; abbrev5 and full8 are UI
 * names. descriptors[0..2] are the shared common rows. lanes names local
 * descriptor indices, with lane zero fixed to EFFECT_LANE_MORPH_SOURCE.
 */
typedef struct {
    const char *token3;
    const char *abbrev5;
    const char *full8;
    uint8_t io_flags;
    const effect_param_descriptor_t *descriptors;
    uint8_t descriptor_count;
    uint8_t lanes[EFFECT_SEQ_LANE_COUNT];
    const effect_select_layout_t *select_layout;
    const effect_type_ops_t *ops;
    const effect_ui_hooks_t *ui;
    uint16_t runtime_bytes;
    uint32_t buffer_min_bytes;
    uint32_t buffer_pref_bytes;
} effect_registry_entry_t;

/* Common runtime consumed by the Step 5 return path. */
typedef struct {
    uint8_t route;
    uint8_t pan;
    float level;
} effects_common_runtime_t;

/* Registry lookup and capability API. */
uint8_t effects_registryCount(void);
const effect_registry_entry_t *effects_registryEntry(effect_type_id_t type);
const char *effects_typeToken(effect_type_id_t type);
uint8_t effects_typeFromToken(const char token[3], effect_type_id_t *type_out);
const effect_param_descriptor_t *effects_descriptor(effect_type_id_t type,
                                                    uint8_t index);
const effect_param_descriptor_t *effects_descriptorByKey(
    effect_type_id_t type, const char *file_key, uint8_t *index_out);
uint8_t effects_paramAutomatable(effect_type_id_t type, uint8_t index);
uint8_t effects_paramModulatable(effect_type_id_t type, uint8_t index);

/*
 * Largest value a row may take now (S091 P2).
 *
 * What: the descriptor max_value, lowered for a BUFFER_DEPENDENT row by the
 * type's effective_max op at the current FxBuffer share, the same clamp
 * effects_service() applies to the resolved value. Why: the Effect page must
 * not let a user set a value the DSP would clamp (user D2: a voice-unit
 * reservation clamps the settable range as well as the DSP target). Stored
 * values are never rewritten here (A30): a value saved above the clamp stays
 * in the record and plays again when the share grows.
 * Inputs: type, local index. Output: inclusive maximum (0 for an unknown
 * row). Foreground only (reads the share with fxbuf_effectShare()).
 * Accessors: menuEffects_clampValue().
 * Affiliates: effect_type_ops_t.effective_max, EFFECT_PARAM_FLAG_BUFFER_DEPENDENT.
 */
uint8_t effects_paramEffectiveMax(effect_type_id_t type, uint8_t index);

/*
 * Inclusive minimum of one Effect row (S091 P5 follow-up).
 *
 * What: the modulation domain's min_value for a row that has a domain, else
 * 0. Effect rows had only an inclusive maximum (max_value); a row built with
 * EFFECT_MOD_RANGE(lo, hi) (CrumpBit's pitch rows, -36..+36 st) also stops
 * at lo. The domain already bounds the LFO (effects_lfoResolve()), so the
 * same bound for every other writer keeps one range per row.
 * Inputs: descriptor (may be NULL). Output: 0..max_value.
 * Accessors: effects_setParameterScene(), effects_setSeqLaneLockScene(),
 * effects_setAutomationValue(), effects_service() (resolution),
 * menuEffects_clampValue(), menu.c STEP automation editing.
 */
static inline uint8_t effects_descriptorMin(
    const effect_param_descriptor_t *descriptor)
{
    return (descriptor &&
            descriptor->base.mod_domain.flags != INSTRUMENT_MOD_DOMAIN_NONE)
        ? (uint8_t)descriptor->base.mod_domain.min_value : 0u;
}

/*
 * `.fx` storage helpers (Session 072 step 6; plan §14.1).
 *
 * effects_recordDefaultsForType() builds one complete, unowned record for a
 * registered type: SceneData's `off` defaults (common out/vol/pan, fwd /
 * length 16 / 1/16, no locks), then the type id and every type-specific index
 * 3..63 set to that type's single default in BOTH endpoint images (0 where the
 * type has no row), the same values effects_changeType() installs.
 * Unknown types build `off`. It never marks AutoSave and never touches a
 * resident Scene: the caller owns the commit. Clients: the `.fx` parser,
 * AutoSave's Effect reader, and the boot narrow Effect loader.
 *
 * effects_laneFileKey() / effects_laneByFileKey() translate FX-sequence lane
 * numbers to and from their `.fx` `[sequence]` key (after the `lane.` prefix).
 * Lane 0 is always EFFECT_LANE_MORPH_FILE_KEY; other lanes use the descriptor
 * file_key the record's lane table names. EFFECT_LANE_NONE lanes have no key
 * (NULL / not found), so a file cannot address an unused lane. Why here: the
 * registry owns lane meaning; storage must not duplicate that table.
 * S091 P3: both take the RECORD, because a type with a lane_map op (CrumpBit)
 * maps lanes by the record's stored sub-type (plan §2.9). The `.fx` writer
 * emits [params] before [sequence], so the reader knows `sub` by then.
 *
 * effects_laneTable() returns the record's 16-entry lane -> row table: the
 * type's lane_map(variant) when it has one, else the registry's lanes[].
 */
#define EFFECT_LANE_MORPH_FILE_KEY "effect_morph"
void effects_recordDefaultsForType(effect_record_t *record,
                                   effect_type_id_t type);
const uint8_t *effects_laneTable(const effect_record_t *record);
const char *effects_laneFileKey(const effect_record_t *record, uint8_t lane);
uint8_t effects_laneByFileKey(const effect_record_t *record,
                              const char *file_key, uint8_t *lane_out);

/*
 * Automation value text of an Effect target outside the Effect page (S091
 * P3, OQ5): calls the Scene type's format_target3 hook for the target's row.
 * Inputs: Scene, Effect target id, value, 3-character output. Output:
 * nonzero when out[0..2] was written (else the caller formats generically).
 * Accessors: menu.c STEP automation value formatting.
 */
uint8_t effects_formatTargetValue3(uint8_t scene_index, uint16_t target,
                                   uint8_t value, char *out);

/*
 * Retained Effect edit API (Session 072 step 7).
 *
 * What: the single mutation boundary for user-facing Effect edits: the
 * Effect page now, type UI hooks, and the Step 8 lock editor later. Each
 * setter validates against the Scene's current type, clamps to the
 * descriptor maximum, writes through SceneData's change-aware setter (which
 * marks AutoSave and clears the card-clean bit), and returns nonzero only
 * when a byte changed. Runtime needs no call: effects_service() rescans every
 * block.
 * Why here: plan §13.6 requires edits to flow through EffectsManager so the
 * edit-mask fan-out lives in one place without touching callers (S072_ST7 D3).
 * Fan-out (S072 Step 10; plan §7.4, A44): an active-Scene edit reaches every
 * Scene in its VOICE edit mask. Parameter, lock, and sequence edits require
 * the target Effect type to equal the origin's; Effect Morph and type changes
 * reach every masked Scene. Inactive-Scene edits remain local. Setters return
 * nonzero when any reached Scene changed.
 * Image rule: EFFECT_IMAGE_MORPH addresses the Morph endpoint for Morphable
 * rows only. For a non-Morphable row it reads and writes the single normal
 * value, so cells without a Morph endpoint show their single value.
 */
typedef enum {
    EFFECT_IMAGE_NORMAL = 0,
    EFFECT_IMAGE_MORPH
} effect_image_t;

uint8_t effects_paramMorphable(effect_type_id_t type, uint8_t index);
uint8_t effects_getParameter(uint8_t scene_index, uint8_t index,
                             effect_image_t image);
/* Convert a retained Effect value to its 7-bit Pattern form (S081 §9.2,
** P1B item 18). WIDE8 rows use the smallest v with expand7(v) >= value. */
uint8_t effects_storeValue7(uint8_t scene_index, uint8_t local, uint8_t value);
uint8_t effects_setParameter(uint8_t scene_index, uint8_t index,
                             effect_image_t image, uint8_t value);
uint8_t effects_setSeqRunMode(uint8_t scene_index, uint8_t mode);
uint8_t effects_setSeqLength(uint8_t scene_index, uint8_t length);
uint8_t effects_setSeqStepScale(uint8_t scene_index, uint8_t scale);
uint8_t effects_setMorphAmount(uint8_t scene_index, uint8_t amount);
/*
 * Effect Morph from the global Morph (S083 follow-up, "global Morph sets
 * all Morph").
 *
 * What: writes ONE Scene's retained Effect Morph amount with the same
 * menu-edit semantics as an `fxm` edit: the Pattern `fxm` override is
 * cleared (S076 Rule A) and, on the active Scene, menuHoldMorph keeps the
 * new amount sounding over an FX Morph lock until the next Morph
 * automation (S081 §5.3). No fan-out: the caller, preset_morphScene(),
 * already runs once per edit-mask Scene. Never records (global Morph is
 * never recorded, G-1; the per-voice Morph writes of the same gesture are
 * not recorded either). Inputs: Scene index, 0..255 amount. Output: 1 if
 * the retained amount changed. Callers: preset_morphScene() (PERF `mrp`,
 * MIDI CC1 on G). Affiliates: effects_setMorphAmount() (the `fxm` page edit
 * with its own fan-out), preset_syncEffectMorphMirror().
 */
uint8_t effects_setMorphAmountFromGlobal(uint8_t scene_index, uint8_t amount);

/*
 * Live FX-sequencer state and lock API (Session 072 step 8; plan §11/§13.4).
 *
 * EFFECT_SEQ_STEP_NONE is returned when a non-SEL sequence is stopped, has
 * not yet consumed a boundary, or has no valid active step. The remaining
 * functions are foreground-only: EffectsManager consumes the TIM3 latch,
 * `sel` selects a retained step immediately, and lane helpers translate the
 * registry's descriptor lanes for the Effect page's hold editor.
 * effects_setSeqLaneLock() uses the same held-step mask and lane for every
 * same-type masked Scene.
 */
#define EFFECT_SEQ_STEP_NONE 0xFFu
uint8_t effects_seqActiveStep(void);
uint8_t effects_seqSelectedStep(void);
void effects_seqSelect(uint8_t step);
uint8_t effects_seqSerial(void);
uint8_t effects_laneOfParam(const effect_record_t *record, uint8_t index,
                             uint8_t *lane_out);
uint8_t effects_getLaneLock(uint8_t scene_index, uint8_t step, uint8_t lane,
                            uint8_t *value_out);
uint8_t effects_setSeqLaneLock(uint8_t scene_index, uint16_t step_mask,
                               uint8_t lane, uint8_t value);

/*
 * Fan-out Effect record and FX sequence operations for copy/clear (S075).
 *
 * What: paste or reset a whole Effect record, paste one FX step (lock mask
 * and 16 lane values), or clear lanes on steps, applying the same change to
 * every Scene in the destination's edit mask that shares the destination's
 * current Effect type (the active Scene uses the self-repairing mask getter,
 * any other Scene its own indexed entry, user F5). effects_pasteRecord() may
 * change the type of the destination and its members; effects_resetRecord()
 * returns them to registered defaults (type `off`). Why: user rule F3 /
 * confirm 1 — Effect pastes and FX sequence pastes and clears fan out; the
 * existing lock setter could only set locks. Inputs: destination Scene and
 * data (src may point at a live record; a member that is the source itself is
 * skipped). Outputs: SceneData commits (each marks its AutoSave cells and
 * card-clean bit), runtime activation or sequence-serial bump / held-Morph
 * invalidation for the active Scene, mask revalidation after type changes.
 * Record functions return the written Scene mask (identity names follow it);
 * sequence functions return nonzero when any lane or lock changed.
 * Clients: copyOps.c, clearOps.c. Affiliates: scene_commitEffectRecord(),
 * scene_setEffectSeqLane*(), effects_activateScene(),
 * bank_revalidateVoiceEditMasks().
 */
uint16_t effects_pasteRecord(uint8_t dst_scene, const effect_record_t *src);
uint16_t effects_resetRecord(uint8_t dst_scene);
/*
 * Reset Effect morph endpoints to their Normal values (S076 P3).
 *
 * What:       effects_resetMorphToNormal() iterates each edit-mask member of
 *             the destination and, for every Effect parameter index where
 *             effects_paramMorphable() is nonzero for the shared type, copies
 *             effect_record.normal[i] onto effect_record.morph[i]. Non-morphable
 *             rows, sequence data, type and settings are untouched.
 *             effects_resetMorphToNormalSingle() performs the same copy for one
 *             Scene with no edit-mask fan-out.
 * Why:        "clear scene reset fx morph" fans out exactly like "clear fx"
 *             (same-type members receive the change). "clear scene reset morph"
 *             and "copy scene -> morph" fan out through the bank edit mask and
 *             call effects_resetMorphToNormalSingle() per member.
 *             Centralising the morphability check and the SceneData whole-record
 *             commit path keeps the effect-morph owner boundary in Effects.
 * Inputs:     effects_resetMorphToNormal(): dst_scene; the destination's
 *             current Effect type determines the fan-out mask and which
 *             parameters are morphable. effects_resetMorphToNormalSingle():
 *             scene_index.
 * Outputs:    effects_resetMorphToNormal() returns the Scene mask of Scenes
 *             whose Effect record was written (0 if invalid or nothing changed);
 *             effects_resetMorphToNormalSingle() returns nonzero if any byte
 *             changed. Both mark the Effect AutoSave region through
 *             scene_finishEffectWholeCommit() and activate the runtime when the
 *             active Scene changed.
 * Accessors:  effects_paramMorphable(), effects_fanoutMask(),
 *             scene_effectConst(), scene_effectRecordForWholeCommit(),
 *             scene_finishEffectWholeCommit(), effects_activateScene().
 * Affiliates: effects_resetRecord() (model), ccClear_runResetFxMorph(),
 *             ccClear_runResetSceneMorph(), ccCopy_runSceneMorph().
 */
uint16_t effects_resetMorphToNormal(uint8_t dst_scene);
uint8_t effects_resetMorphToNormalSingle(uint8_t scene_index);
uint8_t effects_pasteSeqStep(uint8_t dst_scene, uint8_t step,
                             const effect_seq_step_t *src);
uint8_t effects_clearSeqLanes(uint8_t scene_index, uint16_t step_mask,
                              uint16_t lane_mask);

/*
 * Pattern automation and LFO on Effects (Session 072 step 9; plan §9, §10).
 *
 * Target identity: block-7 IDs 448 + local (EffectTypes.h).
 * - Pattern automation accepts AUTOMATABLE locals 0..62. Local 63 aliases the
 *   Pattern off sentinel, and WIDE8 rows require expand7.
 * - LFO accepts MODULATABLE locals that have a modulation domain.
 *
 * Validation is always against a Scene's retained Effect type:
 * - pickers and the Pattern writer pass the viewed Scene;
 * - runtime apply checks the active runtime type on every call, so a stale
 *   entry (for example after `typ`) is ignored rather than misapplied.
 *
 * Overlay/menuHold lifetime (R13, S081 §5.3): a Pattern overlay is last-writer-
 * wins and is held until an FX lock step start, a menu edit, or a Scene change.
 * A menu edit sets menuHold (menu beats lock and overlay); the next automation
 * (an FX lock step start or a track overlay) clears it. The FX step-marker
 * machinery (StepBegin/StepFlush/owner tracks) was removed under R13.
 *
 * effects_automationReset() clears every overlay, menuHold, and the `fxm` Morph
 * override. It runs from the drain on the common sequencer reset path and from
 * Scene activation. LFO contributions are base-independent direction / depth
 * entries per source slot and pair; effects_service() resolves them around the
 * current held value every render block.
 *
 * Context: foreground only; all state is foreground-owned.
 */
#define EFFECT_LFO_TARGET_MORPH      0xFEu
#define EFFECT_LFO_TARGET_NONE       0xFFu
#define EFFECT_LFO_DIRECTION_NONE    0u
#define EFFECT_LFO_DIRECTION_DOWN    1u
#define EFFECT_LFO_DIRECTION_UP      2u

uint8_t effects_targetValid(uint8_t scene_index, uint16_t id,
                            instrument_target_use_t use);
const effect_param_descriptor_t *effects_targetDescriptor(
    uint8_t scene_index, uint16_t id, instrument_target_use_t use);

/*
 * Names of one Effect row (S091 P6).
 *
 * What: the descriptor's short name, category and long name, then the
 * type's labels hook for the given variant (a type whose rows mean
 * different things per variant, CrumpBit per `sub`, names them per
 * variant). Any output pointer may be NULL.
 * - effects_rowLabels(type, variant, index, ...): an explicit variant (the
 *   Effect page passes the variant it shows, held steps included);
 * - effects_targetLabels(scene, target, use, ...): an Effect target of one
 *   Scene, named by that Scene's stored variant (STEP automation,
 *   modulation/LFO targets). Returns 0 when the target is not valid for
 *   the use (the outputs are then untouched).
 * - effects_recordVariant(record): the record's variant (its
 *   lane_variant_row value, clamped), 0 for a type without variants; the
 *   rule effects_laneTable() uses.
 * Accessors: menuEffects_cellLabels(), menu.c (full view, STEP automation,
 * modulation targets). Affiliates: effect_ui_hooks_t.labels.
 */
void effects_rowLabels(effect_type_id_t type, uint8_t variant, uint8_t index,
                       const char **short3, const char **category,
                       const char **long_name);
uint8_t effects_targetLabels(uint8_t scene_index, uint16_t id,
                             instrument_target_use_t use,
                             const char **short3, const char **category,
                             const char **long_name);
uint8_t effects_recordVariant(const effect_record_t *record);
uint16_t effects_stepTarget(uint8_t scene_index, uint16_t current,
                            int8_t direction, instrument_target_use_t use);

void effects_automationReset(void);
/* Register/reset the FX-sequence live-erase lane mask (S081 §10.4, G-3a). */
void effects_setLiveEraseLanes(uint16_t laneMask);
/* Take one FX step's erased lanes (S081 §10.4, P1B item 28): lowest pending
** step + its lane mask, cleared; 0 when none. Caller: liveRec_service(). */
uint8_t effects_takeErasedLocks(uint8_t *step, uint16_t *laneMask);
uint8_t effects_applyAutomation(uint8_t track, uint8_t local, uint8_t value7);
void effects_setMorphAutomation(uint8_t amount);
/*
 * Clear the Pattern `fxm` Morph-base override (S076 Rule A).
 *
 * What: deactivates the morph_override_valid flag without touching per-
 * parameter Effect overlays. Input: none. Output: the Effect service loop
 * falls back to retained Scene Effect Morph on the next block. Caller:
 * effects_setMorphAmountScene(). Affiliate: effects_setMorphAutomation().
 */
void effects_clearMorphAutomationOverride(void);

void effects_setLfoContribution(uint8_t source_slot, uint8_t pair,
                                uint8_t target, uint8_t direction,
                                uint8_t depth);
void effects_clearLfoSource(uint8_t source_slot, uint8_t pair);

#if DEV_MODE_DIAGNOSTIC
/* 0 means the immutable registry passed its runtime self-check. */
uint8_t effects_registryCheckResult(void);
#endif

/* Runtime lifecycle and resolution API. */
void effects_init(void);
void effects_activateScene(uint8_t scene_index);
/*
 * Retarget the running Effect to an identical copy of its source Scene
 * (S082 P2 fix 1).
 * What: changes only the Scene whose Effect record the per-block service
 * reads; no runtime, sequence, automation or LFO state changes.
 * Inputs: scene_index. Outputs: 1 retargeted, 0 refused (no record or a
 * different type).
 * Accessors: preset_tickBackgroundBankLoad() phase 3.
 */
uint8_t effects_rebindSourceScene(uint8_t scene_index);
uint8_t effects_changeType(uint8_t scene_index, effect_type_id_t type);
void effects_service(void);
void effects_process(effect_io_t *io);
effect_type_id_t effects_activeType(void);
uint8_t effects_activeIoFlags(void);
const effects_common_runtime_t *effects_commonRuntime(void);

#endif /* EFFECTS_MANAGER_H_ */
