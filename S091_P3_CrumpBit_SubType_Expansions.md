# S091 P3 — CrumpBit Sub-Type Expansions: General Plan

- **Date:** 2026-11-04. **Branch:** `dev-ph7-voice_expansion`. The code
  baseline is S091 P2 as landed (`S091_P2_Implementation.md` §10).
- **Status:** **accepted on hardware (user, 2026-11-04).** Implemented
  per `S091_P3_Implementation.md` (assessment §10). On the board, no
  sub-type exceeds the previous worst case. The parameter presentation and
  handling are deferred to a separate task (`S091_P4_GRN_TWEAKS.md` for
  `grn`). §11 records the acceptance. (Originally a general plan, written
  while S091 P2 was untested.)
- **Request (user):** sub-types besides the delay:
  - pitch shift;
  - comb filter and other buffer uses;
  - granular buffer;
  - lo-fi reverb.

  Each is scaled to fit within, or very close to, what the delay sub-type
  uses now.
- **Binding:**
  - `S089_P2_WAVEFOLDER_CHAIN.md` §8 (no per-sample increase beyond a
    measured saving);
  - the constant-CPU mandate: within a sub-type every statement runs every
    frame, and the dispatch happens once per block;
  - the RAM policy: **up to 32 B more than today's 76 B runtime is
    approved for anything that helps** (user, 2026-11-04; this includes
    the earlier +8 B). The plan uses 16 B (§5). Anything beyond 32 B needs
    a new approval.
- **Builds on:**
  - `S091_P2_CrumpBit_Expanded_Plan.md` §4: the stage model and row
    ownership (`CRUMPBIT_ROWS_SHARED` / `CRUMPBIT_ROWS_DELAY`);
  - the deferred drives of that plan's §0.

---

## 0. Summary

- **`mix` is not part of any sub-type (binding, user).** It is a shared
  stage after every sub-engine: always the crossfade between the signal
  *without* the sub-engine (CrumpBit's 8-bit conversion with per-bit
  off/invert) and the sub-engine's output. It exists for every sub-type,
  never disappears, keeps lane 3 in every sub-type, and is never a freeable
  lane (§2.8).
- **The envelope a sub-type must fit** is what the delay uses today (§1):
  - about **100 instructions per frame** in the stage-2 loop beyond the
    ADC stage: the delay itself ≈ 93, plus the shared mix stage ≈ 7, which
    executes in the same loop and is identical for every sub-type. The ADC
    stage and the output filter come on top, unchanged;
  - ≈ 15–19 per frame of per-block work (measured for `dly`: the Sync
    search, the Rate limit, three divides and the shared set-up);
  - **28 B** of DSP state;
  - the five sub-type rows (`fbk rte snc dpn ton`) and their lanes.
- **All four requests fit.** Each needs one of three small "engines". The
  engines share code, so four sub-types are not four separate effects.

  | Engine | Read pattern | Sub-types | Loop estimate (per frame) |
  |---|---|---|---:|
  | **A — tape tap** | one fractional tap, glide | `dly` (today), **`cmb`** comb / resonator / flanger | ≈ 95–100 |
  | **B — windowed pair** | two fractional taps, triangular crossfade | **`pit`** pitch shift | ≈ 95–110 |
| **B′ — split record + delay** | two drop-sample pitched grain taps (16.16 positions) on a record line, one fractional tape tap on a delay line | **`grn`** pitched granular with freeze (user design, §3.3) | ≈ 107–110 |
  | **C — packed lines** | four integer taps from one 32-bit word per frame | **`rvb`** lo-fi FDN reverb | ≈ 90–105 |

- **RAM: +16 B of the approved 32 B.** The runtime is reorganised into a
  shared part (56 B) and a 36 B union of sub-type states: 92 B.
  - `dly`, `cmb`, `pit` and `rvb` need 28 B each.
  - `grn` needs 36 B, plus 6 rows (§5).
  - **Arena:** past 88 B the union outgrows the 12 B pad before
    `_sfxbuf`, so the FX arena shrinks by one 32 B granule
    (126,688 → 126,656 B). The Rate curve top is retuned to match (N1).
- **Controls.**
  - Each sub-type maps its controls onto the five sub-type rows
    (`fbk rte snc dpn ton`) plus row 15 `aux`, and `grn` adds rows 16–18.
  - Lanes come from a seven-lane pool assigned per sub-type (§2.9).
  - Sub-type-aware value text and, on the Effect page, sub-type-aware names
    make that readable (§2.5).
  - `mix` is never a sub-type control.
- **Infrastructure first (P3.0).** These come before any sub-type:
  - the runtime union and the per-block dispatch;
  - the shared mix stage;
  - the `sub` row and sub-type switching;
  - a sub-type-aware clamp;
  - the name, layout and lane-map hooks. Proof: `dly` stays bit-identical
  to S091 P2.
- **User, 2026-11-04:** comb, pitch shift and reverb accepted as planned.
  Granular redesigned to the user's split record/delay line with freeze,
  pitched grains without interpolation, a three-note pitch set and spread
  (§3.3): it fits, within the approved RAM (§5).
- **Recommended order:**
  1. `cmb` (engine A, lowest risk);
  2. `pit` (engine B);
  3. `grn` (engine B′: split lines, pitched drop-sample grains, freeze);
  4. `rvb` (engine C, a new buffer format).

---

## 1. The envelope: what `dly` uses today (S091 P2, linked)

| Resource | Delay sub-type | Source |
|---|---|---|
| Stage-2 loop | 126 per frame: the shared ADC/data-line/DAC/AC stage ≈ 25, the shared mix stage ≈ 7, **the delay itself ≈ 93** | `crumpbit_elf_check`; stage 1 counted from the loop (2 × `usat`, `and`, `eor`, `vcvt #7`, `vfma` …) |
| Output filter (shared) | 31 per frame | unchanged by any sub-type |
| Per block | Sync search 113 + Rate limit 101 + three divides + set-up ≈ 15–19 per frame (of which ≈ 7 is the search and the limit; the rest is the shared tone/filter/mix set-up) | `S091_P2_Implementation.md` §2 |
| **Whole Effect** | ≈ 172–176 per frame (pre-S091: ≈ 191) | |
| DSP state | **28 B**: `length write_pos valid delay dc_x dc_y tone_s` | `CrumpBitEffect.h` |
| Arena | up to 126,688 B, one byte per sample (2.87 s) | elastic share |
| Rows / lanes | `fbk rte snc dpn ton` (rows 6, 7, 9, 10, 11), all laned; 2 lanes free. `mix` (row 5, lane 3) is the shared mix stage, not a delay row | `CRUMPBIT_ROWS_DELAY` (in code, `mix` still sits in it until P3.0) |
| Page | s0/s1 `ton fbk rte`, s2 `snc dpn`; `mix` (s2) is the shared mix | layout |

**Where the stage-2 loop's ≈ 100 beyond the ADC stage goes** (estimate
from the P2 loop, ± a few):

| Part | Per frame |
|---|---:|
| Three block ramps (fb, gain L/R) | 3 |
| Glide | 2 |
| Fractional tap: address, wrap, two loads, interpolation | ≈ 21 |
| `valid` mute | ≈ 6 |
| Return chain: DC blocker 3, tilt 4, feedback 1, knee 5 | 13 |
| Loop write through the ADC | ≈ 8 |
| Write position and `valid` | ≈ 6 |
| **Shared mix stage** (mix ramp, dry/wet crossfade, stores): not part of the delay; identical in every sub-type | ≈ 7 |
| Constant re-materialisation and spills | ≈ 15 |
| Loop overhead | ≈ 3 |

**Budget rule proposed for P3:**
- each sub-type's loop ≤ the `dly` loop (126 including stage 1) **+ 5 %**,
  i.e. ≤ 132. **`grn` with pitched grains is estimated at 132–135**, so it
  needs either the S10 exception below or a §4 fallback;
- its per-block work ≤ `dly`'s + 5 per frame;
- so the whole Effect stays below the pre-S091 ≈ 191 in every sub-type.

That is decision S10. The worst-case Scene is set by the most expensive
sub-type, not by the one selected.

---

## 2. Shared architecture (P3.0)

### 2.1 Three stages, one sub-type stage per block

```
stage 1 (shared, per frame)     ADC -> data lines -> DAC -> AC = dry h          (inline helper)
stage 2 (sub-type, per frame)   one of: dly | cmb | pit | grn | rvb -> wet     (per-block switch)
stage 3 (shared, per frame)     MIX: out = h + mix·(wet − h)                    (inline helper, always)
stage 4 (shared, second pass)   TPT output filter (unchanged)
```

- **Dispatch:** `crumpBit_process()` does the shared per-block work, then
  one `switch (rt->subtype)` per block into `crumpBit_run<Sub>()`.
  - Each sub-type function has its own frame loop.
  - Stages 1 and 3 are `static inline` helpers inlined into every
    sub-type's frame loop, so a loop never calls out. Stage 3 (mix) runs
    in the same loop because the dry `h` and the wet exist together only
    there. A separate mix pass would need both stored: 2 × 32 floats =
    256 B of scratch RAM.
  - Constant CPU holds per sub-type: a sub-type's loop has no
    settings-dependent work.
- **Code size:** about 1–2 KB of flash per sub-type; ≈ 6–8 KB for four.
  Flash headroom is 59,508 B. Nothing goes to ITCM (1,128 B left).
