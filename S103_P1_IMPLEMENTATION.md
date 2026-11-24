# S103 P1 — Katran: code implementation schedule

**Date:** 2026-11-24. **Branch:** `dev-ph7-alkaline`. **Base:** the S102 P1
Fix 1 tree (uncommitted on top of `ef3d3802`). **Status:** schedule for
review. **No code has been changed.**

**Plan:** `S103_KATRAN_NOISE_SYNTH.md` revision 3 (decided). §n below refers
to this document; **P§n** refers to the plan; **D§n** refers to
`knowledge_files/drafts/DRAFT_KATRAN_DRONE_SYNTH.md` revision 5.

**Your instruction (2026-11-24):** "Do the code deep-dive and write the full,
detailed code implementation schedule … Make sure CPU is within 5-10% under
budget, cut features where they make sense or stop and ask if the initial cpu
use projection was wrong to above that bound. Include every place the code
must change … cited by file, line, and add/remove/modify. Alongside each code
change, include a detailed comment block …"

**Labels:**
- **Code:** read from the base tree, with a file and line.
- **Measured:** from a device trace or a linked-image record, with its source.
- **Est.:** an estimate, with its basis. The gates in §10 measure each one
  before the next step relies on it.

**Line numbers** are those of the base tree. Each change also quotes an
**anchor** (the existing text it sits beside), because earlier edits in the
same file move later lines. Within one file, apply the changes from the
bottom up, or relocate each one by its anchor.

---

## 0. Summary

| Item | Result |
|---|---|
| **CPU: the plan's projection was low.** | Re-priced on the Colloid's measured instructions-to-cycles ratio (§1.1), the revision-3 design is ≈ 390 instructions a sample ≈ **18.5–19.1k** cycles a block: 2.7–5.7 % under Basic, **outside** the 5–10 % band. The plan said ≈ 349 ≈ 17.0k |
| **Cuts made** (§1.3) | 1. F3 (the output filter) runs one coefficient set for both sides, and `spr` no longer spreads it. 2. TORCIDO's tube stage is folded into the cubic as one multiply-add. 3. The delay rings are 2,048 samples (mask wrap). 4. One `exp2` serves OSC A's two copies, and one serves both delay times. **No row, page or feature is removed.** |
| **CPU after the cuts** | ≈ **376** instructions a sample ≈ **17.9–18.4k** cycles a block = **6.2–8.9 % under** the 19,620 Basic share (centre ≈ 18.1k, 7.7 %). Gates: linked ≤ 380, device median ≤ 18.4k (§1.5) |
| **If the linked count is over 380** | Ladder step L1 (F3 becomes a one-pole LP/HP tilt, −30, `cut` keeps its meaning) is pre-approved by this schedule. Past 395 even after L1, **implementation stops and comes back to you** (§1.5) |
| **New files** | 8 source files in `Core/DSP/Instruments/Katran/`, 2 generators in `tools/katran/`, 1 host module, 1 CPU script, 1 card file (§4, §8, §9) |
| **Changed files** | `InstrumentManager.h/.c` (27 changes), `FxBuffer.h/.c`, `menu.h`, `MenuLabels.c`, `MenuText.h`, `menu.c`, `storageTypes.h`, `Makefile`, `tools/dsp_test/Makefile`, `check_special_tags.py`, `decode_devlogs.py` (§5–§8) |
| **RAM** | `KatranVoice` ≈ 1,052 B est. (≈ 1,104 B calibrated), inside the 1,264 B slot reserve; gate ≤ 1,200 B. **New .bss: +2 B** (`menu_instrumentLoadIndex[]` grows with the type count, `menu.c:1805`): **needs your sign-off** (§2.4) |
| **Voice units** | Two per Katran (P§7.2), from the existing pool; ring 2,048 of each unit's 2,208 samples |
| **Contract change** | `FxBuffer.h`: a new read-only accessor `fxbuf_arenaView()`. An Instrument may now read arena bytes it does not own (the arena read form, P§3.4). **Needs your sign-off** (§5.2) |
| **Flash / ITCM** | Flash ≈ +15 KB est. against ≈ 79 KB headroom (753,664 − 674,520, S102 P1 §0). ITCM **0 B** |
| **Out of scope** | The insert effect and the Basic/Advanced file merge (P§9): only the reservations are built. Every `knowledge_files/` and `MEMORY.md` update (listed in §13 for you) |

---

## 1. CPU

### 1.1 The calibration (measured)

The plan priced Katran with a planning CPI of 1.52 and a per-block share
taken from the draft (42–47 instructions a sample). The Colloid is the
closest measured relative, and it shows the per-block share is larger:

| Colloid mode | Loops | Per block ÷ 32 | Linked total | Device median | Cycles ÷ (total × 32) |
|---|---:|---:|---:|---:|---:|
| `wav` (plain) | 272.2 | 102.2 | 374.4 | 18,362 | **1.53** |
| `pwm` Saw/Rec | 323.2 | 79.5 | 402.8 | 19,136 | **1.485** |

Sources: `S101_P2_REDUCTIONS.md` §A.5 (the linked image with
`colloid_cpu.py`, beside the S096 P3 device probe). The Tetrakt as built is
18.6k at ≈ 1.52 (`S101_P2_REDUCTIONS.md` A2).

**What changes:** the CPI holds (1.485–1.53). The per-block work for a
three-oscillator, four-filter, two-delay voice is ≈ 1,800 instructions
(§1.4), not the ≈ 1,500 the plan used. The loop estimates also rise once the
cycle-start row switch (K-D10) and the sync tests are priced per sample.

### 1.2 The revision-3 design, re-priced (est.)

| Part | Plan | Re-priced | Why it moved |
|---|---:|---:|---|
| Loops | 302 | ≈ 330 | the cycle-start row switch (+5), the sync tests priced in (+10), the D/A/B block stores (+8), the delay wrap (+5) |
| Per block | 47 | ≈ 60 | measured Colloid per-block costs (`col_svfControl` 99, osc control ≈ 134 per oscillator, the async entry 386) |
| **Total** | **349** | **≈ 390** | |
| At 1.485–1.53 | 17.0k | **18.5–19.1k** | **2.7–5.7 % under**: outside the 5–10 % band |

### 1.3 The cuts (no feature removed)

| # | Cut | Saves (est.) | What changes for you |
|---|---|---:|---|
| C1 | **F3 runs one coefficient set for both sides** (one `col_svfControl`-style call a block instead of two, and one coefficient ramp in the loop). `spr` no longer spreads F3's cutoff; it still spreads the delay times and F1 | −3 loop, −3 per block | The output filter is the same on L and R. The spread still comes from the loops and F1, which carry most of it |
| C2 | **TORCIDO's tube stage folded into the cubic.** `y = x' − (4/27)x'³ + c₂·x'²`: x'² is already computed for the cube, so the tube is one FMA. F2 removes the DC it adds, so the draft's `− s_dc` term goes | −4 loop | None: the same even-order term |
| C3 | **Delay rings of 2,048 samples** (46.4 ms) in each 2,208-sample unit: the wrap is one AND | −4 loop | `tim` tops out at ≈ 40.6 ms (1,792 samples) instead of ≈ 44 ms; the lowest loop pitch is 24.6 Hz instead of 22.7 Hz |
| C4 | **Shared `exp2`:** OSC A's L and R copies take one `alk_exp2f()` and a detune ratio; both delay times take one `alk_exp2f()` and the spread ratio | −1 per block | None |
| | **Total** | **≈ −14** | |

### 1.4 The composition after the cuts (est., instructions a sample, dearest settings)

| Part | Per sample | Basis |
|---|---:|---|
| L1 two delay reads: the timed fractional tap + the 32-sample tap, the per-sample time ramp, the D blocks | 30 | Tetrakt `tkt_spaceOut()` (64 a frame for two interpolated taps + LP + write, `S101_P2_REDUCTIONS.md` B3) |
| L2 OSC B, dearest form (PWM two-read with glide), the wrap bit, the cycle-start row switch | 31 | Colloid `col_pwmPairStore` 66 a pair |
| L3 OSC A pair, dearest form, the two sync tests and resets, the row switch | 74 | the same + resets |
| Noise (one RNG read, split L/R) | 4 | Colloid `col_rngRead()` |
| L4 per core ×2: TORCIDO ch1 10, ch2 9, F1 TPT SVF with ramps 22, F2 3, the write 10, ring 4, VCA and store 5 | 126 | Colloid `f2_one` 20–26, the S095 spike |
| L5 F3 ×2 on one coefficient ramp, width, CE1 gain, stores | 55 | Colloid `col_renderPar` ≈ 125 for a fuller chain |
| **Loops** | **320** | |
| Per block ≈ 1,800 ÷ 32 (itemised below) | **56** | |
| **Total** | **≈ 376** | |
| **Device, at 1.485–1.53** | **17.9–18.4k** | **6.2–8.9 % under 19,620** |

**Per-block work (est., instructions a block):**

| Item | Est. |
|---|---:|
| `Katran_calcVoiceAsync()` entry, the row-byte conversions (≈ 36 rows) | 200 |
| Transport edges, run gain, kick, pending-apply fade | 60 |
| Patch LFO (rate `exp2`, sine read, skew, jitter) | 70 |
| CE1, CE2 (a stage step each, one RNG draw per cycle) | 70 |
| Oscillator control: 2 `exp2`, 3 increments, octave rows, PWM width targets and slews, next rows (frame / arena window) | 320 |
| Wavetable bind check; 3 arena window starts | 65 |
| Delay control ×2 (time law, spread, track, drift walk, slew, per-sample step; one shared `exp2`) | 135 |
| F1 control ×2 (`col_svfControl` form, 99 each) + loop-pitch tracking | 230 |
| F2 coefficient; F3 control ×1 | 110 |
| TORCIDO coefficients; VCA, ring, width and CE1 gain ramps | 100 |
| Arena guard peaks (4 samples × 3 blocks) | 40 |
| Sync entry + 7 loop calls | 270 |
| **Total** | **≈ 1,670–1,800** |

### 1.5 Gates and the ladder

| Gate | Where | Limit |
|---|---|---|
| G-CPU1 | `tools/dsp_test/katran_cpu.py` on the linked image: loops (`elf_looppath.py --cond-heads --in-range`) + per-block (`elf_blockpath.py`, `control_cost.py --elf`) ÷ 32 | **≤ 380** all-in (≈ 18.5k at 1.52) |
| G-CPU2 | the arena form vs the dearest form, per loop | arena ≤ dearest (so the arena form is never the charged case) |
| G-CPU3 | `elf_hotloops.py` + the inlined-set diff of `mixer_calcNextSampleBlock()` against `tools/dsp_test/baselines/s103/` | **0 loops grew** |
| G-CPU4 | device, `DEV_MODE_LOGGING` build, the `p` probe, one Katran at its dearest settings with a stereo send | median **≤ 18.4k**; never above 18.64k (5 %) |

**The ladder (applied in order only if G-CPU1 fails):**

| Step | Change | Saves | Who decides |
|---|---|---:|---|
| **L1** | F3 becomes a one-pole LP/HP tilt per side (`cut` keeps its bipolar meaning; it loses the fixed moderate resonance) | ≈ −30 | **pre-approved by this schedule** |
| L2 | anything further (noise exciter, the 32-sample tap, single core) | — | **stop and ask you** |

If G-CPU1 comes in under ≈ 345 (more than 10 % under), the sub-oscillator
(D§3.2 A5) is the first add-back, in a free row (57–59). That is also **your
call** at that point.

---

## 2. Memory

### 2.1 `KatranVoice` (inside the 1,264 B slot reserve, `InstrumentManager.c:291`)

| Member group | Bytes (est.) |
|---|---:|
| `OscInfo osc` (the platform's `osc1_*` rows; `Oscillator.h:396` asserts 72) | 72 |
| `Lfo lfo` (the platform LFO) | 124 |
| `right[32]` (the mixer's right block) | 64 |
| Scratch blocks: `blkD[2][32]`, `blkA[32]`, `blkB[32]` (the wrap bit rides in `blkB`'s LSB, §4.3) | 256 |
| Ring pointers ×2, the arena base and length | 16 |
| Oscillator state: 4 phases, 4 increments, 6 row pointers (now / next), PWM widths 3 × (D, D0, pos, pos0) | 66 |
| Arena conditioning ×3 (DC x/y, damp, guard) + damp coefficient | 52 |
| Delay state ×2 (time now / step, drift now / target) | 32 |
| F1 ×2 (`KatSvf`, 13 floats each) | 104 |
| F2 ×2 + coefficient | 20 |
| F3: one `KatSvf` (coefficients and the L states) + the R states | 60 |
| TORCIDO coefficients (gain ch1, gain ch2, tube, wet) | 16 |
| Run gain now / step, kick level, feedback now / step, cross-feedback | 24 |
| Ring, VCA ×2, CE1 gain, width (now / step) | 36 |
| CE1, CE2 (`KatCe`, 20 B each) + the xorshift state | 44 |
| Patch LFO (phase, cv, ringPrev) | 12 |
| `vol` | 4 |
| Row bytes (36 own rows + `pan`) and 4 reserved effect bytes | 41 |
| Control bytes (slot, note, wasRunning, armed, fadeReq, silentBlocks, flags) | 9 |
| **Total** | **≈ 1,052** |
| Calibrated (+4.9 %, the Colloid's draft-to-link error, D§10.1) | **≈ 1,104** |

- **Gate G-RAM1:** `_Static_assert(sizeof(KatranVoice) <= 1200u)` in
  `InstrumentManager.c` (change IM-5), beside the slot-reserve assert.
- **In-slot reserves**, if the link is over: `blkB` into `right[32]`
  (−64 B); the floats in the "now / step" pairs that only change per block
  become steps recomputed in the sync (−20 to −40 B).

### 2.2 Voice units

- Two per Katran: `fxbuf_voiceUnit(slot, 0)` is core L's ring,
  `fxbuf_voiceUnit(slot, 1)` core R's. Each ring uses samples 0..2,047 of
  its unit (`KAT_RING` 2,048); samples 2,048..2,207 stay unused.
- The pool is 12 units with at most 2 per slot (`FxBuffer.h`
  `FXBUF_VOICE_UNIT_COUNT`, `FXBUF_VOICE_UNITS_PER_SLOT`), so six Katrans fit
  exactly. `instrumentManager_slotVoiceUnits()` releases before it claims, so
  a claim of 2 cannot fail while every slot holds at most 2. The voice still
  handles a NULL ring (§4.2, `Katran_initVoice()`).
- The Effect share shrinks by 8,832 B per Katran, as P§7.2.

### 2.3 Flash and ITCM

| Item | Est. |
|---|---:|
| 8 oscillator loops (`KatranOsc.c`) | 3.5 KB |
| The core and output loops, the voice's control (`KatranVoice.c`) | 6.0 KB |
| Tables (`KatranTables.c`: 7 × 128 floats) | 3.6 KB |
| Descriptors and pages (`KatranParameters.c`), labels, name lists | 2.2 KB |
| InstrumentManager / menu / FxBuffer branches | 0.6 KB |
| **Total** | **≈ 15.9 KB** |

Headroom ≈ 79 KB. ITCM: none (every Katran object is flash, `-fno-lto`, the
Tetrakt's rule, `Makefile:394`).

### 2.4 RAM sign-off needed

| Item | Bytes | Region | Owner |
|---|---:|---|---|
| `menu_instrumentLoadIndex[INSTRUMENT_TYPE_UNKNOWN]` (`menu.c:1805`) grows by one `uint16_t` with the type count | **+2** | SRAM1 `.bss` | `menu.c` |
| `KatranVoice` | 0 new | inside `runtime_slots[]` (7,584 B, unchanged) | `InstrumentManager.c` |
| Voice units | 0 new | DTCM arena, existing units | FxBuffer |

The same +2 B was signed off for every earlier type (`InstrumentManager.h:74`,
`:97`, `:109`, `:124`, `:137` comments).

---

## 3. Change index

| ID | File | Line (base) | Action | What |
|---|---|---|---|---|
| **N-1** | `Core/DSP/Instruments/Katran/KatranVoice.h` | new | add | constants, `KatSvf`, `KatCe`, `KatranVoice`, entry points |
| **N-2** | `Core/DSP/Instruments/Katran/KatranVoice.c` | new | add | init, trigger, async, sync, quiet, fade, setOscType |
| **N-3** | `Core/DSP/Instruments/Katran/KatranOsc.h` | new | add | the 8 oscillator loop declarations |
| **N-4** | `Core/DSP/Instruments/Katran/KatranOsc.c` | new | add | the 8 oscillator loops |
| **N-5** | `Core/DSP/Instruments/Katran/KatranTables.h` | new | add | table declarations |
| **N-6** | `Core/DSP/Instruments/Katran/KatranTables.c` | new | add (generated) | 7 tables |
| **N-7** | `Core/DSP/Instruments/Katran/KatranParameters.h` | new | add | counts, flags, indices |
| **N-8** | `Core/DSP/Instruments/Katran/KatranParameters.c` | new | add | 57 descriptors, 8 pages, labels |
| H-1 | `Core/DSP/Instruments/InstrumentManager.h` | 141–142 | add | `INSTRUMENT_TYPE_KAT` |
| IM-1 | `Core/DSP/Instruments/InstrumentManager.c` | 8, 20 | add | includes |
| IM-2 | same | 133–134 | add | registry row |
| IM-3 | same | 166 | add | engine map row |
| IM-4 | same | 284–302 | modify, add | reserve comment; union member |
| IM-5 | same | 348 | add | size asserts |
| IM-6 | same | 532 | add | `instrumentManager_katranRuntime()` |
| IM-7 | same | 536–556 | modify, add | `instrumentManager_unitsWanted()`: 2 for KAT |
| IM-8 | same | 586–603 | modify | `instrumentManager_slotVoiceUnits()` comment (0..2) |
| IM-9 | same | 631 | add | `instrumentManager_slotRightUpdate()` case |
| IM-10 | same | 1278 | add | `instrumentManager_waveDomainMax()` Katran wav list |
| IM-11 | same | 1940 | add | `instrumentManager_runtimeInstance()` case |
| IM-12 | same | 1970 | add | `instrumentManager_runtimeLfo()` case |
| IM-13 | same | 2082 | add | `instrumentManager_runtimeInit()` case |
| IM-14 | same | 2128 | add | `instrumentManager_ampEnvelopeQuiet()` branch + fade request |
| IM-15 | same | 2393–2399 | modify | `instrumentManager_resetRuntimeSlotFromScene()` unit bind |
| IM-16 | same | 2593 | add | `instrumentManager_runtimePan()` case |
| IM-17 | same | 2633 | add | `instrumentManager_runtimeVolume()` case |
| IM-18 | same | 2915 | add | `instrumentManager_calcSlotAsync()` case |
| IM-19 | same | 3021 | add | `instrumentManager_calcSlotSyncBlock()` case |
| IM-20 | same | 3080 | add | `instrumentManager_cellInactive()` branch |
| IM-21 | same | 3253 | add | `instrumentManager_triggerTrack()` case |
| IM-22 | same | 3279 | add | `instrumentManager_requestGateDuck()`: no duck |
| IM-23 | same | 3504 | add | `instrumentManager_osc()` Katran block |
| IM-24 | same | 3562 | add | `instrumentManager_oscBySelector()` case |
| IM-25 | same | 5252–5258 | modify | `IM_SPECIAL_OSC_TYPE`: `Katran_setOscType()` |
| FB-1 | `Core/DSP/Effects/FxBuffer.h` | 44–75, 265 | modify, add | contract text; `fxbuf_arenaView()` declaration |
| FB-2 | `Core/DSP/Effects/FxBuffer.c` | 270 | add | `fxbuf_arenaView()` |
| ME-1 | `Core/Menu/menu.h` | 287, 364 | add | 15 long, 21 short label ids |
| ME-2 | `Core/Menu/MenuLabels.c` | 81, 185 | add | the label strings |
| ME-3 | `Core/Menu/MenuText.h` | 296–302, 423 | modify, add | free-tag comment; two tags; two name lists |
| ME-4 | `Core/Menu/menu.c` | 70 | add | include |
| ME-5 | same | 12608–12622 | modify | `menu_formatWaveCell()`: Katran's wav list |
| ME-6 | same | 12949 | add | `menu_onoffTagNames()` cases |
| ME-7 | same | 13132 | add | name-count asserts |
| ST-1 | `Core/Hardware/SD/storageTypes.h` | 146 | add | `STORAGE_INSTRUMENT_KAT` |
| MK-1 | `Makefile` | 42, 123, 170, 403 | add | include path, SRCS, DSP_SRCS, `-Ofast -fno-lto` rules |
| T-1 | `tools/dsp_test/check_special_tags.py` | 40 | add | `TABLES['KAT']` |
| T-2 | `tools/katran/gen_kat_tables.py` | new | add | the table generator |
| T-3 | `tools/katran/make_init_kat.py` | new | add | `init.kat` writer and check |
| T-4 | `tools/dsp_test/modules/katran.c` | new | add | host laws |
| T-5 | `tools/dsp_test/katran_cpu.py` | new | add | linked counts and G-CPU1 |
| T-6 | `tools/dsp_test/Makefile` | 64, 69, 530, after 1110 | add | targets |
| T-7 | `tools/diagnostic_tools/decode_devlogs.py` | 1125–1126 | modify | type 9 = `kat` |
| C-1 | `SD_CARD/Instrument/Katran/init.kat` | new | add (generated) | the first Katran |
| B-1 | `tools/dsp_test/baselines/s103/` | new | add | baseline and records |

**Not changed (checked):**
- `mixer.c`: the stereo path reads `instrumentManager_slotRight()`
  (`mixer.c:1580`), which IM-9 feeds.
- `filesystem.c`: the type folder is created from the registry row
  (`filesystem.c:4876`, `afatfs_mkdir_lfn(entry->storage_directory, …)`), and
  the `.hcnames` header takes its tokens from the registry
  (`filesystem_validateHcnamesHeader()`, `:6713`).
- `presetManager.c`: the deferred applies call
  `instrumentManager_ampEnvelopeQuiet()` (`:2218`, `:2380`). IM-14 carries
  the fade request, so no new hook is needed. The force bound is 1,000 passes
  (`DRUMSET_APPLY_FORCE_TICKS`, `:264`), far longer than the ≈ 20 ms fade.
- `MidiNoteModes.c`: a Katran gets `MNM_NORMAL` (`midiNoteMode_trackMode()`,
  `:107–121`); note-on is a kick through `instrumentManager_triggerTrack()`.
- `instrumentManager_classifySpecialKey()` (`InstrumentManager.c:5356`) and
  `instrumentManager_resetSlot()` (`:1090`) are generic over the registry.
- Every other slot-type `switch` in `InstrumentManager.c` defaults to "none"
  for an unlisted type (filter, transient, decay, distortion, space, run
  mode, chain); Katran has none of those, so it needs no case there.

---

## 4. New files

### N-1 `Core/DSP/Instruments/Katran/KatranVoice.h`

**Action:** add. **Template:** `ColloidVoice.h` (struct conventions),
`TetraktVoice.h` (unit pointer, noinline entry points).

**File header comment block:**

```c
/*
 * KatranVoice.h - the Katran drone / noise voice (S103 P1).
 *
 * What:       the eighth instrument type: a dual feedback core after the
 *             TORCIDO noise-synth patch (D§1). Per core: an exciter (OSC A,
 *             or an arena read) -> TORCIDO (two shaped channels) -> the loop
 *             filter F1 (TPT SVF) -> the low-cut F2 -> a 2,048-sample delay
 *             ring in one FxBuffer voice unit, whose output re-clocks the
 *             exciter (hard sync on a threshold crossing). OSC B rings each
 *             core's TORCIDO channel 1; the VCA pair gives the patch's
 *             asymmetric outputs (L = ring + AMP2(loop), R = ring + AMP1(ring)).
 *             Then F3 (one coefficient set for L and R), the width, and the
 *             chaos envelope CE1 on the level. Stereo: buf receives L, the
 *             voice's right[] R (instrumentManager_slotRight()).
 * Why:        S103_KATRAN_NOISE_SYNTH.md revision 3; S103_P1_IMPLEMENTATION.md.
 *             The graph runs every block whatever the note state (constant
 *             CPU); triggers do not restart anything, they kick the loop.
 * Behaviour:  run (127 on; 0..126 a decay of 0.25..60 s), the kick (re-arm,
 *             burst, swell), the transport stop decay, boot silence until the
 *             first kick or start, and the pending-apply fade (P§3.1-§3.2).
 * RAM:        sizeof(KatranVoice) <= 1,200 B (InstrumentManager.c asserts it;
 *             the slot reserve is 1,264 B). No other RAM.
 * Affiliates: KatranOsc.h (the oscillator loops), KatranTables.h,
 *             KatranParameters.c (the rows bound into this struct),
 *             InstrumentManager.c (dispatch, units), FxBuffer.h
 *             (fxbuf_arenaView()).
 */
