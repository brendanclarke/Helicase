/*
 * Core/DSP/Effects/EffectTypes.h
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

#ifndef EFFECT_TYPES_H_
#define EFFECT_TYPES_H_

#include <stdint.h>

/*
 * Effect data contract shared by SceneData, AutoSave, and the future
 * EffectsManager registry, storage, Menu, and sequencer.
 *
 * What: constants, enums, and the retained per-Scene Effect record layout,
 * with no runtime or module dependencies. Why a separate header: SceneData.h
 * embeds effect_record_t while EffectsManager.c will include SceneData.h;
 * keeping this contract data-only avoids a header cycle. Authority:
 * EFFECTS_BUS_FEATURE_PLAN.md sections 7, 10, and 11.
 */

/* Parameters ------------------------------------------------------------ */

/* Per-Effect parameter cells. Indices 0..2 are common to every type. */
#define EFFECT_PARAM_COUNT              64u
#define EFFECT_COMMON_PARAM_AUDIO_OUT    0u
#define EFFECT_COMMON_PARAM_LEVEL        1u
#define EFFECT_COMMON_PARAM_PAN          2u
#define EFFECT_COMMON_PARAM_COUNT        3u

/* Common defaults: St1 route, unity return, and centred pan. */
#define EFFECT_COMMON_DEFAULT_AUDIO_OUT  0u
#define EFFECT_COMMON_DEFAULT_LEVEL      127u
#define EFFECT_COMMON_DEFAULT_PAN        64u

/* Types: registry id 0 is permanently the valid `off` type. */
typedef uint8_t effect_type_id_t;
#define EFFECT_TYPE_OFF                  0u

/* FX sequencer ---------------------------------------------------------- */

#define EFFECT_SEQ_STEP_COUNT            16u
#define EFFECT_SEQ_LANE_COUNT            16u
#define EFFECT_SEQ_LANE_MORPH            0u
#define EFFECT_SEQ_LENGTH_MIN            1u
#define EFFECT_SEQ_LENGTH_MAX            16u
#define EFFECT_SEQ_LENGTH_DEFAULT        16u

/* Stored run modes; text tokens are introduced by the later file/UI steps. */
typedef enum {
    EFFECT_SEQ_RUN_FWD = 0,
    EFFECT_SEQ_RUN_REV,
    EFFECT_SEQ_RUN_PIP,
    EFFECT_SEQ_RUN_RND,
    EFFECT_SEQ_RUN_SEL,
    EFFECT_SEQ_RUN_MODE_COUNT
} effect_seq_run_mode_t;

/* Shared track/Effect step-scale index. Index 4 is the current 1/16 entry. */
#define EFFECT_SEQ_SCALE_COUNT           14u
#define EFFECT_SEQ_SCALE_DEFAULT         4u

/*
 * One FX-sequencer step.
 *
 * Bit L of lock_mask means lane L is locked. Values remain retained while a
 * lane is unlocked so a later lock restores the stored value. AutoSave emits
 * the mask little-endian followed by the 16 lane values: 18 bytes per step.
 */
typedef struct {
    uint16_t lock_mask;
    uint8_t value[EFFECT_SEQ_LANE_COUNT];
} effect_seq_step_t;

/*
 * Scene-retained Effect record.
 *
 * The record belongs solely to one Scene: Kit loads never alter it. It stores
 * the type, sequence settings, normal/Morph parameter images, and 16 steps.
 * Effect Morph amount is deliberately a separate Scene setting, not part of
 * this record or the future .fx file. SceneData owns all retained writes.
 */
typedef struct {
    effect_type_id_t type;
    uint8_t seq_run_mode;
    uint8_t seq_length;
    uint8_t seq_step_scale;
    uint8_t normal[EFFECT_PARAM_COUNT];
    uint8_t morph[EFFECT_PARAM_COUNT];
    effect_seq_step_t steps[EFFECT_SEQ_STEP_COUNT];
} effect_record_t;

_Static_assert(sizeof(effect_seq_step_t) == 18u,
               "FX sequence step is 16 values plus a 16-bit mask");
_Static_assert(sizeof(effect_record_t) == 420u,
               "effect_record_t size is recorded in SRAM_MANIFEST.md");

/* Automation / modulation target IDs (block 7) ------------------------ */

/*
 * Pattern targets use a 3-bit block plus a 6-bit local parameter. Blocks 0..5
 * are voices, block 6 is Scene, and block 7 is Effect. Local 63 aliases the
 * 9-bit automation-off sentinel, so Pattern automation accepts 0..62 while
 * future LFO/FX paths may still address local 63.
 */
#define EFFECT_TARGET_ID_BASE               448u
#define EFFECT_TARGET_ID_COUNT               64u
#define EFFECT_TARGET_PATTERN_LOCAL_LIMIT    63u

/* Return nonzero when an ID is inside the Effect block 448..511. */
static inline uint8_t effectTarget_isEffectId(uint16_t id)
{
    return (uint8_t)(id >= EFFECT_TARGET_ID_BASE &&
                     id < EFFECT_TARGET_ID_BASE + EFFECT_TARGET_ID_COUNT);
}

/* Decode the local Effect parameter index from a block-7 ID. */
static inline uint8_t effectTarget_local(uint16_t id)
{
    return (uint8_t)(id - EFFECT_TARGET_ID_BASE);
}

/* Encode a local Effect parameter index as its canonical block-7 ID. */
static inline uint16_t effectTarget_id(uint8_t local)
{
    return (uint16_t)(EFFECT_TARGET_ID_BASE + local);
}

#endif /* EFFECT_TYPES_H_ */
