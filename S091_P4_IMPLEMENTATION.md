# S091 P4 — `grn` Rework and Page Organisation: Implementation Schedule

- **Plan:** `S091_P4_GRN_TWEAKS.md` Part A: the target model (§A1), the
  physics decisions (§A3), decisions Q1–Q12 (§A5, all taken by the user
  on 2026-11-04), the page organisation and `bal` (§A6), and the
  deep-dive decisions this schedule made (§A7).
- **Baseline:** HEAD `b0ed5701` on `dev-ph7-voice_expansion` (S091 P3
  implemented and accepted on hardware). Every **HEAD line** below refers
  to that tree.
- **Scope:**
  - the granular sub-type rebuilt on the target model:
    - Position, Length and Density as bipolar controls;
    - triggers independent of length;
    - flat-top windows with exact crossfades (the copy identity);
    - steal-oldest with a deferred trigger, and the overwrite clamp;
    - the anchored freeze;
    - absolute P1–P3 with ±1 st spread detune, and the Width law;
    - its own delay-time curve to one bar, divisions marked;
    - level jitter removed;
  - the shared output-filter balance `bal`;
  - the page organisation for every CrumpBit sub-type and the manager's
    second page (`mrp run len scl`);
  - the `grn` lane pool (Q11).
  - The presentation work (value-text formats, final names) stays with the
    later task; only the labels the plan requires are built: the delay
    time's marked divisions, Position's and Density's stops, `off`/`buf`.
- **Pre-verified (2026-11-05):** every code block below was applied to a
  copy of HEAD (`git archive HEAD`) in the session scratchpad. That copy
  was:
  - built clean with the firmware flags (`make all`, `make img`);
  - measured on the linked ELF (§2);
  - tested on the host (CB1–CB16, 0 failures, twelve break builds caught).

  The blocks were copied from the built files by script: a `difflib` map of
  each unit onto the built file, and a check that applying every unit to
  HEAD, bottom-up, rebuilds each built file byte for byte. Nothing was
  retyped.
- **Status:** schedule. No firmware file in the repository is changed.
- **Constraints:**
  - RAM: **+12 B** (runtime 88 → 100 B; +24 B of the user's +32 B over
    76 B).
  - CPU: `grn` measures ≈ 201 per frame worst case (P3 192, user ceiling
    ≈ 195; §2); the other sub-types +0.7 (`bal`).
  - No backward compatibility: the `crump_grain_amp` key is gone (an old
    file's line is ignored); `crump_grain_length` and `crump_filter_bal`
    load their defaults when absent.

---

## 0. Decisions applied, and implementation choices

| # | Decision | Applied as |
|---|---|---|
| A1.1 | Position: 0 = just behind the input; positive = rhythmic lag up to N; negative = random lag up to anywhere | `crumpBit_grainPick()` (E7.1); stops via `crumpBit_grainPosStop()` (H6); the clamp `crumpBit_grainPosLimit()` (E3) through `effective_max` (E15, E16) with `dpn` BUFFER_DEPENDENT (C4) |
| A1.2, Q1 | Length: bipolar, magnitude = length up to N, sign = direction, shortest ≈ 5 ms, exponential | row 20 `crump_grain_length` (P2, C5); `crumpBit_grainFraction` 1/256..1 (G5, R1); pick (E7.1) |
| A1.3, Q6, Q7 | Density: 0 off, +1 once per N, positive rhythmic, negative random (mean N → 20 ms) | the scheduler in `crumpBit_runGrain()` (E7.5); `crumpBit_grainDenStop()` (H6); `crumpBit_grainDensity` (G5, R1) |
| A1.4 | Width: 0 mono, 64 uniform, 127 50–100 % sides | pick (E7.1); balance-law gains (E7.2) |
| A1.5, Q4 | P1–P3 absolute, spread to thirds at 80, ±1 st detune at 127 | pick (E7.1) |
| A1.6, Q8, Q10 | Delay line: own time, continuous, divisions marked, one bar | `crumpBit_grainTime` + `crumpBit_grainStopCc` (G5, R1, B1–B4); delay tap (E7.4, E7.5); the clamp (E3); the label (C6, C8) |
| A1.7, A3.3, Q5 | Freeze kept and anchored; `amp` removed | anchor in `crumpBit_runGrain()` (E7.5), state (H1); row 20 reused (P2, C5) |
| A1.9 | The copy identity | 128-frame complementary fades (E1, E7.4); law CB14(e) |
| A3.1, Q2 | Steal the oldest, fade 128 frames | E7.5; state `pending` (H1) |
| A3.2, Q3 | Clamp reads to recorded, not overwritten audio | pick (E7.1); law CB14(d), break 12 |
| A6 | Pages: `mrp run len scl`; `mrp frq res bal`; `typ frq res bal`; `mix sub …` | `menuEffects_cellAt()` (U1, U2); layouts (C10); names (C6) |
| A6.2 | `bal`: right cutoff + b, left − b | row 22 (P2, P3, C5); `crumpBit_filterCoeffs()` (E11) and stage 4 (E12–E14); law CB16 |
| Q11 | `grn` lanes: 4 den, 5 tim, 6 frz, 9 pos, 10 lng, 14 p1, 15 spr | `crumpBit_laneMaps` (E17, E18); CB9 |
| Q12 | Other sub-types' order after `sub` | layouts (C10) |

**Implementation choices made here** (recorded in the plan, §A7):

1. **Length on its own row 20,** not `ton`. A row has one default for every
   sub-type, and `ton` must stay flat (63) for `dly`; row 20 is `amp`'s old
   index, so Width stays 21 and `bal` is 22. Count 23.
2. **A fixed 128-frame fade** (`CRUMPBIT_GRAIN_FADE`, a power of two).
   - The window ramps step by exactly 2⁻⁷, so two back-to-back grains sum
     to exactly 1.
   - A voice stores only its end (L + 128) and its age.
3. **The balance pan law** (unity at the centre, the S075 delay-pan law),
   so Width 0 and the copy setting play at unity. Constant power would
   play at −3 dB.
4. **Triggers that cannot sound yet are retried on the next block,** and a
   rhythmic Position waits until its lag is recorded. Entering `grn` plays
   at once, with no silent Density period.
5. **Density's rhythmic periods are capped at N.**
6. **The grain state is 44 B:** a 32-bit countdown (random intervals reach
   2N) and a deferred-trigger byte (steals).
7. **Position's clamp** runs through the manager's BUFFER_DEPENDENT
   re-apply (`dpn` flagged), the same mechanism as `rte`. Its search is a
   fixed 5-probe binary search, not a 16-step loop.
8. **Page names** (working, for the presentation task):
   - `tim frz pos p1` through `name3`;
   - `p2 p3 lng` as descriptor short names;
   - `lng`, because SELECT 1's FX-sequence length is already `len`.
9. **Tools:**
   - `elf_looppath.py --in-range` (the new `runGrain`'s cold blocks close
     backward branches that are not loops);
   - break 4 (tape-seat clamp) now has a deterministic law (CB11 seat).

---

## 1. Change list

| Units | File | What |
|---|---|---|
| H1–H6 | `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | 44 B grain state; runtime 100 B (`grain_length`, `filter_bal`); helper declarations; Position/Density stop helpers |
| E1–E18 | `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | `grn` constants and size guards; half-line helpers; delay-time limit and Position limit; init and `write_param`; the grain engine (E7: candidate, gains, loop state, frame loop, block); seat and enter; `bal` in stage 4; `effective_max`; lane maps |
| P1–P6 | `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` | rows 20 and 22, count 23; masks; layout comment |
| C1–C11 | `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | include; descriptor comments and rows; `grn` page names and stop labels; value text; the layouts (A6) |
| B1–B4, R1 | `CrumpBitTables.h`, generated `CrumpBitTables.c` | `grainTime`, `grainStopCc`, `grainDensity`; the Length fraction law |
| M1–M2 | `Core/DSP/Effects/EffectsManager.c` | union comment; RAM guard 100 B |
| U1–U2 | `Core/Menu/menuEffects.c` | SELECT 1 screen 1: `mrp run len scl` (every type) |
| G1–G8 | `tools/plots/gen_crumpbit_tables.py` | the three new tables, the Length law, checks C6 |
| L1–L4 | `tools/dsp_test/elf_looppath.py` | `--in-range` |
| T1–T8, X1–X16 | `tools/dsp_test/Makefile`, `tools/dsp_test/modules/crumpbit.c` | laws CB4–CB16 updated or new; breaks 4, 7 retargeted; breaks 10–12; the `runGrain` gate |
| D1–D6 | documentation | §6 |

**Checked and unchanged:**
- `EffectsManager.h`.
- `storageTypes.c` and `menu.c`: lanes and keys go through the registry.
- `StereoFilter*` (its ops already list the P3 fields).
- `mixer.c` (loops identical, G6).
- `Makefile`: `CrumpBitTables.c` is already a source.

---

## 2. Measured results (scratch build of HEAD + this schedule)

Counts are instructions on the linked `-Ofast` ELF, measured as in S091
P3. The frame loop is its longest path; "per block" is the longest path
through the function with the frame loops weighted 0, every branch taken
the expensive way. Whole-Effect worst case per frame = frame loop + 31
(stage 4) + (engine per block + `process` per block + the manager's clamps
per block) / 32.

| Item | HEAD (S091 P3) | After | Note |
|---|---:|---:|---|
| `crumpBit_runGrain` frame loop | 131 / 125 | **136 / 133** | flat-top windows (+5); `--cond-heads --in-range` |
| `crumpBit_runGrain` per block | 575 + 113 (`divisionFor`) | **736** | scheduler, candidate, clamp, retry; no division search |
| `crumpBit_process` per block | 194 | **216** | `bal`: two coefficient sets, one more divide |
| Manager clamps per block in `grn` | `tableLimit` 63 | `tableLimit` 63 + **`grainPosLimit` 76** | Position is BUFFER_DEPENDENT |
| **`grn` whole Effect, worst** | 191.5 | **≈ 201** | user ceiling ≈ 195 (see below) |
| `dly` / `cmb` / `pit` / `rvb` whole Effect | 171.8 / 167.8 / 181.2 / 192.4 | **≈ 172.5 / 168.5 / 181.9 / 193.1** | loops unchanged (124 / 124 / 135 / 145); `bal` +0.7 |
| `crumpBit_process` stage-4 loop | 31 | **31** | per-channel coefficients cost no instruction in the loop |
| `crumpBit_divisionFor` / `crumpBit_tableLimit` | 113 / 63 | 113 / 63 | |
| Sizes: `runGrain` / `process` / `uiFormatValue3` / `effectiveMax` | 3,196 / 1,128 / 660 / 264 B | **4,336 / 1,264 / 880 / 352 B** | new: `grainPosLimit` 244 B, `grainTime` 512 B, `grainDensity` 256 B, `grainStopCc` 16 B |
| `.text` / `.data` / `.bss` | 706,600 / 420 / 413,040 | **709,440** / 420 / **413,016** | +2,840 B text |
| Flash used | 707,020 | **709,860** (headroom 43,804) | |
| ITCM | 15,280 | **15,328** | LTO setup code; the oscillator loops are unchanged (G9) |
| DTCM statics / FXBUF | 4,384 / 126,688 B at `0x20001120` | **4,392 / 126,656 B at `0x20001140`** | the +12 B crossed the 32 B boundary: exactly the N1 arena (§A7.2) |
| `CrumpBitRuntime` / `effects_runtime_t` | 88 / 88 B | **100 / 100 B** | grain state 44 B |
| `mixer_calcNextSampleBlock` | — | **39 hot loops equal; 86/86 normalised identical** | G6 |
| `calcNextOscSampleSyncBlock` | 11/9, 9/9 | **11/9, 9/9** | G9 |
| Warnings (clean build) | 20 (none in changed files) | **20, the same** | G1 |
| Image (`make img`) | 707,020 B | **709,860 B** | `Written … OK` |
| Host `crumpbit` | CB1–CB15, 9 breaks | **CB1–CB16: 0 failures; 12 breaks caught** | see below |

**The `grn` cost.**
- The frame loop is +5: the flat-top window takes two ramps and three
  clamps per voice, where the triangle took one ramp and two.
- The per-block work is about +4 per frame: the scheduler, the per-grain
  overwrite clamp, the retry, and the Position limit (76 per block in the
  manager).
- At ≈ 201 per frame, `grn` is ≈ 6 above the ≈ 195 the user accepted for
  P3, about 0.25 % of the CPU over P3. Hardware check H1 decides; §7 lists
  the reductions.

**Host evidence for the identity laws:**
- The copy identity (CB14(e)) held over 192,478 frames across two
  retriggers, worst error 1.2·10⁻⁷ (the mix stage's rounding).
- Rhythmic Density (`den` 100 = `d16` at 120 BPM): every one of 233
  starts exactly 8,270 frames apart.
- Random Density (`den` 20): a mean of 3,508 against 3,501.
- Density off: no starts.
- CB8 peaks 1.000.
- CB15: decay 15.35 dB (law 15.2), freeze 2.04 dB.

**Not P4:** `make -C tools/dsp_test all` stops at the `voice_control`
second build (6 failures) and `run_modes` (compile error), identically on
clean HEAD.

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

#### H1 — The reworked `grn` state (44 B) and the no-anchor marker

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, HEAD lines 95–129: **replace** (35 lines → 59; new lines 95–153).

- **What:** replaces the S091 P3 `CrumpBitGrainState` (one shared age, two
  restart-driven grains, level bytes) with the target model's state:
  - **two independent voices**, each with:
    - its 16.16 position `pos_*`;
    - its Q3.12 step `inc_*`;
    - its age `age_*` (frames since its start, saturating at its end);
    - its end `end_*` (= L + 128, 0 = idle);
    - its pan byte `pan_*` (0 left .. 127 centre .. 254 right);
  - **`countdown`** (uint32): frames from the next block start to the next
    Density trigger. A random interval can reach 2N = 126,656, too long
    for 16 bits;
  - **`anchor`:** the record head when freeze began, or
    `CRUMPBIT_GRAIN_NO_ANCHOR` (0xFFFF, above any line) while not frozen;
  - **`pending`:** a stolen voice's deferred trigger (0 = none, else 1 + the
    frames from the next block start);
  - **unchanged:** `write_pos`, `length`, `valid` (16-bit) and the delay
    line's DC memories;
  - **`spare`:** one byte of alignment.
- **Why:** plan §A2 and §A3.
  - Triggers are decoupled from length, so each voice needs its own age
    and end.
  - Steal-oldest needs the deferred trigger.
  - Freeze needs the anchor.
  - Fields are ordered 32-bit first, so the struct is exactly 44 B with no
    hidden padding (asserted in E1).
- **Inputs/outputs:** none (type).
- **Accessors:** `CrumpBitEffect.c` only:
  - `crumpBit_runGrain()`, `crumpBit_seatActive()`, `crumpBit_enter()`,
    `crumpBit_bytePosition()`;
  - host laws CB11, CB14.

Remove (HEAD 95–129):

```c
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
```

Replace with:

```c
 * Granular state: `grn` (engine B'; S091 P3, reworked in S091 P4,
 * S091_P4_GRN_TWEAKS.md Part A).
 *
 * What: the share is split into a record line (first half) and a delay line
 * (second half) of `length` samples each (N), written at the same
 * write_pos. Two grain voices, A and B, read the record line; each is
 * triggered by Density, independently of its Length.
 * - pos_a, pos_b: the voices' read positions in the record line, 16.16
 *   fixed point (integer part = sample index; drop-sample reads, user G6);
 * - countdown: frames from the next block start to the next Density
 *   trigger. 32-bit: a random interval reaches 2 N (126,656 frames);
 * - dc_x, dc_y: the delay line's feedback DC blocker;
 * - inc_a, inc_b: the voices' pitch steps, signed Q3.12 (ratio x 4096;
 *   negative = reversed); pos += inc x 16 per frame;
 * - age_a, age_b: frames since the voice started (saturating at its end);
 * - end_a, end_b: the frames the voice sounds, L + 128, latched at its
 *   start (0 = idle): a fade-in over the first 128 frames, flat, and a
 *   fade-out over the 128 frames after its Length L, so back-to-back
 *   grains crossfade with complementary fades (the copy setting is
 *   seamless, plan A1.9). A steal sets end = age + 128 (the fade-out starts
 *   at once). A voice is free once age >= end;
 * - write_pos, length (N), valid: 16-bit (a line is at most 63,328);
 * - anchor: the record head position when freeze began (positions are
 *   measured from it while frozen, plan A3.3); CRUMPBIT_GRAIN_NO_ANCHOR
 *   (0xFFFF, above any line) while not frozen;
 * - pan_a, pan_b: each voice's pan byte (0 left .. 127 centre .. 254
 *   right), drawn at its start from `wid` (plan A1.4);
 * - pending: a stolen voice's deferred trigger (plan A3.1): 0 = none, else
 *   1 + the frames from the next block start to the trigger;
 * - spare: one byte of alignment.
 * The delay-line tap has no state: it reads exactly D behind the write
 * head, D from the `rte` delay-time curve (no glide, user).
 * Size: 44 B (the union's largest member; S091 P4: +12 B over the S091 P3
 * 32 B, inside the user's +32 B approval). Accessors: CrumpBitEffect.c.
 */
typedef struct {
    uint32_t pos_a;
    uint32_t pos_b;
    uint32_t countdown;
    float dc_x;
    float dc_y;
    int16_t inc_a;
    int16_t inc_b;
    uint16_t age_a;
    uint16_t age_b;
    uint16_t end_a;
    uint16_t end_b;
    uint16_t write_pos;
    uint16_t length;
    uint16_t valid;
    uint16_t anchor;
    uint8_t pan_a;
    uint8_t pan_b;
    uint8_t pending;
    uint8_t spare;
} CrumpBitGrainState;

/* `anchor` while not frozen: above any line length (lines <= 63,328). */
#define CRUMPBIT_GRAIN_NO_ANCHOR   0xFFFFu
```

#### H2 — Runtime comment: 100 B

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, HEAD lines 150–151: **replace** (2 lines → 3; new lines 174–176).

- **What:** the runtime's header comment records 100 B (88 B in P3) and the
  +32 B approval over 76 B.

Remove (HEAD 150–151):

```c
 * CrumpBit runtime (88 B since S091 P3, a member of EffectsManager's DTCM
 * runtime union; user-approved growth up to +32 B, 2026-11-04).
```

Replace with:

```c
 * CrumpBit runtime (100 B since S091 P4; 88 B in S091 P3; a member of
 * EffectsManager's DTCM runtime union; user-approved growth up to +32 B over
 * 76 B, 2026-11-04).
```

#### H3 — Runtime comment: `filter_bal`, union 44 B

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, HEAD lines 166–167: **replace** (2 lines → 4; new lines 191–194).

- **What:** documents the new shared byte `filter_bal` (row 22), which took
  the alignment byte (the shared part stays 56 B), and the 44 B union.

Remove (HEAD 166–167):

```c
 * - drv_reserved: the deferred output-filter drive row's byte (plan OQ6).
 * Union (32 B): one of the five states above.
```

Replace with:

```c
 * - drv_reserved: the deferred output-filter drive row's byte (plan OQ6);
 * - filter_bal: row 22 (S091 P4), the output filter's stereo balance; it
 *   took the alignment byte, so the shared part stays 56 B.
 * Union (44 B, `grn` the largest): one of the five states above.
```

#### H4 — Row 20's byte: `grain_length`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, HEAD lines 199–199: **replace** (1 lines → 1; new lines 226–226).

- **What:** `grain_amp` → `grain_length`, the Length row's raw value.
- **Why:** plan §A7.1. Row 20 now holds the grain Length; the retired
  level jitter's byte is reused, so no other field moves.
- **Accessors:** `crumpBit_writeParam()` (E6), `crumpBit_grainPick()`
  (E7.1), `crumpBit_init()` (E4).

Remove (HEAD 199–199):

```c
    uint8_t grain_amp;         /* row 20, grn */
```

Replace with:

```c
    uint8_t grain_length;      /* row 20, grn: Length (S091 P4) */
```

#### H5 — Row 22's byte: `filter_bal` in the alignment slot

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, HEAD lines 209–209: **replace** (1 lines → 1; new lines 236–236).

- **What:** `pad` → `filter_bal`, the balance row's raw value (63 = 0).
- **Why:** the shared part keeps its size (56 B), and the union stays
  4-aligned.
- **Accessors:** `crumpBit_writeParam()` (E6), `crumpBit_process()`
  (E13), `crumpBit_init()` (E4).

Remove (HEAD 209–209):

```c
    uint8_t pad;               /* alignment of the union */
```

Replace with:

```c
    uint8_t filter_bal;        /* row 22, shared: filter balance (P4) */
```

#### H6 — Helper declarations: Position limit; Position/Density stop helpers

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, HEAD lines 291–301: **replace** (11 lines → 46; new lines 318–363).

- **What:**
  - Updates the helper comment: the `grn` Rate limit is now the delay-time
    curve at the current tempo.
  - Declares `crumpBit_grainPosLimit(share)`.
  - Adds two `static inline` helpers shared by the DSP and the page:
    - `crumpBit_grainPosStop(v)`: Position 64..127 → stop (v − 64)/4,
      shortest first, four values per stop;
    - `crumpBit_grainDenStop(v)`: Density 65..127 → stop
      15 − (v − 65)·16/63, the longest (`1br`) first.
  - `CRUMPBIT_GRAIN_NO_STOP` (0xFF) outside the rhythmic halves.
- **Why:** the DSP, the clamp and the label must name the same division
  for the same value (plan §A1.8). Defined once, they cannot drift.
- **Inputs:** the stored row value.
- **Output:** a stop index 0..15 into `crumpBit_grainStopCc`, or
  `CRUMPBIT_GRAIN_NO_STOP`.
- **Accessors:** E3, E7, C9.

Remove (HEAD 291–301):

```c
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
```

Replace with:

```c
 * under one sub-type's law (delay curve, comb table, pitch window, the `grn`
 * delay-time curve within a half line at the current tempo (S091 P4), the
 * `rvb` word buffer); the effective_max op and the Rate label use it.
 * crumpBit_grainMaxSamples(): the longest `grn` delay or lag, one half line
 * less the guard. crumpBit_combNote(): the MIDI note a `cmb` `rte` plays
 * (112 - rte, clamped at 96), for the note-name label.
 * crumpBit_grainPosLimit() (S091 P4): the largest `dpn` (Position) value in
 * `grn` whose rhythmic lag fits a half line at the current tempo; values at
 * or below 63 (follow, scatter) are never limited (plan A1.8, A3.4).
 * Inputs: sub-type id, share (may be NULL), or the raw `rte`. Outputs: as
 * named. Accessors: CrumpBitEffect.c, crumpBit_uiFormatValue3().
 */
uint8_t crumpBit_subtypeRateLimit(uint8_t subtype, const fx_share_t *share);
float crumpBit_grainMaxSamples(const fx_share_t *share);
uint8_t crumpBit_combNote(uint8_t rate);
uint8_t crumpBit_grainPosLimit(const fx_share_t *share);

/*
 * `grn` rhythmic stop of a Position or Density value (S091 P4, plan A1.8).
 *
 * What: the knob value -> index into crumpBit_grainStopCc (0 = /64 .. 15 =
 * 1br), or CRUMPBIT_GRAIN_NO_STOP outside the rhythmic half.
 * - Position: 64..127 (positive) -> (v - 64) / 4: four values per stop,
 *   shortest first, so turning up plays later.
 * - Density: 64 is +1 (one trigger per N, not a stop); 65..127 -> 15 -
 *   (v - 65) x 16 / 63: the longest stop (1br) first, shorter periods (more
 *   grains) as the knob turns up.
 * Why shared: the DSP and the page label must name the same division.
 * Inputs: the stored row value. Output: 0..15 or CRUMPBIT_GRAIN_NO_STOP.
 * Accessors: crumpBit_runGrain(), crumpBit_grainPosLimit(),
 * crumpBit_uiFormatValue3().
 */
#define CRUMPBIT_GRAIN_NO_STOP   0xFFu

static inline uint8_t crumpBit_grainPosStop(uint8_t value)
{
    return (value > 63u) ? (uint8_t)((value - 64u) / 4u)
                         : (uint8_t)CRUMPBIT_GRAIN_NO_STOP;
}

static inline uint8_t crumpBit_grainDenStop(uint8_t value)
{
    return (value > 64u)
        ? (uint8_t)(15u - ((uint32_t)(value - 65u) * 16u) / 63u)
        : (uint8_t)CRUMPBIT_GRAIN_NO_STOP;
}
```

### 3.2 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`

#### E1 — `grn` constants; size guards (100 B / 44 B); 16-bit line guard

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 109–140: **replace** (32 lines → 53; new lines 109–161).

- **What:** replaces the S091 P3 grain constants (`CRUMPBIT_GRAIN_MIN` 64,
  ±35-cent detune, the linear cent slope) with the target model's:

  | Constant | Value | Meaning |
  |---|---|---|
  | `CRUMPBIT_GRAIN_LINE_MIN` | 512 | the shortest line `grn` plays in; below it the block passes through |
  | `CRUMPBIT_GRAIN_MIN_LEN` | 220.0 | the shortest Length, ≈ 5 ms (Q1) |
  | `CRUMPBIT_GRAIN_FADE` / `_K` | 128 / 1/128 | every grain's fade-in and fade-out |
  | `CRUMPBIT_GRAIN_MIN_PERIOD` | 32 | the shortest trigger interval, one block (≤ 1 trigger per block) |
  | `CRUMPBIT_GRAIN_LAG0` | 2.0 | Position 0's lag behind the write head |
  | `CRUMPBIT_GRAIN_DETUNE_CENTS` | 100.0 | `spr` 127 detune, ±1 st (A1.5) |

  - The runtime guard becomes 100 B and the union guard 44 B.
  - A new assert proves a line's grain end (N + 128) and the no-anchor
    marker fit `uint16_t`.
