# S091 P2 — CrumpBit Expansion: Implementation Schedule

- **Plan:** `S091_P2_CrumpBit_Expanded_Plan.md`, with its §0 (drive
  deferred, user 2026-11-04) and the §0A decisions of
  `S091_P1_CrumpBit_Expansion.md`.
- **Baseline:** HEAD `5c9a92f9` on `dev-ph7-voice_expansion` (the S091 P1
  reductions). Every **HEAD line** below refers to that tree. Its image is
  the one the user measured at −3–4 % `cpu` with `cbt`.
- **Scope:** the plan's features **without the drives**:
  - the elastic delay on the whole arena, with the Rate reversed and
    clamped by the share (plan §5.1);
  - feedback up to 1.2 (§5.2);
  - on the feedback return: an in-loop DC blocker, the `ton` tilt and a
    fixed soft knee (§5.3, without `tdr`);
  - the stereo output filter `typ`/`frq`/`res` (§5.4, without `drv`);
  - the sub-type row ownership (§4) and the new page (§3).
- **Pre-verified (2026-11-04):** every code block below was applied to a
  copy of HEAD in the session scratchpad (`git archive HEAD`). That copy was:
  - built with the firmware flags (`make all`, `make img`);
  - measured on the linked ELF;
  - tested on the host (laws CB1–CB9 and five break builds).

  The blocks were copied from the built files by script
  (`difflib` mapping of each unit onto the built file, with a check that no
  changed line lies outside a unit). They were not retyped. The numbers in
  §2 come from that build.
- **Status:** schedule. No firmware file in the repository is changed.
- **Constraints:**
  - `S089_P2_WAVEFOLDER_CHAIN.md` §8: no per-sample cost above the measured
    saving. The pre-S091 CrumpBit is ≈ 191 per frame; this schedule
    measures ≈ 172–176 (§2).
  - RAM: 0 B. `CrumpBitRuntime` fills the 76 B union, and the +8 B
    approval stays unused.

---

## 0. Decisions applied

| # | Decision | Source | Applied as |
|---|---|---|---|
| U1 | Voice-aligned names: `typ`, `frq`, `res` | user | rows 12–14 with the StereoFilter/voice keys' labels (`Type`, `Frequncy`, `Resnance`), the `MENU_FILTER` list and the voice cutoff/resonance laws (C2, E11) |
| U2 | `typ` morphable | user | `INSTRUMENT_PARAM_FLAG_MORPHABLE \| AUTOMATABLE`, `EFFECT_MOD_NONE`, exactly StereoFilter's `typ`; a type step is a one-block coefficient crossfade (E11) |
| U3 | `ton` belongs to `sub` | user | `CRUMPBIT_ROWS_DELAY` / `CRUMPBIT_ROWS_SHARED` masks with compile-time and host guards (P2, C3, CB9); the stage comments in `crumpBit_process()` (E11) |
| U4 | Page order | user | s0 `ton fbk rte mrp`, s1 `ton fbk rte sub`, s2 `snc dpn mix typ`, s3 `frq res – –` (C5) |
| U5 | Drive | user (deferred) | not implemented; rows 15/16, lanes 14/15 and s3 cells 3/4 stay free for it |
| Q1 | RAM by trims, union stays 76 B | plan recommendation | ramp origins recomputed from raw bytes, AC codes `int16_t` (H2, E11) |
| Q2 | Rate reversed, exponential 20 ms … 2.872 s | plan recommendation | `crumpBit_rateTable` (N2), top-down `effective_max` clamp (E4, E13) |
| Q3 | Feedback unity at 100, 1.2 at 127 | plan recommendation | `crumpBit_feedbackGain()` (E11) |
| Q4 | Output resonance Q ≤ 8 | plan recommendation | `crumpBit_filterDamping()` snaps to R 0.0625 where the voice snaps to 0.02 (E11) |
| Q7 | Sub-type byte waits for a second sub-type | plan recommendation | `write_param` still ignores `sub` (E10) |
| Q9 | `LP2` plays as LP | plan recommendation | `crumpBit_filterMixTable` row 6 = row 0 (E11) |
| D7 | Soft knee | user (§0A) | fixed cubic `v − (4/27)v³` on `v` ∈ [−1.5, 1.5] (E2, E11); the `tdr` knee replaces it later |
| D8 | Keep the tail on a share change | plan | `crumpBit_seat()` (E8) |
| D2 | A reservation clamps the settable Rate and the DSP target | user (§0A) | `effective_max` op (E13), `effects_paramEffectiveMax()` for page edits (M1, M4, U1), clamped label (C4) |

**Implementation choices made here.** These are not plan decisions; each
can be changed before implementing:

- **Control tables** (Rate lengths, tone pole, cutoff `g`) replace a
  per-block `expf`, the voice `fastTan` division and the `valueShaper`
  division (N1–N3).
  - `crumpBit_process()` and `effective_max` run inside
    `mixer_calcNextSampleBlock()`, so per-block math is render time.
  - The Rate table also makes the share clamp and the DSP use bit-identical
    lengths.
- **The output filter is a second pass over the block** (E11). The
  stage-2 loop already uses all 32 FPU registers (§7). The pass also gives
  a future sub-type the same output stage untouched (plan §4.3).
- **A flat tone is weighted (a, b) = (0, 1),** so it is a bit-exact
  pass-through. The plan said "k = 1, (1, 0)", which is exact only to one
  ulp.
- **Both binary searches get an eighth probe** (E4, E5). The seven steps
  sum to 127, not 128 (§8.1).
- **The type mix is a table, not a `switch`.** A jump table's data bytes
  defeat the loop-path gate, and the lookup is branch-free.

---

## 1. Change list

| Unit | File | What |
|---|---|---|
| H1–H4 | `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | elastic geometry constants; 76 B runtime; Rate/share/Sync API |
| E1–E13 | `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | constants and guards; table Rate, share limit and Rate limit; Sync with live maximum (eighth probe); target; elastic seat; init seeds; write_param; gain/filter helpers and the three-stage `process()`; `effective_max` op |
| P1–P3 | `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` | row count 15; rows 11–14; ownership masks; tone centre; filter type ids |
| C1–C6 | `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | rows, ownership guards, clamped Rate label, four-screen layout |
| N1–N3 | new `CrumpBitTables.h`, generated `CrumpBitTables.c`, new `tools/plots/gen_crumpbit_tables.py` | the three 128-entry tables and their generator |
| R1 | `Core/DSP/Effects/EffectParamRows.h` | `EFFECT_ROW_FLAGS` (a row with Effect-only flags) |
| M1–M4 | `Core/DSP/Effects/EffectsManager.h/.c` | `effects_paramEffectiveMax()`; union comment; registry row (lanes, arena min/pref) |
| U1 | `Core/Menu/menuEffects.c` | page edits clamped by the share |
| K1 | `Makefile` | `CrumpBitTables.c` in `SRCS` |
| T1–T5 | `tools/dsp_test/Makefile`, `tools/dsp_test/modules/crumpbit.c` | laws CB1–CB9, five break builds, `crumpbit_tables`, new ELF limits |
| D1–D7 | documentation | §6 |

No other firmware file changes. Checked and unchanged:
- `StereoFilter*`, `ResonantFilter.c` (the voice laws are read, not
  called), `FxBuffer.c/.h` and `mixer.c`;
- the `.fx`/AutoSave code: rows are registry-driven, and the keys are at
  most 40 characters (`STORAGE_EFFECT_KEY_MAX`);
- the registry self-check (no duplicate lanes; defaults ≤ maxima).

---

## 2. Measured results (scratch build of HEAD + this schedule)

| Item | HEAD (S091 P1) | After | Note |
|---|---:|---:|---|
| `crumpBit_process` stage-2 loop (ADC, AC, delay, return), longest / shortest | 98 / 88 | **126 / 112–124** | two rotated loop heads; no VDIV |
| `crumpBit_process` stage-3 loop (output filter, both channels) | — | **31** | no VDIV |
| `crumpBit_divisionFor` (per block) | 117 | **113** | straight line; eighth probe folded into the `t_high` load |
| `crumpBit_rateLimit` (per block, through `effective_max` in `effects_service()`) | — | **101** | straight line |
| Per-block work in `process()` | ≈ 8 per frame (search, `expf`, 2 VDIV) | ≈ 15–19 per frame | search 113, Rate limit 101, 3 VDIV (`per_tick`, `per_frame`, `a1`), tone/filter setup, ramp origins |
| **CrumpBit per frame, worst case** | ≈ 106 | **≈ 172–176** | pre-S091 ≈ 191 |
| `crumpBit_process` size | 996 B | 1,924 B | |
| `crumpBit_divisionFor` / `syncDivision` / `init` | 404 / 100 / 20 B | 380 / 64 / 40 B | |
| New: `rateLimit`, `effectiveMax` (inlined copy), `shareMaxSamples` | — | 312 / 312 / 52 B | |
| New tables: `rateTable`, `toneK`, `filterG`, `filterMixTable` | — | 512 × 3 + 128 B | `expf` (228 B) leaves the image |
| `.text` / `.data` / `.bss` | 690,160 / 420 / 413,028 | **693,736** / 420 / 413,028 | +3,576 B text |
| ITCM / DTCM statics / FXBUF | 15,240 / 4,372 / 126,688 B at `0x20001120` | **15,256** / 4,372 / 126,688 B at `0x20001120` | ITCM +16 B: LTO setup changes in `calcNextOscSampleRingBlock`/`SyncBlock`; their loops are unchanged (9/9 and 11/9) |
| `CrumpBitRuntime` / `effects_runtime_t` | 56 / 76 B | **76** / 76 B | 0 B RAM |
| `mixer_calcNextSampleBlock` loops | — | 86/86 identical apart from branch displacements; hot loops 39 equal, 0 grew | the plain text compare reports 33 "missing" only because one long `b.w` displacement moved (§5 G6) |
| Image | 690,580 B | **694,156 B** | `make img` OK |
| Host | CB1–CB5, 2 breaks | **CB1–CB9 0 failures, 5 breaks caught** | CB8 at `fbk` 127: peak 1.000, mean loop code −0.062 / −0.004 / −0.001 LSB |

**Where stage 2's +28 comes from** (instructions per frame):
- the DC blocker, tilt, feedback multiply and knee: about 14;
- register pressure: the loop holds 32 live single-precision values. GCC
  re-materialises six float constants (±1.5, 0.5, 1.0, 2.0) and reloads
  four literal-pool values on every frame, and keeps five values on the
  stack (four `ldr`/`str` and one `vldr`).

§7 lists the reductions available if hardware wants them.

---

## 3. Firmware changes

Each unit gives its HEAD location, the reason for it, and the exact code.
The comment blocks inside the code are part of the change: they document
the variables, statements and loops in place.

### 3.1 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`

#### H1 — Elastic loop geometry constants

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, HEAD lines 17–32: **replace** (16 lines → 26; new lines 17–42).

- **What:** removes the S074 fixed loop:
  - `CRUMPBIT_DELAY_MAX_SAMPLES` 70,573 (1.60 s);
  - `CRUMPBIT_BUFFER_BYTES` 70,592.

  It defines the elastic geometry (plan §5.1):
  - `CRUMPBIT_DELAY_CURVE_MAX` 126,686 samples (2.872 s), the top of the
    Rate curve and `crumpBit_rateTable[127]`;
  - `CRUMPBIT_LOOP_MAX_BYTES` 126,688, the longest loop seated: today's
    whole arena, a multiple of 32;
  - `CRUMPBIT_LOOP_MIN_SHARE_BYTES`, the share left with all twelve voice
    units claimed (126,688 − 12 × 4,416 = 73,696 B).
- **Why:**
  - The delay uses the whole share, and a voice-unit reservation shortens
    it.
  - The curve top is a constant, so a later DTCM change moves the arena,
    not the meaning of a saved `rte`.
- **Accessors:**
  - `CrumpBitEffect.c`: seat, share limit, guards;
  - `EffectsManager.c`: the registry row's min/pref, which are
    declarative.

  `FXBUF_VOICE_UNIT_*` reach this header through `EffectsManager.h` →
  `FxBuffer.h`.

Remove (HEAD 17–32):

```c
/*
 * CrumpBit loop geometry (S074_CRUMPBIT_EFFECT.md §3.4).
 *
 * CRUMPBIT_DELAY_MAX_SAMPLES: the longest delay, 1.60 s at 44,108 Hz. Rate 0
 * and every Sync division are clamped to it. It is fixed (not
 * share-dependent) so a Scene sounds the same whatever voice units future
 * instruments claim.
 * CRUMPBIT_BUFFER_BYTES: the loop length actually used, 1.60 s plus a
 * 2-sample interpolation guard, rounded up to 32 B. One byte per 8-bit mono
 * sample. It must stay <= the minimum Effect share (73,632 B with all twelve
 * voice units claimed). It is declared as buffer_min_bytes and
 * buffer_pref_bytes in the registry row.
 * Accessors: CrumpBitEffect.c, EffectsManager.c (registry row).
 */
#define CRUMPBIT_DELAY_MAX_SAMPLES   70573.0f
#define CRUMPBIT_BUFFER_BYTES        70592u
```

Replace with:

```c
/*
 * CrumpBit loop geometry (S074_CRUMPBIT_EFFECT.md §3.4; elastic since S091
 * P2 §5.1).
 *
 * CRUMPBIT_DELAY_CURVE_MAX: the top of the Rate curve, 126,686 samples
 * (2.872 s at 44,108 Hz) = crumpBit_rateTable[127]. It is the whole arena
 * less the 2-sample interpolation guard, and it is a constant on purpose: a
 * future small DTCM change moves the arena, not the meaning of a saved `rte`.
 * CRUMPBIT_LOOP_MAX_BYTES: the longest loop CrumpBit seats, the curve top plus
 * the guard (126,688 B, a multiple of 32, the arena size at S091). One byte
 * per 8-bit mono sample. A larger share is not used beyond it; a smaller share
 * (voice units claimed from the top) shortens the loop, and the Rate row and
 * the DSP target are clamped to what it holds (the loop is elastic).
 * CRUMPBIT_LOOP_MIN_SHARE_BYTES: the share left at this arena size with all
 * twelve voice units claimed (73,696 B, 1.671 s): declared as
 * buffer_min_bytes. CrumpBit still works below it (down to CRUMPBIT_LOOP_MIN
 * samples); the field is declarative.
 * Accessors: CrumpBitEffect.c, EffectsManager.c (registry row).
 * Affiliates: FxBuffer.h FXBUF_VOICE_UNIT_*, tools/plots/gen_crumpbit_tables.py
 * (RATE_MAX), tools/diagnostic_tools/link_budget.py (FXBUF size).
 */
#define CRUMPBIT_DELAY_CURVE_MAX       126686.0f
#define CRUMPBIT_LOOP_MAX_BYTES        126688u
#define CRUMPBIT_LOOP_MIN_SHARE_BYTES                                   \
    (CRUMPBIT_LOOP_MAX_BYTES -                                          \
     FXBUF_VOICE_UNIT_COUNT * FXBUF_VOICE_UNIT_BYTES)
```

#### H2 — Runtime: 56 → 76 B, grouped by owner

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, HEAD lines 34–82: **replace** (49 lines → 78; new lines 44–121).

- **What:** the runtime grows from 56 to 76 B and fills the union exactly.
  Field changes:

  | Change | Bytes |
  |---|---:|
  | − `mix`, `feedback`, `gain_l`, `gain_r` (ramp origins, now recomputed from raw bytes) | −16 |
  | `ac_c_l`/`ac_c_r` `int32_t` → `int16_t` (codes 0..255, loaded and stored once per block) | −4 |
  | + `dc_x`, `dc_y` (DC blocker), `tone_s` (tilt pole) | +12 |
  | + `svf_l1`, `svf_l2`, `svf_r1`, `svf_r2` (TPT integrators) | +16 |
  | + `tone_raw`, `filter_type`, `filter_freq`, `filter_reso` | +4 |
  | + `mix_prev`, `feedback_prev`, `pan_prev`, `filter_type_prev`, `filter_reso_prev` (ramp-origin rows) | +5 |
  | **Total** | **73 → 76 with alignment** |

  The fields are ordered by size, so there is no internal padding. Each
  carries its stage owner (§4 of the plan).
- **Why:** plan §8 (Q1). The three spare bytes take the two deferred drive
  rows later without growth.
- **Accessors:** `CrumpBitEffect.c` only. A repository search finds no
  other reader of the removed fields.

Remove (HEAD 34–82):

```c
/*
 * CrumpBit runtime (56 B, a member of EffectsManager's DTCM runtime union).
 *
 * What: the per-sample DSP state and the last resolved row values. Audio (the
 * 8-bit loop) lives in the FxBuffer Effect share, never here.
 * - length: the loop length in samples/bytes (0 = not seated yet);
 * - write_pos: the next loop index to write (0..length-1);
 * - valid: samples written since the loop was last treated as empty,
 *   saturating at length. Reads further back than this are muted, which
 *   gives "clear unless you adopt" without a bulk clear;
 * - delay: the current delay in samples, gliding towards the per-block
 *   target (tape style);
 * - mix, feedback, gain_l, gain_r: block-ramp values (end of the previous
 *   block);
 * - ac_c_*: the previous data-line code per channel, 0..255 (S091 R4; it
 *   replaced the float input memory ac_x_*). crumpBit_init() seeds 128,
 *   silence; the zeroed union value 0 would be a full-scale step;
 * - ac_y_*: the AC-coupling (one-pole high-pass) output memory per channel;
 * - bit_off .. pan_raw: the last row values from write_param();
 * - primed: 0 until the first block sets delay directly to its target (no
 *   glide on entry).
 * Why here: EffectsManager zeroes the union and calls init() when the type
 * becomes active; the union is 76 B (StereoFilter), so 56 B needs no RAM.
 * Accessors: CrumpBitEffect.c only; no other module may keep a pointer.
 * Affiliates: effects_runtime_t, M4 (size guard),
 * EFFECTS_MIXER_DSP_REFERENCE.md §4.2.
 */
typedef struct {
    uint32_t length;
    uint32_t write_pos;
    uint32_t valid;
    float delay;
    float mix;
    float feedback;
    float gain_l;
    float gain_r;
    int32_t ac_c_l;
    int32_t ac_c_r;
    float ac_y_l;
    float ac_y_r;
    uint8_t bit_off;
    uint8_t bit_invert;
    uint8_t mix_raw;
    uint8_t feedback_raw;
    uint8_t rate;
    uint8_t sync;
    uint8_t pan_raw;
    uint8_t primed;
} CrumpBitRuntime;
```

Replace with:

```c
/*
 * CrumpBit runtime (76 B since S091 P2, a member of EffectsManager's DTCM
 * runtime union; it fills the union exactly, so the union does not grow).
 *
 * What: the per-sample DSP state and the last resolved row values. Audio (the
 * 8-bit loop) lives in the FxBuffer Effect share, never here. Fields are
 * ordered by size (no padding inside); the comment of each says its stage
 * owner (S091 P2 §4: shared, delay sub-type, output).
 * Delay sub-type:
 * - length: the loop length in samples/bytes (0 = not seated yet);
 * - write_pos: the next loop index to write (0..length-1);
 * - valid: samples written since the loop was last treated as empty,
 *   saturating at length. Reads further back than this are muted, which
 *   gives "clear unless you adopt" without a bulk clear;
 * - delay: the current delay in samples, gliding towards the per-block
 *   target (tape style); never above length - 2 (crumpBit_seat());
 * - dc_x, dc_y: the in-loop DC blocker's previous input and output (S091
 *   P2; mandatory once feedback can exceed unity);
 * - tone_s: the one-pole state of the `ton` tilt on the feedback return;
 * - mix_raw .. tone_raw: the delay rows from write_param();
 * - mix_prev, feedback_prev, pan_prev: the raw rows the previous block
 *   ramped to. S091 P2 recomputes each ramp origin from them instead of
 *   storing four floats (-16 B; the origin is a pure function of the raw
 *   row, so the result is the same);
 * - primed: 0 until the first block sets delay directly to its target (no
 *   glide on entry).
 * Shared stages:
 * - ac_y_*: the AC-coupling (one-pole high-pass) output memory per channel;
 * - ac_c_*: the previous data-line code per channel, 0..255 (S091 R4; it
 *   replaced the float input memory ac_x_*). int16_t since S091 P2 (-4 B;
 *   loaded and stored once per block). crumpBit_init() seeds 128, silence;
 *   the zeroed union value 0 would be a full-scale step;
 * - bit_off, bit_invert: the data-line masks;
 * - svf_*: the output filter's two TPT integrator states per channel;
 * - filter_type, filter_freq, filter_reso: the output rows;
 * - filter_type_prev, filter_reso_prev: the rows the previous block's output
 *   mix ramped to (the origin of this block's coefficient ramp).
 * Why here: EffectsManager zeroes the union and calls init() when the type
 * becomes active; the union is 76 B (StereoFilter), so CrumpBit needs no
 * RAM of its own.
 * Accessors: CrumpBitEffect.c only; no other module may keep a pointer.
 * Affiliates: effects_runtime_t, M4 (size guard),
 * EFFECTS_MIXER_DSP_REFERENCE.md §4.2, STORAGE_SRAM_MANIFEST.md.
 */
typedef struct {
    uint32_t length;           /* delay */
    uint32_t write_pos;        /* delay */
    uint32_t valid;            /* delay */
    float delay;               /* delay */
    float dc_x;                /* delay: DC blocker input memory */
    float dc_y;                /* delay: DC blocker output memory */
    float tone_s;              /* delay: tone tilt pole state */
    float ac_y_l;              /* shared: AC coupling */
    float ac_y_r;              /* shared: AC coupling */
    float svf_l1;              /* output: TPT band integrator, left */
    float svf_l2;              /* output: TPT low integrator, left */
    float svf_r1;              /* output: TPT band integrator, right */
    float svf_r2;              /* output: TPT low integrator, right */
    int16_t ac_c_l;            /* shared: previous code, left */
    int16_t ac_c_r;            /* shared: previous code, right */
    uint8_t bit_off;           /* shared */
    uint8_t bit_invert;        /* shared */
    uint8_t mix_raw;           /* delay */
    uint8_t feedback_raw;      /* delay */
    uint8_t rate;              /* delay */
    uint8_t sync;              /* delay */
    uint8_t pan_raw;           /* delay */
    uint8_t tone_raw;          /* delay */
    uint8_t filter_type;       /* output */
    uint8_t filter_freq;       /* output */
    uint8_t filter_reso;       /* output */
    uint8_t mix_prev;          /* delay: ramp origin */
    uint8_t feedback_prev;     /* delay: ramp origin */
    uint8_t pan_prev;          /* delay: ramp origin */
    uint8_t filter_type_prev;  /* output: ramp origin */
    uint8_t filter_reso_prev;  /* output: ramp origin */
    uint8_t primed;            /* delay */
} CrumpBitRuntime;
```

#### H3 — Rate, share-limit and Rate-limit API

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, HEAD lines 87–98: **replace** (12 lines → 15; new lines 126–140).

- **What:**
  - `crumpBit_rateSamples()` now reads the table, and a higher `rte` means
    longer.
  - Two new shared functions:
    - `crumpBit_shareMaxSamples(share)`: `min(bytes, LOOP_MAX) − 2`, or 0
      below the smallest loop;
    - `crumpBit_rateLimit(share)`: the largest `rte` that fits.
- **Why:** the DSP target clamp, the manager clamp (`effective_max`), the
  page edit clamp and the label must all use one rule (D2, N1).
- **Accessors:**
  - DSP: `process()` and `effective_max`;
  - foreground: `crumpBit_uiFormatValue3()` and, through the op,
    `effects_paramEffectiveMax()`.

Remove (HEAD 87–98):

```c
/*
 * Free-running delay for one Rate value.
 *
 * What: t = 1.60 s * 80^(-rate/127) in samples: rate 0 = 1.60 s (70,573),
 * rate 127 = 20 ms (882), about 20 steps per octave of time. Higher rate
 * means faster tape and a shorter delay (Q5). Why shared: the DSP and the
 * Effect-page Sync label must use one curve. Input: rate 0..127 (larger
 * values are treated as 127). Output: delay in samples.
 * Accessors: crumpBit_syncDivision(), CrumpBitEffect.c per-block target.
 * Affiliates: CRUMPBIT_DELAY_MAX_SAMPLES.
 */
float crumpBit_rateSamples(uint8_t rate);
```

Replace with:

```c
/*
 * Free-running delay for one Rate value (reversed and extended in S091 P2).
 *
 * What: crumpBit_rateTable[rate], t = 20 ms * (2.872 s / 20 ms)^(rate/127)
 * in samples: rate 0 = 882 (20 ms), rate 127 = 126,686 (2.872 s), about 18
 * steps per octave of time. Higher rate now means a LONGER delay (plan Q2):
 * a shrinking share then removes the top of the range, which the top-down
 * effective_max clamp can express (plan N1). The value is not clamped to a
 * share here. Why shared: the DSP, the share clamp and the Effect-page Sync
 * label must use one curve. Input: rate 0..127 (larger values are treated as
 * 127). Output: delay in samples.
 * Accessors: crumpBit_syncDivision(), CrumpBitEffect.c per-block target.
 * Affiliates: crumpBit_rateTable, CRUMPBIT_DELAY_CURVE_MAX, crumpBit_rateLimit().
 */
float crumpBit_rateSamples(uint8_t rate);
```

#### H4 — Sync division with a live maximum

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, HEAD lines 100–116: **replace** (17 lines → 49; new lines 142–190).

- **What:** `crumpBit_syncDivision()` gains `max_samples`, the live limit,
  in place of the constant 1.60 s.
- **Why:** plan §5.1. Divisions are offered up to what the share holds;
  `1br` at 120 BPM (2.0 s) is available with the whole arena.
- **Caller:** `crumpBit_uiFormatValue3()` (C4). It passes the clamped Rate
  and `crumpBit_shareMaxSamples()` of the live share, the same pair
  `process()` uses.

