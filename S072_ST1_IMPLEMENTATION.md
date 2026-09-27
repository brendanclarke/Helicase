# S072 Step 1 — Implementation Schedule

**Scope:** `EFFECTS_BUS_FEATURE_PLAN.md` §17.1 Step 1:

- `sine_table` moves to flash;
- the `.dtcm_fxbuf` arena section, with its ASSERTs;
- the `FxBuffer` API, handoff record, and dev hook;
- the SCOPING §5.5 flash-growth investigation.

**Status:** implementation complete for the source/build scope. Clean
production and diagnostic links passed; hardware listening/boot gates remain
pending because no hardware run was available in this session.

**Working notes (updated during implementation):**

- 2026-09-27: Confirmed the plan's approved Step 1 allocation before editing:
  the elastic `.dtcm_fxbuf` span is DTCM-reserved audio capacity; the fixed
  SRAM1 control state is 28 B for `fxbuf_state` plus 180 B for the handoff,
  with a 1 B self-test result only in `DEV_MODE_DIAGNOSTIC` builds.
- 2026-09-27: Moved `sine_table` out of `INCCM`, added the linker arena and
  startup exclusion comment, added the public `FxBuffer` API/implementation,
  the diagnostic knob/screen, Makefile integration, and link-budget reporter.
  All new public declarations and definitions carry adjacent contract
  comments; the implementation comments record the no-clear and handoff rules.
- 2026-09-27: The clean build's measured `_eflash_load`, `.dtcm`/`.dtcmz`,
  arena symbols/section flags, and packaged image size are recorded in §22;
  no linker-script correction was required by this toolchain.
- 2026-09-27: Clean production link passed with `text=458,112`, `data=416`,
  `bss=419,100`; the link-budget report measured 458,528 B flash load,
  32,992 B headroom, 4,084 B DTCM statics, and a 126,976 B arena at
  `0x20001000`. The 12-unit diagnostic link also passed; its flash load was
  460,172 B with 31,348 B headroom and the same arena geometry.

**Authority:** `EFFECTS_BUS_FEATURE_PLAN.md` §12 (buffer), §12.6 (handoff),
§16 (RAM), §17.1 (order), and the user decisions A29–A32, F6, G7.

**Line numbers** refer to the tree at commit `783ea4d` (branch
`dev-ph5-effects`, clean). Each entry names the anchor text as well, so the
edit can be found if lines shift.

**Measured baseline** (`build/lxr02.elf`, 2026-09-26 build, matching the
Session 071 figures):

| Quantity | Value |
|---|---|
| Flash load end `_eflash_load` | `0x080779CC`: 457,164 B used of 491,520 B (**34,356 B headroom**) |
| `.dtcm` | 8,708 B (`sine_table` 8,194 B at `0x20000000`, `squareRootLut` 512 B at `0x20002004`) |
| `.dtcmz` | 3,572 B (`0x20002204`–`0x20002FF8`) |
| DTCM free | 118,792 B (unallocated) |
| `.itcm` | 3,768 B of 16,384 B |

---

## 0. Change index

| # | File | Line(s) | Op | Summary |
|---|---|---|---|---|
| 1 | `Core/DSPAudio/wavetable.c` | 40–43 | modify | Drop `INCCM` from `sine_table`; replace the incorrect "Saw Table" comment |
| 2 | `Core/DSPAudio/wavetable.h` | 46 | modify | Document placement on the `sine_table` extern |
| 3 | `STM32F765VIHx_FLASH.ld` | 54–66 | modify | Header section list and arena description |
| 4 | `STM32F765VIHx_FLASH.ld` | 68–71 | modify | Startup-walk note includes the arena |
| 5 | `STM32F765VIHx_FLASH.ld` | after 141 | add | `.dtcm_fxbuf` NOLOAD section |
| 6 | `STM32F765VIHx_FLASH.ld` | 143–144 | modify | Replace "DTCM reserved in full" comment |
| 7 | `STM32F765VIHx_FLASH.ld` | after 160 | add | Arena ASSERTs |
| 8 | `Core/Src/startup_stm32f765xx.s` | after 127 | add | Comment: the arena is deliberately neither copied nor zeroed |
| 9 | `config.h` | after 220 | add | `DEV_FXBUF_FORCE_VOICE_UNITS` knob + guard |
| 10 | `Core/DSP/Effects/FxBuffer.h` | new | add | Arena, voice-unit, share, and handoff API |
| 11 | `Core/DSP/Effects/FxBuffer.c` | new | add | Implementation + dev self-test |
| 12 | `Makefile` | after 37 | add | `-ICore/DSP/Effects` |
| 13 | `Makefile` | after 105 | add | `Core/DSP/Effects/FxBuffer.c` in `SRCS` |
| 14 | `Makefile` | after 157 | add | Link-budget report after `size` |
| 15 | `tools/link_budget.py` | new | add | Flash/DTCM/arena/ITCM report |
| 16 | `main.c` | after 79 | add | `#include "FxBuffer.h"` |
| 17 | `main.c` | after 102 | add | `fxbuf_init()` in `dsp_init()` |
| 18 | `main.c` | after 257 | add | `boot_showFxBufDiagnostic()` (`DEV_MODE_DIAGNOSTIC` only) |
| 19 | `main.c` | after 466 | add | Call the diagnostic after `dsp_init()` |
| 20 | `knowledge_files/specification_reference/SRAM_MANIFEST.md` | ledger | modify | DTCM ledger, new owners |
| 21 | `knowledge_files/specification_reference/DEV_MODES.md` | diagnostic list | modify | `FxBf` screen and knob |
| 22 | `knowledge_files/specification_reference/MODULE_INTERCHANGE_SPEC.md` | module map | modify | FxBuffer boundary |

No change is required in any sine-table consumer: `Oscillator.c`
`calcSineBlock` (274), `calcSine` (299), `calcFmSineBlock` (321),
`calcFmSine` (345), or `lfo.c:78`. They index a `const int16_t[]` and are
placement-agnostic.

No MPU change is required. `clocks.c:184–197` configures only SRAM/DTCM
(region 0) and the DMA window (region 1), and leaves `PRIVDEFENA` set. Flash
therefore uses the default memory map (Normal, cacheable), which is exactly
how `sawTable`, `triTable`, and `recTable` are already read at audio rate.

---

## 1. `Core/DSPAudio/wavetable.c` — lines 40–43 (modify)

Before:

```c
//Saw Table
//128 Wavetables a 256 Samples
//Samplerate: 44000
INCCM const int16_t sine_table[TABLESIZE+1]=
```

After:

```c
/*
 * Full-cycle sine lookup table, 4096 points plus one guard sample.
 *
 * What: signed 16-bit sine samples indexed by the top 12 bits of an
 * oscillator or LFO phase accumulator. The extra final entry lets
 * interpolating readers fetch table[i+1] at i == TABLESIZE-1 without a wrap.
 *
 * Why it is in flash (Session 072, Effects Phase 5 step 1): this table used
 * to be INCCM (copied into DTCM at boot), occupying 8,194 of the 12,280 DTCM
 * bytes in use. Phase 5 dedicates the whole remainder of DTCM to the shared
 * FX/voice audio arena (Core/DSP/Effects/FxBuffer), so the table now lives in
 * .rodata like sawTable/triTable/recTable, which are already read from flash
 * at audio rate. Flash use is unchanged in practice: the former .dtcm load
 * image already stored these bytes in flash.
 *
 * Inputs: none (immutable). Output: sample values in -32767..32767.
 * Readers: Oscillator.c calcSineBlock()/calcSine()/calcFmSineBlock()/
 * calcFmSine() and lfo.c's sine waveform. Reads go through the Cortex-M7
 * D-cache (default memory map; no MPU region covers flash).
 * Affiliates: wavetable.h (extern + TABLESIZE), FxBuffer arena sizing in
 * STM32F765VIHx_FLASH.ld. Performance gate: the step-1 high-pitch sine stress
 * test in S072_ST1_IMPLEMENTATION.md §9.
 */
const int16_t sine_table[TABLESIZE+1]=
```

Line 38, `#include "config.h"`, stays: `wavetable.c` may still rely on other
`config.h` definitions, and removing an include is outside this step.

---

## 2. `Core/DSPAudio/wavetable.h` — line 46 (modify: add comment above)

After:

```c
/*
 * Sine lookup table (TABLESIZE+1 samples), resident in application flash.
 *
 * Placement contract: .rodata (flash), never INCCM/INDTCM. Session 072 moved
 * it out of DTCM so DTCM's remainder can be the FX/voice audio arena. Callers
 * must treat it as ordinary const data and must not take its address for DMA
 * or assume single-cycle access. Definition and full rationale: wavetable.c.
 */
extern const int16_t sine_table[TABLESIZE+1];
```

---

## 3. `STM32F765VIHx_FLASH.ld` — lines 54–66 (modify header comment)

Replace the `Sections:` list and the paragraph below it with:

```
 * Sections:
 *   .isr_vector  → FLASH @ origin
 *   .text/.rodata → FLASH                     — includes sine_table (S072)
 *   .itcm        → ITCM (loaded from FLASH)   — INITCM hot code
 *   .data        → SRAM1 (loaded from FLASH)
 *   .bss         → SRAM1
 *   .dtcm        → DTCM (loaded from FLASH)   — INDTCM data
 *   .dtcmz       → DTCM (NOLOAD, runtime-zero) — INDTCMZ uninitialised
 *   .dtcm_fxbuf  → DTCM (NOLOAD, never copied or zeroed) — FX/voice audio
 *                  arena: every DTCM byte after .dtcmz (S072, Phase 5)
 *   .devwdg_noinit → SRAM2 (NOLOAD, never zeroed) — DEV_LOGGING_IWDG capsule
 *
 * .dtcm is intentionally placed BEFORE .dtcmz in the DTCM region so any
 * accidental size growth in .dtcm shows up as a link error rather than
 * silently overlapping zero-init data. ASSERT below catches DTCM overflow.
 *
 * .dtcm_fxbuf is deliberately elastic: it starts at the first 32-byte
 * boundary after .dtcmz and ends at the end of DTCM, so every INDTCM/INDTCMZ
 * byte added in future shrinks the arena rather than failing the link. The
 * ASSERT below turns shrinkage past the approved minimum (120 KiB,
 * FXBUF_MIN_ARENA_BYTES in Core/DSP/Effects/FxBuffer.h) into a build error.
 * It MUST remain NOLOAD: `objcopy -O binary` emits every loadable section, and
 * a loadable DTCM section would stretch lxr02.bin from 0x08008000 up to
 * 0x2000xxxx.
```

---

## 4. `STM32F765VIHx_FLASH.ld` — lines 68–71 (modify)

Before: `.devwdg_noinit is deliberately NOT referenced by Reset_Handler's ...`
(four lines).

After:

```
 * .devwdg_noinit and .dtcm_fxbuf are deliberately NOT referenced by
 * Reset_Handler's LoopCopyDataInit/LoopFillZerobss
 * (Core/Src/startup_stm32f765xx.s), which only walk _sdata.._edata,
 * _sbss.._ebss, _sdtcm.._edtcm, and _sdtcmz.._edtcmz. For .dtcm_fxbuf this
 * is the Phase 5 "no system-level buffer clear" rule: each arena owner clears
 * what it claims, using FxBuffer's handoff state_flags (boot = nothing valid).
```

The rest of the existing `.devwdg_noinit` paragraph (warm-reset survival,
lines 71–75) is unchanged.

---

## 5. `STM32F765VIHx_FLASH.ld` — insert after line 141 (`} >DTCM` closing `.dtcmz`)

```
    /* FX/voice audio arena — the whole remainder of DTCM (Session 072).
    **
    ** What: a NOLOAD, never-initialised DTCM span from the first 32-byte
    ** boundary after .dtcmz to the end of DTCM. Core/DSP/Effects/FxBuffer.c
    ** is its only owner; it hands out one contiguous Effect share (bottom)
    ** and up to twelve 4,416-byte voice units (top).
    ** Why: Effects Phase 5 reserves free DTCM exclusively for delay-line/audio
    ** buffers (RAM Allocation Approval Policy); DTCM is single-cycle,
    ** uncached, and never touched by DMA.
    ** Inputs: _edtcmz (end of INDTCMZ statics). Outputs: _sfxbuf/_efxbuf,
    ** read by FxBuffer.c as extern uint8_t arrays.
    ** The size is computed outside the section so the arithmetic is absolute.
    ** Affiliates: FxBuffer.h FXBUF_MIN_ARENA_BYTES (runtime mirror of the
    ** ASSERT below), tools/link_budget.py (reports the size),
    ** startup_stm32f765xx.s (never walks this span). */
    _fxbuf_start = ALIGN(_edtcmz, 32);
    _fxbuf_size  = ORIGIN(DTCM) + LENGTH(DTCM) - _fxbuf_start;
    .dtcm_fxbuf _fxbuf_start (NOLOAD) : {
        _sfxbuf = .;
        . = . + _fxbuf_size;
        _efxbuf = .;
    } >DTCM
```

**Expected link result:** `_sfxbuf = 0x20001000`, `_efxbuf = 0x20020000`,
size `0x1F000` (126,976 B). Arithmetic: `.dtcm` 512 B ends at `0x20000200`;
`.dtcmz` 3,572 B ends at `0x20000FF4`; aligned to 32 gives `0x20001000`.

**Fallback** if this binutils version rejects the address-expression form: use
the symbol-only form below. It is purely absolute and needs no section, but it
loses the `size -A` and map visibility.

```
    _sfxbuf = ALIGN(_edtcmz, 32);
    _efxbuf = ORIGIN(DTCM) + LENGTH(DTCM);
```

**Reporting side effect:** a NOLOAD section is NOBITS, so the Berkeley
`arm-none-eabi-size` column `bss` grows by about 126,976 B (from ~291,900 to
~410,000). This is not new SRAM1 use. `SRAM_MANIFEST.md` must cite per-section
sizes (§20); `tools/link_budget.py` reports the arena separately.

---

## 6. `STM32F765VIHx_FLASH.ld` — lines 143–144 (modify)

Before:

```
    /* No ._stack reservation here — main stack lives at _estack (top of
    ** SRAM1). DTCM is reserved in full for INDTCM/INDTCMZ DSP data. */
```

After:

```
    /* No ._stack reservation here — main stack lives at _estack. DTCM holds
    ** INDTCM/INDTCMZ DSP statics followed by the .dtcm_fxbuf audio arena,
    ** which takes every remaining DTCM byte. */
```

The existing "(top of SRAM1)" wording is inaccurate (`0x20080000` is the top
of SRAM2). It is only dropped from the line being edited. Line 48 carries the
same wording and is **not** changed; see §11 "Observed, not changed".

---

## 7. `STM32F765VIHx_FLASH.ld` — insert after line 160 (after the existing DTCM-overflow ASSERT)

```
    /* FX arena guards (Session 072).
    ** The minimum is the user-approved 120 KiB (0x1E000): twelve 4,416-byte
    ** voice units plus at least 64 KiB of Effect share, with margin. Keep in
    ** sync with FXBUF_MIN_ARENA_BYTES in Core/DSP/Effects/FxBuffer.h; the
    ** linker cannot read C headers, so FxBuffer.c re-checks at runtime. */
    ASSERT(_efxbuf <= ORIGIN(DTCM) + LENGTH(DTCM),
           "FX arena overflows DTCM")
    ASSERT((_sfxbuf & 31) == 0,
           "FX arena base is not 32-byte aligned")
    ASSERT(_efxbuf - _sfxbuf >= 0x1E000,
           "FX arena below the approved 120 KiB minimum: DTCM statics grew; resize or re-approve")
```

The existing ASSERTs at 159–164 are unchanged. The flash guards are discussed
in §10.

---

## 8. `Core/Src/startup_stm32f765xx.s` — insert after line 127 (`bcc FillZeroDtcmz`), before `bl main`

```
  /* .dtcm_fxbuf (FX/voice audio arena, _sfxbuf.._efxbuf) is deliberately
  ** neither copied nor zeroed here. Phase 5 rule: there is no system-level
  ** buffer clear; each arena owner (Effect type or voice buffer user) clears
  ** what it claims, guided by FxBuffer's handoff record, whose state_flags
  ** start at 0 ("nothing valid") on every boot. Zeroing ~124 KiB here would
  ** also add boot latency for no consumer. See STM32F765VIHx_FLASH.ld and
  ** Core/DSP/Effects/FxBuffer.h. */
```

This is a comment only; the instruction stream is unchanged.

---

## 9. `config.h` — insert after line 220 (`#define DEV_LOGGING_IWDG_EXPIRE 120000u`)

```c
/*
 * DEV_FXBUF_FORCE_VOICE_UNITS — screen-diagnostic test knob, default 0.
 *
 * What: when DEV_MODE_DIAGNOSTIC is 1, fxbuf_init() claims this many 4,416-
 * byte voice units at boot (two per slot, slots 0..5 in order) so the Effect
 * share can be exercised at its minimum size before any real voice buffer
 * user exists. Values 0..12. Ignored entirely when DEV_MODE_DIAGNOSTIC is 0,
 * so production can never lose arena to a stale test setting.
 *
 * Why: Phase 5 guarantees every Effect type works across the whole share
 * range (full arena down to arena - 12 units). Until Phase 7 voice types
 * allocate units, this is the only way to present the minimum share.
 *
 * Inputs: this constant. Outputs: fxbuf_unitsInUse()/fxbuf_effectShare()
 * report the reduced share; the FxBf boot diagnostic shows it. Affiliates:
 * Core/DSP/Effects/FxBuffer.c fxbuf_init(), main.c
 * boot_showFxBufDiagnostic(), DEV_MODES.md. Units forced here are owned by
 * the named slots; once Phase 7 voice buffer users exist, a nonzero value
 * will collide with them — keep 0 outside deliberate share tests.
 */
#define DEV_FXBUF_FORCE_VOICE_UNITS 0u
#if (DEV_FXBUF_FORCE_VOICE_UNITS > 12u)
#error "DEV_FXBUF_FORCE_VOICE_UNITS must be 0..12 (FXBUF_VOICE_UNIT_COUNT)"
#endif
```