- **Gates:** each `crumpBit_run<Sub>` is its own symbol, so
  `crumpbit_elf_check` gates every sub-type loop separately.

### 2.2 Runtime: shared part plus a 36 B sub-type union

| Part | Fields | Bytes |
|---|---|---:|
| Shared floats | `ac_y_l ac_y_r`, `svf_l1 svf_l2 svf_r1 svf_r2` | 24 |
| Shared codes | `ac_c_l ac_c_r` (int16) | 4 |
| Row bytes | `bit_off bit_invert subtype`, `mix`, the five sub-type rows, `filter_type filter_freq filter_reso` | 12 |
| Ramp origins | `mix_prev feedback_prev pan_prev filter_type_prev filter_reso_prev` | 5 |
| Flags | `primed` | 1 |
| Row 15 `aux` | `st1` in `grn`; later the tone drive (`tdr`) in `dly`/`cmb`/`pit` (takes the reserved `tdr` byte) | 1 |
| Reserved | the deferred `drv` row (later row 22) | 1 |
| `grn` rows 16–21 | `st2 st3 spr den amp wid` | 6 |
| Active sub-type | `active_subtype`: what is running, for switch detection and the prefix conversion (§2.4); `subtype` is what was resolved | 1 |
| Alignment | | 1 |
| **Shared total** | | **56** |
| **Sub-type union** | `union { dly; cmb; pit; grn; rvb; }` (`grn` 36 B, the others 28 B) | **36** |
| **Runtime** | | **92** (+16 B of the approved 32 B; past the 12 B pad, so the arena loses one 32 B granule, §5) |

- **Rows stay outside the union.** `write_param()` stores raw bytes
  whatever the sub-type. Their meaning changes per sub-type, not their
  storage.
- **Common prefix.** Every single-line buffer sub-type (`dly cmb pit`) starts its
  state with `length, write_pos, valid` in the same place. A switch
  between them can keep the recorded audio (§2.4).
- **Guards:** `_Static_assert(sizeof(each state) <= 36)` (the union),
  the union guard in `EffectsManager.c` 76 → 92 B, a link-map check that
  `_sfxbuf` moved by exactly 32 B and FXBUF is 126,656 B, and
  `offsetof(prefix)` equal across the buffer sub-types.
- **Q7 of the P2 plan is answered:** the `subtype` byte is now stored. It
  is counted in the 56.

### 2.3 The `sub` row

- `crump_subtype` (index 8) keeps its key. Its maximum rises from 0 to
  the number of sub-types − 1:

  | Value | Sub-type |
  |---:|---|
  | 0 | `dly` |
  | 1 | `cmb` |
  | 2 | `pit` |
  | 3 | `grn` |
  | 4 | `rvb` |

  Saved Scenes keep 0 = `dly`, so nothing changes for them.
- **Pattern-automatable (user, 2026-11-04, S7).** A Pattern track can
  switch the sub-type per step. The resulting buffer glitches are wanted
  (§2.4).
  - **Flags:** `INSTRUMENT_PARAM_FLAG_AUTOMATABLE` only, not Morph and not
    LFO. Pattern automation targets the row (block 7, 448 + 8), not a
    lane.
  - **The value:** the 7-bit Pattern value is clamped to 0..4 by the
    manager, as StereoFilter's `typ` is. 0 `dly` … 4 `rvb`, and higher
    values play `rvb`. **The sub-types are not spread over the range**
    (user, OQ3).
  - **Never on an FX-sequencer lane** (user, OQ2).
  - **Values shown as names:** `dly cmb pit grn rvb` on the STEP
    automation page and in Pattern value entry, not 0..4 (OQ5, accepted).
    That extends the `format_value3` contract beyond the Effect page for
    this row (P3.0).
- **Label:** `crumpBit_uiFormatValue3()` already prints `dly`. It grows to
  the five tokens. No `DTYPE_MENU` table is needed: all 16 ids are used.

**New rows and ownership (P3.0 / P3.3):**
- **Row 15 `crump_aux`:** a neutral per-sub-type row, aligned as
  *pitch* where a sub-type has one:
  - `st1` (root) in `grn`;
  - the **interval** in `pit`;
  - the tone drive later in `dly`/`cmb`;
  - unused in `rvb`.

  It takes lane 14.
- **Rows 16–18:** `grn`-only (`st2 st3 spr`). Their lanes come from the
  per-sub-type lane map (§2.9).
- **Masks:**
  - `CRUMPBIT_ROWS_DELAY` becomes `CRUMPBIT_ROWS_SUBTYPE`: rows whose
    meaning is set by the active sub-type (6, 7, 9, 10, 11, 15).
  - **`mix` (5) moves to `CRUMPBIT_ROWS_SHARED`,** owned by the shared mix
    stage (§2.8). It is never a sub-type row.
  - New `CRUMPBIT_ROWS_GRN`: rows 16–18.
  - The compile-time ownership guards (S091 P2 C3) extend to the three
    masks.
- **The deferred drives change index:** `tdr` becomes the `aux` row's
  meaning in the delay-family sub-types, and `drv` becomes row 22. This
  amends `S091_P2_CrumpBit_Expanded_Plan.md` §0, which had them as 15/16.
  Nothing is persisted yet, so nothing moves.

### 2.4 Switching sub-types

- **Sources of a switch:**
  - a menu edit (or MIDI, which counts as a menu edit);
  - a Scene change;
  - **a Pattern-automation step (user: allowed, glitches encouraged)**.

  All three take the same path.
- **Detection:** per block, `process()` compares the resolved `subtype`
  with the active one. A switch therefore lands on a block boundary
  (0.73 ms), at most once per block.
- **On a change:** `crumpBit_enter<Sub>(old)` runs.
- **The buffer is reinterpreted, not cleared or gated (S3, revised for the
  user's glitch preference).**
  - The arena bytes are 8-bit audio in every format. The new sub-type
    reads them as it finds them:
    - `dly`/`cmb`/`pit` read the mono line;
    - `grn` treats the two halves as record and delay line;
    - `rvb` treats the words as four lanes.
  - **The cross-format glitches are the point:** `dly` → `rvb` makes a
    burst of the delay's audio diffused through the FDN, and `grn`'s
    record half becomes `dly`'s tail.
  - **What `enter` must still do for safety:**
    - convert the previous sub-type's prefix (`write_pos`, `valid`;
      `uint32_t` in `dly`/`cmb`/`pit`, `uint16_t` in `grn`) into the new
      geometry, clamping them to the new length;
    - re-initialise every float of the new state (DC, tone, damping, phase,
      glide, grain positions and steps), so no reinterpreted union bits
      become a NaN or a huge value;
    - clamp the glide target to the new length, as `crumpBit_seat()` does.
  - `rvb`'s feedback stays bounded with any byte content: Hadamard × `g_k`
    < 1 and the 8-bit write clip. Freeze (`g_k` 0.999) holds a glitch,
    which is a feature.
- **Parameters carry over unchanged (user, 2026-11-04).**
  - Every row, and every FX-lane step, keeps its stored value.
  - The new sub-type simply reads them with its own meaning, through the
    aligned lineup of §6.
  - Nothing is reset, remapped or cleared, whether the switch comes from
    the menu, MIDI, a Scene change or Pattern automation.
  - A hand-off may jump (for example a `dly` time becomes an `rvb` size).
    That is accepted.
- **Unchanged:** the arena's protection against *other owners*. On an
  Effect *type* change, `init()` still masks everything through `valid` =
  0 (clear unless you adopt). Reinterpretation is only between CrumpBit's
  own sub-types.
- **What keeps running:** the dry path (stage 1), the mix stage and the
  output filter. The audible switch is the wet path only.
- **Cost:** `enter` is straight-line, with no loop and no bulk clear
  (≈ 60–100 instructions), plus the `rte` re-clamp (§2.6): ≈ 3–5 per frame
  in the switching block only.

### 2.5 Controls: names and values per sub-type

The five sub-type rows (plus `aux`) become each sub-type's controls (§6).
`mix` keeps its name and meaning in every sub-type. Two hooks make the
sub-type rows readable:

- **Values:** `format_value3` already receives the row index. It reads the
  shown `sub` (`menuEffects_shownParam()`) and prints the sub-type's
  meaning, for example `+7` semitones, `C#3`, `2.5` (s) or `frz`. The
  stored byte and its file key are unchanged.
- **Names (new hook, S2):** `name3(index, out)` on `effect_ui_hooks_t`. It
  replaces the descriptor's 3-letter label **on the Effect page only**
  (cells, full view, held steps), as `format_value3` does for values.
  - Other screens show the generic delay names: STEP automation, LFO and
    Pattern target lists, MIDI learn.
  - The alternative is neutral generic labels for the five sub-type rows.
    That is simpler, but worse for `dly`.
- **Layouts per sub-type (new, S12):** a `layout()` hook returns the
  `effect_select_layout_t` for the shown sub-type, falling back to the
  registry's layout.
  - The encoder already walks cells across *all* SELECT groups
    (menuEffects.c header). So `grn`'s pitch rows can sit on SELECT 3
    screens (`st1 st2 st3 spr`, then `den amp wid`), reached by the
    encoder, while
    the SELECT 3 button keeps toggling data line 2.
  - Other sub-types show no SELECT 3 screens.
  - Without the hook, the cells would show for every sub-type, as `---`.