Remove (HEAD 100–116):

```c
/*
 * Step-scale division that Sync snaps a Rate to (Q7).
 *
 * What: among the shared StepScale CC positions (0..127, 96 PPQ ticks) whose
 * length at the given tempo fits CRUMPBIT_DELAY_MAX_SAMPLES, returns the CC
 * nearest in log time to crumpBit_rateSamples(rate). It uses the bracketing
 * pair and the geometric-mean rule: no logarithms, and a fixed 7-step binary
 * search over the 128 ascending positions (S091 R1; it replaced a 128-step
 * walk and returns the same division). If no division fits (below about
 * 4 BPM) it returns 0, and the DSP clamps that length to the maximum.
 * Why shared: the Effect-page label must show exactly the division the DSP
 * plays. Inputs: rate 0..127, bpm from seq_getBpm() (0 is treated as 1).
 * Output: a StepScale CC 0..127.
 * Accessors: crumpBit_uiFormatValue3() (label), CrumpBitEffect.c (target).
 * Affiliates: stepScale_ticksQ8(), stepScale_formatShort().
 */
uint8_t crumpBit_syncDivision(uint8_t rate, uint16_t bpm);
```

Replace with:

```c
/*
 * Longest delay a share holds (S091 P2).
 *
 * What: min(share bytes, CRUMPBIT_LOOP_MAX_BYTES) - 2 samples (the
 * interpolation guard), as a float; 0 when there is no share or it is
 * smaller than the smallest usable loop. It is exactly the clamp
 * crumpBit_process() applies to its target for the same share.
 * Input: share (may be NULL). Output: samples.
 * Accessors: crumpBit_rateLimit(), crumpBit_uiFormatValue3() (Sync label),
 * crumpBit_process() (target clamp).
 */
float crumpBit_shareMaxSamples(const fx_share_t *share);

/*
 * Largest Rate whose free-running delay fits a share (S091 P2).
 *
 * What: the highest rate r with crumpBit_rateTable[r] <= the share's longest
 * delay, by a fixed 7-step binary search of the ascending table (the same
 * shape as the Sync search, constant cost); 0 if even the shortest delay does
 * not fit. With the whole arena it is 127. Why: the manager clamps the
 * resolved `rte` to it before write_param() (effective_max), the page clamps
 * edits to it, and the label shows the clamped value, so a voice-unit
 * reservation never plays or shows a delay the loop cannot hold.
 * Input: share (may be NULL). Output: 0..127.
 * Accessors: crumpBit_ops.effective_max, crumpBit_uiFormatValue3().
 * Affiliates: crumpBit_shareMaxSamples(), crumpBit_rateTable.
 */
uint8_t crumpBit_rateLimit(const fx_share_t *share);

/*
 * Step-scale division that Sync snaps a Rate to (Q7; live maximum S091 P2).
 *
 * What: among the shared StepScale CC positions (0..127, 96 PPQ ticks) whose
 * length at the given tempo fits max_samples, returns the CC nearest in log
 * time to crumpBit_rateSamples(rate). It uses the bracketing pair and the
 * geometric-mean rule: no logarithms, and a fixed 7-step binary search over
 * the 128 ascending positions (S091 R1). If no division fits it returns 0,
 * and the DSP clamps that length to the maximum. Since S091 P2 the limit is
 * the live share (crumpBit_shareMaxSamples()), not a constant, so `1br` at
 * 120 BPM (2.0 s) is offered while the share holds it.
 * Why shared: the Effect-page label must show exactly the division the DSP
 * plays. Inputs: rate 0..127 (the caller passes the share-clamped value),
 * bpm from seq_getBpm() (0 is treated as 1), max_samples from
 * crumpBit_shareMaxSamples() for the same share the DSP uses.
 * Output: a StepScale CC 0..127.
 * Accessors: crumpBit_uiFormatValue3() (label), CrumpBitEffect.c (target).
 * Affiliates: stepScale_ticksQ8(), stepScale_formatShort().
 */
uint8_t crumpBit_syncDivision(uint8_t rate, uint16_t bpm, float max_samples);
```

### 3.2 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`

#### E1 — Include the tables

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, insert after HEAD line 20. New lines 21–21.

- **What:** includes `CrumpBitTables.h` (N1).
- **Why:** the Rate, tone and cutoff tables replace per-block
  transcendental math.

Insert:

```c
#include "CrumpBitTables.h"     /* Rate, tone and cutoff tables (S091 P2) */
```

#### E2 — Private constants: guard, feedback curve, DC blocker, knee, damping

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 23–44: **replace** (22 lines → 52; new lines 24–75).

- **What:**
  - Removes `CRUMPBIT_RATE_LN_SPAN` (the `expf` curve) and
    `CRUMPBIT_FEEDBACK_MAX` 0.99.
  - Adds `CRUMPBIT_LOOP_GUARD` (2 samples, previously the literal `2`) and
    the P2 constants:

    | Constant | Value | Meaning |
    |---|---|---|
    | `CRUMPBIT_FEEDBACK_UNITY_ROW` | 100 | `fbk` that gives exactly 1.0 (Q3) |
    | `CRUMPBIT_FEEDBACK_ABOVE_STEP` | 0.2/27 | 127 → 1.2 (§0A D6) |
    | `CRUMPBIT_DC_R` | 0.99928775 | `1 − 2π·5/44,108.07`: a 5 Hz DC blocker in the loop |
    | `CRUMPBIT_KNEE_IN_MAX`, `CRUMPBIT_KNEE_CUBIC` | 1.5, 4/27 | `y = v − (4/27)v³` on `v` clamped to ±1.5: slope 1 at 0; ±1 with slope 0 at ±1.5 (D7) |
    | `CRUMPBIT_FILTER_R_SNAP`, `CRUMPBIT_FILTER_R_MIN` | 0.1, 0.0625 | the voice `SVF_setReso()` snap point, redirected to Q 8 (Q4) |
- **Why the knee is this cubic:**
  - It is the D7 soft knee without a division (the `tdr` Padé knee is
    deferred).
  - Its ceiling equals the 8-bit rail, so a return alone never clips the
    ADC.
  - The loop gain `fb · y/v` falls below 1 at high level, so
    self-oscillation settles (CB8: peak 1.0, no DC lock-up).

Remove (HEAD 23–44):

```c
/*
 * Private DSP constants (S074_CRUMPBIT_EFFECT.md §3).
 *
 * CRUMPBIT_SAMPLE_RATE_HZ codec rate used for every time conversion.
 * CRUMPBIT_LSB one 8-bit step, 1/128 of int16 full scale.
 * CRUMPBIT_RATE_LN_SPAN ln(1.60 s / 0.020 s) = ln 80, the Rate curve.
 * CRUMPBIT_SAMPLES_PER_TICK samples per 96-PPQ tick at 1 BPM.
 * CRUMPBIT_GLIDE_K one-pole tape glide, tau 150 ms.
 * CRUMPBIT_AC_R AC coupling pole, about 10 Hz.
 * CRUMPBIT_FEEDBACK_MAX feedback gain at row value 127 (bounded < 1).
 * CRUMPBIT_LOOP_MIN smallest usable loop; below it the block passes through
 * untouched (cannot happen with the guaranteed minimum share).
 */
#define CRUMPBIT_SAMPLE_RATE_HZ    44108u
#define CRUMPBIT_LSB               (1.0f / 128.0f)
#define CRUMPBIT_RATE_LN_SPAN      4.3820266f
#define CRUMPBIT_SAMPLES_PER_TICK  27567.5f
#define CRUMPBIT_GLIDE_K           1.5113e-4f
#define CRUMPBIT_AC_R              0.9985755f
#define CRUMPBIT_FEEDBACK_MAX      0.99f
#define CRUMPBIT_LOOP_MIN          4u
#define CRUMPBIT_DIVISION_NONE     0xFFu
```

Replace with:

```c
/*
 * Private DSP constants (S074_CRUMPBIT_EFFECT.md §3).
 *
 * CRUMPBIT_SAMPLE_RATE_HZ codec rate used for every time conversion.
 * CRUMPBIT_LSB one 8-bit step, 1/128 of int16 full scale.
 * CRUMPBIT_SAMPLES_PER_TICK samples per 96-PPQ tick at 1 BPM.
 * CRUMPBIT_GLIDE_K one-pole tape glide, tau 150 ms.
 * CRUMPBIT_AC_R AC coupling pole, about 10 Hz.
 * CRUMPBIT_LOOP_MIN smallest usable loop; below it the block passes through
 * untouched (cannot happen with the guaranteed minimum share).
 * CRUMPBIT_LOOP_GUARD samples kept free behind the write position for the
 * interpolated read (i0 and i0 + 1); the longest delay is length - guard.
 * (S091 P2 removed CRUMPBIT_RATE_LN_SPAN, the expf curve, now
 * crumpBit_rateTable, and CRUMPBIT_FEEDBACK_MAX 0.99, now the curve below.)
 */
#define CRUMPBIT_SAMPLE_RATE_HZ    44108u
#define CRUMPBIT_LSB               (1.0f / 128.0f)
#define CRUMPBIT_SAMPLES_PER_TICK  27567.5f
#define CRUMPBIT_GLIDE_K           1.5113e-4f
#define CRUMPBIT_AC_R              0.9985755f
#define CRUMPBIT_LOOP_MIN          4u
#define CRUMPBIT_LOOP_GUARD        2u
#define CRUMPBIT_DIVISION_NONE     0xFFu

/*
 * S091 P2 constants (S091_P2_CrumpBit_Expanded_Plan.md §5).
 *
 * CRUMPBIT_FEEDBACK_UNITY_ROW the `fbk` value that gives a loop gain of
 * exactly 1.0 (plan Q3); 0..100 is linear 0..1.0.
 * CRUMPBIT_FEEDBACK_ABOVE_STEP the gain added per row step above unity:
 * 0.2 / 27, so 127 gives 1.2, the maximum (§0A D6).
 * CRUMPBIT_DC_R the in-loop DC blocker pole, about 5 Hz:
 * 1 - 2*pi*5/44108.07. Mandatory above unity: any offset in the loop would
 * otherwise grow to a rail.
 * CRUMPBIT_KNEE_IN_MAX / CRUMPBIT_KNEE_CUBIC the fixed soft knee on the
 * feedback return (§0A D7): y = v - (4/27) v^3 on v clamped to +-1.5, which
 * has slope 1 at 0, slope 0 and value +-1 at +-1.5, so the return never
 * exceeds the 8-bit rail and the loop gain falls below 1 at high level
 * (self-oscillation settles). The drive (`tdr`) that would scale it is a
 * later addition (plan §5.3).
 * CRUMPBIT_FILTER_R_SNAP / CRUMPBIT_FILTER_R_MIN the output-filter damping:
 * the voice law SVF_setReso() is R = 1 - res/127 and snaps R below 0.1 to
 * 0.02 (Q 25); here the snap goes to 0.0625 (Q 8, plan Q4), because this
 * filter is linear and nothing limits its resonant peak.
 */
#define CRUMPBIT_FEEDBACK_UNITY_ROW   100u
#define CRUMPBIT_FEEDBACK_ABOVE_STEP  (0.2f / 27.0f)
#define CRUMPBIT_DC_R                 0.99928775f
#define CRUMPBIT_KNEE_IN_MAX          1.5f
#define CRUMPBIT_KNEE_CUBIC           (4.0f / 27.0f)
#define CRUMPBIT_FILTER_R_SNAP        0.1f
#define CRUMPBIT_FILTER_R_MIN         0.0625f
```

#### E3 — Static guards

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 60–74: **replace** (15 lines → 25; new lines 91–115).

- **What:**
  - The size guard becomes 76 B.
  - The S074 buffer guard becomes `LOOP_MAX ≥ curve max + guard`.
  - New guards: the loop is a whole number of 32 B granules, and the Rate
    table has 128 entries (the binary search assumes it).
- **Why:** the build stops if a later edit grows the runtime past the union
  or makes the loop unable to hold the curve top.

Remove (HEAD 60–74):

```c
_Static_assert(sizeof(CrumpBitRuntime) == 56u,
               "CrumpBitRuntime size is recorded in the S074 schedule and "
               "must stay inside the 76 B Effect union");
/*
 * The Sync search (R1) halves a power-of-two span: STEP_SCALE_COUNT steps
 * starting at STEP_SCALE_COUNT/2 visit every position only if the count is a
 * power of two. A future table of another size must change the search, not
 * silently skip positions.
 */
_Static_assert(STEP_SCALE_COUNT == 128u,
               "crumpBit_divisionFor() binary search assumes 128 positions");
_Static_assert(CRUMPBIT_BUFFER_BYTES >=
               (uint32_t)CRUMPBIT_DELAY_MAX_SAMPLES + 2u,
               "the loop must hold the longest delay plus the interpolation "
               "guard");
```

Replace with:

```c
/*
 * S091 P2: 76 B fills the Effect union exactly (EffectsManager.c guards the
 * union at 76 B). A larger runtime grows .dtcmz and shrinks the arena: RAM
 * approval first (the +8 B approved for this work is still unused).
 */
_Static_assert(sizeof(CrumpBitRuntime) == 76u,
               "CrumpBitRuntime size is recorded in the S091 P2 schedule and "
               "must stay inside the 76 B Effect union");
/*
 * The Sync search (R1) halves a power-of-two span: STEP_SCALE_COUNT steps
 * starting at STEP_SCALE_COUNT/2 visit every position only if the count is a
 * power of two. A future table of another size must change the search, not
 * silently skip positions.
 */
_Static_assert(STEP_SCALE_COUNT == 128u,
               "crumpBit_divisionFor() binary search assumes 128 positions");
_Static_assert(CRUMPBIT_LOOP_MAX_BYTES >=
               (uint32_t)CRUMPBIT_DELAY_CURVE_MAX + CRUMPBIT_LOOP_GUARD,
               "the longest loop must hold the top of the Rate curve plus "
               "the interpolation guard");
_Static_assert((CRUMPBIT_LOOP_MAX_BYTES % FXBUF_ALIGN_BYTES) == 0u,
               "the longest loop is a whole number of share granules");
/* The Rate limit search halves the 128-entry table like the Sync search. */
_Static_assert(CRUMPBIT_TABLE_SIZE == 128u,
               "crumpBit_rateLimit() binary search assumes 128 entries");
```

#### E4 — Table Rate, share limit, Rate limit

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 146–152: **replace** (7 lines → 53; new lines 187–239).

- **What:**
  - `crumpBit_rateSamples()` reads `crumpBit_rateTable` (the S074 `expf`
    is gone).
  - `crumpBit_shareMaxSamples()`: the clamp `process()` applies.
  - `crumpBit_rateLimit()`: a fixed eight-probe search of the ascending
    table.
- **Why eight probes:**
  - Steps 64 + 32 + … + 1 sum to 127, so seven probes find any count
    0..127.
  - The eighth probe, at index `count` (≤ 127, in range), adds 1 only when
    every entry fits.
  - Without it the whole arena would give 126, not 127. CB6 catches this,
    and break build 5 proves the law.
- **Cost:** 101 instructions per block, constant, called by
  `effects_service()` through `effective_max` for the BUFFER_DEPENDENT row.

Remove (HEAD 146–152):

```c
float crumpBit_rateSamples(uint8_t rate)
{
    const uint8_t r = (rate > 127u) ? 127u : rate;

    return CRUMPBIT_DELAY_MAX_SAMPLES *
           expf(-(float)r * (CRUMPBIT_RATE_LN_SPAN / 127.0f));
}
```

Replace with:

```c
float crumpBit_rateSamples(uint8_t rate)
{
    /* r: the table index; a row value above 127 reads the top entry. */
    const uint8_t r = (rate > 127u) ? 127u : rate;

    /* S091 P2: a lookup replaces the S074 expf (one per block). */
    return crumpBit_rateTable[r];
}

float crumpBit_shareMaxSamples(const fx_share_t *share)
{
    /* len: the loop length this share seats (crumpBit_seat()'s rule). */
    uint32_t len = share ? share->bytes : 0u;

    if (len > CRUMPBIT_LOOP_MAX_BYTES)
        len = CRUMPBIT_LOOP_MAX_BYTES;
    /* Below the smallest loop the block passes through; nothing fits. */
    if (len < CRUMPBIT_LOOP_MIN)
        return 0.0f;
    return (float)(len - CRUMPBIT_LOOP_GUARD);
}

uint8_t crumpBit_rateLimit(const fx_share_t *share)
{
    const float max_samples = crumpBit_shareMaxSamples(share);
    /*
     * count: how many table entries (from rate 0 up) are <= max_samples.
     * The table ascends strictly, so the fitting entries are a prefix; the
     * search builds count from the top bit down exactly as
     * crumpBit_divisionFor() does (probe count + step - 1, the last entry of
     * the next step-sized run).
     */
    uint32_t count = 0u;
    uint32_t step;

    /*
     * Fixed seven iterations; the add is a compare-select, not a branch.
     * Steps 64 + 32 + ... + 1 sum to 127, so this finds any count 0..127:
     * every entry below count fits, and entry count does not, except that
     * when count reaches 127 entry 127 has not been probed yet.
     */
    for (step = CRUMPBIT_TABLE_SIZE / 2u; step != 0u; step >>= 1u)
        count += (crumpBit_rateTable[count + step - 1u] <= max_samples)
            ? step : 0u;
    /*
     * Eighth probe: entry count (index <= 127, always in range). Below 127 it
     * is the first entry known not to fit and adds 0; at 127 it decides
     * whether the whole table fits (count 128).
     */
    count += (crumpBit_rateTable[count] <= max_samples) ? 1u : 0u;
    /* The longest fitting rate; 0 also when even 20 ms does not fit. */
    return (uint8_t)((count != 0u) ? count - 1u : 0u);
}
```

#### E5 — Sync search: live maximum and the eighth probe

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 170–244: **replace** (75 lines → 90; new lines 257–346).

- **What:** `crumpBit_divisionFor()`:
  - takes `max_samples`: `key = min(target, max)`, and the `t_high > max`
    test;
  - gains the eighth probe;
  - has its comments corrected.
- **Why the max:** Sync offers what the live share holds (plan §5.1).
- **Why the eighth probe:** see §8.1.
  - The S091 R1 comment claimed the seven steps reach 0..128. They reach
    0..127, so the `count == STEP_SCALE_COUNT` branch could not run.
  - The R1 result was still right. When every position passes, position
    127 becomes `high` with `t ≤ target`, and the geometric-mean test
    always picks it.
  - The probe makes the search complete as written. GCC folds it into the
    `t_high` load, so the function size is unchanged (113 instructions).
- **Proof:** CB1 compares against the S074 walk, generalised to the
  limit, for five shares × BPM 0..1000 × 128 rates, all division-length
  ties, and 2 million random targets and limits.

Remove HEAD lines 170–244 (75 lines), from
`/*`
through
`}`.

Replace with:

```c
/*
 * Nearest fitting division for one free-running length (S091 R1; S091 P2
 * takes the limit as a parameter).
 *
 * What: among the StepScale positions whose length fits max_samples (the
 * live share's longest delay, crumpBit_shareMaxSamples()), finds the longest
 * one <= target (low) and the shortest one > target (high), and picks low
 * when target^2 < low*high, which is nearest in log time. Returns 0 if
 * nothing fits.
 * How: the 128 lengths ascend strictly with the CC (stepScale_q8Table is
 * strictly ascending and per_tick > 0), so "fits and <= target" is a prefix
 * of the table: every position below some count passes, none above it does.
 * A fixed 7-step binary search finds that count; low is count - 1 and the
 * only candidate for high is position count.
 * Why: the S074/S078 form walked all 128 positions every audio block
 * (12-19 instructions each, about 2,400 per block, ~76 per frame of the
 * render), from crumpBit_process() via crumpBit_targetSamples(). This form
 * costs about 7 search steps plus three lengths, and returns the same CC for
 * every input (class S0; host law CB1 checks every rate at BPM 1..1000).
 * Cost: constant to within the three early returns (a few instructions per
 * block), never data-dependent in the search itself.
 * Inputs: target length (samples), per_tick (samples per 96-PPQ tick at the
 * current tempo), max_samples (the longest delay allowed; the S074 constant
 * 70,573 until S091 P2). Output: a StepScale CC 0..127.
 * Callers: crumpBit_syncDivision() (Effect-page label, foreground),
 * crumpBit_targetSamples() (render, once per block).
 * Affiliates: crumpBit_divisionLength(), stepScale_ticksQ8(),
 * STEP_SCALE_COUNT.
 */
static uint8_t crumpBit_divisionFor(float target, float per_tick,
                                    float max_samples)
{
    /*
     * key: one threshold for both S074 tests. A position passed the S074 walk
     * as a low candidate when it fitted (t <= MAX) and was not longer than
     * the target (t <= target), i.e. exactly when t <= min(target, MAX).
     * MAX is the live limit since S091 P2.
     */
    const float key = (target < max_samples) ? target : max_samples;
    /*
     * count: how many positions (from CC 0 up) have a length <= key. It is
     * built from the top bit down: after the step of size s, every position
     * below count is known to pass. Probing position count + s - 1 decides
     * whether the next s positions all pass (the lengths ascend, so testing
     * the last of them is enough). Steps 64, 32, ... 1 sum to 127, so they
     * reach any count in 0..127; the eighth probe below completes 0..128
     * (S091 P2: the S091 R1 comment claimed 0..128 for the seven steps).
     */
    uint32_t count = 0u;
    uint32_t step;
    float t_low;
    float t_high;

    /* Fixed seven iterations; the add is a compare-select, not a branch. */
    for (step = STEP_SCALE_COUNT / 2u; step != 0u; step >>= 1u)
        count += (crumpBit_divisionLength((uint8_t)(count + step - 1u),
                                          per_tick) <= key) ? step : 0u;
    /*
     * Eighth probe (S091 P2): position count (<= 127, in range). Below 127 it
     * is already known to fail and adds 0; at 127 it decides whether every
     * position passes (count 128). Without it count stopped at 127 and the
     * `count == STEP_SCALE_COUNT` branch below could not run; the result was
     * still right, because position 127 then became `high` with
     * t <= target, which the geometric-mean test always picks.
     */
    count += (crumpBit_divisionLength((uint8_t)count, per_tick) <= key)
        ? 1u : 0u;

    /*
     * No low candidate: S074 returned high if any position fitted, else 0.
     * high could only be CC 0 here, so both cases return 0.
     */
    if (count == 0u)
        return 0u;
    t_low = crumpBit_divisionLength((uint8_t)(count - 1u), per_tick);
    /* Every position passed: there is no longer one, so high is NONE. */
    if (count == STEP_SCALE_COUNT)
        return (uint8_t)(count - 1u);
    /*
     * Position count is the first that fails key. It is S074's high when it
     * still fits (then it failed only "<= target"); if it is longer than
     * MAX, no later position fits either, so high is NONE.
     */
    t_high = crumpBit_divisionLength((uint8_t)count, per_tick);
    if (t_high > max_samples)
        return (uint8_t)(count - 1u);
    /* Nearest in log time: target below the geometric mean picks low. */
    return (target * target < t_low * t_high)
        ? (uint8_t)(count - 1u) : (uint8_t)count;
}
```

#### E6 — crumpBit_syncDivision() forwards the limit

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 246–252: **replace** (7 lines → 8; new lines 348–355).

- **What:** `crumpBit_syncDivision()` forwards `max_samples`.
- **Why:** H4.

Remove (HEAD 246–252):

```c
uint8_t crumpBit_syncDivision(uint8_t rate, uint16_t bpm)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);

    return crumpBit_divisionFor(crumpBit_rateSamples(rate), per_tick);
}
```

Replace with:

```c
uint8_t crumpBit_syncDivision(uint8_t rate, uint16_t bpm, float max_samples)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);

    return crumpBit_divisionFor(crumpBit_rateSamples(rate), per_tick,
                                max_samples);
}
```

#### E7 — Per-block target clamped to the live loop

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 254–276: **replace** (23 lines → 26; new lines 357–382).

- **What:** `crumpBit_targetSamples()`:
  - takes `max_samples` and clamps to it;
  - computes the snapped length with `crumpBit_divisionLength()`, the same
    expression as before in one place.
- **Why:** the target now follows the live share. A shrinking share bends
  the delay, as a tempo change does.

Remove (HEAD 254–276):

```c
/*
 * Per-block delay target in samples.
 *
 * What: computes both the free-running length and the snapped division every
 * block (constant control cost, whatever Sync is), selects one by `sync`, and
 * clamps it to CRUMPBIT_DELAY_MAX_SAMPLES. A tempo change therefore moves the
 * target, and the glide bends the pitch like a tape machine. Inputs: row
 * values rate and sync, and bpm (seq_getBpm()). Output: target length in
 * samples. Caller: crumpBit_process(). Affiliates: crumpBit_divisionFor(),
 * stepScale_ticksQ8().
 */
static float crumpBit_targetSamples(uint8_t rate, uint8_t sync, uint16_t bpm)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);
    const float free_run = crumpBit_rateSamples(rate);
    const float synced = ((float)stepScale_ticksQ8(
        crumpBit_divisionFor(free_run, per_tick)) / 256.0f) * per_tick;
    const float target = sync ? synced : free_run;

    return (target > CRUMPBIT_DELAY_MAX_SAMPLES)
        ? CRUMPBIT_DELAY_MAX_SAMPLES : target;
}
```

Replace with:

```c
/*
 * Per-block delay target in samples.
 *
 * What: computes both the free-running length and the snapped division every
 * block (constant control cost, whatever Sync is), selects one by `sync`, and
 * clamps it to max_samples, the live share's longest delay (S091 P2; it was
 * the constant 1.60 s). A tempo change therefore moves the target, and the
 * glide bends the pitch like a tape machine; a shrinking share does the same.
 * Inputs: row values rate (already share-clamped by effective_max) and sync,
 * bpm (seq_getBpm()), max_samples. Output: target length in samples.
 * Caller: crumpBit_process(). Affiliates: crumpBit_divisionFor(),
 * crumpBit_divisionLength(), crumpBit_shareMaxSamples().
 */
static float crumpBit_targetSamples(uint8_t rate, uint8_t sync, uint16_t bpm,
                                    float max_samples)
{
    const float per_tick =
        CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);
    const float free_run = crumpBit_rateSamples(rate);
    /* The division the label shows, at the same tempo and limit. */
    const float synced = crumpBit_divisionLength(
        crumpBit_divisionFor(free_run, per_tick, max_samples), per_tick);
    const float target = sync ? synced : free_run;

    return (target > max_samples) ? max_samples : target;
}
```

#### E8 — Elastic seat: keep the tail, clamp the delay

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 278–300: **replace** (23 lines → 43; new lines 384–426).

- **What:** `crumpBit_seat()` becomes elastic:
  - **length:** `min(share, LOOP_MAX)`;
  - **keep the tail (D8):** when a loop was seated and `write_pos` lies
    inside the new length, keep `write_pos` and cut `valid` to it.
    Otherwise re-seat as S074 did;
  - **clamp the gliding `delay`** to `length − 2`.
- **Why the clamp must exist:** the read index is
  `write_pos − delay + length` at most once.
  - If a share shrinks below the current delay, `rp` stays negative.
    `(uint32_t)rp` is then out of range, and the read leaves the loop
    (outside the share).
  - The jump the clamp causes is inaudible: everything older than
    `write_pos` is muted by `valid`.
  - Break build 4 removes the clamp; the CB4 seat invariant catches it.
- **Why the tail is valid:** voice units are taken from the arena top, so
  bytes `[0, write_pos)` are still the newest audio in order. Reads further
  back than `write_pos` would see older bytes at the wrong age, which is
  why `valid` is cut.
- **Hardware:** no Instrument claims voice units at run time yet, so a share
  change happens only through `DEV_FXBUF_FORCE_VOICE_UNITS` at boot. CB4
  changes the share every 3,001 blocks.

Remove (HEAD 278–300):

```c
/*
 * Seat the loop in the current Effect share.
 *
 * What: the loop length is min(share bytes, CRUMPBIT_BUFFER_BYTES). When it
 * differs from the seated length (first block after init, or a share change),
 * the loop is re-seated: write position 0 and valid 0, so old content is
 * muted rather than read with the wrong geometry. Inputs: runtime, share (may
 * be NULL). Output: loop length (0 if no share). Callers: crumpBit_process(),
 * crumpBit_bufferChanged(). Affiliates: FxBuffer share contract.
 */
static uint32_t crumpBit_seat(CrumpBitRuntime *rt, const fx_share_t *share)
{
    uint32_t len = share ? share->bytes : 0u;

    if (len > CRUMPBIT_BUFFER_BYTES)
        len = CRUMPBIT_BUFFER_BYTES;
    if (len != rt->length) {
        rt->length = len;
        rt->write_pos = 0u;
        rt->valid = 0u;
    }
    return len;
}
```

Replace with:

```c
/*
 * Seat the loop in the current Effect share (elastic since S091 P2).
 *
 * What: the loop length is min(share bytes, CRUMPBIT_LOOP_MAX_BYTES). When it
 * differs from the seated length (first block after init, or a share change):
 * - keep the tail (plan D8) when a loop was seated and write_pos still lies
 *   inside the new length. Voice units are taken from the arena top, so bytes
 *   [0, write_pos) are still the newest audio, in order; valid is cut to
 *   write_pos, which mutes every read that would reach older bytes that now
 *   sit at the wrong age;
 * - otherwise re-seat as S074 did: write position 0 and valid 0, so old
 *   content is muted rather than read with the wrong geometry;
 * - in both cases clamp the gliding delay to length - guard. The read index
 *   is formed as write_pos - delay plus at most one length, so a delay longer
 *   than the new loop would index outside it; the clamp is a jump, but the
 *   audio it skips is muted by valid anyway. The per-block target is clamped
 *   to the same limit, so the glide never leaves it again.
 * Inputs: runtime, share (may be NULL). Output: loop length (0 if no share).
 * Callers: crumpBit_process(), crumpBit_bufferChanged().
 * Affiliates: FxBuffer share contract, crumpBit_shareMaxSamples().
 */
static uint32_t crumpBit_seat(CrumpBitRuntime *rt, const fx_share_t *share)
{
    uint32_t len = share ? share->bytes : 0u;

    if (len > CRUMPBIT_LOOP_MAX_BYTES)
        len = CRUMPBIT_LOOP_MAX_BYTES;
    if (len != rt->length) {
        if (rt->length != 0u && rt->write_pos < len) {
            /* Tail kept: only the samples written since the last wrap count. */
            if (rt->valid > rt->write_pos)
                rt->valid = rt->write_pos;
        } else {
            rt->write_pos = 0u;
            rt->valid = 0u;
        }
        rt->length = len;
        if (len >= CRUMPBIT_LOOP_MIN &&
            rt->delay > (float)(len - CRUMPBIT_LOOP_GUARD))
            rt->delay = (float)(len - CRUMPBIT_LOOP_GUARD);
    }
    return len;
}
```

#### E9 — Init seeds: flat tone, filter off

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, insert after HEAD line 329. New lines 456–465.

- **What:** `crumpBit_init()` also seeds a flat tone, the filter `off`,
  and the filter ramp origin `off`.
- **Why:**
  - The manager zeroes the union first. A zero `tone_raw` is the 150 Hz
    low-pass, and a zero filter type is LP.
  - The forced resolution writes the real rows before the first
    `process()`, but the ramp origin comes from `filter_type_prev`. Seeding
    it `off` makes entering CrumpBit with a filter type set fade the filter
    in over one block, instead of starting from an LP with zero state.
  - The delay ramp origins are left zero (mix 0, fb 0). In the first block
    `valid` = 0 mutes `wet`, so the gains cannot be heard. CB4 compares
    from block 0 against a reference whose stored origins are S074's zeros.

Insert:

```c
    /*
     * S091 P2 neutral rows until the forced resolution writes the real ones
     * (it runs before the first process()): a flat tone, and the output
     * filter `off`. The previous-type seed makes the first block's output
     * ramp start from `off`, so entering CrumpBit with a filter type set
     * fades the filter in over one block instead of from the zeroed LP.
     */
    rt->tone_raw = CRUMPBIT_TONE_FLAT;
    rt->filter_type = CRUMPBIT_FILTER_OFF;
    rt->filter_type_prev = CRUMPBIT_FILTER_OFF;
```

#### E10 — write_param: the new rows

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 364–388: **replace** (25 lines → 31; new lines 500–530).

- **What:** `write_param()` stores the four new rows as raw bytes. The
  comment notes where a second sub-type would store `sub`.
- **Why:** all conversion happens once per block in `process()`, so an LFO
  on `ton` or `frq` costs nothing extra.

Remove (HEAD 364–388):

```c
/*
 * Store one resolved row value.
 *
 * What: keeps the raw 0..255 value of rows 3..10. Every conversion (masks,
 * gains, curve, division) happens once per block in process(), so an LFO that
 * rewrites a row every block costs the same as a static setting. The sub-type
 * row is ignored (only `dly` exists). Inputs: runtime, row index, value
 * (already clamped to the descriptor maximum). Output: runtime fields.
 * Caller: effects_service(). Affiliates: crumpBit_descriptors.
 */
static void crumpBit_writeParam(void *rt_void, uint8_t index, uint8_t value)
{
    CrumpBitRuntime *rt = (CrumpBitRuntime *)rt_void;

    switch (index) {
    case CRUMPBIT_PARAM_BIT_OFF:    rt->bit_off = value;               break;
    case CRUMPBIT_PARAM_BIT_INVERT: rt->bit_invert = value;            break;
    case CRUMPBIT_PARAM_MIX:        rt->mix_raw = value;               break;
    case CRUMPBIT_PARAM_FEEDBACK:  rt->feedback_raw = value;          break;
    case CRUMPBIT_PARAM_RATE:       rt->rate = value;                  break;
    case CRUMPBIT_PARAM_SYNC:       rt->sync = (uint8_t)(value != 0u); break;
    case CRUMPBIT_PARAM_DLY_PAN:    rt->pan_raw = value;               break;
    default:                                                           break;
    }
}
```

Replace with:

```c
/*
 * Store one resolved row value.
 *
 * What: keeps the raw 0..255 value of rows 3..14. Every conversion (masks,
 * gains, curve, division, tone and filter coefficients) happens once per
 * block in process(), so an LFO that rewrites a row every block costs the
 * same as a static setting. The sub-type row is ignored (only `dly` exists;
 * a second sub-type would store it here, plan §4 point 5). Inputs: runtime, row index, value
 * (already clamped to the descriptor maximum). Output: runtime fields.
 * Caller: effects_service(). Affiliates: crumpBit_descriptors.
 */
static void crumpBit_writeParam(void *rt_void, uint8_t index, uint8_t value)
{
    CrumpBitRuntime *rt = (CrumpBitRuntime *)rt_void;

    switch (index) {
    case CRUMPBIT_PARAM_BIT_OFF:    rt->bit_off = value;               break;
    case CRUMPBIT_PARAM_BIT_INVERT: rt->bit_invert = value;            break;
    case CRUMPBIT_PARAM_MIX:        rt->mix_raw = value;               break;
    case CRUMPBIT_PARAM_FEEDBACK:  rt->feedback_raw = value;          break;
    case CRUMPBIT_PARAM_RATE:       rt->rate = value;                  break;
    case CRUMPBIT_PARAM_SYNC:       rt->sync = (uint8_t)(value != 0u); break;
    case CRUMPBIT_PARAM_DLY_PAN:    rt->pan_raw = value;               break;
    /* S091 P2 rows: raw bytes only, converted per block. */
    case CRUMPBIT_PARAM_TONE:        rt->tone_raw = value;             break;
    case CRUMPBIT_PARAM_FILTER_TYPE: rt->filter_type = value;          break;
    case CRUMPBIT_PARAM_FILTER_FREQ: rt->filter_freq = value;          break;
    case CRUMPBIT_PARAM_FILTER_RESO: rt->filter_reso = value;          break;
    default:                                                           break;
    }
}
```

#### E11 — Gain and filter helpers; the three-stage process()

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 390–585: **replace** (196 lines → 484; new lines 532–1015).

This unit replaces the whole S091 P1 `crumpBit_process()`. Before it, it
adds five per-block helpers and one table.

- **`crumpBit_mixGain()`, `crumpBit_feedbackGain()`, `crumpBit_panGainL/R()`:**
  one pure function per delay row.
  - `process()` calls each twice: on the previous block's raw byte (the
    ramp origin) and on the current one (the target). That replaces the
    four stored floats, and the result is identical.
  - CB4's reference stores the floats as S074 did and matches bit for bit.
  - The feedback law is Q3: `v/100` up to 100, then `1 + (v − 100)·0.2/27`.
    `100 · (1/100)` rounds to exactly 1.0f.
- **`crumpBit_filterDamping()`:** the voice `res` law, `R = 1 − res/127`,
  with the voice's low-end snap redirected from 0.02 to 0.0625 (Q ≤ 8,
  Q4). The linear TPT filter does not limit its own peak; Q 8 is +18 dB.
- **`crumpBit_filterMixTable` and `crumpBit_filterMix()`:** every type is
  `out = cx·x + cb·bp + cl·lp`, with `cb = cb0 + cbk·k` and `k = 2R`.
  - The rows are the voice filter's output forms (`ResonantFilter.c`
    switch: `y1` = lp, `y0` = bp, `h = x − k·bp − lp`).
  - `LP2` = LP (Q9); `off` = `(1, 0, 0)`.
  - All types do the same arithmetic.
- **`crumpBit_process()`:** three stages, every statement on every frame.
  1. **Shared:** ADC, data lines, DAC and AC coupling. Unchanged from
     S091 P1, including the R4 `vcvt #7` shape.
  2. **Delay sub-type:**
     - glide, interpolated read and `valid` mute, unchanged;
     - then the **feedback return:** DC blocker → tone tilt (`a·s + b·v`;
       LP (1, 0), flat (0, 1), HP (−1, 1)) → `× fb` → cubic knee;
     - the write `adc(½(hL + hR) + ret)`;
     - the dry/wet crossfade on the **raw** `wet`, so the first echo is
       unshaped and repeat *n* is shaped *n* − 1 times (§0A D5).
  3. **Shared output filter**, a second pass over the block:
     - a Simper TPT SVF per channel;
     - per-block `g` from `crumpBit_filterG`, `a1 = 1/(1 + g(g + k))` (one
       VDIV per block), `a2 = g·a1`, `a3 = g·a2`;
     - the 3-coefficient mix ramps linearly from the previous block's set
       to this block's. That makes a `typ` Morph step, a Pattern type lock
       or a `res` step a one-block crossfade.
     - In a mono block (`right` aliases `left`) both inputs are read
       before either output is stored, so the left result is kept, as in
       stage 2.
- **Exactness claims, each proved by CB4 against an independent
  reference:**
  - A flat tone passes the return bit for bit. The weights are `0·s + dc`,
    an exact FMA; break build 3 sets the flat `a` to 1 and is caught.
  - A settled `off` passes the input bit for bit: `1·x + 0·bp + 0·lp`.
  - The recomputed ramp origins equal the stored ones.
- **Constant CPU:**
  - The only per-block branches are selects: tone side, filter row, the
    early returns.
  - Stage 2 keeps S074's `valid`-mute branch (112 vs 126 instructions).
    The budget uses the longer path.

Remove HEAD lines 390–585 (196 lines), from
`/*`
through
`}`.

Replace with:

```c
/*
 * Ramp targets of the delay rows (S091 P2 §8: the ramp origins are no longer
 * stored as floats).
 *
 * What: the per-block conversion of one raw row to its gain, as one pure
 * function each. process() calls each twice per block, once on the previous
 * block's raw byte (the ramp origin, which S074 stored as the previous
 * target) and once on the current one (the new target). Because the function
 * and its input are the same, the origin is bit-identical to the float S074
 * stored, and RAM drops by 13 B net.
 * - mix: row/127, 0..1;
 * - feedback (plan Q3, §0A D6): 0..100 -> 0..1.0 linear, then
 *   100..127 -> 1.0..1.2. 100 * (1/100) rounds to exactly 1.0f;
 * - delay pan balance centred on 63 (S075 F2-E, F2-Q4): 63 is both-side
 *   unity and displays 0, 0 is right silent, and 127 is left silent. This
 *   matches the Effect return stereo law in mixer.c.
 * Inputs: raw row 0..127 (the descriptor maximum). Output: gain.
 * Callers: crumpBit_process(). Affiliates: crumpBit_descriptors.
 */
static inline float crumpBit_mixGain(uint8_t raw)
{
    return (float)raw * (1.0f / 127.0f);
}

static inline float crumpBit_feedbackGain(uint8_t raw)
{
    return (raw <= CRUMPBIT_FEEDBACK_UNITY_ROW)
        ? (float)raw * (1.0f / (float)CRUMPBIT_FEEDBACK_UNITY_ROW)
        : 1.0f + (float)(raw - CRUMPBIT_FEEDBACK_UNITY_ROW) *
                 CRUMPBIT_FEEDBACK_ABOVE_STEP;
}

static inline float crumpBit_panGainL(uint8_t pan)
{
    return (pan <= 63u) ? 1.0f : (float)(127u - pan) * (1.0f / 64.0f);
}

static inline float crumpBit_panGainR(uint8_t pan)
{
    return (pan >= 63u) ? 1.0f : (float)pan * (1.0f / 63.0f);
}

/*
 * Output-filter damping R from the `res` row (S091 P2 §5.4).
 *
 * What: the voice law SVF_setReso(): R = 1 - res/127, so res 0 is R = 1
 * (Q 0.5, no peak). Where the voice snaps R below 0.1 to 0.02 (Q 25), this
 * snaps to CRUMPBIT_FILTER_R_MIN = 0.0625 (Q 8, plan Q4): the TPT filter here
 * is linear, so its resonant peak (+18 dB at Q 8) is not limited by the
 * filter itself, only by the mixer return's final saturation.
 * Input: res 0..127. Output: R in [0.0625, 1].
 * Callers: crumpBit_process() (current and previous block).
 * Affiliates: ResonantFilter.c SVF_setReso(), crumpBit_filterMix().
 */
static inline float crumpBit_filterDamping(uint8_t reso)
{
    /* q: the voice filter's damping before its low-end snap. */
    const float q = 1.0f - (float)reso * (1.0f / 127.0f);

    return (q < CRUMPBIT_FILTER_R_SNAP) ? CRUMPBIT_FILTER_R_MIN : q;
}

/*
 * Output-mix coefficients of the filter types (S091 P2 §5.4).
 *
 * What: every type is one weighted sum of the same three signals of the TPT
 * state-variable filter, out = cx*x + cb*bp + cl*lp, so all eight types run
 * the same arithmetic (constant CPU) and a type change is a coefficient
 * change that process() ramps over one block. cb depends on the damping, so
 * each row stores cb as cb0 + cbk*k with k = 2R. With y1 = lp, y0 = bp and
 * h = x - k*bp - lp, the forms are the voice filter's (ResonantFilter.c
 * SVF_calcBlockZDF output switch):
 *   LP   y1          HP   h            BP   y0          UBP  2R*y0
 *   Nch  x - 2R*y0   Pek  y1 - h       LP2  = LP (plan Q9; the voice LP2 is
 *   a different naive 2-pole)          off  x, exactly once the ramp settles.
 * The voice LP's output tanh and FILTER_GAIN (0.883) are not copied: this
 * stage is linear up to the mixer return's final saturation.
 * Why a table, not a switch: a lookup is branch-free per block, and a switch
 * compiles to a jump table whose data bytes the loop-path gate would read as
 * instructions.
 * Index: CRUMPBIT_FILTER_* (MENU_FILTER order). Columns: cx, cb0, cbk, cl.
 * Accessors: crumpBit_filterMix(). Affiliates: CrumpBitParameters.h.
 */
static const float crumpBit_filterMixTable[CRUMPBIT_FILTER_OFF + 1u][4] = {
    /*            cx     cb0    cbk    cl  */
    /* LP   */ {  0.0f,  0.0f,  0.0f,  1.0f },
    /* HP   */ {  1.0f,  0.0f, -1.0f, -1.0f },
    /* BP   */ {  0.0f,  1.0f,  0.0f,  0.0f },
    /* UBP  */ {  0.0f,  0.0f,  1.0f,  0.0f },
    /* Nch  */ {  1.0f,  0.0f, -1.0f,  0.0f },
    /* Pek  */ { -1.0f,  0.0f,  1.0f,  2.0f },
    /* LP2  */ {  0.0f,  0.0f,  0.0f,  1.0f },
    /* off  */ {  1.0f,  0.0f,  0.0f,  0.0f },
};

/*
 * Output-mix coefficients of one filter type.
 *
 * What: reads the type's row (a value above 7 is treated as `off`) and forms
 * cb = cb0 + cbk*k with k = 2R, the TPT damping term (the SVF's 1/Q).
 * Inputs: type 0..7, damping R. Outputs: *cx, *cb, *cl.
 * Callers: crumpBit_process() (origin and target of the block ramp).
 * Affiliates: crumpBit_filterMixTable, crumpBit_filterDamping().
 */
static inline void crumpBit_filterMix(uint8_t type, float r,
                                      float *cx, float *cb, float *cl)
{
    const float *row =
        crumpBit_filterMixTable[(type > CRUMPBIT_FILTER_OFF)
                                ? CRUMPBIT_FILTER_OFF : type];

    *cx = row[0];
    *cb = row[1] + row[2] * (2.0f * r);
    *cl = row[3];
}

/*
 * Process one Effect bus block in place (S074_CRUMPBIT_EFFECT.md §3; S091 P2
 * §4-§5).
 *
 * What: three stages, in this order, every one on every frame whatever the
 * settings (constant CPU):
 * 1. shared, per channel: 8-bit ADC -> data lines -> DAC -> AC coupling;
 * 2. sub-type `dly` (the only sub-type): the mono tape loop. The feed
 *    0.5*(hL + hR) plus the shaped feedback return is re-quantised by the same
 *    ADC into the loop; the fractional read uses linear interpolation and is
 *    muted beyond valid; the feedback return is DC-blocked, tilted by `ton`,
 *    scaled by `fbk` (up to 1.2) and soft-kneed; the dry/wet crossfade with
 *    balance-only delay pan uses the raw read, so the first echo is
 *    unshaped and repeat n is shaped n - 1 times;
 * 3. shared output filter: a linear TPT state-variable filter per channel
 *    with one 3-coefficient output mix selected by `typ` (`off` passes the
 *    input), as the last element of the Effect.
 * Stage 3 is a second pass over the block: it keeps the stage-2 loop's
 * register pressure unchanged and gives a future sub-type the same output
 * stage without touching it (plan §4 point 3).
 * Inputs: runtime and normalized stereo io. Output: io->l/io->r in place and
 * loop bytes in the share. Caller: effects_process().
 * Affiliates: crumpBit_adc(), crumpBit_targetSamples(), crumpBit_seat(),
 * crumpBit_filterMix(), crumpBit_rateTable/toneK/filterG, seq_getBpm().
 */
static void crumpBit_process(void *rt_void, effect_io_t *io)
{
    CrumpBitRuntime *rt = (CrumpBitRuntime *)rt_void;
    float *left;
    float *right;
    int8_t *loop;
    uint32_t len;
    uint32_t keep;
    uint32_t invert;
    uint32_t wp;
    uint32_t valid;
    float flen;
    /* max_samples: the longest delay this loop holds, length - guard. */
    float max_samples;
    float target;
    float delay;
    float mix;
    float fb;
    float gain_l;
    float gain_r;
    float mix_to;
    float fb_to;
    float gain_l_to;
    float gain_r_to;
    float mix_inc;
    float fb_inc;
    float gain_l_inc;
    float gain_r_inc;
    /*
     * ac_cl/ac_cr (S091 R4): the previous data-line code of each channel,
     * 0..255. It replaces the S074 float input memory ac_x_* (= (code -
     * 128)/128 of the previous sample); the code difference carries the same
     * information exactly.
     */
    int32_t ac_cl;
    int32_t ac_cr;
    float ac_yl;
    float ac_yr;
    /* dc_x/dc_y: the in-loop DC blocker's input and output memories. */
    float dc_x;
    float dc_y;
    /*
     * Tone tilt (S091 P2 §5.3): tone_s is the one-pole state, tone_k its
     * coefficient, and the output is tone_a*s + tone_b*v:
     * low-pass (1, 0), flat (0, 1) and high-pass (-1, 1).
     */
    float tone_s;
    float tone_k;
    float tone_a;
    float tone_b;
    /*
     * Output filter (S091 P2 §5.4): a1..a3 are the TPT coefficients of this
     * block; cx/cb/cl the output mix, ramping by cx_inc/cb_inc/cl_inc from
     * the previous block's set to this block's; s_l1..s_r2 the integrators.
     */
    float a1;
    float a2;
    float a3;
    float cx;
    float cb;
    float cl;
    float cx_to;
    float cb_to;
    float cl_to;
    float cx_inc;
    float cb_inc;
    float cl_inc;
    float s_l1;
    float s_l2;
    float s_r1;
    float s_r2;
    float per_frame;
    /*
     * frames (S091 R3): io->frames read once. io->frames is a uint8_t, and a
     * float store may alias any character-typed object, so the S074 loop
     * re-read it from io (and re-extended a uint8_t counter) after every
     * output store. A local copy and a 32-bit counter remove both.
     */
    uint32_t frames;
    uint32_t n;

    if (!rt || !io || !io->l || !io->share || !io->share->base ||
        io->frames == 0u)
        return;
    len = crumpBit_seat(rt, io->share);
    if (len < CRUMPBIT_LOOP_MIN)
        return;
    frames = io->frames;
    left = io->l;
    right = io->r ? io->r : io->l;
    loop = (int8_t *)io->share->base;
    flen = (float)len;
    max_samples = (float)(len - CRUMPBIT_LOOP_GUARD);

    /* Data lines: invert wins over off (spec §3.2). */
    invert = rt->bit_invert;
    keep = (uint32_t)~((uint32_t)rt->bit_off & ~invert) & 0xFFu;

    /*
     * Tape target for this block, clamped to this loop (S091 P2: the loop is
     * elastic); the first block starts on it.
     */
    target = crumpBit_targetSamples(rt->rate, rt->sync, seq_getBpm(),
                                    max_samples);
    if (!rt->primed) {
        rt->delay = target;
        rt->primed = 1u;
    }

    /* Ramp step per frame: the block's ramps land on their targets. */
    per_frame = 1.0f / (float)frames;
    /*
     * Block ramps (spec §3.5): each row's previous target is recomputed from
     * the raw byte it was computed from (S091 P2), then ramped to this
     * block's target.
     */
    mix = crumpBit_mixGain(rt->mix_prev);
    fb = crumpBit_feedbackGain(rt->feedback_prev);
    gain_l = crumpBit_panGainL(rt->pan_prev);
    gain_r = crumpBit_panGainR(rt->pan_prev);
    mix_to = crumpBit_mixGain(rt->mix_raw);
    fb_to = crumpBit_feedbackGain(rt->feedback_raw);
    gain_l_to = crumpBit_panGainL(rt->pan_raw);
    gain_r_to = crumpBit_panGainR(rt->pan_raw);
    mix_inc = (mix_to - mix) * per_frame;
    fb_inc = (fb_to - fb) * per_frame;
    gain_l_inc = (gain_l_to - gain_l) * per_frame;
    gain_r_inc = (gain_r_to - gain_r) * per_frame;

    /*
     * Tone tilt for this block: one table read and a side select. 63 (flat)
     * uses (a, b) = (0, 1), so the return passes bit-exactly, while k = 1
     * keeps the pole tracking its input for a click-free move off centre.
     */
    {
        const uint8_t t = (rt->tone_raw > 127u) ? 127u : rt->tone_raw;

        tone_k = crumpBit_toneK[t];
        tone_a = (t < CRUMPBIT_TONE_FLAT) ? 1.0f
               : ((t == CRUMPBIT_TONE_FLAT) ? 0.0f : -1.0f);
        tone_b = (t < CRUMPBIT_TONE_FLAT) ? 0.0f : 1.0f;
    }

    /*
     * Output filter coefficients for this block (Simper TPT SVF):
     * g = tan(pi fc/fs) from the voice-law table, k = 2R,
     * a1 = 1/(1 + g(g + k)), a2 = g*a1, a3 = g*a2. One division per block;
     * the filter is unconditionally stable under per-block steps.
     */
    {
        const uint8_t f = (rt->filter_freq > 127u) ? 127u : rt->filter_freq;
        const float g = crumpBit_filterG[f];
        const float r = crumpBit_filterDamping(rt->filter_reso);
        float r_from;

        a1 = 1.0f / (1.0f + g * (g + 2.0f * r));
        a2 = g * a1;
        a3 = g * a2;
        /* Output mix: origin from the previous block's rows, then target. */
        r_from = crumpBit_filterDamping(rt->filter_reso_prev);
        crumpBit_filterMix(rt->filter_type_prev, r_from, &cx, &cb, &cl);
        crumpBit_filterMix(rt->filter_type, r, &cx_to, &cb_to, &cl_to);
        cx_inc = (cx_to - cx) * per_frame;
        cb_inc = (cb_to - cb) * per_frame;
        cl_inc = (cl_to - cl) * per_frame;
    }

    delay = rt->delay;
    wp = rt->write_pos;
    valid = rt->valid;
    ac_cl = rt->ac_c_l;
    ac_cr = rt->ac_c_r;
    ac_yl = rt->ac_y_l;
    ac_yr = rt->ac_y_r;
    dc_x = rt->dc_x;
    dc_y = rt->dc_y;
    tone_s = rt->tone_s;

    /*
     * Stages 1 and 2, one stereo frame per iteration. Every statement runs on
     * every frame whatever the settings (constant CPU); the count comes from
     * the hoisted local, so no output store forces a reload (R3).
     */
    for (n = 0u; n < frames; n++) {
        /*
         * Stage 1 (shared). ADC -> data lines per channel: the 8-bit code
         * with the forced-off bits cleared (keep) and the inverted bits
         * flipped (invert), 0..255.
         */
        const int32_t code_l =
            (int32_t)((crumpBit_adc(left[n]) & keep) ^ invert);
        const int32_t code_r =
            (int32_t)((crumpBit_adc(right[n]) & keep) ^ invert);
        /*
         * DAC + AC coupling (S091 R4): h = (y - y_prev) + R*h_prev, where the
         * S074 DAC gave y = (code - 128)/128. Both y values are exact
         * multiples of 2^-7, so their difference is exactly
         * (code - code_prev)/128: the integer difference, converted once.
         * The "- 128" and one float subtract per channel disappear, and the
         * sum with R*h_prev is the same single VFMA as before.
         */
        const float hl = crumpBit_codeToFloat(code_l - ac_cl) +
                         CRUMPBIT_AC_R * ac_yl;
        const float hr = crumpBit_codeToFloat(code_r - ac_cr) +
                         CRUMPBIT_AC_R * ac_yr;
        float rp;
        float frac;
        float wet;
        /* dc: the DC-blocked read; tilt: after the tone; ret: the return. */
        float dc;
        float tilt;
        float ret;
        uint32_t i0;
        uint32_t i1;

        /* AC memories: this frame's code (input) and output, per channel. */
        ac_cl = code_l;
        ac_yl = hl;
        ac_cr = code_r;
        ac_yr = hr;

        /* Stage 2, sub-type `dly` (CRUMPBIT_ROWS_DELAY) from here. */
        mix += mix_inc;
        fb += fb_inc;
        gain_l += gain_l_inc;
        gain_r += gain_r_inc;

        /* Tape glide and fractional read (wrap by compare, not modulo). */
        delay += (target - delay) * CRUMPBIT_GLIDE_K;
        rp = (float)wp - delay;
        rp += (rp < 0.0f) ? flen : 0.0f;
        i0 = (uint32_t)rp;
        frac = rp - (float)i0;
        i0 -= (i0 >= len) ? len : 0u;
        i1 = i0 + 1u;
        i1 -= (i1 >= len) ? len : 0u;
        wet = ((float)loop[i0] +
               frac * (float)(loop[i1] - loop[i0])) * CRUMPBIT_LSB;
        wet = ((delay + 2.0f) <= (float)valid) ? wet : 0.0f;

        /*
         * Feedback return (S091 P2 §5.3): DC blocker
         * y = w - w_prev + R_dc*y_prev (about 5 Hz), so no offset can latch
         * the loop at a rail when the gain is above 1.
         */
        dc = wet - dc_x + CRUMPBIT_DC_R * dc_y;
        dc_x = wet;
        dc_y = dc;
        /* Tone tilt: one pole s += k(v - s), output a*s + b*v. */
        tone_s += tone_k * (dc - tone_s);
        tilt = tone_a * tone_s + tone_b * dc;
        /*
         * Feedback gain, then the fixed soft knee: clamp to +-1.5 and
         * y = v(1 - (4/27)v^2), which is +-1 with zero slope at the clamp.
         */
        ret = fb * tilt;
        ret = (ret > CRUMPBIT_KNEE_IN_MAX) ? CRUMPBIT_KNEE_IN_MAX : ret;
        ret = (ret < -CRUMPBIT_KNEE_IN_MAX) ? -CRUMPBIT_KNEE_IN_MAX : ret;
        ret = ret * (1.0f - CRUMPBIT_KNEE_CUBIC * ret * ret);

        /* Re-record through the 8-bit converter; the knee bounds the loop. */
        loop[wp] = (int8_t)((int32_t)crumpBit_adc(0.5f * (hl + hr) + ret) -
                            128);
        wp++;
        wp -= (wp >= len) ? len : 0u;
        valid += (valid < len) ? 1u : 0u;

        /* Dry/wet crossfade on the raw read: the first echo is unshaped. */
        right[n] = hr + mix * (gain_r * wet - hr);
        left[n] = hl + mix * (gain_l * wet - hl);
    }

    rt->delay = delay;
    rt->write_pos = wp;
    rt->valid = valid;
    /* The last code of the block is the next block's AC input memory. */
    rt->ac_c_l = (int16_t)ac_cl;
    rt->ac_c_r = (int16_t)ac_cr;
    rt->ac_y_l = ac_yl;
    rt->ac_y_r = ac_yr;
    rt->dc_x = dc_x;
    rt->dc_y = dc_y;
    rt->tone_s = tone_s;
    /* The rows this block ramped to are the next block's ramp origins. */
    rt->mix_prev = rt->mix_raw;
    rt->feedback_prev = rt->feedback_raw;
    rt->pan_prev = rt->pan_raw;

    /*
     * Stage 3 (shared): the output filter, a second pass over the block.
     * Both inputs are read before either output is stored, so a mono block
     * (right aliases left) filters the same input on both states and keeps
     * the left result, as stage 2 does.
     */
    s_l1 = rt->svf_l1;
    s_l2 = rt->svf_l2;
    s_r1 = rt->svf_r1;
    s_r2 = rt->svf_r2;
    for (n = 0u; n < frames; n++) {
        const float xl = left[n];
        const float xr = right[n];
        /* v1 = band-pass, v2 = low-pass, v3 = input minus the low state. */
        float v1;
        float v2;
        float v3;
        float yl;
        float yr;

        /* Output-mix ramp: lands on this block's set at the last frame. */
        cx += cx_inc;
        cb += cb_inc;
        cl += cl_inc;

        /*
         * TPT SVF tick (Simper): v3 = x - ic2; v1 = a1*ic1 + a2*v3;
         * v2 = ic2 + a2*ic1 + a3*v3; then the trapezoidal state updates
         * ic1 = 2*v1 - ic1 and ic2 = 2*v2 - ic2.
         */
        v3 = xl - s_l2;
        v1 = a1 * s_l1 + a2 * v3;
        v2 = s_l2 + a2 * s_l1 + a3 * v3;
        s_l1 = 2.0f * v1 - s_l1;
        s_l2 = 2.0f * v2 - s_l2;
        yl = cx * xl + cb * v1 + cl * v2;

        v3 = xr - s_r2;
        v1 = a1 * s_r1 + a2 * v3;
        v2 = s_r2 + a2 * s_r1 + a3 * v3;
        s_r1 = 2.0f * v1 - s_r1;
        s_r2 = 2.0f * v2 - s_r2;
        yr = cx * xr + cb * v1 + cl * v2;

        right[n] = yr;
        left[n] = yl;
    }
    rt->svf_l1 = s_l1;
    rt->svf_l2 = s_l2;
    rt->svf_r1 = s_r1;
    rt->svf_r2 = s_r2;
    /* The rows this block's output mix ramped to are the next origin. */
    rt->filter_type_prev = rt->filter_type;
    rt->filter_reso_prev = rt->filter_reso;
}
```