```

**Constants** (each with a one-line comment in the file):

| Name | Value | Meaning |
|---|---|---|
| `KAT_RING` | 2048u | samples per core ring (power of two: mask wrap, C3) |
| `KAT_RING_MASK` | `KAT_RING - 1u` | |
| `KAT_TAP_DRY` | 32u | the dry tap, one block back (K-D5) |
| `KAT_TIME_MIN` / `KAT_TIME_MAX` | 32.0f / 1792.0f | the `tim` law's ends in samples (1,792 × 1.12 + drift < 2,047) |
| `KAT_SPREAD_TIME` | 0.12f | `spr` > 63: L × (1 + 0.12 s), R × (1 − 0.12 s) |
| `KAT_SPREAD_F1_ST` | 12.0f | `spr` > 63: F1 ± 12 semitones |
| `KAT_XFB_MAX` | 0.6f | `spr` 0: cross-feedback at its maximum |
| `KAT_FB_CLAMP` | 0.95f / 1.1f | feedback alone / feedback + cross-feedback + swell |
| `KAT_ARENA_WIN` | 1024u | the arena window (one wavetable frame) |
| `KAT_SYNC_P` | 0.5f | the weight of OSC B's wrap in the sync sum |
| `KAT_SRC_OFF/A/B/AB`, `KAT_SRC_COUNT` | 0..3, 4 | `src` |
| `KAT_LT_LP/BP/HP/LPT/BPT/HPT`, `KAT_LT_COUNT` | 0..5, 6 | F1 type (absolute, tracked) |
| `KAT_FORM_PLAIN/PWMD/WARP/ARENA` | 0..3 | the loop forms |
| `KAT_FADE_HOLD` | 64u | blocks a pending-apply fade request lasts without renewal |
| `KAT_QUIET_BLOCKS` | 72u | blocks of `gr == 0` before the voice reports quiet (one ring length + margin) |
| `KAT_KICK_REARM` | 7u | blocks for the 5 ms re-arm ramp |
| `KAT_OUT_NORM` | by ear (host model) | output normalisation |

**Types:**

```c
/*
 * One TPT state-variable filter, Katran form (S103 P1).
 *
 * What:       the Colloid's ColSvf (ColloidVoice.h) without the targets t1..t3:
 *             the next block's start is a + 32 da, computed at the start of
 *             the next control. ic1, ic2 the states; a1..a3 the coefficients
 *             at the block's start; da1..da3 the per-sample steps; mH mB mL
 *             the output mix; cutSt the slewed cutoff in semitones above
 *             20 Hz; k the slewed damping.
 * Why:        three floats fewer per filter (12 B x 3 filters) for the slot
 *             budget; the arithmetic is the Colloid's.
 */
typedef struct { float ic1, ic2, a1, a2, a3, da1, da2, da3, mH, mB, mL, cutSt, k; } KatSvf;

/*
 * One chaos envelope (S103 P1, P§3.3).
 *
 * What:       level 0..peak; up, down: the per-block steps of the current
 *             cycle's rise and fall (linear); peak: this cycle's top (1 minus
 *             the chaos draw); stage 0 rise, 1 fall.
 * Why:        a free-running cycling function generator; a new cycle draws
 *             new times and a new peak (chs), so the progression never
 *             repeats exactly. Never reset by a trigger (the brief).
 */
typedef struct { float level, up, down, peak; uint8_t stage; } KatCe;
```

**The voice struct** (fields grouped as §2.1; each field carries a trailing
comment naming its row or role):

```c
typedef struct KatranVoiceStruct
{
	OscInfo  osc;                 /* OSC A pitch anchor; typ / wav / pos (platform rows) */
	Lfo      lfo;                 /* the platform LFO (as every engine) */
	int16_t  right[OUTPUT_DMA_SIZE];          /* R output (the mixer reads it) */
	int16_t  blkD[2][OUTPUT_DMA_SIZE];        /* each core's delay output, this block */
	int16_t  blkA[OUTPUT_DMA_SIZE];           /* OSC A of the core being rendered */
	int16_t  blkB[OUTPUT_DMA_SIZE];           /* OSC B; LSB = 1 on its wrap sample */
	int16_t *ring[2];             /* the two voice units (NULL: silent) */
	const int16_t *arena;         /* fxbuf_arenaView() base */
	uint32_t arenaN;              /* arena length in samples */
	uint32_t phA[2], incA[2];     /* OSC A L, R */
	uint32_t phB, incB;           /* OSC B */
	const int16_t *rowA[2], *rowANext[2];     /* the row played; the row from the next cycle start */
	const int16_t *rowB, *rowBNext;
	uint16_t pwD[3], pwD0[3];     /* PWM D form: width at block end / start (A L, A R, B) */
	uint8_t  pwPos[3], pwPos0[3]; /* PWM width index, slewed (warp form) */
	float    dcX[3], dcY[3], damp[3], guard[3], dampK;   /* arena conditioning */
	float    tNow[2], tStep[2], drift[2], driftTgt[2];   /* delay times (samples) */
	KatSvf   f1[2];               /* loop filters */
	float    f2x[2], f2y[2], f2K; /* loop low-cuts */
	KatSvf   f3;                  /* output filter: shared coefficients, L states */
	float    f3R1, f3R2;          /* output filter R states */
	float    tGain, tGain2, tTube, tWet;   /* TORCIDO, this block */
	float    gr, grStep;          /* run gain, now / per-sample step */
	float    kE;                  /* kick level */
	float    fb, fbStep, xfb;     /* feedback now / step; cross-feedback */
	float    rk, rkStep, g1, g1Step, g2, g2Step, ceg, cegStep, wid;
	KatCe    ce[2];
	uint32_t rng;                 /* xorshift for CE and drift draws */
	float    lfoPh, cv, ringPrev; /* patch LFO */
	float    vol;                 /* instrument_vol (the mixer reads it) */
	/* --- row bytes (KatranParameters.c binds them; converted in the async) --- */
	uint8_t  spreadA, source, damp_, run, kick, ce1Cycle, ce1Skew, ce1Depth,
	         ce2Cycle, ce2Skew, ce2Depth, chaos, plfoRate, plfoMod, plfoLevel,
	         vca, bCoarse, bFine, bPos, bLevel, levelA, noise, sync, drive,
	         wet, time, feedback, drift_, track, loopCut, loopRes, loopType,
	         lowCut, outCut, spread, width, pan;
	uint8_t  fxReserved[4];       /* the later insert effect's rows (P§9); not bound */
	/* --- control --- */
	uint8_t  slot, note, wasRunning, armed, fadeReq, stopDecay;
	uint16_t silentBlocks;
} KatranVoice;
```

**Entry points:**

```c
/*
 * Entry points (S103 P1).
 *
 * Katran_initVoice()         the init.kat values; slot; the two voice units
 *                            (either may be NULL: the voice then renders
 *                            silence); clears both rings; reads the arena view.
 * Katran_triggerVoice()      the kick: platform LFO retrigger and velocity
 *                            node, then K1 re-arm, K2 burst, the note.
 * Katran_calcVoiceAsync()    the block's control (render context).
 * Katran_calcVoiceSyncBlock() the block's audio: L into buf, R into right[].
 * Katran_isQuiet()           gr has been 0 for KAT_QUIET_BLOCKS blocks.
 * Katran_requestFade()       a deferred Scene/Kit apply is waiting: fade gr
 *                            to 0 over ~20 ms and hold it there while renewed.
 * Katran_setOscType()        the typ writer (wav / wtb / pwm).
 * The two render entries are noinline so LTO keeps them out of
 * mixer_calcNextSampleBlock() (S086 F1; S101 P3 §4).
 */