- **Lanes:** assigned per sub-type (§2.9).

### 2.6 The share clamp becomes sub-type-aware

- **The problem:** `effective_max(index, share)` has no runtime, so it
  cannot know the sub-type. Today it clamps `rte` by the delay's Rate
  curve. That is right for `dly`. It is wrong for `grn`, whose time must
  fit *half* the share, and for `cmb`, `pit` and `rvb`, whose `rte` maps
  to short lengths that always fit.
- **Change (S8):**
  - The op gains the runtime: `effective_max(const void *rt, uint8_t
    index, const fx_share_t *share)`. The manager passes
    `effects_runtimeMember()`.
  - `effects_paramEffectiveMax()` passes it when the record's type is the
    live type, and NULL otherwise (descriptor maximum).
- **Re-resolution on a sub-type change:**
  - `rte` (7) resolves before `sub` (8), and its value does not change
    when `sub` does, so the clamp would be stale for a block.
  - CrumpBit's `write_param` for `sub` therefore requests a re-resolution
    of the BUFFER_DEPENDENT rows only (today just `rte`), through a small
    new manager call, `effects_requestBufferResolve()`. A full forced
    resolution would rewrite every row in the switching block, which costs
    more and matters now that Pattern automation can switch every step.
  - The DSP target clamp (`length − 2`) protects the audio meanwhile.

### 2.7 Per-block `valid` gating for the new sub-types

- **The saving:** the delay's per-sample `valid` mute costs ≈ 6 per tap.
  Engines B and C know their longest read per block (window, grain extent,
  longest line), so they gate the wet path **per block**: `wet_gain =
  (valid ≥ longest + frames) ? 1 : 0`. `valid` is advanced per block too.
  That saves ≈ 6–12 per frame against a per-tap mute, and the budget below
  counts on it.
- **`dly` is unchanged:** its glide can outrun `valid` within a block (S3
  in P1).

### 2.8 The mix stage (binding, user 2026-11-04)

**`mix` is not part of the delay, or of any sub-engine.** It is a shared
stage of CrumpBit that sits after whichever sub-engine is selected. It is
always the crossfade between:
- **without the sub-engine (dry `h`):** CrumpBit's own signal, i.e. the
  8-bit conversion with the per-bit off/invert masks, the DAC and the AC
  coupling (stage 1);
- **through the sub-engine (wet):** the selected sub-type's output.

```
out = h + mix·(wet − h),   mix = row/127, ramped across the block
```

- **It always exists, for every sub-type, and never disappears.**
  - **The row:** `mix` (row 5), with the same name, page cell and value
    text in every sub-type.
  - **The lane:** lane 3, fixed in every sub-type's lane map. It is
    **never in the pool and never freeable** (§2.9).
  - **The state:** the ramp origin `mix_prev`, in the shared part of the
    runtime.
  - **The mask:** `CRUMPBIT_ROWS_SHARED`.
- **What a sub-type may and may not do:**
  - It only produces its wet signal.
  - It may not read, reinterpret, ramp or skip `mix`.
  - It may not add its own dry path into the wet: the mix stage owns the
    dry path.
  - At `mix` 127 the output is exactly the sub-engine's output.

  | Sub-type | Its wet signal (what the mix stage blends in) |
  |---|---|
  | `dly` | the delay tap with its `dpn` balance (the pan is the delay's, applied before the mix) |
  | `cmb` | the comb output |
  | `pit` | the shifted heads |
  | `grn` | grains plus their echoes |
  | `rvb` | the stereo reverb |

- **Code shape:**
  - **The helper:** the mix stage is one `static inline` helper,
    `crumpBit_mixFrame(h, wet, mix)`, plus the shared ramp set-up in
    `crumpBit_process()`.
  - **Where it runs:** each sub-type's frame loop calls the helper as its
    last statement, because that is where `h` and the wet exist together
    (§2.1). The helper and the ramp belong to the mix stage; sub-type code
    only supplies `wet`.
  - **Constant cost:** ≈ 7 per frame (one ramp add, the crossfade, the
    stores), counted once and identical in every sub-type.
- **Host law, every sub-type:**
  - at `mix` 0 the output equals the stage-1 output bit for bit, before the
    output filter;
  - at `mix` 127 it equals the sub-engine's wet output.

### 2.9 Sequencer lanes per sub-type

**Today** (S091 P2 registry row) the lanes are a fixed `lanes[16]` table
per Effect type:

| Lane | Row | Token | Owner today |
|---:|---:|---|---|
| 0 | — | `mrp` (Effect Morph) | manager (fixed for every type) |
| 1 | 3 | `bof` | shared (data lines) |
| 2 | 4 | `biv` | shared (data lines) |
| 3 | 5 | `mix` | the shared mix stage (§2.8). Not a delay lane; never in the pool |
| 4 | 6 | `fbk` | delay |
| 5 | 7 | `rte` | delay |
| 6 | 9 | `snc` | delay |
| 7 | 1 | `vol` | common |
| 8 | 2 | `pan` | common |
| 9 | 10 | `dpn` | delay |
| 10 | 11 | `ton` | delay |
| 11 | 12 | `typ` | output filter |
| 12 | 13 | `frq` | output filter |
| 13 | 14 | `res` | output filter |
| 14 | — | free | (reserved for the deferred drives) |
| 15 | — | free | (reserved for the deferred drives) |

**Proposal (user: lanes released and reassignable per sub-type):**
- **Nine shared lanes** keep their row in every sub-type: 0 `mrp`, 1 `bof`,
  2 `biv`, **3 `mix`**, 7 `vol`, 8 `pan`, 11 `typ`, 12 `frq`, 13 `res`.
  **Lane 3 (`mix`) can never be released or reassigned.**
- **The seven-lane pool** (4, 5, 6, 9, 10, 14, 15) is assigned by each
  sub-type.

| Lane | `dly` | `cmb` | `pit` | `grn` | `rvb` |
|---:|---|---|---|---|---|
| 4 | `fbk` | `fbk` resonance | `fbk` regeneration | **`st2`** | `fbk` decay |
| 5 | `rte` time | `rte` note | `rte` window | **`st3`** | `rte` size |
| 6 | `snc` Sync | `snc` polarity | `snc` snap | `snc` → `frz` | `snc` → `frz` |
| 9 | `dpn` delay pan | `dpn` pan | `dpn` wet pan | `dpn` → `pos` | `dpn` width |
| 10 | `ton` tone | `ton` damping | `ton` tone | `ton` → `sze` | `ton` damping |
| 14 | `aux` (tone drive, later) | `aux` (tone drive, later) | `aux` → interval | `aux` → `st1` | — |
| 15 | — | — | — | **`spr`** | — |

- **`grn` keeps the lanes whose rows it shares in meaning** (`frz` 6,
  `pos` 9, `sze` 10, `st1` 14). It gives lanes 4, 5 and 15 to its pitch
  rows `st2`, `st3` and `spr`.
- **`grn`'s `fbk` and `div` are then not lane-sequenced.** They stay
  Morph-, LFO- and Pattern-automatable (Pattern automation targets rows,
  not lanes). Which rows `grn` sequences is decision L2.
- **The deferred `drv`** (the shared output-filter drive) has no lane in
  this map. Either it stays lane-less, or it takes lane 15 everywhere and
  `grn`'s `spr` loses its lane (L3).

**Mechanics:**
- **A lane-map op.** It returns the 16-entry table for a record's `sub`
  value (`record->normal[CRUMPBIT_PARAM_SUBTYPE]`). Types without it keep
  `entry->lanes`.
  - The maps are const tables in flash (5 × 16 B): **0 B RAM**.
  - **Every reader goes through it.** The readers are all in
    `EffectsManager.c`:
    - lock consumption (HEAD ≈ 905);
    - `effects_seqOverride()` (≈ 964);
    - `effects_laneOfParam()` (≈ 1024);
    - the lane-lock edit clamp (≈ 1063);
    - `effects_laneFileKey()` / `effects_laneByFileKey()` (≈ 488);
    - the registry self-check (≈ 1653).
  - The lookups gain the record, or its `sub` value, as a parameter.
- **Storage.** The `.fx` `[sequence]` keys are lane → row file keys, so
  they depend on the Scene's sub-type. `storageTypes.c` (three call sites)
  passes the record it is writing or reading. The `.fx` reader must know
  `sub` before it maps lane keys: P3.0 verifies the section order (rows
  before the sequence), or defers lane mapping to the end of the record.
- **Self-check.** In every map:
  - lane 0 is Morph;
  - lane 3 is `mix`;
  - the shared lanes are identical;
  - no row appears twice.
- **Which map applies: the Scene's *stored* sub-type**
  (`record->normal[8]`), for resolution and for storage alike. A
  Pattern-automated switch changes the *active* sub-type for a while but
  not the Scene's lanes.
  - **The effect:** while automation plays `grn` in a `dly` Scene, lane 4
    still sends its locks to row 6. In `grn`, row 6 is feedback, so the
    lock is simply reinterpreted.
  - A pool lane whose row means nothing in the active sub-type (a
    `grn`-only row while `dly` plays) has no effect.
  - Nothing is cleared or remapped per step.
- **Data on a sub-type change (L4, user 2026-11-04): nothing is cleared.**
  - A lane's step values stay in the lane, and in the new map they drive
    whatever row that lane names. For example, `dly` feedback locks on lane
    4 become `grn` `st2` pitch locks.
  - Hand-off glitches are accepted. The lanes of rows that exist in both
    sub-types (lanes 6, 9, 10, 14) carry their meaning across as aligned
    in §6.

---

## 3. The sub-types

### 3.1 `cmb` — comb, resonator, flanger (engine A)

- **What:** the delay engine retuned to short lengths.
  - `rte` sets the comb length, shown as its note. **A higher `rte` is a
    longer comb, so a lower note**, aligned with every other sub-type,
    where a higher `rte` is longer or larger (§6). The length table runs
    from 8 samples (5.5 kHz) to about 2,048 samples (21.5 Hz), in semitone
    steps (S9).
  - The glide becomes fast (≈ 5 ms instead of 150 ms), so retuning and an
    Effects LFO on `rte` (flanger, vibrato) follow quickly.
  - The return chain is reused unchanged: DC blocker → `ton` tilt (damping)
    → `fbk` up to 1.2 → knee.
- **What it gives:**
  - Karplus-Strong plucks: `ton` negative, `fbk` ≈ 1.0, excited by drums
    through the send;
  - tuned resonators;
  - self-oscillating drones above unity, bounded by the knee;
  - flanging, through an Effects LFO on `rte`.
  - The 8-bit re-quantisation in the loop adds grit, which suits the type.
- **`snc` → polarity:** on = negative feedback (hollow, odd harmonics).
  The sign is folded into the per-block feedback gain, so it costs 0 per
  sample.
- **`dpn`** stays the pan of the wet (as in the delay).
- **Arena:** ≤ 2,050 B. The rest of the share is untouched and keeps the
  recording for a switch back.
- **Cost:** the delay loop with a different table and glide constant:
  **≈ 95–100**. Per block: one table read instead of the Sync search, so
  per-block work is *lower* than `dly`'s.
- **State (28 B):** the delay's seven words, unchanged.

### 3.2 `pit` — pitch shifter (engine B)

- **What:** the classic rotating-head ("Doppler") shifter on the 8-bit loop.
  - Two read heads trail the write head by `d = 2 + W·φ` and
    `2 + W·frac(φ + ½)`.
  - The phase advances by `φ += (1 − r)/W` per sample, wrapping in
    [0, 1), where `r` is the pitch ratio and `W` the window.
  - Triangular windows `w_A = 1 − |2φ − 1|` and `w_B = 1 − w_A` sum to 1
    (one `vabs` and two FMAs).
- **With feedback** (`fbk`, through the same return chain), each repeat is
  shifted again, which gives shimmer and cascading octaves. `ton` shapes
  the cascade.
- **Controls:**
  - **`aux` (row 15) → interval,** −24 … +24 semitones (centre 63 = 0),
    the same row and lane (14) as `grn`'s root pitch `st1` (alignment,
    §6). It is continuous, or snapped to semitones while `snc` is on (S6).
    The ratio comes from a 128-entry table, so there is no `exp2f`.
  - `rte` → window `W`, 10–200 ms. Short windows are grainy and fast; long
    ones are smooth and smeared.
  - `fbk` and `ton` as in the delay; `dpn` is the wet pan, as in the
    delay. `pit` has no tone drive (`aux` holds the interval).
