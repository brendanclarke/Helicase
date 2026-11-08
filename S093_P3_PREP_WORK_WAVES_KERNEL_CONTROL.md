# S093 P3 — Alkaline Prep Work: Waves, Kernel, Control Path

- **Session:** 093, phase 3. The prep work of
  `S093_P2_ALKALINE_B-R_PRE_IMPLEMENTATION.md` (its P2-pre, P2-0, P2-1,
  P2-1A), done and measured. The firmware integration (the brief's P2-2) is
  now phase 4: `S093_P4_FIRMWARE_IMPLEMENTATION.md`.
- **Decisions used** (user, S093; brief §9): B2R as Basic; R1 (+2 B RAM, used
  in P4); D14 (Re3/Sw3 removal, no migration); D16 (drive ×1..×8); B-2
  option (b) (make-up gain through the OTA current); T1-7
  (`osc_setTableFreq()`); B-9 (track 7 shares slot 5); Tier 1 + Tier 2.
- **Status (2026-11-08):** all four steps are done, plus a second round
  after the user's listening test (§9). Host benches green, firmware
  `make all` clean, no oscillator or mixer per-sample loop changed. Nothing
  is committed (commits belong to the user).
- **Result: B2R ≈ 256.5 instructions per sample against the frozen Drum
  ≈ 261, about 5 under; with r6 (adopted, applied in P4-0) ≈ 253, about 8
  under** (§9.4). §0–§8 record the first round as measured;
  where §9 supersedes them it says so.

---

## 0. Summary

| Step | Result | Gate |
|---|---|---|
| **P2-pre** Freeze the Drum ceiling | **Drum dearest ≈ 261 instructions per sample, 3 VDIV** on the current linked image (loops 222.5 + per block 922/32 + SRR 10). The brief's "≈ 275+" was an estimate and is replaced | Measured with `elf_looppath.py`/`elf_blockpath.py`, loops attributed to source on a line-mapped twin image (§1) |
| **P2-0** Remove Re3/Sw3 | Flash headroom **40,980 → 86,172 B** (+45,192); ITCM 15,280 → 14,512 B. Built-in waves 0–5, user entries from 6 | Bench `oscillator` 0 failures (both breaks caught); `stock_tables` C1–C4 OK on the archive; no per-sample loop grew (§2.3) |
| **P2-1** DSP port | B2R, B-R, A-R in `DiodeLadder.c`, network form N1, 7 V output, drive, make-up, quiet query. **B2R loop 197 + 3 VDIV** (the reference variant measures 209) | C0: the port equals the S093 P1 reference kernels to ≤ 0.002 dB LSD60 and one output LSB (§3.3); bench laws AL-B/R/S/N/AB/D/Q pass; C1/C2 against SPICE as in S093 P0 |
| **P2-1A** Control path | Per block **3,010 (hand count, S093 P2) → 1,476** (round 1), **→ 1,393, 9 VDIV** after round 2 (§9) | Tier 1 exact against S093 P0 (C2: every step within 0.1 cent); T2-2 within its tolerances; T2-3 failed V3c by 0.2–0.8 %, first reverted, then **accepted by the user after listening** (§9.1) |
| **Round 2** (§9) | (b) T2-3 accepted, (c) T2-4 rejected; division batching, direct pitch, the **fused oscillator read** (−9 per sample, bit-identical); slow-network experiments: **q128 rejected** (unstable at Res full), **r6 adopted** (−3.3 per sample, inaudible against B2R; applied in P4-0) | Bench laws incl. the new AL-X; listening renders `tools/alkaline/listen/` |
| **Budget** | Round 1: ≈ 267 (6 over). **Round 2: ≈ 256.5 against Drum ≈ 261, about 5 under**; with r6 ≈ 253 (8 under) | §9.4 |

---

## 1. P2-pre: the Drum ceiling, frozen

**Image:** `build/s093p3/base.elf`, HEAD `a482be4f` (code identical to
`8ca992e8`), text 712,264 B; re-checked after P2-0 on the image Alkaline will
join.

**Method:**
- Per-sample loops: `tools/dsp_test/elf_looppath.py --cond-heads --in-range`
  on the linked ELF.
- The voice renders are inlined into `mixer_calcNextSampleBlock()`, so its
  45 loops were attributed to source. A `-g` twin was built
  (`make BUILD=build/s093p3/dbg MCU="… -g"`). Its code differs from the
  release image only in 168 bytes of veneer branch offsets, and every loop
  length is identical. `objdump -l` then names each loop's inlined
  functions.
- Per-block paths: `tools/dsp_test/elf_blockpath.py` on the linked ELF, and
  on a standalone `-Ofast` object for `Drum_calcVoiceSyncBlock()` (inlined in
  the image).

**Dearest Drum** (`fm` with an `slp` carrier, T+D chain):

| Part | Instructions | VDIV | Where measured |
|---|---:|---:|---|
| `osc_slpFmRampBlock` (carrier) | 51 | 0 | linked |
| `calcNextOscSampleBlock` (modulator, dearest) | 30 | 0 | linked |
| `transient_calcClickBlock` | 22 | 0 | linked, inlined in the mixer |
| Transient mix-in (QADD16, unrolled: 16 per block) | ≈ 2.5 | 0 | linked |
| `SVF_calcBlockZDF` (dearest type loop) | 92 | 2 | linked |
| `voicePost_drum` | 25 | 1 | linked, inlined in the mixer |
| **Per-sample loops** | **≈ 222.5** | **3** | |
| Per block: `Drum_calcVoiceAsync` 63 + `slopeEg2_calc` 28 + 2 × `osc_setFreq` 128 + dispatch 14 + filter `fastTan` ≈ 30 | 391 | | linked (P2-0 image; `osc_setFreq` was 139 before P2-0) |
| Per block: render entries: `Drum_calcVoiceSyncBlock` 163 + `SVF_calcBlockZDF` 161 + `osc_renderTypedFmRamp` 12 + `osc_slpFmRampBlock` 70 + `calcNextOscSampleBlock` 18 + `transient_calcBlock` 21 + `osc_typedPositionApply` 75 (its `__aeabi_ldivmod` not counted) + `osc_typedPositionRestore` 11 | 531 | | standalone and linked |
| **Per block ÷ 32** | **≈ 28.8** | | 922 per block |
| Mixer SRR (`mixer_decimateBlock`) | 10 | 0 | linked |
| **Charged to the slot** | **≈ 261** | **3** + per-block VDIV (not counted) | |

- **Other modes are cheaper:** `rng` 30 + 42 = 72 and `snc` 8 + 41 = 49
  against `fm` 81. WFd's fold loop (max 22) replaces the transient and its
  mix-in (24.5).
- **Not in the figure** (pool-limited system-wide, as S092 P0 §1.1): the
  `itp` old-carrier copy (15 per sample for 132 samples) and the wavetable
  blend.
- **Record:** `build/s093p3/drum_freeze.txt` (not committed; `build/` is a
  work area).

---

## 2. P2-0: Re3/Sw3 removed

### 2.1 Changes

| File | Change |
|---|---|
| `Core/DSPAudio/Oscillator.h` | `SQ3`, `SW3`, `OSC_STOCK_FIRST` gone; `OSC_STOCK_LAST` = `CRASH`; `OSC_SAMPLE_START` 8 → **6**; `osc_isTableWave()` is Tri/Saw/Rec |
| `Core/DSPAudio/Oscillator.c` | `osc_stockTables[]`, its assert and the six `case SQ3: case SW3:` groups removed |
| `Core/DSPAudio/wavetable.h` | The two externs removed |
| `Core/DSPAudio/wavetableStock.c` | **Moved** to `tools/plots/archive/wavetableStock.c` (outside the build); `Makefile` `DSP_SRCS` no longer lists it |
| `Core/Menu/MenuText.h` | `waveformNames` count 6 (the `menu.c` assert holds) |
| `tools/plots/gen_stock_tables.py` | Paths fixed (it lives in `tools/plots/`); default output is the archive. It reproduced the firmware file byte for byte before the move |
| `tools/plots/plot_stock_tables.py`, `plot_303_raw_vco.py`, `README.md` | Read the archive |
| `tools/dsp_test/modules/oscillator.c`, `osc_host.h`, `modules/alkaline.c` | Host tables and the S086 D1/D2/D5 checks removed; D4 checks `OSC_SAMPLE_START` 6 and `OSC_STOCK_LAST` = `CRASH`; the ring/sync/pos wave lists without SQ3/SW3 |
| Documentation | `INSTRUMENTS_DSP_REFERENCE.md` §6.1, §6.1.2 (now "removed in S093", with the restore steps), §8.1, §9.3; `FILESYSTEM_SPEC.md`, `MIDI_SPEC.md`, `MIDI_NRPN_TABLE.md` (wave ranges); `REPOSITORY_LAYOUT.md` |

### 2.2 Stored data (no migration, D14)

- An osc1 `wav` of 6/7 clamps to `Cym`.
- A modulator value v ≥ 8 now plays what v − 2 played. A modulator 6/7 now
  selects the first two user entries.
- Re-save affected Kits.

### 2.3 Gates

- **Flash and ITCM:** headroom 40,980 → **86,172 B**; ITCM 15,280 → 14,512 B
  (the inlined stock copies in the ITCM dispatchers went).
- **Bench:** `make -C tools/dsp_test oscillator` 0 failures, both break
  builds caught; `stock_tables` C1–C4 OK.
- **Loop gate:** the bench Makefile targets `elf_osc_check`/`elf86_mixer_check`
  no longer exist (removed in `f3805478`; their tools remain). The scripts
  were run directly against `build/s093p3/base.elf`:
  - **Removing the case labels changed LTO's inlining.**
    `calcNextOscSampleFmBlock` is now inlined into the mixer, and
    `mixer_addVoiceInt16ToOutputAndFx` became an out-of-line function.
  - **Every per-sample loop was compared by source attribution:**
    - the Drum ceiling loops are unchanged (51, 30, 92, 22, 25, 10);
    - the output paths 74/59/57/42/41/28 became 73/58/57/42–43/27;
    - `calcSampleOscFmBlock` 38 → 32 (and 36 for the inlined copy);
    - the bus compressor's 71 loop moved into the mixer unchanged;
    - one 24 loop went with the stock-table case.
  - **One loop grew:** Snare's noise loop is restructured. Its hold path is
    15 (was 14) and its RNG-fetch path, taken once per noise period, is 19.
    Snare is not on the ceiling path, and the change is outweighed by the
    output-path savings.
  - **`elf_hotloops.py`'s "5 grew"** were mis-pairings caused by the moved
    output paths. The per-function comparison above replaces it.
- **Device (W7): open,** in P4. Check a Kit with `sNN` modulators
  re-selected and the menus showing six built-in waves.

---

## 3. P2-1: the DSP port

### 3.1 Files

| File | What |
|---|---|
| `Core/DSPAudio/DiodeLadder.h/.c` | `DiodeLadder_renderB2` (B2R, called), `_renderB` (B-R), `_renderA` (A-R): `dlv_kernel.c` MODEL 3/1/0 resolved. N1 network; `DiodeLadderNet` 108 floats (432 B); `DiodeLadder` 232 B; output 7 V full scale; the OTA scale folded into the ramped current (`DL_OA`) |
| `Core/DSP/Instruments/Alkaline/AlkalineTables.h/.c` | Generated, N1, 9 Res points, **64-sample** block system (T2-2), the N1 asymptote constants `ALK_NET_AL/BE/DECAY_U/V` and `ALK_NET_BLOCK` (generated with the table, which asserts the decays) |
| `tools/alkaline/gen_alk_tables.py` | N1 (`n1_net()`, `asym()`), `NET_BLOCK` 64; `--check` passes |
| `Core/DSP/Instruments/Alkaline/AlkalineVoice.h/.c` | B2R call; `drive` (×1..×8 = t³, t = 2^drive); make-up `2^(ALK_MAKEUP_OCT (1 − drive))` (1.0 = +6 dB at ×1) applied to the OTA current; `Alkaline_isQuiet()` on the unscaled current; `noinline` on async and sync block; `AlkalineVoice` **540 B** (slot 1,264 B) |
| `tools/alkaline/variants/` | Frozen copies of the S093 P0 `DiodeLadder.h` and host fragments (`AlkalineControl.host.*`, `AlkalineTables.host.*`), so the variants build and measure as S093 P1 recorded |
| `tools/dsp_test/modules/alkaline.c` | Three kernels; AL-N for N1; AL-AB B2R against B-R and A-R; new AL-D (drive, headroom, tail), AL-Q (quiet), AL-F (fast maths), AL-O (oscillator setup); `--traces … [DRV]` writes all three kernels |
| `tools/alkaline/check_c.py` | New C0 (port against the reference variants and the golden model, ×1/×4/×8); 7 V scale; three kernels; C2 divides out the make-up; runs parts (`check_c.py c0 c2`) |

### 3.2 Cost (standalone `-Ofast`, `elf_looppath.py`)

| Kernel | Port | Reference variant (same tool) | VDIV |
|---|---:|---:|---:|
| **B2R** | **197** | 209 | 3 |
| B-R | 223 | 230 | 3 |
| A-R | 188 | 206 | 2 |

The port keeps state in locals rather than the variant's struct and folds
B2R's constant rungs; S093 P3 also folded the OTA output scale into its
current (−2).

### 3.3 Proof (`check_c.py`, before T2-2)

- **C0, port against the S093 P1 reference kernels on identical inputs**
  (acid and lowres, ×1/×4/×8): LSD60 ≤ 0.0023 dB, level 0.000 dB, largest
  difference one output LSB (0.21 mV). The distances to the golden model
  equal the variants' to three decimals.
- **C1 against SPICE:** step levels within 0.24 dB (as S093 P0). LSD60 is
  dominated by SPICE's I_abc feedthrough (S093 P0 §5.3).
- **C2, the voice on the P4 pattern:** every step within 0.31 dB of SPICE;
  against Python B_rt the level is within ±0.15 dB.

### 3.4 Final figures (after P2-1A, `check_c.py` all parts)

| Case | B2R against golden, LSD60 (variant B2R) | B-R | A-R |
|---|---|---|---|
| acid ×1 / ×4 / ×8 | 0.24 / 0.88 / 0.94 (0.20 / 0.88 / 0.90) | 0.23 / 0.68 / 0.67 | 0.32 / 1.26 / 2.00 |
| lowres ×1 / ×4 / ×8 | 0.27 / 1.16 / 2.24 (0.22 / 1.15 / 2.28) | 0.22 / 0.60 / 1.01 | 0.15 / 0.87 / 1.36 |

- **C2 per step against SPICE (B2R):**
  - acid: cutoff peaks +50 / +48 / +22 / −3 / +48 / −28 / — / +44 cents
    (S093 P0's figures exactly); output within 0.30 dB.
  - lowres: within 0.30 dB except step 6 (−0.50 dB; it was −0.31 before
    T2-2).
- **Calibration ring (AL-R):** B2R and B-R 2.040 ms / 0.364 (golden 2.018 /
  0.364; law 2.0 ± 3 %, 0.364 ± 0.03).
- **Drive (AL-D):** the pattern at ×1 / ×2 / ×4 / ×8 is −29.3 / −26.0 /
  −23.1 / −20.6 dBFS RMS with the make-up, and no sample reaches full scale.
  The ×8 input-pair argument reaches 6.7, so the tail is in use.

---

## 4. P2-1A: the control path

### 4.1 The cuts, as done

| # | Cut | Done | Check | Result |
|---|---|---|---|---|
| T1-1 | Accent network written out | 5 × 5 → 2 × 2 in (W7, W) by elimination; the last block's diode states solved, then the states that solution implies; the first consistent kept | C2 per step within 0.1 cent of S093 P0 | **Pass** (identical to the four-state rule to 0.1 cent) |
| T1-2 | MEG | **One Newton step** (not one fixed-point pass) from the last block's stack current: the fixed point stops contracting near the floor; vy at the new current by its tangent; a strike leaves the cold estimate | V3b t10 within ±1.5 % of SPICE | **Pass**: +0.4 / +0.4 / +1.3 / +1.3 %, identical to the reference |
| T1-4 | Expo | **Two passes** from the last block's current. One pass left the strike peaks 20–30 cents off: a strike moves the current by octaves in one block | V3a within 33 cents; C2 cutoff ±5 cents | **Pass**: V3a 32.88 (reference 32.88); C2 identical |
| T1-5 | Prewarp | tan(θ)/θ by the [5/4] rational | ±10 cents | **Pass** (6e-6 relative, AL-F) |
| T1-6 | Pitch | `alk_log2f`/`alk_exp2f` (degree 6 / 5; the brief's "degree 5 gives 1e-7" holds for exp2, not log2) | ≤ 0.1 cent; AL-S glide | **Pass**: 0.012 cent worst; AL-S |
| T1-7 | Oscillator setup | `osc_setTableFreq()` (new in `Oscillator.c`). Octave level from the float's exponent and mantissa: the edges are exact doublings of 16.3516 Hz, so the result equals `freqToTableIndex()` exactly; 80 → 26 | AL-O: equal to `osc_setFreq()` at every note, dense sweep and every edge, with `pitchMod`/`modNodeValue` ≠ 1 | **Pass**; firmware: no loop changed, `osc_setFreq` per-block path 128 unchanged |
| T1-8 | Drive and make-up | One exp2 for both drive factors; the make-up a second | exact | Pass |
| T2-1 | MEG floor fit | **Not done** | | Not needed for accuracy; saving about 1 per sample (the logs only); see §5 |
| T2-2 | Block system per 64 samples, split over two blocks | Rows 0–3 in a window's second block, rows 4–7 in the next block; output ramp over 64 samples; the Res blend latched between the halves. Table regenerated at T = 64/fs | Ring decay ±0.01 of 0.372; C1 LSD60 ≤ +0.1 dB | **Pass for B2R/B-R**: decay 0.364; LSD60 against golden +0.03…+0.05 dB. **A-R is unstable at Res full** with it (ring grows 3.4× per cycle): A-R as a fallback needs the 32-sample network |
| T2-3 | Accent, one solve with lagged states | Implemented (`ALK_ACC_SOLVES 1`), measured, first reverted to T1-1; **accepted by the user after listening (§9.1), now the default** | V3c within ±2 % | Fails by 0.2–0.8 %: peaks −1.9 to −2.8 % against S093 P0 at Res 0/0.5/1, closer to SPICE (Res 0: +5.1 % against S093 P0's +7.1 %). Saves about 70 per block and 2 VDIV |
| T2-4 | Nearest Res point (33 points) | Built for the listening test only; **rejected by the user** (§9.1); the switch is removed | Listening | Ticks at every table crossing when `res` moves |

The Python model holds the same reductions (`alk_model.FAST_CONTROL`,
`FAST_ACCENT_LAG`). Run with `python3 tools/alkaline/validate.py --fast v3`;
results are stored under `v3_fast`. V5 was not re-run in Python: it rewrites
the committed `alk_v5_*.wav` and plots. C2 runs the C voice itself against
SPICE.

### 4.2 Per block, measured (`tools/alkaline/control_cost.py`)

New tool:
- links the real Alkaline sources for the M7 (`-Ofast`, newlib-nano libm)
  with oscillator/LFO stubs;
- walks the longest static path with each callee's own total at its call
  site (tail calls included), plus every remaining loop × its iterations;
- excludes the render loop; adds the oscillator calls (standalone
  `Oscillator.o`) and the dispatch (14, as for Drum).

| Item | Instructions | VDIV |
|---|---:|---:|
| `Alkaline_calcVoiceAsync` own path | 213 | — |
| MEG (`alkMeg_step` + `alkMeg_n53` 73) | 247 | 4 |
| Accent network (T1-1, two solves) | 194 | 9 |
| Expo (two passes) | 187 | 4 |
| VEG | 45 | 0 |
| Prewarp | 38 | 1 |
| `DiodeLadder_setNet` | 32 | 0 |
| `osc_setBaseNote` + `osc_setTableFreq` | 20 + 26 | 0 |
| **Async total** | **1,002** | **18** |
| Sync block: `Alkaline_calcVoiceSyncBlock` + oscillator entry 18 + `DiodeLadder_renderB2` entry/exit + `dl_slowUpdate` (one half, 327) | 460 | 0 |
| Dispatch | 14 | 0 |
| **Per block** | **1,476** | **18** |
| **Per sample (÷ 32)** | **46.1** | **0.56** |

The S093 P0 path was 3,010 by S093 P2's hand count. The tool cannot weigh
its nested accent loops, so that figure is not re-measured.

---

## 5. The budget against the frozen Drum (round 1; superseded by §9.4)

| Per sample, the same accounting on both sides | Drum (frozen) | Alkaline B2R |
|---|---:|---:|
| Audio loops (with oscillator) | 222.5 | 197 + 24 = 221 |
| Per block ÷ 32 | 28.8 | 46.1 |
| SRR | 10 | 0 |
| **Instructions** | **≈ 261** | **≈ 267** |
| VDIV per sample (loops) / per block | 3 / not counted | 3 / 18 |
| Cycle proxy (+13 per loop VDIV, per-block VDIV ÷ 32) | ≈ 300 + Drum's per-block VDIV/32 × 13 | ≈ 306 |

**B2R is about 6 instructions (2.2 %) over the frozen Drum.** The remaining
options, cheapest first:

| Option | Saves per sample | Cost or risk |
|---|---:|---|
| T2-3 lagged accent (`ALK_ACC_SOLVES 1`) | 2.2 | Fails its ±2 % check against S093 P0 by 0.2–0.8 %; closer to SPICE |
| T2-4 nearest Res point, 33 points | ≈ 3.8 | Flash +10 KB; `res` steps every 4 knob values: listening check |
| T2-1 MEG floor (a 1-D table in log2 i in place of the two logs) | ≈ 0.6–1 | A fit to check against V3b |
| Softplus in `alkMeg_n53` by a [3/2] rational | ≈ 0.6 | n53 error ≈ 20 µV |
| Accept and let the device decide | 0 | Static figures carry a few instructions of uncertainty on both sides (Drum's `__aeabi_ldivmod` and per-block VDIV are uncounted) |

T2-3 + T2-4 together reach about 261, level with Drum. The device test
(four Alkalines in the worst-case Scene, P4 §5) is the binding gate either
way.

---

## 6. Deviations from the brief

| Brief | Done | Why |
|---|---|---|
| N1 constants as literals in `DiodeLadder.c` | Generated into `AlkalineTables.h` with the table | One source; the generator asserts them |
| T1-2 one fixed-point pass | One Newton step | The fixed point does not contract near the floor |
| T1-4 one pass | Two passes | One pass: 20–30 cents off at strikes |
| T2-3 "decide at the step's first block and hold" | One block of lag (re-decided every block) | Holding a wrong state for a step is worse; it was measured and reverted anyway |
| AL-AB B2R against A-R ≤ 0.3 dB | Holds (−0.03 dB) | — |
| AL-R on all kernels | B2R and B-R; A-R printed for information | A-R is unstable with T2-2 at Res full |
| Trigger signature with `source_slot` and the platform calls | Moved to P4 | Needs the firmware's LFO and velocity modules |
| The oscillator a separate loop (`calcWavetableOscBlock()`) | Fused into the B2R loop (round 2) | −9 per sample, bit-identical; the separate path stays as `ALK_FUSED_OSC 0` |

---

## 7. Open items carried to P4

- **r6** (§9.2 S5): adopted by the user; applied as the first step of P4-0
  (`S093_P4_FIRMWARE_IMPLEMENTATION.md` §0).
- The round-1 budget decision of §5 is closed by §9.
- **W7:** the Re3/Sw3 device check.
- **The bench `make all`** still stops at `run_modes` (S093 P0 O7). Run the
  targets one by one: `alkaline`, `alkaline_tables`, `oscillator`,
  `stock_tables`, `special_tags` all pass.
- **The bench Makefile ELF targets** (`elf_osc_check`, `elf86_mixer_check`,
  `elf86_mixer_snapshot`) were removed in `f3805478`. P4 should restore them
  or keep running the scripts by hand (§2.3).
- **A-R and T2-2:** if A-R is ever chosen, its network must stay at 32
  samples, or the instability must be fixed.

## 8. Reproduce

```
make all                                                 # firmware (Alkaline not in it yet)
make -C tools/dsp_test alkaline alkaline_tables oscillator stock_tables special_tags
python3 tools/alkaline/gen_alk_tables.py --check
python3 tools/alkaline/check_c.py [c0] [c1] [c2]         # needs the SPICE traces (tools/spice/tb303/e2e.py)
python3 tools/alkaline/control_cost.py                   # the per-block count
python3 tools/alkaline/validate.py --fast v3             # the reduced control path against SPICE
python3 tools/alkaline/listen_bc.py                      # the (b) listening set (B2R = T1-1 against b)
python3 tools/alkaline/listen_net.py                     # the network experiments: q128, r6 (tables in build/s093p3/)
python3 tools/alkaline/control_cost.py --tables build/s093p3/r6 --rows 3 --define ALK_NET_STATES=6u
python3 tools/dsp_test/elf_looppath.py --elf <o or elf> --sym DiodeLadder_renderB2 --cond-heads --in-range
```

---

## 9. Round 2: after the listening test (user, S093)

### 9.1 Decisions

| Option (§5) | Listening material | Decision |
|---|---|---|
| (b) T2-3, the lagged accent solve | `tools/alkaline/listen/alk_listen_<set>_{B2R,b}.wav` (`listen_bc.py`; B2R there is the exact T1-1 form) | **Accepted** — now the default (`ALK_ACC_SOLVES 1`) |
| (c) T2-4, the nearest of 33 Res points | `alk_listen_<set>_{c,bc}.wav` (the record) | **Rejected** ("absolutely not ok"): a tick at each table crossing when `res` moves (residual −42 → −22 dB around each crossing). The `ALK_RES_NEAREST` switch is removed |

Sets: `acid`, `lowres`, `acid_x8` (the S092 P4 pattern, the SPICE run's knobs),
`sweep`, `sweep_x8` (bench `--sweep`: a 16th bass line, Res 0 → 1 over 6 s,
then a 1.5 Hz wobble). Each set is written at its reference's gain.

### 9.2 The other savings, measured

| # | Saving | Measured | Sound | Status |
|---|---|---|---|---|
| S1 | **Fused oscillator read**: `DiodeLadder_renderB2Osc()` reads the voice's [11][1024] table row inside the B2R loop (the oscillator's own arithmetic: linear interpolation, int16 truncation); no existing loop edited | loop 198 + 24 = 222 → **213**; block entry +4: **≈ −9 per sample** | **bit-identical** (new law AL-X: output, phase and ladder state over a gliding pattern, both waves, ×1 and ×8) | **In** (`ALK_FUSED_OSC 1`) |
| S2 | **Batched divisions** in the control path (expo 4 → 3, MEG 4 → 2, accent 7 → 3 with T2-3) | **VDIV per block 16 → 9**; instructions about −17 | exact to float rounding (the (b) set re-renders to 0.1 dB of the first test) | **In** |
| S3 | **Pitch from the note number** (`MidiNoteFrequencies` is equal-tempered to 1e-4 cent; the fine detune by its second-order log2) in place of `osc_setBaseNote()` + `alk_log2f` | ≈ −40 per block (≈ −1.2 per sample), part of the −17 above net of S2's multiplies | exact to 5e-6 in log2 | **In** |
| S4 | **Quarter-rate network** (128 samples, split over four blocks; `gen_alk_tables.py --block 128`) | slow update 337 → 247; per block −94 (≈ −2.9 per sample) | **unstable**: the [SN] p7 ring grows 3.4× per cycle at Res full; at Res ≤ 0.85 it plays (−17…−21 dB from B2R, ±0.3 dB level) | **Rejected** (stability) |
| S5 | **Reduced-order network, 6 states** on one balanced basis common to every Res point (`--states 6`, `-DALK_NET_STATES=6u`; a per-point basis would break the Res interpolation) | slow update 337 → 228; per block 1,393 → **1,285 (≈ −3.3 per sample)** | ring 2.018 ms / 0.363 (golden 2.018 / 0.364; B2R 2.040 / 0.364); LSD60 against golden equal to B2R's at every drive; **−43…−49 dB from B2R, LSD60 0.01–0.02 dB** | **Adopted (user, S093)**; applied in P4-0 (`S093_P4` §0). Renders `alk_net_<set>_{B2R,r6}.wav` |
| — | 7 states | | −52 dB response error | No saving: 7 rows cannot split over two blocks |
| — | T2-1 MEG floor table, rational softplus | ≈ −1.5 together | small fits | Not needed |

Code shape (no sound change):
- The slow update is now generic in its phase count and state count. The
  latch block and the following blocks are separate branches, so the static
  path is exact.
- The output ramp is a stored step.
- The two-phase chunk index is a compile-time constant: 337 against the
  round-1 specialised 327.

### 9.3 Network audio checks (`listen_net.py`)

| Set | q128 against B2R | r6 against B2R |
|---|---|---|
| `acid` | +0.24 dB; −20.7 dB; LSD60 0.16 | +0.00 dB; **−47.2 dB**; 0.01 |
| `lowres` | −0.30; −21.5; 0.13 | +0.01; −49.3; 0.01 |
| `acid_x8` | +0.33; −18.1; 0.22 | +0.01; −44.0; 0.01 |
| `sweep` | −0.15; −17.9; 0.09 | +0.01; −47.2; 0.01 |
| `sweep_x8` | −0.19; −17.1; 0.16 | +0.03; −43.4; 0.02 |

Columns: level; null residual (re B2R); LSD60 (dB). Files:
`tools/alkaline/listen/alk_net_<set>_{B2R,q128,r6}.wav`; results
`listen/listen_net_results.json`. r6's frequency-response error (−29 dB re
the network's peak, at Res 0) does not reach the output.

### 9.4 The budget now

| Per sample | Drum (frozen) | B2R (shipped form) | B2R + r6 |
|---|---:|---:|---:|
| Audio loop (oscillator fused) | 222.5 | 213 | 213 |
| Per block ÷ 32 | 28.8 | 1,393 / 32 = 43.5 | 1,285 / 32 = 40.2 |
| SRR | 10 | 0 | 0 |
| **Instructions** | **≈ 261** | **≈ 256.5 (−5)** | **≈ 253 (−8)** |
| VDIV: loop / per block | 3 / not counted | 3 / 9 | 3 / 9 |

**B2R is under the Drum ceiling** (standalone static figures; the linked
count and the device are the gates, P4). **The plan is B2R + r6, ≈ 253**
(user, S093).

### 9.5 Wavetables and samples as Alkaline oscillators later (user question)

The fused read does not prevent it:
- A `typ`-style row would dispatch per block: the fused B2R for the table
  waves; for `smp`/`slp`/`wtb`, the existing typed reader into `buf` and then
  the plain `DiodeLadder_renderB2()`, which stays in the code. Nothing in the
  loop branches.
- Such an oscillator would also need `osc_setFreq()` (its bind and clamp
  rules) in place of `osc_setTableFreq()`, about +100 per block in those
  modes.

**But the Basic gate is the voice's dearest setting.** With samples or
wavetables selectable, the dearest is the unfused path:

| Path | Cost |
|---|---|
| Plain B2R loop | 198 |
| + reader: `slp` | 27 |
| + reader: `wtb` plain | 24–26 |
| + reader: `wtb` blend | 34–38 |
| + `osc_setFreq()` per block | ≈ +100 |
| **Dearest total** | **≈ 266–275** |

That is over Drum. So:
- the fusion's saving then holds only for the saw/square settings;
- keeping Basic would need fused twins of those readers (new loops, flash)
  or the Advanced class;
- r6 (−3.3) helps every mode.