void Katran_initVoice(KatranVoice *voice, uint8_t slot, int16_t *ringL, int16_t *ringR)
	__attribute__((noinline, noclone));
void Katran_triggerVoice(KatranVoice *voice, uint8_t source_slot, uint8_t velocity, uint8_t note);
void Katran_calcVoiceAsync(KatranVoice *voice) __attribute__((noinline));
void Katran_calcVoiceSyncBlock(KatranVoice *voice, int16_t *buf, uint8_t size)
	__attribute__((noinline));
uint8_t Katran_isQuiet(const KatranVoice *voice);
void Katran_requestFade(KatranVoice *voice);
void Katran_setOscType(KatranVoice *voice, uint8_t typ);
```

---

### N-2 `Core/DSP/Instruments/Katran/KatranVoice.c`

**Action:** add. **Includes:** `<math.h>`, `<string.h>`, `KatranVoice.h`,
`KatranOsc.h`, `KatranTables.h`, `ColloidTables.h` (`colEgK[]` is not used;
only the shared `COL_CUT_ST_MAX`-style constants are restated locally),
`AlkalineControl.h` (`alk_exp2f()`, `alk_log2f()`), `wavetable.h`,
`oscPwmTables.h`, `MidiPitch.h` (`midiPitch_detune()`,
`MIDI_DEFAULT_TRIGGER_NOTE`), `modulationNode.h`, `InstrumentManager.h`,
`sequencer.h` (`seq_isRunning()`, `sequencer.h:554`), `random.h`
(`GetRngValue()`, `RNG_DR_ADDR` at `random.h:36`), `FxBuffer.h`
(`fxbuf_arenaView()`), `SampleMemory.h` (the wavetable bind).

**Functions, in file order.** Each gets the comment block shown; the
arithmetic is specified in full so the host laws (§9) can be written first.

#### `kat_rngNext()` / `kat_rngRead()` (static inline)

```c
/*
 * The voice's xorshift and the per-sample RNG read (S103 P1).
 *
 * What:       kat_rngNext() advances the per-voice xorshift32 (seeded from
 *             GetRngValue() at init, salted by the slot) for the per-block
 *             draws: CE cycle times and peaks, the drift walk, the patch LFO
 *             jitter. kat_rngRead() is the Colloid's col_rngRead()
 *             (ColloidVoice.c:286): one inline read of RNG_DR per sample on
 *             the device, GetRngValue() on the host.
 * Why:        per-block draws need no hardware read (cheaper, and the host
 *             model is repeatable with a seed); the audio noise keeps the
 *             accepted hardware character (MEMORY.md DSP/RNG). An
 *             out-of-line call in the loop spilled every FP register in the
 *             Colloid (+47 a sample), hence the inline register load.
 */
```

#### `kat_svfControl()` (static, noinline)

```c
/*
 * One loop or output filter's coefficients for this block (S103 P1).
 *
 * What:       col_svfControl() (ColloidVoice.c:550) for KatSvf: the block's
 *             start a = a + 32 da (where the last ramp ended); the cutoff
 *             target st (semitones above 20 Hz, clamped 0..115.7) and the
 *             damping target kT = 2 - 1.95 res, both slewed by 0.28; g =
 *             tan(pi 20 x 2^(st/12) / fs) (col_tanf form), a1 = 1 / (1 + g (g +
 *             k)), a2 = g a1, a3 = g a2 as the targets; da = (target - a) / 32;
 *             then the output mix for the type: LP (0, 0, 1), BP (0, 1, 0),
 *             HP (1, -k, -1).
 * Why:        the TPT form is stable under the per-sample ramp, and the slew
 *             removes block steps from LFO, CE2 and tracking moves; two VDIV
 *             a block per filter, none per sample. A local copy, not a shared
 *             one: col_svfControl() is static, and moving it would change the
 *             Colloid's object (gate: Colloid bit-identical).
 * Inputs:     f, st (semitones), res (0..1), mix (KAT_LT_* % 3).
 * Cost:       ≈ 99 a block (the Colloid's measured figure).
 * Accessors:  Katran_calcVoiceAsync() (F1 x2, F3 x1), Katran_initVoice().
 */
```

#### `kat_wtbBind()` (static, noinline)

```c
/*
 * Bind the shared wavetable (S103 P1; col_wtbBind()'s rule, ColloidVoice.c:668).
 *
 * What:       records the key (KAT_BIND_WTB, the table = osc.waveform clamped
 *             to the installed count, the catalogue generation); the frame
 *             count and the first frame's row. No table: one frame and NULL,
 *             and the async then plays sawTable[1] at the oscillator's level
 *             (silence is not used: the loop must keep an exciter).
 * Why:        one table for both oscillators (K-D8); each picks its own frame
 *             from its own pos. osc_userBind() is never called on Katran.
 */
```

#### `kat_oscControl()` (static, noinline)

```c
/*
 * The three oscillators' control for this block (S103 P1, P§3.4, K-D10).
 *
 * What:       1. pitch: A = the note + osc.midiFreq (coarse, fine) as the
 *                Colloid (ColloidVoice.c:1388-1395), log2 Hz; B = the note +
 *                (bCoarse - 48) semitones + (bFine - 63) / 63 x 0.5 semitone
 *                + the patch LFO's inv x lfl x 0.3 semitone (D§5.7: inv to
 *                OSC B fine). A's L/R: one alk_exp2f() for A, then x 2^(+-d)
 *                for the detune d = dtn'^2 x 25 cents (one exp2 at init per
 *                byte change, kept in a block-local ratio: C4). Increments
 *                inc = f x 2^32 / fs, f capped at 0.45 fs for the table forms;
 *                for the arena form, uncapped (P§3.4: unlimited speed; the
 *                increment wraps the window).
 *             2. form per oscillator: src selects ARENA for A (L and R) and/or
 *                B; otherwise the typ form: wav -> PLAIN (Saw/Rec/Tri rows by
 *                octave as col_oscControl(); Sin the sine row), wtb -> PLAIN
 *                on the frame row, pwm -> PWMD (Saw, Rec) or WARP (Tri).
 *             3. the next row (applied by the loop at the oscillator's next
 *                cycle start, K-D10): wav/pwm the octave row for this block's
 *                pitch; wtb the frame round(pos' x (F - 1)) (A L: pos - psp,
 *                A R: pos + psp, psp = dtn' x 16 steps; B: bPos + the patch
 *                LFO's cv x lfl x 24 steps); ARENA the window start
 *                S = round(pos' x (arenaN - 1,024)) with the same offsets.
 *             4. PWM widths: the S094 P4 position law (oscPwmDRec/DSaw,
 *                oscPwmLevel*, oscPwmTriLift) from each oscillator's pos, slewed
 *                by OSC_PWM_SLEW_STEPS, glide start/end (pwD0/pwD) as the
 *                Colloid's S096 P2 F5 rule.
 * Why:        the loops only read rows, phases and increments; every choice
 *             is made here once per block. The row is swapped at the cycle
 *             start so a moving pos never cuts a cycle (K-D10), for frames
 *             and arena windows alike.
 * Cost:       ≈ 320 a block (§1.4).
 * Accessors:  Katran_calcVoiceAsync().
 */
```

#### `kat_delayControl()` (static)

```c
/*
 * The two delay times for this block (S103 P1, D§5.3, P§8.2 tim/dtk/drf/spr).
 *
 * What:       base T = katTimeSamples[tim] (KatranTables.c: log, 32 ..
 *             1,792 samples) x 2^(-dtk' x (B's pitch offset in octaves)) (one
 *             alk_exp2f(): the delay follows OSC B's pitch, so the comb tracks
 *             it); spread s = max(0, (spr - 63) / 64): T_L = T (1 + 0.12 s),
 *             T_R = T (1 - 0.12 s); drift: each core's driftTgt takes a new
 *             draw in +-1.5 % x drf' every 16 blocks and drift slews to it by
 *             1/16 a block; clamp 32 .. 2,040. The per-sample step is
 *             tStep = (target - tNow) / 32 (the time ramps per sample: a time
 *             change bends the pitch like tape, D§5.3).
 * Why:        one exp2 for both cores (C4); the clamp keeps the fractional
 *             read inside the 2,048 ring with one sample of guard.
 * Inputs:     voice; the B offset from kat_oscControl().
 * Outputs:    tNow, tStep per core; the loop pitch (log2 fs/T) for F1 tracking.
 */
```

#### `kat_runControl()` (static)

```c
/*
 * The run gain gr for this block (S103 P1, P§3.1; decisions Q2, Q3, Q9).
 *
 * What:       the target and the per-sample step of gr, from, in order:
 *             1. the pending-apply fade (fadeReq > 0): gr falls to 0 by
 *                katFadeK per block (≈ 20 ms), fadeReq counts down; at 0 the
 *                normal rule resumes (the apply was cancelled);
 *             2. not armed (boot, Scene load, before a kick or a start): 0;
 *             3. the transport stop edge (wasRunning && !seq_isRunning()):
 *                stopDecay = 1, which decays at katRunK[126] even when run is
 *                127; a start edge or a kick clears it;
 *             4. run == 127 (and no stopDecay): gr rises to 1 over 20 ms;
 *             5. run 0..126: gr x= katRunK[run] per block (-60 dB at T(run),
 *                0.25 .. 60 s); below 1e-4 it is 0.
 *             grStep = (new - gr) / 32: the loops ramp it per sample.
 *             silentBlocks counts blocks with gr == 0 (saturating).
 * Why:        one gain on every delay input ends the sound naturally with its
 *             character (K-D9, G1); the transport and boot rules are Q3; the
 *             fade is Q9 (a Scene change over a drone without a click).
 * Accessors:  Katran_calcVoiceAsync().
 */
```

#### `kat_kickControl()` (static inline)

```c
/*
 * The kick's per-block part (S103 P1, P§3.2).
 *
 * What:       kE decays by katKickK (≈ 80 ms to -60 dB); burst = min(1, 2 kik')
 *             and swell = max(0, 2 kik' - 1) (the base/offset split of one
 *             row). This block: the exciter level += burst x kE x 0.8, the
 *             noise mix += burst x kE x 0.5 (toward noise), the feedback +=
 *             swell x kE x 0.3; all inside the shared feedback clamp.
 * Why:        the kick adds energy through gains the loops already apply, so
 *             it costs nothing per sample.
 */
```

#### `kat_ceStep()` (static)

```c
/*
 * One chaos envelope's block (S103 P1, P§3.3).
 *
 * What:       rise: level += up until level >= peak, then fall; fall: level
 *             -= down until level <= 0, then a new cycle: total = katCeBlocks
 *             [cycle] x 2^(chs' x r) (r uniform +-1 from kat_rngNext()), rise
 *             share = skew' (0 instant .. 1 all rise; each stage >= 3 blocks),
 *             peak = 1 - chs' x |r2| / 2; above chs 64, up is also scaled by
 *             (1 + 2 x (chs' - 0.5) x min(1, 4 ringPrev)): a loud loop hurries
 *             its own envelope.
 * Why:        repeating and chaotic, never retriggered (the brief).
 */
```

#### `kat_plfoStep()` (static)

```c
/*
 * The patch LFO's block (S103 P1, D§5.7; lfm folds lfw).
 *
 * What:       rate = katPlfoHz[lfr] x 2^(lfm' x (1.5 ringPrev + inv)) (one
 *             alk_exp2f()); the phase advances by rate x 32 / fs; the shape is
 *             the sine of the phase warped by w = 0.5 + 0.45 lfm' x inv (lfm 0
 *             is a plain sine; rising lfm skews it); a chaos jitter of chs' x
 *             0.1 on the rate per cycle. cv = shape x lfl', inv = -cv.
 * Why:        the patch's unstable LFO: its rate follows the output and its
 *             own inverted output. ringPrev is the last block's mean |r|.
 */
```

#### `Katran_initVoice()`

```c
/*
 * Katran_initVoice() (S103 P1). Contract: KatranVoice.h.
 *
 * What:       slot, both rings (cleared: the FxBuffer owner clears what it
 *             claims, FxBuffer.h), the arena view (fxbuf_arenaView()), every
 *             row byte at init.kat's value (§11), the OscInfo (oscType
 *             PWM, waveform Rec = OSC_PWM_REC, midiFreq (36 << 8) | 63,
 *             modNodeValue 1), random phases per oscillator and seeds per
 *             voice (GetRngValue() salted by slot), filters at rest
 *             (kat_svfControl() then a = target, da = 0), gr 0 and armed 0
 *             (silent until a kick or a start, Q3), wasRunning =
 *             seq_isRunning(), lfo_init().
 * Why:        the caller memset the slot; this sets every field anyway, as the
 *             other engines do. A NULL ring (no unit) leaves ring[] NULL and
 *             the sync renders silence.
 */
```

#### `Katran_triggerVoice()`

```c
/*
 * The kick (S103 P1, P§3.2; decision Q4). Contract: KatranVoice.h.
 *
 * What:       the platform part first (lfo_retrigger(), the velocity node,
 *             instrumentManager_applyVelocityModulationTarget()), as every
 *             engine; then note = the trigger note; armed = 1; stopDecay = 0;
 *             fadeReq unchanged (a pending apply commits at the trigger
 *             through the force path anyway); K1: gr ramps to 1 over
 *             KAT_KICK_REARM blocks; K2/K3: kE = velocity'.
 * Why:        a step, a pad, a MIDI note and the chromatic overlay all
 *             arrive here (instrumentManager_triggerTrack()); nothing resets
 *             a phase, a delay or an envelope.
 * Note-off:   none (Katran has no gate).
 */
```

#### `Katran_calcVoiceAsync()`

```c
/*
 * The block's control (S103 P1). Contract: KatranVoice.h.
 *
 * What:       in order: the transport edges; kat_runControl();
 *             kat_kickControl(); kat_plfoStep(); kat_ceStep() x2;
 *             the wtb bind check (kat_wtbBind() when the key changed);
 *             kat_oscControl(); kat_delayControl(); F1: per core st =
 *             (tracked types: the loop pitch in semitones + frq' x 48;
 *             absolute: frq' x 115.7) + CE2 (dp2 bipolar) x 48 + cv x lfl x 12
 *             + (core L +, R -) spread x 12, res, type % 3 ->
 *             kat_svfControl(); F2: f2K = katLowCutK[lct]; F3: one
 *             kat_svfControl() for c = (cut - 63) / 63: c < 0 LP at
 *             115.7 + c x 77 semitones, c > 0 HP at c x 80 semitones, c = 0 LP
 *             fully open; res fixed 0.35; + CE2 x 24; TORCIDO: tGain =
 *             katDriveGain[drv], tTube = 0.25 drv' + 0.15 cv x lfl', tWet =
 *             wet', tGain2 = tGain; the feedback target fb = min(fbk' x 0.95 +
 *             0.2 x inv x lfl' + swell, KAT_FB_CLAMP) with xfb from spr below 63,
 *             the sum clamped at 1.1; the ring crossfade target rk = (bLevel >
 *             0); the VCA targets g1 = min(1, 0.67 (1 + v)) x clamp(0.42 + cv,
 *             0, 1), g2 = min(1, 0.38 (1 - v)) x clamp(0.42 + inv, 0, 1),
 *             v = (vca - 63) / 63; the CE1 gain target 1 - dp1' + dp1' x
 *             CE1.level; wid = width / 64; the arena damp coefficient
 *             katDampK[dmp]; the guard: peak of blkA/blkB samples 0, 8, 16,
 *             24 (last block) -> guard target min(1, 0.7 / peak), attack at
 *             once, release x 1.01 a block. Every gain gets now/step for the
 *             per-sample ramp.
 * Why:        every per-sample loop reads only ramps and coefficients; all
 *             laws are per block (constant CPU: no branch here changes what
 *             the loops cost).
 */
