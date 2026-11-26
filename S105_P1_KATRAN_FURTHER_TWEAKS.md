# S105 P1 — Katran: further tweaks (plan, revision 2)

**Date:** 2026-11-26. **Branch:** `dev-ph7-alkaline` (the S103 P5 Katran;
S103 P6 and S104 P1 did not touch `Core/DSP/Instruments/Katran/`).
**Status:** plan, every question decided; ready for step 1. No source was changed.

**History:**
- **revision 1** (2026-11-25, written as S103 P7): coupling the two cores
  through their control state, and the ring → LFO path. Pinned until you had
  reorganised how Katran is presented. It is kept as **Part B**, with its
  references brought up to date.
- **revision 2:** your 2026-11-26 points are **Part A**. They are the
  reorganisation that revision 1 was waiting for, so Part B's exposure
  question (§B7) can now be answered against a real layout.
- **revision 3** (this one, 2026-11-26): **every recommendation in §G is
  accepted** (your answer). Added **§A8**: the buffer read becomes an
  oscillator type, per oscillator, in place of `src` (your earlier proposal,
  which revision 2 had missed), **per oscillator, not per core** (your
  decision, Q-A11). Pages, index map, labels, budget and laws are updated
  for it. **Status: every question is decided; the plan is ready for
  step 1.**

**Your points (2026-11-26) → where they are handled:**

| # | Your point | Section |
|---|---|---|
| P1 | `frq` should use a continuous table with step-synced rates (like the delay times), read backwards so fast is at the top; synced *in general*, drifting away and back at some settings | §A2 |
| P2 | Short names and Category + Long name pairs are incomprehensible (e.g. "Mod Osc Rate" shown as `frq`) | §A1 |
| P3 | F1's selectable filter and F2's cutoff do double work. Merge them into one non-state-variable filter built like Colloid's noise band: bipolar cutoff, unipolar width, bipolar offset (A against B) | §A4 |
| P4 | F3 last before the mixer, on the mix page instead of `spr`; output too quiet at some settings: gain, a drive ahead of the tone filter, or a slow normalise; keep the tone bipolar; drive on mix and `wid` elsewhere if the budget allows, otherwise just raise the gain | §A5 |
| P5 | Seven chaos rows for very little effect. Four instead: `chaos` (depths, the faster envelope coming in at higher depths), `cycle` (both rates, spread wider at faster rates), `skew` (bipolar, opposite on the other envelope), `intermodulation` (breaks the stable rate relationship, rotates skew at higher values) | §A3 |
| P6 | An option to switch the ring mod to FM, for one or both cores, if it fits | §A6 |
| P7 | Propose an additional way of progressively adding sidebands that reach higher into the spectrum | §A7 |
| P8 | (earlier proposal) the buffer read as an oscillator type per oscillator, not its own selector | §A8 |

**Your requests of 2026-11-25 (carried, Part B):** the four ways of
coupling the cores (preference 1 cross-level, 4 cross-sync, 3 kick
ping-pong, 2 anti-phase) and feature 5 (5a the right level measure, 5b
audio-rate AM). All optional: off reproduces the voice they start from.

**Labels:**
- **Code:** read from the tree, with a file and line.
- **Est.:** an estimate with its basis; the gate named measures it.
- **By ear:** a constant set in listening (host renders, then the device).

**Binding rules that apply** (`MEMORY.md`):
- constant CPU and the S089 P2 ceiling; Katran's gate is **380
  instructions a sample** (`tools/dsp_test/katran_cpu.py:47` `GATE`);
- `sizeof(KatranVoice)` ≤ 1,200 B (`InstrumentManager.c:375`);
- no backward compatibility, ever: changed keys and moved indices are not
  migrated;
- every code change carries an adjacent comment block.

No step here edits `MEMORY.md` or `knowledge_files/` (the list of statements
it makes out of date is at the end).

**Names:** Osc 1 (copies 1a, 1b) and Osc 2, as on
`katran_block_diagram_p2.svg`. The code calls them OSC A / OSC B.

---

## 0. Summary

### 0.1 Findings from the code (they explain part of what you hear)

| # | Finding | Where (code) | Effect |
|---|---|---|---|
| **F-1** | **CE2's level is never read.** `dp2` is applied as a fixed offset (F1 ± 48 semitones, F3 ± 24); `ce[1].level` is computed every block and read nowhere (only `ce[0].level` is read, at `:1090`) | `KatranVoice.c:1480`, `:1507`, `:1536` | `cy2`, `sk2` do nothing at all; `dp2` is a static cutoff offset, not an envelope depth. The design (`S103_KATRAN_NOISE_SYNTH.md` §3.3) meant CE2 → F1 ±4 octaves |
| **F-2** | **`init.kat` turns both envelopes off:** `kat_ce1_depth=0`, `kat_ce2_depth=63` (= 0) | `SD_CARD/Instrument/Katran/init.kat:18`, `:21`; `KatranVoice.c:1307`, `:1310` | the default sound has no chaos-envelope movement |
| **F-3** | **The ring is a switch, not a crossfade:** `rk = (bLevel > 0) ? 1 : 0`. At `lvB` 1 the dry signal is gone and the ring is at 1/127: core B is almost silent (`cB0 = 0`) | `KatranVoice.c:1254–1261` (`kat_outGains()`) | a level cliff from `lvB` 0 to 1, and low output at low `lvB` |
| **F-4** | **The output scale is 0.5** on top of the VCA gains (≤ 0.67 × 0.42 at `bal` centre) | `KatranVoice.h` `KAT_OUT_NORM`; `KatranVoice.c:1561–1562` | with F-3, the loop's level reaches the output about 18 dB down (Est.: init values) |

### 0.2 Part A: your 2026-11-26 points

