# S094 P1 — Voice DSP Adjustments: Overall Plan

- **Status:** overall plan, **revision 3** (S094, 2026-11-11). Every decision
  is taken (§0.2). **No firmware code is changed by this document.** The code
  schedule is `S094_P1_IMPLEMENTATION.md`.
- **Shape (user):** **one implementation pass** covering everything except the
  master compressor. The compressor is a separate, later phase, done by ear
  (§9).
- **Sources:**
  - `S094_P0_SAMPLE_VOICE_REDUCTIONS.md` (the reductions draft);
  - `S094_P0_PWM_IMPLEMENTATION.md` (the PWM draft);
  - `S094_P0_ALKALINE_POST_FX.md` (the post-FX draft, revision 3, decided);
  - user brief and decisions, 2026-11-11.
- **Binding rules that apply to every step:**
  - S089: no per-sample cost may rise without an equal or larger measured
    saving;
  - S093: costs include per-block work / 32, with VDIV counted separately;
    constant cost; the longest path counts;
  - new or enlarged RAM (ITCM counts as RAM) needs sign-off: bytes, region,
    owner;
  - new ITCM code goes in a new `.itcm.sNNN` subsection after the last one;
  - no backward compatibility: no migration, aliasing or converters;
  - no step of this plan, or of a schedule made from it, edits `MEMORY.md` or
    `knowledge_files/`. Statements that go out of date are listed in §11.

---

## 0. Summary

### 0.1 The pass

| Step | Work | DSP cost | RAM / ITCM / flash |
|---|---|---|---|
| **S0** | Baselines; Alkaline golden capture | — | — |
| **S1** | LFO: sync fix ("sync wins"); new rate table (`frq` 0 = stopped, 1 = 100 s, semitone grid on top) | 0 per sample; per block measured (L-D2) | flash +512 B |
| **S2** | Remove the crash wave; card sample | **saves** 3 on Drum, 3 on Snare, 6 on Cymbal/HiHat, 3 per `itp` slot | ITCM −≈540 B; flash −32,769 B |
| **S3** | PWM on Drum, Snare, Cymbal, HiHat | no worst case rises | ITCM +≈2.9 KB (**approved**); flash +≈1.5 KB |
| **S4** | Alkaline VCA overdrive + tone, A3 | Alkaline ≈ 261.5 without a slot (accepted, P9-D8) | 0 RAM (slot reserve); flash +2.5 KB tables |
| **S5** | PWM on the Alkaline (fused mode 3), on S4's ladder | must not exceed S4's Alkaline figure | 0 RAM; flash ≈ +1.4 KB |
| **S6** | Ceiling assessment: Basic and Advanced worst cases measured and frozen | — | — |
| **Device** | One image; one hardware test (§10) | | |
| *Later* | *Master compressor, by ear* (§9) | | |

**Why this order:**
- **S1 first:** the LFO fix is a bug in current use and touches no file the
  DSP steps change.
- **S2 before S3:** with crash gone, the plain-read ceiling is 27, so PWM
  uses separate sine and table warp copies (P-D4).
- **S4 before S5:** both edit `dl_renderB2Body()`. Building the PWM wrapper
  on the final ladder means it is measured once.

### 0.2 Decisions (user, 2026-11-11)

