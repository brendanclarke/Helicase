# S093 P9 — Alkaline Post-Filter Drive and Tone (draft)

- **Status:** draft, **revision 3 (user decisions, 2026-11-10; §0.1)**.
  Parked by the user: the plan stays as it is until the schedule is written.
  Open: the curves (P9-D4) and the relabel text (P9-D9). Deferred: the
  cross-instrument row rework (P9-D10). **No
  tree code is changed by this document.** Every figure marked *measured*
  comes from a linked scratch build of the current tree (P8 revision 3),
  counted with the project's tools (§9). Figures marked *estimate* are sums of
  measured parts and are re-measured in the schedule.
- **Brief (user, 2026-11-10):** a post-filter distortion / saturation / drive
  on the Alkaline, whatever fits, with an **amount** and a **tone** control
  that rolls off the highs of the distortion.
- **Binding rules:**
  - the Basic ceiling **261.3** per sample (whole voice: loop + per block /
    32), VDIV counted; the longest path counts. **This plan raises the
    Alkaline's no-slot worst case to ≈ 261.5, accepted by the user (P9-D8)**;
  - S089: no other per-sample loop grows (`elf_p4_check`; the mixer stays
    0 grew);
  - **with Overdrive 0 and Tone 127 the output is bit-identical to today**
    (user, P9-D7; §4);
  - no new ITCM code; RAM sign-off (none needed: the slot reserve absorbs
    it);
  - no step of this plan, or of a schedule made from it, edits `MEMORY.md`
    or `knowledge_files/`. Statements it would make out of date are listed in
    §8.

---

## 0. Summary

**The budget before P9:** 5.2 per sample left without a slot (256.1 of
261.3). With a slot the voice is already over (271.1), accepted in P8 D13.

**The decided design (§3):** overdrive the 303's own VCA. The B2R loop
already models it, an OTA with a smooth tanh, so no new stage is added.
- **Overdrive** (the amount) scales the OTA's input per block.
- **Tone** is a one-pole low-pass on the OTA output, inside the loop. It only
  rolls off highs.
- **A3** (drive gain and make-up from tables) pays for part of it.
- **A4** (one expo pass) is **dropped**; its +1.03 per sample is accepted.

| | Loops (one row / blend) | Per block | Whole voice, no slot | Slot held |
|---|---:|---:|---:|---:|
| Today (P8 r3, measured) | 213 / 228 | 1,378 | 256.1 | 271.1 |
| **P9 decided: overdrive + low-pass tone + A3 (estimate)** | **219 / 234** | **≈ 1,361** | **≈ 261.5** | **≈ 276.5** |
| *(for reference) the same with A4, measured* | 219 / 234 | 1,328 | 260.5 | 275.5 |

- **The estimate:** the measured overdrive + tone + A3 + A4 build (1,328 per
  block), plus A4's measured saving (33 per block) put back.
- **No slot:** the result, ≈ 261.5, is ≈ 0.2 over the Basic ceiling. That
  is the new accepted worst case (P9-D8).
- **Slot held:** ≈ 276.5, 5.4 more than D13's accepted 271.1.
- **The slot pool:** the slot charge stays ≈ 15, below the pool's per-slot
  ceiling (the `itp` fade, 45), so the pool's worst case does not rise.

### 0.1 Decisions (user, 2026-11-10)