- **Why:**
  - The fade must be a power of two for the copy identity's exact
    crossfade (plan §A7.3).
  - The minimum period keeps the frame loop's two-span structure.
  - The size guards make any later growth a deliberate change.
- **Accessors:** the grain engine (E7), the half-line helpers (E2, E3),
  the seat (E9).

Remove (HEAD 109–140):

```c
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
```

Replace with:

```c
 * `grn` (S091 P4, S091_P4_GRN_TWEAKS.md Part A):
 * - CRUMPBIT_GRAIN_LINE_MIN the shortest record line `grn` plays in (below
 *   it the block passes through); also the smallest share the half-line
 *   helpers report;
 * - CRUMPBIT_GRAIN_MIN_LEN the shortest Length, 220 frames (~5 ms, Q1);
 *   only the overwrite clamp (A3.2) may cut a grain shorter;
 * - CRUMPBIT_GRAIN_FADE every grain's fade, 128 frames (2.9 ms): fade-in
 *   over the first 128 frames, fade-out over the 128 frames after its
 *   Length. A power of two, so the window steps (1/128) are exact and two
 *   back-to-back grains sum to exactly 1 (the copy setting, A1.9);
 * - CRUMPBIT_GRAIN_MIN_PERIOD the shortest trigger interval, one block (32
 *   frames, OUTPUT_DMA_SIZE): at most one trigger per block, so the frame
 *   loop runs as two spans around it;
 * - CRUMPBIT_GRAIN_LAG0 the lag at Position 0: two frames behind the write
 *   head (the newest written sample is one frame back);
 * - CRUMPBIT_GRAIN_DETUNE_CENTS the detune at `spr` 127 (plan A1.5: +-1
 *   semitone), applied through crumpBit_ratioOf().
 * CRUMPBIT_RVB_BASE0..3 the four FDN line lengths at size 1.0 (32-47 ms,
 * mutually prime); CRUMPBIT_RVB_FREEZE_LG the log2 per-sample gain while
 * frozen (gain 0.999 per 2,053 samples).
 * Accessors: the crumpBit_run*() engines, crumpBit_grainPick().
 */
#define CRUMPBIT_NOINLINE             __attribute__((noinline))
#define CRUMPBIT_COMB_GLIDE_K         4.5245e-3f
#define CRUMPBIT_PITCH_GUARD          2.0f
#define CRUMPBIT_GRAIN_LINE_MIN       512u
#define CRUMPBIT_GRAIN_MIN_LEN        220.0f
#define CRUMPBIT_GRAIN_FADE           128u
#define CRUMPBIT_GRAIN_FADE_K         (1.0f / 128.0f)
#define CRUMPBIT_GRAIN_MIN_PERIOD     32u
#define CRUMPBIT_GRAIN_LAG0           2.0f
#define CRUMPBIT_GRAIN_DETUNE_CENTS   100.0f
#define CRUMPBIT_RVB_BASE0            1433u
#define CRUMPBIT_RVB_BASE1            1601u
#define CRUMPBIT_RVB_BASE2            1867u
#define CRUMPBIT_RVB_BASE3            2053u
#define CRUMPBIT_RVB_FREEZE_LG        (-7.0272e-7f)

/*
 * S091 P4: 100 B = 56 B shared + a 44 B sub-type union (the reworked `grn`
 * state; user-approved growth up to +32 B over 76 B, 2026-11-04: +24 B
 * used). EffectsManager.c guards the union at 100 B; the arena may lose one
 * 32 B granule with it (LTO packing decides; CRUMPBIT_LOOP_MAX_BYTES assumes
 * it does).
 */
_Static_assert(sizeof(CrumpBitRuntime) == 100u,
               "CrumpBitRuntime size is recorded in the S091 P4 schedule");
_Static_assert(sizeof(((CrumpBitRuntime *)0)->u) == 44u,
               "the sub-type union is 44 B (grn is the largest member)");
/* The grain line fits 16-bit indices with room for a grain's end. */
_Static_assert(CRUMPBIT_LOOP_MAX_BYTES / 2u + CRUMPBIT_GRAIN_FADE <
               CRUMPBIT_GRAIN_NO_ANCHOR,
               "grn ends (L + fade) and the no-anchor marker fit uint16_t");
```

#### E2 — `crumpBit_grainMaxSamples()`: the line minimum

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 311–311: **replace** (1 lines → 1; new lines 332–332).

- **What:** a half line shorter than `CRUMPBIT_GRAIN_LINE_MIN` reports 0
  (was 64 + guard).
- **Why:** it agrees with the seat's pass-through threshold (E9), so the
  page, the clamps and the DSP agree on when `grn` plays.
- **Callers:** E3, `crumpBit_grainPosLimit()`, C8.

Remove (HEAD 311–311):

```c
    return (half < CRUMPBIT_GRAIN_MIN + CRUMPBIT_LOOP_GUARD)
```

Replace with:

```c
    return (half < CRUMPBIT_GRAIN_LINE_MIN)
```

#### E3 — `grn` Rate limit on the delay-time curve; `crumpBit_grainPosLimit()`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 328–339: **replace** (12 lines → 49; new lines 349–397).

- **What:**
  - **`crumpBit_subtypeRateLimit()` for `grn`:** the largest `rte` whose
    `crumpBit_grainTime` tick count fits one line at the current tempo,
    i.e. `tableLimit(grainTime, maxSamples × bpm / samplesPerTick)`.
    Before, the free Rate curve was snapped by the division search.
  - **`crumpBit_grainPosLimit(share)`** (new): the largest Position value
    whose rhythmic lag fits one line.
    - `max_ticks` = the half line at this tempo, in 96-PPQ ticks.
    - `fit` = how many stops fit, built by a fixed binary search: probes 8,
      4, 2, 1, then a fifth for the 16th stop, as `crumpBit_tableLimit()`
      does for 128 entries. Stop lengths ascend, so the fitting stops are a
      prefix.
    - Stop k owns values 64 + 4k .. 67 + 4k, so the limit is 63 + 4·fit.
      Values ≤ 63 (follow, scatter) are never limited.
- **Why:** Q7, Q8, Q10. Every rhythmic control is fixed and clamped: a value
  always names the same division, and the knob stops at the longest that
  fits.
- **Cost:** `grainPosLimit` 76 instructions straight, once per block in
  `effects_service()`.
- **Inputs:** share (NULL = empty); `seq_getBpm()`.
- **Outputs:** limit 0..127 / 63..127.
- **Callers:** `crumpBit_effectiveMax()` (E16), the Position label (C9),
  host CB6.

Remove (HEAD 328–339):

```c
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
```

Replace with:

```c
    case CRUMPBIT_SUBTYPE_GRAIN: {
        /*
         * S091 P4: the delay-time curve is in 96-PPQ ticks, so the longest
         * delay that fits one line is max / samples-per-tick ticks at the
         * current tempo (plan Q8, Q10: fixed and clamped).
         */
        const uint16_t bpm = seq_getBpm();

        return crumpBit_tableLimit(crumpBit_grainTime,
                                   crumpBit_grainMaxSamples(share) *
                                   (float)(bpm ? bpm : 1u) *
                                   (1.0f / CRUMPBIT_SAMPLES_PER_TICK));
    }
    case CRUMPBIT_SUBTYPE_REVERB:
        /* The longest line (base 2,053 x size) must fit the word buffer. */
        return crumpBit_tableLimit(crumpBit_reverbSize,
                                   (words - 2.0f) *
                                   (1.0f / (float)CRUMPBIT_RVB_BASE3));
    default:
        return crumpBit_tableLimit(crumpBit_rateTable, max_samples);
    }
}

uint8_t crumpBit_grainPosLimit(const fx_share_t *share)
{
    /* max_ticks: the longest lag one line holds, in ticks at this tempo. */
    const uint16_t bpm = seq_getBpm();
    const float max_ticks = crumpBit_grainMaxSamples(share) *
                            (float)(bpm ? bpm : 1u) *
                            (1.0f / CRUMPBIT_SAMPLES_PER_TICK);
    /*
     * fit: how many stops (shortest first) fit. The stop lengths ascend, so
     * the fitting ones are a prefix; the search builds the count from the
     * top bit down (8, 4, 2, 1: fixed probes, compare-selects), then a fifth
     * probe decides the 16th stop, as crumpBit_tableLimit() does for 128.
     */
    uint32_t fit = 0u;
    uint32_t step;

    for (step = CRUMPBIT_GRAIN_STOPS / 2u; step != 0u; step >>= 1u)
        fit += (crumpBit_grainTime[crumpBit_grainStopCc[fit + step - 1u]] <=
                max_ticks) ? step : 0u;
    fit += (crumpBit_grainTime[crumpBit_grainStopCc[fit]] <= max_ticks)
        ? 1u : 0u;
    /*
     * Stop k owns the four values 64 + 4k .. 67 + 4k, so the last fitting
     * stop ends at 63 + 4 fit; with none fitting only follow/scatter remain.
     */
    return (uint8_t)(63u + 4u * fit);
```

#### E4 — Init: the copy setting's seeds; `bal` centred

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 588–588: **replace** (1 lines → 7; new lines 646–652).

- **What:** `grain_density` 64 (+1, was 127), `grain_length` 127 (the
  whole line forward), `filter_bal` 63 (0), until the forced resolution
  writes the Scene's rows.
- **Why:** plan §A1.9. A runtime entered without Scene values plays the
  copy setting and an unbalanced filter.
- **Proof:** CB5.

Remove (HEAD 588–588):

```c
    rt->grain_density = 127u;
```

Replace with:

```c
    /*
     * S091 P4 neutral `grn` rows: the copy setting (plan A1.9): Density +1
     * (one trigger per line), Length full forward; the filter balance 0.
     */
    rt->grain_density = 64u;
    rt->grain_length = 127u;
    rt->filter_bal = 63u;
```

#### E5 — `write_param` comment: rows 20 and 22

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 690–690: **replace** (1 lines → 3; new lines 754–756).

- **What:** records row 20 = grain Length (was the level jitter) and row
  22 = the shared balance.

Remove (HEAD 690–690):

```c
 * sub-type, so a switch keeps every value (plan §2.4). Inputs: runtime, row index, value
```

Replace with:

```c
 * sub-type, so a switch keeps every value (plan §2.4). S091 P4: row 20 is
 * the grain Length (was the level jitter), row 22 the shared filter
 * balance. Inputs: runtime, row index, value
```

#### E6 — `write_param`: rows 20 and 22

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 721–721: **replace** (1 lines → 2; new lines 787–788).

- **What:**
  - `CRUMPBIT_PARAM_GRAIN_LENGTH` → `grain_length`;
  - `CRUMPBIT_PARAM_FILTER_BAL` → `filter_bal`;
  - the `GRAIN_AMP` case goes.
- **Why:** the rows are stored raw and converted once per block, so an LFO
  on them costs nothing per frame.
- **Caller:** `effects_service()`.

Remove (HEAD 721–721):

```c
    case CRUMPBIT_PARAM_GRAIN_AMP:      rt->grain_amp = value;         break;
```

Replace with:

```c
    case CRUMPBIT_PARAM_GRAIN_LENGTH:   rt->grain_length = value;      break;
    case CRUMPBIT_PARAM_FILTER_BAL:     rt->filter_bal = value;        break;
```

#### E7 — The grain engine: candidate, gains, loop state, frame loop, block

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 1436–1909: **replace** (474 lines → 647; new lines 1503–2149).

