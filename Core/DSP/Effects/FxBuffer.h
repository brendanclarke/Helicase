/*
 * Core/DSP/Effects/FxBuffer.h
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

#ifndef FX_BUFFER_H_
#define FX_BUFFER_H_

#include <stdint.h>
#include "config.h"

/*
 * Shared DTCM audio arena: ownership, voice units, Effect share, handoff.
 *
 * What: FxBuffer is the single owner of the linker-defined .dtcm_fxbuf span
 * (every DTCM byte after the INDTCM/INDTCMZ statics, ~124 KiB). It grants
 * (a) up to twelve fixed 4,416-byte, 16-bit mono "voice units" to Instrument
 * slots, allocated from the TOP of the arena, at most two per slot, and
 * (b) one contiguous Effect share from the arena BOTTOM up to the lowest
 * claimed voice unit.
 *
 * Why: Phase 5 replaces separate fixed delay/advanced-voice buffers with one
 * elastic allotment (EFFECTS_BUS_FEATURE_PLAN.md §12). Keeping every size and
 * address decision here means Effect types and future buffer voices never
 * compute arena geometry themselves.
 *
 * What it deliberately does NOT do: it never reads, writes, or clears audio
 * bytes. There is no system-level clear (user rule A31/F6): owners clear what
 * they claim. Instead FxBuffer keeps a handoff record (fxbuf_handoff_t) that
 * describes how the arena was being used when the previous owner exited, so
 * the next owner can adopt or dispose of the contents.
 *
 * Context: foreground only (main loop, including Preset/Scene/Instrument
 * transactions and boot). Never call from an ISR. Not reentrant. Audio render
 * also runs in the foreground, so no locking is needed; share changes happen
 * between render blocks by construction.
 *
 * Affiliates: STM32F765VIHx_FLASH.ld (_sfxbuf/_efxbuf + ASSERTs),
 * startup_stm32f765xx.s (never walks the arena), main.c dsp_init()
 * (fxbuf_init), config.h DEV_FXBUF_FORCE_VOICE_UNITS, EffectsManager (Phase 5
 * step 4: share-change callback, handoff exit/entry), and Phase 7 buffer voices
 * (voice-unit acquire/release, handoff voice entries).
 */

/* Arena geometry --------------------------------------------------------- */

/* Byte alignment of the arena base, every voice unit, and the share size.
 * 32 bytes = one Cortex-M7 cache line; DTCM is uncached, but keeping line
 * alignment makes any future SRAM1 fallback placement safe. */
#define FXBUF_ALIGN_BYTES              32u

/* Approved minimum arena size (120 KiB). Mirrors the linker ASSERT in
 * STM32F765VIHx_FLASH.ld; fxbuf_init() re-checks at runtime because the
 * linker script cannot include this header. */
#define FXBUF_MIN_ARENA_BYTES          122880u

/* Voice units ------------------------------------------------------------ */

/* Instrument slots that may own units. Equal to INSTRUMENT_SLOT_COUNT;
 * FxBuffer.c static-asserts the match without making this header depend on
 * InstrumentManager. */
#define FXBUF_VOICE_SLOT_COUNT         6u
/* Total units in the arena and the per-slot cap (user decision A29). */
#define FXBUF_VOICE_UNIT_COUNT         12u
#define FXBUF_VOICE_UNITS_PER_SLOT     2u
/* One unit = 2,208 16-bit mono samples = 50.06 ms at 44,108 Hz = 4,416 B,
 * which is 138 cache lines (32-byte aligned). */
#define FXBUF_VOICE_UNIT_SAMPLES       2208u
#define FXBUF_VOICE_UNIT_BYTES         (FXBUF_VOICE_UNIT_SAMPLES * 2u)
/* Native codec rate used as the default voice-unit store rate. */
#define FXBUF_VOICE_RATE_HZ_DEFAULT    44108u
/* unit_owner value meaning "unit is free". */
#define FXBUF_UNIT_FREE                0xFFu

/* Handoff ---------------------------------------------------------------- */

