# S091 P3 — CrumpBit Sub-Types: Implementation Schedule

- **Plan:** `S091_P3_CrumpBit_SubType_Expansions.md` (all decisions of its
  §8 and §10, including N1 and N2, accepted by the user on 2026-11-04).
- **Baseline:** HEAD `080b1507` on `dev-ph7-voice_expansion` (the S091 P2
  CrumpBit expansion, not yet tested on hardware). Every **HEAD line** below
  refers to that tree.
- **Scope:** everything the plan schedules for P3.0–P3.4:
  - the shared architecture: four stages (shared dry path, one sub-engine
    per block, the shared MIX stage, the shared output filter); the 88 B
    runtime with a 32 B sub-type union; the `sub` row (Pattern-automatable,
    clamped to 0..4); switching by reinterpretation (nothing cleared);
    per-sub-type names, values and layouts; the sub-type-aware share clamp;
    per-sub-type FX lane maps (lane 3 `mix` fixed everywhere);
  - the four new sub-engines `cmb`, `pit`, `grn` (with `den`, `amp`, `wid`)
    and `rvb`;
  - the host laws CB1–CB15 with nine break builds, and per-engine ARM gates.
  - Drives stay deferred (OQ6); `aux` is reserved for the tone drive in
    `dly`/`cmb`.
- **Pre-verified (2026-11-04):** every code block below was applied to a
  copy of HEAD in the session scratchpad (`git archive HEAD`). That copy was:
  - built with the firmware flags (`make all`, `make img`);
  - measured on the linked ELF (§2);
  - tested on the host (`make -C tools/dsp_test crumpbit`: CB1–CB15, 0
    failures, nine break builds caught).

  The blocks were copied from the built files by script: a `difflib` map of
  each unit onto the built file, and a check that applying every unit to
  HEAD, bottom-up, rebuilds each built file byte for byte. Nothing was
  retyped.
- **Status:** **implemented** on 2026-11-04 (HEAD `11397377`); static gates
  pass, hardware H1–H9 pending. The running log is §9.
- **Constraints:**
  - CPU: the user accepted **≈ 195 per frame for the whole Effect**
    (2026-11-04). Every sub-type measures ≤ ≈ 192 worst case (§2).
  - RAM: **+12 B** `.dtcmz` (runtime 76 → 88 B), inside the user's +32 B
    approval.
  - No backward compatibility: `.fx`/AutoSave carry the new rows through
    the registry. Old files load: missing rows take their defaults, and
    `sub` 0 is `dly`.

---

## 0. Decisions applied, and implementation choices

| # | Decision | Applied as |
|---|---|---|
| M1 | `mix` is the shared MIX stage: always the signal without the sub-engine ↔ the sub-engine's wet, every sub-type, lane 3, never freeable | `CRUMPBIT_ROWS_SHARED` (P2); `crumpBit_mixFrame()` / `crumpBit_mixFrameGain()` called by every engine as its last statement per channel (E10); the ramp origin `mix_prev` in `crumpBit_process()` (E10); lane 3 in every lane map (E10, CB9); law CB10 + break 6 |
| S3, L4, A1 | A switch from any source reinterprets the arena (no clear, no gate); every row and lane value carries over; rows are aligned by role | `crumpBit_enter()` (E10) via `crumpBit_bytePosition()` (E7); `write_param` stores every row whatever the sub-type (E9); law CB11 |
| S7, OQ2, OQ3 | `sub` Pattern-automatable, never an FX lane, values above 4 clamp to `rvb`, ids not spread | descriptor flags/max (C5), `write_param` clamp (E9), no lane map names `sub` (E10, CB9) |
| S2, S12, OQ5 | `name3` hook, per-sub-type `layout()` hook, sub-type names on automation pages | `EffectsManager.h` hooks (MH1), `menuEffects_layout()` (U1–U6), `crumpBit_uiName3/uiLayout/uiFormatTarget3` (C8, C9), `effects_formatTargetValue3()` (M5, N1–N3) |
| S8 | `effective_max` takes the runtime | `effect_type_ops_t.effective_max(rt, …)` (MH3), callers (M4, M16), `crumpBit_effectiveMax()` (E10) |
| L1, L2, L3 | Nine shared lanes + a seven-lane pool per sub-type; `grn` pool `st2 st3 frz pos sze st1 spr`; `drv` lane-less | `crumpBit_laneMaps` (E10), `lane_map` op (MH3), `effects_laneTable()` (M6) and every lane reader (M6–M16, S1–S3, N4, U7–U9) |
| S9 | `cmb` in semitone steps over 0..96 | `crumpBit_combTable` (G4), `crumpBit_combNote()` (E4) |
| S6 | `pit` interval on `aux`, snapped on `snc`, `dpn` = wet balance | `crumpBit_runPitch()` (E10) |
| S4 | `rvb` 8-bit lanes | `crumpBit_runReverb()` (E10) |
| S5, G1–G11 | `grn` design: 2 grains, synced R, 50/50 lines, freeze on `snc`, `sze` bipolar, drop-sample pitched grains, pitch set + spread, ±35 cent detune, one hardware RNG word per block | `crumpBit_grainPick()`, `crumpBit_grainSpan()`, `crumpBit_runGrain()` (E10); law CB14 + break 7 |
| G10, N2 | `den`, `amp`, `wid` (rows 19–21), CPU decides | all three kept: their per-frame cost is zero (they shape the per-block candidate and the window step), and `grn` measures ≈ 192 |
| N1 | Retune the Rate curve to the 126,656 B arena | `CRUMPBIT_DELAY_CURVE_MAX` 126,654, `CRUMPBIT_LOOP_MAX_BYTES` 126,656 (H1); `rateTable` regenerated (G3) |
| OQ7 | RAM up to +32 B approved | +12 B used |

**User change during this work (2026-11-04):** "I don't care about [the
tape glide] for the granular engine." The `grn` delay line reads exactly
D = R behind the write head, with no glide and no interpolation. A change
of R jumps. This removes the plan's `delay` float from `CrumpBitGrainState`
(union 36 → 32 B, runtime 92 → 88 B) and 15 instructions per frame.

**Implementation choices made here.** These are not plan decisions; each
can be changed before implementing:

1. **BUFFER_DEPENDENT rows are re-applied every block** (M16), instead of
   the plan's new `effects_requestBufferResolve()` call. The `rte` clamp
   then follows a sub-type switch or a share change one block later. It
   adds no manager state, so the 80 B `effects_state_t` guard is untouched,
   and costs one `crumpBit_tableLimit()` (63 instructions) per block.
2. **The lane maps live in `CrumpBitEffect.c`** beside the ops table
   (E10), so the host test checks them directly (CB9). The registry row
   keeps the `dly` map, and the self-check holds the two equal (code 11,
   M12).
3. **`dly` and `cmb` leave lane 14 empty** until the tone drive lands on
   `aux`. `dly` therefore stays byte-identical to the S091 P2 table, and
   `.fx` files round-trip unchanged.
4. **The `pit` interval is continuous** (24/63 st per step) and snaps to
   semitones on `snc`. The `grn` pitches (`st1` = `aux`, `st2`, `st3`) are
   whole semitones.
5. **Each sub-engine is its own out-of-line symbol** (`CRUMPBIT_NOINLINE`),
   called once per block from a `switch`. The ELF gate can measure each
   frame loop, and `crumpBit_process()` keeps only the shared work and
   stage 4.
6. **Fixed-point reads:**
   - `pit` keeps its head phase as an unsigned 0.32 word that wraps by
     overflow, and reads both heads in Q14 integer arithmetic.
   - `rvb` converts its lanes with the scaling `VCVT #7` and packs four ADC
     codes with one XOR.
   - Together these took `pit` 157 → 135 and `rvb` 162 → 145.
7. **`grn` restarts:**
   - A block can hold at most one grain restart: a grain is ≥ 64 frames,
     restarts are half a grain apart, and a block is ≤ 32 frames
     (`OUTPUT_DMA_SIZE`).
   - The frame loop therefore runs as two spans split at that frame. The
     restarting grain takes ONE per-block candidate between them; the plan
     drew two candidates and selected per frame.
   - Freeze redirects the record write to the delay line's slot, which the
     same frame overwrites, so freezing costs no per-frame select.
   - Together these took `grn` 179 → 131.
8. **The mix stage has two spellings** of the same law:
   - `crumpBit_mixFrame(h, wet, mix)` and `crumpBit_mixFrameGain(h, g, sig,
     mix)` = `h + mix·(g·sig − h)`.
   - The second keeps the S091 P2 expression shape, so `dly` stays
     bit-exact against the P2 reference (CB4). Passed as an argument,
     `g·sig` would round on its own before the FMA.
9. **The `grn` headroom rule uses the played Q3.12 ratio**, not the float
   ratio it was rounded from (E10, `crumpBit_grainPick()`). At G ≈ 63k
   frames the difference is ~8 samples, enough to overtake the record head
   (found by CB14).
10. **The arena granule is decided by LTO, not by the runtime size alone.**
    - The +12 B runtime fits in `.dtcmz` padding in the measured build:
      `_sfxbuf` stays at `0x20001120`, FXBUF 126,688 B.
    - A different LTO packing can still cross the 32 B boundary.
    - The N1 constants (126,656 / 126,654) are correct either way: on a
      126,688 B arena they leave 32 B unused.
    - The link gate therefore checks "FXBUF ≥ 126,656 B", not the plan's
      "moved by exactly 32 B" (§5, G7).

---

## 1. Change list

| Units | File | What |
|---|---|---|
| H1–H2 | `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | arena constants (N1); the four sub-engine states; the 88 B runtime (56 B shared + 32 B union); sub-type helper and lane-map API |
| E1–E10 | `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | stage header; P3 constants and size guards; generalised table-limit search and per-sub-type limits; seat on the union prefix; init seeds; byte position; handoff per family; `write_param` for rows 8 and 15–21; the shared stage helpers, five engines, seat/enter, the new `process()`, sub-type-aware `effective_max`, lane maps and ops (E10) |
| P1–P3 | `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` | 22 rows; rows 15–21; sub-type ids; three ownership masks |
| C1–C10 | `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | rows 8 and 15–21; three-owner guards; per-sub-type value/name/target text; `pit`/`grn` layouts and the hooks |
| B1–B2, R1 | `CrumpBitTables.h`, generated `CrumpBitTables.c` | six new tables (comb, pitch window, semitone ratio, grain fraction, reverb size, reverb decay) |
| MH1–MH5, M1–M16 | `Core/DSP/Effects/EffectsManager.h/.c` | `name3`/`layout`/`format_target3` hooks; `effective_max(rt, …)`, `lane_map` op; record-based lane API and `effects_laneTable()`; 88 B guard; self-check per variant; BUFFER_DEPENDENT re-apply |
| S1–S3 | `Core/Hardware/SD/storageTypes.c` | `.fx` lane keys through the record's lane table |
| N1–N4 | `Core/Menu/menu.c` | `sub` names on automation pages; lane lookup by record |
| U1–U9 | `Core/Menu/menuEffects.c` | `menuEffects_layout()`; `name3`; lane lookup by record |
| G1–G4 | `tools/plots/gen_crumpbit_tables.py` | N1 Rate top; the six new tables and their checks |
| L1–L3 | `tools/dsp_test/elf_looppath.py` | `--cond-heads` |
| T1–T6, X1–X12 | `tools/dsp_test/Makefile`, `tools/dsp_test/modules/crumpbit.c` | extraction of the new macros; laws CB4–CB15; breaks 4–9; per-engine ARM gates |
| D1–D6 | documentation | §6 |

**Checked and unchanged:**
- `Makefile`: `CrumpBitTables.c` is already in `SRCS`.
- `EffectParamRows.h`.
- `FxBuffer.c/.h`, `mixer.c` (the mixer loops are unchanged, G6), the
  StereoFilter type, `ResonantFilter.c`.
- The `.fx` writer order: `[params]` precedes `[sequence]`, which S1
  relies on.

---

## 2. Measured results (scratch build of HEAD + this schedule)

All counts are instructions on the linked `-Ofast` ELF, longest path, no
VDIV in any frame loop. "Per block" is the longest straight path through the
function with its frame loops weighted 0, so every branch is taken the
expensive way (a pessimistic bound). Whole-Effect figures add the engine
loop, the stage-4 loop (31), and per block (÷ 32 frames):
- the engine's per-block code;
- `crumpBit_process()` per block, 194, including the switching block's
  `crumpBit_enter()`;
- one `crumpBit_tableLimit()`, 63 (`effects_service()` re-applies `rte`);
- for `dly` (Sync on) and `grn`, one `crumpBit_divisionFor()`, 113.

| Sub-type | Frame loop (stages 1–3) | Per block (engine) | **Whole Effect, worst, per frame** | Plan S10 loop rule (≤ `dly` + 5 %, `grn` + 10 %) |
|---|---:|---:|---:|---|
| HEAD `dly` (S091 P2) | 126 | 306 (all of `process`) + 113 + 101 (`rateLimit`) | **173.3** | — |
| `dly` | **124** | 166 + 113 | **171.8** | the base |
| `cmb` | **124** | 154 | **167.8** | within |
| `pit` | **135** | 228 | **181.2** | +8.9 % (over 5 %) |
| `grn` | **131** (span 2) / 125 (span 1) | 575 + 113 | **191.5** | +5.6 % (within 10 %) |
| `rvb` | **145** | 269 | **192.4** | +16.9 % (over 5 %) |

Every sub-type is at or below the user's ≈ 195 (2026-11-04), and `dly` is
1.5 below HEAD.
- `pit` and `rvb` exceed the plan's per-loop S10 rule, which the later
  whole-Effect acceptance replaced. §7 lists their reductions if the board
  wants them.
- The board's `cpu` reading decides (H1).

**How the engines got there** (first prototype → schedule):
- `pit` 166 → 135: integer 0.32 phase, Q14 integer head reads, one
  scale/gate factor.
- `rvb` 162 → 145: `VCVT #7` lane conversion, the ADC's ×128 and +128.5
  folded into the gains, one XOR for the four lanes, folded mid/side
  constants.
- `grn` 182 → 131:
  - two spans around the block's single restart, with one candidate per
    block;
  - redirected freeze write;
  - window phases instead of age arithmetic;
  - the gate folded into the gains;
  - no delay glide (user).
- All engines: the block fields copied to locals before the loop. A float
  output store may alias any float reached through the block pointer, which
  forced per-frame reloads.

| Item | HEAD (S091 P2) | After | Note |
|---|---:|---:|---|
| `crumpBit_process` stage-4 loop (output filter) | 31 | **31** | unchanged |
| `crumpBit_divisionFor` | 113 | **113** | unchanged |
| Rate/table limit per block | `rateLimit` 101 | **`tableLimit` 63** | out of line (`CRUMPBIT_NOINLINE`): inlined five times it cost 2.3 KB |
| Sizes: `process` / `runDelay` / `runComb` / `runPitch` / `runGrain` / `runReverb` | 1,924 / — | **1,128 / 1,192 / 1,138 / 1,408 / 3,196 / 1,560 B** | |
| New tables | — | comb, pitch window, reverb size, reverb decay 512 B each; semitone ratio 196 B; grain fraction 260 B; lane maps 80 B; two layouts 146 B each | |
| `crumpBit_uiFormatValue3` / descriptors | 244 / 540 B | 660 / 792 B | |
| `.text` / `.data` / `.bss` | 693,736 / 420 / 413,028 | **706,600** / 420 / **413,040** | +12,864 B text, +12 B bss |
| Flash used (`link_budget`) | 694,156 | **707,020** (headroom 46,644) | |
| ITCM | 15,256 | **15,280** | +24 B: LTO setup code in the oscillator blend blocks; every ITCM loop unchanged (G9) |
| DTCM statics / FXBUF | 4,372 / 126,688 B at `0x20001120` | **4,384** / **126,688 B at `0x20001120`** | +12 B in padding; the granule did not move in this build (choice 10) |
| `CrumpBitRuntime` / `effects_runtime_t` | 76 / 76 B | **88 / 88 B** | 56 B shared + 32 B union |
| `mixer_calcNextSampleBlock` loops | — | **39 hot loops equal; 86/86 normalised dumps identical** | G6 |
| `calcNextOscSampleSyncBlock` loops | 11/9, 9/9 | **11/9, 9/9** | G9; the five `osc_wtb*Block` loop paths are also identical |
| Image (`make img`) | 694,156 B | **707,020 B** | `Written … OK` |
| Host `crumpbit` | CB1–CB9, 5 breaks | **CB1–CB15: 0 failures; 9 breaks caught** | CB8 peaks 1.000, mean loop code 0.061 / −0.002 / −0.001 LSB; CB15 decay 15.60 dB (law 15.2), freeze 1.98 dB/s |

**Not P3:** in `make -C tools/dsp_test all`, the `voice_control`
second build reports 6 failures (laws 1 and 4), and `run_modes` does not
compile (`distortion_curveSampleF` undeclared). Clean HEAD gives the same
result, so both are outside this schedule.

---

## 3. Firmware changes

Each unit gives its HEAD location (or the HEAD line it is inserted after),
the new-file lines, a description (what, why, inputs and outputs,
accessors, affiliated code), and the exact code. The comment blocks inside
the code are part of the change: they document variables, mathematical
statements and loops in place. Large units are shown as consecutive
sub-blocks. Concatenated in order, the sub-blocks are exactly the unit's
new text.

### 3.1 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`

#### H1 — Arena constants (N1), the four sub-engine states, the 88 B runtime

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, HEAD lines 19–120: **replace** (102 lines → 197; new lines 19–215).

- **What:**
  - **Arena constants:** retunes the arena constants to the arena less one
    32 B granule (N1): `CRUMPBIT_DELAY_CURVE_MAX` 126,654,
    `CRUMPBIT_LOOP_MAX_BYTES` 126,656, `CRUMPBIT_LOOP_MIN_SHARE_BYTES`
    derived (73,664).
  - **The S091 P2 runtime** becomes a shared part plus a union of four
    sub-engine states:
    - `CrumpBitTapState` (`dly`, `cmb`, 28 B): the P2 delay fields moved
      unchanged;
    - `CrumpBitPitchState` (`pit`, 28 B): the same `length/write_pos/valid`
      prefix, a 0.32 head phase, the return memories;
    - `CrumpBitGrainState` (`grn`, 32 B, the largest): 16-bit line fields,
      age, two 16.16 positions, two Q3.12 steps, the delay line's DC
      blocker, four level/pan bytes;
    - `CrumpBitReverbState` (`rvb`, 28 B): word position/length/valid and
      four damping states.
  - **The shared part (56 B):**
    - stage-1 AC memories and stage-4 SVF states;
    - every row byte, rows 3–21: their storage is fixed, their meaning
      follows the sub-type;
    - `subtype` (resolved) and `active_subtype` (what ran last block);
    - the ramp origins, `primed`, `drv_reserved` (the deferred drive) and a
      pad byte.
- **Why:**
  - Plan §2.2: one 32 B union holds whichever engine runs.
  - The single-line engines share an identical prefix, so a switch between
    them keeps the recording without conversion (static assert, E2).
  - 88 B is +12 B over 76 B, within the user's +32 B.
- **Inputs/outputs:** none (types and constants).
- **Accessors:** `CrumpBitEffect.c` only; `EffectsManager.c` sizes the
  union from it (M1, M3).
- **Affiliates:** `tools/plots/gen_crumpbit_tables.py` `RATE_MAX` (G3),
  `link_budget.py` (FXBUF), the host test's state checks (CB4, CB11).
- **Note:** the comment records that the granule loss depends on LTO
  packing (choice 10).

Remove HEAD lines 19–120 (102 lines), from
`* P2 §5.1).`
through
`uint8_t primed;            /* delay */`.

Replace with:

```c
 * P2 §5.1; retuned in S091 P3 N1).
 *
 * CRUMPBIT_DELAY_CURVE_MAX: the top of the Rate curve, 126,654 samples
 * (2.871 s at 44,108 Hz) = crumpBit_rateTable[127]: the arena less one
 * 32 B granule and the 2-sample interpolation guard. S091 P3 grew the
 * runtime by 16 B; whether `.dtcmz` then crosses a 32 B boundary (moving
 * `_sfxbuf` up one granule, 126,688 -> 126,656 B) depends on how LTO packs
 * the DTCM statics (the measured S091 P3 build absorbed it in padding). The
 * curve top is set to the smaller arena, so `rte` 127 fits either way (N1,
 * user); a 126,688 B arena leaves 32 B unused.
 * CRUMPBIT_LOOP_MAX_BYTES: the longest loop CrumpBit seats, the curve top plus
 * the guard (126,656 B, a multiple of 32). A
 * larger share is not used beyond it; a smaller share (voice units claimed
 * from the top) shortens the loop, and the Rate row and the DSP target are
 * clamped to what it holds (the loop is elastic).
 * CRUMPBIT_LOOP_MIN_SHARE_BYTES: the share left at this arena size with all
 * twelve voice units claimed (73,664 B): declared as buffer_min_bytes.
 * CrumpBit still works below it (down to CRUMPBIT_LOOP_MIN samples); the
 * field is declarative.
 * Accessors: CrumpBitEffect.c, EffectsManager.c (registry row).
 * Affiliates: FxBuffer.h FXBUF_VOICE_UNIT_*, tools/plots/gen_crumpbit_tables.py
 * (RATE_MAX), tools/diagnostic_tools/link_budget.py (FXBUF size).
 */
#define CRUMPBIT_DELAY_CURVE_MAX       126654.0f
#define CRUMPBIT_LOOP_MAX_BYTES        126656u
#define CRUMPBIT_LOOP_MIN_SHARE_BYTES                                   \
    (CRUMPBIT_LOOP_MAX_BYTES -                                          \
     FXBUF_VOICE_UNIT_COUNT * FXBUF_VOICE_UNIT_BYTES)

/*
 * Single-line tape state: `dly` and `cmb` (engine A; S091 P3 §2.2).
 *
 * What: the S091 P2 delay state, now one member of the sub-type union.
 * - length, write_pos, valid: the COMMON PREFIX of every single-line
 *   sub-type (`dly`, `cmb`, `pit`): loop length in bytes (0 = unseated), the
 *   next index to write, and the samples written since the loop was last
 *   treated as empty (reads further back are muted). It sits at offset 0 in
 *   each of them, so a switch between them keeps the recording (§2.4);
 * - delay: the gliding delay in samples (tape glide; never above
 *   length - 2);
 * - dc_x, dc_y: the in-loop DC blocker memories (feedback can exceed 1);
 * - tone_s: the one-pole state of the `ton` tilt on the feedback return.
 * Size: 28 B. Accessors: CrumpBitEffect.c only.
 */
typedef struct {
    uint32_t length;
    uint32_t write_pos;
    uint32_t valid;
    float delay;
    float dc_x;
    float dc_y;
    float tone_s;
} CrumpBitTapState;

/*
 * Pitch-shifter state: `pit` (engine B; S091 P3 §3.2).
 *
 * What: the common prefix (length, write_pos, valid, as CrumpBitTapState),
 * then phase: the rotating-head phase, an unsigned 0.32 fraction of a turn
 * that wraps by integer overflow (head A trails the write head by
 * 2 + W*phase, head B by 2 + W*(phase + 1/2), the half turn being one
 * 32-bit add), and the same feedback-return memories as the tape (dc_x,
 * dc_y, tone_s).
 * Size: 28 B. Accessors: CrumpBitEffect.c only.
 */
typedef struct {
    uint32_t length;
    uint32_t write_pos;
    uint32_t valid;
    uint32_t phase;
    float dc_x;
    float dc_y;
    float tone_s;
} CrumpBitPitchState;

/*
 * Granular state: `grn` (engine B'; S091 P3 §3.3).
 *
 * What: the share is split into a record line (first half) and a delay line
 * (second half) of `length` samples each, written at the same write_pos.
 * - write_pos, length, valid: 16-bit, because a line is at most 63,328
 *   samples (half the arena);
 * - age: the samples since grain A started (grain B is half a grain later);
 * - pos_a, pos_b: the grains' read positions in the record line, 16.16 fixed
 *   point (integer part = sample index; drop-sample reads, user G6);
 * - inc_a, inc_b: their pitch steps, signed Q3.12 (ratio x 4096; negative =
 *   reversed); pos += inc << 4 per sample;
 * - dc_x, dc_y: the delay line's feedback DC blocker;
 * - amp_a, amp_b, pan_a, pan_b: each grain's level and pan (0..255), drawn
 *   at its restart from `amp` and `wid`.
 * The delay-line tap has no state: it reads exactly D = R behind the write
 * head, with no tape glide (user, 2026-11-04: not wanted in `grn`); a
 * change of R jumps.
 * Size: 32 B (the union's largest member). Accessors: CrumpBitEffect.c.
 */
typedef struct {
    uint16_t write_pos;
    uint16_t length;
    uint16_t valid;
    uint16_t age;
    uint32_t pos_a;
    uint32_t pos_b;
    int16_t inc_a;
    int16_t inc_b;
    float dc_x;
    float dc_y;
    uint8_t amp_a;
    uint8_t amp_b;
    uint8_t pan_a;
    uint8_t pan_b;
} CrumpBitGrainState;

/*
 * Reverb state: `rvb` (engine C; S091 P3 §3.4).
 *
 * What: the share is used as 32-bit words, one per sample, each holding the
 * four FDN lines' 8-bit values (one lane per line). write_pos and length are
 * in words; valid counts words written since the loop was treated as empty
 * (the wet path and the line feedback open per block once it covers the
 * longest line); damp[4] are the per-line one-pole states of the `ton`
 * damping.
 * Size: 28 B. Accessors: CrumpBitEffect.c only.
 */
typedef struct {
    uint32_t write_pos;
    uint32_t length;
    uint32_t valid;
    float damp[4];
} CrumpBitReverbState;

/*
 * CrumpBit runtime (88 B since S091 P3, a member of EffectsManager's DTCM
 * runtime union; user-approved growth up to +32 B, 2026-11-04).
 *
 * What: a SHARED part used by every sub-type (stages 1, 3 and 4 and all row
 * bytes), then a UNION of the sub-engines' DSP states (stage 2).
 * Shared part (56 B):
 * - ac_y_*, ac_c_*: stage 1 AC coupling (outputs; previous codes, int16,
 *   seeded 128 by init);
 * - svf_*: the output filter's two TPT integrators per channel (stage 4);
 * - row bytes, written by write_param() whatever the sub-type: their
 *   meaning changes per sub-type, their storage does not (plan §2.2);
 * - subtype: the resolved `sub` row; active_subtype: the engine that ran
 *   last block. A difference triggers crumpBit_enter() (plan §2.4);
 * - *_prev: the raw rows the previous block ramped to (ramp origins);
 * - primed: 0 until the first block of a tape sub-type starts its glide on
 *   the target;
 * - drv_reserved: the deferred output-filter drive row's byte (plan OQ6).
 * Union (32 B): one of the five states above.
 * Why here: EffectsManager zeroes the union and calls init() when the type
 * becomes active. Accessors: CrumpBitEffect.c only; no other module may keep
 * a pointer. Affiliates: effects_runtime_t, the size guard in
 * CrumpBitEffect.c, STORAGE_SRAM_MANIFEST.md.
 */
typedef struct {
    float ac_y_l;              /* stage 1 */
    float ac_y_r;              /* stage 1 */
    float svf_l1;              /* stage 4: TPT band integrator, left */
    float svf_l2;              /* stage 4: TPT low integrator, left */
    float svf_r1;              /* stage 4: TPT band integrator, right */
    float svf_r2;              /* stage 4: TPT low integrator, right */
    int16_t ac_c_l;            /* stage 1: previous code, left */
    int16_t ac_c_r;            /* stage 1: previous code, right */
    uint8_t bit_off;           /* row 3, shared */
    uint8_t bit_invert;        /* row 4, shared */
    uint8_t mix_raw;           /* row 5, shared: the mix stage */
    uint8_t feedback_raw;      /* row 6, sub-type row */
    uint8_t rate;              /* row 7, sub-type row */
    uint8_t subtype;           /* row 8, shared: resolved sub-type */
    uint8_t sync;              /* row 9, sub-type row */
    uint8_t pan_raw;           /* row 10, sub-type row */
    uint8_t tone_raw;          /* row 11, sub-type row */
    uint8_t filter_type;       /* row 12, shared */
    uint8_t filter_freq;       /* row 13, shared */
    uint8_t filter_reso;       /* row 14, shared */
    uint8_t aux_raw;           /* row 15, sub-type row (pitch) */
    uint8_t grain_st2;         /* row 16, grn */
    uint8_t grain_st3;         /* row 17, grn */
    uint8_t grain_spread;      /* row 18, grn */
    uint8_t grain_density;     /* row 19, grn */
    uint8_t grain_amp;         /* row 20, grn */
    uint8_t grain_width;       /* row 21, grn */
    uint8_t active_subtype;    /* the engine that ran last block */
    uint8_t mix_prev;          /* mix stage ramp origin */
    uint8_t feedback_prev;     /* sub-type ramp origin */
    uint8_t pan_prev;          /* sub-type ramp origin */
    uint8_t filter_type_prev;  /* stage 4 ramp origin */
    uint8_t filter_reso_prev;  /* stage 4 ramp origin */
    uint8_t primed;            /* tape glide primed */
    uint8_t drv_reserved;      /* deferred drive row (plan OQ6) */
    uint8_t pad;               /* alignment of the union */
    union {
        CrumpBitTapState tap;      /* dly, cmb */
        CrumpBitPitchState pit;    /* pit */
        CrumpBitGrainState grn;    /* grn */
        CrumpBitReverbState rvb;   /* rvb */
    } u;
```

#### H2 — Sub-engine helper and lane-map declarations

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, **add** after HEAD line 191. New lines 287–317 (31 lines).

- **What:** declares, for the page (`CrumpBitParameters.c`) and the ops
  table, what the Effect page and the manager read:
  - `crumpBit_subtypeRateLimit()`, the largest `rte` that fits a share
    under one sub-type's length law;
  - `crumpBit_grainMaxSamples()`, the longest `grn` time (a half line less
    the guard);
  - `crumpBit_combNote()`, the MIDI note a `cmb` `rte` plays;
  - `crumpBit_laneMap()`, the per-`sub` 16-lane table.
- **Inputs:** sub-type id and share (NULL allowed), raw `rte`, or the
  variant.
- **Outputs:** limit 0..127, samples, note, or a const table.
- **Accessors:** `crumpBit_uiFormatValue3()` (C8); `crumpBit_ops.lane_map`
  → `effects_laneTable()` (M6); the host laws CB6, CB9.

Add:

```c
/*
 * Sub-engine helpers shared with the page (S091 P3).
 *
 * crumpBit_subtypeRateLimit(): the largest `rte` whose length fits the share
 * under one sub-type's law (delay curve, comb table, pitch window, a `grn`
 * half line, the `rvb` word buffer); the effective_max op and the Rate label
 * use it. crumpBit_grainMaxSamples(): the longest `grn` time, one half line
 * less the guard. crumpBit_combNote(): the MIDI note a `cmb` `rte` plays
 * (112 - rte, clamped at 96), for the note-name label.
 * Inputs: sub-type id, share (may be NULL), or the raw `rte`. Outputs: as
 * named. Accessors: CrumpBitEffect.c, crumpBit_uiFormatValue3().
 */
uint8_t crumpBit_subtypeRateLimit(uint8_t subtype, const fx_share_t *share);
float crumpBit_grainMaxSamples(const fx_share_t *share);
uint8_t crumpBit_combNote(uint8_t rate);

/*
 * Per-sub-type FX lane maps (S091 P3 §2.9).
 *
 * What: the 16-entry lane -> row table for one `sub` value. Nine lanes are
 * shared by every map (0 Morph, 1 bof, 2 biv, 3 mix, 7 vol, 8 pan, 11 typ,
 * 12 frq, 13 res; lane 3 `mix` is never freeable); the seven-lane pool
 * (4, 5, 6, 9, 10, 14, 15) is assigned per sub-type (user L1/L2). The `dly`
 * map is the S091 P2 table, so `.fx` files round-trip unchanged.
 * Input: the `sub` value (anything above the last id reads the last map).
 * Output: a const 16-entry table in flash.
 * Accessors: crumpBit_ops.lane_map (EffectsManager's effects_laneTable());
 * defined in CrumpBitEffect.c beside the ops table (host-tested, CB9).
 */
const uint8_t *crumpBit_laneMap(uint8_t variant);

```

### 3.2 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`

#### E1 — File header: four stages; includes

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 12–22: **replace** (11 lines → 19; new lines 12–30).

- **What:**
  - Rewrites the file header to the P3 structure: stage 1 shared dry
    path, stage 2 one sub-engine per block, stage 3 the shared MIX stage,
    stage 4 the shared output filter. Within a sub-type every statement
    still runs on every frame (constant CPU); the sub-type is chosen once
    per block.
  - Adds `random.h` (`GetRngValue()`, `grn`) and `<string.h>` (`memset()`
    in `crumpBit_enter()`).
- **Why:** the reader of the DSP file needs the stage map before the code;
  the includes are new dependencies.

Remove (HEAD 12–22):

```c
 * S074_CRUMPBIT_EFFECT.md §3). Compiled with -Ofast (Makefile MK4).
 * Every stage runs on every sample whatever its settings (constant CPU).
 */

#include "CrumpBitEffect.h"
#include "CrumpBitParameters.h"
#include "StepScale.h"
#include "sequencer.h"
#include "cmsis_intrinsics.h"   /* __USAT: crumpBit_adc() (S091 R2) */
#include "CrumpBitTables.h"     /* Rate, tone and cutoff tables (S091 P2) */
#include <math.h>
```

Replace with:

```c
 * S074_CRUMPBIT_EFFECT.md §3; sub-engines S091 P3). Compiled with -Ofast
 * (Makefile MK4). Four stages per block:
 *   1 shared   ADC -> data lines -> DAC -> AC = dry h      (inline helper)
 *   2 sub-type one of dly | cmb | pit | grn | rvb -> wet   (per-block switch)
 *   3 shared   MIX: out = h + mix (wet - h)                 (inline helper)
 *   4 shared   TPT output filter, a second pass
 * Within a sub-type every statement runs on every frame whatever its
 * settings (constant CPU); the sub-type is chosen once per block.
 */

#include "CrumpBitEffect.h"
#include "CrumpBitParameters.h"
#include "StepScale.h"
#include "sequencer.h"
#include "cmsis_intrinsics.h"   /* __USAT: crumpBit_adc() (S091 R2) */
#include "CrumpBitTables.h"     /* Rate, tone and cutoff tables (S091 P2) */
#include "random.h"             /* GetRngValue(): grain decisions (S091 P3) */
#include <math.h>
#include <string.h>           /* memset(): crumpBit_enter() */
```

#### E2 — P3 constants, runtime/union size guards, prefix guard

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 92–98: **replace** (7 lines → 51; new lines 100–150).

- **What:**
  - `CRUMPBIT_NOINLINE`: each engine stays a separate symbol, so the ELF
    gate can measure its frame loop. It costs one call per block.
  - `CRUMPBIT_COMB_GLIDE_K` (5 ms tau, `1 − exp(−1/(0.005·fs))`): `cmb`
    retunes and flanges quickly.
  - `CRUMPBIT_PITCH_GUARD` 2: the shortest head distance, as the tape's
    interpolation guard.
  - `CRUMPBIT_GRAIN_MIN` 64: with blocks ≤ 32 frames, at most one restart
    per block.
  - `CRUMPBIT_GRAIN_DETUNE_CENTS` 35 (G8) and `CRUMPBIT_CENT_LIN` =
    ln 2/1200: a linear cents → ratio slope, no `exp2f`.
  - `CRUMPBIT_RVB_BASE0..3`: four mutually prime line lengths, 32–47 ms at
    size 1.
  - `CRUMPBIT_RVB_FREEZE_LG`: the log2 per-sample gain while frozen.
  - The static asserts replace P2's 76 B guard:
    - the runtime is 88 B;
    - the union is 32 B;
    - `CrumpBitTapState` and `CrumpBitPitchState` share the offsets of
      `write_pos` and `valid`, so a `dly ↔ cmb ↔ pit` switch keeps the
      recording.
- **Why:** each constant is used inside a frame loop or per-block setup;
  named once, they are also cited by the host laws (CB12–CB15).
- **Accessors:** the `crumpBit_run*()` engines, `crumpBit_grainPick()`,
  `crumpBit_subtypeRateLimit()`.

Remove (HEAD 92–98):

```c
 * S091 P2: 76 B fills the Effect union exactly (EffectsManager.c guards the
 * union at 76 B). A larger runtime grows .dtcmz and shrinks the arena: RAM
 * approval first (the +8 B approved for this work is still unused).
 */
_Static_assert(sizeof(CrumpBitRuntime) == 76u,
               "CrumpBitRuntime size is recorded in the S091 P2 schedule and "
               "must stay inside the 76 B Effect union");
```

Replace with:

```c
 * S091 P3 sub-engine constants (S091_P3_CrumpBit_SubType_Expansions.md §3).
 *
 * CRUMPBIT_NOINLINE keeps each sub-engine's frame loop in its own symbol, so
 * crumpbit_elf_check can gate every engine separately; the call happens once
 * per block.
 * CRUMPBIT_COMB_GLIDE_K the `cmb` glide, tau 5 ms (1 - exp(-1/(0.005 fs))):
 * fast, so retuning and an Effects LFO on `rte` (flanging) follow quickly.
 * CRUMPBIT_PITCH_GUARD the shortest head delay of `pit` (2 samples, as the
 * tape's interpolation guard).
 * CRUMPBIT_GRAIN_MIN the shortest grain (64 frames), so a grain restarts at
 * most once per 32-frame block and the per-block candidate always covers it.
 * CRUMPBIT_GRAIN_DETUNE_CENTS the detune at `spr` 127 (G8: +-35 cents);
 * CRUMPBIT_CENT_LIN the linear cents-to-ratio slope ln(2)/1200, exact to
 * 0.1 % within +-50 cents (no exp2f).
 * CRUMPBIT_RVB_BASE0..3 the four FDN line lengths at size 1.0 (32-47 ms,
 * mutually prime); CRUMPBIT_RVB_FREEZE_LG the log2 per-sample gain while
 * frozen (gain 0.999 per 2,053 samples).
 * Accessors: the crumpBit_run*() engines, crumpBit_grainPick().
 */
#define CRUMPBIT_NOINLINE             __attribute__((noinline))
#define CRUMPBIT_COMB_GLIDE_K         4.5245e-3f
#define CRUMPBIT_PITCH_GUARD          2.0f
#define CRUMPBIT_GRAIN_MIN            64u
#define CRUMPBIT_GRAIN_DETUNE_CENTS   35.0f
#define CRUMPBIT_CENT_LIN             5.7762265e-4f
#define CRUMPBIT_RVB_BASE0            1433u
#define CRUMPBIT_RVB_BASE1            1601u
#define CRUMPBIT_RVB_BASE2            1867u
#define CRUMPBIT_RVB_BASE3            2053u
#define CRUMPBIT_RVB_FREEZE_LG        (-7.0272e-7f)

/*
 * S091 P3: 88 B = 56 B shared + a 32 B sub-type union (user-approved growth
 * up to +32 B over 76 B, 2026-11-04). EffectsManager.c guards the union at
 * 88 B; the arena may lose one 32 B granule with it (LTO packing decides;
 * CRUMPBIT_LOOP_MAX_BYTES assumes it does: plan §5, N1).
 */
_Static_assert(sizeof(CrumpBitRuntime) == 88u,
               "CrumpBitRuntime size is recorded in the S091 P3 schedule");
_Static_assert(sizeof(((CrumpBitRuntime *)0)->u) == 32u,
               "the sub-type union is 32 B (grn is the largest member)");
/*
 * The common prefix (plan §2.2): the single-line sub-types keep length,
 * write_pos and valid at the same offsets, so a switch between them keeps
 * the recording without a conversion.
 */
_Static_assert(__builtin_offsetof(CrumpBitTapState, valid) ==
               __builtin_offsetof(CrumpBitPitchState, valid) &&
               __builtin_offsetof(CrumpBitTapState, write_pos) ==
               __builtin_offsetof(CrumpBitPitchState, write_pos),
               "dly/cmb and pit must share the length/write_pos/valid prefix");
```

#### E3 — `crumpBit_tableLimit()`: the P2 Rate-limit search, generalised (head)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 209–211: **replace** (3 lines → 9; new lines 261–269).

- **What:**
  - The S091 P2 `crumpBit_rateLimit()` body becomes
    `crumpBit_tableLimit(table, max_samples)`: the same seven halving
    probes plus the eighth, over any ascending 128-entry table. This unit
    is its head; E4 has the body changes.
  - It is `CRUMPBIT_NOINLINE`: five callers inlined it at +2.3 KB of
    flash.
- **Inputs:** an ascending table and the largest length that fits.
- **Output:** the largest index 0..127 whose entry fits (0 when none
  does).
- **Callers:** `crumpBit_rateLimit()`, `crumpBit_subtypeRateLimit()` (E4).

Remove (HEAD 209–211):

```c
uint8_t crumpBit_rateLimit(const fx_share_t *share)
{
    const float max_samples = crumpBit_shareMaxSamples(share);
```

Replace with:

```c
/*
 * Largest index of an ascending 128-entry table whose value is <= max
 * (S091 P3: the S091 P2 Rate-limit search, generalised to every sub-type's
 * length table). Output 0..127; 0 also when even entry 0 does not fit.
 * Callers: crumpBit_rateLimit(), crumpBit_subtypeRateLimit().
 */
static CRUMPBIT_NOINLINE uint8_t crumpBit_tableLimit(const float *table,
                                                     float max_samples)
{
```

#### E4 — Table-limit body; `rateLimit`, `grainMaxSamples`, `subtypeRateLimit`, `combNote`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 229–238: **replace** (10 lines → 59; new lines 287–345).

- **What:**
  - The probe expressions read `table[...]` instead of
    `crumpBit_rateTable[...]` (the arithmetic is unchanged; CB6 compares it
    with a linear scan for every share in 32 B steps and every sub-type).
  - Then the per-sub-type length laws (plan §2.6):
    - `crumpBit_rateLimit()`: the delay curve within `length − 2` (the P2
      result).
    - `crumpBit_grainMaxSamples()`: one `grn` line is half the seated
      share; the longest synced time is that half less the guard, 0 below
      one grain.
    - `crumpBit_subtypeRateLimit(sub, share)`:
      - `cmb`: the comb table within `length − 2`;
      - `pit`: the window table within `length − 2 − guard`, since both
        heads reach W + 2 behind the write head;
      - `grn`: the delay curve within one half line;
      - `rvb`: the size table within (words − 2)/2,053, so the longest
        line (base 2,053 × size) fits the word buffer;
      - `dly`: the P2 rule.
    - `crumpBit_combNote(rte)`: MIDI note `112 − min(rte, 96)`, for the
      note-name label (S9: 96 semitone steps, higher `rte` = lower note).
- **Why:** a `rte` must never ask for more buffer than the engine has
  (§2.6). The DSP, the manager's clamp and the page label all use these
  same functions, so they agree exactly.
- **Inputs:** share (NULL = empty), sub-type, `rte`.
- **Outputs:** limits 0..127, samples, note.
- **Accessors:** `crumpBit_effectiveMax()` (E10), `crumpBit_runGrain()`
  (E10), `crumpBit_uiFormatValue3()` (C8), host CB6.

Remove (HEAD 229–238):

```c
        count += (crumpBit_rateTable[count + step - 1u] <= max_samples)
            ? step : 0u;
    /*
     * Eighth probe: entry count (index <= 127, always in range). Below 127 it
     * is the first entry known not to fit and adds 0; at 127 it decides
     * whether the whole table fits (count 128).
     */
    count += (crumpBit_rateTable[count] <= max_samples) ? 1u : 0u;
    /* The longest fitting rate; 0 also when even 20 ms does not fit. */
    return (uint8_t)((count != 0u) ? count - 1u : 0u);
```

Replace with:

```c
        count += (table[count + step - 1u] <= max_samples) ? step : 0u;
    /*
     * Eighth probe: entry count (index <= 127, always in range). Below 127 it
     * is the first entry known not to fit and adds 0; at 127 it decides
     * whether the whole table fits (count 128).
     */
    count += (table[count] <= max_samples) ? 1u : 0u;
    /* The longest fitting entry; 0 also when even the first does not fit. */
    return (uint8_t)((count != 0u) ? count - 1u : 0u);
}

uint8_t crumpBit_rateLimit(const fx_share_t *share)
{
    return crumpBit_tableLimit(crumpBit_rateTable,
                               crumpBit_shareMaxSamples(share));
}

float crumpBit_grainMaxSamples(const fx_share_t *share)
{
    /* half: one `grn` line, half the seated share (plan §3.3.2). */
    const uint32_t half = (share ? ((share->bytes > CRUMPBIT_LOOP_MAX_BYTES)
                                        ? CRUMPBIT_LOOP_MAX_BYTES
                                        : share->bytes) : 0u) / 2u;

    return (half < CRUMPBIT_GRAIN_MIN + CRUMPBIT_LOOP_GUARD)
        ? 0.0f : (float)(half - CRUMPBIT_LOOP_GUARD);
}

uint8_t crumpBit_subtypeRateLimit(uint8_t subtype, const fx_share_t *share)
{
    const float max_samples = crumpBit_shareMaxSamples(share);
    /* words: the `rvb` frames (4 bytes each) this share holds. */
    const float words = max_samples * 0.25f;

    switch (subtype) {
    case CRUMPBIT_SUBTYPE_COMB:
        return crumpBit_tableLimit(crumpBit_combTable, max_samples);
    case CRUMPBIT_SUBTYPE_PITCH:
        /* Both heads reach at most W + 2 behind the write head. */
        return crumpBit_tableLimit(crumpBit_pitchWindow,
                                   max_samples - CRUMPBIT_PITCH_GUARD);
    case CRUMPBIT_SUBTYPE_GRAIN:
        /* The free time is snapped to a division that fits one line. */
        return crumpBit_tableLimit(crumpBit_rateTable,
                                   crumpBit_grainMaxSamples(share));
    case CRUMPBIT_SUBTYPE_REVERB:
        /* The longest line (base 2,053 x size) must fit the word buffer. */
        return crumpBit_tableLimit(crumpBit_reverbSize,
                                   (words - 2.0f) *
                                   (1.0f / (float)CRUMPBIT_RVB_BASE3));
    default:
        return crumpBit_tableLimit(crumpBit_rateTable, max_samples);
    }
}

uint8_t crumpBit_combNote(uint8_t rate)
{
    /* rte n plays MIDI note 112 - n; n above 96 holds note 16 (20.6 Hz). */
    return (uint8_t)(112u - ((rate > 96u) ? 96u : rate));
```

#### E5 — `crumpBit_seat()`: the single-line prefix in the union

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 402–423: **replace** (22 lines → 27; new lines 509–535).

- **What:**
  - The P2 seat now takes the seated length (the caller clamps the share)
    and works on `rt->u.tap`, the common prefix of `dly`, `cmb` and `pit`.
  - The delay clamp (`delay ≤ length − 2`) applies only to the tape
    engines: `pit` has no `delay` field (it reinterprets that word as its
    phase).
- **Why:** the P2 rules (tail kept while `write_pos` fits, otherwise
  re-seated; `valid` cut to `write_pos`) stay identical for `dly`. CB4
  proves it bit-exact against the P2 reference, and break 4 removes the
  clamp.
- **Inputs:** runtime, length in bytes.
- **Output:** the length (unchanged meaning).
- **Callers:** `crumpBit_seatActive()` (E10).

Remove (HEAD 402–423):

```c
 * Callers: crumpBit_process(), crumpBit_bufferChanged().
 * Affiliates: FxBuffer share contract, crumpBit_shareMaxSamples().
 */
static uint32_t crumpBit_seat(CrumpBitRuntime *rt, const fx_share_t *share)
{
    uint32_t len = share ? share->bytes : 0u;

    if (len > CRUMPBIT_LOOP_MAX_BYTES)
        len = CRUMPBIT_LOOP_MAX_BYTES;
    if (len != rt->length) {
        if (rt->length != 0u && rt->write_pos < len) {
            /* Tail kept: only the samples written since the last wrap count. */
            if (rt->valid > rt->write_pos)
                rt->valid = rt->write_pos;
        } else {
            rt->write_pos = 0u;
            rt->valid = 0u;
        }
        rt->length = len;
        if (len >= CRUMPBIT_LOOP_MIN &&
            rt->delay > (float)(len - CRUMPBIT_LOOP_GUARD))
            rt->delay = (float)(len - CRUMPBIT_LOOP_GUARD);
```

Replace with:

```c
 * Inputs (S091 P3): runtime and the seated length min(share, LOOP_MAX).
 * Callers: crumpBit_seatActive() (process, share changes).
 * Affiliates: FxBuffer share contract, crumpBit_shareMaxSamples().
 */
static uint32_t crumpBit_seat(CrumpBitRuntime *rt, uint32_t len)
{
    /*
     * S091 P3: the single-line prefix lives in the sub-type union; `pit`
     * shares it (CrumpBitPitchState), but has no glide, so only `dly`/`cmb`
     * clamp the delay.
     */
    CrumpBitTapState *t = &rt->u.tap;
    const uint8_t tape = (uint8_t)(rt->active_subtype != CRUMPBIT_SUBTYPE_PITCH);

    if (len != t->length) {
        if (t->length != 0u && t->write_pos < len) {
            /* Tail kept: only the samples written since the last wrap count. */
            if (t->valid > t->write_pos)
                t->valid = t->write_pos;
        } else {
            t->write_pos = 0u;
            t->valid = 0u;
        }
        t->length = len;
        if (tape && len >= CRUMPBIT_LOOP_MIN &&
            t->delay > (float)(len - CRUMPBIT_LOOP_GUARD))
            t->delay = (float)(len - CRUMPBIT_LOOP_GUARD);
```

#### E6 — `crumpBit_init()`: unseat through the union

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 444–446: **replace** (3 lines → 9; new lines 556–564).

- **What:** the three unseat stores now address `rt->u.tap`. The comment
  records the state at entry:
  - the manager zeroed the union, so every engine is unseated and
    `active_subtype` is 0 (`dly`);
  - the forced resolution then writes the Scene's `sub`, and a different
    value is entered on the first block like any switch.
- **Why:** the fields moved (H1); entering CrumpBit must still never play
  a previous owner's tail.

Remove (HEAD 444–446):

```c
    rt->length = 0u;
    rt->write_pos = 0u;
    rt->valid = 0u;
```

Replace with:

```c
    /*
     * The manager zeroed the union: every sub-type state is unseated
     * (length 0) and the active engine is `dly` (0). The forced resolution
     * then writes the Scene's `sub`; a different value is entered on the
     * first block like any switch (crumpBit_enter()).
     */
    rt->u.tap.length = 0u;
    rt->u.tap.write_pos = 0u;
    rt->u.tap.valid = 0u;
```

#### E7 — Init seeds for rows 15–21; `crumpBit_bytePosition()`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, **add** after HEAD line 465. New lines 584–624 (41 lines).

- **What:**
  - **Init seeds (closing `crumpBit_init()`):** `aux`, `st2`, `st3` = 63
    (0 semitones) and `den` = 127 (continuous grains), the neutral values
    until the forced resolution writes the Scene's rows. `spr`, `amp`,
    `wid` stay 0 from the zeroed union.
  - **`crumpBit_bytePosition()`** (new): the active engine's write
    position and written amount in arena bytes:
    - single line: one byte per sample;
    - `grn`: its record line starts at byte 0, so the index is a byte
      index;
    - `rvb`: four bytes per word.
- **Why:**
  - Plan §2.4: a switch converts this format-free position into the new
    engine's geometry, and the handoff export reports it.
  - With the seeds, a CrumpBit entered without a Scene value plays pitch
    0 and full density.
- **Inputs:** runtime.
- **Outputs:** `*pos`, `*written` (bytes).
- **Callers:** `crumpBit_enter()`, `crumpBit_exportHandoff()` (E8).

Add:

```c
    /* S091 P3 neutral pitch rows (63 = 0 semitones) and full density. */
    rt->aux_raw = 63u;
    rt->grain_st2 = 63u;
    rt->grain_st3 = 63u;
    rt->grain_density = 127u;
}

/*
 * The active engine's write position and written amount, in arena bytes
 * (S091 P3 §2.4).
 *
 * What: converts the active sub-type's prefix to a format-free view: the
 * byte at which the next sample will be written, and how many bytes behind
 * it hold samples written since the buffer was last treated as empty.
 * - single line (`dly`, `cmb`, `pit`): one byte per sample;
 * - `grn`: the record line starts at byte 0, so its index is a byte index;
 *   the delay line mirrors it in the second half;
 * - `rvb`: four bytes per sample (one 32-bit word).
 * Why: crumpBit_enter() reinterprets the arena in the new format (no clear,
 * no gate; user: hand-off glitches are encouraged), and the handoff export
 * describes the write head the same way for every engine.
 * Inputs: runtime. Outputs: *pos (byte), *written (bytes; 0 when unseated).
 * Callers: crumpBit_enter(), crumpBit_exportHandoff().
 */
static void crumpBit_bytePosition(const CrumpBitRuntime *rt, uint32_t *pos,
                                  uint32_t *written)
{
    switch (rt->active_subtype) {
    case CRUMPBIT_SUBTYPE_GRAIN:
        *pos = rt->u.grn.write_pos;
        *written = rt->u.grn.valid;
        break;
    case CRUMPBIT_SUBTYPE_REVERB:
        *pos = rt->u.rvb.write_pos * 4u;
        *written = rt->u.rvb.valid * 4u;
        break;
    default:
        *pos = rt->u.tap.write_pos;
        *written = rt->u.tap.valid;
        break;
    }
```

#### E8 — Handoff export per engine family; `write_param` comment

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 489–507: **replace** (19 lines → 43; new lines 648–690).

- **What:**
  - `crumpBit_exportHandoff()` keeps the P2 description for the tape
    engines (`dly`, `cmb`): write position, the fractional read position
    as an integer, written flag. The others report their write position as
    both pointers, via `crumpBit_bytePosition()`.
  - The `write_param` header comment states the P3 rule: `sub` is stored
    (clamped), and rows 15–21 are stored whatever the active sub-type, so
    a switch keeps every value.
- **Why:** nobody adopts the Effect pointers today (CrumpBit never does),
  but the record must stay meaningful for every engine. The read head of
  `pit`, `grn` or `rvb` is not one position.
- **Callers:** EffectsManager's handoff helper.

Remove (HEAD 489–507):

```c
    if (rt->length == 0u)
        return;
    rp = (float)rt->write_pos - rt->delay;
    rp += (rp < 0.0f) ? (float)rt->length : 0.0f;
    out->write_offset[FXBUF_HANDOFF_EFFECT_POINTER_BASE] = rt->write_pos;
    out->read_offset[FXBUF_HANDOFF_EFFECT_POINTER_BASE] =
        (uint32_t)rp % rt->length;
    if (rt->valid != 0u)
        out->state_flags |= FXBUF_STATE_EFFECT_WRITTEN;
}

/*
 * Store one resolved row value.
 *
 * What: keeps the raw 0..255 value of rows 3..14. Every conversion (masks,
 * gains, curve, division, tone and filter coefficients) happens once per
 * block in process(), so an LFO that rewrites a row every block costs the
 * same as a static setting. The sub-type row is ignored (only `dly` exists;
 * a second sub-type would store it here, plan §4 point 5). Inputs: runtime, row index, value
```

Replace with:

```c
    /*
     * S091 P3: only the tape sub-types describe a read head; the others
     * report their write position as the read position. Nobody adopts the
     * Effect pointers today (CrumpBit itself never does).
     */
    if (rt->active_subtype == CRUMPBIT_SUBTYPE_DELAY ||
        rt->active_subtype == CRUMPBIT_SUBTYPE_COMB) {
        const CrumpBitTapState *t = &rt->u.tap;

        if (t->length == 0u)
            return;
        rp = (float)t->write_pos - t->delay;
        rp += (rp < 0.0f) ? (float)t->length : 0.0f;
        out->write_offset[FXBUF_HANDOFF_EFFECT_POINTER_BASE] = t->write_pos;
        out->read_offset[FXBUF_HANDOFF_EFFECT_POINTER_BASE] =
            (uint32_t)rp % t->length;
        if (t->valid != 0u)
            out->state_flags |= FXBUF_STATE_EFFECT_WRITTEN;
        return;
    }
    {
        /* pos/valid: the active engine's write byte and written bytes. */
        uint32_t pos;
        uint32_t written;

        crumpBit_bytePosition(rt, &pos, &written);
        out->write_offset[FXBUF_HANDOFF_EFFECT_POINTER_BASE] = pos;
        out->read_offset[FXBUF_HANDOFF_EFFECT_POINTER_BASE] = pos;
        if (written != 0u)
            out->state_flags |= FXBUF_STATE_EFFECT_WRITTEN;
    }
}

/*
 * Store one resolved row value.
 *
 * What: keeps the raw 0..255 value of rows 3..14. Every conversion (masks,
 * gains, curve, division, tone and filter coefficients) happens once per
 * block in process(), so an LFO that rewrites a row every block costs the
 * same as a static setting. S091 P3: the sub-type row is stored (clamped to
 * the last engine); process() enters a new engine on the next block. Rows
 * 15..21 (`aux` and the granular rows) are stored whatever the active
 * sub-type, so a switch keeps every value (plan §2.4). Inputs: runtime, row index, value
```

#### E9 — `write_param`: `sub` and rows 15–21

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, **add** after HEAD line 527. New lines 711–722 (12 lines).

- **What:**
  - **`sub` (row 8):** stored clamped to 0..4, so a Pattern value above 4
    is `rvb` (OQ3). `crumpBit_process()` enters the new engine on the next
    block.
  - **Rows 15–21:** stored raw into their bytes (`aux_raw`, `grain_st2`,
    `grain_st3`, `grain_spread`, `grain_density`, `grain_amp`,
    `grain_width`).
- **Why:**
  - P2 ignored the row, because only `dly` existed.
  - Every conversion still happens once per block in the engine, so an LFO
    on any row costs the same as a static value.
- **Inputs:** runtime, row, value (already clamped to the descriptor max).
- **Caller:** `effects_service()`.

Add:

```c
    /* S091 P3: the sub-engine selector and its rows. */
    case CRUMPBIT_PARAM_SUBTYPE:
        rt->subtype = (value < CRUMPBIT_SUBTYPE_COUNT)
            ? value : (uint8_t)(CRUMPBIT_SUBTYPE_COUNT - 1u);
        break;
    case CRUMPBIT_PARAM_AUX:            rt->aux_raw = value;           break;
    case CRUMPBIT_PARAM_GRAIN_ST2:      rt->grain_st2 = value;         break;
    case CRUMPBIT_PARAM_GRAIN_ST3:      rt->grain_st3 = value;         break;
    case CRUMPBIT_PARAM_GRAIN_SPREAD:   rt->grain_spread = value;      break;
    case CRUMPBIT_PARAM_GRAIN_DENSITY:  rt->grain_density = value;     break;
    case CRUMPBIT_PARAM_GRAIN_AMP:      rt->grain_amp = value;         break;
    case CRUMPBIT_PARAM_GRAIN_WIDTH:    rt->grain_width = value;       break;
```

#### E10 — From `crumpBit_process()` to the end of the file: the shared stages, five engines, seat/enter, process, ops

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 649–1058 (end of file): **replace** (410 lines → 1681; new lines 844–2524).

- **What:**
  - Replaces the P2 single-loop `crumpBit_process()`, the trailing hooks
    and the ops table (HEAD 649 to the end of the file) with the P3 DSP. It
    is shown as 27 consecutive sub-blocks, one per helper or engine.
  - The P2 statements survive inside the shared helpers (stage 1, the line
    read, the return chain, the tone setup) and `crumpBit_tapLoop()`, the
    `dly`/`cmb` loop.
- **Why:**
  - Plan §2.1: the sub-type stage is chosen once per block, so each
    sub-engine is its own frame loop and pays only for its own work.
  - Stages 1, 3 and 4 are shared code, so the dry path, the mix law and
    the output filter are identical in every sub-type (M1; CB10 proves it
    bit-exact).
- **Boundary:** HEAD line 648 (`/*`) is kept and opens E10.1's comment
  block; the replacement starts at its second line.
- **Order of work per block** (`crumpBit_process()`, sub-block 24):
  1. enter a changed sub-type;
  2. seat;
  3. the shared per-block values;
  4. the engine's frame loop (stages 1–3);
  5. the stage-4 pass.

Remove HEAD lines 649–1058 (410 lines), from
`* Process one Effect bus block in place (S074_CRUMPBIT_EFFECT.md §3; S091 P2`
through
`};`.

Replace with the 27 sub-blocks below, in order:

##### E10.1 — `crumpbit_block_t`: the block facts handed to an engine (new lines 844–868)

- **What:** the io pointers (`right` aliases `left` for mono), the share
  base, `frames`, the data-line masks `keep`/`invert`, the seated length
  in the engine's unit, the ramp step `1/frames`, and the mix stage's
  `mix` origin and step.
- **Why:**
  - Engines are separate symbols; one pointer carries the facts.
  - Every engine copies the fields it uses into locals before its loop: a
    float output store may alias any float reached through the pointer,
    which would otherwise reload them each frame (measured: −2 to −9 per
    frame).
- **Accessors:** written by `crumpBit_process()`, read by
  `crumpBit_run*()`.

```c
 * One block's shared context (S091 P3 §2.1).
 *
 * What: what crumpBit_process() resolves once per block and every sub-engine
 * reads: the io channels (right aliases left for a mono block), the frame
 * count, the stage-1 data-line masks, the ramp step, the mix stage's ramp
 * (origin and step; the mix stage owns them, engines only apply them), the
 * arena base and the seated length in the engine's own unit (bytes, line
 * samples or words).
 * Why a struct: the engines are separate symbols (CRUMPBIT_NOINLINE) so the
 * ELF gate can count each loop; one pointer carries the block's facts.
 * Accessors: crumpBit_process() (writes), crumpBit_run*() (read).
 */
typedef struct {
    float *left;
    float *right;
    int8_t *base;
    uint32_t frames;
    uint32_t keep;
    uint32_t invert;
    uint32_t len;
    float per_frame;
    float mix;
    float mix_inc;
} crumpbit_block_t;

```

##### E10.2 — Stage 1 (shared): `crumpBit_stage1()` (new lines 869–898)

- **What:** the P1/P2 dry path for one frame, unchanged:
  - code = `(adc(x) & keep) ^ invert` per channel;
  - `h = codeToFloat(code − code_prev) + R·h_prev`: the exact S091 R4
    form, one VCVT #7 and one VFMA;
  - the AC memories advance.
- **Why:** the dry signal `h` must be the same in every sub-type, because
  it is the mix stage's "signal without the sub-engine" (M1).
- **Inputs:** both input samples, the masks, the four AC memories (locals
  of the caller's loop, by pointer).
- **Outputs:** `hl`, `hr`.
- **Callers:** every engine loop.
- **Proofs:** CB4 (`dly` bit-exact against P2) and CB10 (every sub-type
  at mix 0 equals this path).

```c
/*
 * Stage 1, shared: ADC -> data lines -> DAC -> AC coupling for one frame.
 *
 * What: the S091 P1/P2 statements, unchanged, as one inline helper so every
 * engine runs the identical dry path: the 8-bit code of each channel with
 * the forced-off bits cleared (keep) and the inverted bits flipped
 * (invert); then h = (code - code_prev)/128 + R*h_prev, the exact R4 form
 * (the integer difference converted by `vcvt #7`, one VFMA). The AC memories
 * advance to this frame.
 * Inputs: the two input samples, the masks, and the four AC memories by
 * pointer (locals of the engine loop, kept in registers after inlining).
 * Outputs: *hl, *hr, the dry signal (the mix stage's `h`).
 * Callers: every crumpBit_run*() loop.
 */
static inline void crumpBit_stage1(float xl, float xr, uint32_t keep,
                                   uint32_t invert, int32_t *ac_cl,
                                   int32_t *ac_cr, float *ac_yl, float *ac_yr,
                                   float *hl, float *hr)
{
    const int32_t code_l = (int32_t)((crumpBit_adc(xl) & keep) ^ invert);
    const int32_t code_r = (int32_t)((crumpBit_adc(xr) & keep) ^ invert);

    *hl = crumpBit_codeToFloat(code_l - *ac_cl) + CRUMPBIT_AC_R * *ac_yl;
    *hr = crumpBit_codeToFloat(code_r - *ac_cr) + CRUMPBIT_AC_R * *ac_yr;
    *ac_cl = code_l;
    *ac_yl = *hl;
    *ac_cr = code_r;
    *ac_yr = *hr;
}

```

##### E10.3 — Stage 3 (shared): `crumpBit_mixFrame()` (new lines 899–916)

- **What:** `out = h + mix·(wet − h)`: mix 0 is exactly the stage-1
  signal, mix 127 exactly the engine's wet.
- **Why:**
  - The binding mix-stage law (§2.8). Engines supply only `wet`; the row,
    its ramp (`blk.mix`/`mix_inc`, origin `mix_prev`) and this statement
    belong to the mix stage.
  - It is inline in each frame loop because `h` and `wet` coexist only
    there; a second pass would need 256 B of scratch.
- **Callers:** `grn`, `rvb` (wet already summed).

```c
/*
 * Stage 3, shared: the MIX STAGE for one channel (binding, user 2026-11-04).
 *
 * What: out = h + mix (wet - h): `mix` 0 is exactly the signal without the
 * sub-engine (stage 1), 127 exactly the sub-engine's wet. It is not part of
 * any sub-engine: engines only supply `wet`; the row, its ramp
 * (crumpbit_block_t.mix/mix_inc, origin mix_prev) and this law are the mix
 * stage's, the same in every sub-type (plan §2.8).
 * Why inline in each engine loop: the dry h and the wet exist together only
 * inside the frame loop; a separate pass would need 256 B of scratch.
 * Inputs: dry h, wet, the ramped mix. Output: the channel sample.
 * Callers: every crumpBit_run*() loop, as its last statement per channel.
 */
static inline float crumpBit_mixFrame(float h, float wet, float mix)
{
    return h + mix * (wet - h);
}

```

##### E10.4 — Stage 3, gain form: `crumpBit_mixFrameGain()` (new lines 917–931)

- **What:** the same law with the wet given as gain × signal:
  `h + mix·(g·sig − h)`.
- **Why:** choice 8. The product must sit inside the one expression, as
  in the P2 statement, so a contracting compiler forms the same FMA.
  Passed as an argument, `g·sig` rounds on its own, and CB4 failed
  bit-exactness against the P2 reference at block 832 until this form was
  used.
- **Callers:** `crumpBit_tapLoop()` (`dly`, `cmb`) and `crumpBit_runPitch()`
  (the `dpn` balance gains).

```c
/*
 * The same mix stage with the wet given as gain x signal (the delay-pan
 * balance of `dly`, `cmb` and `pit`): out = h + mix (gain sig - h).
 * Why a second form: the product must sit inside the one expression, as in
 * the S091 P2 statement `hr + mix * (gain_r * wet - hr)`, so a compiler that
 * contracts within an expression (FMA) rounds it exactly as P2 did; passed
 * as an argument, gain x sig would be rounded on its own (host law CB4 is
 * bit-exact against the P2 equations).
 */
static inline float crumpBit_mixFrameGain(float h, float gain, float sig,
                                          float mix)
{
    return h + mix * (gain * sig - h);
}

```

##### E10.5 — Fractional line read: `crumpBit_lineRead()` (new lines 932–958)

- **What:** the S074 tape read:
  - `rp = wp − d`, wrapped once by compare;
  - linear interpolation between `i0` and `i0 + 1`, each wrapped at
    `len`;
  - scaled by 1/128.
- **Inputs:** line, `len` (integer and float), write index, delay
  (0 < d ≤ len − 2).
- **Output:** the ±1 sample.
- **Callers:** `crumpBit_tapLoop()` only (the `pit` heads use the Q14
  read).

```c
/*
 * Fractional read of an 8-bit line (engines A and B).
 *
 * What: the S074 tape read: position wp - d wrapped by compare (at most one
 * length), linear interpolation between i0 and i0 + 1, scaled to +-1.
 * Inputs: line base, its length (integer and float), write index, delay in
 * samples (0 < d <= len - 2). Output: the interpolated sample.
 * Callers: crumpBit_tapLoop(), crumpBit_runPitch().
 */
static inline float crumpBit_lineRead(const int8_t *loop, uint32_t len,
                                      float flen, uint32_t wp, float d)
{
    float rp = (float)wp - d;
    uint32_t i0;
    uint32_t i1;
    float frac;

    rp += (rp < 0.0f) ? flen : 0.0f;
    i0 = (uint32_t)rp;
    frac = rp - (float)i0;
    i0 -= (i0 >= len) ? len : 0u;
    i1 = i0 + 1u;
    i1 -= (i1 >= len) ? len : 0u;
    return ((float)loop[i0] + frac * (float)(loop[i1] - loop[i0])) *
           CRUMPBIT_LSB;
}

```

##### E10.6 — Feedback return chain: `crumpBit_returnChain()` (new lines 959–986)

- **What:** the P2 §5.3 chain on a line read `w`:
  - the DC blocker `y = w − w₋₁ + R_dc·y₋₁` (≈ 5 Hz; mandatory with
    feedback above 1);
  - the `ton` tilt `s += k(v − s)`, `t = a·s + b·v` (LP (1,0), flat
    (0,1), HP (−1,1));
  - the feedback gain;
  - the cubic soft knee `y = v(1 − (4/27)v²)` on `v` clamped to ±1.5.
- **Inputs:** `w`, the memories by pointer, the coefficients, the ramped
  feedback.
- **Output:** the value added to the loop write.
- **Callers:**
  - `crumpBit_tapLoop()`;
  - `crumpBit_runPitch()`;
  - `crumpBit_grainSpan()`, with the tone fixed flat (k 1, (0, 1)), so the
    tilt folds away at compile time.

```c
/*
 * The feedback return chain shared by the tape-family engines (S091 P2
 * §5.3): DC blocker (y = w - w_prev + R_dc y_prev, ~5 Hz), the `ton` tilt
 * (one pole s += k (v - s); output a s + b v: LP (1,0), flat (0,1), HP
 * (-1,1)), the feedback gain, then the cubic soft knee
 * y = v (1 - (4/27) v^2) on v clamped to +-1.5.
 * Inputs: the line read, the four memories/coefficients by value/pointer,
 * the ramped feedback. Output: the value added to the loop write.
 * Callers: crumpBit_tapLoop(), crumpBit_runPitch(), crumpBit_runGrain().
 */
static inline float crumpBit_returnChain(float w, float *dc_x, float *dc_y,
                                         float *tone_s, float tone_k,
                                         float tone_a, float tone_b, float fb)
{
    const float dc = w - *dc_x + CRUMPBIT_DC_R * *dc_y;
    float tilt;
    float ret;

    *dc_x = w;
    *dc_y = dc;
    *tone_s += tone_k * (dc - *tone_s);
    tilt = tone_a * *tone_s + tone_b * dc;
    ret = fb * tilt;
    ret = (ret > CRUMPBIT_KNEE_IN_MAX) ? CRUMPBIT_KNEE_IN_MAX : ret;
    ret = (ret < -CRUMPBIT_KNEE_IN_MAX) ? -CRUMPBIT_KNEE_IN_MAX : ret;
    return ret * (1.0f - CRUMPBIT_KNEE_CUBIC * ret * ret);
}

```

##### E10.7 — Tone coefficients: `crumpBit_toneSetup()` (new lines 987–1002)

- **What:** per block, `k = crumpBit_toneK[ton]` and the `(a, b)` weights:
  LP below 63, exactly (0, 1) at 63 (a bit-exact pass-through, P2), HP
  above.
- **Callers:** `crumpBit_tapLoop()`, `crumpBit_runPitch()`,
  `crumpBit_runReverb()` (per-line damping).
- **Break 3** weights flat as (1, 1); CB4 catches it.

```c
/*
 * The `ton` tilt coefficients for one block (S091 P2 §5.3).
 * k from crumpBit_toneK; (a, b): LP (1, 0) below 63, flat (0, 1) at 63 (a
 * bit-exact pass-through), HP (-1, 1) above.
 */
static inline void crumpBit_toneSetup(uint8_t raw, float *k, float *a,
                                      float *b)
{
    const uint8_t t = (raw > 127u) ? 127u : raw;

    *k = crumpBit_toneK[t];
    *a = (t < CRUMPBIT_TONE_FLAT) ? 1.0f
       : ((t == CRUMPBIT_TONE_FLAT) ? 0.0f : -1.0f);
    *b = (t < CRUMPBIT_TONE_FLAT) ? 0.0f : 1.0f;
}

```

##### E10.8 — Engine A frame loop: `crumpBit_tapLoop()` (`dly`, `cmb`) (new lines 1003–1108)

- **What:** the S091 P2 stage-2 loop, arithmetic unchanged, around the
  shared helpers.
- **Per block:**
  - the delay-pan balance gains from the previous and current `dpn` (the
    S075 F2-E law) and their per-frame steps;
  - the feedback step;
  - the tone setup;
  - on the first block of a tape engine (`primed` 0), the glide starts on
    its target.
- **Per frame:**
  1. stage 1;
  2. the four ramps;
  3. the glide `delay += (target − delay)·glide_k`;
  4. the fractional read, muted until `delay + 2 ≤ valid`;
  5. the re-record `loop[wp] = adc(½(hL + hR) + return) − 128`;
  6. `wp` and `valid` advance;
  7. the mix stage with the balanced wet.
- **Inputs:** runtime, block, target (≤ len − 2), glide constant,
  feedback from/to (`cmb` passes signed gains).
- **Outputs:** io samples, loop bytes, tape state, AC state; `pan_prev`.
- **Callers:** `crumpBit_runDelay()` and `crumpBit_runComb()`. Being
  inline, each gets its own loop; both measure 124.

```c
/*
 * Engine A frame loop: one fractional tape tap with glide (`dly`, `cmb`).
 *
 * What: the S091 P2 stage-2 loop, unchanged in arithmetic, with stage 1 and
 * the mix stage as the shared helpers: glide towards `target`, fractional
 * read muted beyond `valid`, the return chain, the 8-bit re-record, and the
 * wet = delay-pan-balanced read. Every statement runs every frame.
 * Inputs: runtime, block, target delay (<= len - 2), glide constant, the
 * feedback ramp (from/to; `cmb` passes signed gains), tone coefficients.
 * Outputs: io samples (dry/wet mixed), loop bytes, tape state, AC state.
 * Callers: crumpBit_runDelay() (glide 1.5113e-4, the S074 tape) and
 * crumpBit_runComb() (glide 4.5245e-3); being inline, each gets its own loop.
 */
static inline void crumpBit_tapLoop(CrumpBitRuntime *rt,
                                    const crumpbit_block_t *b, float target,
                                    float glide_k, float fb, float fb_to)
{
    CrumpBitTapState *t = &rt->u.tap;
    int8_t *loop = b->base;
    const uint32_t len = b->len;
    const float flen = (float)len;
    /* Delay-pan balance: the delay's wet placement (S075 F2-E law). */
    float gain_l = crumpBit_panGainL(rt->pan_prev);
    float gain_r = crumpBit_panGainR(rt->pan_prev);
    const float gain_l_to = crumpBit_panGainL(rt->pan_raw);
    const float gain_r_to = crumpBit_panGainR(rt->pan_raw);
    const float fb_inc = (fb_to - fb) * b->per_frame;
    const float gain_l_inc = (gain_l_to - gain_l) * b->per_frame;
    const float gain_r_inc = (gain_r_to - gain_r) * b->per_frame;
    float mix = b->mix;
    float tone_k;
    float tone_a;
    float tone_b;
    float delay;
    float dc_x = t->dc_x;
    float dc_y = t->dc_y;
    float tone_s = t->tone_s;
    int32_t ac_cl = rt->ac_c_l;
    int32_t ac_cr = rt->ac_c_r;
    float ac_yl = rt->ac_y_l;
    float ac_yr = rt->ac_y_r;
    uint32_t wp = t->write_pos;
    uint32_t valid = t->valid;
    uint32_t n;

    crumpBit_toneSetup(rt->tone_raw, &tone_k, &tone_a, &tone_b);
    /* The first block of a tape sub-type starts its glide on the target. */
    if (!rt->primed) {
        t->delay = target;
        rt->primed = 1u;
    }
    delay = t->delay;

    /*
     * The block's fields as locals: an output store (float) may alias any
     * float reached through `b`, so reading them inside the loop would
     * reload them every frame.
     */
    float *const left = b->left;
    float *const right = b->right;
    const uint32_t frames = b->frames;
    const uint32_t keep = b->keep;
    const uint32_t invert = b->invert;
    const float mix_inc = b->mix_inc;

    for (n = 0u; n < frames; n++) {
        float hl;
        float hr;
        float wet;

        /* Stage 1 (shared). */
        crumpBit_stage1(left[n], right[n], keep, invert,
                        &ac_cl, &ac_cr, &ac_yl, &ac_yr, &hl, &hr);
        /* Stage 2, engine A: ramps, glide, read, return, re-record. */
        mix += mix_inc;
        fb += fb_inc;
        gain_l += gain_l_inc;
        gain_r += gain_r_inc;
        delay += (target - delay) * glide_k;
        wet = crumpBit_lineRead(loop, len, flen, wp, delay);
        wet = ((delay + 2.0f) <= (float)valid) ? wet : 0.0f;
        loop[wp] = (int8_t)((int32_t)crumpBit_adc(
            0.5f * (hl + hr) + crumpBit_returnChain(wet, &dc_x, &dc_y,
                                                    &tone_s, tone_k, tone_a,
                                                    tone_b, fb)) - 128);
        wp++;
        wp -= (wp >= len) ? len : 0u;
        valid += (valid < len) ? 1u : 0u;
        /* Stage 3 (shared mix stage); the wet is the balanced read. */
        right[n] = crumpBit_mixFrameGain(hr, gain_r, wet, mix);
        left[n] = crumpBit_mixFrameGain(hl, gain_l, wet, mix);
    }

    t->delay = delay;
    t->write_pos = wp;
    t->valid = valid;
    t->dc_x = dc_x;
    t->dc_y = dc_y;
    t->tone_s = tone_s;
    rt->ac_c_l = (int16_t)ac_cl;
    rt->ac_c_r = (int16_t)ac_cr;
    rt->ac_y_l = ac_yl;
    rt->ac_y_r = ac_yr;
    rt->pan_prev = rt->pan_raw;
}

```

##### E10.9 — `dly`: `crumpBit_runDelay()` (new lines 1109–1130)

- **What:** per block, the P2 target `crumpBit_targetSamples()`:
  - the free Rate from the table, or the Sync division at the tempo;
  - clamped to this loop.
  - Then the tape loop with the 150 ms glide and the P2 feedback curve
    (unity at 100, 1.2 at 127).
- **Why:** `dly` is the P2 sound, bit-exact (CB4).
- **Caller:** `crumpBit_process()` for sub 0.
- **Cost:** loop 124 (P2: 126), per block 166 + `divisionFor` 113.

```c
/*
 * `dly`: the S074/S091 tape delay (engine A).
 *
 * What: per block the Rate/Sync target clamped to this loop (the S091 P2
 * crumpBit_targetSamples()), the feedback ramp (S091 P2 curve, up to 1.2),
 * then the shared tape loop with the 150 ms glide.
 * Inputs: runtime, block. Outputs: as crumpBit_tapLoop(). Caller:
 * crumpBit_process() when the active sub-type is CRUMPBIT_SUBTYPE_DELAY.
 */
static CRUMPBIT_NOINLINE void crumpBit_runDelay(CrumpBitRuntime *rt,
                                                const crumpbit_block_t *b)
{
    const float target = crumpBit_targetSamples(
        rt->rate, rt->sync, seq_getBpm(),
        (float)(b->len - CRUMPBIT_LOOP_GUARD));

    crumpBit_tapLoop(rt, b, target, CRUMPBIT_GLIDE_K,
                     crumpBit_feedbackGain(rt->feedback_prev),
                     crumpBit_feedbackGain(rt->feedback_raw));
    rt->feedback_prev = rt->feedback_raw;
}

```

##### E10.10 — `cmb`: `crumpBit_runComb()` (new lines 1131–1159)

- **What:** engine A retuned (plan §3.1):
  - `rte` picks `crumpBit_combTable[rte]` (MIDI note 112 − rte, clamped
    at 96; L = fs/f), clamped to the loop;
  - the glide is 5 ms (`CRUMPBIT_COMB_GLIDE_K`), so an LFO on `rte`
    flanges;
  - `snc` on makes the feedback negative (odd harmonics, a hollow tone)
    by signing the two per-block gains, which costs 0 per frame.
  - With `ton` LP and `fbk` near 100 it plucks (Karplus-Strong); above
    100 it drones, bounded by the knee.
- **Callers:** `crumpBit_process()` for sub 1.
- **Proofs:** CB12: the first echo at L ± 2 for six notes; the second
  echo's sign follows `snc`.

```c
/*
 * `cmb`: comb / resonator / flanger (engine A, plan §3.1).
 *
 * What: the tape loop retuned: `rte` picks a comb length from
 * crumpBit_combTable (a higher `rte` is a longer comb, a lower note; A1),
 * clamped to this loop; the glide is fast (5 ms) so retuning and an Effects
 * LFO on `rte` follow; `snc` on makes the feedback negative (hollow, odd
 * harmonics) by signing the per-block gains (0 per sample). With `ton`
 * negative and `fbk` near 1 it plucks (Karplus-Strong); above 1 it drones,
 * bounded by the knee.
 * Inputs: runtime, block. Outputs: as crumpBit_tapLoop(). Caller:
 * crumpBit_process() for CRUMPBIT_SUBTYPE_COMB.
 */
static CRUMPBIT_NOINLINE void crumpBit_runComb(CrumpBitRuntime *rt,
                                               const crumpbit_block_t *b)
{
    const float max_samples = (float)(b->len - CRUMPBIT_LOOP_GUARD);
    const float length = crumpBit_combTable[(rt->rate > 127u) ? 127u
                                                              : rt->rate];
    /* sign: negative feedback while `snc` (polarity) is on. */
    const float sign = rt->sync ? -1.0f : 1.0f;

    crumpBit_tapLoop(rt, b, (length > max_samples) ? max_samples : length,
                     CRUMPBIT_COMB_GLIDE_K,
                     sign * crumpBit_feedbackGain(rt->feedback_prev),
                     sign * crumpBit_feedbackGain(rt->feedback_raw));
    rt->feedback_prev = rt->feedback_raw;
}

```

##### E10.11 — Pitch helpers: `crumpBit_rowSemitones()`, `crumpBit_ratioOf()` (new lines 1160–1196)

- **What:**
  - `rowSemitones(row)`: `row − 63` clamped to ±24. These are the `grn`
    pitches (`st1` = `aux`, `st2`, `st3`).
  - `ratioOf(s)`: 2^(s/12) for a fractional s in [−24, 24], by linear
    interpolation of `crumpBit_semitoneRatio`. The error is below 0.5 cent
    over a one-semitone interval; no `exp2f`.
- **Callers:** `crumpBit_runPitch()` (per block), `crumpBit_grainPick()`.

```c
/*
 * The `pit` interval and `grn` pitches: semitones from a bipolar row.
 *
 * crumpBit_rowSemitones(): row - 63 clamped to +-24 (the `grn` rows st1,
 * st2, st3 and the integer `pit` interval; aligned on `aux`, A1).
 * crumpBit_ratioOf(): 2^(s/12) for fractional s in [-24, 24], by linear
 * interpolation of crumpBit_semitoneRatio (error < 0.5 cent; no exp2f).
 */
static inline int32_t crumpBit_rowSemitones(uint8_t row)
{
    int32_t s = (int32_t)row - 63;

    s = (s > (int32_t)CRUMPBIT_SEMITONE_SPAN) ? (int32_t)CRUMPBIT_SEMITONE_SPAN
                                              : s;
    s = (s < -(int32_t)CRUMPBIT_SEMITONE_SPAN) ? -(int32_t)CRUMPBIT_SEMITONE_SPAN
                                               : s;
    return s;
}

static inline float crumpBit_ratioOf(float semitones)
{
    /* x: the table position, 0..48; i its integer part (<= 47). */
    float x = semitones + (float)CRUMPBIT_SEMITONE_SPAN;
    uint32_t i;
    float f;

    x = (x < 0.0f) ? 0.0f
      : ((x > (float)(2u * CRUMPBIT_SEMITONE_SPAN)) ?
         (float)(2u * CRUMPBIT_SEMITONE_SPAN) : x);
    i = (uint32_t)x;
    i = (i >= 2u * CRUMPBIT_SEMITONE_SPAN) ? 2u * CRUMPBIT_SEMITONE_SPAN - 1u
                                           : i;
    f = x - (float)i;
    return crumpBit_semitoneRatio[i] +
           f * (crumpBit_semitoneRatio[i + 1u] - crumpBit_semitoneRatio[i]);
}

```

##### E10.12 — Q20 conversion: `crumpBit_q20ToFloat()` (new lines 1197–1222)

- **What:** `x / 2^20` by VMOV plus one `VCVT.F32.S32 #20`. A Q13
  code-unit value read this way is `code/128`, the ±1 sample.
- **Host:** builds use the exact C equivalent.
- **Why:** the `pit` head mix ends in integer Q13; one scaling conversion
  replaces a conversion and a multiply.
- **Caller:** `crumpBit_runPitch()`.

```c
/*
 * Q20 fixed point to float (the `pit` head mix).
 *
 * What: returns x / 2^20. On the M7 this is VMOV plus one VCVT.F32.S32 with
 * 20 fractional bits: the conversion scales, no multiply (as
 * crumpBit_codeToFloat()). A Q13 code-unit value (code x 2^13) read this
 * way is code / 128, the normalised +-1 sample. Host builds use the C
 * equivalent.
 * Input: x, |x| < 2^24 for exactness (here |x| <= 2^20). Output: x * 2^-20.
 * Callers: crumpBit_runPitch().
 */
static inline float crumpBit_q20ToFloat(int32_t x)
{
#if defined(__arm__) && defined(__VFP_FP__) && !defined(__SOFTFP__)
    float f;

    __asm__("vmov %0, %1\n\t"
            "vcvt.f32.s32 %0, %0, #20"
            : "=t"(f)
            : "r"(x));
    return f;
#else
    return (float)x * (1.0f / 1048576.0f);
#endif
}

```

##### E10.13 — Q14 integer line read: `crumpBit_readQ14()` (new lines 1223–1255)

- **What:** an interpolated read at `rp = (wp << 14) − d` in Q14. The
  whole 126,656 B line fits a signed word (126,656·2¹⁴ < 2³¹), so a wrap
  below zero is the sign bit:
  - `rp += lim14 & (rp >> 31)`: one AND with the shifted sign, one ADD;
  - `i0 = rp >> 14` (always < len, because d ≤ len − 2);
  - `i1 = i0 + 1` wrapped at `len`;
  - result `s0·2¹⁴ + frac·(s1 − s0)` in Q14 code units (|v| ≤ 2²¹;
    `frac` is the low 14 bits).
- **Why:** the float read costs ~22–25 instructions per head (float/int
  conversions and a float wrap); this costs ~13.
- **Caller:** `crumpBit_headRead()`.

```c
/*
 * Interpolated read of an 8-bit line in Q14 integer arithmetic.
 *
 * What: the read position rp = (wp << 14) - d in Q14 (14 fractional bits;
 * the whole arena, 126,656 << 14 < 2^31, fits a signed word, so a wrap
 * below zero is the sign bit):
 * - rp += lim14 when negative (AND with the sign, ADD): the position
 *   wrapped into [0, len) (d <= len - 2, so one length suffices);
 * - i0 = rp >> 14 (< len by construction), i1 = i0 + 1 wrapped at len;
 * - the linear interpolation s0 2^14 + frac (s1 - s0) in Q14 code units
 *   (|value| <= 2^21; frac = the low 14 bits).
 * Why integer: the float read (crumpBit_lineRead()) spends ~25 instructions
 * on float/int conversions and a float wrap; this is ~13.
 * Inputs: line, its length and length << 14, rp. Output: the sample,
 * code x 2^14.
 * Callers: crumpBit_headRead() (`pit`).
 */
static inline int32_t crumpBit_readQ14(const int8_t *loop, uint32_t len,
                                       uint32_t lim14, int32_t rp)
{
    uint32_t i0;
    uint32_t i1;
    int32_t s0;

    rp += (int32_t)(lim14 & (uint32_t)(rp >> 31));
    i0 = (uint32_t)rp >> 14;
    i1 = i0 + 1u;
    i1 -= (i1 >= len) ? len : 0u;
    s0 = loop[i0];
    return (s0 << 14) +
           (int32_t)((uint32_t)rp & 0x3FFFu) * ((int32_t)loop[i1] - s0);
}

```

##### E10.14 — One `pit` head: `crumpBit_headRead()` (new lines 1256–1273)

- **What:** a head trails the write head by `guard + W·phase` samples.
  `wpg = (wp << 14) − guard` is computed once per frame; the high word of
  the 32 × 32 product `phase·w14` (one UMULL) is `W·phase` in Q14. Then
  `crumpBit_readQ14()`.
- **Inputs:** line, `len`, `lim14`, `wpg`, the 0.32 phase, `w14 = W·2¹⁴`.
- **Output:** the head sample in Q14.

```c
/*
 * One `pit` head: guard + W x phase behind the write head.
 *
 * What: rp = wpg - (phase x w14) >> 32, where wpg is (wp << 14) - guard
 * (one per frame, shared by both heads) and the high word of the 32 x 32
 * product (one UMULL) is W x phase in Q14; then crumpBit_readQ14().
 * Inputs: line, its length and length << 14, wpg, the phase word, w14 =
 * W x 2^14. Output: the head sample, code x 2^14.
 * Callers: crumpBit_runPitch() (heads A and B).
 */
static inline int32_t crumpBit_headRead(const int8_t *loop, uint32_t len,
                                        uint32_t lim14, int32_t wpg,
                                        uint32_t phase, uint32_t w14)
{
    return crumpBit_readQ14(loop, len, lim14,
                            wpg - (int32_t)(((uint64_t)phase * w14) >> 32));
}

```

##### E10.15 — `pit`: `crumpBit_runPitch()` (engine B) (new lines 1274–1434)

- **What:**
  - **Per block:**
    - the window W from `crumpBit_pitchWindow[rte]` (10–200 ms), clamped
      so both heads stay in the loop;
    - the interval from `aux`, `(aux − 63)·24/63` semitones (continuous),
      rounded to whole semitones while `snc` is on (S6);
    - the phase step `(1 − r)/W` turns per sample as a signed 0.32 word,
      one division per block. r > 1 makes it negative; the unsigned add
      then wraps backwards;
    - `w14`, `lim14`;
    - the gate as an integer mask: both heads read written samples only
      once `valid ≥ W + 4 + frames`;
    - the tone, feedback and balance ramps.
  - **Per frame:**
    1. stage 1 and the ramps;
    2. `phase += dphi` (wraps by overflow);
    3. head A's weight `wa = phase XOR (phase ASR 31)`, a 0.31 triangle:
       `2·phase` rising, then `2(1 − phase)`;
    4. the heads `ra` (phase) and `rb` (phase + ½ turn, one integer add);
    5. the crossfade `wA·A + (1 − wA)·B = B + wA(A − B)`, computed as one
       SMMLA (`(A − B)·wa >> 32`, then `+ B/2`, so Q13), masked by the
       gate, then one Q20 conversion;
    6. the shifted signal feeds the shared return chain (each repeat
       shifted again: shimmer) and is the wet, balanced by `dpn` as
       `dly`'s.
- **Why:**
  - It is a two-head rotating-tape shifter (plan §3.2): each head is
    silent at its jump, and the windows sum to 1.
  - Integer phase and heads took the loop from 157 to 135.
- **Outputs:** io, loop bytes, the `pit` state (0.32 phase), AC state,
  ramp origins.
- **Caller:** `crumpBit_process()` for sub 2.
- **Proofs:** CB13 (+12/−12 st move a 441 Hz sine's energy to 882 /
  220.5 Hz by ≥ 10×; 0 st keeps it); break 8 flips the step's sign.

```c
/*
 * `pit`: rotating-head pitch shifter (engine B, plan §3.2).
 *
 * What, per block: the window W from crumpBit_pitchWindow (`rte`, 10-200 ms,
 * higher = longer), the interval from `aux` (row 15, aligned with `grn`'s
 * root; continuous over +-24 st in 0.38 st steps, snapped to semitones while
 * `snc` is on: plan S6), the ratio r, and the phase step (1 - r)/W turns per
 * sample as a signed 0.32 word (one division per block); the wet path opens
 * once `valid` covers the window.
 * Per frame: the phase word advances and wraps by integer overflow; head A
 * trails the write head by 2 + W phase, head B by 2 + W (phase + 1/2) (the
 * half turn is one integer add, its wrap free); the triangular crossfade
 * wA = 1 - |2 phase - 1| is the phase word folded by its sign bit
 * (phase XOR (phase ASR 31)) read as a 0.31 fraction; wB = 1 - wA, so the
 * mix is B + wA (A - B) and the 1/128 scale and the gate are one per-block
 * factor. The shifted signal feeds the shared return chain into the loop
 * (each repeat shifted again: shimmer) and is the wet, balanced by `dpn` as
 * the delay's.
 * Inputs: runtime, block. Outputs: io, loop bytes, pitch state, AC state.
 * Caller: crumpBit_process() for CRUMPBIT_SUBTYPE_PITCH.
 */
static CRUMPBIT_NOINLINE void crumpBit_runPitch(CrumpBitRuntime *rt,
                                                const crumpbit_block_t *b)
{
    CrumpBitPitchState *ps = &rt->u.pit;
    int8_t *loop = b->base;
    const uint32_t len = b->len;
    const float flen = (float)len;
    /* window: the head sweep, clamped so both heads stay inside the loop. */
    float window = crumpBit_pitchWindow[(rt->rate > 127u) ? 127u : rt->rate];
    /* semis: the continuous interval, snapped while `snc` is on. */
    float semis;
    /* dphi: the signed phase step, (1 - r)/W turns in 0.32 (wraps). */
    uint32_t dphi;
    /* w14: W in Q14, so a head's distance is the high word of phase x w14. */
    uint32_t w14;
    /* lim14: the line length in Q14, added to a read that wrapped below 0. */
    const uint32_t lim14 = len << 14;
    /* gate: all ones while both heads read written samples, else zero. */
    int32_t gate;
    float fb = crumpBit_feedbackGain(rt->feedback_prev);
    const float fb_to = crumpBit_feedbackGain(rt->feedback_raw);
    float gain_l = crumpBit_panGainL(rt->pan_prev);
    float gain_r = crumpBit_panGainR(rt->pan_prev);
    const float fb_inc = (fb_to - fb) * b->per_frame;
    const float gain_l_inc =
        (crumpBit_panGainL(rt->pan_raw) - gain_l) * b->per_frame;
    const float gain_r_inc =
        (crumpBit_panGainR(rt->pan_raw) - gain_r) * b->per_frame;
    float mix = b->mix;
    float tone_k;
    float tone_a;
    float tone_b;
    uint32_t phase = ps->phase;
    float dc_x = ps->dc_x;
    float dc_y = ps->dc_y;
    float tone_s = ps->tone_s;
    int32_t ac_cl = rt->ac_c_l;
    int32_t ac_cr = rt->ac_c_r;
    float ac_yl = rt->ac_y_l;
    float ac_yr = rt->ac_y_r;
    uint32_t wp = ps->write_pos;
    uint32_t n;

    /*
     * Interval: `aux` is the bipolar pitch row (63 = 0). The continuous law
     * spreads +-24 st over 0..127 (24/63 st per step); `snc` rounds it.
     */
    semis = ((float)rt->aux_raw - 63.0f) * (24.0f / 63.0f);
    semis = (semis > 24.0f) ? 24.0f : ((semis < -24.0f) ? -24.0f : semis);
    semis = rt->sync ? (float)(int32_t)(semis + ((semis < 0.0f) ? -0.5f
                                                               : 0.5f))
                     : semis;
    window = (window > flen - 2.0f * CRUMPBIT_PITCH_GUARD)
        ? flen - 2.0f * CRUMPBIT_PITCH_GUARD : window;
    /*
     * (1 - r)/W turns per sample, at most 3/441 of a turn (r = 4, 10 ms),
     * so x 2^32 fits a signed word; the unsigned add then wraps the phase.
     */
    dphi = (uint32_t)(int32_t)((1.0f - crumpBit_ratioOf(semis)) / window *
                               4294967296.0f);
    w14 = (uint32_t)(window * 16384.0f);
    /* Per-block gate: both heads read written samples only (plan §2.7). */
    gate = ((float)ps->valid >= window + 2.0f * CRUMPBIT_PITCH_GUARD +
            (float)b->frames) ? -1 : 0;
    crumpBit_toneSetup(rt->tone_raw, &tone_k, &tone_a, &tone_b);

    /*
     * The block's fields as locals: an output store (float) may alias any
     * float reached through `b`, so reading them inside the loop would
     * reload them every frame.
     */
    float *const left = b->left;
    float *const right = b->right;
    const uint32_t frames = b->frames;
    const uint32_t keep = b->keep;
    const uint32_t invert = b->invert;
    const float mix_inc = b->mix_inc;

    for (n = 0u; n < frames; n++) {
        float hl;
        float hr;
        /* ra, rb: the two heads' reads, Q14 code units. */
        int32_t ra;
        int32_t rb;
        /* wa: head A's weight in Q31, 0 at its jump, ~1 half a turn on. */
        uint32_t wa;
        /* wpg: this frame's write position minus the guard, Q14. */
        const int32_t wpg = (int32_t)(wp << 14) -
                            (int32_t)(CRUMPBIT_PITCH_GUARD * 16384.0f);
        float shifted;

        crumpBit_stage1(left[n], right[n], keep, invert,
                        &ac_cl, &ac_cr, &ac_yl, &ac_yr, &hl, &hr);
        mix += mix_inc;
        fb += fb_inc;
        gain_l += gain_l_inc;
        gain_r += gain_r_inc;
        /* Head phase: the 32-bit add wraps the turn (either direction). */
        phase += dphi;
        /*
         * Triangle fold: below half a turn the phase itself, above it ~phase,
         * so the 0.31 reading is 2 phase rising, then 2 (1 - phase) falling.
         */
        wa = phase ^ (uint32_t)((int32_t)phase >> 31);
        ra = crumpBit_headRead(loop, len, lim14, wpg, phase, w14);
        rb = crumpBit_headRead(loop, len, lim14, wpg, phase + 0x80000000u,
                               w14);
        /*
         * wA A + (1 - wA) B = B + wA (A - B), halved into Q13: (A - B) x wa
         * >> 32 is (A - B) wA / 2 (one SMMLA with B / 2), then the gate
         * mask and one scaling VCVT (Q13 code units x 2^-7 = Q20).
         */
        shifted = crumpBit_q20ToFloat(
            ((int32_t)(((int64_t)(ra - rb) * (int32_t)wa) >> 32) +
             (rb >> 1)) & gate);
        loop[wp] = (int8_t)((int32_t)crumpBit_adc(
            0.5f * (hl + hr) + crumpBit_returnChain(shifted, &dc_x, &dc_y,
                                                    &tone_s, tone_k, tone_a,
                                                    tone_b, fb)) - 128);
        wp++;
        wp -= (wp >= len) ? len : 0u;
        right[n] = crumpBit_mixFrameGain(hr, gain_r, shifted, mix);
        left[n] = crumpBit_mixFrameGain(hl, gain_l, shifted, mix);
    }

    ps->phase = phase;
    ps->write_pos = wp;
    /* valid advances per block (the gate needs no per-sample count). */
    ps->valid = (ps->valid + b->frames < len) ? ps->valid + b->frames : len;
    ps->dc_x = dc_x;
    ps->dc_y = dc_y;
    ps->tone_s = tone_s;
    rt->ac_c_l = (int16_t)ac_cl;
    rt->ac_c_r = (int16_t)ac_cr;
    rt->ac_y_l = ac_yl;
    rt->ac_y_r = ac_yr;
    rt->feedback_prev = rt->feedback_raw;
    rt->pan_prev = rt->pan_raw;
}

```

##### E10.16 — `grn` candidate: `crumpbit_grain_t`, `crumpBit_hashNext()`, `crumpBit_grainPick()` (new lines 1435–1564)

- **What:** one restart candidate, as a pure function of the block's
  hashed RNG word and the rows. That makes its distributions host-testable
  (CB14).
  - **Five uniforms** from a xorshift32 expansion of the block's single
    `GetRngValue()` word (G9). Back-to-back hardware reads repeat, so the
    hash supplies the rest.
  - **Pitch slot by `spr`:** weights `1 : a : b`:
    - a = min(spr, 40)/40, b = clamp((spr − 40)/40, 0, 1);
    - root only at 0, root/`st2` halves at 40, equal thirds at 80;
    - above 80 a uniform detune of ±(spr − 80)·35/47 cents
      (±35 at 127, G8), applied through the linear cent slope;
    - `st2`, `st3` are relative to the root `st1` = `aux`, clamped to
      ±24.
  - **Step:** `inc = ±round(r·4096)` (Q3.12); the sign is the sign of
    `sze` (left of 63 = reversed, G5). The played ratio is then re-read
    from `inc` (choice 9).
  - **Start lag (`pos` mode = `dpn`):**
    - 63 follow: just behind the head;
    - right spray: uniform over a growing share of [lag_min, lag_max];
    - left slice: a random boundary among the most recent `R/G` slices.
    - `lag_min = 2 + G(r − 1)` for a forward grain above unity: the
      headroom rule, so it never overtakes the record head.
    - `lag_max = min(R, valid − 2)`, less a reversed grain's walk `G·r`.
    - The start index is `wp − lag`, wrapped into the line.
  - **Level byte:** `255(1 − amp/127·u)` (`amp` jitter).
  - **Pan byte:** from the fixed lean (A ¼, B ¾) towards uniform random by
    `wid/127`.
- **Inputs:** hash state, grain (0 A, 1 B), rows, `len`, `wp`, R, G,
  `valid`.
- **Output:** `start`, `inc`, `amp`, `pan`.
- **Callers:** `crumpBit_runGrain()` (once per block), host CB14.

```c
/*
 * One grain restart candidate (plan §3.3.3-§3.3.5).
 *
 * What: a pure function of the block's hashed random word and the rows, so
 * the host can test its distributions directly:
 * - start lag behind the write head by the `pos` mode (`dpn`): 63 follow
 *   (just behind the head), right spray (uniform over an increasing span of
 *   R), left slice (a random slice boundary of R/N, N = R/G, choosing among
 *   more slices as the knob moves left);
 * - the headroom rule: a forward grain at r > 1 starts at least
 *   G (r - 1) + 2 behind the head; any grain stays inside R and `valid`;
 * - the pitch slot by `spr`: weights (1 : a : b), a = min(spr, 40)/40,
 *   b = clamp((spr - 40)/40, 0, 1): root only at 0, half `st2` at 40, equal
 *   thirds at 80; above 80 a random detune of up to +-35 cents;
 * - the direction from the sign of `sze` (left of 63 = reversed);
 * - the level (`amp`: down to silent at 127) and pan (`wid`: from the fixed
 *   lean, A left / B right, to fully random).
 * Inputs: hash word, grain index (0 A, 1 B), rows, the seated line length,
 * write position, R, G, valid, frozen flag. Output: *out.
 * Callers: crumpBit_runGrain() (twice per block), the host test (CB14).
 */
typedef struct {
    uint32_t start;   /* record-line index the grain starts at */
    int16_t inc;      /* Q3.12 pitch step, negative = reversed */
    uint8_t amp;      /* level 0..255 */
    uint8_t pan;      /* 0 = left .. 255 = right */
} crumpbit_grain_t;

static inline uint32_t crumpBit_hashNext(uint32_t *h)
{
    /* xorshift32: the block's one hardware word expanded (G9). */
    *h ^= *h << 13;
    *h ^= *h >> 17;
    *h ^= *h << 5;
    return *h;
}

static void crumpBit_grainPick(uint32_t *hash, uint32_t grain,
                               const CrumpBitRuntime *rt, uint32_t len,
                               uint32_t wp, uint32_t window, uint32_t grain_len,
                               uint32_t valid, crumpbit_grain_t *out)
{
    /* u: uniform fractions in [0, 1) from successive hash words. */
    const float u_pos = (float)(crumpBit_hashNext(hash) >> 8) * (1.0f / 16777216.0f);
    const float u_slot = (float)(crumpBit_hashNext(hash) >> 8) * (1.0f / 16777216.0f);
    const float u_tune = (float)(crumpBit_hashNext(hash) >> 8) * (1.0f / 16777216.0f);
    const float u_amp = (float)(crumpBit_hashNext(hash) >> 8) * (1.0f / 16777216.0f);
    const float u_pan = (float)(crumpBit_hashNext(hash) >> 8) * (1.0f / 16777216.0f);
    const int32_t pos_mode = (int32_t)rt->pan_raw - 63;
    const uint8_t spr = rt->grain_spread;
    const float a = (spr >= 40u) ? 1.0f : (float)spr * (1.0f / 40.0f);
    const float bw = (spr <= 40u) ? 0.0f
                   : ((spr >= 80u) ? 1.0f : (float)(spr - 40u) * (1.0f / 40.0f));
    const float pick = u_slot * (1.0f + a + bw);
    const int32_t root = crumpBit_rowSemitones(rt->aux_raw);
    int32_t semis;
    float cents;
    float ratio;
    float lag_min;
    float lag_max;
    float lag;
    float pan_base;

    /* Pitch slot: root, then st2, then st3, relative to the root. */
    semis = (pick < 1.0f) ? root
          : ((pick < 1.0f + a) ? root + crumpBit_rowSemitones(rt->grain_st2)
                               : root + crumpBit_rowSemitones(rt->grain_st3));
    semis = (semis > (int32_t)CRUMPBIT_SEMITONE_SPAN)
        ? (int32_t)CRUMPBIT_SEMITONE_SPAN : semis;
    semis = (semis < -(int32_t)CRUMPBIT_SEMITONE_SPAN)
        ? -(int32_t)CRUMPBIT_SEMITONE_SPAN : semis;
    /* Detune above `spr` 80: uniform in +-d, d up to 35 cents at 127. */
    cents = (spr > 80u)
        ? (2.0f * u_tune - 1.0f) * (float)(spr - 80u) *
          (CRUMPBIT_GRAIN_DETUNE_CENTS / 47.0f)
        : 0.0f;
    ratio = crumpBit_semitoneRatio[semis + (int32_t)CRUMPBIT_SEMITONE_SPAN] *
            (1.0f + cents * CRUMPBIT_CENT_LIN);
    /* Direction: the sign of `sze` (user G5). */
    out->inc = (int16_t)((rt->tone_raw < CRUMPBIT_TONE_FLAT) ?
                         -(int32_t)(ratio * 4096.0f + 0.5f)
                         : (int32_t)(ratio * 4096.0f + 0.5f));
    /*
     * The ratio the grain actually plays: the Q3.12 step (rounded above).
     * The headroom rule below must use it; at G ~ 63k frames the rounding
     * (up to 1/8192) moves the walk by ~8 samples.
     */
    ratio = (float)((out->inc < 0) ? -out->inc : out->inc) *
            (1.0f / 4096.0f);

    /*
     * Start lag: forward grains above unity need headroom so they never
     * overtake the record head (while recording); reversed grains walk
     * back, so they must start far enough inside `valid`.
     */
    lag_min = 2.0f + ((ratio > 1.0f && out->inc > 0)
                      ? (float)grain_len * (ratio - 1.0f) : 0.0f);
    lag_max = (float)window;
    lag_max = (lag_max > (float)valid - 2.0f) ? (float)valid - 2.0f : lag_max;
    lag_max = (out->inc < 0) ? lag_max - (float)grain_len * ratio : lag_max;
    lag_max = (lag_max < lag_min) ? lag_min : lag_max;
    if (pos_mode > 0) {
        /* Spray: uniform over an increasing share of the window. */
        lag = lag_min + u_pos * (float)pos_mode * (1.0f / 64.0f) *
              (lag_max - lag_min);
    } else if (pos_mode < 0) {
        /* Slice: a random slice boundary among the most recent slices. */
        const float slices = (float)window / (float)grain_len;
        const float reach = slices * (float)(-pos_mode) * (1.0f / 63.0f);
        lag = lag_min + (float)(uint32_t)(u_pos * reach) * (float)grain_len;
        lag = (lag > lag_max) ? lag_max : lag;
    } else {
        /* Follow: just behind the head. */
        lag = lag_min;
    }
    {
        /* start: wp - lag, wrapped into the record line. */
        uint32_t il = (uint32_t)lag;

        il = (il >= len) ? len - 1u : il;
        out->start = (wp >= il) ? wp - il : wp + len - il;
    }
    /* Level and pan, quantised to the stored bytes before use. */
    out->amp = (uint8_t)(255.0f * (1.0f - (float)rt->grain_amp *
                                          (1.0f / 127.0f) * u_amp) + 0.5f);
    pan_base = grain ? 0.75f : 0.25f;
    out->pan = (uint8_t)(255.0f * (pan_base + (float)rt->grain_width *
                                  (1.0f / 127.0f) * (u_pan - pan_base)) + 0.5f);
}

```

##### E10.17 — `grn` loop state and constants: `crumpbit_grain_run_t`, `crumpbit_grain_k_t` (new lines 1565–1624)

- **What:**
  - **The run state (one frame reads and writes all of it):**
    - write index;
    - both 16.16 positions, their steps (`inc·16`) and wrap amounts (±lim
      by direction);
    - both window phases;
    - four gains (level × pan × gate);
    - the delay line's DC-blocker memories;
    - the feedback and mix ramps;
    - the AC memories.
  - **The block constants:**
    - io pointers;
    - the record write base `rec_w` (the record line, or the delay line
      while frozen);
    - the read bases;
    - `len` and `lim = len << 16`;
    - the masks;
    - the delay tap `d = R` in whole samples;
    - the window step `2/active`;
    - the ramp steps;
    - the tap's gate mask.
- **Why:** both live on `crumpBit_runGrain()`'s stack and pass by pointer
  only to the inline span, so the compiler keeps the fields in registers.
  The two spans then share one body.

```c
/*
 * The `grn` frame loop's state and per-block constants (engine B').
 *
 * What: crumpbit_grain_run_t holds everything one frame reads and writes
 * (positions, steps, wraps, window phases, gains, the delay line's return
 * memories, the ramps, the AC memories); crumpbit_grain_k_t the
 * block's constants. Both live on the stack of crumpBit_runGrain() and are
 * passed by pointer only to the inline crumpBit_grainSpan(), so the compiler
 * keeps their fields in registers (scalar replacement after inlining).
 * Why: the frame loop runs as two spans split at the one grain restart a
 * block can hold (grain >= 64 frames, restarts half a grain apart, block <=
 * 32 frames: OUTPUT_DMA_SIZE). Between the spans the restarting grain takes
 * its candidate once, so no frame pays for restart selects.
 * Accessors: crumpBit_runGrain(), crumpBit_grainSpan().
 */
typedef struct {
    uint32_t wp;        /* shared write index of both lines */
    uint32_t pos_a;     /* grain A read position, 16.16 */
    uint32_t pos_b;     /* grain B read position, 16.16 */
    uint32_t step_a;    /* pos_a step per frame: inc x 16 (two's complement) */
    uint32_t step_b;
    uint32_t wrap_a;    /* subtracted when pos_a leaves [0, lim): lim, or
                           -lim (an unsigned underflow) for a reversed grain */
    uint32_t wrap_b;
    int32_t ac_cl;      /* stage 1 previous codes */
    int32_t ac_cr;
    float fa;           /* grain A window phase: 2 age / active length */
    float fb_w;         /* grain B window phase */
    float gla;          /* grain A left gain: level x (1 - pan) x gate */
    float gra;          /* grain A right gain: level x pan x gate */
    float glb;
    float grb;
    float dc_x;         /* delay-line return DC blocker */
    float dc_y;
    float fb;           /* feedback ramp */
    float mix;          /* mix stage ramp */
    float ac_yl;        /* stage 1 AC outputs */
    float ac_yr;
} crumpbit_grain_run_t;

typedef struct {
    float *left;
    float *right;
    int8_t *rec_w;      /* record-line write base: the record line, or the
                           delay line while frozen (overwritten the same
                           frame), so freezing costs no per-frame select */
    const int8_t *rec;  /* record line (grain reads) */
    int8_t *dline;      /* delay line */
    uint32_t len;
    uint32_t lim;       /* len << 16, the 16.16 line end */
    uint32_t keep;
    uint32_t invert;
    uint32_t d;         /* delay-line tap D = R, whole samples (2..len-2) */
    float wstep;        /* window phase step per frame: 2 / active length */
    float fb_inc;
    float mix_inc;
    int32_t gate;       /* the delay tap's gate mask: all ones or zero (the
                           grains' gate is in their gains) */
} crumpbit_grain_k_t;

```

##### E10.18 — `grn` frame loop: `crumpBit_grainSpan()` (new lines 1625–1719)

- **What, per frame over [n0, n1), every statement every frame:**
  1. stage 1, the mix and feedback ramps;
  2. the record write `rec_w[wp] = adc(½(hL + hR)) − 128` (frozen: lands
     on the delay slot and is overwritten below; costs no select);
  3. each grain's drop-sample read `rec[pos >> 16]` (G6), then
     `pos += step` and one compare-and-subtract wrap;
  4. each grain's triangular window `w = max(0, min(f, 2 − f))` over the
     density's active part (f rises 0..2, silent beyond), `f += wstep`;
  5. the windowed samples (`VCVT #7`), panned and levelled into stereo
     grains;
  6. the delay line: the code exactly D behind the write head (one
     subtract and compare wrap, no glide, no interpolation, by user
     choice), gate-masked;
  7. the return chain with a flat tone, re-recording the grains' mono sum
     plus the return;
  8. `wp` advances;
  9. the mix stage with `wet = grains + tap`.
- **Inputs:** run state (in/out), constants, the span.
- **Outputs:** io, both lines, the run state.
- **Caller:** `crumpBit_runGrain()`, inlined twice (spans 1 and 2:
  125/131 per frame).

```c
/*
 * The `grn` frame loop over frames [n0, n1) (plan §3.3).
 *
 * What, per frame, every statement every frame:
 * - stage 1 (shared);
 * - the record line takes the dry mono code (through rec_w: frozen writes
 *   land on the delay line's slot, overwritten below);
 * - each grain reads its record sample drop-sample at pos >> 16 (G6), then
 *   pos += step and wraps by one compare (wrap = +-lim by direction);
 * - each grain's triangular window w = max(0, min(f, 2 - f)) over the
 *   density's active part (f = 2 age / active length, rising 0..2; beyond 2
 *   silent), f += wstep;
 * - grain samples scaled by the window (the scaling VCVT gives +-1), panned
 *   and levelled (gate folded into the gains) into the stereo grain signal;
 * - the delay line: one integer read exactly D behind the write head (no
 *   glide, no interpolation: user, 2026-11-04), gated, then the return
 *   chain (no tone stage) re-recording the grains' mono sum;
 * - stage 3: wet = grains + the delay tap, through the shared mix stage.
 * Inputs: run state (in/out), block constants, the span. Outputs: io
 * samples, both lines, the run state.
 * Callers: crumpBit_runGrain() (two spans; inlined into each).
 */
static inline void crumpBit_grainSpan(crumpbit_grain_run_t *s,
                                      const crumpbit_grain_k_t *k,
                                      uint32_t n0, uint32_t n1)
{
    uint32_t n;

    for (n = n0; n < n1; n++) {
        float hl;
        float hr;
        /* wa, wb: the grains' window values for this frame. */
        float wa;
        float wb;
        /* sa, sb: the grains' windowed samples. */
        float sa;
        float sb;
        float grains_l;
        float grains_r;
        float tap;
        int32_t ca;
        int32_t cb;

        crumpBit_stage1(k->left[n], k->right[n], k->keep, k->invert,
                        &s->ac_cl, &s->ac_cr, &s->ac_yl, &s->ac_yr, &hl, &hr);
        s->mix += k->mix_inc;
        s->fb += k->fb_inc;
        /* Record line: the dry mono code (frozen: the delay slot, reused). */
        k->rec_w[s->wp] = (int8_t)((int32_t)crumpBit_adc(0.5f * (hl + hr)) -
                                   128);
        /* Drop-sample reads (G6), then the 16.16 step and the wrap. */
        ca = k->rec[s->pos_a >> 16];
        cb = k->rec[s->pos_b >> 16];
        s->pos_a += s->step_a;
        s->pos_a -= (s->pos_a >= k->lim) ? s->wrap_a : 0u;
        s->pos_b += s->step_b;
        s->pos_b -= (s->pos_b >= k->lim) ? s->wrap_b : 0u;
        /* Triangular windows: rising f, falling 2 - f, silent past 2. */
        wa = (s->fa < 2.0f - s->fa) ? s->fa : 2.0f - s->fa;
        wb = (s->fb_w < 2.0f - s->fb_w) ? s->fb_w : 2.0f - s->fb_w;
        wa = (wa < 0.0f) ? 0.0f : wa;
        wb = (wb < 0.0f) ? 0.0f : wb;
        s->fa += k->wstep;
        s->fb_w += k->wstep;
        sa = crumpBit_codeToFloat(ca) * wa;
        sb = crumpBit_codeToFloat(cb) * wb;
        grains_l = sa * s->gla + sb * s->glb;
        grains_r = sa * s->gra + sb * s->grb;
        /*
         * Delay line: the sample D behind the write head (index wp - D,
         * wrapped by compare: an unsigned underflow exceeds wp), the gate as
         * a mask on the integer code, +-1 by the scaling VCVT.
         */
        {
            uint32_t ri = s->wp - k->d;

            ri += (ri > s->wp) ? k->len : 0u;
            tap = crumpBit_codeToFloat((int32_t)k->dline[ri] & k->gate);
        }
        {
            float tone_s = 0.0f;

            k->dline[s->wp] = (int8_t)((int32_t)crumpBit_adc(
                0.5f * (grains_l + grains_r) +
                crumpBit_returnChain(tap, &s->dc_x, &s->dc_y, &tone_s, 1.0f,
                                     0.0f, 1.0f, s->fb)) - 128);
        }
        s->wp++;
        s->wp -= (s->wp >= k->len) ? k->len : 0u;
        /* Stage 3: wet = grains + their echoes. */
        k->right[n] = crumpBit_mixFrame(hr, grains_r + tap, s->mix);
        k->left[n] = crumpBit_mixFrame(hl, grains_l + tap, s->mix);
    }
}

```

##### E10.19 — `grn`: `crumpBit_runGrain()` (engine B′) (new lines 1720–1920)

- **What, per block:**
  - the line = half the share; the delay line starts at `base + len`;
  - R = the free Rate snapped to the nearest step division that fits a
    line (Sync rule always on, G1), clamped to [128, len − 2]; D = R;
  - G = R·`crumpBit_grainFraction[|sze − 63|]` (1/32..1), at least 64;
  - the age of the first frame, and the frames until A (age 0) and B (age
    G − ⌊G/2⌋) restart. The earlier one is the split frame and `which`;
    only one restart can fall in a block;
  - the active length `G·(0.25 + 0.75·den/127)` and the window step;
  - the gate: open once `valid ≥ min(len, R + 4G + frames)`, so reads stay
    inside written samples;
  - ONE candidate for the next-restarting grain from one hardware word,
    drawn every block whether used or not (constant cost);
  - the run state from the stored state, the gains from the stored bytes
    × gate.
- **Per frame:** span 1 up to the split. If the split is inside the
  block, the restarting grain takes the candidate (position, step, wrap,
  gains, window phase 0, so the jump is silent) and its bytes are stored.
  Then span 2 to the block end.
- **After the loop:** state stored; age = (age + frames) mod G; `valid`
  advances per block.
- **Caller:** `crumpBit_process()` for sub 3.
- **Proofs:**
  - CB14 (distributions, start lags, freeze keeps the record line
    bit-exact while the delay line runs, stability at `fbk` 127);
  - CB11 (positions stay inside the line under switch storms);
  - break 7 ignores freeze.

```c
/*
 * `grn`: pitched granular record line plus a delay line (engine B', §3.3).
 *
 * What, per block: one line = half the share; the time R = D is the free
 * Rate (crumpBit_rateTable) snapped to the nearest step division that fits a
 * line (the delay's Sync rule, always on: G1); the grain size G = R x
 * crumpBit_grainFraction[|sze - 63|] (>= 64 frames); the density's active
 * length (25..100 % of G); the per-block gate; the frame of the block's one
 * possible grain restart (A at age 0, B at age G - G/2) and which grain it
 * is; ONE restart candidate (crumpBit_grainPick from one hardware RNG word)
 * for that grain, drawn every block whether it restarts or not (constant
 * cost; an unused draw is discarded).
 * Per frame (crumpBit_grainSpan(), two spans around the restart): see there.
 * Inputs: runtime, block. Outputs: io, both lines, grain state, AC state.
 * Caller: crumpBit_process() for CRUMPBIT_SUBTYPE_GRAIN.
 */
static CRUMPBIT_NOINLINE void crumpBit_runGrain(CrumpBitRuntime *rt,
                                                const crumpbit_block_t *b)
{
    CrumpBitGrainState *g = &rt->u.grn;
    const uint32_t len = b->len;
    const float flen = (float)len;
    const float per_tick = CRUMPBIT_SAMPLES_PER_TICK /
                           (float)(seq_getBpm() ? seq_getBpm() : 1u);
    crumpbit_grain_k_t k;
    crumpbit_grain_run_t s;
    float window;
    uint32_t grain_len;
    /* b_at: grain B's restart age (half a grain after A's). */
    uint32_t b_at;
    uint32_t age;
    /* first: the age of this block's first frame. */
    uint32_t first;
    /* n_a, n_b: frames until A / B restart; split: the earlier, <= frames. */
    uint32_t n_a;
    uint32_t n_b;
    uint32_t split;
    /* which: the grain that restarts next (0 A, 1 B). */
    uint32_t which;
    float active;
    float gate;
    uint32_t hash;
    crumpbit_grain_t cand;

    k.len = len;
    k.lim = len << 16;
    k.rec = b->base;
    k.dline = b->base + len;
    /* Freeze (`snc`): the record write is redirected, not skipped. */
    k.rec_w = rt->sync ? k.dline : b->base;
    k.left = b->left;
    k.right = b->right;
    k.keep = b->keep;
    k.invert = b->invert;
    k.mix_inc = b->mix_inc;

    /* Time R = D: the free Rate snapped to a division that fits a line. */
    window = crumpBit_divisionLength(
        crumpBit_divisionFor(crumpBit_rateSamples(rt->rate), per_tick,
                             flen - 2.0f), per_tick);
    window = (window > flen - 2.0f) ? flen - 2.0f : window;
    window = (window < (float)(2u * CRUMPBIT_GRAIN_MIN))
        ? (float)(2u * CRUMPBIT_GRAIN_MIN) : window;
    k.d = (uint32_t)window;
    /* Grain size: a fraction of R by |sze - 63| (1/32 .. 1), >= 64. */
    {
        const int32_t d = (int32_t)rt->tone_raw - 63;
        const uint32_t ad = (uint32_t)((d < 0) ? -d : d);

        grain_len = (uint32_t)(window * crumpBit_grainFraction[
            (ad > CRUMPBIT_GRAIN_STEPS) ? CRUMPBIT_GRAIN_STEPS : ad]);
        grain_len = (grain_len < CRUMPBIT_GRAIN_MIN) ? CRUMPBIT_GRAIN_MIN
                                                     : grain_len;
    }
    b_at = grain_len - grain_len / 2u;
    age = g->age;
    age = (age >= grain_len) ? age % grain_len : age;
    first = age + 1u;
    first -= (first >= grain_len) ? grain_len : 0u;
    /* Frame n has age (age + n + 1) mod G: A restarts at 0, B at b_at. */
    n_a = grain_len - 1u - age;
    n_b = b_at + grain_len - 1u - age;
    n_b -= (n_b >= grain_len) ? grain_len : 0u;
    which = (n_b < n_a) ? 1u : 0u;
    split = which ? n_b : n_a;
    split = (split > b->frames) ? b->frames : split;
    /* Density: the active part of each grain period, 25 .. 100 %. */
    active = (float)grain_len *
             (0.25f + 0.75f * (float)rt->grain_density * (1.0f / 127.0f));
    k.wstep = 2.0f / active;
    /*
     * Gate: the farthest read is the window plus a reversed grain's walk
     * (up to 4 G at ratio 4); once the line is full every byte is written.
     */
    {
        float need = window + 4.0f * (float)grain_len + (float)b->frames;

        need = (need > flen) ? flen : need;
        gate = ((float)g->valid >= need) ? 1.0f : 0.0f;
    }
    k.gate = (gate != 0.0f) ? -1 : 0;
    /* One hardware RNG word per block, expanded by the hash (G9). */
    hash = ((uint32_t)(uint16_t)GetRngValue() << 16) ^ 0x9E3779B9u ^
           g->write_pos;
    hash = (hash == 0u) ? 0x1234567u : hash;
    crumpBit_grainPick(&hash, which, rt, len, g->write_pos, (uint32_t)window,
                       grain_len, g->valid, &cand);

    /* The run state from the stored grain state. */
    s.wp = g->write_pos;
    s.pos_a = g->pos_a;
    s.pos_b = g->pos_b;
    s.step_a = (uint32_t)((int32_t)g->inc_a * 16);
    s.step_b = (uint32_t)((int32_t)g->inc_b * 16);
    s.wrap_a = (g->inc_a < 0) ? 0u - k.lim : k.lim;
    s.wrap_b = (g->inc_b < 0) ? 0u - k.lim : k.lim;
    s.fa = k.wstep * (float)first;
    {
        uint32_t first_b = first + grain_len / 2u;

        first_b -= (first_b >= grain_len) ? grain_len : 0u;
        s.fb_w = k.wstep * (float)first_b;
    }
    /* Per-grain gains (level x pan x gate), from the stored bytes. */
    s.gla = (float)g->amp_a * (1.0f / 255.0f) *
            (1.0f - (float)g->pan_a * (1.0f / 255.0f)) * gate;
    s.gra = (float)g->amp_a * (1.0f / 255.0f) *
            ((float)g->pan_a * (1.0f / 255.0f)) * gate;
    s.glb = (float)g->amp_b * (1.0f / 255.0f) *
            (1.0f - (float)g->pan_b * (1.0f / 255.0f)) * gate;
    s.grb = (float)g->amp_b * (1.0f / 255.0f) *
            ((float)g->pan_b * (1.0f / 255.0f)) * gate;
    s.dc_x = g->dc_x;
    s.dc_y = g->dc_y;
    s.fb = crumpBit_feedbackGain(rt->feedback_prev);
    k.fb_inc = (crumpBit_feedbackGain(rt->feedback_raw) - s.fb) *
               b->per_frame;
    s.mix = b->mix;
    s.ac_cl = rt->ac_c_l;
    s.ac_cr = rt->ac_c_r;
    s.ac_yl = rt->ac_y_l;
    s.ac_yr = rt->ac_y_r;

    /* Span 1: up to the restart frame (all frames when none). */
    crumpBit_grainSpan(&s, &k, 0u, split);
    if (split < b->frames) {
        /*
         * The restart: the grain takes its candidate on frame `split`; its
         * window phase is 0 there (age 0), so the jump is silent.
         */
        const float gl = (float)cand.amp * (1.0f / 255.0f) *
                         (1.0f - (float)cand.pan * (1.0f / 255.0f)) * gate;
        const float gr = (float)cand.amp * (1.0f / 255.0f) *
                         ((float)cand.pan * (1.0f / 255.0f)) * gate;
        const uint32_t pos = cand.start << 16;
        const uint32_t step = (uint32_t)((int32_t)cand.inc * 16);
        const uint32_t wrap = (cand.inc < 0) ? 0u - k.lim : k.lim;

        if (which) {
            s.pos_b = pos;
            s.step_b = step;
            s.wrap_b = wrap;
            s.glb = gl;
            s.grb = gr;
            s.fb_w = 0.0f;
            g->inc_b = cand.inc;
            g->amp_b = cand.amp;
            g->pan_b = cand.pan;
        } else {
            s.pos_a = pos;
            s.step_a = step;
            s.wrap_a = wrap;
            s.gla = gl;
            s.gra = gr;
            s.fa = 0.0f;
            g->inc_a = cand.inc;
            g->amp_a = cand.amp;
            g->pan_a = cand.pan;
        }
    }
    /* Span 2: from the restart to the block end (empty when none). */
    crumpBit_grainSpan(&s, &k, split, b->frames);

    g->write_pos = (uint16_t)s.wp;
    g->valid = (uint16_t)((g->valid + b->frames < len) ? g->valid + b->frames
                                                       : len);
    /* The age of the block's last frame: (age + frames) mod G. */
    age += b->frames;
    age -= (age >= grain_len) ? grain_len : 0u;
    g->age = (uint16_t)age;
    g->pos_a = s.pos_a;
    g->pos_b = s.pos_b;
    g->dc_x = s.dc_x;
    g->dc_y = s.dc_y;
    rt->ac_c_l = (int16_t)s.ac_cl;
    rt->ac_c_r = (int16_t)s.ac_cr;
    rt->ac_y_l = s.ac_yl;
    rt->ac_y_r = s.ac_yr;
    rt->feedback_prev = rt->feedback_raw;
}

```

##### E10.20 — `rvb` gain helper: `crumpBit_exp2Neg()` (new lines 1921–1938)

- **What:** 2^x for x ≤ 0: the integer part as exponent bits, the
  fraction by a cubic (error < 0.01 %). Clamped at −126.
- **Why:** four line gains per block without `exp2f`.
- **Caller:** `crumpBit_runReverb()`.

```c
/*
 * Approximate 2^x for x <= 0 (the `rvb` line gains; no exp2f).
 * Integer part by exponent bits, fraction by a cubic (error < 0.01 %).
 */
static inline float crumpBit_exp2Neg(float x)
{
    union { float f; uint32_t u; } v;
    int32_t i;
    float f;

    x = (x < -126.0f) ? -126.0f : x;
    i = (int32_t)x;
    i -= (x < (float)i) ? 1 : 0;
    f = x - (float)i;
    v.u = (uint32_t)(i + 127) << 23;
    return v.f * (1.0f + f * (0.6951786f + f * (0.2261261f + f * 0.0782401f)));
}

```

##### E10.21 — `rvb`: `crumpBit_runReverb()` (engine C) (new lines 1939–2120)

- **What:**
  - **Per block:**
    - the share as 32-bit words, one per sample, each carrying the four
      lines' 8-bit values (one lane per line);
    - delays `d_k = base_k·size(rte)`, integer, clamped to [1, len − 1];
    - the gate (`valid ≥ d3 + frames`);
    - gains `g_k = 2^(lg·d_k)` with `lg = crumpBit_reverbDecay[fbk]` (RT60
      0.1–10 s), or the freeze value with the input muted while `snc` is
      on;
    - the gains carry the Hadamard's ½, the gate and the ADC's ×128;
    - the damping tilt (`ton`);
    - the width (`dpn`: 0 mono, 1 at 63, ~2 at 127) folded with ¼ and the
      gate into `mid_k` and `side_k`.
  - **Per frame:**
    1. stage 1, the mix ramp;
    2. `xp, xm = 128.5 ± in·(hL + hR)` on the code axis;
    3. four taps: index `wp − d_k` (compare wrap), one word load, the
       lane by shift/`SBFX`, ±1 by `VCVT #7`;
    4. four one-pole damping tilts;
    5. a 4 × 4 Hadamard (`h01`, `h01d`, `h23`, `h23d`);
    6. four `USAT #8` codes of `x ± g_k·(…)`, packed by ORR with shifted
       operands;
    7. one XOR `0x80808080` turns them into signed lanes, one store;
    8. the wet: `mid = mid_k(e + o)` and `side = side_k(e − o)` (e, o the
       even/odd line sums) through the mix stage.
- **Why:**
  - A lo-fi 8-bit FDN (S4) in one word access per line.
  - The folds took the loop from 162 to 145.
- **Caller:** `crumpBit_process()` for sub 4.
- **Proofs:** CB15: 32 extreme combinations stay finite and < 8; an RT60
  of 0.49 s decays 15.6 dB in 0.125 s (law 15.2); freeze holds within
  2 dB over 1 s.

```c
/*
 * `rvb`: lo-fi 4-line FDN reverb, 8-bit lanes (engine C, plan §3.4).
 *
 * What, per block: the share as 32-bit words (one per sample, four 8-bit
 * lanes = four lines); the line delays d_k = base_k x size (`rte`), integer,
 * clamped to the buffer; the line gains g_k = 2^(lg x d_k) with lg from
 * crumpBit_reverbDecay (`fbk`, RT60 0.1-10 s), or near 1 while frozen (`snc`,
 * input muted); the per-line damping tilt (`ton`); the width (`dpn`); the
 * per-block gate that keeps feedback and wet off until `valid` covers the
 * longest line (no stale bytes recirculate after an Effect type change).
 * Per frame: four integer reads (one word each, SBFX of the line's lane),
 * damping, a 4 x 4 Hadamard (scaled 1/2), gains plus the input with
 * alternating signs, four ADC conversions packed into one word and one
 * store; the stereo wet is two orthogonal line sums, widened by `dpn`.
 * Inputs: runtime, block. Outputs: io, the word buffer, reverb state, AC.
 * Caller: crumpBit_process() for CRUMPBIT_SUBTYPE_REVERB.
 */
static CRUMPBIT_NOINLINE void crumpBit_runReverb(CrumpBitRuntime *rt,
                                                 const crumpbit_block_t *b)
{
    CrumpBitReverbState *rv = &rt->u.rvb;
    uint32_t *words = (uint32_t *)(void *)b->base;
    const uint32_t len = b->len;
    const float size = crumpBit_reverbSize[(rt->rate > 127u) ? 127u : rt->rate];
    const float lg = rt->sync ? CRUMPBIT_RVB_FREEZE_LG
                   : crumpBit_reverbDecay[(rt->feedback_raw > 127u)
                                          ? 127u : rt->feedback_raw];
    /* in_gain: the mono input (1/2 (L + R)) on the code axis (x 128). */
    const float in_gain = rt->sync ? 0.0f : 64.0f;
    /* width: 0 mono .. 1 at 63 .. ~2 at 127 (side gain). */
    const float width = (float)rt->pan_raw * (1.0f / 63.0f);
    uint32_t d0 = (uint32_t)((float)CRUMPBIT_RVB_BASE0 * size);
    uint32_t d1 = (uint32_t)((float)CRUMPBIT_RVB_BASE1 * size);
    uint32_t d2 = (uint32_t)((float)CRUMPBIT_RVB_BASE2 * size);
    uint32_t d3 = (uint32_t)((float)CRUMPBIT_RVB_BASE3 * size);
    /*
     * g_k: each line's decay gain with the Hadamard's 1/2, the gate and the
     * ADC's x 128 (the write lands on the code axis directly).
     */
    float g0;
    float g1;
    float g2;
    float g3;
    float gate;
    /* mid_k, side_k: the wet sums' 1/4, the gate and (side) the width. */
    float mid_k;
    float side_k;
    float mix = b->mix;
    float tone_k;
    float tone_a;
    float tone_b;
    float s0 = rv->damp[0];
    float s1 = rv->damp[1];
    float s2 = rv->damp[2];
    float s3 = rv->damp[3];
    int32_t ac_cl = rt->ac_c_l;
    int32_t ac_cr = rt->ac_c_r;
    float ac_yl = rt->ac_y_l;
    float ac_yr = rt->ac_y_r;
    uint32_t wp = rv->write_pos;
    uint32_t n;

    /* Lines must fit the word buffer (the share clamp keeps them inside). */
    d0 = (d0 >= len) ? len - 1u : ((d0 == 0u) ? 1u : d0);
    d1 = (d1 >= len) ? len - 1u : ((d1 == 0u) ? 1u : d1);
    d2 = (d2 >= len) ? len - 1u : ((d2 == 0u) ? 1u : d2);
    d3 = (d3 >= len) ? len - 1u : ((d3 == 0u) ? 1u : d3);
    gate = (rv->valid >= d3 + b->frames) ? 1.0f : 0.0f;
    g0 = 64.0f * crumpBit_exp2Neg(lg * (float)d0) * gate;
    g1 = 64.0f * crumpBit_exp2Neg(lg * (float)d1) * gate;
    g2 = 64.0f * crumpBit_exp2Neg(lg * (float)d2) * gate;
    g3 = 64.0f * crumpBit_exp2Neg(lg * (float)d3) * gate;
    mid_k = 0.25f * gate;
    side_k = mid_k * width;
    crumpBit_toneSetup(rt->tone_raw, &tone_k, &tone_a, &tone_b);

    /*
     * The block's fields as locals: an output store (float) may alias any
     * float reached through `b`, so reading them inside the loop would
     * reload them every frame.
     */
    float *const left = b->left;
    float *const right = b->right;
    const uint32_t frames = b->frames;
    const uint32_t keep = b->keep;
    const uint32_t invert = b->invert;
    const float mix_inc = b->mix_inc;

    for (n = 0u; n < frames; n++) {
        float hl;
        float hr;
        /* xp, xm: 128.5 +- the input on the code axis (the lines' signs). */
        float xp;
        float xm;
        float y0;
        float y1;
        float y2;
        float y3;
        float z0;
        float z1;
        float z2;
        float z3;
        float h01;
        float h01d;
        float h23;
        float h23d;
        /* e, o: the even and odd line sums of the wet. */
        float e;
        float o;
        uint32_t i;
        uint32_t packed;

        crumpBit_stage1(left[n], right[n], keep, invert,
                        &ac_cl, &ac_cr, &ac_yl, &ac_yr, &hl, &hr);
        mix += mix_inc;
        xp = 128.5f + in_gain * (hl + hr);
        xm = 128.5f - in_gain * (hl + hr);
        /*
         * Four integer taps: one word each, the line's signed lane (SBFX or
         * ASR), converted to +-1 by the scaling VCVT (no multiply). The read
         * index wp - d_k wraps by compare: unsigned underflow exceeds wp.
         */
        i = wp - d0; i += (i > wp) ? len : 0u;
        y0 = crumpBit_codeToFloat((int8_t)(words[i]));
        i = wp - d1; i += (i > wp) ? len : 0u;
        y1 = crumpBit_codeToFloat((int8_t)(words[i] >> 8));
        i = wp - d2; i += (i > wp) ? len : 0u;
        y2 = crumpBit_codeToFloat((int8_t)(words[i] >> 16));
        i = wp - d3; i += (i > wp) ? len : 0u;
        y3 = crumpBit_codeToFloat((int32_t)words[i] >> 24);
        /* Damping tilt per line (`ton`). */
        s0 += tone_k * (y0 - s0);
        s1 += tone_k * (y1 - s1);
        s2 += tone_k * (y2 - s2);
        s3 += tone_k * (y3 - s3);
        z0 = tone_a * s0 + tone_b * y0;
        z1 = tone_a * s1 + tone_b * y1;
        z2 = tone_a * s2 + tone_b * y2;
        z3 = tone_a * s3 + tone_b * y3;
        /* 4 x 4 Hadamard, scaled 1/2 (orthonormal: energy preserving). */
        h01 = z0 + z1;
        h01d = z0 - z1;
        h23 = z2 + z3;
        h23d = z2 - z3;
        /*
         * Write: decay gains, the input with alternating signs, already on
         * the code axis (crumpBit_adc()'s y = 128 v + 128.5 with the 128 in
         * g_k and in_gain): truncate and USAT #8 per line, four codes packed
         * (ORR with shifted operands), then one XOR turns all four offset
         * codes into signed lanes.
         */
        packed = __USAT((int32_t)(xp + g0 * (h01 + h23)), 8);
        packed |= __USAT((int32_t)(xm + g1 * (h01d + h23d)), 8) << 8;
        packed |= __USAT((int32_t)(xp + g2 * (h01 - h23)), 8) << 16;
        packed |= __USAT((int32_t)(xm + g3 * (h01d - h23d)), 8) << 24;
        words[wp] = packed ^ 0x80808080u;
        wp++;
        wp -= (wp >= len) ? len : 0u;
        /*
         * Stereo wet: mid = (e + o)/4 and side = (e - o)/4 x width, two
         * orthogonal line sums, widened by `dpn`; the gate in both factors.
         */
        e = y0 + y2;
        o = y1 + y3;
        right[n] = crumpBit_mixFrame(hr, mid_k * (e + o) - side_k * (e - o),
                                     mix);
        left[n] = crumpBit_mixFrame(hl, mid_k * (e + o) + side_k * (e - o),
                                    mix);
    }

    rv->write_pos = wp;
    rv->valid = (rv->valid + b->frames < len) ? rv->valid + b->frames : len;
    rv->damp[0] = s0;
    rv->damp[1] = s1;
    rv->damp[2] = s2;
    rv->damp[3] = s3;
    rt->ac_c_l = (int16_t)ac_cl;
    rt->ac_c_r = (int16_t)ac_cr;
    rt->ac_y_l = ac_yl;
    rt->ac_y_r = ac_yr;
}

```

##### E10.22 — Seat the active engine: `crumpBit_seatActive()` (new lines 2121–2177)

- **What:**
  - **Single line:** `crumpBit_seat()`; passes the block through below
    `CRUMPBIT_LOOP_MIN`.
  - **`grn`:** half the bytes per line (uint16), the P2 keep/re-seat rule;
    passes through below four grains.
  - **`rvb`:** bytes/4 words, the same rule.
- **Inputs:** runtime, seated bytes.
- **Output:** the engine length, or 0 (pass the block through
  untouched).
- **Callers:** `crumpBit_process()`, `crumpBit_bufferChanged()`.

```c
/*
 * Seat the active engine in the current share (S091 P3).
 *
 * What: the engine's length in its own unit, from the seated bytes
 * (min(share, LOOP_MAX)):
 * - single line (`dly`, `cmb`, `pit`): bytes, through crumpBit_seat() (tail
 *   kept when write_pos still fits; the tape delay clamped);
 * - `grn`: half the bytes per line, kept as uint16_t;
 * - `rvb`: bytes / 4 words.
 * A changed length keeps write_pos when it still fits (valid cut to it) and
 * re-seats otherwise (`grn` has no glide state: its tap follows R).
 * Inputs: runtime, seated bytes. Output: the engine length, or 0 when the
 * share is too small (the block then passes through untouched).
 * Callers: crumpBit_process(), crumpBit_bufferChanged().
 */
static uint32_t crumpBit_seatActive(CrumpBitRuntime *rt, uint32_t bytes)
{
    switch (rt->active_subtype) {
    case CRUMPBIT_SUBTYPE_GRAIN: {
        CrumpBitGrainState *g = &rt->u.grn;
        const uint32_t len = bytes / 2u;

        if (len != g->length) {
            if (g->length != 0u && g->write_pos < len) {
                g->valid = (g->valid > g->write_pos) ? g->write_pos : g->valid;
            } else {
                g->write_pos = 0u;
                g->valid = 0u;
            }
            g->length = (uint16_t)len;
        }
        return (len >= 4u * CRUMPBIT_GRAIN_MIN) ? len : 0u;
    }
    case CRUMPBIT_SUBTYPE_REVERB: {
        CrumpBitReverbState *rv = &rt->u.rvb;
        const uint32_t len = bytes / 4u;

        if (len != rv->length) {
            if (rv->length != 0u && rv->write_pos < len) {
                rv->valid = (rv->valid > rv->write_pos) ? rv->write_pos
                                                        : rv->valid;
            } else {
                rv->write_pos = 0u;
                rv->valid = 0u;
            }
            rv->length = len;
        }
        return (len >= CRUMPBIT_LOOP_MIN) ? len : 0u;
    }
    default: {
        const uint32_t len = crumpBit_seat(rt, bytes);

        return (len >= CRUMPBIT_LOOP_MIN) ? len : 0u;
    }
    }
}

```

##### E10.23 — Enter a sub-engine: `crumpBit_enter()` (new lines 2178–2239)

- **What:** on a `sub` change from any source (menu, MIDI, Scene,
  Pattern; S3, L4):
  1. read the old engine's byte position and written bytes;
  2. `memset` the 32 B union, so no reinterpreted bits become a NaN or a
     huge float;
  3. set `active_subtype`; clear `primed`, so a tape engine's glide
     starts on its target;
  4. convert the position into the new geometry, clamped to it:
     - `grn`: `pos % half` (16.16 positions start at the write head; the
       steps are unity, levels full, pans at the fixed lean);
     - `rvb`: words;
     - single line: bytes.
- The arena is never cleared or gated: the new engine plays the old bytes
  in its own format (glitches encouraged). Rows are untouched.
- **Cost:** straight-line apart from one modulo, only in the switching
  block (194 per block including it).
- **Caller:** `crumpBit_process()`.
- **Proof:** CB11 (30 s of random switches every 1–50 blocks: finite,
  bounded, invariants, guard band untouched).

```c
/*
 * Enter a new sub-engine (S091 P3 §2.4: menu, MIDI, Scene or Pattern).
 *
 * What: the arena is REINTERPRETED, never cleared or gated (user: hand-off
 * glitches encouraged). The old engine's write position and written bytes
 * (crumpBit_bytePosition()) are converted into the new format's geometry and
 * clamped to it; every float of the new state is re-initialised (so no
 * reinterpreted union bits become a NaN or a huge value); the tape glide is
 * re-primed. The rows are untouched: the new engine reads every stored row
 * value with its own meaning (plan §2.4, A1).
 * Inputs: runtime, the new sub-type, the seated bytes. Output: the union
 * holds the new engine's state; active_subtype = new.
 * Cost: straight-line apart from one modulo, once in the switching block.
 * Caller: crumpBit_process().
 */
static void crumpBit_enter(CrumpBitRuntime *rt, uint8_t next, uint32_t bytes)
{
    uint32_t pos;
    uint32_t written;
    uint32_t len;

    crumpBit_bytePosition(rt, &pos, &written);
    /* The whole union is rebuilt: zeroed floats and counters first. */
    memset(&rt->u, 0, sizeof(rt->u));
    rt->active_subtype = next;
    rt->primed = 0u;
    switch (next) {
    case CRUMPBIT_SUBTYPE_GRAIN:
        len = bytes / 2u;
        if (len != 0u) {
            rt->u.grn.length = (uint16_t)len;
            rt->u.grn.write_pos = (uint16_t)(pos % len);
            rt->u.grn.valid = (uint16_t)((written < len) ? written : len);
            rt->u.grn.pos_a = (uint32_t)rt->u.grn.write_pos << 16;
            rt->u.grn.pos_b = rt->u.grn.pos_a;
        }
        rt->u.grn.inc_a = 4096;
        rt->u.grn.inc_b = 4096;
        rt->u.grn.amp_a = 255u;
        rt->u.grn.amp_b = 255u;
        rt->u.grn.pan_a = 64u;
        rt->u.grn.pan_b = 191u;
        break;
    case CRUMPBIT_SUBTYPE_REVERB:
        len = bytes / 4u;
        if (len != 0u) {
            rt->u.rvb.length = len;
            rt->u.rvb.write_pos = (pos / 4u) % len;
            rt->u.rvb.valid = (written / 4u < len) ? written / 4u : len;
        }
        break;
    default:
        len = bytes;
        if (len != 0u) {
            rt->u.tap.length = len;
            rt->u.tap.write_pos = pos % len;
            rt->u.tap.valid = (written < len) ? written : len;
        }
        break;
    }
}

```

##### E10.24 — `crumpBit_process()`: the four-stage block (new lines 2240–2398)

- **What:**
  1. enter a changed sub-type (`subtype != active_subtype`);
  2. seat (0 → pass through);
  3. the shared per-block values:
     - the masks (invert wins over off);
     - `per_frame`;
     - the mix ramp from `mix_prev` to `mix_raw`;
     - the output filter's TPT coefficients (`g` from the voice table,
       `R = filterDamping(res)`, `a1 = 1/(1 + g(g + 2R))`, `a2 = g·a1`,
       `a3 = g·a2`);
     - the one-block crossfade of the output mix from the previous
       type/resonance;
  4. one engine by `switch` (stages 1–3 per frame);
  5. stage 4: the P2 TPT SVF second pass, unchanged (31 per frame), with
     its mono rule (both inputs read before either output is stored).
- **Why:** plan §2.1. The sub-type is chosen once per block, so each
  engine pays only for itself.
- **Inputs:** runtime, io.
- **Outputs:** io in place, arena bytes, runtime.
- **Caller:** `effects_process()`.

```c
/*
 * Process one Effect bus block in place (S074_CRUMPBIT_EFFECT.md §3; S091
 * P2 §5; S091 P3 §2.1).
 *
 * What, per block:
 * 1. enter a new sub-engine when the resolved `sub` differs from the one
 *    that ran (any source: menu, MIDI, Scene, Pattern automation);
 * 2. seat the engine in the share (passes the block through untouched when
 *    the share is too small);
 * 3. the shared per-block work: data-line masks, the ramp step, the mix
 *    stage's ramp (origin from mix_prev), the output filter's coefficients
 *    and its one-block type/resonance crossfade;
 * 4. one sub-engine loop (stages 1, 2 and 3 per frame);
 * 5. stage 4: the output filter, a second pass (S091 P2, unchanged).
 * Inputs: runtime and normalized stereo io. Output: io->l/io->r in place,
 * arena bytes, runtime. Caller: effects_process().
 * Affiliates: crumpBit_run*(), crumpBit_enter(), crumpBit_seatActive(),
 * crumpBit_filterMix(), crumpBit_filterG.
 */
static void crumpBit_process(void *rt_void, effect_io_t *io)
{
    CrumpBitRuntime *rt = (CrumpBitRuntime *)rt_void;
    crumpbit_block_t blk;
    uint32_t bytes;
    float mix_to;
    float a1;
    float a2;
    float a3;
    float cx;
    float cb;
    float cl;
    float cx_to;
    float cb_to;
    float cl_to;
    float cx_inc;
    float cb_inc;
    float cl_inc;
    float s_l1;
    float s_l2;
    float s_r1;
    float s_r2;
    uint32_t n;

    if (!rt || !io || !io->l || !io->share || !io->share->base ||
        io->frames == 0u)
        return;
    bytes = io->share->bytes;
    bytes = (bytes > CRUMPBIT_LOOP_MAX_BYTES) ? CRUMPBIT_LOOP_MAX_BYTES : bytes;
    /* 1. A changed sub-type is entered on this block (reinterpretation). */
    if (rt->subtype != rt->active_subtype)
        crumpBit_enter(rt, rt->subtype, bytes);
    /* 2. Seat; too small a share passes the block through untouched. */
    blk.len = crumpBit_seatActive(rt, bytes);
    if (blk.len == 0u)
        return;

    /* 3. Shared per-block work. */
    blk.frames = io->frames;
    blk.left = io->l;
    blk.right = io->r ? io->r : io->l;
    blk.base = (int8_t *)io->share->base;
    /* Data lines: invert wins over off (spec §3.2). */
    blk.invert = rt->bit_invert;
    blk.keep = (uint32_t)~((uint32_t)rt->bit_off & ~(uint32_t)rt->bit_invert) &
               0xFFu;
    /* Ramp step per frame: the block's ramps land on their targets. */
    blk.per_frame = 1.0f / (float)blk.frames;
    /* The mix stage's ramp: origin from the previous block's raw row. */
    blk.mix = crumpBit_mixGain(rt->mix_prev);
    mix_to = crumpBit_mixGain(rt->mix_raw);
    blk.mix_inc = (mix_to - blk.mix) * blk.per_frame;
    /*
     * Output filter coefficients for this block (Simper TPT SVF, S091 P2):
     * g from the voice-law table, k = 2R, a1 = 1/(1 + g(g + k)),
     * a2 = g a1, a3 = g a2; the output mix crossfades over the block.
     */
    {
        const uint8_t f = (rt->filter_freq > 127u) ? 127u : rt->filter_freq;
        const float g = crumpBit_filterG[f];
        const float r = crumpBit_filterDamping(rt->filter_reso);
        float r_from;

        a1 = 1.0f / (1.0f + g * (g + 2.0f * r));
        a2 = g * a1;
        a3 = g * a2;
        r_from = crumpBit_filterDamping(rt->filter_reso_prev);
        crumpBit_filterMix(rt->filter_type_prev, r_from, &cx, &cb, &cl);
        crumpBit_filterMix(rt->filter_type, r, &cx_to, &cb_to, &cl_to);
        cx_inc = (cx_to - cx) * blk.per_frame;
        cb_inc = (cb_to - cb) * blk.per_frame;
        cl_inc = (cl_to - cl) * blk.per_frame;
    }

    /* 4. One sub-engine (stages 1-3 per frame). */
    switch (rt->active_subtype) {
    case CRUMPBIT_SUBTYPE_COMB:   crumpBit_runComb(rt, &blk);   break;
    case CRUMPBIT_SUBTYPE_PITCH:  crumpBit_runPitch(rt, &blk);  break;
    case CRUMPBIT_SUBTYPE_GRAIN:  crumpBit_runGrain(rt, &blk);  break;
    case CRUMPBIT_SUBTYPE_REVERB: crumpBit_runReverb(rt, &blk); break;
    default:                      crumpBit_runDelay(rt, &blk);  break;
    }
    rt->mix_prev = rt->mix_raw;

    /*
     * 5. Stage 4 (shared): the output filter, a second pass over the block.
     * Both inputs are read before either output is stored, so a mono block
     * (right aliases left) filters the same input on both states and keeps
     * the left result, as the engines do.
     */
    s_l1 = rt->svf_l1;
    s_l2 = rt->svf_l2;
    s_r1 = rt->svf_r1;
    s_r2 = rt->svf_r2;
    for (n = 0u; n < blk.frames; n++) {
        const float xl = blk.left[n];
        const float xr = blk.right[n];
        /* v1 = band-pass, v2 = low-pass, v3 = input minus the low state. */
        float v1;
        float v2;
        float v3;
        float yl;
        float yr;

        /* Output-mix ramp: lands on this block's set at the last frame. */
        cx += cx_inc;
        cb += cb_inc;
        cl += cl_inc;

        /*
         * TPT SVF tick (Simper): v3 = x - ic2; v1 = a1*ic1 + a2*v3;
         * v2 = ic2 + a2*ic1 + a3*v3; then the trapezoidal state updates
         * ic1 = 2*v1 - ic1 and ic2 = 2*v2 - ic2.
         */
        v3 = xl - s_l2;
        v1 = a1 * s_l1 + a2 * v3;
        v2 = s_l2 + a2 * s_l1 + a3 * v3;
        s_l1 = 2.0f * v1 - s_l1;
        s_l2 = 2.0f * v2 - s_l2;
        yl = cx * xl + cb * v1 + cl * v2;

        v3 = xr - s_r2;
        v1 = a1 * s_r1 + a2 * v3;
        v2 = s_r2 + a2 * s_r1 + a3 * v3;
        s_r1 = 2.0f * v1 - s_r1;
        s_r2 = 2.0f * v2 - s_r2;
        yr = cx * xr + cb * v1 + cl * v2;

        blk.right[n] = yr;
        blk.left[n] = yl;
    }
    rt->svf_l1 = s_l1;
    rt->svf_l2 = s_l2;
    rt->svf_r1 = s_r1;
    rt->svf_r2 = s_r2;
    /* The rows this block's output mix ramped to are the next origin. */
    rt->filter_type_prev = rt->filter_type;
    rt->filter_reso_prev = rt->filter_reso;
}

```

##### E10.25 — `crumpBit_bufferChanged()` (new lines 2399–2417)

- **What:** re-seats the active engine in the new share (clamped to
  `CRUMPBIT_LOOP_MAX_BYTES`). The manager re-clamps `rte` on its next pass
  (M16).
- **Caller:** `effects_onShareChanged()`.

```c
/*
 * Share-change notification (voice units claimed or released).
 *
 * What: re-seats the active engine in the new share (crumpBit_seatActive():
 * the newest audio kept when the write position still fits, otherwise
 * muted; the tape delay clamped). The manager re-clamps the Rate on its next
 * pass (BUFFER_DEPENDENT rows are re-applied every block, S091 P3).
 * Inputs: runtime and new share. Output: runtime seat fields.
 * Caller: effects_onShareChanged().
 */
static void crumpBit_bufferChanged(void *rt_void, const fx_share_t *share)
{
    const uint32_t bytes = share ? share->bytes : 0u;

    (void)crumpBit_seatActive((CrumpBitRuntime *)rt_void,
                              (bytes > CRUMPBIT_LOOP_MAX_BYTES)
                                  ? CRUMPBIT_LOOP_MAX_BYTES : bytes);
}

```

##### E10.26 — `crumpBit_effectiveMax()`: the sub-type-aware clamp (new lines 2418–2441)

- **What:** for `rte`, `crumpBit_subtypeRateLimit(rt->subtype, share)`;
  without a runtime (a page edit of a Scene that is not live) the `dly`
  rule. Every other row: 255 (unlimited). Stored values are never
  rewritten.
- **Callers:** `effects_service()` (every block for BUFFER_DEPENDENT rows,
  M16), `effects_paramEffectiveMax()` (M4).

```c
/*
 * Buffer-dependent clamp hook (S091 P2 §5.1; sub-type-aware since S091 P3).
 *
 * What: for `rte`, the largest value whose length fits the share under the
 * resolved sub-type's own law (crumpBit_subtypeRateLimit(): the delay
 * curve, the comb table, the pitch window, a `grn` half line, the `rvb`
 * word buffer); any other row is not limited (255). The runtime may be NULL
 * (a page edit of a Scene that is not live): the delay rule applies.
 * Stored values are never rewritten (A30).
 * Inputs: runtime (may be NULL), row index, share. Output: inclusive max.
 * Callers: effects_service() (every block for BUFFER_DEPENDENT rows),
 * effects_paramEffectiveMax() (page edits).
 */
static uint8_t crumpBit_effectiveMax(const void *rt_void, uint8_t index,
                                     const fx_share_t *share)
{
    const CrumpBitRuntime *rt = (const CrumpBitRuntime *)rt_void;

    if (index != CRUMPBIT_PARAM_RATE)
        return 255u;
    return crumpBit_subtypeRateLimit(rt ? rt->subtype : CRUMPBIT_SUBTYPE_DELAY,
                                     share);
}

```

##### E10.27 — Lane maps and the ops table (new lines 2442–2523)

- **What:**
  - `crumpBit_laneMaps[5][16]`: nine shared lanes identical in every map
    (0 Morph, 1 `bof`, 2 `biv`, 3 `mix`, 7 vol, 8 pan, 11 `typ`, 12 `frq`,
    13 `res`), plus the pool per sub-type (L1/L2):

    | lane | dly | cmb | pit | grn | rvb |
    |---|---|---|---|---|---|
    | 4 | fbk | fbk | fbk | st2 | fbk |
    | 5 | rte | rte | rte | st3 | rte |
    | 6 | snc | snc | snc | snc (frz) | snc (frz) |
    | 9 | dpn | dpn | dpn | dpn (pos) | dpn (wid) |
    | 10 | ton | ton | ton | ton (sze) | ton (dmp) |
    | 14 | – | – | aux (int) | aux (st1) | – |
    | 15 | – | – | – | spr | – |

    `dly` equals the S091 P2 table byte for byte.
  - `crumpBit_laneMap(v)`: clamps v to the last map.
  - `crumpBit_ops` adds `crumpBit_laneMap`, the variant row
    `CRUMPBIT_PARAM_SUBTYPE` and the count 5.
- **Why:** reassignable lanes (user). Lane 3 `mix` is never freeable (M1);
  `sub` is never a lane (OQ2).
- **Accessors:** `effects_laneTable()` (M6), the self-check (M12), host
  CB9 (break 9 puts `st2` on a `pit` lane).

```c
/*
 * Per-sub-type FX lane maps (S091 P3 §2.9; user L1/L2, accepted).
 *
 * What: one 16-entry lane -> row table per `sub` value.
 * - Nine SHARED lanes, identical in every map: 0 Effect Morph, 1 bof,
 *   2 biv, 3 MIX (the mix stage: never in the pool, never freeable), 7 vol,
 *   8 pan, 11 typ, 12 frq, 13 res.
 * - The seven-lane POOL (4, 5, 6, 9, 10, 14, 15), assigned per sub-type:
 *     lane   dly   cmb   pit   grn   rvb
 *       4    fbk   fbk   fbk   st2   fbk
 *       5    rte   rte   rte   st3   rte
 *       6    snc   snc   snc   snc   snc     (grn/rvb: freeze)
 *       9    dpn   dpn   dpn   dpn   dpn     (grn: pos; rvb: width)
 *      10    ton   ton   ton   ton   ton     (grn: sze)
 *      14    -     -     aux   aux   -       (pit: interval; grn: st1)
 *      15    -     -     -     spr   -
 *   `dly` equals the S091 P2 table exactly (lane 14 stays empty until the
 *   tone drive lands on `aux`), so `.fx` files round-trip unchanged.
 * - Values carry over on a sub-type change (L4): a lane keeps its steps and
 *   drives whatever row the new map names on it.
 * Why here (DSP file): the host test (CB9) checks the maps, and the ops
 *   table references them.
 * Accessors: crumpBit_laneMap() (crumpBit_ops.lane_map), EffectsManager
 *   effects_laneTable(); the registry row's lanes[] must equal the `dly`
 *   map (self-check code 11).
 */
#define CRUMPBIT_LANES_SHARED_HEAD                                     \
    EFFECT_LANE_MORPH_SOURCE, CRUMPBIT_PARAM_BIT_OFF,                  \
    CRUMPBIT_PARAM_BIT_INVERT, CRUMPBIT_PARAM_MIX
static const uint8_t crumpBit_laneMaps[CRUMPBIT_SUBTYPE_COUNT]
                                      [EFFECT_SEQ_LANE_COUNT] = {
    [CRUMPBIT_SUBTYPE_DELAY] = { CRUMPBIT_LANES_SHARED_HEAD,
        CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
        CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN, CRUMPBIT_PARAM_DLY_PAN,
        CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FILTER_TYPE,
        CRUMPBIT_PARAM_FILTER_FREQ, CRUMPBIT_PARAM_FILTER_RESO,
        EFFECT_LANE_NONE, EFFECT_LANE_NONE },
    [CRUMPBIT_SUBTYPE_COMB] = { CRUMPBIT_LANES_SHARED_HEAD,
        CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
        CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN, CRUMPBIT_PARAM_DLY_PAN,
        CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FILTER_TYPE,
        CRUMPBIT_PARAM_FILTER_FREQ, CRUMPBIT_PARAM_FILTER_RESO,
        EFFECT_LANE_NONE, EFFECT_LANE_NONE },
    [CRUMPBIT_SUBTYPE_PITCH] = { CRUMPBIT_LANES_SHARED_HEAD,
        CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
        CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN, CRUMPBIT_PARAM_DLY_PAN,
        CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FILTER_TYPE,
        CRUMPBIT_PARAM_FILTER_FREQ, CRUMPBIT_PARAM_FILTER_RESO,
        CRUMPBIT_PARAM_AUX, EFFECT_LANE_NONE },
    [CRUMPBIT_SUBTYPE_GRAIN] = { CRUMPBIT_LANES_SHARED_HEAD,
        CRUMPBIT_PARAM_GRAIN_ST2, CRUMPBIT_PARAM_GRAIN_ST3,
        CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN,
        CRUMPBIT_PARAM_DLY_PAN, CRUMPBIT_PARAM_TONE,
        CRUMPBIT_PARAM_FILTER_TYPE, CRUMPBIT_PARAM_FILTER_FREQ,
        CRUMPBIT_PARAM_FILTER_RESO, CRUMPBIT_PARAM_AUX,
        CRUMPBIT_PARAM_GRAIN_SPREAD },
    [CRUMPBIT_SUBTYPE_REVERB] = { CRUMPBIT_LANES_SHARED_HEAD,
        CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
        CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN, CRUMPBIT_PARAM_DLY_PAN,
        CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FILTER_TYPE,
        CRUMPBIT_PARAM_FILTER_FREQ, CRUMPBIT_PARAM_FILTER_RESO,
        EFFECT_LANE_NONE, EFFECT_LANE_NONE },
};

const uint8_t *crumpBit_laneMap(uint8_t variant)
{
    return crumpBit_laneMaps[(variant < CRUMPBIT_SUBTYPE_COUNT)
                             ? variant : CRUMPBIT_SUBTYPE_COUNT - 1u];
}

/* CrumpBit operations; S091 P3 adds the per-sub-type lane maps. */
const effect_type_ops_t crumpBit_ops = {
    crumpBit_init,
    crumpBit_exportHandoff,
    crumpBit_writeParam,
    crumpBit_process,
    crumpBit_bufferChanged,
    crumpBit_effectiveMax,
    crumpBit_laneMap,
    CRUMPBIT_PARAM_SUBTYPE,
    CRUMPBIT_SUBTYPE_COUNT,
};
```

### 3.3 `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`

#### P1 — Row count 22

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 36–41: **replace** (6 lines → 9; new lines 36–44).

- **What:** `CRUMPBIT_PARAM_COUNT` 15 → 22.
  - Rows 15 (`aux`) and 16–21 (`st2 st3 spr den amp wid`) are appended.
  - The deferred output drive would be row 22; the tone drive becomes
    `aux`'s meaning in `dly`/`cmb`.
- **Why:** appending keeps every saved row index; the registry carries
  storage, AutoSave, the page and automation.
- **Accessors:** the registry row (`descriptor_count`), every loop over the
  descriptors.

Remove (HEAD 36–41):

```c
 * Descriptor rows: common rows 0..2 plus type rows 3..14.
 * S091 P2 appended rows 11..14 (`ton`, `typ`, `frq`, `res`); the drive rows
 * of S091_P2_CrumpBit_Expanded_Plan.md (`drv`, `tdr`) are a later addition
 * and would append as 15 and 16.
 */
#define CRUMPBIT_PARAM_COUNT 15u
```

Replace with:

```c
 * Descriptor rows: common rows 0..2 plus type rows 3..21.
 * S091 P2 appended rows 11..14 (`ton`, `typ`, `frq`, `res`). S091 P3
 * appended row 15 (`aux`, the per-sub-type pitch/drive row) and the granular
 * rows 16..21 (`st2 st3 spr den amp wid`). The deferred output-filter drive
 * (`drv`) would append as 22; the tone drive (`tdr`) is the `aux` row's
 * meaning in `dly`/`cmb` when the drives land
 * (S091_P3_CrumpBit_SubType_Expansions.md §2.3).
 */
#define CRUMPBIT_PARAM_COUNT 22u
```

#### P2 — Rows 15–21, sub-type ids, three ownership masks

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 69–110: **replace** (42 lines → 76; new lines 72–147).

- **What:**
  - **Enum:** adds `CRUMPBIT_PARAM_AUX` and the six `CRUMPBIT_PARAM_GRAIN_*`
    rows.
  - **Sub-type ids:** `CRUMPBIT_SUBTYPE_DELAY`..`REVERB` (0..4) and
    `CRUMPBIT_SUBTYPE_COUNT` 5. `dly` stays 0, so saved Scenes keep
    playing the delay.
  - **Ownership masks:**
    - `CRUMPBIT_ROWS_SHARED` adds `mix` (M1: the mix stage is shared);
    - `CRUMPBIT_ROWS_DELAY` becomes `CRUMPBIT_ROWS_SUBTYPE` (`fbk rte snc
      dpn ton aux`, rows whose meaning the sub-type sets, aligned by role:
      A1);
    - new `CRUMPBIT_ROWS_GRN` (`st2 st3 spr den amp wid`).
- **Why:**
  - Each row has exactly one owner, and C7's compile-time guards fail the
    build for a row appended without one.
  - CB9 checks the masks on the host and that every lane-map pool row
    belongs to its own sub-type.
- **Accessors:** C7, `crumpBit_process()` dispatch, the page hooks, host
  CB5/CB9.

Remove (HEAD 69–110):

```c
    CRUMPBIT_PARAM_ENUM_COUNT
} crumpbit_param_t;

/* Sub-effect behind Mix/Feedback/Rate; only the delay exists in v1. */
#define CRUMPBIT_SUBTYPE_DELAY 0u

/*
 * Row ownership by stage (S091 P2 §4; the single record of which rows belong
 * to which CrumpBit stage).
 *
 * What: one 32-bit mask per owner, bit n = descriptor index n.
 * - CRUMPBIT_ROWS_SHARED: the stages every sub-type runs: the ADC data-line
 *   masks (`bof`, `biv`), the sub-type selector itself (`sub`), and the
 *   output filter (`typ`, `frq`, `res`), which is the last element of the
 *   Effect whatever the sub-type is.
 * - CRUMPBIT_ROWS_DELAY: the delay sub-type's rows (`mix`, `fbk`, `rte`,
 *   `snc`, `dpn`, `ton`). `ton` belongs here because it sits on the delay's
 *   feedback return (user, 2026-11-04).
 * Why: a second sub-type must add its own mask here and may reuse delay rows
 * only deliberately; the _Static_asserts in CrumpBitParameters.c prove that
 * the masks are disjoint and cover every type row 3..COUNT-1, so a row
 * appended without an owner fails the build. The common rows 0..2 belong to
 * the manager and are in neither mask.
 * Accessors: CrumpBitParameters.c (guards), tools/dsp_test crumpbit (law).
 * Affiliates: crumpbit_param_t, CRUMPBIT_SUBTYPE_DELAY, crumpBit_process()
 * stage comments.
 */
#define CRUMPBIT_ROW_BIT(index_) (1UL << (uint32_t)(index_))
#define CRUMPBIT_ROWS_SHARED                                  \
    (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_BIT_OFF) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_BIT_INVERT) |            \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_SUBTYPE) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_TYPE) |           \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_FREQ) |           \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_RESO))
#define CRUMPBIT_ROWS_DELAY                                   \
    (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_MIX) |                   \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FEEDBACK) |              \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_RATE) |                  \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_SYNC) |                  \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_DLY_PAN) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_TONE))
```

Replace with:

```c
    /* S091 P3: per-sub-type `aux` (pit interval, grn root `st1`). */
    CRUMPBIT_PARAM_AUX,
    /* S091 P3, `grn` only: second and third pitch, spread, density, level
     * jitter and random pan. */
    CRUMPBIT_PARAM_GRAIN_ST2,
    CRUMPBIT_PARAM_GRAIN_ST3,
    CRUMPBIT_PARAM_GRAIN_SPREAD,
    CRUMPBIT_PARAM_GRAIN_DENSITY,
    CRUMPBIT_PARAM_GRAIN_AMP,
    CRUMPBIT_PARAM_GRAIN_WIDTH,
    CRUMPBIT_PARAM_ENUM_COUNT
} crumpbit_param_t;

/*
 * Sub-engines (the `sub` row, index 8; S091 P3 §2.3).
 *
 * What: the stored values of `crump_subtype`. 0 is the S074 delay, so saved
 * Scenes keep playing `dly`. The row is Pattern-automatable (not Morph, not
 * LFO, never on an FX lane); a Pattern value above the last id is clamped to
 * it by the manager (user OQ3: the ids are not spread over 0..127).
 * Accessors: crumpBit_descriptors (max), crumpBit_process() dispatch,
 * crumpBit_enter(), the page hooks (names, layouts, values).
 */
#define CRUMPBIT_SUBTYPE_DELAY    0u   /* `dly` tape delay (engine A) */
#define CRUMPBIT_SUBTYPE_COMB     1u   /* `cmb` comb / resonator (engine A) */
#define CRUMPBIT_SUBTYPE_PITCH    2u   /* `pit` pitch shifter (engine B) */
#define CRUMPBIT_SUBTYPE_GRAIN    3u   /* `grn` granular + delay (engine B') */
#define CRUMPBIT_SUBTYPE_REVERB   4u   /* `rvb` 4-line FDN reverb (engine C) */
#define CRUMPBIT_SUBTYPE_COUNT    5u

/*
 * Row ownership by stage (S091 P2 §4; the single record of which rows belong
 * to which CrumpBit stage).
 *
 * What: one 32-bit mask per owner, bit n = descriptor index n.
 * - CRUMPBIT_ROWS_SHARED: the stages every sub-type runs: the ADC data-line
 *   masks (`bof`, `biv`), the sub-type selector itself (`sub`), the MIX
 *   STAGE (`mix`, S091 P3 §2.8: always dry <-> the sub-engine's wet, never a
 *   sub-type row) and the output filter (`typ`, `frq`, `res`), which is the
 *   last element of the Effect whatever the sub-type is.
 * - CRUMPBIT_ROWS_SUBTYPE: rows whose meaning is set by the active sub-type
 *   (`fbk`, `rte`, `snc`, `dpn`, `ton`, `aux`), aligned by role across
 *   sub-types (plan §6, A1): `fbk` amount back, `rte` length (higher =
 *   longer), `snc` the switch, `dpn` where the wet sits, `ton` colour/shape,
 *   `aux` pitch. A sub-type change keeps their values (plan §2.4).
 * - CRUMPBIT_ROWS_GRN: rows only `grn` reads (`st2 st3 spr den amp wid`).
 * Why: a new sub-type adds its own mask here; the _Static_asserts in
 * CrumpBitParameters.c prove the masks are disjoint and cover every type
 * row 3..COUNT-1, so a row appended without an owner fails the build. The
 * common rows 0..2 belong to the manager and are in no mask.
 * Accessors: CrumpBitParameters.c (guards), tools/dsp_test crumpbit (CB9).
 * Affiliates: crumpbit_param_t, CRUMPBIT_SUBTYPE_*, crumpBit_process().
 */
#define CRUMPBIT_ROW_BIT(index_) (1UL << (uint32_t)(index_))
#define CRUMPBIT_ROWS_SHARED                                  \
    (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_BIT_OFF) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_BIT_INVERT) |            \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_MIX) |                   \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_SUBTYPE) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_TYPE) |           \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_FREQ) |           \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_RESO))
#define CRUMPBIT_ROWS_SUBTYPE                                 \
    (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FEEDBACK) |              \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_RATE) |                  \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_SYNC) |                  \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_DLY_PAN) |               \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_TONE) |                  \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_AUX))
#define CRUMPBIT_ROWS_GRN                                     \
    (CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_ST2) |             \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_ST3) |             \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_SPREAD) |          \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_DENSITY) |         \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_AMP) |             \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_WIDTH))
```

#### P3 — Layout comment

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 151–156: **replace** (6 lines → 8; new lines 188–195).

- **What:** records that `crumpBit_layout` now serves `dly`, `cmb` and
  `rvb`. `pit` and `grn` get their own variants through
  `crumpBit_ui.layout` (C9).

Remove (HEAD 151–156):

```c
 * (S091 P2: four screens, the layout maximum). crumpBit_ui owns the SELECT
 * buttons (bit toggles), the SELECT LEDs (bit states), the overlay's top row
 * and the `sub`/Rate value text (the Rate shows the share-clamped value and,
 * with Sync on, the division the DSP plays).
 */
extern const effect_select_layout_t crumpBit_layout;
```

Replace with:

```c
 * (S091 P2: four screens, the layout maximum); S091 P3 adds the `pit` and
 * `grn` variants, chosen by crumpBit_ui.layout. crumpBit_ui owns the SELECT
 * buttons (bit toggles), the SELECT LEDs (bit states), the overlay's top row
 * and the `sub`/Rate value text (the Rate shows the share-clamped value and,
 * with Sync on, the division the DSP plays).
 */
extern const effect_select_layout_t crumpBit_layout;

```

### 3.4 `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`

#### C1 — `#include <math.h>`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, **add** after HEAD line 22. New lines 23–23 (1 lines).

- **What:** `powf()` for the `pit` window label (C8).
- **Why:** the label prints the table law `10·20^(rte/127)` ms. It runs
  in the foreground only, at page refresh.

Add:

```c
#include <math.h>      /* powf(): the `pit` window label (S091 P3) */
```

#### C2 — Descriptor comment: rows 3..21

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 32–32: **replace** (1 lines → 1; new lines 33–33).

- **What:** the descriptor table comment states rows 3..21.

Remove (HEAD 32–32):

```c
 * What: rows 0..2 are the shared common rows; rows 3..14 are CrumpBit's.
```

Replace with:

```c
 * What: rows 0..2 are the shared common rows; rows 3..21 are CrumpBit's.
```

#### C3 — Descriptor comment: the `sub` row

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 45–46: **replace** (2 lines → 5; new lines 46–50).

- **What:** documents `sub` as the five-engine selector:
  - Pattern-automatable only (S7), never an FX lane (OQ2);
  - Pattern values above 4 clamp to `rvb` (OQ3);
  - labels from the format hooks, because every `DTYPE_MENU` id is in use.

Remove (HEAD 45–46):

```c
 * - sub-type (8): only `dly` (max 0); its label comes from the format hook,
 *   because every DTYPE_MENU table id (4 bits) is already in use.
```

Replace with:

```c
 * - sub-type (8, S091 P3): the sub-engine, 0 dly, 1 cmb, 2 pit, 3 grn,
 *   4 rvb (max 4). Pattern-automatable (user S7: switches per step, with the
 *   buffer reinterpreted; glitches wanted), not Morph, not LFO, never on an
 *   FX lane (user OQ2); a Pattern value above 4 clamps to `rvb` (OQ3). Its
 *   labels come from the format hooks (every DTYPE_MENU id is in use).
```

#### C4 — Descriptor comment: rows 15–21

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 58–63: **replace** (6 lines → 13; new lines 62–74).

- **What:** documents the new rows:
  - `aux`: bipolar; the `pit` interval and the `grn` root `st1`; the
    future tone drive in `dly`/`cmb`.
  - The grain rows and their lanes (`st2` 4, `st3` 5, `spr` 15 in the
    `grn` map).
  - The row count, 22.

Remove (HEAD 58–63):

```c
 * - Defaults (S075 F2-E, user): mix 0, feedback 64, rate 64, delay pan 63
 *   (the absolute centre, shows 0). The sync row's 3-letter label is `snc`;
 *   its file key remains `crump_sync`.
 * Why: the registry makes storage, AutoSave, the Effect page, lanes,
 * automation and LFO work without type-specific code elsewhere. Inputs:
 * none (const). Output: 15 rows. Accessors: EffectsManager (resolution,
```

Replace with:

```c
 * - aux (15, S091 P3): the per-sub-type pitch row, bipolar (63 = 0): the
 *   `pit` interval, the `grn` root `st1`; the tone drive in `dly`/`cmb`
 *   when the deferred drives land. Morph, LFO and Pattern.
 * - grain rows (16..21, S091 P3, `grn` only): `st2`, `st3` (bipolar,
 *   semitones from the root), `spr` spread/detune, `den` density (default
 *   127 = continuous grains), `amp` level jitter, `wid` random pan. Morph,
 *   LFO and Pattern; lanes from the `grn` lane map (st2 4, st3 5, spr 15).
 * - Defaults (S075 F2-E, user): mix 0, feedback 64, rate 64, delay pan 63
 *   (the absolute centre, shows 0). The sync row's 3-letter label is `snc`;
 *   its file key remains `crump_sync`.
 * Why: the registry makes storage, AutoSave, the Effect page, lanes,
 * automation and LFO work without type-specific code elsewhere. Inputs:
 * none (const). Output: 22 rows. Accessors: EffectsManager (resolution,
```

#### C5 — The `sub` descriptor

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 87–88: **replace** (2 lines → 4; new lines 98–101).

- **What:** `crump_subtype` gains `INSTRUMENT_PARAM_FLAG_AUTOMATABLE` and
  max `CRUMPBIT_SUBTYPE_COUNT − 1` (4). It stays `EFFECT_MOD_NONE`: no
  Morph, no LFO.
- **Why:** S7 makes `sub` switchable per Pattern step. The manager clamps
  a Pattern value to the descriptor max (OQ3); `write_param` clamps again
  (E9).
- **File key:** unchanged, `crump_subtype`, so old `.fx` files load as
  `dly`.

Remove (HEAD 87–88):

```c
    EFFECT_ROW("crump_subtype", "CrumpBit", "SubType", "sub", DTYPE_0B127,
               0u, EFFECT_MOD_NONE, CRUMPBIT_SUBTYPE_DELAY, 0u),
```

Replace with:

```c
    /* S091 P3: five sub-engines; Pattern-automatable only (S7, OQ2). */
    EFFECT_ROW("crump_subtype", "CrumpBit", "SubType", "sub", DTYPE_0B127,
               INSTRUMENT_PARAM_FLAG_AUTOMATABLE, EFFECT_MOD_NONE,
               CRUMPBIT_SUBTYPE_DELAY, CRUMPBIT_SUBTYPE_COUNT - 1u),
```

#### C6 — Rows 15–21

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, **add** after HEAD line 104. New lines 118–133 (16 lines).

- **What:** seven `EFFECT_ROW` entries:

  | Key | Label | Dtype | Default |
  |---|---|---|---|
  | `crump_aux` | `aux` | `DTYPE_PM63` | 63 |
  | `crump_grain_st2` | `st2` | `DTYPE_PM63` | 63 |
  | `crump_grain_st3` | `st3` | `DTYPE_PM63` | 63 |
  | `crump_grain_spread` | `spr` | 0..127 | 0 |
  | `crump_grain_density` | `den` | 0..127 | 127 |
  | `crump_grain_amp` | `amp` | 0..127 | 0 |
  | `crump_grain_width` | `wid` | 0..127 | 0 |

- **Flags:** all `EFFECT_FLAGS_IMAGE` with `EFFECT_MOD_0_127`: Morph, LFO
  and Pattern, like the P2 rows.
- **Why:** G7 and G10. The defaults reproduce the plain sound: root only,
  continuous grains, no jitter, the fixed pan lean.
- **Accessors:** the registry (storage keys, AutoSave, page, automation);
  `write_param` (E9).

Add:

```c
    /* S091 P3: the per-sub-type pitch row (pit interval, grn root). */
    EFFECT_ROW("crump_aux", "CrumpBit", "Aux", "aux", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    /* S091 P3, grn only: pitch set, spread, density, level, width. */
    EFFECT_ROW("crump_grain_st2", "Grain", "Pitch2", "st2", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("crump_grain_st3", "Grain", "Pitch3", "st3", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("crump_grain_spread", "Grain", "Spread", "spr", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("crump_grain_density", "Grain", "Density", "den", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 127u, 127u),
    EFFECT_ROW("crump_grain_amp", "Grain", "LvlJitr", "amp", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("crump_grain_width", "Grain", "Width", "wid", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
```

#### C7 — Three-owner row guards

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 114–121: **replace** (8 lines → 11; new lines 143–153).

- **What:** the compile-time guards:
  - the three masks are pairwise disjoint;
  - their union is exactly rows 3..COUNT−1.
- **Why:** a row added without deciding its owner (shared, sub-type, or
  `grn`) stops the build.

Remove (HEAD 114–121):

```c
 * Row-ownership guards (S091 P2 §4). The stage masks in
 * CrumpBitParameters.h must not overlap, and together they must name every
 * type row 3..COUNT-1 and nothing else, so appending a row without deciding
 * its owner (shared or a sub-type) stops the build.
 */
_Static_assert((CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_DELAY) == 0u,
               "a CrumpBit row cannot belong to two stages");
_Static_assert((CRUMPBIT_ROWS_SHARED | CRUMPBIT_ROWS_DELAY) ==
```

Replace with:

```c
 * Row-ownership guards (S091 P2 §4; three owners since S091 P3). The masks
 * in CrumpBitParameters.h must not overlap, and together they must name
 * every type row 3..COUNT-1 and nothing else, so appending a row without
 * deciding its owner (shared, sub-type, or grn) stops the build.
 */
_Static_assert((CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_SUBTYPE) == 0u &&
               (CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_GRN) == 0u &&
               (CRUMPBIT_ROWS_SUBTYPE & CRUMPBIT_ROWS_GRN) == 0u,
               "a CrumpBit row cannot belong to two stages");
_Static_assert((CRUMPBIT_ROWS_SHARED | CRUMPBIT_ROWS_SUBTYPE |
                CRUMPBIT_ROWS_GRN) ==
```

#### C8 — Per-sub-type value, name and target text

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 252–305: **replace** (54 lines → 183; new lines 284–466).

- **What:** replaces the P2 format hook with the P3 page text (plan §2.5,
  S2, OQ5). Six sub-blocks:
  - the `sub` tokens;
  - the per-sub-type row names;
  - the shown sub-type and the semitone/note formatters;
  - the value hook;
  - the name hook;
  - the automation-target hook.
- **Why:**
  - The stored raw byte stays underneath (pots, storage, automation,
    locks, LFO).
  - Only what is played and shown follows the sub-type, so a switch never
    rewrites a value (A1, L4).

Remove HEAD lines 252–305 (54 lines), from
`* Format hook: CrumpBit value text on the Effect page (Q7; S091 P2).`
through
`return 0u;`.

Replace with the 6 sub-blocks below, in order:

##### C8.1 — `crumpBit_subtypeNames` (new lines 284–291)

- **What:** `dly cmb pit grn rvb`, indexed by the stored id.
- **Accessors:** the value hook, the target hook.

```c
 * Sub-engine tokens (S091 P3): the `sub` row's value text, indexed by the
 * stored id (CRUMPBIT_SUBTYPE_*). Accessors: crumpBit_uiFormatValue3(),
 * crumpBit_uiFormatTarget3().
 */
static const char crumpBit_subtypeNames[CRUMPBIT_SUBTYPE_COUNT][4] = {
    "dly", "cmb", "pit", "grn", "rvb"
};

```

##### C8.2 — `crumpBit_subtypeRowNames`, `crumpBit_shownSubtype()`, `crumpBit_formatSemitones()` (new lines 292–322)

- **What:**
  - **Page names** of `fbk rte snc dpn ton aux` per sub-type (plan §6);
    `""` keeps the descriptor name:
    - `cmb`: `not pol`;
    - `pit`: `win snp`, `aux` → `int`;
    - `grn`: `div frz pos sze`, `aux` → `st1`;
    - `rvb`: `dcy siz frz wid dmp`.
  - **`crumpBit_shownSubtype()`:** the page's view of the record's `sub`,
    clamped.
  - **`crumpBit_formatSemitones()`:** signed semitones of a bipolar row,
    clamped to ±24 (`numtostrps`).

```c
/*
 * Per-sub-type names of the sub-type rows on the Effect page (plan §6,
 * S2): index 0 fbk, 1 rte, 2 snc, 3 dpn, 4 ton, 5 aux, per sub-type.
 * "" keeps the descriptor's own short name. Accessor: crumpBit_uiName3().
 */
static const char crumpBit_subtypeRowNames[CRUMPBIT_SUBTYPE_COUNT][6][4] = {
    [CRUMPBIT_SUBTYPE_DELAY]  = { "",    "",    "",    "",    "",    ""    },
    [CRUMPBIT_SUBTYPE_COMB]   = { "",    "not", "pol", "",    "",    ""    },
    [CRUMPBIT_SUBTYPE_PITCH]  = { "",    "win", "snp", "",    "",    "int" },
    [CRUMPBIT_SUBTYPE_GRAIN]  = { "",    "div", "frz", "pos", "sze", "st1" },
    [CRUMPBIT_SUBTYPE_REVERB] = { "dcy", "siz", "frz", "wid", "dmp", ""    },
};

/* The shown sub-type (the page's view of the record), clamped. */
static uint8_t crumpBit_shownSubtype(void)
{
    const uint8_t sub = menuEffects_shownParam(CRUMPBIT_PARAM_SUBTYPE);

    return (sub < CRUMPBIT_SUBTYPE_COUNT) ? sub
                                          : (uint8_t)(CRUMPBIT_SUBTYPE_COUNT - 1u);
}

/* Signed semitones of a bipolar pitch row (63 = 0, clamped to +-24). */
static void crumpBit_formatSemitones(uint8_t value, char *out)
{
    int32_t s = (int32_t)value - 63;

    s = (s > 24) ? 24 : ((s < -24) ? -24 : s);
    numtostrps(out, (int8_t)s);
}

```

##### C8.3 — `crumpBit_formatNote()` (new lines 323–335)

- **What:** the note name of a MIDI note in the menu's `DTYPE_NOTE_NAME`
  convention (octave = note/12).
- **Why:** `menu.c` `setNoteName()` is file-static, so this is its twin.
  It labels `cmb`'s `rte` (S9).

```c
/*
 * Note name of a MIDI note, the menu's DTYPE_NOTE_NAME convention (octave =
 * note / 12; menu.c setNoteName() is file-static, so this is its twin).
 */
static void crumpBit_formatNote(uint8_t note, char *out)
{
    const uint8_t n = (uint8_t)(note % 12u);

    out[0] = (char)((n > 8u ? 60 : 67) + ((n + (n > 4u)) / 2u));
    out[1] = ((n < 5u && (n & 1u)) || (n > 5u && !(n & 1u))) ? '#' : ' ';
    out[2] = (char)('0' + note / 12u);
}

```

##### C8.4 — `crumpBit_uiFormatValue3()` (new lines 336–427)

- **What:**
  - `sub` → its token.
  - `rte` → first clamped to `crumpBit_subtypeRateLimit(shown sub, live
    share)`, the clamp the manager applies before the DSP, then:
    - `dly`: the step division with the shown Sync on (as P2), else the
      clamped number;
    - `cmb`: the note it plays;
    - `pit`: the window in ms;
    - `grn`: the division it always snaps to, within one half line;
    - `rvb`: the clamped number.
  - `aux` → semitones in `pit`/`grn`, `---` where it has no meaning yet.
  - `st2`/`st3` → semitones.
  - Anything else returns 0 (generic text).
- **Inputs:** row, shown value, 3-character output.
- **Output:** nonzero when written.
- **Runs in:** the foreground; the share is a struct copy.
- **Affiliates:** `stepScale_formatShort()`, `seq_getBpm()`,
  `fxbuf_effectShare()`, `menuEffects_shownParam()`.

```c
/*
 * Format hook: CrumpBit value text on the Effect page (Q7; S091 P2; per
 * sub-type since S091 P3).
 *
 * What:
 * - `sub` shows the sub-engine token (dly cmb pit grn rvb);
 * - `rte`, clamped first to the longest value the live share holds under the
 *   shown sub-type's law (crumpBit_subtypeRateLimit(), the clamp
 *   EffectsManager applies before the DSP): `dly` the step division with
 *   the shown Sync on (as S091 P2), else the clamped number; `cmb` the note
 *   it plays (a higher value is a lower note); `pit` the window in ms;
 *   `grn` the step division it always snaps to (one half line);
 * - `aux` in `pit`/`grn`, and `st2`/`st3` in `grn`: signed semitones;
 * - `aux` where it has no meaning yet (`dly`, `cmb`, `rvb`): `---`.
 * Everything else returns 0 for generic dtype formatting.
 * Why: the stored raw byte stays underneath (pots, storage, automation,
 * locks, LFO); only what is played and shown follows the sub-type.
 * Inputs: row index, value shown, 3-character output. Output: nonzero when
 * out[0..2] was written. Foreground; the share read is a struct copy.
 * Affiliates: stepScale_formatShort(), seq_getBpm(), menuEffects_shownParam(),
 * fxbuf_effectShare(), crumpBit_subtypeRateLimit(), crumpBit_combNote().
 */
static uint8_t crumpBit_uiFormatValue3(uint8_t index, uint8_t value,
                                       char *out)
{
    const uint8_t sub = crumpBit_shownSubtype();

    if (index == CRUMPBIT_PARAM_SUBTYPE) {
        memcpy(out, crumpBit_subtypeNames[(value < CRUMPBIT_SUBTYPE_COUNT)
                                          ? value
                                          : CRUMPBIT_SUBTYPE_COUNT - 1u], 3u);
        return 1u;
    }
    if (index == CRUMPBIT_PARAM_RATE) {
        fx_share_t share;
        /* limit: the largest `rte` that fits this share for this engine. */
        uint8_t limit;
        /* shown: the Rate the DSP will play, never above the clamp. */
        uint8_t shown;

        fxbuf_effectShare(&share);
        limit = crumpBit_subtypeRateLimit(sub, &share);
        shown = (value > limit) ? limit : value;
        switch (sub) {
        case CRUMPBIT_SUBTYPE_COMB:
            crumpBit_formatNote(crumpBit_combNote(shown), out);
            return 1u;
        case CRUMPBIT_SUBTYPE_PITCH:
            /* Window in ms: 10 * 20^(rte/127), the table law. */
            numtostrpu(out, (uint8_t)(10.0f * powf(20.0f, (float)shown /
                                                         127.0f) + 0.5f), ' ');
            return 1u;
        case CRUMPBIT_SUBTYPE_GRAIN:
            stepScale_formatShort(
                crumpBit_syncDivision(shown, seq_getBpm(),
                                      crumpBit_grainMaxSamples(&share)),
                out);
            return 1u;
        case CRUMPBIT_SUBTYPE_REVERB:
            break;
        default:
            if (menuEffects_shownParam(CRUMPBIT_PARAM_SYNC) != 0u) {
                /* S078: nudge stops show their symbol, others the raw CC. */
                stepScale_formatShort(
                    crumpBit_syncDivision(shown, seq_getBpm(),
                                          crumpBit_shareMaxSamples(&share)),
                    out);
                return 1u;
            }
            break;
        }
        if (shown != value) {
            numtostrpu(out, shown, ' ');
            return 1u;
        }
        return 0u;
    }
    if (index == CRUMPBIT_PARAM_AUX) {
        if (sub == CRUMPBIT_SUBTYPE_PITCH || sub == CRUMPBIT_SUBTYPE_GRAIN) {
            crumpBit_formatSemitones(value, out);
            return 1u;
        }
        memcpy(out, "---", 3u);
        return 1u;
    }
    if (index == CRUMPBIT_PARAM_GRAIN_ST2 || index == CRUMPBIT_PARAM_GRAIN_ST3) {
        crumpBit_formatSemitones(value, out);
        return 1u;
    }
    return 0u;
}

```

##### C8.5 — `crumpBit_uiName3()` (new lines 428–451)

- **What:** the name hook. For a sub-type row (`fbk rte snc dpn ton aux`)
  whose current sub-type renames it, writes the page name.
- **Accessor:** `menuEffects_shortName()` (U4).
- **Scope:** Effect page only; other screens keep the descriptor names
  (S2).

```c
/*
 * Name hook: per-sub-type 3-letter names of the sub-type rows on the Effect
 * page only (S091 P3 S2; plan §6). Other screens keep the descriptor names.
 * Inputs: row index, 3-character output. Output: nonzero when written.
 * Accessor: menuEffects_shortName(). Affiliates: crumpBit_subtypeRowNames.
 */
static uint8_t crumpBit_uiName3(uint8_t index, char *out)
{
    static const uint8_t rows[6] = {
        CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
        CRUMPBIT_PARAM_DLY_PAN, CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_AUX
    };
    const char (*names)[4] = crumpBit_subtypeRowNames[crumpBit_shownSubtype()];
    uint8_t i;

    for (i = 0u; i < 6u; i++) {
        if (rows[i] == index && names[i][0] != '\0') {
            memcpy(out, names[i], 3u);
            return 1u;
        }
    }
    return 0u;
}

```

##### C8.6 — `crumpBit_uiFormatTarget3()` (new lines 452–466)

- **What:** on automation pages, `sub` shows its token (OQ5). Other rows
  keep generic text, because their meaning needs the Effect page's
  context.
- **Accessor:** `effects_formatTargetValue3()` (M5).

```c
/*
 * Target-value hook: the `sub` row's token on automation pages (S091 P3,
 * OQ5: the STEP automation page and Pattern value entry show dly cmb pit grn
 * rvb, not 0..4). Other rows keep generic text (their meaning needs the
 * Effect page's context). Accessor: effects_formatTargetValue3().
 */
static uint8_t crumpBit_uiFormatTarget3(uint8_t index, uint8_t value,
                                        char *out)
{
    if (index != CRUMPBIT_PARAM_SUBTYPE)
        return 0u;
    memcpy(out, crumpBit_subtypeNames[(value < CRUMPBIT_SUBTYPE_COUNT)
                                      ? value
                                      : CRUMPBIT_SUBTYPE_COUNT - 1u], 3u);
    return 1u;
```

#### C9 — `pit`/`grn` layouts and the layout hook

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 345–349: **replace** (5 lines → 77; new lines 506–582).

- **What:** two layout variants and `crumpBit_uiLayout()`.
- **Why:** S12. `grn`'s pitch rows appear only in `grn`, and `pit`'s
  interval sits where `dly` has an empty cell.

Remove (HEAD 345–349):

```c
 * CrumpBit page hooks.
 *
 * What: SELECT toggles bits and returns EFFECT_UI_SHOW_HOME, render_leds owns
 * the SELECT row, paint_row0 draws the overlay row, and format_value3 labels
 * `sub` and the (share-clamped, Sync-aware) Rate. TRACK and BAR keep the default page behaviour (NULL).
```

Replace with the 3 sub-blocks below, in order:

##### C9.1 — `crumpBit_layoutPitch`, `crumpBit_layoutGrain` (new lines 506–560)

- **What:**
  - **`pit`:** the base SELECT 2 screens plus `aux` (`int`) in screen 3,
    cell 3.
  - **`grn`:** the base screens plus two SELECT 3 screens: `st1 st2 st3
    spr` and `den amp wid`.
    - The encoder walks cells across every SELECT group, so they follow
      SELECT 2's last screen.
    - The SELECT 3 button still toggles data line 2:
      `crumpBit_uiSelect()` owns every SELECT button.
  - Home stays SELECT 2, screen 0.

```c
 * Sub-engine layouts (S091 P3 §2.5, S12).
 *
 * What: the registry's crumpBit_layout serves `dly`, `cmb` and `rvb` (their
 * rows sit in the same cells; names and values change through the hooks).
 * - `pit` adds its interval (`aux`, shown `int`) in SELECT 2 s3 cell 3.
 * - `grn` adds two SELECT 3 screens: `st1 st2 st3 spr` and `den amp wid`.
 *   The encoder walks cells across every SELECT group, so they are reached
 *   after SELECT 2's last screen; the SELECT 3 button keeps toggling data
 *   line 2 (crumpBit_uiSelect() owns every SELECT button).
 * Accessor: crumpBit_uiLayout(). Affiliates: menuEffects_layout().
 */
static const effect_select_layout_t crumpBit_layoutPitch = {
    .screen_count = { 0u, 4u, 0u, 0u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = {
            { CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, EFFECT_LAYOUT_CELL_MORPH },
            { CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SUBTYPE },
            { CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN,
              CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FILTER_TYPE },
            { CRUMPBIT_PARAM_FILTER_FREQ, CRUMPBIT_PARAM_FILTER_RESO,
              CRUMPBIT_PARAM_AUX, EFFECT_LANE_NONE },
        },
    },
    .custom_row0 = { 0u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u },
    .home_sub_page = 1u,
    .home_screen = 0u,
};

static const effect_select_layout_t crumpBit_layoutGrain = {
    .screen_count = { 0u, 4u, 2u, 0u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = {
            { CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, EFFECT_LAYOUT_CELL_MORPH },
            { CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FEEDBACK,
              CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SUBTYPE },
            { CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN,
              CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_FILTER_TYPE },
            { CRUMPBIT_PARAM_FILTER_FREQ, CRUMPBIT_PARAM_FILTER_RESO,
              EFFECT_LANE_NONE, EFFECT_LANE_NONE },
        },
        [2] = {
            { CRUMPBIT_PARAM_AUX, CRUMPBIT_PARAM_GRAIN_ST2,
              CRUMPBIT_PARAM_GRAIN_ST3, CRUMPBIT_PARAM_GRAIN_SPREAD },
            { CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_GRAIN_AMP,
              CRUMPBIT_PARAM_GRAIN_WIDTH, EFFECT_LANE_NONE },
        },
    },
    .custom_row0 = { 0u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u },
    .home_sub_page = 1u,
    .home_screen = 0u,
};

```

##### C9.2 — `crumpBit_uiLayout()` (new lines 561–573)

- **What:** returns the shown sub-type's layout; `crumpBit_layout` for
  `dly`, `cmb`, `rvb` (never NULL).
- **Accessor:** `menuEffects_layout()` (U1).

```c
/*
 * Layout hook (S091 P3): the layout of the shown sub-type. NULL never: the
 * base layout serves `dly`, `cmb` and `rvb`. Accessor: menuEffects_layout().
 */
static const struct effect_select_layout *crumpBit_uiLayout(void)
{
    switch (crumpBit_shownSubtype()) {
    case CRUMPBIT_SUBTYPE_PITCH: return &crumpBit_layoutPitch;
    case CRUMPBIT_SUBTYPE_GRAIN: return &crumpBit_layoutGrain;
    default:                     return &crumpBit_layout;
    }
}

```

##### C9.3 — Hook table comment (new lines 574–582)

- **What:** the `crumpBit_ui` comment lists the three P3 hooks.

```c
/*
 * CrumpBit page hooks.
 *
 * What: SELECT toggles bits and returns EFFECT_UI_SHOW_HOME, render_leds owns
 * the SELECT row, paint_row0 draws the overlay row, and format_value3 labels
 * `sub` and the per-sub-type values. S091 P3: name3 renames the sub-type
 * rows per sub-type, layout picks the sub-type's screens, format_target3
 * names `sub` on automation pages. TRACK and BAR keep the default page
 * behaviour (NULL).
```

#### C10 — `crumpBit_ui`: the three hooks

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, **add** after HEAD line 361. New lines 595–597 (3 lines).

- **What:** sets `.name3`, `.layout`, `.format_target3`.

Add:

```c
    .name3 = crumpBit_uiName3,
    .layout = crumpBit_uiLayout,
    .format_target3 = crumpBit_uiFormatTarget3,
```

### 3.5 `Core/DSP/Effects/CrumpBit/CrumpBitTables.h` and the generated tables

#### B1 — Table header comment

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitTables.h`, HEAD lines 20–27: **replace** (8 lines → 21; new lines 20–40).

- **What:** documents the N1 Rate top and the six new tables:
  - their laws and ranges;
  - which row indexes each;
  - the two non-128 lengths (semitone ratio 49, grain fraction 65).

Remove (HEAD 20–27):

```c
 *   126,686 (2.872 s, the whole arena less the 2-sample guard) at 127,
 *   exponential and strictly increasing. Higher `rte` = longer delay.
 * - crumpBit_toneK[ton]: the one-pole coefficient of the feedback-return
 *   tilt. 0..62 low-pass (150 Hz .. 18 kHz), 63 exactly 1.0 (flat),
 *   64..127 high-pass (20 Hz .. 2 kHz).
 * - crumpBit_filterG[frq]: the TPT pre-warped cutoff g of the output filter,
 *   the voice cutoff law (fastTan(pi * 0.45 * valueShaperF2F(frq/127,
 *   FILTER_SHAPER))). 0 at frq 0 (closed), strictly increasing.
```

Replace with:

```c
 *   126,654 (2.871 s: the arena less one 32 B granule and the 2-sample
 *   guard, S091 P3 N1) at 127, exponential and
 *   strictly increasing. Higher `rte` = longer delay.
 * - crumpBit_toneK[ton]: the one-pole coefficient of the feedback-return
 *   tilt. 0..62 low-pass (150 Hz .. 18 kHz), 63 exactly 1.0 (flat),
 *   64..127 high-pass (20 Hz .. 2 kHz).
 * - crumpBit_filterG[frq]: the TPT pre-warped cutoff g of the output filter,
 *   the voice cutoff law (fastTan(pi * 0.45 * valueShaperF2F(frq/127,
 *   FILTER_SHAPER))). 0 at frq 0 (closed), strictly increasing.
 * S091 P3 sub-type tables:
 * - crumpBit_combTable[rte]: `cmb` comb length in samples. rte n plays MIDI
 *   note 112 - n (n clamped at 96), L = fs / f, 8.36 .. 2,141 samples: a
 *   higher rte is a longer comb and a lower note (plan A1).
 * - crumpBit_pitchWindow[rte]: `pit` window in samples, 441 .. 8,822
 *   (10 .. 200 ms), exponential.
 * - crumpBit_semitoneRatio[s]: 2^((s - 24)/12), s = 0..48 (-24..+24 st).
 * - crumpBit_grainFraction[d]: `grn` grain size / R, 1/32 .. 1 for
 *   d = |sze - 63| = 0..64.
 * - crumpBit_reverbSize[rte]: `rvb` delay scale 0.25 .. 2.5.
 * - crumpBit_reverbDecay[fbk]: `rvb` log2 of the per-sample line gain for an
 *   RT60 of 0.1 .. 10 s; a line of d samples has gain 2^(entry * d).
```

#### B2 — Table sizes and externs

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitTables.h`, HEAD lines 38–42: **replace** (5 lines → 13; new lines 51–63).

- **What:** `CRUMPBIT_SEMITONE_SPAN` 24 and `CRUMPBIT_GRAIN_STEPS` 64, plus
  the six externs.
- **Why:** the engines index by these bounds, and the host test extracts
  this header whole.
- **Generated file:** `CrumpBitTables.c` is regenerated by
  `python3 tools/plots/gen_crumpbit_tables.py` after G1–G4 (unit R1, §4);
  `--check` must print `crumpbit tables OK`.

Remove (HEAD 38–42):

```c
#define CRUMPBIT_TABLE_SIZE 128u

extern const float crumpBit_rateTable[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_toneK[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_filterG[CRUMPBIT_TABLE_SIZE];
```

Replace with:

```c
#define CRUMPBIT_TABLE_SIZE      128u
#define CRUMPBIT_SEMITONE_SPAN   24u   /* semitoneRatio covers +-24 */
#define CRUMPBIT_GRAIN_STEPS     64u   /* grainFraction covers 0..64 */

extern const float crumpBit_rateTable[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_toneK[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_filterG[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_combTable[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_pitchWindow[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_semitoneRatio[2u * CRUMPBIT_SEMITONE_SPAN + 1u];
extern const float crumpBit_grainFraction[CRUMPBIT_GRAIN_STEPS + 1u];
extern const float crumpBit_reverbSize[CRUMPBIT_TABLE_SIZE];
extern const float crumpBit_reverbDecay[CRUMPBIT_TABLE_SIZE];
```

### R1 — `Core/DSP/Effects/CrumpBit/CrumpBitTables.c` (generated)

**Location:** the whole file, regenerated. Do not edit it by hand.

- **What:** after G1–G4, run from the repository root:

  ```
  python3 tools/plots/gen_crumpbit_tables.py
  python3 tools/plots/gen_crumpbit_tables.py --check
  ```

  The first prints `wrote Core/DSP/Effects/CrumpBit/CrumpBitTables.c`; the
  second must print `crumpbit tables OK`.
- **The file then holds nine tables:**
  - `rateTable`, every entry slightly shorter for N1 (top 126,654);
  - `toneK` and `filterG`, unchanged;
  - `combTable`, `pitchWindow`, `reverbSize`, `reverbDecay` (128 entries
    each);
  - `semitoneRatio` (49) and `grainFraction` (65).
- **Size:** 292 lines, SHA-256 `2d603b0cff0237975a4516658bf6f579706e7dfded52b63985a88b9fd85236f7`.
- **Why generated:** the laws, the firmware and the host test read one
  source of numbers; `crumpbit_tables` (G4) keeps the file equal to the
  generator.

### 3.6 `Core/DSP/Effects/EffectsManager.h`

#### MH1 — Hooks `name3`, `layout`, `format_target3`

**Location:** `Core/DSP/Effects/EffectsManager.h`, HEAD lines 156–163: **replace** (8 lines → 26; new lines 156–181).

- **What:** three optional members of `struct effect_ui_hooks`, appended
  after `flags` (every initialiser is designated, so no order changes);
  `struct effect_select_layout` is forward-declared for `layout()`.
  - **`name3(index, out)`:** a type row's 3-letter name on the Effect page
    only.
  - **`layout()`:** the select layout to show now; NULL = the registry's.
  - **`format_target3(index, value, out)`:** a row's value text on
    automation pages, for rows whose text needs no page context.
- **Why:** plan §2.5, S2, S12, OQ5. Other types leave them NULL.
- **Accessors:** `menuEffects.c` (U1, U4), `effects_formatTargetValue3()`
  (M5).

Remove (HEAD 156–163):

```c
struct effect_ui_hooks {
    uint8_t (*select)(uint8_t button, uint8_t shift, uint8_t pressed);
    uint8_t (*track)(uint8_t track, uint8_t shift, uint8_t pressed);
    uint8_t (*bar)(uint8_t bar, uint8_t shift, uint8_t pressed);
    void (*render_leds)(void);
    void (*paint_row0)(uint8_t sub_page, uint8_t screen, char *row0);
    uint8_t (*format_value3)(uint8_t index, uint8_t value, char *out);
    uint8_t flags;
```

Replace with:

```c
/*
 * S091 P3 hooks (any may be NULL):
 * - name3(index, out): replaces a type row's 3-letter name on the Effect
 *   page only (cells, full view, held steps), e.g. CrumpBit's per-sub-type
 *   names; other screens keep the descriptor's short name. Nonzero when
 *   out[0..2] was written.
 * - layout(): the select layout to show now (CrumpBit: per shown sub-type);
 *   NULL return or no hook = the registry's select_layout.
 * - format_target3(index, value, out): value text for automation targets
 *   outside the Effect page (the STEP automation page, Pattern value entry);
 *   only rows whose text needs no page context (CrumpBit: `sub` names).
 *   Nonzero when out[0..2] was written.
 */
struct effect_select_layout;

struct effect_ui_hooks {
    uint8_t (*select)(uint8_t button, uint8_t shift, uint8_t pressed);
    uint8_t (*track)(uint8_t track, uint8_t shift, uint8_t pressed);
    uint8_t (*bar)(uint8_t bar, uint8_t shift, uint8_t pressed);
    void (*render_leds)(void);
    void (*paint_row0)(uint8_t sub_page, uint8_t screen, char *row0);
    uint8_t (*format_value3)(uint8_t index, uint8_t value, char *out);
    uint8_t flags;
    uint8_t (*name3)(uint8_t index, char *out);
    const struct effect_select_layout *(*layout)(void);
    uint8_t (*format_target3)(uint8_t index, uint8_t value, char *out);
```

#### MH2 — Tag the select-layout struct

**Location:** `Core/DSP/Effects/EffectsManager.h`, HEAD lines 184–184: **replace** (1 lines → 1; new lines 202–202).

- **What:** `typedef struct effect_select_layout { … }
  effect_select_layout_t`.
- **Why:** the hook's return type refers to it by tag before the typedef.

Remove (HEAD 184–184):

```c
typedef struct {
```

Replace with:

```c
typedef struct effect_select_layout {
```

#### MH3 — `effect_type_ops_t`: `effective_max(rt, …)`, `lane_map`

**Location:** `Core/DSP/Effects/EffectsManager.h`, HEAD lines 199–206: **replace** (8 lines → 20; new lines 217–236).

- **What:**
  - **`effective_max`:** now receives the live runtime (`const void *`, or
    NULL when the type is not live), so a type with sub-engines clamps by
    the engine it runs (S8).
  - **New:**
    - `lane_map(variant)`: the 16-entry lane → row table;
    - `lane_variant_row`: the row whose stored value selects the variant;
    - `lane_variant_count`.
- **Why:** L1. Other types leave `lane_map` NULL and keep the registry's
  `lanes[]`.
- **StereoFilter:** its ops initialiser (`StereoFilterEffect.c:107`) is
  positional and ends at `effective_max` (NULL), so the new members are
  zero (`lane_map` NULL); it compiles unchanged.
- **Accessors:** M4, M6, M12, M16; `crumpBit_ops` (E10).

Remove (HEAD 199–206):

```c
 */
typedef struct {
    void (*init)(void *rt, const fxbuf_handoff_t *handoff);
    void (*export_handoff)(const void *rt, fxbuf_handoff_t *out);
    void (*write_param)(void *rt, uint8_t index, uint8_t value);
    void (*process)(void *rt, effect_io_t *io);
    void (*buffer_changed)(void *rt, const fx_share_t *share);
    uint8_t (*effective_max)(uint8_t index, const fx_share_t *share);
```

Replace with:

```c
 * S091 P3:
 * - effective_max receives the live runtime (NULL when the caller has none,
 *   e.g. a page edit of a Scene whose type is not live), so a type with
 *   sub-engines can clamp by the engine it runs;
 * - lane_map (optional) returns the FX lane -> row table for one variant;
 *   the variant is the record's stored value of row lane_variant_row
 *   (0..lane_variant_count-1). NULL keeps the registry's fixed lanes[].
 *   Every lane reader goes through effects_laneTable(record).
 */
typedef struct {
    void (*init)(void *rt, const fxbuf_handoff_t *handoff);
    void (*export_handoff)(const void *rt, fxbuf_handoff_t *out);
    void (*write_param)(void *rt, uint8_t index, uint8_t value);
    void (*process)(void *rt, effect_io_t *io);
    void (*buffer_changed)(void *rt, const fx_share_t *share);
    uint8_t (*effective_max)(const void *rt, uint8_t index,
                             const fx_share_t *share);
    const uint8_t *(*lane_map)(uint8_t variant);
    uint8_t lane_variant_row;
    uint8_t lane_variant_count;
```

#### MH4 — Lane API by record; `effects_laneTable()`; `effects_formatTargetValue3()`

**Location:** `Core/DSP/Effects/EffectsManager.h`, HEAD lines 283–292: **replace** (10 lines → 27; new lines 313–339).

- **What:**
  - `effects_laneFileKey()` and `effects_laneByFileKey()` take the
    `effect_record_t` instead of the type.
  - New `effects_laneTable(record)`.
  - New `effects_formatTargetValue3(scene, target, value, out)`.
- **Why:**
  - A CrumpBit lane's row depends on the record's stored `sub` (§2.9).
  - The `.fx` writer emits `[params]` before `[sequence]`, so a reader
    knows `sub` when it maps lane keys.
- **Accessors:** storage (S1–S3), menu (N1–N4, U7–U9), the manager
  (M6–M16).

Remove (HEAD 283–292):

```c
 * file_key the registry lane table names. EFFECT_LANE_NONE lanes have no key
 * (NULL / not found), so a file cannot address an unused lane. Why here: the
 * registry owns lane meaning; storage must not duplicate that table.
 */
#define EFFECT_LANE_MORPH_FILE_KEY "effect_morph"
void effects_recordDefaultsForType(effect_record_t *record,
                                   effect_type_id_t type);
const char *effects_laneFileKey(effect_type_id_t type, uint8_t lane);
uint8_t effects_laneByFileKey(effect_type_id_t type, const char *file_key,
                              uint8_t *lane_out);
```

Replace with:

```c
 * file_key the record's lane table names. EFFECT_LANE_NONE lanes have no key
 * (NULL / not found), so a file cannot address an unused lane. Why here: the
 * registry owns lane meaning; storage must not duplicate that table.
 * S091 P3: both take the RECORD, because a type with a lane_map op (CrumpBit)
 * maps lanes by the record's stored sub-type (plan §2.9). The `.fx` writer
 * emits [params] before [sequence], so the reader knows `sub` by then.
 *
 * effects_laneTable() returns the record's 16-entry lane -> row table: the
 * type's lane_map(variant) when it has one, else the registry's lanes[].
 */
#define EFFECT_LANE_MORPH_FILE_KEY "effect_morph"
void effects_recordDefaultsForType(effect_record_t *record,
                                   effect_type_id_t type);
const uint8_t *effects_laneTable(const effect_record_t *record);
const char *effects_laneFileKey(const effect_record_t *record, uint8_t lane);
uint8_t effects_laneByFileKey(const effect_record_t *record,
                              const char *file_key, uint8_t *lane_out);

/*
 * Automation value text of an Effect target outside the Effect page (S091
 * P3, OQ5): calls the Scene type's format_target3 hook for the target's row.
 * Inputs: Scene, Effect target id, value, 3-character output. Output:
 * nonzero when out[0..2] was written (else the caller formats generically).
 * Accessors: menu.c STEP automation value formatting.
 */
uint8_t effects_formatTargetValue3(uint8_t scene_index, uint16_t target,
                                   uint8_t value, char *out);
```

#### MH5 — `effects_laneOfParam()` by record

**Location:** `Core/DSP/Effects/EffectsManager.h`, HEAD lines 366–366: **replace** (1 lines → 1; new lines 413–413).

- **What:** the prototype takes the record.

Remove (HEAD 366–366):

```c
uint8_t effects_laneOfParam(effect_type_id_t type, uint8_t index,
```

Replace with:

```c
uint8_t effects_laneOfParam(const effect_record_t *record, uint8_t index,
```

### 3.7 `Core/DSP/Effects/EffectsManager.c`

#### M1 — Union comment; forward declaration of `effects_runtimeMember()`

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 55–62: **replace** (8 lines → 11; new lines 55–65).

- **What:**
  - The union comment records CrumpBit at 88 B.
  - `static void *effects_runtimeMember(void);` is declared before its
    first P3 use in `effects_paramEffectiveMax()` (M4).

Remove (HEAD 55–62):

```c
     * S074: 56 B; S091 P2: 76 B (tone, DC blocker, output SVF, four rows;
     * paid for by trims, so the union stays 76 B). Its audio lives in the
     * FxBuffer share, not in this union.
     */
    CrumpBitRuntime crump_bit;
} effects_runtime_t;

static INDTCMZ effects_runtime_t effects_runtime;
```

Replace with:

```c
     * S074: 56 B; S091 P2: 76 B; S091 P3: 88 B (a shared part plus a 32 B
     * union of five sub-engine states; user-approved growth up to +32 B).
     * Its audio lives in the FxBuffer share, not in this union.
     */
    CrumpBitRuntime crump_bit;
} effects_runtime_t;

static INDTCMZ effects_runtime_t effects_runtime;

/* The live runtime member (defined below; S091 P3 needs it earlier). */
static void *effects_runtimeMember(void);
```

#### M2 — Registry row comment

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 102–111: **replace** (10 lines → 13; new lines 105–117).

- **What:** the CrumpBit registry row's comment.
  - The clamp follows the live sub-type.
  - `lanes[]` is the `dly` map, equal to the S091 P2 table.
  - The live table follows `sub` through `lane_map`, and self-check
    code 11 holds the two equal.
- **The `lanes[]` initialiser** itself is unchanged.

Remove (HEAD 102–111):

```c
     * CrumpBit (`cbt`, S074; S091 P2): stereo in and out. The 8-bit mono
     * tape loop is elastic (S091 P2 §5.1): it uses the whole Effect share up
     * to CRUMPBIT_LOOP_MAX_BYTES (pref, the full arena) and still works at
     * the share left with every voice unit claimed (min); the Rate row is
     * clamped to the share through crumpBit_ops.effective_max. These two
     * fields are declarative (no code reads them today).
     * Lanes 1..13: bit off, bit invert, mix, feedback, rate, sync, vol, pan,
     * delay pan, then (S091 P2) tone, filter type, filter freq, filter reso.
     * Lane 0 is Effect Morph; lanes 14..15 stay free for the deferred drive
     * rows (`drv`, `tdr`).
```

Replace with:

```c
     * CrumpBit (`cbt`, S074; S091 P2; S091 P3 sub-types): stereo in and
     * out. The 8-bit buffer is elastic (S091 P2 §5.1): it uses the whole
     * Effect share up to CRUMPBIT_LOOP_MAX_BYTES (pref, the full arena) and
     * still works at the share left with every voice unit claimed (min);
     * the Rate row is clamped to the share, under the live sub-type's law,
     * through crumpBit_ops.effective_max. These two fields are declarative
     * (no code reads them today).
     * lanes[] below is the `dly` map (sub 0) and equals the S091 P2 table:
     * lanes 1..13 bit off, bit invert, mix, feedback, rate, sync, vol, pan,
     * delay pan, tone, filter type, filter freq, filter reso; lane 0 Effect
     * Morph; lanes 14..15 free. The live table follows `sub`
     * (crumpBit_ops.lane_map, effects_laneTable()); the self-check
     * (code 11) holds this row equal to crumpBit_laneMap(0).
```

#### M3 — RAM guard 88 B

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 137–141: **replace** (5 lines → 9; new lines 143–151).

- **What:** `_Static_assert(sizeof(effects_runtime_t) == 88u, …)`, and the
  comment records the +12 B and the LTO-dependent granule (choice 10).
- **Why:** the guard makes any later growth a deliberate, approved change.

Remove (HEAD 137–141):

```c
 * RAM guard (S074): the DTCM union is 76 B (STORAGE_SRAM_MANIFEST.md). A
 * larger member grows .dtcmz and shrinks the FxBuffer arena by the same
 * amount, which needs RAM approval before it is merged.
 */
_Static_assert(sizeof(effects_runtime_t) == 76u,
```

Replace with:

```c
 * RAM guard (S074; 88 B since S091 P3): the DTCM union size is recorded in
 * STORAGE_SRAM_MANIFEST.md. A larger member grows .dtcmz and shrinks the
 * FxBuffer arena by the same amount (in 32 B granules), which needs RAM
 * approval before it is merged. S091 P3 used 12 B of a 32 B approval; the
 * arena loses one granule (126,688 -> 126,656 B) when .dtcmz crosses a 32 B
 * boundary, which depends on LTO's packing (CRUMPBIT_LOOP_MAX_BYTES assumes
 * the smaller arena).
 */
_Static_assert(sizeof(effects_runtime_t) == 88u,
```

#### M4 — `effects_paramEffectiveMax()`: pass the runtime

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 369–369: **replace** (1 lines → 8; new lines 379–386).

- **What:** page edits pass the live runtime when the edited Scene's type
  is the live one (`type == effects_state.runtime_type`), else NULL.
- **Why:** the page then clamps `rte` by the engine actually running.
- **Caller:** `menuEffects.c` edit path (P2).

Remove (HEAD 369–369):

```c
        limit = entry->ops->effective_max(index, &share);
```

Replace with:

```c
        /*
         * S091 P3: the live runtime when this type is the live one (its
         * active sub-engine decides the law), else NULL (the type's default).
         */
        limit = entry->ops->effective_max(
            (type == effects_state.runtime_type) ? effects_runtimeMember()
                                                 : NULL,
            index, &share);
```

#### M5 — `effects_formatTargetValue3()`

**Location:** `Core/DSP/Effects/EffectsManager.c`, **add** after HEAD line 411. New lines 429–445 (17 lines).

- **What:**
  - Looks up the Scene's type entry and calls its `format_target3` hook
    for an Effect target (`effectTarget_isEffectId()`), passing the local
    row index.
  - Returns 0 when there is no hook or the target is not an Effect target.
- **Inputs:** Scene index, target id, value, 3-character output.
- **Output:** nonzero when written.
- **Caller:** `menu_formatEffectAutomationValue3()` (N1).

Add:

```c
}

uint8_t effects_formatTargetValue3(uint8_t scene_index, uint16_t target,
                                   uint8_t value, char *out)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(effects_sceneType(scene_index));

    /*
     * S091 P3 (OQ5): a type may name a row's values on automation pages
     * (CrumpBit's `sub`: dly cmb pit grn rvb). Only rows whose text needs no
     * Effect-page context use this hook; everything else formats generically.
     */
    if (!out || !entry || !entry->ui || !entry->ui->format_target3 ||
        !effectTarget_isEffectId(target))
        return 0u;
    return entry->ui->format_target3(effectTarget_local(target), value, out);
```

#### M6 — `effects_laneTable()`; `effects_laneFileKey()` by record

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 475–488: **replace** (14 lines → 45; new lines 509–553).

- **What:**
  - **`effects_laneTable(record)`:**
    - a type with a `lane_map` op returns
      `lane_map(record->normal[lane_variant_row])`, the variant clamped to
      the count;
    - every other type returns `entry->lanes`;
    - a NULL record means `off`. Never NULL.
  - **`effects_laneFileKey(record, lane)`:** names lanes through this
    table.
- **Why:**
  - The record's STORED `sub` decides the lane meaning. A Pattern switch
    changes the engine, not the Scene's lanes (L4: values carry over and
    drive the new map's rows only when the stored `sub` changes).
  - One function serves every reader, so storage, menu and resolution
    cannot disagree.
- **Accessors:** M7–M16, S1–S3.

Remove (HEAD 475–488):

```c
 * Resolve one retained FX-sequence lane to its `.fx` file key.
 *
 * Inputs: registry type and lane number. Output: the fixed Effect Morph key,
 * a descriptor key, or NULL for an unused lane. The registry remains the only
 * owner of lane meaning, so storage and future UI cannot drift apart.
 */
const char *effects_laneFileKey(effect_type_id_t type, uint8_t lane)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    uint8_t index;

    if (!entry || lane >= EFFECT_SEQ_LANE_COUNT)
        return NULL;
    index = entry->lanes[lane];
```

Replace with:

```c
 * The lane -> row table of one record (S091 P3 §2.9).
 *
 * What: a type with a lane_map op maps its lanes by the record's STORED
 * value of the variant row (CrumpBit: `sub`, record->normal[8]; a Pattern
 * switch changes the active engine, not the Scene's lanes); any other type
 * uses the registry's fixed lanes[]. Out-of-range variants read the last map.
 * Input: record (NULL = `off`). Output: a 16-entry table (never NULL).
 * Callers: every lane reader in this file and, through the lane-key helpers,
 * `.fx` storage. Affiliates: effect_type_ops_t.lane_map, crumpBit_laneMap().
 */
const uint8_t *effects_laneTable(const effect_record_t *record)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(record ? record->type : EFFECT_TYPE_OFF);

    if (!entry)
        entry = effects_registryEntry(EFFECT_TYPE_OFF);
    if (record && entry->ops && entry->ops->lane_map &&
        entry->ops->lane_variant_count != 0u) {
        uint8_t variant = record->normal[entry->ops->lane_variant_row];

        variant = (variant < entry->ops->lane_variant_count)
            ? variant : (uint8_t)(entry->ops->lane_variant_count - 1u);
        return entry->ops->lane_map(variant);
    }
    return entry->lanes;
}

/*
 * Resolve one retained FX-sequence lane to its `.fx` file key.
 *
 * Inputs: the record (S091 P3: its lane table) and lane number. Output: the
 * fixed Effect Morph key, a descriptor key, or NULL for an unused lane. The
 * registry remains the only owner of lane meaning, so storage and future UI
 * cannot drift apart.
 */
const char *effects_laneFileKey(const effect_record_t *record, uint8_t lane)
{
    const effect_registry_entry_t *entry =
        effects_registryEntry(record ? record->type : EFFECT_TYPE_OFF);
    uint8_t index;

    if (!entry || lane >= EFFECT_SEQ_LANE_COUNT)
        return NULL;
    index = effects_laneTable(record)[lane];
```

#### M7 — `effects_laneByFileKey()` by record

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 499–511: **replace** (13 lines → 14; new lines 564–577).

- **What:** the bounded 16-lane key scan now uses
  `effects_laneFileKey(record, lane)`.
- **Caller:** the `.fx` parser (S1).

Remove (HEAD 499–511):

```c
 * Inputs: registry type, key after the `lane.` prefix, and optional output
 * pointer. Output: nonzero on a named lane match; unused lanes and unknown
 * keys return zero. The bounded 16-entry scan runs only during file parsing.
 */
uint8_t effects_laneByFileKey(effect_type_id_t type, const char *file_key,
                              uint8_t *lane_out)
{
    uint8_t lane;

    if (!file_key)
        return 0u;
    for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        const char *key = effects_laneFileKey(type, lane);
```

Replace with:

```c
 * Inputs: the record being parsed (S091 P3: its stored sub-type selects the
 * lane table), key after the `lane.` prefix, and optional output pointer.
 * Output: nonzero on a named lane match; unused lanes and unknown keys return
 * zero. The bounded 16-entry scan runs only during file parsing.
 */
uint8_t effects_laneByFileKey(const effect_record_t *record,
                              const char *file_key, uint8_t *lane_out)
{
    uint8_t lane;

    if (!file_key)
        return 0u;
    for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        const char *key = effects_laneFileKey(record, lane);
```

#### M8 — `effects_lockClearLayers()` reads the record's table

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 905–905: **replace** (1 lines → 2; new lines 971–972).

- **What:** when a lock step starts, the overlay and `menuHold` bits are
  cleared for the row each locked lane drives, now
  `effects_laneTable(record)[lane]` instead of `entry->lanes[lane]`.
- **Why:** a `grn` Scene's lane 4 lock must release `st2`, not `fbk`.

Remove (HEAD 905–905):

```c
        index = entry->lanes[lane];
```

Replace with:

```c
        /* S091 P3: the record's lane table (per stored sub-type). */
        index = effects_laneTable(record)[lane];
```

#### M9 — `effects_seqOverride()` takes the lane table

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 956–964: **replace** (9 lines → 12; new lines 1023–1034).

- **What:** the held-step lock override searches the given table.
- **Caller:** `effects_service()` (M15), which passes the record's table
  (M14).

Remove (HEAD 956–964):

```c
/* Replace a descriptor value with a locked value from the active step. */
static uint8_t effects_seqOverride(const effect_registry_entry_t *entry,
                                   const effect_seq_step_t *step,
                                   uint8_t index, uint8_t *value)
{
    uint8_t lane;

    for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (entry->lanes[lane] == index &&
```

Replace with:

```c
/*
 * Replace a descriptor value with a locked value from the active step.
 * S091 P3: `lanes` is the record's lane table (effects_laneTable()).
 */
static uint8_t effects_seqOverride(const uint8_t *lanes,
                                   const effect_seq_step_t *step,
                                   uint8_t index, uint8_t *value)
{
    uint8_t lane;

    for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (lanes[lane] == index &&
```

#### M10 — `effects_laneOfParam()` by record

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 1015–1024: **replace** (10 lines → 11; new lines 1085–1095).

- **What:** the row → lane search uses the record's table; NULL record →
  0.
- **Callers:** the menu (N4, U7–U9).

Remove (HEAD 1015–1024):

```c
uint8_t effects_laneOfParam(effect_type_id_t type, uint8_t index,
                            uint8_t *lane_out)
{
    const effect_registry_entry_t *entry = effects_registryEntry(type);
    uint8_t lane;

    if (!entry)
        return 0u;
    for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (entry->lanes[lane] == index) {
```

Replace with:

```c
uint8_t effects_laneOfParam(const effect_record_t *record, uint8_t index,
                            uint8_t *lane_out)
{
    /* S091 P3: the record's lane table (per sub-type for CrumpBit). */
    const uint8_t *lanes = effects_laneTable(record);
    uint8_t lane;

    if (!record)
        return 0u;
    for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (lanes[lane] == index) {
```

#### M11 — `effects_setSeqLaneLockScene()`: lane validation by record

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 1057–1063: **replace** (7 lines → 6; new lines 1128–1133).

- **What:** `effects_setSeqLaneLockScene()` validates a lane by the
  record's key and descriptor.
- **Why:** a lane that is empty in this sub-type (for example lane 15
  outside `grn`) cannot be locked.

Remove (HEAD 1057–1063):

```c
        !effects_laneFileKey(record->type, lane))
        return 0u;
    if (lane != EFFECT_SEQ_LANE_MORPH) {
        const effect_registry_entry_t *entry =
            effects_registryEntry(record->type);
        const effect_param_descriptor_t *descriptor = entry
            ? effects_descriptor(record->type, entry->lanes[lane]) : NULL;
```

Replace with:

```c
        !effects_laneFileKey(record, lane))
        return 0u;
    if (lane != EFFECT_SEQ_LANE_MORPH) {
        /* S091 P3: the lane's row in this record's lane table. */
        const effect_param_descriptor_t *descriptor =
            effects_descriptor(record->type, effects_laneTable(record)[lane]);
```

#### M12 — Self-check: every lane table, code 11

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 1653–1663: **replace** (11 lines → 36; new lines 1723–1758).

- **What:** the registry self-check validates `lanes[]` and, for a type
  with `lane_map`, every variant's map:
  - lane 0 = Morph (code 5);
  - no row twice, every row < descriptor count (code 6).
  - New code 11: variant 0's map must equal `lanes[]`, so a record whose
    variant is 0 and the S091 P2 `.fx` keys keep their meaning.
- **Why:** catches a malformed map at boot (DEV self-check) as well as on
  the host (CB9).

Remove (HEAD 1653–1663):

```c
        if (entry->lanes[0] != EFFECT_LANE_MORPH_SOURCE)
            return 5u;
        for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
            uint8_t descriptor_index = entry->lanes[lane];

            if (descriptor_index == EFFECT_LANE_NONE)
                continue;
            if (descriptor_index >= entry->descriptor_count ||
                (seen & ((uint64_t)1u << descriptor_index)) != 0u)
                return 6u;
            seen |= (uint64_t)1u << descriptor_index;
```

Replace with:

```c
        /*
         * S091 P3: every lane table the type can present is checked: the
         * registry's lanes[] and, for a type with a lane_map op, one map per
         * variant (CrumpBit: per sub-type). Each has lane 0 = Morph and no
         * row twice; variant 0's map must equal lanes[] (code 11), so a
         * record without the variant row and the S091 P2 `.fx` keys keep
         * their meaning.
         */
        {
            const uint8_t variants =
                (entry->ops && entry->ops->lane_map)
                    ? entry->ops->lane_variant_count : 0u;
            uint8_t variant;

            for (variant = 0u; variant <= variants; variant++) {
                const uint8_t *lanes = (variant == variants)
                    ? entry->lanes : entry->ops->lane_map(variant);

                seen = 0u;
                if (lanes[0] != EFFECT_LANE_MORPH_SOURCE)
                    return 5u;
                for (lane = 1u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
                    uint8_t descriptor_index = lanes[lane];

                    if (descriptor_index == EFFECT_LANE_NONE)
                        continue;
                    if (descriptor_index >= entry->descriptor_count ||
                        (seen & ((uint64_t)1u << descriptor_index)) != 0u)
                        return 6u;
                    seen |= (uint64_t)1u << descriptor_index;
                }
            }
            if (variants != 0u &&
                memcmp(entry->ops->lane_map(0u), entry->lanes,
                       EFFECT_SEQ_LANE_COUNT) != 0)
                return 11u;
```

#### M13 — `effects_service()`: `lanes` local

**Location:** `Core/DSP/Effects/EffectsManager.c`, **add** after HEAD line 1919. New lines 2015–2015 (1 lines).

- **What:** declares `const uint8_t *lanes;`.

Add:

```c
    const uint8_t *lanes;
```

#### M14 — `effects_service()`: the record's lane table

**Location:** `Core/DSP/Effects/EffectsManager.c`, **add** after HEAD line 1957. New lines 2054–2055 (2 lines).

- **What:** `lanes = effects_laneTable(record);` once per pass.

Add:

```c
    /* S091 P3: this record's lane table (per stored sub-type). */
    lanes = effects_laneTable(record);
```

#### M15 — `effects_service()`: locks through `lanes`

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 1977–1977: **replace** (1 lines → 1; new lines 2075–2075).

- **What:** `effects_seqOverride(lanes, …)`.

Remove (HEAD 1977–1977):

```c
                (void)effects_seqOverride(entry, step, index, &value);
```

Replace with:

```c
                (void)effects_seqOverride(lanes, step, index, &value);
```

#### M16 — `effects_service()`: runtime-aware clamp, BUFFER_DEPENDENT re-apply

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 1990–1995: **replace** (6 lines → 18; new lines 2088–2105).

- **What:**
  - The BUFFER_DEPENDENT clamp calls `effective_max(effects_runtimeMember(),
    …)`, so it follows the resolved sub-engine.
  - A BUFFER_DEPENDENT row is written every block whether or not its value
    changed: the `last_applied` skip is bypassed for it.
- **Why:** choice 1.
  - A sub-type switch or a share change re-clamps `rte` one block later,
    with no new manager state; the 80 B `effects_state_t` guard is
    untouched.
  - Cost: one `crumpBit_tableLimit()` (63) per block.
- **Proof:** CB11 runs the same write every block.

Remove (HEAD 1990–1995):

```c
            uint8_t limit = entry->ops->effective_max(index, &share);

            if (value > limit)
                value = limit;
        }
        if (!effects_state.force_all &&
```

Replace with:

```c
            /*
             * S091 P3: the live runtime decides the law (CrumpBit clamps by
             * its resolved sub-engine).
             */
            uint8_t limit = entry->ops->effective_max(effects_runtimeMember(),
                                                      index, &share);

            if (value > limit)
                value = limit;
        }
        /*
         * S091 P3: a BUFFER_DEPENDENT row is re-applied every block, changed
         * or not, so its clamp follows a sub-type switch or a share change
         * one block later without extra manager state (≈ 10 instructions per
         * block for CrumpBit's `rte`).
         */
        if (!effects_state.force_all &&
            (descriptor->effect_flags & EFFECT_PARAM_FLAG_BUFFER_DEPENDENT) == 0u &&
```

### 3.8 `Core/Hardware/SD/storageTypes.c`

#### S1 — `.fx` parse: lane keys through the record

**Location:** `Core/Hardware/SD/storageTypes.c`, HEAD lines 1494–1495: **replace** (2 lines → 7; new lines 1494–1500).

- **What:** `effects_laneByFileKey(target, …)` with the record being
  parsed.
- **Why:** by the time `[sequence]` is read, the record holds its stored
  `sub` (the writer emits `[params]` first), so lane keys map through the
  right sub-type's table.
- **Effect:** `grn` lane keys load to the `grn` lanes.

Remove (HEAD 1494–1495):

```c
            if (!effects_laneByFileKey(
                    state->type, key + STORAGE_EFFECT_LANE_PREFIX_LEN,
```

Replace with:

```c
            /*
             * S091 P3: lane keys map through the record's lane table. The
             * writer emits [params] before [sequence], so `target` already
             * holds its stored sub-type here.
             */
            if (!effects_laneByFileKey(
                    target, key + STORAGE_EFFECT_LANE_PREFIX_LEN,
```

#### S2 — `storage_effectNamedLane()` by record

**Location:** `Core/Hardware/SD/storageTypes.c`, HEAD lines 1551–1558: **replace** (8 lines → 9; new lines 1556–1564).

- **What:** the writer's named-lane ordinal walk uses the record's keys.

Remove (HEAD 1551–1558):

```c
static uint8_t storage_effectNamedLane(effect_type_id_t type,
                                       uint8_t ordinal, uint8_t *lane_out)
{
    uint8_t lane;
    uint8_t seen = 0u;

    for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (!effects_laneFileKey(type, lane))
```

Replace with:

```c
static uint8_t storage_effectNamedLane(const effect_record_t *record,
                                       uint8_t ordinal, uint8_t *lane_out)
{
    uint8_t lane;
    uint8_t seen = 0u;

    /* S091 P3: the record's lane table (per stored sub-type for CrumpBit). */
    for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
        if (!effects_laneFileKey(record, lane))
```

#### S3 — `.fx` write: lane line keys by record

**Location:** `Core/Hardware/SD/storageTypes.c`, HEAD lines 1684–1687: **replace** (4 lines → 4; new lines 1690–1693).

- **What:** the lane line formatter passes the record to
  `storage_effectNamedLane()` and `effects_laneFileKey()`.
- **Why:** a `grn` Scene saves `lane.crump_grain_st2`, not
  `lane.crump_feedback`.

Remove (HEAD 1684–1687):

```c
        !storage_effectNamedLane(type, (uint8_t)line_index, &index))
        return 0u;
    return storage_formatEffectLaneLine(dst, capacity, record, index,
                                        effects_laneFileKey(type, index));
```

Replace with:

```c
        !storage_effectNamedLane(record, (uint8_t)line_index, &index))
        return 0u;
    return storage_formatEffectLaneLine(dst, capacity, record, index,
                                        effects_laneFileKey(record, index));
```

### 3.9 `Core/Menu/menu.c`

#### N1 — `menu_formatEffectAutomationValue3()`: type text first

**Location:** `Core/Menu/menu.c`, HEAD lines 14030–14031: **replace** (2 lines → 9; new lines 14030–14038).

- **What:** the function gains `scene` and `target` and tries
  `effects_formatTargetValue3()` first. CrumpBit's `sub` then shows `dly
  cmb pit grn rvb` on the STEP automation page and in Pattern value entry
  (OQ5); everything else keeps the generic text.

Remove (HEAD 14030–14031):

```c
    const effect_param_descriptor_t *descriptor, uint8_t value, char *buf)
{
```

Replace with:

```c
    uint8_t scene, uint16_t target,
    const effect_param_descriptor_t *descriptor, uint8_t value, char *buf)
{
    /*
     * S091 P3 (OQ5): the Scene type may name the value (CrumpBit's `sub`:
     * dly cmb pit grn rvb instead of 0..4); otherwise the generic text.
     */
    if (effects_formatTargetValue3(scene, target, value, buf))
        return;
```

#### N2 — Call site 1: pass Scene and target

**Location:** `Core/Menu/menu.c`, **add** after HEAD line 14801. New lines 14809–14809 (1 lines).

- **What:** adds `scene, target,` to the first caller's argument list.

Add:

```c
                        scene, target,
```

#### N3 — Call site 2: pass Scene and target

**Location:** `Core/Menu/menu.c`, **add** after HEAD line 14909. New lines 14918–14918 (1 lines).

- **What:** the same for the second caller.

Add:

```c
                scene, target,
```

#### N4 — Held-cell lane lookup by the active Scene's record

**Location:** `Core/Menu/menu.c`, HEAD lines 17256–17257: **replace** (2 lines → 3; new lines 17265–17267).

- **What:** `effects_laneOfParam(scene_effectConst(scene_getActiveIndex()),
  …)`.
- **Why:** the lane of a held Effect cell depends on the stored `sub`.

Remove (HEAD 17256–17257):

```c
    (void)effects_laneOfParam(effects_activeType(), cell.fx.index,
                               &out->fx_lane);
```

Replace with:

```c
    /* S091 P3: the active Scene record's lane table (per sub-type). */
    (void)effects_laneOfParam(scene_effectConst(scene_getActiveIndex()),
                              cell.fx.index, &out->fx_lane);
```

### 3.10 `Core/Menu/menuEffects.c`

#### U1 — `menuEffects_layout()`

**Location:** `Core/Menu/menuEffects.c`, **add** after HEAD line 104. New lines 105–125 (21 lines).

- **What:** returns the type's `layout()` hook result when it has one and
  it is non-NULL, else `entry->select_layout`. `menuEffects_hooks()` is
  forward-declared for it.
- **Why:** screen count, cell lookup, custom row 0 and home must all read
  the same layout, or a sub-type change would mix screens.
- **Callers:** U2, U3, U5, U6.

Add:

```c
/*
 * The select layout to show now (S091 P3).
 *
 * What: the type's layout hook when it has one (CrumpBit: per shown
 * sub-type), else the registry's select_layout; NULL = the default layout.
 * Why one accessor: the screen count, cell lookup, custom top row and home
 * screen must all read the same layout, or a sub-type change would mix
 * screens. Output: the layout or NULL. Callers: menuEffects_screenCount(),
 * menuEffects_cellAt(), menuEffects_screenHasCustomRow0(), menuEffects_home().
 */
static const effect_ui_hooks_t *menuEffects_hooks(void);

static const effect_select_layout_t *menuEffects_layout(void)
{
    const effect_registry_entry_t *entry = menuEffects_entry();
    const effect_select_layout_t *layout =
        (entry->ui && entry->ui->layout) ? entry->ui->layout() : NULL;

    return layout ? layout : entry->select_layout;
}

```

#### U2 — Screen count through the accessor

**Location:** `Core/Menu/menuEffects.c`, HEAD lines 133–134: **replace** (2 lines → 2; new lines 154–155).

- **What:** `menuEffects_screenCount()` reads `menuEffects_layout()`.

Remove (HEAD 133–134):

```c
    if (entry->select_layout) {
        screens = entry->select_layout->screen_count[sub_page];
```

Replace with:

```c
    if (menuEffects_layout()) {
        screens = menuEffects_layout()->screen_count[sub_page];
```

#### U3 — Cell lookup through the accessor

**Location:** `Core/Menu/menuEffects.c`, HEAD lines 174–175: **replace** (2 lines → 2; new lines 195–196).

- **What:** `menuEffects_cellAt()` reads `menuEffects_layout()`.

Remove (HEAD 174–175):

```c
    } else if (entry->select_layout) {
        index = entry->select_layout->cells[sub_page][screen][column];
```

Replace with:

```c
    } else if (menuEffects_layout()) {
        index = menuEffects_layout()->cells[sub_page][screen][column];
```

#### U4 — Short name: `name3` hook first

**Location:** `Core/Menu/menuEffects.c`, HEAD lines 539–542: **replace** (4 lines → 12; new lines 560–571).

- **What:** for a parameter cell, `hooks->name3(index, dst)` may write the
  name. Otherwise the descriptor's short name is used, as before.
- **Why:** S2. The Effect page shows `win`, `div`, `dcy` and so on.

Remove (HEAD 539–542):

```c
    case MENU_FX_CELL_PARAM:
        if (cell->descriptor && cell->descriptor->short_name)
            text = cell->descriptor->short_name;
        break;
```

Replace with:

```c
    case MENU_FX_CELL_PARAM: {
        const effect_ui_hooks_t *hooks = menuEffects_hooks();

        /*
         * S091 P3: a type may rename its row on this page (CrumpBit's
         * per-sub-type names); otherwise the descriptor's short name.
         */
        if (hooks && hooks->name3 && hooks->name3(cell->index, dst))
            return;
        if (cell->descriptor && cell->descriptor->short_name)
            text = cell->descriptor->short_name;
        break; }
```

#### U5 — Custom row 0 through the accessor

**Location:** `Core/Menu/menuEffects.c`, HEAD lines 894–897: **replace** (4 lines → 4; new lines 923–926).

- **What:** `menuEffects_screenHasCustomRow0()` reads
  `menuEffects_layout()`.

Remove (HEAD 894–897):

```c
        !entry->select_layout || !hooks || !hooks->paint_row0)
        return 0u;
    screen = menuEffects_screen[sub_page];
    return (uint8_t)((entry->select_layout->custom_row0[sub_page] &
```

Replace with:

```c
        !menuEffects_layout() || !hooks || !hooks->paint_row0)
        return 0u;
    screen = menuEffects_screen[sub_page];
    return (uint8_t)((menuEffects_layout()->custom_row0[sub_page] &
```

#### U6 — Home screen through the accessor

**Location:** `Core/Menu/menuEffects.c`, HEAD lines 967–967: **replace** (1 lines → 1; new lines 996–996).

- **What:** `menuEffects_home()` reads `menuEffects_layout()`.

Remove (HEAD 967–967):

```c
    const effect_select_layout_t *layout = menuEffects_entry()->select_layout;
```

Replace with:

```c
    const effect_select_layout_t *layout = menuEffects_layout();
```

#### U7 — `menuEffects_cellLane()` by record

**Location:** `Core/Menu/menuEffects.c`, HEAD lines 1011–1011: **replace** (1 lines → 1; new lines 1040–1040).

- **What:** `effects_laneOfParam(record, …)`: the lane of a page cell
  (lane LEDs, live record) follows the stored `sub`.

Remove (HEAD 1011–1011):

```c
    return effects_laneOfParam(record->type, cell->index, lane);
```

Replace with:

```c
    return effects_laneOfParam(record, cell->index, lane);
```

#### U8 — `menuEffects_shownParam()` by record

**Location:** `Core/Menu/menuEffects.c`, HEAD lines 1106–1106: **replace** (1 lines → 1; new lines 1135–1135).

- **What:** `effects_laneOfParam(record, …)`: while a step is held, the
  shown value is that lane's lock.

Remove (HEAD 1106–1106):

```c
        effects_laneOfParam(record->type, index, &lane) &&
```

Replace with:

```c
        effects_laneOfParam(record, index, &lane) &&
```

#### U9 — `menuEffects_editParam()` by record

**Location:** `Core/Menu/menuEffects.c`, HEAD lines 1131–1131: **replace** (1 lines → 1; new lines 1160–1160).

- **What:** `effects_laneOfParam(record, …)`: an edit while steps are held
  writes the lock of the lane this sub-type maps the row to.

Remove (HEAD 1131–1131):

```c
            !effects_laneOfParam(record->type, index, &lane))
```

Replace with:

```c
            !effects_laneOfParam(record, index, &lane))
```

---

## 4. Tools and host tests

### 4.1 `tools/plots/gen_crumpbit_tables.py`

#### G1 — Generator docstring: N1 Rate top

**Location:** `tools/plots/gen_crumpbit_tables.py`, HEAD lines 2–11: **replace** (10 lines → 14; new lines 2–15).

- **What:** records `rateTable`'s top, 126,654 = the 126,656 B arena less
  the guard (N1), and that the 88 B runtime may cost a granule depending
  on LTO packing.

Remove (HEAD 2–11):

```python
"""Generate the CrumpBit control tables (S091 P2).

What:       writes Core/DSP/Effects/CrumpBit/CrumpBitTables.c with three
            128-entry float tables, one per 0..127 row value:
              crumpBit_rateTable[r]  delay length in samples for `rte` r:
                                     L(r) = 882 x (126686 / 882)^(r/127),
                                     20 ms at 0 .. 2.872 s (the whole
                                     arena less the 2-sample guard) at 127,
                                     exponential (equal steps = equal time
                                     ratios); higher r = longer (P2 Q2);
```

Replace with:

```python
"""Generate the CrumpBit control tables (S091 P2; sub-type tables S091 P3).

What:       writes Core/DSP/Effects/CrumpBit/CrumpBitTables.c with three
            128-entry float tables, one per 0..127 row value:
              crumpBit_rateTable[r]  delay length in samples for `rte` r:
                                     L(r) = 882 x (126654 / 882)^(r/127),
                                     20 ms at 0 .. 2.871 s (the
                                     arena less the 2-sample guard) at 127,
                                     exponential (equal steps = equal time
                                     ratios); higher r = longer (P2 Q2).
                                     S091 P3 N1: the top assumes the arena
                                     less one 32 B granule (126,656 B; the
                                     92 B runtime may cost a granule,
                                     depending on LTO's DTCM packing);
```

#### G2 — Generator docstring: six new tables and checks

**Location:** `tools/plots/gen_crumpbit_tables.py`, HEAD lines 22–31: **replace** (10 lines → 32; new lines 26–57).

- **What:** documents each new table's law and the checks:
  - **comb:** `fs/f(112 − min(n, 96))`, non-decreasing, ≥ 8 samples;
  - **pitch window:** `10 ms·20^(r/127)`, strictly increasing;
  - **semitone ratio:** `2^((s − 24)/12)`, 49 entries, 1.0 at 24;
  - **grain fraction:** `2^(−5(1 − d/64))`, 65 entries, 1.0 at 64;
  - **reverb size:** `0.25·10^(r/127)`, strictly increasing;
  - **reverb decay:** `−3·log2(10)/(T·fs)` with `T = 0.1 s·100^(f/127)`,
    negative and increasing;
  - `--check` regenerates in memory and compares.

Remove (HEAD 22–31):

```python
                                     valueShaperF2F(v/127, FILTER_SHAPER).
Why:        crumpBit_process() runs inside mixer_calcNextSampleBlock(), so
            per-block expf/logf/fastTan cost render time. Lookups are
            constant-cost, and the Rate table also lets the share clamp
            (crumpBit_effectiveMax) and the DSP use the identical lengths.
Checks:     C1 rate[0] = 882 and rate[127] = 126686 (to 1e-3), strictly
            increasing; C2 toneK in (0, 1], toneK[63] == 1, low-pass side
            increasing, high-pass side increasing; C3 filterG[0] = 0
            (frq 0 closes the filter, as on a voice) and strictly
            increasing; C4 --check: regenerate in memory and
```

Replace with:

```python
                                     valueShaperF2F(v/127, FILTER_SHAPER);
            S091 P3 sub-type tables (one entry per row value unless noted):
              crumpBit_combTable[n]  `cmb` comb length in samples: rte n
                                     plays MIDI note 112 - n (5.27 kHz at 0
                                     .. 20.6 Hz at 96), L = fs / f(note),
                                     so a higher rte is a longer comb and a
                                     lower note (A1); n > 96 repeats 96;
              crumpBit_pitchWindow[r] `pit` window W in samples, 10 ms x
                                     20^(r/127): 441 .. 8,822 (10..200 ms);
              crumpBit_semitoneRatio[s] (49 entries) 2^((s - 24)/12), the
                                     pitch ratio of s - 24 semitones;
              crumpBit_grainFraction[d] (65 entries) 2^(-5 (1 - d/64)): the
                                     `grn` grain size as a fraction of the
                                     synced time R, 1/32 .. 1, d = |sze-63|;
              crumpBit_reverbSize[r] `rvb` size factor 0.25 x 10^(r/127)
                                     (0.25 .. 2.5) for the four base delays;
              crumpBit_reverbDecay[f] `rvb` log2 of the per-sample line gain
                                     for RT60 T(f) = 0.1 s x 100^(f/127)
                                     (0.1 .. 10 s): -3 log2(10) / (T fs).
Why:        crumpBit_process() runs inside mixer_calcNextSampleBlock(), so
            per-block expf/logf/fastTan cost render time. Lookups are
            constant-cost, and the Rate table also lets the share clamp
            (crumpBit_effectiveMax) and the DSP use the identical lengths.
Checks:     C1 rate[0] = 882 and rate[127] = 126654 (to 1e-3), strictly
            increasing; C2 toneK in (0, 1], toneK[63] == 1, low-pass side
            increasing, high-pass side increasing; C3 filterG[0] = 0
            (frq 0 closes the filter, as on a voice) and strictly
            increasing; C4 (S091 P3) comb lengths non-decreasing and
            >= 8 samples, pitch windows strictly increasing, semitone
            ratio 1.0 at index 24, grain fraction 1.0 at 64, reverb sizes
            strictly increasing, decay log-gains negative and increasing
            (longer RT60, gain nearer 1); C5 --check: regenerate in memory and
```

#### G3 — Generator constants

**Location:** `tools/plots/gen_crumpbit_tables.py`, HEAD lines 47–52: **replace** (6 lines → 10; new lines 73–82).

- **What:** `RATE_MAX` 126,654 (N1) and the law constants of the new
  tables.

Remove (HEAD 47–52):

```python
RATE_MIN, RATE_MAX = 882.0, 126686.0
TONE_LP_LO, TONE_LP_HI = 150.0, 18000.0
TONE_HP_LO, TONE_HP_HI = 20.0, 2000.0
TONE_FLAT = 63
FILTER_SHAPER = -0.9
PATH = "Core/DSP/Effects/CrumpBit/CrumpBitTables.c"
```

Replace with:

```python
RATE_MIN, RATE_MAX = 882.0, 126654.0
TONE_LP_LO, TONE_LP_HI = 150.0, 18000.0
TONE_HP_LO, TONE_HP_HI = 20.0, 2000.0
TONE_FLAT = 63
FILTER_SHAPER = -0.9
PATH = "Core/DSP/Effects/CrumpBit/CrumpBitTables.c"
COMB_TOP_NOTE, COMB_STEPS = 112, 96
PIT_W_MIN, PIT_W_RATIO = 0.010, 20.0
RVB_SIZE_MIN, RVB_SIZE_RATIO = 0.25, 10.0
RVB_T_MIN, RVB_T_RATIO = 0.1, 100.0
```

#### G4 — Generator: new tables, generic renderer, checks

**Location:** `tools/plots/gen_crumpbit_tables.py`, HEAD lines 93–128: **replace** (36 lines → 87; new lines 123–209).

- **What:**
  - six table functions;
  - `render_table()` generalised to any length (the last row comment ends
    at `n − 1`);
  - `render()` appends the six tables after the three P2 ones;
  - `main()` adds the C4 checks.
- **Why:** the generated `CrumpBitTables.c` is the only source of these
  numbers. The firmware and the host test read the same file, and
  `crumpbit_tables` keeps it equal to the generator.

Remove (HEAD 93–128):

```python
def render_table(name, comment, vals):
    lines = ["", "/* %s */" % comment, "const float %s[128] = {" % name]
    for i in range(0, 128, 4):
        lines.append("    " + ", ".join("%.9ef" % x for x in vals[i:i + 4]) +
                     ",  /* %3d..%3d */" % (i, i + 3))
    lines.append("};")
    return lines


def render(rate, tone, filt):
    lines = ["/* Generated by tools/plots/gen_crumpbit_tables.py (S091 P2). Do not edit. */",
             '#include "CrumpBitTables.h"']
    lines += render_table("crumpBit_rateTable",
                          "Delay length in samples, rte 0..127: 20 ms .. 2.872 s.", rate)
    lines += render_table("crumpBit_toneK",
                          "Tone tilt pole, ton 0..127: LP 150 Hz..18 kHz, 63 flat, HP 20 Hz..2 kHz.", tone)
    lines += render_table("crumpBit_filterG",
                          "Output filter TPT g, frq 0..127 (voice cutoff law).", filt)
    return "\n".join(lines) + "\n"


def main():
    rate, tone, filt = rate_table(), tone_table(), filter_table()
    ok = abs(rate[0] - RATE_MIN) < 1e-3 and abs(rate[127] - RATE_MAX) < 1e-3
    ok = ok and all(a < b for a, b in zip(rate, rate[1:]))
    ok = ok and all(0.0 < k <= 1.0 for k in tone) and tone[TONE_FLAT] == 1.0
    ok = ok and all(a < b for a, b in zip(tone[:TONE_FLAT], tone[1:TONE_FLAT]))
    ok = ok and all(a < b for a, b in zip(tone[64:], tone[65:]))
    ok = ok and filt[0] == 0.0 and all(a < b for a, b in zip(filt, filt[1:]))
    text = render(rate, tone, filt)
    if "--check" in sys.argv:
        ok = ok and open(PATH).read() == text
        print("crumpbit tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("crumpbit tables: C1-C3 failed")
```

Replace with:

```python
def comb_table():
    vals = []
    for n in range(128):
        note = COMB_TOP_NOTE - min(n, COMB_STEPS)
        vals.append(FS / (440.0 * 2.0 ** ((note - 69) / 12.0)))
    return vals


def pitch_window_table():
    return [PIT_W_MIN * PIT_W_RATIO ** (r / 127.0) * FS for r in range(128)]


def semitone_table():
    return [2.0 ** ((s - 24) / 12.0) for s in range(49)]


def grain_fraction_table():
    return [2.0 ** (-5.0 * (1.0 - d / 64.0)) for d in range(65)]


def reverb_size_table():
    return [RVB_SIZE_MIN * RVB_SIZE_RATIO ** (r / 127.0) for r in range(128)]


def reverb_decay_table():
    return [-3.0 * math.log2(10.0) / (RVB_T_MIN * RVB_T_RATIO ** (f / 127.0) * FS)
            for f in range(128)]


def render_table(name, comment, vals):
    n = len(vals)
    lines = ["", "/* %s */" % comment, "const float %s[%d] = {" % (name, n)]
    for i in range(0, n, 4):
        last = min(i + 3, n - 1)
        lines.append("    " + ", ".join("%.9ef" % x for x in vals[i:i + 4]) +
                     ",  /* %3d..%3d */" % (i, last))
    lines.append("};")
    return lines


def render(rate, tone, filt, extra):
    lines = ["/* Generated by tools/plots/gen_crumpbit_tables.py (S091 P2). Do not edit. */",
             '#include "CrumpBitTables.h"']
    lines += render_table("crumpBit_rateTable",
                          "Delay length in samples, rte 0..127: 20 ms .. 2.871 s.", rate)
    lines += render_table("crumpBit_toneK",
                          "Tone tilt pole, ton 0..127: LP 150 Hz..18 kHz, 63 flat, HP 20 Hz..2 kHz.", tone)
    lines += render_table("crumpBit_filterG",
                          "Output filter TPT g, frq 0..127 (voice cutoff law).", filt)
    comb, win, semi, frac, size, decay = extra
    lines += render_table("crumpBit_combTable",
                          "cmb comb length in samples, rte 0..127: note 112 - rte, clamped at 96.", comb)
    lines += render_table("crumpBit_pitchWindow",
                          "pit window in samples, rte 0..127: 10 ms .. 200 ms.", win)
    lines += render_table("crumpBit_semitoneRatio",
                          "Pitch ratio of (index - 24) semitones, -24 .. +24.", semi)
    lines += render_table("crumpBit_grainFraction",
                          "grn grain size as a fraction of R, |sze - 63| 0..64: 1/32 .. 1.", frac)
    lines += render_table("crumpBit_reverbSize",
                          "rvb size factor, rte 0..127: 0.25 .. 2.5.", size)
    lines += render_table("crumpBit_reverbDecay",
                          "rvb log2 line gain per sample, fbk 0..127: RT60 0.1 s .. 10 s.", decay)
    return "\n".join(lines) + "\n"


def main():
    rate, tone, filt = rate_table(), tone_table(), filter_table()
    comb, win, semi = comb_table(), pitch_window_table(), semitone_table()
    frac, size, decay = grain_fraction_table(), reverb_size_table(), reverb_decay_table()
    ok = abs(rate[0] - RATE_MIN) < 1e-3 and abs(rate[127] - RATE_MAX) < 1e-3
    ok = ok and all(a < b for a, b in zip(rate, rate[1:]))
    ok = ok and all(0.0 < k <= 1.0 for k in tone) and tone[TONE_FLAT] == 1.0
    ok = ok and all(a < b for a, b in zip(tone[:TONE_FLAT], tone[1:TONE_FLAT]))
    ok = ok and all(a < b for a, b in zip(tone[64:], tone[65:]))
    ok = ok and filt[0] == 0.0 and all(a < b for a, b in zip(filt, filt[1:]))
    ok = ok and comb[0] >= 8.0 and all(a <= b for a, b in zip(comb, comb[1:]))
    ok = ok and all(a < b for a, b in zip(win, win[1:]))
    ok = ok and semi[24] == 1.0 and frac[64] == 1.0
    ok = ok and all(a < b for a, b in zip(size, size[1:]))
    ok = ok and all(x < 0.0 for x in decay) and all(a < b for a, b in zip(decay, decay[1:]))
    text = render(rate, tone, filt, (comb, win, semi, frac, size, decay))
    if "--check" in sys.argv:
        ok = ok and open(PATH).read() == text
        print("crumpbit tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("crumpbit tables: C1-C4 failed")
```

### 4.2 `tools/dsp_test/elf_looppath.py`

#### L1 — `elf_looppath.py`: document `--cond-heads`

**Location:** `tools/dsp_test/elf_looppath.py`, HEAD lines 20–20: **replace** (1 lines → 6; new lines 20–25).

- **What:** the docstring explains the option.
  - Only targets of CONDITIONAL backward branches are loop heads.
  - `crumpBit_runGrain()`'s cold blocks sit after its loops and jump back
    with an unconditional `b`; without the option, every such target seeds
    a combinatorial path search that never finishes.
  - A frame loop always closes with a conditional branch.

Remove (HEAD 20–20):

```python
            path).
```

Replace with:

```python
            path), --cond-heads (S091 P3: only targets of CONDITIONAL
            backward branches are loop heads; a function whose cold blocks
            are placed after its loops and jump back with an unconditional
            `b` would otherwise seed a path search from every such target,
            which explodes combinatorially; a frame loop always closes
            with a conditional branch).
```

#### L2 — `--cond-heads` argument

**Location:** `tools/dsp_test/elf_looppath.py`, **add** after HEAD line 100. New lines 106–106 (1 lines).

- **What:** `ap.add_argument('--cond-heads', action='store_true')`.

Add:

```python
    ap.add_argument('--cond-heads', action='store_true')
```

#### L3 — Head filter

**Location:** `tools/dsp_test/elf_looppath.py`, HEAD lines 110–110: **replace** (1 lines → 2; new lines 116–117).

- **What:** with the option, branches in `UNCOND` do not create heads.
- **Default behaviour** is unchanged, so every existing gate measures the
  same.

Remove (HEAD 110–110):

```python
                    and target(ops) <= addr and target(ops) in idx})
```

Replace with:

```python
                    and target(ops) <= addr and target(ops) in idx
                    and not (a.cond_heads and op in UNCOND)})
```

### 4.3 `tools/dsp_test/Makefile`

#### T1 — Makefile header: nine breaks

**Location:** `tools/dsp_test/Makefile`, HEAD lines 12–14: **replace** (3 lines → 5; new lines 12–16).

- **What:** lists the four P3 break builds.

Remove (HEAD 12–14):

```make
#               crumpbit            modules/crumpbit.c (+ 5 breaks: P2 Sync
#                                   key, R4 AC seed, P2 flat tone, P2 seat
#                                   delay clamp, P2 Rate-limit probe)
```

Replace with:

```make
#               crumpbit            modules/crumpbit.c (+ 9 breaks: P2 Sync
#                                   key, R4 AC seed, P2 flat tone, P2 seat
#                                   delay clamp, P2 Rate-limit probe; P3
#                                   wet around the mix stage, grain freeze,
#                                   pitch direction, lane map)
```

#### T2 — Rule comment: CB1–CB15

**Location:** `tools/dsp_test/Makefile`, HEAD lines 259–260: **replace** (2 lines → 3; new lines 261–263).

- **What:** CB1–CB4 and CB10 are bit-exact.

Remove (HEAD 259–260):

```make
# S091 P1/P2: the real CrumpBitEffect.c and its generated tables against
# the references in modules/crumpbit.c (laws CB1-CB9; CB1-CB4 bit-exact).
```

Replace with:

```make
# S091 P1/P2/P3: the real CrumpBitEffect.c and its generated tables against
# the references in modules/crumpbit.c (laws CB1-CB15; CB1-CB4 and CB10
# bit-exact).
```

#### T3 — Rule comment: the P3 breaks

**Location:** `tools/dsp_test/Makefile`, HEAD lines 273–274: **replace** (2 lines → 10; new lines 276–285).

- **What:** describes breaks 6–9:
  - 6: the `rvb` wet added around the mix stage → CB10;
  - 7: freeze ignored → CB14;
  - 8: the `pit` step sign flipped → CB13;
  - 9: a granular row on a `pit` lane → CB9.

Remove (HEAD 273–274):

```make
#     only 127 entries, so the whole arena would give 126) must fail CB6.
CB_GEN := $(GEN)/cb_fxbuf.c $(GEN)/cb_effects.c $(GEN)/cb_params.c \
```

Replace with:

```make
#     only 127 entries, so the whole arena would give 126) must fail CB6;
#   - S091 P3: the `rvb` wet added around the mix stage (mix 0 no longer
#     the dry signal) must fail CB10;
#   - the `grn` freeze ignored (the record line keeps recording) must fail
#     CB14;
#   - the `pit` phase step with its sign flipped (+12 st plays -12) must
#     fail CB13;
#   - a granular row on a `pit` lane must fail CB9.
CB_GEN := $(GEN)/cb_fxbuf.c $(GEN)/cb_efftypes.c $(GEN)/cb_effects.c \
          $(GEN)/cb_params.c \
```

#### T4 — Extraction: lane macros, new parameter macros

**Location:** `tools/dsp_test/Makefile`, HEAD lines 287–293: **replace** (7 lines → 14; new lines 298–311).

- **What:**
  - `EFFECT_SEQ_LANE_COUNT` from `EffectTypes.h` (new fragment
    `cb_efftypes.c`);
  - `EFFECT_LANE_MORPH_SOURCE`/`NONE` from `EffectsManager.h`;
  - `CRUMPBIT_ROWS_SUBTYPE`, `CRUMPBIT_ROWS_GRN` and the
    `CRUMPBIT_SUBTYPE_*` ids replace `CRUMPBIT_ROWS_DELAY`.
- **Why:** the host build compiles the real `CrumpBitEffect.c`, whose lane
  maps and dispatch use them. `extract.py` fails on a missing name, so a
  rename cannot pass silently.

Remove (HEAD 287–293):

```make
	$(EX) --src $(ROOT)/Core/DSP/Effects/EffectsManager.h \
	  --typedef effect_io_t --typedef effect_type_ops_t \
	  --out $(GEN)/cb_effects.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitParameters.h \
	  --typedef crumpbit_param_t --macro CRUMPBIT_PARAM_COUNT \
	  --macro CRUMPBIT_ROW_BIT --macro CRUMPBIT_ROWS_SHARED \
	  --macro CRUMPBIT_ROWS_DELAY --macro CRUMPBIT_TONE_FLAT \
```

Replace with:

```make
	$(EX) --src $(ROOT)/Core/DSP/Effects/EffectTypes.h \
	  --macro EFFECT_SEQ_LANE_COUNT --out $(GEN)/cb_efftypes.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/EffectsManager.h \
	  --macro EFFECT_LANE_MORPH_SOURCE --macro EFFECT_LANE_NONE \
	  --typedef effect_io_t --typedef effect_type_ops_t \
	  --out $(GEN)/cb_effects.c
	$(EX) --src $(ROOT)/Core/DSP/Effects/CrumpBit/CrumpBitParameters.h \
	  --typedef crumpbit_param_t --macro CRUMPBIT_PARAM_COUNT \
	  --macro CRUMPBIT_ROW_BIT --macro CRUMPBIT_ROWS_SHARED \
	  --macro CRUMPBIT_ROWS_SUBTYPE --macro CRUMPBIT_ROWS_GRN \
	  --macro CRUMPBIT_SUBTYPE_DELAY --macro CRUMPBIT_SUBTYPE_COMB \
	  --macro CRUMPBIT_SUBTYPE_PITCH --macro CRUMPBIT_SUBTYPE_GRAIN \
	  --macro CRUMPBIT_SUBTYPE_REVERB --macro CRUMPBIT_SUBTYPE_COUNT \
	  --macro CRUMPBIT_TONE_FLAT \
```

#### T5 — Break 4 follows the seat into the union

**Location:** `tools/dsp_test/Makefile`, HEAD lines 343–343: **replace** (1 lines → 1; new lines 361–361).

- **What:** the sed target becomes `t->delay = (float)(len −
  CRUMPBIT_LOOP_GUARD);` (E5).

Remove (HEAD 343–343):

```make
	sed 's/rt->delay = (float)(len - CRUMPBIT_LOOP_GUARD);/(void)0;/' \
```

Replace with:

```make
	sed 's/t->delay = (float)(len - CRUMPBIT_LOOP_GUARD);/(void)0;/' \
```

#### T6 — Break 5 generalised; breaks 6–9; per-engine ARM gates

**Location:** `tools/dsp_test/Makefile`, HEAD lines 353–384 (end of file): **replace** (32 lines → 83; new lines 371–453).

- **What:**
  - Break 5 targets `table[count]`, the generalised probe (E4).
  - Four new break builds, each grep-checked before compiling.
  - `crumpbit_elf_check` gates:
    - every engine's frame loop at its measured value (`runDelay` 124,
      `runComb` 124, `runPitch` 135, `runGrain` 131 with `--cond-heads
      --top 2`, `runReverb` 145);
    - the stage-4 loop in `crumpBit_process` 31;
    - `crumpBit_divisionFor` 113 ≤ 117;
    - `crumpBit_tableLimit` 63 (replaces `crumpBit_rateLimit` 101, now an
      inlined wrapper).
- **Needs** `make all` at the root first; not in `all`.

Remove (HEAD 353–384):

```make
	sed 's/    count += (crumpBit_rateTable\[count\] <= max_samples) ? 1u : 0u;/    (void)0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb5/CrumpBitEffect.host.c
	@grep -q '    (void)0;' build/break_cb5/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb5 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break5 -lm
	@if ./build/crumpbit_break5 >/dev/null 2>&1; then \
	  echo "crumpbit_break5: FAILED (Rate-limit probe break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break5: caught (law CB6 fails as expected)"; fi

# S091 P2: the committed CrumpBit tables must equal the generator's output.
crumpbit_tables:
	cd $(ROOT) && python3 tools/plots/gen_crumpbit_tables.py --check

# S091 ARM gate (needs `make all` at the repository root first):
#   - every crumpBit_process() loop at or below 126 instructions per frame
#     on its longest path, with no VDIV: the stage-2 loop (ADC, AC, delay,
#     feedback return) measures 126 and the stage-3 output-filter loop 31
#     (S091 P2; the single S091 P1 loop was 98, 111 before S091). --top 3
#     prints all three heads (stage 2 is entered at two rotated heads);
#   - the unrolled Sync search crumpBit_divisionFor() at or below 117
#     instructions per block (the 128-step walk it replaced was up to
#     ~2,430), and the Rate-limit search crumpBit_rateLimit() at or below
#     101 (S091 P2; eight probes, run once per block by effects_service()
#     through effective_max).
crumpbit_elf_check:
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --top 3 --max 126
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_divisionFor --straight --max 117
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_rateLimit --straight --max 101
```

Replace with:

```make
	sed 's/    count += (table\[count\] <= max_samples) ? 1u : 0u;/    (void)0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb5/CrumpBitEffect.host.c
	@grep -q '    (void)0;' build/break_cb5/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb5 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break5 -lm
	@if ./build/crumpbit_break5 >/dev/null 2>&1; then \
	  echo "crumpbit_break5: FAILED (Rate-limit probe break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break5: caught (law CB6 fails as expected)"; fi
	mkdir -p build/break_cb6 build/break_cb7 build/break_cb8 build/break_cb9
	sed 's/left\[n\] = crumpBit_mixFrame(hl, mid_k \* (e + o) + side_k \* (e - o),/left[n] = mid_k * (e + o) + crumpBit_mixFrame(hl, side_k * (e - o),/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb6/CrumpBitEffect.host.c
	@grep -q 'left\[n\] = mid_k \* (e + o) + crumpBit_mixFrame' \
	  build/break_cb6/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb6 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break6 -lm
	@if ./build/crumpbit_break6 >/dev/null 2>&1; then \
	  echo "crumpbit_break6: FAILED (wet-around-mix break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break6: caught (law CB10 fails as expected)"; fi
	sed 's/k.rec_w = rt->sync ? k.dline : b->base;/k.rec_w = b->base;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb7/CrumpBitEffect.host.c
	@grep -q 'k.rec_w = b->base;' build/break_cb7/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb7 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break7 -lm
	@if ./build/crumpbit_break7 >/dev/null 2>&1; then \
	  echo "crumpbit_break7: FAILED (freeze break not caught)"; exit 1; \
	else echo "crumpbit_break7: caught (law CB14 fails as expected)"; fi
	sed 's/((1.0f - crumpBit_ratioOf(semis)) \/ window/((crumpBit_ratioOf(semis) - 1.0f) \/ window/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb8/CrumpBitEffect.host.c
	@grep -q '(crumpBit_ratioOf(semis) - 1.0f)' \
	  build/break_cb8/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb8 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break8 -lm
	@if ./build/crumpbit_break8 >/dev/null 2>&1; then \
	  echo "crumpbit_break8: FAILED (pitch direction break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break8: caught (law CB13 fails as expected)"; fi
	sed 's/        CRUMPBIT_PARAM_AUX, EFFECT_LANE_NONE },/        CRUMPBIT_PARAM_GRAIN_ST2, EFFECT_LANE_NONE },/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb9/CrumpBitEffect.host.c
	@grep -q 'CRUMPBIT_PARAM_GRAIN_ST2, EFFECT_LANE_NONE },' \
	  build/break_cb9/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb9 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break9 -lm
	@if ./build/crumpbit_break9 >/dev/null 2>&1; then \
	  echo "crumpbit_break9: FAILED (lane-map break not caught)"; exit 1; \
	else echo "crumpbit_break9: caught (law CB9 fails as expected)"; fi

# S091 P2: the committed CrumpBit tables must equal the generator's output.
crumpbit_tables:
	cd $(ROOT) && python3 tools/plots/gen_crumpbit_tables.py --check

# S091 ARM gate (needs `make all` at the repository root first):
#   - S091 P3: each sub-engine's frame loop (stages 1-3, one symbol each,
#     CRUMPBIT_NOINLINE) at or below its measured longest path, no VDIV:
#     `dly` 124 and `cmb` 124 (engine A; the S091 P2 loop was 126), `pit`
#     135 (Q14 integer heads), `grn` 131 (two spans around the one restart
#     a block can hold, no delay glide; --cond-heads: its cold blocks jump
#     back with an unconditional `b`, which is not a loop), `rvb` 145;
#   - crumpBit_process(): the stage-4 output-filter loop, 31 (S091 P2);
#   - the unrolled Sync search crumpBit_divisionFor() at or below 117
#     instructions per block (the 128-step walk it replaced was up to
#     ~2,430), and the table-limit search crumpBit_tableLimit() at or below
#     63 (S091 P3: the S091 P2 crumpBit_rateLimit() search generalised to
#     every sub-type's table and kept out of line; eight probes, run once
#     per block by effects_service() through effective_max).
crumpbit_elf_check:
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runDelay --max 124
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runComb --max 124
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runPitch --max 135
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runGrain --cond-heads --top 2 --max 131
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runReverb --max 145
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --max 31
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_divisionFor --straight --max 117
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_tableLimit --straight --max 63
```

### 4.4 `tools/dsp_test/modules/crumpbit.c`

#### X1 — Host test title

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 2–2: **replace** (1 lines → 2; new lines 2–3).

- **What:** names S091 P3.

Remove (HEAD 2–2):

```c
 * CrumpBit host tests (S091 P1 reductions; S091 P2 expansion).
```

Replace with:

```c
 * CrumpBit host tests (S091 P1 reductions; S091 P2 expansion; S091 P3
 * sub-types).
```

#### X2 — Header: CB4 is the `dly` law

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 17–17: **replace** (1 lines → 2; new lines 18–19).

- **What:** CB4 runs sub 0.

Remove (HEAD 17–17):

```c
 *               CB4  whole block: crumpBit_process() against the reference
```

Replace with:

```c
 *               CB4  whole block, `dly` (sub 0): crumpBit_process() against
 *                    the reference
```

#### X3 — Header: CB5, CB6, CB9 extended; CB10–CB15

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 28–46: **replace** (19 lines → 56; new lines 30–85).

- **What:** documents every new or extended law. Their bodies are X11.

Remove (HEAD 28–46):

```c
 *               CB5  init: AC memory 128, tone flat, filter `off` (and its
 *                    ramp origin `off`);
 *               CB6  tables and Rate limit: crumpBit_rateTable endpoints
 *                    and strict order, crumpBit_rateSamples() is the table,
 *                    crumpBit_rateLimit() equals a linear scan for every
 *                    share 0..CRUMPBIT_LOOP_MAX_BYTES in 32-byte steps (127
 *                    for the whole arena), the effective_max op clamps only
 *                    the Rate; toneK[63] == 1; filterG[0] == 0, increasing;
 *               CB7  output resonance bound: crumpBit_filterDamping() is in
 *                    [0.0625, 1] for every `res` (Q <= 8), 1 at res 0;
 *               CB8  stability at fbk 127 (gain 1.2): 60 s of
 *                    self-oscillation after a burst, with DC input, silence
 *                    and full-scale input; no NaN/Inf, |output| < 4, and
 *                    no DC lock-up (the loop's mean code stays within
 *                    +-8 LSB of silence);
 *               CB9  row ownership: CRUMPBIT_ROWS_SHARED and
 *                    CRUMPBIT_ROWS_DELAY are disjoint and cover rows
 *                    3..COUNT-1 (the firmware also asserts this at compile
 *                    time; the host law keeps the test honest).
```

Replace with:

```c
 *                    S091 P3: the sub-type rows (aux, st2, st3, spr, den,
 *                    amp, wid) are written too and must not change `dly`;
 *               CB5  init: AC memory 128, tone flat, filter `off` (and its
 *                    ramp origin `off`); S091 P3: sub `dly`, aux/st2/st3 63,
 *                    den 127;
 *               CB6  tables and Rate limit: crumpBit_rateTable endpoints
 *                    and strict order, crumpBit_rateSamples() is the table,
 *                    crumpBit_rateLimit() equals a linear scan for every
 *                    share 0..CRUMPBIT_LOOP_MAX_BYTES in 32-byte steps (127
 *                    for the whole arena), the effective_max op clamps only
 *                    the Rate; toneK[63] == 1; filterG[0] == 0, increasing;
 *                    S091 P3: every sub-type's Rate limit equals a linear
 *                    scan of its own length table under its own bound, the
 *                    effective_max op follows the runtime's sub-type (the
 *                    delay rule without a runtime), and the new tables'
 *                    endpoints and order (comb, pitch window, semitone
 *                    ratio, grain fraction, reverb size and decay);
 *               CB7  output resonance bound: crumpBit_filterDamping() is in
 *                    [0.0625, 1] for every `res` (Q <= 8), 1 at res 0;
 *               CB8  stability at fbk 127 (gain 1.2): 60 s of
 *                    self-oscillation after a burst, with DC input, silence
 *                    and full-scale input; no NaN/Inf, |output| < 4, and
 *                    no DC lock-up (the loop's mean code stays within
 *                    +-8 LSB of silence);
 *               CB9  row ownership: CRUMPBIT_ROWS_SHARED,
 *                    CRUMPBIT_ROWS_SUBTYPE and CRUMPBIT_ROWS_GRN are
 *                    disjoint and cover rows 3..COUNT-1 (the firmware also
 *                    asserts this at compile time); S091 P3 lane maps: the
 *                    nine shared lanes (lane 3 = mix) identical in every
 *                    map, the `dly` map equal to the S091 P2 table, every
 *                    pool lane a row of its own sub-type, no row twice;
 *               CB10 the MIX STAGE in every sub-type (binding): with `mix`
 *                    0 and the output filter `off`, the output equals the
 *                    stage-1 dry signal (S074 form) bit for bit, whatever
 *                    the sub-engine does;
 *               CB11 sub-type switch storm: 30 s of random `sub` changes
 *                    (every 1..50 blocks) with every row, share changes,
 *                    mono and short blocks, extreme input: finite, bounded,
 *                    the active engine follows `sub`, each engine's seat
 *                    invariants hold, no byte beyond the share changes;
 *               CB12 `cmb` tuning: the first echo of an impulse arrives at
 *                    crumpBit_combTable[rte] (+-2 samples); `snc` inverts
 *                    the second echo; combNote() is 112 - rte, held at 96;
 *               CB13 `pit` ratio: +-12 st (snapped) moves a 441 Hz sine's
 *                    energy to 882 / 220.5 Hz (Goertzel, >= 10x the input
 *                    frequency), 0 st keeps it;
 *               CB14 `grn`: the candidate distributions (spr 0 root only,
 *                    40 root/st2 halves, 80 thirds, 127 detune within +-35
 *                    cents), the level/pan bytes, start lags behind the
 *                    write head (headroom rule), freeze keeping the record
 *                    line bit-exact while the delay line runs, stability at
 *                    fbk 127;
 *               CB15 `rvb`: stable and bounded at every extreme of rte,
 *                    fbk, snc, ton and dpn; an RT60 of ~0.49 s (fbk 44)
 *                    decays 9..21 dB over 0.125 s; freeze holds the tail
 *                    within +-6 dB over 1 s.
```

#### X4 — Header: accessors

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 56–58: **replace** (3 lines → 4; new lines 95–98).

- **What:** nine break builds; cites this schedule.

Remove (HEAD 56–58):

```c
 * Accessors:  make -C tools/dsp_test crumpbit (four break builds follow).
 * Affiliates: CrumpBitEffect.c/.h; CrumpBitTables.c; CrumpBitParameters.h;
 *             StepScale.c; S091_P2_Implementation.md §5.
```

Replace with:

```c
 * Accessors:  make -C tools/dsp_test crumpbit (nine break builds follow).
 * Affiliates: CrumpBitEffect.c/.h; CrumpBitTables.c; CrumpBitParameters.h;
 *             StepScale.c; S091_P2_Implementation.md §5;
 *             S091_P3_Implementation.md (host laws CB4-CB15).
```

#### X5 — `GetRngValue()` stub; `cb_efftypes.c`

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 86–88: **replace** (3 lines → 12; new lines 126–137).

- **What:**
  - A deterministic LCG stands in for the hardware RNG (`int16_t`, as
    `random.h`), so `grn` runs reproducibly on the host.
  - Includes the new extracted fragment (T4).

Remove (HEAD 86–88):

```c
#include "cb_stepscale.h"
#include "cb_stepscale.c"
#include "cb_fxbuf.c"
```

Replace with:

```c
/* GetRngValue() (random.h): the hardware word `grn` draws once per block. */
static uint32_t cb_hw_rng = 0xC0FFEEu;
int16_t GetRngValue(void)
{
    cb_hw_rng = cb_hw_rng * 1664525u + 1013904223u;
    return (int16_t)(cb_hw_rng >> 16);
}

#include "cb_stepscale.h"
#include "cb_stepscale.c"
#include "cb_fxbuf.c"
#include "cb_efftypes.c"
```

#### X6 — CB4: `effective_max(rt, …)`

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 604–604: **replace** (1 lines → 1; new lines 653–653).

- **What:** the CB4 Rate write passes the runtime (MH3).

Remove (HEAD 604–604):

```c
        const uint8_t lim = crumpBit_ops.effective_max(row, share);
```

Replace with:

```c
        const uint8_t lim = crumpBit_ops.effective_max(rt, row, share);
```

#### X7 — CB4: rows 15–21 in the random writes

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 620–620: **replace** (1 lines → 6; new lines 669–674).

- **What:** the random row writes include `aux` and the six grain rows.
- **Why:** proves `dly` ignores them. The 60 s run stays bit-exact
  against the P2 reference.

Remove (HEAD 620–620):

```c
        CRUMPBIT_PARAM_FILTER_RESO };
```

Replace with:

```c
        CRUMPBIT_PARAM_FILTER_RESO,
        /* S091 P3 rows: stored, never read by `dly`. */
        CRUMPBIT_PARAM_AUX, CRUMPBIT_PARAM_GRAIN_ST2,
        CRUMPBIT_PARAM_GRAIN_ST3, CRUMPBIT_PARAM_GRAIN_SPREAD,
        CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_GRAIN_AMP,
        CRUMPBIT_PARAM_GRAIN_WIDTH };
```

#### X8 — CB5: P3 seeds

**Location:** `tools/dsp_test/modules/crumpbit.c`, **add** after HEAD line 638. New lines 693–697 (5 lines).

- **What:** after init:
  - `sub` and the active engine are `dly`;
  - `aux`/`st2`/`st3` are 63;
  - `den` is 127.

Add:

```c
    expect(rt.subtype == CRUMPBIT_SUBTYPE_DELAY &&
           rt.active_subtype == CRUMPBIT_SUBTYPE_DELAY, "CB5 sub dly");
    expect(rt.aux_raw == 63u && rt.grain_st2 == 63u && rt.grain_st3 == 63u,
           "CB5 pitch rows 0 st");
    expect(rt.grain_density == 127u, "CB5 density full");
```

#### X9 — CB4: state through `u.tap`

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 710–727: **replace** (18 lines → 22; new lines 769–790).

- **What:** the state comparison and the seat invariant read
  `rt.u.tap.*` (H1).

Remove (HEAD 710–727):

```c
        expect(rt.length == ref.length && rt.write_pos == ref.write_pos &&
               rt.valid == ref.valid && same_bits(rt.delay, ref.delay) &&
               same_bits(rt.ac_y_l, ref.ac_y_l) &&
               same_bits(rt.ac_y_r, ref.ac_y_r) &&
               same_bits((float)(rt.ac_c_l - 128) * CRUMPBIT_LSB,
                         ref.ac_x_l) &&
               same_bits((float)(rt.ac_c_r - 128) * CRUMPBIT_LSB,
                         ref.ac_x_r) &&
               same_bits(rt.dc_x, ref.dc_x) && same_bits(rt.dc_y, ref.dc_y) &&
               same_bits(rt.tone_s, ref.tone_s) &&
               same_bits(rt.svf_l1, ref.s_l1) &&
               same_bits(rt.svf_l2, ref.s_l2) &&
               same_bits(rt.svf_r1, ref.s_r1) &&
               same_bits(rt.svf_r2, ref.s_r2), msg);
        snprintf(msg, sizeof msg, "CB4 seat invariant block %u", block);
        expect(rt.length < CRUMPBIT_LOOP_MIN ||
               (rt.delay <= (float)(rt.length - 2u) &&
                rt.write_pos < rt.length && rt.valid <= rt.length), msg);
```

Replace with:

```c
        expect(rt.u.tap.length == ref.length &&
               rt.u.tap.write_pos == ref.write_pos &&
               rt.u.tap.valid == ref.valid &&
               same_bits(rt.u.tap.delay, ref.delay) &&
               same_bits(rt.ac_y_l, ref.ac_y_l) &&
               same_bits(rt.ac_y_r, ref.ac_y_r) &&
               same_bits((float)(rt.ac_c_l - 128) * CRUMPBIT_LSB,
                         ref.ac_x_l) &&
               same_bits((float)(rt.ac_c_r - 128) * CRUMPBIT_LSB,
                         ref.ac_x_r) &&
               same_bits(rt.u.tap.dc_x, ref.dc_x) &&
               same_bits(rt.u.tap.dc_y, ref.dc_y) &&
               same_bits(rt.u.tap.tone_s, ref.tone_s) &&
               same_bits(rt.svf_l1, ref.s_l1) &&
               same_bits(rt.svf_l2, ref.s_l2) &&
               same_bits(rt.svf_r1, ref.s_r1) &&
               same_bits(rt.svf_r2, ref.s_r2), msg);
        snprintf(msg, sizeof msg, "CB4 seat invariant block %u", block);
        expect(rt.u.tap.length < CRUMPBIT_LOOP_MIN ||
               (rt.u.tap.delay <= (float)(rt.u.tap.length - 2u) &&
                rt.u.tap.write_pos < rt.u.tap.length &&
                rt.u.tap.valid <= rt.u.tap.length), msg);
```

#### X10 — CB6: `effective_max(NULL, …)`

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 757–759: **replace** (3 lines → 4; new lines 820–823).

- **What:** without a runtime the clamp is the `dly` rule.

Remove (HEAD 757–759):

```c
        expect(crumpBit_ops.effective_max(CRUMPBIT_PARAM_RATE, &share) ==
               ref_rateLimit(bytes), msg);
        expect(crumpBit_ops.effective_max(CRUMPBIT_PARAM_MIX, &share) ==
```

Replace with:

```c
        expect(crumpBit_ops.effective_max(NULL, CRUMPBIT_PARAM_RATE,
                                          &share) == ref_rateLimit(bytes),
               msg);
        expect(crumpBit_ops.effective_max(NULL, CRUMPBIT_PARAM_MIX, &share) ==
```

#### X11 — CB8 through `u.tap`; CB6 sub-types; CB9; CB10–CB15

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 835–861: **replace** (27 lines → 891; new lines 899–1789).

- **What:** the new laws, in eleven sub-blocks.
- **Why:**
  - The board cannot report sample-level output, so the laws prove the
    documented behaviour on the real source.
  - Each new law has a break build that must fail it (T6).

Remove (HEAD 835–861):

```c
        for (i = 0u; i < rt.length; i++)
            sum += (int8_t)arena_new[i];
        snprintf(msg, sizeof msg, "CB8 pass %u finite", pass);
        expect(finite, msg);
        snprintf(msg, sizeof msg, "CB8 pass %u peak %.3f < 4", pass,
                 (double)peak);
        expect(peak < 4.0f, msg);
        snprintf(msg, sizeof msg, "CB8 pass %u mean code %.3f within 8",
                 pass, (double)sum / (double)rt.length);
        expect(llabs(sum) <= (int64_t)8 * (int64_t)rt.length, msg);
        printf("CB8 pass %u: peak %.3f, mean loop code %.3f LSB\n", pass,
               (double)peak, (double)sum / (double)rt.length);
    }
}

/* CB9: row ownership ------------------------------------------------------ */
static void law_cb9(void)
{
    const uint32_t rows = CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_COUNT) -
                          CRUMPBIT_ROW_BIT(EFFECT_COMMON_PARAM_COUNT);

    expect((CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_DELAY) == 0u,
           "CB9 stage masks disjoint");
    expect((CRUMPBIT_ROWS_SHARED | CRUMPBIT_ROWS_DELAY) == rows,
           "CB9 every type row has one owner");
    expect((CRUMPBIT_ROWS_DELAY & CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_TONE)) != 0u,
           "CB9 ton is a delay sub-type row");
```

Replace with the 11 sub-blocks below, in order:

##### X11.1 — CB8 reads `u.tap` (new lines 899–914)

- **What:** the loop-mean scan uses `rt.u.tap.length`.

```c
        for (i = 0u; i < rt.u.tap.length; i++)
            sum += (int8_t)arena_new[i];
        snprintf(msg, sizeof msg, "CB8 pass %u finite", pass);
        expect(finite, msg);
        snprintf(msg, sizeof msg, "CB8 pass %u peak %.3f < 4", pass,
                 (double)peak);
        expect(peak < 4.0f, msg);
        snprintf(msg, sizeof msg, "CB8 pass %u mean code %.3f within 8",
                 pass, (double)sum / (double)rt.u.tap.length);
        expect(llabs(sum) <= (int64_t)8 * (int64_t)rt.u.tap.length, msg);
        printf("CB8 pass %u: peak %.3f, mean loop code %.3f LSB\n", pass,
               (double)peak, (double)sum / (double)rt.u.tap.length);
    }
}


```

##### X11.2 — `ref_tableScan()` (new lines 915–928)

- **What:** a linear scan, the reference for every table-limit search.

```c
/* CB6 (S091 P3): sub-type Rate limits and the new tables ----------------- */

/* The longest index of an ascending table whose entry is <= max. */
static uint8_t ref_tableScan(const float *table, float max)
{
    uint8_t r = 0u;
    uint32_t i;

    for (i = 0u; i < 128u; i++)
        if (table[i] <= max)
            r = (uint8_t)i;
    return r;
}

```

##### X11.3 — `ref_subtypeLimit()`, `law_cb6_subtypes()` (new lines 929–1036)

- **What:**
  - **`ref_subtypeLimit()`:** each sub-type's bound written from the plan
    (§3).
  - **`law_cb6_subtypes()`:**
    - for every share 0..LOOP_MAX + 64 in 32 B steps and every sub-type,
      `crumpBit_subtypeRateLimit()` and the runtime-aware `effective_max`
      equal the scan;
    - `sub` 200 clamps to `rvb`;
    - the comb table equals `fs/f(note)` within 1e-4, with `combNote()`
      `112 − min(n, 96)`;
    - the endpoints of the pitch window (441/8,822), semitone ratio (¼, 1,
      2, 4), grain fraction (1/32, 1) and reverb size (0.25, 2.5);
    - the reverb decay law within 1e-5;
    - strict order where the laws require it.

```c
/*
 * Each sub-type's bound, written from the plan (§3): the delay and comb
 * curves fit the loop (length - 2); the pitch window fits behind the guard;
 * `grn` fits one line (half the share); `rvb` fits its longest line (base
 * 2,053) in the word buffer.
 */
static uint8_t ref_subtypeLimit(uint8_t sub, uint32_t bytes)
{
    const uint32_t len = (bytes > CRUMPBIT_LOOP_MAX_BYTES)
        ? CRUMPBIT_LOOP_MAX_BYTES : bytes;
    const float max = (len < CRUMPBIT_LOOP_MIN) ? 0.0f : (float)(len - 2u);
    const uint32_t half = len / 2u;
    const float grain_max = (half < 66u) ? 0.0f : (float)(half - 2u);

    switch (sub) {
    case CRUMPBIT_SUBTYPE_COMB:   return ref_tableScan(crumpBit_combTable, max);
    case CRUMPBIT_SUBTYPE_PITCH:  return ref_tableScan(crumpBit_pitchWindow,
                                                       max - 2.0f);
    case CRUMPBIT_SUBTYPE_GRAIN:  return ref_tableScan(crumpBit_rateTable,
                                                       grain_max);
    case CRUMPBIT_SUBTYPE_REVERB: return ref_tableScan(crumpBit_reverbSize,
                                      (max * 0.25f - 2.0f) / 2053.0f);
    default:                      return ref_tableScan(crumpBit_rateTable, max);
    }
}

static void law_cb6_subtypes(void)
{
    CrumpBitRuntime rt;
    uint32_t bytes;
    uint32_t sub;
    uint32_t i;
    char msg[96];

    memset(&rt, 0, sizeof rt);
    crumpBit_ops.init(&rt, NULL);
    for (sub = 0u; sub < CRUMPBIT_SUBTYPE_COUNT; sub++) {
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_SUBTYPE, (uint8_t)sub);
        for (bytes = 0u; bytes <= CRUMPBIT_LOOP_MAX_BYTES + 64u;
             bytes += 32u) {
            fx_share_t share = { NULL, 0u, bytes };

            snprintf(msg, sizeof msg, "CB6 sub %u limit bytes %u", sub,
                     bytes);
            expect(crumpBit_subtypeRateLimit((uint8_t)sub, &share) ==
                   ref_subtypeLimit((uint8_t)sub, bytes), msg);
            expect(crumpBit_ops.effective_max(&rt, CRUMPBIT_PARAM_RATE,
                                              &share) ==
                   ref_subtypeLimit((uint8_t)sub, bytes), msg);
        }
    }
    /* sub above the last engine clamps to `rvb`. */
    crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_SUBTYPE, 200u);
    expect(rt.subtype == CRUMPBIT_SUBTYPE_COUNT - 1u, "CB6 sub clamps");

    /* Comb: L = fs / f(note), note = 112 - rte held at 96 (note 16). */
    for (i = 0u; i < 128u; i++) {
        const uint32_t note = 112u - ((i > 96u) ? 96u : i);
        const double f = 440.0 * pow(2.0, ((double)note - 69.0) / 12.0);

        snprintf(msg, sizeof msg, "CB6 comb %u", i);
        expect(crumpBit_combNote((uint8_t)i) == note, msg);
        expect(fabs(crumpBit_combTable[i] - 44108.0 / f) <=
               1e-4 * (44108.0 / f), msg);
        if (i)
            expect(crumpBit_combTable[i] >= crumpBit_combTable[i - 1u], msg);
    }
    /* Pitch window 10..200 ms; the other new tables' endpoints/order. */
    expect(fabsf(crumpBit_pitchWindow[0] - 441.08f) < 0.5f, "CB6 pit 10 ms");
    expect(fabsf(crumpBit_pitchWindow[127] - 8821.6f) < 0.5f,
           "CB6 pit 200 ms");
    expect(crumpBit_semitoneRatio[CRUMPBIT_SEMITONE_SPAN] == 1.0f,
           "CB6 ratio 0 st = 1");
    expect(fabsf(crumpBit_semitoneRatio[CRUMPBIT_SEMITONE_SPAN + 12u] -
                 2.0f) < 1e-6f, "CB6 ratio +12 = 2");
    expect(fabsf(crumpBit_semitoneRatio[0] - 0.25f) < 1e-7f,
           "CB6 ratio -24 = 1/4");
    expect(fabsf(crumpBit_semitoneRatio[2u * CRUMPBIT_SEMITONE_SPAN] -
                 4.0f) < 1e-6f, "CB6 ratio +24 = 4");
    expect(fabsf(crumpBit_grainFraction[0] - 1.0f / 32.0f) < 1e-8f,
           "CB6 grain 1/32");
    expect(crumpBit_grainFraction[CRUMPBIT_GRAIN_STEPS] == 1.0f,
           "CB6 grain 1");
    expect(fabsf(crumpBit_reverbSize[0] - 0.25f) < 1e-7f, "CB6 size 0.25");
    expect(fabsf(crumpBit_reverbSize[127] - 2.5f) < 1e-6f, "CB6 size 2.5");
    for (i = 0u; i < 128u; i++) {
        const double t = 0.1 * pow(100.0, (double)i / 127.0);

        snprintf(msg, sizeof msg, "CB6 new tables %u", i);
        expect(fabs(crumpBit_reverbDecay[i] -
                    (-3.0 * log2(10.0) / (t * 44108.0))) <=
               1e-5 * fabs(crumpBit_reverbDecay[i]), msg);
        if (i) {
            expect(crumpBit_pitchWindow[i] > crumpBit_pitchWindow[i - 1u],
                   msg);
            expect(crumpBit_reverbSize[i] > crumpBit_reverbSize[i - 1u], msg);
            expect(crumpBit_reverbDecay[i] > crumpBit_reverbDecay[i - 1u],
                   msg);
        }
        if (i && i <= 2u * CRUMPBIT_SEMITONE_SPAN)
            expect(crumpBit_semitoneRatio[i] >
                   crumpBit_semitoneRatio[i - 1u], msg);
        if (i && i <= CRUMPBIT_GRAIN_STEPS)
            expect(crumpBit_grainFraction[i] >
                   crumpBit_grainFraction[i - 1u], msg);
    }
}

```

##### X11.4 — `law_cb9()`: masks and lane maps (new lines 1037–1105)

- **What:**
  - **Masks:** disjoint and covering; `ton` a sub-type row; `mix` shared.
  - **Variant metadata:** `lane_variant_row/count` are `sub` and 5.
  - **The `dly` map** equals the literal S091 P2 table.
  - **Every map:**
    - the nine shared lanes equal P2's;
    - every row valid and not `sub`;
    - no row twice;
    - granular rows only in `grn`;
    - `aux` only in `pit`/`grn`.

```c
/* CB9: row ownership and lane maps ---------------------------------------- */
static void law_cb9(void)
{
    /* The S091 P2 CrumpBit lane table (EffectsManager.c at 080b1507). */
    static const uint8_t p2_lanes[EFFECT_SEQ_LANE_COUNT] = {
        EFFECT_LANE_MORPH_SOURCE, CRUMPBIT_PARAM_BIT_OFF,
        CRUMPBIT_PARAM_BIT_INVERT, CRUMPBIT_PARAM_MIX,
        CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_SYNC,
        CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN, CRUMPBIT_PARAM_DLY_PAN,
        CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_FILTER_TYPE,
        CRUMPBIT_PARAM_FILTER_FREQ, CRUMPBIT_PARAM_FILTER_RESO,
        EFFECT_LANE_NONE, EFFECT_LANE_NONE };
    /* The nine shared lanes (plan §2.9): fixed in every map. */
    static const uint8_t shared_lanes[] = { 0u, 1u, 2u, 3u, 7u, 8u, 11u, 12u,
                                            13u };
    const uint32_t rows = CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_COUNT) -
                          CRUMPBIT_ROW_BIT(EFFECT_COMMON_PARAM_COUNT);
    uint32_t v;
    uint32_t lane;
    char msg[96];

    expect((CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_SUBTYPE) == 0u &&
           (CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROWS_GRN) == 0u &&
           (CRUMPBIT_ROWS_SUBTYPE & CRUMPBIT_ROWS_GRN) == 0u,
           "CB9 stage masks disjoint");
    expect((CRUMPBIT_ROWS_SHARED | CRUMPBIT_ROWS_SUBTYPE |
            CRUMPBIT_ROWS_GRN) == rows, "CB9 every type row has one owner");
    expect((CRUMPBIT_ROWS_SUBTYPE &
            CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_TONE)) != 0u,
           "CB9 ton is a sub-type row");
    expect((CRUMPBIT_ROWS_SHARED & CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_MIX)) != 0u,
           "CB9 mix is shared");
    expect(crumpBit_ops.lane_variant_row == CRUMPBIT_PARAM_SUBTYPE &&
           crumpBit_ops.lane_variant_count == CRUMPBIT_SUBTYPE_COUNT,
           "CB9 lane variants are the sub-types");
    expect(memcmp(crumpBit_laneMap(CRUMPBIT_SUBTYPE_DELAY), p2_lanes,
                  sizeof p2_lanes) == 0, "CB9 dly map = S091 P2 table");
    for (v = 0u; v < CRUMPBIT_SUBTYPE_COUNT; v++) {
        const uint8_t *map = crumpBit_ops.lane_map((uint8_t)v);
        uint32_t seen = 0u;
        uint32_t k;

        for (k = 0u; k < sizeof shared_lanes; k++) {
            snprintf(msg, sizeof msg, "CB9 sub %u shared lane %u", v,
                     shared_lanes[k]);
            expect(map[shared_lanes[k]] == p2_lanes[shared_lanes[k]], msg);
        }
        for (lane = 0u; lane < EFFECT_SEQ_LANE_COUNT; lane++) {
            const uint8_t row = map[lane];

            if (row == EFFECT_LANE_NONE || row == EFFECT_LANE_MORPH_SOURCE)
                continue;
            snprintf(msg, sizeof msg, "CB9 sub %u lane %u row %u", v, lane,
                     row);
            expect(row < CRUMPBIT_PARAM_COUNT, msg);
            expect(row != CRUMPBIT_PARAM_SUBTYPE, msg);  /* never a lane */
            expect((seen & CRUMPBIT_ROW_BIT(row)) == 0u, msg);
            seen |= CRUMPBIT_ROW_BIT(row);
            /* Granular rows are lanes of `grn` only. */
            if ((CRUMPBIT_ROWS_GRN & CRUMPBIT_ROW_BIT(row)) != 0u)
                expect(v == CRUMPBIT_SUBTYPE_GRAIN, msg);
            /* `aux` is a lane of the sub-types that read it. */
            if (row == CRUMPBIT_PARAM_AUX)
                expect(v == CRUMPBIT_SUBTYPE_PITCH ||
                       v == CRUMPBIT_SUBTYPE_GRAIN, msg);
        }
    }
}

```

##### X11.5 — Helpers: guard band, `p3_init()`, `p3_write()`, `p3_block()`, `goertzel()` (new lines 1106–1162)

- **What:**
  - **`arena_p3`:** the largest share plus a 256 B guard band.
  - **`p3_write()`:** clamps `rte` through the runtime-aware
    `effective_max`, as the manager does.
  - **`goertzel()`:** the power at one frequency.

```c
/* Shared helpers for CB10-CB15 -------------------------------------------- */

/* A guard band past the largest share: CB11 checks it is never written. */
#define CB_GUARD 256u
static uint8_t arena_p3[CB_ARENA + CB_GUARD];

static void p3_init(CrumpBitRuntime *rt, uint8_t sub)
{
    memset(rt, 0, sizeof *rt);
    crumpBit_ops.init(rt, NULL);
    crumpBit_ops.write_param(rt, CRUMPBIT_PARAM_SUBTYPE, sub);
    crumpBit_ops.write_param(rt, CRUMPBIT_PARAM_DLY_PAN, 63u);
}

/* Write a row, Rate through the sub-type-aware clamp (as the manager). */
static void p3_write(CrumpBitRuntime *rt, uint8_t row, uint8_t v,
                     const fx_share_t *share)
{
    if (row == CRUMPBIT_PARAM_RATE) {
        const uint8_t lim = crumpBit_ops.effective_max(rt, row, share);

        v = (v > lim) ? lim : v;
    }
    crumpBit_ops.write_param(rt, row, v);
}

static void p3_block(CrumpBitRuntime *rt, const fx_share_t *share, float *l,
                     float *r, uint32_t frames)
{
    effect_io_t io;

    io.l = l;
    io.r = r;
    io.frames = (uint8_t)frames;
    io.channels = r ? 2u : 1u;
    io.share = share;
    crumpBit_ops.process(rt, &io);
}

/* Goertzel power of x[0..n) at frequency f (Hz, fs 44,108). */
static double goertzel(const float *x, uint32_t n, double f)
{
    const double w = 2.0 * M_PI * f / 44108.0;
    const double c = 2.0 * cos(w);
    double s1 = 0.0;
    double s2 = 0.0;
    uint32_t i;

    for (i = 0u; i < n; i++) {
        const double s0 = (double)x[i] + c * s1 - s2;

        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

```

##### X11.6 — CB10: the mix stage in every sub-type (new lines 1163–1240)

- **What:**
  - For each sub-type: `mix` 0, filter `off`, 8,000 blocks of random input
    up to ±1.5, every row except `mix` randomised every 37 blocks (bits
    included), mono stretches.
  - Each output sample must equal, bit for bit, the S074-form stage-1
    reference computed alongside (float AC memories).
- **Why:** M1, binding. Mix 0 is exactly the signal without the
  sub-engine, whatever the engine does. Break 6 adds `rvb`'s mid around
  the stage.

```c
/* CB10: the mix stage, every sub-type ------------------------------------- */
static void law_cb10(void)
{
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    uint32_t sub;
    char msg[96];

    for (sub = 0u; sub < CRUMPBIT_SUBTYPE_COUNT; sub++) {
        CrumpBitRuntime rt;
        /* The S074 stage 1: float AC input memory (CB4's reference). */
        float ax_l = 0.0f, ax_r = 0.0f, ay_l = 0.0f, ay_r = 0.0f;
        float l[32], r[32], hl[32], hr[32];
        uint32_t block;

        memset(arena_p3, 0x33, sizeof arena_p3);
        p3_init(&rt, (uint8_t)sub);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 120u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 50u, &share);
        for (block = 0u; block < 8000u; block++) {
            const uint32_t mono = (block % 700u) >= 650u;
            uint32_t n;

            if ((block % 37u) == 0u) {
                /* Every row except mix (and the filter, kept `off`). */
                static const uint8_t rows[] = {
                    CRUMPBIT_PARAM_BIT_OFF, CRUMPBIT_PARAM_BIT_INVERT,
                    CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_RATE,
                    CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN,
                    CRUMPBIT_PARAM_TONE, CRUMPBIT_PARAM_AUX,
                    CRUMPBIT_PARAM_GRAIN_ST2, CRUMPBIT_PARAM_GRAIN_ST3,
                    CRUMPBIT_PARAM_GRAIN_SPREAD,
                    CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_GRAIN_AMP,
                    CRUMPBIT_PARAM_GRAIN_WIDTH };
                const uint8_t row = rows[rnd() % sizeof rows];
                uint8_t v = (uint8_t)(rnd() & 0x7Fu);

                if (row == CRUMPBIT_PARAM_BIT_OFF ||
                    row == CRUMPBIT_PARAM_BIT_INVERT)
                    v = (uint8_t)rnd();
                p3_write(&rt, row, v, &share);
            }
            for (n = 0u; n < 32u; n++) {
                l[n] = rndf(-1.5f, 1.5f);
                r[n] = rndf(-1.5f, 1.5f);
            }
            /* The dry reference, from the same inputs and masks. */
            for (n = 0u; n < 32u; n++) {
                const uint32_t invert = rt.bit_invert;
                const uint32_t keep =
                    (uint32_t)~((uint32_t)rt.bit_off & ~invert) & 0xFFu;
                const float yl = (float)((int32_t)((ref_adc(l[n]) & keep) ^
                                                   invert) - 128) *
                                 CRUMPBIT_LSB;
                const float yr = (float)((int32_t)((ref_adc(mono ? l[n]
                                                                 : r[n]) &
                                                    keep) ^ invert) - 128) *
                                 CRUMPBIT_LSB;

                hl[n] = yl - ax_l + CRUMPBIT_AC_R * ay_l;
                hr[n] = yr - ax_r + CRUMPBIT_AC_R * ay_r;
                ax_l = yl;
                ay_l = hl[n];
                ax_r = yr;
                ay_r = hr[n];
            }
            p3_block(&rt, &share, l, mono ? NULL : r, 32u);
            for (n = 0u; n < 32u; n++) {
                snprintf(msg, sizeof msg, "CB10 sub %u block %u frame %u",
                         sub, block, n);
                expect(same_bits(l[n], hl[n]), msg);
                if (!mono)
                    expect(same_bits(r[n], hr[n]), msg);
            }
        }
    }
}

```

##### X11.7 — CB11: switch storm (new lines 1241–1341)

- **What:** 30 s:
  - `sub` changes every 1–50 blocks;
  - a random row write in a quarter of the blocks;
  - share changes every 1,501 blocks (eight sizes down to 64 B);
  - mono and short blocks;
  - four input kinds including ±4 and DC;
  - the manager's per-block `rte` re-apply.
- **Checks:**
  - finite and < 64 everywhere;
  - `active_subtype == subtype` once seated;
  - per-engine invariants (positions inside the line, `valid ≤ length`,
    tape delay ≤ length − 2);
  - the 256 B guard band past the share never written.

```c
/* CB11: sub-type switch storm --------------------------------------------- */
static void law_cb11(void)
{
    static const uint32_t shares[] = {
        CRUMPBIT_LOOP_MAX_BYTES, 122272u, 100192u,
        CRUMPBIT_LOOP_MIN_SHARE_BYTES, 40000u, 4096u, 300u, 64u };
    CrumpBitRuntime rt;
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    float l[32], r[32];
    uint32_t block;
    uint32_t next_switch = 1u;
    uint32_t i;
    int finite = 1;
    char msg[128];

    memset(arena_p3, 0, sizeof arena_p3);
    memset(arena_p3 + CB_ARENA, 0xA5, CB_GUARD);
    p3_init(&rt, 0u);
    for (block = 0u; block < 41400u; block++) {          /* 30 s */
        const uint32_t mono = ((block / 613u) % 5u) == 2u;
        const uint32_t frames = ((block % 97u) == 7u) ? 1u + rnd() % 32u
                                                      : 32u;
        const uint32_t kind = (block / 300u) % 4u;
        uint32_t n;

        if (block == next_switch) {
            p3_write(&rt, CRUMPBIT_PARAM_SUBTYPE,
                     (uint8_t)(rnd() % CRUMPBIT_SUBTYPE_COUNT), &share);
            next_switch = block + 1u + rnd() % 50u;
        }
        if ((rnd() & 3u) == 0u) {
            const uint8_t row = (uint8_t)(EFFECT_COMMON_PARAM_COUNT +
                                          rnd() % (CRUMPBIT_PARAM_COUNT -
                                                   EFFECT_COMMON_PARAM_COUNT));
            uint8_t v = (uint8_t)(rnd() & 0x7Fu);

            if (row == CRUMPBIT_PARAM_FILTER_TYPE)
                v = (uint8_t)(rnd() & 7u);
            if (row == CRUMPBIT_PARAM_FILTER_RESO)
                v = (uint8_t)(rnd() % 100u);
            if (row != CRUMPBIT_PARAM_SUBTYPE)
                p3_write(&rt, row, v, &share);
        }
        if ((block % 1501u) == 1500u) {
            share.bytes = shares[rnd() % (sizeof shares / sizeof shares[0])];
            crumpBit_ops.buffer_changed(&rt, &share);
        }
        /* The manager re-applies the BUFFER_DEPENDENT Rate every block. */
        p3_write(&rt, CRUMPBIT_PARAM_RATE, rt.rate, &share);
        for (n = 0u; n < frames; n++) {
            switch (kind) {
            case 0: l[n] = rndf(-1.0f, 1.0f); r[n] = rndf(-1.0f, 1.0f); break;
            case 1: l[n] = rndf(-4.0f, 4.0f); r[n] = -l[n];             break;
            case 2: l[n] = 0.0f; r[n] = 0.0f;                           break;
            default: l[n] = 1.0f; r[n] = 1.0f;                          break;
            }
        }
        p3_block(&rt, &share, l, mono ? NULL : r, frames);
        for (n = 0u; n < frames; n++) {
            finite &= isfinite(l[n]) && fabsf(l[n]) < 64.0f;
            if (!mono)
                finite &= isfinite(r[n]) && fabsf(r[n]) < 64.0f;
        }
        snprintf(msg, sizeof msg, "CB11 block %u sub %u: invariants", block,
                 rt.subtype);
        if (share.bytes >= CRUMPBIT_LOOP_MIN)
            expect(rt.active_subtype == rt.subtype, msg);
        switch (rt.active_subtype) {
        case CRUMPBIT_SUBTYPE_GRAIN: {
            const CrumpBitGrainState *g = &rt.u.grn;

            expect(g->length == 0u ||
                   (g->write_pos < g->length && g->valid <= g->length &&
                    (g->length < 4u * CRUMPBIT_GRAIN_MIN ||
                     ((g->pos_a >> 16) < g->length &&
                      (g->pos_b >> 16) < g->length &&
                      g->age < g->length))), msg);
            break;
        }
        case CRUMPBIT_SUBTYPE_REVERB:
            expect(rt.u.rvb.length == 0u ||
                   (rt.u.rvb.write_pos < rt.u.rvb.length &&
                    rt.u.rvb.valid <= rt.u.rvb.length), msg);
            break;
        default:
            expect(rt.u.tap.length < CRUMPBIT_LOOP_MIN ||
                   (rt.u.tap.write_pos < rt.u.tap.length &&
                    rt.u.tap.valid <= rt.u.tap.length &&
                    (rt.active_subtype == CRUMPBIT_SUBTYPE_PITCH ||
                     rt.u.tap.delay <= (float)(rt.u.tap.length - 2u))), msg);
            break;
        }
    }
    expect(finite, "CB11 finite and bounded");
    for (i = 0u; i < CB_GUARD; i++)
        if (arena_p3[CB_ARENA + i] != 0xA5u) {
            expect(0, "CB11 guard band untouched");
            break;
        }
}

```

##### X11.8 — CB12: `cmb` tuning (new lines 1342–1413)

- **What:** for six `rte` values and both polarities:
  - 0.2 s of silence to settle the glide, then an impulse;
  - the first echo's peak lies within ±2 samples of
    `crumpBit_combTable[rte]`;
  - the second echo, one length later, has the opposite sign with `snc`
    on and the same sign with it off.

```c
/* CB12: `cmb` tuning ------------------------------------------------------- */
static void law_cb12(void)
{
    static const uint8_t rates[] = { 0u, 20u, 40u, 60u, 80u, 96u };
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    uint32_t k;
    uint32_t polarity;
    char msg[96];

    for (polarity = 0u; polarity < 2u; polarity++)
        for (k = 0u; k < sizeof rates; k++) {
            CrumpBitRuntime rt;
            const float len_f = crumpBit_combTable[rates[k]];
            const uint32_t span = 3u * (uint32_t)len_f + 64u;
            float *out = calloc(span + 32u, sizeof(float));
            float l[32], r[32];
            uint32_t block;
            uint32_t got = 0u;
            uint32_t peak1 = 0u;
            float best = 0.0f;
            uint32_t i;

            memset(arena_p3, 0, sizeof arena_p3);
            p3_init(&rt, CRUMPBIT_SUBTYPE_COMB);
            p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 90u, &share);
            p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
            p3_write(&rt, CRUMPBIT_PARAM_RATE, rates[k], &share);
            p3_write(&rt, CRUMPBIT_PARAM_SYNC, (uint8_t)polarity, &share);
            /* 0.2 s of silence: the 5 ms glide settles on the length. */
            for (block = 0u; block < 276u; block++) {
                memset(l, 0, sizeof l);
                memset(r, 0, sizeof r);
                p3_block(&rt, &share, l, r, 32u);
            }
            while (got < span) {
                memset(l, 0, sizeof l);
                memset(r, 0, sizeof r);
                if (got == 0u) {
                    l[0] = 0.9f;
                    r[0] = 0.9f;
                }
                p3_block(&rt, &share, l, r, 32u);
                memcpy(out + got, l, sizeof l);
                got += 32u;
            }
            for (i = (uint32_t)(0.5f * len_f); i < (uint32_t)(1.5f * len_f);
                 i++)
                if (fabsf(out[i]) > best) {
                    best = fabsf(out[i]);
                    peak1 = i;
                }
            snprintf(msg, sizeof msg, "CB12 rte %u: echo at %u, L %.1f",
                     rates[k], peak1, (double)len_f);
            expect(fabsf((float)peak1 - len_f) <= 2.0f, msg);
            {
                /* The second echo, one length later: its sign. */
                const uint32_t p2 = peak1 + (uint32_t)(len_f + 0.5f);
                float v2 = 0.0f;
                int32_t d;

                for (d = -2; d <= 2; d++)
                    if (fabsf(out[p2 + d]) > fabsf(v2))
                        v2 = out[p2 + d];
                snprintf(msg, sizeof msg, "CB12 rte %u snc %u polarity",
                         rates[k], polarity);
                expect(polarity ? (v2 * out[peak1] < 0.0f)
                                : (v2 * out[peak1] > 0.0f), msg);
            }
            free(out);
        }
}

```

##### X11.9 — CB13: `pit` ratio (new lines 1414–1466)

- **What:**
  - A 441 Hz sine for 1 s, `snc` on: `aux` 95 (+12 st), 31 (−12), 63 (0).
  - Over the last half second, the Goertzel power at 882 / 220.5 Hz is ≥
    10× that at 441 Hz.
  - At 0 st, 441 Hz is ≥ 10× 882 Hz.

```c
/* CB13: `pit` ratio --------------------------------------------------------- */
static void law_cb13(void)
{
    /* aux 95 -> +12.19 st, 31 -> -12.19 st, snapped by snc to +-12. */
    static const uint8_t aux[] = { 95u, 31u, 63u };
    static const double ratio[] = { 2.0, 0.5, 1.0 };
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    const uint32_t total = 44108u;
    float *out = calloc(total + 32u, sizeof(float));
    uint32_t k;
    char msg[96];

    for (k = 0u; k < 3u; k++) {
        CrumpBitRuntime rt;
        float l[32], r[32];
        uint32_t got = 0u;
        uint32_t t = 0u;
        double e_in;
        double e_out;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_PITCH);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 80u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_SYNC, 1u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_AUX, aux[k], &share);
        while (got < total) {
            uint32_t n;

            for (n = 0u; n < 32u; n++, t++) {
                l[n] = 0.8f * sinf(2.0f * (float)M_PI * 441.0f *
                                   (float)t / 44108.0f);
                r[n] = l[n];
            }
            p3_block(&rt, &share, l, r, 32u);
            memcpy(out + got, l, sizeof l);
            got += 32u;
        }
        /* The last half second. */
        e_in = goertzel(out + total / 2u, total / 2u, 441.0);
        e_out = goertzel(out + total / 2u, total / 2u, 441.0 * ratio[k]);
        snprintf(msg, sizeof msg, "CB13 aux %u: E(%.1f) %.3g vs E(441) %.3g",
                 aux[k], 441.0 * ratio[k], e_out, e_in);
        if (ratio[k] == 1.0)
            expect(e_in > 10.0 * goertzel(out + total / 2u, total / 2u,
                                          882.0), msg);
        else
            expect(e_out > 10.0 * e_in, msg);
    }
    free(out);
}

```

##### X11.10 — CB14: `grn` (new lines 1467–1668)

- **What:**
  - **(a)** 30,000 candidates at `spr` 0/40/80/127 with `st2` +5 and `st3`
    +9:
    - root only at 0;
    - halves within 2 % at 40;
    - thirds within 2 % at 80;
    - mostly off-slot at 127, each within ±36 cents of a slot (35 by law
      plus the linear-cent and Q3.12 error).
  - **(b)** 200,000 random picks:
    - start inside the line, at least 2 behind the head;
    - the headroom rule with the played ratio whenever it fits the line;
    - the direction from `sze`;
    - `amp` 0 → level 255, `wid` 0 → pans 64/191.
  - **(c)** Freeze after 2,000 blocks: the record half is bit-exact 2,000
    blocks later while the delay half changed.
  - **(d)** Four 20 s stability passes at `fbk` 127, `spr` 127, extreme
    rates, pitches and directions: finite, < 8.

```c
/* CB14: `grn` --------------------------------------------------------------- */
static void law_cb14(void)
{
    static const uint8_t spreads[] = { 0u, 40u, 80u, 127u };
    CrumpBitRuntime rt;
    uint32_t s;
    uint32_t i;
    char msg[128];

    /* (a) candidate pitch-slot distribution and detune bound. */
    memset(&rt, 0, sizeof rt);
    crumpBit_ops.init(&rt, NULL);
    rt.aux_raw = 63u;                   /* root 0 st */
    rt.grain_st2 = 68u;                 /* +5 st */
    rt.grain_st3 = 72u;                 /* +9 st */
    rt.tone_raw = 100u;                 /* forward */
    rt.pan_raw = 63u;                   /* follow */
    for (s = 0u; s < sizeof spreads; s++) {
        const int32_t slot_inc[3] = {
            (int32_t)(crumpBit_semitoneRatio[24] * 4096.0f + 0.5f),
            (int32_t)(crumpBit_semitoneRatio[29] * 4096.0f + 0.5f),
            (int32_t)(crumpBit_semitoneRatio[33] * 4096.0f + 0.5f) };
        uint32_t count[4] = { 0u, 0u, 0u, 0u };
        uint32_t hash = 0x12345u;
        const uint32_t trials = 30000u;

        rt.grain_spread = spreads[s];
        for (i = 0u; i < trials; i++) {
            crumpbit_grain_t c;
            uint32_t slot = 3u;
            uint32_t k;

            crumpBit_grainPick(&hash, i & 1u, &rt, 60000u, 30000u, 20000u,
                               2000u, 60000u, &c);
            for (k = 0u; k < 3u; k++)
                if (c.inc == slot_inc[k])
                    slot = k;
            if (slot == 3u) {
                /* Off-slot: only above spr 80, within +-35 cents. */
                double best = 1e9;

                for (k = 0u; k < 3u; k++) {
                    const double cents = 1200.0 *
                        log2((double)c.inc / (double)slot_inc[k]);

                    best = (fabs(cents) < fabs(best)) ? cents : best;
                }
                /* +-35 cents by the linear cent law (< 0.5 cent error at
                 * the extreme), then the Q3.12 step (<= 0.2 cent at 1). */
                snprintf(msg, sizeof msg, "CB14 spr %u detune %.2f cents",
                         spreads[s], best);
                expect(spreads[s] > 80u && fabs(best) <= 36.0, msg);
            }
            count[slot]++;
        }
        snprintf(msg, sizeof msg, "CB14 spr %u slots %u %u %u off %u",
                 spreads[s], count[0], count[1], count[2], count[3]);
        switch (spreads[s]) {
        case 0u:
            expect(count[0] == trials, msg);
            break;
        case 40u:
            expect(fabs((double)count[0] / trials - 0.5) < 0.02 &&
                   fabs((double)count[1] / trials - 0.5) < 0.02 &&
                   count[2] == 0u, msg);
            break;
        case 80u:
            expect(fabs((double)count[0] / trials - 1.0 / 3.0) < 0.02 &&
                   fabs((double)count[1] / trials - 1.0 / 3.0) < 0.02 &&
                   fabs((double)count[2] / trials - 1.0 / 3.0) < 0.02, msg);
            break;
        default:
            expect(count[3] > trials / 2u, msg);
            break;
        }
    }

    /* (b) level/pan bytes and the start lag behind the write head. */
    for (i = 0u; i < 200000u; i++) {
        crumpbit_grain_t c;
        uint32_t hash = rnd() | 1u;
        const uint32_t len = 1000u + rnd() % 62000u;
        const uint32_t window = 128u + rnd() % (len - 130u);
        const uint32_t grain_len = 64u + rnd() % (window - 63u);
        const uint32_t valid = (rnd() & 1u) ? len : rnd() % len;
        const uint32_t wp = rnd() % len;
        const uint32_t grain = i & 1u;
        uint32_t lag;
        float ratio;

        rt.aux_raw = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_st2 = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_st3 = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_spread = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_amp = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_width = (uint8_t)(rnd() & 0x7Fu);
        rt.tone_raw = (uint8_t)(rnd() & 0x7Fu);
        rt.pan_raw = (uint8_t)(rnd() & 0x7Fu);
        if ((i % 7u) == 0u) {
            rt.grain_amp = 0u;
            rt.grain_width = 0u;
        }
        crumpBit_grainPick(&hash, grain, &rt, len, wp, window, grain_len,
                           valid, &c);
        lag = (wp >= c.start) ? wp - c.start : wp + len - c.start;
        ratio = (float)(c.inc < 0 ? -c.inc : c.inc) / 4096.0f;
        snprintf(msg, sizeof msg,
                 "CB14 pick %u: lag %u len %u G %u r %.3f inc %d", i, lag,
                 len, grain_len, (double)ratio, c.inc);
        expect(c.start < len, msg);
        expect(lag >= 2u, msg);
        /*
         * Headroom: a forward grain above unity starts at least
         * 2 + G (r - 1) behind the head (truncated to whole samples), with r
         * the played Q3.12 ratio, whenever that lag fits the line at all.
         */
        if (c.inc > 0 && ratio > 1.0f &&
            2.0f + (float)grain_len * (ratio - 1.0f) < (float)(len - 1u))
            expect((float)lag + 1.0f >
                   2.0f + (float)grain_len * (ratio - 1.0f), msg);
        expect((c.inc < 0) == (rt.tone_raw < CRUMPBIT_TONE_FLAT), msg);
        if (rt.grain_amp == 0u)
            expect(c.amp == 255u, msg);
        if (rt.grain_width == 0u)
            expect(c.pan == (grain ? 191u : 64u), msg);
    }

    /* (c) freeze keeps the record line bit-exact; the delay line runs. */
    {
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        const uint32_t half = CRUMPBIT_LOOP_MAX_BYTES / 2u;
        uint8_t *snap_rec = malloc(half);
        uint8_t *snap_dly = malloc(half);
        float l[32], r[32];
        uint32_t block;
        uint32_t n;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 100u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 64u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 40u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_SPREAD, 100u, &share);
        for (block = 0u; block < 4000u; block++) {
            if (block == 2000u) {
                p3_write(&rt, CRUMPBIT_PARAM_SYNC, 1u, &share);
                memcpy(snap_rec, arena_p3, half);
            }
            if (block == 2001u)
                memcpy(snap_dly, arena_p3 + half, half);
            for (n = 0u; n < 32u; n++) {
                l[n] = rndf(-0.8f, 0.8f);
                r[n] = rndf(-0.8f, 0.8f);
            }
            p3_block(&rt, &share, l, r, 32u);
        }
        expect(memcmp(snap_rec, arena_p3, half) == 0,
               "CB14 freeze: record line bit-exact");
        expect(memcmp(snap_dly, arena_p3 + half, half) != 0,
               "CB14 freeze: delay line still running");
        free(snap_rec);
        free(snap_dly);
    }

    /* (d) stability at fbk 127, every size/direction extreme. */
    for (s = 0u; s < 4u; s++) {
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        float l[32], r[32];
        float peak = 0.0f;
        int finite = 1;
        uint32_t block;
        uint32_t n;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, (uint8_t)(s * 40u), &share);
        p3_write(&rt, CRUMPBIT_PARAM_TONE, (uint8_t)((s & 1u) ? 0u : 127u),
                 &share);
        p3_write(&rt, CRUMPBIT_PARAM_AUX, (uint8_t)(s * 40u), &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_SPREAD, 127u, &share);
        for (block = 0u; block < 27600u; block++) {     /* 20 s */
            for (n = 0u; n < 32u; n++) {
                const float burst = (block < 138u) ? rndf(-1.0f, 1.0f)
                                                   : 0.0f;

                l[n] = burst;
                r[n] = -burst;
            }
            p3_block(&rt, &share, l, r, 32u);
            for (n = 0u; n < 32u; n++) {
                finite &= isfinite(l[n]) && isfinite(r[n]);
                peak = fmaxf(peak, fmaxf(fabsf(l[n]), fabsf(r[n])));
            }
        }
        snprintf(msg, sizeof msg, "CB14 stability pass %u peak %.3f", s,
                 (double)peak);
        expect(finite && peak < 8.0f, msg);
    }
}

```

##### X11.11 — CB15: `rvb` (new lines 1669–1789)

- **What:**
  - **(a)** All 32 combinations of `rte`, `fbk`, `snc`, `ton`, `dpn` at
    their extremes, 5 s with input: finite, < 8.
  - **(b)** `fbk` 44 (RT60 0.493 s), `rte` 64: after a 50 ms burst, the
    RMS at 175–225 ms is 9–21 dB below that at 50–100 ms (law 15.2).
  - **(c)** Freeze after the burst: within ±6 dB over 1 s.

```c
/* CB15: `rvb` --------------------------------------------------------------- */
static double cb15_rms(const float *x, uint32_t n)
{
    double e = 0.0;
    uint32_t i;

    for (i = 0u; i < n; i++)
        e += (double)x[i] * (double)x[i];
    return sqrt(e / (double)n);
}

/* Run `rvb`: 0.5 s settle, a 50 ms burst, then `tail` samples recorded. */
static void cb15_run(CrumpBitRuntime *rt, const fx_share_t *share,
                     uint8_t freeze_after, float *out, uint32_t tail)
{
    float l[32], r[32];
    uint32_t block;
    uint32_t got = 0u;
    uint32_t n;

    for (block = 0u; block < 690u; block++) {
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        p3_block(rt, share, l, r, 32u);
    }
    for (block = 0u; block < 69u; block++) {
        for (n = 0u; n < 32u; n++) {
            l[n] = rndf(-0.5f, 0.5f);
            r[n] = rndf(-0.5f, 0.5f);
        }
        p3_block(rt, share, l, r, 32u);
    }
    if (freeze_after)
        p3_write(rt, CRUMPBIT_PARAM_SYNC, 1u, share);
    while (got < tail) {
        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        p3_block(rt, share, l, r, 32u);
        memcpy(out + got, l, sizeof l);
        got += 32u;
    }
}

static void law_cb15(void)
{
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    const uint32_t tail = 48000u;
    float *out = calloc(tail + 32u, sizeof(float));
    CrumpBitRuntime rt;
    uint32_t pass;
    char msg[128];

    /* (a) every extreme: finite and bounded over 5 s with input. */
    for (pass = 0u; pass < 32u; pass++) {
        float l[32], r[32];
        float peak = 0.0f;
        int finite = 1;
        uint32_t block;
        uint32_t n;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_REVERB);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, (pass & 1u) ? 127u : 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, (pass & 2u) ? 127u : 0u,
                 &share);
        p3_write(&rt, CRUMPBIT_PARAM_SYNC, (uint8_t)((pass >> 2) & 1u),
                 &share);
        p3_write(&rt, CRUMPBIT_PARAM_TONE, (pass & 8u) ? 127u : 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_DLY_PAN, (pass & 16u) ? 127u : 0u,
                 &share);
        for (block = 0u; block < 6900u; block++) {
            for (n = 0u; n < 32u; n++) {
                l[n] = rndf(-1.0f, 1.0f);
                r[n] = rndf(-1.0f, 1.0f);
            }
            p3_block(&rt, &share, l, r, 32u);
            for (n = 0u; n < 32u; n++) {
                finite &= isfinite(l[n]) && isfinite(r[n]);
                peak = fmaxf(peak, fmaxf(fabsf(l[n]), fabsf(r[n])));
            }
        }
        snprintf(msg, sizeof msg, "CB15 extreme %u peak %.3f", pass,
                 (double)peak);
        expect(finite && peak < 8.0f, msg);
    }

    /* (b) decay: fbk 44 is RT60 0.493 s; 0.125 s later -15.2 dB. */
    memset(arena_p3, 0, sizeof arena_p3);
    p3_init(&rt, CRUMPBIT_SUBTYPE_REVERB);
    p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
    p3_write(&rt, CRUMPBIT_PARAM_RATE, 64u, &share);
    p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 44u, &share);
    cb15_run(&rt, &share, 0u, out, tail);
    {
        const double a = cb15_rms(out + 2205u, 2205u);    /* 50..100 ms */
        const double b = cb15_rms(out + 7718u, 2205u);    /* 175..225 ms */
        const double db = 20.0 * log10(a / (b > 1e-12 ? b : 1e-12));

        snprintf(msg, sizeof msg, "CB15 decay %.2f dB over 0.125 s", db);
        expect(db >= 9.0 && db <= 21.0, msg);
        printf("CB15 decay: %.2f dB over 0.125 s (law 15.2)\n", db);
    }

    /* (c) freeze holds the tail within +-6 dB over 1 s. */
    memset(arena_p3, 0, sizeof arena_p3);
    p3_init(&rt, CRUMPBIT_SUBTYPE_REVERB);
    p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
    p3_write(&rt, CRUMPBIT_PARAM_RATE, 64u, &share);
    p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 100u, &share);
    cb15_run(&rt, &share, 1u, out, tail);
    {
        const double a = cb15_rms(out + 2205u, 2205u);
        const double b = cb15_rms(out + 44108u, 2205u);
        const double db = 20.0 * log10(a / (b > 1e-12 ? b : 1e-12));

        snprintf(msg, sizeof msg, "CB15 freeze %.2f dB over 1 s", db);
        expect(fabs(db) <= 6.0, msg);
        printf("CB15 freeze: %.2f dB over 1 s\n", db);
    }
    free(out);
```

#### X12 — `main()`: the new laws

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 871–873: **replace** (3 lines → 10; new lines 1799–1808).

- **What:** after CB6, calls `law_cb6_subtypes()`; after CB9, CB10–CB15.

Remove (HEAD 871–873):

```c
    law_cb7();
    law_cb8();
    law_cb9();
```

Replace with:

```c
    law_cb6_subtypes();
    law_cb7();
    law_cb8();
    law_cb9();
    law_cb10();
    law_cb11();
    law_cb12();
    law_cb13();
    law_cb14();
    law_cb15();
```

---

## 5. Gates

Run in order on the implemented tree.

| # | Command | Expected |
|---|---|---|
| G1 | `make all` | exit 0; no warning from any changed file |
| G2 | `make img` | `Written:build/LXRV2_lxr02.img (707020b) OK` (± a few bytes if the tree differs from HEAD) |
| G3 | `make -C tools/dsp_test crumpbit` | `crumpbit failures: 0`; `crumpbit_break1`…`9: caught`; CB8 peaks 1.000; `CB15 decay: 15.60 dB`; `CB15 freeze: 1.98 dB` |
| G4 | `make -C tools/dsp_test crumpbit_tables` | `crumpbit tables OK` |
| G5 | `make -C tools/dsp_test crumpbit_elf_check` | longest paths `runDelay` 124, `runComb` 124, `runPitch` 135, `runGrain` 131 and 125, `runReverb` 145, `process` 31, all vdiv 0; `crumpBit_divisionFor` 113; `crumpBit_tableLimit` 63 |
| G6 | Mixer loops: copy the HEAD ELF to `build/s091p3_base.elf` before implementing; then `elf_loopdump.py --elf … --sym mixer_calcNextSampleBlock --out …` on both and `elf_hotloops.py --compare BASE NOW` | `39 hot loops equal, 0 shrank, 0 grew`; the dumps normalised (`sed -E 's/\+-?[0-9]+/OFF/g; s/@ ADDR//g; s/0x[0-9a-f]+/ADDR/g'`, sorted) identical, 86 loops |
| G7 | `tools/diagnostic_tools/link_budget.py` (run by `make all`) | ITCM ≈ 15,280 B; DTCM statics 4,384 B; **FXBUF ≥ 126,656 B** (126,688 B at `0x20001120` in the measured build; 126,656 B at `0x20001140` if LTO packs `.dtcmz` across the boundary; both are correct, choice 10) |
| G8 | `arm-none-eabi-size build/lxr02.elf` | text ≈ 706,600; data 420; bss 413,040 (+12) |
| G9 | `elf_looppath.py --sym calcNextOscSampleSyncBlock --top 4` | 11/9 and 9/9, as on HEAD |
| G10 | `make -C tools/dsp_test all` | as on HEAD: every module's failure count as HEAD's. The `voice_control` second build (6 failures) and `run_modes` (compile error) fail identically on clean HEAD and are not part of P3 |

**Hardware checks (user).** Keep the monitor level low for H4–H6.

| # | Check | Expected |
|---|---|---|
| H0 | Before flashing, if not done yet: the S091 P2 checks (`S091_P2_Implementation.md` §5 H1–H7) on HEAD | P2 accepted; this schedule builds on it |
| H1 | The worst-case Scene (`cbt`, every send open), `cpu` widget and the underrun count (DEV `UNR`), once per sub-type (`sub` on the Effect page) | each at or below the user's ≈ 195 budget, with `dly` ≈ HEAD; `grn` and `rvb` highest (≈ 20 instructions per frame above `dly`); no underruns |
| H2 | `sub` stepped through all five on the page, then a Pattern track automating `sub` every step | the switch glitches (buffer reinterpreted, accepted) but never sticks, never goes silent for longer than the new engine's gate (≤ one line), never runs away; no underrun on switch blocks |
| H3 | `dly` after the update | sounds exactly as P2 (host CB4 bit-exact) |
| H4 | `cmb`: `rte` sweep; `snc` (pol) on/off; `fbk` 90–127; `ton` low; an LFO on `rte` | pitched combs falling as `rte` rises (note label on the page), hollow with `pol`, plucks and drones bounded by the knee, flanging under the LFO |
| H5 | `pit`: `int` ±12 with `snp` on, ±24, small detunes; `win` 10–200 ms; `fbk` up (shimmer) | clean octave up/down at mid windows, warbly at short ones, rising/falling shimmer, no runaway at 127 |
| H6 | `grn`: `div`, `sze` ± (reverse), `pos` follow/spray/slice, `st1 st2 st3`, `spr` 0/40/80/127, `frz` on then `fbk` up, `den`, `amp`, `wid` | grains at the division; reverse left of centre; pitch set fanning out with `spr`, detune above 80; freeze holds the record while the delay line keeps echoing; a change of `div` jumps the echoes (no glide, user) |
| H7 | `rvb`: `siz` and `dcy` extremes, `frz`, `dmp`, `wid` | small rooms to long 8-bit tails; freeze sustains; damping darkens/brightens; width mono → wide |
| H8 | Save and reload a `.fx` with `grn` lane steps; AutoSave reboot | `grn` lanes (`st2 st3 … spr`) reload on the same lanes; a P2 `.fx` loads as `dly` with its lanes unchanged |
| H9 | STEP automation page / Pattern value entry for `sub` | shows `dly cmb pit grn rvb` |

---

## 6. Documentation changes (same step)

### D1 — `tools/dsp_test/DSP_TEST.md`

- **Line 38:** `crumpbit.c` description →
  `CrumpBitEffect.c: S091 search/ADC/DAC laws, the S091 P2 reference and the S091 P3 sub-type laws (CB1–CB15)`.
- **Lines 91–93 (targets table):** replace the three rows:

  | Target | What it checks |
  |---|---|
  | `crumpbit` | CB1 Sync search; CB2 ADC; CB3 code DAC; CB4 `dly` whole blocks bit-exact against the S091 P2 reference (with every P3 row written); CB5 init; CB6 tables and every sub-type's Rate limit; CB7 Q ≤ 8; CB8 stability at feedback 1.2; CB9 row ownership and the per-sub-type lane maps; CB10 the mix stage bit-exact in every sub-type; CB11 sub-type switch storm; CB12 `cmb` tuning and polarity; CB13 `pit` ratio; CB14 `grn` distributions, headroom, freeze, stability; CB15 `rvb` stability, decay, freeze. Nine break builds |
  | `crumpbit_tables` | `gen_crumpbit_tables.py --check`: the committed nine tables equal the generator |
  | `crumpbit_elf_check` | linked image: each sub-engine's frame loop at its measured length (`dly`/`cmb` 124, `pit` 135, `grn` 131 with `--cond-heads`, `rvb` 145), the output filter 31, no VDIV; `crumpBit_divisionFor` ≤ 117; `crumpBit_tableLimit` 63 (needs `make all` first; not in `all`) |

- **The `elf_looppath.py` paragraph:** add `--cond-heads` (only
  conditional backward branches start loops; for functions whose cold
  blocks jump back unconditionally).

### D2 — `EFFECTS_MIXER_DSP_REFERENCE.md`

- **Line 312 (§4.2):** "76 B (StereoFilter is the largest; CrumpBit is
  76 B since S091 P2 and fills the union)" → "88 B since S091 P3 (CrumpBit:
  56 B shared + a 32 B sub-engine union; StereoFilter 76 B)".
- **§4.4 (line 343):**
  - Title: add "; sub-types S091 P3".
  - After the stage description, add §4.4a with:
    - the four stages (§2 of this schedule's E1 header);
    - the switch rule (reinterpret, no clear; values carry over);
    - per sub-engine, the per-frame equations of E10's `cmb`, `pit`,
      `grn`, `rvb` sub-blocks, each with its loop count (124 / 135 / 131 /
      145) and whole-Effect worst case (168 / 181 / 192 / 192);
    - the mix stage law `out = h + mix·(wet − h)` (shared; lane 3);
    - the sub-type-aware `rte` clamp (`crumpBit_subtypeRateLimit()`).
- **Lines 480–487 (cost):** replace with the §2 table's `dly` row:
  - loop 124, output filter 31;
  - per block `divisionFor` 113 + `tableLimit` 63 + 166 + 194;
  - ≈ 172 worst case;
  - sizes `process` 1,128 B, engines 1,192 / 1,138 / 1,408 / 3,196 /
    1,560 B, `tableLimit` 192 B, the nine tables 4,040 B.
- **§5.1 table (line 514):** the CrumpBit row: "the share up to 126,656 B
  (N1); `grn` two half lines (≤ 1.436 s each); `rvb` words (31,664 frames
  of four 8-bit lines)".
- **§6.2a (line 766):** the CrumpBit cost cell → "per sub-type, worst case
  per frame: `dly` 172, `cmb` 168, `pit` 181, `grn` 192, `rvb` 192
  (measured S091 P3; constant within a sub-type, which is chosen per
  block)".

### D3 — `EFFECTS_BUS_REFERENCE.md`

- **Line 215 (§4.2a row 2):**
  - rows 22;
  - lanes "per `sub` (`lane_map`): shared `mrp bof biv mix vol pan typ frq
    res`; pool `fbk rte snc dpn ton` (`dly`/`cmb`/`rvb`), + `aux` (`pit`),
    `st2 st3 snc dpn ton aux spr` (`grn`)";
  - hooks add `name3`, `layout`, `format_target3`;
  - runtime 88 B.
- **Lines 219–220:** "pin CrumpBit at 76 B: it fills the union" → "pin
  CrumpBit at 88 B (S091 P3, +12 B approved): the union is CrumpBit's".
- **Line 232 (ops table):**
  - `effective_max(rt, index, share)`: "rt = the live runtime or NULL; a
    type with sub-engines clamps by the running one (S091 P3)".
  - Add rows `lane_map(variant)`, `lane_variant_row`, `lane_variant_count`
    ("per-variant lane tables; every reader goes through
    `effects_laneTable(record)`; variant 0 must equal `lanes[]`,
    self-check 11").
- **Line 528 (hooks table):** add `name3`, `layout`, `format_target3`, with
  MH1's text.
- **§8.6 (lines 610–660):**
  - add a per-sub-type table: names (`cmb` `not pol`; `pit` `win snp int`;
    `grn` `div frz pos sze st1`; `rvb` `dcy siz frz wid dmp`) and values
    (`rte`: note, ms, division; semitones; `---`);
  - the `pit` cell s3/3 = `int`;
  - the `grn` SELECT 3 screens `st1 st2 st3 spr` / `den amp wid`;
  - `sub` now `dly cmb pit grn rvb`, Pattern-automatable.
- **Line 643:** "`sub` shows `dly`" → "`sub` shows its sub-engine token".
- **Line 1032:** "(76 B; CrumpBit 76 B since S091 P2 fills it)" → "(88 B
  since S091 P3; CrumpBit fills it)".
- **New §12 note:** BUFFER_DEPENDENT rows are re-applied every block (M16).

### D4 — `STORAGE_SRAM_MANIFEST.md`

- **Line 697:** "76 … CrumpBit 76 B since S091 P2 … No spare" → "88 B
  since S091 P3 (CrumpBit: 56 B shared + 32 B sub-engine union; +12 B,
  user-approved up to +32 B). No spare."
- **Lines 717–718:** "takes the whole share up to 126,688 B since S091 P2"
  → "takes the whole share up to `CRUMPBIT_LOOP_MAX_BYTES` = 126,656 B
  (S091 P3 N1; the arena is 126,688 or 126,656 B depending on LTO's
  `.dtcmz` packing)".

### D5 — `CPU_USE_DSP_AUDIT.md`

After item 62 (before the `---` at line 1021), add:

```markdown
63. **CHANGED (S091 P3): CrumpBit sub-types.** Four new sub-engines
    (`cmb` comb, `pit` two-head pitch shifter, `grn` pitched granular
    plus delay line, `rvb` 8-bit 4-line FDN), one per block by `sub`,
    each its own frame loop; shared dry path, mix stage and output filter.
    - Linked worst case per frame (loop + output 31 + per block):
      `dly` 124 -> 172 (HEAD 173), `cmb` 124 -> 168, `pit` 135 -> 181,
      `grn` 131 -> 192, `rvb` 145 -> 192. No per-sample VDIV.
    - Reductions inside the engines: integer 0.32 head phase and Q14
      head reads (`pit`), VCVT #7 lanes and packed XOR (`rvb`), two
      spans around the block's one grain restart and no delay glide
      (`grn`), block fields as locals (all).
    - RAM +12 B (runtime 88 B); text +12,864 B.
    - Host CB1-CB15, nine breaks; gate crumpbit_elf_check per engine.
```

### D6 — `MEMORY.md` (after the hardware result)

Add a status line, and under **Binding Rules → Effects**:

- **CrumpBit sub-types (S091 P3):**
  - Four stages: the shared dry path, one sub-engine per block, the
    shared MIX stage (`mix`, lane 3 everywhere, never freeable), the
    shared output filter.
  - `sub` switches reinterpret the buffer and keep every value.
  - Lane maps are per stored `sub` (`effects_laneTable()`).
  - Row owners: `CRUMPBIT_ROWS_SHARED` / `_SUBTYPE` / `_GRN`.

---

## 7. Optional reductions (not in this schedule)

| # | Sub-type | Change | Saving (per frame) | Cost |
|---|---|---|---|---|
| O1 | `rvb` | 3 lines instead of 4 (one lane spare) | ≈ −15 (estimate) | thinner tail |
| O2 | `rvb` | maintain the four read indices incrementally instead of `wp − d` + compare | ≈ −2 | 16 B state; the `rvb` member has 4 B spare, so +12 B RAM |
| O3 | `pit` | one head reading at the window's centre while the crossfade weight < ⅛ | ≈ −8 | a data-dependent branch (breaks constant CPU); not recommended |
| O4 | `grn` | mono grains (no per-grain pan) | ≈ −4 | `wid` loses its meaning |
| O5 | all | `crumpBit_divisionFor()` only when `rte`, BPM or the share changed | ≈ −3.5 average, 0 worst case | 8 B cache; the worst case (an LFO on `rte`) is unchanged |

---

## 8. Risks and notes

- **Arena granule (choice 10).** The measured build kept FXBUF at
  126,688 B. If an unrelated DTCM change later crosses the boundary, the
  arena becomes 126,656 B and nothing in CrumpBit changes, because its
  constants already assume that. Any further growth of the union is a RAM
  approval.
- **`grn` restart bound.** The two-span loop relies on at most one restart
  per block: a grain ≥ 64 frames, a block ≤ 32 (`OUTPUT_DMA_SIZE`). A
  future larger render block needs `CRUMPBIT_GRAIN_MIN ≥ 2 × block`. The
  comment at `CRUMPBIT_GRAIN_MIN` (E2) says so.
- **Hardware RNG.** One `GetRngValue()` per block (G9). It does not wait
  for `RNG_DR`, so back-to-back reads repeat, and the hash supplies the
  candidate's five uniforms.
- **`pit` window clamp.** W (≤ 8,822 samples) is clamped to the loop
  only below a ≈ 8.8 KB share, which the arena never reaches (the smallest
  share, all twelve voice units claimed, is 73,664 B). The page clamps
  `rte` by the same law.
- **Switch hand-offs** play the previous engine's bytes in the new format
  (accepted glitches). `rvb` reading `dly` bytes as words is the loudest
  case: the per-block gate keeps the feedback off until `valid` covers the
  longest line.
- **`.fx` files:** P2 files load as `dly` with unchanged lanes (the `dly`
  map is the P2 table; self-check 11). A `grn` file saved here and loaded
  by P2 firmware would not be supported (no backward compatibility).

---

## 9. Implementation record (2026-11-04)

Applied on HEAD `11397377` (S091 P3 pre-implementation check commit). The
schedule's code blocks were applied by a parser that matched every explicit
`Remove` block against the HEAD file before writing; all 75 replacements and
14 insertions matched, with zero mismatches. The generated targets were then
reviewed as diffs. This section is the running work log; the gates in §5 are
the evidence.

| Step | Done | Result |
|---|---|---|
| Firmware ports | H1–H2, E1–E10, P1–P3, C1–C10, B1–B2, MH1–MH5, M1–M16, S1–S3, N1–N4, U1–U9 | Applied; `old` blocks byte-matched HEAD |
| Tables | G1–G4, R1 | Generator updated; `gen_crumpbit_tables.py --check` prints `crumpbit tables OK` |
| Tools / host tests | L1–L3, T1–T6, X1–X12 | Applied; host laws and gates below |
| Docs | D1–D5, D6 status + binding rule | Applied in the same step |

**Small out-of-schedule fix.** `StereoFilterEffect.c` gained explicit
`NULL, 0u, 0u` for the new `lane_map` / `lane_variant_row` /
`lane_variant_count` ops fields. Without them `-Wmissing-field-initializers`
reported the struct change in an otherwise unchanged file. No behaviour
change (the fields are zero either way).

**Build and static gates.**

| Gate | Result |
|---|---|
| `make all` | exit 0; no warnings from any changed file |
| `make img` | `Written:build/LXRV2_lxr02.img (707020b) OK` |
| `crumpbit` | `crumpbit failures: 0`; CB8 peaks 1.000; CB15 decay 15.60 dB, freeze 1.98 dB; break builds 1–9 all caught |
| `crumpbit_tables` | `crumpbit tables OK` |
| `crumpbit_elf_check` | `runDelay` 124, `runComb` 124, `runPitch` 135, `runGrain` 131/125, `runReverb` 145, `process` 31, all vdiv 0; `divisionFor` 113; `tableLimit` 63 |
| Link map | text 706,600; data 420; bss 413,040; flash 707,020 (headroom 46,644); ITCM 15,280; DTCM statics 4,384; FXBUF 126,688 B at `0x20001120` |
| Mixer loops | 39 hot loops equal, 0 shrank, 0 grew; 86/86 normalised fragments identical |
| Oscillator loop | `calcNextOscSampleSyncBlock` 11/9 and 9/9, as HEAD |
| `make -C tools/dsp_test all` | checked after the change; the pre-existing `voice_control` second build (6 failures) and `run_modes` compile error remain identical to clean HEAD, so they are outside P3 |

**Hardware.** H1–H9 of §5 remain to be run by the user. Nothing in this
session was flashed.

---

## 10. Assessment (2026-11-04)

**Verdict: the implementation is the schedule, byte for byte, and every
static gate reproduces the schedule's numbers.** One deviation is
documented and correct. It exposed a miss in the schedule. A few
reference-doc lines are stale. Hardware H1–H9 is still open.

### 10.1 Code against the schedule

- **All 15 scheduled files are byte-identical** to the verified scratch
  build the schedule was generated from:
  - `CrumpBitEffect.c/.h`, `CrumpBitParameters.c/.h`,
    `CrumpBitTables.c/.h`;
  - `EffectsManager.c/.h`, `storageTypes.c`, `menu.c`, `menuEffects.c`;
  - `gen_crumpbit_tables.py`, `elf_looppath.py`, `tools/dsp_test/Makefile`
    and `modules/crumpbit.c`.

  The comparison was `cmp` of the working tree against the scratch tree.
  This agrees with §9's 75 replacements and 14 insertions (all 89 units).
- **The only firmware change outside the schedule** is
  `StereoFilterEffect.c`: an explicit `NULL, 0u, 0u` for the three new ops
  fields (§9).
  - It changes no code (the fields were zero before), and the image is
    identical with and without it.
  - It is needed: rebuilding the schedule's own scratch tree with
    `StereoFilterEffect.c` touched reproduces
    `warning: missing initializer for field 'lane_map' of 'effect_type_ops_t' [-Wmissing-field-initializers]`.
- **Schedule miss.** MH3 said StereoFilter "compiles unchanged", and G1
  promised no warnings from changed files.
  - Both are true only in the narrow sense that `StereoFilterEffect.c` was
    not a changed file.
  - The pre-verification filtered warnings by CrumpBit file names, so it
    missed a warning that the `EffectsManager.h` change caused in another
    file.
  - The implementer's fix is the right one. A future schedule's G1 should
    grep every warning of a clean build against the baseline's list.

### 10.2 Independent rebuild and gates

The working tree was copied (without `.git` and `build/`) into the
scratchpad, rebuilt clean, and every gate re-run there:

| Gate | Result |
|---|---|
| G1 `make all` | exit 0; **0 warnings** from any CrumpBit, EffectsManager, StereoFilter, storage or menu file (the remaining warnings are the baseline's: asyncfatfs, usb_core, SplashAnimation, PatternData, EuklidGenerator, filesystem) |
| G2 `make img` | `Written:build/LXRV2_lxr02.img (707020b) OK`; **byte-identical** to the image in the repository and to the schedule's verified image |
| G3 `crumpbit` | 0 failures; CB8 peaks 1.000 (mean 0.061 / −0.002 / −0.001 LSB); CB15 decay 15.60 dB, freeze 1.98 dB; breaks 1–9 caught |
| G4 `crumpbit_tables` | OK |
| G5 `crumpbit_elf_check` | 124 / 124 / 135 / 131 + 125 / 145 / 31, vdiv 0; `divisionFor` 113; `tableLimit` 63 |
| G6 mixer loops | 39 hot loops equal, 0 shrank, 0 grew; 86/86 normalised identical |
| G7/G8 link | text 706,600, data 420, bss 413,040; flash 707,020 (headroom 46,644); ITCM 15,280; DTCM statics 4,384; FXBUF 126,688 B at `0x20001120` (≥ 126,656 holds) |
| G9 oscillator | `calcNextOscSampleSyncBlock` 11/9, 9/9 |

The §2 CPU table therefore stands for the implemented image. Worst case
per frame for the whole Effect: `dly` 172 (HEAD 173), `cmb` 168, `pit` 181,
`grn` 192, `rvb` 192, all within the accepted ≈ 195.

### 10.3 Documentation (D1–D6)

D1, D4, D5 and D6 match the schedule. D6's status line was added before the
hardware result and says "hardware pending", which is accurate. D2 and D3
are applied, but these lines are stale or wrong. Most were not named by the
schedule's D-units:

| File | Line | Now | Should be |
|---|---|---|---|
| `EFFECTS_BUS_REFERENCE.md` | 621 | screen 1 cell 4 "the sub-type (`dly`)" | "the sub-type (`dly cmb pit grn rvb`)" |
| `EFFECTS_BUS_REFERENCE.md` | 626 | the layout bullet describes only the base layout | add: `pit` uses `crumpBit_layoutPitch` (`int` at s3/3); `grn` uses `crumpBit_layoutGrain`, `screen_count = {0, 4, 2, …}` |
| `EFFECTS_BUS_REFERENCE.md` | 669 | "clamped to `crumpBit_rateLimit()`" | "clamped to `crumpBit_subtypeRateLimit()` for the shown sub-type"; and add the per-sub-type `rte` text (note, ms, division) |
| `EFFECTS_BUS_REFERENCE.md` | hooks table, `format_target3` row | "…and its per-sub-type `rte` text" | `sub` names only (the hook returns 0 for every other row) |
| `EFFECTS_BUS_REFERENCE.md` | 680 (defaults) | P2 defaults only | add "S091 P3: `sub` `dly`, `aux`/`st2`/`st3` 63 (0 st), `spr` 0, `den` 127, `amp` 0, `wid` 0" |
| `EFFECTS_MIXER_DSP_REFERENCE.md` | 398 | comb "8 samples .. about 2,048" | "8.4 .. 2,141 samples" |
| `EFFECTS_MIXER_DSP_REFERENCE.md` | 453 | Rate clamp through `crumpBit_rateLimit()` | through `crumpBit_subtypeRateLimit()` (`crumpBit_rateLimit()` is the `dly` case) |
| `EFFECTS_MIXER_DSP_REFERENCE.md` | 499 | "`min(share bytes, 126,688)`" | "126,656" (N1) |
| `EFFECTS_MIXER_DSP_REFERENCE.md` | §4.4a `grn` | "triangular windows half a grain apart that sum to 1" | they sum to 1 at `den` 127; below it each window covers the active part only (gaps between grains) |

None of these affects code or gates.

### 10.4 Open

- **Hardware H1–H9 (§5).**
  - H1 decides the CPU question for `grn` and `rvb` (≈ 192, about 20 above
    `dly`).
  - H2 covers switch blocks: `crumpBit_enter()` adds up to 194
    instructions to the switching block only.
- S091 P2 is still untested on hardware (H0). P3's `dly` is bit-exact to P2
  on the host (CB4), so a P2 problem would show in both.
- The doc lines in §10.3.