#### E12 — Share-change comment

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 587–598: **replace** (12 lines → 15; new lines 1017–1031).

- **What:** comment only. A share change now keeps the tail when it can,
  clamps the delay, and is followed by the manager's re-clamp of the Rate.

Remove (HEAD 587–598):

```c
/*
 * Share-change notification (voice units claimed or released).
 *
 * What: re-seats the loop. If the usable length changed, old content is muted
 * through valid. A forced re-resolution follows from the manager. Inputs:
 * runtime and new share. Output: runtime seat fields. Caller:
 * effects_onShareChanged(). Affiliates: crumpBit_seat().
 */
static void crumpBit_bufferChanged(void *rt_void, const fx_share_t *share)
{
    (void)crumpBit_seat((CrumpBitRuntime *)rt_void, share);
}
```

Replace with:

```c
/*
 * Share-change notification (voice units claimed or released).
 *
 * What: re-seats the loop. If the usable length changed, the newest audio is
 * kept when the write position still fits (S091 P2, D8) and older content is
 * muted through valid; otherwise everything is muted. The delay is clamped
 * to the new loop. A forced re-resolution follows from the manager, which
 * re-clamps the Rate through effective_max. Inputs: runtime and new share.
 * Output: runtime seat fields. Caller: effects_onShareChanged().
 * Affiliates: crumpBit_seat().
 */
static void crumpBit_bufferChanged(void *rt_void, const fx_share_t *share)
{
    (void)crumpBit_seat((CrumpBitRuntime *)rt_void, share);
}
```

#### E13 — effective_max op and the ops table

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 600–608: **replace** (9 lines → 25; new lines 1033–1057).

- **What:** `crumpBit_effectiveMax()`, the registry op. It returns
  `crumpBit_rateLimit(share)` for `rte` and 255 for every other row.
  `crumpBit_ops` gets it in place of `NULL`.
- **Why:**
  - `effects_service()` (EffectsManager.c HEAD 1949–1955) already applies
    `effective_max` to BUFFER_DEPENDENT rows after Morph, locks, overlay
    and LFO, and before `write_param()`.
  - Stored values are never rewritten (A30). A Scene saved with a long
    delay plays it again when the share returns.

Remove (HEAD 600–608):

```c
/* CrumpBit operations; the fixed delay range has no buffer-dependent clamp. */
const effect_type_ops_t crumpBit_ops = {
    crumpBit_init,
    crumpBit_exportHandoff,
    crumpBit_writeParam,
    crumpBit_process,
    crumpBit_bufferChanged,
    NULL,
};
```

Replace with:

```c
/*
 * Buffer-dependent clamp hook (S091 P2 §5.1).
 *
 * What: EffectsManager calls it for BUFFER_DEPENDENT rows only, after Morph,
 * locks, overlay and LFO, before write_param(): the Rate is limited to the
 * longest delay the share holds (crumpBit_rateLimit()); any other row is not
 * limited (255). Stored values are never rewritten (A30). Inputs: row index
 * and the share the next block will use. Output: inclusive maximum.
 * Callers: effects_service(), effects_paramEffectiveMax() (page edits).
 * Affiliates: crumpBit_uiFormatValue3() (shows the same clamp).
 */
static uint8_t crumpBit_effectiveMax(uint8_t index, const fx_share_t *share)
{
    return (index == CRUMPBIT_PARAM_RATE) ? crumpBit_rateLimit(share) : 255u;
}

/* CrumpBit operations; since S091 P2 the Rate has a buffer-dependent clamp. */
const effect_type_ops_t crumpBit_ops = {
    crumpBit_init,
    crumpBit_exportHandoff,
    crumpBit_writeParam,
    crumpBit_process,
    crumpBit_bufferChanged,
    crumpBit_effectiveMax,
};
```

### 3.3 `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`

#### P1 — Row count

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 35–36: **replace** (2 lines → 7; new lines 35–41).

- **What:** `CRUMPBIT_PARAM_COUNT` 11 → 15, with a note that the deferred
  drive rows would append as 15 and 16.

Remove (HEAD 35–36):

```c
/* Descriptor rows: common rows 0..2 plus type rows 3..10. */
#define CRUMPBIT_PARAM_COUNT 11u
```

Replace with:

```c
/*
 * Descriptor rows: common rows 0..2 plus type rows 3..14.
 * S091 P2 appended rows 11..14 (`ton`, `typ`, `frq`, `res`); the drive rows
 * of S091_P2_CrumpBit_Expanded_Plan.md (`drv`, `tdr`) are a later addition
 * and would append as 15 and 16.
 */
#define CRUMPBIT_PARAM_COUNT 15u
```

#### P2 — New rows, ownership masks, tone centre, filter type ids

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 56–62: **replace** (7 lines → 80; new lines 61–140).

- **What:**
  - Appends `CRUMPBIT_PARAM_TONE` (11), `FILTER_TYPE` (12), `FILTER_FREQ`
    (13) and `FILTER_RESO` (14). Indices are permanent: lanes, Pattern
    targets, LFO tokens and file keys use them.
  - Adds the stage-ownership masks:
    - `CRUMPBIT_ROWS_SHARED`: `bof biv sub typ frq res`;
    - `CRUMPBIT_ROWS_DELAY`: `mix fbk rte snc dpn ton`.
  - Adds `CRUMPBIT_TONE_FLAT` (63) and the filter type ids in `MENU_FILTER`
    order (the voice/StereoFilter values, `FILTER_*` − 1).
- **Why (U3):**
  - The masks are the single record of which rows belong to the shared
    stages and which to the delay sub-type.
  - A second sub-type adds its own mask. A row appended without an owner
    fails the build (C3).
  - `ton` is a delay row because it sits on the delay's feedback return.

Remove (HEAD 56–62):

```c
    CRUMPBIT_PARAM_SYNC,
    CRUMPBIT_PARAM_DLY_PAN,
    CRUMPBIT_PARAM_ENUM_COUNT
} crumpbit_param_t;

/* Sub-effect behind Mix/Feedback/Rate; only the delay exists in v1. */
#define CRUMPBIT_SUBTYPE_DELAY 0u
```

Replace with:

```c
    CRUMPBIT_PARAM_SYNC,
    CRUMPBIT_PARAM_DLY_PAN,
    /* S091 P2, delay sub-type: feedback-return tone tilt (63 = flat). */
    CRUMPBIT_PARAM_TONE,
    /* S091 P2, shared output stage: the last element of the Effect. */
    CRUMPBIT_PARAM_FILTER_TYPE,
    CRUMPBIT_PARAM_FILTER_FREQ,
    CRUMPBIT_PARAM_FILTER_RESO,
    CRUMPBIT_PARAM_ENUM_COUNT
} crumpbit_param_t;

/* Sub-effect behind Mix/Feedback/Rate; only the delay exists in v1. */
#define CRUMPBIT_SUBTYPE_DELAY 0u

/*
 * Row ownership by stage (S091 P2 §4; the single record of which rows belong
 * to which CrumpBit stage).
 *
 * What: one 32-bit mask per owner, bit n = descriptor index n.
 * - CRUMPBIT_ROWS_SHARED: the stages every sub-type runs: the ADC data-line
 *   masks (`bof`, `biv`), the sub-type selector itself (`sub`), and the
 *   output filter (`typ`, `frq`, `res`), which is the last element of the
 *   Effect whatever the sub-type is.
 * - CRUMPBIT_ROWS_DELAY: the delay sub-type's rows (`mix`, `fbk`, `rte`,
 *   `snc`, `dpn`, `ton`). `ton` belongs here because it sits on the delay's
 *   feedback return (user, 2026-11-04).
 * Why: a second sub-type must add its own mask here and may reuse delay rows
 * only deliberately; the _Static_asserts in CrumpBitParameters.c prove that
 * the masks are disjoint and cover every type row 3..COUNT-1, so a row
 * appended without an owner fails the build. The common rows 0..2 belong to
 * the manager and are in neither mask.
 * Accessors: CrumpBitParameters.c (guards), tools/dsp_test crumpbit (law).
 * Affiliates: crumpbit_param_t, CRUMPBIT_SUBTYPE_DELAY, crumpBit_process()
 * stage comments.
 */
#define CRUMPBIT_ROW_BIT(index_) (1UL << (uint32_t)(index_))
#define CRUMPBIT_ROWS_SHARED                                  \
    (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_BIT_OFF) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_BIT_INVERT) |            \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_SUBTYPE) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_TYPE) |           \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_FREQ) |           \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_RESO))
#define CRUMPBIT_ROWS_DELAY                                   \
    (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_MIX) |                   \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FEEDBACK) |              \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_RATE) |                  \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_SYNC) |                  \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_DLY_PAN) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_TONE))

/*
 * Tone tilt centre (S091 P2 §5.3): stored 63 displays 0 through DTYPE_PM63
 * and selects the flat table entry (crumpBit_toneK[63] == 1.0).
 * Accessors: crumpBit_descriptors (default), crumpBit_process() (side select).
 */
#define CRUMPBIT_TONE_FLAT 63u

/*
 * Output filter types (S091 P2 §5.4), stored by the `typ` row 0..7.
 *
 * What: the MENU_FILTER display order (MenuText.h filterTypes: LP HP BP UBP
 * Nch Pek LP2 off), so the row shows and stores exactly what a voice filter
 * type does (user U1). Each value is ResonantFilter's FILTER_* id minus 1,
 * the same offset StereoFilter applies (`svf_type = value + 1`).
 * CRUMPBIT_FILTER_LP2 is rendered as LP: the voice LP2 is a different naive
 * 2-pole structure that would break constant CPU here (plan Q9).
 * CRUMPBIT_FILTER_OFF passes the input exactly once its one-block coefficient
 * ramp has settled; the filter still runs (constant CPU, warm state).
 * Accessors: crumpBit_descriptors (default/max), crumpBit_filterMix().
 * Affiliates: MENU_FILTER, StereoFilterEffect.c stereoFilter_writeParam().
 */
#define CRUMPBIT_FILTER_LP    0u
#define CRUMPBIT_FILTER_HP    1u
#define CRUMPBIT_FILTER_BP    2u
#define CRUMPBIT_FILTER_UBP   3u
#define CRUMPBIT_FILTER_NOTCH 4u
#define CRUMPBIT_FILTER_PEAK  5u
#define CRUMPBIT_FILTER_LP2   6u
#define CRUMPBIT_FILTER_OFF   7u
```

#### P3 — Layout and hooks comment

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 69–75: **replace** (7 lines → 9; new lines 147–155).

- **What:** the header comment on the layout and hooks: four screens, and
  the `sub`/Rate value text.

Remove (HEAD 69–75):

```c
/*
 * Effect-page layout and hooks.
 *
 * crumpBit_layout places the overlay (home), page 2 and page 3 on SELECT 2.
 * crumpBit_ui owns the SELECT buttons (bit toggles), the SELECT LEDs (bit
 * states), the overlay's top row and the `sub`/Sync value text.
 */
```

Replace with:

```c
/*
 * Effect-page layout and hooks.
 *
 * crumpBit_layout places the overlay (home) and pages 2..4 on SELECT 2
 * (S091 P2: four screens, the layout maximum). crumpBit_ui owns the SELECT
 * buttons (bit toggles), the SELECT LEDs (bit states), the overlay's top row
 * and the `sub`/Rate value text (the Rate shows the share-clamped value and,
 * with Sync on, the division the DSP plays).
 */
```

### 3.4 `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`

#### C1 — Include FxBuffer.h

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, insert after HEAD line 20. New lines 21–21.

- **What:** includes `FxBuffer.h` for `fxbuf_effectShare()`, which the Rate
  label clamp (C4) reads.

Insert:

```c
#include "FxBuffer.h"   /* fxbuf_effectShare(): the Rate label clamp (S091 P2) */
```

#### C2 — Descriptor comment and rows

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 28–73: **replace** (46 lines → 77; new lines 29–105).

- **What:**
  - The descriptor comment covers rows 3..14.
  - `rte` becomes `EFFECT_ROW_FLAGS(..., EFFECT_PARAM_FLAG_BUFFER_DEPENDENT)`
    (R1).
  - Four rows are appended, in enum order:

    | Row | Definition | Default |
    |---|---|---|
    | `crump_tone` `ton` | Delay, `DTYPE_PM63`, full image (Morph, LFO, Pattern) | 63 (flat) |
    | `crump_filter_type` `typ` | Filter, `MENU_FILTER`, Morph + Pattern, no LFO (U2) | 7 (`off`), max 7 |
    | `crump_filter_freq` `frq` | Filter, `DTYPE_0B127`, full image | 64 |
    | `crump_filter_reso` `res` | Filter, `DTYPE_0B127`, full image | 0 |

- **Why:**
  - Names, labels, the menu list and the defaults match the voices and
    StereoFilter (U1). `typ` starts `off` so a Scene's output is unfiltered
    until a type is chosen.
  - The categories (`Delay` / `Filter`) give the long labels their prefix.

Remove (HEAD 28–73):

```c
/*
 * CrumpBit descriptor table (S074_CRUMPBIT_EFFECT.md §4).
 *
 * What: rows 0..2 are the shared common rows; rows 3..10 are CrumpBit's.
 * - bit off / bit invert (3, 4): 8-bit masks, one bit per ADC data line
 *   (bit 0 = LSB). Not Morphable (interpolating a mask gives unrelated
 *   bits), not modulatable, not Pattern-automatable (a Pattern value is
 *   7-bit and cannot reach bit 7). Sequenceable through FX lanes 1 and 2,
 *   which store full 8-bit values. Edited by the SELECT buttons (Stage 2);
 *   in Stage 1 they show as plain 0..255 numbers.
 * - mix, feedback, rate, delay pan (5, 6, 7, 10): Morphable, LFO and
 *   Pattern automation, like voice rows.
 * - sub-type (8): only `dly` (max 0); its label comes from the format hook,
 *   because every DTYPE_MENU table id (4 bits) is already in use.
 * - sync (9): on/off; Pattern-automatable and sequenceable.
 * - Defaults (S075 F2-E, user): mix 0, feedback 64, rate 64, delay pan 63
 *   (the absolute centre, shows 0). The sync row's 3-letter label is `snc`;
 *   its file key remains `crump_sync`.
 * Why: the registry makes storage, AutoSave, the Effect page, lanes,
 * automation and LFO work without type-specific code elsewhere. Inputs:
 * none (const). Output: 11 rows. Accessors: EffectsManager (resolution,
 * clamps, capabilities), menuEffects/menu.c (labels, dtypes), `.fx` and
 * AutoSave (file keys). Affiliates: EffectParamRows.h, CrumpBitEffect.c
 * write_param().
 */
const effect_param_descriptor_t crumpBit_descriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE,
                       EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("crump_bit_off", "Bits", "BitOff", "bof", DTYPE_0B255,
               0u, EFFECT_MOD_NONE, 0u, 255u),
    EFFECT_ROW("crump_bit_invert", "Bits", "BitInv", "biv", DTYPE_0B255,
               0u, EFFECT_MOD_NONE, 0u, 255u),
    EFFECT_ROW("crump_mix", "Delay", "Mix", "mix", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("crump_feedback", "Delay", "Feedback", "fbk", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("crump_rate", "Delay", "Rate", "rte", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("crump_subtype", "CrumpBit", "SubType", "sub", DTYPE_0B127,
               0u, EFFECT_MOD_NONE, CRUMPBIT_SUBTYPE_DELAY, 0u),
    EFFECT_ROW("crump_sync", "Delay", "Sync", "snc", DTYPE_ON_OFF,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
    EFFECT_ROW("crump_dly_pan", "Delay", "DlyPan", "dpn", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
};
```

Replace with:

```c
/*
 * CrumpBit descriptor table (S074_CRUMPBIT_EFFECT.md §4).
 *
 * What: rows 0..2 are the shared common rows; rows 3..14 are CrumpBit's.
 * - bit off / bit invert (3, 4): 8-bit masks, one bit per ADC data line
 *   (bit 0 = LSB). Not Morphable (interpolating a mask gives unrelated
 *   bits), not modulatable, not Pattern-automatable (a Pattern value is
 *   7-bit and cannot reach bit 7). Sequenceable through FX lanes 1 and 2,
 *   which store full 8-bit values. Edited by the SELECT buttons (Stage 2);
 *   in Stage 1 they show as plain 0..255 numbers.
 * - mix, feedback, rate, delay pan (5, 6, 7, 10): Morphable, LFO and
 *   Pattern automation, like voice rows.
 * - rate (7) is BUFFER_DEPENDENT (S091 P2): a higher value is a longer
 *   delay, up to the whole arena, and EffectsManager clamps the resolved
 *   value to the live share through crumpBit_ops.effective_max; the page
 *   clamps edits the same way (effects_paramEffectiveMax()).
 * - sub-type (8): only `dly` (max 0); its label comes from the format hook,
 *   because every DTYPE_MENU table id (4 bits) is already in use.
 * - sync (9): on/off; Pattern-automatable and sequenceable.
 * - tone (11, S091 P2, delay sub-type): bipolar tilt on the feedback return,
 *   DTYPE_PM63 so stored 63 is flat and shows 0; below 63 cuts highs, above
 *   63 cuts lows. Morphable, LFO and Pattern, like `dpn`.
 * - filter type / freq / reso (12..14, S091 P2, shared output stage): the
 *   voice-filter rows by name, label and value law (user U1): `typ` is the
 *   MENU_FILTER list (LP HP BP UBP Nch Pek LP2 off), Morphable and
 *   Pattern-automatable but not LFO-modulatable, exactly like StereoFilter's
 *   `typ` (user U2); `frq` and `res` are full image rows. Defaults match
 *   StereoFilter (frq 64, res 0) except `typ`, which starts `off` so a
 *   Scene's output is unfiltered until a type is chosen.
 * - Defaults (S075 F2-E, user): mix 0, feedback 64, rate 64, delay pan 63
 *   (the absolute centre, shows 0). The sync row's 3-letter label is `snc`;
 *   its file key remains `crump_sync`.
 * Why: the registry makes storage, AutoSave, the Effect page, lanes,
 * automation and LFO work without type-specific code elsewhere. Inputs:
 * none (const). Output: 15 rows. Accessors: EffectsManager (resolution,
 * clamps, capabilities), menuEffects/menu.c (labels, dtypes), `.fx` and
 * AutoSave (file keys). Affiliates: EffectParamRows.h, CrumpBitEffect.c
 * write_param().
 */
const effect_param_descriptor_t crumpBit_descriptors[] = {
    EFFECT_COMMON_ROWS(INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                       EFFECT_FLAGS_IMAGE,
                       EFFECT_FLAGS_IMAGE),
    EFFECT_ROW("crump_bit_off", "Bits", "BitOff", "bof", DTYPE_0B255,
               0u, EFFECT_MOD_NONE, 0u, 255u),
    EFFECT_ROW("crump_bit_invert", "Bits", "BitInv", "biv", DTYPE_0B255,
               0u, EFFECT_MOD_NONE, 0u, 255u),
    EFFECT_ROW("crump_mix", "Delay", "Mix", "mix", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("crump_feedback", "Delay", "Feedback", "fbk", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    /*
     * S091 P2: EFFECT_ROW_FLAGS adds EFFECT_PARAM_FLAG_BUFFER_DEPENDENT, so
     * the resolved Rate is clamped to the longest delay the live share holds.
     */
    EFFECT_ROW_FLAGS("crump_rate", "Delay", "Rate", "rte", DTYPE_0B127,
                     EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u,
                     EFFECT_PARAM_FLAG_BUFFER_DEPENDENT),
    EFFECT_ROW("crump_subtype", "CrumpBit", "SubType", "sub", DTYPE_0B127,
               0u, EFFECT_MOD_NONE, CRUMPBIT_SUBTYPE_DELAY, 0u),
    EFFECT_ROW("crump_sync", "Delay", "Sync", "snc", DTYPE_ON_OFF,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE, 0u, 1u),
    EFFECT_ROW("crump_dly_pan", "Delay", "DlyPan", "dpn", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    /* S091 P2, delay sub-type: tone tilt on the feedback return. */
    EFFECT_ROW("crump_tone", "Delay", "Tone", "ton", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, CRUMPBIT_TONE_FLAT, 127u),
    /* S091 P2, output stage: voice-aligned filter rows (U1, U2). */
    EFFECT_ROW_MENU("crump_filter_type", "Filter", "Type", "typ", MENU_FILTER,
                    INSTRUMENT_PARAM_FLAG_MORPHABLE |
                    INSTRUMENT_PARAM_FLAG_AUTOMATABLE,
                    EFFECT_MOD_NONE, CRUMPBIT_FILTER_OFF, CRUMPBIT_FILTER_OFF),
    EFFECT_ROW("crump_filter_freq", "Filter", "Frequncy", "frq", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("crump_filter_reso", "Filter", "Resnance", "res", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
};
```

#### C3 — Ownership guards

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, insert after HEAD line 79. New lines 112–123.

- **What:** two compile-time guards on the ownership masks:
  - they are disjoint;
  - their union is exactly rows 3..`COUNT − 1`.
- **Why:** U3, so appending a row without an owner cannot compile.

Insert:

```c
/*
 * Row-ownership guards (S091 P2 §4). The stage masks in
 * CrumpBitParameters.h must not overlap, and together they must name every
 * type row 3..COUNT-1 and nothing else, so appending a row without deciding
 * its owner (shared or a sub-type) stops the build.
 */
_Static_assert((CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_DELAY) == 0u,
               "a CrumpBit row cannot belong to two stages");
_Static_assert((CRUMPBIT_ROWS_SHARED | CRUMPBIT_ROWS_DELAY) ==
               (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_COUNT) -
                CRUMPBIT_ROW_BIT(EFFECT_COMMON_PARAM_COUNT)),
               "every CrumpBit type row needs exactly one owner");
```

#### C4 — Rate label: clamped, Sync-aware

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 206–232: **replace** (27 lines → 56; new lines 250–305).

- **What:** the Rate label shows what plays.
  - The shown value is clamped to `crumpBit_rateLimit()` of the live
    share.
  - With Sync on, the label is the division searched within the same
    share's limit.
  - With Sync off and a clamp, the hook writes the clamped number
    (`numtostrpu`, the generic 3-digit form).
  - With no clamp, it falls back to the generic text.
- **Why:** §0A D2/N1: "The Sync label and the Rate cell must show the
  clamped value."
- **Cost:** foreground only. The share read is a struct copy.

Remove (HEAD 206–232):

```c
/*
 * Format hook: CrumpBit value text on the Effect page (Q7).
 *
 * What: `sub` shows `dly`; `rte` shows the snapped step division when the
 * shown Sync is on. crumpBit_syncDivision() applies the same rule the DSP
 * uses, at the current tempo. Everything else returns 0 for generic dtype
 * formatting. Why: the raw 0..127 Rate stays underneath (pots, storage,
 * automation, locks, LFO), and only the Effect page shows the division.
 * Inputs: row index, value being shown, and a 3-character output. Output:
 * nonzero when out[0..2] was written. Affiliates: stepScale_shortName(),
 * seq_getBpm(), menuEffects_shownParam().
 */
static uint8_t crumpBit_uiFormatValue3(uint8_t index, uint8_t value,
                                       char *out)
{
    if (index == CRUMPBIT_PARAM_SUBTYPE) {
        memcpy(out, (value == CRUMPBIT_SUBTYPE_DELAY) ? "dly" : "---", 3u);
        return 1u;
    }
    if (index == CRUMPBIT_PARAM_RATE &&
        menuEffects_shownParam(CRUMPBIT_PARAM_SYNC) != 0u) {
        /* S078: nudge stops show their symbol, other CCs show the raw value. */
        stepScale_formatShort(crumpBit_syncDivision(value, seq_getBpm()), out);
        return 1u;
    }
    return 0u;
}
```

Replace with:

```c
/*
 * Format hook: CrumpBit value text on the Effect page (Q7; S091 P2).
 *
 * What: `sub` shows `dly`. `rte` shows what the DSP plays (plan N1):
 * - the value is first clamped to the longest Rate the live share holds
 *   (crumpBit_rateLimit(), the same clamp EffectsManager applies before the
 *   DSP sees the row), so a stored value above the clamp reads as the
 *   clamped one;
 * - with the shown Sync on, the label is the snapped step division at the
 *   current tempo, searched among the divisions that fit the same share
 *   (crumpBit_syncDivision() with crumpBit_shareMaxSamples()), the rule the
 *   DSP uses;
 * - with Sync off and no clamp the hook returns 0 and the generic number is
 *   shown; with Sync off and a clamp it writes the clamped number.
 * Everything else returns 0 for generic dtype formatting.
 * Why: the raw 0..127 Rate stays underneath (pots, storage, automation,
 * locks, LFO); a voice-unit reservation must not change a stored Scene, only
 * what is played and shown.
 * Inputs: row index, value being shown, and a 3-character output. Output:
 * nonzero when out[0..2] was written. Runs in foreground; the share read is
 * a struct copy (no arena access).
 * Affiliates: stepScale_formatShort(), seq_getBpm(), menuEffects_shownParam(),
 * fxbuf_effectShare(), crumpBit_rateLimit(), numtostrpu().
 */
static uint8_t crumpBit_uiFormatValue3(uint8_t index, uint8_t value,
                                       char *out)
{
    if (index == CRUMPBIT_PARAM_SUBTYPE) {
        memcpy(out, (value == CRUMPBIT_SUBTYPE_DELAY) ? "dly" : "---", 3u);
        return 1u;
    }
    if (index == CRUMPBIT_PARAM_RATE) {
        fx_share_t share;
        /* limit: the largest `rte` whose delay fits this share. */
        uint8_t limit;
        /* shown: the Rate the DSP will play, never above the clamp. */
        uint8_t shown;

        fxbuf_effectShare(&share);
        limit = crumpBit_rateLimit(&share);
        shown = (value > limit) ? limit : value;
        if (menuEffects_shownParam(CRUMPBIT_PARAM_SYNC) != 0u) {
            /* S078: nudge stops show their symbol, other CCs the raw value. */
            stepScale_formatShort(
                crumpBit_syncDivision(shown, seq_getBpm(),
                                      crumpBit_shareMaxSamples(&share)),
                out);
            return 1u;
        }
        if (shown != value) {
            numtostrpu(out, shown, ' ');
            return 1u;
        }
    }
    return 0u;
}
```

#### C5 — Four-screen layout

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 234–262: **replace** (29 lines → 35; new lines 307–341).

- **What:** `screen_count[1]` 3 → 4.
  - s0 `ton fbk rte mrp` (with the bit row, `custom_row0` unchanged);
  - s1 `ton fbk rte sub`;
  - s2 `snc dpn mix typ`;
  - s3 `frq res – –`.
- **Why:** U4. The two empty s3 cells are for the deferred drive rows.
  Four screens is the layout maximum (`cells[8][4][4]`).

Remove (HEAD 234–262):

```c
/*
 * CrumpBit Effect-page layout (S074_CRUMPBIT_EFFECT.md §5.1).
 *
 * What: SELECT 1 stays manager-owned. SELECT 2 holds three screens: home
 * overlay (mix/fbk/rte/mrp), page 2 (mix/fbk/rte/sub), and page 3 (snc/dpn).
 * Every other SELECT has zero screens because its buttons are bit toggles.
 * The encoder walks SELECT 1 s0 -> s1 -> overlay -> page 2 -> page 3.
 * The page-3 short labels are `snc` and `dpn` (S075 F2-E).
 * Inputs: none (const). Output: registry layout consumed by menuEffects.
 * Affiliates: EFFECT_LAYOUT_CELL_MORPH, crumpBit_uiPaintRow0().
 */
const effect_select_layout_t crumpBit_layout = {
    .screen_count = { 0u, 3u, 0u, 0u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = {
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, EFFECT_LAYOUT_CELL_MORPH },
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SUBTYPE },
            { CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN,
              EFFECT_LANE_NONE, EFFECT_LANE_NONE },
            { EFFECT_LANE_NONE, EFFECT_LANE_NONE,
              EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        },
    },
    .custom_row0 = { 0u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u },
    .home_sub_page = 1u,
    .home_screen = 0u,
};
```

Replace with:

```c
/*
 * CrumpBit Effect-page layout (S074_CRUMPBIT_EFFECT.md §5.1; S091 P2 §3).
 *
 * What: SELECT 1 stays manager-owned. SELECT 2 holds four screens (the
 * layout maximum):
 *   s0 home overlay (bit row on top): ton fbk rte mrp
 *   s1 standard page:                 ton fbk rte sub
 *   s2:                               snc dpn mix typ
 *   s3:                               frq res  -   -
 * `ton` takes `mix`'s place on s0 and s1, and `mix` follows `dpn`, then the
 * filter type, cutoff and resonance (user U4). The two empty s3 cells are
 * kept for the deferred drive rows (`drv`, `tdr`). Every other SELECT has
 * zero screens because its buttons are bit toggles. The encoder walks
 * SELECT 1 s0 -> s1 -> overlay -> page 2 -> page 3 -> page 4.
 * Inputs: none (const). Output: registry layout consumed by menuEffects.
 * Affiliates: EFFECT_LAYOUT_CELL_MORPH, crumpBit_uiPaintRow0().
 */
const effect_select_layout_t crumpBit_layout = {
    .screen_count = { 0u, 4u, 0u, 0u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = {
            { CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, EFFECT_LAYOUT_CELL_MORPH },
            { CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SUBTYPE },
            { CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN,
              CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FILTER_TYPE },
            { CRUMPBIT_PARAM_FILTER_FREQ, CRUMPBIT_PARAM_FILTER_RESO,
              EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        },
    },
    .custom_row0 = { 0u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u },
    .home_sub_page = 1u,
    .home_screen = 0u,
};
```

#### C6 — Hooks comment

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 264–273: **replace** (10 lines → 10; new lines 343–352).

- **What:** comment only. `format_value3` now labels `sub` and the clamped,
  Sync-aware Rate.

Remove (HEAD 264–273):

```c
/*
 * CrumpBit page hooks.
 *
 * What: SELECT toggles bits and returns EFFECT_UI_SHOW_HOME, render_leds owns
 * the SELECT row, paint_row0 draws the overlay row, and format_value3 labels
 * `sub` and Sync. TRACK and BAR keep the default page behaviour (NULL).
 * Why: the registry-driven hook contract keeps type UI out of menu.c.
 * Accessors: the registry row and menuEffects hook dispatchers. Affiliates:
 * buttonHandler FX SELECT paths.
 */
```

Replace with:

```c
/*
 * CrumpBit page hooks.
 *
 * What: SELECT toggles bits and returns EFFECT_UI_SHOW_HOME, render_leds owns
 * the SELECT row, paint_row0 draws the overlay row, and format_value3 labels
 * `sub` and the (share-clamped, Sync-aware) Rate. TRACK and BAR keep the default page behaviour (NULL).
 * Why: the registry-driven hook contract keeps type UI out of menu.c.
 * Accessors: the registry row and menuEffects hook dispatchers. Affiliates:
 * buttonHandler FX SELECT paths.
 */
```

### 3.5 `Core/DSP/Effects/EffectParamRows.h`

#### R1 — EFFECT_ROW_FLAGS

**Location:** `Core/DSP/Effects/EffectParamRows.h`, insert after HEAD line 41. New lines 42–59.

- **What:** `EFFECT_ROW_FLAGS`, the `EFFECT_ROW` builder with an
  `effect_flags` argument.
- **Why:** `EFFECT_ROW` always writes 0 there. The Rate is the first row
  that needs `EFFECT_PARAM_FLAG_BUFFER_DEPENDENT`. `expand7` stays 0: a
  WIDE8 row would need its own builder.

Insert:

```c
/*
 * Generic row with Effect-only capability flags (S091 P2).
 *
 * What: EFFECT_ROW plus effect_flags_ (EFFECT_PARAM_FLAG_*), stored in
 * effect_param_descriptor_t.effect_flags. Why: EFFECT_ROW always writes 0
 * there, and CrumpBit's Rate is the first row that needs
 * EFFECT_PARAM_FLAG_BUFFER_DEPENDENT (its resolved value is clamped to the
 * live FxBuffer share through the type's effective_max op). expand7 stays 0:
 * a WIDE8 row would also need an expand function and is not built here.
 * Affiliates: EffectsManager.c effects_service() BUFFER_DEPENDENT clamp,
 * effects_paramEffectiveMax(), crumpBit_descriptors.
 */
#define EFFECT_ROW_FLAGS(key_, cat_, long_, short_, dtype_, flags_, mod_, def_, \
                         max_, effect_flags_) \
    { { key_, short_, long_, cat_, (uint8_t)(dtype_), (uint8_t)(flags_), mod_, \
        EFFECT_BIND_NONE }, (uint8_t)(def_), (uint8_t)(max_), \
        (uint8_t)(effect_flags_), 0 }

```

### 3.6 `Core/DSP/Effects/EffectsManager.h` and `.c`

#### M1 — effects_paramEffectiveMax() declaration

**Location:** `Core/DSP/Effects/EffectsManager.h`, insert after HEAD line 249. New lines 250–266.

- **What:** the declaration and contract of `effects_paramEffectiveMax()`.
- **Why:** D2. The page must stop where the DSP clamp is. Only the page
  calls it.

Insert:

```c

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
```

#### M2 — Runtime union comment

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 52–56: **replace** (5 lines → 9; new lines 52–60).

- **What:** comment only. The union member `CrumpBitRuntime` is 76 B since
  S091 P2. The union size guard (HEAD 131–132, 76 B) is unchanged and still
  holds.

Remove (HEAD 52–56):

```c
typedef union {
    StereoFilterRuntime stereo_filter;
    /* S074: 56 B. Its audio lives in the FxBuffer share, not in this union. */
    CrumpBitRuntime crump_bit;
} effects_runtime_t;
```

Replace with:

```c
typedef union {
    StereoFilterRuntime stereo_filter;
    /*
     * S074: 56 B; S091 P2: 76 B (tone, DC blocker, output SVF, four rows;
     * paid for by trims, so the union stays 76 B). Its audio lives in the
     * FxBuffer share, not in this union.
     */
    CrumpBitRuntime crump_bit;
} effects_runtime_t;
```

#### M3 — Registry row: lanes and arena min/pref

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 97–119: **replace** (23 lines → 29; new lines 101–129).

- **What:**
  - The registry comment describes the elastic loop and the lanes.
  - Lanes 10–13 get `ton`, `typ`, `frq`, `res`; lanes 14–15 stay free for
    the drive rows.
  - `buffer_min_bytes`/`buffer_pref_bytes` become
    `CRUMPBIT_LOOP_MIN_SHARE_BYTES` / `CRUMPBIT_LOOP_MAX_BYTES`. A search
    finds no reader of either field; they describe the type.
- **Why:** FX-sequence lanes make the new rows sequenceable. The registry
  self-check (no duplicate lane, index < count) passes.

Remove (HEAD 97–119):

```c
    /*
     * CrumpBit (`cbt`, S074): stereo in and out. The 8-bit mono tape loop
     * uses CRUMPBIT_BUFFER_BYTES of the FxBuffer Effect share (min = pref;
     * the fixed range fits the minimum share). Lanes 1..9: bit off, bit
     * invert, mix, feedback, rate, sync, vol, pan, delay pan. Lane 0 is
     * Effect Morph.
     */
    {
        crumpBit_token3, crumpBit_abbrev5, crumpBit_full8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        crumpBit_descriptors, CRUMPBIT_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          CRUMPBIT_PARAM_BIT_OFF, CRUMPBIT_PARAM_BIT_INVERT,
          CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
          CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
          CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN,
          CRUMPBIT_PARAM_DLY_PAN,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        &crumpBit_layout, &crumpBit_ops, &crumpBit_ui,
        (uint16_t)sizeof(CrumpBitRuntime),
        CRUMPBIT_BUFFER_BYTES, CRUMPBIT_BUFFER_BYTES
    },
```

Replace with:

```c
    /*
     * CrumpBit (`cbt`, S074; S091 P2): stereo in and out. The 8-bit mono
     * tape loop is elastic (S091 P2 §5.1): it uses the whole Effect share up
     * to CRUMPBIT_LOOP_MAX_BYTES (pref, the full arena) and still works at
     * the share left with every voice unit claimed (min); the Rate row is
     * clamped to the share through crumpBit_ops.effective_max. These two
     * fields are declarative (no code reads them today).
     * Lanes 1..13: bit off, bit invert, mix, feedback, rate, sync, vol, pan,
     * delay pan, then (S091 P2) tone, filter type, filter freq, filter reso.
     * Lane 0 is Effect Morph; lanes 14..15 stay free for the deferred drive
     * rows (`drv`, `tdr`).
     */
    {
        crumpBit_token3, crumpBit_abbrev5, crumpBit_full8,
        EFFECT_IO_STEREO_IN | EFFECT_IO_STEREO_OUT,
        crumpBit_descriptors, CRUMPBIT_PARAM_COUNT,
        { EFFECT_LANE_MORPH_SOURCE,
          CRUMPBIT_PARAM_BIT_OFF, CRUMPBIT_PARAM_BIT_INVERT,
          CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK,
          CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
          CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN,
          CRUMPBIT_PARAM_DLY_PAN,
          CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FILTER_TYPE,
          CRUMPBIT_PARAM_FILTER_FREQ, CRUMPBIT_PARAM_FILTER_RESO,
          EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        &crumpBit_layout, &crumpBit_ops, &crumpBit_ui,
        (uint16_t)sizeof(CrumpBitRuntime),
        CRUMPBIT_LOOP_MIN_SHARE_BYTES, CRUMPBIT_LOOP_MAX_BYTES
    },
```

#### M4 — effects_paramEffectiveMax() definition

**Location:** `Core/DSP/Effects/EffectsManager.c`, insert after HEAD line 335 (the closing `}` of `effects_paramModulatable()`). New lines 346–374.

- **What:** `effects_paramEffectiveMax(type, index)`: the descriptor
  maximum, lowered for a BUFFER_DEPENDENT row by the type's `effective_max`
  at the current share.
- **Why:**
  - It is the same rule `effects_service()` applies, exported for the page
    (D2).
  - MIDI, Pattern and LFO values are not edited through the page. They
    keep their stored value and are clamped at resolution, as before
    (A30).

Insert:

```c

uint8_t effects_paramEffectiveMax(effect_type_id_t type, uint8_t index)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    const effect_param_descriptor_t *descriptor =
        effects_descriptor(type, index);
    /* max: the descriptor's inclusive clamp, lowered by the share below. */
    uint8_t max;

    if (!entry || !descriptor)
        return 0u;
    max = descriptor->max_value;
    /*
     * The same rule effects_service() applies before write_param(): only a
     * BUFFER_DEPENDENT row of a type with an effective_max op is clamped,
     * against the share as it is now (a struct copy; no arena access).
     */
    if ((descriptor->effect_flags & EFFECT_PARAM_FLAG_BUFFER_DEPENDENT) != 0u &&
        entry->ops && entry->ops->effective_max) {
        fx_share_t share;
        uint8_t limit;

        fxbuf_effectShare(&share);
        limit = entry->ops->effective_max(index, &share);
        if (limit < max)
            max = limit;
    }
    return max;
}
```

### 3.7 `Core/Menu/menuEffects.c`

#### U1 — Page edits clamped by the share

**Location:** `Core/Menu/menuEffects.c`, HEAD lines 427–432: **replace** (6 lines → 11; new lines 427–437).

- **What:** `menuEffects_clampValue()` takes a `MENU_FX_CELL_PARAM` maximum
  from `effects_paramEffectiveMax()` instead of `descriptor->max_value`.
- **Why:** the encoder (`menu.c` HEAD 6748), the pots and held-step lane
  edits (`menuEffects_holdEdit()`) all clamp here. One change covers every
  page edit.
- **Effect:** a stored `rte` above the clamp shows the clamped value (C4).
  The first edit jumps to the clamp.

Remove (HEAD 427–432):

```c
    case MENU_FX_CELL_PARAM: {
        const effect_record_t *record = menuEffects_record();
        const effect_param_descriptor_t *descriptor = record
            ? effects_descriptor(record->type, cell->index) : NULL;
        max = descriptor ? descriptor->max_value : 0u;
        break; }
```

Replace with:

```c
    case MENU_FX_CELL_PARAM: {
        const effect_record_t *record = menuEffects_record();

        /*
         * S091 P2: the descriptor maximum, lowered for a BUFFER_DEPENDENT row
         * (CrumpBit `rte`) to what the live FxBuffer share allows, so the
         * encoder, pots and held-step edits stop where the DSP clamp is.
         */
        max = record ? effects_paramEffectiveMax(record->type, cell->index)
                     : 0u;
        break; }
```

### 3.8 `Makefile`

#### K1 — Tables in the firmware build

**Location:** `Makefile`, insert after HEAD line 119. New lines 120–120.

- **What:** adds `Core/DSP/Effects/CrumpBit/CrumpBitTables.c` to `SRCS`.
- **Why:** data only, so the ordinary `-O2` rule. The DSP file keeps its
  explicit `-Ofast` rule.

Insert:

```make
  Core/DSP/Effects/CrumpBit/CrumpBitTables.c \
```


### 3.9 New files

#### N1 — `Core/DSP/Effects/CrumpBit/CrumpBitTables.h` (new)

- **What:** declares the three tables and `CRUMPBIT_TABLE_SIZE`.
- **Why:** the generated `.c` and its two readers (`CrumpBitEffect.c`, the
  host test) share one declaration. The contract, ranges and regeneration
  command are in the comment.

```c
/*
 * Core/DSP/Effects/CrumpBit/CrumpBitTables.h
 *
 *  Created on: 04.11.2026
 * -----------------------------------------------------------------------------
 *  Copyright 2026 Brendan Clarke
 * -----------------------------------------------------------------------------
 *  This file is part of the LXR02 Open-Source software.
 * -----------------------------------------------------------------------------
 */

#ifndef CRUMP_BIT_TABLES_H_
#define CRUMP_BIT_TABLES_H_

/*
 * CrumpBit control tables (S091 P2; generated, CrumpBitTables.c).
 *
 * What: three 128-entry float tables indexed by a 0..127 row value.
 * - crumpBit_rateTable[rte]: delay length in samples, 882 (20 ms) at 0 to
 *   126,686 (2.872 s, the whole arena less the 2-sample guard) at 127,
 *   exponential and strictly increasing. Higher `rte` = longer delay.
 * - crumpBit_toneK[ton]: the one-pole coefficient of the feedback-return
 *   tilt. 0..62 low-pass (150 Hz .. 18 kHz), 63 exactly 1.0 (flat),
 *   64..127 high-pass (20 Hz .. 2 kHz).
 * - crumpBit_filterG[frq]: the TPT pre-warped cutoff g of the output filter,
 *   the voice cutoff law (fastTan(pi * 0.45 * valueShaperF2F(frq/127,
 *   FILTER_SHAPER))). 0 at frq 0 (closed), strictly increasing.
 * Why tables: CrumpBit's per-block work runs inside the render
 * (mixer_calcNextSampleBlock()); a lookup replaces an expf, a fastTan
 * division and a valueShaper division per block, and the Rate table makes the
 * share clamp and the DSP use bit-identical lengths.
 * Regenerate with `python3 tools/plots/gen_crumpbit_tables.py` from the
 * repository root; `make -C tools/dsp_test crumpbit_tables` checks that the
 * committed file matches the generator byte for byte.
 * Accessors: CrumpBitEffect.c only.
 * Affiliates: tools/plots/gen_crumpbit_tables.py.
 */
#define CRUMPBIT_TABLE_SIZE 128u

extern const float crumpBit_rateTable[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_toneK[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_filterG[CRUMPBIT_TABLE_SIZE];

#endif /* CRUMP_BIT_TABLES_H_ */
```

