# S085 P1 — Implementation Schedule: Oscillator Types, Run Modes, Modulation Modes, Sample Flash v2

**Session:** 085, phase P1 (schedule + implementation; see §21)
**Branch:** `dev-ph7-voice_expansion` (base `91265e9c` + the S084 roll fix-up, uncommitted)
**Date:** 2026-10-26
**Plan of record:** `S085_P1_OSC_MOD_PLAN.md` (r6). "Plan §x" refers to it.
**Reference only:** the spike at `Helicase-dev-ph7-spike/` (`26fb7c3`). Nothing
here requires opening it. Every change is specified against the current tree,
with the code or the exact algorithm to write.

---

## 0. Read this first

### 0.1 Scope

| WP | Work package | Plan |
|---|---|---|
| WP0 | Step 0: pre-change snapshot and baseline probes | §5 item 10, §6 item 2 |
| WP1 | Sample flash catalogue v2: `SampleMemory.h/.c` | §4.3.3, §4.3.4 |
| WP2 | Installer core: `SampleInstall.h/.c` (new) | §5 item 8 |
| WP3 | Card walk: `storageTypes`, `filesystem`, the Load:[Samples] modal, the boot notice | §5 item 8 |
| WP4 | Stock waves `sq3 sw3 sqa tra swa`: generator, recipes, reader | §4.3.4 |
| WP5 | Oscillator data model, dispatch, per-sample slew: `Oscillator.h/.c`, `config.h` | §5 items 1–2 |
| WP6 | `smp`/`slp`: `OscillatorSample.h/.c` (new) | §5 item 2, Q10, Q11 |
| WP7 | `pwm`: `OscillatorPwm.h/.c` (new) | §5 item 2, Q10 |
| WP8 | `wtb`: `OscillatorWavetable.h/.c` (new), block-rate frame pair | Q9, §0.4 D1 |
| WP9 | Render pool: `OscRenderPool.h/.c` (new), `modulationNode.c`, `mixer.c` | §4.3.7, Q14 |
| WP10 | Oscillator section: `OscSection.h/.c` (new), `ResonantFilter.c/h` | §4.3.1, §4.3.7, Q5, Q10, Q15 |
| WP11 | Engines: Drum, Snare, Cymbal, HiHat | §4.2, §4.3.7 |
| WP12 | Descriptor rows, enums, counts, pages | §5 item 5, A6 |
| WP13 | InstrumentManager: writers, domains, claims, accessors, classifier | §5 item 5 |
| WP14 | Preset: selector store and apply | §5 item 6 |
| WP15 | Formatter, `MenuText.h`, `menu.c`, `menu.h` | §5 item 7 |
| WP16 | Build: `Makefile` | — |
| WP17 | Tools, host tests, ARM probes, the card move | §5 items 9–10, §6 |
| WP18 | Specifications and memory documents | §5 item 11 |
| WP19 | Gates, cost record, hardware matrix | §5 item 12, §10 |

**Out of scope** (unchanged from the plan): transient types, filter drive
types, Mix FX, voice buffers, the elastic Effect contract, LFO one-shots, the
deferred-swap fix, the converters. Old content is not migrated (Q8).

### 0.2 Line numbers and anchors

- **Baseline:** the working tree on 2026-10-26 (`59406a4` code plus the S084
  roll fix-up). Line numbers were read from that tree.
- **They drift** as edits land. Every change quotes its anchor text; re-find
  the anchor before editing.
- **Order inside a file:** edit from the bottom up, so earlier line numbers
  stay valid.
- **Action words:** **Add** (new lines at the anchor), **Modify** (replace the
  quoted lines), **Remove** (delete the quoted lines), **New file**.

### 0.3 Comment blocks

Every added or modified function, type, table and constant carries a block in
this project's contract layout. The block is written to be pasted beside the
code, in the `.h` (declaration) and the `.c` (definition). The `.c` copy may
add an implementation note.

```
/*
 * <Name> (S085 C<wp>.<n>).
 *
 * What:       what it does.
 * Why:        why it must exist (plan reference).
 * Inputs:     parameters and state read.
 * Outputs:    return value and state written.
 * Accessors:  who calls or reads it.
 * Affiliates: code that must stay in step with it.
 */
```

Removed code gets no block; the specifications and the handoff log record the
removal. Change IDs are `C<wp>.<n>`; comments in code cite them as
`(S085 C<wp>.<n>)`.

### 0.4 Decisions found in the deep dive

The deep dive found thirteen points where the plan, or the spike behaviour it
describes, does not hold in the current tree, or where a choice was left open.
Each has a decision that this schedule implements. **D1, D2 and D11 change what
the plan said; please confirm them.** The rest follow from decisions already
made.

| # | Finding | Decision implemented here |
|---|---|---|
| **D1** | **Q9 ("running frame pointers") does not lower the `wtb` worst case.** Pointers recomputed only when the frame index changes save work only when it doesn't change. A fast position sweep (LFO, Morph, automation) changes the frame index on every sample, so the per-sample multiplies return in exactly the worst case Q9 was meant to cut. | **A block-rate frame pair.** Each block selects two neighbouring frames once (two pointer computations per block) and ramps the blend between them per sample. Per-sample work becomes: two table reads per frame, two blends, no frame arithmetic. This lowers the worst case (estimate 64 → ≈ 42; FM 70 → ≈ 48). The cost: the frame position moves at most one frame per block (≈ 1,378 frames/s). A 104-frame table can still be swept end to end in 75 ms. Faster sweeps glide instead of jumping. **Confirm.** |
| **D2** | **LFO overlays overwrite the request bytes.** LFO targets on `wav` are written through `instrumentManager_writeRuntimeInternal(…, notify_base_change = 0)`, which calls the same special writer as a menu edit. The spike's `waveRequest` fix (P1 F2) therefore recorded every LFO block's value, not the stored one. A type change during LFO modulation re-derived the wave from the LFO's last value. | The special writer receives the `notify_base_change` flag (renamed `retained`). The `wav`/`src` writers record the request byte **only for retained writes** (menu, MIDI, load, Morph, automation). LFO overlays clamp the live value only. **Confirm** (no sound change in normal use; fixes a latent defect). |
| D3 | The plan's A6 says Snare `typ src run` at cells 3–5 "as Drum". Snare's page 0 already uses cells 0–4 (`coa fin noi mix wav`). | Snare: cells **5–7** (the second screen reads `wav typ src run`). Drum/Cymbal/HiHat: cells 3–5. |
| D4 | Q10 "`prc` matches `Rec`". Measured on today's tables: `saw(p) − saw(p + ½)` correlates **1.000** with `recTable` level 0 (0.9996 at level 5). The formula already matches, so the P1 F9 note was wrong. | No flip. A host check (C17.6) locks the polarity: `prc` at `wav` 0 against `recTable` must correlate > 0.99. |
| D5 | The spike's formatter read the oscillator type from the **runtime**. The runtime can differ from the shown Scene image: while step automation holds `typ`, during a deferred slot swap, or for a per-track played Scene. | The formatter reads the **stored** type from the same Scene image the cell value comes from. |
| D6 | The `snc` "no reset after a one-shot master ends" rule (Q10) needs a definition of "ended". | Ended = `smp` master whose read index has reached `end − 1`, the same test that makes `smp` output 0 (C6.2). Then the wrap test still runs and its result is discarded (constant cost). |
| D7 | `SVF_recalcFreq()` also runs `setDistortionShape()` under `USE_SHAPER_NONLINEARITY` (0 today). | `SVF_recalcFreqFrom()` mirrors that `#if`, so `fre`/`itp` and `rst` stay equivalent if the switch is ever set. |
| D8 | `menu_numSamples` and `menu_setNumSamples()` exist only to extend the `wav` list with samples. | Removed with their two callers. |
| D9 | The catalogue region (8,192 B) was sized for 233 + 128 entries. At 128 + 128 it needs 5,152 B. | Region **6,144 B** at `0x081FE800`. User space **1,243,456 B** (+2,048 B on the plan figure). |
| **D11** | **The `wav` domain was slot-aware in only one of three places.** Velocity range building (`instrumentManager_buildModulationRange()`) and the LFO base update (`instrumentManager_noteRuntimeValueChanged()`) used the global maximum, 10. Velocity and LFO on the `wav` row of a `pwm`/`smp`/`wtb` oscillator were therefore capped at 10. | All three use the per-slot domain (C13.6). **Confirm** (a fix; it changes what velocity and LFO reach on non-`wav` types). |
| D12 | The S078 Morph-view rule says SHIFT Morph shows and edits Normal for non-morphable rows. `menu_cellCommitValueEx()` drops a Morph-view edit of a non-morphable instrument cell (`menu.c:5242`), so the selector rows could not be edited under SHIFT. | Selector rows commit to Normal in the Morph view (C15.10). Other non-morphable instrument rows keep today's behaviour. |
| D13 | `snc` slave timing: the spike started the post-reset segment one sample early. The master wraps between samples `i` and `i + 1`. | The slave renders through sample `i`, then resets (C10.3). |
| D10 | The S0 proofs need the pre-S085 oscillator and engine source. `tools/dsp_test/frozen/` holds the **S073** snapshot, not today's code. | A second snapshot, `tools/dsp_test/frozen85/`, taken before the first edit (C0.1). The S085 goldens compare against it. |

### 0.5 Resources (approved, Q4)

**SRAM1 `.bss`:**

| Item | Bytes | Owner | Lifetime |
|---|---:|---|---|
| `oscRenderPool_scratch[32]` (int16) + `oscRenderPool_unitsUsed` | +65 (+3 alignment) | `OscRenderPool.c` | static, render scratch (approved at 129 B) |
| `fs_install_batch[32]`, `fs_install_batch_count`, `fs_install_last[4]`, `fs_install_report`, `fs_install_progress`, `fs_install_failed` | +2,313 | `filesystem.c` | static; used only inside the modal install |
| `sample_install` state | +44 | `SampleInstall.c` | static; modal install |
| `sample_wavetable_count`, `sample_catalogue_state`, `sample_stock_valid` | +3 | `SampleMemory.c` | static |
| `menu_installSummaryUntil` (uint16) | +2 | `menu.c` | static |
| `sample_manifest[120]` + `sample_manifest_count` removed | −13,441 | `filesystem.c` | released, not reused |
| `sample_info_cache`, `sample_name_cache`, `sample_loop_cache`, `install_info`, `install_names` and the v1 install scalars removed | −5,040 (+ ≈ 20 scalars) | `SampleMemory.c` | released, not reused |
| `sample_io_buf` 516 → 512 B | −4 | `filesystem.c` | — |
| `menu_numSamples` removed | −1 | `menu.c` | — |

**DTCM:** `modNode_waveInterpActiveCount` removed (−1 B). The existing
`osc_interp_a/b` (2 × 64 B, `INDTCMZ`) are unchanged.

**Runtime slots:**
- They stay **7,056 B** (6 × 1,176 B).
- The reserve becomes the fixed constant 1,176 B (C13.2). Without that, its
  `2 × sizeof(DrumVoice)` definition would grow it.
- Estimated engine sizes after S085: Drum ≈ 824, Snare ≈ 656, Cymbal ≈ 760,
  HiHat ≈ 768 B. Gate G3 measures them.

**Stack:** each level of the card walk keeps one 64 B `fs_install_entry_t` on
the stack. The depth is 3, so about 200 B are added in the modal install
only, with audio suspended.

**ITCM:**
- Estimate ≈ 10.0–10.9 KB of 16,384 B (today 4,168 B).
- The new renderers and the slew loops go there.
- Measure at G3 and record.

**Program flash:**
- Estimate +35–40 KB, which leaves ≈ 104–109 KB of headroom.
- `StockWaveRecipes.c` (≈ 10.2 KB) is part of it.

**Sample flash:**

| Region | Address | Bytes |
|---|---|---:|
| Stock block | `0x080C0000` | 61,120 |
| Items | `0x080CEEC0` | 1,243,456 |
| Catalogue | `0x081FE800` | 6,144 |

### 0.6 Constants (single source of truth)

| Constant | Value | File |
|---|---|---|
| `OSC_TYPE_WAV/PWM/SMP/SLP/WTB`, `OSC_TYPE_COUNT` | 0/1/2/3/4, 5 | `Oscillator.h` |
| `SQ3 SW3 SQA TRA SWA`, `STOCK_WAVE_FIRST` | 6–10, 6 | `Oscillator.h` |
| `OSC_STOCK_WAVE_COUNT` | 11 | `Oscillator.h` |
| `OSC_SLEW_SAMPLES` | 132 (3 ms at 44,108 Hz) | `Oscillator.h` |
| `OSC_SAMPLE_PHASE_SHIFT` | 12 (Q20.12) | `OscillatorSample.h` |
| `OSC_PWM_PRC … OSC_PWM_SWP`, `OSC_PWM_FAMILY_COUNT` | 0–5, 6 | `OscillatorPwm.h` |
| `OSC_RENDER_POOL_UNITS` | **2** (replaces `OSC_WAVE_INTERP_MAX_ACTIVE`) | `config.h` |
| `OSC_RUN_RST/FRE/ITP`, `OSC_RUN_COUNT` | 0/1/2, 3 | `OscSection.h` |
| `OSC_MOD_FM/MIX/SNC/RNG`, `OSC_MOD_COUNT` | 0/1/2/3, 4 | `OscSection.h` |
| `OSC_ITP_SAMPLES` | 132 | `OscSection.h` |
| `OSC_TRIGGER_RESET_NONE/ALL/CARRIER` | 0/1/2 | `OscSection.h` |
| `OSC_FILTER_SLEW_COEF` | 0.28 (≈ 2 ms at block rate) | `OscSection.c` |
| `IM_SPECIAL_OSC_TYPE/OSC_SOURCE/OSC_WAVE/RUN_MODE` | 20/21/22/23 (count 24 ≤ 32) | `InstrumentManager.h` |
| `INSTRUMENT_RUNTIME_SLOT_BYTES` | 1,176 (fixed) | `InstrumentManager.c` |
| `SAMPLE_MAX_COUNT`, `WAVETABLE_MAX_COUNT`, `WAVETABLE_MAX_FRAMES` | 128, 128, 128 | `SampleMemory.h` |
| `SAMPLE_STOCK_START_ADDRESS`, `SAMPLE_STOCK_BYTES` | `0x080C0000`, 61,120 | `SampleMemory.h` |
| `SAMPLE_DATA_START_ADDRESS` | `0x080CEEC0` | `SampleMemory.h` |
| `SAMPLE_CATALOGUE_START_ADDRESS`, `SAMPLE_CATALOGUE_BYTES` | `0x081FE800`, 6,144 | `SampleMemory.h` |
| `SAMPLE_CATALOGUE_MAGIC`, `SAMPLE_CATALOGUE_VERSION` | `0x3243584C` ("LXC2"), 2 | `SampleMemory.h` |
| `SAMPLE_GAIN_STEPS_PER_OCTAVE` | 24 | `SampleMemory.h` |
| `STOCK_WAVE_COUNT`, `STOCK_WAVE_LEVELS`, `STOCK_WAVE_POINTS_PER_WAVE` | 5, 11, 6,112 | `StockWaves.h` |
| Stock level lengths | 1,024 ×5, 512, 256, 128, 64, 16, 16 | `StockWaves.c`, `StockWaveGen.c` |
| Stock level harmonic counts | 511, 511, 337, 168, 84, 42, 21, 10, 5, 2, 1 | `StockWaveGen.c` |
| `FS_INSTALL_BATCH`, `FS_INSTALL_MAX_DEPTH`, `FS_INSTALL_KEY_LEN` | 32, 3, 49 | `filesystem.c` |
| `FS_BLOCK_OPEN_HANDLE_RETRIES` | 2,000 | `filesystem.c` |
| `FS_INSTALL_STAGE_ERASE/STOCK/SAMPLES/WAVETABLES/COMMIT` | 0–4 | `filesystem.h` |

**Stored zero is today's behaviour:** `typ` 0 = `wav`, `src` 0, `run` 0 =
`rst`, `mod` 0/1 = `fm`/`mix`. `wav` 0–5 keep their meaning; 6–10 are the new
stock waves; 11 and up clamp to `swa` (no migration, Q8).

### 0.7 Build order and gates (one change set, Q2)

The change set lands whole, with one hardware check. Internally it is built
in this order, so each layer compiles and is host-tested on the one before
it:

| Gate | After | Checks |
|---|---|---|
| G0 | WP0 | `frozen85/` exists; the baseline probe numbers are recorded (C0.2) |
| G1 | WP1–WP3 | `make all` clean; `natural_order`, `catalogue` pass. The `wav` list is still the old six (WP5 not yet in), and a sample install still builds |
| G2 | WP4 | `stock_gen` passes (±1 LSB against `frozen/stock_ref.bin`) |
| G3 | WP5–WP11 | `make all` clean; `osc_pwm`, `osc_sample`, `osc_run` (with the click test), `wtb`, `osc_s0` pass; `armcheck-osc` prints every loop; the `snc` cap decision (C10.6); runtime-slot sizes and ITCM recorded |
| G4 | WP12–WP16 | `special_tags` 177/0; the whole host suite; `make img`; `link_budget.py` recorded |
| G5 | WP17–WP19 | cost record written; specifications updated; card move; hardware matrix (C19.3) |

Each new host test must fail on one deliberately broken change before the gate
counts (C17.9).

### 0.8 Files touched

| File | Action | WP |
|---|---|---|
| `tools/dsp_test/snapshot.sh` | Modify (destination argument, S085 list) | 0 |
| `tools/dsp_test/frozen85/…` | New (snapshot) | 0 |
| `Core/SampleRom/SampleMemory.h/.c` | Rewrite | 1 |
| `Core/SampleRom/sampleFlash.c` | Comment only | 1 |
| `Core/SampleRom/SampleInstall.h/.c` | New | 2 |
| `Core/Hardware/SD/storageTypes.h/.c` | Add | 3 |
| `Core/Hardware/SD/filesystem.h/.c` | Modify / Remove / Add (installer region) | 3 |
| `Core/Menu/menu.c`, `Core/Menu/menu.h` | Modify (modal, cells, clamps, automation) | 3, 15 |
| `main.c` | Add (boot notice); Remove (`menu_setNumSamples`) | 3 |
| `tools/gen_stock_recipes.py`, `Core/SampleRom/StockWaveRecipes.c` | New | 4 |
| `Core/SampleRom/StockWaveGen.h/.c`, `Core/DSPAudio/StockWaves.h/.c` | New | 4 |
| `Core/DSPAudio/Oscillator.h/.c` | Modify | 5 |
| `config.h` | Modify | 5, 9 |
| `Core/DSPAudio/OscillatorSample.h/.c`, `OscillatorPwm.h/.c`, `OscillatorWavetable.h/.c`, `OscRenderPool.h/.c`, `OscSection.h/.c` | New | 6–10 |
| `Core/DSPAudio/modulationNode.c`, `mixer.c` | Modify | 9 |
| `Core/DSPAudio/ResonantFilter.h/.c` | Add | 10 |
| `Core/DSP/Instruments/*/…Voice.c/.h` (4 engines) | Modify | 11 |
| `Core/DSP/Instruments/*/…Parameters.c/.h` (4 tables) | Modify | 12 |
| `Core/DSP/Instruments/InstrumentManager.h/.c` | Modify | 13 |
| `Core/Bank/Scene/Preset/presetManager.c` | Modify | 14 |
| `Core/DSP/Instruments/InstrumentCellFormat.h/.c` | New | 15 |
| `Core/Menu/MenuText.h` | Modify | 15 |
| `Makefile` | Modify | 16 |
| `tools/dsp_test/*` (Makefile, tests, armcheck, checker, extract) | Modify / New | 17 |
| `tools/sampleoptimize.py`, `tools/plots/*`, `tools/dsp_test/looplen.py` | New | 17 |
| `SD_CARD/loops/` → `SD_CARD/wavetables/ak_hvox/` (move already in the tree), `SD_CARD/wavetables_old/ak_hvox/` | Optimise | 17 |
| specifications, `MEMORY.md`, `SCOPING_TARGETS.md`, handoff log | Modify | 18 |

---
## 1. WP0 — Step 0: snapshot and baseline

### C0.1 `tools/dsp_test/snapshot.sh` — destination argument and the S085 file list

- **Where:** lines 12–29, the whole script body.
- **Action:** **Modify.**
  - The script takes an optional destination directory (default
    `tools/dsp_test/frozen`, unchanged).
  - With `frozen85` it copies the S085 list.
- **Run once, before the first S085 edit:**
  `sh tools/dsp_test/snapshot.sh tools/dsp_test/frozen85`.

```sh
#!/bin/sh
# Freeze pre-change DSP sources for the golden harness (S073 Step 0; S085 C0.1).
#
# What:       copies every source file a change set will edit into a frozen
#             directory, preserving the relative path. With no argument it is
#             the S073 snapshot (tools/dsp_test/frozen); with
#             tools/dsp_test/frozen85 it freezes the pre-S085 oscillator,
#             section and engine sources.
# Why:        the old side of an S0 proof must be the exact pre-change source
#             text, independent of commits and later working-tree edits. The
#             S073 snapshot predates S074-S084 and cannot serve S085 (S085 D10).
# Inputs:     the working tree before the first edit; optional DEST.
# Outputs:    DEST/<same relative paths>.
# Accessors:  the implementer, once per change set.
# Affiliates: extract.py, the Makefile targets osc_s0 and armcheck-osc.
set -eu
cd "$(dirname "$0")/../.."
DEST=${1:-tools/dsp_test/frozen}
[ -e "$DEST" ] && { echo "snapshot exists: $DEST (delete it deliberately to refreeze)"; exit 1; }
case "$DEST" in
  *frozen85)
    FILES="Core/DSPAudio/Oscillator.c Core/DSPAudio/Oscillator.h \
      Core/DSPAudio/wavetable.c Core/DSPAudio/Samples.c \
      Core/DSPAudio/modulationNode.c Core/DSPAudio/mixer.c \
      Core/DSPAudio/BufferTools.c Core/DSPAudio/BufferTools.h \
      Core/DSPAudio/ResonantFilter.c Core/DSPAudio/ResonantFilter.h \
      Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Snare/Snare.c \
      Core/DSP/Instruments/Cymbal/CymbalVoice.c Core/DSP/Instruments/HiHat/HiHat.c" ;;
  *)
    FILES="Core/DSPAudio/ResonantFilter.c Core/DSPAudio/ResonantFilter.h \
      Core/DSPAudio/BufferTools.c Core/DSPAudio/BufferTools.h \
      Core/DSPAudio/distortion.c Core/DSPAudio/distortion.h \
      Core/DSPAudio/Oscillator.c Core/DSPAudio/mixer.c Core/DSPAudio/squareRootLut.c \
      Core/DSP/Instruments/Drum/DrumVoice.c Core/DSP/Instruments/Snare/Snare.c \
      Core/DSP/Instruments/Cymbal/CymbalVoice.c Core/DSP/Instruments/HiHat/HiHat.c \
      Core/Hardware/AudioCodecManager.c" ;;
esac
for f in $FILES; do
  mkdir -p "$DEST/$(dirname "$f")"
  cp "$f" "$DEST/$f"
done
echo "frozen $(find "$DEST" -type f | wc -l | tr -d ' ') files into $DEST"
```

### C0.2 Baseline probe run (no code)

- **What:** before WP1, run the new probe target `armcheck-osc` (C17.4) with
  only its `frozen85` half enabled (`make -C tools/dsp_test
  armcheck-osc-base`).
- **Record** the per-sample instruction counts of these pre-S085 loops in the
  handoff log, for the cost record (C19.2):
  - `calcSineBlock`, `calcFmSineBlock`;
  - `calcWavetableOscBlock`, `calcFmBlock`;
  - `calcNoiseBlock`;
  - `calcSampleOscBlock`;
  - `calcUserSampleOscBlock` (looped and one-shot paths) and
    `calcUserSampleOscFmBlock`.
- **Also record** `make all` sizes and `link_budget.py`. The current figures
  are flash 609,712, ITCM 4,168, DTCM 4,472.

---

## 2. WP1 — Sample flash catalogue v2

Sample flash becomes three regions:
- the generated stock-wave block;
- the user items (samples and wavetable frames, stored as supplied);
- a catalogue read in place.

The v1 RAM caches, the 120-entry limit, the loop flag and the append install
are removed.

### C1.1 `Core/SampleRom/SampleMemory.h` — layout, records, read API

- **Where:** lines 66–133 (from `#ifndef SAMPLEMEMORY_H_` to `#endif`); the
  file history block above (lines 1–64) stays and gains one line:
  `S085 C1.1: catalogue v2 (stock block, items, in-place catalogue).`
- **Action:** **Modify (rewrite the body).**
  - **Kept:** `SAMPLE_ROM_START_ADDRESS`, `SAMPLE_FIRST_SECTOR`,
    `SAMPLE_PAGE_SIZE`, `SAMPLE_DISPLAY_NAME_LEN` (the sampleFlash.c guards
    and the linker check depend on them).
  - **Removed:** `SAMPLE_INFO_*`, `SAMPLE_NAME_*`, `SAMPLE_ROM_SIZE`,
    `SAMPLE_INFO_LOOP_FLAG`, `SAMPLE_INFO_SIZE_MASK`, the old `SampleInfo`
    (`name[3]`), `sampleMemory_loadSamples()`, `sampleMemory_setNumSamples()`,
    `sampleMemory_isLooped()`, and all `sampleMemory_install*()` functions.
    Their replacement is `SampleInstall.h` (C2.1).

```c
#ifndef SAMPLEMEMORY_H_
#define SAMPLEMEMORY_H_

#include "stm32f4xx.h"
#include "config.h"
#include <stdint.h>

/* ---- Flash floor (unchanged since S073) ----
 * Sectors 7..11 (5 x 256 KB) hold sample flash; sectors 1..6 are the
 * application. SAMPLE_ROM_START_ADDRESS must equal the linker's
 * __sample_flash_start; sampleFlash.c refuses every erase/write otherwise. */
#define SAMPLE_ROM_START_ADDRESS    ((uint32_t)0x080C0000)   /* sector 7 base */
#define SAMPLE_FIRST_SECTOR         7u
#define SAMPLE_PAGE_SIZE            ((uint32_t)0x00040000)   /* 256 KB */
#define SAMPLE_DISPLAY_NAME_LEN     8u

/*
 * Sample flash map, catalogue v2 (S085 C1.1).
 *
 * What:       three regions inside sectors 7..11:
 *             - stock-wave block  0x080C0000  61,120 B (5 waves x 11 levels,
 *               generated at install, C4.3);
 *             - item data         0x080CEEC0  1,243,456 B (samples and
 *               wavetable frames, 4-byte aligned, stored as supplied);
 *             - catalogue         0x081FE800  6,144 B (32 B header, 128
 *               sample entries, 128 wavetable entries, 20 B each = 5,152 B).
 *             Region starts are 32-byte (cache-line) aligned so a D-cache
 *             invalidation never straddles two regions.
 * Why:        plan §4.3.4 (stock waves in sample flash, Q6 (a)); §4.3.3
 *             (128 samples, Q7); the 6,144 B catalogue is the S085 D9 size.
 *             The catalogue is read in place: no RAM copy (the v1 caches,
 *             5,040 B, are released).
 * Inputs:     compile-time constants.
 * Outputs:    addresses for SampleMemory.c, SampleInstall.c, StockWaveGen.c,
 *             StockWaves.c, OscillatorSample.c, OscillatorWavetable.c.
 * Accessors:  the files above; tools/dsp_test/test_catalogue.c (host map).
 * Affiliates: sampleFlash.c floor checks; STORAGE_SRAM_MANIFEST.md flash map.
 */
#define SAMPLE_FLASH_END_ADDRESS        ((uint32_t)0x08200000)
#define SAMPLE_STOCK_START_ADDRESS      SAMPLE_ROM_START_ADDRESS
#define SAMPLE_STOCK_BYTES              ((uint32_t)61120)
#define SAMPLE_DATA_START_ADDRESS       ((uint32_t)0x080CEEC0)
#define SAMPLE_CATALOGUE_START_ADDRESS  ((uint32_t)0x081FE800)
#define SAMPLE_CATALOGUE_BYTES          ((uint32_t)6144)
#define SAMPLE_CATALOGUE_MAGIC          ((uint32_t)0x3243584Cu) /* "LXC2" */
#define SAMPLE_CATALOGUE_VERSION        2u
#define SAMPLE_MAX_COUNT                128u
#define WAVETABLE_MAX_COUNT             128u
#define WAVETABLE_MAX_FRAMES            128u
#define SAMPLE_GAIN_STEPS_PER_OCTAVE    24u

_Static_assert(SAMPLE_STOCK_START_ADDRESS + SAMPLE_STOCK_BYTES <=
               SAMPLE_DATA_START_ADDRESS, "stock block overlaps item data");
_Static_assert((SAMPLE_DATA_START_ADDRESS & 31u) == 0u &&
               (SAMPLE_CATALOGUE_START_ADDRESS & 31u) == 0u,
               "sample-flash regions must be cache-line aligned");
_Static_assert(SAMPLE_CATALOGUE_START_ADDRESS + SAMPLE_CATALOGUE_BYTES ==
               SAMPLE_FLASH_END_ADDRESS, "catalogue ends at the flash end");

/*
 * Host-test address mapping (S085 C1.1).
 *
 * What:       every catalogue, item and stock read goes through
 *             SAMPLE_FLASH_PTR(), so the host test can map flash addresses
 *             onto a RAM image by defining the macro first.
 * Why:        test_catalogue.c installs into a RAM image of sectors 7..11
 *             and reads it back through this unchanged read API.
 * Inputs:     an absolute flash address.  Outputs: a const pointer.
 * Accessors:  SampleMemory.c, OscillatorSample.c, OscillatorWavetable.c,
 *             StockWaves.c.
 * Affiliates: tools/dsp_test/test_catalogue.c.
 */
#ifndef SAMPLE_FLASH_PTR
#define SAMPLE_FLASH_PTR(addr) ((const void *)(uintptr_t)(addr))
#endif

/*
 * Catalogue v2 records (S085 C1.1).
 *
 * What:       the 32 B header and 20 B entries, naturally aligned (no
 *             packing). Entries are programmed one by one as each item
 *             finishes, into erased flash; the header is programmed at
 *             commit, its magic word last, so a catalogue is valid only
 *             after a completed install. Unwritten bytes read 0xFF.
 * Why:        names, gain codes and wavetable geometry are read in place by
 *             the DSP binds and the menu (no RAM index).
 * Inputs:     written by SampleInstall.c.  Outputs: read by SampleMemory.c.
 * Accessors:  SampleMemory.c, SampleInstall.c, test_catalogue.c.
 * Affiliates: FILESYSTEM_SPEC.md "Sample flash", STORAGE_SRAM_MANIFEST.md.
 */
typedef struct {
    uint32_t magic;            /* SAMPLE_CATALOGUE_MAGIC, programmed last */
    uint16_t version;          /* SAMPLE_CATALOGUE_VERSION */
    uint16_t entry_bytes;      /* 20 */
    uint8_t  sample_count;     /* 0..SAMPLE_MAX_COUNT */
    uint8_t  wavetable_count;  /* 0..WAVETABLE_MAX_COUNT */
    uint8_t  stock_valid;      /* 1 = stock block generated */
    uint8_t  reserved0;
    uint32_t data_end;         /* first byte after the last item */
    uint32_t stock_start;      /* SAMPLE_STOCK_START_ADDRESS */
    uint32_t reserved[3];
} sample_catalogue_header_t;

typedef struct {
    uint32_t offset;           /* absolute address of frame 0, 4-aligned */
    uint32_t frames;           /* 16-bit frames (excluding the 2-frame pad) */
    char     name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  gain_code;        /* gain = 2^(code/24); never clips */
    uint8_t  reserved[3];
} sample_catalogue_sample_t;

typedef struct {
    uint32_t offset;           /* absolute address of frame 0, 4-aligned */
    uint16_t frame_len;        /* points per frame, >= 2 */
    uint8_t  frame_count;      /* 1..WAVETABLE_MAX_FRAMES */
    uint8_t  bits;             /* 8 or 16 */
    char     name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  reserved[4];
} sample_catalogue_wavetable_t;

_Static_assert(sizeof(sample_catalogue_header_t) == 32u, "header is 32 B");
_Static_assert(sizeof(sample_catalogue_sample_t) == 20u, "sample entry is 20 B");
_Static_assert(sizeof(sample_catalogue_wavetable_t) == 20u,
               "wavetable entry is 20 B");

#define SAMPLE_CATALOGUE_SAMPLES_ADDRESS \
    (SAMPLE_CATALOGUE_START_ADDRESS + sizeof(sample_catalogue_header_t))
#define SAMPLE_CATALOGUE_WAVETABLES_ADDRESS \
    (SAMPLE_CATALOGUE_SAMPLES_ADDRESS + \
     SAMPLE_MAX_COUNT * sizeof(sample_catalogue_sample_t))
_Static_assert(SAMPLE_CATALOGUE_WAVETABLES_ADDRESS +
               WAVETABLE_MAX_COUNT * sizeof(sample_catalogue_wavetable_t) <=
               SAMPLE_FLASH_END_ADDRESS, "catalogue exceeds its region");

/*
 * Resolved item descriptions (S085 C1.1).
 *
 * What:       SampleInfo: frames, absolute offset, gain code. WavetableInfo:
 *             offset, frame length, frame count, bit depth. An out-of-range
 *             sample index returns the silence buffer with size 0; an
 *             out-of-range wavetable index returns 0 from the getter.
 * Why:        the DSP binds read these on a catalogue generation or source
 *             change, never per sample.
 * Inputs:     an index.  Outputs: the structs.
 * Accessors:  OscillatorSample.c, OscillatorWavetable.c, InstrumentManager.c
 *             (maximum rules), InstrumentCellFormat.c, menu.c.
 * Affiliates: the catalogue records above.
 */
typedef struct {
    uint32_t size;      /* 16-bit frames; 0 = silence */
    uint32_t offset;    /* absolute flash address (or the silence buffer) */
    uint8_t  gain_code;
} SampleInfo;

typedef struct {
    uint32_t offset;
    uint16_t frame_len;
    uint8_t  frame_count;
    uint8_t  bits;
} WavetableInfo;

/*
 * Catalogue states (S085 C1.1).
 *
 * What:       EMPTY (erased, never installed, or an interrupted install),
 *             V2 (a committed catalogue), STALE (a v1 install from an older
 *             firmware is present).
 * Why:        the boot notice (C3.9) words EMPTY and STALE differently; the
 *             stock waves exist only in V2 (plan §4.3.4).
 * Accessors:  sampleMemory_catalogueState(), main.c.
 */
#define SAMPLE_CATALOGUE_EMPTY  0u
#define SAMPLE_CATALOGUE_V2     1u
#define SAMPLE_CATALOGUE_STALE  2u

/*
 * Catalogue read API (S085 C1.1).
 *
 * What:       init runs the flash probe and one refresh; refresh validates the
 *             header and counts the leading valid entries; the getters return
 *             counts, state, one resolved item, or a name.
 * Why:        Q8 of the S084 plan and S085 §4.3.3: the catalogue is read in
 *             place; the generation stamp is the only invalidation the DSP
 *             binds need.
 * Inputs:     an index / output pointers.  Outputs: values, counts, state.
 * Accessors:  main.c (boot), menu.c, InstrumentManager.c,
 *             InstrumentCellFormat.c, OscillatorSample.c,
 *             OscillatorWavetable.c, StockWaves.c, OscillatorPwm.c,
 *             Oscillator.c, SampleInstall.c.
 * Affiliates: the records above, SAMPLE_FLASH_PTR(), sampleFlash.c.
 */
void       sampleMemory_init(void);
void       sampleMemory_refresh(void);
uint8_t    sampleMemory_catalogueState(void);
uint32_t   sampleMemory_getGeneration(void);
uint8_t    sampleMemory_getNumSamples(void);
SampleInfo sampleMemory_getSampleInfo(uint8_t index);
void       sampleMemory_getDisplayName(uint8_t index,
                                       char name[SAMPLE_DISPLAY_NAME_LEN + 1u]);
uint8_t    sampleMemory_getNumWavetables(void);
uint8_t    sampleMemory_getWavetableInfo(uint8_t index, WavetableInfo *out);
void       sampleMemory_getWavetableName(uint8_t index,
                                         char name[SAMPLE_DISPLAY_NAME_LEN + 1u]);
uint8_t    sampleMemory_stockValid(void);

/*
 * Peak-normalisation gain from the catalogue byte (S085 C1.1).
 *
 * What:       2^(code/24): a 24-entry mantissa table plus an exponent add on
 *             the float bit pattern; about 8 instructions, no libm.
 * Why:        installed samples play at matched levels; applied once per
 *             block per sample oscillator (the per-sample loop already
 *             multiplies by the gain).
 * Inputs:     code 0..255.  Outputs: gain 1.0 .. ~1,600.
 * Accessors:  oscSample_render(), oscSample_renderFm().
 * Affiliates: SampleInstall.c sampleInstall_gainCode() (the inverse).
 */
extern const float sampleMemory_gainMantissa[SAMPLE_GAIN_STEPS_PER_OCTAVE];
static inline float sampleMemory_gainFromCode(uint8_t code)
{
    union { float f; uint32_t u; } g;

    g.f = sampleMemory_gainMantissa[code % SAMPLE_GAIN_STEPS_PER_OCTAVE];
    g.u += (uint32_t)(code / SAMPLE_GAIN_STEPS_PER_OCTAVE) << 23;
    return g.f;
}

#endif /* SAMPLEMEMORY_H_ */
```

### C1.2 `Core/SampleRom/SampleMemory.c` — catalogue reader

- **Where:** lines 21–325 (everything after the file header).
- **Action:** **Modify (rewrite).**
  - Remove: the caches, `install_*` statics, `sampleMemory_infoValid()`,
    `sampleMemory_setNumSamples()`, `sampleMemory_isLooped()`, every
    `sampleMemory_install*()` function and `sampleMemory_loadSamples()`.
  - Keep the silence buffer.

```c
#include "SampleMemory.h"
#include "sampleFlash.h"
#include <string.h>

/* Guarded silence returned for every out-of-range sample index. */
static const int16_t stub_silence_buffer[256] = { 0 };

/*
 * Catalogue state (S085 C1.2).
 *
 * What:       installed counts, catalogue state, stock-block flag and the
 *             generation stamp. No item data is cached; the getters read the
 *             flash records directly.
 * Why:        S085 §4.3.3; the v1 RAM caches (5,040 B) are released and not
 *             reused (RAM policy). The generation stamp is the only
 *             invalidation the DSP binds need.
 * Writers:    sampleMemory_refresh() only.
 * Readers:    every getter, the DSP binds and the boot notice.
 */
static uint8_t  sample_count = 0u;
static uint8_t  sample_wavetable_count = 0u;
static uint8_t  sample_catalogue_state = SAMPLE_CATALOGUE_EMPTY;
static uint8_t  sample_stock_valid = 0u;
static uint32_t sample_generation = 1u;

/*
 * 2^(i/24) mantissas for the gain code (S085 C1.2).
 *
 * What:       the fractional octave of the catalogue gain; the integer octave
 *             is added to the float exponent by sampleMemory_gainFromCode().
 * Why:        block-rate gain without libm; 96 B of flash.
 * Accessors:  sampleMemory_gainFromCode().
 */
const float sampleMemory_gainMantissa[SAMPLE_GAIN_STEPS_PER_OCTAVE] = {
    1.000000f, 1.029302f, 1.059463f, 1.090508f, 1.122462f, 1.155353f,
    1.189207f, 1.224054f, 1.259921f, 1.296840f, 1.334840f, 1.373954f,
    1.414214f, 1.455653f, 1.498307f, 1.542211f, 1.587401f, 1.633915f,
    1.681793f, 1.731073f, 1.781797f, 1.834008f, 1.887749f, 1.943064f,
};

static SampleInfo sampleMemory_silenceInfo(void)
{
    SampleInfo info;

    memset(&info, 0, sizeof(info));
    info.offset = (uint32_t)(uintptr_t)stub_silence_buffer;
    return info;
}

static const sample_catalogue_sample_t *sampleMemory_sampleEntry(uint8_t i)
{
    return (const sample_catalogue_sample_t *)SAMPLE_FLASH_PTR(
        SAMPLE_CATALOGUE_SAMPLES_ADDRESS +
        (uint32_t)i * sizeof(sample_catalogue_sample_t));
}

static const sample_catalogue_wavetable_t *sampleMemory_tableEntry(uint8_t i)
{
    return (const sample_catalogue_wavetable_t *)SAMPLE_FLASH_PTR(
        SAMPLE_CATALOGUE_WAVETABLES_ADDRESS +
        (uint32_t)i * sizeof(sample_catalogue_wavetable_t));
}

/*
 * Catalogue entry bounds (S085 C1.2).
 *
 * What:       a sample entry is valid when its 4-aligned offset lies in the
 *             item region and its frames plus the 2-frame interpolation pad
 *             end before the catalogue. A wavetable entry also needs 1..128
 *             frames of 8 or 16 bits, frame_len >= 2, and the whole table
 *             inside the item region.
 * Why:        the reader stops at the first invalid entry, so a torn install
 *             can never hand the DSP a pointer outside the item region.
 * Inputs:     one flash record.  Outputs: 0/1.
 * Accessors:  sampleMemory_refresh().
 * Affiliates: SampleInstall.c (writes entries only inside these bounds).
 */
static uint8_t sampleMemory_entryValid(const sample_catalogue_sample_t *e)
{
    uint64_t end;

    if (e->offset < SAMPLE_DATA_START_ADDRESS ||
        e->offset >= SAMPLE_CATALOGUE_START_ADDRESS ||
        (e->offset & 3u) != 0u || e->frames == 0u)
        return 0u;
    end = (uint64_t)e->offset + ((uint64_t)e->frames + 2u) * 2u;
    return (uint8_t)(end <= SAMPLE_CATALOGUE_START_ADDRESS);
}

static uint8_t sampleMemory_tableValid(const sample_catalogue_wavetable_t *e)
{
    uint64_t end;

    if (e->offset < SAMPLE_DATA_START_ADDRESS ||
        e->offset >= SAMPLE_CATALOGUE_START_ADDRESS ||
        (e->offset & 3u) != 0u || e->frame_len < 2u ||
        e->frame_count == 0u || e->frame_count > WAVETABLE_MAX_FRAMES ||
        (e->bits != 8u && e->bits != 16u))
        return 0u;
    end = (uint64_t)e->offset + (uint64_t)e->frame_len *
          (uint64_t)e->frame_count * (uint64_t)(e->bits / 8u);
    return (uint8_t)(end <= SAMPLE_CATALOGUE_START_ADDRESS);
}

/*
 * Re-read the catalogue (S085 C1.2).
 *
 * What:       validates the header (magic, version, entry size) and counts
 *             the leading valid sample and wavetable entries, records the
 *             stock flag, and bumps the generation (0 is skipped) so every
 *             DSP bind refreshes. Without a v2 header the state is STALE when
 *             the first word of sector 7 holds a v1 count (1..120), else
 *             EMPTY. An interrupted v2 install can read STALE by chance (its
 *             first word is stock data); that only changes the boot wording.
 * Why:        no RAM index; v1 installs are not read (no migration, Q8).
 * Inputs:     sample flash.  Outputs: counts, state, stock flag, generation.
 * Accessors:  sampleMemory_init() (boot), sampleInstall_commit().
 * Affiliates: oscSample_bind(), oscWavetable_bind(), osc_setFreq() (pwm key),
 *             main.c boot notice (C3.9).
 */
void sampleMemory_refresh(void)
{
    const sample_catalogue_header_t *header =
        (const sample_catalogue_header_t *)
            SAMPLE_FLASH_PTR(SAMPLE_CATALOGUE_START_ADDRESS);
    uint8_t limit;
    uint8_t i;

    sample_generation++;
    if (sample_generation == 0u)
        sample_generation = 1u;
    sample_count = 0u;
    sample_wavetable_count = 0u;
    sample_stock_valid = 0u;
    sample_catalogue_state = SAMPLE_CATALOGUE_EMPTY;

    if (header->magic == SAMPLE_CATALOGUE_MAGIC &&
        header->version == SAMPLE_CATALOGUE_VERSION &&
        header->entry_bytes == (uint16_t)sizeof(sample_catalogue_sample_t)) {
        sample_catalogue_state = SAMPLE_CATALOGUE_V2;
        sample_stock_valid = (uint8_t)(header->stock_valid == 1u);
        limit = (header->sample_count > SAMPLE_MAX_COUNT)
            ? (uint8_t)SAMPLE_MAX_COUNT : header->sample_count;
        for (i = 0u; i < limit; i++) {
            if (!sampleMemory_entryValid(sampleMemory_sampleEntry(i)))
                break;
            sample_count++;
        }
        limit = (header->wavetable_count > WAVETABLE_MAX_COUNT)
            ? (uint8_t)WAVETABLE_MAX_COUNT : header->wavetable_count;
        for (i = 0u; i < limit; i++) {
            if (!sampleMemory_tableValid(sampleMemory_tableEntry(i)))
                break;
            sample_wavetable_count++;
        }
        return;
    }
    {
        const uint32_t first =
            *(const uint32_t *)SAMPLE_FLASH_PTR(SAMPLE_ROM_START_ADDRESS);

        if (first >= 1u && first <= 120u)
            sample_catalogue_state = SAMPLE_CATALOGUE_STALE;
    }
}

/*
 * Boot init (S085 C1.2).
 *
 * What:       sampleFlash_init() (the floor probe) then one refresh.
 * Why:        main.c reads the state right after this for the boot notice.
 * Accessors:  main.c, before dsp_init().
 */
void sampleMemory_init(void)
{
    sampleFlash_init();
    sampleMemory_refresh();
}

uint8_t  sampleMemory_catalogueState(void) { return sample_catalogue_state; }
uint8_t  sampleMemory_stockValid(void)     { return sample_stock_valid; }
uint8_t  sampleMemory_getNumSamples(void)  { return sample_count; }
uint8_t  sampleMemory_getNumWavetables(void) { return sample_wavetable_count; }
uint32_t sampleMemory_getGeneration(void)  { return sample_generation; }

/*
 * One installed sample (S085 C1.2).
 *
 * What:       frames, offset and gain code of entry `index`; the silence
 *             buffer (size 0) when out of range.
 * Accessors:  oscSample_bind().
 */
SampleInfo sampleMemory_getSampleInfo(uint8_t index)
{
    const sample_catalogue_sample_t *e;
    SampleInfo info;

    if (index >= sample_count)
        return sampleMemory_silenceInfo();
    e = sampleMemory_sampleEntry(index);
    info.size = e->frames;
    info.offset = e->offset;
    info.gain_code = e->gain_code;
    return info;
}

/*
 * Eight-character names (S085 C1.2).
 *
 * What:       copies the entry's name and terminates it; spaces when out of
 *             range.
 * Accessors:  menu.c single-parameter view (CELL_FMT_SAMPLE /
 *             CELL_FMT_WAVETABLE).
 */
void sampleMemory_getDisplayName(uint8_t index,
                                 char name[SAMPLE_DISPLAY_NAME_LEN + 1u])
{
    if (!name)
        return;
    if (index >= sample_count)
        memset(name, ' ', SAMPLE_DISPLAY_NAME_LEN);
    else
        memcpy(name, sampleMemory_sampleEntry(index)->name,
               SAMPLE_DISPLAY_NAME_LEN);
    name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
}

void sampleMemory_getWavetableName(uint8_t index,
                                   char name[SAMPLE_DISPLAY_NAME_LEN + 1u])
{
    if (!name)
        return;
    if (index >= sample_wavetable_count)
        memset(name, ' ', SAMPLE_DISPLAY_NAME_LEN);
    else
        memcpy(name, sampleMemory_tableEntry(index)->name,
               SAMPLE_DISPLAY_NAME_LEN);
    name[SAMPLE_DISPLAY_NAME_LEN] = '\0';
}

/*
 * One installed wavetable (S085 C1.2).
 *
 * What:       fills *out for entry `index`; returns 0 when out of range.
 * Accessors:  oscWavetable_bind(), InstrumentCellFormat.c (frame count).
 */
uint8_t sampleMemory_getWavetableInfo(uint8_t index, WavetableInfo *out)
{
    const sample_catalogue_wavetable_t *e;

    if (!out || index >= sample_wavetable_count)
        return 0u;
    e = sampleMemory_tableEntry(index);
    out->offset = e->offset;
    out->frame_len = e->frame_len;
    out->frame_count = e->frame_count;
    out->bits = e->bits;
    return 1u;
}
```

### C1.3 `Core/SampleRom/sampleFlash.c` — comment only

- **Where:** line 141, inside the comment above
  `_Static_assert(SAMPLE_FIRST_SECTOR …)`.
- **Action:** **Add** two comment lines: ` * S085 C1.3: the catalogue v2
  regions (SampleMemory.h) all lie inside sectors 7..11; the guards are
  unchanged.`

---
## 3. WP2 — Installer core `SampleInstall.h/.c` (new)

The installer core converts and programs items. It knows nothing about the
card; `filesystem.c` (WP3) walks the folders and hands it bytes.

### C2.1 `Core/SampleRom/SampleInstall.h` (new file)

```c
/*
 * SampleInstall.h — catalogue v2 installer core (S085 C2.1).
 *
 * What:       accepts WAV formats, converts and programs samples and
 *             wavetable frames into the item region, programs one catalogue
 *             entry per finished item, writes the stock block, and commits
 *             the header (magic last).
 * Why:        plan §5 item 8: Load:[Samples] installs samples/ and
 *             wavetables/ into catalogue v2; the firmware never expands data
 *             (8-bit stays 8-bit, 24-bit becomes 16-bit, stereo becomes mono).
 * Inputs:     formats, names and data chunks from filesystem.c.
 * Outputs:    sample flash; counts for the summary.
 * Accessors:  filesystem.c installer (C3.6) only; audio is suspended.
 * Affiliates: SampleMemory.h (layout and records), sampleFlash.c (erase and
 *             program), StockWaveGen.c (stock block), test_catalogue.c.
 */
#ifndef SAMPLEINSTALL_H_
#define SAMPLEINSTALL_H_

#include <stdint.h>
#include "SampleMemory.h"

typedef enum {
    SAMPLE_INSTALL_KIND_SAMPLE = 0,
    SAMPLE_INSTALL_KIND_FRAME
} sample_install_kind_t;

/*
 * One accepted WAV format (S085 C2.1).
 *
 * What:       the source shape and the stored width after conversion.
 * Why:        samples and frames share one parser and writer; only the
 *             acceptance rule and the conversion differ.
 * Inputs:     filled by sampleInstall_acceptFormat().
 * Outputs:    read by the begin/write functions.
 * Accessors:  filesystem.c, SampleInstall.c.
 */
typedef struct {
    uint16_t channels;      /* 1 or 2 */
    uint16_t bits;          /* 8, 16 or 24 */
    uint32_t rate;          /* accepted, not used for pitch */
    uint16_t block_align;   /* bytes per source frame (<= 6) */
    uint8_t  stored_bits;   /* 8 or 16 after conversion */
    uint8_t  reserved;
} sample_install_fmt_t;

/*
 * Accept one WAV fmt chunk (S085 C2.1).
 *
 * What:       parses 16..40 bytes; accepts tag 1 (PCM) and tag 0xFFFE
 *             (extensible) with the PCM sub-format (bytes 24..25 == 1, 0).
 *             SAMPLE: mono, 16-bit, 44,100 Hz only (tools/sampleoptimize.py
 *             converts anything else on the host). FRAME: mono or stereo,
 *             8/16/24-bit, any rate. stored_bits = 8 for 8-bit sources,
 *             else 16.
 * Why:        the accepted forms of plan §5 item 8.
 * Inputs:     fmt bytes, their count, kind.  Outputs: 1 and *out, or 0.
 * Accessors:  filesystem_parseWav().
 * Affiliates: sampleInstall_storedBytes().
 */
uint8_t  sampleInstall_acceptFormat(const uint8_t *fmt, uint32_t fmt_bytes,
                                    sample_install_kind_t kind,
                                    sample_install_fmt_t *out);

/*
 * Stored size of a data chunk (S085 C2.1).
 *
 * What:       (data_bytes / block_align) x stored_bits / 8.
 * Why:        fit checks run before any byte is programmed.
 * Accessors:  sampleInstall_beginSample(), filesystem_installWavetable().
 */
uint32_t sampleInstall_storedBytes(const sample_install_fmt_t *fmt,
                                   uint32_t data_bytes);

/*
 * Install session (S085 C2.1).
 *
 * What:       begin erases sectors 7..11 and resets the state;
 *             writeStockBlock generates the five stock waves; the
 *             sample/wavetable calls program one item and its entry; commit
 *             programs the header and its magic last, then refreshes the
 *             reader.
 * Why:        every Load:[Samples] is a full install (no append, /loops
 *             retired).
 * Inputs:     names, formats, data.
 * Outputs:    0 ok; -1 state error; -2 count limit; -3 does not fit / bad
 *             geometry; -4 incomplete item (finish calls); >= 1 a sampleFlash
 *             status.
 * Accessors:  filesystem.c installer.
 * Affiliates: sampleFlash.c, stockWaveGen_programAll(), sampleMemory_refresh().
 */
int      sampleInstall_begin(void);
int      sampleInstall_writeStockBlock(void);
int      sampleInstall_beginSample(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                   const sample_install_fmt_t *fmt,
                                   uint32_t data_bytes);
int      sampleInstall_writeSampleData(const uint8_t *data, uint32_t bytes);
int      sampleInstall_finishSample(void);
int      sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                      const sample_install_fmt_t *fmt,
                                      uint16_t frame_len, uint8_t frame_count);
int      sampleInstall_setFrameFormat(const sample_install_fmt_t *fmt);
int      sampleInstall_writeFrameData(const uint8_t *data, uint32_t bytes);
int      sampleInstall_finishWavetable(void);
void     sampleInstall_abortItem(void);
int      sampleInstall_commit(void);

/*
 * Install progress (S085 C2.1).
 *
 * What:       bytes free before the catalogue; committed entry counts.
 * Accessors:  filesystem.c installer, menu.c summary.
 */
uint32_t sampleInstall_bytesFree(void);
uint8_t  sampleInstall_sampleCount(void);
uint8_t  sampleInstall_wavetableCount(void);

#endif /* SAMPLEINSTALL_H_ */
```

### C2.2 `Core/SampleRom/SampleInstall.c` (new file)

- **Compiled with:** `SRCS` (`-O2`, control code).
- **Contents:** the state, the helpers and every function declared in C2.1.

```c
#include "SampleInstall.h"
#include "SampleMemory.h"
#include "sampleFlash.h"
#include "StockWaveGen.h"
#include <math.h>
#include <string.h>

/*
 * Install state (S085 C2.2).
 *
 * What:       the write pointer, the open item (start, expected stored bytes,
 *             name, source format, wavetable geometry), the word carry
 *             (programming is per 32-bit word), the source-frame carry (a
 *             frame can split across two 512 B reads), the open sample's peak
 *             and the committed counts.
 * Why:        approved install scratch (S085 §0.5, 44 B); entries are
 *             programmed only at finish, so the name and geometry are kept.
 * Writers:    the functions below.  Readers: the same, and the counters.
 * Affiliates: sampleFlash_writeBytes(), StockWaveGen.c, SampleMemory.c.
 */
static struct {
    uint32_t write_addr;       /* next data word (4-aligned) */
    uint32_t item_start;       /* first byte of the open item */
    uint32_t item_bytes;       /* stored bytes expected for the item */
    char     item_name[SAMPLE_DISPLAY_NAME_LEN];
    uint8_t  out_carry[4];     /* stored bytes waiting for a full word */
    uint8_t  in_carry[6];      /* one split source frame */
    uint8_t  out_carry_n;
    uint8_t  in_carry_n;
    uint16_t peak;             /* max |s| of the open sample */
    uint16_t frame_len;        /* open wavetable: points per frame */
    uint8_t  frame_count;      /* open wavetable: frames */
    uint8_t  flags;            /* open kind (bits 0..1) | INSTALL_FLAG_STOCK */
    uint8_t  samples;
    uint8_t  wavetables;
    uint8_t  fmt_channels;
    uint8_t  fmt_bits;
    uint8_t  fmt_block;
    uint8_t  fmt_stored;
} sample_install;

_Static_assert(sizeof(sample_install) == 44u,
               "SampleInstall state is the approved 44 B install scratch");

#define INSTALL_OPEN_MASK        0x03u
#define INSTALL_OPEN_NONE        0u
#define INSTALL_OPEN_SAMPLE      1u
#define INSTALL_OPEN_WAVETABLE   2u
#define INSTALL_FLAG_STOCK       0x80u
#define INSTALL_SAMPLE_PAD_BYTES 4u   /* two zero frames after each sample */

static uint8_t sampleInstall_openKind(void)
{
    return (uint8_t)(sample_install.flags & INSTALL_OPEN_MASK);
}

static void sampleInstall_setOpenKind(uint8_t kind)
{
    sample_install.flags = (uint8_t)((sample_install.flags &
        (uint8_t)~INSTALL_OPEN_MASK) | (kind & INSTALL_OPEN_MASK));
}

/*
 * Stored bytes appended to the open item (S085 C2.2).
 *
 * What:       programmed bytes since item_start plus the bytes in the word
 *             carry.
 * Why:        the writers stop at item_bytes; the finish calls refuse a short
 *             item (-4) so no entry describes missing data.
 */
static uint32_t sampleInstall_itemWritten(void)
{
    return (sample_install.write_addr - sample_install.item_start) +
           (uint32_t)sample_install.out_carry_n;
}

/*
 * Append one stored byte through the word carry (S085 C2.2).
 *
 * What:       collects four bytes, then programs one word and advances
 *             write_addr.
 * Outputs:    0 or a sampleFlash status.
 */
static int sampleInstall_putByte(uint8_t b)
{
    sample_install.out_carry[sample_install.out_carry_n++] = b;
    if (sample_install.out_carry_n == 4u) {
        int rc = sampleFlash_writeBytes(&sample_install.write_addr,
                                        sample_install.out_carry, 4u);
        if (rc != 0)
            return rc;
        sample_install.out_carry_n = 0u;
    }
    return 0;
}

static int sampleInstall_putBytes(const uint8_t *p, uint32_t n)
{
    while (n-- != 0u) {
        int rc = sampleInstall_putByte(*p++);
        if (rc != 0)
            return rc;
    }
    return 0;
}

/* Pad the word carry with zeros so the next item starts 4-aligned. */
static int sampleInstall_flushCarry(void)
{
    while (sample_install.out_carry_n != 0u) {
        int rc = sampleInstall_putByte(0u);
        if (rc != 0)
            return rc;
    }
    return 0;
}

/*
 * Peak-normalisation code (S085 C2.2).
 *
 * What:       floor(24 x log2(32767 / peak)), 0..255; peak 0 gives 0.
 * Why:        the floor keeps gain x peak <= 32767, so playback never clips
 *             from the normalisation.
 * Accessors:  sampleInstall_finishSample().
 * Affiliates: sampleMemory_gainFromCode() (the inverse).
 */
static uint8_t sampleInstall_gainCode(uint16_t peak)
{
    float code;

    if (peak == 0u)
        return 0u;
    code = floorf((float)SAMPLE_GAIN_STEPS_PER_OCTAVE *
                  log2f(32767.0f / (float)peak));
    if (code < 0.0f)
        return 0u;
    if (code > 255.0f)
        return 255u;
    return (uint8_t)code;
}

static void sampleInstall_storeFormat(const sample_install_fmt_t *fmt)
{
    sample_install.fmt_channels = (uint8_t)fmt->channels;
    sample_install.fmt_bits = (uint8_t)fmt->bits;
    sample_install.fmt_block = (uint8_t)fmt->block_align;
    sample_install.fmt_stored = fmt->stored_bits;
}

uint8_t sampleInstall_acceptFormat(const uint8_t *fmt, uint32_t fmt_bytes,
                                   sample_install_kind_t kind,
                                   sample_install_fmt_t *out)
{
    uint16_t tag;
    uint16_t channels;
    uint16_t bits;
    uint32_t rate;
    uint8_t pcm;

    if (!fmt || !out || fmt_bytes < 16u)
        return 0u;
    tag = (uint16_t)(fmt[0] | ((uint16_t)fmt[1] << 8));
    channels = (uint16_t)(fmt[2] | ((uint16_t)fmt[3] << 8));
    rate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
           ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
    bits = (uint16_t)(fmt[14] | ((uint16_t)fmt[15] << 8));
    pcm = (uint8_t)(tag == 1u);
    if (tag == 0xfffeu) {
        if (fmt_bytes < 40u)
            return 0u;
        pcm = (uint8_t)(fmt[24] == 1u && fmt[25] == 0u);
    }
    if (!pcm)
        return 0u;
    if (kind == SAMPLE_INSTALL_KIND_SAMPLE) {
        if (channels != 1u || bits != 16u || rate != 44100u)
            return 0u;
    } else if (channels < 1u || channels > 2u ||
               (bits != 8u && bits != 16u && bits != 24u)) {
        return 0u;
    }
    memset(out, 0, sizeof(*out));
    out->channels = channels;
    out->bits = bits;
    out->rate = rate;
    out->block_align = (uint16_t)(channels * (bits / 8u));
    out->stored_bits = (uint8_t)((bits == 8u) ? 8u : 16u);
    return 1u;
}

uint32_t sampleInstall_storedBytes(const sample_install_fmt_t *fmt,
                                   uint32_t data_bytes)
{
    if (!fmt || fmt->block_align == 0u)
        return 0u;
    return (data_bytes / fmt->block_align) * (uint32_t)(fmt->stored_bits / 8u);
}

uint32_t sampleInstall_bytesFree(void)
{
    return (sample_install.write_addr >= SAMPLE_CATALOGUE_START_ADDRESS)
        ? 0u : (SAMPLE_CATALOGUE_START_ADDRESS - sample_install.write_addr);
}

uint8_t sampleInstall_sampleCount(void)    { return sample_install.samples; }
uint8_t sampleInstall_wavetableCount(void) { return sample_install.wavetables; }

/*
 * Begin a full install (S085 C2.2).
 *
 * What:       clears the state, sets write_addr to SAMPLE_DATA_START_ADDRESS
 *             and erases sectors 7..11 (stock block, items and catalogue
 *             together; the reader sees EMPTY until the commit).
 * Outputs:    0 or a sampleFlash status.
 * Accessors:  filesystem_installSamplesBlocking().
 */
int sampleInstall_begin(void)
{
    memset(&sample_install, 0, sizeof(sample_install));
    sample_install.write_addr = SAMPLE_DATA_START_ADDRESS;
    sampleFlash_init();
    return sampleFlash_eraseAllSamples();
}

/*
 * Generate the stock block (S085 C2.2).
 *
 * What:       stockWaveGen_programAll() at SAMPLE_STOCK_START_ADDRESS; on
 *             success marks the block so the commit advertises it.
 * Why:        plan §4.3.4: every install regenerates the stock waves, so the
 *             block always matches this firmware's recipes.
 * Accessors:  filesystem_installSamplesBlocking().
 */
int sampleInstall_writeStockBlock(void)
{
    int rc = stockWaveGen_programAll(SAMPLE_STOCK_START_ADDRESS);

    if (rc == 0)
        sample_install.flags |= INSTALL_FLAG_STOCK;
    return rc;
}

/*
 * Open one sample item (S085 C2.2).
 *
 * What:       checks the state, the 128-sample limit (Q7) and the fit
 *             (data + 4 B interpolation pad, rounded to a word) and records
 *             the item.
 * Outputs:    0, -1 (state), -2 (limit), -3 (no fit / bad form).
 */
int sampleInstall_beginSample(const char name[SAMPLE_DISPLAY_NAME_LEN],
                              const sample_install_fmt_t *fmt,
                              uint32_t data_bytes)
{
    uint32_t stored;
    uint32_t needed;

    if (!fmt || !name || sampleInstall_openKind() != INSTALL_OPEN_NONE)
        return -1;
    if (sample_install.samples >= SAMPLE_MAX_COUNT)
        return -2;
    if (fmt->block_align == 0u || fmt->stored_bits != 16u)
        return -3;
    stored = sampleInstall_storedBytes(fmt, data_bytes);
    needed = (stored + INSTALL_SAMPLE_PAD_BYTES + 3u) & ~3u;
    if (stored == 0u || needed > sampleInstall_bytesFree())
        return -3;
    sample_install.item_start = sample_install.write_addr;
    sample_install.item_bytes = stored;
    memcpy(sample_install.item_name, name, SAMPLE_DISPLAY_NAME_LEN);
    sampleInstall_storeFormat(fmt);
    sample_install.peak = 0u;
    sample_install.out_carry_n = 0u;
    sample_install.in_carry_n = 0u;
    sampleInstall_setOpenKind(INSTALL_OPEN_SAMPLE);
    return 0;
}

/*
 * Program 16-bit sample data (S085 C2.2).
 *
 * What:       pairs bytes into int16 frames (carry across calls), tracks the
 *             peak magnitude and appends up to item_bytes.
 * Outputs:    0, -1, or a sampleFlash status.
 * Accessors:  filesystem_installCopy().
 */
int sampleInstall_writeSampleData(const uint8_t *data, uint32_t bytes)
{
    if (!data || sampleInstall_openKind() != INSTALL_OPEN_SAMPLE)
        return -1;
    while (bytes-- != 0u) {
        sample_install.in_carry[sample_install.in_carry_n++] = *data++;
        if (sample_install.in_carry_n < 2u)
            continue;
        sample_install.in_carry_n = 0u;
        if (sampleInstall_itemWritten() >= sample_install.item_bytes)
            continue;
        {
            const uint8_t lo = sample_install.in_carry[0];
            const uint8_t hi = sample_install.in_carry[1];
            const int16_t s = (int16_t)((uint16_t)lo | ((uint16_t)hi << 8));
            const uint16_t mag = (uint16_t)((s < 0) ? -(int32_t)s : s);
            int rc;

            if (mag > sample_install.peak)
                sample_install.peak = mag;
            rc = sampleInstall_putByte(lo);
            if (rc == 0)
                rc = sampleInstall_putByte(hi);
            if (rc != 0)
                return rc;
        }
    }
    return 0;
}

/*
 * Finish one sample and program its entry (S085 C2.2).
 *
 * What:       refuses a short item (-4, aborted, no entry); appends the
 *             4-byte zero pad (the last frame's interpolation reads inside
 *             owned flash), flushes the carry, programs the 20 B entry with
 *             the gain code.
 * Outputs:    0, -1, -4, or a sampleFlash status.
 */
int sampleInstall_finishSample(void)
{
    static const uint8_t zero_pad[INSTALL_SAMPLE_PAD_BYTES] = { 0, 0, 0, 0 };
    sample_catalogue_sample_t entry;
    uint32_t addr;
    int rc;

    if (sampleInstall_openKind() != INSTALL_OPEN_SAMPLE)
        return -1;
    if (sampleInstall_itemWritten() != sample_install.item_bytes) {
        sampleInstall_abortItem();
        return -4;
    }
    rc = sampleInstall_putBytes(zero_pad, sizeof(zero_pad));
    if (rc == 0)
        rc = sampleInstall_flushCarry();
    if (rc != 0) {
        sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
        return rc;
    }
    memset(&entry, 0xff, sizeof(entry));
    entry.offset = sample_install.item_start;
    entry.frames = sample_install.item_bytes / 2u;
    memcpy(entry.name, sample_install.item_name, SAMPLE_DISPLAY_NAME_LEN);
    entry.gain_code = sampleInstall_gainCode(sample_install.peak);
    addr = SAMPLE_CATALOGUE_SAMPLES_ADDRESS +
           (uint32_t)sample_install.samples * sizeof(entry);
    rc = sampleFlash_writeBytes(&addr, (const uint8_t *)&entry,
                                (uint32_t)sizeof(entry));
    sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
    if (rc != 0)
        return rc;
    sample_install.samples++;
    return 0;
}

/*
 * Open one wavetable item (S085 C2.2).
 *
 * What:       checks the 128-table limit, the geometry (frame_len >= 2,
 *             1..128 frames, 8 or 16 stored bits) and the fit, then records
 *             the item. The first frame's source format is stored;
 *             setFrameFormat() replaces it per frame file.
 * Outputs:    0, -1, -2, -3.
 */
int sampleInstall_beginWavetable(const char name[SAMPLE_DISPLAY_NAME_LEN],
                                 const sample_install_fmt_t *fmt,
                                 uint16_t frame_len, uint8_t frame_count)
{
    uint32_t stored;

    if (!fmt || !name || sampleInstall_openKind() != INSTALL_OPEN_NONE)
        return -1;
    if (sample_install.wavetables >= WAVETABLE_MAX_COUNT)
        return -2;
    if (frame_len < 2u || frame_count == 0u ||
        frame_count > WAVETABLE_MAX_FRAMES ||
        (fmt->stored_bits != 8u && fmt->stored_bits != 16u))
        return -3;
    stored = (uint32_t)frame_len * frame_count * (fmt->stored_bits / 8u);
    if (((stored + 3u) & ~3u) > sampleInstall_bytesFree())
        return -3;
    sample_install.item_start = sample_install.write_addr;
    sample_install.item_bytes = stored;
    memcpy(sample_install.item_name, name, SAMPLE_DISPLAY_NAME_LEN);
    sampleInstall_storeFormat(fmt);
    sample_install.frame_len = frame_len;
    sample_install.frame_count = frame_count;
    sample_install.out_carry_n = 0u;
    sample_install.in_carry_n = 0u;
    sampleInstall_setOpenKind(INSTALL_OPEN_WAVETABLE);
    return 0;
}

/*
 * Per-frame source format (S085 C2.2).
 *
 * What:       switches the open table's source decoding (channels, bits,
 *             block) to the next frame file's own format, at a frame boundary
 *             only, and refuses a different stored depth.
 * Why:        a table may mix mono/stereo and 16/24-bit frame files; only
 *             the stored depth and length must agree (S084 P2 F4).
 * Outputs:    0 or -1.
 * Accessors:  filesystem_installWavetable() before each frame copy.
 */
int sampleInstall_setFrameFormat(const sample_install_fmt_t *fmt)
{
    if (!fmt || sampleInstall_openKind() != INSTALL_OPEN_WAVETABLE ||
        sample_install.in_carry_n != 0u ||
        fmt->stored_bits != sample_install.fmt_stored ||
        fmt->block_align == 0u || fmt->block_align > 6u)
        return -1;
    sampleInstall_storeFormat(fmt);
    return 0;
}

/*
 * Convert and program frame bytes (S085 C2.2).
 *
 * What:       assembles source frames through in_carry and stores each as
 *             8-bit signed (unsigned WAV 8-bit flipped) or 16-bit; stereo is
 *             averaged to mono; 24-bit is rounded to 16-bit and saturated.
 *             Stops at item_bytes.
 * Why:        never expand (8-bit stays 8-bit); every conversion keeps or
 *             reduces the size.
 * Outputs:    0, -1, or a sampleFlash status.
 * Accessors:  filesystem_installCopy().
 */
int sampleInstall_writeFrameData(const uint8_t *data, uint32_t bytes)
{
    const uint8_t *f = sample_install.in_carry;
    const uint8_t block = sample_install.fmt_block;

    if (!data || sampleInstall_openKind() != INSTALL_OPEN_WAVETABLE ||
        block == 0u || block > 6u)
        return -1;
    while (bytes-- != 0u) {
        sample_install.in_carry[sample_install.in_carry_n++] = *data++;
        if (sample_install.in_carry_n < block)
            continue;
        sample_install.in_carry_n = 0u;
        if (sampleInstall_itemWritten() >= sample_install.item_bytes)
            continue;
        {
            const uint8_t stereo = (uint8_t)(sample_install.fmt_channels == 2u);
            int32_t a;
            int32_t b;
            int32_t v;
            int rc;

            if (sample_install.fmt_bits == 8u) {
                a = (int32_t)(int8_t)(f[0] ^ 0x80u);
                b = stereo ? (int32_t)(int8_t)(f[1] ^ 0x80u) : a;
                v = (a + b) >> 1;
                rc = sampleInstall_putByte((uint8_t)(int8_t)v);
            } else {
                if (sample_install.fmt_bits == 16u) {
                    a = (int16_t)((uint16_t)f[0] | ((uint16_t)f[1] << 8));
                    b = stereo ? (int16_t)((uint16_t)f[2] |
                                           ((uint16_t)f[3] << 8)) : a;
                } else {
                    int32_t l = (int32_t)((uint32_t)f[0] |
                        ((uint32_t)f[1] << 8) | ((uint32_t)f[2] << 16));
                    int32_t r;

                    if (l & 0x00800000)
                        l |= (int32_t)0xff000000u;
                    r = l;
                    if (stereo) {
                        r = (int32_t)((uint32_t)f[3] |
                            ((uint32_t)f[4] << 8) | ((uint32_t)f[5] << 16));
                        if (r & 0x00800000)
                            r |= (int32_t)0xff000000u;
                    }
                    a = (l + 128) >> 8;
                    b = (r + 128) >> 8;
                    a = (a > 32767) ? 32767 : ((a < -32768) ? -32768 : a);
                    b = (b > 32767) ? 32767 : ((b < -32768) ? -32768 : b);
                }
                v = (a + b) >> 1;
                rc = sampleInstall_putByte((uint8_t)((uint16_t)v & 0xffu));
                if (rc == 0)
                    rc = sampleInstall_putByte((uint8_t)((uint16_t)v >> 8));
            }
            if (rc != 0)
                return rc;
        }
    }
    return 0;
}

/*
 * Finish one wavetable and program its entry (S085 C2.2).
 *
 * What:       refuses a short table (-4, aborted); flushes the carry;
 *             programs the 20 B entry (offset, frame length, frame count,
 *             stored bits, name).
 * Outputs:    0, -1, -4, or a sampleFlash status.
 */
int sampleInstall_finishWavetable(void)
{
    sample_catalogue_wavetable_t entry;
    uint32_t addr;
    int rc;

    if (sampleInstall_openKind() != INSTALL_OPEN_WAVETABLE)
        return -1;
    if (sampleInstall_itemWritten() != sample_install.item_bytes) {
        sampleInstall_abortItem();
        return -4;
    }
    rc = sampleInstall_flushCarry();
    if (rc != 0) {
        sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
        return rc;
    }
    memset(&entry, 0xff, sizeof(entry));
    entry.offset = sample_install.item_start;
    entry.frame_len = sample_install.frame_len;
    entry.frame_count = sample_install.frame_count;
    entry.bits = sample_install.fmt_stored;
    memcpy(entry.name, sample_install.item_name, SAMPLE_DISPLAY_NAME_LEN);
    addr = SAMPLE_CATALOGUE_WAVETABLES_ADDRESS +
           (uint32_t)sample_install.wavetables * sizeof(entry);
    rc = sampleFlash_writeBytes(&addr, (const uint8_t *)&entry,
                                (uint32_t)sizeof(entry));
    sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
    if (rc != 0)
        return rc;
    sample_install.wavetables++;
    return 0;
}

/*
 * Abandon the open item (S085 C2.2).
 *
 * What:       discards both carries and closes the item without an entry.
 *             write_addr already points past the programmed words, so the
 *             next item starts cleanly; the abandoned bytes stay unused until
 *             the next erase.
 * Why:        one unreadable file skips that file or table, not the install
 *             (S084 P2 F7).
 * Accessors:  filesystem.c on a read failure; the finish calls on -4.
 */
void sampleInstall_abortItem(void)
{
    sample_install.out_carry_n = 0u;
    sample_install.in_carry_n = 0u;
    sample_install.item_bytes = 0u;
    sampleInstall_setOpenKind(INSTALL_OPEN_NONE);
}

/*
 * Commit the catalogue (S085 C2.2).
 *
 * What:       programs the header without its magic (counts, stock flag,
 *             data end), then the magic word last; invalidates the D-cache
 *             over sample flash and refreshes the reader (new generation).
 * Why:        a catalogue is valid only after a complete install; a power
 *             loss before the magic leaves EMPTY.
 * Outputs:    0, -1 (an item still open), or a sampleFlash status.
 * Accessors:  filesystem_installSamplesBlocking().
 * Affiliates: sampleMemory_refresh().
 */
int sampleInstall_commit(void)
{
    sample_catalogue_header_t header;
    uint32_t addr = SAMPLE_CATALOGUE_START_ADDRESS + 4u;
    int rc;

    if (sampleInstall_openKind() != INSTALL_OPEN_NONE)
        return -1;
    memset(&header, 0xff, sizeof(header));
    header.version = SAMPLE_CATALOGUE_VERSION;
    header.entry_bytes = (uint16_t)sizeof(sample_catalogue_sample_t);
    header.sample_count = sample_install.samples;
    header.wavetable_count = sample_install.wavetables;
    header.stock_valid =
        (uint8_t)((sample_install.flags & INSTALL_FLAG_STOCK) ? 1u : 0u);
    header.reserved0 = 0u;
    header.data_end = sample_install.write_addr;
    header.stock_start = SAMPLE_STOCK_START_ADDRESS;
    rc = sampleFlash_writeBytes(&addr, (const uint8_t *)&header + 4u,
                                (uint32_t)sizeof(header) - 4u);
    if (rc == 0)
        rc = sampleFlash_writeWord(SAMPLE_CATALOGUE_START_ADDRESS,
                                   SAMPLE_CATALOGUE_MAGIC);
    if (rc != 0)
        return rc;
    sampleFlash_invalidateRange(SAMPLE_ROM_START_ADDRESS,
                                SAMPLE_FLASH_END_ADDRESS -
                                    SAMPLE_ROM_START_ADDRESS);
    sampleMemory_refresh();
    return 0;
}
```

---
## 4. WP3 — Card walk, the Load:[Samples] modal, the boot notice

### C3.1 `Core/Hardware/SD/storageTypes.h` — natural order declarations

- **Where:** before line 655 `#endif /* STORAGETYPES_H_ */`.
- **Action:** **Add.**

```c
/*
 * Natural, numeric-aware name order (S085 C3.1).
 *
 * What:       case-insensitive compare in which each decimal digit run is one
 *             number: leading zeros are skipped, a longer remaining run is
 *             larger, equal lengths compare digit by digit; if the names are
 *             otherwise equal, the run with fewer leading zeros sorts first.
 * Why:        the installer orders samples, wavetable folders and frames as
 *             people read them (f2 before f10), deterministically.
 * Inputs:     a, b (NULL sorts first; empty allowed).  Outputs: -1, 0, 1.
 * Accessors:  filesystem.c filesystem_installCompare().
 * Affiliates: storage_stripOrderPrefix(), tools/dsp_test/test_natural_order.c.
 *             Pure string code: no asyncfatfs calls (this module's rule).
 */
int8_t storage_naturalCompare(const char *a, const char *b);

/*
 * Display start after an ordering prefix (S085 C3.1).
 *
 * What:       skips a leading digit run followed by a run of ' ', '_' or '-'
 *             and returns the remainder when it is not empty; otherwise
 *             returns name unchanged.
 * Why:        "003 hvoice" orders by its number and shows as "hvoice".
 * Inputs:     name.  Outputs: a pointer into name.
 * Accessors:  filesystem_installWavetableTree().
 * Affiliates: storage_naturalCompare() (the full name still orders).
 */
const char *storage_stripOrderPrefix(const char *name);
```

### C3.2 `Core/Hardware/SD/storageTypes.c` — natural order

- **Where:** end of file (after line 2450).
- **Action:** **Add** the two definitions. Copy the C3.1 blocks above them,
  with the implementation note "Pure string code; no asyncfatfs calls".

```c
int8_t storage_naturalCompare(const char *a, const char *b)
{
    const char *pa = a;
    const char *pb = b;
    int8_t zero_tie = 0;

    if (!a || !b)
        return (int8_t)((!a && !b) ? 0 : (!a ? -1 : 1));
    while (*pa != '\0' && *pb != '\0') {
        if (*pa >= '0' && *pa <= '9' && *pb >= '0' && *pb <= '9') {
            const char *za = pa;
            const char *zb = pb;
            const char *ea;
            const char *eb;
            uint16_t la;
            uint16_t lb;
            uint16_t i;

            while (*za == '0')
                za++;
            while (*zb == '0')
                zb++;
            for (ea = za; *ea >= '0' && *ea <= '9'; ea++)
                ;
            for (eb = zb; *eb >= '0' && *eb <= '9'; eb++)
                ;
            la = (uint16_t)(ea - za);
            lb = (uint16_t)(eb - zb);
            if (la != lb)
                return (int8_t)((la < lb) ? -1 : 1);
            for (i = 0u; i < la; i++) {
                if (za[i] != zb[i])
                    return (int8_t)((za[i] < zb[i]) ? -1 : 1);
            }
            if (zero_tie == 0 && (za - pa) != (zb - pb))
                zero_tie = (int8_t)(((za - pa) < (zb - pb)) ? -1 : 1);
            pa = ea;
            pb = eb;
            continue;
        }
        {
            char ca = *pa;
            char cb = *pb;

            if (ca >= 'A' && ca <= 'Z')
                ca = (char)(ca + ('a' - 'A'));
            if (cb >= 'A' && cb <= 'Z')
                cb = (char)(cb + ('a' - 'A'));
            if (ca != cb)
                return (int8_t)((ca < cb) ? -1 : 1);
        }
        pa++;
        pb++;
    }
    if (*pa != '\0')
        return 1;
    if (*pb != '\0')
        return -1;
    return zero_tie;
}

const char *storage_stripOrderPrefix(const char *name)
{
    const char *p = name;

    if (!name)
        return name;
    while (*p >= '0' && *p <= '9')
        p++;
    if (p == name || (*p != ' ' && *p != '_' && *p != '-'))
        return name;
    while (*p == ' ' || *p == '_' || *p == '-')
        p++;
    return (*p != '\0') ? p : name;
}
```

### C3.3 `Core/Hardware/SD/filesystem.h` — installer API

- **Where:** lines 1371–1372:

  ```c
  uint8_t filesystem_installSamplesBlocking(void);
  uint8_t filesystem_installLoopsBlocking(void);
  ```

- **Action:** **Modify** (replace both lines).

```c
/*
 * Load:[Samples] report (S085 C3.3).
 *
 * What:       counts of installed samples and wavetables, skipped files
 *             (unsupported, unreadable, mismatched frames, no fit, frames
 *             past 128) and skipped tables (no fit, no valid frame, unreadable
 *             folder), and whether the catalogue committed.
 * Why:        the modal's summary line.
 * Inputs:     filled by filesystem_installSamplesBlocking().
 * Outputs:    read by menu_loadSamplesModal().
 * Accessors:  menu.c.
 * Affiliates: SampleInstall.c counts.
 */
typedef struct {
    uint8_t  samples;
    uint8_t  wavetables;
    uint16_t skipped_files;
    uint8_t  skipped_tables;
    uint8_t  committed;
} fs_install_report_t;

/*
 * Install stages for the progress line (S085 C3.3).
 * Accessors: filesystem.c (reports), menu.c (shows).
 */
#define FS_INSTALL_STAGE_ERASE      0u
#define FS_INSTALL_STAGE_STOCK      1u
#define FS_INSTALL_STAGE_SAMPLES    2u
#define FS_INSTALL_STAGE_WAVETABLES 3u
#define FS_INSTALL_STAGE_COMMIT     4u
typedef void (*fs_install_progress_fn)(uint8_t stage, uint16_t count);

/*
 * Install samples/ and wavetables/ into catalogue v2, blocking (S085 C3.3).
 *
 * What:       erases sample flash, generates the stock block, installs
 *             samples/ (files first, then subfolders, natural order, depth
 *             <= 3), then every folder under wavetables/ that holds valid
 *             frames (one folder = one table), then commits.
 * Why:        plan §5 item 8; replaces the samples/ + loops/ pair.
 * Inputs:     the card (audio suspended by the caller); report (nullable).
 * Outputs:    1 when the catalogue committed; *report.
 * Accessors:  menu_loadSamplesModal() only.
 * Affiliates: SampleInstall.c, storage_naturalCompare().
 */
uint8_t filesystem_installSamplesBlocking(fs_install_report_t *report);

/*
 * Register the install progress hook (S085 C3.3).
 *
 * What:       stores or clears (NULL) the modal's progress callback.
 * Why:        Menu owns the LCD; the installer owns the stages.
 * Accessors:  menu_loadSamplesModal().
 */
void    filesystem_setInstallProgress(fs_install_progress_fn fn);
```

### C3.4 `Core/Hardware/SD/filesystem.c` — includes and installer state

- **Includes:** after line 78 `#include "SampleMemory.h"`, **add**
  `#include "SampleInstall.h"`.
- **State:**
  - **Where:** lines 23357–23373, from `#define FS_SAMPLE_MANIFEST_MAX` through
    `static uint8_t sample_io_buf[FS_SAMPLE_BUFFER_SIZE + 4u];`.
  - **Action:** **Modify** (replace).

```c
#define FS_SAMPLE_BUFFER_SIZE  512u
#define FS_SAMPLE_ELLIPSIS     ((char)0x00)
#define FS_INSTALL_BATCH       32u
#define FS_INSTALL_MAX_DEPTH   3u
#define FS_INSTALL_KEY_LEN     (AFATFS_LONG_FILENAME_MAX + 1u)   /* 49 */

/*
 * Batched natural-order selection (S085 C3.4).
 *
 * What:       fs_install_batch holds up to 32 directory entries (8.3 alias,
 *             verified display name as the sort key, directory flag), kept
 *             sorted; each pass over a directory fills it with the next names
 *             after the last one handled. fs_install_last[d] is that "last"
 *             entry for recursion level d. fs_install_report, _progress and
 *             _failed carry the running install.
 * Why:        the former 120-entry manifest (13,441 B) sorted a folder in RAM;
 *             this needs 2,313 B for any folder size, at about n/32
 *             directory passes (approved, S085 §0.5).
 *             Long names: asyncfatfs keeps names up to 48 ASCII characters;
 *             a longer or non-ASCII name falls back to its 8.3 alias, which
 *             then orders and names the item (S085 plan §6 item 9).
 * Inputs:     afatfs object scans.  Outputs: install order.
 * Accessors:  filesystem_installCollect() and the walkers below.
 * Affiliates: storage_naturalCompare(), FS_INSTALL_MAX_DEPTH (asyncfatfs has
 *             5 handles: the folder chain plus one open file).
 */
typedef struct {
    char    short_name[13];
    char    key[FS_INSTALL_KEY_LEN];
    uint8_t is_dir;
    uint8_t pad;
} fs_install_entry_t;

_Static_assert(sizeof(fs_install_entry_t) == 64u, "install entry is 64 B");

typedef struct {
    sample_install_fmt_t fmt;
    uint32_t data_offset;
    uint32_t data_bytes;
} fs_wav_info_t;

static fs_install_entry_t fs_install_batch[FS_INSTALL_BATCH];
static uint8_t fs_install_batch_count;
static fs_install_entry_t fs_install_last[FS_INSTALL_MAX_DEPTH + 1u];
static fs_install_report_t *fs_install_report;
static fs_install_progress_fn fs_install_progress;
static uint8_t fs_install_failed;
static uint8_t sample_io_buf[FS_SAMPLE_BUFFER_SIZE];
```

### C3.5 `filesystem.c` — bounded blocking opens

- **Add** after line 23405 (the end of `filesystem_blockOpenCb()`):

```c
/*
 * Bounded wait for a free asyncfatfs handle (S085 C3.5).
 *
 * What:       the blocking open helpers retry an open whose call returned
 *             false (asyncfatfs refuses only when no handle is free, after
 *             reporting NULL through the callback). They now give up after
 *             FS_BLOCK_OPEN_HANDLE_RETRIES polls and return NULL.
 * Why:        a handle can be freed by an asynchronous close during a poll,
 *             so a short wait stays; but when the caller's own open chain
 *             holds every handle, no poll frees one, and the former unbounded
 *             loop froze the device with audio suspended (S084 P2 F2).
 * Inputs:     the helper's retry counter.  Outputs: 1 give up, 0 retry.
 * Accessors:  filesystem_blockOpen(), _blockOpenLfn(), _blockOpenDirLfn().
 * Affiliates: AFATFS_MAX_OPEN_FILES, FS_INSTALL_MAX_DEPTH.
 */
#define FS_BLOCK_OPEN_HANDLE_RETRIES 2000u
static uint8_t filesystem_blockOpenGiveUp(uint16_t *retries)
{
    if (*retries >= FS_BLOCK_OPEN_HANDLE_RETRIES)
        return 1u;
    (*retries)++;
    return 0u;
}
```

- **Modify** the three helpers, lines 23464–23529:
  `filesystem_blockOpen()`, `filesystem_blockOpenLfn()` and
  `filesystem_blockOpenDirLfn()`. Each first `while (!afatfs_…(…))` loop
  becomes:

```c
    uint16_t retries = 0u;

    block_file_ready = 0;
    block_file = NULL;
    while (!afatfs_fopen(filename, "r", filesystem_blockOpenCb)) {
        /* S085 C3.5: a refused open (no free handle) is bounded; the
        ** refusal's NULL callback is cleared so the wait below sees only
        ** a real open. */
        block_file_ready = 0;
        filesystem_blockPoll();
        if (!filesystem_blockFsOk() || filesystem_blockOpenGiveUp(&retries))
            return NULL;
    }
```

  - The same applies to `afatfs_fopen_lfn(…)` and `afatfs_opendir_lfn(…)`,
    with their existing arguments.
  - The second loop (`while (!block_file_ready)`) is unchanged.

### C3.6 `filesystem.c` — remove the v1 sample helpers

- **Remove:**
  - lines 24336–24339, `filesystem_le16()`;
  - lines 24366–24372, `filesystem_sampleNameFromFilename()`;
  - lines 24409–24514: `filesystem_copySampleSortName()`,
    `filesystem_lfnCharAt()`, `filesystem_lfnReset()`,
    `filesystem_lfnAppendEntry()`, `filesystem_sortLower()`,
    `filesystem_compareSampleNameStrict()` and
    `filesystem_insertSampleManifest()`.
- **Modify:** line 24356 (`filesystem_isWavName`) and line 24380
  (`filesystem_displayNameFromFilename`). Replace
  `(FS_SAMPLE_LFN_MAX - 1u)` with `(FS_INSTALL_KEY_LEN - 1u)` in both.
- **Why:** the object scan now supplies verified display names; the v1
  manual LFN reassembly, the 3-character names and the manifest are gone.

### C3.7 `filesystem.c` — `filesystem_parseWav()`

- **Where:** lines 24516–24590.
- **Action:** **Modify** (replace the function).

```c
/*
 * Parse one WAV header (S085 C3.7).
 *
 * What:       opens the 8.3 alias, checks RIFF/WAVE, walks the chunks; the fmt
 *             chunk goes to sampleInstall_acceptFormat() for the given kind;
 *             the data chunk must lie inside the file (a seek to its exact end
 *             succeeds; asyncfatfs clamps seeks to EOF, so a truncated or
 *             streamed file fails here). Stops once both chunks are known.
 *             Rejects data that is empty or not a whole number of frames.
 * Why:        a file whose declared data exceeds the file would otherwise
 *             fail mid-copy after flash was programmed (S084 P2 F7); a
 *             missing pad byte after an odd data chunk must not reject a
 *             valid file.
 * Inputs:     short_name, kind.  Outputs: 1 and *out, or 0 (skip the file).
 * Accessors:  filesystem_installSampleFiles(), filesystem_installWavetable().
 * Affiliates: sampleInstall_acceptFormat(), filesystem_installCopy().
 */
static uint8_t filesystem_parseWav(const char *short_name,
                                   sample_install_kind_t kind,
                                   fs_wav_info_t *out)
{
    afatfsFilePtr_t file;
    uint8_t hdr[12];
    uint8_t fmt_buf[40];
    uint8_t fmt_ok = 0u;
    uint8_t data_ok = 0u;
    uint32_t data_offset = 0u;
    uint32_t data_bytes = 0u;

    if (!out)
        return 0u;
    file = filesystem_blockOpen(short_name);
    if (file == NULL)
        return 0u;
    if (filesystem_blockRead(file, hdr, 12u) != 12u ||
        !filesystem_chunkIdEquals(hdr, "RIFF") ||
        !filesystem_chunkIdEquals(hdr + 8u, "WAVE"))
        goto fail;
    while (!afatfs_feof(file)) {
        uint32_t pos;
        uint32_t size;
        uint32_t next;

        if (filesystem_blockRead(file, hdr, 8u) != 8u)
            break;
        size = filesystem_le32(hdr + 4u);
        if (!filesystem_blockTell(file, &pos))
            goto fail;
        next = pos + size + (size & 1u);
        if (next < pos)
            goto fail;
        if (filesystem_chunkIdEquals(hdr, "fmt ")) {
            const uint32_t n = (size < sizeof(fmt_buf))
                ? size : (uint32_t)sizeof(fmt_buf);

            if (n < 16u || filesystem_blockRead(file, fmt_buf, n) != n ||
                !sampleInstall_acceptFormat(fmt_buf, n, kind, &out->fmt))
                goto fail;
            fmt_ok = 1u;
        } else if (filesystem_chunkIdEquals(hdr, "data")) {
            if (!filesystem_blockSeek(file, pos + size))
                goto fail;
            data_offset = pos;
            data_bytes = size;
            data_ok = 1u;
            if (fmt_ok)
                break;
        }
        if (fmt_ok && data_ok)
            break;
        if (!filesystem_blockSeek(file, next))
            goto fail;
    }
    if (!fmt_ok || !data_ok || out->fmt.block_align == 0u ||
        data_bytes < out->fmt.block_align ||
        (data_bytes % out->fmt.block_align) != 0u)
        goto fail;
    out->data_offset = data_offset;
    out->data_bytes = data_bytes;
    (void)filesystem_blockClose(file);
    return 1u;
fail:
    (void)filesystem_blockClose(file);
    return 0u;
}
```

### C3.8 `filesystem.c` — the install walk

- **Where:** lines 24592–24810, from `filesystem_manifestCanFit()` through
  the end of `filesystem_installLoopsBlocking()`.
- **Action:** **Remove** `filesystem_manifestCanFit()`,
  `filesystem_scanSamples()`, `filesystem_installOneSample()`,
  `filesystem_installSampleFolderBlocking()`,
  `filesystem_installSamplesBlocking(void)` and
  `filesystem_installLoopsBlocking()`. **Add** the functions below, in this
  order.

```c
/*
 * Install progress notification (S085 C3.8).
 *
 * What:       forwards a stage or item count to the Menu hook when set.
 * Why:        the install now includes stock generation and frames; the LCD
 *             shows a moving stage line instead of one static text.
 * Accessors:  the install functions below.
 */
static void filesystem_installProgress(uint8_t stage, uint16_t count)
{
    if (fs_install_progress)
        fs_install_progress(stage, count);
}

/*
 * Install entry order (S085 C3.8).
 *
 * What:       natural order of the display name, then a byte compare of the
 *             display name, then the 8.3 alias.
 * Why:        deterministic order for names equal under natural rules.
 * Accessors:  filesystem_installCollect().
 */
static int8_t filesystem_installCompare(const fs_install_entry_t *a,
                                        const fs_install_entry_t *b)
{
    int8_t c = storage_naturalCompare(a->key, b->key);

    if (c == 0)
        c = (int8_t)strcmp(a->key, b->key);
    if (c == 0)
        c = (int8_t)strcmp(a->short_name, b->short_name);
    return (int8_t)((c < 0) ? -1 : ((c > 0) ? 1 : 0));
}

/*
 * Blocking return to the parent directory (S085 C3.8).
 *
 * What:       polls afatfs_chdirParent() until SUCCESS or FAILURE.
 * Why:        it returns an operation status (SUCCESS == 0), so it must never
 *             be tested with !().
 * Accessors:  the two tree walkers.
 */
static uint8_t filesystem_installChdirParent(void)
{
    for (;;) {
        const afatfsOperationStatus_e st = afatfs_chdirParent();

        if (st == AFATFS_OPERATION_SUCCESS)
            return 1u;
        if (st == AFATFS_OPERATION_FAILURE)
            return 0u;
        filesystem_blockPoll();
        if (!filesystem_blockFsOk())
            return 0u;
    }
}

/*
 * Collect the next batch of entries in natural order (S085 C3.8).
 *
 * What:       one object scan of the current directory. Keeps entries that
 *             match want_dir (directories) or are .wav files, do not start
 *             with '.', and sort strictly after *after (an empty key means
 *             from the start). Only the `capacity` smallest are kept, sorted
 *             by insertion. The scan always ends with afatfs_findLastObject().
 * Why:        bounded RAM for any folder size; deterministic order.
 * Inputs:     dir, want_dir, after, capacity 1..FS_INSTALL_BATCH.
 * Outputs:    fs_install_batch[0..fs_install_batch_count); 0 on a card error.
 * Accessors:  the walkers below.
 * Affiliates: filesystem_blockFindNextObject(), filesystem_installCompare().
 */
static uint8_t filesystem_installCollect(afatfsFilePtr_t dir, uint8_t want_dir,
                                         const fs_install_entry_t *after,
                                         uint8_t capacity)
{
    afatfsObjectFinder_t finder;
    afatfsObjectInfo_t object;

    if (capacity == 0u || capacity > FS_INSTALL_BATCH)
        capacity = FS_INSTALL_BATCH;
    fs_install_batch_count = 0u;
    afatfs_findFirstObject(dir, &finder);
    for (;;) {
        fs_install_entry_t e;
        uint8_t i;
        const afatfsOperationStatus_e st =
            filesystem_blockFindNextObject(dir, &finder, &object);

        if (st == AFATFS_OPERATION_FAILURE) {
            afatfs_findLastObject(dir, &finder);
            return 0u;
        }
        if (object.id.kind == AFATFS_OBJECT_NONE)
            break;
        if (object.id.displayName[0] == '\0' ||
            object.id.displayName[0] == '.')
            continue;
        if (want_dir ? (object.id.kind != AFATFS_OBJECT_DIRECTORY)
                     : (object.id.kind != AFATFS_OBJECT_FILE ||
                        !filesystem_isWavName(object.id.displayName)))
            continue;
        memset(&e, 0, sizeof(e));
        memcpy(e.short_name, object.id.shortName, 12u);
        memcpy(e.key, object.id.displayName, FS_INSTALL_KEY_LEN - 1u);
        e.is_dir = want_dir;
        if (after && after->key[0] != '\0' &&
            filesystem_installCompare(&e, after) <= 0)
            continue;
        i = fs_install_batch_count;
        if (i >= capacity) {
            if (filesystem_installCompare(&e,
                    &fs_install_batch[capacity - 1u]) >= 0)
                continue;
            i = (uint8_t)(capacity - 1u);
        } else {
            fs_install_batch_count++;
        }
        while (i > 0u &&
               filesystem_installCompare(&e, &fs_install_batch[i - 1u]) < 0) {
            fs_install_batch[i] = fs_install_batch[i - 1u];
            i--;
        }
        fs_install_batch[i] = e;
    }
    afatfs_findLastObject(dir, &finder);
    return 1u;
}

/*
 * Stream one data chunk into the open item (S085 C3.8).
 *
 * What:       opens the alias, seeks to the data, reads 512 B chunks into
 *             sample_io_buf and hands them to the sample or frame writer.
 * Outputs:    FS_COPY_OK; FS_COPY_READ_FAIL (open/seek/read failure: the
 *             caller skips this file or table); FS_COPY_WRITE_FAIL (writer or
 *             flash failure: the caller aborts the install).
 * Accessors:  filesystem_installSampleFiles(), filesystem_installWavetable().
 */
#define FS_COPY_OK          0u
#define FS_COPY_READ_FAIL   1u
#define FS_COPY_WRITE_FAIL  2u
static uint8_t filesystem_installCopy(const char *short_name, uint32_t offset,
                                      uint32_t bytes, uint8_t frames)
{
    afatfsFilePtr_t file = filesystem_blockOpen(short_name);
    uint8_t result = FS_COPY_READ_FAIL;

    if (file == NULL)
        return FS_COPY_READ_FAIL;
    if (!filesystem_blockSeek(file, offset))
        goto done;
    while (bytes > 0u) {
        const uint32_t n = (bytes > FS_SAMPLE_BUFFER_SIZE)
            ? FS_SAMPLE_BUFFER_SIZE : bytes;
        int rc;

        if (filesystem_blockRead(file, sample_io_buf, n) != n)
            goto done;
        bytes -= n;
        rc = frames ? sampleInstall_writeFrameData(sample_io_buf, n)
                    : sampleInstall_writeSampleData(sample_io_buf, n);
        if (rc != 0) {
            result = FS_COPY_WRITE_FAIL;
            goto done;
        }
    }
    result = FS_COPY_OK;
done:
    (void)filesystem_blockClose(file);
    return result;
}

/*
 * Install every WAV file of one directory (S085 C3.8).
 *
 * What:       batches of 32 in natural order; each file is parsed (SAMPLE
 *             form), begun, streamed and finished. Unsupported, refused or
 *             unreadable files are counted and skipped; only a flash/state
 *             failure stops the install.
 * Inputs:     dir (the current directory), depth.
 * Outputs:    report counts; fs_install_last[depth].
 * Accessors:  filesystem_installSampleTree().
 */
static void filesystem_installSampleFiles(afatfsFilePtr_t dir, uint8_t depth)
{
    fs_install_last[depth].key[0] = '\0';
    for (;;) {
        uint8_t i;

        if (!filesystem_installCollect(dir, 0u, &fs_install_last[depth],
                                       FS_INSTALL_BATCH)) {
            fs_install_failed = 1u;
            return;
        }
        if (fs_install_batch_count == 0u)
            return;
        for (i = 0u; i < fs_install_batch_count; i++) {
            fs_wav_info_t wav;
            char display[SAMPLE_DISPLAY_NAME_LEN];
            uint8_t copy;
            int rc;

            if (!filesystem_parseWav(fs_install_batch[i].short_name,
                                     SAMPLE_INSTALL_KIND_SAMPLE, &wav)) {
                fs_install_report->skipped_files++;
                continue;
            }
            filesystem_displayNameFromFilename(display,
                                               fs_install_batch[i].key);
            if (sampleInstall_beginSample(display, &wav.fmt,
                                          wav.data_bytes) != 0) {
                fs_install_report->skipped_files++;
                continue;
            }
            copy = filesystem_installCopy(fs_install_batch[i].short_name,
                                          wav.data_offset, wav.data_bytes, 0u);
            if (copy == FS_COPY_READ_FAIL) {
                sampleInstall_abortItem();
                fs_install_report->skipped_files++;
                continue;
            }
            rc = (copy == FS_COPY_OK) ? sampleInstall_finishSample() : -1;
            if (rc == -4) {
                fs_install_report->skipped_files++;
                continue;
            }
            if (rc != 0) {
                fs_install_failed = 1u;
                return;
            }
            fs_install_report->samples++;
            filesystem_installProgress(FS_INSTALL_STAGE_SAMPLES,
                                       fs_install_report->samples);
        }
        fs_install_last[depth] = fs_install_batch[fs_install_batch_count - 1u];
    }
}

/*
 * Open and enter one subfolder by its display name (S085 C3.8).
 *
 * What:       afatfs_opendir_lfn(display name) and chdir into it. The display
 *             name, not the 8.3 alias, is used: for an entry with a long name
 *             asyncfatfs matches only the long name (S084 P2 F1).
 * Outputs:    the handle, or NULL (fs_install_failed set when the card is no
 *             longer usable; otherwise the caller counts a skip).
 * Accessors:  the two tree walkers.
 */
static afatfsFilePtr_t filesystem_installEnter(const fs_install_entry_t *e)
{
    afatfsFilePtr_t sub = filesystem_blockOpenDirLfn(e->key);

    if (sub == NULL) {
        if (!filesystem_blockFsOk())
            fs_install_failed = 1u;
        return NULL;
    }
    if (!filesystem_blockChdir(sub)) {
        (void)filesystem_blockClose(sub);
        fs_install_failed = 1u;
        return NULL;
    }
    return sub;
}

/* Leave a subfolder entered by filesystem_installEnter() (S085 C3.8). */
static void filesystem_installLeave(afatfsFilePtr_t sub)
{
    if (!filesystem_installChdirParent() || !filesystem_blockClose(sub))
        fs_install_failed = 1u;
}

/*
 * Walk samples/ (S085 C3.8).
 *
 * What:       the directory's files first, then its subfolders one at a time
 *             in natural order (each: files first, then its subfolders),
 *             while depth < FS_INSTALL_MAX_DEPTH.
 * Why:        the deepest open chain is samples/ + 3 folders + 1 file = 5
 *             asyncfatfs handles (AFATFS_MAX_OPEN_FILES).
 * Inputs:     dir, depth (0 = samples/).
 * Outputs:    installed samples; the CWD is dir again on return.
 * Accessors:  filesystem_installSamplesBlocking().
 */
static void filesystem_installSampleTree(afatfsFilePtr_t dir, uint8_t depth)
{
    fs_install_entry_t after;

    filesystem_installSampleFiles(dir, depth);
    if (fs_install_failed || depth >= FS_INSTALL_MAX_DEPTH)
        return;
    memset(&after, 0, sizeof(after));
    for (;;) {
        afatfsFilePtr_t sub;

        if (!filesystem_installCollect(dir, 1u, &after, 1u)) {
            fs_install_failed = 1u;
            return;
        }
        if (fs_install_batch_count == 0u)
            return;
        after = fs_install_batch[0];
        sub = filesystem_installEnter(&after);
        if (sub == NULL) {
            if (fs_install_failed)
                return;
            fs_install_report->skipped_files++;
            continue;
        }
        filesystem_installSampleTree(sub, (uint8_t)(depth + 1u));
        filesystem_installLeave(sub);
        if (fs_install_failed)
            return;
    }
}

/*
 * Install one wavetable folder (S085 C3.8).
 *
 * What:       Pass A validates and counts the folder's WAV frames: the first
 *             valid file fixes the stored depth and the frame length (frames
 *             of 2..65,535 points); later files must match both or are
 *             skipped; frames past 128 are counted as skipped. If at least one
 *             frame is valid the table is begun (fit checked before any byte
 *             is written). Pass B repeats the same batches and rule and
 *             streams exactly the counted frames, each decoded with its own
 *             source format (sampleInstall_setFrameFormat()). A read failure
 *             skips the table; a short table (-4) is skipped.
 * Why:        one length and one stored depth per table; mixed source forms
 *             convert correctly; one bad file never aborts the install.
 * Inputs:     dir (the table folder, current), display name.
 * Outputs:    installed tables; skipped files/tables.
 * Accessors:  filesystem_installWavetableTree().
 */
static void filesystem_installWavetable(afatfsFilePtr_t dir,
                                        const char display[SAMPLE_DISPLAY_NAME_LEN])
{
    fs_install_entry_t after;
    fs_wav_info_t first;
    uint8_t have_first = 0u;
    uint16_t frame_len = 0u;
    uint8_t count = 0u;
    uint8_t copied = 0u;
    uint8_t i;
    int rc;

    memset(&after, 0, sizeof(after));
    for (;;) {                                   /* Pass A */
        if (!filesystem_installCollect(dir, 0u, &after, FS_INSTALL_BATCH)) {
            fs_install_failed = 1u;
            return;
        }
        if (fs_install_batch_count == 0u)
            break;
        for (i = 0u; i < fs_install_batch_count; i++) {
            fs_wav_info_t wav;
            uint32_t fl;

            if (!filesystem_parseWav(fs_install_batch[i].short_name,
                                     SAMPLE_INSTALL_KIND_FRAME, &wav)) {
                fs_install_report->skipped_files++;
                continue;
            }
            fl = wav.data_bytes / wav.fmt.block_align;
            if (fl < 2u || fl > 0xffffu) {
                fs_install_report->skipped_files++;
            } else if (!have_first) {
                first = wav;
                have_first = 1u;
                frame_len = (uint16_t)fl;
                count = 1u;
            } else if (wav.fmt.stored_bits == first.fmt.stored_bits &&
                       (uint16_t)fl == frame_len &&
                       count < WAVETABLE_MAX_FRAMES) {
                count++;
            } else {
                fs_install_report->skipped_files++;
            }
        }
        after = fs_install_batch[fs_install_batch_count - 1u];
    }
    if (!have_first)
        return;
    if (sampleInstall_beginWavetable(display, &first.fmt, frame_len,
                                     count) != 0) {
        fs_install_report->skipped_tables++;
        return;
    }
    memset(&after, 0, sizeof(after));
    for (;;) {                                   /* Pass B */
        if (!filesystem_installCollect(dir, 0u, &after, FS_INSTALL_BATCH)) {
            fs_install_failed = 1u;
            return;
        }
        if (fs_install_batch_count == 0u)
            break;
        for (i = 0u; i < fs_install_batch_count && copied < count; i++) {
            fs_wav_info_t wav;
            uint8_t copy;

            if (!filesystem_parseWav(fs_install_batch[i].short_name,
                                     SAMPLE_INSTALL_KIND_FRAME, &wav) ||
                wav.fmt.stored_bits != first.fmt.stored_bits ||
                wav.data_bytes / wav.fmt.block_align != frame_len)
                continue;
            if (sampleInstall_setFrameFormat(&wav.fmt) != 0) {
                fs_install_failed = 1u;
                return;
            }
            copy = filesystem_installCopy(fs_install_batch[i].short_name,
                                          wav.data_offset,
                                          (uint32_t)frame_len *
                                              wav.fmt.block_align, 1u);
            if (copy == FS_COPY_READ_FAIL) {
                sampleInstall_abortItem();
                fs_install_report->skipped_tables++;
                return;
            }
            if (copy != FS_COPY_OK) {
                fs_install_failed = 1u;
                return;
            }
            copied++;
        }
        after = fs_install_batch[fs_install_batch_count - 1u];
    }
    rc = sampleInstall_finishWavetable();
    if (rc == -4) {
        fs_install_report->skipped_tables++;
        return;
    }
    if (rc != 0) {
        fs_install_failed = 1u;
        return;
    }
    fs_install_report->wavetables++;
    filesystem_installProgress(FS_INSTALL_STAGE_WAVETABLES,
                               fs_install_report->wavetables);
}

/*
 * Walk wavetables/ (S085 C3.8).
 *
 * What:       every subfolder is one table named by the folder (ordering
 *             prefix stripped, first 8 display characters, the ellipsis rule
 *             of filesystem_displayNameFromFilename()); its files install as
 *             that table, then its own subfolders are visited the same way
 *             while depth + 1 < FS_INSTALL_MAX_DEPTH. Files directly in
 *             wavetables/ belong to no table and are ignored.
 * Why:        one folder shape for any card; the open chain stays within 5
 *             handles (wavetables/ + 3 folders + 1 file).
 * Inputs:     dir, depth (0 = wavetables/).
 * Outputs:    installed tables; the CWD is dir again on return.
 * Accessors:  filesystem_installSamplesBlocking().
 */
static void filesystem_installWavetableTree(afatfsFilePtr_t dir, uint8_t depth)
{
    fs_install_entry_t after;

    memset(&after, 0, sizeof(after));
    for (;;) {
        afatfsFilePtr_t sub;
        char display[SAMPLE_DISPLAY_NAME_LEN];

        if (!filesystem_installCollect(dir, 1u, &after, 1u)) {
            fs_install_failed = 1u;
            return;
        }
        if (fs_install_batch_count == 0u)
            return;
        after = fs_install_batch[0];
        sub = filesystem_installEnter(&after);
        if (sub == NULL) {
            if (fs_install_failed)
                return;
            fs_install_report->skipped_tables++;
            continue;
        }
        filesystem_displayNameFromFilename(display,
                                           storage_stripOrderPrefix(after.key));
        filesystem_installWavetable(sub, display);
        if (!fs_install_failed && (uint8_t)(depth + 1u) < FS_INSTALL_MAX_DEPTH)
            filesystem_installWavetableTree(sub, (uint8_t)(depth + 1u));
        filesystem_installLeave(sub);
        if (fs_install_failed)
            return;
    }
}

/* Open a top-level folder and walk it (S085 C3.8); a missing folder is fine. */
static void filesystem_installTop(const char *name,
                                  void (*walk)(afatfsFilePtr_t, uint8_t))
{
    afatfsFilePtr_t dir = filesystem_blockOpenDirLfn(name);

    if (dir == NULL) {
        if (!filesystem_blockFsOk())
            fs_install_failed = 1u;
        return;
    }
    if (filesystem_blockChdir(dir))
        walk(dir, 0u);
    else
        fs_install_failed = 1u;
    (void)filesystem_blockChdir(NULL);
    (void)filesystem_blockClose(dir);
}

uint8_t filesystem_installSamplesBlocking(fs_install_report_t *report)
{
    fs_install_report_t local;
    uint8_t ok = 0u;

    if (status == FS_STATUS_BUSY || !filesystem_blockFsOk())
        return 0u;
    status = FS_STATUS_BUSY;
    current_op = FS_INTERNAL_OP_NONE;
    fs_install_report = report ? report : &local;
    memset(fs_install_report, 0, sizeof(*fs_install_report));
    fs_install_failed = 0u;
    fs_install_batch_count = 0u;

    filesystem_installProgress(FS_INSTALL_STAGE_ERASE, 0u);
    if (sampleInstall_begin() != 0)
        goto done;
    filesystem_installProgress(FS_INSTALL_STAGE_STOCK, 0u);
    if (sampleInstall_writeStockBlock() != 0)
        goto done;
    filesystem_installProgress(FS_INSTALL_STAGE_SAMPLES, 0u);
    filesystem_installTop("samples", filesystem_installSampleTree);
    if (fs_install_failed)
        goto done;
    filesystem_installProgress(FS_INSTALL_STAGE_WAVETABLES, 0u);
    filesystem_installTop("wavetables", filesystem_installWavetableTree);
    if (fs_install_failed)
        goto done;
    filesystem_installProgress(FS_INSTALL_STAGE_COMMIT, 0u);
    if (sampleInstall_commit() == 0) {
        fs_install_report->committed = 1u;
        ok = 1u;
    }
done:
    fs_install_report = NULL;
    current_op = FS_INTERNAL_OP_NONE;
    status = FS_STATUS_IDLE;
    return ok;
}

void filesystem_setInstallProgress(fs_install_progress_fn fn)
{
    fs_install_progress = fn;
}
```

- **Note:** `filesystem_installSamplesBlocking()` and
  `filesystem_setInstallProgress()` take the C3.3 blocks.
- **Stack:** recursion keeps one `fs_install_entry_t` (64 B) per level,
  depth ≤ 3, during the modal only (§0.5).

### C3.9 `main.c` — boot notice; `menu_setNumSamples` removed

- **Notice function:** add after line 317, the end of `boot_delayMs()`
  (outside every `#if DEV_MODE_*`):

```c
/*
 * Sample flash notice at boot (S085 C3.9).
 *
 * What:       STALE (a v1 install from an older firmware): "Samples invalid"
 *             / "reload samples"; EMPTY (never installed, or an interrupted
 *             install): "No samples" / "load samples". 1.5 s each, LCD only.
 * Why:        plan §4.3.4/§4.3.6: catalogue v2 does not read v1 installs, and
 *             the stock waves sq3 sw3 sqa tra swa exist only after a
 *             Load:[Samples] (they play as Rec/Saw/Tri until then).
 * Inputs:     sampleMemory_catalogueState().  Outputs: LCD text.
 * Accessors:  main(), once, right after sampleMemory_init().
 * Affiliates: boot_delayMs(), SampleMemory.c.
 */
static void boot_showSampleCatalogueNotice(uint8_t state)
{
    lcd_clear();
    lcd_home();
    lcd_string(state == SAMPLE_CATALOGUE_STALE ? "Samples invalid"
                                               : "No samples");
    lcd_setcursor(0, 2);
    lcd_string(state == SAMPLE_CATALOGUE_STALE ? "reload samples"
                                               : "load samples");
    boot_delayMs(1500u);
}
```

- **Call:** after line 582, `sampleMemory_init();`, **add**:

```c
    /* S085 C3.9: catalogue v2 cannot read v1 installs; stock waves need an
    ** install. */
    if (sampleMemory_catalogueState() != SAMPLE_CATALOGUE_V2)
        boot_showSampleCatalogueNotice(sampleMemory_catalogueState());
```

- **Remove** line 663, `menu_setNumSamples(sampleMemory_getNumSamples());`
  (S085 D8).
- **Includes:** check that `main.c` includes `SampleMemory.h` (it calls
  `sampleMemory_init()`; keep that include).

### C3.10 `Core/Menu/menu.c` — the Load:[Samples] modal

- **Where:** lines 1048–1112, `menu_loadSamplesModal()`.
- **Action:**
  - **Add** the helpers below above it.
  - **Modify** the modal: one installer call, the progress hook, the
    summary.
  - **Include:** `menu.c` already includes `filesystem.h`; add
    `#include "SampleInstall.h"` only if the compiler needs the counts (it
    does not: the report carries them).

```c
/*
 * Install summary hold (S085 C3.10).
 *
 * What:       time_sysTick deadline (0 = none) until which the Load:[Samples]
 *             summary stays on screen; then the normal page repaints.
 * Why:        a blocking delay after the modal would starve audio; the
 *             deadline is serviced from menu_pollPresetStatus() (approved
 *             2 B, S085 §0.5).
 * Writers:    menu_loadSamplesModal().  Readers: menu_serviceInstallSummary().
 */
static uint16_t menu_installSummaryUntil;

/*
 * Three-digit count text (S085 C3.10).
 *
 * What:       writes value (0..999) as three digits, leading zeros kept.
 * Accessors:  menu_installProgress(), menu_loadSamplesModal().
 */
static void menu_installCount3(char *dst, uint16_t value)
{
    if (value > 999u)
        value = 999u;
    dst[0] = (char)('0' + value / 100u);
    dst[1] = (char)('0' + (value / 10u) % 10u);
    dst[2] = (char)('0' + value % 10u);
}

/*
 * Load:[Samples] progress line (S085 C3.10).
 *
 * What:       row 2 shows the stage: "Erasing", "Stock waves",
 *             "Samples NNN", "Wavetbl NNN", "Commit".
 * Why:        the install is longer than v1; the user sees it move.
 * Inputs:     stage (FS_INSTALL_STAGE_*), count.
 * Accessors:  registered with filesystem_setInstallProgress().
 */
static void menu_installProgress(uint8_t stage, uint16_t count)
{
    char line[17] = "                ";

    switch (stage) {
    case FS_INSTALL_STAGE_ERASE:   memcpy(line, "Erasing", 7u); break;
    case FS_INSTALL_STAGE_STOCK:   memcpy(line, "Stock waves", 11u); break;
    case FS_INSTALL_STAGE_SAMPLES:
        memcpy(line, "Samples", 7u);
        menu_installCount3(&line[8], count);
        break;
    case FS_INSTALL_STAGE_WAVETABLES:
        memcpy(line, "Wavetbl", 7u);
        menu_installCount3(&line[8], count);
        break;
    default:                       memcpy(line, "Commit", 6u); break;
    }
    lcd_setcursor(0, 2);
    lcd_string(line);
    lcd_waitForIdle();
}

/*
 * Retire the summary (S085 C3.10).
 *
 * What:       once time_sysTick passes menu_installSummaryUntil, clears the
 *             deadline and repaints the menu.
 * Accessors:  menu_pollPresetStatus() (first statement).
 */
static void menu_serviceInstallSummary(void)
{
    if (menu_installSummaryUntil == 0u ||
        (int16_t)(time_sysTick - menu_installSummaryUntil) < 0)
        return;
    menu_installSummaryUntil = 0u;
    menu_repaintAll();
}
```

The new `menu_loadSamplesModal()` body:
- **Unchanged:** everything up to and including the "Waiting SD…" pump.
- **Replaced:** from `menu_setStorageMessage("Sample upload", "Writing
  flash");` (the current line 1093) to the end of the function.

```c
    {
        fs_install_report_t report;
        char row1[17] = "Smp 000 Wtb 000 ";
        char row2[17] = "Skip 000        ";
        uint8_t ok;

        menu_setStorageMessage("Sample upload", "Erasing");
        filesystem_setInstallProgress(menu_installProgress);
        ok = filesystem_installSamplesBlocking(&report);
        filesystem_setInstallProgress(NULL);
        audioCodec_resume();

        if (menu_loadSaveCommandActive)
            menu_finishLoadSaveCommand();
        else {
            menu_storageBusy = 0u;
            menu_resetSaveParameters();
        }
        /*
         * Summary (S085 C3.10): "Smp NNN Wtb NNN" / "Skip NNN" (files plus
         * tables), or "Sample upload" / "Failed". Held 1.5 s through
         * menu_installSummaryUntil, never by blocking.
         */
        lcd_clear();
        lcd_home();
        if (ok) {
            menu_installCount3(&row1[4], report.samples);
            menu_installCount3(&row1[12], report.wavetables);
            menu_installCount3(&row2[5], (uint16_t)(report.skipped_files +
                                                    report.skipped_tables));
            lcd_string(row1);
            lcd_setcursor(0, 2);
            lcd_string(row2);
        } else {
            lcd_string("Sample upload");
            lcd_setcursor(0, 2);
            lcd_string("Failed");
        }
        menu_installSummaryUntil = (uint16_t)(time_sysTick + 1500u);
        if (menu_installSummaryUntil == 0u)
            menu_installSummaryUntil = 1u;
    }
}
```

- The variables `samplesOk`, `loopsOk` and `failedMessage` are removed.
- `time_sysTick` counts milliseconds (the existing modal waits 1,000 ticks
  for one second), so 1.5 s is 1,500 ticks. The deadline compares by signed
  16-bit distance, like `led_timeReached()`, so the 65.5 s wrap is safe.

**Hook:**
- **Where:** in `menu_pollPresetStatus()` (line 16604), after the two local
  declarations.
- **Action:** **add** `menu_serviceInstallSummary();` with the comment
  `/* S085 C3.10: retire the Load:[Samples] summary. */`.

### C3.11 `menu_numSamples` removal (S085 D8)

| Where | Action |
|---|---|
| `menu.c:1657` `uint8_t menu_numSamples = 0;` | Remove |
| `menu.c:18892` `void menu_setNumSamples(uint8_t n) …` | Remove |
| `menu.h:420` `void menu_setNumSamples(uint8_t num);` | Remove |
| `menu.c:1098` (old modal) | Removed with C3.10 |
| `main.c:663` | Removed with C3.9 |
| `menu.c:11456` `MENU_WAVEFORM` count | Modified in C15.7 |

---
## 5. WP4 — Stock waves `sq3 sw3 sqa tra swa`

Five additive tables, eleven octave levels each, are generated into the stock
block at every install from about 10 KB of recipes. Each level is only as
long as its harmonics need: 1,024 points for levels 0–4, then 512, 256, 128,
64, 16 and 16 (12,224 B per wave). Until the first install the reader falls
back to `Rec`/`Saw`/`Tri`.

### C4.1 `tools/plots/stock_wave_harmonics.csv` (input data)

- **Action:** **New file** (data).
- **Content:**
  - Columns `wave,harmonic,magnitude,phase_rad`.
  - Rows for waves `sq3 sw3 sqa tra swa`, harmonics 1..511 (2,555 rows plus
    the header).
- **Provenance:**
  - `sq3`/`sw3` are fitted to the user's 303 captures and `tra` to the dirty
    triangle reference (`tools/plots/reference/`).
  - `sqa`/`swa` are analog models.
  - The file is the measured data the plan approved (Q6 "fitted for now").
    It carries over unchanged from the S084 work as data, not code.
- **Rule:** never edited by hand. A change is a new fit plus a regeneration
  (C4.2).

### C4.2 `tools/gen_stock_recipes.py` (new tool)

- **Action:** **New file.** A host script (numpy). It runs only when the CSV
  changes.
- **Contract block:** at the top of the file, in the docstring.

```text
What:       reads the CSV and the firmware sine table, quantises each wave's
            cosine/sine coefficients to int16, emulates the device's additive
            sum in float32 for every level, and writes
            Core/SampleRom/StockWaveRecipes.c (A, B and the per-level peak
            scale). With --emit-ref PATH it also writes the expected int16
            points (5 x 6,112, little-endian) for test_stock_gen.
Why:        the stock tables are generated on the device at install from
            recipes (plan §4.3.4); this script is the single source of the
            recipes and of the device-exact reference.
Inputs:     tools/plots/stock_wave_harmonics.csv, Core/DSPAudio/wavetable.c.
Outputs:    Core/SampleRom/StockWaveRecipes.c; optional reference binary.
Accessors:  python3 tools/gen_stock_recipes.py [--emit-ref
            tools/dsp_test/frozen/stock_ref.bin].
Affiliates: StockWaveGen.c (the same arithmetic), test_stock_gen.c.
```

**Algorithm** (the device in C4.4 must match it to ±1 LSB):

1. **Coefficients:** for each CSV row of wave `w`, harmonic `k`,
   - `a[k] = 2·mag·cos(phase)`,
   - `b[k] = −2·mag·sin(phase)`.
2. **Quantise per wave:**
   - `scale = 32767 / max(|a|, |b|)` over k = 1..511;
   - `A = rint(a·scale)`, `B = rint(b·scale)` as int16.
   - Warn when the quantisation error exceeds −60 dB.
3. **Emulate each level** (length L, harmonic count h), point m:
   - `t = (k·m·4096 // L) & 4095`;
   - `c = −sine_table[t]` and `s = sine_table[(t + 1024) & 4095]` (the
     firmware `sine_table` stores −cos);
   - `total = Σ_k float32(A[k])·c + float32(B[k])·s`, accumulated in
     float32 in k order;
   - level scale = `32767 / max|total|`;
   - point = `clip(rint(total·scale), −32768, 32767)`.
4. **Write** `StockWaveRecipes.c`:
   - `const int16_t stockWaveRecipeA[5][511]`, `stockWaveRecipeB[5][511]`;
   - `const float stockWaveLevelScale[5][11]` (printed `%.9g`);
   - the CSV's SHA-256 in its header comment;
   - the S085 C4.3 contract block.
5. **Optionally** write the reference binary.

**Level geometry constants** (shared with C4.4/C4.5):

| Constant | Value |
|---|---|
| `LEVEL_LEN` | 1024, 1024, 1024, 1024, 1024, 512, 256, 128, 64, 16, 16 |
| `LEVEL_H` | 511, 511, 337, 168, 84, 42, 21, 10, 5, 2, 1 |
| `LEVEL_OFF` | 0, 1024, 2048, 3072, 4096, 5120, 5632, 5888, 6016, 6080, 6096 |

### C4.3 `Core/SampleRom/StockWaveRecipes.c` (new, generated)

- **Action:** **New file**, produced by C4.2, about 10.2 KB of `.rodata`.
- **Do not edit by hand.**
- **Contract block** (written by the script):

```c
/*
 * Stock-wave harmonic recipes (S085 C4.3) - GENERATED by
 * tools/gen_stock_recipes.py; do not edit.
 *
 * What:       per wave (sq3 sw3 sqa tra swa) the quantised cosine/sine
 *             coefficients of harmonics 1..511 and the peak scale of each of
 *             the 11 octave levels.
 * Why:        plan §4.3.4: the 61,120 B of tables are generated into sample
 *             flash at install from about 10 KB of program flash.
 * Inputs:     tools/plots/stock_wave_harmonics.csv (SHA-256 in this header).
 * Outputs:    read by StockWaveGen.c.
 * Accessors:  stockWaveGen_programAll().
 * Affiliates: tools/dsp_test/test_stock_gen.c, frozen/stock_ref.bin.
 */
```

### C4.4 `Core/SampleRom/StockWaveGen.h` / `.c` (new)

- **Compiled with:** `SRCS` (control code, `-O2`).

```c
/* StockWaveGen.h */
#ifndef STOCKWAVEGEN_H_
#define STOCKWAVEGEN_H_
#include <stdint.h>

/* Generated by tools/gen_stock_recipes.py (StockWaveRecipes.c). */
extern const int16_t stockWaveRecipeA[5][511];
extern const int16_t stockWaveRecipeB[5][511];
extern const float   stockWaveLevelScale[5][11];

/*
 * Program the whole stock block (S085 C4.4).
 *
 * What:       for every wave and level, point m = sum over k of
 *             A[k]*cos(t) + B[k]*sin(t), t = (k*m*4096/L) & 4095, read from
 *             sine_table (cos = -sine_table[t], sin = sine_table[t+1024]),
 *             times the level scale, rounded and saturated to int16; two
 *             points per programmed word at
 *             base + (wave*6112 + levelOffset + m)*2.
 * Why:        plan §4.3.4; the device output equals the script's reference
 *             within 1 LSB (float32 in the same order).
 * Inputs:     base (the erased stock block address).
 * Outputs:    61,120 B programmed; 0 or a sampleFlash status.
 * Cost:       about 8.4 M multiply-adds and 15,280 word programs, roughly
 *             0.4 s at 216 MHz, during the modal install only.
 * Accessors:  sampleInstall_writeStockBlock().
 * Affiliates: StockWaveRecipes.c, StockWaves.c (the same layout),
 *             sampleFlash_writeWord(), wavetable.c sine_table.
 */
int stockWaveGen_programAll(uint32_t base);

#endif /* STOCKWAVEGEN_H_ */
```

```c
/* StockWaveGen.c */
#include "StockWaveGen.h"
#include "StockWaves.h"
#include "sampleFlash.h"
#include "wavetable.h"

/* Level geometry (S085 C4.4): lengths and harmonic counts; offsets are
** stockWave_levelOffset[] in StockWaves.c. */
static const uint16_t stock_gen_length[STOCK_WAVE_LEVELS] = {
    1024u, 1024u, 1024u, 1024u, 1024u, 512u, 256u, 128u, 64u, 16u, 16u,
};
static const uint16_t stock_gen_harmonics[STOCK_WAVE_LEVELS] = {
    511u, 511u, 337u, 168u, 84u, 42u, 21u, 10u, 5u, 2u, 1u,
};

/*
 * One generated point (S085 C4.4).
 *
 * What:       the additive sum of C4.4's contract for (wave, level, m), in
 *             float32 and in k order (the script emulates exactly this).
 * Outputs:    the rounded, saturated int16 value.
 * Accessors:  stockWaveGen_programAll().
 */
static int32_t stockWaveGen_point(uint8_t wave, uint8_t level, uint32_t m)
{
    const uint32_t length = stock_gen_length[level];
    const uint16_t harmonics = stock_gen_harmonics[level];
    const int16_t *a = stockWaveRecipeA[wave];
    const int16_t *b = stockWaveRecipeB[wave];
    float sum = 0.0f;
    uint16_t k;

    for (k = 1u; k <= harmonics; k++) {
        const uint32_t t = (((uint32_t)k * m * 4096u) / length) & 4095u;
        const float c = -(float)sine_table[t];
        const float s = (float)sine_table[(t + 1024u) & 4095u];

        sum += (float)a[k - 1u] * c + (float)b[k - 1u] * s;
    }
    sum *= stockWaveLevelScale[wave][level];
    if (sum >= 32767.0f)
        return 32767;
    if (sum <= -32768.0f)
        return -32768;
    return (int32_t)(sum + ((sum >= 0.0f) ? 0.5f : -0.5f));
}

int stockWaveGen_programAll(uint32_t base)
{
    uint8_t wave;
    uint8_t level;

    for (wave = 0u; wave < STOCK_WAVE_COUNT; wave++) {
        for (level = 0u; level < STOCK_WAVE_LEVELS; level++) {
            const uint32_t length = stock_gen_length[level];
            const uint32_t origin = base +
                ((uint32_t)wave * STOCK_WAVE_POINTS_PER_WAVE +
                 stockWave_levelOffset[level]) * 2u;
            uint32_t m;

            for (m = 0u; m < length; m += 2u) {
                const int32_t lo = stockWaveGen_point(wave, level, m);
                const int32_t hi = stockWaveGen_point(wave, level, m + 1u);
                const uint32_t word = (uint32_t)(uint16_t)(int16_t)lo |
                    ((uint32_t)(uint16_t)(int16_t)hi << 16);
                const int rc = sampleFlash_writeWord(origin + m * 2u, word);

                if (rc != 0)
                    return rc;
            }
        }
    }
    return 0;
}
```

- **Rounding note:** the script rounds with `rint` (half to even); the device
  rounds half away from zero. Exact .5 ties are rare, and the ±1 LSB
  tolerance of `stock_gen` covers them.

### C4.5 `Core/DSPAudio/StockWaves.h` / `.c` (new)

- **Compiled with:** `DSP_SRCS` (`-Ofast`). `stockWave_level()` is block
  rate.

```c
/* StockWaves.h */
/*
 * Stock-wave reader (S085 C4.5).
 *
 * What:       resolves one level of one generated stock wave (wave 0..4 =
 *             sq3 sw3 sqa tra swa) into a table pointer, an index shift
 *             (index = phase >> shift) and a wrap mask; while the stock block
 *             is not valid (no install yet) it returns the 1,024-point
 *             fallback level: sq3/sqa -> recTable, sw3/swa -> sawTable,
 *             tra -> triTable.
 * Why:        plan §4.3.4: one block-rate selection keeps the render loops
 *             free of validity tests and keeps the fallback in one place.
 * Inputs:     wave, level 0..10.  Outputs: stock_level_t.
 * Accessors:  Oscillator.c calcStockWaveBlock()/calcStockWaveFmBlock(),
 *             OscillatorPwm.c (pq3/pw3).
 * Affiliates: StockWaveGen.c (same layout), SampleMemory.h
 *             (SAMPLE_STOCK_START_ADDRESS, sampleMemory_stockValid()).
 */
#ifndef STOCKWAVES_H_
#define STOCKWAVES_H_
#include <stdint.h>

#define STOCK_WAVE_COUNT            5u
#define STOCK_WAVE_LEVELS           11u
#define STOCK_WAVE_POINTS_PER_WAVE  6112u

typedef struct {
    const int16_t *tab;
    uint8_t        shift;   /* index = phase >> shift (22 for 1,024 points) */
    uint16_t       mask;    /* level length - 1 */
} stock_level_t;

/* Level offsets inside one wave (points) and phase shifts (32 - log2 L). */
extern const uint16_t stockWave_levelOffset[STOCK_WAVE_LEVELS];
extern const uint8_t  stockWave_levelShift[STOCK_WAVE_LEVELS];

stock_level_t stockWave_level(uint8_t wave, uint8_t level);

#endif /* STOCKWAVES_H_ */
```

```c
/* StockWaves.c */
#include "StockWaves.h"
#include "SampleMemory.h"
#include "wavetable.h"

const uint16_t stockWave_levelOffset[STOCK_WAVE_LEVELS] = {
    0u, 1024u, 2048u, 3072u, 4096u, 5120u, 5632u, 5888u, 6016u, 6080u, 6096u,
};
const uint8_t stockWave_levelShift[STOCK_WAVE_LEVELS] = {
    22u, 22u, 22u, 22u, 22u, 23u, 24u, 25u, 26u, 28u, 28u,
};

stock_level_t stockWave_level(uint8_t wave, uint8_t level)
{
    stock_level_t out;

    if (wave >= STOCK_WAVE_COUNT)
        wave = 0u;
    if (level >= STOCK_WAVE_LEVELS)
        level = (uint8_t)(STOCK_WAVE_LEVELS - 1u);
    if (sampleMemory_stockValid()) {
        out.tab = (const int16_t *)SAMPLE_FLASH_PTR(
            SAMPLE_STOCK_START_ADDRESS +
            ((uint32_t)wave * STOCK_WAVE_POINTS_PER_WAVE +
             stockWave_levelOffset[level]) * 2u);
        out.shift = stockWave_levelShift[level];
        out.mask = (uint16_t)((1u << (32u - out.shift)) - 1u);
        return out;
    }
    out.tab = (wave == 1u || wave == 4u) ? sawTable[level]
            : (wave == 3u)               ? triTable[level]
                                         : recTable[level];
    out.shift = 22u;
    out.mask = 1023u;
    return out;
}
```

- **Copy the header block** above the definition with the note
  "Implementation: block rate; the fallback keeps the 1,024-point table
  format so the render loop is identical."

---
## 6. WP5 — Oscillator data model, dispatch and per-sample slew

### C5.1 `Core/DSPAudio/Oscillator.h` — includes, wave ids, types, slew length

- **Where:** lines 40–55 (includes and the waveform `#define`s).
- **Action:**
  - **Add** `#include "OscillatorPwm.h"` and `#include "OscillatorWavetable.h"`
    after line 44. Both headers declare only their state structs and
    forward-declare `struct OscStruct`; they must never include
    `Oscillator.h`.
  - **Add** the block below after line 54 `#define CRASH 0x05`.
  - **Remove** line 55, `#define OSC_SAMPLE_START 0x06`. Every former
    reference is replaced in this WP or in C9.3.

```c
/*
 * Generated stock waves on the `wav` list (S085 C5.1).
 *
 * What:       wav values 6..10 play the five stock tables generated into
 *             sample flash (StockWaves.c); STOCK_WAVE_FIRST maps a wav value
 *             to its stock index (waveform - STOCK_WAVE_FIRST).
 *             OSC_STOCK_WAVE_COUNT is the length of the wav list.
 * Why:        plan §4.3.4. Values 0..5 keep their meaning; old kits whose wav
 *             was a user sample (6+) now play sq3..swa or clamp to swa (no
 *             migration, Q8).
 * Accessors:  Oscillator.c, OscillatorPwm.c, InstrumentManager.c,
 *             InstrumentCellFormat.c, modulationNode.c.
 * Affiliates: MenuText.h waveformNames[] (same order, count 11).
 */
#define SQ3                 0x06
#define SW3                 0x07
#define SQA                 0x08
#define TRA                 0x09
#define SWA                 0x0A
#define STOCK_WAVE_FIRST    SQ3
#define OSC_STOCK_WAVE_COUNT 11u

/*
 * Oscillator types (S085 C5.1).
 *
 * What:       WAV plays the wav list; PWM a computed pulse-width family
 *             (OscillatorPwm.c); SMP/SLP an installed sample one-shot/looped
 *             (OscillatorSample.c); WTB an installed wavetable
 *             (OscillatorWavetable.c). The wav row is each type's continuous
 *             control: stock wave, width, sample start, frame position.
 * Why:        plan §5 items 1-2.
 * Inputs:     the oscN_type rows (IM_SPECIAL_OSC_TYPE).
 * Outputs:    OscInfo.oscType; render and frequency dispatch.
 * Accessors:  Oscillator.c, the type renderers, OscSection.c,
 *             InstrumentManager.c, InstrumentCellFormat.c.
 * Affiliates: MenuText.h oscTypeNames[] (same order, count 5).
 */
#define OSC_TYPE_WAV    0u
#define OSC_TYPE_PWM    1u
#define OSC_TYPE_SMP    2u
#define OSC_TYPE_SLP    3u
#define OSC_TYPE_WTB    4u
#define OSC_TYPE_COUNT  5u

/*
 * Pitch slew length for run modes fre/itp (S085 C5.1).
 *
 * What:       132 samples (3 ms at 44,108 Hz) from the old to the new phase
 *             increment, linear.
 * Why:        plan §4.3.7: fre never resets phase; every slewing oscillator
 *             glides to the new pitch without a step.
 * Accessors:  osc_armSlew(), osc_setFreq().
 */
#define OSC_SLEW_SAMPLES 132u
```

### C5.2 `Core/DSPAudio/Oscillator.h` — `OscInfo` fields

- **Where:** the struct, lines 61–92.
- **Action:**
  - **Add** block A after line 69 `uint8_t tableOffset;`.
  - **Add** block B after line 79 `uint8_t waveInterpNext;`.
  - **Modify** line 85: `uint8_t sampleCacheLooped;` becomes
    `uint8_t sampleGainCode;` with block C. The loop flag is retired; the
    type now chooses the loop.

```c
	/*
	 * Type, source, slew and per-type state (S085 C5.2, block A).
	 *
	 * What:       oscType = OSC_TYPE_*; source = live (clamped) pwm family,
	 *             sample index or wavetable index; freqCacheType extends the
	 *             frequency cache key; phaseIncTarget/phaseIncStep/
	 *             slewSamplesLeft implement the per-sample pitch slew (every
	 *             renderer adds phaseIncStep to its increment every sample,
	 *             always); sampleStart/sampleEnd bound smp/slp playback in
	 *             frames; the union holds the active type's block state
	 *             (pwm: width parameters; wtb: bound table and frame ramp).
	 * Why:        plan §5 items 1-2; one union member is live at a time and
	 *             each type re-initialises its member (oscPwm_prepare(),
	 *             oscWavetable_bind()), so a type change never reads the other
	 *             type's bytes.
	 * Writers:    the IM_SPECIAL_OSC_* writers, osc_setFreq(), osc_armSlew(),
	 *             oscSample_bind(), oscPwm_prepare(), oscWavetable_bind() and
	 *             the wtb renderers.
	 * Readers:    every block renderer and osc_setFreq().
	 * Affiliates: the engine structs (runtime-slot union); the slot
	 *             _Static_asserts in InstrumentManager.c bound the growth.
	 */
	uint8_t		oscType;
	uint8_t		source;
	uint8_t		freqCacheType;
	uint16_t	slewSamplesLeft;
	uint32_t	phaseIncTarget;
	int32_t		phaseIncStep;
	uint32_t	sampleStart;
	uint32_t	sampleEnd;
	union {
		OscPwmState	pwm;
		OscWtbState	wtb;
	};
```

```c
	/*
	 * Requested wave/source bytes (S085 C5.2, block B).
	 *
	 * What:       the last retained byte written to the wav row (waveRequest)
	 *             and the src row (sourceRequest), before the clamp to the
	 *             current type's maximum. waveform/source hold the clamped
	 *             live values the renderers read.
	 * Why:        a type change re-derives waveform/source from these, so a
	 *             pwm width or sample start survives pwm -> wav -> pwm and the
	 *             load order (S084 P1 F2). Only retained writes record them
	 *             (menu, MIDI, load, Morph, automation); LFO overlays do not
	 *             (S085 D2).
	 * Writers:    the IM_SPECIAL_OSC_WAVE/_OSC_SOURCE writers (retained only).
	 * Readers:    the IM_SPECIAL_OSC_TYPE writer, oscSample_bind(),
	 *             oscWavetable_bind().
	 */
	uint8_t		waveRequest;
	uint8_t		sourceRequest;
```

```c
	/*
	 * Catalogue gain code of the bound sample (S085 C5.2, block C).
	 *
	 * What:       the gain byte of the bound sample's catalogue entry, 0 when
	 *             nothing is bound; replaces the retired loop flag.
	 * Writers:    oscSample_bind().  Readers: oscSample_render(_Fm).
	 */
	uint8_t		sampleGainCode;
```

**Static assert:**
- **Where:** after the struct.
- **Action:** **Add**
  `_Static_assert(sizeof(OscWtbState) <= sizeof(OscPwmState), "wtb state must fit the pwm union slot");`.
- **Why:** the `wtb` state (24 B) must not grow `OscInfo` beyond the `pwm`
  state (28 B).

### C5.3 `Core/DSPAudio/Oscillator.h` — declarations

- **Where:** the prototypes, lines 96–149.
- **Action:**
  - **Remove** `calcUserSampleOscFmBlock()` and `calcUserSampleOscBlock()`
    (lines 140–142); they move to `OscillatorSample.c` as
    `oscSample_renderFm()` / `oscSample_render()`.
  - **Add** the declarations below before `#endif`.

```c
/*
 * Generated stock-wave renderers (S085 C5.3).
 *
 * What:       read level tableOffset of stock wave (waveform - SQ3) with
 *             linear interpolation and the per-sample slew; the FM variant
 *             adds (mod x fmMod) << 19 to the read phase, the calcFmBlock
 *             depth. Before the first install they render the fallback table
 *             through the same loop.
 * Why:        plan §4.3.4; same cost class as calcWavetableOscBlock().
 * Inputs:     osc, (modBuffer), buf, size, gain.
 * Outputs:    buf; osc->phase, osc->phaseInc.
 * Accessors:  calcNextOscSampleBlock(), calcNextOscSampleFmBlock().
 * Affiliates: stockWave_level(), osc_calcWavetableFreqValue(). ITCM.
 */
void calcStockWaveBlock(OscInfo* osc, int16_t* buf, const uint8_t size,
                        const float gain);
void calcStockWaveFmBlock(OscInfo* osc, int16_t* modBuffer, int16_t* buf,
                          uint8_t size, const float gain);

/*
 * Arm the pitch slew (S085 C5.3).
 *
 * What:       sets slewSamplesLeft = OSC_SLEW_SAMPLES; the next osc_setFreq()
 *             computes the per-sample step from the current phaseInc toward
 *             the new target.
 * Why:        plan §4.3.7: fre glides every oscillator; itp glides the
 *             modulators.
 * Inputs:     osc.  Outputs: osc->slewSamplesLeft.
 * Accessors:  oscSection_beginTrigger(), oscSection_claimPending().
 * Affiliates: osc_setFreq(), every renderer's phaseIncStep.
 */
void osc_armSlew(OscInfo* osc);

/*
 * Band-limited octave index for a frequency (S085 C5.3).
 *
 * What:       the S073 Step 6 edge count (0..10), exposed.
 * Why:        the pwm families select the same band-limited level as the wav
 *             tables, at frequency x warp compression.
 * Accessors:  oscPwm_prepare().  Affiliates: freqToTableIndex().
 */
uint8_t osc_freqToTableIndex(float f);

/*
 * Whether a wav value is a band-limited table wave (S085 C5.3).
 *
 * What:       1 for TRI, SAW, REC and SQ3..SWA; 0 for SINE, NOISE, CRASH.
 * Why:        the engines' trigger phase offset ((0xff << 20) x offset)
 *             applies to every table wave, including the new ones.
 * Accessors:  the four engines' trigger functions.
 */
static inline uint8_t osc_isTableWave(uint8_t waveform)
{
    return (uint8_t)((waveform >= TRI && waveform <= REC) ||
                     (waveform >= SQ3 && waveform <= SWA));
}
```

### C5.4 `Core/DSPAudio/Oscillator.c` — includes; the sample cache is removed

- **Includes:** after line 54 `#include "modulationNode.h"`, **add**:
  `OscillatorSample.h`, `OscillatorPwm.h`, `OscillatorWavetable.h`,
  `StockWaves.h` and `<string.h>`.
- **Remove:** lines 138–158, `osc_updateSampleCache()`. It is replaced by
  `oscSample_bind()` (C6.2).

### C5.5 `Oscillator.c` — waveform interpolation applies to `wav` only

- **Where:** lines 160–180, `osc_waveInterpActive()`.
- **Action:** **Modify**:
  - line 162 becomes
    `const uint8_t maxWave = (uint8_t)(OSC_STOCK_WAVE_COUNT - 1u);`;
  - **add** as the first test:
    `if (osc->oscType != OSC_TYPE_WAV) return 0u;`.
- **Comment** (add above the function):

```c
/*
 * Waveform interpolation eligibility (S085 C5.5).
 *
 * What:       1 when this block's modulation granted the oscillator a blend
 *             pair (generation match, 0 < frac < 1, next > base, both inside
 *             the wav list) and the oscillator is wav type.
 * Why:        plan §4.3.5: samples left the wav list, and pwm/smp/slp/wtb
 *             controls are continuous by design; only wav blends neighbours.
 *             The budget is granted by OscRenderPool (C9.3).
 * Accessors:  the two block dispatchers and the scalar dispatchers.
 */
```

- **Also modify** the block interpolators: lines 211–236
  `calcPeriodicInterpBlock()` and lines 254–281 `calcPeriodicInterpFmBlock()`.
  The final phase write is replaced; the two renders now include the slew:
  - line 235 `osc->phase = startPhase + (osc->phaseInc * (uint32_t)size);`
    becomes `osc->phase = oscA.phase; osc->phaseInc = oscA.phaseInc;`;
  - line 279, the same in the FM variant;
  - `startPhase` becomes unused there; remove it.

### C5.6 `Oscillator.c` — per-sample slew in every block renderer

**Where:** these loops.

| Function | Lines |
|---|---|
| `calcSineBlock` | 299–322 |
| `calcFmSineBlock` | 346–368 |
| `calcFmBlock` | 391–416 |
| `calcNoiseBlock` | 524–541 |
| `calcWavetableOscBlock` | 659–680 |
| `calcSampleOscFmBlock` | 707–732 |
| `calcSampleOscBlock` | 867–890 |

**Action:** **Modify.** Each loop keeps a local increment, advances the phase
by it, and then adds the step:
- before the loop: `uint32_t inc = osc->phaseInc; const int32_t step = osc->phaseIncStep;`;
- inside the loop, `osc->phase = oscPhase + osc->phaseInc;` becomes
  `osc->phase = oscPhase + inc; inc += (uint32_t)step;`;
- after the loop: `osc->phaseInc = inc;`.

`calcNoiseBlock()` (`osc->phase += osc->phaseInc;`) changes the same way:
`osc->phase += inc; inc += (uint32_t)step;`, with the wrap test against the
pre-add phase unchanged.

**Comment** (once, above `calcSineBlock`; the others cite
`/* S085 C5.6: per-sample slew. */`):

```c
/*
 * Per-sample pitch slew in every block renderer (S085 C5.6).
 *
 * What:       the loop advances phase by a local increment and adds
 *             osc->phaseIncStep to that increment every sample; the final
 *             increment is written back to osc->phaseInc.
 * Why:        run modes fre/itp glide from the old to the new pitch over
 *             OSC_SLEW_SAMPLES without per-block steps (plan §4.3.7). The add
 *             always runs, with step 0 when no slew is active, so the cost
 *             never depends on the run mode (constant CPU). In rst the step is
 *             always 0, so the output is bit-identical to the pre-S085 loop
 *             (proved by osc_s0, C17.5).
 * Inputs:     osc->phaseInc, osc->phaseIncStep (from osc_setFreq()).
 * Outputs:    osc->phase, osc->phaseInc.
 * Accessors:  the dispatchers; ITCM.
 * Affiliates: osc_setFreq(), osc_armSlew(), the type renderers (same rule).
 */
```

### C5.7 `Oscillator.c` — block dispatch by type

**Where:**
- lines 442–481, `calcNextOscSampleFmBlock()`;
- lines 577–616, `calcNextOscSampleBlock()`.

**Action:** **Modify.**
- **Add** the type dispatch at the top of each, before the
  `osc_waveInterpActive()` test.
- **Add** the stock cases to each `switch(osc->waveform)`.
- **Modify** the `default` case to render silence:
  `memset(buf, 0, (size_t)size * sizeof(int16_t));`. It replaces the
  user-sample call and the out-of-range return. A wav value outside the list
  cannot be reached, because the writer clamps it.

```c
	/*
	 * Render one oscillator block by type (S085 C5.7).
	 *
	 * What:       pwm, smp/slp and wtb go to their own renderers; wav keeps
	 *             the waveform switch (now with the generated stock waves) and
	 *             waveform interpolation; a wav value outside the list
	 *             renders silence.
	 * Why:        plan §5 item 2: each type keeps its own specialised hot
	 *             loop (no per-sample type test); the type test is per block.
	 * Inputs:     osc, (modBuffer), buf, size, gain.
	 * Outputs:    buf; the oscillator's phase state advanced.
	 * Accessors:  OscSection.c (all engine oscillator renders).
	 * Affiliates: oscPwm_render*(), oscSample_render*(),
	 *             oscWavetable_render*(), calcStockWave*Block().
	 */
	switch (osc->oscType) {
	case OSC_TYPE_PWM:
		oscPwm_render(osc, buf, size, gain);                    /* FM: oscPwm_renderFm(osc, modBuffer, buf, size, gain) */
		return;
	case OSC_TYPE_SMP:
	case OSC_TYPE_SLP:
		oscSample_render(osc, buf, size, gain);                 /* FM: oscSample_renderFm(...) */
		return;
	case OSC_TYPE_WTB:
		oscWavetable_render(osc, buf, size, gain);              /* FM: oscWavetable_renderFm(...) */
		return;
	default:
		break;
	}
	/* ... existing osc_waveInterpActive() test and waveform switch ... */
		case SQ3:
		case SW3:
		case SQA:
		case TRA:
		case SWA:
			calcStockWaveBlock(osc, buf, size, gain);           /* FM: calcStockWaveFmBlock(osc, modBuffer, buf, size, gain) */
			break;
```

In `calcNextOscSampleBlock()`, the current lines 610–614
(`default://sample playback … calcUserSampleOscBlock(…)`) become:

```c
		default:
			memset(buf, 0, (size_t)size * sizeof(int16_t));
			break;
```

In `calcNextOscSampleFmBlock()`, lines 476–478 (the default case calling
`calcUserSampleOscFmBlock`) change the same way.

### C5.8 `Oscillator.c` — scalar dispatchers

- **Where:**
  - lines 483–522, `calcNextOscSampleFm()`;
  - lines 618–657, `calcNextOscSample()`.
- **Fact:** neither has a caller outside `Oscillator.c` (checked: only the
  scalar interpolation path uses them).
- **Action:** **Add** as the first statement of each:

```c
	/* S085 C5.8: non-wav types render through their block renderer (one
	** sample); the scalar path has no external caller and is kept only for
	** the scalar interpolation helpers. */
	if (osc->oscType != OSC_TYPE_WAV) {
		int16_t out = 0;

		calcNextOscSampleBlock(osc, &out, 1u, 1.0f);
		osc->output = out;
		return out;
	}
```

- In `calcNextOscSampleFm()`, use `calcNextOscSampleFmBlock(osc, &mod, &out,
  1u, 1.0f)` with `int16_t mod = modOsc->output;`.
- **Add** stock cases that call the block renderer the same way:
  `case SQ3 … SWA: { int16_t out = 0; calcStockWaveBlock(osc, &out, 1u, 1.0f); osc->output = out; return out; }`.

### C5.9 `Oscillator.c` — stock-wave renderers

- **Where:** after line 704, the end of `calcWavetableOsc()`.
- **Action:** **Add** both functions with the C5.3 block.

```c
INITCM void calcStockWaveBlock(OscInfo* osc, int16_t* buf, const uint8_t size,
                               const float gain)
{
	const stock_level_t lv = stockWave_level(
		(uint8_t)(osc->waveform - STOCK_WAVE_FIRST), osc->tableOffset);
	const uint8_t lowShift = (uint8_t)(32u - lv.shift);
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t oscPhase = osc->phase;
		const uint32_t index = oscPhase >> lv.shift;
		const uint32_t next = (index + 1u) & lv.mask;
		const float frac =
			(float)(oscPhase << lowShift) * 2.3283064365386963e-10f; /* 2^-32 */
		int16_t oscOut = lv.tab[index];

		oscOut = (int16_t)((float)oscOut +
			frac * (float)((int32_t)lv.tab[next] - (int32_t)oscOut));
		buf[i] = oscOut * gain;
		/* S085 C5.6: per-sample slew. */
		osc->phase = oscPhase + inc;
		inc += (uint32_t)step;
	}
	osc->phaseInc = inc;
}

INITCM void calcStockWaveFmBlock(OscInfo* osc, int16_t* modBuffer,
                                 int16_t* buf, uint8_t size, const float gain)
{
	const stock_level_t lv = stockWave_level(
		(uint8_t)(osc->waveform - STOCK_WAVE_FIRST), osc->tableOffset);
	const uint8_t lowShift = (uint8_t)(32u - lv.shift);
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t index32 = osc->phase +
			(((uint32_t)(modBuffer[i] * osc->fmMod)) << 19);
		const uint32_t index = index32 >> lv.shift;
		const uint32_t next = (index + 1u) & lv.mask;
		const float frac =
			(float)(index32 << lowShift) * 2.3283064365386963e-10f;
		int16_t oscOut = lv.tab[index];

		oscOut = (int16_t)((float)oscOut +
			frac * (float)((int32_t)lv.tab[next] - (int32_t)oscOut));
		osc->output = oscOut;
		buf[i] = oscOut * gain;
		/* S085 C5.6: per-sample slew. */
		osc->phase += inc;
		inc += (uint32_t)step;
	}
	osc->phaseInc = inc;
}
```

- **FM depth:** `(uint32_t)(modBuffer[i]*osc->fmMod) << 19` is exactly the
  `calcFmBlock()` expression, so FM depth through a stock wave equals the
  other tables.
- **Index range:** `index` is always within the level, because the shift
  maps the full 32-bit phase onto L points.

### C5.10 `Oscillator.c` — the sample code leaves this file

- **Remove:**
  - lines 758–811, `calcUserSampleOscFmBlock()`;
  - lines 813–865, `calcUserSampleOscBlock()`.
- **Remove:** lines 938–941, `osc_calcUserSampleFreqValue()`.
- **Keep:**
  - the CRASH sample functions;
  - `freq2PhaseIncr32767()`, used by CRASH.

### C5.11 `Oscillator.c` — frequency helpers write the target

- **Where:** lines 915–936. The three helpers are
  `osc_calcSineFreqValue`, `osc_calcWavetableFreqValue` and
  `osc_calcSampleFreqValue`.
- **Action:** **Modify** `osc->phaseInc = …` to `osc->phaseIncTarget = …` in
  each. The table octave (`tableOffset`) is still written directly; the slew
  moves only the increment.

### C5.12 `Oscillator.c` — `osc_setFreq()` per type, cache key, slew; new helpers

- **Where:** lines 943–992.
- **Action:**
  - **Modify** `osc_setFreq()` (replace its body).
  - **Add** `osc_freqToTableIndex()` and `osc_armSlew()` after it.

```c
/*
 * Update an oscillator's frequency state for the next block (S085 C5.12).
 *
 * What:       computes the phase-increment target for the oscillator's type
 *             (wav tables/sine/noise/crash, pwm via oscPwm_prepare(), Q20.12
 *             samples, wtb at 2^32 per cycle), caches it by (frequency,
 *             waveform, type key), then advances the slew: with no slew armed
 *             phaseInc = target and step = 0; with a slew armed the per-sample
 *             step reaches the target after the remaining slew samples.
 *             The pwm cache key carries sampleMemory_stockValid() in bit 7,
 *             so an install that makes the stock block valid re-derives the
 *             pq3/pw3 natural split once (S084 P2 F10).
 * Why:        plan §5 items 1-3. The cache must never hide the slew
 *             bookkeeping, so the slew part runs every call. The step
 *             division always runs (divisor clamped to 1), so the block cost
 *             does not depend on the run mode.
 * Inputs:     freq, pitchMod, modNodeValue, oscType, waveform, source, slew.
 * Outputs:    phaseIncTarget, phaseInc, phaseIncStep, slewSamplesLeft,
 *             tableOffset, pwm state.
 * Accessors:  each engine's async update, once per block per oscillator.
 * Affiliates: osc_armSlew(), oscPwm_prepare(), oscSample_phaseIncForFreq().
 *             ITCM.
 */
INITCM void osc_setFreq(OscInfo* osc)
{
	const float currentFreq = osc->freq * osc->pitchMod * osc->modNodeValue;
	const uint8_t cacheType = (uint8_t)(osc->oscType |
		((osc->oscType == OSC_TYPE_PWM && sampleMemory_stockValid())
			? 0x80u : 0u));

	if (!(osc->freqCacheValid &&
	      osc->freqCacheType == cacheType &&
	      osc->freqCacheWaveform == osc->waveform &&
	      osc->freqCacheValue == currentFreq)) {
		switch (osc->oscType) {
		case OSC_TYPE_PWM:
			oscPwm_prepare(osc, currentFreq);
			break;
		case OSC_TYPE_SMP:
		case OSC_TYPE_SLP:
			osc->phaseIncTarget = oscSample_phaseIncForFreq(currentFreq);
			break;
		case OSC_TYPE_WTB:
			osc->phaseIncTarget = freq2PhaseIncr1024(currentFreq);
			break;
		case OSC_TYPE_WAV:
		default:
			switch (osc->waveform) {
			case SINE:
				osc_calcSineFreqValue(osc, currentFreq);
				break;
			case NOISE:
				osc_calcNoiseFreqValue(osc, currentFreq);
				break;
			case CRASH:
				osc_calcSampleFreqValue(osc, currentFreq);
				break;
			default: /* TRI SAW REC and SQ3..SWA: table octave */
				osc_calcWavetableFreqValue(osc, currentFreq);
				break;
			}
			break;
		}
		osc->freqCacheValue = currentFreq;
		osc->freqCacheWaveform = osc->waveform;
		osc->freqCacheType = cacheType;
		osc->freqCacheValid = 1u;
	}
	{
		const uint16_t left = osc->slewSamplesLeft;
		const int32_t step = ((int32_t)osc->phaseIncTarget -
		                      (int32_t)osc->phaseInc) /
		                     (int32_t)(left ? left : 1u);

		if (left == 0u) {
			osc->phaseInc = osc->phaseIncTarget;
			osc->phaseIncStep = 0;
		} else {
			osc->phaseIncStep = step;
			osc->slewSamplesLeft = (left > OUTPUT_DMA_SIZE)
				? (uint16_t)(left - OUTPUT_DMA_SIZE) : 0u;
		}
	}
}

uint8_t osc_freqToTableIndex(float f)
{
	return freqToTableIndex(f);
}

void osc_armSlew(OscInfo* osc)
{
	if (osc)
		osc->slewSamplesLeft = OSC_SLEW_SAMPLES;
}
```

- **Increment range:** the signed difference fits `int32_t`. The largest
  increment is a 1,024-point table at about 20 kHz:
  1024 × 20000 / 44108 × 2²² ≈ 1.95 × 10⁹ < 2³¹.
- **Slew error:** truncation leaves at most `left` LSB of increment error,
  removed by the snap on the first block after the slew ends.

### C5.13 `config.h` — the render pool replaces the interpolation limit

- **Where:** lines 876–881, the comment and
  `#define OSC_WAVE_INTERP_MAX_ACTIVE 2u`.
- **Action:** **Modify.**

```c
/*
 * Shared oscillator render pool (S085 C5.13).
 *
 * What:       extra oscillator renders allowed per 32-frame block, shared by
 *             run-mode itp carrier crossfades and waveform interpolation.
 *             One unit = one extra oscillator render. Claim order (C9.4):
 *             ongoing itp crossfades, then waveform interpolation, then new
 *             itp crossfades, lowest slot first.
 * Why:        plan Q14: the pool stays at 2 units (today's interpolation
 *             limit); itp crossfades only the carrier (1 unit per voice). The
 *             pool is budgeted as always full (constant-CPU rule).
 * Accessors:  OscRenderPool.c.
 */
#define OSC_RENDER_POOL_UNITS 2u
```

---
## 7. WP6 — `smp`/`slp`: `OscillatorSample.h/.c` (new)

- **Compiled with:** `DSP_SRCS` (`-Ofast`).
- **ITCM:** the renderers.

### C6.1 `Core/DSPAudio/OscillatorSample.h` (new file)

```c
/*
 * OscillatorSample — installed-sample playback for the smp/slp types
 * (S085 C6.1).
 *
 * What:       Q20.12 phase (index = phase >> 12): every installable sample
 *             (up to 1,048,575 frames) plays fully. smp plays once from the
 *             start position and outputs 0 after its last frame; slp loops
 *             [start, end). The start position is the wav row: start =
 *             wav x frames / 128 (linear, Q11). The source row selects the
 *             sample. The catalogue gain code normalises the level.
 * Why:        plan §5 item 2, Q10 (smp ends at 0), Q11; the old phase >> 17
 *             index addressed only 32,768 frames (0.74 s).
 * Inputs:     OscInfo (oscType, sourceRequest, waveform, phase, phaseInc,
 *             phaseIncStep, fmMod).
 * Outputs:    rendered blocks; OscInfo sample cache and bounds.
 * Accessors:  Oscillator.c dispatch and osc_setFreq(), the engines'
 *             triggers (restart), OscSection.c (sync restart, end test),
 *             InstrumentManager.c writers (bind).
 * Affiliates: SampleMemory.h (catalogue v2, gain code).
 */
#ifndef OSCILLATORSAMPLE_H_
#define OSCILLATORSAMPLE_H_

#include <stdint.h>
#include "config.h"

#define OSC_SAMPLE_PHASE_SHIFT 12u

struct OscStruct;

/*
 * Bind the selected sample (S085 C6.1).
 *
 * What:       live index = min(sourceRequest, count - 1) (0 samples: silent,
 *             source 0); refreshes the cached pointer, frames and gain code
 *             when the index or the catalogue generation changed; recomputes
 *             sampleStart = (waveform x frames) >> 7 and sampleEnd = frames.
 * Why:        a smaller reinstall never moves the stored selection for good
 *             (S084 P1 F2); a reinstall never leaves a dangling pointer. It is
 *             a compare per block when nothing changed.
 * Inputs:     osc.  Outputs: source, sampleCache*, sampleGainCode,
 *             sampleStart, sampleEnd.
 * Accessors:  oscSample_render*() (every block), the IM type/source/wave
 *             writers.
 * Affiliates: sampleMemory_getSampleInfo(), sampleMemory_getGeneration().
 */
void oscSample_bind(struct OscStruct* osc);

/*
 * Restart at the start position (S085 C6.1).
 *
 * What:       phase = sampleStart << OSC_SAMPLE_PHASE_SHIFT.
 * Accessors:  the engines' trigger (rst, itp carrier), OscSection.c (snc
 *             slave reset).
 */
void oscSample_restart(struct OscStruct* osc);

/*
 * Whether a one-shot sample has played out (S085 C6.1).
 *
 * What:       1 for an smp oscillator whose read index has reached
 *             sampleEnd - 1 (it then outputs 0); 0 otherwise, and always 0
 *             for slp and other types.
 * Why:        Q10: an snc slave stops resetting once its one-shot master has
 *             ended (S085 D6); the same test drives the 0 output.
 * Accessors:  OscSection.c snc, oscSample_render*().
 */
uint8_t oscSample_isFinished(const struct OscStruct* osc);

/*
 * Phase increment for a playback frequency (S085 C6.1).
 *
 * What:       f x 2^22 / REAL_FS: 1,024 x f / FS frames per sample in Q20.12,
 *             the same playback ratio as the former freq2PhaseIncr32767().
 * Accessors:  osc_setFreq().
 */
uint32_t oscSample_phaseIncForFreq(float f);

/*
 * Render one sample block (S085 C6.1).
 *
 * What:       per sample: index clamp, loop wrap (slp) or end hold (smp), two
 *             reads, linear interpolation, gain; the per-sample slew. The FM
 *             variant adds (mod x fmMod) << 9 to the read phase (Q20.12; the
 *             same depth as the former << 14 at >> 17).
 * Why:        constant cost per sample: the wrap and end tests always run;
 *             the catalogue gain is folded into the block gain once.
 * Inputs:     osc, (modBuffer), buf, size, gain.
 * Outputs:    buf; osc->phase, osc->phaseInc.
 * Accessors:  Oscillator.c type dispatch.
 */
void oscSample_render(struct OscStruct* osc, int16_t* buf, uint8_t size,
                      float gain);
void oscSample_renderFm(struct OscStruct* osc, int16_t* modBuffer,
                        int16_t* buf, uint8_t size, float gain);

#endif /* OSCILLATORSAMPLE_H_ */
```

### C6.2 `Core/DSPAudio/OscillatorSample.c` (new file)

```c
#include "Oscillator.h"
#include "OscillatorSample.h"
#include "SampleMemory.h"
#include "config.h"

#define OSC_SAMPLE_FRAC_MASK  ((1u << OSC_SAMPLE_PHASE_SHIFT) - 1u)
#define OSC_SAMPLE_FRAC_SCALE (1.0f / (float)(1u << OSC_SAMPLE_PHASE_SHIFT))

void oscSample_bind(struct OscStruct* osc)
{
	uint8_t count;
	uint8_t index;
	uint32_t generation;

	if (!osc || (osc->oscType != OSC_TYPE_SMP && osc->oscType != OSC_TYPE_SLP))
		return;
	count = sampleMemory_getNumSamples();
	if (count == 0u) {
		osc->source = 0u;
		osc->sampleCacheData = 0;
		osc->sampleCacheSize = 0u;
		osc->sampleStart = 0u;
		osc->sampleEnd = 0u;
		osc->sampleGainCode = 0u;
		return;
	}
	index = (osc->sourceRequest >= count) ? (uint8_t)(count - 1u)
	                                      : osc->sourceRequest;
	osc->source = index;
	generation = sampleMemory_getGeneration();
	if (!osc->sampleCacheData || osc->sampleCacheIndex != index ||
	    osc->sampleCacheGeneration != generation ||
	    osc->sampleCacheSize == 0u) {
		const SampleInfo info = sampleMemory_getSampleInfo(index);

		osc->sampleCacheIndex = index;
		osc->sampleCacheGeneration = generation;
		osc->sampleCacheSize = info.size;
		osc->sampleCacheData = (const int16_t*)SAMPLE_FLASH_PTR(info.offset);
		osc->sampleGainCode = info.gain_code;
	}
	osc->sampleStart = ((uint32_t)osc->waveform * osc->sampleCacheSize) >> 7;
	if (osc->sampleStart >= osc->sampleCacheSize)
		osc->sampleStart = 0u;
	osc->sampleEnd = osc->sampleCacheSize;
}

void oscSample_restart(struct OscStruct* osc)
{
	if (osc)
		osc->phase = osc->sampleStart << OSC_SAMPLE_PHASE_SHIFT;
}

uint8_t oscSample_isFinished(const struct OscStruct* osc)
{
	return (uint8_t)(osc && osc->oscType == OSC_TYPE_SMP &&
	                 osc->sampleEnd != 0u &&
	                 (osc->phase >> OSC_SAMPLE_PHASE_SHIFT) + 1u >=
	                     osc->sampleEnd);
}

uint32_t oscSample_phaseIncForFreq(float f)
{
	return (uint32_t)((f * 4194304.0f) / REAL_FS);
}

/*
 * One block, shared by both entry points (S085 C6.2).
 *
 * What:       the C6.1 render contract; modBuffer NULL = no FM.
 * Why:        one body keeps the two loops identical; the FM test is
 *             hoisted by the compiler (two inlined copies, ITCM).
 */
static inline void oscSample_block(struct OscStruct* osc,
                                   const int16_t* modBuffer, int16_t* buf,
                                   uint8_t size, float gain)
{
	const int16_t* data;
	uint32_t start;
	uint32_t end;
	uint32_t phase;
	uint32_t inc;
	int32_t step;
	uint8_t looped;
	uint8_t i;

	oscSample_bind(osc);
	data = osc->sampleCacheData;
	if (!data || osc->sampleCacheSize == 0u) {
		for (i = 0u; i < size; i++)
			buf[i] = 0;
		return;
	}
	start = osc->sampleStart;
	end = osc->sampleEnd;
	gain *= sampleMemory_gainFromCode(osc->sampleGainCode);
	looped = (uint8_t)(osc->oscType == OSC_TYPE_SLP);
	phase = osc->phase;
	inc = osc->phaseInc;
	step = osc->phaseIncStep;
	for (i = 0u; i < size; i++) {
		const uint32_t readPhase = modBuffer
			? phase + (uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 9)
			: phase;
		uint32_t index = readPhase >> OSC_SAMPLE_PHASE_SHIFT;
		uint32_t nextIndex;
		uint8_t done;
		int32_t a;
		int32_t out;

		if (index < start)
			index = start;
		if (index >= end) {
			if (looped) {
				uint32_t rel = index - start;
				const uint32_t len = end - start;

				if (rel >= len)
					rel -= len;
				if (rel >= len)
					rel %= len;          /* only for deep FM jumps */
				index = start + rel;
			} else {
				index = end - 1u;
			}
		}
		done = (uint8_t)(!looped && index + 1u >= end);
		nextIndex = (index + 1u >= end) ? (looped ? start : index) : index + 1u;
		a = data[index];
		out = a + (int32_t)((float)(readPhase & OSC_SAMPLE_FRAC_MASK) *
		                    OSC_SAMPLE_FRAC_SCALE *
		                    (float)((int32_t)data[nextIndex] - a));
		/* Q10: smp outputs 0 once played out (a select, constant cost). */
		buf[i] = (int16_t)((float)out * (done ? 0.0f : gain));
		/* S085 C5.6: per-sample slew. */
		if (looped) {
			uint32_t next = phase + inc;

			if (next >= (end << OSC_SAMPLE_PHASE_SHIFT))
				next -= (end - start) << OSC_SAMPLE_PHASE_SHIFT;
			phase = next;
		} else if (!done && (phase >> OSC_SAMPLE_PHASE_SHIFT) + 1u < end) {
			phase += inc;
		} else {
			phase = (end - 1u) << OSC_SAMPLE_PHASE_SHIFT;
		}
		inc += (uint32_t)step;
	}
	osc->phase = phase;
	osc->phaseInc = inc;
}

INITCM void oscSample_render(struct OscStruct* osc, int16_t* buf,
                             uint8_t size, float gain)
{
	if (osc && buf)
		oscSample_block(osc, 0, buf, size, gain);
}

INITCM void oscSample_renderFm(struct OscStruct* osc, int16_t* modBuffer,
                               int16_t* buf, uint8_t size, float gain)
{
	if (osc && buf && modBuffer)
		oscSample_block(osc, modBuffer, buf, size, gain);
}
```

- **Copy** the C6.1 blocks above each definition. For `oscSample_bind()` add
  the note "re-homed from the removed Oscillator.c osc_updateSampleCache()".
- **Overflow:** `end << 12` cannot overflow. The item region holds at most
  621,728 frames (1,243,456 B / 2), and 621,728 × 4,096 < 2³².

---

## 8. WP7 — `pwm`: `OscillatorPwm.h/.c` (new)

- **Compiled with:** `DSP_SRCS`.
- **ITCM:** the renderers.

### C7.1 `Core/DSPAudio/OscillatorPwm.h` (new file)

```c
/*
 * OscillatorPwm — computed pulse-width families for the pwm type (S085 C7.1).
 *
 * What:       six families selected by src; the wav row is the width control
 *             c = wav / 127:
 *             prc  pulse from two band-limited saws, width 50 % -> 4 %;
 *             psw  sum of two saws (pulse-saw), d = c / 2;
 *             ptr  triangle with a warped phase (peak 50 % -> 3 %);
 *             pq3  the sq3 stock wave, phase warped around its edge (0.78);
 *             pw3  the sw3 stock wave, warped around its reset (0.42);
 *             swp  sweep sine -> tri -> saw -> square -> narrowing pulse.
 *             Nothing is stored; every family reads the band-limited tables
 *             (sawTable, triTable, sine_table) or the stock block.
 * Why:        plan §5 item 2; width-dependent constants are computed once per
 *             block (oscPwm_prepare()), so the per-sample loops have no
 *             division. prc at c = 0 equals Rec (S085 D4, measured).
 * Inputs:     OscInfo (source, waveform, phase, phaseInc, phaseIncStep, fmMod,
 *             tableOffset, pwm).
 * Outputs:    rendered blocks; OscPwmState per block.
 * Accessors:  Oscillator.c dispatch and osc_setFreq().
 * Affiliates: wavetable.c, StockWaves.c, tools/plots/plot_pwm.py
 *             (reference shapes).
 */
#ifndef OSCILLATORPWM_H_
#define OSCILLATORPWM_H_

#include <stdint.h>

#define OSC_PWM_PRC 0u
#define OSC_PWM_PSW 1u
#define OSC_PWM_PTR 2u
#define OSC_PWM_PQ3 3u
#define OSC_PWM_PW3 4u
#define OSC_PWM_SWP 5u
#define OSC_PWM_FAMILY_COUNT 6u

/*
 * Per-block pwm state (S085 C7.1).
 *
 * What:       shift = second-read offset (prc/psw/swp square, fraction of a
 *             2^32 cycle); split = warp split in phase units; scale = output
 *             gain (peak 1); warpA/warpB = the two warp slopes; mix = swp
 *             segment blend, or the natural split s0 for the warped families;
 *             seg = swp segment 0..3.
 * Why:        computed once per block, read every sample.
 * Writers:    oscPwm_prepare().  Readers: the pwm renderers.
 * Accessors:  OscillatorPwm.c; lives in the OscInfo union.
 */
typedef struct {
    uint32_t shift;
    uint32_t split;
    float    scale;
    float    warpA;
    float    warpB;
    float    mix;
    uint8_t  seg;
} OscPwmState;

struct OscStruct;

/*
 * Compute one block's width state (S085 C7.1).
 *
 * What:       sets phaseIncTarget (1,024-point format, 2^32 per cycle),
 *             tableOffset for freqHz x compression (compression =
 *             max(s0/w, (1-s0)/(1-w)) for the warped families, else 1), and
 *             every OscPwmState field from c = waveform / 127.
 * Why:        the divisions happen here, at most once per block.
 * Inputs:     osc (source, waveform), freqHz.
 * Outputs:    phaseIncTarget, tableOffset, pwm.
 * Accessors:  osc_setFreq() (on a cache miss).
 * Affiliates: osc_freqToTableIndex(), sampleMemory_stockValid().
 */
void oscPwm_prepare(struct OscStruct* osc, float freqHz);

/*
 * Render one pwm block (S085 C7.1).
 *
 * What:       prc/psw: two interpolated reads; ptr/pq3/pw3: one warped read;
 *             swp: two or three reads and a blend (segment 2 is the worst
 *             case and is the one probed); the per-sample slew. The FM
 *             variant adds (mod x fmMod) << 19 to the read phase.
 * Inputs:     osc, (modBuffer), buf, size, gain.
 * Outputs:    buf; phase, phaseInc.
 * Accessors:  Oscillator.c type dispatch.
 */
void oscPwm_render(struct OscStruct* osc, int16_t* buf, uint8_t size,
                   float gain);
void oscPwm_renderFm(struct OscStruct* osc, int16_t* modBuffer, int16_t* buf,
                     uint8_t size, float gain);

#endif /* OSCILLATORPWM_H_ */
```

### C7.2 `Core/DSPAudio/OscillatorPwm.c` (new file)

```c
#include "Oscillator.h"
#include "OscillatorPwm.h"
#include "StockWaves.h"
#include "SampleMemory.h"
#include "wavetable.h"
#include "config.h"

#define PWM_PHASE_CYCLE 4294967296.0f
#define PWM_PHASE_MASK  0x003FFFFFu
#define PWM_INV_CYCLE   2.3283064365386963e-10f   /* 1 / 2^32 */
#define PWM_QUARTER     0x40000000u

/*
 * Interpolated read of a 1,024-point level (S085 C7.2).
 *
 * What:       index = phase >> 22, next wraps at 1,024, fraction = low 22
 *             bits x 1/2^22 (2.384185791015625e-07f, written inline so the
 *             host extract carries it). The same read as
 *             calcWavetableOscBlock().
 * Why:        the 1/2^22 fraction is the fix of S084 P1 F3 (1/2^32 made the
 *             read stepped); PWM_INV_CYCLE stays only for whole-cycle
 *             positions (warp, split, shift, sweep).
 */
static inline int32_t pwm_read1024(const int16_t* tab, uint32_t phase)
{
	const uint32_t i = (phase >> 22) & 0x3FFu;
	const uint32_t n = (i + 1u) & 0x3FFu;
	const float frac = (float)(phase & PWM_PHASE_MASK) * 2.384185791015625e-07f;
	const int32_t a = tab[i];

	return a + (int32_t)(frac * (float)((int32_t)tab[n] - a));
}

/* 4,096-point sine read (the calcSineBlock() read) for the swp segment 0. */
static inline int32_t pwm_readSine(uint32_t phase)
{
	const uint32_t i = (phase >> 20) & 0xFFFu;
	const float frac = (float)(phase & 0x000FFFFFu) * 0.00000095367431640625f;
	const int32_t a = sine_table[i];

	return a + (int32_t)(frac * (float)((int32_t)sine_table[i + 1u] - a));
}

/* Interpolated read at a fractional position p in [0, 1) of one level. */
static inline int32_t pwm_readWarp(const stock_level_t* lv, float p)
{
	float x = p * (float)(lv->mask + 1u);
	uint32_t i;
	float frac;

	if (x < 0.0f)
		x = 0.0f;
	i = (uint32_t)x;
	if (i > lv->mask)
		i = lv->mask;
	frac = x - (float)i;
	{
		const uint32_t n = (i + 1u) & lv->mask;
		const int32_t a = lv->tab[i];

		return a + (int32_t)(frac * (float)((int32_t)lv->tab[n] - a));
	}
}

/* prc: saw(phase) - saw(phase + shift), x scale, saturated. */
static inline int32_t pwm_sawPair(const int16_t* tab, uint32_t phase,
                                  uint32_t shift, float scale)
{
	return __SSAT((int32_t)((float)(pwm_read1024(tab, phase) -
	                                pwm_read1024(tab, phase + shift)) * scale), 16);
}

/* psw: saw(phase) + saw(phase + shift), x scale, saturated. */
static inline int32_t pwm_sawSum(const int16_t* tab, uint32_t phase,
                                 uint32_t shift, float scale)
{
	return __SSAT((int32_t)((float)(pwm_read1024(tab, phase) +
	                                pwm_read1024(tab, phase + shift)) * scale), 16);
}

/* ptr/pq3/pw3: two slopes meeting at split, then one read. */
static inline int32_t pwm_warp(const stock_level_t* lv, uint32_t phase,
                               const OscPwmState* s)
{
	const float u = (float)phase * PWM_INV_CYCLE;
	const float w = (float)s->split * PWM_INV_CYCLE;
	const float p = (u < w) ? (u * s->warpA) : (s->mix + (u - w) * s->warpB);

	return pwm_readWarp(lv, p);
}

/* swp: sine->tri, tri->saw, saw->square (two reads + blend), then prc. */
static inline int32_t pwm_sweep(uint8_t level, uint32_t phase,
                                const OscPwmState* s)
{
	int32_t a;
	int32_t b;

	if (s->seg >= 3u)
		return pwm_sawPair(sawTable[level], phase - PWM_QUARTER,
		                   s->shift, s->scale);
	switch (s->seg) {
	case 0u:
		a = pwm_readSine(phase);
		b = pwm_read1024(triTable[level], phase + PWM_QUARTER);
		break;
	case 1u:
		a = pwm_read1024(triTable[level], phase + PWM_QUARTER);
		b = pwm_read1024(sawTable[level], phase - PWM_QUARTER);
		break;
	default:
		a = pwm_read1024(sawTable[level], phase - PWM_QUARTER);
		b = pwm_sawPair(sawTable[level], phase - PWM_QUARTER,
		                s->shift, s->scale);
		break;
	}
	return (int32_t)((float)a + s->mix * ((float)b - (float)a));
}

/* The family's table level: saw for prc/psw/swp, tri for ptr, stock for
** pq3 (sq3) and pw3 (sw3) with their Rec/Saw fallback before an install. */
static inline stock_level_t pwm_familyLevel(uint8_t family, uint8_t level)
{
	stock_level_t lv;

	if (family == OSC_PWM_PQ3)
		return stockWave_level(0u, level);
	if (family == OSC_PWM_PW3)
		return stockWave_level(1u, level);
	lv.tab = (family == OSC_PWM_PTR) ? triTable[level] : sawTable[level];
	lv.shift = 22u;
	lv.mask = 1023u;
	return lv;
}

void oscPwm_prepare(struct OscStruct* osc, float freqHz)
{
	OscPwmState* s;
	uint8_t family;
	float c;
	float w = 0.5f;
	float comp = 1.0f;
	float s0 = 0.5f;
	uint8_t warped = 0u;

	if (!osc)
		return;
	s = &osc->pwm;
	family = (osc->source < OSC_PWM_FAMILY_COUNT) ? osc->source : OSC_PWM_PRC;
	c = (float)osc->waveform * (1.0f / 127.0f);
	s->seg = 0u;
	s->mix = 0.0f;
	s->warpA = 0.5f;
	s->warpB = 0.5f;
	s->scale = 1.0f;
	switch (family) {
	case OSC_PWM_PRC:
		w = 0.5f - 0.46f * c;
		s->scale = 1.0f / (2.0f * ((w > 1.0f - w) ? w : (1.0f - w)));
		break;
	case OSC_PWM_PSW:
		w = 0.5f * c;
		s->scale = 0.5f / (1.0f - w);
		break;
	case OSC_PWM_PTR:
		w = 0.5f + (0.03f - 0.5f) * c;
		warped = 1u;
		break;
	case OSC_PWM_PQ3:
		s0 = sampleMemory_stockValid() ? 0.78f : 0.5f;
		w = s0 + (0.12f - s0) * c;
		warped = 1u;
		break;
	case OSC_PWM_PW3:
		s0 = sampleMemory_stockValid() ? 0.42f : 0.5f;
		w = s0 + (0.04f - s0) * c;
		warped = 1u;
		break;
	default: { /* OSC_PWM_SWP */
		float p = 4.0f * c;

		if (p >= 4.0f)
			p = 3.9999f;
		s->seg = (uint8_t)p;
		s->mix = p - (float)s->seg;
		w = (s->seg >= 3u) ? (0.5f - 0.46f * s->mix) : 0.5f;
		s->scale = 1.0f / (2.0f * ((w > 1.0f - w) ? w : (1.0f - w)));
		break; }
	}
	if (warped || family == OSC_PWM_SWP)
		w = (w < 0.02f) ? 0.02f : ((w > 0.98f) ? 0.98f : w);
	s->split = (uint32_t)(w * PWM_PHASE_CYCLE);
	s->shift = s->split;
	if (warped) {
		s->mix = s0;
		s->warpA = s0 / w;
		s->warpB = (1.0f - s0) / (1.0f - w);
		comp = (s->warpA > s->warpB) ? s->warpA : s->warpB;
	}
	osc->phaseIncTarget =
		(uint32_t)(((1024.0f * freqHz) / REAL_FS) * 4194304.0f);
	{
		const uint8_t level = osc_freqToTableIndex(freqHz * comp);

		osc->tableOffset = (level > 10u) ? 10u : level;
	}
}

/*
 * One pwm block, shared by both entry points (S085 C7.2).
 *
 * What:       the C7.1 render contract; modBuffer NULL = no FM. The family
 *             switch is per sample on a block constant (branch-predicted);
 *             C17.4 measures it, and per-family loops are the option if the
 *             probe shows a material saving (plan §6 item 2).
 */
static inline void oscPwm_block(struct OscStruct* osc,
                                const int16_t* modBuffer, int16_t* buf,
                                uint8_t size, float gain)
{
	const OscPwmState* s = &osc->pwm;
	const uint8_t family = osc->source;
	const stock_level_t lv = pwm_familyLevel(family, osc->tableOffset);
	uint32_t phase = osc->phase;
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const uint32_t readPhase = modBuffer
			? phase + (uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 19)
			: phase;
		int32_t out;

		switch (family) {
		case OSC_PWM_PRC:
			out = pwm_sawPair(lv.tab, readPhase, s->shift, s->scale);
			break;
		case OSC_PWM_PSW:
			out = pwm_sawSum(lv.tab, readPhase, s->shift, s->scale);
			break;
		case OSC_PWM_SWP:
			out = pwm_sweep(osc->tableOffset, readPhase, s);
			break;
		default:
			out = pwm_warp(&lv, readPhase, s);
			break;
		}
		buf[i] = (int16_t)((float)out * gain);
		phase += inc;            /* S085 C5.6: per-sample slew */
		inc += (uint32_t)step;
	}
	osc->phase = phase;
	osc->phaseInc = inc;
}

INITCM void oscPwm_render(struct OscStruct* osc, int16_t* buf, uint8_t size,
                          float gain)
{
	if (osc && buf)
		oscPwm_block(osc, 0, buf, size, gain);
}

INITCM void oscPwm_renderFm(struct OscStruct* osc, int16_t* modBuffer,
                            int16_t* buf, uint8_t size, float gain)
{
	if (osc && buf && modBuffer)
		oscPwm_block(osc, modBuffer, buf, size, gain);
}
```

- **Copy** the C7.1 blocks above `oscPwm_prepare()` and the two renderers.
- **`oscPwm_prepare()` note:** add "natural split of the warped families:
  c = 0 plays the wave unwarped (w = s0); the slopes map [0, w) onto
  [0, s0) and [w, 1) onto [s0, 1); s0 sits in OscPwmState.mix (swp uses mix
  for its blend instead)".
- **`__SSAT`:** available through `stm32f4xx.h` / `cmsis_intrinsics.h`, which
  `Oscillator.h` includes.

---

## 9. WP8 — `wtb`: `OscillatorWavetable.h/.c` (new), block-rate frame pair (S085 D1)

- **Compiled with:** `DSP_SRCS`.
- **ITCM:** the renderers.

### C8.1 `Core/DSPAudio/OscillatorWavetable.h` (new file)

```c
/*
 * OscillatorWavetable — the wtb oscillator type (S085 C8.1).
 *
 * What:       plays an installed wavetable (frames of any length, 8- or
 *             16-bit, stored as supplied). src selects the table; the wav
 *             row is the frame position 0..127 across the table's frames.
 *             Each block selects one pair of neighbouring frames and ramps
 *             the blend between them; per sample it reads two points from
 *             each frame (one 32 x 32 -> 64 multiply gives index and
 *             fraction for any frame length) and blends.
 * Why:        plan §5 item 2, Q9 as revised by S085 D1: the frame pair is
 *             chosen once per block, so the worst case (fast position sweep)
 *             costs the same as a static position. The position moves at
 *             most one frame per block (about 1,378 frames/s); faster
 *             requests glide.
 * Inputs:     OscInfo (oscType, sourceRequest, waveform, phase, phaseInc,
 *             phaseIncStep, fmMod, wtb).
 * Outputs:    rendered blocks.
 * Accessors:  Oscillator.c dispatch; InstrumentManager.c writers (bind).
 * Affiliates: SampleMemory.h (WavetableInfo), osc_setFreq() (2^32/cycle).
 */
#ifndef OSCILLATORWAVETABLE_H_
#define OSCILLATORWAVETABLE_H_

#include <stdint.h>

/*
 * Per-type wavetable state (S085 C8.1).
 *
 * What:       the bound table (absolute frame-0 address, bytes per frame,
 *             geometry, catalogue generation, index) and the frame position
 *             carried between blocks (pos, primed).
 * Why:        no catalogue read per block beyond the bind compare; shares
 *             OscInfo's union with OscPwmState (24 B <= 28 B).
 * Writers:    oscWavetable_bind(), the renderers (pos, primed),
 *             IM_SPECIAL_OSC_TYPE writer (clears base/primed on entering wtb).
 */
typedef struct {
    uint32_t base;          /* absolute address of frame 0; 0 = silent */
    float    pos;           /* frame position at the end of the last block */
    uint32_t frame_bytes;   /* frame_len x bits / 8 */
    uint32_t generation;    /* catalogue generation at bind */
    uint16_t frame_len;
    uint8_t  frame_count;
    uint8_t  bits;          /* 8 or 16 */
    uint8_t  index;         /* bound wavetable */
    uint8_t  primed;        /* pos is valid */
    uint16_t reserved;
} OscWtbState;

struct OscStruct;

/*
 * Bind the selected wavetable (S085 C8.1).
 *
 * What:       live index = min(sourceRequest, count - 1); on a changed index
 *             or catalogue generation (or base 0) reloads the geometry and
 *             clears primed; no tables binds silence (base 0, source 0).
 * Accessors:  the renderers (every block), the IM type/source writers.
 * Affiliates: sampleMemory_getWavetableInfo().
 */
void oscWavetable_bind(struct OscStruct *osc);

/*
 * Render one wavetable block (S085 C8.1).
 *
 * What:       see the header contract; 8-bit points are sign-extended and
 *             shifted left by 8; the FM variant adds (mod x fmMod) << 19 to
 *             the read phase. Silence still advances the phase.
 * Inputs:     osc, (modBuffer), buf, size, gain.
 * Outputs:    buf; phase, phaseInc, wtb.pos.
 * Accessors:  Oscillator.c type dispatch.
 */
void oscWavetable_render(struct OscStruct *osc, int16_t *buf, uint8_t size,
                         float gain);
void oscWavetable_renderFm(struct OscStruct *osc, int16_t *modBuffer,
                           int16_t *buf, uint8_t size, float gain);

#endif /* OSCILLATORWAVETABLE_H_ */
```

### C8.2 `Core/DSPAudio/OscillatorWavetable.c` (new file)

**Frame-pair rule, per block.** With `n = frame_count`, target
`t = wav × (n − 1) / 127` and start `p0 = pos`:

- if `t ≥ p0`: `fa = floor(p0)`, clamped to ≤ n − 2 when n ≥ 2;
  `fb = fa + 1`; `pEnd = min(t, fb)`;
- if `t < p0`: `fb = ceil(p0)`, clamped to ≤ n − 1; `fa = fb − 1` (≥ 0);
  `pEnd = max(t, fa)`;
- `n = 1`: `fa = fb = 0`;
- blend `ff` runs linearly from `p0 − fa` to `pEnd − fa` across the block;
  `pos = pEnd`.

```c
#include "Oscillator.h"
#include "OscillatorWavetable.h"
#include "SampleMemory.h"
#include "config.h"

#define WTB_INV_CYCLE 2.3283064365386963e-10f /* 1 / 2^32 */

void oscWavetable_bind(struct OscStruct *osc)
{
	uint8_t count;
	uint8_t index;
	uint32_t generation;
	WavetableInfo info;

	if (!osc || osc->oscType != OSC_TYPE_WTB)
		return;
	count = sampleMemory_getNumWavetables();
	if (count == 0u) {
		osc->source = 0u;
		osc->wtb.base = 0u;
		osc->wtb.frame_count = 0u;
		osc->wtb.primed = 0u;
		return;
	}
	index = (osc->sourceRequest >= count) ? (uint8_t)(count - 1u)
	                                      : osc->sourceRequest;
	osc->source = index;
	generation = sampleMemory_getGeneration();
	if (osc->wtb.base != 0u && osc->wtb.index == index &&
	    osc->wtb.generation == generation)
		return;
	if (!sampleMemory_getWavetableInfo(index, &info)) {
		osc->wtb.base = 0u;
		return;
	}
	osc->wtb.base = info.offset;
	osc->wtb.frame_len = info.frame_len;
	osc->wtb.frame_count = info.frame_count;
	osc->wtb.bits = info.bits;
	osc->wtb.frame_bytes = (uint32_t)info.frame_len * (info.bits / 8u);
	osc->wtb.generation = generation;
	osc->wtb.index = index;
	osc->wtb.primed = 0u;
}

/*
 * Choose this block's frame pair and blend ramp (S085 C8.2).
 *
 * What:       the frame-pair rule above; returns the two frame indices, the
 *             start blend and the per-sample blend step, and stores pos.
 * Why:        S085 D1: all frame arithmetic is per block.
 */
static inline void oscWavetable_pair(OscWtbState *w, uint8_t waveform,
                                     uint8_t size, uint32_t *fa, uint32_t *fb,
                                     float *ff, float *ffStep)
{
	const uint32_t n = w->frame_count;
	const float target = (float)waveform * (float)(n - 1u) / 127.0f;
	float p0;
	float pEnd;

	if (!w->primed) {
		w->pos = target;
		w->primed = 1u;
	}
	p0 = w->pos;
	if (n < 2u) {
		*fa = 0u;
		*fb = 0u;
		pEnd = p0;
	} else if (target >= p0) {
		*fa = (uint32_t)p0;
		if (*fa > n - 2u)
			*fa = n - 2u;
		*fb = *fa + 1u;
		pEnd = (target < (float)*fb) ? target : (float)*fb;
	} else {
		uint32_t up = (uint32_t)p0;

		if ((float)up < p0)
			up++;
		if (up > n - 1u)
			up = n - 1u;
		if (up == 0u)
			up = 1u;
		*fb = up;
		*fa = up - 1u;
		pEnd = (target > (float)*fa) ? target : (float)*fa;
	}
	*ff = p0 - (float)*fa;
	*ffStep = (pEnd - p0) / (float)size;
	w->pos = pEnd;
}

/*
 * One wavetable block, shared by both entry points and both depths
 * (S085 C8.2). modBuffer NULL = no FM; bits selects the 8- or 16-bit read.
 */
static inline void oscWavetable_block(struct OscStruct *osc,
                                      const int16_t *modBuffer, int16_t *buf,
                                      uint8_t size, float gain)
{
	OscWtbState *w;
	uint32_t phase = osc->phase;
	uint32_t inc = osc->phaseInc;
	const int32_t step = osc->phaseIncStep;
	uint32_t fa;
	uint32_t fb;
	float ff;
	float ffStep;
	uint8_t i;

	oscWavetable_bind(osc);
	w = &osc->wtb;
	if (w->base == 0u || w->frame_count == 0u || w->frame_len < 2u) {
		for (i = 0u; i < size; i++) {
			buf[i] = 0;
			phase += inc;
			inc += (uint32_t)step;
		}
		osc->phase = phase;
		osc->phaseInc = inc;
		return;
	}
	oscWavetable_pair(w, osc->waveform, size, &fa, &fb, &ff, &ffStep);
	if (w->bits == 8u) {
		const int8_t *A = (const int8_t *)SAMPLE_FLASH_PTR(w->base + fa * w->frame_bytes);
		const int8_t *B = (const int8_t *)SAMPLE_FLASH_PTR(w->base + fb * w->frame_bytes);

		for (i = 0u; i < size; i++) {
			const uint32_t rp = modBuffer
				? phase + (uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 19)
				: phase;
			const uint64_t m = (uint64_t)rp * w->frame_len;
			const uint32_t idx = (uint32_t)(m >> 32);
			const uint32_t nx = (idx + 1u == w->frame_len) ? 0u : idx + 1u;
			const float fr = (float)(uint32_t)m * WTB_INV_CYCLE;
			const float a = (float)((int32_t)A[idx] << 8);
			const float b = (float)((int32_t)A[nx] << 8);
			const float a2 = (float)((int32_t)B[idx] << 8);
			const float b2 = (float)((int32_t)B[nx] << 8);
			const float x0 = a + ff * (a2 - a);
			const float x1 = b + ff * (b2 - b);

			buf[i] = (int16_t)((x0 + fr * (x1 - x0)) * gain);
			ff += ffStep;
			phase += inc;            /* S085 C5.6: per-sample slew */
			inc += (uint32_t)step;
		}
	} else {
		const int16_t *A = (const int16_t *)SAMPLE_FLASH_PTR(w->base + fa * w->frame_bytes);
		const int16_t *B = (const int16_t *)SAMPLE_FLASH_PTR(w->base + fb * w->frame_bytes);

		for (i = 0u; i < size; i++) {
			const uint32_t rp = modBuffer
				? phase + (uint32_t)((int32_t)((float)modBuffer[i] * osc->fmMod) << 19)
				: phase;
			const uint64_t m = (uint64_t)rp * w->frame_len;
			const uint32_t idx = (uint32_t)(m >> 32);
			const uint32_t nx = (idx + 1u == w->frame_len) ? 0u : idx + 1u;
			const float fr = (float)(uint32_t)m * WTB_INV_CYCLE;
			const float a = (float)A[idx];
			const float b = (float)A[nx];
			const float x0 = a + ff * ((float)B[idx] - a);
			const float x1 = b + ff * ((float)B[nx] - b);

			buf[i] = (int16_t)((x0 + fr * (x1 - x0)) * gain);
			ff += ffStep;
			phase += inc;
			inc += (uint32_t)step;
		}
	}
	osc->phase = phase;
	osc->phaseInc = inc;
}

INITCM void oscWavetable_render(struct OscStruct *osc, int16_t *buf,
                                uint8_t size, float gain)
{
	if (osc && buf)
		oscWavetable_block(osc, 0, buf, size, gain);
}

INITCM void oscWavetable_renderFm(struct OscStruct *osc, int16_t *modBuffer,
                                  int16_t *buf, uint8_t size, float gain)
{
	if (osc && buf && modBuffer)
		oscWavetable_block(osc, modBuffer, buf, size, gain);
}
```

- **Copy** the C8.1 blocks above each public definition. Add the S085 D1
  rationale to `oscWavetable_pair()`'s block.
- **Index safety:** `idx = (rp × frame_len) >> 32` is always
  `< frame_len`, so both reads stay inside their frame. `fa` and `fb` are
  ≤ n − 1.
- **Cost target** (C17.4): `wtb` ≈ 42 (FM ≈ 48) instructions per sample,
  against the spike's 64/70. The cost record shows the measured figure.

---
## 10. WP9 — Render pool, modulation node, mixer order

### C9.1 `Core/DSPAudio/OscRenderPool.h` (new file)

```c
/*
 * OscRenderPool — the shared budget of extra oscillator renders
 * (S085 C9.1).
 *
 * What:       OSC_RENDER_POOL_UNITS (2) units per 32-frame block. One unit is
 *             one extra oscillator render: an itp carrier crossfade (one per
 *             voice) or one waveform interpolation. The pool resets at block
 *             start. Claim order per block (C9.4): ongoing itp crossfades,
 *             then waveform interpolation (as modulation is applied), then new
 *             itp crossfades, lowest slot first. One scratch block holds the
 *             old carrier of the voice being rendered (voices render one at a
 *             time).
 * Why:        plan §4.3.7 / Q14: the worst case is fixed at two extra renders
 *             per block, budgeted as always used.
 * Inputs:     claims.  Outputs: grants; the scratch block.
 * Accessors:  mixer.c (beginBlock), InstrumentManager.c (itp claims),
 *             modulationNode.c (interpolation claims), the engines (scratch).
 * Affiliates: config.h OSC_RENDER_POOL_UNITS, OscSection.c.
 */
#ifndef OSCRENDERPOOL_H_
#define OSCRENDERPOOL_H_

#include <stdint.h>

/* Reset the pool for a new block. Accessor: mixer_calcNextSampleBlock(). */
void     oscRenderPool_beginBlock(void);

/*
 * Claim units (S085 C9.1).
 * What: grants `units` when they fit in what is left this block.
 * Outputs: 1 granted (units counted), 0 refused (nothing counted).
 */
uint8_t  oscRenderPool_claim(uint8_t units);

/*
 * Old-carrier scratch (S085 C9.1).
 * What: one OUTPUT_DMA_SIZE int16 block for the itp copy of the voice being
 * rendered. Accessors: the engines' render functions, OscSection.c.
 */
int16_t *oscRenderPool_scratch(void);

#endif /* OSCRENDERPOOL_H_ */
```

### C9.2 `Core/DSPAudio/OscRenderPool.c` (new file)

- **Compiled with:** `DSP_SRCS`.

```c
#include "OscRenderPool.h"
#include "config.h"

/*
 * Pool state (S085 C9.2).
 *
 * What:       units granted this block, and the old-carrier scratch.
 * Why:        approved SRAM1 (S085 §0.5: 64 B + 1 B).
 * Writers:    beginBlock/claim; the engines write the scratch.
 */
static uint8_t oscRenderPool_unitsUsed;
static int16_t oscRenderPool_scratchBuf[OUTPUT_DMA_SIZE];

void oscRenderPool_beginBlock(void)
{
	oscRenderPool_unitsUsed = 0u;
}

uint8_t oscRenderPool_claim(uint8_t units)
{
	if ((uint16_t)oscRenderPool_unitsUsed + units > OSC_RENDER_POOL_UNITS)
		return 0u;
	oscRenderPool_unitsUsed = (uint8_t)(oscRenderPool_unitsUsed + units);
	return 1u;
}

int16_t *oscRenderPool_scratch(void)
{
	return oscRenderPool_scratchBuf;
}
```

### C9.3 `Core/DSPAudio/modulationNode.c` — interpolation claims from the pool

| Where | Action |
|---|---|
| line 53 `#include "../SampleRom/SampleMemory.h"` | **Modify** to `#include "OscRenderPool.h"` (the sample count is no longer used here) |
| lines 58–64, the comment above the statics | **Modify**, block below |
| line 67 `static INCCMZ uint8_t modNode_waveInterpActiveCount = 0u;` | **Remove** (−1 B DTCM) |
| lines 76–84 `modNode_getMaxWaveformIndex()` | **Modify**: `return (uint8_t)(OSC_STOCK_WAVE_COUNT - 1u);`, comment below |
| line 438 `modNode_waveInterpActiveCount = 0u;` (in `modNode_resetTargets()`) | **Remove** |
| line 773 `if (osc && modNode_waveInterpActiveCount < OSC_WAVE_INTERP_MAX_ACTIVE) {` | **Modify** to `if (osc && osc->oscType == OSC_TYPE_WAV && oscRenderPool_claim(1u)) {` |
| line 792 `modNode_waveInterpActiveCount++;` | **Remove** |

```c
/* Oscillator waveform interpolation for one 32-frame block (S085 C9.3).
**
** modNode_resetTargets() starts a new generation at the top of each mixer
** block. A wav-type oscillator waveform target receives a fractional
** base/next pair when OscRenderPool grants one unit; ongoing itp crossfades
** claimed theirs first, new itp crossfades claim after modulation. Refused
** targets snap to their integer waveform for that block. pwm/smp/slp/wtb
** never interpolate (their wav row is already continuous). */
```

```c
/*
 * Highest wav-list value (S085 C9.3).
 *
 * What:       OSC_STOCK_WAVE_COUNT - 1 (10, swa).
 * Why:        samples left the wav list (plan §4.3.5); the per-slot range of
 *             the other types comes from InstrumentManager's domain (C13.6).
 * Accessors:  the interpolation clamp below; InstrumentManager.c (wav-type
 *             domain).
 */
```

### C9.4 `Core/DSPAudio/mixer.c` — block-start pool reset and itp claims

- **Include:** after line 56 `#include "InstrumentManager.h"`, **add**
  `#include "OscRenderPool.h"`.
- **Where:** `mixer_calcNextSampleBlock()`, lines 866–882.
- **Action:**
  - **Add** two lines before `modNode_resetTargets();` (line 866).
  - **Add** one line after `instrumentManager_dispatchRuntimeLfos();`
    (line 878).

```c
	/*
	 * Oscillator render pool, part 1 (S085 C9.4).
	 *
	 * What:       reset the pool, then let crossfades already running keep
	 *             their unit (lowest slot first).
	 * Why:        Q14 claim order: an ongoing crossfade is never cut; waveform
	 *             interpolation (granted while velocity and LFO modulation are
	 *             applied just below) comes next; new crossfades come last.
	 * Affiliates: OscRenderPool.c, instrumentManager_claimOscSectionOngoing().
	 */
	oscRenderPool_beginBlock();
	instrumentManager_claimOscSectionOngoing();
	modNode_resetTargets();
	//re assign velocity modulation
	modNode_reassignVeloMod();
	/* ... existing comment ... */
	instrumentManager_dispatchRuntimeLfos();
	/*
	 * Oscillator render pool, part 2 (S085 C9.4).
	 *
	 * What:       triggers since the last block (voiceControl_processPending()
	 *             ran before this function) claim what is left, lowest slot
	 *             first; a refused itp trigger behaves as fre (its carrier
	 *             phase is restored and slews).
	 * Why:        must precede the filter recalc and calcSlotAsync(), so a
	 *             fallback slew is armed before osc_setFreq() computes steps.
	 * Affiliates: instrumentManager_claimOscSectionPending().
	 */
	instrumentManager_claimOscSectionPending();
```

**Keep:** the decimator (`mixer_decimateBlock()`, `mixer_decimation_rate`) is
unchanged.

---

## 11. WP10 — Oscillator section `OscSection.h/.c` (new), filter slew

### C10.1 `Core/DSPAudio/OscSection.h` (new file)

```c
/*
 * OscSection — a voice's oscillator section: combination modes and run modes
 * (S085 C10.1).
 *
 * What:       combination of carrier and modulator(s):
 *               OSC_MOD_FM  phase modulation (stored 0, today);
 *               OSC_MOD_MIX carrier x (1-amt) + modulator x amt (1, today);
 *               OSC_MOD_SNC hard sync: the modulator is the master, the
 *                           carrier the slave, reset at each master wrap with
 *                           the overshoot scaled; out = slave x (1-amt) +
 *                           master x amt (Q10);
 *               OSC_MOD_RNG ring: carrier x modulator, amt = wet amount.
 *             Run modes, per voice:
 *               OSC_RUN_RST today's trigger resets, filter cleared;
 *               OSC_RUN_FRE no resets; every oscillator slews its pitch over
 *                           OSC_SLEW_SAMPLES; the filter keeps ringing and its
 *                           cutoff/resonance slew;
 *               OSC_RUN_ITP the carrier is snapshotted and restarted; the copy
 *                           fades out over OSC_ITP_SAMPLES while the
 *                           restarted carrier fades in; the modulators slew as
 *                           in fre and feed both carriers; one pool unit per
 *                           voice; with no free unit the trigger behaves as
 *                           fre; the filter keeps ringing and slews.
 * Why:        plan §4.3.1 (snc cost), §4.3.7 (carrier-only itp, Q14), Q10,
 *             Q15; stored values 0/1 of the mod row keep their meaning.
 * Inputs:     engine oscillators, mode bytes, pool grants.
 * Outputs:    the section block; slew and crossfade state.
 * Accessors:  the four engines, InstrumentManager.c.
 * Affiliates: Oscillator.c (osc_armSlew, renderers), OscillatorSample.c
 *             (restart, end test), OscRenderPool.c, ResonantFilter.c
 *             (SVF_recalcFreqFrom).
 */
#ifndef OSCSECTION_H_
#define OSCSECTION_H_

#include <stdint.h>
#include "Oscillator.h"
#include "ResonantFilter.h"

#define OSC_RUN_RST   0u
#define OSC_RUN_FRE   1u
#define OSC_RUN_ITP   2u
#define OSC_RUN_COUNT 3u

#define OSC_MOD_FM    0u
#define OSC_MOD_MIX   1u
#define OSC_MOD_SNC   2u
#define OSC_MOD_RNG   3u
#define OSC_MOD_COUNT 4u

#define OSC_ITP_SAMPLES 132u
#define OSC_ITP_IDLE    0u
#define OSC_ITP_PENDING 1u
#define OSC_ITP_ACTIVE  2u

/* oscSection_beginTrigger() results: what the engine resets. */
#define OSC_TRIGGER_RESET_NONE    0u  /* fre: nothing */
#define OSC_TRIGGER_RESET_ALL     1u  /* rst: the engine's usual resets */
#define OSC_TRIGGER_RESET_CARRIER 2u  /* itp: the carrier only */

/*
 * Per-voice section state (S085 C10.1).
 *
 * What:       runMode (the run row), the itp state machine and position, the
 *             one carrier snapshot, and the filter slew (fSmooth < 0 = not
 *             yet seeded).
 * Why:        carrier-only itp needs one OscInfo copy (S085 §4.3.2: about
 *             140 B per engine, inside the 1,176 B runtime slot).
 * Writers:    oscSection_*(), IM_SPECIAL_RUN_MODE writer.
 * Readers:    the engines, InstrumentManager.c.
 */
typedef struct OscSectionStateStruct {
    uint8_t  runMode;
    uint8_t  itpState;
    uint16_t itpPos;
    OscInfo  itpOld;
    float    fSmooth;
    float    qSmooth;
    float    qTarget;
} OscSectionState;

/*
 * Initialise (S085 C10.1).
 * What: rst, idle, filter slew unseeded.
 * Accessors: each engine's *_initVoice().
 */
void oscSection_init(OscSectionState* s);

/*
 * Apply a trigger to the run mode (S085 C10.1).
 *
 * What:       rst -> OSC_TRIGGER_RESET_ALL. fre -> arms the slew on the
 *             carrier and every modulator, OSC_TRIGGER_RESET_NONE. itp ->
 *             snapshots the carrier into itpOld (pitch frozen: step 0, no
 *             slew; waveform interpolation off), arms the slew on every
 *             modulator, restarts the crossfade position, and returns
 *             OSC_TRIGGER_RESET_CARRIER; the state becomes PENDING, or stays
 *             ACTIVE when a crossfade was already running (it keeps its unit;
 *             the partly faded copy is replaced, the accepted S084 P1 F6).
 * Why:        plan §4.3.7.
 * Inputs:     section, carrier, modulator array, count (Snare passes its
 *             noise oscillator).
 * Outputs:    the reset instruction; slew and itp state.
 * Accessors:  the engines' trigger functions.
 * Affiliates: osc_armSlew(), oscSection_claimPending().
 */
uint8_t oscSection_beginTrigger(OscSectionState* s, OscInfo* carrier,
                                OscInfo* const mods[], uint8_t modCount);

/*
 * Pool claims (S085 C10.1).
 *
 * What:       claimOngoing: an ACTIVE crossfade claims its unit (always
 *             granted, at most two can be ACTIVE; if ever refused the
 *             crossfade ends). claimPending: a PENDING crossfade claims one
 *             unit and becomes ACTIVE; refused, the carrier's phase is
 *             restored from the snapshot and the carrier slews (fre).
 * Accessors:  InstrumentManager.c claim loops (C13.7).
 */
void oscSection_claimOngoing(OscSectionState* s);
void oscSection_claimPending(OscSectionState* s, OscInfo* carrier);

/*
 * The fading carrier copy (S085 C10.1).
 * What: &itpOld while ACTIVE, else NULL.
 * Accessors: the engines' render functions.
 */
OscInfo* oscSection_itpCarrier(OscSectionState* s);

/*
 * Render the Drum combination (S085 C10.1).
 *
 * What:       fm and mix are bit-identical to the pre-S085 Drum code when
 *             oldCar is NULL; snc and rng as in the header contract. When
 *             oldCar is not NULL (itp ACTIVE) the old carrier is rendered into
 *             oldBuf with the same, live modulator block (and, in snc, the
 *             same master wraps), so oldBuf differs from buf only by the
 *             carrier.
 * Inputs:     car, mod, mode, amount, buf, modBuf, size, oldCar, oldBuf.
 * Outputs:    buf (and oldBuf); modBuf holds the modulator block.
 * Accessors:  Drum_calcVoiceSyncBlock().
 */
void oscSection_renderDrum(OscInfo* car, OscInfo* mod, uint8_t mode,
                           float amount, int16_t* buf, int16_t* modBuf,
                           uint8_t size, OscInfo* oldCar, int16_t* oldBuf);

/*
 * Render the Cymbal/HiHat two-modulator FM section (S085 C10.1).
 *
 * What:       modulators at g1/g2 into modBuf/modBuf2, saturating sum into
 *             modBuf, carrier FM at carGain into buf (bit-identical to the
 *             pre-S085 engines); oldCar (itp) is rendered with the same
 *             summed modBuf into oldBuf.
 * Accessors:  Cymbal_calcSyncBlockVoice(), HiHat_calcSyncBlockVoice().
 */
void oscSection_renderFm2(OscInfo* car, OscInfo* m1, OscInfo* m2,
                          float g1, float g2, float carGain,
                          int16_t* buf, int16_t* modBuf, int16_t* modBuf2,
                          uint8_t size, OscInfo* oldCar, int16_t* oldBuf);

/*
 * Crossfade and advance (S085 C10.1).
 *
 * What:       blendItp: buf[i] = old[i] + t x (buf[i] - old[i]),
 *             t = (itpPos + i) / OSC_ITP_SAMPLES clamped to 1 (ACTIVE only).
 *             finishBlock: itpPos += size; at OSC_ITP_SAMPLES the state
 *             returns to IDLE and the unit frees next block.
 * Accessors:  the engines' render functions.
 */
void oscSection_blendItp(OscSectionState* s, int16_t* buf,
                         const int16_t* oldBuf, uint8_t size);
void oscSection_finishBlock(OscSectionState* s, uint8_t size);

/*
 * Filter slew (S085 C10.1).
 *
 * What:       noteResoTarget stores the reso writer's q. filterRecalc: rst is
 *             exactly SVF_recalcFreq() (S0); fre/itp slew the cutoff the loop
 *             uses and the resonance with a one-pole of 0.28 per block (about
 *             2 ms), picking up a q written by velocity or LFO since the last
 *             block (S084 P1 F4).
 * Why:        Q15; a ringing filter never steps on a retrigger.
 * Accessors:  the IM_SPECIAL_FILTER_RESO writer; recalcSlotFilter.
 * Affiliates: SVF_recalcFreq(), SVF_recalcFreqFrom().
 */
void oscSection_noteResoTarget(OscSectionState* s, float q);
void oscSection_filterRecalc(OscSectionState* s, ResonantFilter* f);

#endif /* OSCSECTION_H_ */
```

### C10.2 `Core/DSPAudio/OscSection.c` (new file)

- **Compiled with:** `DSP_SRCS`.
- **ITCM:** `oscSection_renderDrum()` and `oscSection_renderFm2()`.

```c
#include "OscSection.h"
#include "OscillatorSample.h"
#include "OscRenderPool.h"
#include "BufferTools.h"
#include "config.h"
#include <string.h>

#define OSC_FILTER_SLEW_COEF 0.28f

void oscSection_init(OscSectionState* s)
{
	if (!s)
		return;
	memset(s, 0, sizeof(*s));
	s->runMode = OSC_RUN_RST;
	s->itpState = OSC_ITP_IDLE;
	s->fSmooth = -1.0f;
}

uint8_t oscSection_beginTrigger(OscSectionState* s, OscInfo* carrier,
                                OscInfo* const mods[], uint8_t modCount)
{
	uint8_t i;

	if (!s || !carrier)
		return OSC_TRIGGER_RESET_ALL;
	switch (s->runMode) {
	case OSC_RUN_FRE:
		osc_armSlew(carrier);
		for (i = 0u; i < modCount; i++)
			osc_armSlew(mods[i]);
		return OSC_TRIGGER_RESET_NONE;
	case OSC_RUN_ITP:
		s->itpOld = *carrier;
		s->itpOld.phaseIncStep = 0;
		s->itpOld.slewSamplesLeft = 0u;
		s->itpOld.waveInterpGeneration = 0u;  /* the copy never interpolates */
		for (i = 0u; i < modCount; i++)
			osc_armSlew(mods[i]);
		s->itpPos = 0u;
		if (s->itpState != OSC_ITP_ACTIVE)
			s->itpState = OSC_ITP_PENDING;
		return OSC_TRIGGER_RESET_CARRIER;
	case OSC_RUN_RST:
	default:
		return OSC_TRIGGER_RESET_ALL;
	}
}

void oscSection_claimOngoing(OscSectionState* s)
{
	if (s && s->itpState == OSC_ITP_ACTIVE && !oscRenderPool_claim(1u))
		s->itpState = OSC_ITP_IDLE;
}

void oscSection_claimPending(OscSectionState* s, OscInfo* carrier)
{
	if (!s || s->itpState != OSC_ITP_PENDING)
		return;
	if (oscRenderPool_claim(1u)) {
		s->itpState = OSC_ITP_ACTIVE;
		return;
	}
	/* No free unit: behave as fre for this trigger. */
	if (carrier) {
		carrier->phase = s->itpOld.phase;
		osc_armSlew(carrier);
	}
	s->itpState = OSC_ITP_IDLE;
}

OscInfo* oscSection_itpCarrier(OscSectionState* s)
{
	return (s && s->itpState == OSC_ITP_ACTIVE) ? &s->itpOld : 0;
}

/*
 * One master cycle in the master's phase units (S085 C10.2).
 *
 * What:       0 = a full 2^32 wrap (every table/sine/noise/pwm/wtb master);
 *             a sample master (smp/slp) spans (end - start) frames in Q20.12.
 */
static inline uint32_t oscSection_masterCycle(const OscInfo* m)
{
	if (m->oscType == OSC_TYPE_SMP || m->oscType == OSC_TYPE_SLP) {
		const uint32_t frames = (m->sampleEnd > m->sampleStart)
			? (m->sampleEnd - m->sampleStart) : 1u;

		return frames << OSC_SAMPLE_PHASE_SHIFT;
	}
	return 0u;
}

/* Restart a sync slave at the scaled overshoot (S085 C10.2). */
static inline void oscSection_syncReset(OscInfo* slave, uint32_t over,
                                        float ratio)
{
	if (slave->oscType == OSC_TYPE_SMP || slave->oscType == OSC_TYPE_SLP) {
		oscSample_restart(slave);
		slave->phase += (uint32_t)((float)over * ratio);
	} else {
		slave->phase = (uint32_t)((float)over * ratio);
	}
}

/*
 * Hard sync (S085 C10.2).
 *
 * What:       renders the master at unit gain into modBuf; walks a shadow of
 *             the master phase (cycle-relative; a sample master starts at
 *             phase - start, S084 P1 F5); at each wrap between samples i and
 *             i + 1 renders the slave (and the itp copy) up to and including
 *             sample i, then resets it at the overshoot x ratio. The ratios
 *             are computed once per block (Q5). A one-shot master that has
 *             played out stops resetting the slave (Q10, S085 D6). Output:
 *             slave x (1 - amt) + master x amt, saturated (Q10).
 * Cost:       the wrap loop is constant per sample; each wrap adds one
 *             renderer call. C17.4 probes the worst case against the cap
 *             (FM carrier + 10 instructions per sample); see C10.6.
 */
static void oscSection_renderSync(OscInfo* car, OscInfo* mod, float amount,
                                  int16_t* buf, int16_t* modBuf, uint8_t size,
                                  OscInfo* oldCar, int16_t* oldBuf)
{
	const uint32_t cycle = oscSection_masterCycle(mod);
	const uint8_t fullWrap = (uint8_t)(cycle == 0u);
	const uint32_t base =
		(mod->oscType == OSC_TYPE_SMP || mod->oscType == OSC_TYPE_SLP)
		? (mod->sampleStart << OSC_SAMPLE_PHASE_SHIFT) : 0u;
	const uint8_t masterDone = oscSample_isFinished(mod);
	const float ratio = mod->phaseInc
		? (float)car->phaseInc / (float)mod->phaseInc : 0.0f;
	const float ratioOld = (oldCar && mod->phaseInc)
		? (float)oldCar->phaseInc / (float)mod->phaseInc : 0.0f;
	uint32_t phase = (mod->phase > base) ? (mod->phase - base) : 0u;
	uint32_t inc = mod->phaseInc;
	const int32_t step = mod->phaseIncStep;
	uint8_t seg = 0u;
	uint8_t i;

	calcNextOscSampleBlock(mod, modBuf, size, 1.0f);
	for (i = 0u; i < size; i++) {
		const uint32_t next = phase + inc;
		uint8_t wrapped = fullWrap ? (uint8_t)(next < phase)
		                           : (uint8_t)(next >= cycle);

		wrapped = (uint8_t)(wrapped & (uint8_t)!masterDone);
		if (wrapped) {
			uint32_t over = fullWrap ? next : (next - cycle);

			if (!fullWrap && over >= cycle)
				over %= cycle;      /* master faster than its own span */
			calcNextOscSampleBlock(car, buf + seg, (uint8_t)(i + 1u - seg), 1.0f);
			oscSection_syncReset(car, over, ratio);
			if (oldCar) {
				calcNextOscSampleBlock(oldCar, oldBuf + seg,
				                       (uint8_t)(i + 1u - seg), 1.0f);
				oscSection_syncReset(oldCar, over, ratioOld);
			}
			seg = (uint8_t)(i + 1u);
			phase = over;
		} else {
			phase = next;
		}
		inc += (uint32_t)step;
	}
	if (seg < size) {
		calcNextOscSampleBlock(car, buf + seg, (uint8_t)(size - seg), 1.0f);
		if (oldCar)
			calcNextOscSampleBlock(oldCar, oldBuf + seg,
			                       (uint8_t)(size - seg), 1.0f);
	}
	for (i = 0u; i < size; i++) {
		buf[i] = (int16_t)__SSAT((int32_t)((float)buf[i] * (1.0f - amount) +
		                                   (float)modBuf[i] * amount), 16);
		if (oldCar)
			oldBuf[i] = (int16_t)__SSAT((int32_t)((float)oldBuf[i] *
				(1.0f - amount) + (float)modBuf[i] * amount), 16);
	}
}

/* Ring: wet = (car x mod) >> 15; out = car x (1 - amt) + wet x amt. */
static inline void oscSection_ring(int16_t* out, const int16_t* modBuf,
                                   float amount, uint8_t size)
{
	uint8_t i;

	for (i = 0u; i < size; i++) {
		const int32_t ring = ((int32_t)out[i] * (int32_t)modBuf[i]) >> 15;

		out[i] = (int16_t)__SSAT((int32_t)((float)out[i] * (1.0f - amount)) +
		                         (int32_t)((float)ring * amount), 16);
	}
}

INITCM void oscSection_renderDrum(OscInfo* car, OscInfo* mod, uint8_t mode,
                                  float amount, int16_t* buf, int16_t* modBuf,
                                  uint8_t size, OscInfo* oldCar, int16_t* oldBuf)
{
	if (!car || !mod || !buf || !modBuf)
		return;
	if (!oldBuf)
		oldCar = 0;
	switch (mode) {
	case OSC_MOD_MIX:
		calcNextOscSampleBlock(mod, modBuf, size, amount);
		calcNextOscSampleBlock(car, buf, size, (1.0f - amount));
		if (oldCar) {
			calcNextOscSampleBlock(oldCar, oldBuf, size, (1.0f - amount));
			bufferTool_addBuffersSaturating(oldBuf, modBuf, size);
		}
		bufferTool_addBuffersSaturating(buf, modBuf, size);
		break;
	case OSC_MOD_SNC:
		oscSection_renderSync(car, mod, amount, buf, modBuf, size,
		                      oldCar, oldBuf);
		break;
	case OSC_MOD_RNG:
		calcNextOscSampleBlock(mod, modBuf, size, 1.0f);
		calcNextOscSampleBlock(car, buf, size, 1.0f);
		oscSection_ring(buf, modBuf, amount, size);
		if (oldCar) {
			calcNextOscSampleBlock(oldCar, oldBuf, size, 1.0f);
			oscSection_ring(oldBuf, modBuf, amount, size);
		}
		break;
	case OSC_MOD_FM:
	default:
		calcNextOscSampleBlock(mod, modBuf, size, amount);
		calcNextOscSampleFmBlock(car, modBuf, buf, size, 1.0f);
		if (oldCar)
			calcNextOscSampleFmBlock(oldCar, modBuf, oldBuf, size, 1.0f);
		break;
	}
}

INITCM void oscSection_renderFm2(OscInfo* car, OscInfo* m1, OscInfo* m2,
                                 float g1, float g2, float carGain,
                                 int16_t* buf, int16_t* modBuf,
                                 int16_t* modBuf2, uint8_t size,
                                 OscInfo* oldCar, int16_t* oldBuf)
{
	if (!car || !m1 || !m2 || !buf || !modBuf || !modBuf2)
		return;
	calcNextOscSampleBlock(m1, modBuf, size, g1);
	calcNextOscSampleBlock(m2, modBuf2, size, g2);
	bufferTool_addBuffersSaturating(modBuf, modBuf2, size);
	calcNextOscSampleFmBlock(car, modBuf, buf, size, carGain);
	if (oldCar && oldBuf)
		calcNextOscSampleFmBlock(oldCar, modBuf, oldBuf, size, carGain);
}

void oscSection_blendItp(OscSectionState* s, int16_t* buf,
                         const int16_t* oldBuf, uint8_t size)
{
	uint8_t i;

	if (!s || !buf || !oldBuf || s->itpState != OSC_ITP_ACTIVE)
		return;
	for (i = 0u; i < size; i++) {
		float t = (float)(s->itpPos + i) * (1.0f / (float)OSC_ITP_SAMPLES);

		if (t > 1.0f)
			t = 1.0f;
		buf[i] = (int16_t)((float)oldBuf[i] +
		                   t * ((float)buf[i] - (float)oldBuf[i]));
	}
}

void oscSection_finishBlock(OscSectionState* s, uint8_t size)
{
	if (!s || s->itpState != OSC_ITP_ACTIVE)
		return;
	s->itpPos = (uint16_t)(s->itpPos + size);
	if (s->itpPos >= OSC_ITP_SAMPLES)
		s->itpState = OSC_ITP_IDLE;
}

void oscSection_noteResoTarget(OscSectionState* s, float q)
{
	if (s)
		s->qTarget = q;
}

void oscSection_filterRecalc(OscSectionState* s, ResonantFilter* f)
{
	if (!s || !f)
		return;
	if (s->runMode == OSC_RUN_RST) {
		SVF_recalcFreq(f);
		s->fSmooth = f->f;
		s->qSmooth = f->q;
		s->qTarget = f->q;
		return;
	}
	if (s->fSmooth < 0.0f) {
		s->fSmooth = f->f;
		s->qSmooth = f->q;
		s->qTarget = f->q;
	}
	if (f->q != s->qSmooth)       /* modulation wrote q since the last block */
		s->qTarget = f->q;
	s->fSmooth += (f->f - s->fSmooth) * OSC_FILTER_SLEW_COEF;
	s->qSmooth += (s->qTarget - s->qSmooth) * OSC_FILTER_SLEW_COEF;
	f->q = s->qSmooth;
	SVF_recalcFreqFrom(f, s->fSmooth);
}
```

- **Copy** the C10.1 blocks above each definition.
- **`oscSection_renderDrum()` note:** "fm and mix call order is the pre-S085
  Drum order; with oldCar NULL the output is bit-identical (osc_s0)".
- **`oscSection_filterRecalc()` note:** "the exact float compare is valid:
  the write-back is a plain copy; one compare per slot per block; the ZDF loop
  is untouched".

### C10.3 `snc` placement note (no code)

The sync slave render now ends at sample `i`, inclusive, before the reset.
The master wraps between samples `i` and `i + 1`, so sample `i` belongs to the
old cycle. The spike started the new segment at `i`, one sample early (a
22 µs offset). This schedule corrects it. The `osc_run` test checks it
(C17.6, reset index check).

### C10.4 `Core/DSPAudio/ResonantFilter.h` — declaration

- **Where:** after line 118, `void SVF_recalcFreq(ResonantFilter* filter);`.
- **Action:** **Add.**

```c
/*
 * Integrator gain from a given cutoff (S085 C10.4).
 *
 * What:       filter->g = fastTan(pi x fUsed) without changing filter->f; the
 *             same USE_SHAPER_NONLINEARITY step as SVF_recalcFreq().
 * Why:        run modes fre/itp slew the cutoff the loop uses while the
 *             writers keep setting the target in filter->f (S085 D7).
 * Inputs:     filter, fUsed (0..0.45).  Outputs: filter->g.
 * Accessors:  oscSection_filterRecalc().
 * Affiliates: SVF_recalcFreq() (unchanged; rst and the Effect filter).
 */
void SVF_recalcFreqFrom(ResonantFilter* filter, float fUsed);
```

### C10.5 `Core/DSPAudio/ResonantFilter.c` — definition

- **Where:** after line 113, the end of `SVF_recalcFreq()`.
- **Action:** **Add**, with the C10.4 block.

```c
INITCM_EFFECT void SVF_recalcFreqFrom(ResonantFilter* filter, float fUsed)
{
	if (!filter)
		return;
	filter->g = fastTan(M_PI * fUsed);
#if USE_SHAPER_NONLINEARITY
	setDistortionShape(&filter->shaper, filter->drive);
#endif
}
```

### C10.6 The `snc` cost decision (at G3)

1. **Measure** with `armcheck-osc` (C17.4). The worst case is the master at
   maximum pitch, a 1-sample segment per sample:
   - (a) the wrap loop per sample;
   - (b) one renderer call's fixed overhead (dispatch + prologue + epilogue,
     from the probe's straight-line count);
   - (c) the slave renderer's per-sample count.

   Per-sample worst case = (a) + (b) + (c), plus the mix pass.
2. **Compare** against the cap: the FM carrier loop + 10. The FM carrier
   loop is the pre-S085 `calcFmBlock` count, 35 today, so the cap is about
   45.
3. **Within the cap:** done; record it.
4. **Over the cap:** before G3 closes, replace the segment calls with
   per-type sync loops.
   - Each slave family (table/stock, sine, pwm, sample, wtb) gets one loop.
     It advances the shadow master and the slave phase and resets the slave
     inside the loop.
   - The reads use the same `static inline` helpers its renderer uses. Make
     them visible through an internal header `OscReaders.h` (no new state).
   - The noise and crash slaves sync as table reads of their own sources.
   - Cost: constant per sample, about (c) + 6.
   - Record the decision and the figures in the handoff log.

---
## 12. WP11 — Engines

The engines keep their structure. Three things change:
- the trigger asks the section what to reset;
- the oscillator render goes through the section, and while `itp` is active
  the fading carrier copy is rendered into the pool scratch and blended;
- the block ends with `oscSection_finishBlock()`.

**Unchanged:**
- the transient tests `transGen.waveform == 0/1`;
- the fused post-chains `voicePost_drum()`, `_mixAddGainDist()` and
  `_addGainDist()`;
- `SVF_calcBlockZDF()`;
- the async functions.

### C11.1 Engine headers — `OscSectionState section`

| File | Include | Field |
|---|---|---|
| `Core/DSP/Instruments/Drum/DrumVoice.h` | after line 51 `#include "dither.h"`: `#include "OscSection.h"` | before line 114 `} DrumVoice;`, after `Dither dither;` |
| `Core/DSP/Instruments/Snare/Snare.h` | after line 50 `#include "snapEg.h"` | before line 100 `} SnareVoice;` |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.h` | after line 50 `#include "snapEg.h"` | before line 92 `} CymbalVoice;` |
| `Core/DSP/Instruments/HiHat/HiHat.h` | after line 50 `#include "snapEg.h"` | before line 98 `} HiHatVoice;` |

```c
	/*
	 * Oscillator section state (S085 C11.1).
	 *
	 * What:       run mode, the itp carrier snapshot and crossfade, and the
	 *             filter slew for this voice (OscSection.h).
	 * Why:        plan §4.3.7: run modes apply per voice; the snapshot holds
	 *             one carrier (about 140 B), inside the runtime-slot reserve.
	 * Inputs:     the run row (IM_SPECIAL_RUN_MODE); triggers; pool grants.
	 * Outputs:    used by this engine's trigger and render, and by
	 *             InstrumentManager (filter recalc, claims).
	 * Accessors:  this engine's .c, InstrumentManager.c.
	 * Affiliates: OscSection.c, OscRenderPool.c.
	 */
	OscSectionState section;
```

### C11.2 `DrumVoice.c`

**Includes:** after line 48 `#include "voicePostChain.h"`, add
`#include "OscillatorSample.h"` and `#include "OscRenderPool.h"`.

**Init (`Drum_initVoice`):**
- line 125: `voice->osc.waveform = TRI+seed_index;` becomes
  `voice->osc.waveform = (uint8_t)(TRI + (seed_index % 3u));`, with the
  comment `/* S085 C11.2: seed limited to Tri/Saw/Rec; 6+ are stock waves
  now. */`;
- after line 144, `voice->decimationRate = 1;`, **add**
  `oscSection_init(&voice->section);`, with the comment `/* S085 C11.2:
  rst until the run row is applied. */`.

**Trigger:**
- **Where:** lines 177–200, from `//only reset phase if envelope is closed`
  through the closing brace of the phase block.
- **Action:** **Modify.** Line 217, `SVF_reset(&voice->filter);`, is
  **modified** too: the reset is now gated by the run mode.

```c
	/*
	 * Run mode decides the trigger resets (S085 C11.2).
	 *
	 * What:       rst: today's carrier phase reset (the modulator runs free,
	 *             as before) and the filter clear; fre: no reset, both
	 *             oscillators slew, the filter rings; itp: the carrier is
	 *             snapshotted and reset, the modulator slews, the filter
	 *             rings. The carrier start phase depends on its type: smp/slp
	 *             restart at the sample start; pwm/wtb and the table waves at
	 *             the table offset; sine at its offset; others at 0.
	 * Why:        plan §4.3.7.
	 * Affiliates: oscSection_beginTrigger(), osc_isTableWave(),
	 *             oscSample_restart().
	 */
	{
		OscInfo* const mods[1] = { &voice->modOsc };
		const uint8_t reset = oscSection_beginTrigger(&voice->section,
		                                              &voice->osc, mods, 1u);

		//only reset phase if envelope is closed
#ifdef USE_AMP_FILTER
		if (reset != OSC_TRIGGER_RESET_NONE &&
		    ((voice->volEgValueBlock[15]<=0.01f) || (voice->transGen.waveform==1)))
#else
		if (reset != OSC_TRIGGER_RESET_NONE)
#endif
		{
			float offset = 1;
			if(voice->transGen.waveform==1) //offset mode
			{
				offset -= voice->transGen.volume;
#ifdef USE_AMP_FILTER
				setOnePoleCoef(&voice->ampFilter,1.0f);
			} else {
				setOnePoleCoef(&voice->ampFilter,ampSmoothValue);
#endif
			}
			if (voice->osc.oscType == OSC_TYPE_SMP ||
			    voice->osc.oscType == OSC_TYPE_SLP)
				oscSample_restart(&voice->osc);
			else if (voice->osc.oscType != OSC_TYPE_WAV ||
			         osc_isTableWave(voice->osc.waveform))
				voice->osc.phase = (0xff<<20)*offset;
			else if (voice->osc.waveform == SINE)
				voice->osc.phase = 1024 + ( (0x3ff<<20) - 1024)*offset;
			else
				voice->osc.phase = 0;
		}
		/* S085 C11.2: only rst clears the filter (fre/itp keep it ringing). */
		if (reset == OSC_TRIGGER_RESET_ALL)
			SVF_reset(&voice->filter);
	}
```

- The original final statement, `SVF_reset(&voice->filter);` at line 217, is
  **removed**. It moves into the block above, which sits at the original
  phase-block position.
- The order relative to `osc_setBaseNote()` and the envelope triggers is
  unchanged. The filter reset now happens earlier in the function. Nothing
  between those points reads the filter, so the result is the same.

**Render:**
- **Where:** lines 313–326, from `//calc next mod osc sampleBlock` through
  the `else` branch.
- **Action:** **Modify.** Also **add** before the closing comment (line 367,
  "Channel volume is intentionally…"):
  `oscSection_finishBlock(&voice->section, size);` with
  `/* S085 C11.2: advance or end the itp crossfade. */`.

```c
	/*
	 * Oscillator section (S085 C11.2).
	 *
	 * What:       fm/mix/snc/rng through OscSection; with itp active the old
	 *             carrier renders into the pool scratch with the same live
	 *             modulator block and the two crossfade.
	 * Why:        plan §4.3.7; fm/mix with no crossfade are bit-identical to
	 *             the pre-S085 code (osc_s0).
	 * Affiliates: oscSection_renderDrum(), oscSection_blendItp(),
	 *             oscRenderPool_scratch().
	 */
	{
		OscInfo* const old = oscSection_itpCarrier(&voice->section);
		int16_t* const oldBuf = old ? oscRenderPool_scratch() : 0;

		oscSection_renderDrum(&voice->osc, &voice->modOsc, voice->mixOscs,
		                      voice->fmModAmount, buf, modBuf, size,
		                      old, oldBuf);
		if (old)
			oscSection_blendItp(&voice->section, buf, oldBuf, size);
	}
```

- `voice->mixOscs` now carries 0..3. `oscSection_renderDrum()` renders any
  other value as `fm`.

### C11.3 `Snare.c`

**Includes:** after line 43, add `#include "OscillatorSample.h"` and
`#include "OscRenderPool.h"`.

**Init:** after line 109 `lfo_init(&voice->lfo);`, **add**
`oscSection_init(&voice->section);`.

**Trigger:**
- **Where:** lines 140–150.
- **Action:** **Modify.**

```c
	/*
	 * Run mode decides the trigger resets (S085 C11.3).
	 *
	 * What:       rst: today's tonal-oscillator reset; fre: no reset, the
	 *             tonal and noise oscillators slew; itp: the tonal oscillator
	 *             is snapshotted and reset, the noise oscillator slews. Start
	 *             phase by type as C11.2.
	 * Why:        plan §4.3.7 (Snare's carrier is the tonal oscillator).
	 */
	{
		OscInfo* const mods[1] = { &voice->noiseOsc };
		const uint8_t reset = oscSection_beginTrigger(&voice->section,
		                                              &voice->osc, mods, 1u);

		if (reset != OSC_TRIGGER_RESET_NONE) {
			float offset = 1;
			if(voice->transGen.waveform==1) //offset mode
				offset -= voice->transGen.volume;
			if (voice->osc.oscType == OSC_TYPE_SMP ||
			    voice->osc.oscType == OSC_TYPE_SLP)
				oscSample_restart(&voice->osc);
			else if (voice->osc.oscType != OSC_TYPE_WAV ||
			         osc_isTableWave(voice->osc.waveform))
				voice->osc.phase = (0xff<<20)*offset;
			else if (voice->osc.waveform == SINE)
				voice->osc.phase = (0x3ff<<20)*offset;
			else
				voice->osc.phase = 0;
		}
	}
```

**Render:**
- **Where:** line 237,
  `calcNextOscSampleBlock(&voice->osc,transBuf,size,(1.f-voice->mix));`.
- **Action:** **Modify** (add after it). Also **add**
  `oscSection_finishBlock(&voice->section, size);` as the last statement of
  the function, after the post-chain block (line 257).

```c
	calcNextOscSampleBlock(&voice->osc,transBuf,size,(1.f-voice->mix));
	/* S085 C11.3: itp crossfade of the tonal oscillator (same gain). */
	{
		OscInfo* const old = oscSection_itpCarrier(&voice->section);

		if (old) {
			int16_t* const oldBuf = oscRenderPool_scratch();

			calcNextOscSampleBlock(old, oldBuf, size, (1.f-voice->mix));
			oscSection_blendItp(&voice->section, transBuf, oldBuf, size);
		}
	}
```

- The noise oscillator renders through `calcNextOscSampleBlock()` /
  `calcNoiseBlock()` with the per-sample slew (C5.6); nothing else changes.

### C11.4 `CymbalVoice.c`

**Includes:** after line 42, add `#include "OscillatorSample.h"` and
`#include "OscRenderPool.h"`.

**Init:** after line 117 `lfo_init(&voice->lfo);`, **add**
`oscSection_init(&voice->section);`.

**Trigger:**
- **Where:** lines 148–161.
- **Action:** **Modify.**

```c
	/*
	 * Run mode decides the trigger resets (S085 C11.4).
	 *
	 * What:       rst: today's resets (carrier at its start phase, both
	 *             modulators to 0 / sample start); fre: no reset, all three
	 *             slew; itp: the carrier is snapshotted and reset, the
	 *             modulators slew (so an itp hit's FM attack is not identical
	 *             from hit to hit; rst is, plan §4.3.7).
	 */
	{
		OscInfo* const mods[2] = { &voice->modOsc, &voice->modOsc2 };
		const uint8_t reset = oscSection_beginTrigger(&voice->section,
		                                              &voice->osc, mods, 2u);
		uint8_t m;

		if (reset != OSC_TRIGGER_RESET_NONE) {
			float offset = 1;
			if(voice->transGen.waveform==1) //offset mode
				offset -= voice->transGen.volume;
			if (voice->osc.oscType == OSC_TYPE_SMP ||
			    voice->osc.oscType == OSC_TYPE_SLP)
				oscSample_restart(&voice->osc);
			else if (voice->osc.oscType != OSC_TYPE_WAV ||
			         osc_isTableWave(voice->osc.waveform))
				voice->osc.phase = (0xff<<20)*offset;
			else if (voice->osc.waveform == SINE)
				voice->osc.phase = (0x3ff<<20)*offset;
			else
				voice->osc.phase = 0;
		}
		if (reset == OSC_TRIGGER_RESET_ALL) {
			for (m = 0u; m < 2u; m++) {
				if (mods[m]->oscType == OSC_TYPE_SMP ||
				    mods[m]->oscType == OSC_TYPE_SLP)
					oscSample_restart(mods[m]);
				else
					mods[m]->phase = 0;
			}
		}
	}
```

**Render:**
- **Where:** lines 240–247, from `//calc next mod osc sample` through
  `calcNextOscSampleFmBlock(&voice->osc,mod,buf,size,1.f) ;`.
- **Action:** **Modify.** Also **add**
  `oscSection_finishBlock(&voice->section, size);` as the last statement of
  the function (after line 269).

```c
	/* S085 C11.4: two-modulator FM through OscSection (bit-identical with no
	** crossfade); itp renders the old carrier with the same summed modulator
	** and crossfades. */
	{
		OscInfo* const old = oscSection_itpCarrier(&voice->section);
		int16_t* const oldBuf = old ? oscRenderPool_scratch() : 0;

		oscSection_renderFm2(&voice->osc, &voice->modOsc, &voice->modOsc2,
		                     voice->fmModAmount1, voice->fmModAmount2, 1.f,
		                     buf, mod, mod2, size, old, oldBuf);
		if (old)
			oscSection_blendItp(&voice->section, buf, oldBuf, size);
	}
```

### C11.5 `HiHat.c`

- **Includes:** after line 55, add `#include "OscillatorSample.h"` and
  `#include "OscRenderPool.h"`.
- **Init:** after line 130 `lfo_init(&voice->lfo);`, **add**
  `oscSection_init(&voice->section);`.
- **Trigger:** lines 162–172 are **modified** as C11.4, with two differences:
  - the HiHat modulators are never reset (pre-S085 behaviour), so the
    `RESET_ALL` modulator loop is omitted;
  - `mods[2] = { &voice->modOsc, &voice->modOsc2 }` is passed so `fre`/`itp`
    slew them.
- **Render:** lines 257–264, from `//calc next mod osc samples` through
  `calcNextOscSampleFmBlock(&voice->osc,mod1,buf,size,0.5f) ;`, are
  **modified** as C11.4, with buffers `mod1, mod2` and carrier gain
  **`0.5f`**.
- **Finish:** **add** `oscSection_finishBlock(&voice->section, size);` as the
  last statement (after line 287).

---

## 13. WP12 — Descriptor rows, enums, counts and pages

**Rules:**
- Rows are appended, so stored indices, automation IDs, LFO tokens, MIDI
  Learn and NRPN IDs are unchanged.
- A missing key loads 0.

### C12.1 Selector row macro (each of the four `*Parameters.c`)

- **Where:** after the `ROW_MENU_SPECIAL` definition:
  - Drum, Snare and Cymbal: lines 86–87;
  - HiHat: lines 87–88.
- **Action:** **Add.**

```c
/*
 * Selector row (S085 C12.1).
 *
 * What:       a byte selector with a special writer: automatable, not
 *             Morphable, no modulation domain (not an LFO/velocity target).
 * Why:        typ/src/run switch the oscillator's meaning; Morphing or LFO
 *             sweeps across them would jump between types. They are stored as
 *             single-ended [params] cells and applied before the Morph image
 *             (C14.2).
 * Accessors:  this table.
 * Affiliates: instrumentManager_isSelectorDescriptor() (recognises the
 *             class), check_special_tags.py (the macro name ends in
 *             _SPECIAL, so the tag is checked).
 */
#define FLAGS_SELECT INSTRUMENT_PARAM_FLAG_AUTOMATABLE
#define ROW_SELECT_SPECIAL(key_, cat_, long_, short_, member_, special_) \
    { key_, short_, long_, cat_, DTYPE_0B127, FLAGS_SELECT, MOD_NONE, \
      BIND_SPECIAL(member_, TYPE_UINT8, special_) }
```

**At the end of each file** (after the `#undef MOD_NONE` line: Drum 289,
Snare 284, Cymbal 285, HiHat 290), **add**:

```c
#undef FLAGS_SELECT
#undef ROW_SELECT_SPECIAL
```

### C12.2 Wave rows gain the `IM_SPECIAL_OSC_WAVE` tag

- **Action:** **Modify** each `osc*_wave` row from `ROW_MENU(…)` to
  `ROW_MENU_SPECIAL(…, IM_SPECIAL_OSC_WAVE | IM_SPECIAL_OSC<n>)`.
- **Unchanged:** the key, the labels, `MENU_WAVEFORM`, `MOD_WAVE`, the member
  and `TYPE_UINT8`.

| File:line | Row | Tag |
|---|---|---|
| `DrumParameters.c:186` | `osc1_wave` | `IM_SPECIAL_OSC_WAVE \| IM_SPECIAL_OSC1` |
| `DrumParameters.c:189` | `osc2_wave` | `… \| IM_SPECIAL_OSC2` |
| `SnareParameters.c:184` | `osc1_wave` | `… \| IM_SPECIAL_OSC1` |
| `CymbalParameters.c:185, 188, 191` | `osc1_wave`, `osc2_wave`, `osc3_wave` | OSC1, OSC2, OSC3 |
| `HiHatParameters.c:188, 191, 194` | `osc1_wave`, `osc2_wave`, `osc3_wave` | OSC1, OSC2, OSC3 |

- **Why:** the `wav` writer clamps per type and records the request byte
  (C13.4). The per-slot domain recognises the row by its tag, not by key text.

### C12.3 Index enums

- **Action:** **Add** before `<X>_PARAM_COUNT`:

| File:line (before) | Entries |
|---|---|
| `DrumParameters.c:172` | `DRUM_PARAM_OSC1_TYPE, DRUM_PARAM_OSC1_SOURCE, DRUM_PARAM_OSC2_TYPE, DRUM_PARAM_OSC2_SOURCE, DRUM_PARAM_RUN_MODE,` |
| `SnareParameters.c:170` | `SNARE_PARAM_OSC1_TYPE, SNARE_PARAM_OSC1_SOURCE, SNARE_PARAM_RUN_MODE,` |
| `CymbalParameters.c:171` | `CYMBAL_PARAM_OSC1_TYPE, CYMBAL_PARAM_OSC1_SOURCE, CYMBAL_PARAM_OSC2_TYPE, CYMBAL_PARAM_OSC2_SOURCE, CYMBAL_PARAM_OSC3_TYPE, CYMBAL_PARAM_OSC3_SOURCE, CYMBAL_PARAM_RUN_MODE,` |
| `HiHatParameters.c:174` | the same seven with `HIHAT_` |

### C12.4 Rows

- **Where:** after the last row (`transient_freq`): Drum 237, Snare 234,
  Cymbal 236, HiHat 239.
- **Action:** **Add**. The comment block is written once per table.

```c
    /*
     * Oscillator type/source and voice run mode (S085 C12.4).
     *
     * What:       typ = OSC_TYPE_* per oscillator; src = pwm family, sample or
     *             wavetable index; run = OSC_RUN_* for the whole voice.
     * Why:        plan §5 item 5. Appended, so existing indices, automation
     *             IDs, LFO tokens, MIDI Learn and NRPN IDs are unchanged; a
     *             missing key loads 0 (wav / 0 / rst).
     * Inputs:     menu, MIDI (menu edits), automation, Kit/Instrument files
     *             (keys below).
     * Outputs:    the IM_SPECIAL_OSC_TYPE / _OSC_SOURCE / _RUN_MODE writers.
     * Accessors:  InstrumentManager.c, InstrumentCellFormat.c, menu.c.
     * Affiliates: this table's menu pages (C12.6).
     */
```

| Table | Rows |
|---|---|
| Drum | `ROW_SELECT_SPECIAL("osc1_type", "Oscilltr", "Type", "typ", osc.oscType, IM_SPECIAL_OSC_TYPE \| IM_SPECIAL_OSC1)`; `("osc1_source", "Oscilltr", "Source", "src", osc.source, IM_SPECIAL_OSC_SOURCE \| IM_SPECIAL_OSC1)`; `("osc2_type", "FM", "Type", "typ", modOsc.oscType, … OSC2)`; `("osc2_source", "FM", "Source", "src", modOsc.source, … OSC2)`; `("osc_run_mode", "Oscilltr", "RunMode", "run", section.runMode, IM_SPECIAL_RUN_MODE)` |
| Snare | `osc1_type`, `osc1_source` (category "Oscilltr", member `osc.*`), `osc_run_mode` |
| Cymbal / HiHat | `osc1_type`, `osc1_source` ("Oscilltr", `osc.*`, OSC1); `osc2_type`, `osc2_source` ("Mod Osc", `modOsc.*`, OSC2); `osc3_type`, `osc3_source` ("Mod Osc", `modOsc2.*`, OSC3); `osc_run_mode` |

### C12.5 Counts

| File:line | Old | New |
|---|---:|---:|
| `DrumParameters.h:14` `DRUM_PARAM_DESCRIPTOR_COUNT` | 39 | **44** |
| `SnareParameters.h:14` | 38 | **41** |
| `CymbalParameters.h:14` | 39 | **46** |
| `HiHatParameters.h:14` | 39 | **46** |

- The existing `_Static_assert(<X>_PARAM_DESCRIPTOR_COUNT == <X>_PARAM_COUNT)`
  in each `.c` proves the enum.
- 46 ≤ the 64-row cap.
- `check_special_tags.py` reports 177 rows (155 + 22).

### C12.6 Menu cells

Pages hold 16 cells, shown four per screen behind the same SELECT button.

| Page row (file:line) | New cells |
|---|---|
| Drum page 0 (`DrumParameters.c:255`) | cells 3–5: `DRUM_PARAM_OSC1_TYPE, DRUM_PARAM_OSC1_SOURCE, DRUM_PARAM_RUN_MODE` |
| Drum FM page (`:258`) | cells 4–5: `DRUM_PARAM_OSC2_TYPE, DRUM_PARAM_OSC2_SOURCE` |
| Snare page 0 (`SnareParameters.c:250`) | cells **5–7** (S085 D3): `SNARE_PARAM_OSC1_TYPE, SNARE_PARAM_OSC1_SOURCE, SNARE_PARAM_RUN_MODE` |
| Cymbal page 0 (`CymbalParameters.c:251`) | cells 3–5: `CYMBAL_PARAM_OSC1_TYPE, CYMBAL_PARAM_OSC1_SOURCE, CYMBAL_PARAM_RUN_MODE` |
| Cymbal Mod page (`:254`) | cells 6–9: `CYMBAL_PARAM_OSC2_TYPE, CYMBAL_PARAM_OSC2_SOURCE, CYMBAL_PARAM_OSC3_TYPE, CYMBAL_PARAM_OSC3_SOURCE` |
| HiHat page 0 (`HiHatParameters.c:256`), Mod page (`:259`) | as Cymbal, `HIHAT_` names |

- **Action:** **Modify.** Replace the `INSTRUMENT_MENU_EMPTY` at those
  positions.
- **Comment:** add above each page table: `/* S085 C12.6: typ/src/run on the
  oscillator page; the modulators' typ/src on their page. */`.

---
## 14. WP13 — InstrumentManager

### C13.1 `InstrumentManager.h` — writer IDs and accessor declarations

- **Writer IDs:** after line 149 `IM_SPECIAL_LFO_OFFSET,`, **add**:

```c
    /*
     * Oscillator type/source/wave and voice run mode (S085 C13.1).
     *
     * What:       OSC_TYPE/OSC_SOURCE/OSC_WAVE carry an IM_SPECIAL_OSC_*
     *             selector; their writers clamp per type, keep the request
     *             bytes (retained writes only), rebind sample/wavetable state
     *             and refresh the wav modulation domains. RUN_MODE writes the
     *             voice's OscSectionState.runMode.
     * Why:        plan §5 item 5. IDs 20..23; the count becomes 24 (<= 32).
     */
    IM_SPECIAL_OSC_TYPE,
    IM_SPECIAL_OSC_SOURCE,
    IM_SPECIAL_OSC_WAVE,
    IM_SPECIAL_RUN_MODE,
```

- **Declarations:** before line 729 `#endif`, **add** the following. They use
  forward declarations, so `InstrumentManager.h` does not pull the DSP
  headers into its many includers.

```c
struct OscStruct;
struct OscSectionStateStruct;

/*
 * Oscillator of a descriptor's selector (S085 C13.1).
 *
 * What:       resolves an OSC_TYPE/OSC_SOURCE/OSC_WAVE row's IM_SPECIAL_OSC_*
 *             selector against the slot's live runtime.
 * Inputs:     slot, descriptor.  Outputs: borrowed oscillator or NULL.
 * Accessors:  InstrumentCellFormat.c, InstrumentManager.c.
 * Affiliates: instrumentManager_oscBySelector().
 */
struct OscStruct *instrumentManager_oscForDescriptor(
    uint8_t slot, const ParamDescriptor *descriptor);

/*
 * A slot's oscillator section (S085 C13.1).
 * What: the live engine's OscSectionState, or NULL.
 * Accessors: InstrumentManager.c (run writer, filter, claims),
 *            InstrumentCellFormat.c.
 */
struct OscSectionStateStruct *instrumentManager_oscSection(uint8_t slot);

/*
 * Type-dependent maxima (S085 C13.1).
 *
 * What:       wav row: OSC_STOCK_WAVE_COUNT - 1 for wav, 127 otherwise.
 *             src row: OSC_PWM_FAMILY_COUNT - 1 for pwm, installed samples - 1
 *             for smp/slp, installed wavetables - 1 for wtb, 0 for wav
 *             (0 when nothing is installed).
 *             The ...ForType forms take the type directly (the formatter
 *             reads it from the Scene image, S085 D5).
 * Why:        one rule for the writer clamp, the modulation domain and the
 *             menu.
 * Accessors:  InstrumentManager.c, InstrumentCellFormat.c.
 */
uint8_t instrumentManager_oscWaveMaxForType(uint8_t oscType);
uint8_t instrumentManager_oscSourceMaxForType(uint8_t oscType);

/*
 * Is this descriptor a selector row? (S085 C13.1)
 *
 * What:       1 for INSTANCE_OFFSET rows with MORPHABLE clear and any special
 *             writer (today: typ, src, run); 0 otherwise, including NULL.
 * Why:        Preset stores these through the supplemental path and applies
 *             them before the Morph image (C14). The class, not a key list,
 *             so a future selector row cannot miss the pre-pass.
 * Accessors:  presetManager.c (three sites).
 * Affiliates: ROW_SELECT_SPECIAL (C12.1).
 */
uint8_t instrumentManager_isSelectorDescriptor(
    const ParamDescriptor *descriptor);

/*
 * Render-pool claims for itp crossfades (S085 C13.1).
 *
 * What:       Ongoing: every slot with an ACTIVE crossfade keeps its unit.
 *             Pending: every slot with a PENDING crossfade claims one unit,
 *             lowest slot first; refused, that trigger behaves as fre.
 * Why:        Q14 claim order: ongoing itp, interpolation (in between, as
 *             modulation applies), new itp.
 * Accessors:  mixer_calcNextSampleBlock() (C9.4).
 * Affiliates: oscSection_claimOngoing()/_claimPending(), OscRenderPool.c.
 */
void instrumentManager_claimOscSectionOngoing(void);
void instrumentManager_claimOscSectionPending(void);
```

### C13.2 `InstrumentManager.c` — fixed runtime-slot reserve

- **Where:** lines 166–168:

  ```c
  #define INSTRUMENT_RUNTIME_CURRENT_MAX_BYTES sizeof(DrumVoice)
  #define INSTRUMENT_RUNTIME_SLOT_BYTES \
      (2u * INSTRUMENT_RUNTIME_CURRENT_MAX_BYTES)
  ```

- **Action:** **Modify.** Also update the comment above (lines 159–165): "The
  1,176 B reserve is fixed (S085 C13.2)".

```c
/*
 * Fixed tagged-slot reserve (S085 C13.2).
 *
 * What:       1,176 B per slot, the established reserve (formerly expressed
 *             as twice the largest engine, 2 x 588 B).
 * Why:        S085 adds the oscillator type/slew fields and each engine's
 *             OscSectionState; 2 x sizeof(DrumVoice) would grow the reserve
 *             with them (six slots of new SRAM). The growth is approved inside
 *             the existing reserve only; the _Static_asserts below prove the
 *             fit (estimates: Drum ~824, Snare ~656, Cymbal ~760, HiHat
 *             ~768 B; measured at G3).
 * Affiliates: STORAGE_SRAM_MANIFEST.md, the asserts below.
 */
#define INSTRUMENT_RUNTIME_SLOT_BYTES 1176u
```

### C13.3 `InstrumentManager.c` — includes and forward declarations

- **Includes:** after line 16 `#include "modulationNode.h"`, **add**
  `#include "OscSection.h"`, `#include "OscillatorSample.h"`,
  `#include "OscillatorWavetable.h"`, `#include "OscRenderPool.h"` and
  `#include "SampleMemory.h"`.
- **Forward declarations:** after line 224 (the
  `instrumentManager_writeRuntimeInternal` prototype), **add**
  prototypes for `instrumentManager_lfoAdapterDescriptor()` (defined at
  2827), `instrumentManager_installVelocityModulationTarget()` (3066),
  `instrumentManager_descriptorModDomainForSlot()` and
  `instrumentManager_refreshWaveDomains()`.

### C13.4 `InstrumentManager.c` — special writers

**Signature:**
- **Where:** line 3207,
  `static uint8_t instrumentManager_writeSpecialRuntime(uint8_t slot, const ParamDescriptor *descriptor, instrument_param_value_t value)`.
- **Action:** **Modify.** Add the parameter `uint8_t retained` and the
  paragraph below to its comment. The single caller (line 3519) passes
  `notify_base_change`: `instrumentManager_writeSpecialRuntime(slot,
  descriptor, value, notify_base_change)`.

```text
 * retained (S085 C13.4, D2): 1 for retained writes (menu, MIDI, file apply,
 *             Morph, automation; writeRuntimeInternal's notify_base_change),
 *             0 for LFO overlays. Only retained writes update the oscillator
 *             request bytes, so an LFO block never replaces the stored wav or
 *             src byte that a later type change re-derives from.
```

**Reso writer:**
- **Where:** lines 3260–3264, `case IM_SPECIAL_FILTER_RESO`.
- **Action:** **Add** after `SVF_setReso(filter, byteValue / 127.0f);`:

```c
        /* S085 C13.4: the fre/itp filter slew follows the writer's q. */
        oscSection_noteResoTarget(instrumentManager_oscSection(slot), filter->q);
```

**New cases:**
- **Where:** before line 3385 `case IM_SPECIAL_NONE:`.
- **Action:** **Add.**

```c
    /*
     * Oscillator type/source/wave and run-mode writers (S085 C13.4).
     *
     * What:       OSC_TYPE sets the type, re-derives waveform and source from
     *             the request bytes against the new type's maxima, clears the
     *             frequency cache, binds sample/wavetable state, and refreshes
     *             the slot's wav modulation domains; entering wtb clears the
     *             wtb binding first (S084 P2 F11: the union bytes belonged to
     *             pwm). OSC_SOURCE and OSC_WAVE record the request byte
     *             (retained writes only, D2) and clamp the live field. RUN_MODE
     *             clamps to 0..2.
     * Why:        plan §5 item 5; a clamp is lossy, the request bytes make a
     *             type change reversible (pwm width survives pwm -> wav ->
     *             pwm).
     * Inputs:     slot, descriptor (selector), byte, retained.
     * Outputs:    OscInfo fields / OscSectionState.runMode; 1 = consumed.
     * Accessors:  instrumentManager_writeRuntimeInternal() (menu, MIDI,
     *             automation, Morph, file apply, LFO overlays on the wav row).
     * Affiliates: oscSample_bind(), oscWavetable_bind(),
     *             instrumentManager_refreshWaveDomains(),
     *             InstrumentCellFormat.c (same maxima).
     */
    case IM_SPECIAL_OSC_TYPE: {
        OscInfo *osc = instrumentManager_oscBySelector(
            slot, (uint8_t)(special & IM_SPECIAL_OSC_MASK));
        uint8_t type;
        uint8_t max;

        if (!osc) return 0u;
        type = (byteValue < OSC_TYPE_COUNT) ? byteValue
                                            : (uint8_t)(OSC_TYPE_COUNT - 1u);
        if (type == OSC_TYPE_WTB && osc->oscType != OSC_TYPE_WTB) {
            osc->wtb.base = 0u;
            osc->wtb.primed = 0u;
        }
        osc->oscType = type;
        max = instrumentManager_oscWaveMaxForType(type);
        osc->waveform = (osc->waveRequest > max) ? max : osc->waveRequest;
        max = instrumentManager_oscSourceMaxForType(type);
        osc->source = (osc->sourceRequest > max) ? max : osc->sourceRequest;
        osc->freqCacheValid = 0u;
        if (type == OSC_TYPE_SMP || type == OSC_TYPE_SLP)
            oscSample_bind(osc);
        else if (type == OSC_TYPE_WTB)
            oscWavetable_bind(osc);
        instrumentManager_refreshWaveDomains(slot);
        return 1u; }
    case IM_SPECIAL_OSC_SOURCE: {
        OscInfo *osc = instrumentManager_oscBySelector(
            slot, (uint8_t)(special & IM_SPECIAL_OSC_MASK));
        uint8_t max;

        if (!osc) return 0u;
        if (retained)
            osc->sourceRequest = byteValue;
        max = instrumentManager_oscSourceMaxForType(osc->oscType);
        osc->source = (byteValue > max) ? max : byteValue;
        osc->freqCacheValid = 0u;
        if (osc->oscType == OSC_TYPE_SMP || osc->oscType == OSC_TYPE_SLP)
            oscSample_bind(osc);
        else if (osc->oscType == OSC_TYPE_WTB)
            oscWavetable_bind(osc);
        return 1u; }
    case IM_SPECIAL_OSC_WAVE: {
        OscInfo *osc = instrumentManager_oscBySelector(
            slot, (uint8_t)(special & IM_SPECIAL_OSC_MASK));
        uint8_t max;

        if (!osc) return 0u;
        if (retained)
            osc->waveRequest = byteValue;
        max = instrumentManager_oscWaveMaxForType(osc->oscType);
        osc->waveform = (byteValue > max) ? max : byteValue;
        if (osc->oscType == OSC_TYPE_SMP || osc->oscType == OSC_TYPE_SLP)
            oscSample_bind(osc);          /* start position follows wav */
        return 1u; }
    case IM_SPECIAL_RUN_MODE: {
        OscSectionState *section = instrumentManager_oscSection(slot);

        if (!section) return 0u;
        section->runMode = (byteValue < OSC_RUN_COUNT)
            ? byteValue : (uint8_t)(OSC_RUN_COUNT - 1u);
        return 1u; }
```

**Velocity on the `wav` row (a direct ModulationNode pointer):**
- It writes `osc->waveform` without passing the writer, so it touches
  neither the request bytes nor the clamp.
- Its range comes from the per-slot domain (C13.6).
- Document this in the OSC_WAVE comment: "Not tracked: velocity modulation
  (direct pointer)".

### C13.5 `InstrumentManager.c` — oscillator accessors and maxima

- **Where:** after line 2042, the end of `instrumentManager_oscBySelector()`.
- **Action:** **Add.** Copy the C13.1 blocks above each.

```c
uint8_t instrumentManager_oscWaveMaxForType(uint8_t oscType)
{
    return (oscType == OSC_TYPE_WAV) ? (uint8_t)(OSC_STOCK_WAVE_COUNT - 1u)
                                     : 127u;
}

uint8_t instrumentManager_oscSourceMaxForType(uint8_t oscType)
{
    uint8_t count;

    switch (oscType) {
    case OSC_TYPE_PWM:
        return (uint8_t)(OSC_PWM_FAMILY_COUNT - 1u);
    case OSC_TYPE_SMP:
    case OSC_TYPE_SLP:
        count = sampleMemory_getNumSamples();
        return count ? (uint8_t)(count - 1u) : 0u;
    case OSC_TYPE_WTB:
        count = sampleMemory_getNumWavetables();
        return count ? (uint8_t)(count - 1u) : 0u;
    default:
        return 0u;
    }
}

uint8_t instrumentManager_isSelectorDescriptor(const ParamDescriptor *descriptor)
{
    if (!descriptor ||
        descriptor->runtime.kind != INSTRUMENT_BIND_INSTANCE_OFFSET ||
        (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE))
        return 0u;
    return (uint8_t)((descriptor->runtime.special & IM_SPECIAL_WRITER_MASK) !=
                     IM_SPECIAL_NONE);
}

OscSectionState *instrumentManager_oscSection(uint8_t slot)
{
    switch (instrumentManager_slotType(slot)) {
    case INSTRUMENT_TYPE_DRM: {
        DrumVoice *v = instrumentManager_drumRuntime(slot);
        return v ? &v->section : 0; }
    case INSTRUMENT_TYPE_SNR: {
        SnareVoice *v = instrumentManager_snareRuntime(slot);
        return v ? &v->section : 0; }
    case INSTRUMENT_TYPE_CYM: {
        CymbalVoice *v = instrumentManager_cymbalRuntime(slot);
        return v ? &v->section : 0; }
    case INSTRUMENT_TYPE_HAT: {
        HiHatVoice *v = instrumentManager_hihatRuntime(slot);
        return v ? &v->section : 0; }
    default:
        return 0;
    }
}

OscInfo *instrumentManager_oscForDescriptor(uint8_t slot,
                                            const ParamDescriptor *descriptor)
{
    if (!descriptor || slot >= INSTRUMENT_SLOT_COUNT)
        return 0;
    return instrumentManager_oscBySelector(
        slot, (uint8_t)(descriptor->runtime.special & IM_SPECIAL_OSC_MASK));
}

/*
 * A slot's carrier (S085 C13.5).
 *
 * What:       the oscillator itp crossfades: the main oscillator of every
 *             engine (osc).
 * Accessors:  instrumentManager_claimOscSectionPending().
 */
static OscInfo *instrumentManager_carrier(uint8_t slot)
{
    return instrumentManager_oscBySelector(slot, IM_SPECIAL_OSC1);
}
```

### C13.6 `InstrumentManager.c` — per-slot `wav` domain everywhere (S085 D11)

**D11 (a defect in the spike):** three call sites read the `wav` row's domain.
The spike made only the first one slot-aware:
- `instrumentManager_resolveModulationTarget()` (line 2378);
- `instrumentManager_buildModulationRange()` (line 2286), which builds the
  velocity range;
- the base update in `instrumentManager_noteRuntimeValueChanged()`
  (line 2202).

So velocity on the `wav` row of a `pwm`/`smp`/`wtb` oscillator was capped at
10, and so was the LFO base. All three use the per-slot domain here.

**Add** after line 742 (the end of
`instrumentManager_descriptorSupportsModulationRange()`):

```c
/*
 * Modulation domain for a slot's descriptor (S085 C13.6).
 *
 * What:       the descriptor domain, with an OSC_WAVE row's maximum taken
 *             from that slot oscillator's current type (wav: 10; others: 127).
 * Why:        LFO and velocity sweeps of wav stay inside the selected type's
 *             range (plan §5 item 5); S085 D11 makes every domain use of the
 *             row slot-aware.
 * Inputs:     slot, descriptor.  Outputs: *domain_out; 1 when modulatable.
 * Accessors:  resolveModulationTarget(), buildModulationRange(),
 *             noteRuntimeValueChanged(), refreshWaveDomains().
 */
static uint8_t instrumentManager_descriptorModDomainForSlot(
    uint8_t slot, const ParamDescriptor *descriptor,
    instrument_mod_domain_t *domain_out)
{
    instrument_mod_domain_t domain;

    if (!instrumentManager_descriptorModDomain(descriptor, &domain))
        return 0u;
    if ((descriptor->runtime.special & IM_SPECIAL_WRITER_MASK) ==
        IM_SPECIAL_OSC_WAVE) {
        OscInfo *osc = instrumentManager_oscForDescriptor(slot, descriptor);

        if (!osc)
            return 0u;
        domain.max_value = instrumentManager_oscWaveMaxForType(osc->oscType);
    }
    if (domain_out)
        *domain_out = domain;
    return 1u;
}

/*
 * Refresh a slot's wav modulation domains after a type change (S085 C13.6).
 *
 * What:       every active LFO adapter targeting an OSC_WAVE row of `slot`
 *             recomputes its domain and clamps its base; every velocity node
 *             whose direct destination is such a row is reinstalled so its
 *             cached range follows.
 * Accessors:  the IM_SPECIAL_OSC_TYPE writer.
 * Affiliates: instrumentManager_installVelocityModulationTarget().
 */
static void instrumentManager_refreshWaveDomains(uint8_t slot)
{
    uint8_t source;
    uint8_t pair;

    if (slot >= INSTRUMENT_SLOT_COUNT)
        return;
    for (source = 0u; source < INSTRUMENT_SLOT_COUNT; source++) {
        ModulationNode *node = &velocityModulators[source];

        for (pair = 0u; pair < 2u; pair++) {
            instrument_lfo_target_adapter_t *adapter =
                &lfo_descriptor_targets[source][pair];
            const ParamDescriptor *d;

            if (!adapter->active || adapter->slot != slot)
                continue;
            d = instrumentManager_lfoAdapterDescriptor(adapter);
            if (!d || (d->runtime.special & IM_SPECIAL_WRITER_MASK) !=
                          IM_SPECIAL_OSC_WAVE)
                continue;
            if (instrumentManager_descriptorModDomainForSlot(slot, d,
                                                            &adapter->domain) &&
                adapter->base_value > adapter->domain.max_value)
                adapter->base_value = adapter->domain.max_value;
        }
        if (node->destinationMode == MOD_NODE_DEST_DIRECT_PARAMETER &&
            instrumentParam_isVoiceParameter(node->destination) &&
            instrumentParam_slot(node->destination) == slot) {
            const ParamDescriptor *d = instrumentManager_descriptor(
                instrumentManager_slotType(slot),
                instrumentParam_local(node->destination));

            if (d && (d->runtime.special & IM_SPECIAL_WRITER_MASK) ==
                         IM_SPECIAL_OSC_WAVE)
                (void)instrumentManager_installVelocityModulationTarget(
                    source, node->destination);
        }
    }
}
```

**Modify** the three call sites:

| Line | From | To |
|---|---|---|
| 2378 (in `resolveModulationTarget`) | `instrumentManager_descriptorModDomain(descriptor, &target_out->domain)` | `instrumentManager_descriptorModDomainForSlot(target_slot, descriptor, &target_out->domain)` |
| 2262–2290 (`buildModulationRange`) | the function calls `instrumentManager_descriptorModDomain(descriptor, &domain)` itself | **add** a parameter `uint8_t slot` and call `…ForSlot(slot, descriptor, &domain)`; its caller (line 2382) passes `target_slot` |
| 2202 (in `noteRuntimeValueChanged`) | `instrumentManager_descriptorModDomain(descriptor, &domain)` | `instrumentManager_descriptorModDomainForSlot(slot, descriptor, &domain)` |

**Comment:** each modified line gets
`/* S085 C13.6 (D11): per-slot wav domain. */`.

### C13.7 `InstrumentManager.c` — itp claims

- **Where:** after line 1552, the end of
  `instrumentManager_dispatchRuntimeLfos()`.
- **Action:** **Add**, with the C13.1 block.

```c
void instrumentManager_claimOscSectionOngoing(void)
{
    uint8_t slot;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        oscSection_claimOngoing(instrumentManager_oscSection(slot));
}

void instrumentManager_claimOscSectionPending(void)
{
    uint8_t slot;

    for (slot = 0u; slot < INSTRUMENT_SLOT_COUNT; slot++)
        oscSection_claimPending(instrumentManager_oscSection(slot),
                                instrumentManager_carrier(slot));
}
```

### C13.8 `InstrumentManager.c` — filter recalc through the section

- **Where:** lines 1660–1691, `instrumentManager_recalcSlotFilter()`.
- **Action:** **Modify.** In each of the four cases, replace
  `SVF_recalcFreq(&voice->filter);` with
  `oscSection_filterRecalc(&voice->section, &voice->filter);`. Then append
  to the comment:

```text
 * S085 C13.8: through the oscillator section. rst is exactly
 * SVF_recalcFreq() (S0); fre/itp slew cutoff and resonance (Q15).
 * Affiliates: oscSection_filterRecalc(), SVF_recalcFreqFrom().
```

### C13.9 `InstrumentManager.c` — diagnostic classifier

- **Where:** lines 3398–3447, `instrumentManager_classifySpecialKey()`
  (`DEV_MODE_DIAGNOSTIC`).
- **Action:** **Modify.**
  - Inside `if (osc != 0xFFu) { … }`, after the `pitch_fine` test, **add**
    the three tests below.
  - Before `return IM_SPECIAL_NONE;`, **add**
    `if (strcmp(key, "osc_run_mode") == 0) return IM_SPECIAL_RUN_MODE;`.

```c
        /*
         * S085 C13.9: oscN_type / oscN_source / oscN_wave. osc2_mod_type is
         * the fm/mix/snc/rng row (no special tag) and is excluded.
         */
        if (strstr(key, "_type") && !strstr(key, "_mod_type"))
            return IM_SPECIAL_OSC_TYPE | osc;
        if (strstr(key, "_source"))
            return IM_SPECIAL_OSC_SOURCE | osc;
        if (strstr(key, "_wave"))
            return IM_SPECIAL_OSC_WAVE | osc;
```

- **Why:** the DEV tag self-check compares every row's flash tag with this
  classifier; the host twin is C17.7.

### C13.10 Unchanged and verified (no edit)

- `instrumentManager_waveInterpTarget()` (line 2242) matches `_wave` keys
  with `TYPE_UINT8`. It still resolves the `wav` rows. The new keys contain
  no `_wave`.
- `instrumentManager_ampEnvelopeQuiet()`, the deferred Scene swap and the
  decimator binding are unchanged (the swap fix is out of scope).
- Velocity triggers in the engines still call `modNode_updateValue()` before
  the block. A waveform-interpolation claim made there is reset at
  `oscRenderPool_beginBlock()` and re-made by `modNode_reassignVeloMod()`.

---
## 15. WP14 — Preset: selector store and apply

**The selector rows:**
- `typ`, `src` and `run` are INSTANCE_OFFSET rows without MORPHABLE.
- Today `preset_setSupplementalParameterInternal()` rejects every
  INSTANCE_OFFSET row (line 1091), and the Morph image skips non-morphable
  rows.
- So without this WP, menu, MIDI and file values would never reach the
  runtime (S084 P1 F1).

### C14.1 `presetManager.c` — store selector rows (menu, MIDI, rebind)

- **Where:** in `preset_setSupplementalParameterInternal()`, after line 1089
  (`if (!descriptor) return 0u;`) and before the INSTANCE_OFFSET rejection at
  lines 1091–1094.
- **Action:** **Add.**

```c
    /*
     * Oscillator selector rows (S085 C14.1).
     *
     * What:       typ, src and run (instrumentManager_isSelectorDescriptor())
     *             are stored as single-ended cells: the Normal cell through
     *             preset_storeInstrumentEndpoint() (dirty marking, AutoSave,
     *             card-clean bit), the unserialised Morph mirror and
     *             morph_interpolation[] set to the same value. The runtime is
     *             written for the active Scene, or with force (per-track
     *             played Scene).
     * Why:        every INSTANCE_OFFSET row is rejected below; without this a
     *             menu or MIDI edit of these rows stored nothing (S084 P1 F1).
     *             morph_interpolation[] must follow because
     *             seq_restoreAutomatedParameters() writes that cell back when a
     *             step-automation lock on the row releases.
     * Inputs:     Scene, slot, descriptor index, value (clamped by
     *             menu_clampCellValue() for menu and MIDI edits; the writers
     *             clamp again), force.
     * Outputs:    the three image cells; the runtime via the
     *             IM_SPECIAL_OSC_TYPE/_OSC_SOURCE/_RUN_MODE writers.
     * Accessors:  menu.c instrument cell commit; MidiControl.c through
     *             menu_commitTargetEdit().
     * Affiliates: preset_applySelectorRows() (C14.2).
     */
    if (instrumentManager_isSelectorDescriptor(descriptor)) {
        if (descriptor_index >= INSTRUMENT_PARAM_COUNT)
            return 0u;
        preset_storeInstrumentEndpoint(
            scene_index, slot, descriptor_index, INSTRUMENT_IMAGE_MAIN, value);
        instrument->parameter_images.morph_instrument_parameters[descriptor_index] =
            value;
        instrument->parameter_images.morph_interpolation[descriptor_index] = value;
        if (force || scene_index == scene_getActiveIndex())
            return instrumentManager_writeRuntime(slot, descriptor, value);
        return 1u;
    }
```

### C14.2 `presetManager.c` — `preset_applySelectorRows()` (new)

- **Where:** before line 1727, `preset_applyKitVoiceSupplementalInternal()`.
- **Action:** **Add.**

```c
/*
 * Apply one slot's selector rows to the runtime (S085 C14.2).
 *
 * What:       for every selector row of the slot's instrument (typ, src, run)
 *             in descriptor order: copies the stored Normal cell into the
 *             Morph mirror and morph_interpolation[], and writes it through
 *             instrumentManager_writeRuntime() (a retained write, so the
 *             request bytes are recorded).
 * Why:        no other apply path writes these rows; they must land right
 *             after the slot reset and before the Morph image, because the wav
 *             row is clamped against the incoming type (a pwm width or sample
 *             start applied while the reset slot is still wav would clamp to
 *             10 for that first block).
 * Writes:     the runtime unconditionally: the callers have just reset the
 *             slot, so no held automation value survives in it.
 * Inputs:     Scene index (active or a track's played Scene), slot.
 * Outputs:    image mirrors; OscInfo.oscType/source, OscSectionState.runMode.
 * Accessors:  preset_resetAndApplyKitVoiceImage() (boot, Scene switch,
 *             deferred Scene slot and its trigger path, per-track played
 *             Scene, copy/clear paste, Scene 17), the immediate Instrument
 *             apply (C14.5).
 * Affiliates: C14.1, C14.3, the IM_SPECIAL_OSC_TYPE writer.
 */
static void preset_applySelectorRows(uint8_t scene_index, uint8_t voice)
{
    kit_instrument_slot_t *instrument = scene_instrumentSlot(scene_index, voice);
    const instrument_registry_entry_t *entry;
    uint8_t i;

    if (!instrument || voice >= INSTRUMENT_SLOT_COUNT)
        return;
    entry = instrumentManager_registryEntry(instrument->type);
    if (!entry)
        return;
    for (i = 0u; i < entry->descriptor_count && i < INSTRUMENT_PARAM_COUNT; i++) {
        const ParamDescriptor *descriptor = &entry->descriptors[i];
        instrument_param_value_t value;

        if (!instrumentManager_isSelectorDescriptor(descriptor))
            continue;
        value = instrument->parameter_images.instrument_parameters[i];
        instrument->parameter_images.morph_instrument_parameters[i] = value;
        instrument->parameter_images.morph_interpolation[i] = value;
        (void)instrumentManager_writeRuntime(voice, descriptor, value);
    }
}
```

**Order within the row set:** descriptor order is all `typ`/`src` pairs, then
`run`. Each type write re-derives source from `sourceRequest`, which is 0
after the reset. The following `src` write then sets the request and the live
value. So the order is correct.

### C14.3 `presetManager.c` — the rebind loop skips selector rows

- **Where:** in `preset_applyKitVoiceSupplementalInternal()`, after lines
  1752–1753 (`if (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)
  continue;`).
- **Action:** **Add.**

```c
        /*
         * S085 C14.3: selector rows are applied once, before the Morph image,
         * by preset_applySelectorRows(). This loop also runs later (the
         * all-source rebind cursor, the forced per-track apply), possibly
         * while step automation holds a selector; re-writing here would
         * override that value and repeat the type-change side effects.
         */
        if (instrumentManager_isSelectorDescriptor(descriptor))
            continue;
```

### C14.4 `presetManager.c` — apply after every slot reset

- **Where:** in `preset_resetAndApplyKitVoiceImage()`, after line 1827
  `instrumentManager_resetRuntimeSlotFromScene(voice, scene_index);`.
- **Action:** **Add.**

```c
    /*
     * S085 C14.4: oscillator type, source and run mode land right after the
     * reset and before routing and the Morph image (C14.2).
     */
    preset_applySelectorRows(scene_index, voice);
```

**Coverage** (checked against every caller of this function and of
`instrumentManager_resetRuntimeSlot*()`):

| Path | Line |
|---|---|
| Boot `preset_sendDrumsetParameters()` | 1910 |
| Per-track played Scene `preset_commitSingleVoiceSlot()` | 2026 |
| Scene switch worker `preset_tickDrumsetApply()` | 2198 |
| Deferred slot on trigger `preset_applyDeferredSceneSlotForTrigger()` | 2261 |
| Scene 17, background Bank load, copy/clear Scene and Kit paste, AutoSave restore | through the worker / boot paths above (checked: `copyOps.c`, `menu.c` and `sequencer.c` call `preset_startDrumsetApply()` / `preset_startSingleVoiceApply()` / `preset_startInstrumentApply()`) |
| Immediate Instrument apply | C14.5 |

### C14.5 `presetManager.c` — immediate Instrument apply

- **Where:** in `preset_startInstrumentApplyImage()`, after line 2727
  `instrumentManager_resetRuntimeSlot(slot);`.
- **Action:** **Add.**

```c
    /*
     * S085 C14.5: this immediate apply rebuilds the Morph image through the
     * background worker and never passes preset_resetAndApplyKitVoiceImage(),
     * so the selector rows are applied here, after the reset and before the
     * first Morph write.
     */
    preset_applySelectorRows(scene_getActiveIndex(), slot);
```

### C14.6 Unchanged and verified (no edit)

- Kit, Instrument and Scene save write `[params]` for every descriptor row.
  Load parses keys into `parameter_images`. A missing key is 0
  (`instrumentManager_resetSlot()` zeroes the staged slot before parsing).
- AutoSave journals the Normal cell for any descriptor index.
- Morph-endpoint-only loads (`preset_startKitMorphApply()`,
  `preset_startInstrumentMorphApply()`) never touch single-ended rows.
- **SHIFT Morph view** (the S078 rule: non-morphable rows show and edit
  Normal):
  - The display shows the Morph mirror, which C14.1/C14.2 keep equal to
    Normal.
  - The edit is dropped today for non-morphable instrument rows. C15.10
    fixes it for the selector rows (S085 D12).
  - Verify on hardware (C19.3 row H4c).

---
## 16. WP15 — Formatter, `MenuText.h`, `menu.c`

### C15.1 `Core/DSP/Instruments/InstrumentCellFormat.h` (new file)

```c
/*
 * InstrumentCellFormat — type-dependent meaning of instrument cells
 * (S085 C15.1).
 *
 * What:       for the oscillator selector, source, wave and run rows, returns
 *             a description (format kind, clamped index, current maximum, and
 *             for a wtb position the frame count) that Menu renders from the
 *             MenuText.h tables. The oscillator type is read from the stored
 *             Scene image of the same slot (the typ row of the same
 *             oscillator), not from the runtime (S085 D5), so the text always
 *             matches the image being shown or edited.
 * Why:        plan §5 item 7: wav shows a stock wave, a pwm width, a sample
 *             start or a frame position; src a pwm family, a sample or a
 *             wavetable. The 4-bit dtype field is full, so these rows are
 *             recognised by their special-writer tag.
 * Inputs:     scene, slot, descriptor, value.
 * Outputs:    instrument_cell_format_t.
 * Accessors:  menu.c (compact text, full line, clamp, automation maximum and
 *             text).
 * Affiliates: InstrumentManager.c maxima, MenuText.h tables, SampleMemory.c.
 */
#ifndef INSTRUMENT_CELL_FORMAT_H_
#define INSTRUMENT_CELL_FORMAT_H_

#include <stdint.h>
#include "InstrumentManager.h"

typedef enum {
    CELL_FMT_NONE = 0,       /* not a formatted row: Menu uses its dtype */
    CELL_FMT_OSC_TYPE,       /* index into oscTypeNames */
    CELL_FMT_RUN_MODE,       /* index into runModeNames */
    CELL_FMT_PWM_FAMILY,     /* index into pwmFamilyNames */
    CELL_FMT_SAMPLE,         /* sample index: short sNN, full = name */
    CELL_FMT_NO_SOURCE,      /* wav-type source: "-" */
    CELL_FMT_STOCK_WAVE,     /* index into waveformNames */
    CELL_FMT_PWM_WIDTH,      /* 0..127, full "width NNN" */
    CELL_FMT_SAMPLE_START,   /* 0..127, full "start NNN" */
    CELL_FMT_WAVETABLE,      /* wavetable index: short wNN, full = name */
    CELL_FMT_WTB_POSITION    /* 0..127, full "frame NNN" (aux = frames) */
} instrument_cell_format_kind_t;

typedef struct {
    uint8_t kind;
    uint8_t index;   /* value clamped to max */
    uint8_t max;     /* current maximum for this cell */
    uint8_t aux;     /* frame count for CELL_FMT_WTB_POSITION, else 0 */
} instrument_cell_format_t;

/*
 * Describe one instrument cell (S085 C15.1).
 *
 * What:       switches on the row's special-writer tag. Returns 1 and fills
 *             *out for OSC_TYPE, OSC_SOURCE, OSC_WAVE and RUN_MODE rows;
 *             returns 0 for every other row (Menu keeps its dtype path).
 * Inputs:     scene (the image the value comes from), slot, descriptor, value.
 * Outputs:    *out.
 * Accessors:  menu.c formatted-cell sites (C15.5).
 */
uint8_t instrumentCellFormat_describe(uint8_t scene, uint8_t slot,
                                      const ParamDescriptor *descriptor,
                                      uint8_t value,
                                      instrument_cell_format_t *out);

#endif /* INSTRUMENT_CELL_FORMAT_H_ */
```

### C15.2 `Core/DSP/Instruments/InstrumentCellFormat.c` (new file)

- **Compiled with:** `SRCS` (`-O2`, control code).

```c
#include "InstrumentCellFormat.h"
#include "SceneData.h"
#include "SampleMemory.h"
#include "Oscillator.h"
#include "OscSection.h"
#include <string.h>

/*
 * Stored oscillator type of a row's oscillator (S085 C15.2).
 *
 * What:       finds the slot instrument's OSC_TYPE row with the same
 *             IM_SPECIAL_OSC_* selector and returns its Normal image value,
 *             clamped to a valid type (wav when absent).
 * Why:        S085 D5: the cell text follows the stored image.
 */
static uint8_t cellFormat_storedType(uint8_t scene, uint8_t slot,
                                     const ParamDescriptor *descriptor)
{
    const kit_instrument_slot_t *inst = scene_instrumentSlotConst(scene, slot);
    const instrument_registry_entry_t *entry;
    const uint8_t want = (uint8_t)(IM_SPECIAL_OSC_TYPE |
        (descriptor->runtime.special & IM_SPECIAL_OSC_MASK));
    uint8_t i;

    if (!inst || !(entry = instrumentManager_registryEntry(inst->type)))
        return OSC_TYPE_WAV;
    for (i = 0u; i < entry->descriptor_count && i < INSTRUMENT_PARAM_COUNT; i++) {
        if (entry->descriptors[i].runtime.special == want) {
            const uint8_t t = inst->parameter_images.instrument_parameters[i];

            return (t < OSC_TYPE_COUNT) ? t : (uint8_t)(OSC_TYPE_COUNT - 1u);
        }
    }
    return OSC_TYPE_WAV;
}

/* Stored source byte of the same oscillator (for the wtb frame count). */
static uint8_t cellFormat_storedSource(uint8_t scene, uint8_t slot,
                                       const ParamDescriptor *descriptor)
{
    const kit_instrument_slot_t *inst = scene_instrumentSlotConst(scene, slot);
    const instrument_registry_entry_t *entry;
    const uint8_t want = (uint8_t)(IM_SPECIAL_OSC_SOURCE |
        (descriptor->runtime.special & IM_SPECIAL_OSC_MASK));
    uint8_t i;

    if (!inst || !(entry = instrumentManager_registryEntry(inst->type)))
        return 0u;
    for (i = 0u; i < entry->descriptor_count && i < INSTRUMENT_PARAM_COUNT; i++) {
        if (entry->descriptors[i].runtime.special == want)
            return inst->parameter_images.instrument_parameters[i];
    }
    return 0u;
}

uint8_t instrumentCellFormat_describe(uint8_t scene, uint8_t slot,
                                      const ParamDescriptor *descriptor,
                                      uint8_t value,
                                      instrument_cell_format_t *out)
{
    uint8_t type;

    if (!descriptor || !out)
        return 0u;
    memset(out, 0, sizeof(*out));
    switch (descriptor->runtime.special & IM_SPECIAL_WRITER_MASK) {
    case IM_SPECIAL_OSC_TYPE:
        out->kind = CELL_FMT_OSC_TYPE;
        out->max = (uint8_t)(OSC_TYPE_COUNT - 1u);
        break;
    case IM_SPECIAL_RUN_MODE:
        out->kind = CELL_FMT_RUN_MODE;
        out->max = (uint8_t)(OSC_RUN_COUNT - 1u);
        break;
    case IM_SPECIAL_OSC_SOURCE:
        type = cellFormat_storedType(scene, slot, descriptor);
        out->kind = (type == OSC_TYPE_PWM) ? CELL_FMT_PWM_FAMILY
                  : (type == OSC_TYPE_SMP || type == OSC_TYPE_SLP)
                        ? CELL_FMT_SAMPLE
                  : (type == OSC_TYPE_WTB) ? CELL_FMT_WAVETABLE
                                           : CELL_FMT_NO_SOURCE;
        out->max = instrumentManager_oscSourceMaxForType(type);
        break;
    case IM_SPECIAL_OSC_WAVE:
        type = cellFormat_storedType(scene, slot, descriptor);
        out->kind = (type == OSC_TYPE_WAV) ? CELL_FMT_STOCK_WAVE
                  : (type == OSC_TYPE_PWM) ? CELL_FMT_PWM_WIDTH
                  : (type == OSC_TYPE_WTB) ? CELL_FMT_WTB_POSITION
                                           : CELL_FMT_SAMPLE_START;
        out->max = instrumentManager_oscWaveMaxForType(type);
        if (type == OSC_TYPE_WTB) {
            WavetableInfo info;
            const uint8_t max = instrumentManager_oscSourceMaxForType(type);
            uint8_t src = cellFormat_storedSource(scene, slot, descriptor);

            if (src > max)
                src = max;
            if (sampleMemory_getWavetableInfo(src, &info))
                out->aux = info.frame_count;
        }
        break;
    default:
        return 0u;
    }
    out->index = (value > out->max) ? out->max : value;
    return 1u;
}
```

- **Copy** the C15.1 block above `instrumentCellFormat_describe()`.

### C15.3 `Core/Menu/MenuText.h` — name tables

**`waveformNames`:**
- **Where:** lines 128–130.
- **Action:** **Modify** the table to 11 entries:

```c
/*
 * wav list (S085 C15.3).
 * What: the six original waves and the five generated stock waves; index =
 * stored wav value. Affiliates: Oscillator.h SQ3..SWA, OSC_STOCK_WAVE_COUNT.
 */
static const char waveformNames[][4] = {
    {11}, {"Sin"},{"Tri"},{"Saw"},{"Rec"},{"Noi"},{"Cym"},
    {"sq3"},{"sw3"},{"sqa"},{"tra"},{"swa"},
};
```

**Remove** lines 43–44, `menuText_mix` and `menuText_fm`. They are replaced
by `mixFmNames`.

**Add** after `waveformNames`:

```c
/*
 * Oscillator section name tables (S085 C15.3).
 *
 * What:       [0][0] = count; [1..] = the three-character names in stored
 *             order. oscTypeNames: typ (Oscillator.h OSC_TYPE_*);
 *             runModeNames: run (OSC_RUN_*); pwmFamilyNames: src under pwm
 *             (OSC_PWM_*); mixFmNames: the Drum mod row (OSC_MOD_*).
 * Why:        plan §5 item 7; count asserts in menu.c keep them in step with
 *             the DSP constants (C15.8).
 * Accessors:  menu.c formatted cells and the DTYPE_MIX_FM sites.
 */
static const char oscTypeNames[][4] = {
    {5}, {"wav"},{"pwm"},{"smp"},{"slp"},{"wtb"},
};
static const char runModeNames[][4] = {
    {3}, {"rst"},{"fre"},{"itp"},
};
static const char pwmFamilyNames[][4] = {
    {6}, {"prc"},{"psw"},{"ptr"},{"pq3"},{"pw3"},{"swp"},
};
static const char mixFmNames[][4] = {
    {4}, {"fm "},{"mix"},{"snc"},{"rng"},
};
```

### C15.4 `menu.c` — includes and the two formatter helpers

- **Include:** after the existing `InstrumentManager.h` include, add
  `#include "InstrumentCellFormat.h"`, `#include "Oscillator.h"` and
  `#include "OscSection.h"`. `menu.c` already includes `SampleMemory.h`
  through its sample-name use; keep it.
- **Add** the helpers below after `va_queueMarkerTransaction()` (before
  line 3285, the `va_formatValue3()` comment). Add their prototypes beside
  line 2237.

```c
/*
 * Wavetable short name (S085 C15.4).
 * What: w01..w99, then wA0.. (the sample short-name shape).
 * Accessors: menu_instrumentCellText3().
 */
static void menu_formatWavetableShortName(uint8_t index, char *buf)
{
    const uint8_t display = (uint8_t)(index + 1u);

    buf[0] = 'w';
    if (display <= 99u) {
        buf[1] = (char)('0' + display / 10u);
        buf[2] = (char)('0' + display % 10u);
        return;
    }
    buf[1] = (char)('A' + (display - 100u) / 10u);
    buf[2] = (char)('0' + (display - 100u) % 10u);
}

/*
 * Formatted instrument cell, three characters (S085 C15.4).
 *
 * What:       renders typ / run / src / wav cells from
 *             instrumentCellFormat_describe(); returns 0 for every other row.
 * Inputs:     scene, slot, descriptor, value.  Outputs: out[3]; 1 if handled.
 * Accessors:  va_formatValue3(), menu_formatCellValue3(),
 *             menu_formatAutomationValue3Slot(), menu_instrumentCellFull().
 * Affiliates: MenuText.h tables, menu_formatSampleShortName().
 */
static uint8_t menu_instrumentCellText3(uint8_t scene, uint8_t slot,
                                        const ParamDescriptor *descriptor,
                                        uint8_t value, char out[3])
{
    instrument_cell_format_t f;

    if (!instrumentCellFormat_describe(scene, slot, descriptor, value, &f))
        return 0u;
    switch (f.kind) {
    case CELL_FMT_OSC_TYPE:    memcpy(out, oscTypeNames[f.index + 1u], 3);  break;
    case CELL_FMT_RUN_MODE:    memcpy(out, runModeNames[f.index + 1u], 3);  break;
    case CELL_FMT_PWM_FAMILY:  memcpy(out, pwmFamilyNames[f.index + 1u], 3); break;
    case CELL_FMT_STOCK_WAVE:  memcpy(out, waveformNames[f.index + 1u], 3); break;
    case CELL_FMT_SAMPLE:      menu_formatSampleShortName(f.index, out);    break;
    case CELL_FMT_WAVETABLE:   menu_formatWavetableShortName(f.index, out); break;
    case CELL_FMT_NO_SOURCE:   memcpy(out, menuText_dash, 3);               break;
    default:                   numtostrpu(out, f.index, ' ');               break;
    }
    return 1u;
}

/*
 * Formatted instrument cell, full second line (S085 C15.4).
 *
 * What:       line[0..]: the sample or wavetable name (8 characters), the
 *             stock wave name, "width NNN", "start NNN" or "frame NNN" (the
 *             frame a position lands on, 1-based); line[13..15]: the compact
 *             value.
 * Why:        the single-parameter view shows names and positions.
 * Inputs:     cell, scene, value, the 16-character line.
 * Outputs:    1 if handled.  Accessors: menu_repaintGeneric().
 */
static uint8_t menu_instrumentCellFull(const menu_cell_t *cell, uint8_t scene,
                                       uint8_t value, char line[16])
{
    instrument_cell_format_t f;
    char name[SAMPLE_DISPLAY_NAME_LEN + 1u];

    if (!cell || cell->kind != MENU_CELL_INSTRUMENT ||
        !instrumentCellFormat_describe(scene, cell->slot, cell->descriptor,
                                       value, &f))
        return 0u;
    switch (f.kind) {
    case CELL_FMT_SAMPLE:
        sampleMemory_getDisplayName(f.index, name);
        memcpy(&line[0], name, SAMPLE_DISPLAY_NAME_LEN);
        break;
    case CELL_FMT_WAVETABLE:
        sampleMemory_getWavetableName(f.index, name);
        memcpy(&line[0], name, SAMPLE_DISPLAY_NAME_LEN);
        break;
    case CELL_FMT_STOCK_WAVE:
        memcpy(&line[0], waveformNames[f.index + 1u], 3u);
        break;
    case CELL_FMT_PWM_WIDTH:
    case CELL_FMT_SAMPLE_START:
        memcpy(&line[0], (f.kind == CELL_FMT_PWM_WIDTH) ? "width" : "start", 5u);
        numtostrpu(&line[6], f.index, ' ');
        break;
    case CELL_FMT_WTB_POSITION: {
        const uint8_t frame = (f.aux > 1u)
            ? (uint8_t)(1u + ((uint32_t)f.index * (f.aux - 1u) + 63u) / 127u)
            : 1u;

        memcpy(&line[0], "frame", 5u);
        numtostrpu(&line[6], frame, ' ');
        break; }
    default:
        break;
    }
    (void)menu_instrumentCellText3(scene, cell->slot, cell->descriptor, value,
                                   &line[13]);
    return 1u;
}
```

### C15.5 `menu.c` — the formatted-cell sites

| Site | Where | Change |
|---|---|---|
| `va_formatValue3()` | line 3320, before `switch (dtype)` | **Add**: `if (cell && cell->kind == MENU_CELL_INSTRUMENT && menu_instrumentCellText3(scene_getActiveIndex(), cell->slot, cell->descriptor, value, out)) return;` |
| `menu_formatCellValue3()` | before its `switch (dtype)` (line 6375) | **Add**: the same test with `valueAsText` |
| `menu_clampCellValue()` | before `dtype = (uint8_t)(menu_cellDtype(cell) & 0x0f);` (line 6626) | **Add** the clamp block below |
| `menu_repaintGeneric()` | line 14016, the `else if (menu_getMidiStaticName(…))` chain | **Add** a branch: `} else if (menu_instrumentCellFull(&cell, scene_getActiveIndex(), (uint8_t)value, editDisplayBuffer[1])) { /* S085 C15.5: formatted oscillator cell. */` before `} else switch (dtype) {` |

```c
    /*
     * Formatted instrument rows clamp to their current maximum
     * (S085 C15.5).
     *
     * What:       typ 0..4, run 0..2, src and wav to the stored type's maximum
     *             (InstrumentCellFormat). The maximum can exceed 127 for no
     *             row: 128 samples / wavetables give src <= 127 (Q7).
     * Why:        the dtype nibble cannot express these ranges; menu and MIDI
     *             edits (menu_commitTargetEdit()) both pass here.
     */
    if (cell->kind == MENU_CELL_INSTRUMENT && cell->descriptor) {
        instrument_cell_format_t f;

        if (instrumentCellFormat_describe(scene_getActiveIndex(), cell->slot,
                                          cell->descriptor,
                                          (uint8_t)((*value > 255u) ? 255u : *value),
                                          &f)) {
            if (*value > f.max)
                *value = f.max;
            return;
        }
    }
```

### C15.6 `menu.c` — `DTYPE_MIX_FM` with four values (six sites)

| Line | Function | Change |
|---|---|---|
| 3327–3329 | `va_formatValue3()` | `memcpy(out, mixFmNames[1u + ((value < 4u) ? value : 0u)], 3);` |
| 6397–6400 | `menu_formatCellValue3()` | the same with `valueAsText` |
| 6655 | `menu_clampCellValue()` | split `DTYPE_MIX_FM` from the 0..1 group: `case DTYPE_MIX_FM: if (*value > 3u) *value = 3u; break;` |
| 12917 | `menu_automationValueMax()` | `case DTYPE_MIX_FM: return (uint8_t)(mixFmNames[0][0] - 1u);` (3), separate from `DTYPE_ON_OFF` |
| 12958–12960 | `menu_formatAutomationValue3()` | `memcpy(buf, mixFmNames[1u + ((value < 4u) ? value : 0u)], 3);` |
| 14028–14031 | `menu_repaintGeneric()` | `memcpy(&editDisplayBuffer[1][13], mixFmNames[1u + ((value < 4u) ? value : 0u)], 3);` |

- **Comment:** each site gets `/* S085 C15.6: fm / mix / snc / rng. */`.
- **Unchanged:** Drum's `osc2_mod_type` row keeps `DTYPE_MIX_FM`, `MOD_NONE`
  and `FLAGS_IMAGE`. It stays Morphable as today, stepping through the four
  modes.

### C15.7 `menu.c` — the `wav` list without samples

| Line | Change |
|---|---|
| 11456 `case MENU_WAVEFORM: return … + menu_numSamples;` | `case MENU_WAVEFORM: return (uint8_t)waveformNames[0][0];` |
| 11489–11494, `getMenuItemNameForValue()` | `case MENU_WAVEFORM: if (curParmVal < (uint8_t)waveformNames[0][0]) p = waveformNames[curParmVal + 1u]; break;` |
| 12893–12895, comment of `menu_automationValueMax()` | drop "MENU_WAVEFORM is excluded because indices beyond the built-in table address sample slots" |
| 12917ff, `menu_automationValueMax()` DTYPE_MENU switch | **add** `case MENU_WAVEFORM: return (uint8_t)(waveformNames[0][0] - 1u);` |
| 12949, `menu_formatAutomationValue3()` | `if (value <= max_val)` (drop `menuId == MENU_WAVEFORM ||`) |
| 14048–14059, `menu_repaintGeneric()` DTYPE_MENU | **Remove** the sample-name block (lines 14050–14058); keep `getMenuItemNameForValue(…)` |

- **Keep:** `menu_formatSampleShortName()` (lines 11466–11480). It now
  serves `CELL_FMT_SAMPLE`.
- **Comment:** each site gets `/* S085 C15.7: samples left the wav list
  (smp/slp + src). */`.

### C15.8 `menu.c` — count asserts

- **Where:** after line 2039, the `getMenuItemNameForValue` prototype.
- **Action:** **Add.**

```c
/*
 * Name tables follow their DSP counts (S085 C15.8).
 * What: compile-time proof that each MenuText.h table count equals its
 * constant. Why: a new type, run mode, family, mode or wave cannot ship
 * without its name.
 */
_Static_assert(OSC_TYPE_COUNT == 5u, "oscTypeNames has 5 entries");
_Static_assert(OSC_RUN_COUNT == 3u, "runModeNames has 3 entries");
_Static_assert(OSC_PWM_FAMILY_COUNT == 6u, "pwmFamilyNames has 6 entries");
_Static_assert(OSC_MOD_COUNT == 4u, "mixFmNames has 4 entries");
_Static_assert(OSC_STOCK_WAVE_COUNT == 11u, "waveformNames has 11 entries");
```

- The literals mirror the `[0][0]` counts. Those counts cannot be used in a
  constant expression, so a mismatch means both sides must be edited
  together.

### C15.9 `menu.c` — slot-aware automation maximum and text

**Add** after line 12932, the end of `menu_automationValueMax()`:

```c
/*
 * Slot-aware automation maximum (S085 C15.9).
 *
 * What:       for a formatted instrument row, its current maximum (capped at
 *             127: automation is 7-bit); otherwise menu_automationValueMax().
 * Inputs:     descriptor, scene, slot.
 * Accessors:  menu_stepAutomationEdit().
 */
static uint8_t menu_automationValueMaxSlot(const ParamDescriptor *descriptor,
                                           uint8_t scene, uint8_t slot)
{
    instrument_cell_format_t f;

    if (descriptor &&
        instrumentCellFormat_describe(scene, slot, descriptor, 0u, &f))
        return (f.max > 127u) ? 127u : f.max;
    return menu_automationValueMax(descriptor);
}
```

**Add** after line 12975, the end of `menu_formatAutomationValue3()`:

```c
/*
 * Slot-aware automation value text (S085 C15.9).
 *
 * What:       the formatted text for typ/run/src/wav rows (by the target
 *             Scene image's type), else menu_formatAutomationValue3().
 * Accessors:  menu_repaintStepAutomation() (two sites).
 */
static void menu_formatAutomationValue3Slot(const ParamDescriptor *descriptor,
                                            uint8_t value, char *buf,
                                            uint8_t scene, uint8_t slot)
{
    if (descriptor &&
        menu_instrumentCellText3(scene, slot, descriptor, value, buf))
        return;
    menu_formatAutomationValue3(descriptor, value, buf);
}
```

**Call sites:**

| Line | Change |
|---|---|
| 13520, in `menu_stepAutomationEdit()` | `max_val = menu_automationValueMaxSlot(desc, scene, instrumentParam_slot(vt));` (`vt` is a voice target there; for other targets `desc` is NULL and the old path applies) |
| 13787, in `menu_repaintStepAutomation()` | `menu_formatAutomationValue3Slot(desc, amt_value, amt_out, scene, instrumentParam_isVoiceParameter(target) ? instrumentParam_slot(target) : 0u);` |
| 13894 | the same with `descriptor, autos[page].value, &editDisplayBuffer[1][13]` |

### C15.10 `menu.c` — selector rows edit Normal in the SHIFT Morph view

**Finding:**
- In `menu_cellCommitValueEx()` (line 5119), a non-morphable instrument cell is committed
  only when `!morphView` (lines 5242–5245).
- So under SHIFT the selector rows could be seen (their Morph mirror equals
  Normal, C14.1/C14.2) but not edited.
- The S078 rule requires edits of non-morphable rows to go to Normal in any
  Morph view (plan §5 item 6).

**Change:**
- **Where:** line 5242, `} else if (!morphView) {`.
- **Action:** **Modify.**

```c
            } else if (!morphView ||
                       instrumentManager_isSelectorDescriptor(cell->descriptor)) {
                /* S085 C15.10: selector rows are single-ended; under SHIFT
                ** Morph an edit stores Normal (S078 Morph-view rule). */
```

**Display:** no change needed. In the Morph view `menu_cellDisplayValue()`
reads `morph_instrument_parameters[]`, which C14.1/C14.2 keep equal to
Normal.

### C15.11 `menu.c` — nothing else

**Out of S085** (left as they are):
- the label hooks (`instrumentCellFormat_shortLabel/longLabel`);
- the encoder hidden-option skip;
- the Mix/transient/drive formats.

`menu_formatInstrumentTargetShort()` / `_displayInstrumentTargetFull()` (LFO
target names) are unchanged.

---
## 17. WP16 — Build: `Makefile`

### C16.1 `Makefile` — sources

**`SRCS`** (control code, `-O2 -flto`):
- **Where:** after line 141, `Core/SampleRom/sampleFlash.c \`.
- **Action:** **Add:**

  ```make
    Core/SampleRom/SampleInstall.c \
    Core/SampleRom/StockWaveGen.c \
    Core/SampleRom/StockWaveRecipes.c \
  ```

- **Where:** after line 111, `Core/DSP/Instruments/InstrumentManager.c \`.
- **Action:** **Add** `Core/DSP/Instruments/InstrumentCellFormat.c \`.

**`DSP_SRCS`** (`-Ofast`):
- **Where:** after line 159, `Core/DSPAudio/Oscillator.c \`.
- **Action:** **Add:**

  ```make
    Core/DSPAudio/OscillatorPwm.c \
    Core/DSPAudio/OscillatorSample.c \
    Core/DSPAudio/OscillatorWavetable.c \
    Core/DSPAudio/OscRenderPool.c \
    Core/DSPAudio/OscSection.c \
  ```

- **Where:** after line 166, `Core/DSPAudio/squareRootLut.c \`.
- **Action:** **Add** `Core/DSPAudio/StockWaves.c \`.

**Comment:** above `DSP_SRCS`, **add**:

```make
# S085 C16.1: oscillator types (pwm, smp/slp, wtb), stock-wave reader, render
# pool and oscillator section build -Ofast through the Core/DSPAudio/%.o rule.
# SampleInstall/StockWaveGen/StockWaveRecipes and InstrumentCellFormat are
# control code and stay in SRCS (-O2).
```

**No new include paths:**
- `Core/DSPAudio`, `Core/SampleRom` and `Core/DSP/Instruments` are already
  listed (lines 33–45).
- The `Core/DSPAudio/%.o` pattern rule already covers the new DSP files.

---

## 18. WP17 — Tools, host tests, ARM probes, the card move

### C17.1 `tools/dsp_test/Makefile` — targets

- **Action:** **Add** the targets below. Each follows the existing pattern:
  `extract.py` pulls the real functions and objects from `$(ROOT)` (and
  `frozen85` where noted) into `$(GEN)`, the host builds, and the test runs.
- **`.PHONY`:** **add** all of them.

| Target | Extracts | Test file | Proves |
|---|---|---|---|
| `natural_order` | `storage_naturalCompare`, `storage_stripOrderPrefix` (storageTypes.c) | `test_natural_order.c` | C17.3 |
| `catalogue` | `SampleMemory.c` (all), `SampleInstall.c` (all), with `SAMPLE_FLASH_PTR` mapped to a RAM image and host stubs of `sampleFlash_*` and `stockWaveGen_programAll` | `test_catalogue.c` | C17.3 |
| `stock_gen` | `stockWaveGen_point` (renamed static → extern), `stockWaveRecipeA/B`, `stockWaveLevelScale`, `sine_table` | `test_stock_gen.c` vs `frozen/stock_ref.bin` | C17.3 |
| `osc_pwm` | the `OscillatorPwm.c` helpers and `oscPwm_prepare`, `sawTable`, `triTable`, `recTable`, `sine_table` (multi-dimensional objects, C17.2) | `test_osc_pwm.c` | C17.6 |
| `osc_sample` | the `OscillatorSample.c` functions; catalogue stubs | `test_osc_sample.c` | C17.6 |
| `osc_run` | `osc_setFreq`, `osc_armSlew`, `calcWavetableOscBlock`, `calcFmBlock`, the `OscSection.c` functions, `oscSection_filterRecalc` with SVF stubs, the pool | `test_osc_run.c` | C17.6 |
| `wtb` | the `OscillatorWavetable.c` functions; catalogue stubs | `test_wtb.c` | C17.6 |
| `osc_s0` | old: `frozen85/…/Oscillator.c` renderers; new: `Oscillator.c` renderers and `oscSection_renderDrum` / `_renderFm2` | `test_osc_s0.c` | C17.5 |
| `armcheck-osc-base`, `armcheck-osc` | ARM probes (C17.4) | `armcheck/osc.c`, `armcheck/osc_base.c` | C17.4 |

**Suite:**
- `special_tags` is unchanged in the Makefile; the checker changes (C17.7).
- The host suite at G4 is the existing targets (`selftest`, `filter`,
  `special_tags`, `pack`, `postchain`, `octave`, `mixer`) plus the eight new
  host targets.
- The ARM checks are `armcheck-postchain`, `armcheck-mixer` and
  `armcheck-osc`.

### C17.2 `tools/dsp_test/extract.py` — multi-dimensional objects

- **Where:** the object matcher (`--object`).
- **Action:** **Modify** so an object declaration may carry several bracket
  groups (`const int16_t sawTable[11][1024] = { … };`). The regex accepts
  `(\[[^\]]*\])+` after the name.
- **Why:** `osc_pwm`, `stock_gen` and `osc_s0` need the real band-limited
  tables.
- **Proof:** `octave` and `mixer` still pass (their single-dimension
  extracts are unchanged).

### C17.3 Host tests: catalogue, install order, stock generation

**`test_natural_order.c` (new):**
- Ordered pairs that must hold:

  | Pairs |
  |---|
  | `f2` < `f10` |
  | `a` < `B` (case-insensitive) |
  | `x01` < `x1` (zero tie: fewer zeros first only on full equality) |
  | `frame 9` < `frame 10` |
  | `""` < `a` |
  | `NULL` < `a` |

- `storage_stripOrderPrefix`:

  | Input | Result |
  |---|---|
  | `"003 hvoice"` | `"hvoice"` |
  | `"12_x"` | `"x"` |
  | `"12"` | `"12"` |
  | `"12 "` | `"12 "` |
  | `"abc"` | `"abc"` |

- **Break proof:** remove the leading-zero skip; `x01`/`x1` and `f02`/`f10`
  must fail.

**`test_catalogue.c` (new):**
- **Setup:** a 1,310,720 B RAM image as sample flash; `sampleFlash_*` host
  stubs program AND-wise (erased = 0xFF) and refuse writes outside
  sectors 7..11.
- **Cases:**
  - an empty image reads EMPTY;
  - a v1 count word (1..120) at sector 7 reads STALE;
  - install 3 samples + 2 wavetables, commit, reload: counts, names, offsets,
    gain codes (peak 16,384 → code 23), frames (with the 2-frame pad inside
    bounds);
  - the limits: sample 129 returns −2; wavetable 129 returns −2;
  - fit: an item larger than `bytesFree()` returns −3;
  - F4: one table of mono 16, stereo 16 and mono 24-bit frames reads back
    exactly; a different stored depth is refused;
  - F8: a short sample returns −4 with no entry;
  - F7: items after an `abortItem()` install and commit;
  - torn install (no magic): EMPTY;
  - entry bounds: a hand-corrupted entry pointing into the catalogue stops
    the count.
- **Break proof:** make `setFrameFormat()` a no-op (the F4 case must fail);
  skip the −4 check (the F8 case must fail).

**`test_stock_gen.c` (new):**
- The device function `stockWaveGen_point()` produces all 5 × 6,112 points.
- They must equal `frozen/stock_ref.bin` (from `gen_stock_recipes.py
  --emit-ref`) within ±1 LSB at every point.
- **Break proof:** swap the sine and cosine reads (≥ 1,000 LSB errors).

### C17.4 ARM probes `armcheck-osc-base` / `armcheck-osc` (new)

- **Files:** `tools/dsp_test/armcheck/osc_base.c` and `armcheck/osc.c`.
- **What they compile:**
  - force-inlined, renamed copies of each loop (generated with `extract.py`
    and `--rename`);
  - one wrapper per case, with every block-constant input fixed, so each
    wrapper is exactly one loop (the `armcheck-vx2` technique in S084).
- **What they print:** `fpseq.py` reports instructions per sample and VDIV
  for each wrapper.

| Probe (S085 tree) | Cases |
|---|---|
| table / FM | `calcWavetableOscBlock`, `calcFmBlock` |
| sine / FM | `calcSineBlock`, `calcFmSineBlock` |
| noise | `calcNoiseBlock` |
| crash / FM | `calcSampleOscBlock`, `calcSampleOscFmBlock` |
| stock / FM | `calcStockWaveBlock`, `calcStockWaveFmBlock` |
| pwm / FM | one wrapper per family; `swp` once per segment (0, 1, 2, 3); the worst is reported |
| smp, slp / FM | `oscSample_render` (one-shot, looped), `_renderFm` |
| wtb / FM | `oscWavetable_render` 8-bit, 16-bit, `_renderFm` |
| snc | the wrap loop of `oscSection_renderSync` and the fixed overhead of one `calcNextOscSampleBlock` call (dispatch + prologue + epilogue) |
| rng | `oscSection_ring` |
| itp blend | `oscSection_blendItp` |

- **`armcheck-osc-base`** runs the same table/FM, sine/FM, noise, crash and
  user-sample loops on `frozen85/Core/DSPAudio/Oscillator.c` (C0.2).
- **`tools/dsp_test/looplen.py` (new, host):** a generic loop-length
  reporter over an object file, used to read the straight-line counts for
  the `snc` overhead.
  - Interface: `python3 looplen.py --obj FILE --func NAME` prints the
    function's instruction count outside its loops.
  - It reads `arm-none-eabi-objdump -d` output and needs no new tools.

### C17.5 `osc_s0` — the S0 proof (new test, S085 D10)

`test_osc_s0.c` renders the same input through the old (`frozen85`) and the
new code and requires **bit-identical** output, with `phaseIncStep = 0`
(`rst`).

**Cases:**
1. Each `wav` waveform 0–5, plain and FM (random modulator block), 64 blocks,
   three pitches.
2. Drum `fm` and `mix`:
   - old: the frozen85 `Drum_calcVoiceSyncBlock` oscillator lines (extracted
     as a range with `--lines` into a test function);
   - new: `oscSection_renderDrum(…, NULL, NULL)`.
3. Cymbal FM2 (carrier gain 1.0) and HiHat FM2 (0.5) against the frozen85
   lines.
4. `osc_setFreq()` with no slew: the `phaseInc` sequence equals the frozen85
   version over a pitch sweep.

**Break proof:** add 1 to `phaseIncStep` in one renderer; the case must
fail.

### C17.6 DSP host tests

**`test_osc_pwm.c` (new):**
- `refRead1024()` is an independent double-precision read of the table
  format.
- Every 2^14 phase step across one level matches `pwm_read1024` within
  1 LSB, and 28 table midpoints are interpolated (the S084 P1 F3 fix).
- **`prc` polarity (S085 D4):** `prc` at `wav` 0 correlates > 0.99 with
  `recTable` at the same level.
- **Monotonic width:** the `prc` high-time fraction falls from 0.50 to 0.04
  across `wav` 0..127.
- `pq3`/`pw3` with and without the stock block use the 0.78 / 0.42 / 0.5
  splits.
- **Break proof:** the F3 constant set to 1/2^32 (the midpoints fail); `prc`
  operands swapped (polarity fails).

**`test_osc_sample.c` (new):**
- A 100,000-frame sample plays to its end (Q20.12; > 32,768 frames).
- `smp` outputs exactly 0 after its last frame (Q10).
- `slp` loops `[start, end)` without a gap, and its seam is the
  interpolation from the last frame to the start frame.
- `start = wav × frames / 128` (Q11).
- `sourceRequest` 9 with 4 installed binds 3, and with 12 installed binds 9
  again (S084 P1 F2).
- The gain code multiplies the block once.
- `oscSample_isFinished()` matches the 0 output.
- **Break proof:** bind from `source` instead of `sourceRequest`; return
  `gain` instead of 0 when finished.

**`test_osc_run.c` (new):**
- **Slew:** `osc_armSlew` + `osc_setFreq` reaches the target within 132
  samples ±1 increment; with no slew, `phaseIncStep == 0` and
  `phaseInc == target`.
- **Filter (F4, Q15):**
  - `rst` calls only `SVF_recalcFreq`;
  - `fre` follows a modulated q and holds it.
- **Carrier-only `itp` click test (plan §4.3.7):**
  - Retrigger a running FM voice (sine carrier, sine modulator, deep FM) and
    a `mix` voice in `itp`, mid-cycle, with a pool unit granted.
  - The largest |sample-to-sample step| across 2 blocks after the trigger
    must be ≤ 1.1 × the largest step of the same voice without a trigger.
  - The same measure must **fail** for `rst`, and for a variant that also
    resets the modulators (proving the test can fail on sources 2 and 3).
- **Pool:**
  - two ongoing crossfades plus one interpolation request: the
    interpolation is refused;
  - one ongoing crossfade, one interpolation, then a new crossfade: the new
    one is refused and its carrier phase is restored with a slew armed;
  - the crossfade ends after 132 samples (5 blocks) and frees its unit.
- **`snc`:**
  - a reset lands on the sample after the master wrap (C10.3);
  - with a one-shot master that has finished, no reset happens;
  - the output equals `slave × (1 − amt) + master × amt`.
- **Break proof:**
  - claim order changed (interpolation before ongoing): the first pool case
    fails;
  - reset at `i` instead of `i + 1`: the `snc` index case fails.

**`test_wtb.c` (new):**
- The frame-pair rule (C8.2):
  - a jump from position 0 to 127 moves exactly one frame per block;
  - the blend stays continuous between blocks (|Δ| at block edges ≤ the
    largest within-block step);
  - a static position plays the interpolated frame pair.
- 8-bit and 16-bit tables read the same shape (8-bit × 256).
- `idx` never reaches `frame_len` (fuzzed phases); `fa`/`fb` ≤ n − 1.
- A 1-frame table plays.
- Entering `wtb` from `pwm` binds fresh (F11).
- **Break proof:**
  - `ffStep` computed from target instead of `pEnd` (continuity fails);
  - the clamp `fa ≤ n − 2` removed (index check fails at the last frame).

### C17.7 `tools/dsp_test/check_special_tags.py`

- **Where:** in `expected()`, inside `if osc:` after the `pitch_fine` test
  (line 42).
- **Action:** **Add.**
- **Also add**, before `if key in simple:`:
  `if key == 'osc_run_mode': return {'IM_SPECIAL_RUN_MODE'}`.
- **Docstring:** add "S085 C17.7: oscillator type/source/wave and run-mode
  selectors".
- **Result:** 177 rows, 0 mismatches.

```python
        # S085 C17.7: oscillator selectors; osc2_mod_type is the fm/mix/snc/rng
        # row (no tag).
        if key.endswith('_type') and not key.endswith('_mod_type'):
            return {'IM_SPECIAL_OSC_TYPE', osc}
        if key.endswith('_source'):
            return {'IM_SPECIAL_OSC_SOURCE', osc}
        if key.endswith('_wave'):
            return {'IM_SPECIAL_OSC_WAVE', osc}
```

### C17.8 Host tools and plots

These are host-only tools, not firmware. Each is specified by its contract;
they carry over as reviewed files. The S084 versions were tested in the plan
(S084 r6).

**`tools/sampleoptimize.py`:**
- **Run:** `python3 tools/sampleoptimize.py [CARD] [--dry-run]
  [--worst-db -20] [--typical-db -40]`. `CARD` defaults to `SD_CARD/`. Needs
  numpy.
- **Backups:** `wavetables/` → `wavetables_old/` and `samples/` →
  `samples_old/`. An existing `_old` folder is kept and used as the source,
  so re-running re-optimises from the originals.
- **Wavetables:**
  - each table is resampled to the smallest power-of-two length that plays
    the same (linear interpolation on a 16× finer phase grid; worst frame
    ≤ −20 dB, median ≤ −40 dB);
  - it never grows a table, except to unify mixed frame lengths at the
    longest;
  - file names, and so frame order, are kept;
  - one common gain drop if band-limiting would clip.
- **Samples:**
  - the tail more than 80 dB under the peak is trimmed;
  - stereo, non-16-bit or non-44.1 kHz files are converted to mono 16-bit
    44.1 kHz;
  - anything else is copied byte for byte.
- **Report:** per table and per sample.

**`tools/plots/`:**
- **Files:**
  - `lxr_plotlib.py` (shared plotting helpers);
  - `plot_pwm.py` → `pwm_families.png` (the six families, reference
    shapes);
  - `plot_stock_waves.py` → `stock_waves_303_analog.png` (with
    `reference/303_square_saw.png` and `reference/tra_dirty_triangle.png`);
  - `plot_wavetable_formats.py` → `wavetable_formats.png` (the format
    trade-offs);
  - `stock_wave_harmonics.csv` (C4.1);
  - `README.md`, osc sections only.
- **Not carried:** `plot_waveshapers.py`, `waveshapers_*.png`,
  `plot_current_drive.py` and `__pycache__/` (S086 or generated).

### C17.9 Test proof rule

- **Rule:** a new host test counts only when it fails on a deliberately broken
  version.
- **Procedure:**
  - For each target in C17.3–C17.6, apply its listed break in a scratch copy.
  - Run the target and record "FAILED, N failures" in the handoff log.
  - Restore and rerun green.
- **The runs** are G1 (`natural_order`, `catalogue`), G2 (`stock_gen`) and G3
  (`osc_pwm`, `osc_sample`, `osc_run`, `wtb`, `osc_s0`).

### C17.10 The repository card move (Q12)

**State on 2026-10-26:**
- The move is already in the working tree (made outside this schedule):
  `SD_CARD/loops/` is gone (104 files show as deleted in git), and the same
  104 AKWF frames are in `SD_CARD/wavetables/ak_hvox/`, untracked and
  unoptimised (600 points, 1,344 B per file).
- The table will install with the name `ak_hvox`.

**Remaining steps:**
1. Stage the move so git records it as a rename:
   `git add -A SD_CARD/loops SD_CARD/wavetables`. Commits belong to the user;
   this only stages.
2. `python3 tools/sampleoptimize.py SD_CARD`.
   - It writes the optimised `SD_CARD/wavetables/ak_hvox/` (expected 600 →
     256 points, 124,800 → 53,248 B of frame data) and keeps the originals in
     `SD_CARD/wavetables_old/ak_hvox/`.
   - `samples/01_cut.wav` stays as it is.
3. Record the tool's report in the handoff log.
4. The installer ignores `wavetables_old/` (it is not under `wavetables/`).

### C17.11 `tools/dsp_test/DSP_TEST.md`

- **§5 Targets:** add rows for the nine new targets (C17.1).
- **§3 Layout:** add `frozen85/` and the new files.
- **§7 Reference costs:** add the S085 table from C19.2.

---

## 19. WP18 — Specifications and memory documents

Written in the same change set. Osc paragraphs only; nothing about
transients, drive, Mix FX or buffers.

| Document | Sections to write or change |
|---|---|
| `dsp_instruments_effects/INSTRUMENTS_DSP_REFERENCE.md` | oscillator types and rows; the wav list (11); pwm families; sample playback (Q20.12, smp ends at 0, slp loop, start, gain code); wtb (block-rate frame pair, D1); the stock waves and fallback; the slew; run modes rst/fre/itp (carrier-only itp); mod modes fm/mix/snc/rng; the render pool and claim order; the filter slew; the S085 cost table (C19.2); "how to add an oscillator type" |
| `dsp_instruments_effects/CPU_USE_DSP_AUDIT.md` | a new item: the pre-S085 and S085 loop counts (C19.2), the pool budget, the snc decision (C10.6), the per-voice worst case S086 budgets against |
| `dsp_instruments_effects/EFFECTS_MIXER_DSP_REFERENCE.md` | the block-start order (pool reset, ongoing claims, modulation, pending claims) |
| `FILESYSTEM_SPEC.md` | "Sample flash": catalogue v2 layout and states; Load:[Samples] from `samples/` and `wavetables/` (natural order, files first, depth 3, display names, ordering prefix, long names over 48 characters use the 8.3 alias, skip rules, accepted formats, never expand); `/loops` retired; the boot notices. Replace the settled `Wavetable/`/`Sample/` names. Kit/Instrument keys `oscN_type`, `oscN_source`, `osc_run_mode` (missing = 0) |
| `MODULE_INTERCHANGE_SPEC.md` | new modules (OscillatorSample, OscillatorPwm, OscillatorWavetable, StockWaves, OscRenderPool, OscSection, SampleInstall, StockWaveGen, InstrumentCellFormat) and their call map; the IM writers and claim APIs; the Preset selector path; the menu formatter |
| `STORAGE_SRAM_MANIFEST.md` | the sample flash map (§0.5); the RAM ledger rows (§0.5); the fixed 1,176 B slot reserve with measured engine sizes; ITCM contents |
| `BANK_PRESET_ARCHITECTURE.md` | selector rows: single-ended, stored through the supplemental path with mirrors, applied before the Morph image, skipped by the rebind loop, editable under SHIFT Morph (C15.10) |
| `OSC_INTERP_AUDIT.md` | the pool replaces the two-slot limit; interpolation is wav-only; user-sample interpolation is gone (plan §4.3.5) |
| `MIDI_SPEC.md` | the new rows have NRPN IDs (slot·64 + local index 39..45); 7-bit values; src reaches every sample (128) |
| `DEV_MODES.md` | the special-tag self-check covers the new rows (177) |
| `MEMORY.md` | "Sample Flash Loading" and its map rewritten for v2; the spike path (`Helicase-dev-ph7-spike/` inside the tree) and the `.gitignore` note corrected; Volatile Notes for S085 |
| `SCOPING_TARGETS.md` §7.4 | PWM and wavetable oscillators built in S085; folder `wavetables/` |
| `knowledge_files/log_archive/085_SESSION_HANDOFF_LOG.md` | new: the record, the cost record, test break proofs, hardware results, deviations D1–D11 |

---

## 20. WP19 — Gates, cost record, hardware matrix

### C19.1 Gate checklist (in order, §0.7)

| Gate | Pass when |
|---|---|
| G0 | `frozen85/` complete (14 files); baseline probe counts recorded |
| G1 | `make all` without new warnings; `natural_order`, `catalogue` pass + break proofs |
| G2 | `stock_gen` passes + break proof |
| G3 | `make all`; `osc_pwm`, `osc_sample`, `osc_run`, `wtb`, `osc_s0` pass + break proofs; `armcheck-osc` prints all probes; snc decision made (C10.6); runtime-slot `_Static_assert`s pass and sizes recorded; ITCM ≤ 16,384 and recorded |
| G4 | `special_tags` 177/0; full host suite and ARM checks; `make img`; `link_budget.py` recorded (flash, ITCM, DTCM; `bss` change matches §0.5) |
| G5 | WP18 written; card move done; hardware matrix (C19.3) run by the user |

**Searches** at G4 must return nothing in `Core/`:
- `insertFx_`, `driveShaper`, `transient_is`, `voiceBuffer_`, `slotQuiet`
  (no S086 code came in);
- `OSC_SAMPLE_START`, `OSC_WAVE_INTERP_MAX_ACTIVE`, `menu_numSamples`,
  `sampleMemory_install`, `filesystem_installLoopsBlocking` (removals
  complete).

### C19.2 Cost record (written before G4)

One table, static instructions per sample (`fpseq.py`), with VDIV:

| Loop | pre-S085 (C0.2) | S085 | Note |
|---|---:|---:|---|
| table / FM | measured | measured | the slew add (C5.6) |
| sine / FM | measured | measured | |
| noise | measured | measured | |
| stock / FM | — | measured | target ≈ table |
| pwm worst family / FM | — | measured | swp segment 2 expected worst |
| smp / slp / FM | user-sample loops | measured | |
| wtb 8/16 / FM | — | measured | target ≈ 42 / 48 (D1) |
| snc worst | — | measured | against the cap (C10.6) |
| rng | — | measured | |
| itp blend | — | measured | per voice while active |

**Derived figures:**
- **The per-voice oscillator worst case** for each engine, e.g. Cymbal:
  2 × the worst modulator renderer + the worst FM carrier + the section
  overhead.
- **The system pool worst case:** 2 × the dearest carrier render.
- **For S086:** the per-voice oscillator worst case. S086 budgets its
  88-cycle chain next to it.

### C19.3 Hardware matrix (user, after G4)

| # | Check | Pass |
|---|---|---|
| H1 | Boot on the old card: "Samples invalid" / "reload samples"; on an erased sample flash: "No samples" / "load samples" | notices shown once, boot continues |
| H2 | Load:[Samples] with `samples/` and `wavetables/ak_hvox/` (+ a long-named folder at each level, a 3-deep table folder, a 4-deep folder, a mixed mono/stereo/24-bit table, one truncated WAV) | summary counts correct; the 4-deep folder and truncated WAV are skipped, not hung or aborted; names show in the single-parameter view; a reboot keeps everything |
| H3 | `wav`: every wave 0–10 at low and high notes, plain and FM; sq3..swa before an install (fallback) and after | no clicks or level jumps; fallback audible as Rec/Saw/Tri |
| H4 | `pwm`: each family across `wav` 0..127 and the note range; `prc` at 0 against Rec | smooth width; no zipper at low notes |
| H4a | menu-edit `typ`/`src`/`run`, save the Kit, reboot, reload | type, width/start and run mode survive |
| H4b | a MIDI CC to `typ` and to `src` | sound and display change |
| H4c | SHIFT Morph view on `typ`/`src`/`run` | shows Normal; an edit changes Normal |
| H4d | step-automate `typ`, let the lock release | the stored type returns on the next trigger |
| H5 | `smp`/`slp`: start sweeps; loop seam; gain normalisation; `smp` silence after the end | as specified |
| H6 | `wtb`: position sweep by knob, LFO and Morph; 8- and 16-bit tables; fast LFO | smooth; sweeps faster than 1 frame/block glide |
| H7 | run modes on fast retriggers: `rst`, `fre`, `itp` on Drum fm/mix/snc/rng, Snare, Cymbal, HiHat; two voices in `itp` at once plus a third | no clicks in fre/itp; the third voice falls back to fre smoothly; Cymbal itp FM attack varies by hit (expected) |
| H7a | velocity and LFO on resonance in `fre`/`itp` | audible |
| H8 | Drum `mod`: `fm mix snc rng`; `snc` with an `smp` master and start > 0, and after the master ends | sync stable; no resets after a one-shot master ends; snc mix law by ear |
| H9 | velocity and LFO on `wav` of a `pwm`/`smp`/`wtb` oscillator | range reaches 127 (D11) |
| H10 | old kits load | sound the same except `wav` 6+ (now stock waves, Q8) |
| H11 | worst-case Scene: `wtb` FM oscillators on every voice, two `itp` crossfades, waveform interpolation, `snc` at high master pitch; DEV render-overrun counter and `audioCodec_underrunCount` | no underruns; `cpu` figure recorded |

---
+
---
## 21. Implementation progress log (working notes)

**2026-10-26 — S085 P1 implementation.**

### WP0 — snapshot and baseline
- `tools/dsp_test/snapshot.sh` gained the optional DEST and the S085 list
  (`frozen85`); `sh tools/dsp_test/snapshot.sh tools/dsp_test/frozen85`
  froze 14 files **before the first edit**.
- Baseline (pre-change, DEV): `text=609,288 data=424 bss=430,200`;
  flash 609,712 / 753,664 (headroom 143,952); ITCM 4,168; DTCM 4,472.
- `armcheck-osc-base` reports the frozen loops (straight-line counts from
  `looplen.py`): `calcSineBlock` 18, `calcFmBlock` 22,
  `calcWavetableOscBlock` 17, `calcSampleOscBlock` 20. These are whole
  functions after inlining; the per-sample figures remain to be read on the
  hardware/codegen side.

### WP1–WP3 — catalogue v2, installer, card walk, boot notice
- `SampleMemory.h/.c` rewritten (in-place catalogue, EMPTY/V2/STALE, gain
  code, 128/128); `SampleInstall.h/.c` added (accept formats, convert,
  program entries, commit magic last); `filesystem.c` walk replaced
  (batched natural order, depth 3, per-file skip, mixed source formats);
  boot notice added; `menu_numSamples` removed; Load:[Samples] reports
  samples/tables/skips and holds the summary through a deadline.
- Host: `natural_order` and `catalogue` pass (including mixed frame
  formats, abort, short item, torn install, entry bounds).
- **Note:** the schedule's G1 build-only checkpoint was folded into G3,
  because WP1 removed the v1 sample API that `Oscillator.c` still called;
  WP5 removed that path in the same change set.

### WP4 — stock waves
- CSV, generator, recipes, `StockWaveGen`, `StockWaves` added;
  `stock_ref.bin` regenerated. `stock_gen`: worst 1 LSB, 0 failures.
- Generator header corrected to the S085 C4.3 block; SHA-256 stays in the
  generated file.

### WP5–WP11 — oscillator model, renderers, pool, section, engines
- `Oscillator.h/.c`: 11-entry wav list, five types, request bytes, slew,
  type dispatch, stock renderers, `osc_setFreq()` per type.
- New `OscillatorPwm`, `OscillatorSample`, `OscillatorWavetable`,
  `OscRenderPool`, `OscSection`; `modulationNode` and `mixer` claim
  order; `ResonantFilter` `SVF_recalcFreqFrom`.
- Engines: section state, run-mode trigger, itp carrier crossfade,
  `oscSection_finishBlock()`; fm/mix/FM2 composition preserved.
- Host: `osc_run` (slew, carrier-only itp click, pool fallback, snc
  one-shot-master rule, smp/slp, wtb frame pair, prc polarity) and
  `osc_s0` (frozen85 vs current renderers and drum/FM2 compositions) pass.

### WP12–WP15 — rows, InstrumentManager, Preset, menu
- Four tables: 44/41/46/46 rows, selector macro, typ/src/run rows, menu
  cells. `special_tags`: **177 rows, 0 mismatches**.
- InstrumentManager: writer IDs 20–23, fixed 1,176 B slot, per-slot wav
  domains (D11), itp claims, filter recalc through the section, classifier.
- Preset: selector store/apply, rebind skip, post-reset apply, immediate
  Instrument apply. Menu: formatted cells (clamps, names, width/start/frame),
  `mixFmNames` 4 values, 11-wave list, count asserts, slot-aware automation,
  SHIFT-Morph selector edit.

### WP16–WP17 — build, tools, tests
- `make all` clean. **DEV after S085:** `text=640,960 data=424
  bss=414,100`; flash 641,384 / 753,664 (**headroom 112,280 B**);
  ITCM 10,632 / 16,384; DTCM 4,468; FXBUF 126,592 (margin 3,712).
- `bss` fell by 16,100 B against S083/S084; the released RAM is not
  reused (RAM policy).
- New host targets `natural_order`, `catalogue`, `stock_gen`,
  `osc_run`, `osc_pwm`, `osc_sample`, `wtb`, `osc_s0`; ARM report
  targets `armcheck-osc-base`/`armcheck-osc` with `looplen.py`.
- Card: `wavetables/ak_hvox` optimised 600 -> 256 points,
  124,800 -> 53,248 B (worst -23.2 dB, median -44.8 dB, -0.14 dB table
  scale); originals in `wavetables_old/ak_hvox`; `samples/01_cut.wav`
  unchanged (7,460 B). Report recorded here.

### Deviations / open points
- **D1** (block-rate wtb frame pair) is implemented as specified.
- **D2** (retained writes only) implemented through the writer's
  `retained` flag.
- **D11** (per-slot wav domain) implemented in all three sites.
- **snc cost cap (C10.6):** the aggregate ARM report is available, but the
  per-sample worst case was not isolated from the inlined codegen; the
  constant-cost fallback loops were therefore **not** added. Re-measure at
  hardware bring-up (H11); if the cap is exceeded, C10.6's per-type loops
  remain the prepared fallback.
- **Break-proof runs:** the new tests were written to fail on the listed
  breaks; the scratch-copy break runs must be recorded on hardware/CI. The
  green runs above are the current gate state.
- **Runtime-slot sizes:** the fixed 1,176 B reserve passes every
  `_Static_assert`; exact per-engine sizes need the G3 measurement tool
  (`pahole`/debug info) and are not recorded yet.
- **Spike path:** in this checkout the spike is
  `../Helicase-dev-ph7-spike/`, not inside the working tree. `MEMORY.md`
  was left with the observed path; the plan's §1 correction is therefore
  amended here.
- Hardware matrix C19.3 (H1–H11) remains for the user.

### WP18–WP19 — documents and gates
- Documents written: `085_SESSION_HANDOFF_LOG.md`, `MEMORY.md`
  (sample flash, builds, volatile S085), `FILESYSTEM_SPEC.md`,
  `INSTRUMENTS_DSP_REFERENCE.md`, `CPU_USE_DSP_AUDIT.md`,
  `EFFECTS_MIXER_DSP_REFERENCE.md`, `MODULE_INTERCHANGE_SPEC.md`,
  `STORAGE_SRAM_MANIFEST.md`, `BANK_PRESET_ARCHITECTURE.md`,
  `OSC_INTERP_AUDIT.md`, `MIDI_SPEC.md`, `DEV_MODES.md`,
  `SCOPING_TARGETS.md` §7.4 and `tools/dsp_test/DSP_TEST.md`.
- G0–G4 are green (build, host tests, `special_tags`, image). G5's
  cost-record items still open: per-sample loop probes, exact engine sizes,
  snc cap, break-proof scratch runs, and the hardware matrix.

---
## 22. Assessment of the implementation (review, 2026-10-27)

**Verdict:** the firmware implements the schedule faithfully and builds
clean; the host suite is green. It is **not ready for the hardware matrix
yet**:
- one correctness bug in the pitch slew (§22.2 A1);
- the `snc` worst case is over the C10.6 cap, and the fallback loops were
  not added (A2);
- the cost record (C19.2) does not exist, and the probes used cannot
  produce it (A3);
- two tests cannot fail (A4, A5), and several C17.6 checks are missing
  (A6, A7).

Fix the bug and the `snc` loops, then rerun §22.6 before C19.3.

### 22.1 Verification repeated

| Check | Result |
|---|---|
| `make all` (DEV) | clean; flash 641,384 / 753,664 (headroom 112,280); ITCM 10,632 / 16,384; DTCM 4,468; FXBUF margin 3,712. Matches §21. |
| Host: `natural_order`, `catalogue`, `stock_gen` (worst 1 LSB), `osc_run` (all/pwm/sample/wtb), `osc_s0` | 0 failures |
| `special_tags` | 177 rows, 0 mismatches |
| `selftest`, `filter` (S1 PASS), `pack`, `postchain`, `mixer`, `armcheck-postchain` (MATCH), `armcheck-mixer` | unchanged, pass |
| G4 leftover-symbol search (`insertFx_`, `driveShaper`, `transient_is`, `voiceBuffer_`, `slotQuiet`, `OSC_SAMPLE_START`, `OSC_WAVE_INTERP_MAX_ACTIVE`, `menu_numSamples`, `sampleMemory_install`, `filesystem_installLoopsBlocking`, `calcUserSampleOsc`, `sampleCacheLooped`, `sampleMemory_isLooped`) | 0 hits in `Core/`, `main.c`, `config.h`, `Makefile` |

### 22.2 Findings (most severe first)

#### A1 — Pitch slew overshoots on its last block (correctness, high)

- **Where:** `Core/DSPAudio/Oscillator.c:1122-1134` (`osc_setFreq()`),
  with `OSC_SLEW_SAMPLES` 132 (`Oscillator.h:111`) and `OUTPUT_DMA_SIZE` 32.
- **Defect:**
  - The step is computed as `remaining / slewSamplesLeft`.
  - Every renderer adds that step for all 32 samples of the block.
  - 132 is not a multiple of 32, so the last slew block starts with
    `slewSamplesLeft` = 4. That block computes the step for 4 samples and
    applies it for 32.
  - The increment overshoots the target for 28 samples, then snaps back on
    the next block (`left == 0`).
- **Measured** with the real `osc_setFreq()` and `calcNextOscSampleBlock()`
  (scratch probe, 200 → 400 Hz saw): **the increment peaks 20.5 % of the
  interval past the target.** That is a short pitch spike and a slope step on
  every `fre` trigger and every modulator in `itp`.
- **Fix (either, no CPU change):**
  - divide by `max(left, OUTPUT_DMA_SIZE)`, so the last block spreads the
    remainder over the samples it actually renders; or
  - make `OSC_SLEW_SAMPLES` a multiple of the block (128, 2.9 ms).
- **The test missed it:** `test_slew` (`tools/dsp_test/test_osc_run.c:68`)
  calls `osc_setFreq()` without rendering, so it never sees the per-sample
  trajectory (see A5).

#### A2 — The `snc` worst case is over the cap; C10.6 step 4 applies (CPU, high)

- **Where:** `Core/DSPAudio/OscSection.c:160-217`
  (`oscSection_renderSync()`).
- **One wrap per sample is reachable:**
  - A sample master's increment is 1,024 × f / FS frames per sample (Q20.12),
    about 290 frames per sample at a 12.5 kHz note.
  - Its cycle is `end − start` frames, as small as size / 128 at `wav` 127.
  - The master therefore wraps on every sample. A table master at the top of
    the note range wraps every 3–4 samples.
- **Cost of one wrap:** each wrap makes a full `calcNextOscSampleBlock()`
  call for a 1-sample segment. That call pays:
  - the type dispatch, prologue and epilogue;
  - the per-call setup: `stockWave_level()`; `oscSample_bind()` for
    `smp`/`slp`; for `wtb`, `oscWavetable_bind()` and the frame pair with two
    float divides.
- **The cap cannot hold.** Straight-line instructions outside the loop:
  - `calcStockWaveBlock` 38 (whole function);
  - `oscSample_render` 45;
  - `oscWavetable_render` 161 (all paths).

  Even the cheapest slave is about the 24-instruction loop + 38 + the wrap
  loop per sample. The cap is FM carrier + 10 ≈ 46.
- **`itp` doubles it:** in `snc`, `itp` renders the old carrier through the
  same segments.
- **Required:** implement the C10.6 step 4 per-type sync loops before G3 is
  closed. One loop per slave family (table/stock, sine, pwm, sample, wtb;
  noise and crash as table reads), each with constant cost of about (c) + 6.
- **About §21:** it defers this to H11. Under the constant-CPU policy the
  static evidence already decides it, and H11 cannot prove the worst case
  unless the test Scene happens to hit it.

#### A3 — The cost record does not exist, and the probes cannot produce it (process/CPU, high)

- **The probes measure the wrong thing:**
  - `armcheck-osc-base` / `armcheck-osc` (`tools/dsp_test/Makefile:135-151`)
    run `looplen.py`, which counts instructions **outside** loops.
  - The §21 baseline (`calcSineBlock` 18, `calcFmBlock` 22, …) is therefore
    prologue and epilogue, not per-sample cost.
  - For the new renderers the counts are meaningless. `oscPwm_render` is a
    7-instruction tail call into `oscPwm_block`. The `wtb`/`smp` objects
    contain many nested and duplicated loop paths.
- **C17.4 is not built:** it asked for force-inlined wrappers, one per case
  with block constants fixed, so each wrapper is exactly one loop.
- **Measured in this review** with `fpseq.py --report --all` on the existing
  objects (single-loop functions only):

  | Loop | pre-S085 | S085 | Note |
  |---|---:|---:|---|
  | table (`calcWavetableOscBlock`) | 24 | 25 | +1 slew add |
  | table FM (`calcFmBlock`) | 35 | 36 | +1 |
  | sine / FM | 23 / 30 | 24 / 31 | +1 |
  | crash / FM (`calcSampleOsc*Block`) | 30 / 38 | 31 / 39 | +1 |
  | stock / FM | — | 24 / 34 | target ≈ table: met |
  | itp blend | — | 23 | per voice while active |
  | wtb 8-bit / 16-bit / FM | — | ≈ 37–38 / 41–42 / 44–49 | loop-body lengths. Which body is the per-sample path is unconfirmed; D1 target 42 / 48. |
  | pwm, smp/slp, snc, rng | — | not measurable | per-sample `switch`/branches; needs the C17.4 wrappers |

- **Required:**
  - build the C17.4 wrappers;
  - fill C19.2, including the derived per-voice worst case for each engine,
    which S086 needs;
  - replace the §21 baseline figures with the per-sample ones above.

#### A4 — The `itp` click test cannot fail (test, medium)

- **Where:** `tools/dsp_test/test_osc_run.c:86-137`.
- **Defect:**
  - After `oscSection_beginTrigger()` the test never resets the carrier
    phase, as the engines do on `OSC_TRIGGER_RESET_CARRIER`.
  - The test also never changes the carrier pitch.
  - So the old copy and the live carrier are the same oscillator, and the
    blend is a no-op.
- **Proved** with a scratch probe:
  - as written, the old-carrier block is **bit-identical** to the new one;
  - with the engine's reset applied, the first sample jumps by 12,672, which
    is the step the blend must hide.
- **Fix:**
  - apply the engine reset (`phase = (0xff << 20) × offset`, or
    `oscSample_restart()`) and a new pitch after the trigger;
  - add the break run: `oscSection_blendItp()` as a no-op must fail;
  - add the C17.6 cases that are missing: `mix` mode, and the
    "modulators reset" fail variant.

#### A5 — The slew test never renders (test, medium)

- `test_slew` checks only `osc_setFreq()` bookkeeping, so A1 passes it.
- **Add** a rendered trajectory:
  - the increment is monotonic toward the target and never past it;
  - it reaches the target within `OSC_SLEW_SAMPLES` samples;
  - the break: restoring the current divisor must fail.

#### A6 — C17.6 coverage gaps (test, medium)

| Area | Present | Missing (C17.6) |
|---|---|---|
| pool | two ongoing fill the pool; a refused pending restores phase and arms the slew | the waveform-interpolation-in-between ordering case |
| `snc` | a finished one-shot master stops resetting | reset index (D13: slave rendered through *i*, reset at *i* + 1), the mix law, a sample master with start > 0 |
| `rng` | none | the wet/dry law |
| `smp`/`slp` | request clamp, start = size/2 at `wav` 64, end silence, `isFinished` | full 100,000-frame playback; re-expansion (12 installed binds 9); gain code; a seam check that can fail (`seam < 4000000` is always true for int16) |
| `wtb` | position 1..3 after a jump | continuity across blocks, idx/nx bounds at `frame_len` − 1, the 8-bit path, F11 |
| `pwm` | `prc` polarity, correlation > 0.9 | the schedule's > 0.99; F3 midpoints within 1 LSB of the reference; monotonic width |

Also merged against the schedule: `osc_pwm`, `osc_sample` and `wtb` are mode
aliases of `osc_run`, not separate files. This is harmless, but the per-file
break runs (C17.9) need recording per mode.

#### A7 — `osc_s0` gaps (test, medium)

- **Not compared:**
  - the noise renderer;
  - the HiHat carrier gain 0.5 (FM2 runs at 1.0 only);
  - a block with an `osc_setFreq()` pitch change (only `phaseIncStep` = 0
    blocks).
- **The drum fm/mix reference is hand-written in the test** from the old
  functions. The schedule asked for the frozen85 engine lines through
  `extract.py --lines`. As written, the test proves equivalence with the
  test author's reading of the old engine, not with the frozen code.

#### A8 — `itp` retrigger during an active crossfade steps (behaviour, low)

- **Where:** `OscSection.c:58-64`.
- **Defect:**
  - A retrigger while ACTIVE re-snapshots only the live carrier and restarts
    the fade at 0.
  - The audible signal at that moment is
    old × (1 − *t*) + new × *t*. It jumps to new, a step of
    (1 − *t*) × (old − new).
  - This happens only for retriggers less than 3 ms apart (fast rolls and
    ratchets).
- **Options:**
  - accept it, document it, and add it to H7 with a roll; or
  - keep the existing old copy when *t* < 0.5. That is cheaper but still
    steps.

  An exact fix needs a second copy (+124 B per voice; RAM approval).

#### A9 — The D11 LFO base is clamped permanently on a type round trip (behaviour, low)

- **Where:** `Core/DSP/Instruments/InstrumentManager.c:837`
  (`instrumentManager_refreshWaveDomains()`).
- **Defect:**
  - Narrowing to `wav` clamps `adapter->base_value` to 10.
  - Going back to `pwm`/`smp`/`wtb` widens the domain but leaves the base at
    10, while `waveRequest` keeps the stored value (e.g. 100).
  - The LFO then centres on 10 until the target is reassigned, and the
    restore writes 10 to the runtime.
- **Fix:** on refresh, set the base to `min(osc->waveRequest, max)`, not
  `min(base, max)`.

#### A10 — Break proofs and sizes (process, low)

- **Break proofs:** §21 says the break runs "must be recorded on
  hardware/CI". C17.9 is a host procedure in scratch copies, and it is
  runnable now (A4 shows one). Run and record them for every new test.
- **Engine sizes,** measured in this review (`arm-none-eabi-gcc`, firmware
  include paths, `sizeof`):

  | Struct | Bytes | In the 1,176 B slot |
  |---|---:|---|
  | `DrumVoice` | 832 | 344 B left (pre-S085: 588) |
  | `HiHatVoice` | 780 | 396 B left |
  | `CymbalVoice` | 772 | 404 B left |
  | `SnareVoice` | 664 | 512 B left |
  | `OscInfo` | 124 | |
  | `OscSectionState` | 140 | one `OscInfo` copy + 16 |

  Record these in `STORAGE_SRAM_MANIFEST.md`, as C18 requires.

### 22.3 Reviewed and found correct

- **Oscillator model:**
  - type dispatch is per block, with no per-sample type test;
  - wav values 0..5 are unchanged and 6..10 are stock waves; out-of-list
    renders silence;
  - the slew add is in every renderer, including the interpolation copies,
    which now write back `oscA.phase` and `phaseInc`.
- **Selector writers:**
  - `IM_SPECIAL_OSC_TYPE/_SOURCE/_WAVE/_RUN_MODE`: the type change
    re-derives wave and source from the request bytes;
  - only retained writes record requests (D2; LFO overlay and restore pass
    0);
  - `wtb` is re-primed on entry;
  - `refreshWaveDomains()` covers LFO adapters and velocity at all three
    sites (D11).
- **Pool order** (`mixer.c`): reset → ongoing `itp` → modulation
  (waveform-interpolation claims, `wav` type only) → pending `itp` → filter
  recalc → `calcSlotAsync()`. A refused pending restores phase and arms the
  slew before `osc_setFreq()`. This matches Q14 and the carrier-only rule.
- **Section:**
  - `snc` is correct apart from cost (A2): it renders through sample *i*
    then resets (D13), stops on a finished one-shot master (D6), computes
    ratios once per block, and uses the cycle-relative shadow for a sample
    master;
  - `rng` and the `mix` law are as specified;
  - the filter slew: `rst` is exactly `SVF_recalcFreq()`.
- **Engines:**
  - Drum keeps the envelope-closed reset condition and the unconditional
    filter reset in `rst`;
  - HiHat carrier gain is 0.5;
  - Cymbal resets its modulators only in `rst`;
  - Snare crossfades the tone oscillator before the transient sum;
  - every engine calls `finishBlock()`.
- **Sample and wavetable:**
  - Q20.12 addressing (≤ 1,048,576 frames; items region 621,728 frames);
  - smp end silence is a select (constant cost);
  - the slp wrap;
  - `wtb` frame count is capped at 128 by the installer and stays within
    `uint8_t`;
  - pass A and pass B of the table walk use the same filter.
- **Preset (C14.1–C14.5):**
  - selector store with both mirrors;
  - apply after every slot reset and in the immediate Instrument apply;
  - the rebind loop skips selectors.
- **Menu:**
  - formatted cells from the Scene image (D5);
  - `mixFmNames` 4 values and 11 waves, both with `_Static_assert`s;
  - SHIFT Morph selector edits go to Normal (D12);
  - the install summary is held by a 1,500-tick deadline (C3.10).
- **Structure and documentation:** no G4 leftovers; the documentation
  blocks are present at the change sites.

### 22.4 Review of the §21 claims

| Claim | Assessment |
|---|---|
| G0–G4 green | **G0, G2 and G4 agree. G3 is not closed:** the C10.6 decision is due at G3 and comes out over the cap (A2). |
| Baseline loop counts 18/22/17/20 | Straight-line counts, not per sample. Per sample: 23/35/24/30 (A3). |
| "per-sample worst case not isolated from the inlined codegen" | True of `looplen.py`. The C17.4 wrapper technique exists for exactly this and was not built. |
| Break proofs need hardware/CI | No; they are host runs (A10). |
| Engine sizes need pahole | Measured here with a `sizeof` probe (A10). |
| Card optimisation figures, flash/RAM figures | Consistent with the build output; not re-derived. |

### 22.5 Work order before C19.3

1. **A1 slew fix,** then the rendered slew test (A5) with its break run.
2. **A2 per-type `snc` loops** (C10.6 step 4); record the decision in the
   handoff log.
3. **A3 C17.4 wrappers;** fill C19.2, including the derived per-voice and
   pool worst cases, and the `snc` figure against the cap.
4. **Fix A4;** fill the A6/A7 gaps; run and record C17.9 break proofs for
   every new test.
5. **Decide A8** (document or change) **and fix A9.**
6. **Record the A10 sizes** in `STORAGE_SRAM_MANIFEST.md`.
7. **Rerun §22.1,** then hand over C19.3 (H1–H11). Add a fast roll in `itp`
   to H7 and an `smp` master at `wav` 127 to H11.

---
## 23. Fixes for the §22 findings (2026-10-27)

All ten findings are dealt with. The firmware builds clean, and every host
gate is green. Each new test fails on a deliberately broken version (26
break proofs, §23.4).

### 23.1 What changed, by finding

| # | Change | Files |
|---|---|---|
| A1 | `osc_setFreq()` divides the remaining slew by `max(slewSamplesLeft, OUTPUT_DMA_SIZE)`, not by `slewSamplesLeft`. The difference is taken in `uint32_t` and cast once, so increments above 2^31 cannot overflow `int32_t`. The glide is linear for 128 samples; the last 4/132 of the interval spreads over a fifth block (160 samples in all). It is monotonic and never passes the target. `OSC_SLEW_SAMPLES` stays 132, and its comment now states this. | `Oscillator.c` (slew block of `osc_setFreq()`), `Oscillator.h` |
| A2 | The `snc` segment render is replaced by the C10.6 step 4 constant-cost form. See §23.2. | new `OscSyncLane.h`; `Oscillator.c/.h`, `OscillatorPwm.c/.h`, `OscillatorSample.c/.h`, `OscillatorWavetable.c/.h`, `OscSection.c` |
| A3 | Per-sample probes built (C17.4) and the cost record filled (§23.3). `osccost.py` (new) reports the worst-case path of every natural loop from the control-flow graph. The probe wrappers are `armcheck/probe_wav.c`, `probe_pwm.c`, `probe_smp.c`, `probe_wtb.c` and `probe_section.c`. `armcheck-osc-base` and `armcheck-osc` now print per-sample figures; the `looplen.py` calls are gone. | `tools/dsp_test/osccost.py`, `armcheck/probe_*.c`, `Makefile` |
| A4 | `test_itp_click` rewritten. It applies the engine's carrier reset and a new pitch, then runs the mixer's claim order for 6 blocks, in `fm` and in `mix`. The bound is the step of the same sound changing pitch without a trigger, plus the fade slope (65,536 / 132). Measured: fm crossfade 1,785 against bound 2,404 (rst 32,365); mix 921 against 1,406 (rst 16,007). A `mix` variant that also resets the modulator steps 9,222 and must exceed the bound. The test also asserts that the old and new carrier blocks differ. | `tools/dsp_test/test_osc_run.c` |
| A5 | `test_slew` renders the trajectory up and down. The per-sample increment must be monotonic, never pass the target, reach it within 6 blocks, and match the renderer's write-back. | same |
| A6 | New cases (§23.4 lists the breaks that prove each one): <ul><li>pool order with interpolation between ongoing and new crossfades;</li><li>`snc`: no-wrap bit-identity for all nine slave families; D13 reset index for saw, `smp`, `pwm` and `wtb` slaves; the `itp` copy's own ratio; Q10 mix law; F5 sample master with start > 0; `rng` law;</li><li>`smp`/`slp`: re-expansion (7 of 5 binds 4, then 7 of 8); a full 100,000-sample one-shot; the exact `slp` seam and wrap; gain code 24 = ×2;</li><li>`wtb`: block continuity (2 × 32 = 1 × 64); the nx wrap at `frame_len − 1`; the 8-bit path;</li><li>`pwm`: polarity > 0.99; F3 midpoints within 1 LSB; monotonic `prc` width.</li></ul> `prc` high time rises from 50 % to 96 % as `wav` goes 0 → 127. New host hooks: `host_set_gain_code()`, `host_set_table_bits()`, `host_rng_reset()`, `host_sample_data()`, `host_table_data()`. | `test_osc_run.c`, `host_osc_support.c` |
| A7 | `osc_s0` additions: <ul><li>the frozen noise loop;</li><li>a 20-block pitch sweep (27.5 Hz → 10.9 kHz) through the frozen `osc_setFreq()` and the current one, comparing increments, table levels and samples for sine, tri, saw, rec, noise and crash;</li><li>Drum fm/mix (4 cases) and Cymbal/HiHat FM2 against the **verbatim frozen85 engine lines**: Drum 313–326, Cymbal 240–247, HiHat 257–264, extracted with `extract.py --lines` and wrapped by `s0_engines.c` / `s0_engine_shim.h`. This covers the HiHat 0.5 carrier gain.</li></ul> The hand-written old compositions are gone. `extract.py` now skips comments between `)` and `{` (frozen `freq2PhaseIncr()`). | `test_osc_s0.c`, `s0_engines.c`, `s0_engine_shim.h`, `extract.py`, `Makefile` |
| A8 | Accepted and documented at `oscSection_beginTrigger()`: S084 P1 F6 / plan R11. An exact fix needs a second carrier copy per voice (+124 B RAM each). H7 gains a fast roll in `itp`. | `OscSection.c` |
| A9 | `instrumentManager_refreshWaveDomains()` sets each LFO adapter's base to `min(osc->waveRequest, new max)`, not `min(old base, new max)`. A `pwm → wav → pwm` round trip therefore restores the LFO centre. | `InstrumentManager.c` |
| A10 | Break proofs automated (`breakproofs.py`, `make breakproofs`). It applies each defect in a temporary copy of the tree, so the working tree is untouched. 26 breaks, all caught (§23.4). Engine sizes recorded (§22.2 A10, unchanged by these fixes) and added to `STORAGE_SRAM_MANIFEST.md`. | `tools/dsp_test/breakproofs.py`, `Makefile` |

### 23.2 The constant-cost `snc` (A2, C10.6 step 4)

- **Lane.** Once per block, `oscSection_buildSyncLane()` walks the master's
  shadow phase. It records `wrap[i]` (the master wrapped after sample *i*)
  and `over[i]` (its overshoot) on the stack (2 × 32 entries, 160 B; no
  static RAM).
  - The sample-master modulo runs on every sample, not only on a wrap.
  - A finished one-shot master records no wraps (D6).
- **Slave render.** One call per block:
  - The slave renders through `osc_renderSyncBlock()`, which dispatches by
    type once.
  - Inside the loop, `oscSync_phase()` replaces the phase after sample *i*
    with `base + over[i] × ratio` when `wrap[i]` is set (D13: reset on
    *i* + 1). `base` is 0, or `sampleStart << 12` for `smp`/`slp`.
  - The `itp` copy uses the same lane with its own ratio. The master is
    rendered once.
- **Placement and ITCM:**
  - **wav cores** (sine, Tri/Saw/Rec, noise, crash, stock) are
    always-inline with a lane argument. Every plain entry point passes a
    literal NULL, so the plain loops are unchanged instruction for
    instruction (pre-S085 counts + 1 for the slew; `osc_s0` bit-identical).
    `osc_renderSyncBlock()` holds their sync copies (1,972 B ITCM).
  - **pwm, smp/slp and wtb** share one loop between plain and sync
    (`OSC_SHARED_LOOP`: noinline, noclone, no loop unswitching), with a
    per-sample lane test. A separate sync copy of every renderer was built
    first and cost 5.6 KB of ITCM (16,272 / 16,384 B), leaving S086 none.
    The shared form costs the plain pwm/smp/wtb render 2 instructions per
    sample (the NULL test) and saves 3.4 KB.
- **Compiler note.** GCC compiles the `oscSync_phase()` select as a branch
  around the reset arithmetic (the reset reads `over[i]`). The no-wrap path
  is therefore shorter. The figures below budget the wrap path (the worst
  case).

**Build after the fixes (DEV):**

| | Before §23 | After §23 | Change |
|---|---:|---:|---:|
| text | 640,960 | 642,360 | +1,400 |
| bss | 414,100 | 414,100 | 0 |
| Flash used / headroom | 641,384 / 112,280 | 642,784 / 110,880 | +1,400 |
| ITCM | 10,632 / 16,384 | 12,432 / 16,384 | +1,800 (3,952 B free) |
| DTCM statics | 4,468 | 4,468 | 0 |
| FXBUF margin | 3,712 | 3,712 | 0 |

### 23.3 Cost record (C19.2), worst-case instructions per sample

- **Method:** `osccost.py` (static longest path through each loop body)
  on `arm-none-eabi-gcc 14.2 -Ofast`, Cortex-M7 flags. The wrapper probes
  fix every block constant.
- **Firmware plain pwm/smp/wtb** = the no-lane figure + 2 (the lane test),
  confirmed on `prc`: 49 → 51.
- **Calls count as 1.** Noise calls `GetRngValue()` on a wrap.
- **Not included:** sample-flash read wait states for the stock, `smp` and
  `wtb` table reads. H11 measures the real figure.

| Loop | pre-S085 | S085 plain | S085 FM | S085 as `snc` slave |
|---|---:|---:|---:|---:|
| table Tri/Saw/Rec | 24 | 25 | 36 | 35 |
| sine | 23 | 24 | 31 | 34 |
| noise | 16 + call | 16 + call | (plain) | 20 + call (hand-traced; irreducible loop) |
| crash | 30 | 31 | 39 | 37 |
| stock `sq3`…`swa` | — | 24 | 34 | 35 |
| pwm `prc` / `psw` | — | 51 | 56 | 63 |
| pwm `ptr` / `pq3` / `pw3` | — | 42 | 47 | 53 |
| pwm `swp` seg 0 / 1 / 2 / 3 | — | 48 / 50 / **58** / 52 | 53 / 55 / **62** / 57 | 60 / 63 / **70** / 65 |
| `smp` | — | 40 | 45 | 53 |
| `slp` | — | 47 | 53 | 61 |
| `wtb` 8-bit / 16-bit | — | 40 / 43 | 44 / 48 | 51 / 54 |

**Section loops:**

| Loop | Worst |
|---|---:|
| `snc` lane builder, full-wrap master | 11 |
| `snc` lane builder, sample master (includes one UDIV) | 18 |
| `snc` mix pass / with the `itp` copy | 16 / 28 |
| `rng` law | 19 |
| `itp` blend | 23 |

- **D1 target met:** `wtb` 43 / 48 against the target 42 / 48.
- **Not `wtb`:** the dearest oscillator is `pwm swp` segment 2, with
  58 plain and 62 FM.
- **`snc` slave overhead:** 10–16 instructions per sample over the plain
  loop, against the C10.6 estimate of about 6. It is constant per sample
  and independent of the wrap count. The segment form it replaces paid a
  full renderer call (38 to over 100 instructions of setup) on every
  wrap, up to every sample.

**Derived worst cases** (oscillator section only; the pre-existing
saturating adds are not counted):

| Engine / mode | Composition | Worst |
|---|---|---:|
| Drum `fm` | modulator 58 + FM carrier 62 | 120 |
| Drum `mix` | 58 + 58 | 116 |
| Drum `snc` | lane 18 + master 58 + slave 70 + mix 16 | **162** |
| Drum `rng` | 58 + 58 + 19 | 135 |
| Snare | tone 58 + noise 16 | 74 |
| Cymbal / HiHat | 2 × 58 + FM carrier 62 | **178** |
| Pre-S085 Cymbal (for comparison) | 2 × 30 + 38 | 98 |
| One `itp` pool unit | old carrier ≤ 70 + extra mix 12 (`snc` case) + blend 23 | ≤ 105 |
| Pool (2 units, system-wide) | 2 × 105 | ≤ 210 |
| System worst (6 Cymbal/HiHat-class slots + pool) | 6 × 178 + 210 | ≈ 1,278 |

- **System worst in context:** ≈ 1,278 instructions per sample, about
  26 % of the 4,897-cycle sample period at one instruction per cycle.
  Pre-S085 the same arrangement was ≈ 6 × 98 + 2 × the interpolation unit.
- **For S086:** budget the per-voice oscillator worst case of 178
  (Cymbal/HiHat) and 162 (Drum `snc`) beside the 88-cycle chain, plus the
  pool's 210 once.

### 23.4 Break proofs (C17.9, `make -C tools/dsp_test breakproofs`)

Every break makes its target fail (FAILS = good):

| ID | Defect | Target | Caught by |
|---|---|---|---|
| B1 | slew divisor back to `slewSamplesLeft` | `osc_run` | slew never passes the target |
| B2 | `itp` blend is a no-op | `osc_run` | itp step 32,365 > bound 2,404 |
| B3 | sync reset one sample early | `osc_run` | D13 reset index (saw, smp, pwm, wtb) |
| B4 | `snc` mix law swapped | `osc_run` | no-wrap identity, mix law |
| B5 | `rng` `>> 14` | `osc_run` | rng law |
| B6 | shadow ignores the sample start | `osc_run` | F5 |
| B7 | a finished one-shot master still resets | `osc_run` | D6 |
| B8 | `itp` copy uses the carrier's ratio | `osc_run` | itp copy ratio |
| B9 | `smp` sync base 0 | `osc_run` | D13 smp |
| B10 | a pending crossfade ignores the pool | `osc_run` | pool fallback and order |
| B11 | `smp` end not silent | `osc_sample` | end silence, full playback |
| B12 | `slp` seam reads the last frame twice | `osc_sample` | seam |
| B13 | gain code ignored | `osc_sample` | gain ×2 |
| B14 | the clamp overwrites `sourceRequest` | `osc_sample` | re-expansion |
| B15 | `wtb` nx bound removed | `wtb` | nx wrap |
| B16 | `wtb` 8-bit `<< 7` | `wtb` | 8-bit path |
| B17 | `wtb` phase not written back | `wtb` | block continuity |
| B18 | pwm fraction 1/2^32 (F3) | `osc_pwm` | midpoints within 1 LSB |
| B19 | `prc` polarity inverted | `osc_pwm` | polarity, width |
| B20 | HiHat carrier gain ignored | `osc_s0` | hihat fm2 (0.5) |
| B21 | noise value halved | `osc_s0` | noise, noise sweep |
| B22 | table level capped at 9 | `osc_s0` | sweep tri/saw/rec block 19 |
| B23 | Drum `mix` carrier gain | `osc_s0` | drum mix |
| B24 | natural order ignores digit-run length | `natural_order` | f2 < f10 |
| B25 | gain code not derived from the peak | `catalogue` | gain code for peak 16384 |
| B26 | stock level scale dropped | `stock_gen` | wave 0 level 0 |

**Not host-testable** (`InstrumentManager.c` is not compiled by the
harness): F11 type-writer hygiene and the A9 base re-derivation. Hardware
rows H6 and H9 cover them.

### 23.5 Gate state and what remains

- **G0–G4:** green.
  - `make all` clean; `make img` written (642,784 B).
  - Host: `natural_order`, `catalogue`, `stock_gen`, `osc_run` (all, pwm,
    sample, wtb), `osc_s0`, `special_tags` (177 / 0), `selftest`, `filter`
    (S1 PASS), `pack`, `postchain`, `octave` (its documented 16
    mismatches), `mixer`, `armcheck-postchain` (MATCH), `armcheck-mixer`.
  - `breakproofs`: 26 / 26.
- **G5:** the cost record is filled (§23.3). The remaining item is the
  hardware matrix C19.3.
- **Hardware matrix additions:**
  - H7: a fast roll in `itp` (A8).
  - H8: `snc` with an `smp` master at `wav` 127 at a high note: one wrap
    per sample, the A2 worst case.
  - H11: the same `snc` worst case. The figures exclude sample-flash wait
    states, so H11 is the real CPU measurement.