- **Arena:** ≤ 8,824 B (200 ms + guard).
- **Cost:**

  | Part | Per frame |
  |---|---:|
  | Two taps (2 × ≈ 21) | ≈ 42 |
  | Phase and wrap | ≈ 4 |
  | Windows | ≈ 4 |
  | Return chain | 13 |
  | Write | ≈ 8 |
  | Write position | ≈ 4 |
  | Shared mix stage (not part of `pit`) | ≈ 7 |
  | Ramps | ≈ 3 |
  | Spills | ≈ 15 |
  | **Total** | **≈ 95–110** |

  The two per-tap `valid` mutes are replaced by the per-block gate (§2.7).
- **State (28 B):** the prefix (12) + `φ` 4 + `dc_x dc_y tone_s` 12 = 28.
- **Variant `rvs` (reverse delay):** the same engine with `r` = −1 and
  `W` = the delay time. Each window plays a slice backwards. It is a
  candidate sixth sub-type at no extra cost (§3.5).

### 3.3 `grn` — pitched granular record line plus tape delay (engine B′)

**User design (2026-11-04).**
- Split the buffer 50/50 into a **grain/record line** and a **delay line**
  of the same length, tempo-synced.
- **Freeze** the record line while the grains keep feeding through the
  delay line, with variable feedback like the existing delay, tape-style.
- **Pitched grains without interpolation.**
- A grain **pitch set of three semitone values**, and a **spread** that
  fans grains out from the root to the other two up to about 80, and above
  that adds a slight random detune around the semitones.

**Verdict: it fits.**
- The loop is ≈ 107–110 per frame against the delay's ≈ 100, tape glide
  included.
- The pitch set, spread, detune and direction are decided per block, so
  they cost ≈ 1 per frame.
- RAM comes from the approval of up to 32 B (§5). With rows 19–21 the
  runtime is 92 B, and the arena shrinks by one 32 B granule.

How it fits:
1. **Grain positions are 16.16 fixed point.**
   - Each sample adds the grain's pitch step to its position. The top 16
     bits are the sample index.
   - One load reads the sample, with no interpolation (classic
     drop-sample sampler pitching, user decision). That costs about +1
     instruction per grain over a native-pitch read.
   - Reverse is a negative step.
   - Interpolated grains would cost ≈ +26 and do not fit (G6).
2. **Freeze takes the `snc` row,** so `grn` time is always tempo-synced
   (G1).
3. **The pitch set and grain options get new rows** (§3.3.5). Their
   bytes, the two pitch increments and the per-grain level and pan are
   what the approved RAM pays for.

#### 3.3.1 Signal flow

```
                 ┌──────────── record line R (first half of the share) ────────────┐
½(hL+hR) ──┬──►  write unless frozen  (rec[wp] = x, skipped by IT when frozen)     │
           │                                                                       │
           │     grain A ◄─ rec[posA >> 16];  posA += incA   (drop-sample, window) │
           │     grain B ◄─ rec[posB >> 16];  posB += incB   (half a grain later)  │
           │                  │ g = wA·A + wB·B   (stereo: A leans L, B leans R)   │
           │                  ▼                                                     │
           │     delay line D (second half) ◄─ adc(g + knee(fb · dc(tap)))          │
           │     tap = D[wp − delay], fractional, tape glide (as `dly`)             │
           │                                                                       │
 dry h ────┴──────────────────┐   wet = spread(gA, gB) + tap
                              ▼
               shared mix stage: out = h + mix·(wet − h)  → output filter
```

- **One write position** `wp` serves both lines: they have the same
  length, and the lines are two halves of the share.
- **One time control** `rte` sets the record window *R* and the delay
  time *D* together (*R* = *D*), so frozen grains repeat in time with the
  echoes.
- **The wet signal** is the grains plus their echoes. `fbk` is the delay
  line's feedback, as in `dly`: DC blocker, up to 1.2, cubic knee, no tone
  stage.
- **Freeze:**
  - The record line stops being written: an `IT`-predicated `strb`, so the
    cost is constant either way.
  - `wp` keeps advancing for the delay line.
  - Grains keep playing over the frozen loop, and new grains pick new
    positions in it (§3.3.4).
  - Unfreezing resumes recording at `wp`.

#### 3.3.2 Lengths and tempo

| Share | Each line | Longest synced time at 120 BPM |
|---|---:|---|
| Whole arena (126,656 B after the P3 runtime growth, §5) | 63,328 B = **1.436 s** | ½ bar (1.0 s); a full bar needs ≥ 168 BPM |
| 12 voice units (73,696 B) | 36,848 B = 0.835 s | ¼ bar (0.5 s); ½ bar needs ≥ 144 BPM |

- The divisions offered are those that fit *R* ≤ line − 2, found by the
  live-limit Sync search against the half share (§2.6).
- 16.16 positions need line length × 65,536 < 2³². A line is at most
  63,344 samples, so 16 bits of integer position suffice.

#### 3.3.3 Grain mechanics

- **Two grains, half a grain apart.** One age counter serves both.
  - Grain B's age is A's + *G*/2 (mod *G*).
  - The triangular windows `wA = 1 − |2·age/G − 1|` and `wB = 1 − wA` sum
    to 1.
- **Grain size** *G* = *R*/32 … *R*, in rhythmic steps, set by `ton`. The
  minimum is clamped to 64 frames, so a grain restarts at most once per
  block.
- **Per-grain pitch:**
  - **The step:** each grain plays at its own increment
    `inc = r · 2¹⁶` (stored as a signed Q3.12 `int16_t`, ±8, 0.4 cent
    resolution at unity).
  - **Per sample:** `pos += inc << 4` (one `add` with a shifted operand)
    and a wrap at the line end (`pos −= (pos ≥ L·2¹⁶) ? ±L·2¹⁶ : 0`, where
    the sign follows the direction).
- **Restart:** when a grain's age wraps, it takes a new position, step and
  direction. These were drawn **once per block** (§3.3.6), so per sample
  the restart is a select, not a branch.