This is a numeric knob under the existing `DEV_MODE_DIAGNOSTIC` mode, like
`DEV_LOGGING_IWDG_EXPIRE`, not a third development mode. It respects the
`DEV_MODES.md` two-mode policy.

---

## 10. New file `Core/DSP/Effects/FxBuffer.h`

```c
/*
 * Core/DSP/Effects/FxBuffer.h
 *
 *  Created on: 27.09.2026
 * ------------------------------------------------------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 *  (standard LXR02 Open-Source licence header, copied verbatim from sample_mix.h)
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
 * step 4: share-change callback, handoff exit/entry), Phase 7 buffer voices
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
#define FXBUF_EFFECT_POINTER_COUNT     4u
#define FXBUF_HANDOFF_POINTER_COUNT    (FXBUF_VOICE_UNIT_COUNT + FXBUF_EFFECT_POINTER_COUNT)
#define FXBUF_HANDOFF_EFFECT_POINTER_BASE FXBUF_VOICE_UNIT_COUNT
/* Offset value meaning "no pointer". Offsets are arena-relative bytes. */
#define FXBUF_OFFSET_NONE              0xFFFFFFFFu
/* Handoff effect_type meaning "no Effect / off". EffectsManager (step 4)
 * static-asserts that its EFFECT_TYPE_OFF registry id equals this value. */
#define FXBUF_EFFECT_TYPE_NONE         0u
/* state_flags bits (user-accepted meaning, G7). All clear at boot because
 * DTCM contents are undefined after power-up. */
#define FXBUF_STATE_EFFECT_WRITTEN     0x01u  /* Effect share holds audio the exiting Effect wrote */
#define FXBUF_STATE_VOICE_WRITTEN      0x02u  /* at least one voice unit holds written audio      */

/*
 * One contiguous Effect share.
 *
 * What: base pointer, arena-relative offset, and byte length of the region an
 * Effect type may use. Why: types must never derive arena geometry
 * themselves; they receive this on activation and on every share change.
 * Invariants: base is 32-byte aligned; bytes is a multiple of 32; bytes is 0
 * only if twelve units consumed the whole arena, which the 120 KiB minimum
 * makes impossible. Producers: fxbuf_effectShare(), share-change callback.
 * Consumers: EffectsManager and Effect type buffer_changed()/init() (step 4+).
 */
typedef struct {
    uint8_t  *base;
    uint32_t  offset;
    uint32_t  bytes;
} fx_share_t;

/*
 * Arena handoff record between Scene switches and Effect type changes.
 *
 * What: a snapshot of how the arena was used when the previous Effect (and,
 * from Phase 7, voice buffer users) exited: the exited Effect's type, channel
 * count, bit depth and store rate; the share bounds and the twelve unit
 * owners at exit; each unit's store rate; and up to 16 read and 16 write
 * positions (arena-relative byte offsets: [0..11] voice units, [12..15]
 * Effect). Written-content flags say which regions hold real audio.
 *
 * Why: Phase 5 has no system-wide buffer disposal, even across a type change
 * (user decision F6). Buffers persist; the entering owner decides whether to
 * adopt (e.g. keep a delay tail) or dispose (clear its own region). This
 * record is the only information exchange for that decision.
 *
 * Inputs/writers: FxBuffer fills allocation fields and resets Effect fields
 * in fxbuf_handoffBeginExit(); the exiting Effect fills Effect fields and
 * entries [12..15] through the returned pointer (EffectsManager step 4
 * export_handoff); voice owners fill entries [0..11] via
 * fxbuf_handoffSetVoiceUnit(). Readers: the entering Effect type's init()
 * and future voice types, through fxbuf_handoff().
 *
 * Offsets instead of pointers keep the record meaningful if the arena base
 * moves between builds. Field order avoids padding; FxBuffer.c static-asserts
 * the 180-byte size recorded in SRAM_MANIFEST.md.
 */
typedef struct {
    uint32_t effect_share_offset;                         /* share bounds on exit          */
    uint32_t effect_share_bytes;
    uint32_t read_offset[FXBUF_HANDOFF_POINTER_COUNT];    /* FXBUF_OFFSET_NONE = unused    */
    uint32_t write_offset[FXBUF_HANDOFF_POINTER_COUNT];
    uint16_t effect_rate_hz;                              /* exited Effect store rate      */
    uint16_t unit_rate_hz[FXBUF_VOICE_UNIT_COUNT];        /* 0 when free                   */
    uint16_t unit_written_mask;                           /* bit u: unit u holds audio     */
    uint8_t  effect_type;                                 /* FXBUF_EFFECT_TYPE_NONE = off  */
    uint8_t  effect_channels;                             /* 1 mono, 2 stereo, 0 none      */
    uint8_t  effect_bits;                                 /* 8, 16, or 0 none              */
    uint8_t  state_flags;                                 /* FXBUF_STATE_*                 */
    uint8_t  unit_owner_slot[FXBUF_VOICE_UNIT_COUNT];     /* FXBUF_UNIT_FREE = free        */
} fxbuf_handoff_t;

/*
 * Share-change notification.
 *
 * What: called synchronously after any acquire/release that changes the
 * Effect share, with the new share. Why: the active Effect must re-seat its
 * read/write positions and re-clamp BUFFER_DEPENDENT parameters (plan §12.5)
 * before the next render block. Registrant: EffectsManager (step 4); NULL
 * until then. The callback must not call back into acquire/release.
 */
typedef void (*fxbuf_share_changed_fn)(const fx_share_t *share);

/*
 * Initialise arena bookkeeping at boot.
 *
 * Inputs: linker symbols _sfxbuf/_efxbuf. Output: all units free, share =
 * whole arena, handoff = "nothing valid" (state_flags 0, offsets NONE,
 * owners FREE). Arena bytes are NOT touched. Under DEV_MODE_DIAGNOSTIC it
 * also runs fxbuf_selfTest() on the empty table and then claims
 * DEV_FXBUF_FORCE_VOICE_UNITS units. Must run pre-audio, before any Instrument
 * runtime or Effect is constructed. Client: main.c dsp_init().
 */
void fxbuf_init(void);

/*
 * Arena size in bytes (link-time remainder of DTCM). Output: _efxbuf -
 * _sfxbuf. Clients: diagnostics, link-budget cross-checks, EffectsManager.
 */
uint32_t fxbuf_arenaBytes(void);

/*
 * Current Effect share.
 *
 * Inputs: out (non-NULL). Output: *out = {arena base, 0, arena bytes minus
 * (highest claimed unit index + 1) x unit bytes}. Units are packed from the
 * top by lowest-free-index, so a released unit below a still-claimed one does
 * not return space to the share until the higher unit is released (no
 * compaction: moving a voice's audio would corrupt it). Clients:
 * EffectsManager activation, diagnostics.
 */
void fxbuf_effectShare(fx_share_t *out);

/*
 * Claim voice units for one Instrument slot (all-or-nothing).
 *
 * Inputs: slot 0..5; count 1..2. Output: 1 on success, 0 on refusal
 * (bad args, per-slot cap FXBUF_VOICE_UNITS_PER_SLOT exceeded, or not enough
 * free units). On success the lowest-index free units (nearest the top) are
 * assigned, their handoff rate defaults to FXBUF_VOICE_RATE_HZ_DEFAULT, and
 * the share-change callback fires if the share shrank. Unit contents are
 * undefined; the caller clears if it needs silence. Clients: Phase 7 buffer
 * voice types inside Instrument/Scene transactions; the dev knob at boot.
 */
uint8_t fxbuf_voiceAcquire(uint8_t slot, uint8_t count);

/*
 * Release every unit owned by one slot.
 *
 * Inputs: slot 0..5 (others ignored). Output: units marked free; contents
 * untouched (they persist for the handoff rule); callback fires if the share
 * grew. Clients: Phase 7 Instrument replacement / Scene activation.
 */
void fxbuf_voiceRelease(uint8_t slot);

/* Units currently owned by one slot (0..2); 0 for invalid slots. */
uint8_t fxbuf_voiceUnitCount(uint8_t slot);

/* Total units claimed across all slots (0..12). Diagnostics and tests. */
uint8_t fxbuf_unitsInUse(void);

/*
 * Borrow the sample memory of one owned unit.
 *
 * Inputs: slot 0..5; ordinal 0..(count-1) in ascending unit order. Output:
 * pointer to FXBUF_VOICE_UNIT_SAMPLES int16 samples, 32-byte aligned, or NULL
 * if the slot does not own that many units. Valid until the slot releases.
 */
int16_t *fxbuf_voiceUnit(uint8_t slot, uint8_t ordinal);

/*
 * Register (or clear with NULL) the single share-change callback.
 * Client: EffectsManager init (step 4). Last registration wins.
 */
void fxbuf_setShareChangedCallback(fxbuf_share_changed_fn fn);

/*
 * Begin an exit snapshot and return the writable handoff record.
 *
 * What: refreshes FxBuffer-owned fields (share bounds, unit owners, unit
 * rates of free units reset to 0, free units' offsets set NONE and written
 * bits cleared), resets every Effect field (type NONE, channels/bits/rate 0,
 * entries [12..15] NONE, FXBUF_STATE_EFFECT_WRITTEN cleared), recomputes
 * FXBUF_STATE_VOICE_WRITTEN from unit_written_mask, and returns the record so
 * the exiting Effect can describe itself. Voice entries of still-owned units
 * are preserved. Clients: EffectsManager at Scene switch and Effect type
 * change (step 4). Output pointer is valid until the next call.
 */
fxbuf_handoff_t *fxbuf_handoffBeginExit(void);

/*
 * Record one voice unit's positions/rate/written state in the handoff.
 *
 * Inputs: unit 0..11 (must be owned; free units are ignored), arena-relative
 * read/write offsets (or FXBUF_OFFSET_NONE), store rate in Hz, written flag.
 * Output: handoff entries [unit] and unit_written_mask/state_flags updated.
 * Clients: Phase 7 buffer voices when they exit or change rate.
 */
void fxbuf_handoffSetVoiceUnit(uint8_t unit, uint32_t read_offset,
                               uint32_t write_offset, uint16_t rate_hz,
                               uint8_t written);

/*
 * Read-only view of the current handoff record for the entering owner.
 * Clients: Effect type init(rt, handoff) via EffectsManager; Phase 7 voices.
 */
const fxbuf_handoff_t *fxbuf_handoff(void);

#if DEV_MODE_DIAGNOSTIC
/*
 * Boot self-test result (DEV_MODE_DIAGNOSTIC only).
 *
 * Output: 0 = pass; otherwise the number of the first failing check
 * (FxBuffer.c fxbuf_selfTest() table). Recorded by fxbuf_init() before the
 * dev knob claims units. Client: main.c boot_showFxBufDiagnostic().
 */
uint8_t fxbuf_devSelfTestResult(void);
#endif

#endif /* FX_BUFFER_H_ */
```

