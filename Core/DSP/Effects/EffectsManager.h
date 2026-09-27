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
 * Samples are normalized floats where 1.0 is int16 full scale. r is NULL for
 * a mono block. frames is the current render-block length, and share is the
 * current FxBuffer allocation for any buffer-using type.
 */
typedef struct {
    float *l;
    float *r;
    uint8_t frames;
    uint8_t channels;
    const fx_share_t *share;
} effect_io_t;

/* Step 7 UI layout placeholder; registry rows remain NULL until then. */
struct effect_ui_hooks;
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