/* 16 read and 16 write pointer entries: [0..11] voice units (index = unit
 * number), [12..15] up to four Effect pointers (user decision F6). */
#define FXBUF_EFFECT_POINTER_COUNT      4u
#define FXBUF_HANDOFF_POINTER_COUNT    (FXBUF_VOICE_UNIT_COUNT + FXBUF_EFFECT_POINTER_COUNT)
#define FXBUF_HANDOFF_EFFECT_POINTER_BASE FXBUF_VOICE_UNIT_COUNT
/* Offset value meaning "no pointer". Offsets are arena-relative bytes. */
#define FXBUF_OFFSET_NONE              0xFFFFFFFFu
/* Handoff effect_type meaning "no Effect / off". */
#define FXBUF_EFFECT_TYPE_NONE         0u
/* state_flags bits. All clear at boot because DTCM contents are undefined
 * after power-up. */
#define FXBUF_STATE_EFFECT_WRITTEN     0x01u
#define FXBUF_STATE_VOICE_WRITTEN      0x02u

/*
 * One contiguous Effect share.
 *
 * What: base pointer, arena-relative offset, and byte length of the region an
 * Effect type may use. Why: types must never derive arena geometry themselves;
 * they receive this on activation and on every share change. Invariants: base
 * is 32-byte aligned; bytes is a multiple of 32; bytes is 0 only if twelve
 * units consumed the whole arena, which the 120 KiB minimum makes impossible.
 */
typedef struct {
    uint8_t  *base;
    uint32_t offset;
    uint32_t bytes;
} fx_share_t;

/*
 * Arena handoff record between Scene switches and Effect type changes.
 *
 * What: a snapshot of how the arena was used when the previous Effect (and,
 * from Phase 7, voice buffer users) exited: the exited Effect's type, channel
 * count, bit depth and store rate; the share bounds and the twelve unit owners
 * at exit; each unit's store rate; and up to 16 read and 16 write positions
 * (arena-relative byte offsets: [0..11] voice units, [12..15] Effect).
 * Written-content flags say which regions hold real audio.
 *
 * Why: Phase 5 has no system-wide buffer disposal, even across a type change
 * (user decision F6). Buffers persist; the entering owner decides whether to
 * adopt or dispose of them. This record is the only information exchange for
 * that decision. Offsets instead of pointers keep it meaningful if the arena
 * moves between builds.
 */
typedef struct {
    uint32_t effect_share_offset;
    uint32_t effect_share_bytes;
    uint32_t read_offset[FXBUF_HANDOFF_POINTER_COUNT];
    uint32_t write_offset[FXBUF_HANDOFF_POINTER_COUNT];
    uint16_t effect_rate_hz;
    uint16_t unit_rate_hz[FXBUF_VOICE_UNIT_COUNT];
    uint16_t unit_written_mask;
    uint8_t  effect_type;
    uint8_t  effect_channels;
    uint8_t  effect_bits;
    uint8_t  state_flags;
    uint8_t  unit_owner_slot[FXBUF_VOICE_UNIT_COUNT];
} fxbuf_handoff_t;

/*
 * Share-change notification.
 *
 * What: called synchronously after an acquire/release changes the Effect
 * share. Why: the active Effect must re-seat its positions and re-clamp any
 * BUFFER_DEPENDENT parameters before the next render block. The callback must
 * not call back into acquire/release. It is registered by effects_init(); it
 * is NULL only before that call.
 */
typedef void (*fxbuf_share_changed_fn)(const fx_share_t *share);

/*
 * Initialise arena bookkeeping at boot.
 *
 * Inputs: linker symbols `_sfxbuf`/`_efxbuf`. Output: all units free, Effect
 * share = the whole arena, and handoff = "nothing valid". Arena bytes are
 * NOT touched. Under DEV_MODE_DIAGNOSTIC this also runs the allocation
 * self-test and then claims DEV_FXBUF_FORCE_VOICE_UNITS units. Must run
 * pre-audio, before Instrument runtime or an Effect is constructed.
 */
void fxbuf_init(void);