---

## 11. New file `Core/DSP/Effects/FxBuffer.c`

```c
/*
 * Core/DSP/Effects/FxBuffer.c
 *
 *  Created on: 27.09.2026
 *  (standard LXR02 Open-Source licence header, copied verbatim from sample_mix.h)
 */

#include "FxBuffer.h"
#include "InstrumentManager.h"
#include <stddef.h>
#include <string.h>

/*
 * Linker-defined arena bounds (STM32F765VIHx_FLASH.ld .dtcm_fxbuf).
 * Declared as arrays so their addresses are the symbol values; never
 * dereferenced as objects.
 */
extern uint8_t _sfxbuf[];
extern uint8_t _efxbuf[];

_Static_assert(FXBUF_VOICE_SLOT_COUNT == INSTRUMENT_SLOT_COUNT,
               "FxBuffer slot count must match InstrumentManager");
_Static_assert((FXBUF_VOICE_UNIT_BYTES % FXBUF_ALIGN_BYTES) == 0u,
               "voice unit must be a whole number of cache lines");
_Static_assert(FXBUF_VOICE_UNIT_COUNT <= 16u,
               "unit_written_mask is 16 bits");
_Static_assert(FXBUF_VOICE_UNIT_COUNT ==
                   FXBUF_VOICE_SLOT_COUNT * FXBUF_VOICE_UNITS_PER_SLOT,
               "12 units = 6 slots x 2 (user decision A29)");
_Static_assert(sizeof(fxbuf_handoff_t) == 180u,
               "handoff size is recorded in SRAM_MANIFEST.md; update both");

/*
 * FxBuffer resident bookkeeping (SRAM1 .bss, 28 B incl. padding).
 *
 * What: arena base/size captured once from the linker, the owner slot of
 * each unit, the claimed-unit count, and the one share-change callback.
 * Why SRAM1 not DTCM: touched only on acquire/release/exit (control rate),
 * and every DTCM byte left over belongs to the arena itself.
 * Accessors: every function in this file; nothing else may write it.
 */
typedef struct {
    uint8_t *base;
    uint32_t bytes;
    fxbuf_share_changed_fn on_share_changed;
    uint8_t  unit_owner[FXBUF_VOICE_UNIT_COUNT];
    uint8_t  units_in_use;
} fxbuf_state_t;

static fxbuf_state_t fxbuf_state;

/*
 * Arena handoff record (SRAM1 .bss, 180 B; approved G7).
 * Written by fxbuf_handoffBeginExit()/fxbuf_handoffSetVoiceUnit() and the
 * exiting Effect through the pointer those return; read via fxbuf_handoff().
 */
static fxbuf_handoff_t fxbuf_handoffRecord;

#if DEV_MODE_DIAGNOSTIC
/* Boot self-test outcome (1 B, DEV_MODE_DIAGNOSTIC builds only). */
static uint8_t fxbuf_selfTestResult;
#endif

/*
 * Byte offset of unit u from the arena base.
 *
 * Unit 0 is the topmost unit; unit u occupies
 * [bytes - (u+1)*UNIT, bytes - u*UNIT). Packing from the top keeps the
 * Effect share one contiguous region starting at the base.
 */
static uint32_t fxbuf_unitOffset(uint8_t unit)
{
    return fxbuf_state.bytes -
           ((uint32_t)unit + 1u) * FXBUF_VOICE_UNIT_BYTES;
}

/*
 * Effect share length for the current owner table.
 *
 * Output: arena bytes minus every byte from the lowest claimed unit's
 * start to the top, i.e. bytes - (highest claimed index + 1) * UNIT; the
 * whole arena when no unit is claimed. Always a multiple of 32.
 */
static uint32_t fxbuf_shareBytes(void)
{
    int8_t u;

    for (u = (int8_t)(FXBUF_VOICE_UNIT_COUNT - 1u); u >= 0; u--) {
        if (fxbuf_state.unit_owner[(uint8_t)u] != FXBUF_UNIT_FREE)
            return fxbuf_unitOffset((uint8_t)u);
    }
    return fxbuf_state.bytes;
}

/*
 * Notify the registered owner when the share size actually changed.
 * Inputs: share bytes before the mutation. No call when unchanged or when
 * no callback is registered (always the case in step 1).
 */
static void fxbuf_notifyIfShareChanged(uint32_t before_bytes)
{
    fx_share_t share;

    if (!fxbuf_state.on_share_changed)
        return;
    fxbuf_effectShare(&share);
    if (share.bytes != before_bytes)
        fxbuf_state.on_share_changed(&share);
}

/*
 * Reset the handoff record to "nothing valid".
 * Used at boot: DTCM contents are undefined after power-up, so no region
 * may be advertised as holding audio.
 */
static void fxbuf_handoffResetAll(void)
{
    uint8_t i;

    memset(&fxbuf_handoffRecord, 0, sizeof(fxbuf_handoffRecord));
    for (i = 0u; i < FXBUF_HANDOFF_POINTER_COUNT; i++) {
        fxbuf_handoffRecord.read_offset[i] = FXBUF_OFFSET_NONE;
        fxbuf_handoffRecord.write_offset[i] = FXBUF_OFFSET_NONE;
    }
    for (i = 0u; i < FXBUF_VOICE_UNIT_COUNT; i++)
        fxbuf_handoffRecord.unit_owner_slot[i] = FXBUF_UNIT_FREE;
    fxbuf_handoffRecord.effect_type = FXBUF_EFFECT_TYPE_NONE;
    fxbuf_handoffRecord.effect_share_bytes = fxbuf_state.bytes;
}

/* Reset the owner table to all-free without touching the callback. */
static void fxbuf_clearOwners(void)
{
    memset(fxbuf_state.unit_owner, FXBUF_UNIT_FREE,
           sizeof(fxbuf_state.unit_owner));
    fxbuf_state.units_in_use = 0u;
}

#if DEV_MODE_DIAGNOSTIC
/*
 * Allocation-logic self-test on an empty table (DEV_MODE_DIAGNOSTIC only).
 *
 * What: exercises caps, packing, share arithmetic, release behaviour, and
 * unit pointer bounds purely in bookkeeping; never reads or writes arena
 * bytes. Why: step 1 has no real buffer user, so this is the only runtime
 * evidence that the geometry matches the linker result on hardware.
 * Output: 0 pass, else first failing check number:
 *   1 arena size/alignment   2 empty share == arena   3 two-unit acquire
 *   4 per-slot cap refusal    5 fill all 12 units     6 full refusal
 *   7 release keeps packing   8 full release restores 9 unit pointer bounds
 * Leaves the table all-free. The callback is not invoked (none registered
 * at boot, and it is temporarily cleared for safety).
 */
static uint8_t fxbuf_selfTest(void)
{
    const uint32_t unit = FXBUF_VOICE_UNIT_BYTES;
    fxbuf_share_changed_fn saved = fxbuf_state.on_share_changed;
    fx_share_t share;
    uint8_t result = 0u;
    uint8_t s;

    fxbuf_state.on_share_changed = NULL;
    fxbuf_clearOwners();

    if (fxbuf_state.bytes < FXBUF_MIN_ARENA_BYTES ||
        (((uintptr_t)fxbuf_state.base) & (FXBUF_ALIGN_BYTES - 1u)) != 0u ||
        (fxbuf_state.bytes & (FXBUF_ALIGN_BYTES - 1u)) != 0u) {
        result = 1u; goto done;
    }
    fxbuf_effectShare(&share);
    if (share.bytes != fxbuf_state.bytes || share.base != fxbuf_state.base) {
        result = 2u; goto done;
    }
    if (!fxbuf_voiceAcquire(0u, 2u) ||
        fxbuf_shareBytes() != fxbuf_state.bytes - 2u * unit) {
        result = 3u; goto done;
    }
    if (fxbuf_voiceAcquire(0u, 1u)) {
        result = 4u; goto done;
    }
    for (s = 1u; s < FXBUF_VOICE_SLOT_COUNT; s++) {
        if (!fxbuf_voiceAcquire(s, 2u)) { result = 5u; goto done; }
    }
    if (fxbuf_unitsInUse() != FXBUF_VOICE_UNIT_COUNT ||
        fxbuf_shareBytes() != fxbuf_state.bytes -
                              FXBUF_VOICE_UNIT_COUNT * unit) {
        result = 5u; goto done;
    }
    fxbuf_voiceRelease(1u);
    if (!fxbuf_voiceAcquire(1u, 2u) || fxbuf_voiceAcquire(1u, 1u)) {
        result = 6u; goto done;
    }
    fxbuf_voiceRelease(0u);   /* units 0,1 (top) free; unit 11 still held */
    if (fxbuf_shareBytes() != fxbuf_state.bytes -
                              FXBUF_VOICE_UNIT_COUNT * unit) {
        result = 7u; goto done;
    }
    for (s = 0u; s < FXBUF_VOICE_SLOT_COUNT; s++)
        fxbuf_voiceRelease(s);
    if (fxbuf_unitsInUse() != 0u || fxbuf_shareBytes() != fxbuf_state.bytes) {
        result = 8u; goto done;
    }
    if (!fxbuf_voiceAcquire(5u, 2u)) { result = 9u; goto done; }
    for (s = 0u; s < 2u; s++) {
        uint8_t *p = (uint8_t *)fxbuf_voiceUnit(5u, s);
        if (!p || p < fxbuf_state.base ||
            p + unit > fxbuf_state.base + fxbuf_state.bytes ||
            (((uintptr_t)p) & (FXBUF_ALIGN_BYTES - 1u)) != 0u) {
            result = 9u; goto done;
        }
    }

done:
    fxbuf_clearOwners();
    fxbuf_state.on_share_changed = saved;
    return result;
}
#endif

void fxbuf_init(void)
{
    fxbuf_state.base = _sfxbuf;
    fxbuf_state.bytes = (uint32_t)(_efxbuf - _sfxbuf);
    fxbuf_state.on_share_changed = NULL;
    fxbuf_clearOwners();

#if DEV_MODE_DIAGNOSTIC
    fxbuf_selfTestResult = fxbuf_selfTest();
    {
        uint8_t n;
        for (n = 0u; n < (uint8_t)DEV_FXBUF_FORCE_VOICE_UNITS; n++)
            (void)fxbuf_voiceAcquire((uint8_t)(n / FXBUF_VOICE_UNITS_PER_SLOT), 1u);
    }
#endif
    /* Handoff reflects the post-boot allocation and advertises no content. */
    fxbuf_handoffResetAll();
    (void)fxbuf_handoffBeginExit();
}

uint32_t fxbuf_arenaBytes(void)
{
    return fxbuf_state.bytes;
}

void fxbuf_effectShare(fx_share_t *out)
{
    if (!out)
        return;
    out->base = fxbuf_state.base;
    out->offset = 0u;
    out->bytes = fxbuf_shareBytes();
}

uint8_t fxbuf_voiceAcquire(uint8_t slot, uint8_t count)
{
    uint32_t before;
    uint8_t owned;
    uint8_t u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT || count == 0u ||
        count > FXBUF_VOICE_UNITS_PER_SLOT)
        return 0u;
    owned = fxbuf_voiceUnitCount(slot);
    if ((uint8_t)(owned + count) > FXBUF_VOICE_UNITS_PER_SLOT ||
        (uint8_t)(fxbuf_state.units_in_use + count) > FXBUF_VOICE_UNIT_COUNT)
        return 0u;

    before = fxbuf_shareBytes();
    for (u = 0u; u < FXBUF_VOICE_UNIT_COUNT && count; u++) {
        if (fxbuf_state.unit_owner[u] == FXBUF_UNIT_FREE) {
            fxbuf_state.unit_owner[u] = slot;
            fxbuf_state.units_in_use++;
            fxbuf_handoffRecord.unit_rate_hz[u] = FXBUF_VOICE_RATE_HZ_DEFAULT;
            count--;
        }
    }
    fxbuf_notifyIfShareChanged(before);
    return 1u;
}

void fxbuf_voiceRelease(uint8_t slot)
{
    uint32_t before;
    uint8_t u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return;
    before = fxbuf_shareBytes();
    for (u = 0u; u < FXBUF_VOICE_UNIT_COUNT; u++) {
        if (fxbuf_state.unit_owner[u] == slot) {
            fxbuf_state.unit_owner[u] = FXBUF_UNIT_FREE;
            fxbuf_state.units_in_use--;
        }
    }
    fxbuf_notifyIfShareChanged(before);
}

uint8_t fxbuf_voiceUnitCount(uint8_t slot)
{
    uint8_t n = 0u;
    uint8_t u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return 0u;
    for (u = 0u; u < FXBUF_VOICE_UNIT_COUNT; u++)
        if (fxbuf_state.unit_owner[u] == slot)
            n++;
    return n;
}

uint8_t fxbuf_unitsInUse(void)
{
    return fxbuf_state.units_in_use;
}

int16_t *fxbuf_voiceUnit(uint8_t slot, uint8_t ordinal)
{
    uint8_t u;

    if (slot >= FXBUF_VOICE_SLOT_COUNT)
        return NULL;
    for (u = 0u; u < FXBUF_VOICE_UNIT_COUNT; u++) {
        if (fxbuf_state.unit_owner[u] != slot)
            continue;
        if (ordinal == 0u)
            return (int16_t *)(void *)(fxbuf_state.base + fxbuf_unitOffset(u));
        ordinal--;
    }
    return NULL;
}

void fxbuf_setShareChangedCallback(fxbuf_share_changed_fn fn)
{
    fxbuf_state.on_share_changed = fn;
}

fxbuf_handoff_t *fxbuf_handoffBeginExit(void)
{
    fxbuf_handoff_t *h = &fxbuf_handoffRecord;
    uint8_t i;

    h->effect_share_offset = 0u;
    h->effect_share_bytes = fxbuf_shareBytes();
    for (i = 0u; i < FXBUF_VOICE_UNIT_COUNT; i++) {
        h->unit_owner_slot[i] = fxbuf_state.unit_owner[i];
        if (fxbuf_state.unit_owner[i] == FXBUF_UNIT_FREE) {
            h->unit_rate_hz[i] = 0u;
            h->read_offset[i] = FXBUF_OFFSET_NONE;
            h->write_offset[i] = FXBUF_OFFSET_NONE;
            h->unit_written_mask &= (uint16_t)~(1u << i);
        }
    }
    h->effect_type = FXBUF_EFFECT_TYPE_NONE;
    h->effect_channels = 0u;
    h->effect_bits = 0u;
    h->effect_rate_hz = 0u;
    for (i = FXBUF_HANDOFF_EFFECT_POINTER_BASE;
         i < FXBUF_HANDOFF_POINTER_COUNT; i++) {
        h->read_offset[i] = FXBUF_OFFSET_NONE;
        h->write_offset[i] = FXBUF_OFFSET_NONE;
    }
    h->state_flags &= (uint8_t)~FXBUF_STATE_EFFECT_WRITTEN;
    if (h->unit_written_mask)
        h->state_flags |= FXBUF_STATE_VOICE_WRITTEN;
    else
        h->state_flags &= (uint8_t)~FXBUF_STATE_VOICE_WRITTEN;
    return h;
}

void fxbuf_handoffSetVoiceUnit(uint8_t unit, uint32_t read_offset,
                               uint32_t write_offset, uint16_t rate_hz,
                               uint8_t written)
{
    fxbuf_handoff_t *h = &fxbuf_handoffRecord;

    if (unit >= FXBUF_VOICE_UNIT_COUNT ||
        fxbuf_state.unit_owner[unit] == FXBUF_UNIT_FREE)
        return;
    h->read_offset[unit] = read_offset;
    h->write_offset[unit] = write_offset;
    h->unit_rate_hz[unit] = rate_hz;
    if (written)
        h->unit_written_mask |= (uint16_t)(1u << unit);
    else
        h->unit_written_mask &= (uint16_t)~(1u << unit);
    if (h->unit_written_mask)
        h->state_flags |= FXBUF_STATE_VOICE_WRITTEN;
    else
        h->state_flags &= (uint8_t)~FXBUF_STATE_VOICE_WRITTEN;
}

const fxbuf_handoff_t *fxbuf_handoff(void)
{
    return &fxbuf_handoffRecord;
}

#if DEV_MODE_DIAGNOSTIC
uint8_t fxbuf_devSelfTestResult(void)
{
    return fxbuf_selfTestResult;
}
#endif
```