- **What:** replaces the whole S091 P3 grain engine (HEAD 1436–1909, from
  the candidate's comment to the end of `crumpBit_runGrain()`) with the
  target model, in five sub-blocks.
- **Why:** plan Part A.
  - The trigger is decoupled from length.
  - The window becomes a flat top with exact complementary fades.
  - Position, Length, Density, Width and pitch get their new laws.
  - The delay tap gets its own time.
  - The anchored freeze, steal-oldest and the overwrite clamp are added.
- **Kept:**
  - the record line and delay line split;
  - the redirected freeze write;
  - the drop-sample 16.16 reads;
  - one hardware RNG word per block;
  - the two-span frame loop around the block's one event;
  - the return chain;
  - the shared mix stage.

Remove HEAD lines 1436–1909 (474 lines), from
`* One grain restart candidate (plan §3.3.3-§3.3.5).`
through
`g->age = (uint16_t)age;`.

Replace with the 5 sub-blocks below, in order:

##### E7.1 — The candidate: `crumpbit_grain_t`, `crumpBit_hashNext()`, `crumpBit_hashUnit()`, `crumpBit_grainPick()` (new lines 1503–1668)

- **What:** one new grain, as a pure function of the block's hash and the
  rows (host-tested in CB14).
  - **Five uniforms** from the block's one hardware word (xorshift32), via
    `crumpBit_hashUnit()` (24-bit fraction):
    - `u_pos` (scatter);
    - `u_slot` (pitch slot);
    - `u_tune` (detune);
    - `u_pan` (pan magnitude);
    - `u_side` (pan side).
  - **Pitch (A1.5, Q4):**
    - the slot is P1 (`aux`), P2 (`st2`) or P3 (`st3`), each absolute
      (`crumpBit_rowSemitones()`, ±24 st);
    - weights 1 : a : b with a = min(spr, 40)/40 and
      b = clamp((spr − 40)/40, 0, 1);
    - above 80, a uniform detune of ±(spr − 80)·100/47 cents
      (±1 st at 127);
    - the ratio comes from `crumpBit_ratioOf()` (the semitone table,
      interpolated);
    - quantised to the Q3.12 step `q`, and the clamps use the ratio `q`
      actually plays.
  - **Direction and Length (A1.2):**
    - `ld = len − 63`, reversed when negative;
    - the magnitude `m` picks `crumpBit_grainFraction[m]` (2^(−8(1 − m/64)),
      1/256..1) of N;
    - at least 220 frames;
    - `span` = L + 128 is every frame the grain reads.
  - **Position (A1.1):**
    - 63 = `LAG0` (two behind the reference head);
    - above 63, the stop's ticks × samples-per-tick. Not yet recorded that
      far: `span` = 0, so the grain does not sound and the trigger retries
      (§A7.3);
    - below 63, uniform in [2, 2 + m·(max − 2)] with m = (63 − pos)/63;
    - always clamped to [2, `max_lag`], `max_lag` = min(valid, N) − 2.
  - **Overwrite clamp (A3.2, Q3):**
    - `gr` = (head moves ? 1 : 0) + (reversed ? r : −r) is how the read lag
      changes per frame;
    - **`gr` < 0** (a forward grain catching the head): first the headroom
      `need` = 2 − gr·span pushes the start back (bounded by `max_lag`),
      then the span is cut to (lag − 2)/−gr;
    - **`gr` > 0:** cut to (max_lag − lag)/gr;
    - **frozen, full line:** no clamp (it wraps freely, A1.7);
    - **too short to fade** (span ≤ 128): `end` 0.
  - **Start:** `ref` − lag, wrapped into the line. `ref` is the write
    position at the event frame, or the anchor while frozen.
  - **Width (A1.4):**
    - ≤ 64: p = (2u − 1)·wid/64;
    - above 64: |p| uniform in [lo, 1], lo = 0.5·(wid − 64)/63, random side;
    - stored as `pan` = 127 + 127p (0..254).
- **Inputs:** hash, runtime rows, N, reference head, `valid`, frozen flag,
  samples per tick.
- **Output:** `start`, `inc`, `end`, `pan`.
- **Callers:** `crumpBit_runGrain()` once per block (constant cost), host
  CB14.

```c
 * One new grain (S091 P4; S091_P4_GRN_TWEAKS.md A1.1-A1.5, A3.2).
 *
 * What: a pure function of the block's hashed random word and the rows, so
 * the host can test its laws directly (CB14):
 * - pitch slot by `spr` from the ABSOLUTE pitches P1 (`aux`), P2 (`st2`),
 *   P3 (`st3`), each row - 63 semitones clamped to +-24 (Q4): weights
 *   1 : a : b with a = min(spr, 40)/40, b = clamp((spr - 40)/40, 0, 1), so
 *   P1 only at 0, P1/P2 halves at 40, equal thirds at 80; above 80 a
 *   uniform detune of up to +-(spr - 80) x 100/47 cents (+-1 st at 127);
 * - Length from the `len` row (row 20): m = |len - 63| (0..64) gives
 *   L = N x crumpBit_grainFraction[m] (eight octaves up to the whole line),
 *   at least 220 frames; left of 63 plays reversed (A1.2);
 * - the start lag by Position (`dpn`, row 10): 63 = two frames behind the
 *   reference head; right of 63 the rhythmic stop (crumpBit_grainPosStop())
 *   at the current tempo, and until that lag is recorded the grain does
 *   not sound (end 0: the trigger is retried), so a delay never starts at
 *   a wrong, shorter lag; left of 63 uniform in [2, 2 + m (max - 2)] with
 *   m = (63 - pos)/63, so fully left is anywhere in the recorded line
 *   (A1.1); always inside what is recorded;
 * - the overwrite clamp (Q3): the read lag changes by gr per frame, where
 *   gr = (head moves ? 1 : 0) + (reversed ? r : -r) (the head moves unless
 *   frozen). A shrinking lag (gr < 0, a forward grain faster than the head)
 *   first starts further back (headroom), then the span L + fade is cut to
 *   (lag - 2)/-gr; a growing lag (gr > 0) is cut to (max - lag)/gr. A grain
 *   left with no span does not sound (end 0). A frozen, full line wraps
 *   freely and is not clamped (A1.7);
 * - the pan by `wid` (A1.4): 0 mono; up to 64 uniform within +-wid/64;
 *   above 64 a magnitude uniform in [lo, 1], lo = 0.5 (wid - 64)/63, on a
 *   random side, so 127 is 50-100 % left or right.
 * Inputs: hash state, runtime rows, line length N, the reference head
 * (the write position at the trigger frame, or the freeze anchor), valid,
 * frozen flag, samples per 96-PPQ tick. Output: *out.
 * Callers: crumpBit_runGrain() (once per block, constant cost), host CB14.
 */
typedef struct {
    uint32_t start;   /* record-line index the grain starts at */
    int16_t inc;      /* Q3.12 pitch step, negative = reversed */
    uint16_t end;     /* frames the grain sounds: L + fade; 0 = none */
    uint8_t pan;      /* 0 left .. 127 centre .. 254 right */
} crumpbit_grain_t;

static inline uint32_t crumpBit_hashNext(uint32_t *h)
{
    /* xorshift32: the block's one hardware word expanded (G9). */
    *h ^= *h << 13;
    *h ^= *h >> 17;
    *h ^= *h << 5;
    return *h;
}

/* A uniform fraction in [0, 1) from the next hash word (24 bits). */
static inline float crumpBit_hashUnit(uint32_t *h)
{
    return (float)(crumpBit_hashNext(h) >> 8) * (1.0f / 16777216.0f);
}

static void crumpBit_grainPick(uint32_t *hash, const CrumpBitRuntime *rt,
                               uint32_t len, uint32_t ref, uint32_t valid,
                               uint32_t frozen, float per_tick,
                               crumpbit_grain_t *out)
{
    const float u_pos = crumpBit_hashUnit(hash);
    const float u_slot = crumpBit_hashUnit(hash);
    const float u_tune = crumpBit_hashUnit(hash);
    const float u_pan = crumpBit_hashUnit(hash);
    const float u_side = crumpBit_hashUnit(hash);
    const uint8_t spr = rt->grain_spread;
    const uint8_t pos = rt->pan_raw;
    const uint8_t wid = rt->grain_width;
    /* a, bw: the weights of P2 and P3 against P1's 1. */
    const float a = (spr >= 40u) ? 1.0f : (float)spr * (1.0f / 40.0f);
    const float bw = (spr <= 40u) ? 0.0f
                   : ((spr >= 80u) ? 1.0f : (float)(spr - 40u) * (1.0f / 40.0f));
    const float pick = u_slot * (1.0f + a + bw);
    /* ld: Length's distance from the centre; its sign is the direction. */
    const int32_t ld = (int32_t)rt->grain_length - 63;
    const uint32_t m = (uint32_t)((ld < 0) ? -ld : ld);
    const uint32_t reverse = (ld < 0) ? 1u : 0u;
    /* max_lag: the oldest recorded sample a read may reach, as a lag. */
    const float max_lag = (float)((valid < len) ? valid : len) - 2.0f;
    int32_t semis;
    float cents;
    float ratio;
    float span;
    float lag;
    float p;

    /* Pitch: P1, P2 or P3, each absolute (Q4). */
    semis = crumpBit_rowSemitones((pick < 1.0f) ? rt->aux_raw
                                  : ((pick < 1.0f + a) ? rt->grain_st2
                                                       : rt->grain_st3));
    cents = (spr > 80u)
        ? (2.0f * u_tune - 1.0f) * (float)(spr - 80u) *
          (CRUMPBIT_GRAIN_DETUNE_CENTS / 47.0f)
        : 0.0f;
    ratio = crumpBit_ratioOf((float)semis + cents * 0.01f);
    {
        /* q: the Q3.12 step; the clamps below use the ratio it plays. */
        const int32_t q = (int32_t)(ratio * 4096.0f + 0.5f);

        out->inc = (int16_t)(reverse ? -q : q);
        ratio = (float)q * (1.0f / 4096.0f);
    }

    /* Length (A1.2): eight octaves up to N, at least ~5 ms (Q1). */
    span = (float)len * crumpBit_grainFraction[(m > CRUMPBIT_GRAIN_STEPS)
                                               ? CRUMPBIT_GRAIN_STEPS : m];
    span = (span < CRUMPBIT_GRAIN_MIN_LEN) ? CRUMPBIT_GRAIN_MIN_LEN : span;
    /* From here `span` is every frame the grain reads: L + the fade-out. */
    span += (float)CRUMPBIT_GRAIN_FADE;

    /* Position (A1.1): follow, rhythmic lag, or scatter. */
    if (pos > 63u) {
        lag = crumpBit_grainTime[crumpBit_grainStopCc[
                  crumpBit_grainPosStop(pos)]] * per_tick;
        /* Not recorded that far back yet: no grain; the trigger retries. */
        span = (lag > max_lag) ? 0.0f : span;
    } else if (pos < 63u)
        lag = CRUMPBIT_GRAIN_LAG0 + u_pos * (float)(63u - pos) *
              (1.0f / 63.0f) * (max_lag - CRUMPBIT_GRAIN_LAG0);
    else
        lag = CRUMPBIT_GRAIN_LAG0;
    lag = (lag > max_lag) ? max_lag : lag;
    lag = (lag < CRUMPBIT_GRAIN_LAG0) ? CRUMPBIT_GRAIN_LAG0 : lag;

    /* Overwrite clamp (A3.2, Q3); a frozen full line wraps freely. */
    if (!frozen || valid < len) {
        /* gr: how the read lag changes per frame (head minus read speed). */
        const float gr = (frozen ? 0.0f : 1.0f) + (reverse ? ratio : -ratio);
        float s_max = span;

        if (gr < 0.0f) {
            /* need: the headroom a catching-up grain must start with. */
            const float need = CRUMPBIT_GRAIN_LAG0 - gr * span;

            lag = (lag < need) ? ((need > max_lag) ? max_lag : need) : lag;
            s_max = (lag - CRUMPBIT_GRAIN_LAG0) / -gr;
        } else if (gr > 0.0f) {
            s_max = (max_lag - lag) / gr;
        }
        span = (span > s_max) ? s_max : span;
    }
    /* end: the span as frames; a grain without room to fade does not sound. */
    out->end = (max_lag >= CRUMPBIT_GRAIN_LAG0 &&
                span > (float)CRUMPBIT_GRAIN_FADE)
        ? (uint16_t)span : 0u;
    {
        /* start: the reference head minus the lag, wrapped into the line. */
        const uint32_t il = (uint32_t)lag;

        out->start = (ref >= il) ? ref - il : ref + len - il;
    }

    /* Width (A1.4): p in [-1, 1], 0 = centre. */
    if (wid <= 64u) {
        p = (2.0f * u_pan - 1.0f) * (float)wid * (1.0f / 64.0f);
    } else {
        /* lo: the smallest |pan| above 64, 0.5 at 127. */
        const float lo = 0.5f * (float)(wid - 64u) * (1.0f / 63.0f);
        const float mag = lo + u_pan * (1.0f - lo);

        p = (u_side < 0.5f) ? -mag : mag;
    }
    out->pan = (uint8_t)(127.0f + 127.0f * p + 0.5f);
}

```

##### E7.2 — Balance-law gains: `crumpBit_grainGains()` (new lines 1669–1685)

- **What:** p = (pan − 127)/127;
  - left = 1 when p ≤ 0, else 1 − p;
  - right = 1 when p ≥ 0, else 1 + p.
- **Why:** §A7.3. The S075 balance law keeps the centre at unity on both
  sides, so Width 0 and the copy setting reproduce the recorded signal at
  unity; a hard pan silences the far side.
- **Callers:** `crumpBit_runGrain()` (each voice per block, and the new
  voice at its start).

```c
/*
 * The balance-law gains of a pan byte (S091 P4, plan A1.4).
 *
 * What: p = (pan - 127)/127; left = 1 while p <= 0, else 1 - p; right = 1
 * while p >= 0, else 1 + p. The centre is unity on both sides (the S075 F2
 * balance law of the delay pan), so Width 0 and the copy setting reproduce
 * the recorded signal at unity (A1.9); a hard pan silences the far side.
 * Inputs: pan byte 0..254. Outputs: *gl, *gr. Caller: crumpBit_runGrain().
 */
static inline void crumpBit_grainGains(uint8_t pan, float *gl, float *gr)
{
    const float p = ((float)pan - 127.0f) * (1.0f / 127.0f);

    *gl = (p <= 0.0f) ? 1.0f : 1.0f - p;
    *gr = (p >= 0.0f) ? 1.0f : 1.0f + p;
}

```

##### E7.3 — Loop state and block constants: `crumpbit_grain_run_t`, `crumpbit_grain_k_t` (new lines 1686–1745)

- **What:**
  - **The per-frame state:**
    - the write index;
    - per voice, position, step (inc × 16), wrap (±lim by direction);
    - the window ramps `in_*` (age/128, rising) and `out_*`
      ((end − age)/128, falling);
    - the balance gains;
    - the delay line's DC memories;
    - the feedback and mix ramps;
    - the AC memories.
  - **The block constants:**
    - the io pointers;
    - the record write base `rec_w` (the record line, or the delay line
      while frozen);
    - the bases and lengths;
    - the delay tap `d` (whole frames);
    - the ramp steps;
    - the tap's gate mask.
- **Why:** both live on `crumpBit_runGrain()`'s stack and pass by pointer
  only to the inline span, so the compiler keeps the fields in registers.
  The two spans share one body.

```c
/*
 * The `grn` frame loop's state and per-block constants (engine B').
 *
 * What: crumpbit_grain_run_t holds everything one frame reads and writes
 * (positions, steps, wraps, the window ramps, gains, the delay line's
 * return memories, the ramps, the AC memories); crumpbit_grain_k_t the
 * block's constants. Both live on the stack of crumpBit_runGrain() and are
 * passed by pointer only to the inline crumpBit_grainSpan(), so the compiler
 * keeps their fields in registers (scalar replacement after inlining).
 * Why: the frame loop runs as two spans split at the block's one grain
 * event (a trigger or a voice steal; at most one per block, the minimum
 * trigger interval being one block). The event is applied once between the
 * spans, so no frame pays for selects.
 * Accessors: crumpBit_runGrain(), crumpBit_grainSpan().
 */
typedef struct {
    uint32_t wp;        /* shared write index of both lines */
    uint32_t pos_a;     /* voice A read position, 16.16 */
    uint32_t pos_b;     /* voice B read position, 16.16 */
    uint32_t step_a;    /* pos_a step per frame: inc x 16 (two's complement) */
    uint32_t step_b;
    uint32_t wrap_a;    /* subtracted when pos_a leaves [0, lim): lim, or
                           -lim (an unsigned underflow) for a reversed grain */
    uint32_t wrap_b;
    int32_t ac_cl;      /* stage 1 previous codes */
    int32_t ac_cr;
    float in_a;         /* voice A fade-in ramp: age / 128 */
    float out_a;        /* voice A fade-out ramp: (end - age) / 128 */
    float in_b;
    float out_b;
    float gla;          /* voice A left gain (balance law) */
    float gra;          /* voice A right gain */
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
    uint32_t d;         /* delay-line tap D, whole frames (2..len-2) */
    float fb_inc;
    float mix_inc;
    int32_t gate;       /* the delay tap's gate mask: all ones or zero */
} crumpbit_grain_k_t;

```

##### E7.4 — The frame loop: `crumpBit_grainSpan()` (new lines 1746–1846)

- **What, per frame over [n0, n1), every statement every frame:**
  1. stage 1, the mix and feedback ramps;
  2. the record write through `rec_w` (frozen writes land on the delay
     slot and are overwritten below: no select);
  3. each voice's drop-sample read, then `pos += step` and one
     compare-and-subtract wrap;
  4. **the flat-top windows:** `w = clamp(min(in, out), 0, 1)`, then
     `in += 1/128`, `out −= 1/128`. A grain fades in over 128 frames,
     holds 1, and fades out over the 128 frames after L. An idle voice has
     out ≤ 0 and is silent. All steps are exact (2⁻⁷), so the copy
     setting's overlapping fades sum to exactly 1;
  5. the windowed samples (`VCVT #7`) and balance gains into the stereo
     grains;
  6. **the delay line:** the code exactly D behind the write head (one
     subtract and compare wrap), gate-masked, then the return chain (no
     tone stage) re-recording the grains' mono sum;
  7. the mix stage with wet = grains + tap.
- **Inputs:** run state (in/out), constants, the span.
- **Outputs:** io, both lines, the run state.
- **Cost:** 136 / 133 per frame (the two inlined copies).
- **Caller:** `crumpBit_runGrain()`.

```c
/*
 * The `grn` frame loop over frames [n0, n1) (plan A2).
 *
 * What, per frame, every statement every frame:
 * - stage 1 (shared);
 * - the record line takes the dry mono code (through rec_w: frozen writes
 *   land on the delay line's slot, overwritten below);
 * - each voice reads its record sample drop-sample at pos >> 16 (G6), then
 *   pos += step and wraps by one compare (wrap = +-lim by direction);
 * - each voice's flat-top window w = clamp(min(in, out), 0, 1): `in` rises
 *   1/128 per frame from 0 at the start, `out` falls 1/128 per frame and
 *   reaches 0 at the grain's end (L + 128), so a grain fades in over 128
 *   frames, holds 1, and fades out over the 128 frames after L; an idle
 *   voice has out <= 0 and is silent. All steps are exact (2^-7);
 * - the voices scaled by their windows (the scaling VCVT gives +-1) and
 *   their balance gains into the stereo grain signal;
 * - the delay line: the code exactly D behind the write head (one subtract
 *   and compare wrap), gate-masked, then the return chain (no tone stage)
 *   re-recording the grains' mono sum plus the return;
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
        /* wa, wb: the voices' window values for this frame. */
        float wa;
        float wb;
        /* sa, sb: the voices' windowed samples. */
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
        /* Flat-top windows: the lower ramp, at most 1, never negative. */
        wa = (s->in_a < s->out_a) ? s->in_a : s->out_a;
        wb = (s->in_b < s->out_b) ? s->in_b : s->out_b;
        wa = (wa > 1.0f) ? 1.0f : wa;
        wb = (wb > 1.0f) ? 1.0f : wb;
        wa = (wa < 0.0f) ? 0.0f : wa;
        wb = (wb < 0.0f) ? 0.0f : wb;
        s->in_a += CRUMPBIT_GRAIN_FADE_K;
        s->out_a -= CRUMPBIT_GRAIN_FADE_K;
        s->in_b += CRUMPBIT_GRAIN_FADE_K;
        s->out_b -= CRUMPBIT_GRAIN_FADE_K;
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

##### E7.5 — The block: `crumpBit_runGrain()` (new lines 1847–2149)

- **What, per block:**
  1. **Geometry:** N = half the share; the delay line at base + N; freeze
     redirects the record write.
  2. **Freeze anchor (A3.3):**
     - set to the write position on the first frozen block;
     - released on unfreeze;
     - an unfreeze before the line was ever full restarts `valid` at 0,
       because the frozen stretch was never written;
     - `valid` does not grow while frozen.
  3. **Delay time (Q8, Q10):** D = `crumpBit_grainTime[rte]` ticks ×
     samples-per-tick, clamped to [2, N − 2]; the tap is gated until
     `valid` ≥ D + frames.
  4. **One hardware RNG word**, hashed; `u_iv` for a random interval.
  5. **Density (A1.3, Q6, Q7)** into `interval` (frames after a trigger)
     and a cap on the countdown:
     - +1: N;
     - +2..: the stop's length, at most N;
     - negative: u·2M with M = N·`crumpBit_grainDensity[63 − den]`;
     - at least 32 frames;
     - off: no trigger, countdown 0.

     A faster setting acts within one period, because the countdown is
     clamped to the current period.
  6. **The block's one event:**
     - the deferred trigger `t_pend` first, else the Density trigger
       `t_reg` (when both fall in one block, the Density trigger waits:
       `deferred`);
     - the voice ages at the event frame decide: a free voice takes the
       trigger;
     - with both busy, a Density trigger steals the older voice, and while
       a steal runs a further trigger is dropped.
  7. **One candidate per block** (constant cost) for the event frame's
     reference head. A trigger whose candidate cannot sound (`end` 0) is
     cancelled and retried next block (`retry`).
  8. **The run state:** window ramps from age and end, gains from the pan
     bytes, ramps and AC memories.
  9. **Span 1**, then the event:
     - **a start:** position, step, wrap, gains, in = 0,
       out = end/128, its bytes stored;
     - **a steal:** the older voice's `out` = 1, its end = age + 128, and
       `pend_next` = 1 + ev + 128 − frames.

     Then **span 2**.
  10. **After the loop:**
      - the countdown (0 when deferred or retried, else t + interval −
        frames, or minus frames);
      - `pending` = `pend_next`;
      - ages advanced and saturated at their ends;
      - state, `valid` (not while frozen) and AC memories stored.
- **Inputs:** runtime, block.
- **Outputs:** io, both lines, grain state, AC state.
- **Caller:** `crumpBit_process()` for `CRUMPBIT_SUBTYPE_GRAIN`.
- **Proofs:**
  - CB14 (copy identity, Density statistics, freeze and anchor,
    stability);
  - CB11 (invariants under switch storms);
  - breaks 7, 10, 12.

```c
/*
 * `grn`: triggered grains over a record line, plus a delay line (engine
 * B'; S091 P4, S091_P4_GRN_TWEAKS.md Part A).
 *
 * What, per block:
 * - geometry: one line = half the share (N); the delay line starts at
 *   base + N; freeze (`snc`) redirects the record write and sets the
 *   anchor (the head position at the freeze), released on unfreeze (A3.3);
 *   `valid` stops growing while frozen, and an unfreeze before the line was
 *   ever full restarts it at 0 (the frozen stretch was never written);
 * - the delay time D = crumpBit_grainTime[rte] ticks at the current tempo,
 *   clamped to [2, N - 2] (Q8, Q10); its tap is gated until D is recorded;
 * - Density (`den`, row 19, bipolar): 63 off; 64 (+1) one trigger per N;
 *   65..127 a rhythmic period (crumpBit_grainDenStop(), longest first) of at
 *   most N; 0..62 a random interval uniform in [0, 2 M], M = N x
 *   crumpBit_grainDensity[63 - den]; at least one block (32 frames). The
 *   countdown is clamped to the current period, so a faster setting acts
 *   within one period;
 * - the block's one event: a deferred (stolen-voice) trigger first, else
 *   the Density trigger (a Density trigger meeting a deferred one in the
 *   same block waits for the next block). A trigger takes the free voice; a
 *   Density trigger with both voices busy steals the older one (A3.1): its
 *   fade-out starts at the trigger frame and the trigger is deferred by the
 *   fade (128 frames), when that voice is free. One steal at a time: while
 *   a trigger is deferred, a further Density trigger with no free voice is
 *   dropped;
 * - ONE candidate (crumpBit_grainPick(), from one hardware RNG word) every
 *   block whether used or not (constant cost). A trigger whose candidate
 *   cannot sound yet (end 0: nothing or too little recorded behind its
 *   lag) is not consumed: it fires again on the next block, so a grain
 *   starts as soon as its audio exists (no silent period at entry);
 * - the run state from the stored state: window ramps from age and end,
 *   gains from the pan bytes.
 * Per frame (crumpBit_grainSpan(), two spans around the event): see there.
 * After the loop: ages advance (saturating at each voice's end); the
 * countdown and the deferred trigger move on by the block.
 * Inputs: runtime, block. Outputs: io, both lines, grain state, AC state.
 * Caller: crumpBit_process() for CRUMPBIT_SUBTYPE_GRAIN.
 */
static CRUMPBIT_NOINLINE void crumpBit_runGrain(CrumpBitRuntime *rt,
                                                const crumpbit_block_t *b)
{
    CrumpBitGrainState *g = &rt->u.grn;
    const uint32_t len = b->len;
    const uint32_t frames = b->frames;
    const float flen = (float)len;
    const uint16_t bpm = seq_getBpm();
    /* per_tick: samples per 96-PPQ tick at the current tempo. */
    const float per_tick = CRUMPBIT_SAMPLES_PER_TICK /
                           (float)(bpm ? bpm : 1u);
    const uint32_t frozen = rt->sync;
    /* dv: Density as a signed step, 0 = off. */
    const int32_t dv = (int32_t)rt->grain_density - 63;
    crumpbit_grain_k_t k;
    crumpbit_grain_run_t s;
    crumpbit_grain_t cand;
    uint32_t hash;
    /* u_iv: the uniform for a random trigger interval. */
    float u_iv;
    /* interval: the frames to the next trigger after one fires. */
    uint32_t interval = 0u;
    /* ev: the event frame (frames = none); ev_trigger: 1 trigger, 0 steal. */
    uint32_t ev = frames;
    uint32_t ev_trigger = 0u;
    /* deferred: the Density trigger waits for the next block. */
    uint32_t deferred = 0u;
    /* retry: the event's trigger could not sound; it fires next block. */
    uint32_t retry = 0u;
    /* voice: the voice the event acts on (0 A, 1 B). */
    uint32_t voice = 0u;
    /* t_reg, t_pend: the Density and deferred trigger frames (or frames). */
    uint32_t t_reg;
    uint32_t t_pend;
    /* pend_next: the deferred trigger stored for the next block. */
    uint32_t pend_next;
    uint32_t age_a;
    uint32_t age_b;
    uint32_t end_a = g->end_a;
    uint32_t end_b = g->end_b;

    k.len = len;
    k.lim = len << 16;
    k.rec = b->base;
    k.dline = b->base + len;
    /* Freeze (`snc`): the record write is redirected, not skipped. */
    k.rec_w = frozen ? k.dline : b->base;
    k.left = b->left;
    k.right = b->right;
    k.keep = b->keep;
    k.invert = b->invert;
    k.mix_inc = b->mix_inc;

    /* Freeze anchor (A3.3): set at the freeze, released on unfreeze. */
    if (frozen) {
        g->anchor = (g->anchor == CRUMPBIT_GRAIN_NO_ANCHOR)
            ? g->write_pos : g->anchor;
    } else if (g->anchor != CRUMPBIT_GRAIN_NO_ANCHOR) {
        g->anchor = CRUMPBIT_GRAIN_NO_ANCHOR;
        /* A line frozen before it filled was not written while frozen. */
        g->valid = (g->valid < len) ? 0u : g->valid;
    }

    /* Delay time D (Q8, Q10): the tick curve at this tempo, in the line. */
    {
        float d = crumpBit_grainTime[(rt->rate > 127u) ? 127u : rt->rate] *
                  per_tick;

        d = (d > flen - 2.0f) ? flen - 2.0f : d;
        d = (d < 2.0f) ? 2.0f : d;
        k.d = (uint32_t)d;
    }
    k.gate = (g->valid >= k.d + frames) ? -1 : 0;

    /* One hardware RNG word per block, expanded by the hash (G9). */
    hash = ((uint32_t)(uint16_t)GetRngValue() << 16) ^ 0x9E3779B9u ^
           g->write_pos;
    hash = (hash == 0u) ? 0x1234567u : hash;
    u_iv = crumpBit_hashUnit(&hash);

    /* Density (A1.3, Q6, Q7): the interval after a trigger, and its cap. */
    if (dv != 0) {
        float iv;

        if (dv > 0) {
            iv = (dv == 1) ? flen
               : crumpBit_grainTime[crumpBit_grainStopCc[
                     crumpBit_grainDenStop(rt->grain_density)]] * per_tick;
            iv = (iv > flen) ? flen : iv;
            /* A rhythmic countdown never waits longer than one period. */
            g->countdown = ((float)g->countdown > iv) ? (uint32_t)iv
                                                      : g->countdown;
        } else {
            /* mean: the average interval; draws are uniform in [0, 2 mean]. */
            const float mean = flen * crumpBit_grainDensity[-dv];

            iv = u_iv * 2.0f * mean;
            g->countdown = ((float)g->countdown > 2.0f * mean)
                ? (uint32_t)(2.0f * mean) : g->countdown;
        }
        interval = (iv < (float)CRUMPBIT_GRAIN_MIN_PERIOD)
            ? CRUMPBIT_GRAIN_MIN_PERIOD : (uint32_t)iv;
    } else {
        /* Off: no trigger; the next Density starts at once. */
        g->countdown = 0u;
    }

    /* The block's one event. */
    t_reg = (dv != 0 && g->countdown < frames) ? g->countdown : frames;
    t_pend = (g->pending != 0u && g->pending - 1u < frames)
        ? g->pending - 1u : frames;
    /* A deferred trigger not due in this block moves on by the block. */
    pend_next = (g->pending != 0u && t_pend >= frames)
        ? (uint32_t)g->pending - frames : 0u;
    if (t_pend < frames) {
        ev = t_pend;
        ev_trigger = 1u;
        deferred = (t_reg < frames) ? 1u : 0u;
    } else if (t_reg < frames) {
        ev = t_reg;
        ev_trigger = 1u;
    }
    /* The ages at the event frame (saturated at each voice's end). */
    age_a = g->age_a + ((ev < frames) ? ev : 0u);
    age_a = (age_a > end_a) ? end_a : age_a;
    age_b = g->age_b + ((ev < frames) ? ev : 0u);
    age_b = (age_b > end_b) ? end_b : age_b;
    if (ev_trigger) {
        if (age_a >= end_a) {
            voice = 0u;
        } else if (age_b >= end_b) {
            voice = 1u;
        } else if (t_pend >= frames && g->pending == 0u) {
            /* Steal (A3.1): the older voice fades out from this frame. */
            ev_trigger = 0u;
            voice = (age_a >= age_b) ? 0u : 1u;
        } else {
            /* A steal is already running: this trigger is dropped. */
            ev = frames;
            ev_trigger = 0u;
        }
    }

    /* One candidate per block, for the event frame's reference head. */
    {
        const uint32_t at = (ev < frames) ? ev : 0u;
        uint32_t ref = g->write_pos + at;

        ref -= (ref >= len) ? len : 0u;
        crumpBit_grainPick(&hash, rt, len,
                           frozen ? g->anchor : ref, g->valid, frozen,
                           per_tick, &cand);
    }
    if (ev_trigger && cand.end == 0u) {
        /* Nothing to play yet: the trigger is kept for the next block. */
        retry = 1u;
        ev = frames;
        ev_trigger = 0u;
    }

    /* The run state from the stored state. */
    s.wp = g->write_pos;
    s.pos_a = g->pos_a;
    s.pos_b = g->pos_b;
    s.step_a = (uint32_t)((int32_t)g->inc_a * 16);
    s.step_b = (uint32_t)((int32_t)g->inc_b * 16);
    s.wrap_a = (g->inc_a < 0) ? 0u - k.lim : k.lim;
    s.wrap_b = (g->inc_b < 0) ? 0u - k.lim : k.lim;
    /* Window ramps at the first frame: age/128 and (end - age)/128. */
    s.in_a = (float)g->age_a * CRUMPBIT_GRAIN_FADE_K;
    s.out_a = ((float)end_a - (float)g->age_a) * CRUMPBIT_GRAIN_FADE_K;
    s.in_b = (float)g->age_b * CRUMPBIT_GRAIN_FADE_K;
    s.out_b = ((float)end_b - (float)g->age_b) * CRUMPBIT_GRAIN_FADE_K;
    crumpBit_grainGains(g->pan_a, &s.gla, &s.gra);
    crumpBit_grainGains(g->pan_b, &s.glb, &s.grb);
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

    /* Span 1: up to the event frame (all frames when none). */
    crumpBit_grainSpan(&s, &k, 0u, ev);
    if (ev < frames) {
        if (ev_trigger) {
            /* The voice starts the candidate; its fade-in starts at 0. */
            const uint32_t pos = cand.start << 16;
            const uint32_t step = (uint32_t)((int32_t)cand.inc * 16);
            const uint32_t wrap = (cand.inc < 0) ? 0u - k.lim : k.lim;
            const float out = (float)cand.end * CRUMPBIT_GRAIN_FADE_K;

            if (voice) {
                s.pos_b = pos;
                s.step_b = step;
                s.wrap_b = wrap;
                s.in_b = 0.0f;
                s.out_b = out;
                crumpBit_grainGains(cand.pan, &s.glb, &s.grb);
                g->inc_b = cand.inc;
                g->pan_b = cand.pan;
                end_b = cand.end;
                age_b = 0u;
            } else {
                s.pos_a = pos;
                s.step_a = step;
                s.wrap_a = wrap;
                s.in_a = 0.0f;
                s.out_a = out;
                crumpBit_grainGains(cand.pan, &s.gla, &s.gra);
                g->inc_a = cand.inc;
                g->pan_a = cand.pan;
                end_a = cand.end;
                age_a = 0u;
            }
        } else {
            /*
             * The steal: the older voice's fade-out starts now (out = 1),
             * it ends one fade later, and the trigger waits until then.
             */
            if (voice) {
                s.out_b = 1.0f;
                end_b = age_b + CRUMPBIT_GRAIN_FADE;
            } else {
                s.out_a = 1.0f;
                end_a = age_a + CRUMPBIT_GRAIN_FADE;
            }
            pend_next = 1u + ev + CRUMPBIT_GRAIN_FADE - frames;
        }
    }
    /* Span 2: from the event to the block end (empty when none). */
    crumpBit_grainSpan(&s, &k, (ev < frames) ? ev : frames, frames);

    /* The countdown and the deferred trigger move on by the block. */
    if (retry && t_pend < frames)
        pend_next = 1u;   /* the deferred trigger fires on the next block */
    if (dv != 0) {
        if (deferred || (retry && t_pend >= frames))
            g->countdown = 0u;
        else if (t_reg < frames)
            g->countdown = t_reg + interval - frames;
        else
            g->countdown -= frames;
    }
    g->pending = (uint8_t)pend_next;
    /* Ages from the event frame (or the block start) to the block end. */
    {
        const uint32_t rest = frames - ((ev < frames) ? ev : 0u);

        age_a += rest;
        age_b += rest;
    }
    g->age_a = (uint16_t)((age_a > end_a) ? end_a : age_a);
    g->age_b = (uint16_t)((age_b > end_b) ? end_b : age_b);
    g->end_a = (uint16_t)end_a;
    g->end_b = (uint16_t)end_b;
    g->write_pos = (uint16_t)s.wp;
    if (!frozen)
        g->valid = (uint16_t)((g->valid + frames < len) ? g->valid + frames
                                                        : len);
```

#### E8 — Seat comment: `grn` length change

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 2131–2131: **replace** (1 lines → 4; new lines 2371–2374).

- **What:** documents E9: a `grn` length change also stops both voices and
  drops a deferred trigger and the anchor.

Remove (HEAD 2131–2131):

```c
 * re-seats otherwise (`grn` has no glide state: its tap follows R).
```

Replace with:

```c
 * re-seats otherwise. S091 P4: a `grn` length change also stops both
 * voices (end 0, positions at the write head) and drops a deferred trigger
 * and the freeze anchor, so no read or anchor can lie beyond the new line;
 * an anchor is set again on the next frozen block.
```

#### E9 — `crumpBit_seatActive()`: reset voices on a `grn` length change

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 2151–2152: **replace** (2 lines → 10; new lines 2394–2403).

- **What:** when the line length changes:
  - both positions go to the write head;
  - ends and ages to 0 (idle);
  - `pending` to 0;
  - the anchor to none.

  The pass-through threshold becomes `CRUMPBIT_GRAIN_LINE_MIN`.
- **Why:** a voice position, a deferred trigger or an anchor could
  otherwise point beyond a shorter line. A frozen block sets a new anchor
  on its next block.
- **Proof:** CB11 (positions, ages, ends, anchor and `pending` within
  bounds after every block).

Remove (HEAD 2151–2152):

```c
        }
        return (len >= 4u * CRUMPBIT_GRAIN_MIN) ? len : 0u;
```

Replace with:

```c
            g->pos_a = (uint32_t)g->write_pos << 16;
            g->pos_b = g->pos_a;
            g->end_a = 0u;
            g->end_b = 0u;
            g->age_a = 0u;
            g->age_b = 0u;
            g->pending = 0u;
            g->anchor = CRUMPBIT_GRAIN_NO_ANCHOR;
        }
        return (len >= CRUMPBIT_GRAIN_LINE_MIN) ? len : 0u;
```

#### E10 — `crumpBit_enter()`: the `grn` entry state

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 2214–2219: **replace** (6 lines → 10; new lines 2465–2474).

- **What:** after the union is zeroed:
  - both voices idle at unity pitch (`inc` 4096) and centre pan (127);
  - the anchor set to none (a zeroed 0 would be a valid anchor);
  - countdown and `pending` 0.

  The level bytes are gone.
- **Why:** the first Density trigger fires on the first block, and retries
  until the record line holds audio (§A7.3), so entering `grn` plays at
  once.

Remove (HEAD 2214–2219):

```c
        rt->u.grn.inc_a = 4096;
        rt->u.grn.inc_b = 4096;
        rt->u.grn.amp_a = 255u;
        rt->u.grn.amp_b = 255u;
        rt->u.grn.pan_a = 64u;
        rt->u.grn.pan_b = 191u;
