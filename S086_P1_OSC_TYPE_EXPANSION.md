# S086 P1 — Oscillator type expansion: startup plan

**Session:** 086, phase P1 (startup plan; no code yet)
**Branch:** `dev-ph7-voice_expansion`, after the S085 close-out
**Written:** 2026-10-27, at the end of S085
**Status:** for review at the start of S086. The open questions are in
§9.

---

## 0. Read this first

### 0.1 What S086 does

S086 re-implements the expanded oscillator types, one stage at a time. Each
stage has its own CPU gate.

| Stage | Content |
|---|---|
| **S1** | the new stock waveforms (`Re3 Sw3 ReA TrA SwA`), and the new `pos` and `typ` rows (menu layout, §3) |
| **S2** | the sample rework: `smp` (one-shot) and `slp` (loop) types, sample flash catalogue v2, the installer |
| **S3** | wavetables: the `wtb` type |
| **S4** | the sample and wavetable optimizer (`tools/sampleoptimize.py`), including the optimizer flag that enables the zero-crossing bitmap |
| **S5** | `pwm`: re-examine how to do it without blowing up the CPU (evaluation first) |

**Out of S086:**
- the run modes `rst fre itp`, the `run` row and the render pool (S087);
- the effects chain.

### 0.2 Inputs

| Document | Use |
|---|---|
| `S085_P1_OSC_MOD_SNC_RNG.md` | the CPU rule, the sync/ring design, the future sync rules (§12) |
| `S085_P1_OSC_MOD_IMPLEMENTATION.md` | the measured baseline (§1, §11–§14), the tooling (§5), lessons D1–D8 |
| `S085_SAMPLE_INDEX_SPEEDUP.md` | install-time preparation for samples and wavetables; the wavetable blend-pool rule |
| `S085_OSC_MOD_CHERRYPICK.md` | what the spike built: catalogue v2, the installer, stock waves, `wtb` |
| `Helicase-dev-ph7-voice-pwm-spike/` (the failed S085 tree): `S085_POST_MORTEM.md`, `S085_POST_MORTEM_OSC_RUN.md`, `S085_POST_MORTEM_MENU.md`, `S085_P1_IMPLEMENTATION.md` | why the last attempt failed; per-type costs; menu decisions; reference code only |
| `Helicase-dev-ph7-spike/` (`26fb7c3`) | the original spike; reference code only |

**Code-reuse rule (S084/S085):** copy only the hunks an S086 document
names. No "take whole" files.

### 0.3 Preconditions

- **The S085 close-out:**
  - `085_SESSION_HANDOFF_LOG.md`;
  - `MEMORY.md` updated (it still describes S084);
  - the four documentation nits F1–F4 (implementation §12.6).
- **The S084 hardware check V1–V7** is still listed as gating; confirm it
  is closed or waived.

---

## 1. Rules carried from S085 (binding)

### 1.1 The CPU rule

**"No increase in CPU cycles overall"** (user, S085). It is gated as:
- **(a) the gate:** the dearest setting of any voice, in any mode, must not
  exceed the dearest setting that voice has today;
- **(b) reported only:** each option compared with the same waves.

**The per-oscillator ceiling** (S085 Q9): a new type must not exceed the
dearest loop that oscillator can select today, in the voice that runs the
most oscillators (Cymbal/HiHat, three). Today the dearest loops are the
user-sample loops (§1.2).

**Freeze the ceiling as numbers.** S2 replaces the v1 user-sample loops.
Once they are gone, "the dearest loop today" would move. So the S086
baseline (§6, step 0) records the ceilings as fixed figures, and every
stage is measured against those figures.

### 1.2 Measured baseline at the end of S085 (static instructions per sample, `fpseq.py`)

| Loop | Plain | FM carrier | Sync slave (no reset / reset) | Ring carrier, ramped |
|---|---:|---:|---:|---:|
| Tri/Saw/Rec table | 24 | 35 | 34 / 39 | 31 |
| Sine | 23 | 30 | 33 / 38 | 30 |
| Crash | 30 | 38 | 36 / 41 | 33 |
| Noise | 12–13 | = plain | (unsynced) | 16–19 |
| User sample (v1) | 40–41 (linked ≈ 41–43) | 48–49 (linked 50–51) | (unsynced) | 46–48 (linked 49–50) |
| Sync reset-list builder | 7 | — | — | — |

**Drum oscillator section, user samples on both oscillators** (linked
image):