Implementation notes to carry into the in-code comments (each public function
gets the matching header block copied above its definition, per the project's
contract-comment rule):

- `fxbuf_voiceAcquire()` reserves the unit's handoff *rate* only. Read/write
  offsets stay NONE until the owner reports them, so an entering owner never
  mistakes a fresh claim for adoptable audio.
- `fxbuf_voiceRelease()` does not clear handoff entries. `fxbuf_handoffBeginExit()`
  resets the entries of free units at the next exit snapshot. Between those
  points the record still describes the last exit, which is the intended
  snapshot semantics.
- The self-test runs before the dev knob, so the knob cannot mask a logic
  failure, and before `fxbuf_handoffResetAll()`, so the record never reflects
  test claims.
- The file lives in `SRCS` (-O2), not `DSP_SRCS` (-Ofast). It is control-rate
  bookkeeping with no floating point.

---

## 12. `Makefile` — insert after line 37 (`-ICore/DSP/Instruments/HiHat \`)

```make
          -ICore/DSP/Effects \
```

This is the Effects framework include root. `EffectsManager.h` and the
per-type folders added in later steps sit under it; each type folder will add
its own `-I` line then.

## 13. `Makefile` — insert after line 105 (`Core/DSP/Instruments/HiHat/HiHatParameters.c \`)

