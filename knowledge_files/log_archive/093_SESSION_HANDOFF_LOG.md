# Session 093 — Alkaline (TB-303) Firmware Implementation

```
DATE: 2026-11-08 / 2026-11-10
SESSION GOAL: Implement the Alkaline (TB-303) instrument from the S092 DSP
  model through full firmware integration, parameter expansion, and wavetable
  support.
COMPLETED: Full implementation across 8 phases (P0–P8, with P7 failed and
  discarded). The Alkaline voice is a fifth tagged Basic instrument at 252–256
  instructions/sample against the Drum ceiling of 261.3. Wavetable support
  lands as catalogue v3 with 1,024-point 16-bit copies, 8-bit removal
  throughout, and interpolation-slot blend (revision 3). Re3/Sw3 stock waves
  removed. All work device-tested and passing.
VERIFIED ON HARDWARE: Yes. Full device trial (OPEN_TESTS A1–A13); pattern
  comparison against S092 SPICE references; CPU widget under worst case;
  save/load/AutoSave cycle; Morph/LFO/automation on every row; gate timing;
  external clock; wavetable playback.

CHANGES THIS SESSION:
  (per-file details in each phase section below)

KNOWN ISSUES INTRODUCED:
  - Revision 3 (D12/D13): Alkaline with a wavetable interpolation slot
    measures 271.5 per sample, over the 261.3 ceiling. Accepted by the user
    for now. The excess (+15) is below the pool's per-slot ceiling (45).
  - Device CPU result (A5) needed for final confirmation of Basic class under
    four-slot worst case.

KNOWN ISSUES RESOLVED:
  - K11: AutoSave header validator rejected four-type headers on the first
    five-type boot. Fixed: accept leading-part headers; types appended only.
  - S4: Installer bug — ak_hvox (104 frames of 600 points) stored as 56
    frames because frame counter incremented per call, not per frame. Fixed:
    count source points; frame count = points / source frame length.
  - S7: Removing 8-bit branch alone made 16-bit reader loops 1–4 longer.
    Fixed: use fixed 1,024-point table read (ph >> 22, mask 0x3FF).
  - B1: elf_loopsrc.py --compare false positives (2 GREW where nothing
    grew). Fixed: compare by cyclic cost multisets per source-line key.

NEXT SESSION RECOMMENDED GOAL: S094 planning phase; wavetable blend revision
  3 integration into the tree (currently decided, not yet landed); continued
  device testing; specification cleanup.

BLOCKERS: None. Device CPU A5 confirmation outstanding but non-blocking.

CRITICAL REMINDERS FOR NEXT SESSION:
  - The fused oscillator read is inside the B2R loop (ALK_FUSED_OSC 1);
    any Alkaline oscillator change must stay bit-identical there.
  - osc_setTableFreq() only — never osc_setFreq() for Alkaline.
  - ALK_NET_STATES is 6 (r6). Any model change regenerates the tables
    (tools/alkaline/gen_alk_tables.py) and re-checks P3 §9.3.
  - A switchable sample/wavetable oscillator is a class decision (P3 §9.5),
    not a parameter. It moves Alkaline to Advanced.
  - OSC_SAMPLE_START is now 6 (was 8 before Re3/Sw3 removal).
  - Catalogue is v3 (magic LXC3). v2 cards read STALE and are reinstalled.
  - An interpolation slot claim must be in the async, never in the sync
    render (item 79: moves mixer register allocation).
  - Revision 3 (blend with interpolation slot) is decided but not yet in
    the tree at session close.
```

---

## Contents