| Mode | Cost |
|---|---:|
| `fm` | ≈ 92 |
| `mix` | ≈ 87 (standalone; not measured linked) |
| `rng` | ≈ 90–91, the narrowest margin |
| `snc` | ≤ 48 |

The Cymbal/HiHat ceiling (user samples on all three oscillators) is about
41 + 41 + 49 ≈ 131 standalone. **Measure it at step 0.**

**Build (DEV) at the S085 end:**
- text 613,536; data 424; bss 430,296;
- flash headroom 139,704 B;
- ITCM 7,792 / 16,384 B (8,592 free);
- DTCM 4,472 B; FX-arena margin 3,712 B;
- `DrumVoice` 596 B (`runtime_slots` 7,152 B).

### 1.3 Lessons that shape the code (S085 D1–D8 and the rollback)

1. **Never edit an existing loop to add a feature.**
   - Add separate copies: per waveform × the uses it needs (plain, FM
     carrier, sync slave, ring carrier).
   - The rollback's shared plain/sync loops put a per-sample test on every
     plain render (+2), and that raised `fm` and `mix`.
2. **Place new ITCM code in its own subsection** (`.itcm.s086`, after
   `.itcm` and `.itcm.s085`).
   - **Why:** appending inside `.itcm` moved existing loops and changed a
     tail branch from `b.n` to `b.w` (D7).
   - The linker already places `*(.itcm.*)` after `*(.itcm)`.
3. **Do not add constant-argument call sites to existing dispatchers**
   (`calcNextOscSampleBlock()`). LTO re-specialises their clones. Call the
   leaf loops directly, as `osc_renderPlainWave()` does (D8).
4. **Mark new loops `noinline, noclone`.** Each then has its own symbol for
   `fpseq.py` and ITCM accounting, and LTO cannot merge it into an existing
   loop.
5. **Measure before accepting.**
   - Static counts on the standalone **and** the linked image; they differ
     by 1–3 (§1.2).
   - Then the device `cpu` widget with the six-slot worst-case Scene (G-C).
   - Static counts are evidence, not the gate (post-mortem §1.3).
6. **Constant cost.** No per-sample work that depends on the data (wrap
   counts, crossings). Use branchless selects, or budget the dearer path.
7. **Restrict types where they don't fit** (post-mortem §4.3). The
   rollback excluded `pwm` from the Cymbal/HiHat modulators.
8. **Flash stalls are not in the counts.** Sample, wavetable and
   stock-wave reads come from sample flash and can miss the D-cache. G-C
   must use those waves.

### 1.4 Tooling to reuse (S085 P1 §5)

| Tool | Use in S086 |
|---|---|
| `tools/dsp_test/snapshot85.sh` → **`snapshot86.sh`** | freeze the files S086 edits into `frozen86/` before any edit |
| `oscmod_identity` | extend: no removed or changed lines in `Oscillator.c/.h` against `frozen86/` |
| `elf_symdump.py`, `elf_osc_baseline/check` | re-baseline on the S085-end image; **add the S085 copies** (sync, ring, builder, `osc_renderPlainWave`) to the symbol list, so S086 cannot perturb `snc`/`rng` either |
| `test_osc_mod.c`, the `extract.py` harness | the pattern for one host test per new type (law, identity, break proofs) |
| `armcheck-oscmod` | extend with every new loop beside the loop it is budgeted against |

---

## 2. What S085 decided that applies to the new types

### 2.1 Interaction with `fm`, `mix`, `snc` and `rng` (Drum)

- **Every new type a carrier can select needs:**
  - an FM carrier loop (`fm`);
  - a ring copy (`rng`; the law and the per-sample depth ramp of S085 P1
    §14);
  - a sync copy (`snc`), **or** an explicit "renders unsynced" rule, as for
    noise and v1 user samples today.

  Each copy is measured against the same-wave `fm` carrier (S085 rule (b)
  is reported; rule (a) gates).
- **Every new type a modulator can select needs** a plain loop, and a sync
  master rule for `snc`.
- **The mod envelope:** `fm`, `snc` and `rng` share amt² × env
  (`Drum_modEnvDepth()`). New types inherit it; no change is needed.

### 2.2 Sync master rules for the new types (S085 plan §12, user)