```make
  Core/DSP/Effects/FxBuffer.c \
```

FxBuffer is control-rate bookkeeping, so it compiles with the normal -O2
`CFLAGS`. Future `*Effect.c` render sources join `DSP_SRCS` with an explicit
-Ofast rule, like the Instrument voices at Makefile lines 172–183.

## 14. `Makefile` — insert after line 157 (`	$(SZ) $(BUILD)/$(TARGET).elf`)

```make
	# Link budget (Session 072): flash headroom vs the 480 KiB application
	# region and the DTCM FX arena size. Report only; the linker ASSERTs in
	# STM32F765VIHx_FLASH.ld are the enforcing guards.
	python3 tools/link_budget.py $(PREFIX)nm $(BUILD)/$(TARGET).elf
```

(The recipe line uses a leading TAB, like the neighbouring lines.)

---

## 15. New file `tools/link_budget.py`

```python
#!/usr/bin/env python3
"""
Link budget report for the LXR-02 application image (Session 072).

What: reads linker symbols from the ELF via arm-none-eabi-nm and prints
flash use vs the 480 KiB application region, ITCM use, DTCM statics, and
the size of the .dtcm_fxbuf FX/voice audio arena.

Why: every Phase 5 step must be measured (EFFECTS_BUS_FEATURE_PLAN.md
§17.1), and flash headroom (34,356 B at S071) is the tightest Phase 5 risk.
`size` alone cannot show headroom, and its Berkeley `bss` column includes
the NOLOAD arena, which is misleading.

Inputs: argv[1] = nm executable, argv[2] = ELF path. Optional env
LINK_BUDGET_WARN_FLASH (bytes, default 16384): print a WARNING line when
flash headroom falls below it. Output: text on stdout; exit status is always
0 (report only; linker ASSERTs enforce hard limits). Affiliates: Makefile
`all`, STM32F765VIHx_FLASH.ld symbols _eflash_load, _eitcm, _sdtcm,
_edtcmz, _sfxbuf, _efxbuf; Core/DSP/Effects/FxBuffer.h FXBUF_MIN_ARENA_BYTES.
"""
import os
import subprocess
import sys

FLASH_ORIGIN = 0x08008000
FLASH_LIMIT = 0x08080000          # sample flash (sector 6) begins here
ITCM_BYTES = 16 * 1024
DTCM_ORIGIN = 0x20000000
FXBUF_MIN = 122880                # mirrors FXBUF_MIN_ARENA_BYTES


def symbols(nm, elf):
    out = subprocess.run([nm, elf], check=True, capture_output=True,
                         text=True).stdout
    table = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16)
    return table


def main():
    if len(sys.argv) != 3:
        print("usage: link_budget.py <nm> <elf>")
        return 0
    s = symbols(sys.argv[1], sys.argv[2])
    warn = int(os.environ.get("LINK_BUDGET_WARN_FLASH", "16384"))

    used = s["_eflash_load"] - FLASH_ORIGIN
    limit = FLASH_LIMIT - FLASH_ORIGIN
    head = FLASH_LIMIT - s["_eflash_load"]
    print(f"Flash : {used:,} / {limit:,} B used, headroom {head:,} B")
    if head < warn:
        print(f"WARNING: flash headroom {head:,} B < {warn:,} B threshold "
              "(see S072_ST1_IMPLEMENTATION.md §16 growth paths)")

    print(f"ITCM  : {s['_eitcm']:,} / {ITCM_BYTES:,} B")
    print(f"DTCM  : statics {s['_edtcmz'] - DTCM_ORIGIN:,} B")
    if "_sfxbuf" in s and "_efxbuf" in s:
        arena = s["_efxbuf"] - s["_sfxbuf"]
        print(f"FXBUF : {arena:,} B at 0x{s['_sfxbuf']:08X} "
              f"(min {FXBUF_MIN:,}, margin {arena - FXBUF_MIN:,})")
    else:
        print("FXBUF : arena symbols absent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

It uses the standard library only; no `requirements.txt` change is needed.

---

## 16. `main.c` — insert after line 79 (`#include "InstrumentManager.h"`)

```c
#include "FxBuffer.h"
```

## 17. `main.c` — `dsp_init()` (line 98), insert after line 102 (`    initRng();`)

```c
    /*
     * Initialise the shared DTCM audio arena bookkeeping before any DSP
     * runtime exists.
     *
     * Inputs: linker symbols _sfxbuf/_efxbuf. Output: all voice units free,
     * Effect share = whole arena, handoff = "nothing valid". Arena bytes are
     * not touched (no system-level clear; owners clear what they claim).
     * Why here: InstrumentManager (Phase 7 buffer voices) and EffectsManager
     * (Phase 5 step 4) must find valid bookkeeping when they construct.
     * Pre-audio and foreground only. Affiliates: Core/DSP/Effects/FxBuffer.c,
     * STM32F765VIHx_FLASH.ld .dtcm_fxbuf, config.h DEV_FXBUF_FORCE_VOICE_UNITS.
     */
    fxbuf_init();
```

## 18. `main.c` — insert after line 257 (closing `}` of `boot_delayMs()`)

```c
#if DEV_MODE_DIAGNOSTIC
/*
 * FxBf boot diagnostic (DEV_MODE_DIAGNOSTIC only; screen-only, no file I/O).
 *
 * What: shows the linked FX arena size, the Effect share after the dev knob,
 * units in use, and the FxBuffer self-test code for 1.5 s:
 *   "FxBf 124K u00   "
 *   "Shr  124K st0   "
 * Why: step 1 has no audible buffer user; this is the on-hardware proof that
 * the linker geometry and allocation logic agree. Inputs: FxBuffer getters.
 * Output: LCD rows 1-2 only; no FxBuffer state changes. The hold adds 1.5 s
 * to diagnostic boots only; production boots compile this out entirely.
 * Affiliates: DEV_MODES.md diagnostic list, config.h
 * DEV_FXBUF_FORCE_VOICE_UNITS. libc is discarded by the linker script
 * (/DISCARD/ libc.a), so digits are formatted by hand.
 */
static void boot_formatDec(char *dst, uint32_t value, uint8_t width)
{
    while (width--) {
        dst[width] = (char)('0' + (value % 10u));
        value /= 10u;
    }
}

static void boot_showFxBufDiagnostic(void)
{
    fx_share_t share;
    char row1[17] = "FxBf 000K u00   ";
    char row2[17] = "Shr  000K st0   ";

    fxbuf_effectShare(&share);
    boot_formatDec(&row1[5], fxbuf_arenaBytes() / 1024u, 3u);
    boot_formatDec(&row1[11], fxbuf_unitsInUse(), 2u);
    boot_formatDec(&row2[5], share.bytes / 1024u, 3u);
    boot_formatDec(&row2[12], fxbuf_devSelfTestResult(), 1u);
    lcd_clear();
    lcd_setcursor(0, 1);
    lcd_string(row1);
    lcd_setcursor(0, 2);
    lcd_string(row2);
    boot_delayMs(1500u);
}
#endif
```

The LCD row origin is 1-based, confirmed against `boot_show_splash()`
(`main.c:122`, `128`: `lcd_setcursor(0, 1)` / `lcd_setcursor(0, 2)`). The code
above matches it.

## 19. `main.c` — insert after line 466 (`    dsp_init();`)

```c
#if DEV_MODE_DIAGNOSTIC
    /* Screen-only FxBuffer proof; see boot_showFxBufDiagnostic(). */
    boot_showFxBufDiagnostic();
#endif
```

---

## 20. Documentation updates (same commit as the code)

- **`SRAM_MANIFEST.md`:**
  - DTCM ledger: `.dtcm` 8,708 → 512 B (`sine_table` removed; `squareRootLut`
    remains), `.dtcmz` 3,572 B unchanged, and a new row `.dtcm_fxbuf` of
    126,976 B at `0x20001000`, the "FX/voice audio arena, elastic remainder".
    DTCM capacity after statics becomes 0 B unallocated, with the arena
    listed as the reserved delay-buffer use.
  - SRAM1 owners: new rows `FxBuffer.c fxbuf_state` 28 B and
    `fxbuf_handoffRecord` 180 B (plus 1 B `fxbuf_selfTestResult` in diagnostic
    builds only).
  - Add the note that the Berkeley `bss` figure now includes the NOLOAD arena.
  - Regenerate from the new link.
