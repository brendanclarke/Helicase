/*
 * InsertEffect.h
 *
 *  Created on: 26.10.2026
 * ----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * ----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * ----------------------------------------------------------------------------
 */

#ifndef INSERT_EFFECT_H_
#define INSERT_EFFECT_H_

#include <stdint.h>
#include <stddef.h>
#include "distortion.h"

#if defined(INSERT_HOST_TEST) && !defined(INSERT_HOST_SSAT_DEFINED)
/*
 * Host insert-test intrinsic boundary (S084 VX3 VX3-11.4).
 *
 * What:       replaces the target-only CMSIS saturating intrinsic after the
 *             real distortion contract has been included. Why: the registry
 *             type test runs the actual insert C files on the host while
 *             retaining the production Distortion layout and operation API.
 * Production builds never define INSERT_HOST_TEST.
 */
#define INSERT_HOST_SSAT_DEFINED 1
static inline int32_t insert_host_ssat(int32_t value, unsigned bits)
{
    const int32_t hi = (int32_t)((1u << (bits - 1u)) - 1u);
    const int32_t lo = -hi - 1;

    return value > hi ? hi : (value < lo ? lo : value);
}
#undef __SSAT
#define __SSAT(value, bits) insert_host_ssat((int32_t)(value), (bits))
#endif

/*
 * Insert effect type contract (S084 VX3 VX3-3.2).
 *
 * What: describes one Mix FX or transient buffer type without exposing the
 * manager, instrument descriptors or Menu. `units` is the required FxBuffer
 * reservation; `family` identifies same-use continuation; `placement` is
 * PRE/POST for transient types; flags select the common input gain or half-
 * rate policy. The operations own a bounded state record and process one
 * block in place, including a wet-only tail block. Why: the manager and
 * VoiceBuffer can remain type-agnostic. Inputs: resolved block controls and a
 * ring context. Outputs: processed samples and type state. Affiliates:
 * DelayLine.h, distortion.h, VoiceBuffer.c and InsertEffectsManager.c.
 */
#define INSERT_STATE_BYTES       48u
#define INSERT_FLAG_INPUT_GAIN   0x01u
#define INSERT_FLAG_HALF_RATE    0x02u
/*
 * Disabled type (S084 VX3 remediation §18.4).
 *
 * What: the type's measured per-sample loop exceeds its cost cap (Mix: the
 * D+S worst path, 33 instructions, +25 %; transient buffer: 22, +25 %) and
 * no obvious change brings it inside. The manager never selects it (Mix
 * falls back to D+S, a transient option releases the stage) and the menu
 * hides it. Stored kit values are kept, so re-enabling restores them.
 * Re-enable by removing the flag from the descriptor once the type fits.
 */
#define INSERT_FLAG_DISABLED     0x04u
#define INSERT_PLACE_NONE        0u
#define INSERT_PLACE_PRE         1u
#define INSERT_PLACE_POST        2u

/*
 * Per-block context supplied by VoiceBuffer (S084 VX3 VX3-3.2).
 *
 * What: carries the ring, write-position pointer, smoothed c0..c2 targets,
 * D+S's shaped rate, the engine distortion curve and one trigger pulse. Why:
 * types receive no slot or descriptor knowledge and therefore cannot allocate
 * or perform per-sample control resolution. Inputs: VoiceBuffer state and
 * InsertEffectsManager stage values. Outputs: the type's read-only context.
 */
typedef struct {
    int16_t        *ring;
    uint32_t        ring_len;
    uint32_t       *wp;
    float           c0;
    float           c1;
    float           c2;
    float           rate_shaped;
    const Distortion *drive;
    uint8_t         trigger;
} insert_block_ctx_t;

/*
 * Immutable type descriptor and operations (S084 VX3 VX3-3.2).
 *
 * What: names and control labels are used by the menu; init starts a fresh
 * record, retarget changes a same-family use, process transforms input, and
 * tail produces wet-only continuation. Inputs: state and block context.
 * Outputs: block samples/state. Each implementation asserts its state is no
 * larger than INSERT_STATE_BYTES and performs constant-cost work.
 */
typedef struct insert_effect_type {
    const char *name3;
    const char *label3[3];
    const char *label8[3];
    uint8_t units;
    uint8_t family;
    uint8_t placement;
    uint8_t flags;
    void (*init)(void *state, const insert_block_ctx_t *ctx);
    void (*retarget)(void *state, const insert_block_ctx_t *ctx,
                     uint8_t old_units);
    void (*process)(void *state, int16_t *buf, uint8_t size,
                    const insert_block_ctx_t *ctx);
    void (*tail)(void *state, int16_t *wet_out, uint8_t size,
                 const insert_block_ctx_t *ctx);
} insert_effect_type_t;

#endif /* INSERT_EFFECT_H_ */