| FM-page (master) source | Optimizer flag set | No flag |
|---|---|---|
| Stock waves (incl. the new ones) | — | the 2^32 phase clock (as today) |
| One-shot sample (`smp`) | resets at the sample's zero crossings (bitmap) | **as noise**: one reset per nominal period at `frq` |
| Looped sample (`slp`) | zero crossings (bitmap) | **reset when the sample loops** |
| Wavetable (`wtb`) | zero crossings (bitmap, per frame) | **reset when the frame loops** (the phase wrap) |

**Architecture:**
- Each master kind adds a **reset-list builder**
  (`osc_buildSyncList()` is the periodic one). The slave copies stay
  unchanged; that is why S085 chose the list (Q11).
- The loop-wrap builder (`slp`) compares and subtracts at constant cost.
  It has no per-sample modulo; one modulo per block only when the step
  exceeds the loop.
- The bitmap builder reads two words per sample and uses `RBIT`/`CLZ`. It
  costs about 12–15 per sample, with no search and no divide.

**The zero-crossing bitmap:**
- 1 bit per point, set where an upward crossing starts, with hysteresis.
- Stored after the item in sample flash: 6.25 % of a 16-bit item.
- **Generated at upload only for files the optimizer flagged** (§4.4).
- **There is no crash table:** the crash is treated as noise (user).

### 2.3 Trigger behaviour (user, S085 Q6b): unchanged

- `osc1` resets on a trigger; Drum `modOsc` never does (Cymbal resets its
  modulators).
- **Consequence for `smp` on a free-running modulator:** a one-shot sample
  on the Drum FM page plays once after selection or power-up, then holds
  (S085 R11/R12). The same applies to `pos` (§3.3) on a modulator that is
  never reset. This is the S086 question Q5.
- **S087 note (user):** in the free-run run mode, try "ramp always" for
  the `rng` depth instead of the trigger snap.

---

## 3. Menu layout (user, end of S085)

### 3.1 The rows

| Short | Category + long name (single-parameter view) | What it is |
|---|---|---|
| `wav` | `Oscilltr` `Waveform` (`FM` / `Mod Osc` on those pages) | the selection: stock wave, sample number, wavetable number (later the `pwm` family) |
| **`pos`** | **`Oscilltr` `Position`** | the continuous control, 0–127: `smp`/`slp` start position; `wtb` position across the frames; possibly the **phase offset** for the stock waves (Q2) |
| `typ` | `Oscilltr` `Type` | `wav smp slp wtb` (+ `pwm` after S5) |
| `run` | `Oscilltr` `RunMode` | S087 |

- **`pos` replaces the rollback's continuous row** (`mod`, formerly `src`).
  This also resolves the rollback's unresolved Drum FM page clash, where
  `mod` meant both the mod mode and the FM oscillator's continuous control.
  `mod` is now only the mod mode.
- **The display rules from the rollback** (`S085_POST_MORTEM_MENU.md` §3)
  still apply:
  - `pos` shows the number once, on the left, with no label;
  - `wav` under `smp`/`wtb` shows the 8-character name on the left and
    `sNN`/`wNN` on the right;
  - the formatter reads the **stored** `typ` (D5).

### 3.2 Where the rows sit (screen = four cells behind one SELECT)

**User rule:** the first oscillator screen's fourth cell is `pos`. The
second oscillator screen holds `typ` (and `run`, S087).

| Instrument / page | Screen 1 (cells 1–4) | Screen 2 (cells 5–8) | Screen 3 (cells 9–12) |
|---|---|---|---|
| Drum Oscillator (page 1) | `coa fin wav pos` | `typ` (`run`, S087) | — |
| Drum FM (page 4) | `amt frq wav mod` (unchanged) | `pos typ` (osc2) | — |
| Snare Oscillator (page 1) | `coa fin wav pos` (proposed, Q3) | `noi mix typ` (`run`, S087) | — |
| Cymbal/HiHat Oscillator (page 1) | `coa fin wav pos` | `typ` (`run`, S087) | — |
| Cymbal/HiHat Mod Osc (page 4) | `f1 f2 g1 g2` (unchanged) | `wav wav pos pos` (osc2, osc3) | `typ typ` (osc2, osc3) |

**Today's pages, for reference:**

| Page | Today |
|---|---|
| Drum page 1 | `coa fin wav –` |
| Drum FM | `amt frq wav mod` |
| Snare page 1 | `coa fin noi mix` / `wav` |
| Cymbal/HiHat page 1 | `coa fin wav –` |
| Cymbal/HiHat Mod Osc | `f1 f2 g1 g2` / `wav wav` |

### 3.3 `pos` for the stock waveforms (Q2)