#### N2 — `Core/DSP/Effects/CrumpBit/CrumpBitTables.c` (generated, new)

- **What:** the three tables, written by N3. Do not type it: run
  `python3 tools/plots/gen_crumpbit_tables.py` from the repository root.
  `make -C tools/dsp_test crumpbit_tables` checks the committed file byte
  for byte.
- **Shape:**
  - 110 lines;
  - three 128-entry `const float` arrays in `%.9e` form, four per line,
    each line ending in its index range.

  The excerpt shows each table's first line, its last line, and the tone
  table's flat centre:

```c
/* Generated by tools/plots/gen_crumpbit_tables.py (S091 P2). Do not edit. */
#include "CrumpBitTables.h"

/* Delay length in samples, rte 0..127: 20 ms .. 2.872 s. */
const float crumpBit_rateTable[128] = {
    8.820000000e+02f, 9.171806519e+02f, 9.537645672e+02f, 9.918077183e+02f,  /*   0..  3 */
    ...
    9.218209122e+03f, 9.585899152e+03f, 9.968255367e+03f, 1.036586276e+04f,  /*  60.. 63 */
    1.077932967e+04f, 1.120928868e+04f, 1.165639761e+04f, 1.212134055e+04f,  /*  64.. 67 */
    ...
    1.126599944e+05f, 1.171537042e+05f, 1.218266562e+05f, 1.266860000e+05f,  /* 124..127 */
};

/* Tone tilt pole, ton 0..127: LP 150 Hz..18 kHz, 63 flat, HP 20 Hz..2 kHz. */
const float crumpBit_toneK[128] = {
    2.114080006e-02f, 2.281841651e-02f, 2.462747669e-02f, 2.657800000e-02f,  /*   0..  3 */
    ...
    8.888829743e-01f, 9.068509102e-01f, 9.230112560e-01f, 1.000000000e+00f,  /*  60.. 63 */
    2.844941074e-03f, 3.060359277e-03f, 3.292061949e-03f, 3.541275869e-03f,  /*  64.. 67 */
    ...
    2.045114062e-01f, 2.181949999e-01f, 2.326535857e-01f, 2.479102104e-01f,  /* 124..127 */
};

/* Output filter TPT g, frq 0..127 (voice cutoff law). */
const float crumpBit_filterG[128] = {
    -0.000000000e+00f, 5.908373859e-04f, 1.190623033e-03f, 1.799562217e-03f,  /*   0..  3 */
    ...
    6.377698525e-02f, 6.573292214e-02f, 6.774366550e-02f, 6.981156846e-02f,  /*  60.. 63 */
    7.193912176e-02f, 7.412896409e-02f, 7.638389330e-02f, 7.870687866e-02f,  /*  64.. 67 */
    ...
    1.454093747e+00f, 1.886853244e+00f, 2.788992925e+00f, 6.109598515e+00f,  /* 124..127 */
};
```

- **Check values (C1–C3 of the generator):**
  - `rateTable[0]` = 882.0 and `rateTable[127]` = 126,686.0, both exactly
    representable;
  - `rateTable[64]` ≈ 10,780 samples (0.244 s, the default `rte`);
  - `toneK[63]` = 1.0 exactly;
  - `filterG[0]` = 0 (frq 0 closes the filter, as on a voice) and
    `filterG[127]` ≈ 6.11.

#### N3 — `tools/plots/gen_crumpbit_tables.py` (new)

- **What:** the table generator and checker. It follows the
  `gen_fold_tables.py` pattern (constants, checks C1–C3, `--check` byte
  compare).
- **Why:** the tables must be reproducible from their laws, with their
  sources:
  - Rate: plan §5.1;
  - tone: plan §5.3, with ranges judged by ear (§0A D2);
  - cutoff: the voice law of `SVF_directSetFilterValue()` with
    `valueShaperF2F(…, FILTER_SHAPER)` and `fastTan()`, mirrored in
    double precision.

  A voice computes `g` in single precision, so the two differ by rounding
  only.

```python
#!/usr/bin/env python3
"""Generate the CrumpBit control tables (S091 P2).

What:       writes Core/DSP/Effects/CrumpBit/CrumpBitTables.c with three
            128-entry float tables, one per 0..127 row value:
              crumpBit_rateTable[r]  delay length in samples for `rte` r:
                                     L(r) = 882 x (126686 / 882)^(r/127),
                                     20 ms at 0 .. 2.872 s (the whole
                                     arena less the 2-sample guard) at 127,
                                     exponential (equal steps = equal time
                                     ratios); higher r = longer (P2 Q2);
              crumpBit_toneK[t]      one-pole coefficient of the `ton`
                                     tilt, k = 1 - exp(-2 pi fc / fs):
                                     t 0..62 low-pass, fc 150 Hz .. 18 kHz;
                                     t 63 exactly 1 (flat: the pole follows
                                     its input, a bit-exact pass-through);
                                     t 64..127 high-pass, fc 20 Hz .. 2 kHz;
              crumpBit_filterG[v]    TPT pre-warped cutoff g of `frq` v,
                                     g = fastTan(pi x 0.45 x shaper(v/127)),
                                     the voice-filter law of
                                     SVF_directSetFilterValue() with
                                     valueShaperF2F(v/127, FILTER_SHAPER).
Why:        crumpBit_process() runs inside mixer_calcNextSampleBlock(), so
            per-block expf/logf/fastTan cost render time. Lookups are
            constant-cost, and the Rate table also lets the share clamp
            (crumpBit_effectiveMax) and the DSP use the identical lengths.
Checks:     C1 rate[0] = 882 and rate[127] = 126686 (to 1e-3), strictly
            increasing; C2 toneK in (0, 1], toneK[63] == 1, low-pass side
            increasing, high-pass side increasing; C3 filterG[0] = 0
            (frq 0 closes the filter, as on a voice) and strictly
            increasing; C4 --check: regenerate in memory and
            compare with the committed file byte for byte (exit 1 on any
            failure).
Inputs:     the constants below (FS matches config.h REAL_FS; FILTER_SHAPER
            matches Core/globals.h).
Outputs:    Core/DSP/Effects/CrumpBit/CrumpBitTables.c, or a pass/fail line
            with --check.
Accessors:  tools/dsp_test/Makefile crumpbit_tables target (run from the
            repository root).
Affiliates: CrumpBitTables.h; CrumpBitEffect.c; ResonantFilter.c fastTan();
            valueShaper.h valueShaperF2F(); gen_fold_tables.py (pattern).
"""
import math
import sys

FS = 44108.07291666667
RATE_MIN, RATE_MAX = 882.0, 126686.0
TONE_LP_LO, TONE_LP_HI = 150.0, 18000.0
TONE_HP_LO, TONE_HP_HI = 20.0, 2000.0
TONE_FLAT = 63
FILTER_SHAPER = -0.9
PATH = "Core/DSP/Effects/CrumpBit/CrumpBitTables.c"


def rate_table():
    return [RATE_MIN * (RATE_MAX / RATE_MIN) ** (r / 127.0) for r in range(128)]


def one_pole_k(fc):
    return 1.0 - math.exp(-2.0 * math.pi * fc / FS)


def tone_table():
    vals = []
    for t in range(128):
        if t < TONE_FLAT:
            fc = TONE_LP_LO * (TONE_LP_HI / TONE_LP_LO) ** (t / 62.0)
            vals.append(one_pole_k(fc))
        elif t == TONE_FLAT:
            vals.append(1.0)
        else:
            fc = TONE_HP_LO * (TONE_HP_HI / TONE_HP_LO) ** ((t - 64) / 63.0)
            vals.append(one_pole_k(fc))
    return vals


def value_shaper(val, shape):
    k = 2.0 * shape / (1.0001 - shape)
    return (1.0 + k) * val / (1.0 + k * abs(val))


def fast_tan(x):
    a = -15.0 * x + x * x * x
    b = 3.0 * (-5.0 + 2.0 * x * x)
    return a / b


def filter_table():
    return [fast_tan(math.pi * 0.45 * value_shaper(v / 127.0, FILTER_SHAPER))
            for v in range(128)]


def render_table(name, comment, vals):
    lines = ["", "/* %s */" % comment, "const float %s[128] = {" % name]
    for i in range(0, 128, 4):
        lines.append("    " + ", ".join("%.9ef" % x for x in vals[i:i + 4]) +
                     ",  /* %3d..%3d */" % (i, i + 3))
    lines.append("};")
    return lines


def render(rate, tone, filt):
    lines = ["/* Generated by tools/plots/gen_crumpbit_tables.py (S091 P2). Do not edit. */",
             '#include "CrumpBitTables.h"']
    lines += render_table("crumpBit_rateTable",
                          "Delay length in samples, rte 0..127: 20 ms .. 2.872 s.", rate)
    lines += render_table("crumpBit_toneK",
                          "Tone tilt pole, ton 0..127: LP 150 Hz..18 kHz, 63 flat, HP 20 Hz..2 kHz.", tone)
    lines += render_table("crumpBit_filterG",
                          "Output filter TPT g, frq 0..127 (voice cutoff law).", filt)
    return "\n".join(lines) + "\n"


def main():
    rate, tone, filt = rate_table(), tone_table(), filter_table()
    ok = abs(rate[0] - RATE_MIN) < 1e-3 and abs(rate[127] - RATE_MAX) < 1e-3
    ok = ok and all(a < b for a, b in zip(rate, rate[1:]))
    ok = ok and all(0.0 < k <= 1.0 for k in tone) and tone[TONE_FLAT] == 1.0
    ok = ok and all(a < b for a, b in zip(tone[:TONE_FLAT], tone[1:TONE_FLAT]))
    ok = ok and all(a < b for a, b in zip(tone[64:], tone[65:]))
    ok = ok and filt[0] == 0.0 and all(a < b for a, b in zip(filt, filt[1:]))
    text = render(rate, tone, filt)
    if "--check" in sys.argv:
        ok = ok and open(PATH).read() == text
        print("crumpbit tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("crumpbit tables: C1-C3 failed")
        return 1
    open(PATH, "w").write(text)
    print("wrote", PATH)
    return 0


if __name__ == "__main__":
    sys.exit(main())
```


---

## 4. Host tests (`tools/dsp_test`)

### 4.1 `tools/dsp_test/Makefile`

#### T1 — Header comment

**Location:** `tools/dsp_test/Makefile`, HEAD lines 12–13: **replace** (2 lines → 4; new lines 12–15).

- **What:** header comment: the `crumpbit` target now has five break
  builds, and `crumpbit_tables` is new.

Remove (HEAD 12–13):

```make
#               crumpbit            modules/crumpbit.c (+ 2 breaks: S091 R1
#                                   probe, R4 AC seed)
```

Replace with:

```make
#               crumpbit            modules/crumpbit.c (+ 5 breaks: P2 Sync
#                                   key, R4 AC seed, P2 flat tone, P2 seat
#                                   delay clamp, P2 Rate-limit probe)
#               crumpbit_tables     tools/plots/gen_crumpbit_tables.py --check
```

#### T2 — .PHONY and all

**Location:** `tools/dsp_test/Makefile`, HEAD lines 37–42: **replace** (6 lines → 7; new lines 39–45).

- **What:** `crumpbit_tables` is added to `.PHONY` and `all`.

Remove (HEAD 37–42):

```make
.PHONY: all clean oscillator modulation voice_control sequencer \
        instrument_manager samples sampleoptimize run_modes special_tags \
        stock_tables voicefold fold_tables crumpbit crumpbit_elf_check
all: oscillator modulation voice_control sequencer instrument_manager samples \
     sampleoptimize run_modes special_tags stock_tables voicefold fold_tables \
     crumpbit
```

Replace with:

```make
.PHONY: all clean oscillator modulation voice_control sequencer \
        instrument_manager samples sampleoptimize run_modes special_tags \
        stock_tables voicefold fold_tables crumpbit crumpbit_tables \
        crumpbit_elf_check
all: oscillator modulation voice_control sequencer instrument_manager samples \
     sampleoptimize run_modes special_tags stock_tables voicefold fold_tables \
     crumpbit crumpbit_tables
```

#### T3 — crumpbit target, break builds, crumpbit_tables

**Location:** `tools/dsp_test/Makefile`, HEAD lines 256–306: **replace** (51 lines → 103; new lines 259–361).

- **What:** the `crumpbit` target.
  - **Extraction:** the target also extracts:
    - `FXBUF_ALIGN_BYTES`, `FXBUF_VOICE_UNIT_SAMPLES/BYTES`;
    - the new `CrumpBitParameters.h` macros (row count, ownership masks,
      tone centre, filter ids);
    - `CrumpBitTables.h/.c` (`--body`).
  - **Break builds,** each of which must fail:

    | # | Change | Law that catches it |
    |---|---|---|
    | 1 | the Sync key ignores the live limit (`key = target`) | CB1 |
    | 2 | AC seed 0 (unchanged) | CB4/CB5 |
    | 3 | flat tone weighted (1, 1) | CB4 |
    | 4 | seat without the delay clamp | CB4 invariant |
    | 5 | Rate limit without its eighth probe | CB6 |

  - **New target `crumpbit_tables`:** the generator's `--check`.