```

#### `kat_coreRender()` (static, noinline): L1 + L4 for one core

```c
/*
 * One core's audio for the block (S103 P1, P§4 L1, L3 handled before, L4).
 *
 * What:       per sample n: t1 = TOR1(D[n]): x = D x tGain, x' = clamp(x,
 *             +-1.5), y = x' - (4/27) x'^3 + tTube x'^2 (C2: the tube folded
 *             into the cubic), t1 = lv1 x (tWet y + (1 - tWet) D);
 *             t2 = lv2 x cubic(tGain2 x (exc ((1 - nse) A[n] + nse noise[n])));
 *             u = F2(F1(t1 + t2)); ring[w] = SSAT16((gr (u + xfb D_other[n] +
 *             fb D[n])) x 32768); r = t1 ((1 - rk) + rk bLvl B[n]); core L:
 *             outL[n] = r + g2 t1; core R: right[n] = r (1 + g1); every gain
 *             += its step; w = (w + 1) & KAT_RING_MASK.
 * Why:        one loop per core keeps the live values few (the staged form,
 *             D§9.3; S086: no spills). D_other is read from the other core's
 *             block, already filled by L1 for both cores before either core
 *             writes.
 * Inputs:     voice, the core index, its out block (buf or right).
 */
```

#### `kat_delayRead()` (static, noinline): L1 for both cores

```c
/*
 * Both cores' delay outputs for the block (S103 P1, D§4 L1).
 *
 * What:       per core and sample: the timed tap at w - T(n) (fractional,
 *             linear interpolation; T ramps by tStep), the dry tap at w - 32,
 *             D = 0.5 timed + 0.5 dry (dmx fixed at 64, P§8.3), stored as
 *             int16 into blkD[core].
 * Why:        with T >= 32, the whole block's output is in the ring before
 *             this block writes, so the exciters' resets (L3) and the
 *             cross-feedback (L4) can read it first.
 */
```

#### `kat_outRender()` (static, noinline): L5

```c
/*
 * The output stage, in place on buf and right (S103 P1, P§4 L5).
 *
 * What:       per sample: L, R from the blocks (x 1/32768); F3 on each with
 *             the shared ramped coefficients (C1); width: m = (L + R) / 2,
 *             s = (L - R) / 2 x wid; L = m + s, R = m - s; x the CE1 gain
 *             (ramped); x KAT_OUT_NORM; SSAT16 stores.
 * Why:        one coefficient ramp for both sides (C1); the width before the
 *             level so wid 0 is true mono.
 */
```

#### `Katran_calcVoiceSyncBlock()`

```c
/*
 * The block's audio (S103 P1). Contract: KatranVoice.h.
 *
 * What:       no ring (a unit was not granted): memset buf and right, return.
 *             Otherwise, in order: kat_delayRead() (both cores); OSC B's loop
 *             for its form (KatranOsc.h) into blkB; for core L then core R:
 *             OSC A's loop for that core's copy into blkA (resets from
 *             blkD[core] and blkB), then kat_coreRender(); kat_outRender();
 *             ringPrev = mean |r| of the block's last sample per core; the
 *             guard's peaks are read from blkA / blkB by the next async.
 * Why:        the staged order of D§4; every loop runs every block
 *             (constant CPU); the form only picks which equal-cost loop.
 */
```

#### `Katran_isQuiet()`, `Katran_requestFade()`, `Katran_setOscType()`

```c
/*
 * Quiet, the fade request, the type writer (S103 P1).
 *
 * Katran_isQuiet():     silentBlocks >= KAT_QUIET_BLOCKS: gr has been 0
 *                       for longer than one ring, so every delay holds zeros
 *                       and the output is silent.
 * Katran_requestFade(): fadeReq = KAT_FADE_HOLD. Called by
 *                       instrumentManager_ampEnvelopeQuiet() while a deferred
 *                       apply polls a non-quiet Katran (Q9); the request lapses
 *                       if the polls stop (the apply was cancelled) and the
 *                       voice returns to its run state.
 * Katran_setOscType():  Colloid_setOscType()'s shape (ColloidVoice.c:2194):
 *                       byte 0 wav, 1 wtb, 2 pwm -> OSC_TYPE_WAV / _WTB / _PWM
 *                       (Oscillator.h:95-99); clamp osc.waveform to the new
 *                       list (wav: COL_WAVE_COUNT, pwm: OSC_PWM_WAVE_COUNT,
 *                       wtb: the installed tables); drop the bind.
 */
```

---

### N-3 / N-4 `Core/DSP/Instruments/Katran/KatranOsc.h` / `.c`

**Action:** add. **Template:** `ColloidOsc.h/.c` (the read helpers
`col_read()` at `ColloidOsc.c:154`, the two-read PWM, the warp). **Placement:**
flash (no section attribute), `noinline, noclone`.

**File comment block (KatranOsc.h):**

```c
/*
 * KatranOsc.h - Katran's oscillator loops (S103 P1).
 *
 * What:       eight loops, each one block (32 samples):
 *               kat_bPlain / kat_bPwmD / kat_bWarp / kat_bArena  OSC B into
 *                 blkB; each sample's LSB is 1 on the sample where the phase
 *                 wrapped (the carry of p + inc), and on that sample the row
 *                 switches to rowNext (K-D10);
 *               kat_aPlain / kat_aPwmD / kat_aWarp / kat_aArena  one copy of
 *                 OSC A into blkA, with the hard sync: per sample
 *                 s = P x wrap(B[n]) x 32767 + D[n]; a reset when s crosses
 *                 the threshold upward (cur = s > th; reset = cur & !prev):
 *                 phase = 0 and row = rowNext; a natural wrap also takes
 *                 rowNext.
 *             The arena loops read row[ph >> 22] (no interpolation: the rough
 *             read, P§3.4), then the DC block (one-pole HP at ~10 Hz), the
 *             damping one-pole (dampK), and the guard gain, into int16.
 * Why:        the Colloid's loops sum pairs into buses and never reset;
 *             Katran's write blocks and take a reset per sample. One loop per
 *             form keeps every form a fixed cost; the dearest (PWM D or warp)
 *             is the one budgeted, and gate G-CPU2 keeps the arena form under
 *             it.
 * Inputs:     the voice (rows, phases, increments, widths, conditioning
 *             state), the threshold th (sth: 127 = 32767 + 1, never), D and B
 *             blocks for A.
 * Outputs:    blkB or blkA; phases, rows and conditioning state updated.
 * Accessors:  Katran_calcVoiceSyncBlock().
 * Affiliates: ColloidOsc.c (the read and PWM arithmetic), oscPwmTables.h,
 *             oscPwmWarp.h (osc_pwmWarp()), tools/dsp_test/modules/katran.c
 *             (laws KA-OSC, KA-CYC, KA-SYNC, KA-ARENA).
 */
```

**Signatures** (one comment line each in the header):

```c
void kat_bPlain(KatranVoice *v);
void kat_bPwmD(KatranVoice *v, int32_t sign);       /* -1 Rec, +1 Saw */
void kat_bWarp(KatranVoice *v);
void kat_bArena(KatranVoice *v);
void kat_aPlain(KatranVoice *v, uint32_t c, int32_t th);
void kat_aPwmD(KatranVoice *v, uint32_t c, int32_t th, int32_t sign);
void kat_aWarp(KatranVoice *v, uint32_t c, int32_t th);
void kat_aArena(KatranVoice *v, uint32_t c, int32_t th);
```

**Gate per loop** (`katran_cpu.py`): `kat_bPwmD` ≤ 31, `kat_aPwmD` ≤ 37
(one copy; the pair is two calls, ≤ 74), every arena loop ≤ its PWM sibling.

---

### N-5 / N-6 `KatranTables.h` / `KatranTables.c` (generated)

**Action:** add. Generated by `tools/katran/gen_kat_tables.py` (T-2);
`--check` compares the committed file.

```c
/*
 * KatranTables.h - Katran's per-block laws (S103 P1; generated by
 * tools/katran/gen_kat_tables.py: do not edit by hand).
 *
 *   katRunK[128]        run: the per-block decay factor for T(run) = 0.25 s x
 *                       240^(run/126) (0.25 .. 60 s, log), 10^(-3 x 32 / (fs
 *                       T)); [127] = 1.0 (on).
 *   katCeBlocks[128]    a chaos envelope's cycle in blocks: 10 ms .. 60 s, log.
 *   katTimeSamples[128] the delay time: 32 x 56^(tim/127) samples (32 .. 1,792).
 *   katLowCutK[128]     F2's one-pole coefficient: 5 .. 600 Hz, log.
 *   katPlfoHz[128]      the patch LFO rate: 0.02 .. 20 Hz, log.
 *   katDampK[128]       the arena damping one-pole: open (1.0) .. ~300 Hz.
 *   katDriveGain[128]   TORCIDO drive: 2^(5 x drv / 127) (x1 .. x32).
 *   katKickK, katFadeK  scalars: the kick's 80 ms and the fade's 20 ms decay.
 * Why:        no exp/log per block where a table serves (the colEgK[] rule).
 */
```

---

### N-7 `Core/DSP/Instruments/Katran/KatranParameters.h`

**Action:** add. Template `ColloidParameters.h`.

```c
/*
 * KatranParameters.h (S103 P1).
 *
 * KATRAN_INSTRUMENT_TYPE_FLAGS   INSTRUMENT_FLAG_BASIC (the brief).
 * KATRAN_PARAM_DESCRIPTOR_COUNT  57: 21 platform rows + 36 Katran rows.
 * KATRAN_PARAM_FX_FIRST          60: the first index reserved for the later
 *                                insert effect (fx_type, fx_rate, fx_depth,
 *                                fx_mix; P§9). The Basic table must end at or
 *                                before it (asserted in KatranParameters.c),
 *                                so the four effect rows always fit the 64-row
 *                                Kit image when the Basic/Advanced merge
 *                                appends them after the last Basic row, as
 *                                the Colloid's clx rows follow its 59.
 * KATRAN_MENU_PAGE_COUNT         8.
 * KAT_IDX_*                      the indices InstrumentManager uses (none at
 *                                first; kept for the merge's fx_type).
 */
```

---

### N-8 `Core/DSP/Instruments/Katran/KatranParameters.c`

**Action:** add. Template `ColloidParameters.c` (the macros at lines 51–133
are copied with `KatranVoice` in `offsetof`).

**File comment block:**

```c
/*
 * Katran parameter source of truth (S103 P1; plan revision 3 §8).
 *
 * What:       what each Kit storage cell means when the slot holds a Katran:
 *             key, labels, dtype, flags, modulation domain, runtime binding.
 *             Every row on one line (check_special_tags.py and
 *             make_init_kat.py parse them). The enum is in page order.
 * Why:        the musically relevant levers only (your Q1 answer): 36 own
 *             rows, many of them combined (a base / offset split of one
 *             range), plus the platform's 21; the LFO page is the platform's,
 *             unchanged. 57 rows; 57..59 free; 60..63 reserved for the
 *             insert effect (P§9).
 * Not offered: srt (no SRR), osc1_run (free-running), chn, a gate, an amp
 *             ADSR (the chaos envelopes, P§3.3), the sub (P§6.2), every
 *             fixed value of P§8.3.
 * Affiliates: KatranVoice.h; MenuText.h (katSourceNames, katLoopTypeNames,
 *             alkTypeNames for typ); tools/katran/make_init_kat.py;
 *             SD_CARD/Instrument/Katran/init.kat.
 */