| # | Change | Cost a sample (Est.) | Fits? |
|---|---|---:|---|
| A1 | Labels and pages: every row named Category + Long + Short that agree | 0 | yes |
| A2 | `frq` → `rte`: a step-synced rate table (32 bars .. /64, fast at the top), pulled back onto the clock | +1.0 | yes |
| A3 | Chaos: 7 rows → 4 (`chs cyc skw imd`); fixes F-1 | +0.5 | yes |
| A4 | One loop filter: the band (cutoff, width, offset) replaces F1 + F2 | **−10** (−8 .. −13) | yes: it pays for A5–A7 |
| A5 | F3 last (after the stereo matrix), on the mix page; the ring crossfade (F-3); the gain (F-4); a drive | +7.5 (the drive; the rest 0) | yes, by Est. |
| A6 | Ring → FM per core (Osc 2 phase-modulates that core's loop) | +4.5 | yes, by Est. |
| A7 | Sidebands: Osc 2 self-feedback (proposal) | +2.5 | yes, by Est. |
| A8 | `buf` as a fourth oscillator type, and a type and wave per oscillator; `src` removed | 0 | yes |
| | **Part A** | **366.6 → ≈ 372.6** (≈ 368 .. 376) | **under 380 by Est.** |

### 0.3 Part B: the core couplings (revision 1, carried)

| # | Feature | Cost (Est.) | Fits after Part A? |
|---|---|---:|---|
| 1 | Cross-level coupling (without the pitch nudge) | +2.1 | yes |
| 4 | Cross-sync, its 0-per-sample form (§B3.3) | 0 (flash +1.5 KB) | yes |
| 3 | Kick ping-pong | +0.3 | yes |
| 2 | Anti-phase modulation | +0.3 | yes |
| 5a | Ring → LFO: the right level measure (shared with 1) | +0.2 | yes |
| 5b | Ring → VCA: audio-rate AM (m = Osc 2) | +3 | **at the gate** |
| | **A + B (without 5b)** | **≈ 375.5** (≈ 371 .. 379) | **under 380 by Est.**; only the linked count decides (§C) |

- **RAM:** Part A +14 B net (the band filter frees 36 B, A8 takes 14);
  Part B +51 B: ≈ 1,100 → ≈ 1,165 B of the 1,200 B gate (§C).
- **Rows:** Part A leaves 58 rows (A8's Osc 2 wave takes 57) and **two free
  indices, 58–59**, for Part B (§A1.4, §B7).

---

## 1. What exists (code, S103 P5 tree)

| Item | Where | Note |
|---|---|---|
| The patch LFO rate: `katPlfoHz[frq]` (0.02 .. 20 Hz log) × 2^(lfm' (1.5 ringPrev + inv) + jitter) | `KatranVoice.c:422–423`; `KatranTables.h` | not tempo-related |
| The chaos envelopes: cycle `katCeBlocks[cyc]` (10 ms .. 60 s; a higher value is **slower**), skew = rise share, chs' jitters each cycle's time and peak | `kat_ceStart()` `:329`, `kat_ceStep()` `:371` | chs above 64 hurries the rise with `ringPrev` (`:379–383`) |
| CE1 → the stereo matrix gain (1 − dp1 + dp1 · CE1) | `:1089–1091` | the only envelope level read |
| CE2 → nothing (F-1) | `:1480`, `:1507`, `:1536` | |
| Other chs uses: the patch LFO's per-cycle jitter `chs · 0.1`, the rotation's per-turn jitter `chs · 0.5` | `:427`, `:1138–1141` | kept on `chs` (§A3.4) |
| **F1:** TPT SVF per core, types LP/BP/HP and the same three tracking the loop pitch; cutoff, res; two `kat_svfControl()` a block (≈ 99 each, 2 VDIV each) | per sample `:984–991`; control `:1499–1517` | |
| **F2:** a fixed one-pole low-cut after F1, `z += f2k (yf − z)` | `:992–995`; `katLowCutK[lct]` | the same job as F1 in HP |
| `spr`: above centre spreads the delay times ±12 % **and** F1 ±12 semitones; below centre cross-feeds | `:776`, `:1508`; `:1551` | |
| **F3:** one-pole LP / HP tilt on each core, **before** the stereo matrix | `kat_outRender()` `:1165`; control `:1519–1544` | |
| The ring and VCA folded into four ramped gains | `kat_outGains()` `:1252` | F-3 |
| Osc 2's loops (one oscillator, both cores read it) | `KatranOsc.c:110` plain, `:143` PWM D, `:170` warp, `:202` arena | |
| Osc 1's sync test `st = wrapB · KAT_SYNC_WRAP + dSelf[n]` | `KatranOsc.c:298` | own core only |
| The delay read, Q16 position `((w + n + KAT_RING) << 16) − tq` | `kat_delayBody()` `:845–870` | |
| The sequencer clock: `seq_elapsedPpqTicks` (96 PPQ since the last start / pattern reset) is file-static, **no getter**; `seq_getBpm()` is public | `sequencer.c:87`; `sequencer.h:304` | A2 needs a getter |
| The step-synced delay table: `crumpBit_delayTicks[120]` (rows 4..123), StepScale's 14 stops /64 .. /2 exact, equal ratios between; built by `tools/plots/gen_crumpbit_tables.py:132` | `CrumpBitTables.h:21–28` | A2 takes its law |
| Colloid's noise band: tone ±, width, one-pole HP → one-pole LP | `col_noiseControl()` `ColloidVoice.c:1673`; per sample `:1821–1827` | A4 takes its corner law |
| **CPU now:** loops 295.0 + per block 2,290 / 32 = **366.6**; core A 85, core B 93 (per half-rate iteration), out 37 | `tools/dsp_test/baselines/s103/katran_cpu_p5.txt` | gate 380 |

---

# Part A — your 2026-11-26 points

## A1. Names and pages

### A1.1 The rule

- **The short name abbreviates the long name**, or, for a group named by
  its category (Chaos), the category. Never a word that appears in neither.
- **Category + Long reads as a phrase** that says what the knob does
  ("Mod Osc Rate", "Filter Width", "Ring/FM Depth").
- **The category is the block on the diagram:** Osc 1, Osc 2, Exciter,
  Torcido, Filter, Delay, Ring/FM, VCA, Chaos, Mod Osc, Stereo, Voice.
- **A short name may repeat only on different pages, for the same word:**
  `drv` (Torcido Drive, Voice Drive), `wid` (Stereo Width, Filter Width),
  `dpt` (Mod Osc Depth, Ring/FM Depth). The single-parameter view shows the
  category (`S103_P3_SHORT_NAMES_MERGE.md` §1). If you would rather have no
  repeats, the alternatives are in Q-A1.
- The S103 P3 Tier B reuses that broke the rule are undone (`frq` for
  Rate, `dpt` for Level, `bln` for Wet, `wow` for Drift, `lvA` / `lvB`).

### A1.2 The pages

| Page | Today | Proposed |
|---|---|---|
| 0 osc | `coa fin pos dtn` \| `wav typ src dmp` | `coa fin pos dtn` \| `typ wav snc dmp` (Osc 1; A8) |
| 1 aeg | `run kik cy1 sk1` \| `dp1 cy2 sk2 dp2` | `run kik exc noi` \| `chs cyc skw imd` |
| 2 mod | `dst amt chs rot` \| `frq mod dpt bal` | `dst amt rot wid` \| `rte slf dpt bal` |
| 3 fm | `coa fin pos lvB` \| `lvA noi snc ---` | `coa fin pos typ` \| `wav dpt r/f sdb` (Osc 2; A8) |
| 4 click | `drv bln tim fbk` \| `wow dtk --- ---` \| (fx) | `drv bln tim fbk` \| `wow dtk spr ---` \| (fx, unchanged) |
| 5 filter | `frq res typ lct` \| `cut --- --- ---` | `cut wid ofs ---` |
| 6 LFO | the platform's | unchanged |
| 7 mix | `vol pan spr wid` | `vol pan ton drv` |

- **aeg** holds everything that puts energy into the loop and moves it:
  run, the kick, the exciter level and noise (the kick boosts both,
  `kat_kickControl()` `:530–531`), then the four chaos rows.
- **mod** gets the stereo pair (`rot`, `wid`) together; the patch LFO and
  the VCA balance stay.
- **osc** is Osc 1 alone: pitch, its type and wave (A8), the sync
  threshold (it resets Osc 1) and the buffer damping.
- **fm** is Osc 2 alone: pitch, its type and wave (A8), and what it does to
  the cores (ring or FM, sidebands).
- **click** takes `spr`: after A4 it only moves the delay times and the
  cross-feedback, both loop settings.
- **filter** has one screen (a fourth cell free for Q-A5).
- **mix** matches Alkaline and Colloid: `vol pan ton drv`.

### A1.3 Every row

Platform rows not listed (LFO page, velocity `dst`/`amt`) are unchanged.
**Bold** = changed.

| Idx | Key | Cell | Today: Cat / Long / Short | Proposed: Cat / Long / Short | Note |
|---:|---|---|---|---|---|
| 0 | `osc1_pitch_coarse` | osc 0 | Oscilltr Coarse coa | **Osc 1** Coarse coa | |
| 1 | `osc1_pitch_fine` | osc 1 | Oscilltr Fine fin | **Osc 1** Fine fin | |
| 2 | `osc1_position` | osc 2 | Oscilltr Position pos | **Osc 1** Position pos | |
| 3 | `kat_spread_a` | osc 3 | Oscilltr Detune dtn | **Osc 1** Detune dtn | 1a against 1b |
| 5 | `osc1_type` | **osc 4** | Oscilltr Type typ | **Osc 1** Type typ | **wav wtb pwm buf** (A8) |
| 4 | `osc1_wave` | **osc 5** | Oscilltr Waveform wav | **Osc 1** Waveform wav | Osc 1's only (A8); `---` under buf |
| 27 | `kat_sync` | **osc 6** | Oscilltr SyncThr snc | **Osc 1** SyncThr snc | moved from fm |
| 7 | `kat_damp` | osc 7 | Oscilltr Damping dmp | **Buffer** Damping dmp | `---` unless an oscillator is buf |
| 8 | `kat_run` | aeg 0 | Voice Run run | unchanged | |
| 9 | `kat_kick` | aeg 1 | Trigger Kick kik | unchanged | |
| 25 | `kat_level_a` | **aeg 2** | Oscilltr Exciter lvA | **Exciter Level exc** | Osc 1's level into Torcido ch 2 |
| 26 | `kat_noise` | **aeg 3** | Noise NoiseMix noi | **Exciter** NoiseMix noi | |
| 16 | `kat_chaos` | **aeg 4** | Envelope Chaos chs | **Chaos Depth** chs | new law, §A3 |
| 10 | **`kat_chaos_cycle`** | aeg 5 | (Envelope Cycle cy1) | **Chaos Cycle cyc** | §A3; higher = faster |
| 11 | **`kat_chaos_skew`** | aeg 6 | (Envelope Skew sk1) | **Chaos Skew skw** | PM63 |
| 12 | **`kat_chaos_imod`** | aeg 7 | (Envelope Depth dp1) | **Chaos Intermod imd** | |
| 37 | `kat_rotate` (**was 57**) | **mod 2** | Voice Rotation rot | **Stereo** Rotation rot | moved index (§A1.4) |
| 42 | `kat_width` | **mod 3** | Voice Width wid | **Stereo** Width wid | |
| 17 | `kat_plfo_rate` | mod 4 | Mod Osc Rate **frq** | Mod Osc Rate **rte** | step names at the stops, §A2 |
| 18 | `kat_plfo_mod` | mod 5 | Mod Osc SelfMod mod | Mod Osc SelfMod **slf** | |
| 19 | `kat_plfo_level` | mod 6 | Mod Osc Level dpt | Mod Osc **Depth** dpt | |
| 20 | `kat_vca` | mod 7 | Voice VCA Bal bal | **VCA Balance** bal | |
| 21 | `kat_b_coarse` | fm 0 | FM Coarse coa | **Osc 2** Coarse coa | |
| 22 | `kat_b_fine` | fm 1 | FM Fine fin | **Osc 2** Fine fin | |
| 23 | `kat_b_pos` | fm 2 | FM Position pos | **Osc 2** Position pos | |
| 6 | **`kat_b_type`** (was `kat_source`) | fm 3 | (Oscilltr Source src) | **Osc 2 Type typ** | **wav wtb pwm buf** (A8) |
| 57 | **`kat_b_wave`** | fm 4 | — | **Osc 2 Waveform wav** | A8; `---` under buf |
| 24 | **`kat_ring_depth`** (was `kat_b_level`) | fm 5 | FM Level lvB | **Ring/FM Depth dpt** | the ring crossfade (§A5.2) or the FM depth (§A6) |
| 13 | **`kat_ring_mode`** | fm 6 | — | **Ring/FM Mode r/f** | `rng fmA fmB fAB` |
| 14 | **`kat_ring_sideband`** | fm 7 | — | **Ring/FM Sideband sdb** | §A7 |
| 28 | `kat_drive` | click 0 | Delay Drive drv | **Torcido** Drive drv | the loop's shaper |
| 29 | `kat_wet` | click 1 | Delay Wet bln | **Torcido Blend** bln | |
| 30 | `kat_time` | click 2 | Delay Time tim | unchanged | |
| 31 | `kat_feedback` | click 3 | Delay Feedback fbk | unchanged | |
| 32 | `kat_drift` | click 4 | Delay Drift wow | Delay **WowFlutr** wow | |
| 33 | `kat_track` | click 5 | Delay Tracking dtk | unchanged | follows Osc 2's pitch |
| 41 | `kat_spread` | **click 6** | Voice Spread spr | **Delay** Spread spr | times and cross-feedback only (§A4.4) |
| 34 | **`kat_flt_cut`** | filter 0 | (Filter Cutoff frq) | **Filter Cutoff cut** | PM63, §A4 |
| 35 | **`kat_flt_width`** | filter 1 | (Filter Reso res) | **Filter Width wid** | |
| 36 | **`kat_flt_offset`** | filter 2 | (Filter FiltType typ) | **Filter Offset ofs** | PM63 |
| 39 | `instrument_vol` | mix 0 | Voice Volume vol | unchanged | |
| 40 | `instrument_pan` | mix 1 | Voice Panning pan | unchanged | |
| 38 | `kat_out_cut` | **mix 2** | Filter OutFilt cut | **Voice Tone ton** | PM63; after the matrix (§A5) |
| 15 | **`kat_out_drive`** | mix 3 | — | **Voice Drive drv** | §A5.3; Alkaline's labels |

### A1.4 Index map and keys

- **Removed keys (11):** `kat_ce1_cycle`, `kat_ce1_skew`, `kat_ce1_depth`,
  `kat_ce2_cycle`, `kat_ce2_skew`, `kat_ce2_depth`, `kat_loop_cutoff`,
  `kat_loop_reso`, `kat_loop_type`, `kat_low_cut`, `kat_source` (A8); and
  `kat_b_level` renamed `kat_ring_depth` (its law changes, §A5.2).
- **Added keys (11):** `kat_b_type` 6, `kat_chaos_cycle` 10,
  `kat_chaos_skew` 11, `kat_chaos_imod` 12, `kat_ring_mode` 13,
  `kat_ring_sideband` 14, `kat_out_drive` 15, `kat_flt_cut` 34,
  `kat_flt_width` 35, `kat_flt_offset` 36, `kat_b_wave` 57.
- **Index 37** (`kat_low_cut`) would be a hole in a table that must have
  one row per index (`KatranParameters.c` asserts). **`kat_rotate` moves
  57 → 37**, and `kat_b_wave` takes 57 (Q-A5 decided: tracking is automatic,
  no row).
- **`KAT_PARAM_COUNT` stays 58**; `KATRAN_PARAM_DESCRIPTOR_COUNT` 58.
  **Free: 58, 59** (60..63 stay reserved for the insert effect).
- **Kept keys with a new meaning:** `kat_chaos` (a depth now),
  `kat_plfo_rate` (the synced table). No migration (standing rule): old
  Scenes and Kits with a Katran slot load their old bytes into whatever row
  now holds the index. `init.kat` is regenerated
  (`tools/katran/make_init_kat.py`, which parses the descriptor table).

### A1.5 Label ids

| Table | Added | Removed (Katran-only, unused after) | Count |
|---|---|---|---|
| Categories | `Osc 1`, `Osc 2`, `Buffer`, `Exciter`, `Chaos`, `Stereo`, `VCA`, `Ring/FM`, `Torcido` | — | 48 → 57 |
| Long | `Intermod`, `Sideband` | `Exciter`, `VCA Bal`, `Low Cut`, `OutFilt`, `Wet`, `Drift`, `Chaos` | 196 → 191 |
| Short | `exc`, `slf`, `r/f`, `sdb`; `cy1 sk1 dp1` become `cyc skw imd` in place | `cy2`, `sk2`, `dp2`, `lct`, `src` | 224 → 223 |

- `LONG_SOURCE` ("Source") stays: the S099 chorus rows use it.
  `SHORT_SRCE` ("src") was Katran's only.

- Each removed id was checked to be used by `KatranParameters.c` only
  (`grep` over `Core/`, `menu.h` and `MenuLabels.c` aside). `lvA` / `lvB`
  stay: Tetrakt uses them.
- All counts stay ≤ 256 (`MenuLabels.c` asserts). Widths: category 8, long
  9, short 3 characters.
- The `r/f` selector reuses `ONOFF_TAG_KAT_LOOP_TYPE` (tag 10,
  `MenuText.h:452`), freed by `typ`: renamed `ONOFF_TAG_KAT_RING_MODE`,
  names `{4} rng fmA fmB fAB`.
- The two type rows reuse `ONOFF_TAG_KAT_SOURCE` (tag 4, `MenuText.h:451`),
  freed by `src`: renamed `ONOFF_TAG_KAT_OSC_TYPE`, names
  `{4} wav wtb pwm buf` (`katSourceNames` removed).

---

## A2. `frq` → a step-synced rate (`rte`)

### A2.1 The table

A new generated table, **`katPlfoTicks[128]`**, in place of `katPlfoHz[]`
(512 B either way). Its law is the delay curve's
(`gen_crumpbit_tables.py` `delay_ticks_table()`): the stops sit exactly on
their rows, with equal ratios between neighbouring stops (≈ 6 % a row, about
a semitone). **Read backwards: row 127 is the fastest.**

| Row | Stop | Ticks | Hz at 120 BPM |
|---:|---|---:|---:|
| 127 | /64 | 6 | 32 |
| 122 | 32t | 8 | 24 |
| 115 | /32 | 12 | 16 |
| 111 | 16t | 16 | 12 |
| 109 | d32 | 18 | 10.7 |
| 104 | /16 | 24 | 8 |
| 99 | 8t | 32 | 6 |
| 97 | d16 | 36 | 5.33 |
| 92 | /8 | 48 | 4 |
| 88 | 4t | 64 | 3 |
| 86 | d/8 | 72 | 2.67 |
| 81 | /4 | 96 | 2 |
| 74 | d/4 | 144 | 1.33 |
| 69 | /2 | 192 | 1 |
| 63 | d/2 | 288 | 0.67 |
| 58 | 1br | 384 | 0.5 |
| 46 | 2br | 768 | 0.25 |
| 35 | 4br | 1,536 | 0.125 |
| 23 | 8br | 3,072 | 0.0625 |
| 12 | 16b | 6,144 | 0.031 |
| 0 | 32b | 12,288 | 0.0156 |

- Rows computed with the generator's rounding: row = 127 −
  round(127 · log2(t / 6) / log2(2,048)).
- The span is 11 octaves; today's is 10 (0.02 .. 20 Hz). `init.kat`'s 74
  (1.1 Hz today) lands on d/4 (1.33 Hz at 120 BPM).
- The table stores **1 / ticks**, so the block's phase step is
  `bpm × invTicks[r] × 96 × 32 / (60 fs)`: a multiply, no division.

### A2.2 Why not the delay table itself

`crumpBit_delayTicks` spans /64 .. /2 (rows 4..123), with tails to 2 samples
and 2.87 s. As LFO periods that is ≈ 0.35 .. 32 Hz at 120 BPM: the bottom
four octaves of today's range (down to 0.02 Hz, the slow drone sweeps) would
be lost, and the top tail reaches audio rate. The proposal keeps its law and
extends it to 32 bars. If you would rather use exactly the delay table,
that is Q-A2.

### A2.3 Synced in general, drifting away and back

The rate law stays: rate = synced rate × 2^(lfm' (1.5 ringPrev + inv) +
jitter). Today's modulation therefore still bends the rate. A
**clock pull** brings the phase back:

- **Clock phase:** φc = frac(E × invTicks[r]), where E is the elapsed tick
  count, extrapolated between ticks from the tempo (a tick is 5.2 ms at
  120 BPM, ≈ 7 blocks). Computed in double precision (the F765's FPU has
  it), so it stays exact over a day of play.
- **Each block:** e = φc − φ wrapped into (−0.5, +0.5];
  φ += step + κ · e.
- **Pull strength** κ = κ0 · (1 − 0.9 lfm'), κ0 ≈ 0.01 a block (≈ 70 ms;
  by ear):
  - `slf` 0 and `chs` 0: locked to the clock;
  - rising `slf` (and `chs`'s per-cycle jitter): the rate wanders, the phase
    leaves the clock, and the weaker pull brings it back;
  - `slf` 127: κ0 / 10, still above 0, so it always returns eventually.
- **Transport stopped:** κ = 0, free-running at the synced rate.
- **A sequencer reset** (E jumps) is a phase error like any other: the
  phase glides back, it never jumps.
- **New accessor:** `uint32_t seq_getElapsedTicks(void)` in
  `sequencer.c/h`, returning `seq_elapsedPpqTicks` (one aligned 32-bit load,
  atomic against TIM3). Read once a block from the render; it changes no
  sequencer behaviour.
- Feature 5a (§B6) changes only the `ringPrev` term inside this law.

### A2.4 Display

A tagged `DTYPE_0B127` (the `DTYPE_0B127_FX_DEPTH` pattern, `menu.h:505–521`;
tag 2 is free): **`DTYPE_0B127_KAT_RATE`**. The 21 stop rows show their name;
other rows show the number. Names: the first 16 are CrumpBit's
(`crumpBit_grainStopNames`, `CrumpBitParameters.c:456`), plus `2br 4br 8br
16b 32b`. The generator also writes the stop rows (`katPlfoStopRow[21]`).

### A2.5 Cost and RAM

- Two calls (`seq_getBpm()`, `seq_getElapsedTicks()`), one double multiply
  and floor, the pull: ≈ 30–40 a block, **≈ +1 a sample**. Per sample 0.
- RAM: the tick extrapolation (last E, blocks since it changed): +8 B.
  Flash: the stop names and rows ≈ +105 B.

---

## A3. The chaos envelopes: 7 rows → 4

### A3.1 What was wrong (F-1, F-2)

CE2's level never reaches the filters, and `init.kat` sets both depths to
0. The two envelopes run, but only CE1 can be heard, and only once `dp1` is
raised. Four rows replace the seven. The engine stays: two `KatCe`
envelopes, `kat_ceStart()` / `kat_ceStep()`, the same destinations.

### A3.2 Roles

- **The fast envelope** (today's CE1) → the output gain, through the stereo
  matrix (`:1090`, unchanged form).
- **The slow envelope** (today's CE2) → the band filter's centre (§A4.5),
  and F3 by half. **This fixes F-1:** the term becomes depth × (level − 0.5),
  not a fixed offset.

### A3.3 The four rows

| Row | Law (per block) |
|---|---|
| **`chs` Chaos Depth** (0..127, x = chs / 127) | slow depth d_s = x (2 − x): comes in first. Fast depth d_f = x²: **the faster envelope comes in more at higher depths**. Gain = 1 − d_f + d_f · L_f; band centre += 48 · d_s · (L_s − 0.5) semitones (± 24 about the cutoff; F3 ± 12); by ear |
| **`cyc` Chaos Cycle** (0..127, higher = **faster**) | T_f = `katCeBlocks[127 − cyc]` (10 ms .. 60 s, the table read backwards, as `rte`); T_s = N · T_f with N = 1 + round(7 (cyc / 127)²) (1 .. 8): **the faster the rates, the wider the spread** (at the top, 10 ms against 82 ms). An integer N makes the pair's pattern repeat exactly (the "stable relationship" that `imd` breaks) |
| **`skw` Chaos Skew** (PM63, s = −1 .. +1) | rise share of the slow one 0.5 (1 + σ_s), of the fast one 0.5 (1 + σ_f), with (σ_s, σ_f) = (s, −s) rotated by φ (below), clamped to ±1. At φ = 0: **opposite on the other envelope**. The ≥ 3-block stage rule of `kat_ceStart()` stays |
| **`imd` Chaos Intermod** (0..127, m = imd / 127) | (i) each cycle's time × 2^(m r) and peak 1 − m \|r2\| / 2, drawn per envelope (today's chs law, `:337`, `:353`): the ratio stops being exact; (ii) cross-rate coupling: the fast envelope's step × (1 + 0.8 m (2 L_s − 1)), the slow one's × (1 − 0.4 m (2 L_f − 1)); (iii) above m = 0.5, **the skew pair rotates**: φ += (π / 2)(2m − 1) per slow cycle (a quarter turn a cycle at 127: opposite → same direction → swapped → …); (iv) the loud-loop hurry of the rise (today chs > 64, `:379–383`) moves to m > 0.5. All constants by ear |

- At `skw` centre the rotation has nothing to rotate. Q-A4 asks whether
  `imd` should also give a skew of its own there.
- `imd` 0, `chs` > 0: two envelopes in an exact N : 1 rhythm. Rising `imd`
  loosens it step by step: per-cycle drift, then mutual pull, then the
  rotating shapes.

### A3.4 What stays on `chs`

The patch LFO's per-cycle jitter (`chs · 0.1`, `:427`) and the rotation's
per-turn jitter (`chs · 0.5`, `:1141`). `chs` stays the "how chaotic"
knob; `imd` is about the two envelopes' relationship.

### A3.5 Cost and RAM

≈ 15 a block (+0.5 a sample). RAM: the skew phase φ, +4 B. Row bytes 7 → 4,
and the three freed bytes hold `r/f`, `sdb` and `drv` (0 B net).

---

## A4. One loop filter: the band (F1 + F2 merged)

### A4.1 Why

F1 in HP mode and F2's low-cut are both high-passes in series; F1's `typ`
picks one curve where the band below needs none. One band does both jobs
with three rows: **cutoff (PM63), width (0..127), offset (PM63)**. It is
not a state-variable filter: two one-poles and a resonance feedback (A4.3).

### A4.2 The corners (Colloid's law, `ColloidVoice.c:1673`)

t = (cut − 63) / 63, w = wid / 127, in semitones above 20 Hz (0 .. 115.7
core A; 0 .. 104 core B, its 8.1 kHz ceiling `KAT_CUT_ST_MAX_H`):

| | HP corner | LP corner |
|---|---|---|
| width 127, t > 0 (**raising cutoff cuts low**) | t · 115.7 | 115.7 |
| width 127, t < 0 (**lowering it cuts high**) | 0 | (1 + t) · 115.7 |
| width 127, t = 0 | 0 (20 Hz) | 115.7: the full band |
| width w | c + w (h1 − c) | c + w (l1 − c) |

c = (h1 + l1) / 2: **lowering the width closes both corners on their
centre**, a band-pass around the cutoff's point.

### A4.3 Resonance as the width falls

- **Form:** the band's output feeds back into its input, one sample late:
  x' = mk · x + r · y[n − 1].
- **Peak:** two coincident one-poles pass 0.5 at their centre (Gc), so the
  peak is 1 / (1 − r Gc).
- **Law:** r = r_max · max(0, 1 − w / 0.5)²: no resonance in the upper half
  of the width, rising below it. r_max = 1.8, so r · Gc ≤ 0.9 (+20 dB at the
  centre).
- **Make-up:** mk = 1 + 1.0 · (1 − w)², for the band's loss at its centre
  (Colloid's make-up idea, smaller here because the loop has its own gain).
- **Stability:** a loop gain below 1 is stable whatever the phase of the
  one-sample delay (small gain), so the bound holds per sample.
- All constants by ear.

### A4.4 Offset: core A against core B

Both of core A's corners move by +24 · o semitones and both of core B's by
−24 · o (o = (ofs − 63) / 63; ±24 by ear): **positive raises A's band
against B's, negative the reverse.** This replaces `spr`'s F1 spread
(`:1508`), so `spr` keeps only the delay-time spread and the cross-feedback.

### A4.5 What else moves the band

Added to both corners, as F1's semitone sum is today: the patch LFO
`cv · lfl · 12` (`:1507`), the slow chaos envelope 48 · d_s · (L_s − 0.5)
(§A3), and later Part B's features 1 and 2. The corners slew by
`KAT_SLEW` 0.28 a block as F1's cutoff does.

**Tracking the loop pitch** (today's LPt / BPt / HPt): proposed automatic,
Colloid's rule. Both corners move by 0.85 (1 − w) · (loop pitch − reference)
semitones: none at width 127, most at a narrow band, so a narrow resonant
band stays on the same harmonic of the comb as `tim` moves. Alternatives in
Q-A5.

### A4.6 Per sample (each core)

```
x'  = mk * xin + r * sL         (the resonance; sL is last sample's output)
sH += gH * (x' - sH);  hp = x' - sH     (HP one-pole, F2's form :992-995)
sL += gL * (hp - sL)                    (LP one-pole)
u   = sL                                (replaces u = F2(F1(xin)))
gH += dgH;  gL += dgL                   (per-sample ramps, as F1's)
```

- Both corners use F2's impulse-invariant one-pole: G = 1 − exp(−2π f / fs),
  two `alk_exp2f()` a corner. No `tan()` and no division (F1 had 2 VDIV a
  core).
- Core B uses its half-rate G, as F2 does today (`f2K (2 − f2K)`).

### A4.7 Cost, RAM, fallback

| | Today | Band | Est. |
|---|---|---|---|
| Per sample, core A | SVF 10 + mix 3 + 3 ramps + F2 2–3 ≈ 16–18 | 9 | −7 .. −9 |
| Per sample, core B (half rate) | the same per iteration | | −3.5 .. −4.5 an output sample |
| Per block | 2 × `kat_svfControl()` ≈ 198 + F2 | 4 corners × 2 exp2 + the laws ≈ 160 | −1.2 a sample |
| **Total** | | | **≈ −10 (−8 .. −13)** |

- Also fewer live registers (14 → 9 in the filter), which may save more in
  the linked count.
- **RAM:** `f1[2]` (104 B) + `f2x[2]`, `f2K` (12 B) → 10 floats a core
  (80 B): **−36 B**.
- **Fallback, if the one-pole band sounds too soft** (6 dB an octave a
  side): the LP corner as a resonant 2-pole (the TPT pair fixed to LP, no
  mode mix), the HP one-pole kept. Est. −5 instead of −10, which would put
  the drive on the drop ladder (§C).

---

## A5. Output: F3 last, the level, a drive

### A5.1 Order

Core loops (ring / VCA, **then the drive**) → stereo matrix (rotation, width,
CE1, the output scale) → **F3** → the int16 store → the mixer. F3 moves after
the matrix, onto L and R. It costs the same and stays the bipolar LP / HP
tilt (`ton`, PM63, 63 = open, bit-exact pass).

### A5.2 The level

| # | Change | Cost |
|---|---|---|
| L1 | **The ring as a crossfade** (fixes F-3): b = `dpt` / 127; cA0 = (1 − b + g2), cA1 = b; cB0 = (1 − b)(1 + g1), cB1 = b (1 + g1) (× the int16 scales, `kat_outGains()`). b = 0 is today's `lvB` 0 exactly; b = 1 is today's `lvB` 127 exactly; between, no cliff | 0 (per block) |
| L2 | **The output scale** `KAT_OUT_NORM`: set by measurement. `init.kat`'s RMS in the host should match Colloid's `init.col` (both rendered in the host model); est. ×2 to ×3 from today's 0.5 | 0 |
| L3 | The drive (A5.3) catches the peaks that L2 brings | below |

### A5.3 The drive (`Voice Drive`, mix page)

- **Where:** in each core's loop, on o before its store (`:998–1005`).
  This is ahead of F3, as you asked, and 2.5 cheaper than on L/R after the
  matrix, because core B runs at half rate.
- **Law:** input gain G = 2^(4 drv') (×1 .. ×16), folded into o0 / o1
  (free). Then c = clamp(o · s, ±1.5), y = c (A − B c²), with A, B per block
  carrying the cubic (4/27), the make-up M = G^−0.6 (by ear) and the int16
  scale: 5 operations.
- **drv 0 is bit-identical to no drive:** two more core wrappers (with and
  without the clip), chosen per block, as the oscillator forms are. The gate
  counts the dearer one.
- **Alternative (Q-A8):** on L and R after the matrix (CE1's swells then
  push into the drive, and the cores intermodulate): +10 instead of +7.5.
- **Cost:** **+7.5 a sample** (+5 core A, +2.5 core B). Flash +≈ 1.2 KB (the
  two wrappers). RAM +8 B (A, B).

### A5.4 Not proposed: a slow normalise

A level follower that raises the gain of a quiet output would cost ≈ +1. It
would also undo `run`'s decay tails, the kick's swell and CE1's dips: the
voice's dynamics are mostly slow gain changes. L1 + L2 + the drive raise the
level without that. If you still want it, it would have to freeze while `gr`
falls (Q-A8).

---

## A6. Ring → FM

### A6.1 The modes (`r/f`, per core)

| `r/f` | Core A | Core B |
|---|---|---|
| `rng` | ring (today, with L1's crossfade) | ring |
| `fmA` | FM | ring |
| `fmB` | ring | FM |
| `fAB` | FM | FM |

A core in FM drops its ring (o1 = 0, o0 = its dry VCA gain), and `dpt` is
its FM depth.

### A6.2 The FM: Osc 2 phase-modulates the core's loop (recommended)

- **What:** the delay read is displaced by Osc 2 every sample:
  pos −= (kq[c] · B[n]) >> 15. This is FM of everything in the loop. Core B
  reads B[2n], in half-rate samples.
- **Why here:** the ring acts on the core's own signal (Torcido ch 1, which
  is the delay output), and so does this. The FM is heard at once, since t1
  is computed from the same read, and it builds up with feedback: each pass
  adds sidebands.
- **Depth:** k = `dpt`'² × K_MAX samples, K_MAX ≈ 8 full rate, 4 half rate
  (by ear). A partial at f gets an index of 2π f k / fs (8 samples: 1.1 at
  1 kHz, 5.7 at 5 kHz).
- **The staged-read rule:** a block may only read samples already written.
  The time clamp becomes T ≥ 32 + k (core B: 16 + k / 2), per block. With k
  = 0 it is today's clamp.
- **Identity:** `rng` (kq 0) is exactly today's integer read.
- **Cost:** a load, an SMULWB and a subtract: +3 core A, +1.5 core B =
  **+4.5**. RAM +8 B (`kq[2]`).
- **Interaction:** it leaves `kat_aBody()` untouched, so it does not compete
  with Part B's cross-sync, which changes that loop.

### A6.3 The alternative: exciter FM (Osc 2 → Osc 1)

- **What:** Osc 1's phase step gets `inc + SMULWB(kf, B[n])`, with
  kf = I × incB (a constant index across pitch). The B load is already there
  for the sync test.
- **Cost:** +3 .. +4.5 (PWM D needs its second phase stepped too).
- **What it means:** the classic two-operator timbre, but it enters the loop
  only through the exciter: filtered, delayed, partly re-clocked by the
  sync.
- **Why not first:** it touches all eight `kat_aBody()` wrappers, which
  Part B's cross-sync changes too, and a negative increment trips the
  wrap-based row swap (harmless, but untidy). Q-A9.

---

## A7. Sidebands that reach higher (proposal)

### A7.1 Osc 2 self-feedback (recommended; `Ring/FM Sideband`)

- **What:** Osc 2 reads its table at ph + SMULWB(kfb, y[n − 1]), its own
  last output. This is a DX-style operator feedback. Raising `sdb` adds
  harmonics to Osc 2 **one order after another**.
- **What you hear:** the ring's sidebands (t1's partials ± Osc 2's) and the
  FM's (A6) extend further up the spectrum as `sdb` rises. At the top Osc 2
  turns noisy, which suits Katran.
- **Where:** the four Osc 2 loops (`KatranOsc.c:110`, `:143`, `:170`,
  `:202`). The phase step and the wrap flag use the unmodulated phase, so
  the row swap and the sync's wrap bit are unchanged.
- **Wave dependence:** the clearest effect is on Sin and Tri; on Saw and Rec
  it brightens and roughens. Against aliasing at a high Osc 2 pitch, kfb is
  scaled down per block above ≈ 2 kHz (by ear).
- **Identity:** `sdb` 0 (kfb 0) gives today's Osc 2 exactly.
- **Cost:** +2 a sample (+1 for PWM D's second read): **+2 .. +3**. RAM
  +8 B.

### A7.2 Considered

| Alternative | Why not first |
|---|---|
| A Chebyshev shaper on the ring's modulator (B → T2 → T3 blend) | +3; doubles B's band each order, so it aliases with the band-limited stock rows; weaker progression on Saw / Rec |
| A6.2's loop PM as the sideband control, with FM as A6.3 | it would work (the index rises with each partial's frequency), but both together cost ≈ +9 |
| Osc 2's `pos` (PWM width / wavetable frame) | already does this, so it is not an addition |

---

## A8. The buffer read as an oscillator type, per oscillator

### A8.1 What `src` is today

- **`src` selects oscillators, not cores:**
  - `A` = Osc 1 (code: OSC A), both of its copies: 1a in core A and 1b in
    core B;
  - `B` = Osc 2 (OSC B);
  - `AB` = both oscillators; `off` = neither.
  - (Code: `aArena` / `bArena` from `KAT_SRC_A` / `KAT_SRC_B`,
    `KatranVoice.c:591–592`; `aArena` picks the rows of both copies,
    `:651–655`.)
- **What a selected oscillator does:** it reads the buffer instead of
  `typ` / `wav`. The other oscillator plays the shared form
  (`S103_KATRAN_NOISE_SYNTH.md` §3.4).
- **`typ` and `wav` are one pair for both oscillators** (one wavetable,
  K-D8).

### A8.2 The change

`buf` becomes a fourth type, and each oscillator gets its own type and wave:

| Row | Values | Replaces |
|---|---|---|
| `osc1_type` (Osc 1 Type) | `wav wtb pwm buf` | the shared `typ`, and `src`'s A bit |
| `osc1_wave` (Osc 1 Waveform) | Osc 1's list for its type; `---` under buf | the shared `wav` |
| `kat_b_type` (Osc 2 Type, index 6) | `wav wtb pwm buf` | `src`'s B bit |
| `kat_b_wave` (Osc 2 Waveform, index 57) | Osc 2's list for its type; `---` under buf | — |

**Why it is better than `src`:**
- **The type is chosen in one place:** an oscillator reads the buffer or
  a table, not a table type overridden by a second selector.
- **Each oscillator gets its own wave.** For example a pulse exciter on
  Osc 1 with a sine Osc 2: the ring's cleanest modulator, and the wave on
  which A7's sideband feedback is clearest. Today that pair is impossible.
- **All four of `src`'s combinations stay reachable** (neither, either, both).

**Per oscillator, not per core** (your decision, Q-A11): Osc 1's two copies
(1a in core A, 1b in core B) share Osc 1's type, as they share its wave. A
per-core type would have cost a third type row.

### A8.3 What changes in the code

| Where | Change |
|---|---|
| `Katran_setOscType()` (`KatranVoice.c:1722`; its writer `InstrumentManager.c:5543–5548`, `IM_SPECIAL_OSC_TYPE`) | takes 0..3. Today any value above 1 means pwm, so 3 would play pwm. 3 sets Osc 1's buffer flag and keeps `osc.oscType` as its table type, so leaving buf restores the wave |
| Osc 2's type and wave | two plain row bytes (`bType`, `bWave`), converted in the async; `bWave` clamped to `bType`'s list there |
| `kat_oscControl()` (`:587`) | Osc 2's rows, octave and PWM width table (`dTab`) from its own type and wave instead of Osc 1's |
| `Katran_calcVoiceSyncBlock()` (`:1601`) | `tableForm`, `pwmForm` and the PWM D `sign` per oscillator (today one set from the shared wave, `:1604–1610`); the form selection is already separate per oscillator |
| The wavetable bind | one bind per oscillator when both are wtb with different waves. A second bind key for Osc 2 (index, generation, size, data); `kat_wtbBind()` takes the oscillator |
| `instrumentManager_waveDomainMax()` (`InstrumentManager.c:1290`) | a Katran branch for `kat_b_wave` from Osc 2's type. Without one, the key falls into the `_wave` modulator-list path (`:1352–1356`) and gets the wrong range |
| `katranParameters_cellInactive()` | each `wav` is `---` under its oscillator's buf; `dmp` is `---` unless an oscillator is buf; Osc 1's `pos` rule reads Osc 1's type |
| The selector names | tag 4 → `ONOFF_TAG_KAT_OSC_TYPE`, `{4} wav wtb pwm buf` (§A1.5); `KAT_SRC_*` and `katSourceNames` removed |

### A8.4 Cost and RAM

- **Per sample: 0.** Each oscillator already picks its loop per block, and
  the gate counts each oscillator's dearest form; a mixed pair is never
  dearer.
- **Per block:** a few compares, plus a second bind check: ≈ +0.1.
- **RAM:** +2 row bytes, +≈ 12 B for the second bind: ≈ +14 B.

---

# Part B — coupling the two cores (revision 1, carried)

### B0. What changed since revision 1

- **The reference identity** (law PC-0) is now the **Part A voice**, frozen
  when Part A is done, not the S103 P5 voice.
- **"F1 cutoff" now means the band's two corners** (both move together,
  §A4.5); "CE2's ±48 dp2" now means the slow envelope's term (§A3).
- **Feature 4 is planned in its 0-per-sample form** (§B3.3). It and A6.3
  would both change `kat_aBody()`; A6.2 does not.
- **The exposure (§B7)** has two free indices (58–59; A8 took 57) and a
  real layout.

## B1. Groundwork: per-core control state (G)

Features 1, 2 and 3 move one core's feedback, tube or kick without the
other. Today those are single values read by both cores' loops.

| Change | Detail | RAM |
|---|---|---:|
| G1 | `fb`, `fbStep` → `[2]`; the async computes each core's target; `kat_coreBody()` reads `v->fb[c]` (`c` is a constant per wrapper) | +8 |
| G2 | `tTube` → `[2]` (feature 2) | +4 |
| G3 | `kE`, `tExc`, `tNse`, `kickSwell` → `[2]` (feature 3) | +16 |
| G4 | the clamps per core: fb ≤ 0.95, fb + xfb ≤ 1.1 (`:1553–1560`) | 0 |

Both elements equal → bit-identical (PC-0). Per sample: nothing changes.

## B2. Feature 1 — cross-level coupling (first preference)

**The measure:**
- level follower per core: P_c = max |blkD[c][i]| over every 4th sample,
  ÷ 32,768;
- L_c += k (P_c − L_c), attack 0.07 (≈ 10 ms), release 0.0073 (≈ 100 ms),
  by ear;
- RAM `lvl[2]`, +8 B;
- `ringPrev` is kept for its own users (Q-B2).

**The law** (per block, on the other core; d_B = min(1, 2 L_A) and
symmetrically; x ∈ [−1, +1]):

| x | Core B receives | Bound |
|---|---|---|
| < 0, rivalry | fb_B × (1 − 0.8 \|x\| d_B); band corners − 24 \|x\| d_B semitones | only lowers gain: self-limiting |
| > 0, sympathy | band corners + 24 x d_B semitones; (Osc 1b pitch × 2^(x d_B / 4): the nudge, **dropped** in the budget) | never raises feedback |
| 0 | nothing | identity |

**Cost:** ≈ 70 a block = **+2.1** (+3.1 with the nudge). Per sample: 0.

## B3. Feature 4 — cross-sync (second preference)

### B3.1 The law

st = wrapB · KAT_SYNC_WRAP + D_own or D_other (`KatranOsc.c:298`). Modes:
own (today), other, both.

### B3.2 The index into the other core's D

1a (full rate, n) reads core B's half-rate block at n >> 1; 1b (half rate,
k) reads core A's at 2k. Both D blocks exist before either core renders
(`:1620–1621`).

### B3.3 The planned form: separate wrappers (0 a sample)

- `kat_aBody()` takes a constant `dsrc` (which core's D syncs it), like its `form` and `half`;
  "other" is its own wrapper per form and copy, chosen per block.
- "Both" is dropped. Flash +8 wrappers ≈ +1.5 KB.
- The mask form (own / other / both in one loop) would cost +4 .. +6 a
  sample. It is kept only if the count leaves room (§C).

## B4. Feature 3 — kick ping-pong (third preference)

| Mode | A trigger kicks |
|---|---|
| both | both cores (today) |
| alternate | A, then B on the next trigger |
| echo | A now, B after core A's loop time (`tNow[0]` / 32 blocks, ≤ 56) |
| random | A or B from the voice's xorshift |

- The run gain `gr` stays shared (Q-B3); ping-pong moves the burst and the
  swell only.
- Cost ≈ 10 a block (+0.3). RAM +2 B.

## B5. Feature 2 — anti-phase modulation (fourth preference)

- **The sign:** φ ∈ [0, 1] gives core B s_B = 1 − 2φ on its shared-modulator
  terms:
  - the patch LFO on the band (`cv · lfl · 12`), on the tube
    (`0.15 cv lfl`) and on the feedback (`0.2 inv lfl`);
  - the slow chaos envelope's band term.
- **Untouched:** Osc 2's terms, F3, the VCA offsets.
- φ = 0 is identity. Cost ≈ 10 a block (+0.3).

## B6. Feature 5 — the ring → LFO path

The patch fed RING OUT into its LFO's MOD IN at audio rate. Katran's
equivalent is block-rate and measures one delay-output sample per core
(`ringPrev`, `:1671`).

| | 5a: the right measure (per block) | 5b: audio-rate AM into the VCAs (per sample) |
|---|---|---|
| What | the peak of each core's output block (`buf`, `right[]` before the matrix, every 4th sample) through §B2's follower, in place of `ringPrev` in the LFO rate (`:423`; after A2, inside the synced law) and optionally the CE rise | core A: o = r + (g2 + kAM m[n]) t1; core B: o = r (1 + g1 + kAM m[n]); m = Osc 2 (B, already loaded) or the ring r |
| Off | a source switch selects `ringPrev` (today) | kAM = 0: `fma(0, …)` returns o exactly |
| Cost | +1.2; **+0.2 sharing feature 1's measure** | +3 (m = B), +6 (m = r) |
| RAM | +5 B | +2 B |

With A5's drive in the core loops, 5a measures after the drive, the
outputs as they leave the cores (no change to the law).

## B7. Exposure: the free indices (two after A8)

**Control values the features need** (each a byte in `KatranVoice`, read in
the async, off by default):
- feature 1: x (bipolar);
- feature 4: own / other;
- feature 3: both / alternate / echo / random;
- feature 2: φ;
- 5a: the source switch;
- 5b: kAM.

**Revision 2 proposed three rows at 57–59, a `Cores` category** on the
filter page's free cell and the mix page's second screen. A8 takes 57, so
two remain:

| Idx | Row | Carries |
|---:|---|---|
| 58 | Cores Couple `cpl` (PM63) | feature 1's x |
| 59 | Cores Link `lnk` (selector, 8 stops) | feature 4 × feature 3: `own both`, `own alt`, `own ech`, `own rnd`, `oth both`, … |
| — | Cores Phase `phs` | feature 2's φ: needs a third index (one of 60..63 if the insert effect is dropped), or φ fixed by ear |

- **5a's source:** decided by ear in step 6, then fixed (no row).
- **5b:** gets a row only if it fits the CPU and a fourth index is found
  (e.g. one of 60..63 if the insert effect is dropped).
- This is still your decision (Q-B1).

---

# C. CPU and RAM fit

| Item | Instructions a sample |
|---|---:|
| Now (S103 P5, measured) | 366.6 |
| A1 labels | 0 |
| A2 `rte` sync and the pull | +1.0 |
| A3 chaos | +0.5 |
| A4 the band | **−10** (−8 .. −13) |
| A5 F3 moved, the ring crossfade, the gain | 0 |
| A5 the drive (in the cores) | +7.5 |
| A6 FM (loop PM) | +4.5 |
| A7 sidebands | +2.5 (+2 .. +3) |
| A8 a type per oscillator | ≈ +0.1 |
| **Part A** | **≈ 372.6** (≈ 368 .. 376) |
| Part B: 1 (no nudge) 2.1, 2 0.3, 3 0.3, 4 wrappers 0, 5a 0.2 | +2.9 |
| **Part A + B** | **≈ 375.5** (≈ 371 .. 379) |
| 5b (m = B) | +3 → ≈ 378.5 |
| Gate | 380 |

**The drop ladder, if the linked count exceeds 380** (optional work first,
then what you made conditional):
1. 5b not scheduled.
2. Feature 1 dropped (−2.1).
3. The drive dropped; the gain bump stays (your fallback, −7.5).
4. FM on one core only (−1.5), or none (−4.5).

**If A4 saves more than estimated** (−13), the order runs the other way:
the drive on L/R (+2.5), feature 1's nudge (+1), cross-sync's mask form.

**The S094 P2 cycle gate:** the `p` probe on the device, median ≤ 18.6k
cycles a block. At 1.53 CPI: ≈ 18.2k at 375.5.

**RAM:**

| Item | Bytes |
|---|---:|
| A2 tick extrapolation | +8 |
| A3 skew phase | +4 |
| A4 the band (−116 + 80) | −36 |
| A5 drive constants | +8 |
| A6 `kq[2]` | +8 |
| A7 feedback state and gain | +8 |
| A8 Osc 2's type and wave, its wavetable bind | +14 |
| **Part A** | **+14** |
| Part B (G1–G3 28, followers 8, ping-pong 2, 5a/5b 7, alignment ≈ 6) | +51 |
| **Total** | ≈ 1,100 → **≈ 1,165 B** (gate 1,200) |

**Flash (Est.):** labels +≈ 30 B, rate names +105 B, drive wrappers
+1.2 KB, cross-sync wrappers +1.5 KB, smaller filter loops (−): **≈ +2.8 KB**.
`link_budget.py` gates.

---

# D. Laws (host, `tools/dsp_test/modules/katran.c`)

**New (Part A):**

| Law | What |
|---|---|
| KT-LBL | every Katran row: short ≤ 3, category ≤ 8, long ≤ 9; no short repeats on one page; no two rows share category + long (a check in `make_init_kat.py --check`) |
| KT-RATE | at 60, 120 and 200 BPM, every stop row's period = its ticks × samples per tick (1e-4); the rate rises with the row; `slf` 0, `chs` 0, transport running: phase error to the clock < 1 % after 2 s; `slf` 127: the error passes 25 % and returns below 5 %; transport stopped: free-running at the synced rate, no pull |
| KT-CE | `chs` 0: gain exactly 1 and the band untouched; `chs` 127: the band's centre follows the slow envelope (48 · (L_s − 0.5), 0.1 semitone; the F-1 check); `imd` 0: T_s = N T_f exactly and the pair repeats with period T_s; `skw` +1: slow rise share 1, fast 0 (within the 3-block rule); `imd` > 0.5: the pair rotates by a quarter turn a slow cycle at 127; a trigger leaves both untouched (KA-CE) |
| KT-BAND | `cut` 63, `wid` 127: flat within 1 dB 30 Hz .. 12 kHz (core A; core B to its ceiling); `cut` 127 / 0: HP / LP at the ends (≤ −40 dB at 1 kHz); `wid` 0: a band at the cutoff's centre with a peak ≥ +15 dB; `ofs` o: A's corners = B's + 48 o semitones; finite and bounded at every cut / wid / ofs with `fbk` 127 over 4,000 blocks |
| KT-OUT | `ton` 63 passes the matrix output exactly; `drv` 0 is bit-identical to the no-drive wrapper; `drv` 127 stays inside int16 without reaching the SSAT; `init.kat` RMS within ±1 dB of the measured target (L2) |
| KT-RING | `dpt` 0 = today's `lvB` 0 output and `dpt` 127 = today's `lvB` 127 output (the same gains); the output level is monotonic between (no cliff at 1) |
| KT-FM | `rng`: the integer read exactly as today; FM on core c: its ring gain 0, its read displaced by k · B[n] (against a reference read); the time clamp T ≥ 32 + k (16 + k / 2) holds at every block, so no read reaches the current block's unwritten samples |
| KT-SDB | `sdb` 0: Osc 2 bit-identical; on Sin, the spectral centroid of Osc 2 rises monotonically with `sdb`; finite at 127 on every form |
| KT-TYPE | each of the 16 type pairs plays its own forms: Osc 1 `buf` + Osc 2 `wav` reproduces S103 P5's `src A` with the same wav, `wav` + `buf` its `src B`, `buf` + `buf` its `AB`, and a table pair with equal type and wave its `src off` (bit-identical); Osc 1 `buf` → `wav` restores its wave; two wtb oscillators on different tables each read their own; the menu range of `kat_b_wave` follows Osc 2's type |

**Changed:**
- **KA-SPR:** `spr` no longer spreads F1.
- **KA-F3:** F3 sits after the matrix; the open check is KT-OUT's.
- **KA-FOLD:** its reference is the Part A core loop.
- **KA-BND:** adds the new extremes (`wid` 0, `drv` 127, FM 127, `sdb` 127).
- **KA-ROT:** unchanged.

**Carried (Part B):**
- PC-0, against the Part A voice;
- PC-LVL, PC-RIV, PC-SYM: "F1" read as the band's corners;
- PC-SYNC: own and other only;
- PC-PP, PC-PHASE, PC-RLV, PC-AM, PC-BND.

**Break builds:**
- `katran_break_band` (the resonance feedback's sign flipped) must fail
  KT-BAND;
- `katran_break_pmclamp` (the FM time clamp removed) must fail KT-FM;
- `katran_break_riv` must fail PC-RIV and `katran_break_xsync` PC-SYNC
  (carried).

---

# E. Order of work

| Step | What | Gate |
|---|---|---|
| 0 | Your answers to §G: **done** (all recommendations; Q-A11 per oscillator) | done |
| 1 | **A4 + A3 + A8 + A1:** the band, the chaos rows, the type per oscillator, the descriptor table, pages and labels, the index map, the `r/f` and type tags, `KAT_PARAM_COUNT` 58; the new bytes bound (`r/f`, `sdb`, `drv` read from steps 2 and 4) | KT-BAND, KT-CE, KT-TYPE, KT-LBL; `katran_cpu.py` (**the band's saving measured here** decides the ladder) |
| 2 | **A5:** F3 after the matrix, the ring crossfade, the drive wrappers; L2 measured in the host | KT-OUT, KT-RING; the count |
| 3 | **A2:** `katPlfoTicks[]` (`gen_kat_tables.py`), `seq_getElapsedTicks()`, the pull, `DTYPE_0B127_KAT_RATE` and its names | KT-RATE |
| 4 | **A6, A7:** the loop PM and its clamp; Osc 2's feedback | KT-FM, KT-SDB; the count |
| 5 | `init.kat` values by ear from host renders (the chaos defaults on, F-2); `make_init_kat.py` | your ear |
| 6 | Part B, as far as the count allows: G (PC-0 against a frozen Part A render), feature 1, 4 (wrappers), 3, 2, 5a; then 5b only if ≥ 3 remain | PC-* |
| 7 | KA-BND / PC-BND, the break builds; `make all && make img`; the mixer hot-loop and inlined-set diff; the size assert; `link_budget.py`; the diagrams (`tools/katran/make_block_diagram*.py`, `KATRAN_BLOCK_DIAGRAM_SVG.md`) | all |
| 8 | Device: the `p` probe (≤ 18.6k), listening | yours |

---

# F. Risks

| # | Risk | Mitigation |
|---|---|---|
| R-A1 | The one-pole band sounds too soft (6 dB an octave a side) | the 2-pole LP fallback (§A4.7), at the cost of the drive on the ladder |
| R-A2 | The band's resonance and the loop's own feedback together self-oscillate | r · Gc ≤ 0.9; the existing clamps and the SSAT store; KT-BAND's 4,000-block bound |
| R-A3 | The clock pull is heard as small pitch bends of the patch LFO | it acts on the phase slowly (κ0 ≈ 0.01 a block), never as a jump; by ear |
| R-A4 | Fixing F-1 and turning the envelopes on changes the default sound | that is the request; `init.kat` is re-tuned in step 5 |
| R-A5 | The drive in the cores sounds different from a drive on L/R | Q-A8; the L/R form is the alternative at +2.5 |
| R-A6 | Loop FM at high depth and feedback turns to noise | the `dpt`'² law and K_MAX by ear; KA-BND |
| R-A7 | Osc 2 feedback aliases at high pitch | kfb scaled down above ≈ 2 kHz, per block |
| R-A8 | The band's saving is the swing factor of the whole budget | it is measured first (step 1) and the ladder follows it |
| R-B1 | Cross-sync's register pressure | the wrapper form costs 0 a sample |
| R-B2 | Sympathy and high feedback run hot through the band | feedback is never raised; PC-BND |
| R-B3 | Rivalry at block rate buzzes on fast loops | the follower's 10 / 100 ms smoothing |
| R-B4 | 5a's smoother level changes the patch LFO's character | the source switch; by ear |
| R-B5 | 5b at high kAM clips at the store | kAM ≤ 0.5; PC-AM |

---

# G. Questions

**All decided (your answers, 2026-11-26): every recommendation is
accepted**, and Q-A11 is per oscillator. The plan above is written to
them. Nothing is open before step 1.

| # | Question | Decision |
|---|---|---|
| Q-A11 | A8: the buffer type per oscillator, or per core as well? | **per oscillator** (Osc 1, Osc 2) |
|---|---|---|
| Q-A1 | The label table (§A1.3), with three repeated shorts on different pages: `drv`, `wid`, `dpt` | keep them (the same word each time). Otherwise: Filter Width `bnd`, Ring/FM Depth `rdp`, Torcido Drive `tdr` |
| Q-A2 | `rte`'s range: 32 bars .. /64 (proposed), or exactly the delay table's /64 .. /2 (≈ 0.35 .. 32 Hz at 120 BPM)? | the proposed range |
| Q-A3 | Which settings let the rate drift: `slf` only (proposed), or `chs` too (via a weaker pull)? | `slf` only; `chs` already jitters each cycle |
| Q-A4 | Roles: the fast envelope on the amplitude, the slow one on the band (proposed), or the reverse? And should `imd` give a skew of its own when `skw` is centred? | as proposed; no own skew |
| Q-A5 | The band's tracking of the loop pitch: automatic at narrow width (proposed), a row at index 37, or none? | automatic |
| Q-A6 | The resonance form: one-poles with feedback (proposed), or the 2-pole LP section (+5)? | one-poles; the 2-pole only if it sounds thin |
| Q-A7 | The ring as a crossfade (F-3 fixed), or keep today's switch at `dpt` 0? | the crossfade |
| Q-A8 | The drive in the cores (proposed) or on L/R after the matrix (+2.5)? A slow normalise as well? | in the cores; no normalise |
| Q-A9 | FM as loop PM (proposed) or exciter FM? | loop PM |
| Q-A10 | Sidebands as Osc 2 feedback (proposed) or the Chebyshev modulator? | Osc 2 feedback |
| Q-B1 | Part B's rows: the §B7 proposal for 58–59? | after Part A is heard |
| Q-B2 | Should the patch LFO and the CE rise move to the new followers (5a), or keep `ringPrev`? | by ear in step 6 |
| Q-B3 | Ping-pong: split the run gain per core too? | no: one `gr` keeps the drone's on / off a single control |
| Q-B4 | Cross-sync own / other per copy, or one for both? | one for both |
| Q-B5 | 5b's priority and modulator | only if ≥ 3 remain after step 6; m = Osc 2 |

---

**Statements this would make out of date (for you to log when
implemented):**
- `MEMORY.md`: none. It has no Katran entry. If you add one, the figures
  to record are the voice size, the CPU count, the row count (57) and the
  index map.
- `knowledge_files/drafts/DRAFT_KATRAN_DRONE_SYNTH.md`: the row list, the
  F1 / F2 / F3 chain and the chaos envelopes' rows.
- In the tree (updated in step 7, not `knowledge_files/`):
  - `KATRAN_BLOCK_DIAGRAM_SVG.md` and both diagrams: the band, F3's
    position, the drive, the FM path and Osc 2's feedback, the four chaos
    rows, and (Part B) the per-core feedback and the couplings;
  - `S103_KATRAN_NOISE_SYNTH.md` §3.3 and §8.2 (the rows they describe).