- **Why break 1 changed:** the S091 P1 break ("probe one position too
  far") no longer fails. The eighth probe repairs a one-position
  overshoot, so that break no longer models a fault.

Remove HEAD lines 256–306 (51 lines), from
`# S091 P1: the real CrumpBitEffect.c against the S074 reference in`
through
`else echo "crumpbit_break2: caught (law CB4/CB5 fails as expected)"; fi`.

Replace with:

```make
# S091 P1/P2: the real CrumpBitEffect.c and its generated tables against
# the references in modules/crumpbit.c (laws CB1-CB9; CB1-CB4 bit-exact).
# Break proofs:
#   - the Sync search key ignoring the live share limit (key = target
#     instead of min(target, max)) must fail CB1 (S091 P2; the S091 P1
#     probe off-by-one break no longer applies, because the eighth probe
#     repairs a one-position overshoot);
#   - the AC memory seeded 0 instead of 128 (a full-scale step on the first
#     sample after init) must fail CB4/CB5;
#   - the flat tone weighted (1, 1) instead of (0, 1) (the return would be
#     s + v, not v) must fail CB4;
#   - the seat without its delay clamp (a share shrink leaves the delay
#     longer than the loop) must fail the CB4 seat invariant;
#   - the Rate-limit search without its eighth probe (seven steps reach
#     only 127 entries, so the whole arena would give 126) must fail CB6.
CB_GEN := $(GEN)/cb_fxbuf.c $(GEN)/cb_effects.c $(GEN)/cb_params.c \
          $(GEN)/cb_stepscale.c $(GEN)/cb_stepscale.h \
          $(GEN)/cb_tables.h $(GEN)/cb_tables.c \
          $(GEN)/CrumpBitEffect.host.h $(GEN)/CrumpBitEffect.host.c
crumpbit: | $(GEN)
	$(EX) --src $(ROOT)/Core/DSP/Effects/FxBuffer.h \
	  --typedef fx_share_t --typedef fxbuf_handoff_t \
	  --macro FXBUF_ALIGN_BYTES \
	  --macro FXBUF_VOICE_UNIT_COUNT --macro FXBUF_VOICE_UNIT_SAMPLES \
	  --macro FXBUF_VOICE_UNIT_BYTES --macro FXBUF_EFFECT_POINTER_COUNT \
	  --macro FXBUF_HANDOFF_POINTER_COUNT \
	  --macro FXBUF_HANDOFF_EFFECT_POINTER_BASE \
	  --macro FXBUF_STATE_EFFECT_WRITTEN --out $(GEN)/cb_fxbuf.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/EffectsManager.h \
	  --typedef effect_io_t --typedef effect_type_ops_t \
	  --out $(GEN)/cb_effects.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitParameters.h \
	  --typedef crumpbit_param_t --macro CRUMPBIT_PARAM_COUNT \
	  --macro CRUMPBIT_ROW_BIT --macro CRUMPBIT_ROWS_SHARED \
	  --macro CRUMPBIT_ROWS_DELAY --macro CRUMPBIT_TONE_FLAT \
	  --macro CRUMPBIT_FILTER_LP --macro CRUMPBIT_FILTER_HP \
	  --macro CRUMPBIT_FILTER_BP --macro CRUMPBIT_FILTER_UBP \
	  --macro CRUMPBIT_FILTER_NOTCH --macro CRUMPBIT_FILTER_PEAK \
	  --macro CRUMPBIT_FILTER_LP2 --macro CRUMPBIT_FILTER_OFF \
	  --out $(GEN)/cb_params.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitTables.h --body \
	  --out $(GEN)/cb_tables.h
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitTables.c --body \
	  --out $(GEN)/cb_tables.c
	$(EX) --src $(ROOT)/Core/Sequencer/StepScale.h \
	  --macro STEP_SCALE_COUNT --macro STEP_SCALE_DEFAULT \
	  --out $(GEN)/cb_stepscale.h
	$(EX) --src $(ROOT)/Core/Sequencer/StepScale.c \
	  --object stepScale_q8Table --func stepScale_ticksQ8 \
	  --out $(GEN)/cb_stepscale.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitEffect.h --body \
	  --out $(GEN)/CrumpBitEffect.host.h
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitEffect.c --body \
	  --out $(GEN)/CrumpBitEffect.host.c
	$(HOSTCC) $(HOSTFLAGS) modules/crumpbit.c -o build/crumpbit -lm
	./build/crumpbit
	mkdir -p build/break_cb1 build/break_cb2
	sed 's/const float key = (target < max_samples) ? target : max_samples;/const float key = target;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb1/CrumpBitEffect.host.c
	@grep -q 'const float key = target;' build/break_cb1/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb1 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break1 -lm
	@if ./build/crumpbit_break1 >/dev/null 2>&1; then \
	  echo "crumpbit_break1: FAILED (Sync live-limit break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break1: caught (law CB1 fails as expected)"; fi
	sed 's/rt->ac_c_l = CRUMPBIT_CODE_MID;/rt->ac_c_l = 0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb2/CrumpBitEffect.host.c
	@grep -q 'rt->ac_c_l = 0;' build/break_cb2/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb2 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break2 -lm
	@if ./build/crumpbit_break2 >/dev/null 2>&1; then \
	  echo "crumpbit_break2: FAILED (AC-seed break not caught)"; exit 1; \
	else echo "crumpbit_break2: caught (law CB4/CB5 fails as expected)"; fi
	mkdir -p build/break_cb3 build/break_cb4
	sed 's/((t == CRUMPBIT_TONE_FLAT) ? 0.0f : -1.0f)/((t == CRUMPBIT_TONE_FLAT) ? 1.0f : -1.0f)/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb3/CrumpBitEffect.host.c
	@grep -q '(t == CRUMPBIT_TONE_FLAT) ? 1.0f' \
	  build/break_cb3/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb3 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break3 -lm
	@if ./build/crumpbit_break3 >/dev/null 2>&1; then \
	  echo "crumpbit_break3: FAILED (flat-tone break not caught)"; exit 1; \
	else echo "crumpbit_break3: caught (law CB4 fails as expected)"; fi
	sed 's/rt->delay = (float)(len - CRUMPBIT_LOOP_GUARD);/(void)0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb4/CrumpBitEffect.host.c
	@grep -q '(void)0;' build/break_cb4/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb4 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break4 -lm
	@if ./build/crumpbit_break4 >/dev/null 2>&1; then \
	  echo "crumpbit_break4: FAILED (seat delay-clamp break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break4: caught (law CB4 fails as expected)"; fi
	mkdir -p build/break_cb5
	sed 's/    count += (crumpBit_rateTable\[count\] <= max_samples) ? 1u : 0u;/    (void)0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb5/CrumpBitEffect.host.c
	@grep -q '    (void)0;' build/break_cb5/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb5 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break5 -lm
	@if ./build/crumpbit_break5 >/dev/null 2>&1; then \
	  echo "crumpbit_break5: FAILED (Rate-limit probe break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break5: caught (law CB6 fails as expected)"; fi
```

#### T4 — ELF gate limits

**Location:** `tools/dsp_test/Makefile`, HEAD lines 308–317: **replace** (10 lines → 22; new lines 363–384).

- **What:** `crumpbit_elf_check`:
  - every `crumpBit_process` loop ≤ 126 with no VDIV (the stage-2 loop
    has two rotated heads at 126; the output loop is 31);
  - `crumpBit_divisionFor` ≤ 117;
  - `crumpBit_rateLimit` ≤ 101.
- **Why:** these are the measured counts (§2). Any later growth fails the
  gate.

Remove (HEAD 308–317):

```make
# S091 P1 ARM gate (needs `make all` at the repository root first): the
# linked crumpBit_process() per-sample loop must stay at or below 98
# instructions per frame on its longest path (111 before S091), and the
# unrolled Sync search crumpBit_divisionFor() at or below 117 instructions
# per block (the 128-step walk it replaced was up to ~2,430).
crumpbit_elf_check:
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --top 2 --max 98
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_divisionFor --straight --max 117
```

Replace with:

```make
# S091 P2: the committed CrumpBit tables must equal the generator's output.
crumpbit_tables:
	cd $(ROOT) && python3 tools/plots/gen_crumpbit_tables.py --check

# S091 ARM gate (needs `make all` at the repository root first):
#   - every crumpBit_process() loop at or below 126 instructions per frame
#     on its longest path, with no VDIV: the stage-2 loop (ADC, AC, delay,
#     feedback return) measures 126 and the stage-3 output-filter loop 31
#     (S091 P2; the single S091 P1 loop was 98, 111 before S091). --top 3
#     prints all three heads (stage 2 is entered at two rotated heads);
#   - the unrolled Sync search crumpBit_divisionFor() at or below 117
#     instructions per block (the 128-step walk it replaced was up to
#     ~2,430), and the Rate-limit search crumpBit_rateLimit() at or below
#     101 (S091 P2; eight probes, run once per block by effects_service()
#     through effective_max).
crumpbit_elf_check:
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --top 3 --max 126
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_divisionFor --straight --max 117
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_rateLimit --straight --max 101
```


### 4.2 T5 — `tools/dsp_test/modules/crumpbit.c` (replace the whole file)

- **What:** the S091 P1 file is replaced.
  - CB1–CB3 are kept: CB1 is generalised to a live limit; CB2 (ADC) and
    CB3 (code DAC) are unchanged.
  - The S074 whole-block law (old CB4) cannot hold: P2 changes the output
    on purpose. It is replaced by a reference written from the P2
    equations.
  - CB5–CB9 are new (the file header lists every law).
- **Why the reference differs deliberately from the firmware:**
  - It keeps S074's float AC memory and **stored float ramp origins**,
    picks the tone output by branch, and uses its own filter coefficient
    table.
  - Bit-identity against it therefore proves:
    - the R4 code memory and the origin trim are exact;
    - a flat tone and a settled `off` filter are exact pass-throughs;
    - `crumpBit_filterMix()` matches the voice forms.
  - The Sync target uses the S074 walk, so the search is checked
    independently inside every block.
- **Rate writes:** CB4 writes `rte` through the `effective_max` clamp and
  re-clamps on every share change, as `effects_service()` does. It also
  checks that the op equals a linear scan.
- **Results (scratch build):**
  - `crumpbit failures: 0`, and all five breaks caught;
  - CB8 at `fbk` 127 with `ton` 40/80/120:

    | Input | Peak | Mean loop code |
    |---|---:|---:|
    | silence | 1.000 | −0.062 LSB |
    | DC 0.5 | 1.000 | −0.004 LSB |
    | full-scale noise | 1.000 | −0.001 LSB |

  - The run takes about 3 s.

```c
/*
 * CrumpBit host tests (S091 P1 reductions; S091 P2 expansion).
 *
 * What:       compiles the real Core/DSP/Effects/CrumpBit/CrumpBitEffect.c
 *             and its generated tables (firmware includes stripped by
 *             extract.py) and checks:
 *               CB1  Sync division: crumpBit_divisionFor() and
 *                    crumpBit_syncDivision() against the 128-step S074 walk
 *                    (generalised to a live maximum, S091 P2), for every
 *                    rate at BPM 0..1000 and five share limits, every exact
 *                    division length (ties), and random targets/limits;
 *               CB2  8-bit ADC: crumpBit_adc() against the clamped S074 form
 *                    at every code edge (+-8 ulps), across +-3 and at large
 *                    magnitudes;
 *               CB3  code-difference DAC: crumpBit_codeToFloat(c - p) equals
 *                    the S074 (c - 128)/128 - (p - 128)/128 for every pair;
 *               CB4  whole block: crumpBit_process() against the reference
 *                    below, written from the S091 P2 equations with the
 *                    S074 float AC memory and STORED float ramp origins
 *                    (which the firmware recomputes from raw bytes), over
 *                    60 s of random blocks with parameter jumps (every row,
 *                    every filter type), out-of-range input, mono
 *                    io->r == NULL, short blocks and share changes (tail
 *                    kept or re-seated); outputs, loop bytes and state are
 *                    compared bit for bit, and the seat invariants
 *                    (delay <= length - 2, write_pos < length,
 *                    valid <= length) are checked after every block;
 *               CB5  init: AC memory 128, tone flat, filter `off` (and its
 *                    ramp origin `off`);
 *               CB6  tables and Rate limit: crumpBit_rateTable endpoints
 *                    and strict order, crumpBit_rateSamples() is the table,
 *                    crumpBit_rateLimit() equals a linear scan for every
 *                    share 0..CRUMPBIT_LOOP_MAX_BYTES in 32-byte steps (127
 *                    for the whole arena), the effective_max op clamps only
 *                    the Rate; toneK[63] == 1; filterG[0] == 0, increasing;
 *               CB7  output resonance bound: crumpBit_filterDamping() is in
 *                    [0.0625, 1] for every `res` (Q <= 8), 1 at res 0;
 *               CB8  stability at fbk 127 (gain 1.2): 60 s of
 *                    self-oscillation after a burst, with DC input, silence
 *                    and full-scale input; no NaN/Inf, |output| < 4, and
 *                    no DC lock-up (the loop's mean code stays within
 *                    +-8 LSB of silence);
 *               CB9  row ownership: CRUMPBIT_ROWS_SHARED and
 *                    CRUMPBIT_ROWS_DELAY are disjoint and cover rows
 *                    3..COUNT-1 (the firmware also asserts this at compile
 *                    time; the host law keeps the test honest).
 * Why:        the board cannot report output sample by sample; the
 *             reference proves the code computes exactly the documented
 *             equations (including that the ramp-origin trim is exact and
 *             that a flat tone and a settled `off` filter pass bit-exactly),
 *             and the ARM shape is gated on the linked ELF
 *             (crumpbit_elf_check).
 * Inputs:     build/gen/cb_*.c|h, CrumpBitEffect.host.h/.c (extract.py).
 * Outputs:    one line per failure (first 40); "crumpbit failures: N";
 *             exit 1 if N > 0.
 * Accessors:  make -C tools/dsp_test crumpbit (four break builds follow).
 * Affiliates: CrumpBitEffect.c/.h; CrumpBitTables.c; CrumpBitParameters.h;
 *             StepScale.c; S091_P2_Implementation.md §5.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Host substitutes for firmware-only pieces ------------------------------ */

/* __USAT: the ARM instruction clamps a signed value to [0, 2^bits - 1]. */
static inline uint32_t host_usat(int32_t v, unsigned bits)
{
    const int32_t hi = (int32_t)((1u << bits) - 1u);

    return (uint32_t)(v < 0 ? 0 : (v > hi ? hi : v));
}
#define __USAT(v, b) host_usat((int32_t)(v), (b))

#define EFFECT_COMMON_PARAM_AUDIO_OUT 0u
#define EFFECT_COMMON_PARAM_LEVEL     1u
#define EFFECT_COMMON_PARAM_PAN       2u
#define EFFECT_COMMON_PARAM_COUNT     3u

/* seq_getBpm(): the test sets the tempo the DSP reads. */
static uint16_t cb_bpm = 120u;
uint16_t seq_getBpm(void) { return cb_bpm; }

#include "cb_stepscale.h"
#include "cb_stepscale.c"
#include "cb_fxbuf.c"
#include "cb_effects.c"
#include "cb_params.c"
#include "cb_tables.h"
#include "cb_tables.c"
#include "CrumpBitEffect.host.h"
#include "CrumpBitEffect.host.c"

/* S074 reference pieces that still define today's behaviour -------------- */

/* The S074 ADC (CrumpBitEffect.c at 93f35799), for CB2. */
static inline uint32_t ref_adc(float x)
{
    uint32_t code;

    x = (x > 1.0f) ? 1.0f : x;
    x = (x < -1.0f) ? -1.0f : x;
    code = (uint32_t)(x * 128.0f + 128.5f);
    return code - (code >> 8);
}

/*
 * The S074 128-step Sync walk with its constant limit replaced by max (the
 * only S091 P2 change to the rule), for CB1 and the CB4 reference.
 */
static uint8_t ref_divisionFor(float target, float per_tick, float max)
{
    uint8_t low = CRUMPBIT_DIVISION_NONE;
    uint8_t high = CRUMPBIT_DIVISION_NONE;
    float t_low = 0.0f;
    float t_high = 0.0f;
    uint8_t i;

    for (i = 0u; i < STEP_SCALE_COUNT; i++) {
        const float t = ((float)stepScale_ticksQ8(i) / 256.0f) * per_tick;

        if (t > max)
            continue;
        if (t <= target) {
            low = i;
            t_low = t;
        } else if (high == CRUMPBIT_DIVISION_NONE) {
            high = i;
            t_high = t;
        }
    }
    if (low == CRUMPBIT_DIVISION_NONE)
        return (high == CRUMPBIT_DIVISION_NONE) ? 0u : high;
    if (high == CRUMPBIT_DIVISION_NONE)
        return low;
    return (target * target < t_low * t_high) ? low : high;
}

/* Rate limit by linear scan: the largest rate whose length fits. */
static uint8_t ref_rateLimit(uint32_t bytes)
{
    uint32_t len = (bytes > CRUMPBIT_LOOP_MAX_BYTES)
        ? CRUMPBIT_LOOP_MAX_BYTES : bytes;
    const float max = (len < CRUMPBIT_LOOP_MIN) ? 0.0f : (float)(len - 2u);
    uint8_t r = 0u;
    uint8_t i;

    for (i = 0u; i < 128u; i++)
        if (crumpBit_rateTable[i] <= max)
            r = i;
    return r;
}

/*
 * S091 P2 reference process, written from the plan's equations
 * (S091_P2_CrumpBit_Expanded_Plan.md §5) in the firmware's operation order.
 * Deliberate differences from the firmware form, each proving one claim:
 * - AC input memory is the S074 float (ac_x = (code - 128)/128), not a code;
 * - ramp origins (mix, fb, gain_l, gain_r and the output mix cx/cb/cl) are
 *   STORED floats, as S074 stored them, not recomputed from raw bytes;
 * - the tone output is chosen by branch (LP s, flat v, HP v - s), not by the
 *   (a, b) weights;
 * - the filter mix uses its own coefficient table.
 */
typedef struct {
    uint32_t length;
    uint32_t write_pos;
    uint32_t valid;
    float delay;
    float mix;
    float feedback;
    float gain_l;
    float gain_r;
    float ac_x_l;
    float ac_x_r;
    float ac_y_l;
    float ac_y_r;
    float dc_x;
    float dc_y;
    float tone_s;
    float s_l1;
    float s_l2;
    float s_r1;
    float s_r2;
    float cx;
    float cb;
    float cl;
    uint8_t bit_off;
    uint8_t bit_invert;
    uint8_t mix_raw;
    uint8_t feedback_raw;
    uint8_t rate;
    uint8_t sync;
    uint8_t pan_raw;
    uint8_t tone_raw;
    uint8_t filter_type;
    uint8_t filter_freq;
    uint8_t filter_reso;
    uint8_t primed;
} RefP2;

static void ref2_mix(uint8_t type, float r, float c[3])
{
    static const float table[8][3] = {
        { 0.0f, 0.0f, 1.0f },    /* LP   (k entries patched below) */
        { 1.0f, 0.0f, -1.0f },   /* HP:  cb = -k */
        { 0.0f, 1.0f, 0.0f },    /* BP */
        { 0.0f, 0.0f, 0.0f },    /* UBP: cb = k */
        { 1.0f, 0.0f, 0.0f },    /* Nch: cb = -k */
        { -1.0f, 0.0f, 2.0f },   /* Pek: cb = k */
        { 0.0f, 0.0f, 1.0f },    /* LP2 = LP */
        { 1.0f, 0.0f, 0.0f },    /* off */
    };
    const float k = 2.0f * r;
    const uint8_t t = (type > 7u) ? 7u : type;

    c[0] = table[t][0];
    c[1] = table[t][1];
    c[2] = table[t][2];
    if (t == 1u || t == 4u)
        c[1] = -k;
    if (t == 3u || t == 5u)
        c[1] = k;
}

static float ref2_damping(uint8_t reso)
{
    const float q = 1.0f - (float)reso * (1.0f / 127.0f);

    return (q < 0.1f) ? 0.0625f : q;
}

static float ref2_fbGain(uint8_t v)
{
    return (v <= 100u) ? (float)v * (1.0f / 100.0f)
                       : 1.0f + (float)(v - 100u) * (0.2f / 27.0f);
}

static void ref2_init(RefP2 *rt)
{
    float c[3];

    memset(rt, 0, sizeof *rt);
    rt->tone_raw = 63u;
    rt->filter_type = 7u;
    ref2_mix(7u, ref2_damping(0u), c);
    rt->cx = c[0];
    rt->cb = c[1];
    rt->cl = c[2];
}

static void ref2_writeParam(RefP2 *rt, uint8_t index, uint8_t value)
{
    switch (index) {
    case CRUMPBIT_PARAM_BIT_OFF:     rt->bit_off = value;               break;
    case CRUMPBIT_PARAM_BIT_INVERT:  rt->bit_invert = value;            break;
    case CRUMPBIT_PARAM_MIX:         rt->mix_raw = value;               break;
    case CRUMPBIT_PARAM_FEEDBACK:    rt->feedback_raw = value;          break;
    case CRUMPBIT_PARAM_RATE:        rt->rate = value;                  break;
    case CRUMPBIT_PARAM_SYNC:        rt->sync = (uint8_t)(value != 0u); break;
    case CRUMPBIT_PARAM_DLY_PAN:     rt->pan_raw = value;               break;
    case CRUMPBIT_PARAM_TONE:        rt->tone_raw = value;              break;
    case CRUMPBIT_PARAM_FILTER_TYPE: rt->filter_type = value;           break;
    case CRUMPBIT_PARAM_FILTER_FREQ: rt->filter_freq = value;           break;
    case CRUMPBIT_PARAM_FILTER_RESO: rt->filter_reso = value;           break;
    default:                                                            break;
    }
}

static uint32_t ref2_seat(RefP2 *rt, uint32_t bytes)
{
    uint32_t len = (bytes > CRUMPBIT_LOOP_MAX_BYTES)
        ? CRUMPBIT_LOOP_MAX_BYTES : bytes;

    if (len != rt->length) {
        if (rt->length != 0u && rt->write_pos < len) {
            if (rt->valid > rt->write_pos)
                rt->valid = rt->write_pos;
        } else {
            rt->write_pos = 0u;
            rt->valid = 0u;
        }
        rt->length = len;
        if (len >= 4u && rt->delay > (float)(len - 2u))
            rt->delay = (float)(len - 2u);
    }
    return len;
}

static void ref2_process(RefP2 *rt, float *io_l, float *io_r, uint32_t frames,
                         uint8_t *base, uint32_t bytes)
{
    float *left = io_l;
    float *right = io_r ? io_r : io_l;
    int8_t *loop = (int8_t *)base;
    const uint32_t len = ref2_seat(rt, bytes);
    float flen, max, target, per_tick, free_run, synced;
    float mix, fb, gl, gr, mix_to, fb_to, gl_to, gr_to;
    float mix_inc, fb_inc, gl_inc, gr_inc, per_frame;
    float k_tone, g, r, a1, a2, a3, c_to[3], cx_inc, cb_inc, cl_inc;
    float cx, cb, cl;
    uint32_t keep, invert, wp, valid, n;
    uint8_t t;

    if (len < 4u)
        return;
    flen = (float)len;
    max = (float)(len - 2u);
    invert = rt->bit_invert;
    keep = (uint32_t)~((uint32_t)rt->bit_off & ~invert) & 0xFFu;
    per_tick = CRUMPBIT_SAMPLES_PER_TICK / (float)(cb_bpm ? cb_bpm : 1u);
    free_run = crumpBit_rateTable[rt->rate > 127u ? 127u : rt->rate];
    synced = ((float)stepScale_ticksQ8(
        ref_divisionFor(free_run, per_tick, max)) / 256.0f) * per_tick;
    target = rt->sync ? synced : free_run;
    target = (target > max) ? max : target;
    if (!rt->primed) {
        rt->delay = target;
        rt->primed = 1u;
    }
    per_frame = 1.0f / (float)frames;
    mix_to = (float)rt->mix_raw * (1.0f / 127.0f);
    fb_to = ref2_fbGain(rt->feedback_raw);
    gl_to = (rt->pan_raw <= 63u) ? 1.0f
        : (float)(127u - rt->pan_raw) * (1.0f / 64.0f);
    gr_to = (rt->pan_raw >= 63u) ? 1.0f
        : (float)rt->pan_raw * (1.0f / 63.0f);
    mix = rt->mix;
    fb = rt->feedback;
    gl = rt->gain_l;
    gr = rt->gain_r;
    mix_inc = (mix_to - mix) * per_frame;
    fb_inc = (fb_to - fb) * per_frame;
    gl_inc = (gl_to - gl) * per_frame;
    gr_inc = (gr_to - gr) * per_frame;
    t = (rt->tone_raw > 127u) ? 127u : rt->tone_raw;
    k_tone = crumpBit_toneK[t];
    g = crumpBit_filterG[rt->filter_freq > 127u ? 127u : rt->filter_freq];
    r = ref2_damping(rt->filter_reso);
    a1 = 1.0f / (1.0f + g * (g + 2.0f * r));
    a2 = g * a1;
    a3 = g * a2;
    ref2_mix(rt->filter_type, r, c_to);
    cx = rt->cx;
    cb = rt->cb;
    cl = rt->cl;
    cx_inc = (c_to[0] - cx) * per_frame;
    cb_inc = (c_to[1] - cb) * per_frame;
    cl_inc = (c_to[2] - cl) * per_frame;
    wp = rt->write_pos;
    valid = rt->valid;

    for (n = 0u; n < frames; n++) {
        const uint32_t code_l = (ref_adc(left[n]) & keep) ^ invert;
        const uint32_t code_r = (ref_adc(right[n]) & keep) ^ invert;
        const float yl = (float)((int32_t)code_l - 128) * CRUMPBIT_LSB;
        const float yr = (float)((int32_t)code_r - 128) * CRUMPBIT_LSB;
        const float hl = yl - rt->ac_x_l + CRUMPBIT_AC_R * rt->ac_y_l;
        const float hr = yr - rt->ac_x_r + CRUMPBIT_AC_R * rt->ac_y_r;
        float rp, frac, wet, dc, tilt, ret;
        uint32_t i0, i1;

        rt->ac_x_l = yl;
        rt->ac_y_l = hl;
        rt->ac_x_r = yr;
        rt->ac_y_r = hr;
        mix += mix_inc;
        fb += fb_inc;
        gl += gl_inc;
        gr += gr_inc;
        rt->delay += (target - rt->delay) * CRUMPBIT_GLIDE_K;
        rp = (float)wp - rt->delay;
        rp += (rp < 0.0f) ? flen : 0.0f;
        i0 = (uint32_t)rp;
        frac = rp - (float)i0;
        i0 -= (i0 >= len) ? len : 0u;
        i1 = i0 + 1u;
        i1 -= (i1 >= len) ? len : 0u;
        wet = ((float)loop[i0] +
               frac * (float)(loop[i1] - loop[i0])) * CRUMPBIT_LSB;
        wet = ((rt->delay + 2.0f) <= (float)valid) ? wet : 0.0f;
        dc = wet - rt->dc_x + CRUMPBIT_DC_R * rt->dc_y;
        rt->dc_x = wet;
        rt->dc_y = dc;
        rt->tone_s += k_tone * (dc - rt->tone_s);
        if (t < 63u)
            tilt = rt->tone_s;
        else if (t == 63u)
            tilt = dc;
        else
            tilt = dc - rt->tone_s;
        ret = fb * tilt;
        ret = (ret > 1.5f) ? 1.5f : ret;
        ret = (ret < -1.5f) ? -1.5f : ret;
        ret = ret * (1.0f - (4.0f / 27.0f) * ret * ret);
        loop[wp] = (int8_t)((int32_t)ref_adc(0.5f * (hl + hr) + ret) - 128);
        wp++;
        wp -= (wp >= len) ? len : 0u;
        valid += (valid < len) ? 1u : 0u;
        right[n] = hr + mix * (gr * wet - hr);
        left[n] = hl + mix * (gl * wet - hl);
    }
    for (n = 0u; n < frames; n++) {
        const float xl = left[n];
        const float xr = right[n];
        float v1, v2, v3, yl, yr;

        cx += cx_inc;
        cb += cb_inc;
        cl += cl_inc;
        v3 = xl - rt->s_l2;
        v1 = a1 * rt->s_l1 + a2 * v3;
        v2 = rt->s_l2 + a2 * rt->s_l1 + a3 * v3;
        rt->s_l1 = 2.0f * v1 - rt->s_l1;
        rt->s_l2 = 2.0f * v2 - rt->s_l2;
        yl = cx * xl + cb * v1 + cl * v2;
        v3 = xr - rt->s_r2;
        v1 = a1 * rt->s_r1 + a2 * v3;
        v2 = rt->s_r2 + a2 * rt->s_r1 + a3 * v3;
        rt->s_r1 = 2.0f * v1 - rt->s_r1;
        rt->s_r2 = 2.0f * v2 - rt->s_r2;
        yr = cx * xr + cb * v1 + cl * v2;
        right[n] = yr;
        left[n] = yl;
    }
    rt->write_pos = wp;
    rt->valid = valid;
    rt->mix = mix_to;
    rt->feedback = fb_to;
    rt->gain_l = gl_to;
    rt->gain_r = gr_to;
    rt->cx = c_to[0];
    rt->cb = c_to[1];
    rt->cl = c_to[2];
}

/* Test plumbing ----------------------------------------------------------- */

static int failures;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        if (failures < 40)
            printf("FAIL: %s\n", msg);
        failures++;
    }
}

static uint32_t rng_state = 0x5091u;

static uint32_t rnd(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static float rndf(float lo, float hi)
{
    return lo + (hi - lo) * ((float)(rnd() & 0xFFFFFFu) / 16777215.0f);
}

static int same_bits(float a, float b)
{
    uint32_t x;
    uint32_t y;

    memcpy(&x, &a, 4u);
    memcpy(&y, &b, 4u);
    return x == y;
}

/* Shares used by CB1 and CB4: whole arena, 1, 2, 6 and 12 units, odd. */
static const uint32_t cb_shares[] = {
    CRUMPBIT_LOOP_MAX_BYTES, 122272u, 117856u, 100192u,
    CRUMPBIT_LOOP_MIN_SHARE_BYTES, 40000u, 64u
};
#define CB_SHARE_COUNT (sizeof cb_shares / sizeof cb_shares[0])

/* CB1: the Sync division ------------------------------------------------- */
static void law_cb1(void)
{
    uint32_t bpm;
    uint32_t i;
    uint32_t s;
    char msg[96];

    for (s = 0u; s < 5u; s++) {
        fx_share_t share = { NULL, 0u, cb_shares[s] };
        const float max = crumpBit_shareMaxSamples(&share);

        for (bpm = 0u; bpm <= 1000u; bpm++) {
            const float per_tick =
                CRUMPBIT_SAMPLES_PER_TICK / (float)(bpm ? bpm : 1u);
            uint32_t r;

            for (r = 0u; r < 128u; r++) {
                const float t = crumpBit_rateSamples((uint8_t)r);

                snprintf(msg, sizeof msg, "CB1 share %u rate %u bpm %u",
                         s, r, bpm);
                expect(crumpBit_divisionFor(t, per_tick, max) ==
                       ref_divisionFor(t, per_tick, max), msg);
                expect(crumpBit_syncDivision((uint8_t)r, (uint16_t)bpm,
                                             max) ==
                       ref_divisionFor(t, per_tick, max), msg);
            }
            for (i = 0u; i < STEP_SCALE_COUNT; i++) {
                const float t = ((float)stepScale_ticksQ8((uint8_t)i) /
                                 256.0f) * per_tick;
                const float probe[3] = { t, nextafterf(t, 0.0f),
                                         nextafterf(t, INFINITY) };
                uint32_t k;

                for (k = 0u; k < 3u; k++) {
                    snprintf(msg, sizeof msg,
                             "CB1 tie share %u cc %u bpm %u k %u",
                             s, i, bpm, k);
                    expect(crumpBit_divisionFor(probe[k], per_tick, max) ==
                           ref_divisionFor(probe[k], per_tick, max), msg);
                }
            }
        }
    }
    for (i = 0u; i < 2000000u; i++) {
        const float per_tick = rndf(0.4f, 30000.0f);
        const float max = rndf(0.0f, CRUMPBIT_DELAY_CURVE_MAX);
        const float t = (i & 1u) ? rndf(0.0f, 2.0f * max)
                                 : max * rndf(0.999f, 1.001f);

        snprintf(msg, sizeof msg, "CB1 random %u", i);
        expect(crumpBit_divisionFor(t, per_tick, max) ==
               ref_divisionFor(t, per_tick, max), msg);
    }
}

/* CB2: the 8-bit ADC ----------------------------------------------------- */
static void law_cb2(void)
{
    uint32_t i;
    int32_t k;
    char msg[96];

    for (k = -2; k <= 258; k++) {
        float x = ((float)k - 128.5f) / 128.0f;
        int32_t u;

        for (u = 0; u < 8; u++)
            x = nextafterf(x, -INFINITY);
        for (u = 0; u < 17; u++) {
            snprintf(msg, sizeof msg, "CB2 edge %d step %d", k, u);
            expect(crumpBit_adc(x) == ref_adc(x), msg);
            x = nextafterf(x, INFINITY);
        }
    }
    for (i = 0u; i < 4000000u; i++) {
        const float x = rndf(-3.0f, 3.0f);

        snprintf(msg, sizeof msg, "CB2 sweep %u", i);
        expect(crumpBit_adc(x) == ref_adc(x), msg);
    }
    {
        static const float big[] = { 1.0f, -1.0f, 0.0f, -0.0f, 6.0f, -6.0f,
                                     1.0e6f, -1.0e6f, 1.6e7f, -1.6e7f };
        for (i = 0u; i < sizeof big / sizeof big[0]; i++) {
            snprintf(msg, sizeof msg, "CB2 big %g", (double)big[i]);
            expect(crumpBit_adc(big[i]) == ref_adc(big[i]), msg);
        }
    }
}

/* CB3: code-difference DAC is exact -------------------------------------- */
static void law_cb3(void)
{
    int32_t c;
    int32_t p;
    char msg[96];

    for (c = 0; c < 256; c++)
        for (p = 0; p < 256; p++) {
            const float want = (float)(c - 128) * CRUMPBIT_LSB -
                               (float)(p - 128) * CRUMPBIT_LSB;

            snprintf(msg, sizeof msg, "CB3 c %d p %d", c, p);
            expect(same_bits(crumpBit_codeToFloat(c - p), want), msg);
        }
}

/* CB4 + CB5: the whole block against the P2 reference -------------------- */
#define CB_ARENA CRUMPBIT_LOOP_MAX_BYTES

static uint8_t arena_new[CB_ARENA];
static uint8_t arena_ref[CB_ARENA];

/* Write one row to both runtimes; Rate goes through the share clamp. */
static void cb4_write(CrumpBitRuntime *rt, RefP2 *ref, uint8_t row,
                      uint8_t v, const fx_share_t *share)
{
    if (row == CRUMPBIT_PARAM_RATE) {
        const uint8_t lim = crumpBit_ops.effective_max(row, share);

        expect(lim == ref_rateLimit(share->bytes), "CB4 rate limit");
        v = (v > lim) ? lim : v;
    }
    crumpBit_ops.write_param(rt, row, v);
    ref2_writeParam(ref, row, v);
}

static void law_cb4(void)
{
    static const uint8_t rows[] = {
        CRUMPBIT_PARAM_BIT_OFF, CRUMPBIT_PARAM_BIT_INVERT,
        CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE,
        CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN, CRUMPBIT_PARAM_TONE,
        CRUMPBIT_PARAM_FILTER_TYPE, CRUMPBIT_PARAM_FILTER_FREQ,
        CRUMPBIT_PARAM_FILTER_RESO };
    CrumpBitRuntime rt;
    RefP2 ref;
    fx_share_t share;
    float l_new[32], r_new[32], l_ref[32], r_ref[32];
    uint8_t rate_intended = 64u;
    uint32_t block;
    uint32_t share_i = 0u;
    char msg[128];

    memset(arena_new, 0x55, sizeof arena_new);
    memcpy(arena_ref, arena_new, sizeof arena_ref);
    memset(&rt, 0, sizeof rt);
    crumpBit_ops.init(&rt, NULL);
    ref2_init(&ref);
    expect(rt.ac_c_l == 128 && rt.ac_c_r == 128, "CB5 AC memory seeded 128");
    expect(rt.tone_raw == CRUMPBIT_TONE_FLAT, "CB5 tone flat");
    expect(rt.filter_type == CRUMPBIT_FILTER_OFF &&
           rt.filter_type_prev == CRUMPBIT_FILTER_OFF, "CB5 filter off");
    share.base = arena_new;
    share.offset = 0u;
    share.bytes = cb_shares[0];
    cb4_write(&rt, &ref, CRUMPBIT_PARAM_FEEDBACK, 64u, &share);
    cb4_write(&rt, &ref, CRUMPBIT_PARAM_RATE, rate_intended, &share);
    cb4_write(&rt, &ref, CRUMPBIT_PARAM_DLY_PAN, 63u, &share);

    for (block = 0u; block < 82800u; block++) {    /* 60 s at 32 frames */
        effect_io_t io;
        const uint32_t mono = ((block / 997u) % 7u) == 3u;
        const uint32_t frames = ((block % 211u) == 5u)
            ? 1u + (rnd() % 32u) : 32u;
        const uint32_t kind = (block / 400u) % 5u;
        uint32_t n;

        if ((block % 13u) == 0u || (block % 1000u) < 40u) {
            const uint8_t row = rows[rnd() % (sizeof rows)];
            uint8_t v = (uint8_t)(rnd() & 0x7Fu);

            if (row == CRUMPBIT_PARAM_BIT_OFF ||
                row == CRUMPBIT_PARAM_BIT_INVERT)
                v = ((rnd() & 3u) == 0u) ? (uint8_t)rnd() : 0u;
            if (row == CRUMPBIT_PARAM_FILTER_TYPE)
                v = (uint8_t)(rnd() & 7u);
            if (row == CRUMPBIT_PARAM_SYNC)
                v = (uint8_t)(rnd() & 1u);
            if (row == CRUMPBIT_PARAM_TONE && (rnd() & 3u) == 0u)
                v = CRUMPBIT_TONE_FLAT;
            if (row == CRUMPBIT_PARAM_RATE)
                rate_intended = v;
            cb4_write(&rt, &ref, row, v, &share);
        }
        if ((block % 5000u) == 2500u)
            cb_bpm = (uint16_t)(20u + rnd() % 280u);
        /* Share changes (voice units claimed or released), then the
         * manager's forced re-resolution re-clamps the retained Rate. */
        if ((block % 3001u) == 3000u) {
            share_i = (share_i + 1u + rnd() % 3u) % CB_SHARE_COUNT;
            share.bytes = cb_shares[share_i];
            crumpBit_ops.buffer_changed(&rt, &share);
            (void)ref2_seat(&ref, share.bytes);
            cb4_write(&rt, &ref, CRUMPBIT_PARAM_RATE, rate_intended, &share);
        }
        for (n = 0u; n < frames; n++) {
            float a;
            float b;

            switch (kind) {
            case 0: a = rndf(-1.0f, 1.0f); b = rndf(-1.0f, 1.0f); break;
            case 1: a = 0.0f; b = 0.0f; break;
            case 2: a = rndf(-3.0f, 3.0f); b = rndf(-3.0f, 3.0f); break;
            case 3: a = 1.0f; b = -1.0f; break;
            default: a = 0.25f * sinf((float)(block * 32u + n) * 0.01f);
                     b = a * 0.5f; break;
            }
            l_new[n] = a; r_new[n] = b; l_ref[n] = a; r_ref[n] = b;
        }
        io.l = l_new; io.r = mono ? NULL : r_new;
        io.frames = (uint8_t)frames; io.channels = mono ? 1u : 2u;
        io.share = &share;
        crumpBit_ops.process(&rt, &io);
        ref2_process(&ref, l_ref, mono ? NULL : r_ref, frames, arena_ref,
                     share.bytes);

        for (n = 0u; n < frames; n++) {
            snprintf(msg, sizeof msg, "CB4 block %u frame %u", block, n);
            expect(same_bits(l_new[n], l_ref[n]), msg);
            if (!mono)
                expect(same_bits(r_new[n], r_ref[n]), msg);
        }
        snprintf(msg, sizeof msg, "CB4 state block %u", block);
        expect(rt.length == ref.length && rt.write_pos == ref.write_pos &&
               rt.valid == ref.valid && same_bits(rt.delay, ref.delay) &&
               same_bits(rt.ac_y_l, ref.ac_y_l) &&
               same_bits(rt.ac_y_r, ref.ac_y_r) &&
               same_bits((float)(rt.ac_c_l - 128) * CRUMPBIT_LSB,
                         ref.ac_x_l) &&
               same_bits((float)(rt.ac_c_r - 128) * CRUMPBIT_LSB,
                         ref.ac_x_r) &&
               same_bits(rt.dc_x, ref.dc_x) && same_bits(rt.dc_y, ref.dc_y) &&
               same_bits(rt.tone_s, ref.tone_s) &&
               same_bits(rt.svf_l1, ref.s_l1) &&
               same_bits(rt.svf_l2, ref.s_l2) &&
               same_bits(rt.svf_r1, ref.s_r1) &&
               same_bits(rt.svf_r2, ref.s_r2), msg);
        snprintf(msg, sizeof msg, "CB4 seat invariant block %u", block);
        expect(rt.length < CRUMPBIT_LOOP_MIN ||
               (rt.delay <= (float)(rt.length - 2u) &&
                rt.write_pos < rt.length && rt.valid <= rt.length), msg);
    }
    expect(memcmp(arena_new, arena_ref, sizeof arena_new) == 0,
           "CB4 loop bytes identical after 60 s");
}

/* CB6: tables and the Rate limit ----------------------------------------- */
static void law_cb6(void)
{
    uint32_t i;
    uint32_t bytes;
    char msg[96];

    expect(crumpBit_rateTable[0] == 882.0f, "CB6 rate[0] = 882");
    expect(crumpBit_rateTable[127] == CRUMPBIT_DELAY_CURVE_MAX,
           "CB6 rate[127] = curve max");
    for (i = 0u; i < 128u; i++) {
        snprintf(msg, sizeof msg, "CB6 rate %u", i);
        expect(same_bits(crumpBit_rateSamples((uint8_t)i),
                         crumpBit_rateTable[i]), msg);
        if (i)
            expect(crumpBit_rateTable[i] > crumpBit_rateTable[i - 1u], msg);
    }
    expect(crumpBit_rateSamples(200u) == crumpBit_rateTable[127],
           "CB6 rate > 127 reads the top");
    for (bytes = 0u; bytes <= CRUMPBIT_LOOP_MAX_BYTES + 64u; bytes += 32u) {
        fx_share_t share = { NULL, 0u, bytes };

        snprintf(msg, sizeof msg, "CB6 limit bytes %u", bytes);
        expect(crumpBit_rateLimit(&share) == ref_rateLimit(bytes), msg);
        expect(crumpBit_ops.effective_max(CRUMPBIT_PARAM_RATE, &share) ==
               ref_rateLimit(bytes), msg);
        expect(crumpBit_ops.effective_max(CRUMPBIT_PARAM_MIX, &share) ==
               255u, msg);
    }
    {
        fx_share_t full = { NULL, 0u, CRUMPBIT_LOOP_MAX_BYTES };

        expect(crumpBit_rateLimit(&full) == 127u, "CB6 whole arena = 127");
    }
    expect(crumpBit_toneK[CRUMPBIT_TONE_FLAT] == 1.0f, "CB6 toneK flat");
    expect(crumpBit_filterG[0] == 0.0f, "CB6 filterG[0] = 0");
    for (i = 1u; i < 128u; i++) {
        snprintf(msg, sizeof msg, "CB6 tables %u", i);
        expect(crumpBit_filterG[i] > crumpBit_filterG[i - 1u], msg);
        expect(crumpBit_toneK[i] > 0.0f && crumpBit_toneK[i] <= 1.0f, msg);
    }
}

/* CB7: output resonance bound -------------------------------------------- */
static void law_cb7(void)
{
    uint32_t v;
    char msg[64];

    expect(crumpBit_filterDamping(0u) == 1.0f, "CB7 res 0 = R 1");
    for (v = 0u; v < 128u; v++) {
        const float r = crumpBit_filterDamping((uint8_t)v);

        snprintf(msg, sizeof msg, "CB7 res %u", v);
        expect(r >= CRUMPBIT_FILTER_R_MIN && r <= 1.0f, msg);
    }
}

/* CB8: stability at the maximum feedback ---------------------------------- */
static void law_cb8(void)
{
    uint32_t pass;
    char msg[96];

    for (pass = 0u; pass < 3u; pass++) {
        CrumpBitRuntime rt;
        fx_share_t share = { arena_new, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        float l[32], r[32];
        float peak = 0.0f;
        int finite = 1;
        int64_t sum = 0;
        uint32_t block;
        uint32_t i;

        memset(arena_new, 0, sizeof arena_new);
        memset(&rt, 0, sizeof rt);
        crumpBit_ops.init(&rt, NULL);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_FEEDBACK, 127u);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_MIX, 127u);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_DLY_PAN, 63u);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_RATE, 20u);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_TONE,
                                 (uint8_t)(40u + 40u * pass));
        for (block = 0u; block < 82800u; block++) {
            effect_io_t io = { l, r, 32u, 2u, &share };
            uint32_t n;

            for (n = 0u; n < 32u; n++) {
                /* A 0.1 s burst, then the pass's steady input. */
                const float burst = (block < 138u) ? rndf(-1.0f, 1.0f) : 0.0f;
                const float steady = (pass == 0u) ? 0.0f
                                   : (pass == 1u) ? 0.5f : rndf(-1.0f, 1.0f);

                l[n] = burst + steady;
                r[n] = burst - steady;
            }
            crumpBit_ops.process(&rt, &io);
            for (n = 0u; n < 32u; n++) {
                finite &= isfinite(l[n]) && isfinite(r[n]);
                peak = fmaxf(peak, fmaxf(fabsf(l[n]), fabsf(r[n])));
            }
        }
        for (i = 0u; i < rt.length; i++)
            sum += (int8_t)arena_new[i];
        snprintf(msg, sizeof msg, "CB8 pass %u finite", pass);
        expect(finite, msg);
        snprintf(msg, sizeof msg, "CB8 pass %u peak %.3f < 4", pass,
                 (double)peak);
        expect(peak < 4.0f, msg);
        snprintf(msg, sizeof msg, "CB8 pass %u mean code %.3f within 8",
                 pass, (double)sum / (double)rt.length);
        expect(llabs(sum) <= (int64_t)8 * (int64_t)rt.length, msg);
        printf("CB8 pass %u: peak %.3f, mean loop code %.3f LSB\n", pass,
               (double)peak, (double)sum / (double)rt.length);
    }
}

/* CB9: row ownership ------------------------------------------------------ */
static void law_cb9(void)
{
    const uint32_t rows = CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_COUNT) -
                          CRUMPBIT_ROW_BIT(EFFECT_COMMON_PARAM_COUNT);

    expect((CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_DELAY) == 0u,
           "CB9 stage masks disjoint");
    expect((CRUMPBIT_ROWS_SHARED | CRUMPBIT_ROWS_DELAY) == rows,
           "CB9 every type row has one owner");
    expect((CRUMPBIT_ROWS_DELAY & CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_TONE)) != 0u,
           "CB9 ton is a delay sub-type row");
}

int main(void)
{
    law_cb1();
    law_cb2();
    law_cb3();
    law_cb4();
    law_cb6();
    law_cb7();
    law_cb8();
    law_cb9();
    printf("crumpbit failures: %d\n", failures);
    return failures ? 1 : 0;
}
```


---

## 5. Build, gates and hardware checks

**Order of work.**
1. Copy `build/lxr02.elf` to `build/s091p2_base.elf` (the G6
   before-image; no frozen snapshots are kept otherwise).
2. Add N1 and N3, then run
   `python3 tools/plots/gen_crumpbit_tables.py` from the repository root to
   create N2.
3. Apply H1–H4, E1–E13, P1–P3, C1–C6, R1, M1–M4, U1 and K1.
4. Apply T1–T5.
5. Apply D1–D5. D6 waits for the hardware result. D7 is already done.

**Gates.**

| # | Gate | Pass (scratch build) |
|---|---|---|
| G1 | `make all` | exit 0; no warning from any changed file |
| G2 | `make img` | `Written:build/LXRV2_lxr02.img (694156b) OK` (± a few bytes if the tree differs from HEAD) |
| G3 | `make -C tools/dsp_test crumpbit` | `crumpbit failures: 0`; `crumpbit_break1`…`5: caught` |
| G4 | `make -C tools/dsp_test crumpbit_tables` | `crumpbit tables OK` |
| G5 | `make -C tools/dsp_test crumpbit_elf_check` | `crumpBit_process` heads 126 / 126 / 31, vdiv 0; `crumpBit_divisionFor` 113; `crumpBit_rateLimit` 101 |
| G6 | Mixer loops: `elf_loopdump.py --sym mixer_calcNextSampleBlock` on the base and new ELF, then `elf_hotloops.py --compare BASE NOW` | `39 hot loops equal, 0 shrank, 0 grew`; the dumps, with branch displacements normalised (`sed -E 's/\+-?[0-9]+/OFF/g; s/@ ADDR//g; s/0x[0-9a-f]+/ADDR/g'`, sorted), are identical (86 loops) |
| G7 | `tools/link_budget.py` (run by `make all`) | ITCM 15,256 B; DTCM statics 4,372 B; FXBUF 126,688 B at `0x20001120` |
| G8 | `arm-none-eabi-size` | text 693,736; data 420; bss 413,028 |
| G9 | ITCM loops unchanged: `elf_looppath.py --sym calcNextOscSampleSyncBlock --top 4` | 11/9 and 9/9, as on the base |

**Hardware checks (user).** Keep the monitor level low for H5.

| # | Check | Expected |
|---|---|---|
| H0 | Before flashing: the worst-case Scene with `cbt`, every send open; the `cpu` widget and the underrun count (DEV `UNR` snippet in `main.c`) | the S091 P1 reading (already taken: −3–4 % against pre-S091) |
| H1 | The same Scene on the new image, then with `ton`, `typ` (LP, high `res`) and LFOs on `frq`/`ton`/`fbk` | `cpu` above H0 by about the +68 instructions per frame of §2 (≈ 1.4 % of the frame), and **at or below the pre-S091 reading**; no underruns |
| H2 | A Scene saved before the image | plays with `ton` flat and `typ` `off` (new rows default). `rte` and `fbk` sound different (re-scaled, no migration) |
| H3 | `DEV_FXBUF_FORCE_VOICE_UNITS` 0, 1, 2, 12 (boot) | the `rte` pot stops at the clamp; the label shows the clamped value (or division); delay times as §3.1 of the plan (2.87 / 2.77 / 2.67 / 1.67 s at the top) |
| H4 | Sync on at 120 BPM, `rte` swept up | divisions up to `1br` (2.0 s) with the whole arena; the label matches what plays |
| H5 | `fbk` 101..127 after a burst; then sweep `rte` | self-oscillation that settles at a bounded level (no rail lock, no DC thump); tape-speed sweeps |
| H6 | `ton` −63..+64 at `fbk` ≈ 110 | repeats darken (−) or thin (+); 0 sounds like no tone stage; the first echo is unfiltered |
| H7 | `typ` each type with `frq` swept and `res` 0/64/127; Morph between two types; Pattern lock on `typ` | voice-like filter responses; `res` 127 resonant but not explosive (Q 8); type steps without clicks; `off` = unfiltered |
| H8 | Page | s0 bit row + `ton fbk rte mrp`; s1 `ton fbk rte sub`; s2 `snc dpn mix typ` (type names as on a voice); s3 `frq res` |
| H9 | Enter `cbt` from `off` and from `flt`; same-type Scene switch with a ringing tail | no thump on entry; the tail keeps ringing |
| H10 | Save and reload the Scene; AutoSave round trip | the four new rows return (`crump_tone`, `crump_filter_*` keys) |

---

## 6. Documentation changes (same step)

### D1 — `tools/dsp_test/DSP_TEST.md`

- **Line 38:** replace the `crumpbit.c` description with
  `CrumpBitEffect.c: S091 search/ADC/DAC laws and the S091 P2 reference (CB1–CB9)`.
- **Lines 91–92 (targets table):** replace both rows with:

  | Target | What it checks |
  |---|---|
  | `crumpbit` | CB1 Sync search (live limit) against the S074 walk; CB2 ADC; CB3 code DAC; CB4 whole blocks bit-exact against the S091 P2 reference (stored ramp origins, branch tone, own filter table) with share changes and seat invariants; CB5 init; CB6 tables and Rate limit; CB7 Q ≤ 8; CB8 stability at feedback 1.2; CB9 row ownership. Five break builds |
  | `crumpbit_tables` | `gen_crumpbit_tables.py --check`: the committed tables equal the generator |
  | `crumpbit_elf_check` | linked image: every `crumpBit_process` loop ≤ 126 per frame, no VDIV (stage 2 126, output filter 31); `crumpBit_divisionFor` ≤ 117; `crumpBit_rateLimit` ≤ 101 (needs `make all` first; not in `all`) |

### D2 — `EFFECTS_MIXER_DSP_REFERENCE.md`

- **Line 312 (§4.2):** "CrumpBit is 56 B" → "CrumpBit is 76 B since S091
  P2 and fills the union".
- **§4.4 (lines 342–427):** rewrite to match `crumpBit_process()` (E11).
  - The per-sample block gains the return chain:

    ```
    dc   = wet − wet[n−1] + 0.99928775·dc[n−1]        DC blocker, ~5 Hz (in the loop)
    s   += k(ton)·(dc − s);  t = a·s + b·dc            tilt: LP (1,0), flat (0,1), HP (−1,1)
    v    = clamp(fb·t, ±1.5);  ret = v − (4/27)·v³     soft knee, ±1 at ±1.5
    loop[wp] = adc(½(hL + hR) + ret) − 128
    out  = h + mix·(g·wet − h)                         first echo unshaped
    output filter (second pass): TPT SVF, out = cx·x + cb·bp + cl·lp
    ```

  - **Rate and Sync:**
    - `free = rateTable[rte]`, 882 · 143.635^(rte/127): 20 ms at 0,
      2.872 s at 127, 0.244 s at 64;
    - clamped to `length − 2`;
    - `rte` BUFFER_DEPENDENT with `effective_max`;
    - Sync searched within the live limit, with the eighth probe.
  - **Feedback:** `v/100` up to 100, then `1 + (v − 100)·0.2/27`.
  - **Output filter:** per-block `g` from the voice-law table, `R` per the
    voice law with Q ≤ 8, `a1 = 1/(1 + g(g + 2R))`; types as E11's table,
    `LP2` = LP; the mix ramps over one block.
  - **Arena:** elastic, `min(share, 126,688)`; tail kept when `write_pos`
    fits; delay clamped on a shrink.
  - **Runtime:** 76 B (H2).
  - **Cost:** stage 2 126, output 31, per block ≈ 15–19: ≈ 172–176 per
    frame. Flash: `process` 1,924 B, `divisionFor` 380 B, `rateLimit`
    312 B, tables 1,664 B.
- **§5.1 table (line 453):** replace the CrumpBit row with
  `| CrumpBit's loop, the share up to 126,688 B | — | — | 2.872 s (whole arena) … 1.671 s (12 units) | — |`.
- **§6.2a (line 705):** replace the CrumpBit cost cell with
  "126 per frame (stage 2) + 31 (output filter) + per block a 113-instruction
  search, a 101-instruction Rate limit and three divides ≈ 172–176 per frame
  (measured S091 P2)".

### D3 — `EFFECTS_BUS_REFERENCE.md`

- **§4.2a row 2 (line 214):**
  - rows 15;
  - lanes `bof, biv, mix, fbk, rte, snc, vol, pan, dpn, ton, typ, frq, res`;
  - runtime 76 B;
  - arena "elastic: min 73,696 B (12 units), pref 126,688 B".
- **The paragraph after it (lines 216–221):** "pin CrumpBit at 56 B, so
  only 20 B remain" → "pin CrumpBit at 76 B: it fills the union, so any
  larger runtime grows `.dtcmz` (RAM approval)".
- **§8.6 table (lines 614–617):** replace the SELECT 2 rows with s0
  `ton fbk rte mrp`, s1 `ton fbk rte sub`, s2 `snc dpn mix typ`, s3
  `frq res` (two empty cells, reserved for the drive rows). The layout
  bullet becomes `screen_count = {0, 4, 0, …}` and "… → page 3 → page 4".
- **§8.6 value labels bullet:** add the share clamp. The shown Rate (number
  or division) is clamped to `crumpBit_rateLimit()` of the live share;
  page edits stop there (`effects_paramEffectiveMax()`).
- **Last paragraph (lines 656–657):** add "S091 P2 defaults: tone 63 (flat,
  shows 0), filter type `off`, frq 64, res 0."