```

**Label and flags:** `katran_instrument_display_label[] = "Katran"` (6 cells;
the Morph row shows `KatraMrp`, `menu.c` 5 characters + `Mrp`);
`katran_instrument_type_flags = KATRAN_INSTRUMENT_TYPE_FLAGS`.

**The rows** (enum `kat_param_index_t` in this order; macro, key, category,
long, short, dtype, mod domain, member, type, special):

| # | Macro | Key | Cat | Long | Short | Dtype | Mod | Member | Type | Special |
|---:|---|---|---|---|---|---|---|---|---|---|
| 0 | ROW_SPECIAL | `osc1_pitch_coarse` | OSC | COARSE | COARSE | 0B127 | 0_127 | `osc.modNodeValue` | SPECIAL_F | PITCH_COARSE \| OSC1 |
| 1 | ROW_SPECIAL | `osc1_pitch_fine` | OSC | FINE | FINE | PM63 | PM63 | `osc.modNodeValue` | SPECIAL_F | PITCH_FINE \| OSC1 |
| 2 | ROW | `osc1_position` | OSC | POSITION | POS | 0B127 | 0_127 | `osc.position` | UINT8 | |
| 3 | ROW | `kat_spread_a` | OSC | DETUNE | DTN | 0B127 | 0_127 | `spreadA` | UINT8 | |
| 4 | ROW_MENU | `osc1_wave` | OSC | WAVE | WAVE | MENU_WAVEFORM | WAVE | `osc.waveform` | UINT8 | |
| 5 | ROW_SELECT_ONOFF_SPECIAL | `osc1_type` | OSC | TYPE | FIL_TYPE | tag ALK_TYPE (3) | — | `osc.oscType` | UINT8 | OSC_TYPE \| OSC1 |
| 6 | ROW_SELECT_ONOFF | `kat_source` | OSC | SOURCE | SRCE | tag KAT_SOURCE (4) | — | `source` | UINT8 | |
| 7 | ROW | `kat_damp` | OSC | DAMPING | DMP | 0B127 | 0_127 | `damp_` | UINT8 | |
| 8 | ROW | `kat_run` | VOICE | RUN | PLAY_MODE (`run`) | 0B127 | 0_127 | `run` | UINT8 | |
| 9 | ROW | `kat_kick` | TRIGGER | KICK | KIK | 0B127 | 0_127 | `kick` | UINT8 | |
| 10 | ROW | `kat_ce1_cycle` | ENVELOPE | CYCLE | CY1 | 0B127 | 0_127 | `ce1Cycle` | UINT8 | |
| 11 | ROW | `kat_ce1_skew` | ENVELOPE | SKEW | SK1 | 0B127 | 0_127 | `ce1Skew` | UINT8 | |
| 12 | ROW | `kat_ce1_depth` | ENVELOPE | DEPTH | DP1 | 0B127 | 0_127 | `ce1Depth` | UINT8 | |
| 13 | ROW | `kat_ce2_cycle` | ENVELOPE | CYCLE | CY2 | 0B127 | 0_127 | `ce2Cycle` | UINT8 | |
| 14 | ROW | `kat_ce2_skew` | ENVELOPE | SKEW | SK2 | 0B127 | 0_127 | `ce2Skew` | UINT8 | |
| 15 | ROW | `kat_ce2_depth` | ENVELOPE | DEPTH | DP2 | PM63 | PM63 | `ce2Depth` | UINT8 | |
| 16 | ROW | `kat_chaos` | ENVELOPE | CHAOS | CHS | 0B127 | 0_127 | `chaos` | UINT8 | |
| 17 | ROW | `kat_plfo_rate` | MOD_OSC | RATE | LFR | 0B127 | 0_127 | `plfoRate` | UINT8 | |
| 18 | ROW | `kat_plfo_mod` | MOD_OSC | SELFMOD | LFM | 0B127 | 0_127 | `plfoMod` | UINT8 | |
| 19 | ROW | `kat_plfo_level` | MOD_OSC | LEVEL | LFL | 0B127 | 0_127 | `plfoLevel` | UINT8 | |
| 20 | ROW | `kat_vca` | VOICE | VCABAL | VCA | PM63 | PM63 | `vca` | UINT8 | |
| 21 | ROW | `kat_b_coarse` | FM | COARSE | COARSE | 0B127 | 0_127 | `bCoarse` | UINT8 | |
| 22 | ROW | `kat_b_fine` | FM | FINE | FINE | PM63 | PM63 | `bFine` | UINT8 | |
| 23 | ROW | `kat_b_pos` | FM | POSITION | POS | 0B127 | 0_127 | `bPos` | UINT8 | |
| 24 | ROW | `kat_b_level` | FM | LEVEL | LVL | 0B127 | 0_127 | `bLevel` | UINT8 | |
| 25 | ROW | `kat_level_a` | OSC | EXCITER | EXC | 0B127 | 0_127 | `levelA` | UINT8 | |
| 26 | ROW | `kat_noise` | NOISE | NOISEMIX | NSE | 0B127 | 0_127 | `noise` | UINT8 | |
| 27 | ROW | `kat_sync` | OSC | SYNCTHR | STH | 0B127 | 0_127 | `sync` | UINT8 | |
| 28 | ROW | `kat_drive` | DELAY | DRIVE_WORD | DRIVE | 0B127 | 0_127 | `drive` | UINT8 | |
| 29 | ROW | `kat_wet` | DELAY | WET | WET | 0B127 | 0_127 | `wet` | UINT8 | |
| 30 | ROW | `kat_time` | DELAY | TIME | MICROTIMING (`tim`) | 0B127 | 0_127 | `time` | UINT8 | |
| 31 | ROW | `kat_feedback` | DELAY | FEEDBACK | FBK | 0B127 | 0_127 | `feedback` | UINT8 | |
| 32 | ROW | `kat_drift` | DELAY | DRIFT | DRF | 0B127 | 0_127 | `drift_` | UINT8 | |
| 33 | ROW | `kat_track` | DELAY | TRACK | DTK | 0B127 | 0_127 | `track` | UINT8 | |
| 34 | ROW | `kat_loop_cutoff` | FILTER | CUTOFF | FREQ | 0B127 | 0_127 | `loopCut` | UINT8 | |
| 35 | ROW | `kat_loop_reso` | FILTER | RESO | FIL_RESO | 0B127 | 0_127 | `loopRes` | UINT8 | |
| 36 | ROW_SELECT_ONOFF | `kat_loop_type` | FILTER | FILTTYPE | FIL_TYPE | tag KAT_LOOP_TYPE (10) | — | `loopType` | UINT8 | |
| 37 | ROW | `kat_low_cut` | FILTER | LOWCUT | LCT | 0B127 | 0_127 | `lowCut` | UINT8 | |
| 38 | ROW | `kat_out_cut` | FILTER | OUTFILT | CUT | PM63 | PM63 | `outCut` | UINT8 | |
| 39 | ROW | `instrument_vol` | VOICE | VOL | VOL | 0B127 | 0_127 | `vol` | FLT | |
| 40 | ROW | `instrument_pan` | VOICE | PAN | PAN | PM63 | PM63 | `pan` | UINT8 | |
| 41 | ROW | `kat_spread` | VOICE | SPREAD | SPR | PM63 | PM63 | `spread` | UINT8 | |
| 42 | ROW | `kat_width` | VOICE | WIDTH | WID | 0B127 | 0_127 | `width` | UINT8 | |
| 43–50 | (as `ColloidParameters.c` 339–346) | `lfo_rate` `lfo_amount` `lfo_amount_2` `lfo_wave` `lfo_retrigger_voice` `lfo_polarity` `lfo_sync` `lfo_offset` | LFO | | | | | `lfo.*` | | LFO_RATE, LFO_SYNC, LFO_OFFSET |
| 51–52 | (as `:349–350`) | `velo_mod_amount` `velo_mod_dest` | VELOCITY | | | | | NOBIND | | |
| 53–56 | (as `:352–355`) | `lfo_target_voice` `lfo_target_param` `lfo_target_voice_2` `lfo_target_param_2` | LFO | | | | | NOBIND | | |

- Every Katran row is `IM_SPECIAL_NONE` (a `kat_*` key classifies as NONE in
  `check_special_tags.py` and in `instrumentManager_classifySpecialKey()`).
- `TYPE_UINT8` for every own row: the async converts the byte, so an LFO or
  velocity overlay writes the same 0..127 domain (the Colloid's rule,
  `ColloidParameters.c` comment above the table). RAM: 1 B a row (§2.1).

**Asserts** (after the enum):

```c
_Static_assert(KATRAN_PARAM_DESCRIPTOR_COUNT == KAT_PARAM_COUNT, "...");
/* P§9: the four effect rows must fit after the last Basic row */
_Static_assert(KAT_PARAM_COUNT <= KATRAN_PARAM_FX_FIRST, "Katran Basic rows past 59");
_Static_assert(KATRAN_PARAM_FX_FIRST + 4u <= INSTRUMENT_PARAM_COUNT, "...");
/* no Basic key may start with fx_ (the merge's keys) - checked by
 * make_init_kat.py --check, not here */
```

**Pages** (`katran_menu_pages[KATRAN_MENU_PAGE_COUNT]`; `KAT_E` =
`INSTRUMENT_MENU_EMPTY`; P§8.2):

```c
/*
 * Katran voice menu layout (S103 P1; plan revision 3 §8.2, your rules).
 *
 * What:       eight SELECT pages of 16 cells (four-cell screens):
 *               0 osc     coa fin pos dtn | wav typ src dmp
 *               1 aeg     run kik cy1 sk1 | dp1 cy2 sk2 dp2
 *               2 mod     dst amt chs --- | lfr lfm lfl vca
 *               3 fm      coa fin pos lvl | exc nse sth ---
 *               4 click   drv wet tim fbk | drf dtk --- --- | (fx dpt rte mix)
 *               5 filter  frq res typ lct | cut --- --- ---
 *               6 LFO     the platform layout (the Colloid's), unchanged
 *               7 mix     vol pan spr wid
 * Why:        at most two screens a page; the one screen 3 is click's, cells
 *             8..11, left empty for the later insert effect, as on clx / alx
 *             (colloid_adv_menu_pages[], ColloidParameters.c); the velocity
 *             cells keep their standard place on mod; mix is one screen.
 */
```

---

## 5. Changed files: Core

### 5.1 `Core/DSP/Instruments/InstrumentManager.h`

#### H-1 — enum: add `INSTRUMENT_TYPE_KAT` (line 141, after `INSTRUMENT_TYPE_ALX,`; before `INSTRUMENT_TYPE_UNKNOWN` at 142). **Add.**

```c
    /*
     * S103 P1: Katran, the drone / noise synth (token "kat"), before UNKNOWN.
     *
     * What:       value 9; UNKNOWN moves from 9 to 10. Its own engine
     *             (KatranVoice, a union member of its own); Basic.
     * Why:        appended (the standing rule): no stored value changes
     *             meaning; menu_instrumentLoadIndex[] (menu.c: +2 B .bss,
     *             signed off) and every `type >= UNKNOWN` guard follow; a
     *             .hcnames header written before the addition is a leading
     *             part of the new list and is accepted.
     */
    INSTRUMENT_TYPE_KAT,
```

### 5.2 `Core/DSP/Effects/FxBuffer.h` / `FxBuffer.c`

#### FB-1a — `FxBuffer.h` header comment, lines 44–75 ("What it deliberately does NOT do"). **Modify:** append one paragraph after line 58 (`snapshot … dispose of them.`):

```c
 * S103 P1: an Instrument may READ (never write) any arena byte through
 * fxbuf_arenaView(): the Katran's arena read form plays the arena as a raw
 * oscillator (S103_KATRAN_NOISE_SYNTH.md §3.4). Reads are foreground-render
 * only, between share changes by construction, so no locking is needed; the
 * contents are whatever the owners left (stale audio, float state, 8-bit
 * lines, power-up garbage), by design.
```

#### FB-1b — declaration after line 265 (`uint32_t fxbuf_arenaBytes(void);`). **Add.**

```c
/*
 * A read-only view of the whole arena (S103 P1).
 *
 * What:       *base = the arena's first byte as int16 samples, *samples =
 *             fxbuf_arenaBytes() / 2. Both outputs are link-time constants.
 * Why:        the Katran's arena read form (P§3.4) reads anywhere in the
 *             arena; geometry stays FxBuffer's (Effect types still use
 *             fxbuf_effectShare()). const: a reader can never write audio it
 *             does not own.
 * Inputs:     non-NULL base, samples.  Context: foreground.
 * Accessors:  Katran_initVoice().
 */
void fxbuf_arenaView(const int16_t **base, uint32_t *samples);
```

#### FB-2 — `FxBuffer.c` after line 270 (end of `fxbuf_arenaBytes()`). **Add.**

```c
/* A read-only view of the whole arena (S103 P1). Contract: FxBuffer.h. */
void fxbuf_arenaView(const int16_t **base, uint32_t *samples)
{
    if (base)
        *base = (const int16_t *)(const void *)fxbuf_state.base;
    if (samples)
        *samples = fxbuf_state.bytes / 2u;
}
```

### 5.3 `Core/DSP/Instruments/InstrumentManager.c`

#### IM-1 — includes. **Add.**
- After line 8 (`#include "TetraktParameters.h"`):
  `#include "KatranParameters.h"     /* S103 P1: registry row */`
- After line 20 (`#include "TetraktVoice.h"`):
  `#include "KatranVoice.h"          /* S103 P1: the runtime member and its entry points */`

#### IM-2 — registry row, after line 133 (the `[INSTRUMENT_TYPE_ALX]` row's last line `alkaline_adv_menu_pages, ALKALINE_MENU_PAGE_COUNT },`), before `};` (134). **Add.**

```c
    /*
     * S103 P1: Katran. Token/extension "kat"/".kat"; directory
     * /Instrument/Katran (6 characters; SD_CARD/Instrument/Katran in the card
     * image with init.kat; filesystem.c creates the folder on a first boot
     * that lacks it, from this row). Basic: may take any slot.
     */
    [INSTRUMENT_TYPE_KAT] =
    { INSTRUMENT_TYPE_KAT, "kat", katran_instrument_display_label, ".kat", "Katran",
      KATRAN_INSTRUMENT_TYPE_FLAGS,
      katran_param_descriptors, KATRAN_PARAM_DESCRIPTOR_COUNT,
      katran_menu_pages, KATRAN_MENU_PAGE_COUNT },
```

#### IM-3 — engine map, after line 166 (`[INSTRUMENT_TYPE_ALX] = INSTRUMENT_TYPE_ALK,`). **Add.**

```c
    [INSTRUMENT_TYPE_KAT] = INSTRUMENT_TYPE_KAT,   /* S103 P1: its own engine */
```

#### IM-4 — slot reserve comment (lines 291–295) and union (line 302). **Modify + add.**
- Comment line 295: `(Drum 712 B, Tetrakt 1,080 B, Colloid 1,224 B).` →
  `(Drum 712 B, Tetrakt 1,080 B, Colloid 1,224 B; S103 P1: Katran ≤ 1,200 B).`
- After line 302 (`TetraktVoice tetrakt; …`):

```c
    KatranVoice katran;       /* S103 P1: ≈ 1,104 B (est.; asserted ≤ 1,200 below); slot reserve 1,264 B */
```

#### IM-5 — asserts, after line 348 (`"the Tetrakt line is one voice unit");`). **Add.**

```c
/* S103 P1: the Katran voice fits the tagged slot with the plan's margin
 * (S103 P1 §2.1: <= 1,200 B of the 1,264 B reserve, K-D8), and each core's
 * ring fits one voice unit. */
_Static_assert(sizeof(KatranVoice) <= 1200u,
               "KatranVoice exceeds its 1,200 B target (S103 P1 §2.1)");
_Static_assert(KAT_RING <= FXBUF_VOICE_UNIT_SAMPLES,
               "a Katran ring is one voice unit");
```

#### IM-6 — accessor, after line 532 (end of `instrumentManager_tetraktRuntime()`). **Add.**

```c
/*
 * The slot's Katran runtime (S103 P1).
 *
 * What:       the tagged union member for slot, or NULL out of range. Like
 *             the other accessors it does not check the slot's type: every
 *             caller switches on instrumentManager_slotType() first.
 */
static KatranVoice *instrumentManager_katranRuntime(uint8_t slot)
{
    return (slot < INSTRUMENT_SLOT_COUNT) ? &runtime_slots[slot].katran : 0;
}
```

#### IM-7 — `instrumentManager_unitsWanted()` (line 549). **Modify** the comment (lines 536–548) and **add** a case after line 555 (`return 1u;` of the TKT case).
- Comment, "What:" line: append `S103 P1: 2 for a Katran (one ring per core).`
- Case:

```c
    /*
     * S103 P1: a Katran owns two units, one 2,048-sample ring per core
     * (P§7.2); the per-slot cap is 2 (FXBUF_VOICE_UNITS_PER_SLOT), so six
     * Katrans use the whole pool of 12.
     */
    case INSTRUMENT_TYPE_KAT:
        return 2u;
```

#### IM-8 — `instrumentManager_slotVoiceUnits()` comment (lines 586–603). **Modify** only.
- `Inputs:     slot, wanted (0 or 1).` → `Inputs:     slot, wanted (0..2; S103 P1: 2 for a Katran).`
- `Outputs:    the unit's samples or NULL.` → `Outputs:    the first unit's samples or NULL; a second unit is fxbuf_voiceUnit(slot, 1).`
- "With six Tetrakts, or four plus two Advanced choruses, six of the twelve
  units are owned." → append "Six Katrans own all twelve."

The code (`:604–612`) already claims `wanted` units and needs no change.

#### IM-9 — `instrumentManager_slotRightUpdate()`, after line 631 (`break;` of the TKT case). **Add.**

```c
    case INSTRUMENT_TYPE_KAT:   /* S103 P1: always stereo (L buf, R right[]) */
        right = runtime_slots[slot].katran.right;
        break;
```

#### IM-10 — `instrumentManager_waveDomainMax()`, after line 1278 (the closing `}` of the Colloid `if` block that ends `return (uint8_t)(COL_PWM_WAVE_COUNT - 1u);`; anchor: before `const uint8_t count = osc ? osc_userCount(…)` at 1280). **Add.**

```c
            /* S103 P1: a Katran slot lists the Colloid's band-limited wav
             * list (Saw Rec Tri Sin, COL_WAVE_COUNT) under wav; under pwm the
             * platform's three (Tri Saw Rec: osc_userCount(OSC_TYPE_PWM)
             * below, no R-S: the chop pair is not built for Katran); under
             * wtb the installed tables (below). */
            if (osc && instrumentManager_slotType(slot) == INSTRUMENT_TYPE_KAT &&
                osc->oscType == OSC_TYPE_WAV)
                return (uint8_t)(COL_WAVE_COUNT - 1u);
```

#### IM-11 — `instrumentManager_runtimeInstance()`, after line 1940. **Add.**

```c
    case INSTRUMENT_TYPE_KAT: return instrumentManager_katranRuntime(slot);   /* S103 P1 */
```

#### IM-12 — `instrumentManager_runtimeLfo()`, after line 1970 (the TKT case's `return voice ? &voice->lfo : 0; }`). **Add.**

```c
    case INSTRUMENT_TYPE_KAT: {   /* S103 P1: the Katran voice owns its LFO like every engine */
        KatranVoice *voice = instrumentManager_katranRuntime(slot);
        return voice ? &voice->lfo : 0; }
```

#### IM-13 — `instrumentManager_runtimeInit()`, after line 2082 (`break;` of the TKT case). **Add.**

```c
        case INSTRUMENT_TYPE_KAT:
            /*
             * S103 P1: the boot claim of the Katran's two units (fxbuf_init()
             * ran first in main.c; the Effect reads the share when its type
             * starts, so nothing is notified here, the Tetrakt's rule).
             * The claim runs first, into a local: C leaves the order of a
             * call's argument evaluations unspecified, so the second unit
             * (ordinal 1) must not be read in the same argument list.
             */
        {
            int16_t *const ringL = instrumentManager_slotVoiceUnits(slot, 2u);

            Katran_initVoice(instrumentManager_katranRuntime(slot), slot,
                             ringL, fxbuf_voiceUnit(slot, 1u));
        }
            break;
```