```

Replace with:

```c
        /*
         * S091 P4: both voices idle (end 0) at unity pitch and centre pan;
         * no anchor (the zeroed 0 would be a valid one), no countdown, so
         * the first Density trigger fires on the first block.
         */
        rt->u.grn.inc_a = 4096;
        rt->u.grn.inc_b = 4096;
        rt->u.grn.pan_a = 127u;
        rt->u.grn.pan_b = 127u;
        rt->u.grn.anchor = CRUMPBIT_GRAIN_NO_ANCHOR;
```

#### E11 — `crumpBit_filterCoeffs()`: one channel's TPT coefficients

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, **add** after HEAD line 2237. New lines 2493–2510 (18 lines).

- **What:** a1 = 1/(1 + g(g + 2R)), a2 = g·a1, a3 = g·a2, the exact S091 P2
  expressions as a helper, placed before `crumpBit_process()`'s comment.
  The insertion is aligned so that it starts with the `}` that closes the
  function above, and HEAD line 2238's `}` then closes the new helper.
- **Why:** `bal` needs the coefficients twice per block (left and right
  cutoff indices). The same expression gives the P2 coefficients bit for
  bit at `bal` 0 (CB4 and CB16).
- **Inputs:** g from `crumpBit_filterG[]`, R from `crumpBit_filterDamping()`.
- **Outputs:** `*a1`, `*a2`, `*a3`.
- **Caller:** `crumpBit_process()` (E13).

Add:

```c
}

/*
 * TPT SVF coefficients of one channel (S091 P2 law; S091 P4: per channel).
 *
 * What: a1 = 1/(1 + g(g + 2R)), a2 = g a1, a3 = g a2 for the pre-warped
 * cutoff g and the damping R, the exact S091 P2 expressions, so a channel
 * whose cutoff index is the shared `frq` gets the P2 coefficients bit for
 * bit. One divide per call (twice per block).
 * Inputs: g (crumpBit_filterG[]), R (crumpBit_filterDamping()). Outputs:
 * *a1, *a2, *a3. Caller: crumpBit_process() (left, then right, for `bal`).
 */
static inline void crumpBit_filterCoeffs(float g, float r, float *a1,
                                         float *a2, float *a3)
{
    *a1 = 1.0f / (1.0f + g * (g + 2.0f * r));
    *a2 = g * *a1;
    *a3 = g * *a2;
```

#### E12 — Stage 4: per-channel coefficient locals

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 2265–2267: **replace** (3 lines → 7; new lines 2538–2544).

- **What:** `a1`, `a2`, `a3` become `a1l`..`a3l` and `a1r`..`a3r`.

Remove (HEAD 2265–2267):

```c
    float a1;
    float a2;
    float a3;
```

Replace with:

```c
    /* a1..a3 per channel (S091 P4 `bal`: each side has its own cutoff). */
    float a1l;
    float a2l;
    float a3l;
    float a1r;
    float a2r;
    float a3r;
```

#### E13 — Stage 4 setup: the balance law

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 2315–2324: **replace** (10 lines → 18; new lines 2592–2609).

- **What:**
  - `tilt` = `filter_bal` − 63 (−63..64);
  - the left cutoff index `fl` = clamp(frq − tilt, 0, 127);
  - the right `fr` = clamp(frq + tilt, 0, 127);
  - each channel's coefficients from `crumpBit_filterG[]` through
    `crumpBit_filterCoeffs()`.

  R, the type mix and its one-block crossfade stay shared.
- **Why:** plan §A6.2. +1 `frq` step on the right and −1 on the left per
  value.
- **Cost:** one more table read and divide per block; the stage-4 loop
  stays 31.

Remove (HEAD 2315–2324):

```c
     */
    {
        const uint8_t f = (rt->filter_freq > 127u) ? 127u : rt->filter_freq;
        const float g = crumpBit_filterG[f];
        const float r = crumpBit_filterDamping(rt->filter_reso);
        float r_from;

        a1 = 1.0f / (1.0f + g * (g + 2.0f * r));
        a2 = g * a1;
        a3 = g * a2;
```

Replace with:

```c
     * S091 P4 `bal` (row 22, 63 = 0): the right channel's cutoff index is
     * frq + (bal - 63), the left's frq - (bal - 63), each clamped to 0..127
     * (S091_P4_GRN_TWEAKS.md A6.2). At bal 63 both indices are frq and the
     * two coefficient sets are the S091 P2 set bit for bit.
     */
    {
        /* tilt: the balance in cutoff steps, -63..64. */
        const int32_t tilt = (int32_t)((rt->filter_bal > 127u) ? 127u
                                                                : rt->filter_bal) - 63;
        const int32_t f = (int32_t)((rt->filter_freq > 127u) ? 127u
                                                              : rt->filter_freq);
        const int32_t fl = (f - tilt < 0) ? 0 : ((f - tilt > 127) ? 127 : f - tilt);
        const int32_t fr = (f + tilt < 0) ? 0 : ((f + tilt > 127) ? 127 : f + tilt);
        const float r = crumpBit_filterDamping(rt->filter_reso);
        float r_from;

        crumpBit_filterCoeffs(crumpBit_filterG[fl], r, &a1l, &a2l, &a3l);
        crumpBit_filterCoeffs(crumpBit_filterG[fr], r, &a1r, &a2r, &a3r);
```

#### E14 — Stage 4 loop: each channel its coefficients

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 2374–2382: **replace** (9 lines → 9; new lines 2659–2667).

- **What:** the left TPT tick uses `a1l`..`a3l`, the right `a1r`..`a3r`.
  Everything else is unchanged.
- **Proof:** CB16. Each channel is bit-identical to a balance-0 run at its
  own cutoff, for every type; break 11 swaps the sides.

Remove (HEAD 2374–2382):

```c
        v1 = a1 * s_l1 + a2 * v3;
        v2 = s_l2 + a2 * s_l1 + a3 * v3;
        s_l1 = 2.0f * v1 - s_l1;
        s_l2 = 2.0f * v2 - s_l2;
        yl = cx * xl + cb * v1 + cl * v2;

        v3 = xr - s_r2;
        v1 = a1 * s_r1 + a2 * v3;
        v2 = s_r2 + a2 * s_r1 + a3 * v3;
```

Replace with:

```c
        v1 = a1l * s_l1 + a2l * v3;
        v2 = s_l2 + a2l * s_l1 + a3l * v3;
        s_l1 = 2.0f * v1 - s_l1;
        s_l2 = 2.0f * v2 - s_l2;
        yl = cx * xl + cb * v1 + cl * v2;

        v3 = xr - s_r2;
        v1 = a1r * s_r1 + a2r * v3;
        v2 = s_r2 + a2r * s_r1 + a3r * v3;
```

#### E15 — `effective_max` comment: Position

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 2423–2426: **replace** (4 lines → 7; new lines 2708–2714).

- **What:** documents E16.
  - `dpn` in `grn` (Position) is clamped to the stops that fit.
  - `dpn` is not limited in any other sub-type, or without a runtime.

Remove (HEAD 2423–2426):

```c
 * curve, the comb table, the pitch window, a `grn` half line, the `rvb`
 * word buffer); any other row is not limited (255). The runtime may be NULL
 * (a page edit of a Scene that is not live): the delay rule applies.
 * Stored values are never rewritten (A30).
```

Replace with:

```c
 * curve, the comb table, the pitch window, the `grn` delay-time curve in a
 * half line, the `rvb` word buffer). S091 P4: for `dpn` in `grn` (Position),
 * the last rhythmic lag that fits a half line (crumpBit_grainPosLimit(); the
 * follow and scatter values below 64 are never limited); in every other
 * sub-type `dpn` is not limited. Any other row: 255. The runtime may be NULL
 * (a page edit of a Scene that is not live): the delay rule applies, and
 * `dpn` is not limited. Stored values are never rewritten (A30).
```

#### E16 — `crumpBit_effectiveMax()`: the Position clamp

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, **add** after HEAD line 2435. New lines 2724–2726 (3 lines).

- **What:** for `CRUMPBIT_PARAM_DLY_PAN`, `crumpBit_grainPosLimit(share)`
  when the runtime's `sub` is `grn`, else 255.
- **Why:** Q7. The manager applies it every block, because `dpn` is now
  BUFFER_DEPENDENT (C4), and page edits apply it through
  `effects_paramEffectiveMax()`.
- **Proof:** CB6 (63 + 4·fit in `grn`, 255 otherwise).

Add:

```c
    if (index == CRUMPBIT_PARAM_DLY_PAN)
        return (rt && rt->subtype == CRUMPBIT_SUBTYPE_GRAIN)
            ? crumpBit_grainPosLimit(share) : 255u;
```

#### E17 — Lane-map comment: the `grn` pool (Q11)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 2451–2457: **replace** (7 lines → 10; new lines 2742–2751).

- **What:** the comment's table shows the `grn` pool:
  - 4 `den`, 5 `rte` (delay time), 6 `snc` (freeze), 9 `dpn` (Position),
    10 `lng`, 14 `aux` (P1), 15 `spr`;
  - P2, P3, Width and Feedback have no lane in `grn`;
  - `bal` has no lane anywhere.

Remove (HEAD 2451–2457):

```c
 *       4    fbk   fbk   fbk   st2   fbk
 *       5    rte   rte   rte   st3   rte
 *       6    snc   snc   snc   snc   snc     (grn/rvb: freeze)
 *       9    dpn   dpn   dpn   dpn   dpn     (grn: pos; rvb: width)
 *      10    ton   ton   ton   ton   ton     (grn: sze)
 *      14    -     -     aux   aux   -       (pit: interval; grn: st1)
 *      15    -     -     -     spr   -
```

Replace with:

```c
 *       4    fbk   fbk   fbk   den   fbk     (grn: Density, S091 P4)
 *       5    rte   rte   rte   rte   rte     (grn: delay time `tim`)
 *       6    snc   snc   snc   snc   snc     (grn/rvb: freeze)
 *       9    dpn   dpn   dpn   dpn   dpn     (grn: Position; rvb: width)
 *      10    ton   ton   ton   lng   ton     (grn: Length, row 20)
 *      14    -     -     aux   aux   -       (pit: interval; grn: P1)
 *      15    -     -     -     spr   -
 *   S091 P4 (plan Q11, accepted): the `grn` pool carries its performance
 *   controls; P2, P3, Width and Feedback are lane-less in `grn`, and the
 *   shared `bal` (row 22) has no lane in any sub-type.
```

#### E18 — `crumpBit_laneMaps[GRAIN]`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, HEAD lines 2492–2494: **replace** (3 lines → 3; new lines 2786–2788).

- **What:**
  - lane 4 `GRAIN_DENSITY` (was `st2`);
  - lane 5 `RATE` (was `st3`);
  - lane 10 `GRAIN_LENGTH` (was `ton`).

  The other lanes are unchanged.
- **Why:** Q11. The performance controls are sequenceable.
- **Proof:** CB9, plus the self-check: no row twice, lane 0 Morph, every
  row valid.

Remove (HEAD 2492–2494):

```c
        CRUMPBIT_PARAM_GRAIN_ST2, CRUMPBIT_PARAM_GRAIN_ST3,
        CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN,
        CRUMPBIT_PARAM_DLY_PAN, CRUMPBIT_PARAM_TONE,
```

Replace with:

```c
        CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_RATE,
        CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_LEVEL, CRUMPBIT_PARAM_PAN,
        CRUMPBIT_PARAM_DLY_PAN, CRUMPBIT_PARAM_GRAIN_LENGTH,
```

### 3.3 `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`

#### P1 — Row count 23

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 36–44: **replace** (9 lines → 10; new lines 36–45).

- **What:** `CRUMPBIT_PARAM_COUNT` 22 → 23, and the comment:
  - row 20 is the grain Length (the retired `amp` index);
  - row 22 the shared balance;
  - the deferred `drv` would be 23.
- **Why:** plan §A7.1. Appending keeps every saved index; only `amp`'s
  index changes meaning (no backward compatibility, user rule).

Remove (HEAD 36–44):

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

Replace with:

```c
 * Descriptor rows: common rows 0..2 plus type rows 3..22.
 * S091 P2 appended rows 11..14 (`ton`, `typ`, `frq`, `res`). S091 P3
 * appended row 15 (`aux`, the per-sub-type pitch/drive row) and the granular
 * rows 16..21. S091 P4 (S091_P4_GRN_TWEAKS.md): row 20 is the grain Length
 * (`crump_grain_length`; the retired level jitter `amp` held this index),
 * and row 22 is the shared output-filter balance `bal`. The deferred
 * output-filter drive (`drv`) would append as 23; the tone drive (`tdr`) is
 * the `aux` row's meaning in `dly`/`cmb` when the drives land.
 */
#define CRUMPBIT_PARAM_COUNT 23u
```

#### P2 — Enum: `GRAIN_LENGTH` (20), `FILTER_BAL` (22)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 74–81: **replace** (8 lines → 11; new lines 75–85).

- **What:** `CRUMPBIT_PARAM_GRAIN_AMP` → `CRUMPBIT_PARAM_GRAIN_LENGTH`
  (same index 20); `CRUMPBIT_PARAM_FILTER_BAL` appended (22). The comment
  states the P4 meanings of rows 16–21.
- **Accessors:** every user of the names (E6, E7, E18, C5, C10, host laws).

Remove (HEAD 74–81):

```c
    /* S091 P3, `grn` only: second and third pitch, spread, density, level
     * jitter and random pan. */
    CRUMPBIT_PARAM_GRAIN_ST2,
    CRUMPBIT_PARAM_GRAIN_ST3,
    CRUMPBIT_PARAM_GRAIN_SPREAD,
    CRUMPBIT_PARAM_GRAIN_DENSITY,
    CRUMPBIT_PARAM_GRAIN_AMP,
    CRUMPBIT_PARAM_GRAIN_WIDTH,
```

Replace with:

```c
    /* S091 P3, `grn` only (S091 P4 meanings): pitches P2 and P3 (absolute),
     * spread, density (bipolar: trigger rate), Length (bipolar: size and
     * direction; index 20 was the retired level jitter) and width. */
    CRUMPBIT_PARAM_GRAIN_ST2,
    CRUMPBIT_PARAM_GRAIN_ST3,
    CRUMPBIT_PARAM_GRAIN_SPREAD,
    CRUMPBIT_PARAM_GRAIN_DENSITY,
    CRUMPBIT_PARAM_GRAIN_LENGTH,
    CRUMPBIT_PARAM_GRAIN_WIDTH,
    /* S091 P4, shared output stage: the filter's stereo balance. */
    CRUMPBIT_PARAM_FILTER_BAL,
```

#### P3 — Mask comment: `bal` shared, `lng` in `grn`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 110–117: **replace** (8 lines → 10; new lines 114–123).

- **What:** documents `bal` in `CRUMPBIT_ROWS_SHARED` (the output filter)
  and `len` in `CRUMPBIT_ROWS_GRN`.

Remove (HEAD 110–117):

```c
 *   sub-type row) and the output filter (`typ`, `frq`, `res`), which is the
 *   last element of the Effect whatever the sub-type is.
 * - CRUMPBIT_ROWS_SUBTYPE: rows whose meaning is set by the active sub-type
 *   (`fbk`, `rte`, `snc`, `dpn`, `ton`, `aux`), aligned by role across
 *   sub-types (plan §6, A1): `fbk` amount back, `rte` length (higher =
 *   longer), `snc` the switch, `dpn` where the wet sits, `ton` colour/shape,
 *   `aux` pitch. A sub-type change keeps their values (plan §2.4).
 * - CRUMPBIT_ROWS_GRN: rows only `grn` reads (`st2 st3 spr den amp wid`).
```

Replace with:

```c
 *   sub-type row) and the output filter (`typ`, `frq`, `res`, and since
 *   S091 P4 its balance `bal`), which is the last element of the Effect
 *   whatever the sub-type is.
 * - CRUMPBIT_ROWS_SUBTYPE: rows whose meaning is set by the active sub-type
 *   (`fbk`, `rte`, `snc`, `dpn`, `ton`, `aux`), aligned by role across
 *   sub-types (plan §6, A1): `fbk` amount back, `rte` length (higher =
 *   longer), `snc` the switch, `dpn` where the wet sits, `ton` colour/shape,
 *   `aux` pitch. A sub-type change keeps their values (plan §2.4).
 * - CRUMPBIT_ROWS_GRN: rows only `grn` reads (`st2 st3 spr den len wid`;
 *   S091 P4: `len` replaced the retired `amp`).
```

#### P4 — `CRUMPBIT_ROWS_SHARED` += `FILTER_BAL`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 133–133: **replace** (1 lines → 2; new lines 139–140).

- **What:** the balance row belongs to the shared output stage.
- **Why:** the compile-time guards (every row exactly one owner) cover row
  22.
- **Proof:** CB9.

Remove (HEAD 133–133):

```c
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_RESO))
```

Replace with:

```c
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_RESO) |           \
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_BAL))
```

#### P5 — `CRUMPBIT_ROWS_GRN`: `GRAIN_LENGTH` for `GRAIN_AMP`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 146–146: **replace** (1 lines → 1; new lines 153–153).

- **What:** row 20 stays a `grn` row under its new name.

Remove (HEAD 146–146):

```c
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_AMP) |             \
```

Replace with:

```c
     CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_GRAIN_LENGTH) |          \
```

#### P6 — Layout comment: the P4 page order

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, HEAD lines 189–189: **replace** (1 lines → 3; new lines 196–198).

- **What:** records the overlay `mrp frq res bal`, then `typ frq res bal`,
  then `mix sub` and the sub-effect's controls.

Remove (HEAD 189–189):

```c
 * `grn` variants, chosen by crumpBit_ui.layout. crumpBit_ui owns the SELECT
```

Replace with:

```c
 * `grn` variants, chosen by crumpBit_ui.layout. S091 P4 (user): the overlay
 * is `mrp frq res bal`, then `typ frq res bal`, then `mix sub` and the
 * sub-effect's controls (CrumpBitParameters.c). crumpBit_ui owns the SELECT
```

### 3.4 `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`

#### C1 — `#include "CrumpBitTables.h"`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, **add** after HEAD line 23. New lines 24–24 (1 lines).

- **What:** `crumpBit_grainStopCc` and `CRUMPBIT_GRAIN_STOPS` for the `grn`
  labels (C6, C9).

Add:

```c
#include "CrumpBitTables.h" /* crumpBit_grainStopCc: `grn` labels (S091 P4) */
```

#### C2 — Descriptor comment: rows 3..22; `dpn` BUFFER_DEPENDENT

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 33–41: **replace** (9 lines → 12; new lines 34–45).

- **What:** rows 3..22. Delay pan (10) is BUFFER_DEPENDENT because in `grn`
  it is Position, clamped to the stops that fit the line; it is never
  limited elsewhere.

Remove (HEAD 33–41):

```c
 * What: rows 0..2 are the shared common rows; rows 3..21 are CrumpBit's.
 * - bit off / bit invert (3, 4): 8-bit masks, one bit per ADC data line
 *   (bit 0 = LSB). Not Morphable (interpolating a mask gives unrelated
 *   bits), not modulatable, not Pattern-automatable (a Pattern value is
 *   7-bit and cannot reach bit 7). Sequenceable through FX lanes 1 and 2,
 *   which store full 8-bit values. Edited by the SELECT buttons (Stage 2);
 *   in Stage 1 they show as plain 0..255 numbers.
 * - mix, feedback, rate, delay pan (5, 6, 7, 10): Morphable, LFO and
 *   Pattern automation, like voice rows.
```

Replace with:

```c
 * What: rows 0..2 are the shared common rows; rows 3..22 are CrumpBit's.
 * - bit off / bit invert (3, 4): 8-bit masks, one bit per ADC data line
 *   (bit 0 = LSB). Not Morphable (interpolating a mask gives unrelated
 *   bits), not modulatable, not Pattern-automatable (a Pattern value is
 *   7-bit and cannot reach bit 7). Sequenceable through FX lanes 1 and 2,
 *   which store full 8-bit values. Edited by the SELECT buttons (Stage 2);
 *   in Stage 1 they show as plain 0..255 numbers.
 * - mix, feedback, rate, delay pan (5, 6, 7, 10): Morphable, LFO and
 *   Pattern automation, like voice rows. S091 P4: delay pan (10) is also
 *   BUFFER_DEPENDENT, because in `grn` it is Position, whose rhythmic lags
 *   are clamped to the line (crumpBit_ops.effective_max); in the other
 *   sub-types it is never limited.
```

#### C3 — Descriptor comment: the `grn` rows and `bal`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 65–74: **replace** (10 lines → 18; new lines 69–86).

- **What:** documents the P4 meanings and defaults:
  - `p2`/`p3` absolute pitches;
  - bipolar Density (63 off, 64 +1, rhythmic above, random below; default
    64);
  - bipolar Length (row 20; default 127);
  - Width (mono at 0);
  - the lanes (den 4, lng 10, spr 15).

  Also `bal` (row 22, shared): right cutoff frq + (bal − 63), left
  frq − (bal − 63). The row count is 23.

Remove (HEAD 65–74):