### D4 — `STORAGE_SRAM_MANIFEST.md`

- **Line 697:** "CrumpBit 56 B, S074). Only 20 B spare before the union
  grows." → "CrumpBit 76 B since S091 P2: DC blocker, tone and output-filter
  state paid for by trims). No spare: a larger member grows the union."
- **Lines 717–718:** "CrumpBit (S074), the first arena user, takes 70,592 B
  of it" → "CrumpBit (S074), the first arena user, takes the whole share up
  to 126,688 B since S091 P2 (elastic; its Rate is clamped to the share)".

### D5 — `CPU_USE_DSP_AUDIT.md`

After item 61 (before the `---` at line 1007), add:

```markdown
62. **GREW WITHIN THE S091 SAVING (S091 P2): CrumpBit expansion.**
    Elastic delay (whole arena), feedback to 1.2, DC blocker, tone tilt
    and a cubic soft knee on the return, and a stereo TPT output filter
    (eight voice types, Q <= 8) as a second pass. Drives deferred.
    - Linked: stage-2 loop 98 -> 126 per frame (register pressure: six
      float constants and four literals re-materialised, five stack
      slots), output-filter loop 31, per block ~15-19: ~106 -> ~172-176
      per frame, below the pre-S091 ~191 (item 61). No per-sample VDIV.
    - Per-block tables replace expf, fastTan and valueShaper divisions;
      the Rate limit (101 instructions) runs once per block in
      effects_service().
    - RAM 0 B (runtime 76 B fills the union); text +3,576 B.
    - Host CB1-CB9, five breaks; gate crumpbit_elf_check.
```

### D6 — `MEMORY.md` (after the hardware result)

Add a status line after the S090 lines, and under **Binding Rules → Effects**
add:

- **CrumpBit stages (S091 P2):**
  - Rows are owned by `CRUMPBIT_ROWS_SHARED` or a sub-type mask
    (`CRUMPBIT_ROWS_DELAY`); compile-time guards require exactly one owner.
  - The output filter is shared and comes last.
  - `rte` is reversed and elastic (BUFFER_DEPENDENT, `effective_max`).
  - Lanes 14–15 are reserved for the deferred drive rows.

### D7 — `S091_P2_CrumpBit_Expanded_Plan.md` (done 2026-11-04)

The plan's §0 records the drive deferral and the first-implementation
differences, and rows 15/16 are marked deferred.

---

## 7. Optional reductions (not in this schedule)

Use these only if H1 reads above the pre-S091 figure. Each changes code
that this schedule verifies, so it needs its own measurement.

| # | Change | Expected |
|---|---|---|
| O1 | The delay-pan gains and `mix` ramp as two products `mix·gL`, `mix·gR`, plus `1 − mix`. Two fewer live floats in stage 2, but the ramp shape becomes quadratic within a block (rounding-level change) | −4 to −8 (fewer constant reloads) |
| O2 | Move the knee clamp constants into one ±1.5 pair loaded with `vldr` before the loop, via a local `const float` pair | depends on GCC's allocation; measure |

---

## 8. Risks and notes

### 8.1 Finding in the S091 P1 search (fixed here)

- **The fault:** `crumpBit_divisionFor()`'s seven steps (64 … 1) sum to
  127. `count` could not reach 128, its `count == STEP_SCALE_COUNT` branch
  was dead, and its comment ("reach any count in 0..128") was wrong.
- **The output was nevertheless always right:**
  - When all 128 positions pass, `count` stops at 127, so `low` = 126 and
    `high` = position 127.
  - Position 127 passed, so its length is ≤ target. Both lengths are then
    ≤ target, so `target² ≥ t_low·t_high`, and the test returns `high` =
    127, the S074 answer.
  - S091 P1's CB1 confirmed this exhaustively.
- **How it surfaced:** the same pattern copied into `crumpBit_rateLimit()`
  does give a wrong answer (126 for the whole arena). CB6 caught it in the
  scratch build.
- **The fix:** E4/E5 add the eighth probe to both searches.
- **The S091 P1 assessment** ("no defect found") stands for output. It
  missed this comment/dead-branch fault.

### 8.2 Behaviour and compatibility

- **Saved Scenes change audibly** (no migration, standing rule):
  - `rte` is reversed and re-scaled (64: 0.176 s → 0.244 s);
  - `fbk` 64 goes from 0.50 to 0.64;
  - the in-loop DC blocker very slightly shortens very long low-frequency
    tails;
  - the soft knee compresses loud repeats: −1.4 dB per pass at a
    full-scale return, −0.3 dB at half scale.

  The original plan's "Scenes below unity unchanged" is therefore not
  literal. The difference is small.
- **Retained values above the clamp:**
  - MIDI, Pattern and LFO writes to `rte` are clamped at resolution
    (`effects_service()`), not stored clamped.
  - On the page the value shows clamped, and the first edit jumps to the
    clamp.
- **Share changes:** no Instrument claims voice units at run time today, so
  only `DEV_FXBUF_FORCE_VOICE_UNITS` (boot) exercises the clamp on the
  board. Run-time share changes are covered by CB4.
- **Filter at `frq` 0:** `g` = 0, so LP/BP output 0 and HP passes. This is
  voice behaviour.
- **Voice alignment:** `g` comes from a double-precision table of the voice
  law, while a voice computes it in single precision. They differ in the
  last bits only.

### 8.3 Code shape

- **Register pressure in stage 2:**
  - The 126 includes about 10 instructions of constant
    re-materialisation and 5 of stack traffic.
  - A later feature in this loop (the deferred `tdr`) will move spills
    again. Re-measure with G5 at every step.
- **ITCM +16 B:** LTO changed setup code in two oscillator functions (wider
  encodings, moved stores). G9 shows their loops are unchanged. The ITCM
  budget has 1,128 B left.
- **The `effective_max` hook runs per block in the render**
  (`effects_service()` from `mixer_calcNextSampleBlock()`): 101
  instructions, counted in §2.
- **Mono:** the output pass reads both inputs before storing either, so
  a mono block (`right` aliases `left`) keeps stage 2's left-wins rule.
- **The `valid` mute** stays a branch (S074): 112 vs 126 in stage 2. The
  budget uses 126.

---

## 9. Implementation record

To be filled in when the schedule is applied: the gate results against §5,
any deviation, and the hardware results H0–H10.