| # | Question | Decided |
|---|---|---|
| P9-D1 | Which design | **VCA overdrive + a low-pass tone in the loop + A3** (P9-A without A4) |
| P9-D2 | The slot-held voice (≈ 276.5) | **Accepted**, as D13 (the pool's worst case does not rise) |
| P9-D3 | A4 (one expo pass) | **Dropped.** Kept in §5 for the record; its +1.03 per sample is accepted |
| P9-D4 | The overdrive curve (×1 … ×16) and the tone range (open … ≈ 800 Hz) | Open: set by listening on the device; the tables are generated |
| P9-D5 | The menu page and positions | **Decided: the mix page**, aligned with the other four types: `vol`, `pan`, **Tone in the `srt` cell, the VCA drive in the distortion (`drv`) cell** (§3.4). Open: the row keys and labels (P9-D9) |
| P9-D9 | The VCA drive's key, and the label clash with the filter page's `alk_drive` ("Overdriv" / `drv`) | **Decided: K1 "for now"** (§3.4.2): the shared key `instrument_drive` with an Alkaline branch in its writer, and the filter-page `alk_drive` relabelled (labels only). The new label is proposed as "InDrive" / `idr`, to be confirmed with the schedule |
| P9-D10 | The row order across instruments | **Deferred (user):** the rows of all five instruments are to be reworked together later (for example the oscillator `pos` sits at the end of the first page). P9 appends its two rows and keeps the current layout; K1 is "for now" because of that rework |
| P9-D6 | Tone: low-pass only, or bipolar (low-pass / high-pass) | **Low-pass only.** Bipolar measured at 266.6, over the ceiling (§3.3) |
| **P9-D7** | Neutral settings | **Overdrive 0 and Tone 127 leave the output bit-identical to today** (§4) |
| **P9-D8** | The new worst case | **≈ 261.5 per sample without a slot is accepted for now** (≈ +0.2 over 261.3; the +1.03 A4 would have saved) |

---

## 1. Where the budget stands (measured, the tree after P8 revision 3)

| | Per sample |
|---|---:|
| Loop, one row (`DiodeLadder_renderB2Osc`) / two rows (`…Osc2`, slot held) | 213 / 228, 3 VDIV |
| Per block (`control_cost.py --elf`) | 1,378 → 43.1 |
| **Whole voice, no slot / slot held** | **256.1 / 271.1** |
| Basic ceiling | 261.3 (3 VDIV) |
| **Left, no slot** | **5.2** |

A post stage must also leave the VDIV count alone: the Alkaline already has
3 + 9/32 per sample against the ceiling's 3. The engines' `dist` curve
divides once per sample, so it is not used here.

---

## 2. The candidates (all measured, linked, mixer gate 32 equal / 0 grew)

Per block is `control_cost.py --elf` with stand-in controls (two or three
per-block multiplies, as the real table lookups will be).

| # | What | Loops (one row / blend) | Extra pass | Per block | No slot | Slot held |
|---|---|---:|---:|---:|---:|---:|
| F | Cubic soft clip + tone + dry/wet, **fused** at the ladder output | 236 / 249 | — | 1,403 | 279.8 | 292.8 |
| S | Same, a **separate pass** after the ladder (with an output clamp) | 213 / 228 | 22 | 1,415 | 279.2 | 294.2 |
| S3 | Separate pass: cubic + tone + dry/wet, no clamp | 213 / 228 | 20 | 1,415 | 277.2 | 292.2 |
| S2 | Separate pass: cubic + tone | 213 / 228 | 18 | 1,414 | 275.2 | 290.2 |
| T1 | **VCA overdrive**: the OTA input gain a per-block variable | 215 / 227 | — | 1,388 | 258.4 | 270.4 |
| T2 | T1 + tone as a separate pass | 215 / 227 | 10 | 1,410 | 269.1 | 281.1 |
| **T3** | **T1 + a low-pass tone in the loop** | **219 / 234** | — | 1,403 | 262.8 | 277.8 |
| T3 + A3 + A4 | T3 with both control savings | 219 / 234 | — | 1,328 | 260.5 | 275.5 |
| T3B | T3 + A3 + A4 with a bipolar tone | 225 / 238 | — | 1,332 | 266.6 | 279.6 |
| **T3 + A3 (decided)** | T3 with A3 only | 219 / 234 | — | ≈ 1,361 (estimate) | **≈ 261.5** | **≈ 276.5** |

- **Fusing a general shaper is expensive.** F adds +23 per sample: the B2R
  loop has every float register in use, so each new live value spills. That
  matches the P8 finding that a 3-instruction change cost +7.
- **A separate pass costs 18–22** plus about 1.1 per sample per block.
- **The VCA overdrive is almost free** because it adds no new stage. It
  turns a constant the loop already multiplies by into a variable (§3).

---

## 3. The design: VCA overdrive and a low-pass tone

### 3.1 What the loop already does

The last step of each B2R sample is the 303's VCA, an OTA
(`DiodeLadder.c`, `dl_renderB2Body()`, the output line):

```c
const float zG = dl_clamp(vcaL * DL_INV_2VT, DL_P32_ZMAX), sG = zG * zG;
...
buf[j] = (int16_t)__SSAT((int32_t)(iaL * zG * tota), 16);   /* R_OTA I_abc tanh(v/2Vt) */
```

- `zG · tota` is the OTA's `tanh(v / 2Vt)`, as a Padé form
  `z (27 + z²) / (27 + 9 z²)`, clamped at |z| = 3. There the curve reaches
  exactly ±1 with zero slope, so the clip is smooth (no corner).
- `iaL` is the VCA current (`I_abc`, the envelope and accent), which
  multiplies the output **after** the tanh.

### 3.2 Overdrive: drive the OTA's input

- **Per sample:** `zG = dl_clamp(vcaL * otaIn, DL_P32_ZMAX)`, where
  `otaIn = g · DL_INV_2VT` is set per block. Measured: one-row loop
  213 → 215, blend loop 228 → 227.
- **The gain:** `g = 2^(4 · ovd / 127)`, ×1 … ×16, from a 128-entry flash
  table. Entry 0 is **exactly 1.0f**, so `otaIn` is exactly today's
  constant (§4).
- **Make-up:** a second 128-entry table `m(ovd)`, folded into the per-block
  make-up the render already passes (`iAbcEnd · makeup`). 0 per sample.
  Entry 0 is exactly 1.0f.
- **The sound:**
  - the filter output, resonance peak included, drives the 303's VCA into
    saturation. The OTA is the circuit's own nonlinear stage after the
    filter, so this overdrives a stage the model already has, not an added
    effect;
  - odd harmonics only (a symmetric curve);
  - the drive does **not** follow the envelope: `I_abc` scales the output
    after the tanh, so a decaying note stays as distorted as its attack;
  - this is post-filter. The existing `drive` drives the ladder's input
    pair before the filter, so its harmonics are mostly filtered away.

### 3.3 Tone: a low-pass on the OTA output, exact when open

- **Per sample, in the loop, in its exact-bypass form:**

  ```c
  const float o = iaL * zG * tota;          /* the OTA output (today's value) */
  z = o + toneC * (z - o);                  /* one-pole low-pass */
  buf[j] = (int16_t)__SSAT((int32_t)z, 16);
  ```

  - `toneC = e^(−2π fc / fs)` from a 128-entry table; `z` is the state.
  - The same one-pole as `z += k (o − z)` (`k = 1 − c`), and the same
    operations (one subtract, one multiply-add). The spike measured the
    `k` form: loops 215 → 219 (one row), 227 → 234 (blend). The schedule
    re-measures the `c` form.
- **Tone 127 is exactly open:** entry 127 is `toneC = 0.0f`, so
  `z = o + 0·(z − o) = o` exactly, whether or not the compiler fuses the
  multiply-add. The `k` form is not exact at `k = 1` (`z + (o − z)` can
  differ from `o` in the last bit, ±1 LSB after the int16 conversion). That
  is why the decided form is the `c` form.
- **Lower values:** `fc` falls toward ≈ 800 Hz (the curve is set by
  listening, P9-D4).
- **What it filters:** the whole voice output, so it rolls off the highs the
  overdrive adds, and some top of a clean voice when Overdrive is low.
- **Not measured:** the low-pass before the tanh (taming the harmonics
  generated instead of filtering them afterwards).
- **A bipolar tone was measured and does not fit Basic** (P9-D6).
  - **The form:** − rolls off highs (low-pass), + rolls off lows (high-pass),
    from one one-pole: `out = c·o + d·z`, with `(c, d)` = (0, 1), (1, −1),
    (1, 0) at the centre, set per block.
  - **Measured** (`p9T3B`): loops **225 / 238** (+6 / +4 over the
    low-pass-only form, because two more live constants spill); per block
    1,332; whole voice **266.6** / 279.6.
  - **If revisited:** the change of mode at the centre needs per-block state
    handling, or the output steps. On entering the high-pass side `z` starts
    at 0; on entering the low-pass side it starts at the last output.

### 3.4 Controls and menu placement (P9-D5, user)

#### 3.4.1 The mix page

The last page of every instrument is the mix page, and its cells line up
across the types (`DrumParameters.c` l. 412; Snare, Cymbal and HiHat the
same; `AlkalineParameters.c` l. 288):

| Cell | Drum / Snare / Cymbal / HiHat | Alkaline today | **Alkaline after P9** |
|---:|---|---|---|
| 1 | `instrument_vol` "Volume" `vol` | `instrument_vol` | `instrument_vol` (unchanged) |
| 2 | `instrument_pan` "Panning" `pan` | `instrument_pan` | `instrument_pan` (unchanged) |
| 3 | `instrument_decimation` "SampleRt" `srt` | empty | **Tone** |
| 4 | `instrument_drive` "Overdriv" `drv` (the post-chain distortion) | empty | **VCA drive** |

- **Tone takes the `srt` cell, not the `srt` row.**
  - `instrument_decimation` is bound to the mixer's per-slot sample-rate
    reducer (`ROW_SLOT_DECIMATION`, `INSTRUMENT_BIND_SLOT_DECIMATION`), and
    the Alkaline uses no SRR (`instrumentManager_slotUsesSrr()` returns 0
    for it).
  - So Tone is its own row: key `alk_tone`, category "Voice", long "Tone",
    short `ton`, a plain `ROW` (no special writer), `DTYPE_0B127`,
    `MOD_0_127`, default 127.
- **The VCA drive takes the `drv` cell.** Its key and labels are P9-D9
  below. Default 0.
- **Descriptor indices:** the two rows are appended (29 and 30). The menu
  cell comes from the page table, not the index, so no existing row moves,
  and stored `.alk` files and NRPN numbers of the existing rows are
  unchanged.

#### 3.4.2 P9-D9: the VCA drive's key and labels (decided: K1, for now)

**Two facts:**
- **The key decides the writer.** `instrumentManager` classifies rows by
  key: a row named `instrument_drive` must carry `IM_SPECIAL_INSTRUMENT_DRIVE`
  (the boot diagnostic and `check_special_tags.py` compare every row against
  the rule; `InstrumentManager.c` l. 4067). That writer
  (`InstrumentManager.c` l. 3853) looks up the slot's `Distortion` and fails
  (returns 0) for an Alkaline, which has none.
- **The label is taken.** The Alkaline's filter page already shows the
  pre-filter drive as `alk_drive` "Overdriv" / `drv`
  (`AlkalineParameters.c` l. 190). Two different `drv` cells on one
  instrument would be ambiguous.

| | **K1 (decided, user)** | K2 (not taken) |
|---|---|---|
| Key | `instrument_drive`, as the other four types | `alk_ovd` |
| Labels | "Voice" / "Overdriv" / `drv`, as the other four | "Voice" / "VCA Drv" / `vdr` |
| Writer | `IM_SPECIAL_INSTRUMENT_DRIVE` gains an Alkaline branch (slot type ALK: the byte → the overdrive gain index), ≈ 5 lines | a plain `ROW` |
| The filter page's `alk_drive` | **relabelled** (labels only; the key and the stored value are unchanged), for example "InDrive" / `idr` | unchanged |
| Alignment | key, labels and the cell match the other four types: the same name in LFO/velocity target lists, the NRPN chart's "Voice drive" group and the Learn table's CC 23 pattern | the cell only |

**The Learn CC map (`SD_CARD/midilrn.cfg`, the user's):**
- Its rows are type-matched: Drum, Snare, Cymbal and HiHat use CC 22 filter
  drive, CC 23 voice drive and CC 24 sample rate.
- An Alkaline set on the same pattern would be CC 22 → `alk_drive`, CC 23 →
  the VCA drive, CC 24 → Tone.
- The table has 8 free rows (120 of 128 used), so a full Alkaline Learn set
  does not fit beside the other four. Outside this plan.

### 3.5 State and memory

| Item | Where | Size |
|---|---|---:|
| `otaIn`, `toneC`, `toneZ` | `DiodeLadder` (inside `AlkalineVoice`) | 12 B; the voice is 548 B in a 1,264 B slot reserve: **no RAM growth** |
| Overdrive gain, overdrive make-up, tone `c` tables | flash (`AlkalineTables.c`, generated) | 3 × 512 B |
| A3 drive gain and make-up tables (§5) | flash | 2 × 512 B |

No ITCM: the ladder is not in ITCM.

---

## 4. The neutral settings: bit-identical to today (P9-D7)

**Requirement:** with Overdrive 0 and Tone 127, every output sample equals
today's, on both the one-row and the blend loop.

**Why it holds by construction:**

| Stage | At the neutral setting | Exact because |
|---|---|---|
| OTA input | `otaIn = 1.0f × DL_INV_2VT` | `x × 1.0f = x` |
| `vcaL * otaIn` | the same product as `vcaL * DL_INV_2VT` | the same two operands; only the second moves from an immediate to a register |
| Overdrive make-up | `makeup × 1.0f` | the same; folded per block |
| Tone | `z = o + 0.0f · (z − o)` | `0 · finite = ±0` and `o + (±0) = o`; a fused multiply-add gives `o` exactly too. `z − o` is always finite: `z` and `o` are bounded |
| The rest of the loop | unchanged | the ladder, secants and network do not read the new values |

**A3 at its own neutral (drive 0, the `init.alk` value) is bit-identical
too.**
- At drive 0 the runtime computes `alk_exp2f(0)⁴` and `alk_exp2f(1)`. In
  both, the polynomial's fraction is 0, so every term is
  `constant + 0 · (...)`: exact, fused or not, and the same in the generator.
- **At other drive values** the target's fused multiply-adds may round the
  last bit differently from the host generator. The generator mirrors the
  target's evaluation (`fmaf()` in the order of the linked disassembly of
  `Alkaline_calcVoiceAsync`), and the gate reports any entry that still
  differs. At most 1 ulp (≈ −144 dB relative), reported, not hidden.

**Gates (in the schedule):**
- **Golden capture before any change:** the Alkaline bench's pattern
  renders (Saw, Sqr, wtb without and with a slot; drive 0, 0.5, 1; Res and
  accent sweeps), written as files from the current tree.
- **AL-OD0 (new):** the same renders after the change, at Overdrive 0 /
  Tone 127, **byte-identical** to the capture.
- **AL-A3 (new):** each table entry against the runtime formula on the host;
  drive 0 exact, every entry ≤ 1 ulp. AL-D figures unchanged.

---

## 5. The control-rate savings

| # | Change | Per block | Per sample | Status |
|---|---|---:|---:|---|
| **A3** | `driveGain` and `makeup` from two 128-entry tables indexed by the drive byte, replacing two `alk_exp2f()` calls per block | −40 (measured) | −1.25 | **Implemented in P9** (P9-D1). Bit-identical at drive 0; ≤ 1 ulp elsewhere (§4). A modulated drive between byte steps rounds to the nearest of 128 steps (≈ 0.19 dB of input gain per step) |
| A4 | `ALK_EXPO_PASSES` 2 → 1 (`AlkalineControl.c`): one fixed-point pass of the expo current per block | −33 (measured) | −1.03 | **Dropped** (P9-D3). It changes the expo current's convergence where it moves fastest (the MEG strike, accents), so the start of the tuned filter sweep could change. The bench passed 16/16 laws with unchanged figures, but does not measure that error. Kept here for the record: if taken later, run `tools/alkaline/check_c.py --pattern` against the validated reference first |

The +1.03 per sample A4 would have saved is accepted as part of the new
worst case (P9-D8).

---

## 6. The slot case

- Holding a slot, the decided design is **≈ 276.5** per sample: 5.4 more
  than D13's accepted 271.1, and ≈ 15.2 over 261.3. Accepted (P9-D2).
- Its slot charge (≈ 276.5 − 261.5 = 15) is the same as today's blend, below
  the pool's per-slot ceiling (the `itp` fade, 45), so the pool's worst case
  does not rise.

---

## 7. Options considered

| | What | Class | Status |
|---|---|---|---|
| **P9-A′ (decided)** | VCA overdrive + low-pass tone in the loop + A3 | Basic (worst case ≈ 261.5, accepted) | **Decided** |
| P9-A | The same + A4 | Basic (260.5) | A4 dropped |
| P9-B | VCA overdrive only | Basic (258.4 / 270.4) | Not taken: no tone |
| P9-C | A separate post-VCA soft clipper (cubic) + tone, with or without dry/wet (S2 / S3) | Advanced (`DRAFT_SAMPLE_VOICE_REDUCTIONS.md` §4.7) | Not taken; a different character (cleaner as a note decays) |
| P9-D | P9-A′ in Basic, P9-C added when the Alkaline is Advanced (two registry types sharing the engine) | both | Not taken |

**Work list for the schedule:**
- **Ladder:** `DiodeLadder.c` / `.h`: `otaIn`, `toneC`, `toneZ` in the
  struct; the OTA input and the tone in `dl_renderB2Body()` (the fused paths
  and the unfused one, so AL-X stays a like-for-like law); `DiodeLadder_init()`
  sets `otaIn = DL_INV_2VT`, `toneC = 0`, `toneZ = 0`.
- **Voice:** `AlkalineVoice.c` / `.h`:
  - the `ovd` and `tone` parameters;
  - per block: `otaIn`, `toneC`, the make-up fold;
  - A3's table lookups in place of the two `alk_exp2f()` calls.
- **Tables:** `tools/alkaline/gen_alk_tables.py` → `AlkalineTables.c` / `.h`
  (overdrive gain and make-up, tone `c`, A3 drive gain and make-up);
  `alkaline_tables --check`.
- **Rows:** `AlkalineParameters.c`:
  - 29 → 31 rows, appended: the VCA drive (key per P9-D9) and `alk_tone`;
  - the mix page `{ VOL, PAN, TONE, VCA drive }` (§3.4.1);
  - under K1, the `alk_drive` labels changed.
- **Other files:**
  - under K1, the Alkaline branch in the `IM_SPECIAL_INSTRUMENT_DRIVE`
    writer (`InstrumentManager.c`);
  - `MenuText.h` if a label table is touched;
  - `make_init_alk.py` and `init.alk` (defaults 0 and 127);
  - `check_special_tags.py` (200 rows → 202).
- **Bench:** the golden capture (before any edit), AL-OD0, AL-A3; an
  overdrive law (output bounded by the OTA's tanh; level within the make-up
  curve); a tone law (−3 dB point against the table); two break builds (the
  tone's `c` form changed to the `k` form must fail AL-OD0; entry 0 of the
  gain table at 1.0001 must fail AL-OD0).
- **Gates:** `control_cost.py --elf`; `elf_looppath.py`; `elf_p4_check`
  (oscillators exact, mixer 0 grew); `link_budget.py`; RAM symbol diff.

---

## 8. Statements this would make out of date (for the user to log, if adopted)

- `knowledge_files/specification_reference/dsp_instruments_effects/`:
  - `INSTRUMENTS_DSP_REFERENCE.md` §5.5 (the Alkaline's rows and chain) and
    §8.2 (its whole-voice figures; the Alkaline becomes the dearest Basic
    voice at ≈ 261.5);
  - `CPU_USE_DSP_AUDIT.md` item 78 (the loops and per block) and the Basic
    ceiling text (261.3 → the accepted ≈ 261.5).
- `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md`: the
  `DiodeLadder` fields (+12 B inside the slot reserve).
- `knowledge_files/specification_reference/FILESYSTEM_SPEC.md`: the `.alk`
  row list (29 → 31).
- `MEMORY.md`: the Alkaline row count and its budget figure.
- Root, the user's: `MIDI_NRPN_TABLE.md`:
  - the Alkaline column gains rows 29 and 30;
  - under K1, `instrument_drive` joins the "Voice drive" group for the
    Alkaline, and the `alk_drive` labels change;
  - the Learn CC map (`SD_CARD/midilrn.cfg`) has no Alkaline rows (§3.4.2).

---

## 9. Measurement record (2026-11-10)

Scratch copies of the current tree (P8 revision 3), `make all` from clean,
`-Ofast -flto`; `elf_looppath.py --cond-heads --in-range` (loops),
`control_cost.py --elf` (per block, with `--iters alk_post*:1=0` for the
separate passes), `elf_loopdump.py` + `elf_hotloops.py --compare` against
`baselines/s093p8/mixer.txt` (32 equal, 0 grew in every variant).

| Build | Change | Result |
|---|---|---|
| `p9F` | cubic + tone + dry/wet fused before `__SSAT` | loops 236 / 249; per block 1,403 |
| `p9S` | the same as `alk_postFx()` after the dispatch, with an output clamp | pass 22; per block 1,415 |
| `p9S3` | pass without the clamp | 20; 1,415 |
| `p9S2` | pass without the clamp and the mix | 18; 1,414 |
| `p9T1` | `DL_INV_2VT` → `dl->otaIn` (per block) | loops 215 / 227; 1,388 |
| `p9T2` | T1 + a one-pole pass | loops 215 / 227, pass 10; 1,410 |
| `p9T3` | T1 + a one-pole (`k` form) on the OTA output in the loop | loops 219 / 234; 1,403 |
| `p9A3` | drive tables | per block 1,338 (−40) |
| `p9A4` | `ALK_EXPO_PASSES` 1 | per block 1,345 (−33); Alkaline bench 16/16 laws pass, figures unchanged |
| `p9T3A` | T3 + A3 + A4 | loops 219 / 234; per block **1,328**; flash headroom 43,156 B |
| `p9T3B` | T3A with a bipolar tone output `c·o + d·z` | loops 225 / 238; per block 1,332 (not taken) |

- **Not built:** T3 + A3 without A4 (the decided form) and the tone's `c`
  form. Their figures in §0 and §2 are estimates from the rows above; the
  schedule measures them.
- **Shaper curve in F/S:** `x = clamp(g·o, ±1.5)`, `y = x − (4/27) x³`.
- **Stand-in controls:** the spikes set the new per-block values from
  existing knobs (accent, tuning, drive), so they are not constant-folded.
  The real table lookups cost about the same.