- **Proposal:** `pos` = the phase the oscillator restarts at on a trigger,
  as `pos`/128 of a cycle.
- **Default 0 must reproduce today exactly.** Today's start phases are
  specific:
  - Sine: `1024 + …`;
  - Tri/Saw/Rec: `0xff<<20`;
  - both scaled by the transient offset mode.

  So `pos` **adds** `pos/128 × 2^32` to today's start phase. A missing key
  (old kits) loads 0.
- **Storage:** `OscInfo.startPhase` already exists and is unused. It is
  written by the uncalled `Drum_setPhaseVoice()`, and its trigger use is
  commented out (`DrumVoice.c:208`, after the S085 edits). It can hold the offset at no RAM cost.
- **Cost:** trigger time only; nothing per sample.
- **Limits:**
  - It only acts where the engine resets that oscillator: `osc1` on every
    engine, and the Cymbal/HiHat modulators.
  - On the Drum FM oscillator it does nothing unless Q5 changes the
    trigger.
  - In S087's `fre` run mode it does nothing, because there is no reset.

### 3.4 Storage, rows and counts

- **New rows are appended** at the end of each descriptor table, so
  existing automation IDs, LFO tokens, Learn and NRPN IDs are unchanged.
  A missing key loads 0 (`typ` 0 = `wav`, `pos` 0).
- **Keys (proposed, Q4):** `oscN_type`, `oscN_position`.
- **Rows added:**

  | Instrument | Rows | Count |
  |---|---|---:|
  | Drum | `typ` + `pos` for two oscillators | +4 |
  | Snare | one oscillator | +2 |
  | Cymbal | three oscillators | +6 |
  | HiHat | three oscillators | +6 |
  | **Total** | | **+18** |

  `special_tags` 155 → 173 rows. S087 adds `run` per voice.
- **`typ`:** a selector row, not morphable. It is stored single-ended and
  applied before the Morph image on every slot reset; under SHIFT it shows
  the mirror (the rollback's F1/F2/D12 rules, which need the
  `presetManager.c` selector hunks).
- **`pos`:** an ordinary morphable row; LFO and velocity target;
  automatable.
- **Retained request bytes** (rollback): `typ → wav → typ` restores the
  previous `wav`/`pos`.
- **The `wav` list and old kits:**
  - Today `wav` 6+ are v1 user samples (`OSC_SAMPLE_START` 6).
  - The new stock waves take 6–10, and samples move to the `smp`/`slp`
    types, so old kits pointing at user samples change sound.
  - S085 planning decided **no migration** (Q8); re-confirm (Q6).

---

## 4. The stages

### 4.1 S1: stock waveforms (+ `pos`, `typ` rows)

**Content:**
- `Re3 Sw3 ReA TrA SwA` (code constants `SQ3 SW3 SQA TRA SWA`), `wav`
  values 6–10, 11 levels;
- generated from recipes into the **sample-flash stock block at install**
  (S085 plan Q6 (a): program-flash headroom kept; an EMPTY boot notice
  because they need an install);
- a reader with fallback before the first install;
- `tools/gen_stock_recipes.py`.

**Copies needed** (the stock reader ≈ the table reader):
- plain;
- FM carrier;
- sync slave;
- ring carrier (ramped);
- the waveform-interpolation path.

**Expected cost** (spike, static): plain 24, FM 31. The plain and FM
figures are under the ceiling; the sync and ring copies are about +10 and
+7. **Flash stalls:** the stock block is 61 KB in sample flash; measure
D-cache behaviour at G-C.

**The rows:** land `typ` and `pos` here (Q1). Then the menu, storage and
Preset selector work is done once and tested early, with `typ` offering
only `wav`.

### 4.2 S2: the sample rework (`smp`, `slp`)

**Content (spike `OscillatorSample`, re-planned):**
- Q20.12 phase; `pos` = start position (linear 0..127);
- `smp` ends at 0, `slp` loops;
- the gain code per item;
- catalogue v2: 128 samples; folders `samples/` and `wavetables/` in
  natural order; depth 3; the installer (WAV acceptance; 24→16-bit and
  stereo→mono conversion, never expanding); the STALE/EMPTY boot notice;
- the RAM release (≈ −16 KB from the v1 manifest and caches).

**Install-time preparation** (`S085_SAMPLE_INDEX_SPEEDUP.md` §2.2):
- end padding removes the one-shot "played-out" select and the next-point
  hold;
