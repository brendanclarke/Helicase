# Session 094 — Voice DSP Adjustments, SRAM D-Cache Discovery, Cycle Budget, Memory Allocation

```
DATE: 2026-11-11 / 2026-11-12
SESSION GOAL: Implement crash wave removal, PWM oscillator type, Alkaline
  VCA overdrive + tone, LFO sync fix and rate table; then investigate and
  resolve the Alkaline CPU overrun (the SRAM D-cache discovery), adopt a
  cycle-based budget, implement QADD and memory allocation speed-ups, fix
  the PWM position law, and expand the Alkaline wave set.
COMPLETED: All planned P1 voice DSP adjustments (S0–S6 + device test),
  the SRAM D-cache fix (−14.8 % on a six-voice Scene), a cycle-based
  budget replacing the instruction model, QADD (stereo dry add 41→26),
  Cymbal/HiHat reclassified as Basic, CrumpBit resonant SVF restored,
  dedicated copy/clear scratch (+1,932 B SRAM1), AsyncFATFS cache 8→16
  sectors (+4,224 B SRAM1), discardable data-read hint, write-back stack
  region, PWM position law fix (128 distinct widths from tables), and
  Alkaline Tri wave under both wav and pwm.
VERIFIED ON HARDWARE: Yes. Two full device runs (P1 uncached, P2 cached),
  P3 device run, and P4 device listening.

CHANGES THIS SESSION:
  (per-file details in each phase section below)

KNOWN ISSUES INTRODUCED:
  - Alkaline tone (S094 P1 S4) costs 6 per sample in spill reloads. AS-2
    (remove tone) is proposed in P2 §2 but status is open at session close.
  - Alkaline worst case ≈ 261.5 per sample (accepted, P9-D8), marginally
    above the 261.3 Basic ceiling.
  - ITCM at 13,984 / 16,384 B (85.3 %). Future ITCM consumers need
    sign-off.

KNOWN ISSUES RESOLVED:
  - SRAM D-cache was effectively off since Session 013 (MPU region 0 had
    S=1 shareable; Cortex-M7 with CACR.SIWT=0 treats that as uncacheable).
    Fixed by clearing the S bit. −14.8 % on same Scene.
  - LFO sync/rate bug: changing lfo_sync on the menu did nothing until frq
    or the tempo moved. Fixed with IM_SPECIAL_LFO_SYNC writer.
  - PWM pos law: pos p and 128−p gave identical harmonic content (half the
    knob wasted). Fixed with position-indexed tables: pos 0 = 50 %,
    pos 127 = narrowest.
  - CrumpBit grn/rvb lost their resonant SVF in S091 P5 F1 (replaced with
    one-pole). Restored in P2 §23 now that cycle budget has room.

NEXT SESSION RECOMMENDED GOAL: S095 already underway (colloid voice,
  space effects).

BLOCKERS: None.

CRITICAL REMINDERS FOR NEXT SESSION:
  - The fused oscillator read is inside the B2R loop (ALK_FUSED_OSC 1);
    any Alkaline oscillator change must stay bit-identical there.
  - osc_setTableFreq() only — never osc_setFreq() for Alkaline.
  - ALK_NET_STATES is 6 (r6). Any model change regenerates the tables
    (tools/alkaline/gen_alk_tables.py) and re-checks P3 §9.3.
  - OSC_SAMPLE_START is now 5 (was 6 at start of S094, was 8 before S093).
  - Catalogue is v3 (magic LXC3). v2 cards read STALE and are reinstalled.
  - An interpolation slot claim must be in the async, never in the sync
    render (item 79: moves mixer register allocation).
  - Budget is now cycle-based (DWT_CYCCNT probe), not instruction-based.
    97 % average cap of 156,705 cycles per block.
  - MPU region 0 is non-shareable (S=0) for SRAM D-cache. Do not set S=1.
  - MPU region 2 covers SRAM2 for write-back stack. Do not remove without
    understanding the IWDG capsule SRD.
  - AFATFS cache is 16 sectors. Discardable hint is on regular-file data
    reads only; directory reads must remain non-discardable.
  - Stall record part 1 is 32-bit layout (decode_devlogs.py updated).
```

---

## Contents