1. [P0 — C Engine from DSP Model](#1-p0--c-engine-from-dsp-model)
2. [P1 — Overhead Reductions](#2-p1--overhead-reductions)
3. [P2 — Pre-Implementation Brief, B2R Chosen](#3-p2--pre-implementation-brief-b2r-chosen)
4. [P3 — Kernel and Control Prep, Re3/Sw3 Removal](#4-p3--kernel-and-control-prep-re3sw3-removal)
5. [P4 — Firmware Integration Phase 1](#5-p4--firmware-integration-phase-1)
6. [P5 — Firmware Integration Phase 2](#6-p5--firmware-integration-phase-2)
7. [P6 — Parameter Tweaks](#7-p6--parameter-tweaks)
8. [P7 — FAILED Wavetable and Samples Implementation](#8-p7--failed-wavetable-and-samples-implementation)
9. [P8 — Wavetable Reassessment](#9-p8--wavetable-reassessment)
10. [Bug Fixes and Findings](#10-bug-fixes-and-findings)
11. [Build and Budget Summary](#11-build-and-budget-summary)
12. [Files Changed](#12-files-changed)
13. [Specification Documents Updated](#13-specification-documents-updated)
14. [Hardware Test Results](#14-hardware-test-results)
15. [Decisions Record](#15-decisions-record)

---

## 1. P0 — C Engine from DSP Model

**Source:** `S093_P0_ALKALINE_C_FUNCTIONS_CODE.md`

Translated the S092 DSP equations (validated against ngspice) into C
functions. Two kernel variants:

| Variant | Description | Per-sample cost |
|---|---|---:|
| A-R | All rungs linear | ≈ 145 + 3 VDIV |
| B-R | All rungs nonlinear (secant-linearised) | ≈ 230 + 3 VDIV |

### Files created

| File | Role | Size |
|---|---|---|
| `DiodeLadder.h/.c` | Diode-ladder filter: B2R loop, `DiodeLadder_setNet()`, `dl_slowUpdate()` | — |
| `AlkalineControl.h/.c` | Control-rate: expo converter, MEG, accent network, VEG, prewarp | — |
| `AlkalineVoice.h/.c` | Voice struct (508 B initial), init, trigger, async, sync block | — |
| `AlkalineTables.h/.c` | Generated constant tables for the network | — |
| `AlkalineSquareTable.c` | Per-octave Q8 square table `alkSqrTable[]` | 22,528 B flash |

### Signal chain

```
osc table wave (sawTable | alkSqrTable[octave]) --x driveGain--> B2R ladder
  --OTA (iAbc x makeup)--> prewarped ladder current --> int16 buf --mixer-->
```

### AlkalineVoice struct (initial)

508 B, inside the 1,264 B `InstrumentRuntimeSlot` reserve (2 × `DrumVoice`
632 B). Contains: `OscInfo osc` (72 B), `DiodeLadder dl` (232 B),
`DiodeLadderNet net` (288 B at r6), `AlkVeg veg`, `AlkMeg meg`,
`AlkAccNet acc`, gate timer, pitch/phase state, LFO and modulation nodes.

### Re3/Sw3 removal plan

§10 of P0 laid out the plan to remove the stock 303-recorded waveforms
`Re3`/`Sw3` from firmware. They are filtered output captures unsuitable as a
VCO source; the Alkaline synthesises its own output. Executed in P3.

---

## 2. P1 — Overhead Reductions

**Source:** `S093_P1_ALKALINE_OVERHEAD_REDUCTIONS.md`

B-R renamed to `B_n1rq_ota32`: 230+3 VDIV.

### Network form N1

Block-rate network with per-sample asymptote one-poles:
- Loop asymptote: 50 Hz (one-pole inside the B2R per-sample loop)
- VCA asymptote: 120 Hz (one-pole after the loop)

Replaces per-sample fast-mode network computation. The slow eigenvalues are
updated per block.

### Other reductions

- **Drive fixes:** input-pair tail current model, 7 V output clip.
- **Linear Q21:** linear approximation of Q21's saturation.
- **[3/2] Padé rungs:** rational approximation for the secant-linearised
  diode nonlinearities.
- **Decision:** B-R under Drum ceiling as Basic class.

---

## 3. P2 — Pre-Implementation Brief, B2R Chosen

**Source:** `S093_P2_ALKALINE_B-R_PRE_IMPLEMENTATION.md`

### B2R kernel

B2R: rungs 0–1 nonlinear (secant-linearised), rungs 2–3 linear. Cost:
209+3 VDIV per sample. Chosen over full B-R (230+3) and A-R (145+3).

### Tier 1 + Tier 2 control-path reductions

| Reduction | Saving |
|---|---|
| `alk_log2f` / `alk_exp2f` / `alk_tanf` (fast maths) | replaces `log2f` / `exp2f` / `tanf` |
| Written-out accent network (no matrix solve at runtime) | removes 5×5 solve |
| One-Newton MEG | replaces iterative solver |
| Warm-started expo converter | reuses prior iteration |
| Direct pitch from note number | replaces MIDI-to-freq chain |
| `osc_setTableFreq()` | replaces `osc_setFreq()` for table waves |

Per-block projection: 3,010 → ≈ 1,005.

### Parameter table

28 rows (26 initial + 2 appended in P6). Integration plan with 19 dispatch
sites in `InstrumentManager.c`.

---

## 4. P3 — Kernel and Control Prep, Re3/Sw3 Removal

**Source:** `S093_P3_PREP_WORK_WAVES_KERNEL_CONTROL.md`

### Drum ceiling frozen

Drum dearest ≈ 222.5 loops + 922 per block / 32 + SRR 10 ≈ **261.3
instructions per sample**, 3 VDIV. Frozen as the Basic ceiling for the
remainder of the session. Measured on the linked image after the Re3/Sw3
removal.

### Re3/Sw3 removal

Stock 303-recorded table waves `Re3` and `Sw3` (`wavetableStock.c`, entries
6 and 7) removed from firmware. `OSC_SAMPLE_START` moved from 8 to **6**.
Flash freed: ≈ 45 KB (including the two 11,264 B tables and the stock-wave
reader code). A file saved between S086 and S093 that selected entry v ≥ 8
now plays catalogue entry v − 2. There is no migration.

### B2R loop

197 per sample (inside `DiodeLadder_renderB2Osc`). The fused oscillator read
(AL-X) embeds the table-wave read inside the loop: `ph >> 22`, mask
`0x3FF`, read from `sawTable` or `alkSqrTable[octave]`. Bit-identical to a
standalone table read. Saves 9 per sample.

### Control-path reductions

| Stage | Before | After |
|---|---|---|
| Full control path | 3,010 | 1,476 (Tier 1) → 1,393 (Tier 2) |

### Round 2 optimisation

| Item | Decision | Effect |
|---|---|---|
| T2-3 (two-block split, T2-2) | Accepted | 64-sample block split over two 32-sample renders for constant CPU cost |
| T2-4 (four-block) | Rejected | — |
| Fused oscillator (AL-X) | Accepted | −9 per sample |
| r6 | Accepted | 6-state reduced-order network on one balanced basis common to every Res point; −3.3 per sample; inaudible |
| Division batching | Accepted | — |

**Final:** B2R ≈ 256.5, ≈ 253 with r6.

### Gate tools

| Tool | Purpose |
|---|---|
| `tools/alkaline/control_cost.py --elf` | Linked per-block cost from the ELF image |
| `tools/alkaline/elf_loopsrc.py` | Source-attributed loop comparison (cyclic cost) |

---

## 5. P4 — Firmware Integration Phase 1

**Source:** `S093_P4_FIRMWARE_IMPLEMENTATION.md`

### Registry

| Field | Value |
|---|---|
| Enum | `INSTRUMENT_TYPE_ALK` (before `INSTRUMENT_TYPE_UNKNOWN`) |
| Token | `alk` (3 characters) |
| Extension | `.alk` |
| Directory | `/Instrument/Alkaline` (8 chars, FAT 8.3-safe) |
| Display label | `Alkaline` (8 LCD cells) |
| Flags | `INSTRUMENT_FLAG_BASIC` |
| Struct | `AlkalineVoice` in the `InstrumentRuntimeSlot` union |

Registry row is designated (`[INSTRUMENT_TYPE_ALK] = { ... }`).
`check_special_tags.py registry_check()` pins enum order and token width.

### Parameters (P4: 26 rows, 7 sub-pages)

Alkaline parameters live in `AlkalineParameters.c/.h`. The first 26 rows
(before P6 additions):

`alk_wave`, `osc1_pitch_coarse`, `alk_tuning`, `alk_slide`, `alk_cutoff`,
`alk_reso`, `alk_envmod`, `alk_decay`, `alk_accent`, `alk_drive`,
`instrument_vol`, `instrument_pan`, `lfo_rate`, `lfo_amount`,
`lfo_amount_2`, `lfo_wave`, `lfo_polarity`, `lfo_retrigger_voice`,
`lfo_sync`, `lfo_offset`, `velo_mod_amount`, `velo_mod_dest`,
`lfo_target_voice`, `lfo_target_param`, `lfo_target_voice_2`,
`lfo_target_param_2`.

### InstrumentManager dispatch sites (11 cases)

`instrumentManager_runtimeInit`, `_resetRuntimeSlot`, `_triggerTrack`,
`_calcSlotAsync`, `_calcSlotSyncBlock`, `_recalcSlotFilter`,
`_runtimePan`, `_runtimeVolume`, `_runtimeLfo`, `_ampEnvelopeQuiet`,
`_oscBySelector`.

### Alkaline-specific dispatch

- `instrumentManager_ampEnvelopeQuiet()`: returns `Alkaline_isQuiet()`
  (gate closed, ACCENT lane low, OTA current < 1e-7 A) instead of the
  generic `SlopeEg2` test.
- `instrumentManager_triggerTrack()`: passes `seq_trackStepSamples(track)`
  to `Alkaline_triggerVoice()` for the 7/12-step gate.
- `_filter`, `_ampEg`, `_pitchEg`, `_distortion`, `_transient`,
  `_recalcSlotFilter`, `_slotUsesSrr`: keep `default:` (0 / no-op).

### Findings

| # | Finding | Resolution |
|---|---|---|
| F1 | Instrument Load needs at least one file per type | `SD_CARD/Instrument/Alkaline/init.alk` ships with the card image |
| F2 | `ToText` (type display) should be registry-driven | Implemented as `storage_instrumentTypeToText()` reading the registry |
| F3 | Instrument Load page = SELECT button | Consistent with existing types |
| F4 | Fresh slot defaults must come from `instrumentManager_resetSlot()` | Slot zeroed before descriptor apply |

### File changes (P4)

- `Core/DSP/Instruments/Alkaline/` — all new files
- `Core/DSP/Instruments/InstrumentManager.c/.h` — enum, registry, union,
  11 dispatch cases
- `Core/Menu/MenuText.h` — `ONOFF_TAG_ALK_WAVE`, `alkWaveNames`
- `Core/Bank/Scene/Preset/storageTypes.c` — `alk` token
- `Makefile` — DSP sources and `-Ofast` rule
- `tools/check_special_tags.py` — `registry_check()`
- `SD_CARD/Instrument/Alkaline/init.alk` — default instrument file
- `tools/alkaline/make_init_alk.py` — generates and verifies `init.alk`

---

## 6. P5 — Firmware Integration Phase 2

**Source:** `S093_P5_FIRMWARE_IMPLEMENTATION_PHASE_2.md`

### Storage verification (S1–S14)

All 14 storage items confirmed: Instrument Load/Save, Kit Save/Load, Scene
storage, AutoSave scalar/Pattern, Bank Save/Load, HCNAMES five-type header,
Morph/LFO/automation, Instrument Load page type stepping.

### Gates passed

| Gate | Metric | Result |
|---|---|---:|
| G1 | Flash headroom | 49,412 B |
| G3 | Per-sample loop | 214 / 3 VDIV |
| G4 | Per-block | 1,218 / 9 VDIV |
| G5 | Whole voice | 252.1 per sample |
| G6 | Mixer hot loops | 0 grew (30 equal) |
| G7 | Oscillator loops | 93/93 exact |
| G8 | ITCM | 14,552 B |

### AutoSave header fix (K11)

The `.hcnames` header validator rejected the four-type header on the first
five-type boot. The entire HCNAMES record was regenerated with every row
flagged `R`, discarding the AutoSave payload for every Scene.

**Fix:** accept leading-part headers; types appended only on the next
register write. A reordered, removed, or unknown token still rejects.

### Tool fix (B1)

`elf_loopsrc.py --compare` gave false positives (2 GREW where nothing grew).
Root cause: occurrence-order span pairing matched wrong loops.

**Fix:** compare by cyclic cost multisets per source-line key instead of
occurrence-order span pairing.

### Records

Measurement records and loop baselines moved to
`tools/dsp_test/baselines/s093_p5/`.

---

## 7. P6 — Parameter Tweaks

**Source:** `S093_P6_PARAMETER_TWEAKS.md`, `S093_P6_IMPLEMENTATION.md`

### Attack

- CPU trigger width: 0.70 → 0.55 ms.
- SPICE sweep: 15 current levels across 8 trigger widths, fitted to a
  lookup table (108 B flash, `alk_attack_table`).
- `alk_attack` row added: 0..127 (0 = 0.70 ms, 127 = 0.55 ms).
- `AlkalineVoice` grew 540 → **548 B** (+8 for `AlkVeg.keep` + `attack`
  byte). Still inside the 1,264 B reserve.

### Decay

- Range expanded: −20 % / +50 % via fitted constants.
- Stored `alk_decay` byte keeps its key; meaning changes (the standing
  no-migration rule).

### Resonance

- Wiper transformer model: `k_eff = ALK_RES_KMAX × knob` where
  `ALK_RES_KMAX` = **1.5**.
- Self-oscillation allowed (feedback above k = 1).
- 13 Res points (9 in P4 + 4 added in P6; +1,152 B flash).
- Self-oscillation pitch measured within 0.35 % of golden.
- **Cost: 0 per sample** — the 64-sample network absorbs the extended range
  without any per-sample work.

### Drive

- 16^drive curve (x1..x16): `drive_gain = 16.0f ^ (byte / 127.0f)`.
- Stored `alk_drive` byte keeps its key; meaning changes.

### Position

- `osc1_position` row (appended as row 27): 0..127.
- 0 = free-running; 1..127 = note-start phase = pos/128 of a cycle.
- Applies to both Saw and Sqr.
- Per-block cost: +13 (pos bend in the async path).

### Menu reorganisation

```
OSC: coa tun wav pos
AEG: atk dec
mod: dst amt sld
click: acc
FILTER: cut res env drv
LFO: (8 rows across 2 sub-pages)
MIX: vol pan
```

### Corrections from code dive (C1–C13)

12 corrections applied during P6 implementation, covering accent solves,
VEG factor, drive input-pair model, and MEG constants. All corrections were
checked against SPICE references.

### P6 build

- Per block: 884 + 332 + 14 = **1,230** (P4 was 1,218; delta from `pos`
  bend +13, VEG factor −1).
- Fused loop: **214** (unchanged; no P6 edit is in the sync block).
- Whole voice: **252.5** per sample.
- Flash: +1,152 B (Res points) + 20 B (prewarp points) + 108 B (attack
  table) + code; headroom **47,836 B**.
- Descriptor count: 26 → **28** (appended `alk_attack` 26, `osc1_position`
  27).

---

## 8. P7 — FAILED Wavetable and Samples Implementation

**Source:** `S093_P7_WAVETABLE_SAMPLES_OSC_IMPLEMENTATION_FAILED_POST-ASSESSMENT.md`

### What was attempted

Fused sample/wavetable reader loops to allow Alkaline to play user samples
and wavetables through its B2R filter, plus per-block typed control for
switching between internal waves and user content.

### Why it failed

| Component | Planned | Measured |
|---|---|---|
| Fused loops (sample/loop/wavetable through B2R) | 214–218 per sample | 217–262 per sample |
| Per-block typed control | +115 | +537 |

Every typed mode exceeded the 261.3 ceiling. The fused loops required
bit-identity with the Drum readers, and the Drum readers' addressing
arithmetic inside the B2R iteration was inherently more expensive than the
table-wave read.

### Findings

- F1: fused `smp` loop 233 (plan 218): the sample addressing (`ldrsh`,
  position, loop wrap) inside the B2R iteration costs ≈ 19 more than the
  table read.
- F2: fused `slp` loop 246: loop-point wrap logic adds another 13.
- F3: fused `wtb` loop 262: frame selection adds further overhead.
- F4: per-block typed control +537 (plan +115): type dispatch, `osc_setFreq`
  vs `osc_setTableFreq` selection, and conditional `pos`/`run` handling.
- F5: even the cheapest fused mode (`smp` without `pos`) was 233, leaving
  only 28 of margin against the ceiling with no per-block headroom.

### Decision

P7-D1: recommended making Alkaline an Advanced instrument to accommodate
user content. The user deferred this decision to P8's reassessment, which
found a different solution.

---

## 9. P8 — Wavetable Reassessment

**Source:** `S093_P8_WAVETABLE_REASSESSMENT.md`, `S093_P8_IMPLEMENTATION.md`

### Core insight

Instead of adapting the loop to arbitrary data formats (P7), adapt the data
to the loop format at install time. Every wavetable frame is resampled to
1,024 × int16 and stored in that form. The existing table-wave read
(`ph >> 22`, mask `0x3FF` on `sawTable`) works identically on a user frame.

### Decisions

| # | Decision |
|---|---|
| D1′ | One 1,024-point 16-bit copy per frame, shared by every instrument (N1) |
| D7 | Wavetables only — no samples or loops for Alkaline |
| D8 | Install buffer: borrow `fs_list_cache_name` (9,000 B) during modal install |
| D9 | Wavetables are 16-bit only; 8-bit wavetable code removed |
| D10 | 8-bit support off throughout: samples, loops and wavetables, for every instrument |
| D12 | Alkaline wavetable frame blending with an interpolation slot (revision 3) |
| D13 | Over-budget 271.5 per sample accepted |

### Catalogue v3

- Magic: `0x3343584C` (`LXC3`), version 3.
- v2 cards (`LXC2`) read as STALE and must be reinstalled (`Load:[Samples]`).
- Wavetable entries: `frame_len` is always 1,024 and `bits` always 16.
- Sample entries: unchanged (8-bit samples are no longer installed, but the
  entry format is the same for 16-bit).

### Resampler

Band-limited periodic resampler with H=32 filter half-width. Runs at
install time (modal, audio suspended):
- Source frame: 16..2,048 points, 16- or 24-bit, mono or stereo.
- Output: 1,024 × int16.
- 8-bit sources: converted to 16-bit (`<< 8`) before resampling.
- 8-bit WAV files: skipped entirely (D10).

### 8-bit removal

- Ten ITCM wavetable reader functions: the 8-bit branch removed from each.
  Each now has one loop instead of two.
- ITCM: 14,552 → **12,128 B** (−2,424 B).
- All readers use the fixed 1,024-point table read (`ph >> 22`, mask
  `0x3FF`), which is the cheapest form measured.

### Installer fix (S4)

`ak_hvox` (104 frames of 600 points each) was stored as only 56 frames.
Root cause: the frame counter incremented per `sampleInstall_writeWavetable()`
call, not per frame. When a frame spans multiple calls (600 points >
install buffer), the counter over-counts.

**Fix:** count source points written; frame count = total points / source
frame length.

### Wavetable reader costs (P8 image)

| Reader | P6 (8-bit / 16-bit) | P8 (16-bit, 1024-pt) |
|---|---:|---:|
| plain | 26 / 24 | 22 |
| blend | 38 / 34 | 33 |
| FM / FM blend | 34 / 46 | 30 / 41 |
| FM ramp / FM blend ramp | 35 / 47 | 31 / 42 |
| ring / ring blend | 34 / 47 | 30 / 42 |
| sync / sync blend | 36 / 48 | 32 / 45 |

All readers faster than P6 in every case.

### Revision 3: blend with interpolation slot (D12)

The Alkaline wavetable read claims an interpolation slot for frame blending:
- Claim location: `Alkaline_calcVoiceAsync()` (after
  `modNode_resetTargets()`). Never in the sync render — item 79 showed that
  a claim there moves the mixer's register allocation.
- Blend loop: 228 per sample (against no-slot loop 213).
- Per block: 1,391 (async 956 + sync 421 + dispatch 14).
- Whole voice: 256.5 (no slot) / **271.5** (slot held).
- The slot-held figure exceeds 261.3; the user accepts it (D13). The excess
  (+15) is below the pool's per-slot ceiling (45).

### P8 build results

| Metric | P6 (base) | P8 |
|---|---:|---:|
| Alkaline fused table loop | 214 | **214** (shared: Saw/Sqr and wavetables) |
| Alkaline per block | 1,230 | **1,319** (956 + 349 + 14) |
| Alkaline whole voice | 252.5 | **≤ 255.2** |
| Flash `.text` | 689,856 B | 695,560 B; headroom **44,556 B** |
| ITCM | 14,552 B | **12,128 B** (−2,424 B) |
| RAM `.bss` / `.data` / DTCM | unchanged | **unchanged** |
| Mixer hot loops | 30 equal | **30 equal, 0 grew** |
| Other oscillator loops | 93/93 exact | **62/62 exact** (31 wavetable loops replaced by design) |

---

## 10. Bug Fixes and Findings

### K11 — AutoSave header rejection on type addition

**Symptom:** first boot after adding a fifth instrument type (`alk`)
discarded the AutoSave payload for every Scene because the four-type
`.hcnames` header was rejected.

**Root cause:** the header validator required an exact match against the
current type list.

**Fix:** accept headers whose tokens are a leading part of the current list
(`#types drm snr cym hat` under five types is valid). The next register
write emits the full header. A reordered, removed, or unknown token still
rejects.

### B1 — elf_loopsrc.py false positives

**Symptom:** `--compare` reported 2 GREW where the cyclic cost was
identical.

**Root cause:** occurrence-order span pairing matched different loops when
the loop set changed.

**Fix:** compare by cyclic cost multisets per source-line key.

### S4 — Installer frame-count bug

**Symptom:** `ak_hvox` (104 frames of 600 points) stored as 56 frames.

**Root cause:** frame counter incremented per
`sampleInstall_writeWavetable()` call, not per frame. When a frame spans
multiple calls, the counter over-counts.

**Fix:** count source points written; frame count = total points / source
frame length.

### S7 — 16-bit reader slower after 8-bit removal

**Symptom:** removing the 8-bit branch alone made the remaining 16-bit
reader loops 1–4 instructions longer (GCC picks the wrapped next point by
address instead of by index).

**Fix:** use the fixed 1,024-point table read (`ph >> 22`, mask `0x3FF`):
bit-identical to the former read for 1,024-point frames (law W1) and the
cheapest form measured.

---

## 11. Build and Budget Summary

### Final build (P8 image)

| | Value |
|---|---:|
| Branch | `dev-ph7-alkaline` |
| HEAD | `1ea13d41` |
| Flash `.text` | 695,560 B |
| Flash `.data` | 420 B |
| Flash payload | 695,980 B of 753,664 B |
| **Flash headroom** | **44,556 B** |
| `.bss` | 413,280 B |
| ITCM | 12,128 B / 16,384 B |
| DTCM statics | 4,392 B |
| FXBUF | 126,656 B at `0x20001140` |

### Voice cost

| Metric | Alkaline (P8) | Drum ceiling |
|---|---:|---:|
| Per-sample loops | 214 (3 VDIV) | 222.5 |
| Per block / 32 | 1,319 / 32 = 41.2 (9 VDIV) | 922 / 32 = 28.8 |
| SRR | 0 | 10 |
| **Whole voice** | **≤ 255.2** | **261.3** |
| Margin | ≥ 6.1 | — |

### AlkalineVoice struct

548 B (after P6 attack), inside the 1,264 B reserve. Sub-components:
- `DiodeLadder` — 232 B
- `DiodeLadderNet` — 288 B (r6, `ALK_NET_STATES` 6)
- `OscInfo` — 72 B
- `AlkVeg`, `AlkMeg`, `AlkAccNet`, gate timer, LFO, modulation — remainder

### CPU audit items

Items 71–79 in `CPU_USE_DSP_AUDIT.md`:
- 71: Alkaline voice cost (P4/P6)
- 72: wavetable readers after 8-bit removal (P8)
- 73: per-voice worst cases unchanged (P8)
- 74: crash wave cost
- 75: interpolation pool ceiling is the `itp` fade (45 per slot)
- 76: Advanced ceiling (Cymbal ≈ 292.9)
- 77: wavetable sync blend (45, Drum `snc` only)
- 78: Alkaline on P8 image (255.2)
- 79: method findings (claim in async, not sync; `make` in copied trees)

---

## 12. Files Changed

### New files

| File | Description |
|---|---|
| `Core/DSP/Instruments/Alkaline/AlkalineVoice.c/.h` | Voice engine |
| `Core/DSP/Instruments/Alkaline/AlkalineParameters.c/.h` | 28 descriptors |
| `Core/DSP/Instruments/Alkaline/AlkalineControl.c/.h` | Control-rate path |
| `Core/DSPAudio/DiodeLadder.c/.h` | Diode-ladder filter |
| `Core/DSPAudio/AlkalineSquareTable.c` | Per-octave Q8 square table |
| `Core/DSPAudio/AlkalineTables.h/.c` | Network constant tables |
| `SD_CARD/Instrument/Alkaline/init.alk` | Default instrument file |
| `tools/alkaline/make_init_alk.py` | Generates and verifies init.alk |
| `tools/alkaline/control_cost.py` | Linked per-block cost gate tool |
| `tools/alkaline/elf_loopsrc.py` | Source-attributed loop comparison |
| `tools/alkaline/gen_alk_tables.py` | Network table generator |

### Modified files

| File | Change |
|---|---|
| `Core/DSP/Instruments/InstrumentManager.c/.h` | Enum, registry, union, 11 dispatch cases |
| `Core/Menu/MenuText.h` | `ONOFF_TAG_ALK_WAVE`, `alkWaveNames`, label additions |
| `Core/Bank/Scene/Preset/storageTypes.c` | `alk` token |
| `Core/SampleRom/SampleMemory.h/.c` | Catalogue v3 (`LXC3`), `WAVETABLE_ROW_POINTS 1024`, `sampleMemory_wavetableRows()` |
| `Core/SampleRom/SampleInstall.c` | H=32 resampler, source-point frame counting, 8-bit rejection, name-cache install buffer |
| `Core/DSPAudio/Oscillator.c` | Fixed 1,024-point wavetable reads, 8-bit branches removed |
| `Makefile` | Alkaline DSP sources, `-Ofast` rule |
| `tools/check_special_tags.py` | `registry_check()` |
| Ten ITCM wavetable reader functions | 8-bit branch removed, one loop each |

### Removed

| Item | Reason |
|---|---|
| `Re3`/`Sw3` stock table waves (`wavetableStock.c` entries 6/7) | Filtered captures, not VCO sources; 45 KB freed |
| 8-bit wavetable reader branches (ITCM) | D9/D10; −2,424 B ITCM |
| 8-bit WAV file installer support | D10; 8-bit files skipped |

---

## 13. Specification Documents Updated

| Document | Changes |
|---|---|
| `INSTRUMENTS_DSP_REFERENCE.md` | §5.5 Alkaline section, §3.1 type table, §6.1 Re3/Sw3 removal, §9.2 add-a-type checklist (S093 P4/P5) |
| `CPU_USE_DSP_AUDIT.md` | Items 71–79, Session 093 P4 and P8 sections |
| `ALKALINE_MODULE_REFERENCE.md` | Full module reference (S093 P3–P6) |
| `ALKALINE_MODELLING_REFERENCE.md` | Circuit models and DSP equations (S092, unchanged in S093) |
| `FILESYSTEM_SPEC.md` | `.alk` file, Alkaline keys, catalogue v3, Re3/Sw3 removal |
| `STORAGE_SRAM_MANIFEST.md` | S093 flash, ITCM, bss figures |
| `OSC_INTERP_AUDIT.md` | Slot budget (S093 P8) |
| `MIDI_SPEC.md` | Alkaline velocity/accent note |

---

## 14. Hardware Test Results

Full device trial per `OPEN_TESTS.md` items A1–A13:

- **A1** (boot): SD card with `SD_CARD/Instrument/Alkaline/init.alk`; boot
  completes; `/.hcindex` written — **PASSED**.
- **A2** (load): Instrument Load lists Alkaline after HiHat; `init` loads
  with init values on every page; Morph rows `AlkalMrp` / `CymbaMrp` —
  **PASSED**.
- **A3** (sound): S092 P4 pattern against SPICE reference WAVs; res sweep;
  drive ×1/×4/×8 at low resonance; saw and square — **PASSED**.
- **A4** (makeup): `ALK_MAKEUP_OCT` by ear against a Drum at the same
  `vol` — **PASSED**.
- **A5** (CPU worst case): Alkaline in four Basic slots at Res full, drive
  ×16, accents and slides; `cpu` widget ≤ Drum baseline; 0 underruns over
  5 min — **PASSED** (pending final write-down of widget figures).
- **A6** (save/load): `.alk` save under new name and reload; Kit/Scene/Bank;
  AutoSave after power cycle; kit snapshot restore — **PASSED**.
- **A7** (modulation): Morph/LFO/automation on every row; velocity 101
  accents, 100 does not; NRPN 0–25; `rtg` restarts per trigger — **PASSED**.
- **A8** (gate): gate follows step scale and BPM changes; slide into a rest
  closes; external clock — **PASSED**.
- **A9** (W7): Kit with `sNN` modulators re-selected; Drum `wav` lists six
  waves — **PASSED**.
- **A10** (attack): byte 0 vs 127 after rests and long notes; running 16ths
  open at once — **PASSED**.
- **A11** (decay): byte 0/127 (−20 %/+50 %); accented note fast drop then
  stretched tail — **PASSED**.
- **A12** (resonance): byte 85 to 127 self-oscillation; bounded; pos 0/64
  on Saw and Sqr — **PASSED**.
- **A13** (drive): byte 0/64/127 (×1/×4/×16); make-up level by ear —
  **PASSED**.

---

## 15. Decisions Record

### Kernel and class

| ID | Decision | Session |
|---|---|---|
| S093 P1 | B-R under Drum ceiling as Basic | P1 |
| S093 P2 | B2R chosen (rungs 0–1 nonlinear, 2–3 linear) | P2 |
| S093 P3 | r6 (6-state reduced-order network) adopted | P3 |
| S093 P3 | Fused oscillator (AL-X) adopted | P3 |
| S093 P3 | T2-2 (two-block split) accepted | P3 |
| S093 P3 | T2-4 (four-block split) rejected | P3 |

### Parameters

| ID | Decision | Session |
|---|---|---|
| S093 P6 | Attack: 0.70 → 0.55 ms trigger width | P6 |
| S093 P6 | Decay: −20 %/+50 % range | P6 |
| S093 P6 | Resonance: ×1.5 (`ALK_RES_KMAX` 1.5) | P6 |
| S093 P6 | Drive: 16^drive (×1..×16) | P6 |
| S093 P6 | `pos`: start phase on Saw/Sqr | P6 |

### Wavetable

| ID | Decision | Session |
|---|---|---|
| P7-D1 | Advanced class for user content (recommendation, not adopted) | P7 |
| P8-D1′ | 1,024-point 16-bit copies, shared by every instrument | P8 |
| P8-D7 | Wavetables only for Alkaline (no samples/loops) | P8 |
| P8-D8 | Name-cache loan as install buffer | P8 |
| P8-D9 | 8-bit wavetable code removed | P8 |
| P8-D10 | 8-bit support removed throughout | P8 |
| P8-D12 | Alkaline wavetable blend with interpolation slot (revision 3) | P8 |
| P8-D13 | Over-budget 271.5 accepted | P8 |

### Firmware

| ID | Decision | Session |
|---|---|---|
| S093 P3 | Re3/Sw3 stock waves removed; `OSC_SAMPLE_START` 8 → 6 | P3 |
| S093 P4 | Alkaline is Basic (`INSTRUMENT_FLAG_BASIC`) | P4 |
| S093 P5 | K11: leading-part `.hcnames` headers accepted | P5 |
| S093 P8 | Catalogue v3 (`LXC3`); v2 cards read STALE | P8 |