#### IM-14 — `instrumentManager_ampEnvelopeQuiet()`, after line 2128 (the Tetrakt branch's closing `}`). **Add.**

```c
    /*
     * S103 P1 (Q9): a Katran is quiet once its run gain has been 0 for more
     * than one ring (Katran_isQuiet()). It never goes quiet on its own while
     * run is on, so this query also asks it to fade: the only callers are
     * the deferred Scene/Kit apply workers (presetManager.c:2218, :2380),
     * which poll exactly while an apply waits for this slot. The request
     * lapses after KAT_FADE_HOLD blocks without a poll (a cancelled apply),
     * and the voice returns to its run state. The force path (1,000 passes,
     * DRUMSET_APPLY_FORCE_TICKS) is far longer than the ~20 ms fade.
     */
    if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_KAT) {
        KatranVoice *voice = instrumentManager_katranRuntime(slot);

        if (!voice)
            return 1u;
        if (Katran_isQuiet(voice))
            return 1u;
        Katran_requestFade(voice);
        return 0u;
    }
```

#### IM-15 — `instrumentManager_resetRuntimeSlotFromScene()`, lines 2393–2399. **Modify** the unit block.

Base:
```c
    {
        int16_t *const line =
            instrumentManager_slotVoiceUnits(slot, instrumentManager_unitsWanted(incoming));

        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_TKT)
            Tetrakt_initVoice(instrumentManager_tetraktRuntime(slot), slot, line);
    }
```
After (add the Katran line; extend the comment above at 2384–2392 with
"S103 P1: a Katran two (one ring per core)"):
```c
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_TKT)
            Tetrakt_initVoice(instrumentManager_tetraktRuntime(slot), slot, line);
        /* S103 P1: the Katran's second unit is ordinal 1 of the same claim */
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_KAT)
            Katran_initVoice(instrumentManager_katranRuntime(slot), slot, line,
                             fxbuf_voiceUnit(slot, 1u));
```

#### IM-16 — `instrumentManager_runtimePan()`, after line 2593. **Add.**

```c
    case INSTRUMENT_TYPE_KAT: {   /* S103 P1: the mixer's per-slot pan (as the Colloid, C-D11) */
        KatranVoice *voice = instrumentManager_katranRuntime(slot);
        return voice ? voice->pan : 64u; }
```

#### IM-17 — `instrumentManager_runtimeVolume()`, after line 2633. **Add.**

```c
    case INSTRUMENT_TYPE_KAT: {   /* S103 P1 */
        KatranVoice *voice = instrumentManager_katranRuntime(slot);
        return voice ? voice->vol : 0.0f; }
```

#### IM-18 — `instrumentManager_calcSlotAsync()`, after line 2915 (`break;` of the TKT case). **Add.**

```c
    case INSTRUMENT_TYPE_KAT:   /* S103 P1: control rate; the audio loops are the sync block */
        Katran_calcVoiceAsync(instrumentManager_katranRuntime(slot));
        break;
```

#### IM-19 — `instrumentManager_calcSlotSyncBlock()`, after line 3021 (`break;` of the TKT case). **Add.**

```c
    case INSTRUMENT_TYPE_KAT:
        /*
         * S103 P1: Katran_calcVoiceAsync() (control: run, kick, the patch
         * LFO, the chaos envelopes, the oscillators' rows and increments, the
         * delay times, the filters' coefficient ramps) and
         * Katran_calcVoiceSyncBlock() (the delay reads, OSC B, per core OSC A
         * and the core loop, then the output stage). buf receives the left
         * block; the right block stays in the voice (instrumentManager_
         * slotRight()). Both are noinline and the object is -fno-lto
         * (KatranVoice.h; Makefile), so the mixer's loops do not move
         * (S101 P3 §4; gate G-CPU3).
         */
        Katran_calcVoiceSyncBlock(instrumentManager_katranRuntime(slot), buf, size);
        break;
```

#### IM-20 — `instrumentManager_cellInactive()`, after line 3080 (the ALX branch's closing `}`). **Add.**

```c
    /* S103 P1: Katran's dimmed cells (katranParameters_cellInactive(): dmp
     * while src is off; pos under wav when src does not read the arena with
     * that oscillator) */
    if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_KAT) {
        const KatranVoice *kat = instrumentManager_katranRuntime(slot);

        return kat ? katranParameters_cellInactive(kat, descriptor) : 0u;
    }
```