- for `slp`, padding needs a **fixed loop region** (Q7).

**Ceiling:** spike `smp` 40, `slp` 47 static (plain). `slp` is over the
v1 sample loop, so it must come down (the padding, fixed loop) or be
restricted. FM, sync and ring copies are each measured.

**Sync:** the masters as in §2.2, unflagged column first; the bitmap
comes in S4.

**Removes:** the v1 user-sample tail of `wav`, and its loops (the
ceiling figures stay frozen, §1.1).

### 4.3 S3: wavetables (`wtb`)

**Content:**
- **Selection:** `wav` = the wavetable number. `pos` = the position across
  the frames; the user wrote "wavetable index" (confirm, Q8).
- **One frame length per table**, a power of two guaranteed at install
  (speedup §2.1: shift/mask indexing, no 64-bit multiply).
- **Frame blending uses a pool slot** (user rule, speedup §3):
  - the base loop is **one frame** (2 reads, 1 interpolation);
  - blending two frames claims one of the two interpolation units (today
    `OSC_WAVE_INTERP_MAX_ACTIVE`, shared with `wav` waveform
    interpolation);
  - when no unit is free, it steps.

  The claim policy, the fallback stepping, and what happens when a slot is
  lost are open (Q9).

**Cost:** spike `wtb` 40/43 plain (blending always). The one-frame loop
should be about table-reader cost; measure.

**Sync master:** the frame-wrap clock (= the phase wrap).

### 4.4 S4: the sample and wavetable optimizers

**Content:** `tools/sampleoptimize.py` from the spike, re-checked:
- wavetables are shrunk to the smallest power-of-two frame that plays the
  same (−20 dB worst, −40 dB typical);
- if none passes, **round up to the next power of two** (speedup §2.1);
- sample tails below −80 dB are trimmed;
- non-installable WAVs are converted;
- `_old` backups; `--dry-run`.

**The optimizer flag (user, S085):**
- A private RIFF chunk (e.g. `lxrO`: a version byte and flags; bit 0 =
  "generate the crossing bitmap"), written **before** `data`. The
  rollback's `filesystem_parseWav()` stops at `fmt `+`data`.
- The optimizer must **rewrite every file** it outputs. It byte-copies
  unchanged files today, and Python's `wave` module cannot write custom
  chunks, so it needs a small RIFF writer.
- **Installer:** a flagged file gets a catalogue v2 flag bit and a bitmap
  generated from the installed data (memory-mapped flash, no RAM buffer).
- A stale flag is harmless, because the bitmap is computed from the
  installed data.

**Sync:** the bitmap builder for flagged `smp`/`slp`/`wtb` masters (§2.2).

### 4.5 S5: `pwm` without blowing up the CPU (evaluate, then decide)

**The record:**
- spike `pwm` loops 42–58 static (plain), FM 47–62;
- all instruments on `pwm` drove the device to 100 % CPU;
- Cymbal/HiHat with three `pwm` oscillators was the worst case (178
  static).

**Candidates to evaluate against the frozen ceiling:**

| Candidate | How | Cost estimate |
|---|---|---|
| (a) Two band-limited saw reads | rec(φ, w) = saw(φ) − saw(φ + w). Saw/Rec families only. | ≈ table cost + one more interpolated read (≈ 34 static, estimate) |
| (b) Width tables precomputed at install | A small set of band-limited tables per family across width, in sample flash; `pos` selects the pair, blended through the pool like `wtb` frames | the one-frame `wtb` cost |
| (c) Restrictions | `pwm` on main oscillators only; no `pwm` FM carrier, sync slave or ring copy | — |

**Decision at the end of S5, with measurements.** No `pwm` code lands
until a candidate fits the ceiling on the device.

---

## 5. Per-stage gates (the S085 pattern)

1. **Baseline** (once, step 0):
   - freeze the files (`snapshot86.sh`);
   - write `elf_osc.txt` (with the S085 copies);
   - record `fpseq` figures for every existing loop, standalone and
     linked;
   - the user records the six-slot `cpu` readings for the dearest Drum,
     Snare and Cymbal/HiHat Scenes.
2. **Per stage:**
   - `oscmod_identity` (extended);
   - the host test for the new type (its law, identity with existing code
     where it should match, break proofs);
   - `armcheck` counts (standalone and linked) against the frozen
     ceilings;
   - `elf_osc_check`: 0 missing, 0 changed;
   - `special_tags`;
   - the ledgers.