/*
 * Return the link-time size of the shared DTCM arena in bytes.
 *
 * Output: `_efxbuf - _sfxbuf`. Clients use this for diagnostics and capacity
 * reporting; Effect types must use fxbuf_effectShare() rather than deriving
 * their own geometry.
 */
uint32_t fxbuf_arenaBytes(void);

/*
 * Return the current contiguous Effect share.
 *
 * Input: non-NULL `out`. Output: base pointer, arena-relative offset zero,
 * and bytes from the arena bottom to the lowest claimed voice unit. A
 * released unit below a still-claimed unit does not return space to the share;
 * no compaction is performed because moving audio would corrupt it.
 */
void fxbuf_effectShare(fx_share_t *out);

/*
 * Claim voice units for one Instrument slot, all-or-nothing.
 *
 * Inputs: slot 0..5 and count 1..2. Output: 1 on success, 0 for invalid
 * arguments, a per-slot cap violation, or insufficient free units. Successful
 * claims use the lowest free unit indexes nearest the arena top and assign
 * the default handoff rate. Unit contents remain undefined; the owner clears
 * them if needed. Share changes notify the registered callback.
 */
uint8_t fxbuf_voiceAcquire(uint8_t slot, uint8_t count);

/*
 * Release every unit owned by an Instrument slot.
 *
 * Input: slot 0..5; other values are ignored. Output: units become free and
 * the share-change callback is notified if the contiguous share grows. Audio
 * contents are deliberately untouched and remain available to the handoff
 * snapshot until the next owner decides whether to adopt or clear them.
 */
void fxbuf_voiceRelease(uint8_t slot);

/* Return the number of units currently owned by a slot, or zero if invalid. */
uint8_t fxbuf_voiceUnitCount(uint8_t slot);

/* Return the total number of claimed units. */
uint8_t fxbuf_unitsInUse(void);

/*
 * Borrow one owned voice unit as an aligned sample array.
 *
 * Inputs: slot 0..5 and an ordinal in ascending owned-unit order. Output: a
 * pointer to FXBUF_VOICE_UNIT_SAMPLES 16-bit mono samples, aligned to
 * FXBUF_ALIGN_BYTES, or NULL if the slot does not own that ordinal. The
 * pointer remains valid until the slot releases its unit.
 */
int16_t *fxbuf_voiceUnit(uint8_t slot, uint8_t ordinal);

/* Register or clear the single foreground-only share-change callback. */
void fxbuf_setShareChangedCallback(fxbuf_share_changed_fn fn);

/*
 * Begin a handoff snapshot and return its writable record.
 *
 * Output: FxBuffer-owned share bounds, unit owners/rates, free-unit offsets,
 * and Effect fields are refreshed. Entries for still-owned voice units remain
 * available for their owner to describe; the exiting Effect writes its fields
 * and entries FXBUF_HANDOFF_EFFECT_POINTER_BASE..15 through this pointer.
 * The pointer is valid until the next begin call.
 */
fxbuf_handoff_t *fxbuf_handoffBeginExit(void);

/*
 * Record one owned voice unit's handoff state.
 *
 * Inputs: unit 0..11, arena-relative read/write offsets (or
 * FXBUF_OFFSET_NONE), store rate, and nonzero written flag. Free or invalid
 * units are ignored. The corresponding handoff written mask and aggregate
 * FXBUF_STATE_VOICE_WRITTEN flag are updated.
 */
void fxbuf_handoffSetVoiceUnit(uint8_t unit, uint32_t read_offset,
                               uint32_t write_offset, uint16_t rate_hz,
                               uint8_t written);

/*
 * Return the read-only handoff record for the entering owner.
 *
 * Output: the latest arena-relative snapshot. DTCM contents are undefined at
 * boot, so an adopter must require the appropriate state flag before reading
 * any audio state and otherwise clear the region it claims.
 */
const fxbuf_handoff_t *fxbuf_handoff(void);

#if DEV_MODE_DIAGNOSTIC
/* Return the diagnostic self-test result: zero is pass. */
uint8_t fxbuf_devSelfTestResult(void);
#endif

#endif /* FX_BUFFER_H_ */