(`katranParameters_cellInactive()` is declared in N-7 and defined in N-8,
the Colloid's `colloidParameters_cellInactive()` pattern.)

#### IM-21 — `instrumentManager_triggerTrack()`, after line 3253 (`break;` of the TKT case). **Add.**

```c
    case INSTRUMENT_TYPE_KAT:
        /*
         * S103 P1: a trigger kicks the loop (P§3.2): Katran_triggerVoice()
         * re-arms the run gain, bursts the exciters and swells the feedback;
         * nothing restarts. The platform part (LFO retrigger, velocity nodes)
         * runs inside it for `slot`. No step length: Katran has no gate. Track
         * 7 plays slot 5 as an ordinary kick (the slot-5 alternate decay
         * above finds no amp_envelope_decay row).
         */
        Katran_triggerVoice(instrumentManager_katranRuntime(slot), slot,
                            velocity, note);
        break;
```

#### IM-22 — `instrumentManager_requestGateDuck()`, before line 3279 (`if (instrumentManager_typeEngine(… ) == INSTRUMENT_TYPE_COL) {`). **Add.**

```c
    /*
     * S103 P1: a Katran ignores the roll fill boundary's duck. The duck
     * holds the slot's gate fade at 0 until the next trigger (S079 P2 fix
     * 4), which would silence a drone between steps. Katran has no note to
     * end; a roll window's end leaves the loop as it is.
     */
    if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_KAT)
        return;
```

#### IM-23 — `instrumentManager_osc()`, after line 3504 (the Colloid block's closing `}`). **Add.**

```c
    if (type == INSTRUMENT_TYPE_KAT) {   /* S103 P1: OSC A's OscInfo is the osc1_* anchor */
        KatranVoice *voice = instrumentManager_katranRuntime(slot);
        if (!voice) return 0;
        if (strncmp(key, "osc1_", 5) == 0) return &voice->osc;
    }
```

Why: `instrumentManager_osc1Type()` (`:1321`) and the modulation binding read
`osc1_*` through this; `menu_formatWaveCell()` needs the type.

#### IM-24 — `instrumentManager_oscBySelector()`, after line 3562 (the COL case's `return 0; }`). **Add.**

```c
    case INSTRUMENT_TYPE_KAT: {   /* S103 P1: the osc1_* writers (coarse, fine, typ) */
        KatranVoice *voice = instrumentManager_katranRuntime(slot);
        if (!voice) return 0;
        if (selector == IM_SPECIAL_OSC1) return &voice->osc;
        return 0; }
```

#### IM-25 — the `IM_SPECIAL_OSC_TYPE` writer, lines 5252–5258. **Modify:** add a branch after the Colloid branch (before `} else {` at 5258).

```c
        } else if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_KAT) {
            /* S103 P1: the Katran's typ (wav / wtb / pwm, alkTypeNames) */
            KatranVoice *kat = instrumentManager_katranRuntime(slot);

            if (!kat) return 0u;
            Katran_setOscType(kat, byteValue);
```

Also extend the writer's comment (`Affiliates:` line, ≈ 5241) with
`Katran_setOscType() (S103 P1)`.

### 5.4 Menu

#### ME-1 — `Core/Menu/menu.h`. **Add.**
- Long labels, line 287 (`LONG_DEPTH, LONG_SOURCE,`), insert after it:

```c
    /* S103 P1: the Katran rows (MenuLabels.c, same order) */
    LONG_RUN, LONG_KICK, LONG_CYCLE, LONG_SKEW, LONG_CHAOS, LONG_SELFMOD,
    LONG_VCABAL, LONG_EXCITER, LONG_NOISEMIX, LONG_SYNCTHR, LONG_WET,
    LONG_DRIFT, LONG_TRACK, LONG_LOWCUT, LONG_OUTFILT,
```

- Short labels, line 364 (`SHORT_FX, SHORT_FXR, SHORT_FXD, SHORT_FXM,`), insert after it:

```c
    /* S103 P1: the Katran rows; run, tim, dmp, dtn, spr, wid, fbk, drv, frq,
     * res, typ, cut, pos, coa, fin reuse existing ids */
    SHORT_KIK, SHORT_CY1, SHORT_SK1, SHORT_DP1, SHORT_CY2, SHORT_SK2,
    SHORT_DP2, SHORT_CHS, SHORT_LFR, SHORT_LFM, SHORT_LFL, SHORT_VCA,
    SHORT_EXC, SHORT_NSE, SHORT_STH, SHORT_SRCE, SHORT_WET, SHORT_DRF,
    SHORT_DTK, SHORT_LCT, SHORT_LVL,
```

Count check: short 228 → 249, long 180 → 195; both under the 256 assert
(`MenuLabels.c:194`).

#### ME-2 — `Core/Menu/MenuLabels.c`. **Add.**
- After line 81 (`{"fx "},{"rte"},{"dpt"},{"mix"},`):

```c
    /* S103 P1: the Katran rows (menu.h SHORT_KIK .. SHORT_LVL) */
    {"kik"},{"cy1"},{"sk1"},{"dp1"},{"cy2"},{"sk2"},{"dp2"},{"chs"},{"lfr"},
    {"lfm"},{"lfl"},{"vca"},{"exc"},{"nse"},{"sth"},{"src"},{"wet"},{"drf"},
    {"dtk"},{"lct"},{"lvl"},
```
- After line 185 (`{"Depth"},{"Source"},`):

```c
    /* S103 P1: the Katran rows (menu.h LONG_RUN .. LONG_OUTFILT) */
    {"Run"},{"Kick"},{"Cycle"},{"Skew"},{"Chaos"},{"SelfMod"},{"VCA Bal"},
    {"Exciter"},{"NoiseMix"},{"SyncThr"},{"Wet"},{"Drift"},{"Tracking"},
    {"Low Cut"},{"OutFilt"},
```

#### ME-3 — `Core/Menu/MenuText.h`. **Modify + add.**
- The free-tag comment in the Colloid tag block (lines 296–302: "tag 7 is
  the Tetrakt's layers; 4 and 9-15 are free"): → "tag 7 is the Tetrakt's
  layers, 9 the effect type, 4 and 10 the Katran's (S103 P1); 11-15 are free".
- After line 423 (the closing `};` of `fxTypeNames`):

```c
/*
 * The Katran's selectors (S103 P1).
 * What:       ONOFF_TAG_KAT_SOURCE 4 (src, katSourceNames: off, A, B, AB: the
 *             oscillators that read the arena, KAT_SRC_*); ONOFF_TAG_KAT_LOOP_TYPE
 *             10 (the loop filter's typ, katLoopTypeNames: LP BP HP, then the
 *             same tracking the loop's pitch: LPt BPt HPt, KAT_LT_*). The rows'
 *             dtype byte is DTYPE_ON_OFF | (tag << 4). Tags 4 and 10 were free.
 * Why:        every DTYPE_MENU id is taken (S078); menu_onoffTagNames()
 *             resolves the list per tag, so display, clamp, automation range
 *             and the step-edit line follow the count. The Katran's typ reuses
 *             ONOFF_TAG_ALK_TYPE (alkTypeNames: wav wtb pwm).
 * Accessors:  KatranParameters.c; menu.c menu_onoffTagNames().
 * Affiliates: KAT_SRC_COUNT, KAT_LT_COUNT (KatranVoice.h): asserted in menu.c.
 */
#define ONOFF_TAG_KAT_SOURCE       4u
#define ONOFF_TAG_KAT_LOOP_TYPE    10u
static const char katSourceNames[][4] = {
    {4}, {"off"},{"A  "},{"B  "},{"AB "},
};
static const char katLoopTypeNames[][4] = {
    {6}, {"LP "},{"BP "},{"HP "},{"LPt"},{"BPt"},{"HPt"},
};
```

#### ME-4 — `Core/Menu/menu.c` line 70 (after `#include "TetraktVoice.h"`). **Add.**

```c
#include "KatranVoice.h"       /* S103 P1: KAT_SRC_COUNT, KAT_LT_COUNT (the name asserts) */
```

#### ME-5 — `menu_formatWaveCell()`, lines 12608–12622. **Modify.**
- The comment (12608–12613): append "S103 P1: a Katran slot's osc1 under wav
  lists the same four (the Katran plays the Colloid's band-limited rows);
  under pwm the shared Tri/Saw/Rec branch below names it."
- The condition at 12615–12616:

```c
    if (isOsc1 && type == OSC_TYPE_WAV &&
        (instrumentManager_runtimeEngine(cell->slot) == INSTRUMENT_TYPE_COL ||
         instrumentManager_runtimeEngine(cell->slot) == INSTRUMENT_TYPE_KAT)) {
```

#### ME-6 — `menu_onoffTagNames()`, after line 12949 (`case ONOFF_TAG_TKT_LAYERS: …`). **Add.**

```c
    case ONOFF_TAG_KAT_SOURCE:     return katSourceNames;     /* S103 P1 */
    case ONOFF_TAG_KAT_LOOP_TYPE:  return katLoopTypeNames;   /* S103 P1 */
```

Also extend the function's comment (12922–12928):
"ONOFF_TAG_KAT_* -> the Katran's lists (S103 P1)".

#### ME-7 — name asserts, after line 13130 (the `tktLayerNames` assert). **Add.**

```c
/* S103 P1: the Katran lists name every value (KatranVoice.h). */
_Static_assert((sizeof(katSourceNames) / sizeof(katSourceNames[0])) - 1u == KAT_SRC_COUNT,
               "katSourceNames must list off, A, B and AB");
_Static_assert((sizeof(katLoopTypeNames) / sizeof(katLoopTypeNames[0])) - 1u == KAT_LT_COUNT,
               "katLoopTypeNames must list the six loop filter types");
```

### 5.5 `Core/Hardware/SD/storageTypes.h`

#### ST-1 — after line 146 (`#define STORAGE_INSTRUMENT_ALX …`). **Add.**

```c
#define STORAGE_INSTRUMENT_KAT     INSTRUMENT_TYPE_KAT   /* S103 P1 */
```

(No reader needs it; it keeps the alias list complete, as the earlier types.)

---

## 6. `Makefile`

#### MK-1a — include path, after line 41 (`-ICore/DSP/Instruments/Tetrakt \`). **Add:** `-ICore/DSP/Instruments/Katran \`

#### MK-1b — `SRCS`, after line 123 (`Core/DSP/Instruments/Tetrakt/TetraktParameters.c \`). **Add:** `Core/DSP/Instruments/Katran/KatranParameters.c \`

#### MK-1c — `DSP_SRCS`, after line 170 (`Core/DSP/Instruments/Tetrakt/TetraktVoice.c \`). **Add:**

```make
  Core/DSP/Instruments/Katran/KatranOsc.c \
  Core/DSP/Instruments/Katran/KatranTables.c \
  Core/DSP/Instruments/Katran/KatranVoice.c \
```

#### MK-1d — rules, after line 403 (the `TetraktTables.o` rule). **Add.**

```make
# S103 P1: the Katran engine, its oscillator loops and its tables are DSP:
# -Ofast like the Tetrakt, and -fno-lto for the same reason (outside the LTO
# unit the Katran cannot shift LTO's partitioning of the mixer or the other
# engines' loops; gate G-CPU3). Its entry points are noinline calls from
# InstrumentManager. KatranParameters.c stays in SRCS (-O2), as the other
# tables. Flash only: no ITCM section.
$(BUILD)/Core/DSP/Instruments/Katran/KatranVoice.o: Core/DSP/Instruments/Katran/KatranVoice.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
$(BUILD)/Core/DSP/Instruments/Katran/KatranOsc.o: Core/DSP/Instruments/Katran/KatranOsc.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
$(BUILD)/Core/DSP/Instruments/Katran/KatranTables.o: Core/DSP/Instruments/Katran/KatranTables.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
```

---

## 7. Tools

#### T-1 — `tools/dsp_test/check_special_tags.py`, after line 40 (`'TKT': …`). **Add.**

```python
    # S103 P1: the Katran. The osc1_* rows are Drum twins (coa/fin
    # PITCH_COARSE/FINE | OSC1, typ OSC_TYPE | OSC1, wav and pos NONE); the
    # LFO rows their twins; every kat_* key and vol/pan classify as NONE.
    'KAT': 'Core/DSP/Instruments/Katran/KatranParameters.c',
```

`registry_check()` (`:114`) then confirms the enum order, the token and the
TABLES entry.

#### T-2 — `tools/katran/gen_kat_tables.py`. **Add.**

```python
"""Generate and check Core/DSP/Instruments/Katran/KatranTables.c (S103 P1).

What:       writes the seven 128-entry laws and the two scalars of
            KatranTables.h from their formulas (fs = 44,108 Hz, 32-sample
            blocks); --check compares the committed file byte for byte.
Why:        the tables are the by-ear laws; the host model (modules/katran.c)
            and the firmware read the same file, and a hand edit cannot drift.
Accessors:  python3 tools/katran/gen_kat_tables.py [--check];
            make -C tools/dsp_test katran_tables.
"""
```

#### T-3 — `tools/katran/make_init_kat.py`. **Add.** A copy of
`tools/colloid/make_init_col.py` with one job (`kat`), the `@advanced-rows`
marker honoured (no Advanced job yet), and one extra check: **no Basic key
starts with `fx_`** (P§9). Docstring as `make_init_tkt.py` (lines 1–40) with
the Katran's INIT values (§11).

#### T-4 — `tools/dsp_test/modules/katran.c`. **Add.** The extracted-source
pattern of `modules/tetrakt.c`: the real `KatranVoice.c`, `KatranOsc.c`,
`KatranTables.c` through `extract.py`, a stubbed `seq_isRunning()`,
`GetRngValue()` (seeded), `fxbuf_arenaView()` (a synthetic arena) and
`sampleMemory_*`. The laws are listed in §9.

#### T-5 — `tools/dsp_test/katran_cpu.py`. **Add.** The `tetrakt_cpu.py`
pattern: the eight oscillator loops, `kat_delayRead`, `kat_coreRender`,
`kat_outRender` (loop path counts), `Katran_calcVoiceAsync` +
`Katran_calcVoiceSyncBlock` entries (block path counts), the all-in figure
and gate G-CPU1 (≤ 380), G-CPU2 (arena ≤ PWM sibling per loop), and the
per-loop limits of §4 N-4. Exit 1 on a failed gate.

#### T-6 — `tools/dsp_test/Makefile`. **Add.**
- Line 64 (`.PHONY` list): add `katran katran_tables katran_cpu_check`.
- Line 69 (`all:`): add `katran katran_tables`.
- Line 530 (`special_tags:`), after the `make_init_tkt.py --check` line:
  `cd $(ROOT) && python3 tools/katran/make_init_kat.py --check`
- After the `tetrakt` / `tetrakt_tables` / `tetrakt_cpu_check` block (≈ line
  1110): the three Katran targets, built as the Tetrakt's (the `$(EX)`
  extraction loop over `KatranTables.h/.c`, `KatranOsc.h/.c`,
  `KatranVoice.h/.c`; `AlkalineControl.h` and `ColloidTables.h` bodies;
  `oscPwmTables` and `oscPwmWarp` host copies), plus two break builds
  (§9: `katran_break_cycle`, `katran_break_run`).

#### T-7 — `tools/diagnostic_tools/decode_devlogs.py`, lines 1125–1126. **Modify.**

```python
            # S103 P1: the Katran (9)
            types = {0: "drm", 1: "snr", 2: "cym", 3: "hat", 4: "alk", 5: "col", 6: "tkt",
                     7: "clx", 8: "alx", 9: "kat"}
```

#### B-1 — `tools/dsp_test/baselines/s103/`. **Add** before any code
change: the base image's `elf_hotloops.py` mixer record and inlined set,
`elf_s099_check` and `elf_p4_check` snapshots, `link_budget.py` output.
G-CPU3 compares against these.

---

## 8. Card image

#### C-1 — `SD_CARD/Instrument/Katran/init.kat`. **Add**, written by
`make_init_kat.py` (never by hand). Values in §11.

---

## 9. Host laws (`modules/katran.c`)

| Law | Checks |
|---|---|
| KA-RUN1 | `run` r < 127: from gr = 1 with no kick, gr reaches −60 dB at T(r) ± 5 % (r = 0, 40, 80, 126) |
| KA-RUN2 | `run` 127, armed, transport running: gr stays 1 for 10 s of blocks |
| KA-STOP | a stop edge at `run` 127: gr decays at T(126); a start edge re-arms |
| KA-BOOT | after init, gr = 0 until the first kick or start edge |
| KA-KICK | a kick re-arms within 7 blocks; kE reaches −60 dB at 80 ms ± 10 %; `kik` 63 gives the full burst and no swell; `kik` 127 the full swell; the feedback never passes the clamps |
| KA-BND | worst case for 60 s (fbk 127, `spr` 0, res 127, `kik` 127 at every block, arena form on a garbage arena, every drive): every ring sample and output is finite and inside int16; no NaN |
| KA-SILENT | after `run` 0 and silentBlocks ≥ 72, both outputs are exactly 0 |
| KA-CE | CE cycle within ± 2 % of katCeBlocks at `chs` 0; at `chs` 127 every cycle within × 2^±1 and the peak within 0.5..1; a trigger leaves CE level and stage unchanged |
| KA-CYC | the row (wtb frame and arena window) changes only on the sample where the phase wraps or resets; a pos sweep mid-cycle leaves the cycle's samples equal to a still pos |
| KA-SYNC | `sth` 127: no reset in 10 s; a step in D above th resets OSC A on that sample only |
| KA-ARENA | every read index is inside the arena for every pos, speed and spread (including the uncapped increments); a DC step through the DC block falls below 1 % in 100 ms; `dmp` 127 attenuates 5 kHz by ≥ 20 dB |
| KA-GUARD | a full-scale garbage arena at `lvl` 127: the guarded read peak ≤ 0.7 within 1 block |
| KA-FADE | `Katran_requestFade()` every block: quiet within 30 blocks; requests stopping: gr returns to its run state within 70 blocks |
| KA-SPR | `spr` 63: equal times; 127: L/R times ± 12 %; 0: xfb at 0.6 and times equal |
| KA-VCA | `vca` 63: g1, g2 at the patch values (0.67, 0.38 × the offset term) |
| KA-F3 | `cut` 63 passes 20 Hz–18 kHz within 1 dB; L and R coefficients identical (C1) |
| KA-QUIET | `Katran_isQuiet()` is 0 while gr > 0 and 1 after 72 blocks of gr = 0 |
| Break: `katran_break_cycle` | the row switch moved to block start: KA-CYC must fail |
| Break: `katran_break_run` | gr not applied to the feedback term: KA-RUN1 must fail |

---

## 10. Order of work and gates

| Step | Work | Gate (all must pass before the next step) |
|---|---|---|
| 0 | B-1: baseline records of the base image | records committed under `tools/dsp_test/baselines/s103/` |
| 1 | T-2, N-5, N-6: the tables and their generator | `gen_kat_tables.py --check` |
| 2 | N-1, N-3, N-4: the voice header and the oscillator loops; T-4 with KA-OSC, KA-CYC, KA-SYNC, KA-ARENA, KA-GUARD | the laws pass; the break `katran_break_cycle` is caught |
| 3 | N-2: the voice; the rest of T-4 | every §9 law; both breaks caught |
| 4 | **The CPU checkpoint.** N-7, N-8, then H-1, IM-1…IM-25, FB-1/2, ME-1…7, ST-1, MK-1; build `make all && make img`; T-5 | **G-CPU1 ≤ 380, G-CPU2, G-CPU3, G-RAM1** (sizeof ≤ 1,200); ITCM unchanged (15,424 B); flash headroom ≥ 16 KiB (`link_budget.py`). Over 380: apply L1 (§1.5) and re-measure. **Over 395 after L1: stop and ask you** |
| 5 | T-1, T-3, C-1, T-6, T-7 | `make -C tools/dsp_test special_tags` (registry, tags, every init file); `make -C tools/dsp_test all` |
| 6 | Regressions | `elf_s099_check` 15/15, `elf_p4_check` exact, the Colloid and Tetrakt host modules unchanged, the mixer's hot loops equal to B-1 |
| 7 | **Device** (your test): §12 | G-CPU4; the behaviour list in §12 |

---

## 11. `init.kat` values (the starting point; set by ear in Step 3)

From D§1.3 and P§8; every key present (a missing key loads 0).

| Key | Value | | Key | Value |
|---|---:|---|---|---:|
| `osc1_pitch_coarse` | 36 (32', an octave under B) | | `kat_b_coarse` | 48 (16') |
| `osc1_pitch_fine` | 63 | | `kat_b_fine` | 64 |
| `osc1_position` | 46 | | `kat_b_pos` | 60 |
| `kat_spread_a` | 0 | | `kat_b_level` | 55 |
| `osc1_wave` | 2 (Rec under pwm) | | `kat_level_a` | 50 |
| `osc1_type` | 2 (pwm) | | `kat_noise` | 0 |
| `kat_source` | 0 (off) | | `kat_sync` | 60 |
| `kat_damp` | 40 | | `kat_drive` | 56 |
| `kat_run` | 127 | | `kat_wet` | 57 |
| `kat_kick` | 40 | | `kat_time` | 74 |
| `kat_ce1_cycle` / `_skew` / `_depth` | 90 / 63 / 0 | | `kat_feedback` | 61 |
| `kat_ce2_cycle` / `_skew` / `_depth` | 100 / 63 / 63 | | `kat_drift` / `kat_track` | 0 / 0 |
| `kat_chaos` | 20 | | `kat_loop_cutoff` / `_reso` / `_type` | 53 / 30 / 0 |
| `kat_plfo_rate` / `_mod` / `_level` | 74 / 57 / 51 | | `kat_low_cut` / `kat_out_cut` | 20 / 63 |
| `kat_vca` | 63 | | `instrument_vol` / `instrument_pan` | 100 / 63 |
| `kat_spread` / `kat_width` | 63 / 64 | | LFO and velocity rows | off, as `init.col` |

---

## 12. Device test (yours, after Step 6)

1. **CPU:** a `DEV_MODE_LOGGING` image, one Katran at its dearest settings
   (`typ` pwm Rec, `src` off, `spr` 127, `res` 127, every send active); the
   `p` probe's median ≤ 18.4k (G-CPU4). Then `src` AB (it must not be
   dearer). Then a six-slot Scene: 4 Katrans + 2 Advanced Colloids and 2
   Katrans + 4 Colloids, against the 144k cap (S101 P2 §A.7).
2. **Behaviour:** boot silent; first step plays; `run` 0..126 dies out at
   the expected rates; `run` 127 holds; transport stop dies out slowly; a pad
   while stopped plays; a Scene change over a drone fades without a click;
   a roll fill does not cut the drone; `src` A/B/AB reads the arena (with
   and without a Delay/Reverb running); CE1/CE2 cycles at `chs` 0 and 127.
3. **Storage:** Load lists Katran; save/load an instrument; Scene and Bank
   round-trip; AutoSave restores a Katran slot; the Effect share shrinks by
   8,832 B per Katran and the Delay's maximum time follows.

---

## 13. Statements this work would make out of date (for you to log)

- `MEMORY.md`: the type list (Katran, the eighth type, Basic, token `kat`);
  voice units (a two-unit claimant); FxBuffer read access by an Instrument
  (`fxbuf_arenaView()`); the label counts (short 249 / 256).
- `INSTRUMENTS_DSP_REFERENCE.md`: instrument count; §3.1 engine table and
  union; §3.2 dispatchers; a Katran section; §9.2 (the arena view; the
  reserved effect rows and `click` cells 8–11).
- `EFFECTS_BUS_REFERENCE.md`, `EFFECTS_MODULES_DSP_REFERENCE.md`: the Effect
  share shrinks by two units per Katran; an Instrument may read arena bytes.
- `BANK_PRESET_ARCHITECTURE.md`, `FILESYSTEM_SPEC.md`: the eighth type, its
  token, extension and folder.
- `STORAGE_SRAM_MANIFEST.md`: the union member; the units; +2 B .bss; flash.
- `CPU_USE_DSP_AUDIT.md`: Katran's cost items and its device figure.
- `MENU_UI_SPEC.md`: Katran's pages; tags 4 and 10.
- `S103_KATRAN_NOISE_SYNTH.md`: §6 (CPU figures superseded by §1 here);
  §7.2 (`tim` tops out at ≈ 40.6 ms, C3); §8.2 (`spr` no longer spreads F3,
  C1).

---

## 14. Risks during implementation

| # | Risk | Mitigation |
|---|---|---|
| R1 | The re-priced estimate is still low | G-CPU1 at Step 4, before any tool or card work; L1 pre-approved; stop at 395 |
| R2 | The mixer's loops move (LTO) | `-fno-lto` objects, `noinline` entries; G-CPU3 against B-1 |
| R3 | `sizeof(KatranVoice)` over 1,200 | the in-slot reserves of §2.1; no new RAM |
| R4 | The short-label table nears its 256 cap (249) | noted for later types; no action now |
| R5 | The arena read is loud in ways the host model misses (real arena contents) | the guard (KA-GUARD); `src` off in `init.kat`; device step 2 |
| R6 | A Colloid change creeps in through shared code | nothing in the Colloid's files changes; Katran restates `col_svfControl()` and the PWM reads; Colloid host laws in Step 6 |
| R7 | `instrumentManager_ampEnvelopeQuiet()` gains a side effect (the fade request) | documented in IM-14; its only callers are the two deferred-apply workers (checked: `presetManager.c:2218`, `:2380`) |

---

## 15. Session implementation note (2026-11-24) - STOPPED AT THE CPU GATE

**Status: the code is implemented and builds; the host laws pass; G-CPU1
fails and the plan's stop rule is reached. No device run was made.**

### 15.1 What landed

- New engine: Core/DSP/Instruments/Katran/ (KatranVoice.h/.c,
  KatranOsc.h/.c, KatranTables.h/.c, KatranParameters.h/.c), with the
  plan's comment blocks beside every declaration and definition.
- Integration: InstrumentManager.h/.c (all IM-1..IM-25), FxBuffer.h/.c
  (fxbuf_arenaView()), menu.h, MenuLabels.c, MenuText.h, menu.c,
  storageTypes.h, Makefile (include path, SRCS, DSP_SRCS, three
  -Ofast -fno-lto rules).
- Tools: tools/katran/gen_kat_tables.py and make_init_kat.py;
  SD_CARD/Instrument/Katran/init.kat; tools/dsp_test/modules/katran.c,
  katran_cpu.py, Makefile targets; check_special_tags.py KAT;
  decode_devlogs.py type 9; baselines/s103/.

### 15.2 Gates measured

| Gate | Result |
|---|---|
| Build (make all && make img) | passes |
| check_special_tags.py | 488 rows, 0 mismatches |
| make -C tools/dsp_test special_tags | passes (every init file, Katran tables) |
| tools/dsp_test/modules/katran.c | all laws pass (KA-BOOT .. KA-BND) |
| Colloid and Tetrakt host modules | unchanged, pass |
| elf_s099_check | 15/15 exact |
| elf_p4_check | the same 17 missing / 5 renamed loops as the pre-Katran base image (pre-existing baseline drift, not caused here) |
| G-CPU3 (mixer hot loops) | **0 grew**, 1 shrank, 40 equal |
| link_budget.py | 694,472 / 753,664 B, 59,192 B headroom; ITCM 15,424 B unchanged |
| G-RAM1 (sizeof KatranVoice <= 1,200) | passes the static assert |
| **G-CPU1** | **FAIL: loops 469 + per block 2,102/32 = 534.7 (gate 380)** |

Per-loop counts on the linked image (elf_looppath.py --cond-heads
--in-range): delayRead 53, coreRender 111 (x2), outRender 47, OSC B
20/30/26/23 (plain/pwmD/warp/arena), OSC A 38/53/47/38.

At the schedule's 1.52 cycles per instruction, 534.7 instructions a
sample is about 26.0k cycles a block against the 18.6k device gate and
the 19,620 Basic share.

### 15.3 The ladder

- L1 (pre-approved) was applied: F3 is now a one-pole LP/HP tilt per
  side with one coefficient set, and out_cut 63 is an exact pass-through.
  It saved 14.1 a sample (548.8 -> 534.7), not the estimated 30.
- The result is past the plan's 395 stop line even after L1. The only
  further steps in the schedule are L2 (drop the noise exciter, the
  32-sample tap, or a core), which the plan says must come back to you.

### 15.4 Implementation deviations, for review

- The arena guard peaks are updated in the sync block, after each read
  loop, rather than in the async: blkA is shared by the two cores, so the
  async cannot see both copies' last block.
- out_cut 63 is an exact pass (the L1 form); the plan's "LP fully open"
  could not pass 18 kHz within 1 dB through a 15.96 kHz 2-pole.
- The delay, kick, chaos, patch-LFO and oscillator controls that are
  small enough were inlined by -Ofast; katran_cpu.py adds only the
  out-of-line helpers that survive (and their internal loop iterations).
- Float-to-int stores truncate (kat_sat16f), as the Colloid does; lrintf
  was an out-of-line call in every per-sample loop.
- The host law for spr 127 expects the correct ratio
  (1 + 0.12) / (1 - 0.12); the plan's text wrote 1 + 2 x 0.12.

### 15.5 What is needed next

1. **Your decision on the CPU.** Options: (a) accept a device probe first
   (the 1.52 CPI is the Tetrakt's; this loop is FP-dense and the real
   CPI may be lower), (b) authorize L2 cuts, or (c) a design change (for
   example one loop handling both cores with the shared work hoisted).
2. No further feature work should be based on this engine until the
   gate is resolved.

---

## 16. Assessment of the implementation (2026-11-24, review of `0669d578`)

**Scope:** a source review of commit `0669d578` ("katran implementation")
against this schedule. Files read in full: `KatranVoice.c`, `KatranOsc.c`,
the InstrumentManager diff, `KatranParameters.c` (rows, pages, asserts,
`katranParameters_cellInactive()`), `katran_cpu.py`, the `baselines/s103/`
records.

**What could not be checked here:** there is no ARM toolchain or linked ELF
on the review machine, so the per-loop counts in §15.2 are taken as
recorded, not re-measured. Two questions about how they were counted are
in §16.4.

### 16.1 Verdict

| Item | Finding |
|---|---|
| **The stop at the CPU gate** | **Correct.** 534.7 is far past the 395 stop line; L1 was the only pre-approved step, and it was applied |
| **Integration (H-1, IM-1…IM-25, FB-1/2, ME-1…7, ST-1, MK-1)** | **Matches the schedule.** The IM-13 argument-order fix (claim into a local, then read ordinal 1) is in. IM-14's fade request, IM-22's duck exemption and IM-25's typ branch are as specified |
| **Behaviour (run, kick, stop, boot, fade, CEs, cycle-start rows, sync)** | Matches §4 N-2 apart from the items in §16.2 |
| **Deviations listed in §15.4** | All acceptable: the guard peaks in the sync, `out_cut` 63 as an exact pass, truncating stores, and the corrected `spr` ratio law |
| **Bugs** | **Four found** (§16.2). Three of them produce wrong audio; none is caught by the current host laws |
| **The CPU estimate** | **Wrong by ≈ 40 %** (376 estimated, 534.7 measured). The causes are in §16.3. After the code-level savings available without a feature change (§16.5), Katran is still ≈ 6–14 % **over** the Basic share, so **a design decision is needed (§16.6)** |

### 16.2 Bugs (fix before any device run)

| # | Where | Defect | Effect | Fix |
|---|---|---|---|---|
| **B1** | `KatranVoice.c:754–758`, `kat_delayRead()` | `idx = (w + n) − t` is negative whenever the write index is behind the delay time (for `w < T`: ≈ 87 % of the ring cycle at `tim` 127). `(int32_t)idx` truncates toward zero, so the integer part is the ceiling and `frac` lies in (−1, 0]: the read **extrapolates** from `ring[i0]` along the next sample's slope instead of interpolating between `i0 − 1` and `i0` | The timed tap is wrong by up to one sample's difference on most samples: added high-frequency error, overshoot past ±32,767 into the saturating store, and a pitch slightly off its law | Make the index positive before the conversion: `idx = (float)(w + n + KAT_RING) − t` (t ≤ 2,040 < 2,048, so idx > 0). §16.5 O1 replaces the read with a Q16 form, which fixes it as well |
| **B2** | `KatranOsc.c:126` (`kat_bPwmD`), `:228` (`kat_aPwmD`) | `kat_read(row, ph) + sign × kat_read(row, q)` reaches ±65,534 and is stored as `(int16_t)` (OSC B also through `& 0xFFFFFFFE`) without saturation or a halving. It **wraps** | Under `typ` pwm (the `init.kat` setting) the exciter and the ring modulator wrap: hard discontinuities whenever the two reads add past full scale (Saw form often; Rec form near narrow widths) | Halve the sum (`>> 1`) before the int16 store. The Colloid has no wrap here because it keeps the same sum in int32 buses (`ColloidOsc.c:416–419`) and scales later. Then apply the width level table (the "PWM D level" item below) |
| **B3** | `KatranOsc.c:291` (`kat_aArena`), `:176` (`kat_bArena`) | the conditioned arena value (`kat_cond()`, DC block then damping, × guard) is stored with a bare `(int16_t)` cast. The DC block's high-pass of a full-scale step overshoots to ≈ 2× before the guard (which acts a block later) catches it | Wraps on garbage and stale-state reads, the form's most common input | Saturate: `kat_sat16(s0)`, and for B `kat_sat16(s) & ~1` |
| **B4** | `kat_runControl()`, `KatranVoice.c:435–437` | the K1 re-arm adds `KAT_REARM_STEP` (0.15) to `gr` with no ceiling; `grStep` is computed from the unclamped value and only the sync's end-of-block clamp (`:1285`) pulls `gr` back to 1 | During a re-arm from `gr` > 0.85 the per-sample ramp exceeds 1 for one block: the loop gain goes above unity briefly on every kick into a sounding drone | `newGr = fminf(newGr, 1.0f)` before `grStep` |

**Smaller items (not bugs, for the same fix pass):**
- **Two RNG reads a sample.** `kat_coreRender()` calls `kat_rngRead()` once
  per core (`:819`). The plan specified one read split into L/R halves
  (P1 §1.4 "noise 4"). Each read is a peripheral load (AHB2, the RNG data
  register), slower than a memory load, and with no DRDY wait the two cores
  often read the same word. See O2 for a zero-RAM fix.
- **The PWM D level.** The Colloid scales the two-read form by the width's
  level table (`oscPwmLevelRecQ14` / `SawQ14`). Katran does not, so the
  exciter and ring depth change level as `pos` sweeps. Apply it per block
  (one multiply in the loop, or fold it into `exc` / `bLvl`).
- **Host laws.** None of §9's laws exercises the fractional delay or the
  oscillators' output range, which is why B1–B3 passed. Add **KA-DLY** (a
  sine through the delay at a non-integer T, compared with the analytic
  value over a whole ring cycle, `w < T` included) and **KA-RANGE** (every
  oscillator form and the arena form at full level: no sample changes sign
  against its unclipped value). Add a break build for each.