3. **Device gate G-C** per stage: the six-slot worst-case Scene with the
   new type on every oscillator that can select it (flash-resident data
   included). The `cpu` figure must be no worse than baseline, with zero
   underruns. **A stage does not start until the previous stage passes
   G-C.**

---

## 6. Order of work in S086

0. **Baseline** (§5.1).
1. **S1:** stock waves; `typ`/`pos` rows; the menu layout of §3; gates;
   device.
2. **S2:** catalogue v2 and the installer first (testable alone), then
   `smp`/`slp`; gates; device.
3. **S3:** `wtb`; gates; device.
4. **S4:** the optimizer, the flag, the bitmap builder; gates; device.
5. **S5:** the `pwm` evaluation report; a decision; code only if a
   candidate passes.

Each stage is planned with its own `S086_Pn_*` document before any code,
as in S085.

---

## 7. Resource outlook

| Resource | Now | S086 pressure |
|---|---|---|
| ITCM | 7,792 / 16,384 (8,592 free) | high: each type × {plain, FM, sync, ring} copies; the rollback reached 16,272 B. Budget per stage, and restrict combinations rather than overflow. |
| Program flash | 139,704 B headroom | recipes (≈ 10 KB), the installer, the catalogue code |
| Sample flash | v1 layout | v2: stock block 61,120 B + items + catalogue 8 KB; bitmaps for flagged items (6.25 %) |
| SRAM | — | catalogue v2 releases ≈ 16 KB; selector state per slot |
| `DrumVoice` | 596 B | small; any new per-oscillator state is ×2 for the slot reserve |

---

## 8. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | The `rng` margin with user samples is 1–2 instructions (linked). A sample rework that makes the ring copy dearer breaks the gate. | Freeze the ceilings (§1.1); measure the `smp`/`slp` ring copies first |
| R2 | ITCM runs out before S5 | measure per stage; restrict type × mode combinations |
| R3 | Flash stalls on stock/sample/wavetable reads exceed the static estimate | G-C with those waves on every oscillator |
| R4 | Selector apply order (`typ`) regresses on a Preset path | the rollback's F1/F2 hunks; a hardware row for store, reload, Morph and Scene change |
| R5 | `pos` on oscillators that are never reset does nothing (Drum FM, S087 `fre`) | Q5; document it |
| R6 | Old kits with v1 sample `wav` values change sound | Q6 (no migration was decided) |

---

## 9. Open questions (for the start of S086)

| # | Question | Recommendation |
|---|---|---|
| **Q1** | Land `typ` and `pos` in S1 (with `typ` = `wav` only), or with S2? | **S1**: the menu, storage and selector work is done once and verified before the sample code |
| **Q2** | `pos` as the phase offset for the stock waves? | **Yes**: it adds to today's start phase (0 = unchanged), stored in `OscInfo.startPhase`, trigger-time only |
| **Q3** | Snare page 1: screen 1 becomes `coa fin wav pos`, so `noi mix` move to screen 2 (`noi mix typ`, then `run`)? | **Yes**: every oscillator page then has the same screen 1 |
| **Q4** | File keys `oscN_type`, `oscN_position`? | **Yes**: the rollback keys are not in the tree, so these names are free |
| **Q5** | Reset the Drum FM oscillator on a trigger when its type is `smp` (or when `pos` ≠ 0)? Today it never resets (user S085: no change unless tested). | Leave it as today in S086; list it for a hardware trial |
| **Q6** | Confirm no migration for kits whose `wav` selects a v1 user sample | Confirm (S085 plan Q8) |
| **Q7** | `slp`: a fixed loop region (the whole sample or the file's `smpl` points), so `pos` moves only the start and the loop padding can be precomputed? | **Yes**: it removes the next-point wrap test (speedup §2.2) |
| **Q8** | `wtb` `pos` = the position across the frames (the "wavetable index" in the user's note)? | Yes; `wav` stays the wavetable number |
| **Q9** | `wtb` blend claims: when to claim, the fallback stepping, losing a slot mid-glide (speedup §3.3 item 5) | Claim only while `pos` sits between frames; snap to the nearest frame on fallback; decide by ear |
| **Q10** | Cymbal/HiHat modulators: which types? (The rollback excluded `pwm`.) | Decide per type from the measured worst case in the three-oscillator voice |
| **Q11** | `pwm` approach (S5) | Evaluate candidate (a) first; it is the cheapest that keeps a continuous width |