- **Headroom (a per-block rule):** the record head must never be overtaken
  while recording.
  - A forward grain at ratio *r* > 1 starts at least *G*·(*r* − 1) + 2
    behind `wp`.
  - A reverse grain starts no further back than `valid` − *G*·|*r*|.
  - While frozen, positions wrap freely inside the frozen loop.
- **Gate:** the wet path opens per block once `valid` ≥ *R* + *G*·max *r*
  (§2.7).

#### 3.3.4 Grain selection: where a new grain starts

Each restart picks a start position behind the write head, inside the
window *R* and inside `valid`, then the headroom rule above.

| # | Mode | Rule | What it sounds like |
|---|---|---|---|
| M1 | **Follow** | start just behind the head (*G* back) | grains trail the input closely: a smeared, stuttering delay |
| M2 | **Random spray** | start uniformly in [*G*, *G* + amount·(*R* − *G*)] | the classic cloud |
| M3 | **Slice** | start on a random slice boundary, *R*/*N* with *N* = *R*/*G* | rhythmic re-ordering of the loop's own steps: beat-slicing |
| M4 | **Scan** | each new start = previous − stride (wrapping in *R*) | time-stretch or scrub through the frozen loop without pitch change |
| M5 | **Anchor + random** | grain A keeps its start; grain B draws M2 | a steady motif with scattered fragments |
| M6 | **Repeat chance** | with probability *p* keep the previous start | glitchy repeats |
| M7 | **Recent-weighted** | M2 with u² instead of u | mostly near the head, sometimes far back |
| M8 | **Pattern-placed** | a deterministic position, set per step through the `dpn` lane | composed slicing |

**Recommended mapping (G4).** One bipolar row, `dpn` (centre 63):
- 63 = **M1** follow;
- right of centre = **M2** spray, increasing;
- left of centre = **M3** slice, with increasing randomness of the slice
  choice;
- **M8** through `dpn` lane locks.

M4–M7 are later options.

#### 3.3.5 Grain parameters

**Existing rows, `grn` meaning:**

| Row | `grn` meaning | Page name |
|---|---|---|
| `rte` (7) | time *R* = *D*: a StepScale division that fits the half share (always synced) | `div` |
| `snc` (9) | **freeze** the record line (on/off; Pattern-automatable and sequenceable) | **`frz`** (user) |
| `dpn` (10) | position: slice ◄ 63 follow ► spray | **`pos`** (user) |
| `ton` (11) | **grain size, bipolar** (user): right of 63 plays forward, left plays reversed; the distance from 63 sets the size *R*/32 … *R* (63 itself = the shortest, forward) | **`sze`** (user) |
| `fbk` (6) | delay-line feedback 0–1.2 | `fbk` |

`mix` is not a `grn` row: it is the shared mix stage (§2.8), which blends
`grn`'s wet (grains + echoes) with the signal without the sub-engine.