- **`DEV_MODES.md`:** add the `FxBf`/`Shr` boot screen under
  `DEV_MODE_DIAGNOSTIC`, the `DEV_FXBUF_FORCE_VOICE_UNITS` knob and its 0..12
  range, and the self-test code table (§11).
- **`MODULE_INTERCHANGE_SPEC.md`:** add FxBuffer as the sole arena owner, with
  its foreground-only contract, public API, and planned clients
  (EffectsManager step 4, Phase 7 voices).
- **`MEMORY.md` volatile notes:** one line — "S072 step 1: sine_table in
  flash; DTCM arena `.dtcm_fxbuf` owned by FxBuffer."

---

## 21. §5.5 Flash-growth investigation (findings and deliverable)

**Question** (SCOPING §5.5): what happens when the program approaches or
exceeds the 480 KiB application region, and what is the tested growth path?

### Findings (from source, linker, and build artifacts)

1. **The build fails before the image can overlap sample flash.**
   `STM32F765VIHx_FLASH.ld:161–164` asserts `_etext <= 0x08080000` and
   `_eflash_load <= ORIGIN(FLASH)+LENGTH(FLASH)`. The flash image is `.text`
   + `.itcm` load + `.data` load + `.dtcm` load, ending at `_eflash_load`.
   Oversize links therefore stop with "Application load image overlaps sample
   flash region". No oversize `.bin` can be produced from this script.
2. **`lxr02.bin` equals `_eflash_load − 0x08008000`** (457,164 B).
   `objcopy -O binary` emits loadable sections only (Makefile:159–160), which
   is why the new arena must be NOLOAD (§5).
3. **The packer has no size check** (`tools/build_lxrv2_img.py`). It adds the
   16-byte `LXRV2IMG` header and a checksum. This is redundant given finding 1;
   no change is made.
4. **Bootloader behaviour on a payload over 0x78000 bytes is unknown.** It is
   a closed binary in sector 0; `README.md:44–50` documents only the header.
   Whether it rejects such a payload, truncates it, or erases into sector 6
   (sample flash) is unverified. **It is not tested in step 1.** A test
   risks the installed sample sectors and belongs with a chosen growth path.
5. **The sample floor is sector 6**, hard-coded in `SampleMemory.h:75`
   (`SAMPLE_ROM_START_ADDRESS 0x08080000`) and in the `sampleFlash.c:84`
   sector table. `sampleFlash` rejects erases below sector 6.
6. **Largest flash consumers** (current `.elf`) are constant tables totalling
   126,812 B (about 28 % of the image):
   - `crashSample` 32,768 B
   - `transientData` 26,460 B
   - `sawTable`, `triTable`, `recTable` 22,528 B each

   The largest code objects are `main` (9,024 B), `filesystem_tick`
   (7,896 B), `menu_repaintGeneric` (7,864 B), and the Scene/Bank load/save
   ticks (6,500–7,600 B each).
7. **The sine move is flash-neutral.** Its 8,194 B move from the `.dtcm` load
   image into `.rodata`, so the expected change is 0–4 B of alignment.

### Growth paths, ranked (for a later decision; none implemented in step 1)

| Rank | Path | Gain | Cost / risk |
|---|---|---|---|
| 1 | `-Os` for cold control modules (`menu.c`, `filesystem.c`, `presetManager.c`, `storageTypes.c`) via per-file rules, like the existing `-Ofast` DSP rule | Likely several KiB | Must be measured, with UI/filesystem timing checks. No layout change. |
| 2 | Move large constant tables (`crashSample`, the wavetables) into the sample region as installed data | Up to ~100 KiB | Needs an install path and a reserved sample-region span; reduces user sample space. Bootloader-independent. |
| 3 | Move the sample floor to sector 7 | +256 KiB app | −256 KiB samples; `SampleMemory.h`, `sampleFlash.c`, and linker FLASH changes; users must reinstall samples. **Requires the unknown bootloader behaviour (finding 4) to be tested first.** |
| 4 | Remove dead legacy code (File/Dir strings, legacy `.SND` Morph paths) | Small | Churn outside Phase 5; not recommended now. |

**Step 1 deliverables:** these findings, plus `tools/link_budget.py`, which
prints headroom on every build and warns below 16 KiB. The Phase 5 estimate of
+10–16 KiB leaves about 18–24 KiB after Phase 5.

**Decision point:** when the warning first fires, or before Phase 7,
whichever is first. This does not block Phase 5 steps 2–5.

**Verification item, local only:** temporarily add a 40 KiB `const` array,
confirm the link fails with the load-image ASSERT message, then revert. Do not
commit it. This proves finding 1 on this toolchain.

---

## 22. Build and hardware verification gates

**Link and build checks**, recorded in the step-1 closeout:

1. `make` succeeds, and `link_budget.py` prints:
   - Flash headroom 34,356 B ± 4 (the sine move is flash-neutral).
   - DTCM statics 4,084 B (`0x20000FF4 − 0x20000000`).
   - FXBUF 126,976 B at `0x20001000` (margin 4,096 B over the 120 KiB
     minimum).
2. `arm-none-eabi-nm build/lxr02.elf | grep sine_table` shows an
   `0x080xxxxx` address, not `0x2000xxxx`.
3. `arm-none-eabi-objdump -h build/lxr02.elf` shows `.dtcm_fxbuf` with flags
   `ALLOC` only (no `LOAD`/`CONTENTS`).
4. `lxr02.bin` size equals `_eflash_load − 0x08008000`, meaning no
   DTCM-stretched binary.
5. `make img` produces a `.img` of `.bin` + 16 B.
6. The local-only oversize-link ASSERT check (§21).

### Step 1 closeout measurements (2026-09-27)

The production build produced `text=458,112`, `data=416`, `bss=419,100`.
`tools/link_budget.py` reported 458,528 B of the 491,520 B application flash
region used, 32,992 B headroom, 3,768 B ITCM, 4,084 B DTCM statics, and
126,976 B of FX arena at `0x20001000` (4,096 B above the approved minimum).

The final ELF symbols are `_sfxbuf=0x20001000`, `_efxbuf=0x20020000`, and
`sine_table=0x0806B2FC`. `objdump -h` reports `.dtcm_fxbuf` as `0x1F000`
bytes with `ALLOC` only; it has no `LOAD` or `CONTENTS` flag. The binary is
458,528 B, exactly the flash load span, and `make img` produced a 458,544 B
image (16-byte wrapper).

The diagnostic build with `DEV_MODE_DIAGNOSTIC=1` and
`DEV_FXBUF_FORCE_VOICE_UNITS=12` also linked successfully: `text=459,752`,
`data=420`, `bss=419,100`, 460,172 B flash load, and 31,348 B headroom. The
screen and self-test were not hardware-observed in this session.

The host verification does not include the production sine stress test,
normal Scene/Kit/AutoSave smoke test, or the temporary 40 KiB oversize-link
experiment; those are retained as hardware/local gates below.

**Hardware, production build** (`DEV_MODE_DIAGNOSTIC 0`):

7. Boot and play normally; there is no diagnostic screen.
8. **Sine stress test.** On a test Scene, set every voice that exposes an
   oscillator waveform to `Sin`, at high pitch (coarse near maximum), and set
   the voice LFOs to sine at high rate targeting pitch or filter. Run a dense
   16-step Pattern at 180+ BPM for 2 minutes.
   - Record the global `cpu` widget.
   - Listen for underrun clicks.
   - Compare against the same Scene on the Session 071 image.

   **Pass** if the `cpu` reading rises by no more than 2 points and no
   underruns occur. **If it fails**, record the numbers, stop, and discuss.
   The fallback is to restore `INCCM` on `sine_table`, which gives back 8 KiB
   of arena and still clears the 120 KiB ASSERT.
9. Run a normal session smoke test: Scene switch, Kit load, and AutoSave cycle.

**Hardware, diagnostic build** (`DEV_MODE_DIAGNOSTIC 1`):

10. With knob 0 the boot shows `FxBf 124K u00` / `Shr  124K st0`.
11. With knob 12 it shows `FxBf 124K u12` / `Shr  072K st0`
    (126,976 − 52,992 = 73,984 B = 72.25 KiB, rounded down to 72).
12. Restore knob 0 and `DEV_MODE_DIAGNOSTIC 0` before committing. The
    committed defaults are unchanged.

---

## 23. RAM accounting (exact, replacing plan §16 estimates for these items)

| Object | Region | Bytes | Approval |
|---|---|---|---|
| `sine_table` | DTCM → flash | −8,194 DTCM, flash ±4 | released |
| `.dtcm_fxbuf` arena | DTCM NOLOAD | 126,976 (elastic remainder) | A46 (arena = reserved delay-buffer use) |
| `fxbuf_state` | SRAM1 `.bss` | 28 | plan §16 item 6 ("about 24") |
| `fxbuf_handoffRecord` | SRAM1 `.bss` | 180 | G7 ("about 176") |
| `fxbuf_selfTestResult` | SRAM1 `.bss` | 1, diagnostic builds only | dev-only |