```c
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

Replace with:

```c
 * - grain rows (16..21, `grn` only; S091 P4 meanings, S091_P4_GRN_TWEAKS.md
 *   Part A): `p2`, `p3` (rows 16, 17, keys `crump_grain_st2/st3`: absolute
 *   pitches, bipolar semitones, Q4), `spr` spread/detune, `den` Density
 *   (bipolar: 63 off, 64 +1 = one trigger per line, rhythmic above, random
 *   below; default 64), `lng` Length (row 20, bipolar: size, reversed left
 *   of 63; default 127 = the whole line forward; the retired level jitter
 *   `amp` held this index), `wid` Width (mono at 0). Defaults give the copy
 *   setting (A1.9). Morph, LFO and Pattern; lanes from the `grn` lane map
 *   (den 4, lng 10, spr 15).
 * - filter balance (22, S091 P4, shared output stage): `bal`, bipolar
 *   (63 = 0): the right channel's cutoff index is frq + (bal - 63), the
 *   left's frq - (bal - 63). Morph, LFO and Pattern, like `frq`; no lane.
 * - Defaults (S075 F2-E, user): mix 0, feedback 64, rate 64, delay pan 63
 *   (the absolute centre, shows 0). The sync row's 3-letter label is `snc`;
 *   its file key remains `crump_sync`.
 * Why: the registry makes storage, AutoSave, the Effect page, lanes,
 * automation and LFO work without type-specific code elsewhere. Inputs:
 * none (const). Output: 23 rows. Accessors: EffectsManager (resolution,
```

#### C4 — `dpn` row: BUFFER_DEPENDENT

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 104–105: **replace** (2 lines → 4; new lines 116–119).

- **What:** `EFFECT_ROW` → `EFFECT_ROW_FLAGS(…,
  EFFECT_PARAM_FLAG_BUFFER_DEPENDENT)`. Key, label, range and default are
  unchanged.
- **Why:**
  - The manager re-applies BUFFER_DEPENDENT rows every block through
    `effective_max` (S091 P3 M16), so `grn`'s Position follows a tempo or
    share change within a block.
  - In the other sub-types `effective_max` returns 255.

Remove (HEAD 104–105):

```c
    EFFECT_ROW("crump_dly_pan", "Delay", "DlyPan", "dpn", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
```

Replace with:

```c
    /* S091 P4: BUFFER_DEPENDENT for the `grn` Position clamp. */
    EFFECT_ROW_FLAGS("crump_dly_pan", "Delay", "DlyPan", "dpn", DTYPE_PM63,
                     EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u,
                     EFFECT_PARAM_FLAG_BUFFER_DEPENDENT),
```

#### C5 — Rows 16–22

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 121–133: **replace** (13 lines → 19; new lines 135–153).

- **What:**
  - **`st2`/`st3`:** short names `p2`/`p3` (keys unchanged).
  - **`den`:** `DTYPE_PM63`, default 64.
  - **`crump_grain_length`:** "Grain", "Length", `lng`, `DTYPE_PM63`,
    default 127, replaces `crump_grain_amp`.
  - **`crump_grain_width`:** unchanged.
  - **`crump_filter_bal`** (new): "Filter", "Balance", `bal`, `DTYPE_PM63`,
    default 63, image flags and 0..127 modulation, like `frq`.
- **Why:** plan §A5 Q5 (no `amp`), §A6.2, §A7.1. The defaults give the copy
  setting.
- **Accessors:** the registry (storage keys, AutoSave, page, automation,
  LFO).

Remove (HEAD 121–133):

```c
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

Replace with:

```c
    /*
     * grn only (S091 P4): absolute pitches P2/P3, spread, bipolar Density,
     * bipolar Length (row 20, was the level jitter), width.
     */
    EFFECT_ROW("crump_grain_st2", "Grain", "Pitch2", "p2", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("crump_grain_st3", "Grain", "Pitch3", "p3", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
    EFFECT_ROW("crump_grain_spread", "Grain", "Spread", "spr", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    EFFECT_ROW("crump_grain_density", "Grain", "Density", "den", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 64u, 127u),
    EFFECT_ROW("crump_grain_length", "Grain", "Length", "lng", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 127u, 127u),
    EFFECT_ROW("crump_grain_width", "Grain", "Width", "wid", DTYPE_0B127,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 0u, 127u),
    /* S091 P4, output stage: stereo balance of the filter cutoff. */
    EFFECT_ROW("crump_filter_bal", "Filter", "Balance", "bal", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
```

#### C6 — `grn` page names; stop names; the delay-time label

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 301–303: **replace** (3 lines → 32; new lines 321–352).

- **What:**
  - **`crumpBit_subtypeRowNames[GRAIN]`:** `fbk`, `tim` (rte), `frz`
    (snc), `pos` (dpn), nothing for `ton` (not on the `grn` page), `p1`
    (aux).
  - **`crumpBit_grainStopNames[16]`:** StepScale's 14 short names plus
    `d/2` and `1br`, indexed like `crumpBit_grainStopCc`.
  - **`crumpBit_formatGrainTime(cc)`:** the stop's name when `cc` is a
    stop, else the position number, as StepScale-scaled controls show it
    (Q8: the divisions marked).
- **Accessors:** `crumpBit_uiName3()`, `crumpBit_uiFormatValue3()` (C8,
  C9).

Remove (HEAD 301–303):

```c
    [CRUMPBIT_SUBTYPE_GRAIN]  = { "",    "div", "frz", "pos", "sze", "st1" },
    [CRUMPBIT_SUBTYPE_REVERB] = { "dcy", "siz", "frz", "wid", "dmp", ""    },
};
```

Replace with:

```c
    /* S091 P4: delay time, freeze, Position, P1 (`ton` is not on its page). */
    [CRUMPBIT_SUBTYPE_GRAIN]  = { "",    "tim", "frz", "pos", "",    "p1"  },
    [CRUMPBIT_SUBTYPE_REVERB] = { "dcy", "siz", "frz", "wid", "dmp", ""    },
};

/*
 * `grn` rhythmic stop names (S091 P4, plan A1.6/A1.8): StepScale's 14 stops
 * (stepScale_shortName()) plus d/2 and one bar, indexed by stop k as
 * crumpBit_grainStopCc. Accessor: crumpBit_formatGrainStop().
 */
static const char crumpBit_grainStopNames[CRUMPBIT_GRAIN_STOPS][4] = {
    "/64", "32t", "/32", "16t", "d32", "/16", "8Tr", "d16",
    "/8 ", "4Tr", "d/8", "/4 ", "d/4", "/2 ", "d/2", "1br"
};

/*
 * The `grn` delay-time label of an `rte` position: the stop's name when the
 * position is a stop (the divisions are marked, Q8), else the position
 * number, as StepScale's controls show them.
 */
static void crumpBit_formatGrainTime(uint8_t cc, char *out)
{
    uint32_t k;

    for (k = 0u; k < CRUMPBIT_GRAIN_STOPS; k++) {
        if (crumpBit_grainStopCc[k] == cc) {
            memcpy(out, crumpBit_grainStopNames[k], 3u);
            return;
        }
    }
    numtostrpu(out, cc, ' ');
}
```

#### C7 — Value-hook comment: `grn` labels

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 348–348: **replace** (1 lines → 4; new lines 397–400).

- **What:** documents the P4 value text:
  - the delay time (stop name or number);
  - Position's stop;
  - Density `off` / `buf` / stop;
  - `p2`/`p3` semitones.

Remove (HEAD 348–348):

```c
 * - `aux` in `pit`/`grn`, and `st2`/`st3` in `grn`: signed semitones;
```

Replace with:

```c
 * - `aux` in `pit`/`grn`, and `p2`/`p3` in `grn`: signed semitones;
 * - S091 P4 `grn`: `rte` is the delay-time curve (a stop's name, else its
 *   position); Position's rhythmic half the stop it plays (clamped);
 *   Density `off`, `buf` (+1) or its stop;
```

#### C8 — `grn` `rte`: the delay-time label

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 389–392: **replace** (4 lines → 2; new lines 441–442).

- **What:** the clamped `rte` value goes through
  `crumpBit_formatGrainTime()`, replacing the S091 P3 synced division.

Remove (HEAD 389–392):

```c
            stepScale_formatShort(
                crumpBit_syncDivision(shown, seq_getBpm(),
                                      crumpBit_grainMaxSamples(&share)),
                out);
```

Replace with:

```c
            /* S091 P4: the delay-time curve, its divisions marked. */
            crumpBit_formatGrainTime(shown, out);
```

#### C9 — `grn` Position and Density labels

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, **add** after HEAD line 422. New lines 473–502 (30 lines).

- **What:**
  - **Position in `grn`, above 63:** the stop it plays, after the live
    clamp (`crumpBit_grainPosLimit()`, the value the DSP sees). With no
    stop fitting it shows `0`.
  - **Density:** 63 `off`, 64 `buf` (one trigger per line), above that the
    stop name.
  - **Everything else** (negative halves, Length, Width, Spread, `bal`):
    generic text, left to the presentation task.
- **Runs in:** the foreground. The share read is a struct copy.

Add:

```c
        return 1u;
    }
    /*
     * S091 P4, `grn` Position and Density: the rhythmic half names its stop
     * (Position after the live clamp, as the DSP plays it); Density 63 is
     * `off` and 64 (+1) `buf` (one trigger per line); the rest generic.
     */
    if (sub == CRUMPBIT_SUBTYPE_GRAIN && index == CRUMPBIT_PARAM_DLY_PAN &&
        value > 63u) {
        fx_share_t share;
        uint8_t limit;

        fxbuf_effectShare(&share);
        limit = crumpBit_grainPosLimit(&share);
        if (limit > 63u) {
            memcpy(out, crumpBit_grainStopNames[crumpBit_grainPosStop(
                            (value > limit) ? limit : value)], 3u);
            return 1u;
        }
        memcpy(out, "  0", 3u);
        return 1u;
    }
    if (index == CRUMPBIT_PARAM_GRAIN_DENSITY && value >= 63u) {
        if (value == 63u)
            memcpy(out, "off", 3u);
        else if (value == 64u)
            memcpy(out, "buf", 3u);
        else
            memcpy(out, crumpBit_grainStopNames[crumpBit_grainDenStop(value)],
                   3u);
```

#### C10 — The layouts (A6, Q12)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, HEAD lines 470–553: **replace** (84 lines → 79; new lines 550–628).

- **What:** replaces the three layouts and their comment.
  - **`CRUMPBIT_LAYOUT_FILTER_SCREENS`:** SELECT 2 s0 (the overlay, bit
    row, home) `mrp frq res bal` and s1 `typ frq res bal`, shared by every
    sub-type.
  - **`crumpBit_layout` (`dly`, `cmb`, `rvb`):** s2 `mix sub rte fbk`;
    s3 `snc dpn ton –`.
  - **`crumpBit_layoutPitch`:** s2 `mix sub aux rte`; s3 `fbk snc dpn ton`.
  - **`crumpBit_layoutGrain`:**
    - s2 `mix sub snc dpn`;
    - s3 `lng den aux p2`;
    - SELECT 3 s0 `p3 spr wid rte` and s1 `fbk`.

    This is the user's order: freeze, position, length, density, pitches,
    width, delay time, feedback.
- **Rule (user):** only sub-effect controls follow `sub`. The SELECT
  buttons stay bit toggles; the encoder reaches every screen.
- **Accessors:** `crumpBit_uiLayout()`, `menuEffects_layout()`.

Remove HEAD lines 470–553 (84 lines), from
`* CrumpBit Effect-page layout (S074_CRUMPBIT_EFFECT.md §5.1; S091 P2 §3).`
through
`CRUMPBIT_PARAM_GRAIN_WIDTH, EFFECT_LANE_NONE },`.

Replace with:

```c
 * CrumpBit Effect-page layouts (S074 §5.1; S091 P2 §3; S091 P4 §A6 of
 * S091_P4_GRN_TWEAKS.md, user 2026-11-04).
 *
 * What: SELECT 1 stays manager-owned (`typ out vol pan`, `mrp run len scl`).
 * CrumpBit's pages follow, in encoder order:
 *   SELECT 2 s0 home overlay (bit row on top, values only): mrp frq res bal
 *   SELECT 2 s1 the output filter with its type:             typ frq res bal
 *   SELECT 2 s2 the mix stage, the sub-type, then the sub-effect's first
 *              two controls:                                 mix sub  x   y
 *   SELECT 2 s3 (and SELECT 3 for `grn`): the rest of the sub-effect's
 *              controls.
 * Rule (user): only sub-effect controls follow `sub`. `frq` and `res` sit on
 * s0 and s1; both cells edit the same rows. Every SELECT button stays a bit
 * toggle (crumpBit_uiSelect()); the encoder walks cells across screens and
 * SELECT groups, so SELECT 3's screens follow SELECT 2's last one.
 * - crumpBit_layout (`dly`, `cmb`, `rvb`, same rows; names per sub-type
 *   through crumpBit_uiName3()): s2 mix sub rte fbk; s3 snc dpn ton -.
 * - crumpBit_layoutPitch: s2 mix sub aux(int) rte(win); s3 fbk snc(snp)
 *   dpn ton.
 * - crumpBit_layoutGrain (user order: freeze, position, length, density,
 *   pitches, width, delay time, feedback): s2 mix sub snc(frz) dpn(pos);
 *   s3 lng den aux(p1) p2; SELECT 3 s0 p3 spr wid rte(tim); s1 fbk.
 * Inputs: none (const). Output: layouts consumed by menuEffects through
 * crumpBit_uiLayout(). Affiliates: EFFECT_LAYOUT_CELL_MORPH,
 * crumpBit_uiPaintRow0(), menuEffects_layout().
 */
#define CRUMPBIT_LAYOUT_FILTER_SCREENS                              \
    { EFFECT_LAYOUT_CELL_MORPH, CRUMPBIT_PARAM_FILTER_FREQ,         \
      CRUMPBIT_PARAM_FILTER_RESO, CRUMPBIT_PARAM_FILTER_BAL },      \
    { CRUMPBIT_PARAM_FILTER_TYPE, CRUMPBIT_PARAM_FILTER_FREQ,       \
      CRUMPBIT_PARAM_FILTER_RESO, CRUMPBIT_PARAM_FILTER_BAL }

const effect_select_layout_t crumpBit_layout = {
    .screen_count = { 0u, 4u, 0u, 0u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = {
            CRUMPBIT_LAYOUT_FILTER_SCREENS,
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_SUBTYPE,
              CRUMPBIT_PARAM_RATE, CRUMPBIT_PARAM_FEEDBACK },
            { CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN,
              CRUMPBIT_PARAM_TONE, EFFECT_LANE_NONE },
        },
    },
    .custom_row0 = { 0u, 0x01u, 0u, 0u, 0u, 0u, 0u, 0u },
    .home_sub_page = 1u,
    .home_screen = 0u,
};

static const effect_select_layout_t crumpBit_layoutPitch = {
    .screen_count = { 0u, 4u, 0u, 0u, 0u, 0u, 0u, 0u },
    .cells = {
        [1] = {
            CRUMPBIT_LAYOUT_FILTER_SCREENS,
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_SUBTYPE,
              CRUMPBIT_PARAM_AUX, CRUMPBIT_PARAM_RATE },
            { CRUMPBIT_PARAM_FEEDBACK, CRUMPBIT_PARAM_SYNC,
              CRUMPBIT_PARAM_DLY_PAN, CRUMPBIT_PARAM_TONE },
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
            CRUMPBIT_LAYOUT_FILTER_SCREENS,
            { CRUMPBIT_PARAM_MIX, CRUMPBIT_PARAM_SUBTYPE,
              CRUMPBIT_PARAM_SYNC, CRUMPBIT_PARAM_DLY_PAN },
            { CRUMPBIT_PARAM_GRAIN_LENGTH, CRUMPBIT_PARAM_GRAIN_DENSITY,
              CRUMPBIT_PARAM_AUX, CRUMPBIT_PARAM_GRAIN_ST2 },
        },
        [2] = {
            { CRUMPBIT_PARAM_GRAIN_ST3, CRUMPBIT_PARAM_GRAIN_SPREAD,
              CRUMPBIT_PARAM_GRAIN_WIDTH, CRUMPBIT_PARAM_RATE },
            { CRUMPBIT_PARAM_FEEDBACK, EFFECT_LANE_NONE,
              EFFECT_LANE_NONE, EFFECT_LANE_NONE },
```

### 3.5 `Core/DSP/Effects/CrumpBit/CrumpBitTables.h` and the generated tables

#### B1 — `#include <stdint.h>`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitTables.h`, **add** after HEAD line 13. New lines 14–15 (2 lines).

- **What:** `uint8_t` for `crumpBit_grainStopCc`. The generated file
  includes only this header.

Add:

```c

#include <stdint.h>   /* uint8_t: crumpBit_grainStopCc (S091 P4) */
```

#### B2 — Table comment: Length law, grain time, stops, density

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitTables.h`, HEAD lines 36–40: **replace** (5 lines → 16; new lines 38–53).

- **What:** documents:
  - **`crumpBit_grainFraction`:** now the Length fraction of N,
    2^(−8(1 − m/64));
  - **`crumpBit_grainTime`:** 6..384 ticks, stops exact;
  - **`crumpBit_grainStopCc`:** the stop positions;
  - **`crumpBit_grainDensity`:** the mean-interval fractions.

Remove (HEAD 36–40):

```c
 * - crumpBit_grainFraction[d]: `grn` grain size / R, 1/32 .. 1 for
 *   d = |sze - 63| = 0..64.
 * - crumpBit_reverbSize[rte]: `rvb` delay scale 0.25 .. 2.5.
 * - crumpBit_reverbDecay[fbk]: `rvb` log2 of the per-sample line gain for an
 *   RT60 of 0.1 .. 10 s; a line of d samples has gain 2^(entry * d).
```

Replace with:

```c
 * - crumpBit_grainFraction[m]: S091 P4: `grn` grain Length / N (the record
 *   line), 2^(-8 (1 - m/64)) = 1/256 .. 1 for m = |len - 63| = 0..64; the
 *   DSP holds a grain at 220 samples (~5 ms) or more (plan Q1).
 * - crumpBit_reverbSize[rte]: `rvb` delay scale 0.25 .. 2.5.
 * - crumpBit_reverbDecay[fbk]: `rvb` log2 of the per-sample line gain for an
 *   RT60 of 0.1 .. 10 s; a line of d samples has gain 2^(entry * d).
 * S091 P4 `grn` tables:
 * - crumpBit_grainTime[rte]: the delay time in 96-PPQ ticks (samples = ticks
 *   x CRUMPBIT_SAMPLES_PER_TICK / BPM), 6 (1/64) .. 384 (one bar): a
 *   continuous log curve whose 16 rhythmic stops sit exactly on their tick
 *   counts (plan Q8, Q10).
 * - crumpBit_grainStopCc[k]: the `rte` position of stop k (k = 0 /64 ..
 *   15 1br); Position and Density pick stops by k, the page labels them.
 * - crumpBit_grainDensity[d]: negative Density d = 1..63 -> the mean
 *   trigger interval as a fraction of N (1 at d = 1, ~20 ms at d = 63 with
 *   the whole arena; plan Q6). Entry 0 is unused.
```

#### B3 — `CRUMPBIT_GRAIN_STOPS` 16, `CRUMPBIT_DENSITY_STEPS` 63

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitTables.h`, **add** after HEAD line 53. New lines 67–68 (2 lines).

- **What:** the two new table sizes.

Add:

```c
#define CRUMPBIT_GRAIN_STOPS     16u   /* grainStopCc: /64 .. 1br (P4) */
#define CRUMPBIT_DENSITY_STEPS   63u   /* grainDensity covers 1..63 (P4) */
```

#### B4 — Externs of the three new tables

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitTables.h`, **add** after HEAD line 63. New lines 79–81 (3 lines).

- **What:** `crumpBit_grainTime[128]`, `crumpBit_grainStopCc[16]`,
  `crumpBit_grainDensity[64]`.

Add:

```c
extern const float crumpBit_grainTime[CRUMPBIT_TABLE_SIZE];
extern const uint8_t crumpBit_grainStopCc[CRUMPBIT_GRAIN_STOPS];
extern const float crumpBit_grainDensity[CRUMPBIT_DENSITY_STEPS + 1u];
```

### R1 — `Core/DSP/Effects/CrumpBit/CrumpBitTables.c` (generated)

**Location:** the whole file, regenerated. Do not edit it by hand.

- **What:** after G1–G8 (and B1–B4, for `uint8_t`), run from the
  repository root:

  ```
  python3 tools/plots/gen_crumpbit_tables.py
  python3 tools/plots/gen_crumpbit_tables.py --check
  ```

  The first prints `wrote Core/DSP/Effects/CrumpBit/CrumpBitTables.c`; the
  second must print `crumpbit tables OK`.
- **The file's changes:**
  - `crumpBit_grainFraction` has the eight-octave Length law (1/256 .. 1);
  - three tables are appended: `crumpBit_grainTime[128]` (ticks, stops
    exact), `crumpBit_grainStopCc[16]` (0, 9, 21, 30, 34, 42, 51, 55, 64,
    72, 76, 85, 97, 106, 118, 127) and `crumpBit_grainDensity[64]`.
- **Size:** 353 lines, SHA-256 `193f047cd379ecb9f9b724152ca7d50a650d35b34c2e778f3b78a28f2d3f7f18`.

### 3.6 `Core/DSP/Effects/EffectsManager.c`

#### M1 — Union comment: 100 B

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 55–56: **replace** (2 lines → 3; new lines 55–57).

- **What:** records CrumpBit at 100 B in S091 P4 (a 44 B sub-engine union).

Remove (HEAD 55–56):

```c
     * S074: 56 B; S091 P2: 76 B; S091 P3: 88 B (a shared part plus a 32 B
     * union of five sub-engine states; user-approved growth up to +32 B).
```

Replace with:

```c
     * S074: 56 B; S091 P2: 76 B; S091 P3: 88 B; S091 P4: 100 B (a shared
     * part plus a 44 B union of five sub-engine states, the reworked `grn`
     * the largest; user-approved growth up to +32 B over 76 B).
```

#### M2 — RAM guard 100 B

**Location:** `Core/DSP/Effects/EffectsManager.c`, HEAD lines 143–151: **replace** (9 lines → 10; new lines 144–153).

- **What:** `_Static_assert(sizeof(effects_runtime_t) == 100u, …)`. The
  comment records +24 B of the 32 B approval (P3 12, P4 12) and the
  LTO-dependent granule.

Remove (HEAD 143–151):

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

Replace with:

```c
 * RAM guard (S074; 100 B since S091 P4): the DTCM union size is recorded in
 * STORAGE_SRAM_MANIFEST.md. A larger member grows .dtcmz and shrinks the
 * FxBuffer arena by the same amount (in 32 B granules), which needs RAM
 * approval before it is merged. S091 P3 and P4 used 24 B of a 32 B
 * approval (P3 +12 B, P4 +12 B for the reworked `grn` state); the
 * arena loses one granule (126,688 -> 126,656 B) when .dtcmz crosses a 32 B
 * boundary, which depends on LTO's packing (CRUMPBIT_LOOP_MAX_BYTES assumes
 * the smaller arena).
 */
_Static_assert(sizeof(effects_runtime_t) == 100u,
```

### 3.7 `Core/Menu/menuEffects.c`

#### U1 — Page comment: SELECT 1 screen 1

**Location:** `Core/Menu/menuEffects.c`, HEAD lines 14–14: **replace** (1 lines → 2; new lines 14–15).

- **What:** the layout comment at the top of `menuEffects.c` names
  `mrp run len scl`.

Remove (HEAD 14–14):

```c
 *   SELECT 1  screen 0: typ out vol pan    screen 1: run len scl mrp
```

Replace with:

```c
 *   SELECT 1  screen 0: typ out vol pan    screen 1: mrp run len scl
 *     (S091 P4, user: `mrp` first on the second page, for every type)
```

#### U2 — `menuEffects_cellAt()`: `mrp` first on SELECT 1 screen 1

**Location:** `Core/Menu/menuEffects.c`, HEAD lines 182–184: **replace** (3 lines → 7; new lines 183–189).

- **What:** the manager's `second[]` order becomes `MORPH_AMOUNT, RUN,
  LENGTH, SCALE` (was `RUN, LENGTH, SCALE, MORPH_AMOUNT`).
- **Why:** plan §A6.1. **This applies to every Effect type** (`flt` too).
  Every handler dispatches on the cell kind, not the column; checked: no
  code assumes `mrp` in column 3.

Remove (HEAD 182–184):

```c
        static const uint8_t second[MENU_FX_CELLS] = {
            MENU_FX_CELL_RUN, MENU_FX_CELL_LENGTH,
            MENU_FX_CELL_SCALE, MENU_FX_CELL_MORPH_AMOUNT
```

Replace with:

```c
        /*
         * SELECT 1 screen 1, manager-owned for every type: S091 P4 (user)
         * puts the Effect Morph amount first: mrp run len scl.
         */
        static const uint8_t second[MENU_FX_CELLS] = {
            MENU_FX_CELL_MORPH_AMOUNT, MENU_FX_CELL_RUN,
            MENU_FX_CELL_LENGTH, MENU_FX_CELL_SCALE
```

---

## 4. Tools and host tests

### 4.1 `tools/plots/gen_crumpbit_tables.py`

#### G1 — Generator docstring: P4

**Location:** `tools/plots/gen_crumpbit_tables.py`, HEAD lines 2–2: **replace** (1 lines → 2; new lines 2–3).

- **What:** names the S091 P4 rework tables in the title line.

Remove (HEAD 2–2):

```python
"""Generate the CrumpBit control tables (S091 P2; sub-type tables S091 P3).
```

Replace with:

```python
"""Generate the CrumpBit control tables (S091 P2; sub-type tables S091 P3;
the `grn` rework tables S091 P4).
```

#### G2 — Generator docstring: the new laws

**Location:** `tools/plots/gen_crumpbit_tables.py`, HEAD lines 37–44: **replace** (8 lines → 31; new lines 38–68).

- **What:** documents:
  - the Length fraction law (eight octaves, at least 220 frames in the
    DSP);
  - `crumpBit_grainTime` (6..384 ticks, stops pinned);
  - `crumpBit_grainStopCc`;
  - `crumpBit_grainDensity` ((882/63328)^((d − 1)/62)).

Remove (HEAD 37–44):

```python
              crumpBit_grainFraction[d] (65 entries) 2^(-5 (1 - d/64)): the
                                     `grn` grain size as a fraction of the
                                     synced time R, 1/32 .. 1, d = |sze-63|;
              crumpBit_reverbSize[r] `rvb` size factor 0.25 x 10^(r/127)
                                     (0.25 .. 2.5) for the four base delays;
              crumpBit_reverbDecay[f] `rvb` log2 of the per-sample line gain
                                     for RT60 T(f) = 0.1 s x 100^(f/127)
                                     (0.1 .. 10 s): -3 log2(10) / (T fs).
```

Replace with:

```python
              crumpBit_grainFraction[m] (65 entries) 2^(-8 (1 - m/64)):
                                     S091 P4: the `grn` grain Length as a
                                     fraction of the record line N, 1/256
                                     .. 1 (eight octaves, equal ratios),
                                     m = |len - 63|; the DSP holds it at
                                     least 220 samples (~5 ms, Q1);
              crumpBit_reverbSize[r] `rvb` size factor 0.25 x 10^(r/127)
                                     (0.25 .. 2.5) for the four base delays;
              crumpBit_reverbDecay[f] `rvb` log2 of the per-sample line gain
                                     for RT60 T(f) = 0.1 s x 100^(f/127)
                                     (0.1 .. 10 s): -3 log2(10) / (T fs).
            S091 P4 `grn` rework tables:
              crumpBit_grainTime[c]  the `grn` delay time in 96-PPQ ticks
                                     for `rte` c (Q8, Q10): a continuous
                                     log curve from 6 ticks (1/64) to 384
                                     (one bar), six octaves over 0..127,
                                     with the 16 rhythmic stops pinned to
                                     exact tick counts at the positions
                                     GRAIN_STOP_CC and equal ratios
                                     between neighbouring stops (the
                                     StepScale "nudged" curve, extended
                                     past 1/2 by d/2 and 1br);
              crumpBit_grainStopCc[k] (16 entries, uint8) the `rte`
                                     position of stop k, shortest first;
                                     Position and Density index stops by k;
              crumpBit_grainDensity[d] (64 entries) the mean trigger
                                     interval of negative Density d = 1..63
                                     as a fraction of N: (882/63328)^((d-1)
                                     /62), N at -1 down to ~20 ms (882
                                     samples) at -63 with the whole arena
                                     (Q6); entry 0 (unused) is 1.
```

#### G3 — Generator docstring: check C6

**Location:** `tools/plots/gen_crumpbit_tables.py`, HEAD lines 57–57: **replace** (1 lines → 4; new lines 81–84).

- **What:** C6:
  - grain time strictly increasing, exact at every stop, 6 at 0 and 384 at
    127;
  - grain density 1 at d = 1 and strictly decreasing.

Remove (HEAD 57–57):

```python
            (longer RT60, gain nearer 1); C5 --check: regenerate in memory and
```

Replace with:

```python
            (longer RT60, gain nearer 1); C6 (S091 P4) grain time strictly
            increasing, equal to the stop's tick count at every stop
            position, 6 at 0 and 384 at 127; grain density 1 at d = 1
            (entry 0 unused, 1) and strictly decreasing from there; C5 --check: regenerate in memory and
```

#### G4 — Generator constants: stops, octaves, density ends

**Location:** `tools/plots/gen_crumpbit_tables.py`, **add** after HEAD line 82. New lines 110–115 (6 lines).

- **What:**
  - `GRAIN_STOP_TICKS` (the 16 divisions in 96-PPQ ticks, `/64` .. `1br`);
  - `GRAIN_LEN_OCTAVES` 8;
  - `GRAIN_DENSITY_MIN/N` 882 / 63,328.

Add:

```python
# S091 P4: the `grn` rhythmic stops in 96-PPQ ticks, shortest first:
# /64 32t /32 16t d32 /16 8Tr d16 /8 4Tr d/8 /4 d/4 /2 d/2 1br.
GRAIN_STOP_TICKS = [6, 8, 12, 16, 18, 24, 32, 36, 48, 64, 72, 96, 144, 192,
                    288, 384]
GRAIN_LEN_OCTAVES = 8.0
GRAIN_DENSITY_MIN, GRAIN_DENSITY_N = 882.0, 63328.0
```

#### G5 — Table functions: Length law, stops, time curve, density

**Location:** `tools/plots/gen_crumpbit_tables.py`, HEAD lines 140–140: **replace** (1 lines → 30; new lines 173–202).

- **What:**
  - **`grain_fraction_table()`:** 2^(−8(1 − m/64)).
  - **`grain_stop_cc()`:** stop k at round(127·log2(t_k/6)/6), its place on
    the six-octave log curve (Python rounds half to even), giving 0, 9, 21,
    30, 34, 42, 51, 55, 64, 72, 76, 85, 97, 106, 118, 127.
  - **`grain_time_table()`:** exactly the stop's ticks at every stop, and
    equal ratios between neighbouring stops (piecewise log), the StepScale
    "nudged" curve extended past `/2`.
  - **`grain_density_table()`:** 1 (unused), then
    (882/63328)^((d − 1)/62) for d = 1..63.

Remove (HEAD 140–140):

```python
    return [2.0 ** (-5.0 * (1.0 - d / 64.0)) for d in range(65)]
```

Replace with:

```python
    return [2.0 ** (-GRAIN_LEN_OCTAVES * (1.0 - m / 64.0)) for m in range(65)]


def grain_stop_cc():
    """Stop k at the position of its tick count on the six-octave log curve
    (Python round: half to even), so the curve passes exactly through every
    division."""
    t0, t1 = GRAIN_STOP_TICKS[0], GRAIN_STOP_TICKS[-1]
    return [int(round(127.0 * math.log2(t / t0) / math.log2(t1 / t0)))
            for t in GRAIN_STOP_TICKS]


def grain_time_table():
    """Equal ratios between neighbouring stops: piecewise log, exact at
    every stop."""
    cc, ticks = grain_stop_cc(), GRAIN_STOP_TICKS
    vals = []
    for c in range(128):
        k = max(i for i in range(len(cc)) if cc[i] <= c)
        if cc[k] == c or k == len(cc) - 1:
            vals.append(float(ticks[k]))
            continue
        frac = (c - cc[k]) / (cc[k + 1] - cc[k])
        vals.append(ticks[k] * (ticks[k + 1] / ticks[k]) ** frac)
    return vals


def grain_density_table():
    ratio = GRAIN_DENSITY_MIN / GRAIN_DENSITY_N
    return [1.0] + [ratio ** ((d - 1) / 62.0) for d in range(1, 64)]
```

#### G6 — `render_u8()`

**Location:** `tools/plots/gen_crumpbit_tables.py`, **add** after HEAD line 162. New lines 225–232 (8 lines).

- **What:** renders a `uint8_t` array (the stop positions).

Add:

```python
def render_u8(name, comment, vals):
    lines = ["", "/* %s */" % comment,
             "const uint8_t %s[%d] = {" % (name, len(vals))]
    lines.append("    " + ", ".join("%du" % v for v in vals) + ",")
    lines.append("};")
    return lines


```

#### G7 — `render()`: the three new tables

**Location:** `tools/plots/gen_crumpbit_tables.py`, HEAD lines 172–191: **replace** (20 lines → 27; new lines 242–268).

- **What:** unpacks the extra tables and appends `crumpBit_grainTime`,
  `crumpBit_grainStopCc` and `crumpBit_grainDensity` after the S091 P3
  tables. The Length table's comment line is updated.

Remove (HEAD 172–191):

```python
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
```

Replace with:

```python
    comb, win, semi, frac, size, decay, gtime, gstop, gdens = extra
    lines += render_table("crumpBit_combTable",
                          "cmb comb length in samples, rte 0..127: note 112 - rte, clamped at 96.", comb)
    lines += render_table("crumpBit_pitchWindow",
                          "pit window in samples, rte 0..127: 10 ms .. 200 ms.", win)
    lines += render_table("crumpBit_semitoneRatio",
                          "Pitch ratio of (index - 24) semitones, -24 .. +24.", semi)
    lines += render_table("crumpBit_grainFraction",
                          "grn grain Length as a fraction of N, |len - 63| 0..64: 1/256 .. 1.", frac)
    lines += render_table("crumpBit_reverbSize",
                          "rvb size factor, rte 0..127: 0.25 .. 2.5.", size)
    lines += render_table("crumpBit_reverbDecay",
                          "rvb log2 line gain per sample, fbk 0..127: RT60 0.1 s .. 10 s.", decay)
    lines += render_table("crumpBit_grainTime",
                          "grn delay time in 96-PPQ ticks, rte 0..127: 1/64 .. 1 bar, stops exact.", gtime)
    lines += render_u8("crumpBit_grainStopCc",
                       "grn rhythmic stop positions on the rte curve, /64 .. 1br.", gstop)
    lines += render_table("crumpBit_grainDensity",
                          "grn mean trigger interval / N, negative Density 1..63 (entry 0 unused).", gdens)
    return "\n".join(lines) + "\n"


def main():
    rate, tone, filt = rate_table(), tone_table(), filter_table()
    comb, win, semi = comb_table(), pitch_window_table(), semitone_table()
    frac, size, decay = grain_fraction_table(), reverb_size_table(), reverb_decay_table()
    gtime, gstop, gdens = grain_time_table(), grain_stop_cc(), grain_density_table()
```

#### G8 — `main()`: compute and check the new tables (C6)

**Location:** `tools/plots/gen_crumpbit_tables.py`, HEAD lines 203–209: **replace** (7 lines → 14; new lines 280–293).

- **What:** builds the three tables and adds the C6 checks:
  - strict order;
  - 6 and 384 at the ends;
  - exact at every stop;
  - distinct stop positions;
  - the density law's ends and order.

  It passes them to `render()`.

Remove (HEAD 203–209):

```python
    text = render(rate, tone, filt, (comb, win, semi, frac, size, decay))
    if "--check" in sys.argv:
        ok = ok and open(PATH).read() == text
        print("crumpbit tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("crumpbit tables: C1-C4 failed")
```

Replace with:

```python
    ok = ok and all(a < b for a, b in zip(gtime, gtime[1:]))
    ok = ok and gtime[0] == 6.0 and gtime[127] == 384.0
    ok = ok and all(gtime[c] == float(t) for c, t in zip(gstop, GRAIN_STOP_TICKS))
    ok = ok and len(set(gstop)) == len(gstop)
    ok = ok and gdens[0] == 1.0 and gdens[1] == 1.0
    ok = ok and all(a > b for a, b in zip(gdens[1:], gdens[2:]))
    text = render(rate, tone, filt,
                  (comb, win, semi, frac, size, decay, gtime, gstop, gdens))
    if "--check" in sys.argv:
        ok = ok and open(PATH).read() == text
        print("crumpbit tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("crumpbit tables: C1-C4/C6 failed")
```

### 4.2 `tools/dsp_test/elf_looppath.py`

#### L1 — `elf_looppath.py`: document `--in-range`

**Location:** `tools/dsp_test/elf_looppath.py`, HEAD lines 25–25: **replace** (1 lines → 6; new lines 25–30).

- **What:** a loop's path search stays between its head and the furthest
  backward branch that closes it.
- **Why:** the new `crumpBit_runGrain()` has cold blocks that close
  conditional branches back into the function. As loop heads they make
  the simple-path search explore the whole function, which never ends.
- **Checked:** contiguous frame loops measure identically with the option
  (`runDelay`, `runPitch`, `runReverb`, `process`; `runGrain` 136 / 133,
  as measured head by head).

Remove (HEAD 25–25):

```python
            with a conditional branch).
```

Replace with:

```python
            with a conditional branch), --in-range (S091 P4: a loop's path
            search stays between its head and the furthest backward branch
            that closes it; a cold block that closes a conditional branch
            back into the function, which is not a frame loop, would
            otherwise search every path of the whole function. Frame loops
            laid out contiguously measure the same with it).
```

#### L2 — `loop_paths()`: the optional range

**Location:** `tools/dsp_test/elf_looppath.py`, HEAD lines 83–89: **replace** (7 lines → 10; new lines 88–97).

- **What:** a new `last` argument. With it, successors outside
  `idx[head]..last` are not followed. The default (None) is unchanged.

Remove (HEAD 83–89):

```python
def loop_paths(r, idx, head):
    """Every simple path head -> ... -> head, as (length, vdivs, indices)."""
    found = []
    stack = [(idx[head], [idx[head]])]
    while stack:
        i, path = stack.pop()
        for j in successors(r, idx, i):
```

Replace with:

```python
def loop_paths(r, idx, head, last=None):
    """Every simple path head -> ... -> head, as (length, vdivs, indices).
    With `last` (an index), only instructions idx[head]..last are followed."""
    found = []
    stack = [(idx[head], [idx[head]])]
    while stack:
        i, path = stack.pop()
        for j in successors(r, idx, i):
            if last is not None and not (idx[head] <= j <= last):
                continue
```

#### L3 — `--in-range` argument

**Location:** `tools/dsp_test/elf_looppath.py`, **add** after HEAD line 106. New lines 115–115 (1 lines).

- **What:** `ap.add_argument('--in-range', action='store_true')`.

Add:

```python
    ap.add_argument('--in-range', action='store_true')
```

#### L4 — `closer`: each head's furthest closing branch

**Location:** `tools/dsp_test/elf_looppath.py`, HEAD lines 119–120: **replace** (2 lines → 8; new lines 128–135).

- **What:** for every head, the index of the furthest backward branch to
  it. It is passed as `last` only with `--in-range`.

Remove (HEAD 119–120):

```python
    for h in heads:
        p = loop_paths(r, idx, h)
```

Replace with:

```python
    # closer[h]: the index of the furthest backward branch to head h.
    closer = {}
    for i, (addr, op, ops) in enumerate(r):
        t = target(ops) if BRANCH.match(op) else None
        if t in heads and t <= addr:
            closer[t] = max(closer.get(t, 0), i)
    for h in heads:
        p = loop_paths(r, idx, h, closer[h] if a.in_range else None)
```

### 4.3 `tools/dsp_test/Makefile`

#### T1 — Makefile header: twelve breaks

**Location:** `tools/dsp_test/Makefile`, HEAD lines 12–16: **replace** (5 lines → 7; new lines 12–18).

- **What:** names the three P4 break builds.

Remove (HEAD 12–16):

```make
#               crumpbit            modules/crumpbit.c (+ 9 breaks: P2 Sync
#                                   key, R4 AC seed, P2 flat tone, P2 seat
#                                   delay clamp, P2 Rate-limit probe; P3
#                                   wet around the mix stage, grain freeze,
#                                   pitch direction, lane map)
```

Replace with:

```make
#               crumpbit            modules/crumpbit.c (+ 12 breaks: P2
#                                   Sync key, R4 AC seed, P2 flat tone, P2
#                                   seat delay clamp, P2 Rate-limit probe;
#                                   P3 wet around the mix stage, grain
#                                   freeze, pitch direction, lane map; P4
#                                   grain fade asymmetry, `bal` side swap,
#                                   grain overwrite clamp)
```

#### T2 — Rule comment: CB1–CB16

**Location:** `tools/dsp_test/Makefile`, HEAD lines 262–263: **replace** (2 lines → 2; new lines 264–265).

- **What:** CB1–CB4, CB10 and CB16 are bit-exact.

Remove (HEAD 262–263):

```make
# the references in modules/crumpbit.c (laws CB1-CB15; CB1-CB4 and CB10
# bit-exact).
```

Replace with:

```make
# the references in modules/crumpbit.c (laws CB1-CB16; CB1-CB4, CB10 and
# CB16 bit-exact).
```

#### T3 — Break comments: break 4 deterministic; breaks 10–12

**Location:** `tools/dsp_test/Makefile`, HEAD lines 274–283: **replace** (10 lines → 18; new lines 276–293).

- **What:**
  - Break 4 now fails the CB11 seat law. It used to be met only by chance,
    and P4's row count changed CB11's random sequence.
  - **Break 10:** a fade-out twice as fast as the fade-in fails the copy
    identity.
  - **Break 11:** the left filter built from the right cutoff fails CB16.
  - **Break 12:** the overwrite clamp removed fails CB14's read-safety law.

Remove (HEAD 274–283):

```make
#     longer than the loop) must fail the CB4 seat invariant;
#   - the Rate-limit search without its eighth probe (seven steps reach
#     only 127 entries, so the whole arena would give 126) must fail CB6;
#   - S091 P3: the `rvb` wet added around the mix stage (mix 0 no longer
#     the dry signal) must fail CB10;
#   - the `grn` freeze ignored (the record line keeps recording) must fail
#     CB14;
#   - the `pit` phase step with its sign flipped (+12 st plays -12) must
#     fail CB13;
#   - a granular row on a `pit` lane must fail CB9.
```

Replace with:

```make
#     longer than the loop) must fail the CB11 seat law (S091 P4: made
#     deterministic; CB4/CB11 met it only by chance);
#   - the Rate-limit search without its eighth probe (seven steps reach
#     only 127 entries, so the whole arena would give 126) must fail CB6;
#   - S091 P3: the `rvb` wet added around the mix stage (mix 0 no longer
#     the dry signal) must fail CB10;
#   - the `grn` freeze ignored (the record line keeps recording) must fail
#     CB14;
#   - the `pit` phase step with its sign flipped (+12 st plays -12) must
#     fail CB13;
#   - a granular row on a `pit` lane must fail CB9;
#   - S091 P4: a grain fade-out that falls twice as fast as its fade-in
#     (back-to-back grains no longer sum to 1) must fail CB14's copy
#     identity;
#   - the left channel's filter built from the right channel's cutoff must
#     fail CB16;
#   - the grain overwrite clamp removed (grains read into unrecorded or
#     overwritten audio) must fail CB14's read-safety law.
```

#### T4 — Break 4's message: CB11 seat

**Location:** `tools/dsp_test/Makefile`, HEAD lines 369–369: **replace** (1 lines → 1; new lines 379–379).

- **What:** the caught message names the law that now catches it.

Remove (HEAD 369–369):

```make
	else echo "crumpbit_break4: caught (law CB4 fails as expected)"; fi
```

Replace with:

```make
	else echo "crumpbit_break4: caught (law CB11 seat fails as expected)"; fi
```

#### T5 — Break 7's target: `frozen`

**Location:** `tools/dsp_test/Makefile`, HEAD lines 391–391: **replace** (1 lines → 1; new lines 401–401).

- **What:** the sed target follows the renamed condition,
  `k.rec_w = frozen ? k.dline : b->base;`.

Remove (HEAD 391–391):

```make
	sed 's/k.rec_w = rt->sync ? k.dline : b->base;/k.rec_w = b->base;/' \
```

Replace with:

```make
	sed 's/k.rec_w = frozen ? k.dline : b->base;/k.rec_w = b->base;/' \
```

#### T6 — Breaks 10–12

**Location:** `tools/dsp_test/Makefile`, **add** after HEAD line 417. New lines 428–457 (30 lines).

- **What:** three break builds, each grep-checked before compiling.
  - **Break 10:** `s->out_a -= 2.0f * CRUMPBIT_GRAIN_FADE_K;`.
  - **Break 11:** the left coefficients from `crumpBit_filterG[fr]`.
  - **Break 12:** `span = (span > s_max) ? s_max : span;` → `(void)s_max;`.

Add:

```make
	mkdir -p build/break_cb10 build/break_cb11 build/break_cb12
	sed 's/        s->out_a -= CRUMPBIT_GRAIN_FADE_K;/        s->out_a -= 2.0f * CRUMPBIT_GRAIN_FADE_K;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb10/CrumpBitEffect.host.c
	@grep -q 's->out_a -= 2.0f \* CRUMPBIT_GRAIN_FADE_K;' \
	  build/break_cb10/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb10 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break10 -lm
	@if ./build/crumpbit_break10 >/dev/null 2>&1; then \
	  echo "crumpbit_break10: FAILED (grain fade break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break10: caught (law CB14 fails as expected)"; fi
	sed 's/crumpBit_filterCoeffs(crumpBit_filterG\[fl\], r, &a1l/crumpBit_filterCoeffs(crumpBit_filterG[fr], r, \&a1l/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb11/CrumpBitEffect.host.c
	@grep -q 'crumpBit_filterG\[fr\], r, &a1l' \
	  build/break_cb11/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb11 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break11 -lm
	@if ./build/crumpbit_break11 >/dev/null 2>&1; then \
	  echo "crumpbit_break11: FAILED (bal side break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break11: caught (law CB16 fails as expected)"; fi
	sed 's/        span = (span > s_max) ? s_max : span;/        (void)s_max;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb12/CrumpBitEffect.host.c
	@grep -q '        (void)s_max;' build/break_cb12/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb12 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break12 -lm
	@if ./build/crumpbit_break12 >/dev/null 2>&1; then \
	  echo "crumpbit_break12: FAILED (overwrite clamp break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break12: caught (law CB14 fails as expected)"; fi
```

#### T7 — ELF gate comment: `grn` 136

**Location:** `tools/dsp_test/Makefile`, HEAD lines 427–429: **replace** (3 lines → 4; new lines 467–470).

- **What:** the gate comment records 136 (two spans around the block's one
  trigger or steal; flat-top windows) and `--cond-heads --in-range`.

Remove (HEAD 427–429):

```make
#     135 (Q14 integer heads), `grn` 131 (two spans around the one restart
#     a block can hold, no delay glide; --cond-heads: its cold blocks jump
#     back with an unconditional `b`, which is not a loop), `rvb` 145;
```

Replace with:

```make
#     135 (Q14 integer heads), `grn` 136 (S091 P4: two spans around the
#     block's one trigger or steal; flat-top windows; --cond-heads
#     --in-range: its cold blocks close backward branches into the body,
#     which are not frame loops), `rvb` 145;
```

#### T8 — ELF gate: `runGrain`

**Location:** `tools/dsp_test/Makefile`, HEAD lines 445–445: **replace** (1 lines → 1; new lines 486–486).

- **What:** `--sym crumpBit_runGrain --cond-heads --in-range --top 2
  --max 136`.

Remove (HEAD 445–445):

```make
	  --sym crumpBit_runGrain --cond-heads --top 2 --max 131
```

Replace with:

```make
	  --sym crumpBit_runGrain --cond-heads --in-range --top 2 --max 136
```

### 4.4 `tools/dsp_test/modules/crumpbit.c`

#### X1 — Host test title: P4

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 3–3: **replace** (1 lines → 1; new lines 3–3).

- **What:** names the S091 P4 `grn` rework and `bal`.

Remove (HEAD 3–3):

```c
 * sub-types).
```

Replace with:

```c
 * sub-types; S091 P4 `grn` rework and `bal`).
```

#### X2 — Header: CB4 and CB5

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 30–34: **replace** (5 lines → 7; new lines 30–36).

- **What:**
  - CB4 writes the P3/P4 `grn` rows (incl. `len`), which must not change
    `dly`; `bal` is excluded because it has its own law.
  - CB5 checks den 64, len 127, bal 63.

Remove (HEAD 30–34):

```c
 *                    S091 P3: the sub-type rows (aux, st2, st3, spr, den,
 *                    amp, wid) are written too and must not change `dly`;
 *               CB5  init: AC memory 128, tone flat, filter `off` (and its
 *                    ramp origin `off`); S091 P3: sub `dly`, aux/st2/st3 63,
 *                    den 127;
```

Replace with:

```c
 *                    S091 P3/P4: the sub-type rows (aux, st2, st3, spr,
 *                    den, len, wid) are written too and must not change
 *                    `dly` (`bal`, which does, has its own law CB16);
 *               CB5  init: AC memory 128, tone flat, filter `off` (and its
 *                    ramp origin `off`); S091 P3: sub `dly`, aux/st2/st3 63;
 *                    S091 P4: den 64 (+1), len 127, bal 63 (the copy
 *                    setting and a centred balance);
```

#### X3 — Header: CB14 (P4 model), CB16

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 76–85: **replace** (10 lines → 21; new lines 78–98).

- **What:** documents the rewritten CB14 and the new CB16.

Remove (HEAD 76–85):

```c
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

Replace with:

```c
 *               CB14 `grn` (S091 P4 model): the pitch-slot distribution
 *                    over the absolute P1..P3 (spr 0 P1 only, 40 halves,
 *                    80 thirds, 127 detune within +-1 st); the Width
 *                    distributions (0 centre, 64 uniform, 127 50-100 %
 *                    sides); the Position laws (0 two behind the head,
 *                    positive exactly the stop at the tempo, negative inside
 *                    the scatter range); no read ever reaches unrecorded or
 *                    overwritten audio (the overwrite clamp, every row);
 *                    the copy identity (Density +1, Length full, Position
 *                    0: output = the recorded signal two frames late plus
 *                    its delay-line echo, across retriggers); rhythmic
 *                    Density intervals exact, random ones of the right
 *                    mean, off = none; freeze keeps the record line
 *                    bit-exact and anchors Position; stability at fbk 127;
 *               CB15 `rvb`: stable and bounded at every extreme of rte,
 *                    fbk, snc, ton and dpn; an RT60 of ~0.49 s (fbk 44)
 *                    decays 9..21 dB over 0.125 s; freeze holds the tail
 *                    within +-6 dB over 1 s;
 *               CB16 `bal` (S091 P4): with balance b, the left channel is
 *                    bit-identical to a run at frq - b and balance 0, the
 *                    right to one at frq + b, for every filter type.
```

#### X4 — Header: twelve break builds

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 95–95: **replace** (1 lines → 1; new lines 108–108).

- **What:** the accessor line names twelve break builds.

Remove (HEAD 95–95):

```c
 * Accessors:  make -C tools/dsp_test crumpbit (nine break builds follow).
```

Replace with:

```c
 * Accessors:  make -C tools/dsp_test crumpbit (twelve break builds follow).
```

#### X5 — CB4: `GRAIN_LENGTH` in the random rows

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 673–673: **replace** (1 lines → 1; new lines 686–686).

- **What:** `GRAIN_AMP` → `GRAIN_LENGTH`. CB4 stays bit-exact against the
  P2 reference, including `bal` 63 in stage 4.

Remove (HEAD 673–673):

```c
        CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_GRAIN_AMP,
```

Replace with:

```c
        CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_GRAIN_LENGTH,
```

#### X6 — CB5: the P4 seeds

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 697–697: **replace** (1 lines → 3; new lines 710–712).

- **What:** density 64 and length 127 (the copy setting), balance 63.

Remove (HEAD 697–697):

```c
    expect(rt.grain_density == 127u, "CB5 density full");
```

Replace with:

```c
    expect(rt.grain_density == 64u && rt.grain_length == 127u,
           "CB5 grn copy setting (den +1, len full)");
    expect(rt.filter_bal == 63u, "CB5 balance centred");
```

#### X7 — `ref_subtypeLimit()`: the `grn` delay-time law

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 941–948: **replace** (8 lines → 11; new lines 956–966).

- **What:** the `grn` bound is the half line (0 below 512 frames) in ticks
  at the test tempo, scanned over `crumpBit_grainTime`.

Remove (HEAD 941–948):

```c
    const float grain_max = (half < 66u) ? 0.0f : (float)(half - 2u);

    switch (sub) {
    case CRUMPBIT_SUBTYPE_COMB:   return ref_tableScan(crumpBit_combTable, max);
    case CRUMPBIT_SUBTYPE_PITCH:  return ref_tableScan(crumpBit_pitchWindow,
                                                       max - 2.0f);
    case CRUMPBIT_SUBTYPE_GRAIN:  return ref_tableScan(crumpBit_rateTable,
                                                       grain_max);
```

Replace with:

```c
    const float grain_max = (half < 512u) ? 0.0f : (float)(half - 2u);
    /* S091 P4: the grn delay-time curve is in ticks at the test tempo. */
    const float grain_ticks = grain_max * (float)(cb_bpm ? cb_bpm : 1u) /
                              CRUMPBIT_SAMPLES_PER_TICK;

    switch (sub) {
    case CRUMPBIT_SUBTYPE_COMB:   return ref_tableScan(crumpBit_combTable, max);
    case CRUMPBIT_SUBTYPE_PITCH:  return ref_tableScan(crumpBit_pitchWindow,
                                                       max - 2.0f);
    case CRUMPBIT_SUBTYPE_GRAIN:  return ref_tableScan(crumpBit_grainTime,
                                                       grain_ticks);
```

#### X8 — CB6: the P4 tables and the Position clamp

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 1008–1011: **replace** (4 lines → 63; new lines 1026–1088).

- **What:**
  - the Length fraction 1/256 .. 1;
  - grain time 6 .. 384, exact at every stop, the stop positions
    ascending;
  - the first 14 stops are StepScale tick counts (`stepScale_ticksQ8()`);
  - grain density 1 → 882/63328, strictly decreasing;
  - grain time strictly increasing;
  - for every share in 1 KB steps, `effective_max` of `dpn` is
    63 + 4·(stops that fit) in `grn` and 255 in `dly`.

Remove (HEAD 1008–1011):

```c
    expect(fabsf(crumpBit_grainFraction[0] - 1.0f / 32.0f) < 1e-8f,
           "CB6 grain 1/32");
    expect(crumpBit_grainFraction[CRUMPBIT_GRAIN_STEPS] == 1.0f,
           "CB6 grain 1");
```

Replace with:

```c
    expect(fabsf(crumpBit_grainFraction[0] - 1.0f / 256.0f) < 1e-9f,
           "CB6 grain length 1/256 of N");
    expect(crumpBit_grainFraction[CRUMPBIT_GRAIN_STEPS] == 1.0f,
           "CB6 grain length N");
    /* S091 P4: the delay-time curve and its stops (Q8, Q10). */
    expect(crumpBit_grainTime[0] == 6.0f && crumpBit_grainTime[127] == 384.0f,
           "CB6 grain time 1/64 .. 1 bar");
    {
        static const float stop_ticks[CRUMPBIT_GRAIN_STOPS] = {
            6, 8, 12, 16, 18, 24, 32, 36, 48, 64, 72, 96, 144, 192, 288, 384 };
        uint32_t k;

        for (k = 0u; k < CRUMPBIT_GRAIN_STOPS; k++) {
            snprintf(msg, sizeof msg, "CB6 grain stop %u", k);
            expect(crumpBit_grainTime[crumpBit_grainStopCc[k]] ==
                   stop_ticks[k], msg);
            if (k)
                expect(crumpBit_grainStopCc[k] > crumpBit_grainStopCc[k - 1u],
                       msg);
            /* The first 14 are StepScale's stops, by tick count (Q8). */
            if (k < 14u) {
                uint32_t cc;
                uint32_t hit = 0u;

                for (cc = 0u; cc < STEP_SCALE_COUNT; cc++)
                    hit |= (stepScale_ticksQ8((uint8_t)cc) ==
                            (uint16_t)(stop_ticks[k] * 256.0f)) ? 1u : 0u;
                expect(hit != 0u, msg);
            }
        }
    }
    expect(crumpBit_grainDensity[1] == 1.0f &&
           fabsf(crumpBit_grainDensity[63] - 882.0f / 63328.0f) < 1e-6f,
           "CB6 grain density N .. 20 ms");
    for (i = 2u; i <= CRUMPBIT_DENSITY_STEPS; i++) {
        snprintf(msg, sizeof msg, "CB6 grain density %u", i);
        expect(crumpBit_grainDensity[i] < crumpBit_grainDensity[i - 1u], msg);
    }
    for (i = 1u; i < 128u; i++) {
        snprintf(msg, sizeof msg, "CB6 grain time %u", i);
        expect(crumpBit_grainTime[i] > crumpBit_grainTime[i - 1u], msg);
    }
    /* Position's clamp in grn: 63 + 4 x (stops that fit); 255 elsewhere. */
    for (bytes = 0u; bytes <= CRUMPBIT_LOOP_MAX_BYTES; bytes += 1024u) {
        fx_share_t share = { NULL, 0u, bytes };
        const uint32_t half = bytes / 2u;
        const float gmax = (half < 512u) ? 0.0f : (float)(half - 2u);
        const float tmax = gmax * (float)cb_bpm / CRUMPBIT_SAMPLES_PER_TICK;
        uint32_t fit = 0u;
        uint32_t k;

        for (k = 0u; k < CRUMPBIT_GRAIN_STOPS; k++)
            fit += (crumpBit_grainTime[crumpBit_grainStopCc[k]] <= tmax);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_SUBTYPE,
                                 CRUMPBIT_SUBTYPE_GRAIN);
        snprintf(msg, sizeof msg, "CB6 grn position limit bytes %u", bytes);
        expect(crumpBit_ops.effective_max(&rt, CRUMPBIT_PARAM_DLY_PAN,
                                          &share) == 63u + 4u * fit, msg);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_SUBTYPE,
                                 CRUMPBIT_SUBTYPE_DELAY);
        expect(crumpBit_ops.effective_max(&rt, CRUMPBIT_PARAM_DLY_PAN,
                                          &share) == 255u, msg);
    }
```

#### X9 — CB9: `bal` is shared

**Location:** `tools/dsp_test/modules/crumpbit.c`, **add** after HEAD line 1068. New lines 1146–1148 (3 lines).

- **What:** `CRUMPBIT_ROWS_SHARED` contains `FILTER_BAL`.

Add:

```c
    expect((CRUMPBIT_ROWS_SHARED &
            CRUMPBIT_ROW_BIT(CRUMPBIT_PARAM_FILTER_BAL)) != 0u,
           "CB9 bal is shared (output filter)");
```

#### X10 — CB9: `bal` lane-less; the `grn` pool

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 1102–1102: **replace** (1 lines → 14; new lines 1182–1195).

- **What:** no map names `bal`. The `grn` map has den 4, rte 5, snc 6,
  dpn 9, lng 10, aux 14, spr 15 (Q11).

Remove (HEAD 1102–1102):

```c
        }
```

Replace with:

```c
            /* S091 P4: `bal` has no lane (Q11). */
            expect(row != CRUMPBIT_PARAM_FILTER_BAL, msg);
        }
    }
    {
        /* S091 P4 (Q11): the grn pool carries its performance controls. */
        const uint8_t *g = crumpBit_laneMap(CRUMPBIT_SUBTYPE_GRAIN);

        expect(g[4] == CRUMPBIT_PARAM_GRAIN_DENSITY &&
               g[5] == CRUMPBIT_PARAM_RATE && g[6] == CRUMPBIT_PARAM_SYNC &&
               g[9] == CRUMPBIT_PARAM_DLY_PAN &&
               g[10] == CRUMPBIT_PARAM_GRAIN_LENGTH &&
               g[14] == CRUMPBIT_PARAM_AUX &&
               g[15] == CRUMPBIT_PARAM_GRAIN_SPREAD, "CB9 grn lane pool");
```

#### X11 — CB10: `GRAIN_LENGTH` in the rows

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 1195–1195: **replace** (1 lines → 1; new lines 1288–1288).

- **What:** the mix-stage law writes `GRAIN_LENGTH` instead of `GRAIN_AMP`.

Remove (HEAD 1195–1195):

```c
                    CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_GRAIN_AMP,
```

Replace with:

```c
                    CRUMPBIT_PARAM_GRAIN_DENSITY, CRUMPBIT_PARAM_GRAIN_LENGTH,
```

#### X12 — CB11: the deterministic seat law

**Location:** `tools/dsp_test/modules/crumpbit.c`, **add** after HEAD line 1255. New lines 1349–1367 (19 lines).

- **What:** `dly` at the full arena with `rte` 127 (the delay primes to its
  target), then a share shrink to 4 KB. The tape delay must be ≤ length − 2
  at once.
- **Why:** break 4 removes this clamp. The random switch storm met the
  case only by chance.

Add:

```c
    /*
     * S091 P4: the seat's tape-delay clamp, deterministically (it was only
     * met by chance before): a full-length delay, then a share shrink, must
     * leave delay <= length - 2 at once (break 4 removes the clamp).
     */
    {
        fx_share_t big = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        fx_share_t small = { arena_p3, 0u, 4096u };
        float bl[32], br[32];

        memset(bl, 0, sizeof bl);
        memset(br, 0, sizeof br);
        p3_init(&rt, CRUMPBIT_SUBTYPE_DELAY);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 127u, &big);
        p3_block(&rt, &big, bl, br, 32u);
        crumpBit_ops.buffer_changed(&rt, &small);
        expect(rt.u.tap.delay <= (float)(rt.u.tap.length - 2u),
               "CB11 seat: a share shrink clamps the tape delay");
    }
```

#### X13 — CB11: `grn` invariants

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 1312–1317: **replace** (6 lines → 12; new lines 1424–1435).

- **What:** after every block:
  - positions inside the line;
  - each age ≤ its end ≤ length + 128;
  - the anchor none or inside the line;
  - `pending` ≤ 1 + 32 + 128.

Remove (HEAD 1312–1317):

```c
            expect(g->length == 0u ||
                   (g->write_pos < g->length && g->valid <= g->length &&
                    (g->length < 4u * CRUMPBIT_GRAIN_MIN ||
                     ((g->pos_a >> 16) < g->length &&
                      (g->pos_b >> 16) < g->length &&
                      g->age < g->length))), msg);
```

Replace with:

```c
            /* S091 P4: voices inside the line, ages within their ends. */
            expect(g->length == 0u ||
                   (g->write_pos < g->length && g->valid <= g->length &&
                    (g->length < CRUMPBIT_GRAIN_LINE_MIN ||
                     ((g->pos_a >> 16) < g->length &&
                      (g->pos_b >> 16) < g->length &&
                      g->age_a <= g->end_a && g->age_b <= g->end_b &&
                      g->end_a <= g->length + CRUMPBIT_GRAIN_FADE &&
                      g->end_b <= g->length + CRUMPBIT_GRAIN_FADE &&
                      (g->anchor == CRUMPBIT_GRAIN_NO_ANCHOR ||
                       g->anchor < g->length) &&
                      g->pending <= 1u + 32u + CRUMPBIT_GRAIN_FADE))), msg);
```

#### X14 — CB14: `grn` (the P4 model)

**Location:** `tools/dsp_test/modules/crumpbit.c`, HEAD lines 1467–1648: **replace** (182 lines → 406; new lines 1585–1990).

- **What:** the rewritten law, in nine sub-blocks (§A4 of the plan).
- **Why:** the board cannot report samples. These laws prove the documented
  behaviour on the real source, and breaks 7, 10 and 12 must fail them.

Remove HEAD lines 1467–1648 (182 lines), from
`/* CB14: `grn` --------------------------------------------------------------- */`
through
`p3_write(&rt, CRUMPBIT_PARAM_GRAIN_SPREAD, 127u, &share);`.

Replace with the 9 sub-blocks below, in order:

##### X14.1 — The dry reference: `cb_dry_t`, `cb_recorded()` (new lines 1585–1618)

- **What:** the S074-form stage 1 (float AC memories) and the recorded mono
  8-bit value, `code/128` of `adc(½(hL + hR))`. This is what the record
  line holds, the reference for the copy identity.

```c
/* CB14: `grn` (S091 P4 model) ---------------------------------------------- */

/* The S074-form dry path and the recorded mono value, for CB14's copy law. */
typedef struct {
    float ax_l, ax_r, ay_l, ay_r;
} cb_dry_t;

static float cb_recorded(cb_dry_t *d, float l, float r)
{
    const float yl = (float)((int32_t)ref_adc(l) - 128) * CRUMPBIT_LSB;
    const float yr = (float)((int32_t)ref_adc(r) - 128) * CRUMPBIT_LSB;
    const float hl = yl - d->ax_l + CRUMPBIT_AC_R * d->ay_l;
    const float hr = yr - d->ax_r + CRUMPBIT_AC_R * d->ay_r;

    d->ax_l = yl;
    d->ay_l = hl;
    d->ax_r = yr;
    d->ay_r = hr;
    /* The record line holds the 8-bit code of the mono dry signal. */
    return (float)((int32_t)crumpBit_adc(0.5f * (hl + hr)) - 128) *
           CRUMPBIT_LSB;
}

static void law_cb14(void)
{
    static const uint8_t spreads[] = { 0u, 40u, 80u, 127u };
    const float per_tick = CRUMPBIT_SAMPLES_PER_TICK / 120.0f;
    CrumpBitRuntime rt;
    uint32_t s;
    uint32_t i;
    char msg[160];

    cb_bpm = 120u;

```

##### X14.2 — Setup; (a) pitch slots and detune (new lines 1619–1684)

- **What:** `cb_bpm` 120.
  - 30,000 candidates at `spr` 0 / 40 / 80 / 127, with P1 0, P2 +5, P3 +9
    (absolute).
  - P1 only at 0; halves within 2 % at 40; thirds within 2 % at 80.
  - At 127, mostly off-slot, each within ±101 cents of a slot.

```c
    /* (a) pitch slots over the absolute P1..P3, and the detune bound. */
    memset(&rt, 0, sizeof rt);
    crumpBit_ops.init(&rt, NULL);
    rt.aux_raw = 63u;                   /* P1 0 st */
    rt.grain_st2 = 68u;                 /* P2 +5 st (absolute) */
    rt.grain_st3 = 72u;                 /* P3 +9 st (absolute) */
    rt.grain_length = 63u;              /* shortest, forward */
    rt.pan_raw = 63u;                   /* Position 0 */
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

            crumpBit_grainPick(&hash, &rt, 60000u, 30000u, 60000u, 0u,
                               per_tick, &c);
            for (k = 0u; k < 3u; k++)
                if (c.inc == slot_inc[k])
                    slot = k;
            if (slot == 3u) {
                /* Off-slot: only above spr 80, within +-1 st (+ Q3.12). */
                double best = 1e9;

                for (k = 0u; k < 3u; k++) {
                    const double cents = 1200.0 *
                        log2((double)c.inc / (double)slot_inc[k]);

                    best = (fabs(cents) < fabs(best)) ? cents : best;
                }
                snprintf(msg, sizeof msg, "CB14 spr %u detune %.2f cents",
                         spreads[s], best);
                expect(spreads[s] > 80u && fabs(best) <= 101.0, msg);
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

```

##### X14.3 — (b) Width (new lines 1685–1728)

- **What:** 20,000 pans each:
  - Width 0: all 127;
  - Width 64: the extremes reached (≤ 4, ≥ 250), mean 127 ± 4;
  - Width 127: every pan ≤ 64 or ≥ 190, both sides > 9,000.

```c
    /* (b) Width: 0 centre, 64 uniform over the field, 127 sides only. */
    {
        static const uint8_t widths[3] = { 0u, 64u, 127u };
        uint32_t w;

        rt.grain_spread = 0u;
        for (w = 0u; w < 3u; w++) {
            uint32_t hash = 0xBEEFu;
            uint32_t lo = 255u;
            uint32_t hi = 0u;
            uint32_t left = 0u;
            uint32_t right = 0u;
            uint32_t bad = 0u;
            double sum = 0.0;

            rt.grain_width = widths[w];
            for (i = 0u; i < 20000u; i++) {
                crumpbit_grain_t c;

                crumpBit_grainPick(&hash, &rt, 60000u, 30000u, 60000u, 0u,
                                   per_tick, &c);
                lo = (c.pan < lo) ? c.pan : lo;
                hi = (c.pan > hi) ? c.pan : hi;
                sum += c.pan;
                left += (c.pan < 127u);
                right += (c.pan > 127u);
                if (widths[w] == 0u)
                    bad += (c.pan != 127u);
                if (widths[w] == 127u)
                    bad += (c.pan > 64u && c.pan < 190u);
            }
            snprintf(msg, sizeof msg,
                     "CB14 wid %u: pans %u..%u mean %.1f L %u R %u bad %u",
                     widths[w], lo, hi, sum / 20000.0, left, right, bad);
            expect(bad == 0u, msg);
            if (widths[w] == 64u)
                expect(lo <= 4u && hi >= 250u &&
                       fabs(sum / 20000.0 - 127.0) < 4.0, msg);
            if (widths[w] == 127u)
                expect(left > 9000u && right > 9000u, msg);
        }
        rt.grain_width = 0u;
    }

```

##### X14.4 — (c) Position (new lines 1729–1762)

- **What:** at unity pitch, every Position value 0..127:
  - 63 → lag 2;
  - above 63 → lag = (uint) stop ticks × samples-per-tick, or no grain
    (`end` 0) if that is not recorded;
  - below 63 → lag inside [2, 2 + m·(N − 4) + 1].

```c
    /* (c) Position laws, at unity pitch (no headroom). */
    for (i = 0u; i < 128u; i++) {
        crumpbit_grain_t c;
        uint32_t hash = 0x777u + i;
        const uint32_t len = 60000u;
        const uint32_t ref = 1000u;
        uint32_t lag;

        rt.pan_raw = (uint8_t)i;
        rt.aux_raw = 63u;
        rt.grain_length = 63u;
        crumpBit_grainPick(&hash, &rt, len, ref, len, 0u, per_tick, &c);
        lag = (ref >= c.start) ? ref - c.start : ref + len - c.start;
        snprintf(msg, sizeof msg, "CB14 position %u lag %u end %u", i, lag,
                 c.end);
        if (i == 63u) {
            expect(lag == 2u && c.end != 0u, msg);
        } else if (i > 63u) {
            const float want = crumpBit_grainTime[crumpBit_grainStopCc[
                                   crumpBit_grainPosStop((uint8_t)i)]] *
                               per_tick;

            if (want <= (float)(len - 2u))
                expect(lag == (uint32_t)want && c.end != 0u, msg);
            else
                expect(c.end == 0u, msg);    /* not recorded: waits */
        } else {
            const float m = (float)(63u - i) / 63.0f;

            expect(lag >= 2u &&
                   (float)lag <= 2.0f + m * (float)(len - 4u) + 1.0f, msg);
        }
    }

```

##### X14.5 — (d) Read safety (the overwrite clamp) (new lines 1763–1800)

- **What:** 300,000 random picks: every row random, `valid` random,
  quarter frozen. For every sounding grain not on a frozen full line:
  - the start lag lies in [2, max];
  - the lag after `end` − 1 frames (lag0 + gr·(end − 1)) lies in
    [1, max + 1].

  Break 12 removes the clamp.

```c
    /* (d) No read reaches unrecorded or overwritten audio (Q3). */
    for (i = 0u; i < 300000u; i++) {
        crumpbit_grain_t c;
        uint32_t hash = rnd() | 1u;
        const uint32_t len = 1000u + rnd() % 62000u;
        const uint32_t valid = (rnd() & 1u) ? len : rnd() % (len + 1u);
        const uint32_t ref = rnd() % len;
        const uint32_t frozen = ((rnd() & 3u) == 0u);
        float r;
        float gr;
        float lag0;
        float lag_end;
        float max_lag;

        rt.aux_raw = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_st2 = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_st3 = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_spread = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_length = (uint8_t)(rnd() & 0x7Fu);
        rt.pan_raw = (uint8_t)(rnd() & 0x7Fu);
        rt.grain_width = (uint8_t)(rnd() & 0x7Fu);
        crumpBit_grainPick(&hash, &rt, len, ref, valid, frozen,
                           rndf(30.0f, 1500.0f), &c);
        if (c.end == 0u || (frozen && valid >= len))
            continue;
        r = (float)(c.inc < 0 ? -c.inc : c.inc) / 4096.0f;
        gr = (frozen ? 0.0f : 1.0f) + ((c.inc < 0) ? r : -r);
        lag0 = (float)((ref >= c.start) ? ref - c.start : ref + len - c.start);
        lag_end = lag0 + gr * (float)(c.end - 1u);
        max_lag = (float)((valid < len) ? valid : len) - 2.0f;
        snprintf(msg, sizeof msg,
                 "CB14 safety %u: lag %.1f -> %.1f max %.1f end %u r %.3f%s",
                 i, (double)lag0, (double)lag_end, (double)max_lag, c.end,
                 (double)r, frozen ? " frozen" : "");
        expect(lag0 >= 2.0f && lag0 <= max_lag + 0.01f &&
               lag_end >= 1.0f && lag_end <= max_lag + 1.0f, msg);
    }

```

##### X14.6 — (e) The copy identity (new lines 1801–1845)

- **What:** `grn`, mix 127, `fbk` 0, `rte` 0 (D = the `/64` stop), the
  default copy rows, random input for 3N + 4,096 frames.
  - From frame 32 + 128 + D + 64 on, every output sample (both channels)
    equals `rec[n − 2] + rec[n − 2 − D]` within 2·10⁻⁶: the recorded
    signal two frames late plus its delay-line echo.
  - This covers two retriggers: the crossfades must sum to 1.
  - Measured worst 1.2·10⁻⁷.
  - Break 10 makes the fades asymmetric.

```c
    /* (e) The copy identity (A1.9), across two retriggers. */
    {
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        const uint32_t len = CRUMPBIT_LOOP_MAX_BYTES / 2u;
        const uint32_t d = (uint32_t)(crumpBit_grainTime[0] * per_tick);
        const uint32_t total = 3u * len + 4096u;
        float *rec = calloc(total + 64u, sizeof(float));
        cb_dry_t dry = { 0.0f, 0.0f, 0.0f, 0.0f };
        uint32_t n = 0u;
        uint32_t bad = 0u;
        float worst = 0.0f;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 0u, &share);
        while (n < total) {
            float l[32], r[32];
            uint32_t k;

            for (k = 0u; k < 32u; k++) {
                l[k] = rndf(-0.9f, 0.9f);
                r[k] = rndf(-0.9f, 0.9f);
                rec[n + k] = cb_recorded(&dry, l[k], r[k]);
            }
            p3_block(&rt, &share, l, r, 32u);
            for (k = 0u; k < 32u; k++, n++) {
                /* After the first grain's fade-in and its first echo. */
                if (n >= 32u + 128u + d + 64u) {
                    const float want = rec[n - 2u] + rec[n - 2u - d];
                    const float err = fabsf(l[k] - want);

                    worst = (err > worst) ? err : worst;
                    bad += (err > 2e-6f || fabsf(r[k] - want) > 2e-6f);
                }
            }
        }
        snprintf(msg, sizeof msg,
                 "CB14 copy identity: %u frames off (worst %.3g)", bad,
                 (double)worst);
        expect(bad == 0u, msg);
        free(rec);
    }

```

##### X14.7 — (f) Density (new lines 1846–1909)

- **What:** Length shortest (no steals).
  - **`den` 100** (the `d16` stop): every interval between starts equals
    (uint)(36 ticks × samples-per-tick) exactly.
  - **`den` 20:** the mean interval within 10 % of N·`grainDensity[43]`.
  - **`den` 63:** no starts.
  - Starts are read from the voice ages: a voice with age ≤ 32 started in
    this block.

```c
    /* (f) Density: rhythmic intervals exact, random mean, off = none. */
    {
        static const uint8_t dens[3] = { 100u, 20u, 63u };
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        const float flen = (float)(CRUMPBIT_LOOP_MAX_BYTES / 2u);
        uint32_t t;

        for (t = 0u; t < 3u; t++) {
            uint32_t block;
            uint32_t starts = 0u;
            uint32_t last = 0u;
            uint32_t wrong = 0u;
            double sum = 0.0;
            float want_iv;

            memset(arena_p3, 0, sizeof arena_p3);
            p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
            p3_write(&rt, CRUMPBIT_PARAM_GRAIN_LENGTH, 63u, &share);
            p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY, dens[t], &share);
            want_iv = (dens[t] > 64u)
                ? crumpBit_grainTime[crumpBit_grainStopCc[
                      crumpBit_grainDenStop(dens[t])]] * per_tick
                : flen * crumpBit_grainDensity[63u - dens[t]];
            for (block = 0u; block < 60000u; block++) {
                float l[32], r[32];
                uint32_t k;

                for (k = 0u; k < 32u; k++) {
                    l[k] = rndf(-0.5f, 0.5f);
                    r[k] = l[k];
                }
                p3_block(&rt, &share, l, r, 32u);
                /* A voice started in this block has age <= 32. */
                for (k = 0u; k < 2u; k++) {
                    const uint32_t age = k ? rt.u.grn.age_b : rt.u.grn.age_a;
                    const uint32_t end = k ? rt.u.grn.end_b : rt.u.grn.end_a;

                    if (end != 0u && age <= 32u) {
                        const uint32_t at = block * 32u + 32u - age;

                        if (starts && dens[t] > 64u)
                            wrong += (at - last != (uint32_t)want_iv);
                        if (starts)
                            sum += (double)(at - last);
                        last = at;
                        starts++;
                    }
                }
            }
            snprintf(msg, sizeof msg,
                     "CB14 density %u: %u starts, mean %.1f (want %.1f), %u off",
                     dens[t], starts, starts > 1u ? sum / (starts - 1u) : 0.0,
                     (double)want_iv, wrong);
            if (dens[t] == 63u)
                expect(starts == 0u, msg);
            else if (dens[t] > 64u)
                expect(starts > 100u && wrong == 0u, msg);
            else
                expect(starts > 100u &&
                       fabs(sum / (starts - 1u) - want_iv) < 0.1 * want_iv,
                       msg);
        }
    }

```

##### X14.8 — (g) Freeze and the anchor (new lines 1910–1969)

- **What:**
  - Frozen at block 2000: the record half is bit-exact 2,000 blocks later,
    while the delay half changed.
  - The anchor equals the write position at the freeze, on every frozen
    block.
  - Unfreeze releases it.
  - A frozen Position-0 pick starts at anchor − 2.
  - Break 7 ignores freeze.

```c
    /* (g) Freeze keeps the record line bit-exact and anchors Position. */
    {
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        const uint32_t half = CRUMPBIT_LOOP_MAX_BYTES / 2u;
        uint8_t *snap_rec = malloc(half);
        uint8_t *snap_dly = malloc(half);
        float l[32], r[32];
        uint32_t block;
        uint32_t n;
        uint32_t anchor_ok = 1u;
        uint32_t frozen_at = 0u;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, 100u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 64u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY, 110u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_SPREAD, 100u, &share);
        for (block = 0u; block < 4000u; block++) {
            if (block == 2000u) {
                p3_write(&rt, CRUMPBIT_PARAM_SYNC, 1u, &share);
                memcpy(snap_rec, arena_p3, half);
                frozen_at = rt.u.grn.write_pos;
            }
            if (block == 2001u)
                memcpy(snap_dly, arena_p3 + half, half);
            for (n = 0u; n < 32u; n++) {
                l[n] = rndf(-0.8f, 0.8f);
                r[n] = rndf(-0.8f, 0.8f);
            }
            p3_block(&rt, &share, l, r, 32u);
            if (block >= 2000u)
                anchor_ok &= (rt.u.grn.anchor == frozen_at);
        }
        expect(memcmp(snap_rec, arena_p3, half) == 0,
               "CB14 freeze: record line bit-exact");
        expect(memcmp(snap_dly, arena_p3 + half, half) != 0,
               "CB14 freeze: delay line still running");
        expect(anchor_ok, "CB14 freeze: anchored at the freeze position");
        p3_write(&rt, CRUMPBIT_PARAM_SYNC, 0u, &share);
        p3_block(&rt, &share, l, r, 32u);
        expect(rt.u.grn.anchor == CRUMPBIT_GRAIN_NO_ANCHOR,
               "CB14 unfreeze releases the anchor");
        /* Anchored picks: Position 0 starts two behind the anchor. */
        {
            crumpbit_grain_t c;
            uint32_t hash = 99u;

            rt.pan_raw = 63u;
            rt.aux_raw = 63u;
            crumpBit_grainPick(&hash, &rt, half, frozen_at, half, 1u,
                               per_tick, &c);
            expect(c.start == ((frozen_at >= 2u) ? frozen_at - 2u
                                                 : frozen_at + half - 2u),
                   "CB14 frozen Position 0 = anchor - 2");
        }
        free(snap_rec);
        free(snap_dly);
    }

```

##### X14.9 — (h) Stability (new lines 1970–1990)

- **What:** four 20 s passes at `fbk` 127, `spr` 127, `wid` 127, extreme
  Lengths (both directions), Densities and pitches: finite and < 8.

```c
    /* (h) Stability at fbk 127 over sizes, directions and pitches. */
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
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_LENGTH,
                 (uint8_t)((s & 1u) ? 0u : 127u), &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY,
                 (uint8_t)((s & 2u) ? 20u : 120u), &share);
        p3_write(&rt, CRUMPBIT_PARAM_AUX, (uint8_t)(s * 40u), &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_SPREAD, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_WIDTH, 127u, &share);
```

#### X15 — CB16: `bal`

**Location:** `tools/dsp_test/modules/crumpbit.c`, **add** after HEAD line 1791. New lines 2134–2192 (59 lines).

- **What:** 24 passes over the seven filter types, random `res`, cutoff f
  and balance d (|d| ≤ 20). Three runtimes run `dly` at mix 0 (the output
  is the dry signal through stage 4):
  - A: f with balance 63 + d;
  - B: f − d with balance 63;
  - C: f + d with balance 63.

  Over 400 blocks, A's left equals B's left and A's right equals C's
  right, bit for bit.
- **Why:** plan §A6.2. Break 11 swaps a side.

Add:

```c
/* CB16: `bal` (S091 P4) ------------------------------------------------------ */
static void law_cb16(void)
{
    static uint8_t arena_bal[3][4096];
    uint32_t pass;
    char msg[128];

    for (pass = 0u; pass < 24u; pass++) {
        /* A: frq f, bal 63 + d; B: frq f - d, bal 63; C: frq f + d. */
        const uint8_t type = (uint8_t)(pass % 7u);
        const int32_t d = (int32_t)(rnd() % 41u) - 20;
        const int32_t f = 30 + (int32_t)(rnd() % 68u);
        const uint8_t res = (uint8_t)(rnd() % 120u);
        CrumpBitRuntime rt[3];
        fx_share_t share[3];
        const uint8_t freq[3] = { (uint8_t)f, (uint8_t)(f - d),
                                  (uint8_t)(f + d) };
        const uint8_t bal[3] = { (uint8_t)(63 + d), 63u, 63u };
        uint32_t bad = 0u;
        uint32_t block;
        uint32_t k;

        for (k = 0u; k < 3u; k++) {
            share[k].base = arena_bal[k];
            share[k].offset = 0u;
            share[k].bytes = sizeof arena_bal[k];
            p3_init(&rt[k], CRUMPBIT_SUBTYPE_DELAY);
            p3_write(&rt[k], CRUMPBIT_PARAM_FILTER_TYPE, type, &share[k]);
            p3_write(&rt[k], CRUMPBIT_PARAM_FILTER_FREQ, freq[k], &share[k]);
            p3_write(&rt[k], CRUMPBIT_PARAM_FILTER_RESO, res, &share[k]);
            p3_write(&rt[k], CRUMPBIT_PARAM_FILTER_BAL, bal[k], &share[k]);
        }
        for (block = 0u; block < 400u; block++) {
            float l[3][32], r[3][32];
            uint32_t n;

            for (n = 0u; n < 32u; n++) {
                const float a = rndf(-1.0f, 1.0f);
                const float b = rndf(-1.0f, 1.0f);

                for (k = 0u; k < 3u; k++) {
                    l[k][n] = a;
                    r[k][n] = b;
                }
            }
            for (k = 0u; k < 3u; k++)
                p3_block(&rt[k], &share[k], l[k], r[k], 32u);
            for (n = 0u; n < 32u; n++) {
                bad += !same_bits(l[0][n], l[1][n]);
                bad += !same_bits(r[0][n], r[2][n]);
            }
        }
        snprintf(msg, sizeof msg,
                 "CB16 bal pass %u type %u frq %d d %d: %u samples differ",
                 pass, type, f, d, bad);
        expect(bad == 0u, msg);
    }
}

```

#### X16 — `main()`: CB16

**Location:** `tools/dsp_test/modules/crumpbit.c`, **add** after HEAD line 1808. New lines 2210–2210 (1 lines).

- **What:** calls `law_cb16()` after CB15.

Add:

```c
    law_cb16();
```

---

## 5. Gates

Run in order on the implemented tree. Copy the HEAD image's ELF to
`build/s091p4_base.elf` first, for G6.

| # | Command | Expected |
|---|---|---|
| G1 | `make all` (clean) | exit 0. **Compare every warning of the clean build with the baseline's**, not only the changed files' (the S091 P3 lesson): the same 20, none in a changed file |
| G2 | `make img` | `Written:build/LXRV2_lxr02.img (709860b) OK` (± a few bytes if the tree differs from HEAD) |
| G3 | `make -C tools/dsp_test crumpbit` | `crumpbit failures: 0`; `crumpbit_break1`…`12: caught`; CB8 peaks 1.000; CB15 decay ≈ 15.35 dB, freeze ≈ 2.04 dB |
| G4 | `make -C tools/dsp_test crumpbit_tables` | `crumpbit tables OK` |
| G5 | `make -C tools/dsp_test crumpbit_elf_check` | `runDelay` 124, `runComb` 124, `runPitch` 135, `runGrain` 136 and 133, `runReverb` 145, `process` 31, all vdiv 0; `divisionFor` 113; `tableLimit` 63 |
| G6 | `elf_loopdump.py --sym mixer_calcNextSampleBlock` on base and new, `elf_hotloops.py --compare` | `39 hot loops equal, 0 shrank, 0 grew`; normalised dumps identical (86) |
| G7 | `link_budget.py` (run by `make all`) | ITCM ≈ 15,328 B; DTCM statics 4,392 B; **FXBUF ≥ 126,656 B** (126,656 B at `0x20001140` in the measured build) |
| G8 | `arm-none-eabi-size build/lxr02.elf` | text ≈ 709,440; data 420; bss ≈ 413,016 |
| G9 | `elf_looppath.py --sym calcNextOscSampleSyncBlock --top 4` | 11/9 and 9/9 |
| G10 | `make -C tools/dsp_test all` | every module as on HEAD (the `voice_control` second build's 6 failures and `run_modes`' compile error are pre-existing) |

**Hardware checks (user).** Keep the monitor level low for H3–H6.

| # | Check | Expected |
|---|---|---|
| H1 | The worst-case Scene with `cbt` in `grn`, Density fast (e.g. `den` 120), `lng` long, `spr` and `wid` up; `cpu` widget and DEV `UNR` | at or below the previous worst case; `grn` ≈ +9 per frame over S091 P3 (≈ 0.25 % of the CPU); no underruns. Repeat once per other sub-type: as S091 P3 |
| H2 | Switch to `grn` on a fresh Scene (defaults), `mix` 127, `fbk` 0 | **the copy:** the input (mono, 8-bit) at once, with no gap at the 1.44 s retriggers, plus one echo at `tim` (default `/8`); Width 0 centred at full level |
| H3 | `pos`: centre → right (`/64` … `1br`) → left (scatter) | right: a clean single delay at the shown division (it starts once that much is recorded); left: random positions, the whole buffer at full left |
| H4 | `lng`: full right → centre → full left; with `frz` on and off | long → short grains; left plays reversed. Full-left while recording plays about half the buffer before re-reading; with `frz` the whole frozen buffer reversed |
| H5 | `den`: `off`, `buf`, rhythmic stops, negative | silence (playing grains finish); one grain per buffer; rhythmic retriggers on the shown division; random clouds, denser to the left. Dense + long: steals fade (no clicks) |
| H6 | `p1 p2 p3`, `spr` 0/40/80/127, `wid` 0/64/127 | P1 only at 0, mixes up to thirds, detune above 80; mono → wide random → hard sides |
| H7 | `bal` with `typ` LP/HP and `res` up | the cutoff moves apart per side (right up for positive); 0 = as before |
| H8 | Pages in every sub-type | SELECT 1 s1 `mrp run len scl` (also on `flt`); overlay `mrp frq res bal`; `typ frq res bal`; `mix sub …` then the sub-effect's controls in the A6.3 order; `grn` continues on SELECT 3 |
| H9 | `.fx` save/load of a `grn` Scene with lanes; AutoSave reboot; an older `.fx` | the `grn` lanes (`den tim frz pos lng p1 spr`) reload; an older file loads with Length 127, Density as stored, `bal` 0 (its `crump_grain_amp` line ignored) |

---

## 6. Documentation changes (same step)

### D1 — `tools/dsp_test/DSP_TEST.md`

- **The `crumpbit` row:**
  - CB6 adds the `grn` time curve and its stops, the Length and Density
    tables, and the Position clamp;
  - CB9 adds `bal` shared and lane-less, and the `grn` pool;
  - CB11 adds the scripted seat law;
  - CB14 is "`grn` P4 model: pitch slots (absolute), Width, Position,
    read safety, copy identity, Density, freeze anchor, stability";
  - new: CB16 `bal`;
  - twelve break builds.
- **The `crumpbit_elf_check` row:** `grn` 136 with `--cond-heads
  --in-range`.
- **The `elf_looppath.py` paragraph:** `--in-range` (a loop's search stays
  between its head and its furthest closing branch).

### D2 — `EFFECTS_MIXER_DSP_REFERENCE.md`

- **§4.2:** 100 B (CrumpBit: 56 B shared + a 44 B sub-engine union).
- **§4.4a, the `grn` bullet:** replace with the P4 model:
  - two voices triggered by Density (bipolar: off, +1 per line, rhythmic,
    random);
  - Length bipolar up to N (reversed left);
  - Position (0 follow, rhythmic lag, random scatter);
  - flat-top windows with 128-frame complementary fades (the copy
    identity);
  - steal-oldest with a deferred trigger;
  - the overwrite clamp;
  - the anchored freeze;
  - absolute P1–P3 with ±1 st detune; Width;
  - the delay line's own time on a 6..384-tick curve;
  - the loop 136, ≈ 201 per frame worst case.
- **§4.4, the output filter:** add `bal` (right cutoff index frq + b, left
  frq − b, per-channel coefficients, bit-identical at 0).
- **§6.2a:** `grn` 201, the others +0.7.

### D3 — `EFFECTS_BUS_REFERENCE.md`

- **§4.2a row 2:**
  - rows 23;
  - the `grn` lanes `den tim frz pos lng p1 spr`;
  - `bal` lane-less;
  - runtime 100 B.
- **§8.6:**
  - the new page table (A6.1/A6.3 of the plan: SELECT 1 s1
    `mrp run len scl`; overlay `mrp frq res bal`; `typ frq res bal`;
    `mix sub …`);
  - the per-sub-type orders;
  - the `grn` names `tim frz pos lng den p1 p2 p3 spr wid`;
  - the labels: delay time stop or number; Position stop; Density
    `off`/`buf`/stop.
- **The SELECT 1 description** (§8, the manager pages): screen 1 is
  `mrp run len scl` for every type.
- **Defaults:** S091 P4 adds `grn` den 64 (+1), lng 127; `bal` 0.
- Also fix the S091 P3 stale lines listed in `S091_P3_Implementation.md`
  §10.3, in the same pass.

### D4 — `STORAGE_SRAM_MANIFEST.md`

- **Line 697:** `effects_runtime` 100 B (CrumpBit 56 B shared + 44 B union;
  +24 B of the 32 B approval).
- **The arena paragraph:** FXBUF 126,656 B at `0x20001140` in the S091 P4
  build (the boundary was crossed, as N1 assumed).

### D5 — `CPU_USE_DSP_AUDIT.md`

After item 63, add:

```markdown
64. **CHANGED (S091 P4): CrumpBit `grn` rework, filter balance.** Grains
    triggered by Density independently of Length, flat-top windows with
    exact 128-frame crossfades, steal-oldest, per-grain overwrite clamp,
    anchored freeze, own delay-time curve; shared `bal` (per-channel
    output-filter coefficients).
    - Linked worst case per frame: `grn` 131 -> 136 loop, ~192 -> ~201
      whole Effect; `dly`/`cmb`/`pit`/`rvb` +0.7 (`bal`). No per-sample
      VDIV.
    - RAM +12 B (runtime 100 B); text +2,840 B.
    - Host CB1-CB16, twelve breaks.
```

### D6 — `MEMORY.md` (after the hardware result)

A status line. Under **Binding Rules → Effects**, extend the CrumpBit line:
`grn` (S091 P4) triggers by Density, plays Length, reads at Position, the
copy setting is the identity, and `bal` is shared.

---

## 7. Optional reductions (not in this schedule)

If H1 shows `grn` too heavy:

| # | Change | Saving (per frame) | Cost |
|---|---|---|---|
| O1 | Window: clamp only below 0, with the `in` ramp saturating at 1 when written (per block) | ≈ −2 | the ramp needs a per-block clamp; exactness of the copy fades kept |
| O2 | Position limit computed in the DSP only when tempo, share or `dpn` change (cached) | ≈ −2.4 average, 0 worst | 2 B cache; the worst case (an LFO on `dpn`) unchanged |
| O3 | Drop the per-block retry for random Density (a missed grain waits for the next interval) | ≈ −0.5 | a rare skipped grain while recording starts |
| O4 | Mono grains (no per-voice pan) | ≈ −4 | Width loses its meaning |

---

## 8. Risks and notes

- **The one-event-per-block bound.** The two-span loop relies on one
  trigger or steal per block. The minimum trigger interval
  (`CRUMPBIT_GRAIN_MIN_PERIOD`) and the "Density waits for the deferred
  trigger" rule keep it. A larger render block needs the period to equal
  the block.
- **Steals.** A dense, long setting steals on most triggers. Each steal
  defers its grain by 128 frames (2.9 ms), so a rhythmic Density with
  overlapping long grains drifts by that much per steal. A third voice (≈
  +17 per frame) would remove it.
- **Freeze with a partial line.** While frozen with `valid` < N, reads
  stay inside the recorded part, measured from the anchor. A full frozen
  line wraps freely.
- **The arena** crossed the 32 B boundary in this build (FXBUF 126,656 B
  at `0x20001140`), exactly what the N1 constants assume. Nothing in
  CrumpBit depends on which side of the boundary LTO lands.
- **`.fx` files.**
  - The `crump_grain_amp` key no longer exists; its line is ignored.
  - Row 20's new key `crump_grain_length` and row 22's `crump_filter_bal`
    take their defaults in older files.
  - Lane maps follow the stored `sub` (S091 P3), so an older `grn` Scene's
    lanes 4/5/10 now drive Density, delay time and Length (no backward
    compatibility, user rule).

---

## 9. Implementation notes (2026-11-05 session)

The tree was at `cc8ca99a` (docs only over `b0ed5701`); every firmware file
matched the schedule's HEAD. The work below followed §3–§6 in order.

### 9.1 Code application

- **Method.** The 84 code units (H1–H6, E1–E18, P1–P6, C1–C10, B1–B4,
  M1–M2, U1–U2, G1–G8, L1–L4, T1–T8, X1–X16) were extracted from this
  schedule by a scratch script and applied mechanically: each unit's
  `Remove (HEAD ...)` text was compared byte for byte with the current file
  before replacement (`errors: 0`). E7, C10 and X14 give their removal as
  an exact line range only, as the schedule says.
- **Multi-block units.** E7's five sub-blocks were concatenated in order
  (into `crumpBit_runGrain()`'s 647 lines) and X14's nine sub-blocks into
  `law_cb14()` (406 lines). The first extraction pass took only E7.1 and
  X14.1; the scripts were re-run from a clean `git archive HEAD` copy after
  fixing the sub-block collection, so the final tree has all blocks.
- **No manual code edits.** The applied text is the schedule's, including
  every comment block. Verified files: `CrumpBitEffect.c/.h`,
  `CrumpBitParameters.c/.h`, `CrumpBitTables.h`, `EffectsManager.c`,
  `menuEffects.c`, `gen_crumpbit_tables.py`, `elf_looppath.py`,
  `tools/dsp_test/Makefile`, `modules/crumpbit.c`.
- **Generated table (R1).** `python3 tools/plots/gen_crumpbit_tables.py`
  wrote the 353-line `CrumpBitTables.c`; `--check` prints `crumpbit
  tables OK`. SHA-256 is exactly the schedule's
  `193f047cd379ecb9f9b724152ca7d50a650d35b34c2e778f3b78a28f2d3f7f18`.

### 9.2 Gates (run in this tree)

| Gate | Result |
|---|---|
| G1 `make all` (clean) | exit 0. 20 warnings in both baseline and new logs; the only textual difference is LTO's `15` vs `16` LTRANS jobs. None in a changed file. |
| G2 `make img` | `Written:build/LXRV2_lxr02.img (709860b) OK` |
| G3 `make -C tools/dsp_test crumpbit` | `crumpbit failures: 0`; CB8 peaks 1.000; CB15 15.35 dB decay / 2.04 dB freeze; breaks 1–12 all caught |
| G4 `crumpbit_tables` | `crumpbit tables OK` |
| G5 `crumpbit_elf_check` | `runDelay` 124, `runComb` 124, `runPitch` 135, `runGrain` 136 and 133 (`--cond-heads --in-range`), `runReverb` 145, `process` 31, all vdiv 0; `divisionFor` 113; `tableLimit` 63 |
| G6 mixer loops | baseline built from `git archive HEAD`: same ELF sizes as §2; `39 hot loops equal, 0 shrank, 0 grew`; 86/86 dumps |
| G7 link budget | ITCM 15,328 B; DTCM statics 4,392 B; FXBUF 126,656 B at `0x20001140`; flash 709,860 / 753,664 B, headroom 43,804 B |
| G8 `arm-none-eabi-size` | text 709,440; data 420; bss 413,016 |
| G9 oscillator | `calcNextOscSampleSyncBlock` 11/9 and 9/9 |
| G10 `make -C tools/dsp_test all` | same as HEAD: `voice_control` second build 6 failures, `run_modes` compile error; every other module 0 failures |

The break-11 build emits one `-Wunused-variable` (`fl`) because the break
removes its use; the production tree is warning-free in that file. G10's
failures are the pre-existing ones named in §2.

### 9.3 Documentation (D1–D5)

- **D1** `tools/dsp_test/DSP_TEST.md`: `crumpbit` CB1–CB16 and the P4
  laws, twelve breaks; `crumpbit_elf_check` `grn` 136 with
  `--cond-heads --in-range`; the `--in-range` paragraph.
- **D2** `EFFECTS_MIXER_DSP_REFERENCE.md`: runtime 100 B; the `grn` P4
  model bullet; `bal` in the output-filter bullet; arena 126,656 B;
  runtime union 44 B; §6.2a `grn` ≈ 201 and the others +0.7.
- **D3** `EFFECTS_BUS_REFERENCE.md`: registry row 23 / 100 B / the `grn`
  lane pool; SELECT 1 s1 `mrp run len scl`; the §8.6 page table; the
  per-sub-type orders, names and labels; the `grn` defaults. The S091 P3
  §10.3 stale lines were fixed in the same pass (comb 8.4..2,141,
  `crumpBit_subtypeRateLimit()`).
- **D4** `STORAGE_SRAM_MANIFEST.md`: `effects_runtime` 100 B; FXBUF
  126,656 B at `0x20001140`.
- **D5** `CPU_USE_DSP_AUDIT.md`: item 64.
- **D6** `MEMORY.md`: deliberately not applied; the schedule puts the
  status line after the hardware result, and H1–H9 are still open.

### 9.4 Open (unchanged by this session)

- Hardware H1–H9 (§5): H1 is the CPU decision for `grn` ≈ 201 per frame,
  H2–H9 cover the copy setting, Position/Length/Density/Width, `bal`, the
  pages and `.fx`/AutoSave.
- `make -C tools/dsp_test all`'s `voice_control` and `run_modes` failures
  are pre-existing on clean HEAD.

---

## 10. Assessment (2026-11-05)

**Verdict:** the implementation is the schedule, byte for byte. No firmware
file outside the schedule changed, and every static gate reproduces the
schedule's numbers in an independent rebuild. Two reference-doc lines are
still stale; hardware H1–H9 is open.

### 10.1 Code against the schedule

- **All 12 scheduled files are byte-identical** to the scratch build the
  schedule was generated from (`cmp` of the working tree against it):
  - `CrumpBitEffect.c/.h`, `CrumpBitParameters.c/.h`,
    `CrumpBitTables.h/.c`;
  - `EffectsManager.c`, `menuEffects.c`;
  - `gen_crumpbit_tables.py`, `elf_looppath.py`, `tools/dsp_test/Makefile`
    and `modules/crumpbit.c`.
- **This agrees with §9.1:** the units applied by script against their
  `Remove` text, and R1's SHA-256 matches.
- **The first extraction pass** took only the first sub-block of E7 and
  X14. §9.1 records it, and the re-run from a clean archive shows in the
  byte-identical result: the multi-block units arrived whole.
- **No firmware change outside the schedule.** `cc8ca99a` adds only
  documents over `b0ed5701`, and the working tree's other changes are the
  scheduled docs and the rebuilt image.

### 10.2 Independent rebuild and gates

I copied the working tree (without `.git` and `build/`) into the
scratchpad, rebuilt it clean, and re-ran every gate:

| Gate | Result |
|---|---|
| G1 `make all` | exit 0; 20 warnings, the baseline's 20; **none in a changed file** |
| G2 `make img` | `Written:build/LXRV2_lxr02.img (709860b) OK`; **byte-identical** to the repository's image and to the schedule's verified image |
| G3 `crumpbit` | 0 failures; CB8 peaks 1.000; CB15 15.35 dB / 2.04 dB; breaks 1–12 caught |
| G4 `crumpbit_tables` | OK |
| G5 `crumpbit_elf_check` | 124 / 124 / 135 / 136 + 133 / 145 / 31, vdiv 0; `divisionFor` 113; `tableLimit` 63 |
| G6 mixer loops | 39 hot loops equal, 0 grew; 86/86 normalised identical (against the HEAD baseline ELF) |
| G7/G8 link | text 709,440, data 420, bss 413,016; flash 709,860 (headroom 43,804); ITCM 15,328; DTCM statics 4,392; FXBUF 126,656 B at `0x20001140` |
| G9 oscillator | 11/9, 9/9 |

§2 therefore stands for the implemented image. Worst case per frame for
the whole Effect:
- `grn` ≈ 201 (P3 192, the user's ceiling ≈ 195);
- `dly` 172.5, `cmb` 168.5, `pit` 181.9, `rvb` 193.1.

### 10.3 Documentation (D1–D5)

D1–D5 are applied as scheduled and read accurately against the code. The
`grn` model, `bal`, the pages, the lane pool, the 100 B runtime, the
126,656 B arena and the costs all match.

The implementer also fixed most S091 P3 §10.3 lines in the same pass:
- the comb range;
- `crumpBit_subtypeRateLimit()`;
- `min(share bytes, 126,656)`;
- the page table, the layout bullet and the defaults;
- the `grn` windows.

D6 (`MEMORY.md`) correctly waits for the hardware result.

Two lines in `EFFECTS_BUS_REFERENCE.md` are still stale:

| Line | Now | Should be |
|---|---|---|
| 69 | "the whole share up to 126,688 B for its delay loop" | "up to 126,656 B (`CRUMPBIT_LOOP_MAX_BYTES`)" |
| 535 (hooks table, `format_target3`) | "CrumpBit's `sub` names, and its per-sub-type `rte` text" | "CrumpBit's `sub` names only (the hook returns 0 for every other row)" |

Neither affects code or gates.

### 10.4 Open

- **Hardware H1–H9 (§5).**
  - H1 decides the CPU question: `grn` ≈ 201 per frame, ≈ 9 over S091 P3.
  - H2 is the copy setting (seamless 1.44 s retriggers, unity level).
  - H8 covers the pages, including the manager's `mrp run len scl` on
    every Effect type.
- **The two doc lines of §10.3.**
- **D6 after hardware.**