### 16.3 Why the estimate was wrong

Measured (§15.2) against the §1.4 estimate, instructions a sample:

| Part | Est. | Measured | Main causes (from the source) |
|---|---:|---:|---|
| L1 delay reads | 30 | 53 | float index arithmetic per sample (int→float, subtract, truncate, fraction), three int16→float conversions, the float lerp, a saturating float→int store |
| OSC B (dearest) | 31 | 30 | as estimated |
| OSC A ×2 (dearest) | 74 | 106 | the sync test reads `blkB` and `blkD` through `v->` and writes `v->syncPrev[c]` every sample; the row switch is a branch, not a select |
| Core ×2 | 126 | 222 | four int16→float conversions with a scale each; two `kat_sat16f()` (convert + two-sided clamp) per core; the per-sample `(c == 0u) ?` output select; five gain ramps + three coefficient ramps; the second RNG read |
| Output (after L1) | 55 | 47 | as estimated (L1 saved 14.1, not 30: the SVF it replaced cost less than estimated) |
| **Loops** | **320** | **469** | |
| Per block | 56 | 65.7 | close |
| **Total** | **376** | **534.7** | |

The estimate priced the arithmetic and missed the **format traffic**: every
stage boundary is an int16 block, so each stage pays a load, a conversion and
a scale on the way in, and a scale, conversion and clamp on the way out. The
Colloid keeps its buses in int32/float, so its measured per-loop costs, which
the estimate borrowed, do not include this traffic.

### 16.4 Two questions about the measurement

| # | Question | Why it matters | How to settle |
|---|---|---|---|
| M1 | Is `kat_delayRead`'s 53 **one core's** sample or both cores'? The function is an outer loop over two cores around the per-sample loop; `katran_cpu.py` adds `longest(kat_delayRead)` once | If 53 is one core's inner loop, the true per-sample cost is 106, and the total is ≈ 588, not 534.7 | `elf_loopdump.py --sym kat_delayRead`: one inner loop body = one core-sample. If so, multiply by 2 in `katran_cpu.py` |
| M2 | Did `-Ofast` unroll any per-sample loop by 2 (one body = two samples)? | An unrolled body's count is two samples' worth; adding it as one sample doubles that part | the same loop dump: check the induction step (`n += 2`) |

Also note `katran_cpu.py`'s per-block sum:
- it adds the block entries of all eight oscillator functions, though only
  three calls run in a block (one B, two A);
- it counts `kat_svfControl` × 3, though F3 no longer calls it after L1.

Both over-count, by ≈ 3–4 a sample together. Fix the script so the record
is exact before the next decision.

### 16.5 Savings available without a feature change (est., from the measured counts)

| # | Change | Where | Saves (est.) |
|---|---|---|---:|
| O1 | **Fixed-point delay read:** T in Q16 (`uint32_t`), index = `((w + n + KAT_RING) << 16) − Tq`, `i = idx >> 16 & mask`, `frac = idx & 0xFFFF`, integer lerp `a + (((b − a) × frac) >> 16)`, `(a_t + dry) >> 1` store. All integer, no conversion; **fixes B1** | `kat_delayRead()` | 20–25 |
| O2 | **One RNG read a sample:** core L reads the 32-bit RNG word, uses the low 16 bits and stores the high 16 bits in `right[n]`; core R reads its noise from `right[n]` before it overwrites `right[n]` with its output (same index, read first). Zero RAM | `kat_coreRender()` | 4 + one peripheral stall |
| O3 | **Two specialised core loops** (an `always_inline` body with a constant core index, called from two `noinline` wrappers): no per-sample `(c == 0u)` select, and each core ramps only its own VCA gain | `kat_coreRender()` | 8 |
| O4 | **Conversion and saturation:** `__SSAT(x, 16)` for every int16 store (one instruction instead of the two-sided compare); the D, A, B and output scales folded into the per-block gains (`tg`, `exc`, `bLvl`, `xfb`, `fb`, the output) so the loads are `vcvt` only | core, out, delay | 20–28 |
| O5 | **Loop state in registers:** `syncPrev`, the DC/damp states, `blkD[c]` / `blkB` base pointers as locals in the oscillator loops, written back once at the end; the row switch as a select | `KatranOsc.c` (A and arena loops) | 16–22 |
| O6 | `katran_cpu.py` per-block fixes (§16.4) | the record | 3–4 |
| | **Total** | | **≈ 70–90** |

**After O1–O6:** ≈ 445–465 instructions a sample, i.e. **21.1k–22.8k cycles a
block** at 1.485–1.53. That is still **8–16 % over the 19,620 Basic share**,
and 13–23 % over the 18.6k ceiling. Under M1 it would be ≈ 50 higher.

A device probe alone (§15.5 option a) cannot close this. Even at a CPI of
1.3, below anything measured in this tree, 445 instructions is 18.5k:
inside the share, but with no margin, before M1 is settled.

### 16.6 The decision this needs from you

All figures are est., from the measured counts after O1–O6 (≈ 455 centre).
Cycles at 1.485–1.53.

| Option | Change | Instructions a sample | Cycles a block | Against Basic 19,620 | Sound cost |
|---|---|---:|---:|---|---|
| **K1** | **Single core** (K-D1 reversed): one TORCIDO / F1 / F2 / delay loop, one OSC A. Stereo from the delay's two taps at L/R times (`spr`), the VCA pair's asymmetry, and the width. Saves the second core (≈ 95), the second OSC A (≈ 45), half the delay reads (≈ 15), one F1 control and one delay control (≈ 5) | ≈ 295 | ≈ 14.0–14.5k | **26–29 % under**: room to restore the sub (+12) and the F3 SVF (+15) and still be ≈ 20 % under | the two cores no longer re-clock independently; the stereo becomes tap spread rather than two chaotic loops |
| **K2** | **Dual core, one shared OSC A** (reset by either core's delay crossing), the 32-sample tap and the noise exciter removed | ≈ 390 | ≈ 18.5–19.1k | 2.7–5.7 % under: **outside** the 5–10 % band, no margin | the exciters no longer beat against each other (`dtn` keeps only a frame/width spread); no dry-tap resonance (K-D5); no noise |
| **K3** | **Dual core at half rate** in the loop: TORCIDO, F1, F2 and the ring write at 22 kHz; the oscillators and delay reads at the same rate; the output stage at full rate with a 2× interpolator | ≈ 270–300 | ≈ 12.8–14.7k | 25–35 % under | the loop's bandwidth stops at 11 kHz (darker, less aliasing from the drive); the shortest comb 2.9 ms instead of 0.73 ms; the largest code change |
| **K4** | **Advanced class** for the dual core (as is after O1–O6) | ≈ 455 | ≈ 21.6–22.3k | over Basic; ≈ 0–2 % **over** the Advanced share (21,803) | none, but at most two per Scene, and no room left for the later insert effect. **Not viable as is** |

**Recommendation: K1, single core.**
- It is the only option that lands inside your 5–10 % band with margin after
  estimates this far off. K2 has no margin, and K4 does not fit even
  Advanced.
- It keeps every row, page and behaviour of the plan.
- It frees enough to restore the sub-oscillator and the resonant output
  filter.
- The stereo moves from "two loops" to "one loop, two taps": still the
  patch's asymmetric L/R (the VCA pair), plus the delay spread and width.

K3 is the alternative if keeping two independent loops matters more than
the loop's top octave. It is a larger change, and its cost needs its own
spike before it is relied on.

### 16.7 Next steps (in order)

1. **Fix B1–B4**, and add KA-DLY, KA-RANGE and their break builds (§16.2).
   These are needed whatever you decide.
2. **Settle M1/M2** with `elf_loopdump.py`, and correct `katran_cpu.py`
   (§16.4).
3. **Apply O1–O5** and re-measure. This is the honest baseline for the
   decision, and O1 also closes B1.
4. **Your decision on §16.6** (K1 recommended). Then revise this schedule's
   §1, §4 N-1/N-2/N-4, §9 and §10 for the chosen form before more code.
5. Only then the device probe (G-CPU4), to calibrate the CPI for this kind
   of loop.

**Not yet applied:** the short-label merges of `S103_P2_SHORT_NAMES_MERGE.md`
(Tier B reuses for Katran's labels) are still pending your decision; the
code uses the 21 new labels as scheduled.