1. [P0 — Planning Drafts: Reductions, PWM, Alkaline Post-FX](#1-p0--planning-drafts-reductions-pwm-alkaline-post-fx)
2. [P1 — Voice DSP Adjustments (S0–S6)](#2-p1--voice-dsp-adjustments-s0s6)
3. [P2 — Alkaline Savings: SRAM D-Cache, Cycle Probe, Budget](#3-p2--alkaline-savings-sram-d-cache-cycle-probe-budget)
4. [P3 — Memory Allocation Speed-ups](#4-p3--memory-allocation-speed-ups)
5. [P4 — PWM Position Law Fix and Alkaline Wave Additions](#5-p4--pwm-position-law-fix-and-alkaline-wave-additions)
6. [Bug Fixes and Findings](#6-bug-fixes-and-findings)
7. [Build and Budget Summary](#7-build-and-budget-summary)
8. [Files Changed](#8-files-changed)
9. [Specification Documents Updated](#9-specification-documents-updated)
10. [Hardware Test Results](#10-hardware-test-results)
11. [Decisions Record](#11-decisions-record)

---

## 1. P0 — Planning Drafts: Reductions, PWM, Alkaline Post-FX

**Sources:** `S094_P0_SAMPLE_VOICE_REDUCTIONS.md`,
`S094_P0_PWM_IMPLEMENTATION.md`, `S094_P0_ALKALINE_POST_FX.md` (all
draft proposals, no firmware code changed).

### 1.1 Sample and voice reductions draft

Analysed crash wave removal, the wavetable sync blend cost, Alkaline
reductions, and the first frozen Advanced ceiling.

**Key finding:** the per-voice worst cases are set by the `slp` loops and
the crash loop, not by the P8 wavetable readers. P8's shorter readers lower
no voice's dearest setting. The interpolation pool's per-slot ceiling is
the `itp` fade (45), not any blend.

| Item | What it frees (measured) | Recommendation |
|---|---|---|
| Remove crash | Drum −3, Snare −3, Cymbal/HiHat −6 per sample; ITCM −≈540 B; flash −32,769 B | Do it, with card sample from ROM data |
| Sync blend | Nothing in any worst case (Drum `snc` with a slot totals 52 against dearest 81); 536 B ITCM | Keep |
| Alkaline as Advanced | Advanced ceiling measured at **≈292.9** (Cymbal/HiHat dearest, 3 VDIV) | Price the option |

**Advanced ceiling (§4.7.1):** first frozen measurement. Dearest
Cymbal/HiHat: T+D chain, both modulators on crash, `slp` FM carrier.
Loops 251.5 + per block 1,006/32 = 31.4 + SRR 10 = **≈292.9, 3 VDIV**.
Without crash: ≈286.9.

**Beyond distortion (§4.8):** wavefolder 15–25 (measured), 4-stage phaser
≈32, flanger ≈38 (loops measured standalone). Each instrument slot reserves
1,264 B; the Alkaline uses 548 B, so ≈700 B free for state. A phaser or
256-point flanger fits; a chorus does not.

### 1.2 PWM oscillator type draft

Designed PWM as `typ` 4 on all five instruments. Hybrid mechanism: phase
warp (W) for Sin and Tri, two-read (D) for Saw and Rec. Integer Q15/Q14
interpolation.

**Key finding:** integer interpolation pays for the second read / phase
warp. A float-interpolation two-read costs 30; integer costs 23 — the
saving pays for the PWM work.

| | Plain (D) | Plain (W, sine/table) | FM | FM ramp | Ring | Sync |
|---|---:|---:|---:|---:|---:|---:|
| Measured | 23 | 22/23 | 30–31 | 31 | 31 | 35 |
| Role ceiling | 30 (crash) | 30 | 50/51 | 51 | 42 | 41 |

**Alkaline:** fused mode 3 in `dl_renderB2Body()`. Three read forms
measured: both interpolated 219, A interpolated B truncated **212**, both
truncated 204. The 212 form (read form 2) fits with ≈5 to spare.

### 1.3 Alkaline post-filter drive and tone draft (revision 3)

**Decided design (P9-A′):** overdrive the 303's own VCA (the OTA). The
B2R loop already models an OTA with a smooth tanh. Overdrive scales the
OTA input per block. Tone is a one-pole low-pass in the `c` form on the
OTA output, inside the loop.

| | Loops (one/blend) | Per block | Whole voice, no slot | Slot held |
|---|---:|---:|---:|---:|
| Before P9 (P8 r3) | 213 / 228 | 1,378 | 256.1 | 271.1 |
| **P9 decided** | **219 / 234** | **≈1,361** | **≈261.5** | **≈276.5** |

- **A3 (drive tables):** drive gain and make-up from 128-entry flash
  tables, replacing two `alk_exp2f()` calls. Saves 40 per block (−1.25 per
  sample). Bit-identical at drive 0; ≤1 ulp elsewhere.
- **A4 (one expo pass):** dropped (P9-D3). Its +1.03 per sample accepted.
- **Neutral settings (P9-D7):** Overdrive 0 / Tone 127 = bit-identical to
  before. Tone 127 = `toneC = 0.0f`, so `z = o + 0·(z − o) = o` exactly.
  The `c` form was chosen over the `k` form because `k = 1` is not exact
  (`z + (o − z)` can differ from `o` in the last bit).
- **Rows appended:** 29 `instrument_drive` and 30 `alk_tone`. Mix page:
  vol pan ton drv. The filter page's `alk_drive` labels unchanged (P9-D9).
- **The slot-held figure (≈276.5)** accepted (P9-D2); pool ceiling (itp
  fade 45) not affected.

---

## 2. P1 — Voice DSP Adjustments (S0–S6)

**Sources:** `S094_P1_VOICE_DSP_ADJUSTMENTS.md` (overall plan revision 3),
`S094_P1_IMPLEMENTATION.md` (code schedule).

One implementation pass: S0 baselines → S1 LFO → S2 crash removal → S3
drum-side PWM → S4 Alkaline post-FX → S5 Alkaline PWM → S6 ceiling
assessment → device test.

### 2.1 S0 — Baselines and Alkaline golden capture

No firmware change. Recorded `baselines/s094p1/s0/`: loops, per block
(`control_cost.py`), `link_budget.py`.

**Alkaline golden capture:** `tools/dsp_test/modules/alkaline.c` gained
`--golden` mode and law AL-OD0. The capture renders Saw/Sqr × drive bytes
0, 64, 127 through the S092 P4 pattern, a Res sweep, and wavetable renders
with and without a slot. Every later step re-runs AL-OD0 against this
capture: drive 0 must be byte-identical, drive 64/127 ≤ 2 LSB (A3's 1-ulp
table entries).

### 2.2 S1 — LFO sync fix and rate table

**The bug:** `lfo_sync` was a plain `ROW_MENU` byte store. `phaseInc` was
recomputed only by the rate writer (`IM_SPECIAL_LFO_RATE`) or on a tempo
change (`seq_setBpm()` → `lfo_recalcSync()`). A menu `snc` change did
nothing until `frq` or the tempo moved. On load, the rate was applied with
the previous sound's sync byte.

**The fix:**
- New tag `IM_SPECIAL_LFO_SYNC` (writer 23; count becomes 24 of 32). The
  writer calls `lfo_setSync()`, which stores the byte and recomputes
  `phaseInc`.
- The five `lfo_sync` rows become `ROW_MENU_SPECIAL`.
- **Sync wins (L-D2):** `lfo_calc()` uses a rate scale of 1.0 while
  `sync` is set. Implemented as a select, not a branch (constant cost).

**The rate table (D-3):** generated `const float lfoRateHz[128]` (512 B
flash), replacing the cubic `200 × ((f+1)/128)³`. `LFO_MAX_F` removed.

| Part | `frq` | Rates |
|---|---|---|
| Stopped | 0 | 0 Hz: `phaseInc` 0; LFO holds its phase |
| Slow and LFO range | 1..62 | 0.01 Hz (100 s) .. 7.71 Hz; step eases from 2.73 to 1 semitone |
| Semitone grid | 63..127 | MIDI notes 0..64 (8.18 Hz .. 329.63 Hz E4), 1 semitone apart |

Top stops at E4 so that the ×2 rate-modulation overlay keeps `phaseInc`
below 2³² and leaves ≥4 LFO values per cycle at the 1,378 Hz block rate.

**Files:** `lfoRateTable.c` (generated), `lfo.h`, `lfo.c`,
`InstrumentManager.h/.c`, five parameter files (`DrumParameters.c`,
`SnareParameters.c`, `CymbalParameters.c`, `HiHatParameters.c`,
`AlkalineParameters.c`), `check_special_tags.py`, `make_init_alk.py`.

**Generator:** `tools/plots/gen_lfo_rate_table.py` with `--check`.

**Host test:** `tools/dsp_test/modules/lfo.c` with laws LF1–LF5 and two
break builds (store-only sync writer, no sync-wins select).

### 2.3 S2 — Remove the crash wave

**`crashSample[32769]`** (8-bit, 32,768 points plus guard) removed from
firmware. `CRASH` enum, all crash oscillator loops (FM, ring, sync, plain),
`Samples.c/.h` removed.

**Card sample:** `SD_CARD/samples/02_crash.wav` generated by
`tools/samples/make_crash_wav.py` from the ROM data before the array was
deleted. 16-bit mono, 32,769 frames, `smpl` loop 0..32,767, conversion
`(u − 127) × 256` exact. Provenance: freesound, X-Act, CC0.

**Code changes:**
- `OSC_STOCK_LAST` → `NOISE`; `OSC_SAMPLE_START` 6 → **5**.
- `Cym` removed from `waveformNames` (count 6 → 5).
- `osc_positionIsPhase()` loses `CRASH` term.
- `osc_syncClockInc()` loses crash `<<5` rule.

**Savings:** Drum −3, Snare −3, Cymbal/HiHat −6 per sample; ITCM −≈540 B;
flash −32,769 B.

**Scan tool:** `tools/samples/scan_wave_refs.py` lists card files whose
stored wave bytes change meaning (no remap code, standing rule).

### 2.4 S3 — PWM on Drum, Snare, Cymbal, HiHat

**Type:** `typ` 4 `pwm` appended. `OSC_TYPE_MODLIST` 4 → 5 (never
stored). `OSC_BIND_PWM` 3.

**Per wave:**
- Sin, Tri: phase warp (W), no DC term (finding 1: the sine table starts
  at its trough, so both half-cycles have mean zero).
- Saw: two-saw sum (D).
- Rec: two-saw difference (D), the band-limited pulse.
- Integer Q15 interpolation, Q14 gain. `SSAT16` output clamp on D forms.

**Loops:** 15 in `.itcm.s094p1`: {D, W-table, W-sine} × {plain, FM,
FM ramp, ring, sync}. All at or below their role ceilings.

**Tables (flash):** `oscPwmRecipTable[128]` (512 B, warp reciprocals),
`oscPwmLevelQ14[64]` (128 B, D-form level).

**Width control:** slew ≤2 steps per block (`OSC_PWM_SLEW_STEPS`); trigger
snaps; start phase 0. Width from `position`/`positionApplied` (0 B RAM).

### 2.5 S4 — Alkaline VCA overdrive + tone

**Implemented as P9-A′ (post-FX draft revision 3):**

**Ladder changes (`DiodeLadder.c/.h`):**
- `otaIn` (the OTA input gain, per block), `toneC` (the tone `c`
  coefficient), `toneZ` (tone state): 12 B inside `DiodeLadder`, inside
  the slot reserve.
- The OTA output line becomes `z = o + toneC * (z − o)` in all three fused
  modes (0, 1, 2) and the unfused loop.
- At `toneC = 0`, `z = o` exactly.

**Voice changes (`AlkalineVoice.c/.h`):**
- Per block: `otaIn = gain[vcaDrive] * DL_INV_2VT`, makeup fold, `toneC`
  from table.
- A3: `driveGain` and `makeup` from two 128-entry tables (replacing two
  `alk_exp2f()` calls; −40 per block).

**Tables:** new `AlkalinePostTables.c/.h` (generated by
`tools/alkaline/gen_alk_post_tables.py`): overdrive gain, overdrive makeup,
tone `c`, A3 drive gain, A3 drive makeup. `OVD_OCT` = 5.0 (gain ×1..×32),
`OVD_MAKEUP_OCT` = −2.5.

**Rows appended:**
- 29 `instrument_drive` "Voice" / "Overdriv" / `drv` (K1: shared key with
  Alkaline branch in the `IM_SPECIAL_INSTRUMENT_DRIVE` writer).
- 30 `alk_tone` "Voice" / "Tone" / `ton`, plain `ROW`, `DTYPE_0B127`,
  default 127.
- Mix page: vol pan ton drv.

**Post-filter overdrive range (§10.2):** `OVD_OCT` was raised from 4.0 to
5.0 during the P2 investigation (gain ×1..×16 → ×1..×32), and
`OVD_MAKEUP_OCT` from −2.0 to −2.5.

**AL-OD0 gate:** byte-identical at drive 0; ±2 LSB at drive 64/127.

### 2.6 S5 — PWM on the Alkaline

**Type:** `ALK_TYP_PWM 2u`, `ALK_TYP_COUNT` 3 (was 2).

**Ladder:** fused mode 3 in `dl_renderB2Body()`, wrapper
`DiodeLadder_renderB2OscPwm()`. Read form 2 (A interpolated, B truncated;
204 instructions). Measured on S4's ladder (which has the tone/overdrive
changes): fit confirmed.

**Waves:** Saw (the two-saw sum/difference pulse) and Sqr. The 303 square's
tilt (`alkSqrTable`) is not reproduced; the PWM pulse is flat. Sqr scaled
by 1.40 (finding 3: the 303 square RMS is 1.41–1.43× a flat pulse).

**Width:** slew ≤2 per block; non-legato note snaps; phase runs free (the
303's VCO).

### 2.7 S6 — Ceiling assessment

Measured on the final P1 image with `drum_ceiling.txt` method.

**Basic worst case (basic_worst.txt):** Drum dearest **257.8** per sample.
Basic gate remains 261.3 (the Alkaline's ≈261.5 is the accepted
exception).

**Advanced ceiling (advanced_ceiling.txt):** the first frozen Advanced
ceiling, measured before the P2 reclassification.

### 2.8 Findings made while writing the P1 schedule

| # | Finding |
|---|---|
| 1 | The sine table starts at its trough (`sine_table[0]` = −32,767): a −cos. The warp splits rising and falling halves, both with mean zero. **P-D8 (sine DC correction) is not needed.** |
| 2 | Table shapes: `sawTable` descends (jump at 0), `recTable` high first half, `triTable` starts at trough. Largest step: saw 32,767, rec 32,753, tri 207, sine 51. Two-read difference in Q15 stays inside int32. |
| 3 | Alkaline 303 square RMS 1.41–1.43× flat pulse (rows 3–5). PWM `Sqr` scaled by 1.40. |
| 4 | Warp octave lift rounded, not ceiled. Ceiling lifts a whole octave for 1.6 % compression (pos 63). Triangle harmonics fall as 1/n², so rounded lift (0 from pos 46 to 82) adds aliasing far below the table's own. |
| 5 | A3 golden capture: ±1 ulp at non-zero drive bytes. AL-OD0 allows ±2 LSB at drive 64/127, byte-identical at drive 0. |
| 6 | `make_init_alk.py` gains `ROW_MENU_SPECIAL` in its `MORPHABLE` set. |
| 7 | Crash ROM is CC0 (freesound, X-Act). Card file can carry it freely. |
| 8 | Post-FX tables in separate `AlkalinePostTables.c/.h`, so `gen_alk_tables.py --check` stays untouched. |
| 9 | Sine/table split costs ITCM, not cycles. Finding 1 removes the DC term, so W loops have no extra per-sample work. |

---

## 3. P2 — Alkaline Savings: SRAM D-Cache, Cycle Probe, Budget

**Source:** `S094_P2_ALKALINE_SAVINGS.md`.

**Trigger:** after P1 device test, three Alkaline instances pinned the CPU
and the interface lagged badly; a fourth would not run. Investigation
discovered it was not Alkaline-specific.

### 3.1 The cycle probe (§10)

`DEV_MODE_LOGGING`-only probe using `DWT_CYCCNT` around each slot's
async+sync. Per-slot block-rate and render timing. Seven mixer sections.
Trace stage `p` (the probe record). 94 B RAM in logging builds only.

**Probe v2 (§14):** refined after the first run. Per-slot cycle counts,
mixer section breakdown, owner records for filesystem operations.

### 3.2 Where the Alkaline stood (§1, measured on P1 image)

| Path | Loop | Per block | Whole voice |
|---|---:|---:|---:|
| `wav`/`wtb`, no slot | 221 | 1,367 | **263.7** |
| `wtb` with slot | 234 | 1,367 | 276.7 |
| `pwm` (read form 2) | 215 | 1,330 | 256.6 |
| *Drum dearest (S6)* | | | *257.8* |

**S4 added +8 on the one-row loop (213→221).** The three new live floats
(`otaIn`, `toneC`, tone state) pushed the loop into spill reloads: stack
loads/stores 30→41. The tone costs 6 per sample; the overdrive about 2.

### 3.3 Hypothesis: instruction fetch + dependent arithmetic

The Drum's per-sample oscillator loops run from ITCM (no cache, no wait
states). The Alkaline's whole path runs from flash behind the 16 KB
instruction cache (7 wait states at 216 MHz). One Alkaline's per-block
working set is ≈7.3 KB; the mixer alone is 12.6 KB. Straight-line control
code (the 4.2 KB async), used once per block, is likely refetched every
block. Plus 3 serial VDIV per sample (14 cycles each on M7).

### 3.4 SRAM D-cache discovery (§13)

**Root cause:** MPU region 0 (`clocks.c`) had `S=1` (shareable) since
Session 013. On Cortex-M7 with `CACR.SIWT=0`, shareable means the D-cache
is effectively **OFF** for all SRAM. This was set in S013 and never
noticed.

**Fix:** clear the S bit. MPU region 0 becomes non-shareable write-through
(`TEX 000, C1 B0, S0`).

**Result (§17):** **−14.8 % on the same Scene.** Six Alkalines went from
pinning the CPU to 80.9 %. The worst Scene measured (four Alkalines, two
HiHats, CrumpBit on grain, compressor on) averages **84.0 %**, peak
94.7 %.

| Measured | Uncached (run 1) | Cached (run 2) |
|---|---:|---:|
| FluxTime, whole block | 148,965 (95.1 %) | **126,979 (81.0 %)** |
| Six Alkalines | — | 126,853 (80.9 %) |
| Four Alk + two HH + grain | — | **131,561 (84.0 %)** |

All voices benefited: Alkaline −20.2 %, drums −10–15 %. The Drum loops
were already in ITCM, but their per-block work, the mixer, and the filter
SVF all run from flash and read SRAM data that is now cached.

### 3.5 Post-filter overdrive doubled (§10.2)

During the P2 investigation, the overdrive range was doubled:
- `OVD_OCT` 4.0 → **5.0** (gain ×1..×16 → ×1..×32).
- `OVD_MAKEUP_OCT` −2.0 → **−2.5**.
- `gen_alk_post_tables.py` updated; tables regenerated.

### 3.6 QADD (§22)

`bufferTool_satAdd32()` (the output add in the mixer's dry and send loops)
replaced with a single `__QADD()` intrinsic. Bit-exact: `QADD` saturates
int32 in hardware, which is the same operation.

| Loop | Before | After |
|---|---:|---:|
| Stereo dry add | 41–42 | **26** instructions/sample |
| Stereo with send | 56–57 | **33–34** |

### 3.7 Cymbal and HiHat moved to Basic (§22)

`INSTRUMENT_FLAG_BASIC` set on `CymbalParameters.h` and `HiHatParameters.h`.
**No current type is Advanced.** The Advanced machinery is preserved for
future types (first candidate: an Alkaline variant with wavefolders/tone).

### 3.8 CrumpBit SVF restored (§23)

The resonant TPT SVF was removed from CrumpBit `grn` and `rvb` in S091 P5
F1, replaced with a one-pole. Restored now that the cycle budget has room.

- `grn` measured at **180.8 instructions/frame**.
- Effect cap 170 → **181**.
- About 16,280 cycles against the 16,634 Effect share.
- `CrumpBitEffect.c/.h`: one-pole code removed; resonant SVF for all
  sub-types; filter state kept across a sub-type switch.
- `CrumpBitParameters.c/.h`: `grn`/`rvb` typ/res display mapping removed.
- `CrumpBitTables.c/.h`: `crumpBit_onePoleK` table removed.

### 3.9 Budget adopted (§21)

**97 % average cap** (152,004 of 156,705 cycles per block). Replaces
S089's instruction-based gates.

| Item | Cycles/block | % |
|---|---:|---:|
| Outside mixer (menu, filesystem, background) | 4,701 | 3.0 |
| Shared mixer (control, setup, compressor; worst) | 12,784 | 8.2 |
| Effect (CrumpBit with SVF) | 16,634 | 10.6 |
| 4 Basic slots × 19,620 | 78,480 | 50.1 |
| 2 Advanced slots × 21,803 | 43,606 | 27.8 |
| Unallocated (slack) | 500 | 0.3 |

### 3.10 Open items at P2 close

| # | Item | Status |
|---|---|---|
| AS-2 | Remove tone from Alkaline | Open |
| AS-5 | Move Alkaline ladder to ITCM | Open |
| AS-7 | Staggered control (A1) | Open |
| AS-8 | One expo pass (A4) | Open |
| AS-11 | Single int16→float conversion | Open |
| AS-12 | Deduplicate the CrumpBit filter loops | Open |

---

## 4. P3 — Memory Allocation Speed-ups

**Source:** `S094_P3_MEMORY_ALLOCATION_SPEEDUPS.md`. All four items
implemented in one build.

### 4.1 A — Dedicated copy/clear scratch (+1,932 B SRAM1)

**Problem:** copy/clear borrowed the 9,000 B name cache
(`fs_list_cache_name`) to use 2,027 B of it. The loan blocked: AutoSave,
settings, and Load requests refused while copy held the loan; the paste
name phase waited for idle facade and no Load/Save; the browser index was
cleared on return.

**Fix:** `filesystem_copy_clear_scratch_t` (1,932 B, `.bss`, SRAM1):
`sources[161]` (322 B), `names[161][9]` (1,449 B), `remap[161]` (161 B).
The loan stays only for sample/wavetable install (runs with audio
suspended).

**Changes:** `filesystem.c/.h` (the static, validity flag, accessor pair),
`copyClearService.c/.h` (uses dedicated scratch; D53 wait removed).

### 4.2 B — AsyncFATFS sector cache 8→16 (+4,224 B SRAM1)

`AFATFS_NUM_CACHE_SECTORS` 8 → **16** in `asyncfatfs.c`. With 5–6 handle
pins on 8 sectors, only 2 free slots remained for directory/FAT lookups.
16 sectors gives 10 free slots.

### 4.3 C — Discardable hint for regular-file data reads (0 B)

In `afatfs_fileRetainCursorSectorForRead()`: set
`AFATFS_CACHE_DISCARDABLE` when the file is `AFATFS_FILE_TYPE_NORMAL` and
not a directory. Only on first cache. Streamed file data stops evicting
directory and FAT sectors. Directory reads excluded (they must stay LRU).

### 4.4 D — Write-back stack region (0 B)

MPU region 2 added over SRAM2 for the stack: `TEX 001, C1 B1, S0`
(write-back, write-allocate, non-shareable). SRD `0x01` protects the
bottom 32 KB sub-region for the IWDG capsule.

Speeds up the voices' spill stores: write-back allocates lines on store,
so the spill stores in the Alkaline's B2R loop (14 per sample) and the
mixer's inlined renders hit cache instead of memory.

### 4.5 Stall record

Part 1 changed to 32-bit layout. `decode_devlogs.py` updated for probe
records and the new stall layout.

### 4.6 SRAM1 after A+B

**87,404 B free** in the DEV link (measured). Still leaves room for the
Pattern reservation at `PAT_STACK_SIZE` 384 (69,632 B).

---

## 5. P4 — PWM Position Law Fix and Alkaline Wave Additions

**Sources:** `S094_P4_PWM_OSC_INVESTIGATION.md` (investigation and
decisions), `S094_P4_IMPLEMENTATION.md` (code schedule).

### 5.1 The problem

Under `typ pwm`, `pos` *p* and `128 − p` produce identical harmonic
content. The two ends of the knob sound the same. Measured on every
position: Saw and Rec 0.00 dB difference, Sin 0.02 dB, Tri 0.63 dB above
−40 dB.

**Root cause:** the width was defined symmetrically about `pos` 64 (the
stock wave). Moving either direction narrows the pulse. The mirror symmetry
was inherent and never examined as a knob law.

### 5.2 The fix: position-indexed tables

**New law:** `pos` 0 = 50 % (the unmodified wave), `pos` 127 = the
narrowest pulse. 128 distinct widths on a monotonic curve.

| Form | `pos` 0 | `pos` 127 |
|---|---|---|
| D (Rec/Saw) | offset `d` from 128-entry table `pwm_d_table[128]` | narrowest |
| W (Sin/Tri) | wPh, RA, RB from `pwm_w_table[128]` (3 × uint32) | narrowest |
| Level | `pwm_level_q14[128]` | smallest |
| Lift (Tri) | `pwm_lift[128]` | highest |

**Narrowest widths:** Rec **0.39 %** (5 of 1,024 points), Saw/Tri
**2.54 %**, Sin **6.25 %**.

**Implementation:** `osc_pwmPlan()` reads position-indexed tables instead
of computing from `p` and `128 − p`. No per-sample cost change; no RAM.
Flash: ≈+3.5 KB net (tables replace the symmetric computation).

**Generator:** `tools/plots/gen_pwm_tables.py` with `--check`.

**Alkaline:** `alk_renderPwm()` reads from the same tables. The same law.

### 5.3 Alkaline wave additions

**`wav` gains Tri (index 2):** 0 per sample in the existing fused loop.
The triangle table read uses the same `ph >> 22`, mask `0x3FF` as Saw.
Appended to preserve stored values. `ALK_WAVE_COUNT` 2 → 3. `alkWaveNames`
updated.

**`pwm` gains Tri (index 2):** a new fused ladder mode for the warp form.
Appended. Gated by spike measurement against the Alkaline's dearest path.

**Not added:** Sin (the ladder filters highs so strongly that warped sine
and warped triangle would sound the same), Noi (no pulse-width form of
noise), Rec (the existing Sqr is Rec).

### 5.4 Investigation tools

- `tools/dsp_test/modules/pwm_dump.c`: PWM waveform dump for the
  investigation (renders one cycle of every PWM combination at every pos).
- `tools/plots/plot_s094p4_pwm.py`: generates the investigation plots
  (shapes, harmonics, law comparison).

---

## 6. Bug Fixes and Findings

### SRAM D-cache disabled since S013

**Symptom:** three Alkaline instances pinned the CPU. Investigation
revealed all voices ran 2.3–2.6× the instruction model.

**Root cause:** MPU region 0 had `S=1` (shareable) since Session 013. On
Cortex-M7 with `CACR.SIWT=0`, shareable means the D-cache is effectively
OFF for SRAM.

**Fix:** clear the S bit (`clocks.c`). −14.8 % on same Scene.

### LFO sync/rate desync

**Symptom:** changing `lfo_sync` on the menu did nothing until `frq` or
the tempo moved. On load, the rate was applied with the previous sound's
sync byte.

**Root cause:** `lfo_sync` was a plain byte store (no recomputation of
`phaseInc`).

**Fix:** `IM_SPECIAL_LFO_SYNC` writer calls `lfo_setSync()`. All five
`lfo_sync` rows become `ROW_MENU_SPECIAL`.

### PWM position mirror symmetry

**Symptom:** `pos` *p* and `128 − p` sound the same (half the knob
wasted).

**Root cause:** the width was defined symmetrically about 64, and the
mirror was inherent by construction for all four waveforms.

**Fix:** position-indexed tables. `pos` 0 = 50 %, `pos` 127 = narrowest.

### Alkaline tone spill cost

**Symptom:** the three new live floats (`otaIn`, `toneC`, tone state)
pushed the B2R loop into spill reloads: stack loads/stores 30→41.

**Impact:** tone costs 6 per sample, overdrive about 2. The Alkaline's
whole voice went from 256.1 to 263.7 with the tone.

**Status:** AS-2 (remove tone) proposed in P2 §2; open at session close.
The tone is in the tree.

### CrumpBit build note

During AS-13 implementation, `menu.c` used `ALK_WAVE_COUNT` without
including `AlkalineVoice.h` (part of P4 S7 being edited concurrently).
Fixed by adding the include in a scratch copy for measurement.

---

## 7. Build and Budget Summary

### Final build (end of S094)

| | Value |
|---|---:|
| Branch | `dev-ph7-alkaline` |
| HEAD | `264c1e34` |
| ITCM | **13,984 / 16,384 B** (was 12,128 at start of S094) |
| SRAM1 free (DEV link) | 87,404 B |

### ITCM breakdown

| | ITCM (B) |
|---|---:|
| Start of S094 (P8 image) | 12,128 |
| S2: crash removal | −≈540 |
| S3: PWM loops (15 in `.itcm.s094p1`) | +≈2,400 |
| **End of S094** | **13,984** |

### Cycle budget (adopted, P2 §21)

97 % average cap: 152,004 of 156,705 cycles per block.

| Item | Cycles/block | % |
|---|---:|---:|
| Outside mixer | 4,701 | 3.0 |
| Shared mixer | 12,784 | 8.2 |
| Effect (CrumpBit SVF) | 16,634 | 10.6 |
| 4 Basic × 19,620 | 78,480 | 50.1 |
| 2 Advanced × 21,803 | 43,606 | 27.8 |
| Slack | 500 | 0.3 |

### Voice costs (instruction model, for reference)

| Voice | Loops + per block/32 | Notes |
|---|---:|---|
| Drum dearest (basic_worst) | 257.8 | S6 measurement |
| Alkaline wav/wtb, no slot | 263.7 | With tone; 261.5 estimated at plan time |
| Alkaline pwm | 256.6 | |
| Alkaline wtb, slot held | 276.7 | Accepted |
| Cymbal/HiHat dearest | ≈286.9 | Without crash (P2 reclassification) |

### AlkalineVoice struct

548 B (unchanged from S093 P6), inside the 1,264 B reserve.
`DiodeLadder` gained 12 B (`otaIn`, `toneC`, `toneZ`), still within.

---

## 8. Files Changed

### New files

| File | Description |
|---|---|
| `Core/DSPAudio/lfoRateTable.c` | Generated LFO rate table (128 entries) |
| `Core/DSPAudio/AlkalinePostTables.c/.h` | Generated post-FX tables (overdrive, tone, A3 drive) |
| `SD_CARD/samples/02_crash.wav` | Card crash sample (16-bit, 32,769 frames) |
| `tools/samples/make_crash_wav.py` | Generates crash card sample from ROM data |
| `tools/samples/scan_wave_refs.py` | Lists card files affected by wave index change |
| `tools/alkaline/gen_alk_post_tables.py` | Generates AlkalinePostTables |
| `tools/plots/gen_lfo_rate_table.py` | Generates LFO rate table |
| `tools/plots/gen_pwm_tables.py` | Generates PWM position-indexed tables |
| `tools/plots/plot_s094p4_pwm.py` | PWM investigation plots |
| `tools/dsp_test/modules/lfo.c` | LFO host test (laws LF1–LF5, break builds) |
| `tools/dsp_test/modules/pwm_dump.c` | PWM waveform dump for investigation |

### Modified files

| File | Change |
|---|---|
| `Core/Hardware/clocks.c` | MPU region 0: S bit cleared (AS-6 D-cache); MPU region 2 added (write-back stack, SRAM2) |
| `Core/DSPAudio/Oscillator.h` | `CRASH` removed; `OSC_STOCK_LAST` → `NOISE`; `OSC_SAMPLE_START` 5; `OSC_TYPE_PWM` 4u; `OSC_BIND_PWM` 3; `OSC_TYPE_MODLIST` 5 |
| `Core/DSPAudio/Oscillator.c` | All crash cases removed; 15 PWM loops in `.itcm.s094p1`; PWM dispatchers and helpers; position-indexed table reads |
| `Core/DSPAudio/Samples.c/.h` | `crashSample` removed |
| `Core/DSPAudio/BufferTools.h` | `bufferTool_satAdd32()` → `__QADD()` |
| `Core/DSPAudio/DiodeLadder.c/.h` | `otaIn`, `toneC`, `toneZ` in struct; VCA overdrive + tone in loop (all fused modes); fused mode 3 for PWM |
| `Core/DSPAudio/AlkalineVoice.c/.h` | `ALK_TYP_PWM` 2u, `ALK_TYP_COUNT` 3; A3 table lookups; post-FX controls; `wav`/`pwm` gain Tri; `ALK_WAVE_COUNT` 2→3 |
| `Core/DSPAudio/AlkalineParameters.c` | Rows 29 (`instrument_drive`) and 30 (`alk_tone`) appended; mix page vol pan ton drv; `ROW_MENU_SPECIAL` macro added |
| `Core/DSPAudio/lfo.h` | `LFO_RATE_COUNT` 128; `lfoRateHz[128]` extern; `LFO_MAX_F` removed; `lfo_setSync()` contract |
| `Core/DSPAudio/lfo.c` | `lfo_setFreq()` reads table; `lfo_calc()` sync-wins select; `lfo_setSync()` contract comment |
| `Core/DSPAudio/CrumpBitEffect.c/.h` | One-pole removed; resonant SVF for all sub-types; filter kept across switch |
| `Core/DSPAudio/CrumpBitParameters.c/.h` | `grn`/`rvb` typ/res display mapping removed |
| `Core/DSPAudio/CrumpBitTables.c/.h` | `crumpBit_onePoleK` table removed |
| `Core/DSPAudio/mixer.c` | Cycle probe (DEV only); seven mixer sections |
| `Core/DSP/Instruments/InstrumentManager.h` | `IM_SPECIAL_LFO_SYNC` (writer 23, count 24) |
| `Core/DSP/Instruments/InstrumentManager.c` | LFO sync writer case; Alkaline branch in `IM_SPECIAL_INSTRUMENT_DRIVE` |
| `Core/DSP/Instruments/Drum/DrumParameters.c` | `lfo_sync` → `ROW_MENU_SPECIAL` |
| `Core/DSP/Instruments/Snare/SnareParameters.c` | `lfo_sync` → `ROW_MENU_SPECIAL` |
| `Core/DSP/Instruments/Cymbal/CymbalParameters.c` | `lfo_sync` → `ROW_MENU_SPECIAL` |
| `Core/DSP/Instruments/Cymbal/CymbalParameters.h` | `INSTRUMENT_FLAG_BASIC` set |
| `Core/DSP/Instruments/HiHat/HiHatParameters.c` | `lfo_sync` → `ROW_MENU_SPECIAL` |
| `Core/DSP/Instruments/HiHat/HiHatParameters.h` | `INSTRUMENT_FLAG_BASIC` set |
| `Core/Menu/MenuText.h` | `Cym` removed from `waveformNames`; `pwm` added to `oscTypeNames`; `alkTypeNames` gains `pwm`; `alkWaveNames` gains `Tri` |
| `STM32F765VIHx_FLASH.ld` | `.itcm.s094p1` subsection pinned |
| `Core/Filesystem/filesystem.c` | Dedicated copy/clear scratch; loan removed for copy/clear (kept for install only); stall record 32-bit layout |
| `Core/Filesystem/filesystem.h` | Scratch accessor prototypes |
| `Core/Filesystem/copyClearService.c/.h` | Uses dedicated scratch; D53 wait removed |
| `Core/AsyncFATFS/asyncfatfs.c` | `AFATFS_NUM_CACHE_SECTORS` 16; discardable hint for regular-file reads |
| `Makefile` | `lfoRateTable.c`, `AlkalinePostTables.c` sources |
| `tools/dsp_test/Makefile` | `lfo` and `lfo_rate_table` targets |
| `tools/dsp_test/check_special_tags.py` | `lfo_sync` → `LFO_SYNC` |
| `tools/alkaline/make_init_alk.py` | `ROW_MENU_SPECIAL` in `MORPHABLE` |
| `tools/diagnostic_tools/decode_devlogs.py` | Updated for probe records and 32-bit stall layout |

### Removed

| Item | Reason |
|---|---|
| `crashSample[32769]` (`Samples.c/.h`) | C-D1: card sample replaces it; −32,769 B flash |
| `CRASH` enum and all crash oscillator loops (FM, ring, sync, plain) | C-D1: ≈540 B ITCM |
| `crumpBit_onePoleK` table | SVF restored; one-pole removed |
| `LFO_MAX_F` macro | Replaced by `lfoRateHz[128]` table |

---

## 9. Specification Documents Updated

| Document | Changes |
|---|---|
| `INSTRUMENTS_DSP_REFERENCE.md` | PWM type (§6.1), crash removal (stock wave list, §6.1.2), Alkaline post-FX rows and chain (§5.5), Alkaline wave additions (Tri), per-voice worst cases (§8.2), LFO sync special row |
| `CPU_USE_DSP_AUDIT.md` | PWM loop items, SRAM D-cache discovery, QADD item, cycle budget (replaces instruction gates), CrumpBit SVF restored |
| `EFFECTS_MODULES_DSP_REFERENCE.md` | CrumpBit SVF restored on `grn`/`rvb`; one-pole section removed; cap 170→181 |
| `FILESYSTEM_SPEC.md` | `OSC_SAMPLE_START` 5; `02_crash.wav` card sample; `typ` 4 `pwm`; Alkaline rows 29–30 |
| `STORAGE_SRAM_MANIFEST.md` | ITCM 13,984; SRAM1 +6,156 B (copy/clear scratch + AFATFS 16 sectors); SRAM1 free 87,404 B; MPU region 2 (write-back stack); `DiodeLadder` +12 B |
| `OSC_INTERP_AUDIT.md` | PWM never claims a slot; `itp` old carrier under PWM; crash removal (pool ceiling 90→84) |
| `COPYCLEAR_UTILITIES.md` | Dedicated scratch replaces name-cache loan; D53 wait removed |
| `MODULE_INTERCHANGE_SPEC.md` | `IM_SPECIAL_LFO_SYNC` (writer 23, count 24); Alkaline `instrument_drive` branch |
| `DEV_MODES.md` | Cycle probe (DWT_CYCCNT, trace stage `p`, seven mixer sections) |
| `ASYNCFATFS_REFERENCE.md` | 16 sectors; discardable hint |
| `MENU_UI_SPEC.md` | PWM type in oscillator type menu; Alkaline mix page (vol pan ton drv); `alkWaveNames` Tri |
| `ALKALINE_MODULE_REFERENCE.md` | Post-FX (overdrive + tone), PWM fused mode 3, Tri wave, A3 tables, `AlkalinePostTables.c/.h` |

---

## 10. Hardware Test Results

### P1 device test (the uncached run)

PWM worked on all instruments. Width sweeps, narrow pulses, all Drum modes
(`fm`, `mix`, `rng`, `snc`), Alkaline overdrive/tone sweeps, LFO sync
on/off, crash card sample after install.

**Problem discovered:** three Alkaline instances pinned the CPU. This
triggered the P2 investigation.

### P2 device runs

**Run 1 (uncached, §11):** the cycle probe's first capture. Revealed the
entire block was 95.1 % of capacity.

**Run 2 (cached, §17):** after the SRAM D-cache fix. Same Scene at 81.0 %.
Six Alkalines at 80.9 %. Worst Scene 84.0 %, peak 94.7 %.

**Post-QADD + write-back stack (§22):** further improvement on the output
add loops.

### P3 device test

Memory allocation speed-ups verified: copy/clear no longer blocks
Load/Save; AFATFS with 16 sectors shows faster directory lookups.

### P4 device listening

PWM position law confirmed: `pos` 0 = unmodified wave, `pos` 127 =
narrowest. Full range used. Alkaline Tri under `wav` and `pwm` confirmed.

---

## 11. Decisions Record

### Voice DSP (P1)

| # | Decision | Phase |
|---|---|---|
| D-1 | Basic stays gated at 261.3 (plus P9-D8's ≈0.2 for Alkaline) | P1 |
| D-2 | Advanced ceiling measured after implementation (S6); the measured worst case frozen | P1 |
| D-3 | LFO rates: `frq` 0 = stopped (true zero); `frq` 1 = 100 s cycle; semitone grid 63..127 | P1 |
| D-4 | One pass for S0–S6; compressor is a later phase | P1 |
| P-D5 + ITCM | PWM on all five uses (plain, FM, FM ramp, ring, sync), +≈2.9 KB ITCM approved | P1 |
| L-D2 | Sync wins: `lfo_calc()` uses rate scale 1.0 while synced | P1 |
| P9-D9 | No relabel: `alk_drive` keeps labels; VCA drive is `instrument_drive` (K1) | P1 |
| C-D1 | Remove crash, with card sample | P1 |
| C-D2 | Card file: `02_crash.wav`, 32,769 frames, `smpl` loop 0..32,767, generated from ROM | P1 |
| C-D3 | No remap code for stored values (standing rule) | P1 |
| P-D1 | Hybrid mechanism: warp for Sin/Tri, two-read for Saw/Rec | P1 |
| P-D2 | `Saw` under `pwm`: two-saw sum | P1 |
| P-D3 | Width range: `pos` clamped to 1..127 | P1 |
| P-D4 | Separate sine and table warp copies (crash removed, so 28 > 27) | P1 |
| P-D7 | Width slew ≤2 steps per block | P1 |
| P-D8 | Sine DC correction not needed (finding 1: trough start) | P1 |
| P-D9 | No modulator PWM entries | P1 |
| P9-D1 | VCA overdrive + low-pass tone in the loop + A3 | P1 |
| P9-D2 | Slot-held voice ≈276.5 accepted | P1 |
| P9-D3 | A4 (one expo pass) dropped; +1.03 accepted | P1 |
| P9-D4 | Ship starting curves, tune by ear | P1 |
| P9-D5 | Mix page: vol pan ton drv | P1 |
| P9-D6 | Tone: low-pass only (bipolar 266.6, over ceiling) | P1 |
| P9-D7 | Overdrive 0 / Tone 127 = bit-identical to before | P1 |
| P9-D8 | ≈261.5 accepted as the new worst case | P1 |

### SRAM and budget (P2)

| # | Decision | Phase |
|---|---|---|
| AS-6 | Clear the S bit in MPU region 0 (SRAM D-cache fix) | P2 |
| AS-10 | Replace `bufferTool_satAdd32()` with `__QADD()` | P2 |
| §22 | Cymbal and HiHat moved to Basic; no current type is Advanced | P2 |
| §21 | 97 % cycle-based budget adopted | P2 |
| §23 | CrumpBit resonant SVF restored on `grn`/`rvb` | P2 |
| §10.2 | Post-filter overdrive doubled: `OVD_OCT` 4.0→5.0 | P2 |

### Memory allocation (P3)

| # | Decision | Phase |
|---|---|---|
| A | Dedicated copy/clear scratch (+1,932 B SRAM1) | P3 |
| B | AsyncFATFS 8→16 sectors (+4,224 B SRAM1) | P3 |
| C | Discardable hint for regular-file data reads | P3 |
| D | Write-back stack region (MPU region 2, SRAM2) | P3 |

### PWM fix and Alkaline waves (P4)

| # | Decision | Phase |
|---|---|---|
| P4 | PWM pos law: pos 0 = 50 %, pos 127 = narrowest, position-indexed tables | P4 |
| P4 | Alkaline `wav` gains Tri (index 2); preserves stored values | P4 |
| P4 | Alkaline `pwm` gains Tri (index 2); no Sin (ladder filters it) | P4 |
| P4 | Option A for tables: all tabled (no per-block computation) | P4 |
| P4 | Narrowest: Rec 0.39 %, Saw/Tri 2.54 %, Sin 6.25 % | P4 |