The SRAM1 total for step 1 is **208 B**. The approved estimates were about
200 B (24 + 176); the exact figures are 4 B over each (alignment padding, and
the 16-bit `unit_written_mask` that implements the accepted "voice unit
written" flag). This is flagged for acknowledgement per the RAM policy.

---

## 24. Observed, not changed (outside step-1 scope; reported only)

1. **`Core/DSPAudio/modulationNode.c:67`:**
   `static INCCMZ uint32_t modNode_waveInterpGeneration = 1u;`.
   `INCCMZ` places it in `.dtcmz` (NOLOAD, zeroed by startup), so the `= 1u`
   initializer is discarded and the value starts at 0. If any logic relies on
   generation 0 meaning "never set", it may misbehave on the first use.
   Unverified. Candidate for a later bugfix entry in `SCOPING_TARGETS.md` if
   you want it logged.
2. **`STM32F765VIHx_FLASH.ld:48`** says "Stack lives at top of SRAM1
   (`_estack = 0x20080000`)". `0x20080000` is the top of **SRAM2**
   (`SRAM_MANIFEST.md` states this correctly), and `MEMORY.md` repeats the
   SRAM1 wording. Documentation only.

---

## 25. Commit and rollback

- **One commit:** "S072 step 1: sine_table to flash, DTCM FX arena, FxBuffer
  API, link budget". It contains §1–§20. The §21 local ASSERT test is not
  committed.
- **Rollback:** revert the commit. The only runtime-visible change is the sine
  table location. FxBuffer has no consumers until step 4.

---

## 26. Review assessment (2026-09-27, post-implementation)

**Verdict:** the Step 1 source and build changes match this schedule (§1–§20)
and are accepted for commit. One minor dev-only defect (§26.3 item 1) should be
fixed in the same commit. The hardware gates (§22 items 7–12) are still open.

### 26.1 What was checked

- **Diff against HEAD `a0531ae`, for every scheduled file.**
  - `wavetable.c/.h`, `startup_stm32f765xx.s`, `STM32F765VIHx_FLASH.ld`,
    `config.h`, `Makefile`, and `main.c` match §1–§19. Line anchors and
    comment blocks are as specified.
  - `tools/link_budget.py` differs from §15 only in docstring wording and two
    inline comments; the logic is identical.
  - `Core/DSP/Effects/FxBuffer.c` matches §11 line for line, including the
    self-test, init order, and handoff logic. `FxBuffer.h` compiles against it
    with the specified API.
- **Clean build** of the working tree (`make all`):
  - `text=458,112`, `data=416`, `bss=419,100`.
  - `link_budget.py` reports 458,528 B flash (**32,992 B headroom**),
    3,768 B ITCM, 4,084 B DTCM statics, and FXBUF 126,976 B at
    `0x20001000` (margin 4,096 B).
  - Symbols: `_sfxbuf=0x20001000`, `_efxbuf=0x20020000`, and `sine_table`
    at `0x0806B2FC` (`T`, flash). `fxbuf_state` is 28 B at `0x2002111C` and
    `fxbuf_handoffRecord` is 180 B at `0x20021138`.
  - This independently reproduces the §22 closeout numbers.
- **Baseline rebuild** of HEAD in a scratch worktree (since removed):
  `_eflash_load=0x080779CC` (457,164 B), which is identical to the §0
  baseline. It gives a like-for-like comparison.
- **Section comparison, baseline → Step 1:**
  - `.dtcm` 8,708 → 512.
  - `.dtcmz` 3,572 → 3,572, now at `0x20000200`.
  - New `.dtcm_fxbuf` of 126,976.
  - `.bss` 285,228 → 285,452 (**+224 B**).
  - `.data`, `.dma_nocache`, and `.itcm` unchanged.
- **Documentation updates** (`SRAM_MANIFEST.md`, `DEV_MODES.md`,
  `MODULE_INTERCHANGE_SPEC.md`, `MEMORY.md`) match §20 and the measured link.
  The SRAM1 ledger's previous `.bss` figure (285,052) was stale relative to
  the actual S071 link (285,228). The new row is measured and correct.

### 26.2 Measured costs versus the schedule

| Item | Scheduled | Measured | Note |
|---|---|---|---|
| Flash | "±4 B" (sine move) | **+1,364 B** | See below. 0.28 % of the region; headroom 32,992 B. |
| SRAM1 `.bss` | 208 B | **+224 B** | 208 B of FxBuffer objects + 16 B layout/alignment shift in `.bss`. Within the approved allocation; recorded for the RAM ledger. |
| DTCM statics | 4,084 B | 4,084 B | Exact. |
| Arena | 126,976 B | 126,976 B | Exact. |

**Flash growth attribution** (symbol-size diff, baseline vs Step 1):

- The sine move is flash-neutral as predicted: `sine_table` +8,194 in
  `.rodata`, and the `.dtcm` load image −8,196.
- FxBuffer has no out-of-line symbols in production: every `fxbuf_*` function
  is inlined by LTO.
- The +1,364 B comes from LTO re-partitioning inlining across the image:
  - `dsp_init` is now an out-of-line 440 B function (it was inlined into
    `main`);
  - `encode_read4` is out-of-line (+308 B);
  - `filesystem_ensureAutosaveFiles_tick` grew +336 B;
  - `mixer_calcNextSampleBlock` grew **+208 B**;
  - many `.lto_priv` renames net to about zero.

**Consequence for the CPU gate:** `mixer_calcNextSampleBlock` changed its
generated code although its source did not. The §22 item 8 comparison against
the Session 071 image therefore measures the sine move *and* this codegen
shift together. If the `cpu` reading changes, compare against a build with
only the `INCCM` removal reverted before blaming the sine table.

### 26.3 Findings

1. **Minor defect (dev-only, from this schedule's §11): forced dev units show
   a handoff rate of 0.**
   - Cause: in `fxbuf_init()`, the forced-unit loop runs before
     `fxbuf_handoffResetAll()`. The reset zeroes the `unit_rate_hz` that
     `fxbuf_voiceAcquire()` set for those units, and
     `fxbuf_handoffBeginExit()` only restores entries of *free* units.
   - Effect: with `DEV_FXBUF_FORCE_VOICE_UNITS > 0`, the forced units carry
     `unit_rate_hz = 0` instead of 44,108. There is no reader until step 4, and
     production is unaffected.
   - **Fix:** call `fxbuf_handoffResetAll()` immediately after
     `fxbuf_clearOwners()` in `fxbuf_init()`, before the self-test. Then run
     the self-test, then the forced-unit loop, then
     `fxbuf_handoffBeginExit()`. The self-test's claims are released, and
     `BeginExit` resets the free units' entries, so the "record never
     reflects test claims" property still holds. Update the §11 note to match.
2. **Cosmetic: the Makefile recipe comments are echoed on every build.** The
   three `# Link budget …` lines are TAB-indented recipe lines, so make passes
   them to the shell and prints them (visible in the build log). **Fix:**
   prefix them with `@` (`@# …`), or move the comment above the `all:` rule.
3. **Pre-existing, not a Step 1 change: a bare `make` does not build the
   firmware in an incremental tree.**
   - `Makefile` places `-include $(OBJS:.o=.d)` (about line 154) before
     `all:`. Once `.d` files exist, the first rule they contain
     (`build/main.o`) becomes make's default goal.
   - `make` then prints "`build/main.o' is up to date" and stops (observed
     during this review).
   - `make img` still builds, because it names its target. But the documented
     `make && make img` workflow never runs the `all` recipe, so the new
     link-budget report does **not** print.
   - The fix is one line (`.DEFAULT_GOAL := all` near the top, or move the
     `-include` below `all:`). It is outside Effects scope and is reported
     here for your decision. Until then, use `make all` to see the report.
4. **Design note, not a defect:** in `fxbuf_voiceAcquire()` the global-capacity
   refusal can never be the sole reason for a refusal. With 12 units = 6 slots
   × 2, a full table implies every slot is at its cap, so self-test check 6
   exercises the per-slot cap. The global check is kept as a guard in case the
   unit count or cap ever changes independently.
5. **Still open (unchanged):**
   - the §22 hardware gates: sine stress/CPU test, smoke test, the
     diagnostic-build `FxBf` screen at knob 0 and 12;
   - the local oversize-link ASSERT experiment;
   - the §24 observations (`modNode_waveInterpGeneration` initializer, and the
     SRAM1/SRAM2 stack wording).

### 26.4 Hardware result (2026-09-27, user-reported)

- **§22 items 7–9 (production build): PASS.** Normal boot and play worked.
  The sine stress test showed no appreciable rise on the `cpu` widget and no
  underruns. The comparison covers the sine move and the §26.2 LTO codegen
  shift in `mixer_calcNextSampleBlock` together.
- **§22 items 10–12 (diagnostic `FxBf` screen): waived by the user**
  (2026-09-27). The system is running correctly. The production build shows no
  diagnostic screen, as intended, because it is compiled only with
  `DEV_MODE_DIAGNOSTIC 1`. Step 1 hardware verification is closed.