**New rows (the user's pitch set):**

| Index | Key (proposed) | Page name | Range and display | Default | Morph / LFO / Pattern | Lane |
|---|---|---|---|---|---|---|
| 15 | `crump_aux` | `st1` | **root**, −24 … +24 semitones (`DTYPE_PM63`, centre 63 = 0; values beyond ±24 clamp) | 63 (0) | yes / yes / yes | 14 |
| 16 | `crump_grain_st2` | `st2` | **second pitch**, −24 … +24 semitones relative to the root | 63 (0) | yes / yes / yes | 4 in `grn` (§2.9) |
| 17 | `crump_grain_st3` | `st3` | **third pitch**, −24 … +24 semitones relative to the root | 63 (0) | yes / yes / yes | 5 in `grn` |
| 18 | `crump_grain_spread` | `spr` | **spread**, 0..127 (shown 0..127) | 0 | yes / yes / yes | 15 in `grn` |
| 19 | `crump_grain_density` | `den` | **density**: each grain plays for 25 … 100 % of its period, silent for the rest | 127 (100 %) | yes / yes / yes | — |
| 20 | `crump_grain_amp` | `amp` | **level jitter**: each grain's level is drawn at its restart, from none at 0 down to occasional silent grains at 127 | 0 | yes / yes / yes | — |
| 21 | `crump_grain_width` | `wid` | **random pan**: each grain's pan is drawn at its restart, from the fixed A-left/B-right lean at 0 to hard L/R at 127 | 0 | yes / yes / yes | — |

- **Pitches are relative to the root,** so a Morph or LFO on `st1`
  transposes the whole set. A grain's total pitch is clamped to ±24
  semitones (ratio 0.25 … 4).
- **Row 15 is the neutral `aux` row** pulled forward from the deferred
  drives (§2.3). It is `st1` in `grn`, and the return-knee drive (`tdr`)
  in `dly`/`cmb`/`pit` when the drives land. It takes the reserved `tdr`
  byte and lane 14. The deferred `drv` becomes a later row 22 with the
  other reserved byte. It has no lane (L3).
- **Lanes for rows 16–18** come from the per-sub-type lane map (§2.9).
  **Rows 19–21 have no lane:** the pool is full. All six are Morph-, LFO-
  and Pattern-automatable.
- **Rows 19–21 were approved with the RAM (user, OQ7).** Their order of
  priority under the CPU limit is N2 (§10).

**Spread (`spr`), per grain at its restart:**

| `spr` | Pitch choice | Detune |
|---|---|---|
| 0 | every grain at `st1` | none |
| 0 → 40 | `st2` fades in: weights (1 : a : 0), a = `spr`/40; at 40, half the grains play `st2` | none |
| 40 → 80 | `st3` fades in: weights (1 : 1 : b), b = (`spr` − 40)/40; at 80, equal thirds | none |
| 80 → 127 | equal thirds | random per grain, uniform in ±*d* cents, *d* = (`spr` − 80)/47 · *d*ₘₐₓ |

- The fan-out runs root → second → third, as asked. A chord set (0, +7,
  +12) becomes a root drone at low spread, fifths at 40, and the full chord
  at 80, then a chorus-like shimmer above 80.
- ***d*ₘₐₓ ≈ ±35 cents** (decision G8; ±50 sounds deliberately out of
  tune).
- **The ratio** comes from a 49-entry semitone table; the detune is
  applied linearly as `1 + c·0.000578` (exact to 0.1 % within ±50 cents).
  There is no `exp2f`.

**Direction: the sign of `sze` (user, 2026-11-04).** Every grain in a
block plays the same direction: a negative step when `sze` is left of
centre. A random per-grain reverse (a `rev` probability row) is not in the
set.

**Other grain parameters considered.** `den`, `amp` and `wid` are in the
set above. The rest are not:

| Candidate | Effect | Per frame | RAM | Verdict |
|---|---|---:|---:|---|
| `den` density / duty | grains play for 25–100 % of their period; gaps make sparse, rhythmic grains | +3 | 1 B row | **in the set** (row 19) |
| `wid` random pan | each grain's pan drawn at its restart (0 = the fixed spread … 127 = hard L/R) | +2 | 1 B row + 2 B state | **in the set** (row 21) |
| `amp` level jitter / dropout | random level per grain, down to silent (a "chance" control) | +2 | 1 B row + 2 B state | **in the set** (row 20) |
| `shp` window shape | triangle → trapezoid (punchier, clickier grains) | +2 | 1 B row | optional |
| `rev` reverse probability | each grain reversed at random (on top of the `sze` sign) | +0 (per block) | 1 B row | optional: costs no per-sample CPU, and RAM is approved |
| grain → record feedback | grains fed back into the record line while recording (cloud build-up) | +3 | 1 B row | conflicts with freeze; later |
| interpolated grains | smooth pitch | +26 | — | does not fit (G6) |

RAM is no longer the limit (32 B approved). CPU is: see N2 (§10).

#### 3.3.6 Cost and state

| Part | Per frame |
|---|---:|
| Ramp (fb) | 1 |
| Mono feed | 2 |
| Record write: ADC + predicated store | ≈ 6 |
| Grains: age/wrap 3, restart selects 6, window 5, two drop-sample reads with step and wrap 2 × 9, sum 2 | ≈ 34 |
| Delay tap, tape style (glide 2 + fractional tap 21) | ≈ 23 |
| Delay write: DC 3, fb 1, knee 5, sum 1, ADC 4, store 1 | ≈ 15 |
| Shared `wp` (`valid` per block) | ≈ 3 |
| Stereo spread of A/B, plus the tap | ≈ 2 |
| Shared mix stage (not part of `grn`) | ≈ 7 |
| Loop overhead | 3 |
| Spills (as P2) | ≈ 15 |
| **Total, core** (pitch set and spread) | **≈ 107–110** (`dly` ≈ 100; +7–10 %) |
| + `den` (duty clamp of the window) | +3 |
| + `amp` (per-grain level, one multiply each) | +2 |
| + `wid` (per-grain pan, two multiplies each instead of the fixed lean) | +2 |
| **Total with rows 19–21** | **≈ 114–117** (+14–17 %: see N2) |

**Per block, for each grain that may restart in this block:**
- one start position (§3.3.4);
- one pitch slot by the spread weights, plus a detune above 80;
- the direction from the sign of `sze`;
- the step `inc` and the headroom clamp;
- plus the Sync search and share clamp, as for `dly`.

The **randomness** is one hardware RNG word per block (`GetRngValue()`).
Successive reads within a block return the same word (it refreshes every
≈ 833 ns), so the fields are derived from that word by a small integer
hash (xorshift) (G9). That is control randomness, not the audio noise
whose software PRNG S073 rejected. Per-block total ≈ `dly`'s + 1–2 per
frame.

**State: 36 B** (the union grows from 28 to 36 B, §5):

| Field | Bytes |
|---|---:|
| `write_pos length valid age` (`uint16_t`, all ≤ 63,344) | 8 |
| `posA posB` (16.16 `uint32_t`) | 8 |
| `incA incB` (signed Q3.12 `int16_t`) | 4 |
| `delay` (float glide) | 4 |
| `dc_x dc_y` | 8 |
| `ampA ampB panA panB` (`uint8_t`, drawn at each grain's restart) | 4 |
| **Total** | **36** |

**Hand-off:** like every sub-type, `grn` reinterprets the arena on a
switch (§2.4).
- Switching from `dly`, `cmb` or `pit` turns the first half of their
  recording into `grn`'s record line, and the second half into its delay
  line, at once. Freeze it and the old delay audio is granulated.
- `write_pos` and `valid` are converted to the half length.

### 3.4 `rvb` — lo-fi reverb, 4-line FDN (engine C)

- **What:** a four-line feedback delay network with a 4 × 4 Hadamard mix,
  stored as **four 8-bit lanes in one 32-bit word per sample**. One write
  pointer serves all four lines, and one `str` writes all four.
  - **Reads:** the line delays `d_k` are integers. A reverb needs no
    fractional delay, and size changes are per block. Each tap is
    therefore `ldrsb [lane_k, idx, lsl #2]` after a compare-wrap: ≈ 6
    instructions instead of the ≈ 21 of a fractional tap.
  - **The lanes hold offset-binary codes.** One `eor r, #0x80808080`
    converts the packed word, so `ldrsb` reads signed values directly.
- **Per sample:**
  1. four tap reads;
  2. per-line damping (a one-pole low-pass, set by `ton`);
  3. Hadamard (8 add/sub, scale ½);
  4. per-line decay gains `g_k` (from `fbk` as RT60, per block) plus the
     input;
  5. four ADC conversions, packed (3 × `orr … lsl`), `eor`, one `str`;
  6. stereo out as two orthogonal line sums.
- **Controls:**

  | Row | Meaning |
  |---|---|
  | `rte` | size: base delays {1,433, 1,601, 1,867, 2,053} samples (32–47 ms, mutually prime) scaled ×0.25 … ×2.5 |
  | `fbk` | decay, RT60 0.1–10 s (table → `g_k` per block) |
  | `ton` | damping: negative darker, 0 neutral, positive thinner |
  | `dpn` | width: mono … wide |
  | `snc` | **freeze** (`g_k` → 0.999, input off) |

  `mix` is the shared mix stage (§2.8), not a reverb control.

- **Character:** 8-bit lines re-quantise every pass. Tails get grainy and
  stop at about −42 dB (the ½-LSB dead zone), which makes a gated, crunchy
  "lo-fi" reverb. Sixteen-bit lanes (two words per frame) would give clean
  tails at ≈ +6 per frame (S4).
- **Arena:** 4 B per frame × ≤ 5,133 frames (2,053 × 2.5) ≈ 20.5 KB. It
  always fits the minimum share.
- **Gate:** the wet path *and* the line feedback open per block, once
  `valid` ≥ the longest line. So stale arena bytes from another owner are
  never recirculated after an Effect *type* change (clear unless you
  adopt, §2.7). A sub-type switch reinterprets CrumpBit's own bytes
  instead (§2.4).
- **Cost:**

  | Part | Per frame |
  |---|---:|
  | Four taps | ≈ 24 |
  | Damping | ≈ 8 |
  | Hadamard | 8 |
  | Gains and input | ≈ 8 |
  | Packed write | ≈ 20 |
  | Output | ≈ 6 |
  | Write position | ≈ 3 |
  | Spills | ≈ 15 |
  | **Total** | **≈ 90–105** |

  Per block: four `g_k` from a table, plus integer delay updates.
- **State (28 B):** `write_pos length valid` (12) + four damping states
  (16) = 28. Not prefix-compatible: the buffer format differs (§2.4).
- **Not possible in this envelope:** input diffusion (allpasses) or a
  modulated tank. Either would need a second packed word (8 lanes) at
  ≈ +20–25 per frame. That is listed as a later option, not P3.

### 3.5 Other buffer uses (candidates, not scheduled)

| Candidate | Engine | Cost against `dly` | Note |
|---|---|---|---|
| `rvs` reverse delay | B (`r` = −1, `W` = time) | ≈ same as `pit` | backwards slices; Sync divisions as for `dly` |
| `stt` stutter / beat repeat | A | ≈ same as `dly` | capture one step division and loop it; `snc` arms; synced to `seq_getBpm()` |
| `tps` tape stop/start | A | ≈ same | the read rate ramps to 0 and back (glide on rate) |
| Ping-pong (stereo loop) | A ×2 | ≈ +30 | two lanes and two writes: over budget unless the output filter is bypassed |

---

## 4. Budget (estimates, per frame; `dly` measured)

| Sub-type | Engine | Stage-2 loop incl. stage 1 | Per block | Whole Effect (+ 31 output filter) | vs pre-S091 ≈ 191 |
|---|---|---:|---:|---:|---|
| `dly` (measured) | A | 126 | ≈ 15–19 | ≈ 172–176 | below |
| `cmb` | A | ≈ 120–126 | ≈ 9–12 | ≈ 160–169 | below |
| `pit` | B | ≈ 120–135 | ≈ 9–12 | ≈ 160–178 | below |
| `grn` (core) | B′ | ≈ 132–135 | ≈ 16–21 | ≈ 179–187 | below (closest to the limit) |
| `grn` + `den amp wid` | B′ | ≈ 139–142 | ≈ 17–22 | ≈ 187–195 | **at or over the limit**: N2 decides what ships |
| `rvb` | C | ≈ 115–130 | ≈ 10–13 | ≈ 156–174 | below |

- **Every loop figure includes** the ADC stage (≈ 25) and the shared mix
  stage (≈ 7), identical in all sub-types.
- **A block that switches sub-type** costs extra once (§2.4): ≈ 3–5 per
  frame for that block. It must keep the whole Effect below ≈ 191 in the
  heaviest pair (any → `grn`). P3.0 measures it.
- **Caveat on the estimates:** S091 P2's loop came in ≈ 14 above its
  estimate because of register pressure. Every estimate here includes ≈ 15
  for spills, but each sub-type must be prototyped and counted (as P1 and
  P2 were) before its schedule. `pit` is the one most at risk of exceeding
  `dly`.
- **Fallbacks if a sub-type exceeds the rule:**
  - `pit`: drop the feedback return chain to DC + feedback only (−9);
  - `grn`: feedback ≤ 0.99 without the DC blocker (−3; it also frees
    8 B); then no stereo lean for grain A/B (−2);
  - `rvb`: undamped lines (−8).

---

## 5. RAM

| Item | Bytes |
|---|---:|
| Today `CrumpBitRuntime` | 76 (73 used + 3 alignment) |
| P3 layout: shared 56 + union 36 | **92** (+16 B of the 32 B approved, user 2026-11-04) |
| Shared part | 56: the stage-1 and output-filter state, all row bytes (incl. `subtype`, `active_subtype`, row 15 `aux`, `grn` rows 16–21, the reserved `drv` byte), ramp origins, `primed`, alignment |
| `dly` state | 28 (`length write_pos valid delay dc_x dc_y tone_s`) |
| `cmb` state | 28 (same as `dly`) |
| `pit` state | 28 (prefix 12, `phase` 4, `dc_x dc_y tone_s` 12) |
| `grn` state | 36 (`write_pos length valid age` `uint16_t` 8, `posA posB` 16.16 8, `incA incB` Q3.12 4, `delay` 4, `dc_x dc_y` 8, `ampA ampB panA panB` 4) |
| `rvb` state | 28 (`write_pos length valid` 12, four damping states 16) |
| Static RAM | 0 |
| Flash | ≈ +6–8 KB code, ≈ +1.5 KB tables (cmb lengths, pit ratios, the 49-entry semitone table, grn/pit sizes, rvb RT60) |

- **Where it lands:**
  - `effects_runtime` grows from 76 to 92 B. That passes the 12 B pad
    before `_sfxbuf` (which holds up to 88 B).
  - `.dtcmz` therefore ends 32 B later, and the **FX arena shrinks by one
    32 B granule:** 126,688 → 126,656 B, base `0x20001120` → `0x20001140`.
  - That is 0.7 ms of 8-bit delay, inaudible. It is included in the
    approval (user: anything up to 32 B that helps).
- **The constant that follows the arena (N1):** `CRUMPBIT_LOOP_MAX_BYTES`
  and the Rate curve top (`CRUMPBIT_DELAY_CURVE_MAX`, `rateTable[127]`)
  are set to the new arena: 126,656 B and 126,654 samples.
  - The table is regenerated, and every `rte` value plays 0.025 % shorter,
    which is inaudible. No backward compatibility applies.
  - Otherwise `rte` 127 would no longer fit the whole arena and would
    clamp to 126 (2.76 s instead of 2.87 s).
- **Unspent:** 16 B of the approval. Nothing else in P3 needs it, and it
  stays in reserve.
- **The P2 guards change with it:**
  - `sizeof(CrumpBitRuntime)`: 76 → 92;
  - the union guard;
  - the `link_budget.py` expectation: FXBUF 126,656 B.

---

## 6. Row meaning per sub-type

- **The five sub-type rows** keep their keys and ranges. Only the
  meaning, the Effect-page name (§2.5) and the value text change, and their
  lanes come from the sub-type's lane map (§2.9).
- **Row 15 `aux`** is new and per-sub-type.
- **Rows 16–21** exist for `grn` only (§3.3.5).
- **`mix` is not in this table.** It is the shared mix stage (§2.8): the
  same row, name, meaning and lane (3) in every sub-type.

**Alignment (user, 2026-11-04).** Because values carry over on a
sub-type change (§2.4), each row has one role across sub-types, as far as
the sub-types allow:

| Row | Role in every sub-type | Direction |
|---|---|---|
| `fbk` | how much comes back / how long it lasts | higher = more |
| `rte` | length or size of the buffer use | **higher = longer/larger everywhere** (`cmb`: longer comb = lower note) |
| `snc` | the sub-type's switch | on = the special mode (Sync, polarity, snap, freeze) |
| `dpn` | where the wet sits (pan, width, position) | centre 63 = neutral |
| `ton` | colour or grain shape, bipolar | centre 63 = neutral |
| `aux` | **pitch** where the sub-type has one (`pit` interval, `grn` root); otherwise the later tone drive | centre 63 = 0 |

| Row (key) | `dly` | `cmb` | `pit` | `grn` | `rvb` |
|---|---|---|---|---|---|
| `fbk` (`crump_feedback`) | feedback 0–1.2 | resonance 0–1.2 | regeneration 0–1.2 | delay-line feedback 0–1.2 | decay RT60 0.1–10 s |
| `rte` (`crump_rate`) | time 20 ms–2.87 s | comb length (shown as a note; higher = lower) | window 10–200 ms | time *R* = *D*, synced division ≤ 1.436 s | size ×0.25–2.5 |
| `snc` (`crump_sync`) | Sync | negative polarity | semitone snap | `frz` freeze (record line) | freeze |
| `dpn` (`crump_dly_pan`) | delay pan | wet pan | wet pan | `pos` position: slice ◄ follow ► spray | width |
| `ton` (`crump_tone`) | return tone | damping | return tone | `sze` grain size, reverse ◄ 63 ► forward | damping |
| `aux` (`crump_aux`, row 15) | tone drive (later) | tone drive (later) | interval ±24 st | `st1` root ±24 st | — |
| rows 16–21 | — | — | — | `st2` `st3` (±24 st from root), `spr` spread/detune, `den` density, `amp` level jitter, `wid` random pan | — |
| page names (S2) | `ton fbk rte snc dpn` | `ton fbk not pol dpn` | `ton fbk win snp dpn int` | `sze fbk div frz pos`; SELECT 3: `st1 st2 st3 spr`, `den amp wid` | `dmp dcy siz frz wid` |

- **Shared rows are the same for every sub-type:**
  - `bof biv`;
  - **`mix`** (the mix stage; its page cell stays where it is, s2);
  - the output filter `typ frq res`;
  - the common `out vol pan`.
- **The deferred drives:**
  - `drv` (later row 22) is shared.
  - `tdr` becomes the `aux` row's meaning in `dly` and `cmb`.
  - `pit` (interval) and `grn` (`st1`) use `aux` for pitch, so they have
    no tone-drive control. `rvb` leaves `aux` unused.

---

## 7. Phasing and verification

| Step | Content | Proof |
|---|---|---|
| **P3.0** | runtime union and prefix; `subtype` byte; per-block dispatch with stage 1 as an inline helper; `sub` max 4 with labels; `crumpBit_enter<Sub>()`; `effective_max` with the runtime; `effects_requestFullResolve()`; `name3` and `layout()` hooks; row 15 `aux` and the three ownership masks; `sub` Pattern-automatable with the reinterpreting `enter` path; **the shared mix stage** (`crumpBit_mixFrame()`, the `mix` ramp, `mix` in `CRUMPBIT_ROWS_SHARED`, lane 3 fixed); the per-sub-type lane-map op and its six EffectsManager readers plus the `.fx` lane-key path; value carry-over on a `sub` change (nothing cleared) | **`dly` bit-identical to S091 P2** (host CB4 unchanged; the mix moved into its own stage computes the same expression); mix law `mix` 0 = stage-1 output exactly; a switch-storm law (a random sub-type every block for 60 s: every index inside the share, no NaN or Inf, output bounded) and the switching block's cost measured; the `dly` lane map equals today's table, so `.fx` files round-trip byte-identically; `crumpBit_runDelay` loop ≤ 126; mixer loops unchanged; 0 B RAM (the runtime grows to 92 B only with P3.3) |
| **P3.1** | `cmb` | host: tuning table, polarity, stability at 1.2, `dly`↔`cmb` prefix adoption; ELF loop ≤ 132; hardware: plucks, drones, LFO flanging |
| **P3.2** | `pit` | host: the ratio law (a sine in → its frequency × r out, via FFT peak), window sum = 1, stability with feedback; ELF ≤ 132 |
| **P3.3** | `grn` | host: every grain read inside the record window and `valid` for every mode, size, pitch and direction (the headroom rule, never overtaking the record head); the spread distribution (0 → all `st1`, 40 → ½/½, 80 → thirds; detune bounded by ±*d*); a grain's pitch equals its slot's ratio within 0.5 cent (16.16 step, Q3.12 increment); freeze leaves the record line bit-exact while the delay line runs; windows sum to 1; feedback stability at 1.2. ELF ≤ 135 (the closest sub-type to the limit: prototype first). Link map: `_sfxbuf` moved by exactly 32 B, FXBUF 126,656 B, runtime 92 B |
| **P3.4** | `rvb` | host: Hadamard orthogonality, RT60 decay within ±20 %, gate keeps stale bytes out, packed-lane round trip; ELF ≤ 132 |

**For every step:**
- a prototype count *before* the schedule (scratch copy, linked ELF), as
  in S091 P1/P2;
- the schedule written from the verified build;
- the `cpu` widget on the worst-case Scene with the new sub-type at its
  heaviest settings, at or below the `dly` reading.

---

## 8. Decisions for the user

| # | Question | Recommendation |
|---|---|---|
| S1 | Which sub-types, and in which order | **comb, pitch, reverb accepted (user); granular as redesigned (§3.3).** Order `cmb` → `pit` → `grn` → `rvb` |
| S2 | Per-sub-type names: a page-only `name3` hook, or neutral generic labels for the six rows | **accepted (user, 2026-11-04):** the `name3` hook (Effect page only; other screens keep delay names) |
| S3 | Buffer on a sub-type switch | **revised (user: glitches encouraged):** every switch reinterprets the arena in the new format, with no clearing and no gating; only the DSP state floats are re-initialised and the prefix is converted and clamped (§2.4) |
| S4 | `rvb` lanes 8-bit (crunchy, gated tails) or 16-bit (clean tails, ≈ +6 per frame) | **accepted (user, 2026-11-04):** 8-bit, the CrumpBit character; revisit after listening |
| S5 | `grn` grains: 2 | **accepted (user, 2026-11-04):** 2; a third would cost ≈ +17 per frame and 6 B state: over both limits |
| G1 | `grn` time always tempo-synced, so freeze can take `snc` | **accepted (user, 2026-11-04):** yes (no free row otherwise; the drive lanes stay reserved) |
| G2 | Split 50/50 (record = delay = 1.436 s max), or a longer record share | **accepted (user, 2026-11-04):** 50/50, as asked ("same length") |
| G3 | `grn` and an earlier `dly` recording | **resolved by S3:** a switch reinterprets the buffer, so `grn` granulates the first half of the `dly` recording (glitchy, accepted) |
| G4 | Grain selection: M1/M2/M3 on `dpn` (follow / spray / slice) plus M8 via `dpn` lanes now; M4 scan, M5 anchor, M6 repeat, M7 recent-weighted later | **accepted (user, 2026-11-04):** yes |
| G5 | `ton` = `sze`, bipolar: right forward, left reversed, distance = size | **decided (user, 2026-11-04)** |
| G6 | Pitched grains **without interpolation** (16.16 positions, drop-sample), tape glide kept | **decided (user, 2026-11-04)** |
| G7 | Pitch set `st1` (root, row 15 `aux`), `st2`, `st3` (relative to the root, ±24 st), `spr` spread (fan-out to 80, detune above); within the approved RAM | **accepted (user, 2026-11-04):** yes (the user's design) |
| G11 | Page labels in `grn`: `sze` (ton), `pos` (dpn), `frz` (snc) | **decided (user, 2026-11-04)** |
| M1 | `mix` is the shared mix stage, not part of any sub-engine. It is always the signal without the sub-engine ↔ the sub-engine's output, exists for every sub-type, never disappears, keeps lane 3 everywhere, and is never freeable | **decided (user, 2026-11-04)** (§2.8) |
| L1 | **Accepted (user).** Lanes: nine shared (`mrp bof biv mix vol pan typ frq res`; lane 3 `mix` fixed, never freeable) plus a seven-lane pool (4, 5, 6, 9, 10, 14, 15) assigned per sub-type through a lane-map op | yes (the user asked for reassignable lanes) |
| L2 | **Accepted (user).** `grn` pool assignment: `st2` 4, `st3` 5, `frz` 6, `pos` 9, `sze` 10, `st1` 14, `spr` 15 (so `fbk` and `div` are not lane-sequenced in `grn`) | as accepted |
| L3 | The deferred `drv` (shared): lane-less, or lane 15 everywhere (then `grn` `spr` loses its lane) | **accepted (user, 2026-11-04):** lane-less |
| L4 | Lane data on a sub-type change | **decided (user, 2026-11-04):** nothing is cleared, from any source; lanes keep their values and drive the new map's rows |
| G8 | Maximum detune at `spr` 127 | **accepted (user, 2026-11-04):** ±35 cents (±50 for deliberately out-of-tune) |
| G9 | Randomness: one hardware RNG word per block, expanded by an integer hash (back-to-back hardware reads repeat the same word) | **accepted (user, 2026-11-04):** yes |
| G10 | Further grain rows `den`, `amp`, `wid` | **approved (user, 2026-11-04)** with up to 32 B of RAM; added as rows 19–21; CPU priority in N2 |
| S12 | A per-sub-type `layout()` hook, so `grn`'s pitch rows sit on SELECT 3 screens only in `grn` | **accepted (user, 2026-11-04):** yes |
| S6 | `pit` interval on `aux` (row 15, lane 14, aligned with `grn`'s root pitch), `dpn` = wet pan, snap on `snc` | **accepted (user, 2026-11-04):** yes (revised for alignment) |
| A1 | Sub-type changes keep every row and lane value; the rows are aligned by role (§6): `rte` higher = longer everywhere (`cmb` lower note), `aux` = pitch where there is one | **decided (user, 2026-11-04)** |
| S7 | `sub` automation | **decided (user, 2026-11-04):** Pattern-automatable (no Morph, no LFO); switches land per block |
| S8 | `effective_max` takes the runtime; `effects_requestFullResolve()` on a sub-type change | **accepted (user, 2026-11-04):** yes (needed for a correct per-sub-type clamp) |
| S9 | `cmb` tuning: continuous (~16 steps/octave, nearest-note label) or semitone steps over 0..96 with 97..127 clamped | **accepted (user, 2026-11-04):** semitone steps (musical; 8 octaves) |
| S10 | Budget rule: each sub-type loop ≤ `dly` + 5 %, and the whole Effect < pre-S091 ≈ 191 | **accepted (user, 2026-11-04):** yes, with one exception: `grn` (pitched grains, user design) may reach `dly` + 10 % (≤ 139) as long as the whole Effect stays < 191, measured on the board |
| S11 | Candidates (§3.5): add `rvs` and/or `stt` later | **accepted (user, 2026-11-04):** after P3.4, by ear |

---

## 9. Risks and notes

- **Register pressure** is the main cost risk, as P2 showed. Engine B has
  two taps plus windows plus the return chain, so `pit` is the tightest.
  Prototype it first if `pit` is a priority.
- **Row reuse means one Scene byte has a different meaning per sub-type.**
  Morph between Scenes of different sub-types interpolates raw bytes, as
  it does today. Since `sub` itself is not Morphable, a Morph never mixes
  two sub-types.
- **Values on a sub-type change:** nothing is cleared (L4, A1).
  - Rows, lanes and Pattern data keep playing, and the active sub-type
    reads them with its own meaning. The aligned lineup (§6) keeps most
    hand-offs musical. The rest are accepted glitches.
  - Shared lanes, `mix` included, carry the same meaning everywhere.
- **A switch block costs more than a steady block** (`enter` plus the
  `rte` re-clamp). With Pattern automation a switch can happen on every
  step, so the switching block, not only the steady state, must stay
  under the budget (§4). That is measured in P3.0.
- **8-bit everywhere:** pitch-shifted, granular and reverb tails all carry
  the 8-bit floor. That is the type's identity. `rvb` is where it is most
  audible (S4).
- **The hardware RNG** (`grn`) is read once per block. Back-to-back reads
  return the same word, because `RNG_DR` refreshes every ≈ 833 ns and
  `GetRngValue()` does not wait. The grain decisions therefore expand one
  word with an integer hash (G9). S073's rejection of a software PRNG was
  about the *audio* noise character; these are control decisions.
- **`grn` is the heaviest sub-type** (≈ 107–110 in-loop against `dly`'s
  ≈ 100, ≈ 179–187 for the whole Effect against pre-S091 ≈ 191). It sets
  the worst-case Scene, so prototype it before its schedule. The §4
  fallbacks are ordered by how little they take away.
- **Drop-sample pitch** aliases when pitched up and steps when pitched
  down: deliberate sampler grit (G6). The output filter's LP is the
  remedy where it is too much.
- **Constant CPU across sub-types** is per sub-type, not across them. The
  worst-case Scene is set by the heaviest sub-type, which is why S10 bounds
  each one, not the mix.
- **The S091 P2 hardware results come first.** If P2 reads high on the
  board, P3's envelope shrinks with it. The fallbacks of §4 and P2 §7 are
  the levers.

---

## 10. Open questions

**Resolved (user, 2026-11-04):**

| # | Resolution |
|---|---|
| OQ1 | The listed recommendations are accepted (§8). |
| OQ2 | `sub` is never on an FX-sequencer lane. |
| OQ3 | Pattern values clamp to 0..4 (higher = `rvb`); the sub-types are not spread. |
| OQ4 | The accepted lane map stands; hand-offs onto `grn`'s pitch lanes are accepted glitches. |
| OQ5 | Sub-type names on the STEP automation page and in Pattern entry (P3.0). |
| OQ6 | Still deferred with the drives (the P2 plan keeps its 15/16 numbering until then). |
| OQ7 | Approved, together with up to 32 B of RAM for anything that helps; rows 19–21 added (§3.3.5, §5). |

**Still open:**

| # | Question | Recommendation |
|---|---|---|
| N1 | **The Rate curve after the arena shrinks.** The 92 B runtime moves `_sfxbuf` up by 32 B (FXBUF 126,688 → 126,656 B). Either retune `CRUMPBIT_LOOP_MAX_BYTES` and the curve top to the new arena (regenerate `rateTable`; every `rte` plays 0.025 % shorter), or keep the constants, in which case `rte` 127 no longer fits the whole arena and clamps to 126 (2.76 s instead of 2.87 s) | retune (inaudible; `rte` 127 keeps meaning "the whole arena") |
| N2 | **`grn` CPU with rows 19–21.** The core `grn` is ≈ 179–187 per frame for the whole Effect. With `den amp wid` it is ≈ 187–195, at or over the pre-S091 ≈ 191 that the binding rule allows. RAM is approved, but CPU decides what ships | prototype `grn` with all three, then keep them in the order **`den` → `amp` → `wid`** while the linked whole-Effect count stays < 191 and the board's `cpu` reading stays at or below pre-S091. A row whose DSP is dropped is not added |

**Checked and consistent (review 2026-11-04):**
- **The mix stage** is shared, after every sub-engine, on lane 3 in every
  map, and never freeable (§2.8).
- **Sub-type switches** (menu, MIDI, Scene or Pattern) reinterpret the
  buffer and keep every value. Nothing is cleared, and the lineup is
  aligned (§2.4, §6).
- **The lane maps** keep `dly` byte-identical to today, so `.fx` files
  round-trip.
- **RAM:**
  - the shared part is 56 B and the union 36 B, 92 B in total (+16 B of
    the 32 B);
  - the arena loses one 32 B granule (N1);
  - every sub-type state is ≤ 36 B;
  - row and lane counts agree across §2.2, §2.3, §2.9, §3.3.5, §5 and §6.
- **Budget:** `dly`, `cmb`, `pit`, `rvb` and core `grn` are below ≈ 191 by
  estimate. `grn` with rows 19–21 is the one open case (N2). The switching
  block is measured in P3.0.

---

## 11. Acceptance (user, 2026-11-04)

- **Implemented** by `S091_P3_Implementation.md`.
  - The repository code is byte-identical to the pre-verified schedule,
    plus one warning fix in `StereoFilterEffect.c` (that document, §9–§10).
  - Static gates: CB1–CB15, nine breaks, per-engine ELF counts, mixer and
    oscillator loops unchanged, image 707,020 B.
- **Hardware: accepted.**
  - The five sub-types (`dly cmb pit grn rvb`) work on the board.
  - None exceeds the previous worst case. The linked whole-Effect worst
    cases per frame are `dly` 172, `cmb` 168, `pit` 181, `grn` 192,
    `rvb` 192, against pre-S091 ≈ 191 and the accepted ≈ 195.
  - S091 P2 is accepted with it: `dly` is bit-exact to P2 on the host.
- **Differences from this plan, as built:**
  - **No delay glide in `grn`** (user): the delay tap reads exactly D = R.
    The runtime is therefore 88 B (+12 B), not 92 B.
  - **The arena granule did not move** in the built image (FXBUF still
    126,688 B at `0x20001120`). The N1 constants are correct either way.
  - **`rte` is re-clamped every block** (BUFFER_DEPENDENT re-apply), not
    through a new manager call.
  - **One `grn` restart candidate per block,** with the frame loop split at
    the block's single restart.
- **Deferred to a separate task (user):** how the parameters appear
  (names, value text, layouts) and how individual parameters are handled.
  For `grn` the starting point is `S091_P4_GRN_TWEAKS.md`. The output-filter
  and tone drives stay deferred (OQ6).
