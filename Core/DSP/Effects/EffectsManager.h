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
 * Effect registry and runtime manager (Session 072, Effects Phase 5 step 4).
 *
 * What: owns the immutable Effect registry and the active Scene's runtime
 * state. It resolves retained endpoint images, selects type operations,
 * switches types through FxBuffer's handoff contract, and publishes the
 * common return settings used by the later FX bus.
 * Why: only this module knows a type's descriptor layout; SceneData owns the
 * retained bytes and FxBuffer owns the shared audio arena independently.
 * Context: foreground only. It is called by boot, Preset, and the render
 * block; it must not be called from an ISR.
 * Affiliates: SceneData, FxBuffer, Preset, mixer, AutoSave, Menu, and the
 * later sequencer/LFO and FX-bus steps.
 */

/* Registry ids are append-only; persisted identity is the three-byte token. */
#define EFFECT_TYPE_STEREO_FILTER        1u
#define EFFECT_TYPE_COUNT                2u

/* Extra descriptor capability flags used by Effect-specific owners. */
#define EFFECT_PARAM_FLAG_WIDE8              0x01u
#define EFFECT_PARAM_FLAG_BUFFER_DEPENDENT   0x02u

/* Sequencer lane sentinel values. Lane zero is always the Morph source. */
#define EFFECT_LANE_MORPH_SOURCE         0xFEu
#define EFFECT_LANE_NONE                 0xFFu

/* Runtime I/O shape flags used by the future mixer bus. */
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
 * Optional per-type Effect-page hooks (Session 072 step 7; plan §13.6).
 *
 * What: lets a type take over SELECT, TRACK, or BAR gestures and add LED
 * rendering on the Effect page. Each input hook receives the zero-based
 * button, the SHIFT state, and pressed (1) / released (0), and returns
 * nonzero when it handled the gesture; zero falls back to the default page
 * behavior. render_leds runs after the page has drawn its own LEDs.
 * Rules: hooks run in foreground, must not block, must write retained data
 * only through the EffectsManager edit API, and never touch the filesystem.
 * Any member may be NULL. Affiliates: menuEffects_hook*(), buttonHandler FX
 * mode branches, and the registry's ui field.
 */
struct effect_ui_hooks {
    uint8_t (*select)(uint8_t button, uint8_t shift, uint8_t pressed);
    uint8_t (*track)(uint8_t track, uint8_t shift, uint8_t pressed);
    uint8_t (*bar)(uint8_t bar, uint8_t shift, uint8_t pressed);
    void (*render_leds)(void);
};
typedef struct effect_ui_hooks effect_ui_hooks_t;

/* Optional four-cell SELECT layout for future Effect pages. */
typedef struct {
    uint8_t screen_count[8];
    uint8_t cells[8][4][4];
} effect_select_layout_t;

/*
 * DSP operations of one Effect type.
 *
 * init/export_handoff own type-specific arena adoption. write_param receives
 * only type-specific indices (3..63); EffectsManager owns common indices 0..2.
 * process handles one normalized block. buffer_changed and effective_max are
 * optional hooks for types affected by a changing FxBuffer share.
 */
typedef struct {
    void (*init)(void *rt, const fxbuf_handoff_t *handoff);
    void (*export_handoff)(const void *rt, fxbuf_handoff_t *out);
    void (*write_param)(void *rt, uint8_t index, uint8_t value);
    void (*process)(void *rt, effect_io_t *io);
    void (*buffer_changed)(void *rt, const fx_share_t *share);
    uint8_t (*effective_max)(uint8_t index, const fx_share_t *share);
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
 * file_key the registry lane table names. EFFECT_LANE_NONE lanes have no key
 * (NULL / not found), so a file cannot address an unused lane. Why here: the
 * registry owns lane meaning; storage must not duplicate that table.
 */
#define EFFECT_LANE_MORPH_FILE_KEY "effect_morph"
void effects_recordDefaultsForType(effect_record_t *record,
                                   effect_type_id_t type);
const char *effects_laneFileKey(effect_type_id_t type, uint8_t lane);
uint8_t effects_laneByFileKey(effect_type_id_t type, const char *file_key,
                              uint8_t *lane_out);

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
 * Why here: plan §13.6 requires edits to flow through EffectsManager so Step
 * 10 can add edit-mask fan-out inside these functions without touching any
 * caller (S072_ST7 D3). Step 7 writes the given Scene only.
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
uint8_t effects_setParameter(uint8_t scene_index, uint8_t index,
                             effect_image_t image, uint8_t value);
uint8_t effects_setSeqRunMode(uint8_t scene_index, uint8_t mode);
uint8_t effects_setSeqLength(uint8_t scene_index, uint8_t length);
uint8_t effects_setSeqStepScale(uint8_t scene_index, uint8_t scale);
uint8_t effects_setMorphAmount(uint8_t scene_index, uint8_t amount);

/*
 * Live FX-sequencer state and lock API (Session 072 step 8; plan §11/§13.4).
 *
 * EFFECT_SEQ_STEP_NONE is returned when a non-SEL sequence is stopped, has
 * not yet consumed a boundary, or has no valid active step. The remaining
 * functions are foreground-only: EffectsManager consumes the TIM3 latch,
 * `sel` selects a retained step immediately, and lane helpers translate the
 * registry's descriptor lanes for the Effect page's hold editor.
 */
#define EFFECT_SEQ_STEP_NONE 0xFFu
uint8_t effects_seqActiveStep(void);
uint8_t effects_seqSelectedStep(void);
void effects_seqSelect(uint8_t step);
uint8_t effects_seqSerial(void);
uint8_t effects_laneOfParam(effect_type_id_t type, uint8_t index,
                             uint8_t *lane_out);
uint8_t effects_getLaneLock(uint8_t scene_index, uint8_t step, uint8_t lane,
                            uint8_t *value_out);
uint8_t effects_setSeqLaneLock(uint8_t scene_index, uint16_t step_mask,
                               uint8_t lane, uint8_t value);

#if DEV_MODE_DIAGNOSTIC
/* 0 means the immutable registry passed its runtime self-check. */
uint8_t effects_registryCheckResult(void);
#endif

/* Runtime lifecycle and resolution API. */
void effects_init(void);
void effects_activateScene(uint8_t scene_index);
uint8_t effects_changeType(uint8_t scene_index, effect_type_id_t type);
void effects_service(void);
void effects_process(effect_io_t *io);
effect_type_id_t effects_activeType(void);
uint8_t effects_activeIoFlags(void);
const effects_common_runtime_t *effects_commonRuntime(void);

#endif /* EFFECTS_MANAGER_H_ */