| # | Decision |
|---|---|
| D-1 | **Basic stays gated at 261.3** (plus P9-D8's accepted ≈ 0.2 for the Alkaline). |
| D-2 | **The Advanced ceiling comes down too:** it is measured after the implementation (S6), and the measured worst case is frozen as the new Advanced gate. |
| D-3 | **LFO rates:** a smooth range with more options at both ends, tuned to semitones at the top (§3.4). **`frq` 0 is a true zero (the LFO stops); `frq` 1 is the 100 s cycle.** |
| D-4 | One pass for S0–S6; the compressor is a later phase, by ear. |
| P-D5 + ITCM | **Approved:** PWM on all five uses (plain, FM, FM ramp, ring, sync), +≈2.9 KB ITCM in `.itcm.s094p1`. |
| §3.5 | **Accepted:** audio-rate LFO values are per block (stepped on most targets, piecewise-linear on volume). |
| P9-D9 | **No relabel.** The filter page's `alk_drive` keeps its labels. The VCA drive takes the shared `instrument_drive` key with the other four types' labels ("Voice" / "Overdriv" / `drv`) on the mix page (K1). |
| All others | **The recommendations:** C-D1..C-D3 (crash out, card sample, no remap), L-D2 (sync wins, if free or paid for), P-D1 (hybrid), P-D2 (two-saw sum), P-D3 (`pos` clamped to 1..127), P-D7 (≤ 2 width steps per block), P-D9 (no modulator PWM), P9-D4 (ship starting curves, tune by ear). |

### 0.3 Findings made while writing the schedule (2026-11-11)

These change details of the drafts; the schedule follows them.

1. **The sine table starts at its trough** (`sine_table[0]` = −32,767,
   `[2048]` = +32,766: a −cos). The PWM draft assumed a positive lobe first.
   The warp therefore splits the rising and falling halves, as it does on the
   triangle, and **the warped sine has no DC**: both halves have mean zero.
   **P-D8 (sine DC correction) is not needed** and no DC term is built.
2. **Table shapes, measured on `wavetable.c`:**
   - `sawTable` descends, with its jump at index 0;
   - `recTable` is high for the first half;
   - `triTable` starts at its trough.
   The largest step between neighbouring points is 32,767 (saw), 32,753
   (rec), 207 (tri) and 51 (sine). So the two-read difference interpolated in
   Q15 stays inside int32 (65,534 × 32,767 < 2³¹).
3. **The Alkaline 303 square is louder than a flat pulse of the same peak.**
   RMS ratio 1.41–1.43 on rows 3–5 (1.09 on row 1). The PWM pulse under `Sqr`
   is scaled by 1.40 so `pos` 64 matches `Sqr` under `wav`.
4. **The warp octave lift is rounded, not ceiled.** A ceiling lifts a whole
   octave for a 1.6 % compression (`pos` 63). The triangle's harmonics fall as
   1/n², so the rounded lift (0 from `pos` 46 to 82) adds aliasing far below
   the table's own.
5. **A3 and the golden capture:** at drive 0 the table entries are exact. At
   other drive bytes they may differ from the runtime formula by 1 ulp
   (post-FX §4). AL-OD0 is therefore byte-identical at drive 0 and allowed
   ±2 LSB (reported) at drive bytes 64 and 127.
6. **`make_init_alk.py` treats `ROW_MENU` as morphable but not
   `ROW_MENU_SPECIAL`.** Making `lfo_sync` special would drop it from
   `init.alk`'s `[morph]`. The script gains `ROW_MENU_SPECIAL`.
7. **The crash ROM is CC0** (freesound, X-Act, recorded at 32 kHz). The card
   file can carry it freely; its header gives 44,100 Hz (pitch comes from the
   sample law, not the header).
8. **The post-FX tables go in a new generated pair**
   (`AlkalinePostTables.c/.h`, `tools/alkaline/gen_alk_post_tables.py`), so
   `gen_alk_tables.py --check` and the network tables stay untouched.
9. **The sine/table split costs ITCM, not cycles.** Because finding 1 removes
   the DC term, the W loops have no extra per-sample work.

---

## 1. The budget across the pass

Figures come from the drafts (measured there, or marked as estimates). Every
step re-measures on a linked image.

| | Today | After S2 | After S3 | After S4 | After S5 |
|---|---:|---:|---:|---:|---:|
| Drum dearest | 261.3 | 258.3 | 258.3 (PWM never dearest) | 258.3 | 258.3 |
| **Basic gate (D-1)** | 261.3 | 261.3 | 261.3 | 261.3 (+0.2 Alkaline) | same |
| Cymbal/HiHat dearest (Advanced) | ≈ 292.9 | ≈ 286.9 | ≈ 286.9 | ≈ 286.9 | ≈ 286.9 → **frozen in S6 (D-2)** |
| Alkaline, no slot | 256.1 / 256.5 (the drafts disagree; S0 settles it) | same | same | ≈ 261.5 | ≤ S4's figure |
| Alkaline, slot held | 271.1 / 271.5 | same | same | ≈ 276.5 (accepted, P9-D2) | PWM holds no slot |
| `itp` pool, per slot / both | 45 / 90 | 42 / 84 | ≤ 42 | 42 / 84 | 42 / 84 |
| ITCM used (of 16,384 B) | 12,128 | ≈ 11,590 | ≈ 14,500 | same | same |

---

## 2. S0 — Baselines

1. `make all && make img`. Record into `tools/dsp_test/baselines/s094p1/s0/`:
   - `elf_p4_snapshot P4BASE=baselines/s094p1/s0`;
   - the Alkaline per block (`control_cost.py --elf`) and loops
     (`elf_looppath.py`);
   - `link_budget.py`.
2. **Alkaline golden capture:** a new bench mode `--golden` writes
   `tools/dsp_test/baselines/s094p1/alk_golden.bin` (Saw/Sqr × drive bytes 0,
   64, 127 × the P4 pattern; a Res sweep; `wtb` without and with a slot).
3. **After each step:** its gates run against the previous step's accepted
   snapshot (`s094p1/s<N>/`), and AL-OD0 re-compares the golden capture.

---

## 3. S1 — LFO sync and rate

### 3.1–3.2 The bug (checked)

- **The sync write does not recompute the rate.**
  - `lfo_sync` is a plain `ROW_MENU` byte store in all five parameter tables;
  - `phaseInc` is recomputed only by the rate writer (`IM_SPECIAL_LFO_RATE`)
    and on a tempo change (`seq_setBpm()` → `lfo_recalcSync()`);
  - `lfo_setSync()` exists but has no callers.
- **Symptoms:**
  - a menu `snc` change does nothing until `frq` or the tempo moves;
  - on load, the rate is applied with the previous sound's sync byte;
  - external clock hides it, because it calls `seq_setBpm()` continuously.
- **Rate modulation:** an LFO or velocity target on `lfo_rate` writes
  `lfo.modNodeValue` directly every block (`modNode_updateValuePolarity()`,
  `TYPE_SPECIAL_F`), and `lfo_calc()` multiplies it in even while synced.

### 3.3 The fix

- **New tag `IM_SPECIAL_LFO_SYNC`** (writer value 23; the writer count
  becomes 24 of 32). The writer calls `lfo_setSync()`.
- **Rows:** the five `lfo_sync` rows become `ROW_MENU_SPECIAL`. Also updated:
  the classifier, `check_special_tags.py`, and `make_init_alk.py`'s
  `MORPHABLE` (finding 6).
- **Sync wins (L-D2):** `lfo_calc()` uses a rate scale of 1.0 while `sync` is
  set. The per-block cost is measured. If it is not free and no saving inside
  the LFO path pays for it, this one change is held back and reported.

### 3.4 The rate table (D-3)

A generated `const float lfoRateHz[128]` (512 B flash) read by
`lfo_setFreq()`, in place of the cubic. `LFO_MAX_F` goes.

| Part | `frq` | Rates | Step |
|---|---|---|---|
| **Stopped** | 0 | 0 Hz: `phaseInc` 0; the LFO holds its phase (a retrigger sets it to `ofs`) | — |
| **Slow and LFO range** | 1 … 62 | 0.01 Hz (100 s) … 7.71 Hz | eases from 2.73 to 1.01 semitones; changes by ≤ 0.028 semitone per step |
| **Semitone grid** | 63 … 127 | MIDI note `frq` − 63 at A4 = 440 Hz: 8.18 Hz … 329.63 Hz (E4) | exactly 1 semitone |

| `frq` | 0 | 1 | 8 | 16 | 24 | 32 | 40 | 48 | 56 | 63 | 76 | 88 | 100 | 108 | 120 | 127 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Hz | 0 | 0.010 | 0.029 | 0.090 | 0.25 | 0.62 | 1.41 | 2.86 | 5.24 | 8.18 | 17.3 | 34.6 | 69.3 | 110 | 220 | 329.6 |
| Period | stop | 100 s | 34 s | 11 s | 4.0 s | 1.6 s | 0.71 s | 0.35 s | 0.19 s | 0.12 s | | | | | | |

| Range | Stopped | > 10 s | 1–10 s | 1–8 Hz | 8–20 Hz | 20–100 Hz | > 100 Hz |
|---|---:|---:|---:|---:|---:|---:|---:|
| Today (cubic) | 0 | 10 | 11 | 22 | 16 | 42 | 27 |
| **New** | **1** | **16** | **20** | 26 | 16 | 28 | 21 |

- **Why the top is E4:** the ×2 rate-modulation overlay keeps `phaseInc`
  below 2³² (2.05 × 10⁹ at the top) and leaves at least 4 LFO values per
  cycle.
- **Display:** `frq` stays 0..127.
- **Stored data:** every saved `frq` plays at a new rate (no migration).
- **Generator:** `tools/plots/gen_lfo_rate_table.py --check` (target
  `lfo_rate_table`).

### 3.5 Audio rate at a 1,378 Hz LFO (accepted)

- The LFO stays one value per block.
- Volume targets are ramped across the block by the mixer (piecewise-linear).
- Pitch, pan and most other targets step per block.
- Filter cutoff is slewed in `fre` / `itp`.

---

## 4. S2 — Remove the crash wave

1. `tools/samples/make_crash_wav.py` writes `SD_CARD/samples/02_crash.wav`
   from `crashSample` before the array is deleted (32,769 frames, 16-bit,
   `smpl` loop 0..32,767, `(u − 127) × 256`).
2. `tools/samples/scan_wave_refs.py` lists every card file whose stored wave
   bytes move (no remap).
3. **Removal:**
   - `CRASH`, its loops, its cases and `Samples.c/.h`;
   - `OSC_STOCK_LAST` = `NOISE`; `OSC_SAMPLE_START` 6 → 5;
   - `Cym` leaves `waveformNames`.
4. **Gates:**
   - the crash loops are MISSING by design (reviewed, re-snapshotted);
   - mixer 0 GREW;
   - the host tests move off `CRASH`.

---

## 5. S3 — PWM on Drum, Snare, Cymbal, HiHat

- **Type:** `typ` 4 `pwm`; `OSC_TYPE_MODLIST` 4 → 5 (never stored);
  `OSC_BIND_PWM` 3.
- **Per wave:**
  - `Sin`, `Tri` warp (the rising half over `[0, W)`, the falling half over
    `[W, 1)`), with no DC term (finding 1);
  - `Saw` two-saw sum;
  - `Rec` two-saw difference (the pulse);
  - integer Q15 interpolation, Q14 gain.
- **Loops:** fifteen, in `.itcm.s094p1`: {D, W-table, W-sine} × {plain, FM,
  FM ramp, ring, sync}.
- **Control:**
  - the per-block plan and the five role dispatchers live in flash; the
    tables are in flash;
  - width slew ≤ 2 steps per block;
  - a trigger snaps the width; the start phase is 0.
- **Gates:** every loop against its role ceiling; old loops 0 MISSING /
  0 changed; mixer 0 GREW.

---

## 6. S4 — Alkaline post-filter overdrive and tone

- **Decided form:** P9-A′, the post-FX draft revision 3.
  - **Overdrive:** the OTA input is a per-block `otaIn = gain × 1/(2Vt)`.
  - **Tone:** a one-pole low-pass in the `c` form on the OTA output, in the
    loop; `c` = 0 at Tone 127 is exact.
  - **A3:** the drive gain and make-up come from tables.
  - **Tables:** in `AlkalinePostTables.c/.h` (finding 8).
- **Rows (appended):**
  - 29 `instrument_drive` "Voice" / "Overdriv" / `drv` → `vcaDrive` (K1,
    explicit Alkaline branch in the shared writer);
  - 30 `alk_tone` "Voice" / "Tone" / `ton` → `tone`;
  - mix page `vol pan ton drv`;
  - `alk_drive` labels unchanged.
- **AL-OD0:** byte-identical at drive 0; ±2 LSB at drive bytes 64/127,
  reported (finding 5).
- **Break builds:** tone entry 127 = 0.001 and gain entry 0 = 1.0001 must
  fail.

---

## 7. S5 — PWM on the Alkaline

- **Type:** `typ` 2 `pwm`.
- **Ladder:**
  - fused mode 3 in `dl_renderB2Body()` and `DiodeLadder_renderB2OscPwm()`;
  - A read as fused mode 1; B truncated at `ph − pos × 8` points.
- **Read forms:** `ALK_PWM_READ_FORM` 1 (default, the 212 form) or 2 (both
  truncated, 204) is chosen by the linked measurement against S4's figure.
- **Levels:**
  - `Sqr` is the pulse at 1.40 × the saw scale (finding 3);
  - `Saw` is the two-saw sum.
- **Width:** slew ≤ 2 steps per block; a non-legato note snaps it; the phase
  runs free (the 303's VCO).

---

## 8. S6 — Ceiling assessment (D-1, D-2)

- **Method:** on the final image, the `drum_ceiling.txt` method (linked loops,
  inlined mixer loops by source on a `-g` twin, per block, VDIV).
- **Files:**
  - `tools/dsp_test/baselines/s094p1/basic_worst.txt`;
  - `tools/dsp_test/baselines/s094p1/advanced_ceiling.txt`, the first frozen
    Advanced ceiling.
- **Gates after S6:**
  - Basic at 261.3 (the Alkaline's ≈ 261.5 is the accepted exception);
  - Advanced at the frozen figure;
  - the report comes to you before the figures are used.

---

## 9. Later phase — the master compressor (by ear)

Unchanged from revision 2; the analysis is kept here for that phase.

### 9.1 Signal path (checked)

| Stage | What it does to level |
|---|---|
| Voice at `drv` 0 | Nothing exceeds int16 full scale. |
| `drv` | Small-signal gain `1 + 2s/(1−s)`, `s = drv/128`: +4.4 / +9.5 / +16.9 / +48 dB at 32 / 64 / 96 / 127. **This is the voice's only gain above unity.** |
| `vol` | 0..1 (init 0.8). |
| Fader | Log taper. |
| Pan | Square-root law, −3.0 dB at centre. |
| Bus | Full scale 8,388,352. |
| Compressor | On the Scene's St1 or St2 pair only, after the FX return. |

One voice at `drv` 0, `vol` 127, fader up and centre pan peaks at −3.0 dBFS
(−5.0 at `vol` 0.8).

### 9.2 Macro (checked)

| `cam` | Threshold (dBFS) | Ratio | Makeup (dB) | Saturator drive | Sidechain (dB) |
|---:|---:|---:|---:|---:|---:|
| 0 | −3.0 | 1.0 | 0.0 | 1.00 | 2.0 |
| 64 | −16.6 | 3.0 | 2.7 | 1.13 | 11.6 |
| 96 | −23.4 | 5.0 | 7.1 | 1.29 | 16.4 |
| 127 | −30.0 | 8.0 | 11.0 | 1.50 | 21.0 |

- **Detector:** RMS over 5 ms, one block late.
- **Attack:** 5–15 ms. **Release:** 60–600 ms.
- **Makeup:** referenced to −12 dBFS RMS.

### 9.3 Hypotheses and work

**Hypotheses:**
- there is no input gain;
- the makeup reference outweighs the reduction on quiet hits;
- the attack misses the transient;
- the saturator sees the post-gain level.

**Work:**
- **Measure first:** a `modules/bus_comp.c` bench on the real
  `BusCompressor.c`; synthetic hits; your St1 captures.
- **Options:**
  - **C1:** input drive `cdr`, folded per block; +1 B per Scene, +16 B
    SRAM1, needs sign-off; four free page cells;
  - **C2:** re-map `cam`;
  - **C3:** `vol` above unity;
  - **C4:** a faster attack, or a peak-weighted detector;
  - **C5:** more saturator drive.
- **Lean:** C1 (+ C4's attack range if the transient escapes).

---

## 10. Device test (one image, end of the pass)

1. **Boot:** the tag diagnostic is clean; ITCM and flash are as
   `link_budget.py` reported.
2. **CPU:** the six-slot worst-case Scene on the `cpu` widget, with no
   underruns:
   - Drum `fm` on `slp`;
   - two Cymbal/HiHat on `slp` FM;
   - Alkaline with overdrive, tone and a slot;
   - Alkaline `pwm`.
3. **LFO:**
   - `snc` on and off under internal clock;
   - load a synced Kit over an unsynced one, and the reverse;
   - `frq` 0 stops the LFO; `frq` 1 runs a 100 s cycle;
   - the semitone end against a tuned oscillator;
   - an audio-rate LFO on volume against pitch.
4. **Crash:** `02_crash` after `Load:[Samples]`, as `slp` and as a modulator
   `l` entry; the card-scan list checked by hand.
5. **PWM:**
   - width sweeps on Sin/Tri/Saw/Rec;
   - narrow pulses at low notes;
   - `rst` / `fre` / `itp`;
   - Drum `fm` / `mix` / `rng` / `snc`.
6. **Alkaline:**
   - Overdrive 0 / Tone 127 sounds unchanged;
   - overdrive and tone sweeps (P9-D4 by ear);
   - `pwm` Saw/Sqr and width sweeps.

---

## 11. Statements this pass would make out of date (for you to log)

- **S1:**
  - `INSTRUMENTS_DSP_REFERENCE.md` LFO rows (the `lfo_sync` special row; the
    rate table, `frq` 0 stopped);
  - the special-tag list (`IM_SPECIAL_LFO_SYNC` = 23, writer count 24);
  - `MEMORY.md` wherever the S073 tag invariant lists tags.
- **S2:**
  - the stock wave list (no `Cym`);
  - `OSC_SAMPLE_START` 5;
  - `FILESYSTEM_SPEC.md` sample folder (`02_crash.wav`);
  - the reductions draft's §5 list.
- **S3, S5:**
  - the PWM draft's §9 list;
  - `typ` 4 `pwm`, `OSC_TYPE_MODLIST` 5;
  - Alkaline `typ` 2 `pwm`;
  - `.itcm.s094p1` in the ITCM order;
  - the warped sine has no DC (finding 1).
- **S4:**
  - the post-FX draft's §8 list;
  - `AlkalinePostTables.c/.h`;
  - Alkaline rows 29 → 31;
  - `DiodeLadder` +12 B;
  - `MIDI_NRPN_TABLE.md` (root, yours).
- **S6:**
  - `CPU_USE_DSP_AUDIT.md` (Basic ceiling text, the first Advanced ceiling);
  - `INSTRUMENTS_DSP_REFERENCE.md` §8.2;
  - `MEMORY.md` "CPU ceiling" / "Cost comparisons" lines.
- **Inconsistencies in the drafts** (for the schedule only):
  - old draft filenames;
  - the PWM draft §7 gate names, which do not exist;
  - `check_c.py --pattern` → `check_c.py c2`;
  - the 256.1 / 256.5 Alkaline figure;
  - the PWM draft's sine-phase assumption (finding 1).
