# S095 P2 — Colloid: implementation schedule

- **Status:** steps 0-2 implemented and host-gated (2026-11-12); steps 3
  and 4 need the device and your ear (§9.3). See §9 for the notes kept at
  each step. Written 2026-11-12 from
  `S095_P2_DRAFT_COLLOID.md` (the plan; its §H wins over its body) with every
  user decision C-D1…C-D21 in force (§A).
- **Tree:** branch `dev-ph7-alkaline`, `b7f7eec4` ("s095 p1 space fx in and
  test"), clean. **Every line number below is a line of that tree**, quoted
  with its text. Step 0 touches none of the files that steps 1 and 2 touch,
  so the numbers hold through the whole schedule. Within one file, apply the
  edits **from the bottom up** (or find each by its quoted anchor): an
  insertion shifts the lines below it.
- **Verified before hand-over (§C):** every change in this document was
  applied to an export of `b7f7eec4` in a scratch directory (`git archive`,
  so the repository was not touched), built with `make all`, linked, gated
  with `elf_p4_check`'s tools and run through `make -C tools/dsp_test all`.
  The code in this document is the code that was built: the new files are
  quoted whole, and the edits were applied by a script that checked each
  anchor's line number and text first. What only the device or your ear can
  settle is listed in §D steps 3 and 4.
- **Audience:** the implementing session. It reads `MEMORY.md`, the draft's
  §H and this document, then works through §D in order.

---

## A. Decisions in force

| # | Decision (user) | Where it lands |
|---|---|---|
| C-D1 | Basic class | registry flags `INSTRUMENT_FLAG_BASIC` (S1) |
| C-D2 | Oscillator count by measurement: 12 plain / 6 PWM, else 10 plain | `COL_OSC_COUNT` (`ColloidVoice.h`), decided in step 3 |
| C-D3 | Gate = measured cycles per block on the device | step 3 |
| C-D4 | Wavetable frames latched at each non-legato trigger | `col_wtbTrigger()` |
| C-D5 | Spread = frames within the selected table | `col_wtbTrigger()` |
| C-D6 | No PWM on a wavetable frame | `pwm` reads `sawTable` only |
| C-D7 | No dense (truncated) option | not built |
| C-D8 | No gate length; an automatable sustain switch `sus` | `col_sustain` row, `col_egStep()` |
| C-D9 | Per-sample coefficient ramps + per-block cutoff slew | `col_svfControl()`, render loops |
| C-D10 | 1,024-point sine across the board, as step 0 | §1 |
| C-D11 | The mixer's own per-slot pan on the stereo pair | `mixer_addVoiceStereo*()` (S2) |
| C-D12 | 1/√N level into the drive (by ear) | `COL_NORM_WAV/PWM` |
| C-D13 | Linear mid/side pan law | the pair loops + `L = M + 2S` |
| C-D14 | w(f) by absolute frequency, 80 / 640 Hz (by ear) | `col_oscControl()` |
| C-D15 | `mot`: depth 0.6 √mot, rate 0.05→2 Hz, own rate ratios, parabolic sine (by ear) | `col_oscControl()` |
| C-D16 | `spr`/`mot` no effect in series, shown `---` | `colloidParameters_cellInactive()` |
| C-D17 | One bipolar `fam` on both cutoffs | `Colloid_calcVoiceAsync()` |
| C-D18 | Velocity only through the `dst` targets | the velocity rows |
| C-D19 | Sustain holds only while the transport runs | `susOn = sus && seq_isRunning()` |
| C-D20 | Platform `typ`/`wav`/`pos` shape; one spread row `psp` | the OSC rows, `waveDomainMax`, `menu_formatWaveCell()` |
| C-D21 | PWM: the Rec pulse only | `COL_PWM_WAVE_COUNT` 1 |

Budgets approved 2026-11-12 (draft §H.3): flash ≤ +8,192 B net against
711,036 B; ITCM ≤ +1,024 B against 14,032 B; RAM 0 B new; CPU ≤ 19,620
cycles per block (Basic), device-measured.

## B. Where this schedule departs from the draft, and why

Implementation choices (the draft's H.6: the implementer's), each measured or
checked in §C:

1. **No separate spike.** The draft's step 1 (a scratch spike) and step 2 (a
   device run of a stub) are replaced by the scratch build of this whole
   schedule (§C): the loops, the per-block paths and the mixer gate were
   measured on a linked image of the real code. The device run (step 3) is
   then done on the real voice, not a stub.
2. **A 15-bit interpolation fraction**, not 16 bits (draft §2.1). A user
   wavetable frame can step by 65,535 between points; 65,535 × 65,535
   overflows int32, 65,535 × 32,767 does not. Same instruction count.
   The host law CO-OSC catches a 16-bit fraction (break `frac`).
3. **Pair loops only.** Every count is even (12, 10, 6), so the draft's
   "single" loops are not built (ITCM 848 B for four loops).
4. **No transport-edge flag** (draft §4): with C-D19 the effective switch is
   `sus && seq_isRunning()`, read once per block. A stop releases a held note
   on the next block because the sustain stage sees the switch off. That
   covers the stop edge, stopped-transport hits and `sus` automation with
   one rule, and no state.
5. **The glide uses `colEgK[]`** (rows 0..105 = 1 ms..2 s) instead of a table of
   its own.
6. **A local tangent** `col_tanf()` (the `fastTan()` [3/2] Padé form, one
   VDIV). `fastTan()` is an ITCM function with no header declaration.
7. **Series drive:** the drive gain is applied once, before filter A; before
   filter B only the soft clip runs (it bounds A's resonant peak).
8. **No zero row for an empty catalogue:** `wtb` with no table sets the
   block's input gain to 0 (silence, with the filters still running) instead
   of a 2,048 B silent row.
9. **`---` in the compact cell only** (`menu_formatCellValue3()`); the
   single-parameter view still shows the stored value.
10. **The motion bound is 0.01 of the field per block** (host law CO-MOT), not
    the draft's 0.005: a parabolic sine at 2 Hz × 1.37 with depth 0.6 moves up
    to 0.6 × 8 × 2.74 / 1,378 = 0.0095 per block.
11. **Envelope times and glide are bytes** (`TYPE_UINT8`, the `colEgK` index),
    so the async needs no float-to-index conversion; an LFO or velocity
    overlay still writes them in the 0..127 domain.
12. **`Colloid_initVoice()` is `noinline, noclone`:** without it LTO kept two
    688 B copies (an ipa-split `.part` clone), measured.
13. **`pos` under `wav` and `psp`:** C-D20's one spread row is
    `col_pos_spread` (`psp`).

## C. Verification record (scratch export of `b7f7eec4`, 2026-11-12)

| Check | Result |
|---|---|
| Baseline `make all` | flash 711,036 B, ITCM 14,032 B: as the draft's §H.2 |
| Step 0 `make all` | flash **704,892 B (−6,144 B)**; ITCM 14,032 B; `sine_table` 2,050 B |
| Step 0 loops | the sine readers' loops unchanged in length; the five PWM sine-warp loops (`osc_pwmWs*`) **1 shorter** each; the mixer 59 hot loops equal |
| Step 0 host | `make -C tools/dsp_test all` exit 0 (47 break builds caught) |
| Step 1 `make all` | flash **713,572 B (+8,680 B; running +2,536 B)**; ITCM **14,880 B (+848 B)**; mixer 58 equal, 1 shrank, 0 grew |
| Step 2 `make all` | flash **716,820 B (+3,248 B; running +5,784 B)**; mixer **55 equal, 4 shrank, 0 grew**. *Corrected during implementation (§9.1): this check compared loop text only and missed real growth. Compared by length, the step 1 image had ten Drum-family PWM loops 1-3 longer and the step 2 image `osc_smpFmBlock` +1. With the Colloid engine compiled `-fno-lto` (§9.1) every oscillator loop is equal, at 714,116 B (step 1) and 717,372 B (step 2).* |
| RAM | `.data` 420 B and `.bss` 419,564 B unchanged; `runtime_slots` 7,584 B unchanged; `sizeof(ColloidVoice)` **1,032 B** (≤ 1,264, asserted); `menu_instrumentLoadIndex[]` 10 → 12 B inside existing padding |
| Host | `make -C tools/dsp_test all` exit 0: `special tags OK (249 rows, 0 mismatches)`, `init.col matches the table (47 rows)`, the Colloid's CO-OSC…CO-WTB 0 failures, its 8 break builds caught, `ColloidTables.c matches` |
| `COL_OSC_COUNT` 10 | compiles; every host law passes |
| Linked counts (`colloid_cpu.py`, voice only) | plain 12 par **309.5** (loops 243 + 2,129/32), ser 299.5; plain 10 par **271.5** (213 + 1,873/32), ser 261.5; pwm 6 par **256.1** (213 + 1,380/32), ser 246.1 |
| Linked loops | `col_oscPairStore` 27, `col_oscPairAdd` 30, `col_pwmPairStore` 47, `col_pwmPairAdd` 50, `col_renderPar` 66, `col_renderSer` 56; the oscillator-control loop 82–116 per oscillator per block; the mixer's stereo dry loop 31 (stereo routes) / 21 (one side), stereo dry + send 41–47 |

**Reading the counts (estimate, the device decides):** the Alkaline's worst,
276.9 instructions per sample, measured 19,609 cycles with its send against
19,620. At about 1.85 cycles per instruction without the Alkaline's three
VDIV per sample, 12 plain oscillators (309.5 + the send) **probably exceed**
the Basic share, and 10 (271.5) and 6 PWM (256.1) **probably fit**. Step 3
measures it. The per-oscillator control (82–116 per oscillator per block,
about 3 per sample each) is the largest lever if the device needs one: the
motion and position work is about half of it.

## D. Order of work, gates and running totals

| Step | What | Gate (all pass before the next step) | Running net flash (measured) |
|---|---|---|---|
| **0** | 1,024-point sine (§1) | `make all`; `make -C tools/dsp_test all` exit 0; `elf_p4_snapshot` taken **before** the step (`P4BASE=baselines/s095p2/pre`), then the step's oscillator loops compared **by length** (`elf_looppath.py` on the changed symbols: equal or shorter) and the mixer `elf_hotloops.py --compare` 0 grew; flash −6,144 B; one listening check (a Drum `sin`, an FM carrier, a sine LFO on pitch) | **−6,144** |
| **1** | The Colloid engine, type, parameters, pages, `init.col`, build, host bench (§2). The mixer is unchanged: the voice plays its left block through the mono path | `make all` with no new warning; `make -C tools/dsp_test all` exit 0 (special tags, `init.col`, CO-* laws, 8 breaks, tables); mixer 0 grew; ITCM ≤ 15,056 B; on the device: the boot special-tag self-check reads 0, Instrument Load lists `Colloid` (copy `SD_CARD/Instrument/Colloid/` to the card), a Colloid plays, a Scene save and load keeps it | **+2,536** |
| **2** | The mixer's stereo path (§3) | `make all`; mixer 0 grew; host law MX-ST (§3.3); on the device: `spr` and `mot` move the image; a send into Delay/Reverb/Echo is stereo, into CrumpBit mono | **+5,784** (≤ +8,192) |
| **3** | Device CPU, C-D2 (§4) | the probe's Colloid slot ≤ 19,620 cycles per block, cold, `fmd par`, a stereo send, at the chosen count | unchanged, or −(10-oscillator saving) |
| **4** | A Scene of Colloids and Alkalines; underruns; listening (§5) | `cpu` widget, no underruns; the by-ear values recorded | unchanged |

Records go in `tools/dsp_test/baselines/s095p2/` (not `build/`): the
pre-step-0 P4 snapshot, the link budget after each step, the
`colloid_cpu.py` output after step 2 (`colloid_cpu.txt`, the regression
baseline for `COL_BASE`), and the device probe decode of step 3.

## E. Rules for the implementing session (standing, restated)

- `MEMORY.md` and `knowledge_files/` are never edited, and never in a turn
  that edits anything else. No step here edits them (§7 lists what goes out
  of date).
- Commits are the user's. Nothing needed goes in `build/`. No `.claude`
  auto-memory.
- RAM and ITCM growth need sign-off; this schedule stays inside the approved
  lines (§6). Report every resource figure as one net total against 711,036 B.
- One flash per device question.
- Code comments name functions and sessions (S095 P2), never this
  document's entry numbers (the schedule is deleted at close-out).

---

## 1. Step 0: the 1,024-point sine (C-D10)

The table becomes the former table's every fourth point (1,025 values with the
guard, which equals point 0). Every reader shifts by 22 instead of 20 and takes
a 22-bit fraction; the PWM sine warp's split points are phase values and do not
change. Measured: −6,144 B, no loop longer (§C).

### S0.1 `Core/DSPAudio/wavetable.h:43`: modify

Anchor: `#define TABLESIZE 4096`. Replace the line with:

```c
/*
 * The sine table's length (S095 P2 step 0).
 *
 * What:       1,024 points (plus one guard point: sine_table[TABLESIZE + 1]),
 *             read at phase >> 22 with a 22-bit fraction, as the stock
 *             [11][1024] rows are.
 * Why:        S095 P2 C-D10 (user): linear interpolation of a sine on 1,024
 *             points errs by at most (2 pi / 1024)^2 / 8 = -106.5 dB, below
 *             the int16 output's step; the table shrinks by 6,144 B of flash
 *             and keeps 2 KB of D-cache hot instead of 8 KB. The 1,024 points
 *             are the former 4,096-point table's every fourth point, so a read
 *             on a multiple of 2^22 is unchanged.
 * Accessors:  sine_table's readers (Oscillator.c, lfo.c); the Colloid's
 *             Sin row (its loops wrap at 1,024 and never read the guard).
 */
#define TABLESIZE 1024
```

### S0.2 `Core/DSPAudio/wavetable.c:41`, `:43`, `:62`, `:65-577`: modify

- `:41` ` * Full-cycle sine lookup table, 4096 points plus one guard sample.`
  becomes ` * Full-cycle sine lookup table, 1024 points plus one guard sample.`
- `:43` `indexed by the top 12 bits of an` becomes `indexed by the top 10
  bits of an`.
- Add before `:62` (the comment's closing ` */`):

```c
 * S095 P2 step 0 (C-D10, user 2026-11-12): 1,024 points, the former
 * 4,096-point table's every fourth point (a -cos, trough first, as before;
 * the guard point equals point 0). Readers shift by 22, not 20, and take a
 * 22-bit fraction; -6,144 B of flash.
```

- `:65-577` (the 4,097 values, ending `-32767};`): replace with the 1,025
  values `v[4 i]`, i = 0..1,024, eight per line, the last line ending `};`.
  Generate them from the current file; do not type them. The shipped table
  is not a closed formula (4,005 of its 4,097 entries differ from
  `round(−32767 cos)`), so decimation is the only way to keep its points:

```python
# one-off, run from the repository root, before the comment edits of S0.2
import re
L = open("Core/DSPAudio/wavetable.c").read().split("\n")
assert L[62] == "const int16_t sine_table[TABLESIZE+1]=" and L[63] == "{"
assert L[576].endswith("-32767};")
v = [int(x) for x in re.findall(r"-?\d+", "\n".join(L[64:577]))]
assert len(v) == 4097
d = [v[4 * i] for i in range(1025)]
assert d[1024] == d[0] == -32767
rows = [", ".join(str(x) for x in d[i:i + 8]) + "," for i in range(0, 1025, 8)]
rows[-1] = rows[-1].rstrip(",") + "};"
L[64:577] = rows
open("Core/DSPAudio/wavetable.c", "w").write("\n".join(L))
```

Run the snippet first, then make the comment edits above. (The table then
ends at line 197: 1,025 values on 129 lines, after the four new comment
lines.)

### S0.3 `Core/DSPAudio/Oscillator.c`: modify twelve reads and four comments

The float readers (`calcSineBlock()`, `calcSine()`, `calcFmSineBlock()`,
`calcFmSine()`, the sync sine `calcSineSyncBlock()`, the ring sine
`calcSineRingBlock()`):

| Line | Today | Becomes |
|---:|---|---|
| 128, 150, 175, 196, 744, 873 | `uint32_t  itg	= index>>20;` | `uint32_t  itg	= index>>22;` |
| 132, 154, 179, 200, 747, 876 | `frac = (index & 0xfffff) * 0.00000095367431640625f;` | `frac = (index & 0x3fffff) * 0.0000002384185791015625f;` |

(`0x3fffff` is the 22-bit fraction and 2^-22 = 2.384185791015625e-07; the
`INTERPOLATE_OSC 0` branches read `itg` and need nothing else.)

The integer reader `osc_pwmReadS()` (the PWM sine warp):

| Line | Today | Becomes |
|---:|---|---|
| 3788 | `const uint32_t ia = ps >> 20;` | `const uint32_t ia = ps >> 22;` |
| 3791 | `... * (int32_t)((ps >> 5) & 0x7FFFu)) >> 15);` | `... * (int32_t)((ps >> 7) & 0x7FFFu)) >> 15);` |

Comments:

- `:723-724` `(4,096-point table,` / `<<20 phase)` become `(1,024-point table,` /
  `<<22 phase; S095 P2)`.
- `:3647` `/* warp on the 4,096-point sine: Sin */` becomes `1,024-point`.
- `:3777-3782` (the `osc_pwmReadS()` block) becomes:

```c
 * Read the 1,024-point sine at psi, integer interpolation (S094 P1 S3;
 * S095 P2 step 0: 1,024 points).
 *
 * What:       a = sine_table[psi >> 22], b = the next point (the table's
 *             guard point 1,024 makes the last one safe, as calcSineBlock()),
 *             f = (psi >> 7) & 0x7FFF; returns a + ((b - a) x f >> 15).
 * Range:      |b - a| <= 202 (measured on the 1,024-point table);
 *             202 x 32,767 < 2^31.
```

### S0.4 `Core/DSPAudio/lfo.c:94`: modify

```c
			return ((sine_table[(lfo->phase>>22)]/32767.f) + 1)/2.f;   /* S095 P2: the 1,024-point sine (top 10 bits) */
```

### S0.5 Host stand-ins: modify

| File:line | Today | Becomes |
|---|---|---|
| `tools/dsp_test/osc_host.h:37` | `#define TABLESIZE 4096` | `#define TABLESIZE 1024   /* S095 P2 step 0: the 1,024-point sine */` |
| `tools/dsp_test/modules/oscillator.c:107` | `for (unsigned i = 0u; i < 4097u; ++i)` | `... i < 1025u; ...` |
| `tools/dsp_test/modules/oscillator.c:108` | `... / 4096.0));` | `... / 1024.0));   /* S095 P2: 1,024 points */` |
| `tools/dsp_test/modules/oscillator.c:484` | `const uint32_t itg = phase >> 20;` | `... phase >> 22;` |
| `tools/dsp_test/modules/oscillator.c:486` | `(phase & 0x000FFFFFu) * 9.5367431640625e-07f` | `(phase & 0x003FFFFFu) * 2.384185791015625e-07f` |
| `tools/dsp_test/modules/lfo.c:64` | `int16_t sine_table[4097];` | `int16_t sine_table[1025];   /* S095 P2: the 1,024-point sine + guard */` |
| `tools/dsp_test/modules/pwm_dump.c:92` | `sine[i] = sine_table[i * 4u];` | `sine[i] = sine_table[i];   /* S095 P2: the table is 1,024 points */` |

`modules/alkaline.c:107` uses `TABLESIZE` and needs nothing.
`tools/dsp_test/Makefile:1054` extracts the table from `wavetable.c` and
needs nothing. `tools/dsp_test/spike/` is the frozen retry kit: not edited.
The linker script names `sine_table` only in a comment (`:60`).

**Note for the export check:** `tools/dsp_test/baselines/s094p1/alk_golden.bin`
(law AL-OD0) is git-ignored. It exists in your working tree, but a
`git archive` or a fresh clone does not have it.

---

## 2. Step 1: the Colloid engine and type

### 2.1 New files

Directory `Core/DSP/Instruments/Colloid/` (new), plus the tools.

#### S1.1 `Core/DSP/Instruments/Colloid/ColloidTables.h`: add (new file)

```c
/*
 * ColloidTables.h - the Colloid's constant tables (S095 P2).
 *
 * What:       the sizes and the declarations of the tables that
 *             tools/colloid/gen_col_tables.py writes into ColloidTables.c:
 *               colEgK[r]           the envelope's per-block one-pole
 *                                   coefficient for time row r (0..127);
 *               colRatio[s + 12]    2^(s / 12) for s = -12..+24 semitones;
 *               colIntervals[i][m]  member m's semitone offset of interval
 *                                   set i (`int`), colIntervalCount[i] the
 *                                   number of members (1..4);
 *               colMember[n - 1][o] the interval member that oscillator o
 *                                   plays in an n-member set: (o >> 1) mod n,
 *                                   so the two oscillators of a pair always
 *                                   play the same member;
 *               colDetC[o]          oscillator o's detune coefficient,
 *                                   -1..+1 (pairs at +-c, dense near the
 *                                   centre, wider at the edges);
 *               colHome[o]          oscillator o's home stereo position,
 *                                   -1..+1 (a pair on opposite sides; the
 *                                   detune-low member alternates sides from
 *                                   pair to pair);
 *               colMotK[o]          oscillator o's motion rate ratio,
 *                                   0.71..1.37 (no two in step).
 * Why:        constants instead of exp2/exp/log in the control path: the
 *             per-block cost stays a table read (S089/S093 CPU rules), and one
 *             generator owns every value (gen_col_tables.py --check).
 * Inputs:     none (immutable flash data).
 * Accessors:  ColloidVoice.c.
 * Affiliates: tools/colloid/gen_col_tables.py (the source of every value);
 *             S095_P2_DRAFT_COLLOID.md §2.2, §2.6, §4.1.
 */
#ifndef COLLOIDTABLES_H_
#define COLLOIDTABLES_H_

#include <stdint.h>

/* The oscillator arrays' size: the most a Colloid renders (C-D2: 12, or 10
 * if the device gate fails; ColloidVoice.h COL_OSC_COUNT). */
#define COL_OSC_MAX           12u
/* Sixteen interval sets (`int`), up to four members each. */
#define COL_INTERVAL_COUNT    16u
#define COL_INTERVAL_MEMBERS  4u
/* colRatio[] covers -12..+24 semitones: index = semitones - COL_RATIO_LOW. */
#define COL_RATIO_LOW         (-12)
#define COL_RATIO_COUNT       37u
/* One envelope coefficient per 0..127 time byte. */
#define COL_EG_ROWS           128u

extern const float   colEgK[COL_EG_ROWS];
extern const float   colRatio[COL_RATIO_COUNT];
extern const int8_t  colIntervals[COL_INTERVAL_COUNT][COL_INTERVAL_MEMBERS];
extern const uint8_t colIntervalCount[COL_INTERVAL_COUNT];
extern const uint8_t colMember[COL_INTERVAL_MEMBERS][COL_OSC_MAX];
extern const float   colDetC[COL_OSC_MAX];
extern const float   colHome[COL_OSC_MAX];
extern const float   colMotK[COL_OSC_MAX];

#endif /* COLLOIDTABLES_H_ */
```

#### S1.2 `tools/colloid/gen_col_tables.py`: add (new file)

```python
#!/usr/bin/env python3
"""Write and check the Colloid's constant tables, ColloidTables.c (S095 P2).

What:       computes every table declared in ColloidTables.h and writes
            Core/DSP/Instruments/Colloid/ColloidTables.c:
              colEgK[r]         1 - exp(-32 / (T(r) fs)), T(r) = 1 ms x
                                10^(4 r / 127): 1 ms at r 0, 10 s at r 127
                                (log-spaced; the per-block one-pole of both
                                envelopes and of the glide);
              colRatio[s + 12]  2^(s / 12), s = -12..+24;
              colIntervals / colIntervalCount  the sixteen `int` sets of the
                                draft (§2.2), members padded with 0;
              colMember[n - 1][o]  (o >> 1) mod n;
              colDetC[o]        +-PAIR_C[o >> 1]: the pair's lower member
                                (even o) below, the upper above;
              colHome[o]        +-PAIR_H[o >> 1], the side alternating from
                                pair to pair;
              colMotK[o]        MOTK (0.71..1.37, no simple ratios).
            fs is config.h REAL_FS (44,108.0729 Hz), 32 is OUTPUT_DMA_SIZE.
            Floats are written with 9 significant digits (float32 exact).
Why:        one owner for every constant (S095 P2 draft §2.2, §2.6, §4.1);
            --check keeps the committed file equal to this script.
Inputs:     --check: compare instead of write.
Outputs:    the file; with --check a message, exit 1 on a difference.
Accessors:  python3 tools/colloid/gen_col_tables.py [--check];
            make -C tools/dsp_test colloid_tables.
Affiliates: ColloidTables.h (sizes); ColloidVoice.c (readers).
"""
import argparse
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "Core", "DSP", "Instruments", "Colloid", "ColloidTables.c")

FS = 44108.07291666667      # config.h REAL_FS
BLOCK = 32                  # config.h OUTPUT_DMA_SIZE
OSC_MAX = 12                # ColloidTables.h COL_OSC_MAX
EG_ROWS = 128

# the `int` sets (draft §2.2): name, semitones (1..4 members)
INTERVALS = [
    ("uni", [0]), ("oct", [0, 12]), ("sub", [-12, 0]), ("5th", [0, 7]),
    ("4th", [0, 5]), ("o+5", [0, 7, 12]), ("maj", [0, 4, 7]), ("min", [0, 3, 7]),
    ("su2", [0, 2, 7]), ("su4", [0, 5, 7]), ("7th", [0, 4, 7, 10]),
    ("m7", [0, 3, 7, 10]), ("mj7", [0, 4, 7, 11]), ("6th", [0, 4, 7, 9]),
    ("m9", [0, 3, 7, 14]), ("pwr", [-12, 0, 7, 12]),
]
# per pair j = o >> 1: detune magnitude (dense near the centre) and home
# position magnitude (pair 0 nearest the centre)
PAIR_C = [0.06, 0.22, 0.40, 0.58, 0.78, 1.0]
PAIR_H = [0.30, 0.48, 0.63, 0.76, 0.88, 1.0]
# motion rate ratios: none a simple ratio of another
MOTK = [0.71, 1.37, 0.83, 1.23, 0.91, 1.13, 0.77, 1.31, 0.97, 1.07, 0.87, 1.19]


def f32(x):
    """A C float literal with 9 significant digits."""
    s = f"{x:.9g}"
    if "." not in s and "e" not in s:
        s += ".0"
    return s + "f"


def eg_k(r):
    t = 0.001 * 10.0 ** (4.0 * r / 127.0)
    return 1.0 - math.exp(-BLOCK / (t * FS))


def det_c(o):
    return (-1.0 if o % 2 == 0 else 1.0) * PAIR_C[o >> 1]


def home(o):
    j = o >> 1
    side = -1.0 if o % 2 == 0 else 1.0
    if j % 2 == 1:
        side = -side
    return side * PAIR_H[j]


def rows(values, per=4):
    out = []
    for i in range(0, len(values), per):
        out.append("    " + ", ".join(values[i:i + per]) + ",")
    return out


def text():
    assert len(INTERVALS) == 16 and all(1 <= len(s) <= 4 for _, s in INTERVALS)
    assert all(-12 <= x <= 24 for _, s in INTERVALS for x in s)
    assert len(MOTK) == OSC_MAX
    L = ["/*",
         " * ColloidTables.c - generated by tools/colloid/gen_col_tables.py",
         " * (S095 P2). Do not edit: change the script and run it again.",
         " * Contract and meaning of every table: ColloidTables.h.",
         " */",
         '#include "ColloidTables.h"',
         "",
         "const float colEgK[COL_EG_ROWS] = {"]
    L += rows([f32(eg_k(r)) for r in range(EG_ROWS)])
    L += ["};", "", "const float colRatio[COL_RATIO_COUNT] = {"]
    L += rows([f32(2.0 ** (s / 12.0)) for s in range(-12, 25)])
    L += ["};", "", "const int8_t colIntervals[COL_INTERVAL_COUNT][COL_INTERVAL_MEMBERS] = {"]
    for name, s in INTERVALS:
        m = s + [0] * (4 - len(s))
        L.append("    { " + ", ".join(str(x) for x in m) + " },   /* " + name + " */")
    L += ["};", "", "const uint8_t colIntervalCount[COL_INTERVAL_COUNT] = {"]
    L += rows([str(len(s)) for _, s in INTERVALS], 8)
    L += ["};", "", "const uint8_t colMember[COL_INTERVAL_MEMBERS][COL_OSC_MAX] = {"]
    for n in (1, 2, 3, 4):
        L.append("    { " + ", ".join(str((o >> 1) % n) for o in range(OSC_MAX)) + " },")
    L += ["};", "", "const float colDetC[COL_OSC_MAX] = {"]
    L += rows([f32(det_c(o)) for o in range(OSC_MAX)])
    L += ["};", "", "const float colHome[COL_OSC_MAX] = {"]
    L += rows([f32(home(o)) for o in range(OSC_MAX)])
    L += ["};", "", "const float colMotK[COL_OSC_MAX] = {"]
    L += rows([f32(k) for k in MOTK])
    L += ["};", ""]
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    want = text()
    if a.check:
        have = open(OUT, newline="").read() if os.path.exists(OUT) else None
        if have != want:
            sys.exit(f"gen_col_tables: {OUT} differs (rerun without --check)")
        print("gen_col_tables: ColloidTables.c matches")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as fh:
        fh.write(want)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
```

#### S1.3 `Core/DSP/Instruments/Colloid/ColloidTables.c`: add (generated)

Run `python3 tools/colloid/gen_col_tables.py` from the repository root. It
writes exactly this file (`--check` compares):

```c
/*
 * ColloidTables.c - generated by tools/colloid/gen_col_tables.py
 * (S095 P2). Do not edit: change the script and run it again.
 * Contract and meaning of every table: ColloidTables.h.
 */
#include "ColloidTables.h"

const float colEgK[COL_EG_ROWS] = {
    0.515913067f, 0.490710687f, 0.466095141f, 0.442134952f,
    0.418886828f, 0.396396327f, 0.374698619f, 0.353819324f,
    0.3337754f, 0.314576026f, 0.29622349f, 0.278714044f,
    0.26203872f, 0.2461841f, 0.231133025f, 0.216865256f,
    0.203358061f, 0.190586756f, 0.178525177f, 0.167146105f,
    0.156421634f, 0.146323487f, 0.136823295f, 0.127892825f,
    0.119504181f, 0.111629961f, 0.104243395f, 0.0973184434f,
    0.0908298871f, 0.0847533847f, 0.0790655196f, 0.0737438289f,
    0.0687668209f, 0.0641139808f, 0.0597657678f, 0.0557036045f,
    0.0519098602f, 0.0483678293f, 0.0450617056f, 0.041976554f,
    0.0390982792f, 0.0364135937f, 0.0339099838f, 0.0315756761f,
    0.0293996032f, 0.0273713692f, 0.0254812166f, 0.0237199932f,
    0.0220791195f, 0.0205505581f, 0.019126783f, 0.0178007507f,
    0.0165658722f, 0.0154159862f, 0.0143453339f, 0.0133485343f,
    0.012420561f, 0.0115567209f, 0.0107526326f, 0.0100042073f,
    0.00930762991f, 0.00865934175f, 0.00805602388f, 0.00749458161f,
    0.00697212986f, 0.00648597942f, 0.00603362405f, 0.00561272838f,
    0.00522111652f, 0.00485676148f, 0.00451777514f, 0.00420239896f,
    0.00390899528f, 0.00363603907f, 0.0033821104f, 0.00314588724f,
    0.00292613884f, 0.00272171946f, 0.00253156264f, 0.00235467571f,
    0.00219013476f, 0.00203707993f, 0.00189471098f, 0.00176228324f,
    0.00163910371f, 0.00152452757f, 0.00141795481f, 0.00131882714f,
    0.00122662513f, 0.00114086547f, 0.00106109852f, 0.000986905956f,
    0.000917898592f, 0.000853714364f, 0.000794016446f, 0.000738491497f,
    0.000686848031f, 0.00063881489f, 0.000594139838f, 0.000552588239f,
    0.000513941834f, 0.0004779976f, 0.000444566686f, 0.000413473429f,
    0.000384554436f, 0.000357657721f, 0.000332641917f, 0.000309375534f,
    0.000287736266f, 0.000267610353f, 0.000248891986f, 0.000231482749f,
    0.000215291104f, 0.000200231911f, 0.000186225981f, 0.000173199661f,
    0.000161084445f, 0.000149816618f, 0.000139336919f, 0.000129590229f,
    0.000120525284f, 0.000112094404f, 0.000104253242f, 9.69605546e-05f,
    9.01779796e-05f, 8.38698387e-05f, 7.80029484e-05f, 7.25464459e-05f,
};

const float colRatio[COL_RATIO_COUNT] = {
    0.5f, 0.529731547f, 0.561231024f, 0.594603558f,
    0.629960525f, 0.667419927f, 0.707106781f, 0.749153538f,
    0.793700526f, 0.840896415f, 0.890898718f, 0.943874313f,
    1.0f, 1.05946309f, 1.12246205f, 1.18920712f,
    1.25992105f, 1.33483985f, 1.41421356f, 1.49830708f,
    1.58740105f, 1.68179283f, 1.78179744f, 1.88774863f,
    2.0f, 2.11892619f, 2.2449241f, 2.37841423f,
    2.5198421f, 2.66967971f, 2.82842712f, 2.99661415f,
    3.1748021f, 3.36358566f, 3.56359487f, 3.77549725f,
    4.0f,
};

const int8_t colIntervals[COL_INTERVAL_COUNT][COL_INTERVAL_MEMBERS] = {
    { 0, 0, 0, 0 },   /* uni */
    { 0, 12, 0, 0 },   /* oct */
    { -12, 0, 0, 0 },   /* sub */
    { 0, 7, 0, 0 },   /* 5th */
    { 0, 5, 0, 0 },   /* 4th */
    { 0, 7, 12, 0 },   /* o+5 */
    { 0, 4, 7, 0 },   /* maj */
    { 0, 3, 7, 0 },   /* min */
    { 0, 2, 7, 0 },   /* su2 */
    { 0, 5, 7, 0 },   /* su4 */
    { 0, 4, 7, 10 },   /* 7th */
    { 0, 3, 7, 10 },   /* m7 */
    { 0, 4, 7, 11 },   /* mj7 */
    { 0, 4, 7, 9 },   /* 6th */
    { 0, 3, 7, 14 },   /* m9 */
    { -12, 0, 7, 12 },   /* pwr */
};

const uint8_t colIntervalCount[COL_INTERVAL_COUNT] = {
    1, 2, 2, 2, 2, 3, 3, 3,
    3, 3, 4, 4, 4, 4, 4, 4,
};

const uint8_t colMember[COL_INTERVAL_MEMBERS][COL_OSC_MAX] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1 },
    { 0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2 },
    { 0, 0, 1, 1, 2, 2, 3, 3, 0, 0, 1, 1 },
};

const float colDetC[COL_OSC_MAX] = {
    -0.06f, 0.06f, -0.22f, 0.22f,
    -0.4f, 0.4f, -0.58f, 0.58f,
    -0.78f, 0.78f, -1.0f, 1.0f,
};

const float colHome[COL_OSC_MAX] = {
    -0.3f, 0.3f, 0.48f, -0.48f,
    -0.63f, 0.63f, 0.76f, -0.76f,
    -0.88f, 0.88f, 1.0f, -1.0f,
};

const float colMotK[COL_OSC_MAX] = {
    0.71f, 1.37f, 0.83f, 1.23f,
    0.91f, 1.13f, 0.77f, 1.31f,
    0.97f, 1.07f, 0.87f, 1.19f,
};
```

#### S1.4 `Core/DSP/Instruments/Colloid/ColloidOsc.h`: add (new file)

```c
/*
 * ColloidOsc.h - the Colloid's oscillator pair loops (S095 P2).
 *
 * What:       four per-sample loops, each rendering one pair of free-running
 *             oscillators for one 32-sample block into the voice's two int32
 *             buses:
 *               busM[n] (mid)  = s0 + s1
 *               busS[n] (side) = (s0 x p0 + s1 x p1) / 2, p in Q15
 *             where s is the oscillator's interpolated row read and p its
 *             stereo position (-1..+1) for this block. The "store" forms write
 *             the buses (the first pair of a block, so no clear pass is
 *             needed); the "add" forms accumulate into them.
 *               col_oscPairStore / col_oscPairAdd  a 1,024-point row each
 *                 (a stock octave row, the 1,024-point sine, or a wavetable
 *                 frame), linearly interpolated;
 *               col_pwmPairStore / col_pwmPairAdd  the band-limited pulse:
 *                 the saw row read at the phase minus the saw row read
 *                 `width` points earlier, interpolated once (the shipped
 *                 osc_pwmReadD() form with sign -1).
 * Why:        S095 P2 draft §2.1/§2.4: a pair per pass is the cheapest form
 *             measured (13 per oscillator plain, 22 PWM, before the side
 *             term); the side term is one SMLAW per oscillator with both
 *             positions packed in one register (posPair: oscillator 0 in the
 *             low half, oscillator 1 in the high half).
 * Inputs:     row[0..1] (the pair's rows), phase[0..1] (updated in place),
 *             inc[0..1], width[0..1] (pwm: the pulse width in row points,
 *             1..1,023), posPair, busM[32], busS[32].
 * Outputs:    busM, busS; phase[0..1] advanced by 32 x inc.
 * Accessors:  Colloid_calcVoiceSyncBlock() (ColloidVoice.c), once per pair
 *             per block.
 * Affiliates: ColloidOsc.c (placement .itcm.s095p2); STM32F765VIHx_FLASH.ld;
 *             tools/dsp_test modules/colloid.c laws CO-OSC, CO-POS, CO-PWM.
 */
#ifndef COLLOIDOSC_H_
#define COLLOIDOSC_H_

#include <stdint.h>

void col_oscPairStore(const int16_t *const *row, uint32_t *phase,
                      const uint32_t *inc, uint32_t posPair,
                      int32_t *busM, int32_t *busS);
void col_oscPairAdd(const int16_t *const *row, uint32_t *phase,
                    const uint32_t *inc, uint32_t posPair,
                    int32_t *busM, int32_t *busS);
void col_pwmPairStore(const int16_t *const *row, uint32_t *phase,
                      const uint32_t *inc, const uint16_t *width,
                      uint32_t posPair, int32_t *busM, int32_t *busS);
void col_pwmPairAdd(const int16_t *const *row, uint32_t *phase,
                    const uint32_t *inc, const uint16_t *width,
                    uint32_t posPair, int32_t *busM, int32_t *busS);

#endif /* COLLOIDOSC_H_ */
```

#### S1.5 `Core/DSP/Instruments/Colloid/ColloidOsc.c`: add (new file)

```c
/*
 * ColloidOsc.c - the Colloid's oscillator pair loops (S095 P2).
 * Contract: ColloidOsc.h.
 */
#include "ColloidOsc.h"
#include "config.h"   /* OUTPUT_DMA_SIZE */

/*
 * Placement of the four loops (S095 P2).
 *
 * What:       COL_S095P2_SECTION puts a function in .itcm.s095p2, which
 *             STM32F765VIHx_FLASH.ld pins after .itcm.s094p1 (the last named
 *             subsection before S095 P2), so no existing ITCM function moves.
 *             COL_LOOP adds noinline, noclone: each loop is one out-of-line
 *             call per pair per block, never inlined into
 *             Colloid_calcVoiceSyncBlock() or the mixer (its registers stay
 *             its own; INSTRUMENTS_DSP_REFERENCE.md §12).
 * Why:        the oscillator loops are the Colloid's dearest per-sample code
 *             (up to 6 pairs per block); ITCM runs them with no flash wait
 *             states. The ITCM growth is inside the approved +1 KB (user,
 *             2026-11-12).
 * Note:       the section attribute stands alone so that the host extractor
 *             (tools/dsp_test/extract.py) removes it for the host build.
 */
#define COL_S095P2_SECTION __attribute__((section(".itcm.s095p2")))
#define COL_LOOP           COL_S095P2_SECTION __attribute__((noinline, noclone))

/*
 * The side-bus multiplies: (s x p) >> 16 with p a signed 16-bit half of
 * posPair (S095 P2).
 *
 * What:       col_smulwb(s, pq)      = (s x (int16)pq[15:0]) >> 16;
 *             col_smlawb(s, pq, acc) = (s x (int16)pq[15:0]) >> 16 + acc;
 *             col_smlawt(s, pq, acc) = (s x (int16)pq[31:16]) >> 16 + acc.
 *             With p in Q15 the result is s x p / 2: the side bus carries
 *             half the positioned signal, and the filter loop restores it
 *             with L = M + 2S, R = M - 2S (a shifted add, no multiply).
 * Why:        one instruction per oscillator (SMULWB/SMLAWB/SMLAWT) and the
 *             pair's two positions in one register. GCC does not reliably
 *             select these from the C expression (voiceFold.c vf_smlawb(),
 *             S089 P2), so the device build uses inline assembly; the host
 *             build (no DSP extension) uses the exact C equivalent: the top
 *             32 bits of the 48-bit product, plus the accumulator.
 * Range:      |s| <= 65,534 (the pwm pulse), so the result fits easily.
 * Accessors:  the four loops below.
 */
static inline __attribute__((always_inline)) int32_t col_smulwb(int32_t s, uint32_t pq)
{
#if defined(__ARM_FEATURE_DSP)
	int32_t r;
	__asm__ ("smulwb %0, %1, %2" : "=r"(r) : "r"(s), "r"(pq));
	return r;
#else
	return (int32_t)(((int64_t)s * (int16_t)(pq & 0xFFFFu)) >> 16);
#endif
}

static inline __attribute__((always_inline)) int32_t col_smlawb(int32_t s, uint32_t pq, int32_t acc)
{
#if defined(__ARM_FEATURE_DSP)
	int32_t r;
	__asm__ ("smlawb %0, %1, %2, %3" : "=r"(r) : "r"(s), "r"(pq), "r"(acc));
	return r;
#else
	return (int32_t)(((int64_t)s * (int16_t)(pq & 0xFFFFu)) >> 16) + acc;
#endif
}

static inline __attribute__((always_inline)) int32_t col_smlawt(int32_t s, uint32_t pq, int32_t acc)
{
#if defined(__ARM_FEATURE_DSP)
	int32_t r;
	__asm__ ("smlawt %0, %1, %2, %3" : "=r"(r) : "r"(s), "r"(pq), "r"(acc));
	return r;
#else
	return (int32_t)(((int64_t)s * (int16_t)(pq >> 16)) >> 16) + acc;
#endif
}

/*
 * One interpolated row read (S095 P2).
 *
 * What:       ia = ph >> 22 (the point, 0..1,023 of the 1,024-point row),
 *             na = (ia + 1) & 1,023 (the next point, wrapping: no guard point
 *             is read, so the 1,025-point sine and the guard-less stock and
 *             wavetable rows are read alike), f = (ph >> 7) & 0x7FFF (the
 *             15-bit fraction below the point);
 *             returns a + ((b - a) x f >> 15).
 * Why:        integer interpolation into the int32 bus: no float conversion
 *             per oscillator (draft §2.1, finding 1). The fraction is 15 bits,
 *             not the draft's 16: a user wavetable frame may step by up to
 *             65,535 between points, and 65,535 x 32,767 < 2^31 where
 *             65,535 x 65,535 is not. Same instruction count.
 * Accessors:  col_oscPairStore(), col_oscPairAdd().
 */
static inline __attribute__((always_inline)) int32_t col_read(const int16_t *row, uint32_t ph)
{
	const uint32_t ia = ph >> 22;
	const uint32_t na = (ia + 1u) & 0x3FFu;
	const int32_t a = row[ia];

	return a + (((row[na] - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

/*
 * One band-limited pulse read (S095 P2; the shipped two-read form D).
 *
 * What:       A at ph, B at ph - d (d points earlier, the row wraps):
 *               d0 = row[ia] - row[ib], d1 = the same one point on,
 *               out = d0 + ((d1 - d0) x f >> 15), f = (ph >> 7) & 0x7FFF.
 *             Both reads share one fraction (d is a whole number of points),
 *             so the difference is interpolated once.
 * Why:        osc_pwmReadD() (Oscillator.c, S094 P1 S3) with sign -1: the
 *             saw-row difference is the Rec-orientation pulse, high for the
 *             first d points of the cycle; d = oscPwmDRec[pos] (the S094 P4
 *             law, pos 0 = 512 = 50 %). That function is static to
 *             Oscillator.c, so the form is restated here.
 * Range:      |d1 - d0| <= 65,534 on the band-limited saw rows (the S094 P1
 *             bound), and 65,534 x 32,767 < 2^31.
 * Accessors:  col_pwmPairStore(), col_pwmPairAdd().
 */
static inline __attribute__((always_inline)) int32_t col_pulse(const int16_t *row, uint32_t ph,
                                                             uint32_t d)
{
	const uint32_t ia = ph >> 22;
	const uint32_t na = (ia + 1u) & 0x3FFu;
	const uint32_t ib = (ia - d) & 0x3FFu;
	const uint32_t nb = (ib + 1u) & 0x3FFu;
	const int32_t d0 = row[ia] - row[ib];
	const int32_t d1 = row[na] - row[nb];

	return d0 + (((d1 - d0) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

/*
 * The first plain pair of a block: stores both buses (S095 P2).
 * Loop: n = 0..31; s0, s1 the two reads; M = s0 + s1; S = s0 p0/2 + s1 p1/2;
 * both phases advance by their increments (free-running: never reset).
 */
COL_LOOP void col_oscPairStore(const int16_t *const *row, uint32_t *phase,
                               const uint32_t *inc, const uint32_t posPair,
                               int32_t *busM, int32_t *busS)
{
	const int16_t *const r0 = row[0];
	const int16_t *const r1 = row[1];
	const uint32_t i0 = inc[0];
	const uint32_t i1 = inc[1];
	uint32_t p0 = phase[0];
	uint32_t p1 = phase[1];
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s0 = col_read(r0, p0);
		const int32_t s1 = col_read(r1, p1);

		busM[n] = s0 + s1;
		busS[n] = col_smlawt(s1, posPair, col_smulwb(s0, posPair));
		p0 += i0;
		p1 += i1;
	}
	phase[0] = p0;
	phase[1] = p1;
}

/*
 * Every further plain pair: adds into both buses (S095 P2).
 * As col_oscPairStore(), with M += s0 + s1 and S += s0 p0/2 + s1 p1/2.
 */
COL_LOOP void col_oscPairAdd(const int16_t *const *row, uint32_t *phase,
                             const uint32_t *inc, const uint32_t posPair,
                             int32_t *busM, int32_t *busS)
{
	const int16_t *const r0 = row[0];
	const int16_t *const r1 = row[1];
	const uint32_t i0 = inc[0];
	const uint32_t i1 = inc[1];
	uint32_t p0 = phase[0];
	uint32_t p1 = phase[1];
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s0 = col_read(r0, p0);
		const int32_t s1 = col_read(r1, p1);

		busM[n] += s0 + s1;
		busS[n] = col_smlawt(s1, posPair, col_smlawb(s0, posPair, busS[n]));
		p0 += i0;
		p1 += i1;
	}
	phase[0] = p0;
	phase[1] = p1;
}

/*
 * The first pwm pair of a block: stores both buses (S095 P2).
 * As col_oscPairStore() with the pulse read; each oscillator has its own
 * width (width[0], width[1]: the per-block widths after the slew).
 */
COL_LOOP void col_pwmPairStore(const int16_t *const *row, uint32_t *phase,
                               const uint32_t *inc, const uint16_t *width,
                               const uint32_t posPair, int32_t *busM, int32_t *busS)
{
	const int16_t *const r0 = row[0];
	const int16_t *const r1 = row[1];
	const uint32_t i0 = inc[0];
	const uint32_t i1 = inc[1];
	const uint32_t d0 = width[0];
	const uint32_t d1 = width[1];
	uint32_t p0 = phase[0];
	uint32_t p1 = phase[1];
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s0 = col_pulse(r0, p0, d0);
		const int32_t s1 = col_pulse(r1, p1, d1);

		busM[n] = s0 + s1;
		busS[n] = col_smlawt(s1, posPair, col_smulwb(s0, posPair));
		p0 += i0;
		p1 += i1;
	}
	phase[0] = p0;
	phase[1] = p1;
}

/*
 * Every further pwm pair: adds into both buses (S095 P2).
 */
COL_LOOP void col_pwmPairAdd(const int16_t *const *row, uint32_t *phase,
                             const uint32_t *inc, const uint16_t *width,
                             const uint32_t posPair, int32_t *busM, int32_t *busS)
{
	const int16_t *const r0 = row[0];
	const int16_t *const r1 = row[1];
	const uint32_t i0 = inc[0];
	const uint32_t i1 = inc[1];
	const uint32_t d0 = width[0];
	const uint32_t d1 = width[1];
	uint32_t p0 = phase[0];
	uint32_t p1 = phase[1];
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t s0 = col_pulse(r0, p0, d0);
		const int32_t s1 = col_pulse(r1, p1, d1);

		busM[n] += s0 + s1;
		busS[n] = col_smlawt(s1, posPair, col_smlawb(s0, posPair, busS[n]));
		p0 += i0;
		p1 += i1;
	}
	phase[0] = p0;
	phase[1] = p1;
}
```

#### S1.6 `Core/DSP/Instruments/Colloid/ColloidVoice.h`: add (new file)

```c
/*
 * ColloidVoice.h - the Colloid voice engine (S095 P2).
 *
 * What:       one Colloid voice: a Basic-class stack of free-running
 *             band-limited oscillators (Saw, Rec, Tri, Sin or wavetable
 *             frames, or the Rec pulse under pwm), each at its own pitch
 *             (interval member and detune) and its own stereo position (spr,
 *             mot), summed into a mid and a side bus; two smooth TPT
 *             state-variable filters with drive, in parallel (stereo: A on
 *             L, B on R) or in series (mono); lightweight amp and filter ADSRs
 *             with the sustain switch; a true stereo output.
 *             Entry points follow the other engines (init / trigger / async /
 *             sync block), taking an instance pointer owned by the caller.
 * Why:        S095_P2_DRAFT_COLLOID.md (user briefs 2026-11-10 and
 *             2026-11-12; decisions C-D1..C-D21).
 * Inputs:     descriptor writes into the parameter members (TYPE_FLT rows
 *             store byte / 127, TYPE_UINT8 rows the byte); triggers with
 *             velocity and note.
 * Outputs:    per block two pre-volume int16 blocks: the left one into the
 *             caller's buffer (the mixer's sampleData), the right one into
 *             voice->right (read by the mixer through
 *             instrumentManager_slotRight()). Colloid_isQuiet() for
 *             instrumentManager_ampEnvelopeQuiet().
 * Accessors:  InstrumentManager dispatchers; tools/dsp_test modules/colloid.c.
 * Affiliates: ColloidOsc.h (the pair loops), ColloidTables.h,
 *             ColloidParameters.h, AlkalineControl.h (alk_log2f/alk_exp2f).
 */
#ifndef COLLOIDVOICE_H_
#define COLLOIDVOICE_H_

#include <stdint.h>
#include "Oscillator.h"       /* OscInfo, OSC_TYPE_*, config.h (OUTPUT_DMA_SIZE) */
#include "lfo.h"
#include "ColloidTables.h"    /* COL_OSC_MAX */

/*
 * The oscillator count (C-D2, set by the device gate).
 *
 * What:       COL_OSC_COUNT oscillators render under typ wav and wtb, and
 *             COL_PWM_COUNT under pwm. Both are even (the loops render pairs)
 *             and at most COL_OSC_MAX (the arrays' size).
 * Why:        S095 P2 draft §6.1: 12 plain / 6 PWM are measured against the
 *             Basic share (19,620 cycles per block, cold, parallel, stereo
 *             send); if 12 does not fit, COL_OSC_COUNT becomes 10 here and
 *             nothing else changes (the arrays keep 12 entries).
 * Constant:   the count follows the mode, never a parameter value: every
 *             block of a given typ renders exactly this many oscillators.
 */
#ifndef COL_OSC_COUNT
#define COL_OSC_COUNT         12u
#endif
#define COL_PWM_COUNT         6u
#define COL_PAIR_MAX          (COL_OSC_MAX / 2u)
_Static_assert(COL_OSC_COUNT >= 2u && COL_OSC_COUNT <= COL_OSC_MAX &&
               (COL_OSC_COUNT % 2u) == 0u, "COL_OSC_COUNT: an even 2..12");
_Static_assert(COL_PWM_COUNT >= 2u && COL_PWM_COUNT <= COL_OSC_COUNT &&
               (COL_PWM_COUNT % 2u) == 0u, "COL_PWM_COUNT: an even 2..COL_OSC_COUNT");

/*
 * osc.waveform under typ wav (S095 P2, C-D20).
 *
 * What:       0 Saw, 1 Rec, 2 Tri (the stock band-limited [11][1024] rows,
 *             one octave row per oscillator), 3 Sin (the 1,024-point
 *             sine_table, one row for every octave). Under typ pwm the list
 *             is one entry, 0 = Rec (the saw-row pulse, C-D21). Under typ wtb
 *             the byte is the wavetable index.
 * Accessors:  col_oscControl(), Colloid_setOscType(),
 *             instrumentManager_waveDomainMax(), menu_formatWaveCell()
 *             (colWaveNames, MenuText.h).
 */
#define COL_WAVE_SAW          0u
#define COL_WAVE_REC          1u
#define COL_WAVE_TRI          2u
#define COL_WAVE_SIN          3u
#define COL_WAVE_COUNT        4u
#define COL_PWM_WAVE_COUNT    1u
/*
 * The typ byte (row osc1_type; C-D20: the Alkaline's names and values).
 *
 * What:       0 wav, 1 wtb, 2 pwm (alkTypeNames, tag ONOFF_TAG_ALK_TYPE).
 *             Colloid_setOscType() maps it to OSC_TYPE_WAV / OSC_TYPE_WTB /
 *             OSC_TYPE_PWM in osc.oscType, so the shared wave-cell helpers
 *             (instrumentManager_osc1Type(), osc_userCount()) read the
 *             platform's values.
 */
#define COL_TYP_WAV           0u
#define COL_TYP_WTB           1u
#define COL_TYP_PWM           2u
#define COL_TYP_COUNT         3u
/* The wavetable bind state in osc.sampleCacheLooped (as ALK_BIND_*). */
#define COL_BIND_NONE         0u
#define COL_BIND_WTB          1u
/* Envelope stages (ColEg.stage). */
#define COL_EG_IDLE           0u
#define COL_EG_ATTACK         1u
#define COL_EG_DECAY          2u
#define COL_EG_SUSTAIN        3u
#define COL_EG_RELEASE        4u
/* fmd: 0 parallel (stereo), any other value series (mono). */
#define COL_FMD_PAR           0u
#define COL_FMD_SER           1u
#define COL_FMD_COUNT         2u
/* tya / tyb: the filter's output mix. */
#define COL_FT_LP             0u
#define COL_FT_BP             1u
#define COL_FT_HP             2u
#define COL_FT_NOTCH          3u
#define COL_FT_PEAK           4u
#define COL_FT_COUNT          5u
/* pmd: 0 every note glides, any other value only legato notes. */
#define COL_PMD_ALL           0u
#define COL_PMD_LEG           1u
#define COL_PMD_COUNT         2u

/*
 * One lightweight envelope (S095 P2, draft §4.1).
 *
 * What:       level (0..1) and stage (COL_EG_*). Stepped once per block by
 *             col_egStep(): level += colEgK[time] x (aim - level).
 * Why:        both ADSRs are per-block one-poles: no per-sample work in the
 *             envelope itself (the amp level is ramped per sample in the
 *             output loop; the filter level moves the cutoff targets, which
 *             the filters ramp).
 */
typedef struct {
	float   level;
	uint8_t stage;
} ColEg;

/*
 * One TPT (Simper/Zavalishin) state-variable filter (S095 P2, draft §3).
 *
 * What:       ic1, ic2: the two trapezoidal integrator states;
 *             a1..a3: the coefficients at the start of this block;
 *             t1..t3: this block's targets (the coefficients at its end);
 *             da1..da3: (t - a) / 32, added once per sample;
 *             mH, mB, mL: the output mix y = mH x + mB v1 + mL v2 (LP 0 0 1,
 *             BP 0 1 0, HP 1 -k -1, notch 1 -k 0, peak 1 -k -2);
 *             cutSt: the slewed cutoff in semitones above 20 Hz;
 *             k: the slewed damping, 2 - 1.95 x res (0.05 at full res).
 * Why:        the TPT form stays stable under any per-sample change of its
 *             coefficients, so a linear coefficient ramp gives sweeps with no
 *             block steps ("smooth", the brief), with no division per sample.
 */
typedef struct {
	float ic1, ic2;
	float a1, a2, a3;
	float t1, t2, t3;
	float da1, da2, da3;
	float mH, mB, mL;
	float cutSt;
	float k;
} ColSvf;

typedef struct ColloidVoiceStruct
{
	/*
	 * The pitch anchor (as the Alkaline's osc): midiFreq (coa/fin),
	 * modNodeValue (the LFO/velocity target of coa and fin, a frequency
	 * multiplier), waveform (wav), oscType (typ), position (pos), baseNote;
	 * under wtb the bind in sampleCacheLooped / sampleCacheIndex /
	 * sampleCacheGeneration / sampleCacheSize (frames) / sampleCacheData
	 * (the table's first 1,024-point row, NULL with no table installed).
	 * Its phase and phaseInc are not used: every oscillator has its own.
	 */
	OscInfo  osc;
	Lfo      lfo;            /* platform LFO pair 1 (as every engine) */

	/* --- the oscillator bank: arrays, the pair loops' operands --- */
	uint32_t phase[COL_OSC_MAX];      /* free-running phase, 2^32 per cycle */
	uint32_t inc[COL_OSC_MAX];        /* per-sample phase step, this block */
	const int16_t *row[COL_OSC_MAX];  /* the 1,024-point row played, this block */
	uint32_t posPair[COL_PAIR_MAX];   /* pair j: Q15 position of 2j (low half), 2j+1 (high) */
	uint16_t pwD[COL_OSC_MAX];        /* pwm: pulse width in row points, this block */
	uint32_t motPhase[COL_OSC_MAX];   /* the motion LFO phase, 2^32 per cycle */
	int16_t  rnd[COL_OSC_MAX];        /* the draw of the last non-legato trigger, Q15 */
	uint8_t  frame[COL_OSC_MAX];      /* wtb: the frame latched at the trigger */
	uint8_t  pwPos[COL_OSC_MAX];      /* pwm: the slewed width index 0..127 */

	/* --- buses and the right output block --- */
	int32_t  busM[OUTPUT_DMA_SIZE];   /* mid: sum of every oscillator */
	int32_t  busS[OUTPUT_DMA_SIZE];   /* side: sum of s x p / 2 */
	int16_t  right[OUTPUT_DMA_SIZE];  /* the pre-volume right block (the mixer reads it) */

	/* --- filters and envelopes --- */
	ColSvf   svfA;                    /* parallel: on L; series: first */
	ColSvf   svfB;                    /* parallel: on R; series: second */
	ColEg    ampEg;
	ColEg    fltEg;

	/* --- parameters: TYPE_FLT rows (byte / 127) --- */
	float    posSpread;     /* psp: frame (wtb) or width (pwm) spread */
	float    sustainLevel;  /* slv */
	float    fSustain;      /* fsl */
	float    fAmount;       /* fam: 63/127 = none, bipolar */
	float    detune;        /* dtn */
	float    spread;        /* spr */
	float    motion;        /* mot */
	float    cutA;          /* cta */
	float    resA;          /* rsa */
	float    cutB;          /* ctb */
	float    resB;          /* rsb */
	float    drive;         /* drv: x1..x16 */
	float    vol;           /* read by the mixer only */
	/* --- parameters: TYPE_UINT8 rows (the byte) --- */
	uint8_t  attack;        /* atk: colEgK row */
	uint8_t  decay;         /* dec */
	uint8_t  release;       /* rel */
	uint8_t  fAttack;       /* fat */
	uint8_t  fDecay;        /* fde */
	uint8_t  fRelease;      /* fre */
	uint8_t  glide;         /* por: 0 off */
	uint8_t  pan;           /* the mixer's per-slot pan (C-D11) */
	uint8_t  sustain;       /* sus: on/off (C-D8) */
	uint8_t  tie;           /* lgt: on/off */
	uint8_t  glideMode;     /* pmd: COL_PMD_* */
	uint8_t  interval;      /* int: 0..15 */
	uint8_t  filtMode;      /* fmd: COL_FMD_* */
	uint8_t  typeA;         /* tya: COL_FT_* */
	uint8_t  typeB;         /* tyb: COL_FT_* */

	/* --- state --- */
	uint8_t  note;          /* last trigger note */
	uint8_t  gliding;       /* this note glides */
	uint8_t  pitchJump;     /* set the pitch to the target at the next block */
	float    pitchLog2;     /* the glided base pitch, log2 Hz */
	float    ampLast;       /* the amp level at the end of the previous block */
	float    inGain;        /* this block's bus-to-drive gain (normalisation x drive) */
} ColloidVoice;

/*
 * Initialise a voice to the init.col sound (S095 P2).
 *
 * What:       every member: typ wav, Saw, coa 48 (C3), the init.col bytes,
 *             random oscillator and motion phases (GetRngValue()), idle
 *             envelopes, both filters at rest on their knobs.
 * Why:        noinline, noclone: called from instrumentManager_runtimeInit()
 *             and instrumentManager_resetRuntimeSlotFromScene(); without them
 *             LTO kept two 688 B copies (an ipa-split .part clone beside the
 *             function, measured S095 P2), 672 B of flash for nothing.
 * Inputs:     voice (the slot's runtime member, already cleared; NULL is
 *             ignored).
 * Accessors:  InstrumentManager.c (boot and slot reset, main loop); the host
 *             bench.
 */
void Colloid_initVoice(ColloidVoice *voice) __attribute__((noinline, noclone));
/*
 * Trigger the voice (S095 P2, draft §4).
 *
 * What:       the platform part first (as Drum_triggerVoice() and
 *             Alkaline_triggerVoice(): the LFO retrigger and the two velocity
 *             paths for source_slot); then legato: a trigger with lgt on while
 *             the amp envelope is still held (attack, decay or sustain) is
 *             legato: neither envelope restarts, no new draws, and the pitch
 *             glides. Any other trigger restarts both attacks from their
 *             current levels (no click), draws a new rnd[] per oscillator,
 *             latches the wavetable frames (wtb) or snaps the widths (pwm).
 *             pmd all: every note glides; pmd leg: only legato notes.
 * Inputs:     voice (non-NULL), source_slot 0..5, velocity 0..127, note.
 * Outputs:    the voice's sequencing state; the slot's LFO phase and
 *             velocity nodes.
 * Accessors:  instrumentManager_triggerTrack() (main loop:
 *             voiceControl_processPending()).
 */
void Colloid_triggerVoice(ColloidVoice *voice, uint8_t source_slot,
                          uint8_t velocity, uint8_t note);
/* Both are called from inside mixer_calcNextSampleBlock(): noinline keeps
 * LTO from inlining them there (the S086 F1 register-pressure rule; the
 * Alkaline's AlkalineVoice.h precedent). */
void Colloid_calcVoiceAsync(ColloidVoice *voice) __attribute__((noinline));
void Colloid_calcVoiceSyncBlock(ColloidVoice *voice, int16_t *buf, uint8_t size)
	__attribute__((noinline));
/*
 * Whether the voice is silent enough to swap (S095 P2, draft §4.2).
 *
 * What:       1 when the amp envelope is idle (its release fell below 1e-4,
 *             the instrumentManager_ampEnvelopeQuiet() threshold), else 0.
 * Why:        the generic test would call a type without a SlopeEg2 quiet
 *             while it sounds (the Alkaline precedent).
 */
uint8_t Colloid_isQuiet(const ColloidVoice *voice);
/*
 * Set the oscillator type from the typ byte (S095 P2, C-D20).
 *
 * What:       0 -> OSC_TYPE_WAV, 1 -> OSC_TYPE_WTB, 2 and above ->
 *             OSC_TYPE_PWM in osc.oscType; clamps osc.waveform to the new
 *             list (wav: Saw..Sin, pwm: Rec, wtb: the installed tables);
 *             clears the bind so the next block binds; under pwm snaps every
 *             width to its target. Phases run on. An unchanged type is a no-op.
 * Inputs:     voice (non-NULL), typ (0..255).
 * Accessors:  instrumentManager_writeSpecialRuntime() (IM_SPECIAL_OSC_TYPE,
 *             a Colloid slot); the host bench.
 */
void Colloid_setOscType(ColloidVoice *voice, uint8_t typ);

#endif /* COLLOIDVOICE_H_ */
```

#### S1.7 `Core/DSP/Instruments/Colloid/ColloidVoice.c`: add (new file)

```c
/*
 * ColloidVoice.c - the Colloid voice engine (S095 P2).
 * See ColloidVoice.h. Behaviour: S095_P2_DRAFT_COLLOID.md §1-§5.
 */
#include <math.h>
#include <string.h>
#include "ColloidVoice.h"
#include "ColloidOsc.h"
#include "ColloidTables.h"
#include "AlkalineControl.h"    /* alk_log2f(), alk_exp2f(), ALK_LOG2E (S093 P3 fast maths) */
#include "wavetable.h"          /* sine_table, sawTable, recTable, triTable */
#include "oscPwmTables.h"       /* oscPwmDRec[] (the S094 P4 width law) */
#include "MidiPitch.h"          /* midiPitch_detune(), MIDI_DEFAULT_TRIGGER_NOTE */
#include "modulationNode.h"     /* velocityModulators[], modNode_updateValue() */
#include "InstrumentManager.h"  /* instrumentManager_applyVelocityModulationTarget() */
#include "sequencer.h"          /* seq_isRunning() (read only) */
#include "random.h"             /* GetRngValue() (main loop only) */

/*
 * Control constants (S095 P2). The "by ear" values (C-D12/C-D14/C-D15 and
 * the ranges of H.3) are first settings, set in the listening step and
 * recorded there.
 *
 * COL_LOG2_440        log2(440 Hz): the A4 anchor of the pitch formula.
 * COL_CUT_BASE_HZ     the cutoff law's 0: 20 Hz.
 * COL_CUT_ST_MAX      the cutoff law's top in semitones above 20 Hz: 115.7
 *                     = 15,963 Hz, where the Pade tangent below is still
 *                     within 0.4 % of tan() (0.82 at 18 kHz, its pole at
 *                     0.503 fs); the filter envelope is clamped to it too.
 * COL_PI_OVER_FS      pi / fs: the prewarp argument per Hz.
 * COL_SLEW            the per-block one-pole on the cutoff and the damping
 *                     (SVF_recalcFreqSlew()'s 0.28: about 3.5 blocks).
 * COL_K_SPAN          k = 2 - 1.95 res: k's floor 0.05 bounds the Q (about 20).
 * COL_FENV_ST         fam at either end moves both cutoffs 72 semitones (6
 *                     octaves) at envelope level 1.
 * COL_DRIVE_OCT       drv 0..1 -> the drive gain 2^(4 drv) = x1..x16.
 * COL_CLIP            the drive's input clamp: the cubic below is monotonic
 *                     up to 1.5, where it reaches exactly 1.0.
 * COL_CUBIC           4/27: y = x - (4/27) x^3, the division-free soft clip.
 * COL_OUT_SCALE       full scale of the int16 output stores.
 * COL_EG_ATTACK_AIM   the attack aims at 1.2 and stops at 1.0: a finite
 *                     attack with the analogue curve.
 * COL_EG_SETTLE       a decay within 0.01 of its sustain level is settled.
 * COL_EG_IDLE_LEVEL   a release below 1e-4 ends (the ampEnvelopeQuiet()
 *                     threshold).
 * COL_DETUNE_MAX_LN   dtn 127 spreads +-50 cents: 50 / 1200 x ln 2.
 * COL_MOT_DEPTH_MAX   mot's depth law 0.6 x sqrt(mot) (C-D15).
 * COL_MOT_RATE_MIN_HZ mot's rate law 0.05 Hz x 40^mot: 0.05 .. 2 Hz.
 * COL_MOT_RATE_OCT    log2(40).
 * COL_MOT_PHASE_PER_HZ one block's motion phase step per Hz: 32 / fs x 2^32.
 * COL_INC_PER_HZ      an oscillator's phase step per Hz: 2^32 / fs.
 * COL_F_MAX           no oscillator above 0.45 fs (an interval set two
 *                     octaves up on a high note must not wrap the step).
 * COL_W_LOG2_LO       log2(80 Hz): w(f) is 0 at and below 80 Hz (C-D14).
 * COL_W_OCTAVES       w(f) rises over 3 octaves: 1 at and above 640 Hz.
 * COL_NORM_WAV/PWM    the bus into the drive, 1 / (full scale x sqrt(N)):
 *                     detuned free-running oscillators add by power (C-D12).
 *                     The pulse swings twice the saw's peak (a row
 *                     difference), so its full scale is 65,536.
 * COL_GLIDE_ROWS      por 0..127 -> colEgK rows 0..105: 1 ms .. 2.0 s.
 */
#define COL_LOG2_440          8.78135971f
#define COL_CUT_BASE_HZ       20.0f
#define COL_CUT_ST_MAX        115.7f
#define COL_PI_OVER_FS        (3.14159265f / REAL_FS)
#define COL_SLEW              0.28f
#define COL_K_SPAN            1.95f
#define COL_FENV_ST           72.0f
#define COL_DRIVE_OCT         4.0f
#define COL_CLIP              1.5f
#define COL_CUBIC             (4.0f / 27.0f)
#define COL_OUT_SCALE         32767.0f
#define COL_EG_ATTACK_AIM     1.2f
#define COL_EG_SETTLE         0.01f
#define COL_EG_IDLE_LEVEL     1.0e-4f
#define COL_DETUNE_MAX_LN     0.0288811325f
#define COL_MOT_DEPTH_MAX     0.6f
#define COL_MOT_RATE_MIN_HZ   0.05f
#define COL_MOT_RATE_OCT      5.32192809f
#define COL_MOT_PHASE_PER_HZ  ((float)OUTPUT_DMA_SIZE * 4294967296.0f / REAL_FS)
#define COL_INC_PER_HZ        (4294967296.0f / REAL_FS)
#define COL_F_MAX             (0.45f * REAL_FS)
#define COL_W_LOG2_LO         6.32192809f
#define COL_W_OCTAVES         3.0f
#if COL_OSC_COUNT == 12u
#define COL_INV_SQRT_COUNT    0.288675135f    /* 1 / sqrt(12) */
#elif COL_OSC_COUNT == 10u
#define COL_INV_SQRT_COUNT    0.316227766f    /* 1 / sqrt(10) */
#else
#error "COL_OSC_COUNT: add its 1 / sqrt(N) here"
#endif
#define COL_NORM_WAV          (COL_INV_SQRT_COUNT / 32768.0f)
#define COL_NORM_PWM          (0.408248290f / 65536.0f)   /* 1 / sqrt(6) */
_Static_assert(COL_PWM_COUNT == 6u, "COL_NORM_PWM is 1 / sqrt(6)");
#define COL_GLIDE_ROWS        105u

/* x clamped to 0..1 (descriptor floats are byte / 127, but an LFO or
 * velocity overlay may push them past either end). */
static inline float col_clamp01(const float x)
{
	return fminf(fmaxf(x, 0.0f), 1.0f);
}

/*
 * tan(x) by the [3/2] Pade form x (15 - x^2) / (15 - 6 x^2) (S095 P2).
 *
 * What:       the same rational form as ResonantFilter.c fastTan(), one VDIV.
 * Why:        the prewarp g = tan(pi f / fs) per filter per block. fastTan()
 *             is an ITCM function with no header declaration; a local copy
 *             keeps the Colloid's control path in its own file.
 * Range:      x <= pi x 15,963 / fs = 1.137 (COL_CUT_ST_MAX): 0.4 % low there;
 *             the pole (x^2 = 2.5) is never reached.
 */
static inline float col_tanf(const float x)
{
	const float x2 = x * x;

	return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

/*
 * The octave row of a stock table for frequency f (S095 P2).
 *
 * What:       0..10: the number of octave edges (16.3516 Hz x 2^k) at or
 *             below f, from f's float bits: the exponent minus 4, plus 1 when
 *             the mantissa reaches the edges' mantissa 0x02D013, clamped.
 * Why:        osc_setTableFreq()'s law (Oscillator.c, S093 P3) for each
 *             oscillator's own frequency, so an interval set spanning two
 *             octaves stays band-limited on every member (draft §2.2). Ten
 *             compares become a few integer operations.
 * Inputs:     f > 0 (the caller's frequencies are positive).
 */
static inline uint32_t col_octave(const float f)
{
	union { float f; int32_t i; } v = { f };
	int32_t k = ((v.i >> 23) & 0xFF) - (127 + 4)
	          + (int32_t)((v.i & 0x007FFFFF) >= 0x0002D013);

	return (uint32_t)((k < 0) ? 0 : ((k > 10) ? 10 : k));
}

/*
 * Step one envelope by one block (S095 P2, draft §4.1).
 *
 * What:       attack: level += K[a] (1.2 - level), at 1.0 -> decay;
 *             decay: level += K[d] (s - level), within 0.01 of s -> sustain;
 *             sustain: keeps following s (K[d]); when the effective sustain
 *             switch is off -> release;
 *             release: level += K[r] (0 - level), below 1e-4 -> idle at 0.
 *             K = colEgK[byte & 127].
 * Why:        a lightweight ADSR: one multiply-add per block (C-D8 the switch,
 *             C-D19 its effective value). With susOn 0 the envelope runs
 *             attack, decay, release; with susOn 1 it holds at s. Switching
 *             susOn off while held (sustain stage) starts the release on that
 *             block; a decay still under way releases once it settles.
 * Inputs:     e, the time bytes a, d, r, the level s (0..1 float), susOn.
 * Outputs:    e->level, e->stage.
 * Accessors:  Colloid_calcVoiceAsync() (both envelopes, with the same susOn).
 */
static void col_egStep(ColEg *e, const uint8_t a, const uint8_t d, const float s,
                       const uint8_t r, const uint8_t susOn)
{
	const float sl = col_clamp01(s);

	switch (e->stage) {
	case COL_EG_ATTACK:
		e->level += colEgK[a & 127u] * (COL_EG_ATTACK_AIM - e->level);
		if (e->level >= 1.0f) {
			e->level = 1.0f;
			e->stage = COL_EG_DECAY;
		}
		break;
	case COL_EG_DECAY:
		e->level += colEgK[d & 127u] * (sl - e->level);
		if (fabsf(e->level - sl) <= COL_EG_SETTLE)
			e->stage = COL_EG_SUSTAIN;
		break;
	case COL_EG_SUSTAIN:
		/* the level keeps following slv, so a sustain-level change is smooth */
		e->level += colEgK[d & 127u] * (sl - e->level);
		if (!susOn)
			e->stage = COL_EG_RELEASE;
		break;
	case COL_EG_RELEASE:
		e->level -= colEgK[r & 127u] * e->level;
		if (e->level < COL_EG_IDLE_LEVEL) {
			e->level = 0.0f;
			e->stage = COL_EG_IDLE;
		}
		break;
	default:
		break;
	}
}

/*
 * One filter's output mix for this block (S095 P2, draft §3).
 *
 * What:       mH, mB, mL from the type and this block's k: LP (0, 0, 1),
 *             BP (0, 1, 0), HP (1, -k, -1), notch (1, -k, 0), peak (1, -k, -2);
 *             any other byte is LP.
 * Why:        every type is the same loop with other mix constants (constant
 *             cost). The mix steps once per block; k is slewed, so its steps
 *             are small.
 */
static void col_svfMix(ColSvf *f, const uint8_t type)
{
	switch (type) {
	case COL_FT_BP:    f->mH = 0.0f; f->mB = 1.0f;  f->mL = 0.0f;  break;
	case COL_FT_HP:    f->mH = 1.0f; f->mB = -f->k; f->mL = -1.0f; break;
	case COL_FT_NOTCH: f->mH = 1.0f; f->mB = -f->k; f->mL = 0.0f;  break;
	case COL_FT_PEAK:  f->mH = 1.0f; f->mB = -f->k; f->mL = -2.0f; break;
	default:           f->mH = 0.0f; f->mB = 0.0f;  f->mL = 1.0f;  break;
	}
}

/*
 * One filter's coefficients for this block (S095 P2, draft §3, C-D9).
 *
 * What:       1. the ramp of the previous block ended on its targets, so
 *                this block starts there: a = t;
 *             2. the cutoff target in semitones above 20 Hz,
 *                st = clamp(cut x 115.7 + envSt, 0, 115.7), and the damping
 *                target kT = 2 - 1.95 res; both slewed by 0.28 per block;
 *             3. hz = 20 x 2^(cutSt / 12); g = tan(pi hz / fs) (one VDIV);
 *                t1 = 1 / (1 + g (g + k)) (one VDIV), t2 = g t1, t3 = g t2;
 *             4. da = (t - a) / 32: the output loop adds da once per sample,
 *                so the coefficients reach t exactly at the block's end;
 *             5. the output mix (col_svfMix()).
 * Why:        smooth sweeps (the brief): the TPT filter is stable under the
 *             per-sample ramp, and the slew removes block steps from knob,
 *             LFO and envelope moves. 2 VDIV per filter per block, none per
 *             sample.
 * Inputs:     f, cut and res (0..1 floats), type (COL_FT_*), envSt (the
 *             filter envelope's offset in semitones).
 * Outputs:    f's a, t, da, cutSt, k and mix.
 * Accessors:  Colloid_calcVoiceAsync(), col_svfInit().
 */
static void col_svfControl(ColSvf *f, const float cut, const float res,
                           const uint8_t type, const float envSt)
{
	const float st = fminf(fmaxf(col_clamp01(cut) * COL_CUT_ST_MAX + envSt, 0.0f),
	                       COL_CUT_ST_MAX);
	const float kT = 2.0f - COL_K_SPAN * col_clamp01(res);
	float g;

	f->a1 = f->t1;
	f->a2 = f->t2;
	f->a3 = f->t3;
	f->cutSt += COL_SLEW * (st - f->cutSt);
	f->k += COL_SLEW * (kT - f->k);
	g = col_tanf(COL_CUT_BASE_HZ * alk_exp2f(f->cutSt * (1.0f / 12.0f)) * COL_PI_OVER_FS);
	f->t1 = 1.0f / (1.0f + g * (g + f->k));
	f->t2 = g * f->t1;
	f->t3 = g * f->t2;
	f->da1 = (f->t1 - f->a1) * (1.0f / (float)OUTPUT_DMA_SIZE);
	f->da2 = (f->t2 - f->a2) * (1.0f / (float)OUTPUT_DMA_SIZE);
	f->da3 = (f->t3 - f->a3) * (1.0f / (float)OUTPUT_DMA_SIZE);
	col_svfMix(f, type);
}

/*
 * A filter at rest on its knob settings (S095 P2).
 *
 * What:       clears the integrators, puts the slewed cutoff and damping on
 *             their targets, computes the coefficients, and makes the first
 *             block start on them (a = t, da = 0).
 * Accessors:  Colloid_initVoice().
 */
static void col_svfInit(ColSvf *f, const float cut, const float res, const uint8_t type)
{
	f->ic1 = 0.0f;
	f->ic2 = 0.0f;
	f->cutSt = col_clamp01(cut) * COL_CUT_ST_MAX;
	f->k = 2.0f - COL_K_SPAN * col_clamp01(res);
	col_svfControl(f, cut, res, type, 0.0f);
	f->a1 = f->t1;
	f->a2 = f->t2;
	f->a3 = f->t3;
	f->da1 = 0.0f;
	f->da2 = 0.0f;
	f->da3 = 0.0f;
}

/*
 * Bind the wavetable (S095 P2; the Alkaline's alk_wtbBind() rule).
 *
 * What:       records the key (COL_BIND_WTB, the table = osc.waveform clamped
 *             to the last installed table, the catalogue generation), then
 *             sampleCacheSize = the table's frame count and sampleCacheData =
 *             its first 1,024-point row (frame f at + f x 1,024). With no
 *             table installed: one frame and NULL (the async then sets the
 *             input gain to 0: silence, with the filters still running).
 * Why:        the Colloid binds its own tables (draft §2.3): osc_userBind() is
 *             never called on a Colloid oscillator. It runs only when the key
 *             changes; noinline keeps its registers out of the async's path.
 * Accessors:  Colloid_calcVoiceAsync(), col_wtbTrigger().
 * Affiliates: SampleMemory.h sampleMemory_wavetableEntry(),
 *             sampleMemory_wavetableRows().
 */
static __attribute__((noinline)) void col_wtbBind(ColloidVoice *voice)
{
	OscInfo *const osc = &voice->osc;
	const uint8_t count = sampleMemory_getNumWavetables();
	const uint8_t index = (osc->waveform < count) ? osc->waveform
	                    : (uint8_t)(count ? count - 1u : 0u);
	const wavetable_catalogue_entry_t *e = sampleMemory_wavetableEntry(index);

	osc->sampleCacheLooped = COL_BIND_WTB;
	osc->sampleCacheIndex = index;
	osc->sampleCacheGeneration = sampleMemory_getGeneration();
	osc->sampleCacheSize = e ? e->frame_count : 1u;
	osc->sampleCacheData = e ? sampleMemory_wavetableRows(e) : 0;
}

/* Whether the bound table no longer matches (alk_wtbKeyChanged()'s rule). */
static inline uint8_t col_wtbKeyChanged(const OscInfo *osc)
{
	return (uint8_t)(osc->sampleCacheLooped != COL_BIND_WTB ||
	                 osc->waveform != osc->sampleCacheIndex ||
	                 sampleMemory_getGeneration() != osc->sampleCacheGeneration);
}

/*
 * Latch every oscillator's frame at a non-legato trigger (S095 P2, C-D4/C-D5).
 *
 * What:       binds first when the key changed; then, with F frames,
 *             centre = round(pos x (F - 1) / 127), and for each oscillator
 *             frame = wrap(centre + round(r x psp x F / 2)), r = rnd / 32768
 *             (-1..+1). psp 0 puts every oscillator on the centre frame; at
 *             psp 1 the frames reach half a table either side. |offset| <=
 *             F / 2, so one add or subtract of F wraps it.
 * Why:        "a spread that picks a random index further from the centre as
 *             the spread rises" (the brief), within the selected table (C-D5);
 *             latched, so a held note never steps between frames (C-D4).
 * Accessors:  Colloid_triggerVoice() (main loop).
 */
static void col_wtbTrigger(ColloidVoice *voice)
{
	OscInfo *const osc = &voice->osc;
	uint32_t frames, centre, i;
	float half;

	if (col_wtbKeyChanged(osc))
		col_wtbBind(voice);
	frames = osc->sampleCacheSize;
	centre = ((uint32_t)(osc->position & 127u) * (frames - 1u) + 63u) / 127u;
	half = col_clamp01(voice->posSpread) * (float)frames * (0.5f / 32768.0f);
	/* main loop only: kept rolled (flash, not cycles) */
#pragma GCC unroll 1
	for (i = 0u; i < COL_OSC_MAX; i++) {
		const float off = (float)voice->rnd[i] * half;
		int32_t fr = (int32_t)centre + (int32_t)(off + ((off >= 0.0f) ? 0.5f : -0.5f));

		if (fr < 0)
			fr += (int32_t)frames;
		else if (fr >= (int32_t)frames)
			fr -= (int32_t)frames;
		voice->frame[i] = (uint8_t)fr;
	}
}

/*
 * One oscillator's pwm width target (S095 P2, draft §2.4, U.4).
 *
 * What:       pos + rnd x scale in pos space, scale = psp x 63.5 / 32768 (so
 *             the offset is r x psp x 63.5, r = rnd / 32768 in -1..+1),
 *             folded back at 0 and at 127 (|offset| <= 63, so one fold
 *             suffices).
 * Why:        every oscillator has its own width around the centre pos; the
 *             width law itself is the shipped oscPwmDRec[] (pos 0 = 50 %,
 *             127 = 0.39 %). pos and scale are passed in, computed once per
 *             call site: read through voice-> inside the loop they were
 *             reloaded for every oscillator (the loop stores into the voice).
 * Inputs:     pos (osc.position & 127), rnd (the oscillator's draw), scale.
 * Accessors:  col_oscControl(), Colloid_triggerVoice(), Colloid_setOscType().
 */
static inline uint8_t col_pwmTarget(const int32_t pos, const int16_t rnd, const float scale)
{
	int32_t v = pos + (int32_t)((float)rnd * scale);

	if (v < 0)
		v = -v;
	if (v > 127)
		v = 254 - v;
	return (uint8_t)((v < 0) ? 0 : v);
}

/* The pwm target's scale: psp x 63.5 / 32768 (col_pwmTarget()). */
static inline float col_pwmScale(const ColloidVoice *voice)
{
	return col_clamp01(voice->posSpread) * (63.5f / 32768.0f);
}

/*
 * Every oscillator's increment, row, width and stereo position for this
 * block (S095 P2, draft §2.2, §2.6).
 *
 * What:       for each of the `count` oscillators o:
 *             - member m = colMember[n - 1][o] of the interval set (n
 *               members), its offset st semitones;
 *             - f = f0 x 2^(st / 12) x (1 + c_o x delta), c_o = colDetC[o],
 *               delta = dtn^2 x 50 cents (in ln units; within 0.7 cent of the
 *               exponential at 50 cents), clamped to 0.45 fs;
 *             - inc = f x 2^32 / fs;
 *             - row: wtb, the latched frame of the bound table (the last
 *               frame if the table shrank); Sin, the 1,024-point sine;
 *               pwm, the octave row of sawTable; Saw/Rec/Tri, the octave row
 *               of that table (col_octave(f));
 *             - pwm: the width index slews toward col_pwmTarget() by at most
 *               OSC_PWM_SLEW_STEPS (2) per block, and pwD = oscPwmDRec[index];
 *             - the position: the motion phase advances by
 *               rate x colMotK[o]; m = 4 t (1 - |t|) (a parabolic sine,
 *               t = phase / 2^31 in -1..1);
 *               w = clamp((log2 f - log2 80) / 3, 0, 1), log2 f taken as
 *               pitchLog2 + st / 12 + c_o delta log2(e) (no log per
 *               oscillator);
 *               p = w x clamp(spr x colHome[o] + depth x m, -1, +1);
 *               the pair's two Q15 positions packed into posPair[o / 2].
 * Why:        all per block: the per-sample loops only read the rows and
 *             add. Lower notes sit nearer the centre and move less (w, C-D14);
 *             each oscillator moves at its own rate (C-D15).
 * Inputs:     voice (parameters, osc, the bind, rnd, frame), count
 *             (COL_OSC_COUNT or COL_PWM_COUNT), f0 (the base frequency, Hz),
 *             isPwm, isWtb.
 * Outputs:    inc[], row[], pwPos[], pwD[], motPhase[], posPair[].
 * Accessors:  Colloid_calcVoiceAsync().
 */
static void col_oscControl(ColloidVoice *voice, const uint32_t count, const float f0,
                           const uint8_t isPwm, const uint8_t isWtb)
{
	const OscInfo *const osc = &voice->osc;
	const uint32_t iv = (voice->interval < COL_INTERVAL_COUNT)
	                  ? voice->interval : (COL_INTERVAL_COUNT - 1u);
	const int8_t *const semis = colIntervals[iv];
	const uint8_t *const memberOf = colMember[colIntervalCount[iv] - 1u];
	const float dtn = col_clamp01(voice->detune);
	const float delta = dtn * dtn * COL_DETUNE_MAX_LN;
	const float spr = col_clamp01(voice->spread);
	const float mot = col_clamp01(voice->motion);
	/* mot 0 is still: no depth, no movement */
	const float depth = COL_MOT_DEPTH_MAX * __builtin_sqrtf(mot);
	const float motStep = (mot > 0.0f)
	    ? COL_MOT_RATE_MIN_HZ * alk_exp2f(COL_MOT_RATE_OCT * mot) * COL_MOT_PHASE_PER_HZ
	    : 0.0f;
	const float wBase = voice->pitchLog2 - COL_W_LOG2_LO;
	const uint8_t wave = osc->waveform;
	/* under pwm the pulse is the saw row's difference; under wav the list */
	const int16_t (*const stock)[1024] = isPwm ? (const int16_t (*)[1024])sawTable
	    : ((wave == COL_WAVE_REC) ? (const int16_t (*)[1024])recTable
	    : ((wave == COL_WAVE_TRI) ? (const int16_t (*)[1024])triTable
	                              : (const int16_t (*)[1024])sawTable));
	const uint8_t isSin = (uint8_t)(!isPwm && !isWtb && wave == COL_WAVE_SIN);
	const int16_t *const wtbBase = osc->sampleCacheData;
	const uint32_t frames = osc->sampleCacheSize ? osc->sampleCacheSize : 1u;
	/* pwm: the centre width and the spread scale, once per block */
	const int32_t pwPos0 = (int32_t)(osc->position & 127u);
	const float pwScale = col_pwmScale(voice);
	int32_t qEven = 0;
	uint32_t i;

	for (i = 0u; i < count; i++) {
		const int32_t st = semis[memberOf[i]];
		const float cd = colDetC[i] * delta;
		const float f = fminf(f0 * colRatio[st - COL_RATIO_LOW] * (1.0f + cd), COL_F_MAX);

		voice->inc[i] = (uint32_t)(f * COL_INC_PER_HZ);
		if (isWtb) {
			const uint32_t fr = (voice->frame[i] < frames) ? voice->frame[i] : (frames - 1u);

			voice->row[i] = wtbBase ? wtbBase + fr * WAVETABLE_ROW_POINTS : sawTable[0];
		} else if (isSin) {
			voice->row[i] = sine_table;
		} else {
			voice->row[i] = stock[col_octave(f)];
		}
		if (isPwm) {
			int32_t d = (int32_t)col_pwmTarget(pwPos0, voice->rnd[i], pwScale) - (int32_t)voice->pwPos[i];

			if (d > OSC_PWM_SLEW_STEPS)
				d = OSC_PWM_SLEW_STEPS;
			else if (d < -OSC_PWM_SLEW_STEPS)
				d = -OSC_PWM_SLEW_STEPS;
			voice->pwPos[i] = (uint8_t)((int32_t)voice->pwPos[i] + d);
			voice->pwD[i] = oscPwmDRec[voice->pwPos[i] & 127u];
		}
		voice->motPhase[i] += (uint32_t)(motStep * colMotK[i]);
		{
			const float t = (float)(int32_t)voice->motPhase[i] * (1.0f / 2147483648.0f);
			const float m = 4.0f * t * (1.0f - fabsf(t));
			const float w = col_clamp01((wBase + (float)st * (1.0f / 12.0f) + cd * ALK_LOG2E)
			                            * (1.0f / COL_W_OCTAVES));
			const float p = fminf(fmaxf(spr * colHome[i] + depth * m, -1.0f), 1.0f) * w;
			const int32_t q = (int32_t)(p * 32767.0f);

			if (i & 1u)
				voice->posPair[i >> 1] = ((uint32_t)qEven & 0xFFFFu) | ((uint32_t)q << 16);
			else
				qEven = q;
		}
	}
}

void Colloid_initVoice(ColloidVoice *voice)
{
	uint32_t i;

	if (!voice)
		return;
	/* the pitch anchor: typ wav, Saw, coa 48 (C3 at the default note 63),
	 * fin at its centre 63 (init.col osc1_type 0, osc1_wave 0) */
	voice->osc.oscType = OSC_TYPE_WAV;
	voice->osc.waveform = COL_WAVE_SAW;
	voice->osc.position = 0u;
	voice->osc.freq = 130.813f;
	voice->osc.pitchMod = 1.0f;
	voice->osc.modNodeValue = 1.0f;
	voice->osc.midiFreq = (uint16_t)((48u << 8) | 63u);
	voice->osc.baseNote = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->osc.sampleCacheLooped = COL_BIND_NONE;
	voice->osc.sampleCacheSize = 1u;
	voice->osc.sampleCacheData = 0;
	lfo_init(&voice->lfo);

	/* the bytes of SD_CARD/Instrument/Colloid/init.col, as the descriptor
	 * writers store them (TYPE_FLT byte / 127, TYPE_UINT8 the byte), so a
	 * voice before its first apply and a freshly loaded init.col match */
	voice->posSpread = 0.0f;
	voice->sustainLevel = 90.0f / 127.0f;
	voice->fSustain = 64.0f / 127.0f;
	voice->fAmount = 63.0f / 127.0f;
	voice->detune = 40.0f / 127.0f;
	voice->spread = 80.0f / 127.0f;
	voice->motion = 30.0f / 127.0f;
	voice->cutA = 90.0f / 127.0f;
	voice->resA = 20.0f / 127.0f;
	voice->cutB = 90.0f / 127.0f;
	voice->resB = 20.0f / 127.0f;
	voice->drive = 0.0f;
	voice->vol = 100.0f / 127.0f;
	voice->attack = 40u;
	voice->decay = 70u;
	voice->release = 70u;
	voice->fAttack = 0u;
	voice->fDecay = 70u;
	voice->fRelease = 70u;
	voice->glide = 0u;
	voice->pan = 63u;
	voice->sustain = 1u;
	voice->tie = 0u;
	voice->glideMode = COL_PMD_ALL;
	voice->interval = 0u;
	voice->filtMode = COL_FMD_PAR;
	voice->typeA = COL_FT_LP;
	voice->typeB = COL_FT_LP;

	/* free-running from a random start: the stack never starts phase-aligned
	 * (GetRngValue(): the hardware RNG; main loop or boot, never audio) */
#pragma GCC unroll 1
	for (i = 0u; i < COL_OSC_MAX; i++) {
		voice->phase[i] = ((uint32_t)(uint16_t)GetRngValue() << 16)
		                | (uint32_t)(uint16_t)GetRngValue();
		voice->motPhase[i] = ((uint32_t)(uint16_t)GetRngValue() << 16)
		                   | (uint32_t)(uint16_t)GetRngValue();
		voice->inc[i] = 0u;
		voice->row[i] = sawTable[0];
		voice->rnd[i] = 0;
		voice->frame[i] = 0u;
		voice->pwPos[i] = 0u;
		voice->pwD[i] = oscPwmDRec[0];
	}
	for (i = 0u; i < COL_PAIR_MAX; i++)
		voice->posPair[i] = 0u;
	memset(voice->busM, 0, sizeof(voice->busM));
	memset(voice->busS, 0, sizeof(voice->busS));
	memset(voice->right, 0, sizeof(voice->right));
	voice->ampEg.level = 0.0f;
	voice->ampEg.stage = COL_EG_IDLE;
	voice->fltEg.level = 0.0f;
	voice->fltEg.stage = COL_EG_IDLE;
	col_svfInit(&voice->svfA, voice->cutA, voice->resA, voice->typeA);
	col_svfInit(&voice->svfB, voice->cutB, voice->resB, voice->typeB);
	voice->note = MIDI_DEFAULT_TRIGGER_NOTE;
	voice->gliding = 0u;
	voice->pitchJump = 1u;
	voice->pitchLog2 = alk_log2f(130.813f);
	voice->ampLast = 0.0f;
	voice->inGain = COL_NORM_WAV;
}

void Colloid_triggerVoice(ColloidVoice *voice, const uint8_t source_slot,
                          const uint8_t velocity, const uint8_t note)
{
	/* the velocity as the modulation system's 0..1 value (Drum: vol/127.f) */
	const float vel = (float)velocity * (1.0f / 127.0f);
	const uint8_t stage = voice->ampEg.stage;
	/* held: the amp envelope has not started its release */
	const uint8_t held = (uint8_t)(stage == COL_EG_ATTACK || stage == COL_EG_DECAY ||
	                               stage == COL_EG_SUSTAIN);
	/* legato: lgt on (a step lock lands before this trigger) and held */
	const uint8_t legato = (uint8_t)(voice->tie != 0u && held);
	uint32_t i;

	/* --- platform part (as Drum/Alkaline): LFO and velocity, per slot --- */
	lfo_retrigger(source_slot);
	modNode_updateValue(&velocityModulators[source_slot], vel);
	instrumentManager_applyVelocityModulationTarget(source_slot, vel);

	voice->note = note;
	voice->gliding = (uint8_t)(legato || voice->glideMode == COL_PMD_ALL);
	if (legato)
		return;
	/* a new note: both attacks restart from their current levels */
	voice->ampEg.stage = COL_EG_ATTACK;
	voice->fltEg.stage = COL_EG_ATTACK;
#pragma GCC unroll 1
	for (i = 0u; i < COL_OSC_MAX; i++)
		voice->rnd[i] = GetRngValue();
	if (voice->osc.oscType == OSC_TYPE_WTB) {
		col_wtbTrigger(voice);
	} else if (voice->osc.oscType == OSC_TYPE_PWM) {
		/* the widths start on their targets (no slew across a new note) */
		const int32_t pos = (int32_t)(voice->osc.position & 127u);
		const float scale = col_pwmScale(voice);

#pragma GCC unroll 1
		for (i = 0u; i < COL_OSC_MAX; i++)
			voice->pwPos[i] = col_pwmTarget(pos, voice->rnd[i], scale);
	}
}

void Colloid_calcVoiceAsync(ColloidVoice *voice)
{
	OscInfo *const osc = &voice->osc;
	const uint8_t isPwm = (uint8_t)(osc->oscType == OSC_TYPE_PWM);
	const uint8_t isWtb = (uint8_t)(osc->oscType == OSC_TYPE_WTB);
	const uint32_t count = isPwm ? COL_PWM_COUNT : COL_OSC_COUNT;
	/*
	 * The effective sustain switch (C-D8, C-D19).
	 *
	 * What:       sus on and the transport running. Read once per block.
	 * Why:        nothing tells an engine that a note has ended. With the
	 *             transport stopped, a pad, a MIDI note or the preview plays
	 *             attack, decay, release; a transport stop releases a held
	 *             note on the next block (the sustain stage sees 0), with no
	 *             edge flag and no ISR-to-engine call.
	 */
	const uint8_t susOn = (uint8_t)(voice->sustain != 0u && seq_isRunning() != 0u);
	float target, f0, fam, envSt;

	/* envelopes: the amp ramp starts on the previous block's end */
	voice->ampLast = voice->ampEg.level;
	col_egStep(&voice->ampEg, voice->attack, voice->decay, voice->sustainLevel,
	           voice->release, susOn);
	col_egStep(&voice->fltEg, voice->fAttack, voice->fDecay, voice->fSustain,
	           voice->fRelease, susOn);

	/*
	 * Pitch (the Alkaline's formula, S093 P3): log2 of the note plus coarse
	 * minus the default note 63, from 440 Hz at note 69, plus the fine byte's
	 * detune (midiPitch_detune() is 1 + x, |x| <= 0.03: log2 by its second
	 * order). osc.baseNote is kept for the coarse writer's osc_recalcFreq().
	 */
	{
		int16_t n = (int16_t)(osc->midiFreq >> 8) + (int16_t)voice->note
		          - (int16_t)MIDI_DEFAULT_TRIGGER_NOTE;
		const float x = midiPitch_detune((uint8_t)(osc->midiFreq & 0xffu)) - 1.0f;

		n = (n < 0) ? 0 : ((n > 127) ? 127 : n);
		osc->baseNote = voice->note;
		target = COL_LOG2_440 + ((float)n - 69.0f) * (1.0f / 12.0f)
		       + x * (ALK_LOG2E - 0.5f * ALK_LOG2E * x);
	}
	/*
	 * Portamento (draft §2.2): a per-block one-pole on log2 f. por 0 is off;
	 * por 1..127 takes colEgK row por x 105 / 127 (1 ms .. 2 s). A first
	 * note (pitchJump) or a note that does not glide jumps.
	 */
	if (voice->pitchJump || !voice->gliding || voice->glide == 0u)
		voice->pitchLog2 = target;
	else
		voice->pitchLog2 += colEgK[((uint32_t)(voice->glide & 127u) * COL_GLIDE_ROWS
		                            + 63u) / 127u] * (target - voice->pitchLog2);
	voice->pitchJump = 0u;
	/* the base frequency: osc.modNodeValue is the coa/fin modulation multiplier */
	f0 = alk_exp2f(voice->pitchLog2) * osc->modNodeValue;

	/* wtb: bind on a key change (an install, or a wav change by the menu,
	 * Morph, an LFO or automation) */
	if (isWtb && col_wtbKeyChanged(osc))
		col_wtbBind(voice);
	/*
	 * The bus into the drive (C-D12): 1 / (full scale x sqrt(N)) times the
	 * drive gain 2^(4 drv). wtb with no table installed is silenced here
	 * (the rows point at a stock row so the loops still read valid memory).
	 */
	voice->inGain = (isPwm ? COL_NORM_PWM : ((isWtb && !osc->sampleCacheData) ? 0.0f : COL_NORM_WAV))
	              * alk_exp2f(COL_DRIVE_OCT * col_clamp01(voice->drive));

	col_oscControl(voice, count, f0, isPwm, isWtb);

	/*
	 * Filters (draft §3, §4.2): fam (63 = none) x the filter envelope x 72
	 * semitones moves both cutoffs together (C-D17); each keeps its knob.
	 */
	fam = fminf(fmaxf((voice->fAmount * 127.0f - 63.0f) * (1.0f / 63.0f), -1.0f), 1.0f);
	envSt = fam * COL_FENV_ST * voice->fltEg.level;
	col_svfControl(&voice->svfA, voice->cutA, voice->resA, voice->typeA, envSt);
	col_svfControl(&voice->svfB, voice->cutB, voice->resB, voice->typeB, envSt);
}

/*
 * The drive: x' = clamp(x, +-1.5), y = x' - (4/27) x'^3 (draft §3).
 * A cubic soft clip with no division; 1.0 at the clamp.
 */
static inline float col_drive(const float x)
{
	const float c = fminf(fmaxf(x, -COL_CLIP), COL_CLIP);

	return c - COL_CUBIC * c * c * c;
}

/*
 * The parallel (stereo) filter and output loop (S095 P2, draft §3, §5).
 *
 * What:       per sample n:
 *               L = M + 2S, R = M - 2S (the side bus carries s p / 2);
 *               xl = drive(L x inGain), xr = drive(R x inGain);
 *               filter A on xl, filter B on xr (the TPT step:
 *                 v3 = x - ic2; v1 = a1 ic1 + a2 v3; v2 = ic2 + a2 ic1 + a3 v3;
 *                 ic1 = 2 v1 - ic1; ic2 = 2 v2 - ic2;
 *                 y = mH x + mB v1 + mL v2);
 *               outL[n], right[n] = SSAT16(y x amp), amp ramping linearly
 *               from the previous block's envelope level to this block's
 *               (x 32,767);
 *               every coefficient += its per-sample step.
 *             The integrator states are written back; the coefficients are
 *             not (the next async starts the ramp on the targets).
 * Why:        the draft's measured f5_par form (72 per sample, no VDIV).
 *             noinline: one call per block, kept out of the sync block's and
 *             the mixer's register allocation.
 * Accessors:  Colloid_calcVoiceSyncBlock() (fmd par).
 */
static __attribute__((noinline)) void col_renderPar(ColloidVoice *voice, int16_t *outL)
{
	ColSvf *const A = &voice->svfA;
	ColSvf *const B = &voice->svfB;
	const int32_t *const busM = voice->busM;
	const int32_t *const busS = voice->busS;
	int16_t *const outR = voice->right;
	const float gin = voice->inGain;
	const float ampStep = (voice->ampEg.level - voice->ampLast)
	                    * (COL_OUT_SCALE / (float)OUTPUT_DMA_SIZE);
	const float da1A = A->da1, da2A = A->da2, da3A = A->da3;
	const float da1B = B->da1, da2B = B->da2, da3B = B->da3;
	const float mHA = A->mH, mBA = A->mB, mLA = A->mL;
	const float mHB = B->mH, mBB = B->mB, mLB = B->mL;
	float a1A = A->a1, a2A = A->a2, a3A = A->a3, ic1A = A->ic1, ic2A = A->ic2;
	float a1B = B->a1, a2B = B->a2, a3B = B->a3, ic1B = B->ic1, ic2B = B->ic2;
	float amp = voice->ampLast * COL_OUT_SCALE;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const int32_t m = busM[n];
		const int32_t s2 = busS[n] * 2;
		const float xl = col_drive((float)(m + s2) * gin);
		const float xr = col_drive((float)(m - s2) * gin);
		float v1, v2, v3, yl, yr;

		v3 = xl - ic2A;
		v1 = a1A * ic1A + a2A * v3;
		v2 = ic2A + a2A * ic1A + a3A * v3;
		ic1A = 2.0f * v1 - ic1A;
		ic2A = 2.0f * v2 - ic2A;
		yl = mHA * xl + mBA * v1 + mLA * v2;

		v3 = xr - ic2B;
		v1 = a1B * ic1B + a2B * v3;
		v2 = ic2B + a2B * ic1B + a3B * v3;
		ic1B = 2.0f * v1 - ic1B;
		ic2B = 2.0f * v2 - ic2B;
		yr = mHB * xr + mBB * v1 + mLB * v2;

		outL[n] = (int16_t)__SSAT((int32_t)(yl * amp), 16);
		outR[n] = (int16_t)__SSAT((int32_t)(yr * amp), 16);
		a1A += da1A; a2A += da2A; a3A += da3A;
		a1B += da1B; a2B += da2B; a3B += da3B;
		amp += ampStep;
	}
	A->ic1 = ic1A; A->ic2 = ic2A;
	B->ic1 = ic1B; B->ic2 = ic2B;
}

/*
 * The series (mono) filter and output loop (S095 P2, draft §3, C-D16).
 *
 * What:       per sample: x = drive(M x inGain) -> filter A -> drive(y_A)
 *             (the shaper alone: the gain was applied once, before A) ->
 *             filter B -> y; outL[n] = right[n] = SSAT16(y x amp). The side
 *             bus is not read: in series the voice is mono and the
 *             positions have no effect (spr and mot show "---").
 * Why:        the second shaper bounds the first filter's resonant peak into
 *             the second filter, the only drive the brief allows after A.
 * Accessors:  Colloid_calcVoiceSyncBlock() (fmd ser).
 */
static __attribute__((noinline)) void col_renderSer(ColloidVoice *voice, int16_t *outL)
{
	ColSvf *const A = &voice->svfA;
	ColSvf *const B = &voice->svfB;
	const int32_t *const busM = voice->busM;
	int16_t *const outR = voice->right;
	const float gin = voice->inGain;
	const float ampStep = (voice->ampEg.level - voice->ampLast)
	                    * (COL_OUT_SCALE / (float)OUTPUT_DMA_SIZE);
	const float da1A = A->da1, da2A = A->da2, da3A = A->da3;
	const float da1B = B->da1, da2B = B->da2, da3B = B->da3;
	const float mHA = A->mH, mBA = A->mB, mLA = A->mL;
	const float mHB = B->mH, mBB = B->mB, mLB = B->mL;
	float a1A = A->a1, a2A = A->a2, a3A = A->a3, ic1A = A->ic1, ic2A = A->ic2;
	float a1B = B->a1, a2B = B->a2, a3B = B->a3, ic1B = B->ic1, ic2B = B->ic2;
	float amp = voice->ampLast * COL_OUT_SCALE;
	uint32_t n;

	for (n = 0u; n < OUTPUT_DMA_SIZE; n++) {
		const float x = col_drive((float)busM[n] * gin);
		float v1, v2, v3, y;
		int16_t o;

		v3 = x - ic2A;
		v1 = a1A * ic1A + a2A * v3;
		v2 = ic2A + a2A * ic1A + a3A * v3;
		ic1A = 2.0f * v1 - ic1A;
		ic2A = 2.0f * v2 - ic2A;
		y = col_drive(mHA * x + mBA * v1 + mLA * v2);

		v3 = y - ic2B;
		v1 = a1B * ic1B + a2B * v3;
		v2 = ic2B + a2B * ic1B + a3B * v3;
		ic1B = 2.0f * v1 - ic1B;
		ic2B = 2.0f * v2 - ic2B;
		y = mHB * y + mBB * v1 + mLB * v2;

		o = (int16_t)__SSAT((int32_t)(y * amp), 16);
		outL[n] = o;
		outR[n] = o;
		a1A += da1A; a2A += da2A; a3A += da3A;
		a1B += da1B; a2B += da2B; a3B += da3B;
		amp += ampStep;
	}
	A->ic1 = ic1A; A->ic2 = ic2A;
	B->ic1 = ic1B; B->ic2 = ic2B;
}

void Colloid_calcVoiceSyncBlock(ColloidVoice *voice, int16_t *buf, uint8_t size)
{
	/*
	 * One block (S095 P2).
	 *
	 * What:       the pair loops (the first pair stores the buses, every
	 *             further pair adds): COL_PWM_COUNT / 2 pwm pairs under typ
	 *             pwm, COL_OSC_COUNT / 2 plain pairs otherwise; then the
	 *             filter/output loop of the mode (fmd 0 parallel, any other
	 *             value series).
	 * Why:        one branch per block for the type and one for the mode,
	 *             outside every loop; the count depends on typ only (constant
	 *             cost). Both modes run both filters every block.
	 * Inputs:     voice (the async ran this block), buf (the left output).
	 * Outputs:    buf, voice->right; phases, buses, filter states.
	 */
	const uint8_t isPwm = (uint8_t)(voice->osc.oscType == OSC_TYPE_PWM);
	const uint32_t pairs = (isPwm ? COL_PWM_COUNT : COL_OSC_COUNT) / 2u;
	uint32_t j;

	(void)size;   /* the voice always renders OUTPUT_DMA_SIZE */
	if (isPwm) {
		col_pwmPairStore(&voice->row[0], &voice->phase[0], &voice->inc[0], &voice->pwD[0],
		                 voice->posPair[0], voice->busM, voice->busS);
		for (j = 1u; j < pairs; j++)
			col_pwmPairAdd(&voice->row[2u * j], &voice->phase[2u * j], &voice->inc[2u * j],
			               &voice->pwD[2u * j], voice->posPair[j], voice->busM, voice->busS);
	} else {
		col_oscPairStore(&voice->row[0], &voice->phase[0], &voice->inc[0],
		                 voice->posPair[0], voice->busM, voice->busS);
		for (j = 1u; j < pairs; j++)
			col_oscPairAdd(&voice->row[2u * j], &voice->phase[2u * j], &voice->inc[2u * j],
			               voice->posPair[j], voice->busM, voice->busS);
	}
	if (voice->filtMode == COL_FMD_PAR)
		col_renderPar(voice, buf);
	else
		col_renderSer(voice, buf);
}

uint8_t Colloid_isQuiet(const ColloidVoice *voice)
{
	return (uint8_t)(voice->ampEg.stage == COL_EG_IDLE);
}

void Colloid_setOscType(ColloidVoice *voice, const uint8_t typ)
{
	OscInfo *const osc = &voice->osc;
	/* the byte -> the platform type: 0 wav, 1 wtb, 2 and above pwm */
	const uint8_t type = (typ == COL_TYP_WAV) ? OSC_TYPE_WAV
	                   : ((typ == COL_TYP_WTB) ? OSC_TYPE_WTB : OSC_TYPE_PWM);
	/* the new list's length: Saw..Sin, Rec, or the installed tables */
	const uint8_t count = (type == OSC_TYPE_WTB) ? sampleMemory_getNumWavetables()
	                    : ((type == OSC_TYPE_PWM) ? (uint8_t)COL_PWM_WAVE_COUNT
	                                              : (uint8_t)COL_WAVE_COUNT);
	const uint8_t last = count ? (uint8_t)(count - 1u) : 0u;
	uint32_t i;

	if (type == osc->oscType)
		return;
	osc->oscType = type;
	/* the stored byte is used as it is, clamped to an entry that exists
	 * (the platform's typ rule, S087 P0 r8) */
	if (osc->waveform > last)
		osc->waveform = last;
	osc->sampleCacheLooped = COL_BIND_NONE;
	if (type == OSC_TYPE_PWM) {
		const int32_t pos = (int32_t)(osc->position & 127u);
		const float scale = col_pwmScale(voice);

#pragma GCC unroll 1
		for (i = 0u; i < COL_OSC_MAX; i++)
			voice->pwPos[i] = col_pwmTarget(pos, voice->rnd[i], scale);
	}
}
```

#### S1.8 `Core/DSP/Instruments/Colloid/ColloidParameters.h`: add (new file)

```c
/*
 * ColloidParameters.h - the Colloid descriptor table and menu layout
 * (S095 P2).
 *
 * What:       the registry-facing exports of ColloidParameters.c: the
 *             descriptor rows, their count, the eight voice sub-pages, the
 *             display label and the type flags (AlkalineParameters.h shape),
 *             and the menu's inactive-cell test.
 * Why:        InstrumentManager's instrument_registry[] imports these, as it
 *             imports the five other engines' tables.
 * Accessors:  InstrumentManager.c (registry row; instrumentManager_cell-
 *             Inactive()).
 * Affiliates: ColloidVoice.h (the bound members); check_special_tags.py
 *             (TABLES['COL']); tools/colloid/make_init_col.py (the keys).
 */
#ifndef COLLOID_PARAMETERS_H_
#define COLLOID_PARAMETERS_H_

#include "InstrumentManager.h"
#include "ColloidVoice.h"

/* 47 rows: 6 oscillator, 6 amp envelope, 2 glide, 4 unison, 5 filter
 * envelope, 8 filter, vol/pan, 8 LFO rows, 2 velocity rows, 4 LFO target
 * rows (asserted against colloid_param_index_t in the .c) */
#define COLLOID_PARAM_DESCRIPTOR_COUNT 47u
/* eight sub-pages: the front-panel SELECT1..8 buttons (menu.c) */
#define COLLOID_MENU_PAGE_COUNT        8u
/* Basic (C-D1, user 2026-11-12): any slot, any number per Scene, held to
 * the Basic share. A type with no class flag is not selectable (S094 P2). */
#define COLLOID_INSTRUMENT_TYPE_FLAGS  INSTRUMENT_FLAG_BASIC

extern const ParamDescriptor colloid_param_descriptors[];
extern const uint8_t colloid_param_descriptor_count;
extern const instrument_menu_page_t colloid_menu_pages[];
extern const uint8_t colloid_menu_page_count;
extern const char colloid_instrument_display_label[];
extern const uint8_t colloid_instrument_type_flags;

/*
 * Whether a Colloid cell has no effect in the voice's current mode
 * (S095 P2, C-D16, C-D20).
 *
 * What:       1 for pos and psp under typ wav (the oscillators are
 *             free-running: no start phase, no frame, no width), and for spr
 *             and mot in fmd ser (the chain is mono); else 0.
 * Why:        the compact cell shows "---" for a value that changes nothing
 *             (menu_formatCellValue3()); the value is still stored and edited,
 *             and takes effect when the mode changes.
 * Inputs:     voice (the slot's runtime member), descriptor (a row of
 *             colloid_param_descriptors[], or any other pointer: 0).
 * Accessors:  InstrumentManager.c instrumentManager_cellInactive().
 */
uint8_t colloidParameters_cellInactive(const ColloidVoice *voice,
                                       const ParamDescriptor *descriptor);

#endif /* COLLOID_PARAMETERS_H_ */
```

#### S1.9 `Core/DSP/Instruments/Colloid/ColloidParameters.c`: add (new file)

```c
#include "ColloidParameters.h"
#include "ColloidVoice.h"
#include "menu.h"
#include "MenuText.h"
#include <stddef.h>

/*
 * Colloid parameter source of truth (S095 P2).
 *
 * What:       what each generic Kit storage cell means when the slot holds a
 *             Colloid: file key, menu text, dtype, Morph/modulation flags,
 *             modulation domain and the runtime binding into ColloidVoice.
 *             The pattern is AlkalineParameters.c; every row is on one line
 *             (check_special_tags.py and make_init_col.py parse them).
 * Why:        S095 P2 draft §8 with C-D20 (the platform's typ / wav / pos
 *             shape): the oscillator (coa fin wav typ pos psp), both
 *             envelopes, the sustain switch and tie, glide, the unison rows
 *             (int dtn spr mot), both filters and the drive, and the platform
 *             rows every engine shares (vol, pan, the LFO pair, velocity), so
 *             Morph, LFO, automation, MIDI and storage work unchanged.
 * Not offered: srt (no SRR, the brief), run (free-running only), chn,
 *             instrument_drive (the drive is the filter page's col_drive),
 *             a gate-length row (C-D8), a pan law of its own (C-D11).
 * Accessors:  InstrumentManager registry; Menu (pages); Preset/Morph/LFO/
 *             automation through the registry; storage (keys, [morph] for
 *             the MORPHABLE rows only).
 * Affiliates: ColloidVoice.h; MenuText.h (colIntervalNames, colFiltModeNames,
 *             colFiltTypeNames, colGlideModeNames, colWaveNames, the
 *             ONOFF_TAG_COL_* tags; alkTypeNames for typ);
 *             tools/colloid/make_init_col.py; SD_CARD/Instrument/Colloid/
 *             init.col.
 */

/* Load-menu label and type flags, imported by the registry. "Colloid" is 7
 * cells; the Morph row shows "ColloMrp" (menu.c, 5 characters + Mrp). */
const char colloid_instrument_display_label[] = "Colloid";
const uint8_t colloid_instrument_type_flags = COLLOID_INSTRUMENT_TYPE_FLAGS;

/* A normal sound cell: in both images, Morph-interpolated, a modulation
 * destination and step-automatable; written under [params] and [morph]. */
#define FLAGS_IMAGE \
    (INSTRUMENT_PARAM_FLAG_MORPHABLE | INSTRUMENT_PARAM_FLAG_MODULATABLE | \
     INSTRUMENT_PARAM_FLAG_AUTOMATABLE)

/* Runtime binding into the slot's ColloidVoice, with an optional
 * special-writer tag (S073 Step 2). */
#define BIND_SPECIAL(member_, type_, special_) \
    { INSTRUMENT_BIND_INSTANCE_OFFSET, (uint16_t)offsetof(ColloidVoice, member_), type_, special_ }
#define BIND(member_, type_) BIND_SPECIAL(member_, type_, IM_SPECIAL_NONE)

/* Modulation domains in descriptor units (AlkalineParameters.c). */
#define MOD_NONE  { 0u, 0u, INSTRUMENT_MOD_DOMAIN_NONE }
#define MOD_0_127 { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
#define MOD_PM63  { 0u, 127u, INSTRUMENT_MOD_DOMAIN_CONTINUOUS }
/* a wave row's domain: integers, the maximum from the slot's type
 * (instrumentManager_waveDomainMax(): Saw..Sin under wav, Rec under pwm,
 * the installed tables under wtb) */
#define MOD_WAVE  { 0u, 0u, (uint8_t)(INSTRUMENT_MOD_DOMAIN_INTEGER | INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX) }

/* Row shapes (AlkalineParameters.c). */
#define ROW(key_, cat_, long_, short_, dtype_, mod_, member_, type_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_SPECIAL(key_, cat_, long_, short_, dtype_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
#define ROW_MENU(key_, cat_, long_, short_, menu_, mod_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND(member_, type_) }
#define ROW_MENU_SPECIAL(key_, cat_, long_, short_, menu_, mod_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), FLAGS_IMAGE, mod_, BIND_SPECIAL(member_, type_, special_) }
/* A tagged DTYPE_ON_OFF selector with a special writer (typ: the Alkaline's
 * ROW_SELECT_ONOFF_SPECIAL): AUTOMATABLE | SELECTOR, never morphed, not an
 * LFO/velocity target. */
#define ROW_SELECT_ONOFF_SPECIAL(key_, cat_, long_, short_, tag_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND_SPECIAL(member_, type_, special_) }
/*
 * A DTYPE_ON_OFF selector with the generic store (S095 P2).
 *
 * What:       DTYPE_ON_OFF | (tag << 4) with AUTOMATABLE | SELECTOR and no
 *             special writer: the byte is stored as it is (TYPE_UINT8).
 *             Tag 0 is a plain on/off cell (sus, lgt); tags 4-7 name the
 *             Colloid's lists (MenuText.h ONOFF_TAG_COL_*).
 * Why:        the selectors are step-automatable (a `sus` off lock ends a
 *             note, C-D8; `lgt` on a step ties it) and never morphed (their
 *             in-between values are other choices). The menu clamps every
 *             edit, MIDI write and recorded automation to the tag's count
 *             (menu_clampCellValue()); the voice clamps on read too, so no
 *             store path can select past a list.
 */
#define ROW_SELECT_ONOFF(key_, cat_, long_, short_, tag_, member_, type_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_ON_OFF | ((tag_) << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND(member_, type_) }
/* Cells with no ColloidVoice member (target selectors), flags 0: [params]
 * only. */
#define ROW_NOBIND(key_, cat_, long_, short_, dtype_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, 0u, MOD_NONE, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }
/* Image cells applied through a supplemental binding (velocity amount). */
#define ROW_NOBIND_IMAGE(key_, cat_, long_, short_, dtype_, mod_, bind_kind_) \
    { key_, short_, long_, cat_, dtype_, FLAGS_IMAGE, mod_, { bind_kind_, 0u, 0u, IM_SPECIAL_NONE } }

/*
 * Colloid-local descriptor indices.
 *
 * What:       the order of colloid_param_descriptors[]; Scene/Kit storage,
 *             automation IDs, LFO/velocity tokens and NRPN numbers (slot x 64
 *             + index) use these numbers, and the file writer emits keys in
 *             this order.
 * Why:        the menu layout names cells by enum, not by number; a later
 *             row is appended before COL_PARAM_COUNT so every stored index
 *             stays (the standing append-only rule).
 */
typedef enum {
    COL_PARAM_OSC1_PITCH_COARSE = 0u,
    COL_PARAM_OSC1_PITCH_FINE,
    COL_PARAM_WAVE,
    COL_PARAM_OSC1_TYPE,
    COL_PARAM_OSC1_POSITION,
    COL_PARAM_POS_SPREAD,
    COL_PARAM_ATTACK,
    COL_PARAM_DECAY,
    COL_PARAM_SUSTAIN_LEVEL,
    COL_PARAM_RELEASE,
    COL_PARAM_SUSTAIN,
    COL_PARAM_TIE,
    COL_PARAM_GLIDE,
    COL_PARAM_GLIDE_MODE,
    COL_PARAM_INTERVAL,
    COL_PARAM_DETUNE,
    COL_PARAM_SPREAD,
    COL_PARAM_MOTION,
    COL_PARAM_FENV_ATTACK,
    COL_PARAM_FENV_DECAY,
    COL_PARAM_FENV_SUSTAIN_LEVEL,
    COL_PARAM_FENV_RELEASE,
    COL_PARAM_FENV_AMOUNT,
    COL_PARAM_CUT_A,
    COL_PARAM_RES_A,
    COL_PARAM_TYPE_A,
    COL_PARAM_FILT_MODE,
    COL_PARAM_CUT_B,
    COL_PARAM_RES_B,
    COL_PARAM_TYPE_B,
    COL_PARAM_DRIVE,
    COL_PARAM_INSTRUMENT_VOL,
    COL_PARAM_INSTRUMENT_PAN,
    COL_PARAM_LFO_RATE,
    COL_PARAM_LFO_AMOUNT,
    COL_PARAM_LFO_AMOUNT_2,
    COL_PARAM_LFO_WAVE,
    COL_PARAM_LFO_RETRIGGER_VOICE,
    COL_PARAM_LFO_POLARITY,
    COL_PARAM_LFO_SYNC,
    COL_PARAM_LFO_OFFSET,
    COL_PARAM_VELO_MOD_AMOUNT,
    COL_PARAM_VELO_MOD_DEST,
    COL_PARAM_LFO_TARGET_VOICE,
    COL_PARAM_LFO_TARGET_PARAM,
    COL_PARAM_LFO_TARGET_VOICE_2,
    COL_PARAM_LFO_TARGET_PARAM_2,
    COL_PARAM_COUNT
} colloid_param_index_t;

_Static_assert(COLLOID_PARAM_DESCRIPTOR_COUNT == COL_PARAM_COUNT,
               "COLLOID_PARAM_DESCRIPTOR_COUNT must match colloid_param_index_t");
_Static_assert(COL_PARAM_COUNT <= INSTRUMENT_PARAM_COUNT,
               "Colloid rows must fit the 64-cell Kit image");

/*
 * Colloid parameter descriptors.
 *
 * TYPE_FLT rows store byte / 127 into a 0..1 float
 * (instrumentManager_writeParameter()); TYPE_UINT8 rows store the byte (the
 * envelope times and glide index colEgK[] directly, so the async needs no
 * float-to-index conversion; an LFO or velocity overlay writes them in the
 * same 0..127 domain). ColloidVoice converts at control rate.
 */
const ParamDescriptor colloid_param_descriptors[] = {
    /* coa / fin: the existing pitch writers (midiFreq's high and low byte,
     * osc_recalcFreq()); the async recomputes the pitch from midiFreq and the
     * note every block. Their LFO/velocity target is osc.modNodeValue, the
     * base frequency's multiplier. coa 48 = C3 at the default note 63. */
    ROW_SPECIAL("osc1_pitch_coarse", "Oscilltr", "Coarse", "coa", DTYPE_0B127, MOD_0_127, osc.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_PITCH_COARSE | IM_SPECIAL_OSC1),
    ROW_SPECIAL("osc1_pitch_fine", "Oscilltr", "Fine", "fin", DTYPE_PM63, MOD_PM63, osc.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_PITCH_FINE | IM_SPECIAL_OSC1),
    /* wav: under typ wav Saw Rec Tri Sin, under pwm Rec, under wtb the table
     * (wNN); the shared wave helpers key on "osc1_wave" and know the Colloid
     * case (instrumentManager_waveDomainMax(), menu_formatWaveCell()) */
    ROW_MENU("osc1_wave", "Oscilltr", "Waveform", "wav", MENU_WAVEFORM, MOD_WAVE, osc.waveform, TYPE_UINT8),
    /* typ: wav / wtb / pwm, the Alkaline's names (C-D20); the writer is
     * Colloid_setOscType() (IM_SPECIAL_OSC_TYPE's Colloid branch) */
    ROW_SELECT_ONOFF_SPECIAL("osc1_type", "Oscilltr", "Type", "typ", ONOFF_TAG_ALK_TYPE, osc.oscType, TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1),
    /* pos: the frame centre under wtb (latched at the trigger, C-D4), the
     * width under pwm (every block, slewed); "---" under wav */
    ROW("osc1_position", "Oscilltr", "Position", "pos", DTYPE_0B127, MOD_0_127, osc.position, TYPE_UINT8),
    /* psp: the frame spread (wtb) or the width spread (pwm) around pos */
    ROW("col_pos_spread", "Oscilltr", "PosSprd", "psp", DTYPE_0B127, MOD_0_127, posSpread, TYPE_FLT),
    /* the amp ADSR (draft §4.1): times are colEgK rows, 1 ms .. 10 s */
    ROW("col_attack", "Amp EG", "Attack", "atk", DTYPE_0B127, MOD_0_127, attack, TYPE_UINT8),
    ROW("col_decay", "Amp EG", "Decay", "dec", DTYPE_0B127, MOD_0_127, decay, TYPE_UINT8),
    ROW("col_sustain_level", "Amp EG", "SustLvl", "slv", DTYPE_0B127, MOD_0_127, sustainLevel, TYPE_FLT),
    ROW("col_release", "Amp EG", "Release", "rel", DTYPE_0B127, MOD_0_127, release, TYPE_UINT8),
    /* sus (C-D8): hold at the sustain level while on and the transport runs
     * (C-D19); a step lock or Pattern automation turns it off to end a note */
    ROW_SELECT_ONOFF("col_sustain", "Amp EG", "Sustain", "sus", 0u, sustain, TYPE_UINT8),
    /* lgt: a trigger while the note is held, on a step with lgt on, is legato */
    ROW_SELECT_ONOFF("col_tie", "Amp EG", "Tie", "lgt", 0u, tie, TYPE_UINT8),
    /* por / pmd: glide time (0 off, colEgK rows up to 2 s) and mode */
    ROW("col_glide", "Glide", "Glide", "por", DTYPE_0B127, MOD_0_127, glide, TYPE_UINT8),
    ROW_SELECT_ONOFF("col_glide_mode", "Glide", "GlideMod", "pmd", ONOFF_TAG_COL_GLIDE_MODE, glideMode, TYPE_UINT8),
    /* the unison rows: interval set, detune spread, stereo spread, motion */
    ROW_SELECT_ONOFF("col_interval", "Unison", "Interval", "int", ONOFF_TAG_COL_INTERVAL, interval, TYPE_UINT8),
    ROW("col_detune", "Unison", "Detune", "dtn", DTYPE_0B127, MOD_0_127, detune, TYPE_FLT),
    ROW("col_spread", "Unison", "Spread", "spr", DTYPE_0B127, MOD_0_127, spread, TYPE_FLT),
    ROW("col_motion", "Unison", "Motion", "mot", DTYPE_0B127, MOD_0_127, motion, TYPE_FLT),
    /* the filter ADSR (draft §4.2) and its bipolar amount on both cutoffs */
    ROW("col_fenv_attack", "Filt EG", "Attack", "fat", DTYPE_0B127, MOD_0_127, fAttack, TYPE_UINT8),
    ROW("col_fenv_decay", "Filt EG", "Decay", "fde", DTYPE_0B127, MOD_0_127, fDecay, TYPE_UINT8),
    ROW("col_fenv_sustain_level", "Filt EG", "SustLvl", "fsl", DTYPE_0B127, MOD_0_127, fSustain, TYPE_FLT),
    ROW("col_fenv_release", "Filt EG", "Release", "fre", DTYPE_0B127, MOD_0_127, fRelease, TYPE_UINT8),
    ROW("col_fenv_amount", "Filt EG", "Amount", "fam", DTYPE_PM63, MOD_PM63, fAmount, TYPE_FLT),
    /* filter A, the mode, filter B, the drive into them (draft §3) */
    ROW("col_cut_a", "Filter", "CutoffA", "cta", DTYPE_0B127, MOD_0_127, cutA, TYPE_FLT),
    ROW("col_res_a", "Filter", "ResoA", "rsa", DTYPE_0B127, MOD_0_127, resA, TYPE_FLT),
    ROW_SELECT_ONOFF("col_type_a", "Filter", "TypeA", "tya", ONOFF_TAG_COL_FILT_TYPE, typeA, TYPE_UINT8),
    ROW_SELECT_ONOFF("col_filt_mode", "Filter", "FiltMode", "fmd", ONOFF_TAG_COL_FILT_MODE, filtMode, TYPE_UINT8),
    ROW("col_cut_b", "Filter", "CutoffB", "ctb", DTYPE_0B127, MOD_0_127, cutB, TYPE_FLT),
    ROW("col_res_b", "Filter", "ResoB", "rsb", DTYPE_0B127, MOD_0_127, resB, TYPE_FLT),
    ROW_SELECT_ONOFF("col_type_b", "Filter", "TypeB", "tyb", ONOFF_TAG_COL_FILT_TYPE, typeB, TYPE_UINT8),
    ROW("col_drive", "Filter", "Drive", "drv", DTYPE_0B127, MOD_0_127, drive, TYPE_FLT),
    /* platform rows: the mixer reads them (vol, and pan for its per-slot
     * pan law, C-D11) */
    ROW("instrument_vol", "Voice", "Volume", "vol", DTYPE_0B127, MOD_0_127, vol, TYPE_FLT),
    ROW("instrument_pan", "Voice", "Panning", "pan", DTYPE_PM63, MOD_PM63, pan, TYPE_UINT8),
    /* the LFO pair: one oscillator, two destinations (AlkalineParameters.c) */
    ROW_SPECIAL("lfo_rate", "LFO", "Frequncy", "frq", DTYPE_0B127, MOD_0_127, lfo.modNodeValue, TYPE_SPECIAL_F, IM_SPECIAL_LFO_RATE),
    ROW("lfo_amount", "LFO", "Amount", "am1", DTYPE_0B127, MOD_0_127, lfo.modTarget.amount, TYPE_FLT),
    ROW("lfo_amount_2", "LFO", "Amount 2", "am2", DTYPE_0B127, MOD_0_127, lfo.modTarget2.amount, TYPE_FLT),
    ROW_MENU("lfo_wave", "LFO", "Waveform", "wav", MENU_LFO_WAVES, MOD_NONE, lfo.waveform, TYPE_UINT8),
    ROW_MENU("lfo_retrigger_voice", "LFO", "Retriggr", "rtg", MENU_RETRIGGER, MOD_NONE, lfo.retrigger, TYPE_UINT8),
    ROW("lfo_polarity", "LFO", "Polarity", "pol", DTYPE_LFO_POLARITY, MOD_NONE, lfo.polarity, TYPE_UINT8),
    ROW_MENU_SPECIAL("lfo_sync", "LFO", "ClockSnc", "snc", MENU_SYNC_RATES, MOD_NONE, lfo.sync, TYPE_UINT8, IM_SPECIAL_LFO_SYNC),
    ROW_SPECIAL("lfo_offset", "LFO", "Offset", "ofs", DTYPE_0B127, MOD_NONE, lfo.phaseOffset, TYPE_UINT32, IM_SPECIAL_LFO_OFFSET),
    /* velocity: it reaches the envelopes only through its dst targets (the
     * vol row, fam), C-D18 */
    ROW_NOBIND_IMAGE("velo_mod_amount", "Velocity", "Amount", "amt", DTYPE_0B127, MOD_NONE, INSTRUMENT_BIND_VELOCITY_AMOUNT),
    ROW_NOBIND("velo_mod_dest", "Velocity", "DstParam", "dst", DTYPE_TARGET_SELECTION_VELO, INSTRUMENT_BIND_VELOCITY_TARGET),
    /* the LFO destinations */
    ROW_NOBIND("lfo_target_voice", "LFO", "DstVoice", "vo1", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE),
    ROW_NOBIND("lfo_target_param", "LFO", "DstParam", "ds1", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM),
    ROW_NOBIND("lfo_target_voice_2", "LFO", "DstVoice2", "vo2", DTYPE_VOICE_LFO, INSTRUMENT_BIND_LFO_TARGET_VOICE_2),
    ROW_NOBIND("lfo_target_param_2", "LFO", "DstParam2", "ds2", DTYPE_TARGET_SELECTION_LFO, INSTRUMENT_BIND_LFO_TARGET_PARAM_2),
};

/* Runtime registry count used by InstrumentManager bounds checks. */
const uint8_t colloid_param_descriptor_count =
    (uint8_t)(sizeof(colloid_param_descriptors) / sizeof(colloid_param_descriptors[0]));

_Static_assert(sizeof(colloid_param_descriptors) / sizeof(colloid_param_descriptors[0]) ==
                   COL_PARAM_COUNT,
               "colloid_param_descriptors must have one row per index");

/* One empty cell of a 16-cell page. */
#define COL_E INSTRUMENT_MENU_EMPTY

/*
 * Colloid voice menu layout (S095 P2, draft §8 with C-D20).
 *
 * What:       eight sub-pages at the index of the front-panel SELECT button:
 *             0 OSC (coa fin wav typ | pos psp), 1 AEG (atk dec slv rel |
 *             sus lgt), 2 mod (dst amt | por pmd), 3 unison on the FM button
 *             (int dtn spr mot), 4 filter envelope on the click button
 *             (fat fde fsl fre | fam), 5 FILTER (cta rsa tya fmd | ctb rsb
 *             tyb drv), 6 LFO (the Drum layout), 7 MIX (vol pan). Each page
 *             is 16 cells shown as four-cell screens.
 * Why:        the same SELECT positions as the other types (oscillator,
 *             amp envelope, modulation, filter, LFO, mix keep their buttons);
 *             the FM and click buttons carry the Colloid's own groups.
 */
const instrument_menu_page_t colloid_menu_pages[] = {
    /* 0 OSC */
    {{ COL_PARAM_OSC1_PITCH_COARSE, COL_PARAM_OSC1_PITCH_FINE, COL_PARAM_WAVE, COL_PARAM_OSC1_TYPE, COL_PARAM_OSC1_POSITION, COL_PARAM_POS_SPREAD, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 1 AEG */
    {{ COL_PARAM_ATTACK, COL_PARAM_DECAY, COL_PARAM_SUSTAIN_LEVEL, COL_PARAM_RELEASE, COL_PARAM_SUSTAIN, COL_PARAM_TIE, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 2 mod: velocity, then glide */
    {{ COL_PARAM_VELO_MOD_DEST, COL_PARAM_VELO_MOD_AMOUNT, COL_E, COL_E, COL_PARAM_GLIDE, COL_PARAM_GLIDE_MODE, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 3 unison (the FM button) */
    {{ COL_PARAM_INTERVAL, COL_PARAM_DETUNE, COL_PARAM_SPREAD, COL_PARAM_MOTION, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 4 filter envelope (the click button) */
    {{ COL_PARAM_FENV_ATTACK, COL_PARAM_FENV_DECAY, COL_PARAM_FENV_SUSTAIN_LEVEL, COL_PARAM_FENV_RELEASE, COL_PARAM_FENV_AMOUNT, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 5 FILTER: A and the mode, then B and the drive */
    {{ COL_PARAM_CUT_A, COL_PARAM_RES_A, COL_PARAM_TYPE_A, COL_PARAM_FILT_MODE, COL_PARAM_CUT_B, COL_PARAM_RES_B, COL_PARAM_TYPE_B, COL_PARAM_DRIVE, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
    /* 6 LFO: the Drum layout */
    {{ COL_PARAM_LFO_RATE, COL_PARAM_LFO_SYNC, COL_PARAM_LFO_WAVE, COL_PARAM_LFO_OFFSET, COL_PARAM_LFO_RETRIGGER_VOICE, COL_PARAM_LFO_POLARITY, COL_PARAM_LFO_AMOUNT, COL_PARAM_LFO_AMOUNT_2, COL_PARAM_LFO_TARGET_VOICE, COL_PARAM_LFO_TARGET_PARAM, COL_PARAM_LFO_TARGET_VOICE_2, COL_PARAM_LFO_TARGET_PARAM_2, COL_E, COL_E, COL_E, COL_E }},
    /* 7 MIX */
    {{ COL_PARAM_INSTRUMENT_VOL, COL_PARAM_INSTRUMENT_PAN, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E, COL_E }},
};

const uint8_t colloid_menu_page_count =
    (uint8_t)(sizeof(colloid_menu_pages) / sizeof(colloid_menu_pages[0]));

_Static_assert(COLLOID_MENU_PAGE_COUNT ==
                   (sizeof(colloid_menu_pages) / sizeof(colloid_menu_pages[0])),
               "COLLOID_MENU_PAGE_COUNT must match colloid_menu_pages");

uint8_t colloidParameters_cellInactive(const ColloidVoice *voice,
                                       const ParamDescriptor *descriptor)
{
    /* the row's index, or no answer for a pointer outside this table */
    const ParamDescriptor *const base = colloid_param_descriptors;

    if (!voice || !descriptor || descriptor < base ||
        descriptor >= base + COL_PARAM_COUNT)
        return 0u;
    switch ((colloid_param_index_t)(descriptor - base)) {
    case COL_PARAM_OSC1_POSITION:
    case COL_PARAM_POS_SPREAD:
        /* free-running wav oscillators: no start phase, frame or width */
        return (uint8_t)(voice->osc.oscType == OSC_TYPE_WAV);
    case COL_PARAM_SPREAD:
    case COL_PARAM_MOTION:
        /* the series chain is mono: positions have no effect (C-D16) */
        return (uint8_t)(voice->filtMode != COL_FMD_PAR);
    default:
        return 0u;
    }
}

#undef FLAGS_IMAGE
#undef BIND_SPECIAL
#undef BIND
#undef ROW
#undef ROW_SPECIAL
#undef ROW_MENU
#undef ROW_MENU_SPECIAL
#undef ROW_SELECT_ONOFF_SPECIAL
#undef ROW_SELECT_ONOFF
#undef ROW_NOBIND
#undef ROW_NOBIND_IMAGE
#undef COL_E
#undef MOD_WAVE
#undef MOD_PM63
#undef MOD_0_127
#undef MOD_NONE
```

#### S1.10 `tools/colloid/make_init_col.py`: add (new file)

```python
#!/usr/bin/env python3
"""Write and check the first Colloid instrument, init.col (S095 P2).

What:       reads the descriptor rows of Core/DSP/Instruments/Colloid/
            ColloidParameters.c (one row per line, as check_special_tags.py)
            and writes SD_CARD/Instrument/Colloid/init.col in the firmware
            writer's exact form (storageTypes.c storage_formatInstrumentLineView):
              format=helicase.instrument / version=1 / type=col / blank /
              [params] every key in table order / blank /
              [morph] the MORPHABLE rows only (macros ROW, ROW_SPECIAL,
              ROW_MENU, ROW_MENU_SPECIAL, ROW_NOBIND_IMAGE carry FLAGS_IMAGE;
              ROW_SELECT_ONOFF, ROW_SELECT_ONOFF_SPECIAL and ROW_NOBIND do
              not) / blank; LF endings.
            Values (INIT): Colloid_initVoice()'s bytes: typ wav, Saw, coa 48
            (C3), fin 63, the amp envelope 40/70/90/70 with sus on, the filter
            envelope 0/70/64/70 with fam 63 (none), both filters LP at 90 / 20
            in parallel, drive 0, int uni, dtn 40, spr 80, mot 30, por 0 (pmd
            all), vol 100, pan 63; LFO and velocity off: the LFO voices
            `self` (the writer's own-slot token), the target selectors 255
            (INSTRUMENT_TARGET_TOKEN_OFF).
Why:        Instrument Load only lists files, and a slot's type only changes
            by loading one (S093 P4 F1), so the card image carries a first
            Colloid. Every key is written: a missing key loads 0
            (instrumentManager_resetSlot()), which for coa and vol would be
            8 Hz and silence.
Inputs:     --check: the committed file must equal what this script writes,
            and every table key must have an INIT value (and no extra).
Outputs:    the file (default mode); with --check, a message and exit 1 on
            a difference.
Accessors:  python3 tools/colloid/make_init_col.py [--check];
            make -C tools/dsp_test special_tags (runs --check).
Affiliates: ColloidVoice.c Colloid_initVoice() (the same bytes);
            storage_instrumentParseLine() (the reader);
            tools/alkaline/make_init_alk.py (the same writer).
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
TABLE = os.path.join(ROOT, "Core", "DSP", "Instruments", "Colloid", "ColloidParameters.c")
OUT = os.path.join(ROOT, "SD_CARD", "Instrument", "Colloid", "init.col")
# one descriptor row: macro name, then the quoted file key
ROW = re.compile(r'^\s*(ROW[A-Z_]*)\("([^"]+)"')
# rows whose macro sets INSTRUMENT_PARAM_FLAG_MORPHABLE (FLAGS_IMAGE)
MORPHABLE = {"ROW", "ROW_SPECIAL", "ROW_MENU", "ROW_MENU_SPECIAL", "ROW_NOBIND_IMAGE"}

# init bytes per key; every key of the table must be here (and only those)
INIT = {
    "osc1_pitch_coarse": "48", "osc1_pitch_fine": "63", "osc1_wave": "0",
    "osc1_type": "0", "osc1_position": "0", "col_pos_spread": "0",
    "col_attack": "40", "col_decay": "70", "col_sustain_level": "90",
    "col_release": "70", "col_sustain": "1", "col_tie": "0",
    "col_glide": "0", "col_glide_mode": "0",
    "col_interval": "0", "col_detune": "40", "col_spread": "80", "col_motion": "30",
    "col_fenv_attack": "0", "col_fenv_decay": "70", "col_fenv_sustain_level": "64",
    "col_fenv_release": "70", "col_fenv_amount": "63",
    "col_cut_a": "90", "col_res_a": "20", "col_type_a": "0", "col_filt_mode": "0",
    "col_cut_b": "90", "col_res_b": "20", "col_type_b": "0", "col_drive": "0",
    "instrument_vol": "100", "instrument_pan": "63",
    "lfo_rate": "0", "lfo_amount": "0", "lfo_amount_2": "0", "lfo_wave": "0",
    "lfo_retrigger_voice": "0", "lfo_polarity": "0", "lfo_sync": "0", "lfo_offset": "0",
    "velo_mod_amount": "0", "velo_mod_dest": "255",
    "lfo_target_voice": "self", "lfo_target_param": "255",
    "lfo_target_voice_2": "self", "lfo_target_param_2": "255",
}


def rows():
    """(macro, key) for every descriptor row, in table order."""
    with open(TABLE) as fh:
        return [(m.group(1), m.group(2)) for m in map(ROW.match, fh) if m]


def text(r):
    """The file text, line for line as the firmware writer emits it."""
    lines = ["format=helicase.instrument", "version=1", "type=col", "", "[params]"]
    lines += [f"{k}={INIT[k]}" for _, k in r]
    lines += ["", "[morph]"]
    lines += [f"{k}={INIT[k]}" for m, k in r if m in MORPHABLE]
    lines += [""]
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    r = rows()
    keys = [k for _, k in r]
    missing = [k for k in keys if k not in INIT]
    extra = [k for k in INIT if k not in keys]
    if missing or extra:
        sys.exit(f"make_init_col: {len(r)} rows; no INIT value for {missing}; "
                 f"INIT keys not in the table {extra}")
    want = text(r)
    if a.check:
        have = open(OUT, newline="").read() if os.path.exists(OUT) else None
        if have != want:
            sys.exit(f"make_init_col: {OUT} differs from the table (rerun without --check)")
        print(f"make_init_col: init.col matches the table ({len(r)} rows)")
        return
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as fh:
        fh.write(want)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
```

#### S1.11 `SD_CARD/Instrument/Colloid/init.col`: add (generated)

Run `python3 tools/colloid/make_init_col.py`. It writes exactly this file
(`--check` compares; LF endings). Copy the folder to the card for the device
checks.

```ini
format=helicase.instrument
version=1
type=col

[params]
osc1_pitch_coarse=48
osc1_pitch_fine=63
osc1_wave=0
osc1_type=0
osc1_position=0
col_pos_spread=0
col_attack=40
col_decay=70
col_sustain_level=90
col_release=70
col_sustain=1
col_tie=0
col_glide=0
col_glide_mode=0
col_interval=0
col_detune=40
col_spread=80
col_motion=30
col_fenv_attack=0
col_fenv_decay=70
col_fenv_sustain_level=64
col_fenv_release=70
col_fenv_amount=63
col_cut_a=90
col_res_a=20
col_type_a=0
col_filt_mode=0
col_cut_b=90
col_res_b=20
col_type_b=0
col_drive=0
instrument_vol=100
instrument_pan=63
lfo_rate=0
lfo_amount=0
lfo_amount_2=0
lfo_wave=0
lfo_retrigger_voice=0
lfo_polarity=0
lfo_sync=0
lfo_offset=0
velo_mod_amount=0
velo_mod_dest=255
lfo_target_voice=self
lfo_target_param=255
lfo_target_voice_2=self
lfo_target_param_2=255

[morph]
osc1_pitch_coarse=48
osc1_pitch_fine=63
osc1_wave=0
osc1_position=0
col_pos_spread=0
col_attack=40
col_decay=70
col_sustain_level=90
col_release=70
col_glide=0
col_detune=40
col_spread=80
col_motion=30
col_fenv_attack=0
col_fenv_decay=70
col_fenv_sustain_level=64
col_fenv_release=70
col_fenv_amount=63
col_cut_a=90
col_res_a=20
col_cut_b=90
col_res_b=20
col_drive=0
instrument_vol=100
instrument_pan=63
lfo_rate=0
lfo_amount=0
lfo_amount_2=0
lfo_wave=0
lfo_retrigger_voice=0
lfo_polarity=0
lfo_sync=0
lfo_offset=0
velo_mod_amount=0
```

#### S1.12 `tools/dsp_test/modules/colloid.c`: add (new file)

The host bench (laws CO-OSC … CO-WTB). Law MX-ST is added in step 2 (§3.3).

```c
/*
 * modules/colloid.c - host laws for the Colloid voice (S095 P2).
 *
 * What:       builds the real ColloidTables.c, ColloidOsc.c and
 *             ColloidVoice.c (extracted, firmware includes and the ITCM
 *             section removed) over the real stock tables, the real PWM width
 *             table and the real sample catalogue code, and checks:
 *               CO-OSC  the pair loops against the read law restated here:
 *                       M = sum of a + ((b - a) f >> 15) (15-bit fraction,
 *                       wrapping next point), S = sum of (s q) >> 16, store
 *                       then add, phases advanced by 32 inc;
 *               CO-POS  spr 0 and mot 0: every position 0, the side bus 0, and
 *                       with equal filters the left and right blocks equal;
 *               CO-W    w(f): a note below 80 Hz centres every oscillator at
 *                       spr 1; above 640 Hz each sits at its home position
 *                       (to the Q15 step);
 *               CO-MOT  mot 1: no oscillator's position moves more than 0.01
 *                       of the field in one block;
 *               CO-INT  every interval set's increments at dtn 0 are the
 *                       base increment x 2^(st / 12) (within 2e-6), the
 *                       outer pair at dtn 1 is +-50 cents (within 0.7 cent);
 *               CO-EG   the envelopes: sus on holds at the sustain level while
 *                       running; a transport stop releases on the next block;
 *                       sus off runs attack, decay, release to idle; a hit
 *                       while stopped runs attack, decay, release (C-D19);
 *                       lgt on while held is legato (no restart); a retrigger
 *                       starts from the current level;
 *               CO-AMP  the amp ramp: no output step between samples larger
 *                       than the block's level change / 32 (x full scale)
 *                       plus the oscillator's own step;
 *               CO-SVF  the filters: 20,000 blocks of random cutoff,
 *                       resonance, type, envelope and drive stay finite and
 *                       bounded; every block's ramp starts exactly on the
 *                       previous block's target; LP passes DC at unity
 *                       (within 1 %);
 *               CO-SER  fmd ser: the left and right blocks are equal;
 *               CO-PWM  the pwm loops against the pulse law restated here;
 *                       pos 0, psp 0: every width oscPwmDRec[0] (512); the
 *                       width moves at most 2 indices per block;
 *               CO-WTB  wtb: psp 0 puts every oscillator on pos's frame;
 *                       frames stay latched when pos moves during a note;
 *                       an empty catalogue plays silence.
 * Why:        S095 P2 draft §11 step 3; the schedule's gates for steps 2
 *             and 4.
 * Inputs:     generated fragments (Makefile target colloid).
 * Outputs:    failure counts per group; exit 1 on any failure.
 * Accessors:  make -C tools/dsp_test colloid.
 * Affiliates: ColloidVoice.c, ColloidOsc.c, ColloidTables.c.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "osc_host.h"
#include "Oscillator.host.h"
#include "oscPwmTables.host.h"
#include "oscPwmTables.host.c"

/* the real stock tables (wavetable.c, const removed for the host) */
#include "col_sine_table.c"
#include "col_sawTable.c"
#include "col_triTable.c"
#include "col_recTable.c"

/* host stand-ins for what the voice references */
typedef struct { uint8_t bytes[124]; } Lfo;
typedef struct { float value; } ModulationNode;
ModulationNode velocityModulators[6];
static unsigned host_lfoRetriggers;
void lfo_init(Lfo *lfo) { memset(lfo, 0, sizeof(*lfo)); }
void lfo_retrigger(uint8_t voiceNr) { (void)voiceNr; host_lfoRetriggers++; }
void modNode_updateValue(ModulationNode *vm, float val) { vm->value = val; }
void instrumentManager_applyVelocityModulationTarget(uint8_t source_slot, float v)
{ (void)source_slot; (void)v; }
static uint8_t host_running = 1u;
uint8_t seq_isRunning(void) { return host_running; }
static uint32_t host_rng = 0x1234567u;
int16_t GetRngValue(void) { return (int16_t)(golden_rand(&host_rng) & 0xFFFFu); }

#include "AlkalineControl.host.h"
#include "ColloidTables.host.h"
#include "ColloidOsc.host.h"
#include "ColloidVoice.host.h"
#include "ColloidTables.host.c"
#include "ColloidOsc.host.c"
#include "ColloidVoice.host.c"

#define BLK OUTPUT_DMA_SIZE
enum { G_OSC, G_POS, G_W, G_MOT, G_INT, G_EG, G_AMP, G_SVF, G_SER, G_PWM, G_WTB, G_N };
static const char *g_name[G_N] = { "CO-OSC", "CO-POS", "CO-W", "CO-MOT", "CO-INT", "CO-EG",
                                   "CO-AMP", "CO-SVF", "CO-SER", "CO-PWM", "CO-WTB" };
static unsigned g_fail[G_N];

static void fail(int g, const char *what, double a, double b)
{
	if (g_fail[g] < 5u)
		printf("  %s FAIL: %s (%g, %g)\n", g_name[g], what, a, b);
	g_fail[g]++;
}

static ColloidVoice V;
static int16_t L[BLK];

static void voice_init(void)
{
	memset(&V, 0, sizeof(V));
	Colloid_initVoice(&V);
}

static void block(void)
{
	Colloid_calcVoiceAsync(&V);
	Colloid_calcVoiceSyncBlock(&V, L, BLK);
}

/* the read law, restated */
static int32_t ref_read(const int16_t *row, uint32_t ph)
{
	const uint32_t ia = ph >> 22, na = (ia + 1u) & 1023u;
	const int32_t a = row[ia], b = row[na];
	return a + (int32_t)(((int64_t)(b - a) * (int64_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

static int32_t ref_pulse(const int16_t *row, uint32_t ph, uint32_t d)
{
	const uint32_t ia = ph >> 22, na = (ia + 1u) & 1023u;
	const uint32_t ib = (ia - d) & 1023u, nb = (ib + 1u) & 1023u;
	const int32_t d0 = row[ia] - row[ib], d1 = row[na] - row[nb];
	return d0 + (int32_t)(((int64_t)(d1 - d0) * (int64_t)((ph >> 7) & 0x7FFFu)) >> 15);
}

static int32_t ref_side(int32_t s, int16_t q) { return (int32_t)(((int64_t)s * q) >> 16); }

static void test_osc(int pwm)
{
	/* plain: a full-scale alternating row (steps of 65,535, as a user frame
	 * may have) proves the 15-bit fraction's headroom; pwm: the saw rows its
	 * |d1 - d0| <= 65,534 bound is stated for (C-D21) */
	static int16_t edge[1024];
	const int16_t *plain[4] = { sawTable[3], edge, recTable[0], sine_table };
	const int16_t *pulse[4] = { sawTable[3], sawTable[0], sawTable[10], sawTable[6] };
	const int16_t *const *rows = pwm ? pulse : plain;
	uint32_t ph[4] = { 0x00000000u, 0x7fff1234u, 0xfffff000u, 0x12345678u };
	const uint32_t inc[4] = { 0x01234567u, 0x00a00000u, 0x7ffffff0u, 0x00010001u };
	const uint16_t wid[4] = { 512u, 3u, 1000u, 64u };
	const uint32_t pq0 = (uint32_t)(uint16_t)(int16_t)-20000 | ((uint32_t)(uint16_t)(int16_t)31000 << 16);
	const uint32_t pq1 = (uint32_t)(uint16_t)(int16_t)32767 | ((uint32_t)(uint16_t)(int16_t)-32768 << 16);
	int32_t M[BLK], S[BLK];
	uint32_t p[4];
	int g = pwm ? G_PWM : G_OSC;
	unsigned n, o;

	for (n = 0u; n < 1024u; n++)
		edge[n] = (n & 1u) ? -32768 : 32767;
	memcpy(p, ph, sizeof(p));
	if (pwm) {
		col_pwmPairStore(&rows[0], &p[0], &inc[0], &wid[0], pq0, M, S);
		col_pwmPairAdd(&rows[2], &p[2], &inc[2], &wid[2], pq1, M, S);
	} else {
		col_oscPairStore(&rows[0], &p[0], &inc[0], pq0, M, S);
		col_oscPairAdd(&rows[2], &p[2], &inc[2], pq1, M, S);
	}
	for (n = 0u; n < BLK; n++) {
		int32_t m = 0, s = 0;
		for (o = 0u; o < 4u; o++) {
			const uint32_t at = ph[o] + n * inc[o];
			const int32_t v = pwm ? ref_pulse(rows[o], at, wid[o]) : ref_read(rows[o], at);
			const uint32_t pq = (o < 2u) ? pq0 : pq1;
			m += v;
			s += ref_side(v, (int16_t)((o & 1u) ? (pq >> 16) : (pq & 0xFFFFu)));
		}
		if (M[n] != m) fail(g, "mid bus", M[n], m);
		if (S[n] != s) fail(g, "side bus", S[n], s);
	}
	for (o = 0u; o < 4u; o++)
		if (p[o] != ph[o] + BLK * inc[o]) fail(g, "phase advance", p[o], ph[o] + BLK * inc[o]);
}

static void test_pos_and_w(void)
{
	unsigned b, i;
	/* CO-POS: spr 0, mot 0 */
	voice_init();
	V.spread = 0.0f; V.motion = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (b = 0u; b < 50u; b++) {
		block();
		for (i = 0u; i < COL_PAIR_MAX; i++)
			if (V.posPair[i] != 0u) fail(G_POS, "position not 0", i, V.posPair[i]);
		for (i = 0u; i < BLK; i++) {
			if (V.busS[i] != 0) fail(G_POS, "side bus not 0", i, V.busS[i]);
			if (L[i] != V.right[i]) fail(G_POS, "L != R", L[i], V.right[i]);
		}
	}
	/* CO-W: below 80 Hz (coa 24: C1, 32.7 Hz) every position is 0 at spr 1 */
	voice_init();
	V.spread = 1.0f; V.motion = 0.0f; V.detune = 0.0f;
	V.osc.midiFreq = (uint16_t)((24u << 8) | 63u);
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	for (i = 0u; i < COL_PAIR_MAX; i++)
		if (V.posPair[i] != 0u) fail(G_W, "low note not centred", i, V.posPair[i]);
	/* above 640 Hz (coa 84: C6, 1,046.5 Hz) each oscillator at its home */
	V.osc.midiFreq = (uint16_t)((84u << 8) | 63u);
	block();
	for (i = 0u; i < COL_OSC_COUNT; i++) {
		const int16_t q = (int16_t)((i & 1u) ? (V.posPair[i >> 1] >> 16) : (V.posPair[i >> 1] & 0xFFFFu));
		const int32_t want = (int32_t)(colHome[i] * 32767.0f);
		if (abs(q - want) > 1) fail(G_W, "high note not at home", q, want);
	}
}

static void test_motion(void)
{
	unsigned b, i;
	int16_t last[COL_OSC_MAX];
	voice_init();
	V.spread = 0.5f; V.motion = 1.0f;
	V.osc.midiFreq = (uint16_t)((84u << 8) | 63u);   /* w = 1: the full movement */
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	for (i = 0u; i < COL_OSC_COUNT; i++)
		last[i] = (int16_t)((i & 1u) ? (V.posPair[i >> 1] >> 16) : (V.posPair[i >> 1] & 0xFFFFu));
	for (b = 0u; b < 3000u; b++) {
		block();
		for (i = 0u; i < COL_OSC_COUNT; i++) {
			const int16_t q = (int16_t)((i & 1u) ? (V.posPair[i >> 1] >> 16) : (V.posPair[i >> 1] & 0xFFFFu));
			/* 0.01 of the field (-1..+1, so 2 x 32,767) per block */
			if (abs(q - last[i]) > (int)(0.01 * 2.0 * 32767.0)) fail(G_MOT, "position step", q, last[i]);
			last[i] = q;
		}
	}
}

static void test_intervals(void)
{
	unsigned s, i;
	for (s = 0u; s < COL_INTERVAL_COUNT; s++) {
		voice_init();
		V.detune = 0.0f; V.interval = (uint8_t)s;
		Colloid_triggerVoice(&V, 0u, 100u, 63u);
		block();
		{
			const double base = exp2((double)V.pitchLog2) * COL_INC_PER_HZ;
			for (i = 0u; i < COL_OSC_COUNT; i++) {
				const int st = colIntervals[s][colMember[colIntervalCount[s] - 1u][i]];
				const double want = base * exp2(st / 12.0);
				if (fabs(V.inc[i] / want - 1.0) > 2e-6) fail(G_INT, "interval ratio", V.inc[i], want);
			}
		}
	}
	voice_init();
	V.detune = 1.0f; V.interval = 0u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	{
		const double cents = 1200.0 * log2((double)V.inc[COL_OSC_COUNT - 1u] / (double)V.inc[COL_OSC_COUNT - 2u]) / 2.0;
		const double want = 50.0 * fabs(colDetC[COL_OSC_COUNT - 1u]);
		if (fabs(cents - want) > 0.7) fail(G_INT, "outer pair cents", cents, want);
	}
}

static unsigned run_until_stage(ColEg *e, uint8_t stage, unsigned max)
{
	unsigned b;
	for (b = 0u; b < max; b++) {
		block();
		if (e->stage == stage) return b;
	}
	return max;
}

static void test_eg(void)
{
	unsigned b;
	float lvl;
	/* sus on, running: holds */
	voice_init(); host_running = 1u; V.sustain = 1u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	if (run_until_stage(&V.ampEg, COL_EG_SUSTAIN, 5000u) >= 5000u) fail(G_EG, "no sustain", 0, 0);
	for (b = 0u; b < 1000u; b++) {
		block();
		if (V.ampEg.stage != COL_EG_SUSTAIN) { fail(G_EG, "sustain did not hold", b, V.ampEg.stage); break; }
	}
	if (fabsf(V.ampEg.level - V.sustainLevel) > 0.01f) fail(G_EG, "sustain level", V.ampEg.level, V.sustainLevel);
	/* a transport stop releases on the next block */
	host_running = 0u;
	block();
	if (V.ampEg.stage != COL_EG_RELEASE) fail(G_EG, "stop did not release", V.ampEg.stage, COL_EG_RELEASE);
	if (V.fltEg.stage != COL_EG_RELEASE) fail(G_EG, "stop did not release fenv", V.fltEg.stage, COL_EG_RELEASE);
	if (run_until_stage(&V.ampEg, COL_EG_IDLE, 20000u) >= 20000u) fail(G_EG, "release never ended", 0, 0);
	if (!Colloid_isQuiet(&V)) fail(G_EG, "idle voice not quiet", 0, 0);
	/* a hit while stopped: attack, decay, release, idle */
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	if (Colloid_isQuiet(&V)) fail(G_EG, "sounding voice quiet", 0, 0);
	if (run_until_stage(&V.ampEg, COL_EG_IDLE, 40000u) >= 40000u) fail(G_EG, "stopped hit held", V.ampEg.stage, 0);
	/* sus off while running: A-D-R */
	host_running = 1u; V.sustain = 0u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	if (run_until_stage(&V.ampEg, COL_EG_IDLE, 40000u) >= 40000u) fail(G_EG, "sus off held", V.ampEg.stage, 0);
	/* legato: lgt on while held does not restart */
	V.sustain = 1u; V.tie = 1u;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	(void)run_until_stage(&V.ampEg, COL_EG_SUSTAIN, 5000u);
	Colloid_triggerVoice(&V, 0u, 100u, 70u);
	if (V.ampEg.stage != COL_EG_SUSTAIN) fail(G_EG, "legato restarted", V.ampEg.stage, COL_EG_SUSTAIN);
	if (!V.gliding) fail(G_EG, "legato note does not glide", 0, 0);
	/* retrigger without lgt: attack from the current level */
	V.tie = 0u;
	lvl = V.ampEg.level;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	if (V.ampEg.stage != COL_EG_ATTACK || V.ampEg.level != lvl) fail(G_EG, "retrigger level", V.ampEg.level, lvl);
}

static void test_amp(void)
{
	unsigned b, i;
	int16_t prev = 0;
	voice_init(); host_running = 1u;
	V.sustain = 0u; V.attack = 0u; V.release = 0u; V.decay = 0u;
	V.cutA = V.cutB = 1.0f; V.resA = V.resB = 0.0f; V.detune = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (b = 0u; b < 400u; b++) {
		const float before = V.ampEg.level;
		block();
		{
			const float bound = fabsf(V.ampEg.level - before) * 32767.0f / (float)BLK;
			for (i = 0u; i < BLK; i++) {
				/* the step also carries the signal's own change: a full-scale
				 * saw stack moves at most a few thousand LSB per sample */
				if (abs(L[i] - prev) > (int)(bound + 6000.0f)) fail(G_AMP, "output step", L[i] - prev, bound);
				prev = L[i];
			}
		}
	}
}

static void test_svf(void)
{
	unsigned b, i;
	uint32_t s = 99u;
	voice_init(); host_running = 1u;
	Colloid_triggerVoice(&V, 0u, 127u, 63u);
	for (b = 0u; b < 20000u; b++) {
		const float t1A = V.svfA.t1, t2A = V.svfA.t2, t3A = V.svfA.t3;
		if ((b % 7u) == 0u) {
			V.cutA = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.cutB = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.resA = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.resB = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.typeA = (uint8_t)(golden_rand(&s) % 5u);
			V.typeB = (uint8_t)(golden_rand(&s) % 5u);
			V.fAmount = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.drive = (float)(golden_rand(&s) & 127u) / 127.0f;
			V.filtMode = (uint8_t)(golden_rand(&s) & 1u);
		}
		if ((b % 300u) == 0u) Colloid_triggerVoice(&V, 0u, 127u, (uint8_t)(40u + (golden_rand(&s) % 50u)));
		block();
		if (V.svfA.a1 != t1A || V.svfA.a2 != t2A || V.svfA.a3 != t3A) fail(G_SVF, "ramp start != last target", V.svfA.a1, t1A);
		if (!isfinite(V.svfA.ic1) || !isfinite(V.svfA.ic2) || !isfinite(V.svfB.ic1) || !isfinite(V.svfB.ic2) ||
		    fabsf(V.svfA.ic1) > 100.0f || fabsf(V.svfA.ic2) > 100.0f ||
		    fabsf(V.svfB.ic1) > 100.0f || fabsf(V.svfB.ic2) > 100.0f) {
			fail(G_SVF, "state unbounded", V.svfA.ic1, V.svfB.ic2);
			break;
		}
	}
	/* LP DC gain: a constant input through the parallel A filter */
	{
		ColSvf f;
		float y = 0.0f, ic1, ic2;
		col_svfInit(&f, 0.5f, 0.3f, COL_FT_LP);
		ic1 = f.ic1; ic2 = f.ic2;
		for (i = 0u; i < 20000u; i++) {
			const float x = 0.5f;
			const float v3 = x - ic2, v1 = f.a1 * ic1 + f.a2 * v3, v2 = ic2 + f.a2 * ic1 + f.a3 * v3;
			ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
			y = f.mH * x + f.mB * v1 + f.mL * v2;
		}
		if (fabsf(y - 0.5f) > 0.005f) fail(G_SVF, "LP DC gain", y, 0.5);
	}
}

static void test_ser(void)
{
	unsigned b, i;
	voice_init(); host_running = 1u;
	V.filtMode = COL_FMD_SER; V.spread = 1.0f; V.motion = 1.0f;
	V.osc.midiFreq = (uint16_t)((84u << 8) | 63u);
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (b = 0u; b < 100u; b++) {
		block();
		for (i = 0u; i < BLK; i++)
			if (L[i] != V.right[i]) { fail(G_SER, "series L != R", L[i], V.right[i]); break; }
	}
}

static void test_pwm_voice(void)
{
	unsigned b, i;
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_PWM);
	V.osc.position = 0u; V.posSpread = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	for (i = 0u; i < COL_PWM_COUNT; i++)
		if (V.pwD[i] != oscPwmDRec[0]) fail(G_PWM, "pos 0 width", V.pwD[i], oscPwmDRec[0]);
	V.osc.position = 127u;
	for (b = 0u; b < 80u; b++) {
		uint8_t before[COL_OSC_MAX];
		memcpy(before, V.pwPos, sizeof(before));
		block();
		for (i = 0u; i < COL_PWM_COUNT; i++)
			if (abs((int)V.pwPos[i] - (int)before[i]) > OSC_PWM_SLEW_STEPS) fail(G_PWM, "width slew", V.pwPos[i], before[i]);
	}
	for (i = 0u; i < COL_PWM_COUNT; i++)
		if (V.pwPos[i] != 127u) fail(G_PWM, "width did not arrive", V.pwPos[i], 127);
}

static void install_table(unsigned frames)
{
	static int16_t fr[16][1024];
	static int16_t fbuf[WAVETABLE_FRAME_MAX];
	const sample_install_fmt_t fw = { 1u, 1u, 44100u, 2u, 16u, 16u };
	unsigned f, i;
	for (f = 0u; f < frames; f++)
		for (i = 0u; i < 1024u; i++)
			fr[f][i] = (int16_t)(20000.0 * sin(6.283185307 * i * (f + 1) / 1024.0));
	catalogue_host_reset();
	sampleInstall_begin();
	if (frames) {
		(void)sampleInstall_beginWavetable("wcol    ", 1024u, (uint8_t)frames, &fw);
		for (f = 0u; f < frames; f++)
			(void)sampleInstall_writeFrame((const uint8_t *)fr[f], 1024u, fbuf);
		(void)sampleInstall_finishWavetable();
	}
	(void)sampleInstall_commit();
	sampleMemory_refresh();
}

static void test_wtb(void)
{
	unsigned b, i;
	/* psp 0: every oscillator on pos's frame; latched while pos moves */
	install_table(8u);
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_WTB);
	V.osc.waveform = 0u; V.osc.position = 127u; V.posSpread = 0.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	block();
	if (V.osc.sampleCacheSize != 8u) fail(G_WTB, "frames bound", V.osc.sampleCacheSize, 8);
	for (i = 0u; i < COL_OSC_MAX; i++)
		if (V.frame[i] != 7u) fail(G_WTB, "psp 0 frame", V.frame[i], 7);
	V.osc.position = 0u;
	block();
	for (i = 0u; i < COL_OSC_COUNT; i++)
		if (V.row[i] != V.osc.sampleCacheData + 7u * WAVETABLE_ROW_POINTS) fail(G_WTB, "frame not latched", i, 0);
	/* psp 1: frames spread, all inside the table */
	V.posSpread = 1.0f;
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (i = 0u; i < COL_OSC_MAX; i++)
		if (V.frame[i] >= 8u) fail(G_WTB, "frame outside", V.frame[i], 8);
	/* an empty catalogue: silence */
	install_table(0u);
	voice_init(); host_running = 1u;
	Colloid_setOscType(&V, COL_TYP_WTB);
	Colloid_triggerVoice(&V, 0u, 100u, 63u);
	for (b = 0u; b < 50u; b++) {
		block();
		for (i = 0u; i < BLK; i++)
			if (L[i] != 0 || V.right[i] != 0) { fail(G_WTB, "empty catalogue not silent", L[i], V.right[i]); b = 50u; break; }
	}
}

int main(void)
{
	unsigned g, total = 0u;
	install_table(0u);
	test_osc(0);
	test_osc(1);
	test_pos_and_w();
	test_motion();
	test_intervals();
	test_eg();
	test_amp();
	test_svf();
	test_ser();
	test_pwm_voice();
	test_wtb();
	for (g = 0u; g < G_N; g++) {
		printf("colloid %-7s failures: %u\n", g_name[g], g_fail[g]);
		total += g_fail[g];
	}
	return total ? 1 : 0;
}
```

#### S1.13 `tools/dsp_test/colloid_cpu.py`: add (new file)

```python
#!/usr/bin/env python3
"""
colloid_cpu.py: the Colloid's instructions per sample (S095 P2).

What:       for each mode, from the linked ELF (the space_cpu.py method,
            S095 P1):
              per-sample loops: the first pair loop (store) + (pairs - 1)
              add loops + the filter/output loop of the mode, each at its
              longest path;
              per-block paths / 32: Colloid_calcVoiceAsync() (its straight
              path) + its oscillator-control loop x the oscillator count (the
              longest path: the pwm branch is an upper bound for the plain
              modes) + every call it makes to the voice's own helpers that LTO
              kept out of line (col_svfControl, col_egStep, col_wtbBind: the
              bind counted every block, its dearest case) + the sync block's
              path + each pair loop's entry/exit path + the filter loop's
              entry/exit path.
            Modes: plain (COL_OSC_COUNT oscillators, from --count) and pwm
            (COL_PWM_COUNT 6), each parallel (the dearest) and series.
Why:        the device run is the gate (S094 P2: measured cycles per block,
            cold, dearest, stereo send, against the Basic 19,620); these
            counts are the host regression baseline, recorded at step 5 in
            tools/dsp_test/baselines/s095p2/ and compared by --baseline.
Inputs:     --elf PATH; --count N (COL_OSC_COUNT, 12 or 10); --baseline FILE
            (optional: fail when a mode grows by more than 0.5 per sample).
Outputs:    one line per mode; exit 1 on a missing symbol or a growth.
Accessors:  tools/dsp_test/Makefile colloid_cpu_check (after `make all`).
Affiliates: elf_looppath.py, elf_blockpath.py, space_cpu.py; ColloidVoice.c,
            ColloidOsc.c.
"""
import argparse
import re
import sys

import elf_blockpath as B
import elf_looppath as E

FRAMES = 32
PWM_COUNT = 6
HELPERS = ('col_svfControl', 'col_egStep', 'col_wtbBind')


def loops(elf, sym):
    """[(longest, shortest)] of the function's distinct loops, longest first
    (space_cpu.py's rule: a cycle found again through an inner head counts
    once)."""
    r = E.rows(elf, sym)
    idx = {a: i for i, (a, _, _) in enumerate(r)}
    heads = sorted({E.target(ops) for (addr, op, ops) in r
                    if E.BRANCH.match(op) and E.target(ops) is not None
                    and E.target(ops) <= addr and E.target(ops) in idx})
    out = {}
    for h in heads:
        p = E.loop_paths(r, idx, h)
        if p:
            p.sort(key=lambda x: -x[0])
            key = frozenset(p[0][2])
            if key not in out:
                out[key] = (p[0][0], p[-1][0])
    return sorted(out.values(), key=lambda x: -x[0])


def calls_to(elf, sym, callee):
    """How many `bl` instructions of sym call callee (any LTO suffix)."""
    return sum(1 for (_, op, ops) in E.rows(elf, sym)
               if op == 'bl' and re.search(r'<' + re.escape(callee) + r'[.>]', ops))


def present(elf, sym):
    try:
        E.rows(elf, sym)
        return True
    except SystemExit:
        return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--count', type=int, default=12)
    ap.add_argument('--baseline')
    a = ap.parse_args()
    for s in ('Colloid_calcVoiceAsync', 'Colloid_calcVoiceSyncBlock',
              'col_oscPairStore', 'col_oscPairAdd', 'col_pwmPairStore',
              'col_pwmPairAdd', 'col_renderPar', 'col_renderSer'):
        if not present(a.elf, s):
            sys.exit(f'colloid_cpu.py: {s} not found (inlined or renamed)')
    ctl = loops(a.elf, 'Colloid_calcVoiceAsync')
    if not ctl:
        sys.exit('colloid_cpu.py: no oscillator-control loop in the async')
    ctl_len = ctl[0][0]
    helpers = 0
    for h in HELPERS:
        if present(a.elf, h):
            helpers += calls_to(a.elf, 'Colloid_calcVoiceAsync', h) * B.block_path(a.elf, h)[0]
    base_blk = (B.block_path(a.elf, 'Colloid_calcVoiceAsync')[0] + helpers +
                B.block_path(a.elf, 'Colloid_calcVoiceSyncBlock')[0])
    want = {}
    if a.baseline:
        for line in open(a.baseline):
            m = re.match(r'(\S+): .* = ([0-9.]+)', line)
            if m:
                want[m.group(1)] = float(m.group(2))
    fail = False
    for typ, n in (('plain', a.count), ('pwm', PWM_COUNT)):
        store, add = (('col_oscPairStore', 'col_oscPairAdd') if typ == 'plain'
                      else ('col_pwmPairStore', 'col_pwmPairAdd'))
        pairs = n // 2
        for mode, render in (('par', 'col_renderPar'), ('ser', 'col_renderSer')):
            per_sample = (loops(a.elf, store)[0][0] + (pairs - 1) * loops(a.elf, add)[0][0]
                          + loops(a.elf, render)[0][0])
            blk = (base_blk + n * ctl_len + B.block_path(a.elf, store)[0]
                   + (pairs - 1) * B.block_path(a.elf, add)[0]
                   + B.block_path(a.elf, render)[0])
            total = per_sample + blk / FRAMES
            name = f'{typ}{n}-{mode}'
            note = ''
            if name in want and total > want[name] + 0.5:
                note = f'  GREW (baseline {want[name]:.1f})'
                fail = True
            print(f'{name}: loops {per_sample} + per block {blk}/{FRAMES} = {total:.1f}{note}')
    sys.exit(1 if fail else 0)


if __name__ == '__main__':
    main()
```

### 2.2 Build

#### S1.14 `Makefile:39`: add after line 39

Anchor (line 39 today): `-ICore/DSP/Instruments/Alkaline \`

```make
          -ICore/DSP/Instruments/Colloid \
```

#### S1.15 `Makefile:118`: add after line 118

Anchor (line 118 today): `Core/DSP/Instruments/Alkaline/AlkalineParameters.c \`

```make
  Core/DSP/Instruments/Colloid/ColloidParameters.c \
```

#### S1.16 `Makefile:161`: add after line 161

Anchor (line 161 today): `Core/DSP/Instruments/Alkaline/AlkalineVoice.c \`

```make
  Core/DSP/Instruments/Colloid/ColloidOsc.c \
  Core/DSP/Instruments/Colloid/ColloidTables.c \
  Core/DSP/Instruments/Colloid/ColloidVoice.c \
```

#### S1.17 `Makefile:255`: add after line 255

Anchor (line 255 today): `$(CC) -c $(CFLAGS_DSP) $< -o $@`

*As implemented (§9.1): the three recipes carry `-fno-lto`, and the comment
explains why.*

```make
# S095 P2: the Colloid engine, its oscillator loops and its tables are DSP:
# -Ofast like the Alkaline (the measured counts and the constant-cost loops
# assume it). ColloidParameters.c stays in SRCS (-O2), as the other tables.
#
# -fno-lto on these three objects (S095 P2, measured): with them in the LTO
# unit, the larger program shifted LTO's partitioning and ten Drum-family PWM
# loops (osc_pwmWt/Ws Fm, FmRamp, Ring, Sync; osc_smpFm, smpFmRamp) were
# re-laid as predicated forms, 1 to 3 instructions longer per sample, though
# their source is unchanged. Outside the LTO unit the Colloid leaves every
# other engine's code generation as it was: every oscillator loop equal to
# the step 0 image, the mixer 0 grew, the Alkaline ladder loops equal. The
# Colloid loses nothing it needs from LTO: its entry points are noinline
# calls from InstrumentManager, and its helpers are its own. Cost: +544 B of
# flash and +24 B of ITCM against the LTO build.
$(BUILD)/Core/DSP/Instruments/Colloid/ColloidVoice.o: Core/DSP/Instruments/Colloid/ColloidVoice.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
$(BUILD)/Core/DSP/Instruments/Colloid/ColloidOsc.o: Core/DSP/Instruments/Colloid/ColloidOsc.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
$(BUILD)/Core/DSP/Instruments/Colloid/ColloidTables.o: Core/DSP/Instruments/Colloid/ColloidTables.c | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS_DSP) $< -o $@ -fno-lto
```


#### S1.18 `STM32F765VIHx_FLASH.ld:151`, `:157-158`, `:172`, `:182`: modify

- `:151` `ITCM input order (S089 P2; S089 P3; S094 P1).` becomes
  `ITCM input order (S089 P2; S089 P3; S094 P1; S095 P2).`
- `:157-158` `... then .itcm.s094p1 (S094 P1 PWM` / `loops), then any other
  .itcm.* subsection.` becomes `... then .itcm.s094p1 (S094 P1 PWM` /
  `loops), then .itcm.s095p2 (S095 P2, the Colloid's pair` / `loops), then
  any other .itcm.* subsection.`
- add after `:172` (`no existing function moves.`):

```
         *             S095 P2: .itcm.s095p2 (the Colloid's four oscillator
         *             pair loops, ColloidOsc.c) is named after .itcm.s094p1,
         *             so it is appended and no existing function moves.
```

- `:182` becomes:

```
        *(.itcm) *(.itcm.s085) *(.itcm.s087 .itcm.s087.*) *(.itcm.s089) *(.itcm.s089p3) *(.itcm.s094p1) *(.itcm.s095p2) *(.itcm.*)
```

### 2.3 InstrumentManager

`instrumentManager_slotRight()` is declared and defined here, in step 1
(unused until step 2: `--gc-sections` drops it, measured). Its caller is the
mixer in step 2.

#### S1.19 `Core/DSP/Instruments/InstrumentManager.h:89`: add after line 89

Anchor (line 89 today): `INSTRUMENT_TYPE_ALK,`

```c
    /*
     * S095 P2: the Colloid type (token "col"), before UNKNOWN.
     *
     * What:       value 5; UNKNOWN moves from 5 to 6.
     * Why:        appended (the standing rule, as the Alkaline in S093 P4):
     *             no stored value changes meaning; every type-sized array
     *             (menu_instrumentLoadIndex[], menu.c: +2 B .bss) and every
     *             `type >= INSTRUMENT_TYPE_UNKNOWN` guard follow; the
     *             .hcnames header written before the addition is a leading
     *             part of the new list and is accepted.
     */
    INSTRUMENT_TYPE_COL,
```

#### S1.20 `Core/DSP/Instruments/InstrumentManager.h:836`: add after line 836

Anchor (line 836 today): `uint8_t size);`

```c
/*
 * The slot's right output block, for a stereo engine (S095 P2, draft §5).
 *
 * What:       the Colloid's voice->right (32 int16, written by its sync
 *             block in the same call that fills the mixer's left buffer), or
 *             NULL for every other type and for an empty slot.
 * Why:        the mixer renders one mono stack block per slot (sampleData);
 *             the right block lives inside the voice (the slot reserve), so a
 *             stereo voice costs no new RAM. noinline keeps the call out of
 *             mixer_calcNextSampleBlock()'s register allocation.
 * Inputs:     slot 0..5. Outputs: a borrowed pointer valid until the next
 *             render of the slot, or NULL.
 * Accessors:  mixer_calcNextSampleBlock() (once per slot per block, after
 *             instrumentManager_calcSlotSyncBlock()).
 */
const int16_t *instrumentManager_slotRight(uint8_t slot) __attribute__((noinline));
/*
 * Whether an instrument cell has no effect in the slot's current mode
 * (S095 P2, C-D16, C-D20).
 *
 * What:       for a Colloid slot, colloidParameters_cellInactive() (pos and
 *             psp under typ wav; spr and mot in fmd ser); 0 for every other
 *             type.
 * Why:        the compact cell shows "---" (menu_formatCellValue3()).
 * Inputs:     slot 0..5, the cell's descriptor.
 * Accessors:  menu.c menu_formatCellValue3().
 */
uint8_t instrumentManager_cellInactive(uint8_t slot, const ParamDescriptor *descriptor);
```


#### S1.21 `Core/DSP/Instruments/InstrumentManager.c:6`: add after line 6

Anchor (line 6 today): `#include "AlkalineParameters.h"`

```c
#include "ColloidParameters.h"    /* S095 P2: registry row, the inactive-cell test */
```

#### S1.22 `Core/DSP/Instruments/InstrumentManager.c:16`: add after line 16

Anchor (line 16 today): `#include "AlkalineVoice.h"`

```c
#include "ColloidVoice.h"         /* S095 P2: the runtime member and its entry points */
```

#### S1.23 `Core/DSP/Instruments/InstrumentManager.c:79`: add after line 79

Anchor (line 79 today): `alkaline_menu_pages, ALKALINE_MENU_PAGE_COUNT },`

```c
    /*
     * S095 P2: Colloid. Token/extension "col"/".col"; directory
     * /Instrument/Colloid (7 characters; SD_CARD/Instrument/Colloid in the
     * card image with init.col). Basic (C-D1): may take any slot.
     */
    [INSTRUMENT_TYPE_COL] =
    { INSTRUMENT_TYPE_COL, "col", colloid_instrument_display_label, ".col", "Colloid",
      COLLOID_INSTRUMENT_TYPE_FLAGS,
      colloid_param_descriptors, COLLOID_PARAM_DESCRIPTOR_COUNT,
      colloid_menu_pages, COLLOID_MENU_PAGE_COUNT },
```

#### S1.24 `Core/DSP/Instruments/InstrumentManager.c:210`: add after line 210

Anchor (line 210 today): `AlkalineVoice alkaline;`

```c
    ColloidVoice colloid;     /* S095 P2: 1,032 B (measured); slot reserve 1,264 B */
```

#### S1.25 `Core/DSP/Instruments/InstrumentManager.c:227`: add after line 227

Anchor (line 227 today): `"AlkalineVoice exceeds tagged slot reserve");`

```c
/* S095 P2: the Colloid voice fits the tagged slot (no RAM growth: 1,032 B,
 * its right output block and both buses included, against 2 x 632 B). */
_Static_assert(sizeof(ColloidVoice) <= INSTRUMENT_RUNTIME_SLOT_BYTES,
               "ColloidVoice exceeds tagged slot reserve");
```

#### S1.26 `Core/DSP/Instruments/InstrumentManager.c:298`: add after line 298

Anchor (line 298 today): `}`

```c

/*
 * The slot's Colloid runtime (S095 P2).
 *
 * What:       the tagged union member for slot, or NULL out of range. Like
 *             the other accessors it does not check the slot's type: every
 *             caller switches on instrumentManager_slotType() first.
 */
static ColloidVoice *instrumentManager_colloidRuntime(uint8_t slot)
{
    return (slot < INSTRUMENT_SLOT_COUNT) ? &runtime_slots[slot].colloid : 0;
}
```

#### S1.27 `Core/DSP/Instruments/InstrumentManager.c:853`: add after line 853

Anchor (line 853 today): `return (uint8_t)(ALK_WAVE_COUNT - 1u);`

```c
            /* S095 P2 (C-D20): a Colloid slot lists Saw Rec Tri Sin under
             * wav and Rec alone under pwm; under wtb the shared count below
             * (the installed tables) applies. */
            if (osc && instrumentManager_slotType(slot) == INSTRUMENT_TYPE_COL) {
                if (osc->oscType == OSC_TYPE_WAV)
                    return (uint8_t)(COL_WAVE_COUNT - 1u);
                if (osc->oscType == OSC_TYPE_PWM)
                    return (uint8_t)(COL_PWM_WAVE_COUNT - 1u);
            }
```

#### S1.28 `Core/DSP/Instruments/InstrumentManager.c:1472`: add after line 1472

Anchor (line 1472 today): `case INSTRUMENT_TYPE_ALK: return instrumentManager_alkalineRuntime(slot);`

```c
    case INSTRUMENT_TYPE_COL: return instrumentManager_colloidRuntime(slot);   /* S095 P2 */
```

#### S1.29 `Core/DSP/Instruments/InstrumentManager.c:1494`: add after line 1494

Anchor (line 1494 today): `return voice ? &voice->lfo : 0; }`

```c
    case INSTRUMENT_TYPE_COL: {   /* S095 P2: the Colloid voice owns its LFO like every engine */
        ColloidVoice *voice = instrumentManager_colloidRuntime(slot);
        return voice ? &voice->lfo : 0; }
```

#### S1.30 `Core/DSP/Instruments/InstrumentManager.c:1565`: add after line 1565

Anchor (line 1565 today): `break;`

```c
        case INSTRUMENT_TYPE_COL:   /* S095 P2 */
            Colloid_initVoice(instrumentManager_colloidRuntime(slot));
            break;
```

#### S1.31 `Core/DSP/Instruments/InstrumentManager.c:1595`: add after line 1595

Anchor (line 1595 today): `}`

```c
    /*
     * S095 P2: the Colloid has no SlopeEg2 either; it is quiet when its amp
     * envelope is idle (Colloid_isQuiet(): the release fell below 1e-4, this
     * function's threshold).
     */
    if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_COL) {
        const ColloidVoice *voice = instrumentManager_colloidRuntime(slot);
        return voice ? Colloid_isQuiet(voice) : 1u;
    }
```

#### S1.32 `Core/DSP/Instruments/InstrumentManager.c:1709`: add after line 1709

Anchor (line 1709 today): `break;`

```c
    case INSTRUMENT_TYPE_COL:   /* S095 P2 */
        Colloid_initVoice(instrumentManager_colloidRuntime(slot));
        break;
```

#### S1.33 `Core/DSP/Instruments/InstrumentManager.c:1827`: add after line 1827

Anchor (line 1827 today): `return voice ? voice->pan : 64u; }`

```c
    case INSTRUMENT_TYPE_COL: {   /* S095 P2: the mixer's per-slot pan (C-D11) */
        ColloidVoice *voice = instrumentManager_colloidRuntime(slot);
        return voice ? voice->pan : 64u; }
```

#### S1.34 `Core/DSP/Instruments/InstrumentManager.c:1859`: add after line 1859

Anchor (line 1859 today): `return voice ? voice->vol : 0.0f; }`

```c
    case INSTRUMENT_TYPE_COL: {   /* S095 P2 */
        ColloidVoice *voice = instrumentManager_colloidRuntime(slot);
        return voice ? voice->vol : 0.0f; }
```

#### S1.35 `Core/DSP/Instruments/InstrumentManager.c:2090`: add after line 2090

Anchor (line 2090 today): `break;`

```c
    case INSTRUMENT_TYPE_COL:   /* S095 P2: control rate; the audio loops are the sync block */
        Colloid_calcVoiceAsync(instrumentManager_colloidRuntime(slot));
        break;
```

#### S1.36 `Core/DSP/Instruments/InstrumentManager.c:2136`: add after line 2136

Anchor (line 2136 today): `break;`

```c
    case INSTRUMENT_TYPE_COL:
        /*
         * S095 P2: Colloid_calcVoiceAsync() (control rate: envelopes, pitch
         * and glide, every oscillator's increment, row and stereo position,
         * both filters' coefficient ramps) and Colloid_calcVoiceSyncBlock()
         * (the oscillator pair loops, ITCM, then the filter/output loop).
         * buf receives the left block; the right block stays in the voice
         * for the mixer (instrumentManager_slotRight()). Both are noinline
         * (ColloidVoice.h): LTO must not inline them into
         * mixer_calcNextSampleBlock() (S086 F1 register pressure).
         */
        Colloid_calcVoiceSyncBlock(instrumentManager_colloidRuntime(slot), buf, size);
        break;
```

#### S1.37 `Core/DSP/Instruments/InstrumentManager.c:2162`: add after line 2162

Anchor (line 2162 today): `}`

```c

/*
 * The slot's right output block (S095 P2). Contract: InstrumentManager.h.
 */
const int16_t *instrumentManager_slotRight(uint8_t slot)
{
    const ColloidVoice *voice;

    if (instrumentManager_slotType(slot) != INSTRUMENT_TYPE_COL)
        return 0;
    voice = instrumentManager_colloidRuntime(slot);
    return voice ? voice->right : 0;
}

/*
 * Whether an instrument cell has no effect now (S095 P2). Contract:
 * InstrumentManager.h.
 */
uint8_t instrumentManager_cellInactive(uint8_t slot, const ParamDescriptor *descriptor)
{
    const ColloidVoice *voice;

    if (!descriptor || instrumentManager_slotType(slot) != INSTRUMENT_TYPE_COL)
        return 0u;
    voice = instrumentManager_colloidRuntime(slot);
    return voice ? colloidParameters_cellInactive(voice, descriptor) : 0u;
}
```

#### S1.38 `Core/DSP/Instruments/InstrumentManager.c:2275`: add after line 2275

Anchor (line 2275 today): `break;`

```c
    case INSTRUMENT_TYPE_COL:
        /*
         * S095 P2: no step length (C-D8: the sustain switch ends notes). The
         * platform part (LFO retrigger, velocity nodes) runs inside
         * Colloid_triggerVoice() for `slot`; the slot-5 alternate decay
         * above finds no amp_envelope_decay row and does nothing, so track 7
         * plays slot 5 as an ordinary hit.
         */
        Colloid_triggerVoice(instrumentManager_colloidRuntime(slot), slot,
                             velocity, note);
        break;
```

#### S1.39 `Core/DSP/Instruments/InstrumentManager.c:2399`: add after line 2399

Anchor (line 2399 today): `}`

```c
    if (type == INSTRUMENT_TYPE_COL) {   /* S095 P2: the pitch anchor */
        ColloidVoice *voice = instrumentManager_colloidRuntime(slot);
        if (!voice) return 0;
        if (strncmp(key, "osc1_", 5) == 0) return &voice->osc;
    }
```

#### S1.40 `Core/DSP/Instruments/InstrumentManager.c:2450`: add after line 2450

Anchor (line 2450 today): `return 0; }`

```c
    case INSTRUMENT_TYPE_COL: {   /* S095 P2 */
        ColloidVoice *voice = instrumentManager_colloidRuntime(slot);
        if (!voice) return 0;
        if (selector == IM_SPECIAL_OSC1) return &voice->osc;
        return 0; }
```

#### S1.41 `Core/DSP/Instruments/InstrumentManager.c:3956`: modify (replace) line 3956

Anchor (line 3956 today): `} else {`

Line 3956 becomes:

```c
        } else if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_COL) {
            /* S095 P2 (C-D20): the Colloid's typ (wav / wtb / pwm) */
            ColloidVoice *col = instrumentManager_colloidRuntime(slot);

            if (!col) return 0u;
            Colloid_setOscType(col, byteValue);
        } else {
```


**InstrumentManager switches that need no Colloid case** (each defaults
correctly, as for the Alkaline; checked): `instrumentManager_slotRunMode()`
(`:1878`, default rst: no run row), `instrumentManager_slotVoiceChain()`
(`:1906`), `instrumentManager_recalcSlotFilter()` (`:1926`: no
`ResonantFilter`), `instrumentManager_slotHasActiveItpFade()` (`:1996`: no
interpolation slots), `instrumentManager_slotUsesSrr()` (`:2143`, default 0:
no SRR, the brief), `instrumentManager_filter()` / `_ampEg()` /
`_distortion()` / `_transient()` (`:2456`, `:2476`, `:2510`, `:2530`: NULL),
`instrumentManager_applySlot6AlternateDecay()` (no `amp_envelope_decay` row:
returns), the `IM_SPECIAL_INSTRUMENT_DRIVE` writer (`:3868`: the Colloid has
no `instrument_drive` row), `instrumentManager_classifySpecialKey()` (`:4046`:
key rules, type-independent for `osc1_*`, `lfo_*` and `col_*`; the boot
self-check walks the registry, so the Colloid is covered with no edit).

### 2.4 Storage

#### S1.42 `Core/Hardware/SD/storageTypes.h:142`: add after line 142

Anchor (line 142 today): `#define STORAGE_INSTRUMENT_ALK`

```c
#define STORAGE_INSTRUMENT_COL     INSTRUMENT_TYPE_COL   /* S095 P2 */
```


Nothing else in storage changes: the type token, extension and folder come
from the registry row (`storage_instrumentTypeToText()`,
`instrumentManager_storageDirectory()`); the `.hcnames` header accepts a
header written before the type was appended as a leading part; the boot
directory trim walks every `/Instrument` subdirectory
(`filesystem_trimInstrumentTreeBlocking()`, `filesystem.c:24251`). The two
fixed default-type tables (`filesystem.c:18307`, `SceneData.c:1126`) keep
Drum/Snare/Cymbal/HiHat defaults by design.

### 2.5 Menu

#### S1.43 `Core/Menu/MenuText.h:268`: add after line 268

Anchor (line 268 today): `};` (the end of `alkWaveNames[]`, `:266-268`).

```c
/*
 * DTYPE_ON_OFF tags for the Colloid's selectors (S095 P2).
 *
 * What:       ONOFF_TAG_COL_INTERVAL 4 (int, colIntervalNames),
 *             ONOFF_TAG_COL_FILT_MODE 5 (fmd, colFiltModeNames),
 *             ONOFF_TAG_COL_FILT_TYPE 6 (tya / tyb, colFiltTypeNames),
 *             ONOFF_TAG_COL_GLIDE_MODE 7 (pmd, colGlideModeNames). The rows'
 *             dtype byte is DTYPE_ON_OFF | (tag << 4). Tags 1-3 stay
 *             (osc1_run, voice_chain, the Alkaline/Colloid typ); 8-15 free.
 * Why:        every DTYPE_MENU id is taken (S078); menu.c resolves the name
 *             table per tag in menu_onoffTagNames(), so display, clamp,
 *             automation range and the step-edit line follow the count.
 * Accessors:  ColloidParameters.c (ROW_SELECT_ONOFF rows); menu.c
 *             menu_onoffTagNames().
 * Affiliates: COL_INTERVAL_COUNT (ColloidTables.h), COL_FMD_COUNT,
 *             COL_FT_COUNT, COL_PMD_COUNT (ColloidVoice.h): the counts are
 *             asserted in menu.c.
 */
#define ONOFF_TAG_COL_INTERVAL     4u
#define ONOFF_TAG_COL_FILT_MODE    5u
#define ONOFF_TAG_COL_FILT_TYPE    6u
#define ONOFF_TAG_COL_GLIDE_MODE   7u
/*
 * Colloid name tables (S095 P2). [0] = count, then 3-character names
 * (menu.c copies exactly 3 characters, so shorter names are space-padded).
 *
 * colIntervalNames   the sixteen interval sets, in colIntervals[] order
 *                    (ColloidTables.c, generated): uni oct sub 5th 4th o+5
 *                    maj min su2 su4 7th m7 mj7 6th m9 pwr.
 * colFiltModeNames   par (stereo: A on L, B on R), ser (mono: A then B).
 * colFiltTypeNames   LP BP HP Nch Pek (COL_FT_*).
 * colGlideModeNames  all (every note glides), leg (legato notes only).
 * colWaveNames       the osc1_wave cell under typ wav: Saw Rec Tri Sin
 *                    (COL_WAVE_*); under pwm the cell names colWaveNames[2]
 *                    (Rec) for its one entry (menu_formatWaveCell()).
 */
static const char colIntervalNames[][4] = {
    {16}, {"uni"},{"oct"},{"sub"},{"5th"},{"4th"},{"o+5"},{"maj"},{"min"},
          {"su2"},{"su4"},{"7th"},{"m7 "},{"mj7"},{"6th"},{"m9 "},{"pwr"},
};
static const char colFiltModeNames[][4] = {
    {2}, {"par"},{"ser"},
};
static const char colFiltTypeNames[][4] = {
    {5}, {"LP "},{"BP "},{"HP "},{"Nch"},{"Pek"},
};
static const char colGlideModeNames[][4] = {
    {2}, {"all"},{"leg"},
};
static const char colWaveNames[][4] = {
    {4}, {"Saw"},{"Rec"},{"Tri"},{"Sin"},
};
```

#### S1.44 `Core/Menu/menu.c:68`: add after line 68

Anchor (line 68 today): `#include "AlkalineVoice.h"`

```c
#include "ColloidVoice.h"      /* S095 P2: COL_WAVE_COUNT, the COL_* counts (the name asserts) */
```

#### S1.45 `Core/Menu/menu.c:6461`: add after line 6461

Anchor (line 6461 today): `return;`

```c

    /*
     * S095 P2: a cell with no effect in its slot's current mode shows "---"
     * (instrumentManager_cellInactive(): a Colloid's pos/psp under typ wav,
     * spr/mot in fmd ser). The value is still edited and stored; it takes
     * effect when the mode changes.
     */
    if (cell && cell->kind == MENU_CELL_INSTRUMENT &&
        instrumentManager_cellInactive(cell->slot, cell->descriptor)) {
        memcpy(valueAsText, menuText_dash, 3);
        return;
    }
```

#### S1.46 `Core/Menu/menu.c:12191`: add after line 12191

Anchor (line 12191 today): `}`

```c
    /*
     * S095 P2 (C-D20, C-D21): a Colloid slot's osc1 under wav lists Saw, Rec,
     * Tri, Sin (colWaveNames); under pwm its one entry is Rec. Under wtb the
     * cell takes the shared branch below (wNN, the table name).
     */
    if (isOsc1 && (type == OSC_TYPE_WAV || type == OSC_TYPE_PWM) &&
        instrumentManager_runtimeType(cell->slot) == INSTRUMENT_TYPE_COL) {
        const uint8_t entry = (type == OSC_TYPE_PWM)
            ? (uint8_t)((value < COL_PWM_WAVE_COUNT) ? COL_WAVE_REC : 0xFFu)
            : (uint8_t)((value < COL_WAVE_COUNT) ? value : 0xFFu);

        if (entry != 0xFFu) {
            memcpy(nameDst, colWaveNames[entry + 1u], 3u);
            memcpy(codeDst, colWaveNames[entry + 1u], 3u);
        }
        return;
    }
```

#### S1.47 `Core/Menu/menu.c:12400`: add after line 12400

Anchor (line 12400 today): `case ONOFF_TAG_ALK_TYPE:    return alkTypeNames;`

```c
    /* S095 P2: the Colloid's selectors (MenuText.h ONOFF_TAG_COL_*) */
    case ONOFF_TAG_COL_INTERVAL:   return colIntervalNames;
    case ONOFF_TAG_COL_FILT_MODE:  return colFiltModeNames;
    case ONOFF_TAG_COL_FILT_TYPE:  return colFiltTypeNames;
    case ONOFF_TAG_COL_GLIDE_MODE: return colGlideModeNames;
```

#### S1.48 `Core/Menu/menu.c:12555`: add after line 12555

Anchor (line 12555 today): `"alkTypeNames must list wav, wtb and pwm");`

```c
/* S095 P2: the Colloid name tables list every value of their voice-side
 * enumerations (ColloidVoice.h, ColloidTables.h). */
_Static_assert((sizeof(colWaveNames) / sizeof(colWaveNames[0])) - 1u == COL_WAVE_COUNT,
               "colWaveNames must list Saw, Rec, Tri and Sin");
_Static_assert((sizeof(colIntervalNames) / sizeof(colIntervalNames[0])) - 1u ==
                   COL_INTERVAL_COUNT,
               "colIntervalNames must list every interval set");
_Static_assert((sizeof(colFiltModeNames) / sizeof(colFiltModeNames[0])) - 1u == COL_FMD_COUNT,
               "colFiltModeNames must list par and ser");
_Static_assert((sizeof(colFiltTypeNames) / sizeof(colFiltTypeNames[0])) - 1u == COL_FT_COUNT,
               "colFiltTypeNames must list every filter type");
_Static_assert((sizeof(colGlideModeNames) / sizeof(colGlideModeNames[0])) - 1u == COL_PMD_COUNT,
               "colGlideModeNames must list all and leg");
```


The Load menu needs no edit: `menu_instrumentLoadIndex[]` (`menu.c:1729`) is
sized by `INSTRUMENT_TYPE_UNKNOWN`, and the Morph row's label is built from
the display label (`ColloMrp`, `menu.c:9674-9700`). Clamp, automation range,
the step-edit line and the single-parameter view of the new tags follow
`menu_onoffTagNames()` (its six callers, `menu.c:3388`, `:6572`, `:6866` and
the three automation sites).

### 2.6 Host tools

#### S1.49 `tools/dsp_test/check_special_tags.py:31`: add after line 31

Anchor (line 31 today): `'ALK': 'Core/DSP/Instruments/Alkaline/AlkalineParameters.c',`

```python
    # S095 P2: the Colloid's oscillator rows are Drum twins (coa/fin
    # PITCH_COARSE/FINE | OSC1, typ OSC_TYPE | OSC1, wav and pos NONE); every
    # col_* key and vol/pan classify as NONE; the LFO rows as their twins.
    'COL': 'Core/DSP/Instruments/Colloid/ColloidParameters.c',
```


#### S1.50 `tools/dsp_test/Makefile:62`: modify (replace) line 62

Anchor (line 62 today): `space space_cpu_check`

Line 62 becomes:

```make
        space space_cpu_check colloid colloid_tables colloid_cpu_check
```

#### S1.51 `tools/dsp_test/Makefile:66`: modify (replace) line 66

Anchor (line 66 today): `space`

Line 66 becomes:

```make
     space colloid colloid_tables
```

#### S1.52 `tools/dsp_test/Makefile:354`: add after line 354

Anchor (line 354 today): `cd $(ROOT) && python3 tools/alkaline/make_init_alk.py --check`

```make
	cd $(ROOT) && python3 tools/colloid/make_init_col.py --check
```

#### S1.53 `tools/dsp_test/Makefile:502`: add after line 502

Anchor (line 502 today): `cd $(ROOT) && python3 tools/alkaline/gen_alk_tables.py --check`

```make

# ---------------------------------------------------------------------------
# S095 P2: the Colloid.
#
# What:       colloid builds modules/colloid.c on the real ColloidTables.c,
#             ColloidOsc.c and ColloidVoice.c (extracted, the ITCM section
#             removed), the real stock tables and PWM width table and the
#             real catalogue code, and runs laws CO-OSC .. CO-WTB; then eight
#             break builds, each of which must fail its law:
#               side   the second oscillator's position from the low half
#                      (SMLAWB for SMLAWT)                       -> CO-OSC
#               frac   a 16-bit fraction (>> 6, >> 16)           -> CO-OSC
#               sus    the sustain stage ignores the switch      -> CO-EG
#               snap   the ramp does not start on the target     -> CO-SVF
#               legato every trigger restarts                    -> CO-EG
#               wlaw   w(f) held at 1                            -> CO-W
#               slew   the pwm width jumps                       -> CO-PWM
#               latch  wtb frames follow pos mid-note            -> CO-WTB
#             colloid_tables checks ColloidTables.c against its generator;
#             colloid_cpu_check prints the linked counts (colloid_cpu.py),
#             against COL_BASE when given.
# Accessors:  make -C tools/dsp_test colloid | colloid_tables |
#             colloid_cpu_check [COL_COUNT=10] [COL_BASE=baselines/s095p2/colloid_cpu.txt].
COL := $(ROOT)/Core/DSP/Instruments/Colloid
COL_GEN := $(GEN)/ColloidTables.host.h $(GEN)/ColloidTables.host.c \
           $(GEN)/ColloidOsc.host.h $(GEN)/ColloidOsc.host.c \
           $(GEN)/ColloidVoice.host.h $(GEN)/ColloidVoice.host.c
COL_COUNT ?= 12
colloid: $(GEN)/Oscillator.host.h $(GEN)/oscPwmTables.host.h $(GEN)/oscPwmTables.host.c \
	$(GEN)/oscPwmWarp.host.h | $(GEN)
	$(EX) --src $(ALK)/AlkalineControl.h --body --out $(GEN)/AlkalineControl.host.h
	for f in ColloidTables.h ColloidTables.c ColloidOsc.h ColloidOsc.c ColloidVoice.h ColloidVoice.c; do \
	  $(EX) --src $(COL)/$$f --body --out $(GEN)/$${f%.*}.host.$${f##*.} || exit 1; \
	done
	for t in sine_table sawTable triTable recTable; do \
	  $(EX) --src $(ROOT)/Core/DSPAudio/wavetable.c --object $$t --out $(GEN)/col_$$t.raw.c && \
	  sed "s/const int16_t $$t/int16_t $$t/" $(GEN)/col_$$t.raw.c > $(GEN)/col_$$t.c || exit 1; \
	done
	$(HOSTCC) $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c $(CAT_HOST_SRC) -o build/colloid -lm
	./build/colloid
	mkdir -p build/break_col_side
	sed 's/busS\[n\] = col_smlawt(s1, posPair, col_smulwb(s0, posPair));/busS[n] = col_smlawb(s1, posPair, col_smulwb(s0, posPair));/' \
	  $(GEN)/ColloidOsc.host.c > build/break_col_side/ColloidOsc.host.c
	@grep -q 'col_smlawb(s1, posPair, col_smulwb' build/break_col_side/ColloidOsc.host.c || \
	  { echo "colloid_break_side: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_side $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_side -lm
	@if ./build/colloid_break_side >/dev/null 2>&1; then \
	  echo "colloid_break_side: FAILED (the second position from the low half not caught)"; exit 1; \
	else echo "colloid_break_side: caught (law CO-OSC fails as expected)"; fi
	mkdir -p build/break_col_frac
	sed 's/return a + (((row\[na\] - a) \* (int32_t)((ph >> 7) \& 0x7FFFu)) >> 15);/return a + (((row[na] - a) * (int32_t)((ph >> 6) \& 0xFFFFu)) >> 16);/' \
	  $(GEN)/ColloidOsc.host.c > build/break_col_frac/ColloidOsc.host.c
	@grep -q '(ph >> 6) \& 0xFFFFu)) >> 16' build/break_col_frac/ColloidOsc.host.c || \
	  { echo "colloid_break_frac: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_frac $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_frac -lm
	@if ./build/colloid_break_frac >/dev/null 2>&1; then \
	  echo "colloid_break_frac: FAILED (a 16-bit fraction not caught)"; exit 1; \
	else echo "colloid_break_frac: caught (law CO-OSC fails as expected)"; fi
	mkdir -p build/break_col_sus
	sed 's/if (!susOn)/if (0)/' \
	  $(GEN)/ColloidVoice.host.c > build/break_col_sus/ColloidVoice.host.c
	@grep -q 'if (0)' build/break_col_sus/ColloidVoice.host.c || \
	  { echo "colloid_break_sus: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_sus $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_sus -lm
	@if ./build/colloid_break_sus >/dev/null 2>&1; then \
	  echo "colloid_break_sus: FAILED (a sustain that ignores the switch not caught)"; exit 1; \
	else echo "colloid_break_sus: caught (law CO-EG fails as expected)"; fi
	mkdir -p build/break_col_snap
	sed 's/	f->a1 = f->t1;/	(void)0;/' \
	  $(GEN)/ColloidVoice.host.c > build/break_col_snap/ColloidVoice.host.c
	@grep -q '(void)0;' build/break_col_snap/ColloidVoice.host.c || \
	  { echo "colloid_break_snap: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_snap $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_snap -lm
	@if ./build/colloid_break_snap >/dev/null 2>&1; then \
	  echo "colloid_break_snap: FAILED (a ramp that does not start on its target not caught)"; exit 1; \
	else echo "colloid_break_snap: caught (law CO-SVF fails as expected)"; fi
	mkdir -p build/break_col_legato
	sed 's/const uint8_t legato = (uint8_t)(voice->tie != 0u \&\& held);/const uint8_t legato = 0u;/' \
	  $(GEN)/ColloidVoice.host.c > build/break_col_legato/ColloidVoice.host.c
	@grep -q 'legato = 0u;' build/break_col_legato/ColloidVoice.host.c || \
	  { echo "colloid_break_legato: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_legato $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_legato -lm
	@if ./build/colloid_break_legato >/dev/null 2>&1; then \
	  echo "colloid_break_legato: FAILED (every trigger restarting not caught)"; exit 1; \
	else echo "colloid_break_legato: caught (law CO-EG fails as expected)"; fi
	mkdir -p build/break_col_wlaw
	sed 's/\* (1.0f \/ COL_W_OCTAVES));/* (1.0f \/ COL_W_OCTAVES)) * 0.0f + 1.0f;/' \
	  $(GEN)/ColloidVoice.host.c > build/break_col_wlaw/ColloidVoice.host.c
	@grep -q '0.0f + 1.0f' build/break_col_wlaw/ColloidVoice.host.c || \
	  { echo "colloid_break_wlaw: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_wlaw $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_wlaw -lm
	@if ./build/colloid_break_wlaw >/dev/null 2>&1; then \
	  echo "colloid_break_wlaw: FAILED (w(f) held at 1 not caught)"; exit 1; \
	else echo "colloid_break_wlaw: caught (law CO-W fails as expected)"; fi
	mkdir -p build/break_col_slew
	sed 's/if (d > OSC_PWM_SLEW_STEPS)/if (0)/' \
	  $(GEN)/ColloidVoice.host.c > build/break_col_slew/ColloidVoice.host.c
	@grep -q 'if (0)' build/break_col_slew/ColloidVoice.host.c || \
	  { echo "colloid_break_slew: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_slew $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_slew -lm
	@if ./build/colloid_break_slew >/dev/null 2>&1; then \
	  echo "colloid_break_slew: FAILED (a pwm width that jumps not caught)"; exit 1; \
	else echo "colloid_break_slew: caught (law CO-PWM fails as expected)"; fi
	mkdir -p build/break_col_latch
	sed 's/const uint32_t fr = (voice->frame\[i\] < frames) ? voice->frame\[i\] : (frames - 1u);/const uint32_t fr = (osc->position * (frames - 1u)) \/ 127u;/' \
	  $(GEN)/ColloidVoice.host.c > build/break_col_latch/ColloidVoice.host.c
	@grep -q 'osc->position \* (frames' build/break_col_latch/ColloidVoice.host.c || \
	  { echo "colloid_break_latch: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_latch $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_latch -lm
	@if ./build/colloid_break_latch >/dev/null 2>&1; then \
	  echo "colloid_break_latch: FAILED (frames that follow pos mid-note not caught)"; exit 1; \
	else echo "colloid_break_latch: caught (law CO-WTB fails as expected)"; fi

colloid_tables:
	cd $(ROOT) && python3 tools/colloid/gen_col_tables.py --check

colloid_cpu_check:
	python3 colloid_cpu.py --elf $(FW_ELF) --count $(COL_COUNT) $(if $(COL_BASE),--baseline $(COL_BASE))
```


### 2.7 Step 1 gates (measured in §C)

- `make all`: no new warning; flash 713,572 B; ITCM 14,880 B.
- `make -C tools/dsp_test all` exit 0.
- Mixer `elf_hotloops.py --compare` against the pre-step-0 snapshot: 0 grew.
- Device (one flash): the boot self-check (`instrumentManager_specialTagSelfCheck()`) 0;
  Instrument Load lists `Colloid` and `ColloMrp`; `init.col` loads into any
  slot and plays (mono, left block); `sus` on holds while running and
  releases at stop; a pad hit while stopped plays A-D-R; Scene save and load
  keeps the Colloid and its values.

---

## 3. Step 2: the mixer's stereo path

### 3.1 `Core/DSPAudio/mixer.c`

Two new noinline functions after the mono pair, and the slot loop's branch.
The mono path is untouched (measured: 0 grew).

#### S2.1 `Core/DSPAudio/mixer.c:746`: add after line 746

Anchor (line 746 today): `}`

```c

/*
 * A stereo voice's dry output (S095 P2, draft §5, C-D11).
 *
 * What:       for one pre-volume stereo block (dataL, dataR: the Colloid's
 *             left and right int16 blocks), the same per-sample law as
 *             mixer_addVoiceInt16ToOutput() for each side:
 *             - the gain ramp from lastGain to gain over the block;
 *             - smL = (sample_mx_t)(dataL x currentGain x 256) (S088 P1: the
 *               float gain and the x256 shift in one expression), and smR;
 *             - the stereo routes add smL x panL to the left bus and
 *               smR x panR to the right bus, with the mixer's own per-slot
 *               constant-power gains (squareRootLut[127 - pan],
 *               squareRootLut[pan]), exactly as for a mono voice: at the
 *               centre a stereo voice with L = R adds what a mono voice adds;
 *               towards an edge it acts as a balance;
 *             - a single-side route (DAC1_L ...) adds the mono sum
 *               (L + R) / 2 at unity, as a mono voice adds its block.
 * Why:        C-D11 (user): the voice's pan is the mixer's per-slot pan,
 *             applied to the stereo pair. A separate noinline function keeps
 *             the mono path and its inlined loops unchanged (the mixer gate,
 *             elf_p4_check: 0 grew).
 * Inputs:     dest (jack-resolved routing), panL/panR, dataL/dataR, gain,
 *             lastGain, the four interleaved output buses.
 * Outputs:    the output buses (saturating adds).
 * Accessors:  mixer_calcNextSampleBlock(), for a slot whose
 *             instrumentManager_slotRight() is not NULL and whose send is
 *             idle.
 * Affiliates: mixer_addVoiceInt16ToOutput() (the mono twin; with
 *             dataL == dataR the stereo routes give its result bit for bit:
 *             host law MX-ST); mixer_addVoiceStereoToOutputAndFx().
 */
static __attribute__((noinline)) void mixer_addVoiceStereoToOutput(uint8_t dest,
		const float panL,
		const float panR,
		const int16_t* dataL,
		const int16_t* dataR,
		const float gain,
		const float lastGain,
		sample_mx_t* outL,
		sample_mx_t* outR,
		sample_mx_t* outL2,
		sample_mx_t* outR2)
{
	uint8_t i;
	const float inv_size = 1.f / (OUTPUT_DMA_SIZE - 1.f);
	const float gain_delta = gain - lastGain;

/* The mono sum of one sample for a single-side route: (L + R) / 2. */
#define MIXER_STEREO_MONO(i) ((float)(((int32_t)dataL[i] + (int32_t)dataR[i]) >> 1))

	switch(dest)
	{
	case MIXER_ROUTING_DAC1_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t smL = (sample_mx_t)((float)dataL[i] * currentGain * 256.0f);
			const sample_mx_t smR = (sample_mx_t)((float)dataR[i] * currentGain * 256.0f);
			*outL2 = bufferTool_satAdd32(*outL2, (sample_mx_t)((float)smL * panL));
			outL2 += 2;
			*outR2 = bufferTool_satAdd32(*outR2, (sample_mx_t)((float)smR * panR));
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t smL = (sample_mx_t)((float)dataL[i] * currentGain * 256.0f);
			const sample_mx_t smR = (sample_mx_t)((float)dataR[i] * currentGain * 256.0f);
			*outL = bufferTool_satAdd32(*outL, (sample_mx_t)((float)smL * panL));
			outL += 2;
			*outR = bufferTool_satAdd32(*outR, (sample_mx_t)((float)smR * panR));
			outR += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t sm = (sample_mx_t)(MIXER_STEREO_MONO(i) * currentGain * 256.0f);
			*outL2 = bufferTool_satAdd32(*outL2, sm);
			outL2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t sm = (sample_mx_t)(MIXER_STEREO_MONO(i) * currentGain * 256.0f);
			*outR2 = bufferTool_satAdd32(*outR2, sm);
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t sm = (sample_mx_t)(MIXER_STEREO_MONO(i) * currentGain * 256.0f);
			*outL = bufferTool_satAdd32(*outL, sm);
			outL += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t sm = (sample_mx_t)(MIXER_STEREO_MONO(i) * currentGain * 256.0f);
			*outR = bufferTool_satAdd32(*outR, sm);
			outR += 2;
		}
		break;
	}
#undef MIXER_STEREO_MONO
}

/*
 * A stereo voice's dry output and FX send in one pass (S095 P2, draft §5).
 *
 * What:       the dry half of every sample exactly as
 *             mixer_addVoiceStereoToOutput(); the send half as
 *             mixer_addVoiceInt16ToOutputAndFx()'s, per side:
 *             - its own ramp from sendLastGain to sendGain;
 *             - a stereo-input Effect: L x send x panL into bus 0,
 *               R x send x panR into bus 1;
 *             - a mono-input Effect: (L + R) / 2 x send, unpanned, into
 *               bus 0 (as a mono voice's unpanned send).
 * Why:        the Colloid's stereo picture reaches the stereo-input Space
 *             Effects (S095 P1) intact; one read of each block. The Effect,
 *             its stopped-transport idle (effects_returnGain()) and its return
 *             are untouched: this only fills the FX bus.
 * Inputs:     as mixer_addVoiceStereoToOutput(), plus sendGain/sendLastGain
 *             and stereo (the live Effect's stereo-input flag).
 * Outputs:    the output buses and mixer_fx_bus.mx[0..1].
 * Accessors:  mixer_calcNextSampleBlock(), for a stereo slot when the send
 *             is active (fx_active and either send gain > 0: the existing
 *             condition).
 * Affiliates: mixer_addVoiceInt16ToOutputAndFx() (the mono twin; with
 *             dataL == dataR it gives the same dry result, and the same
 *             stereo-input send: host law MX-ST).
 */
static __attribute__((noinline)) void mixer_addVoiceStereoToOutputAndFx(uint8_t dest,
		const float panL,
		const float panR,
		const int16_t* dataL,
		const int16_t* dataR,
		const float gain,
		const float lastGain,
		const float sendGain,
		const float sendLastGain,
		const uint8_t stereo,
		sample_mx_t* outL,
		sample_mx_t* outR,
		sample_mx_t* outL2,
		sample_mx_t* outR2)
{
	uint8_t i;
	const float inv_size = 1.f / (OUTPUT_DMA_SIZE - 1.f);
	const float gain_delta = gain - lastGain;
	const float send_delta = sendGain - sendLastGain;

/* The mono sum of one sample: (L + R) / 2. */
#define MIXER_STEREO_MONO(i) ((float)(((int32_t)dataL[i] + (int32_t)dataR[i]) >> 1))
/* The send half of one sample. The stereo test is loop-invariant; GCC
** -Ofast unswitches it (as in the mono twin). */
#define MIXER_FX_SEND_STEREO_SAMPLE()                                         \
	do {                                                                      \
		const float sendCurrentGain = sendLastGain                            \
				+ ((float)i * inv_size * send_delta);                         \
		if (stereo) {                                                         \
			const float sl = (float)dataL[i] * sendCurrentGain * 256.0f;      \
			const float sr = (float)dataR[i] * sendCurrentGain * 256.0f;      \
			mixer_fx_bus.mx[0][i] = bufferTool_satAdd32(                      \
					mixer_fx_bus.mx[0][i], (sample_mx_t)(sl * panL));         \
			mixer_fx_bus.mx[1][i] = bufferTool_satAdd32(                      \
					mixer_fx_bus.mx[1][i], (sample_mx_t)(sr * panR));         \
		} else {                                                              \
			const float sm = MIXER_STEREO_MONO(i) * sendCurrentGain * 256.0f; \
			mixer_fx_bus.mx[0][i] = bufferTool_satAdd32(                      \
					mixer_fx_bus.mx[0][i], (sample_mx_t)sm);                  \
		}                                                                     \
	} while (0)

	switch(dest)
	{
	case MIXER_ROUTING_DAC1_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t smL = (sample_mx_t)((float)dataL[i] * currentGain * 256.0f);
			const sample_mx_t smR = (sample_mx_t)((float)dataR[i] * currentGain * 256.0f);
			MIXER_FX_SEND_STEREO_SAMPLE();
			*outL2 = bufferTool_satAdd32(*outL2, (sample_mx_t)((float)smL * panL));
			outL2 += 2;
			*outR2 = bufferTool_satAdd32(*outR2, (sample_mx_t)((float)smR * panR));
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_STEREO:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t smL = (sample_mx_t)((float)dataL[i] * currentGain * 256.0f);
			const sample_mx_t smR = (sample_mx_t)((float)dataR[i] * currentGain * 256.0f);
			MIXER_FX_SEND_STEREO_SAMPLE();
			*outL = bufferTool_satAdd32(*outL, (sample_mx_t)((float)smL * panL));
			outL += 2;
			*outR = bufferTool_satAdd32(*outR, (sample_mx_t)((float)smR * panR));
			outR += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t sm = (sample_mx_t)(MIXER_STEREO_MONO(i) * currentGain * 256.0f);
			MIXER_FX_SEND_STEREO_SAMPLE();
			*outL2 = bufferTool_satAdd32(*outL2, sm);
			outL2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC1_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t sm = (sample_mx_t)(MIXER_STEREO_MONO(i) * currentGain * 256.0f);
			MIXER_FX_SEND_STEREO_SAMPLE();
			*outR2 = bufferTool_satAdd32(*outR2, sm);
			outR2 += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_L:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t sm = (sample_mx_t)(MIXER_STEREO_MONO(i) * currentGain * 256.0f);
			MIXER_FX_SEND_STEREO_SAMPLE();
			*outL = bufferTool_satAdd32(*outL, sm);
			outL += 2;
		}
		break;
	case MIXER_ROUTING_DAC2_R:
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
		{
			const float currentGain = lastGain + ((float)i * inv_size * gain_delta);
			const sample_mx_t sm = (sample_mx_t)(MIXER_STEREO_MONO(i) * currentGain * 256.0f);
			MIXER_FX_SEND_STEREO_SAMPLE();
			*outR = bufferTool_satAdd32(*outR, sm);
			outR += 2;
		}
		break;
	default:
		/* An unknown routing adds no dry signal, but the send still
		** accumulates (the mono twin's rule). */
		for(i=0;i<OUTPUT_DMA_SIZE;i++)
			MIXER_FX_SEND_STEREO_SAMPLE();
		break;
	}
#undef MIXER_FX_SEND_STEREO_SAMPLE
#undef MIXER_STEREO_MONO
}
```

#### S2.2 `Core/DSPAudio/mixer.c:1136`: add after line 1136

Anchor (line 1136 today): `uint8_t pan;`

```c
		const int16_t *right;   /* S095 P2: a stereo voice's right block, or NULL */
```

#### S2.3 `Core/DSPAudio/mixer.c:1167`: add after line 1167

Anchor (line 1167 today): `pan = instrumentManager_runtimePan(slot);`

```c
		/*
		 * S095 P2: a stereo engine (the Colloid) leaves its right block in
		 * the voice; NULL for every mono type, whose path below is unchanged.
		 * One pointer test per slot per block.
		 */
		right = instrumentManager_slotRight(slot);
```

#### S2.4 `Core/DSPAudio/mixer.c:1185`: modify (replace) line 1185

Anchor (line 1185 today): `if (fx_active && (sendGain > 0.0f || mixer_send_last_gain[slot] > 0.0f))`

Line 1185 becomes:

```c
		if (right) {
			/*
			 * S095 P2 (draft §5, C-D11): the stereo dry path, and the stereo
			 * send under the same condition as the mono path's; the mixer's
			 * per-slot pan gains apply to the left and right blocks.
			 */
			if (fx_active && (sendGain > 0.0f || mixer_send_last_gain[slot] > 0.0f))
				mixer_addVoiceStereoToOutputAndFx(effectiveRouting[slot],
						squareRootLut[127-pan], squareRootLut[pan],
						sampleData, right, voiceGain, mixer_voice_last_gain[slot],
						sendGain, mixer_send_last_gain[slot], fx_stereo_in,
						&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
			else
				mixer_addVoiceStereoToOutput(effectiveRouting[slot],
						squareRootLut[127-pan], squareRootLut[pan],
						sampleData, right, voiceGain, mixer_voice_last_gain[slot],
						&output[pos],&output[pos+1],&output2[pos],&output2[pos+1]);
		} else if (fx_active && (sendGain > 0.0f || mixer_send_last_gain[slot] > 0.0f))
```


### 3.2 What the stereo path does at each routing

| Route | Dry | Send, stereo-input Effect | Send, mono-input Effect |
|---|---|---|---|
| DAC1/DAC2 stereo | L × panL → left, R × panR → right | L × panL → bus 0, R × panR → bus 1 | (L+R)/2 → bus 0 |
| DAC1_L … DAC2_R | (L+R)/2 at unity | as above | as above |

With L = R (a Colloid at `spr` 0) every case equals the mono functions'
result for that block.

### 3.3 Host law MX-ST (add to `modules/colloid.c` and the `colloid` target)

The two mono functions and the two stereo functions are extracted from the
real `mixer.c` (`extract.py --func`); `prelude.h` already provides
`sample_mx_t`, the routing enum and `mixer_fx_bus_t`.

#### S2.5 `tools/dsp_test/modules/colloid.c`: add the MX-ST group

Add before `int main(void)`:

```c
/*
 * MX-ST (S095 P2 step 2): the stereo mixer functions against the mono ones.
 *
 * What:       with dataL == dataR, mixer_addVoiceStereoToOutput() and
 *             mixer_addVoiceStereoToOutputAndFx() (stereo-input and
 *             mono-input send) produce the same output buses and FX bus as
 *             mixer_addVoiceInt16ToOutput() / mixer_addVoiceInt16ToOutputAndFx()
 *             for every routing, pan 0, 63 and 127, a ramping gain and send;
 *             with dataR = 0 a stereo route adds nothing to the right bus.
 * Why:        C-D11: a stereo voice is the mono voice's law per side.
 */
static mixer_fx_bus_t mixer_fx_bus;
static inline sample_mx_t bufferTool_satAdd32(sample_mx_t a, sample_mx_t b)
{
	const int64_t s = (int64_t)a + b;
	return (sample_mx_t)(s > INT32_MAX ? INT32_MAX : (s < INT32_MIN ? INT32_MIN : s));
}
#include "col_mixer_funcs.c"
static float sqrtLut(uint8_t p) { return sqrtf((float)p / 127.0f); }

static void test_mixer(void)
{
	static const uint8_t pans[3] = { 0u, 63u, 127u };
	int16_t d[BLK], z[BLK];
	unsigned r, pi, st, i;
	for (i = 0u; i < BLK; i++) { d[i] = (int16_t)(12000 * sin(0.3 * i)); z[i] = 0; }
	for (r = 0u; r < 6u; r++)
		for (pi = 0u; pi < 3u; pi++)
			for (st = 0u; st < 3u; st++) {   /* 0 dry, 1 send stereo-in, 2 send mono-in */
				sample_mx_t a[4][2 * BLK], b[4][2 * BLK];
				mixer_fx_bus_t fa, fb;
				const float pl = sqrtLut((uint8_t)(127u - pans[pi])), pr = sqrtLut(pans[pi]);
				memset(a, 0, sizeof(a)); memset(b, 0, sizeof(b));
				memset(&mixer_fx_bus, 0, sizeof(mixer_fx_bus));
				if (st == 0u)
					mixer_addVoiceInt16ToOutput((uint8_t)r, pl, pr, d, 0.8f, 0.5f, a[0], a[1], a[2], a[3]);
				else
					mixer_addVoiceInt16ToOutputAndFx((uint8_t)r, pl, pr, d, 0.8f, 0.5f, 0.6f, 0.2f,
					                                 (uint8_t)(st == 1u), a[0], a[1], a[2], a[3]);
				fa = mixer_fx_bus;
				memset(&mixer_fx_bus, 0, sizeof(mixer_fx_bus));
				if (st == 0u)
					mixer_addVoiceStereoToOutput((uint8_t)r, pl, pr, d, d, 0.8f, 0.5f, b[0], b[1], b[2], b[3]);
				else
					mixer_addVoiceStereoToOutputAndFx((uint8_t)r, pl, pr, d, d, 0.8f, 0.5f, 0.6f, 0.2f,
					                                  (uint8_t)(st == 1u), b[0], b[1], b[2], b[3]);
				fb = mixer_fx_bus;
				if (memcmp(a, b, sizeof(a)) != 0) fail(G_MX, "dry differs", r, pans[pi]);
				if (memcmp(&fa, &fb, sizeof(fa)) != 0) fail(G_MX, "send differs", r, st);
			}
	/* dataR = 0 on a stereo route: nothing reaches the right bus */
	{
		sample_mx_t b[4][2 * BLK];
		memset(b, 0, sizeof(b));
		mixer_addVoiceStereoToOutput(0u, 1.0f, 1.0f, d, z, 1.0f, 1.0f, b[0], b[1], b[2], b[3]);
		for (i = 0u; i < BLK; i++)
			if (b[3][2 * i] != 0) { fail(G_MX, "right bus fed from L", i, b[3][2 * i]); break; }
	}
}
```

and in the same file: `G_MX` before `G_N` in the group enum, `"MX-ST"` at
the end of `g_name[]`, and `test_mixer();` before the results loop in `main()`.

#### S2.6 `tools/dsp_test/Makefile`: add to the `colloid` recipe

After the `for t in sine_table ...` loop of the `colloid` target, the
extraction; and after the `latch` break, the `mx` break (the right side panned
by the left gain must fail MX-ST):

```make
	$(EX) --src $(ROOT)/Core/DSPAudio/mixer.c --func mixer_addVoiceInt16ToOutput \
	  --func mixer_addVoiceInt16ToOutputAndFx --func mixer_addVoiceStereoToOutput \
	  --func mixer_addVoiceStereoToOutputAndFx --out $(GEN)/col_mixer_funcs.c
```

```make
	mkdir -p build/break_col_mx
	sed 's/(sample_mx_t)((float)smR \* panR)/(sample_mx_t)((float)smR * panL)/' \
	  $(GEN)/col_mixer_funcs.c > build/break_col_mx/col_mixer_funcs.c
	@grep -q 'smR \* panL' build/break_col_mx/col_mixer_funcs.c || \
	  { echo "colloid_break_mx: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_col_mx $(HOSTFLAGS) $(OSC_HOST_INC) modules/colloid.c \
	  $(CAT_HOST_SRC) -o build/colloid_break_mx -lm
	@if ./build/colloid_break_mx >/dev/null 2>&1; then \
	  echo "colloid_break_mx: FAILED (a right side panned by panL not caught)"; exit 1; \
	else echo "colloid_break_mx: caught (law MX-ST fails as expected)"; fi
```

Run in the scratch export on the step 2 image: MX-ST 0 failures; the `mx`
break caught (18 failures). The extraction keeps the `MIXER_*` macros, which
are defined inside the function bodies.

### 3.4 Step 2 gates (measured in §C)

- `make all`: flash 716,820 B (running +5,784 B, ≤ +8,192 B).
- Mixer: 0 grew (measured 55 equal, 4 shrank).
- `make -C tools/dsp_test all` exit 0, with MX-ST and the `mx` break.
- `make -C tools/dsp_test colloid_cpu_check` and save its output as
  `tools/dsp_test/baselines/s095p2/colloid_cpu.txt` (the regression baseline;
  §C has the values measured in the export).
- Device: `spr` 127 at a high note widens the image, 0 is mono; `mot` moves
  it; `fmd ser` is mono and the compact cells of `spr`/`mot` show `---`; `pos`
  and `psp` show `---` under `typ wav`; a send into Delay/Reverb/Echo keeps
  the image, into CrumpBit/StereoFilter-mono sums.

---

## 4. Step 3: the device CPU run (C-D2)

- **Build:** a `DEV_MODE_LOGGING` image (the probe, `mixer.c:885-1003`,
  records per slot `async << 16 | sync` cycles, `MIXER_PROBE_SEC(MIX)` the
  dry/send adds; `decode_devlogs.py` `'p'` records).
- **Scene:** a Colloid in one slot, **cold** (the first instance), `typ wav`,
  Saw, `int pwr` (four members), `dtn` 127, `spr` 127, `mot` 127, `fmd par`,
  both filters LP at mid cutoff and resonance, `drv` 127, `sus` on, held, a
  stereo send into Reverb. Then the same at `typ pwm` and at `typ wtb` (a
  table installed).
- **Measure:** the slot's async + sync + its share of `MIX`. Gate: ≤ 19,620
  cycles per block.
- **If 12 does not fit:** set `#define COL_OSC_COUNT 10u` in `ColloidVoice.h`
  (the only edit; the `#elif` norm, the arrays and every law already cover
  10), rebuild, run the host suite with `COL_COUNT=10` for
  `colloid_cpu_check`, and measure again (one more flash: a new question).
- **Record:** the decoded probe in `tools/dsp_test/baselines/s095p2/`, and
  cycles per instruction (the measured cycles ÷ the `colloid_cpu.py` total ×
  32).

## 5. Step 4: a full Scene, underruns, listening

- **Scene:** Colloids and Alkalines in every slot, the worst settings, the
  Space Effect on. Watch the `cpu` widget and underruns.
- **Listening:** set and record the by-ear values: the `dtn` curve
  (`delta = dtn² × 50 cents`), the `por` range (rows 0..105 = 1 ms..2 s), the
  `mot` law (`COL_MOT_*`), the `w(f)` corners (`COL_W_LOG2_LO`,
  `COL_W_OCTAVES`), `COL_FENV_ST` (±72 semitones), the drive range (×1..×16),
  the normalisation (`COL_NORM_*`), the envelope time curve (`colEgK`, 1 ms..10 s;
  change in `gen_col_tables.py`) and `init.col`. Each changed constant's
  comment records its new value and why. A change to the tables reruns the
  generator and `colloid_tables`.

---

## 6. Resource accounting (measured in the scratch export)

| | Step 0 | Step 1 | Step 2 | Net | Line |
|---|---:|---:|---:|---:|---:|
| Flash (B) | −6,144 | +8,680 | +3,248 | **+5,784** | ≤ +8,192 |
| ITCM (B) | 0 | +848 | 0 | **+848** (14,880 / 16,384) | ≤ +1,024 |
| *As implemented (§9): flash* | −6,144 | +9,224 | +3,256 | ***+6,336*** (717,372) | ≤ +8,192 |
| *As implemented: ITCM* | 0 | +872 | 0 | ***+872*** (14,904 / 16,384) | ≤ +1,024 |
| `.data` / `.bss` (B) | 0 | 0 | 0 | **0** | 0 |
| DTCM statics (B) | 0 | 0 | 0 | 0 (4,396) | — |

The main flash items in the linked image (`nm -S`): `Colloid_calcVoiceAsync`
2,160 B, the stereo mixer functions 700 + 2,232 B, the descriptor table
1,316 B, the four ITCM loops 170 + 182 + 252 + 262 B, `colEgK` 512 B,
`Colloid_initVoice` 688 B, `col_svfControl` 500 B, the two render loops
432 + 484 B. **RAM:** the voice is 1,032 B inside the existing 1,264 B slot
reserve (232 B to spare, asserted). `menu_instrumentLoadIndex[]` grows from
10 to 12 B as every type-sized array does; it falls in existing alignment
padding, so `.bss` does not change (measured). Please confirm this 2 B is
covered by the 0 B approval.

## 7. Statements this work makes out of date (for the user to log)

- `MEMORY.md`:
  - the DSP Alkaline bullet's "`OSC_SAMPLE_START` stays **6**" and the
    "`OSC_SAMPLE_START` is 6 (S093 P3)" bullet (already 5 since S094 P1 S2);
  - the five-type list implied by the Alkaline bullets (now six: Colloid,
    Basic, `col`);
  - "Mixer … one mono block per slot" wherever stated (a stereo slot path now
    exists);
  - the ITCM subsection rule's last named subsection (`.itcm.s095p2`);
  - the sine: "`sine_table` 4,097 points" anywhere (now 1,025).
- `INSTRUMENTS_DSP_REFERENCE.md`: the opening voice count; §3.1 (the type
  table, the union, the type limits); §3.2 (the dispatcher list: `slotRight`,
  `cellInactive`); a new §5.6 (the Colloid engine); §8.2 (per-voice costs:
  §C); §9.2 (the stereo slot accessor and the inactive-cell hook as checklist
  items); the sine table size and read (`phase >> 22`).
- `MIXER_SYSTEM_DSP_REFERENCE.md`: the stereo dry and send paths (§3.2 here).
- `BANK_PRESET_ARCHITECTURE.md`, `FILESYSTEM_SPEC.md`: the sixth type, its
  token `col`, extension `.col`, folder `Colloid`, keys.
- `STORAGE_SRAM_MANIFEST.md`: the union member (1,032 B), ITCM 14,880 B,
  flash, the sine table size.
- `CPU_USE_DSP_AUDIT.md`: the Colloid's counts (§C) and the device result of
  step 3.
- `MENU_UI_SPEC.md`: ON_OFF tags 4–7; the `---` inactive cell.
- `MIDI_NRPN_TABLE.md` (root): the Colloid's 47 rows (slot × 64 + index).
- `knowledge_files/volatile/OPEN_TESTS.md`: the step 1–4 device checks.
- `S095_P2_DRAFT_COLLOID.md`: superseded by this schedule where they differ
  (§B).

## 8. Anchor checklist (verified on `b7f7eec4`)

Every edit's line and text was asserted by the patch script before it ran
(§C). The functions at the InstrumentManager lines: registry `:79`; union
`:210`; assert `:227`; `instrumentManager_alkalineRuntime()` `:295-298`;
`instrumentManager_waveDomainMax()` `:839` (branch `:853`);
`instrumentManager_runtimeInstance()` `:1456` (`:1472`);
`instrumentManager_runtimeLfo()` `:1477` (`:1494`);
`instrumentManager_runtimeInit()` `:1522` (`:1565`);
`instrumentManager_ampEnvelopeQuiet()` `:1572` (`:1595`);
`instrumentManager_resetRuntimeSlotFromScene()` `:1683` (`:1709`);
`instrumentManager_runtimePan()` `:1810` (`:1827`);
`instrumentManager_runtimeVolume()` `:1842` (`:1859`);
`instrumentManager_calcSlotAsync()` (`:2090`);
`instrumentManager_calcSlotSyncBlock()` (`:2136`);
`instrumentManager_slotUsesSrr()` `:2143-2162`;
`instrumentManager_triggerTrack()` `:2213` (`:2275`);
`instrumentManager_osc()` `:2363` (`:2399`);
`instrumentManager_oscBySelector()` `:2417` (`:2450`);
`IM_SPECIAL_OSC_TYPE` `:3931` (`:3956`). `menu.c`: include `:68`;
`menu_formatCellValue3()` `:6448` (`:6461`); `menu_formatWaveCell()` `:12151`
(`:12191`); `menu_onoffTagNames()` `:12396` (`:12400`); the name asserts
`:12549-12555`. `mixer.c`: `mixer_addVoiceInt16ToOutputAndFx()` ends `:746`;
the slot loop `:1136`, `:1167`, `:1185`. `Makefile`: `:39`, `:118`, `:161`,
`:255`. Linker `:151`, `:157-158`, `:172`, `:182`. `tools/dsp_test/Makefile`:
`.PHONY` `:62`, `all` `:66`, `special_tags` `:354`, `alkaline_tables` `:502`.
`check_special_tags.py` `TABLES` `:31`. `storageTypes.h` `:142`.
`MenuText.h` `:268`.

---

## 9. Implementation notes (kept at each step)

Tree at the start: `61c4b778` ("S095 P2 colloid pre-implementation"), which
differs from `b7f7eec4` only in the two P2 documents, so every line number
above holds. Each step was applied by the anchor-checked patch scripts verified
in the scratch export (§C): they assert each cited line's number and text
before editing, and they carry the comment blocks quoted above verbatim.

### 9.0 Step 0: the 1,024-point sine (done, 2026-11-12)

- **Before the step:** `make all` (flash 711,036 B, ITCM 14,032 B); the P4
  snapshot `make -C tools/dsp_test elf_p4_snapshot
  P4BASE=baselines/s095p2/pre`; `baselines/s095p2/link_budget_before.txt`.
- **Applied:** S0.1-S0.5 exactly as written. The table was decimated by the
  snippet (1,025 values, guard = point 0 = -32,767); then the comment edits.
  Files: `wavetable.h`, `wavetable.c`, `Oscillator.c` (12 reads, 4 comments),
  `lfo.c`, `osc_host.h`, `modules/oscillator.c`, `modules/lfo.c`,
  `modules/pwm_dump.c`.
- **Gates (all pass):**
  - `make all`: 0 errors, no new warning; **flash 704,892 B (-6,144 B)**,
    ITCM 14,032 B (`baselines/s095p2/link_budget_step0.txt`);
  - oscillator loops by length against the pre-step image
    (`baselines/s095p2/step0_loop_lengths.txt`, 62 symbols): no loop longer;
    the five sine-warp loops one shorter each (`osc_pwmWsPlainBlock` 24 -> 23,
    `FmBlock` 30 -> 29, `FmRampBlock` 31 -> 30, `RingBlock` 33 -> 32,
    `SyncBlock` 36 -> 35), the `>> 22` / `>> 7` pair folding one shift;
  - mixer: 59 hot loops equal, 0 grew (`baselines/s095p2/step0_mixer.txt`);
  - `make -C tools/dsp_test all` exit 0, 47 break builds caught.
- **Open for the device (yours):** the one listening check (a Drum `sin`, an
  FM carrier, a sine LFO on pitch). It can share the step 1 flash.
- **Note:** `elf_p4_check` compares loop *text*, so after this step it
  reports the thirteen changed sine loops as "missing" by design; the gate is
  their length (above). Later steps compare against this step's image.

### 9.1 Step 1: the Colloid engine and type (done, 2026-11-12)

- **Applied:** S1.1-S1.53 by the anchor-checked patch (every cited line's
  number and text asserted first). `ColloidTables.c` and `init.col` were
  produced by their generators in the tree (`gen_col_tables.py`,
  `make_init_col.py`) and are byte-identical to the files quoted in S1.3 and
  S1.11. `instrumentManager_slotRight()` is in, unused until step 2
  (`--gc-sections` drops it).
- **First build:** flash 713,572 B, ITCM 14,880 B, as in the scratch trial.
  Every warning was a pre-existing one; none is in new or touched code.
- **Gate failure found and fixed.** Oscillator loops compared **by length**
  (`elf_looppath.py`, all 62 P4 symbols, against the step 0 image) showed ten
  Drum-family loops longer: `osc_pwmWtFmBlock` / `osc_pwmWsFmBlock` 30/29 ->
  32/31, the `FmRamp` pair +2, the `Ring` pair +2, the `Sync` pair +3,
  `osc_smpFmBlock` 42 -> 43, `osc_smpFmRampBlock` 43 -> 44.
  `elf_p4_check` agreed (20 loop dumps missing, 8 loops merged away).
  - **What it was:** the source of those functions is unchanged. GCC compiled
    `osc_pwmWarp()`'s select as an IT block instead of a duplicated-tail
    branch. The function got smaller (54 vs 56 instructions) but each loop
    iteration got longer.
  - **Ruled out:** the dispatch switches' guessed frequencies (moving the
    Colloid case into a `__builtin_expect` branch changed nothing);
    `size == 32` propagation (both images have it); IPA profile propagation
    (`-fno-ipa-profile` gave a byte-identical image).
  - **Confirmed:** it follows LTO partitioning (`-flto-partition=one` moved a
    different set of loops).
  - **Fix (S1.17 amended):** the three Colloid engine objects are compiled
    `-fno-lto` (Makefile comment block). Every other engine's LTO unit then
    compiles as before.
  - **Cost of the fix:** +544 B flash, +24 B ITCM. The Colloid's own counts
    are 0.3 better (no LTO re-layout of its loops).
  - **Lesson for the reference documents:** a length check on every P4
    oscillator symbol belongs in each step's gate; `elf_loopdump --compare`
    alone compares text, and the scratch §C row relied on it (corrected
    there).
- **Gates after the fix (all pass):**
  - `make all`: flash **714,116 B** (step +9,224 B; **running +3,080 B**);
    ITCM **14,904 B (+872 B)**; `.data` 420 B, `.bss` 419,564 B unchanged
    (`baselines/s095p2/link_budget_step1.txt`);
  - oscillator loops by length against step 0: no loop longer
    (`step1_loop_lengths.txt`); `elf_p4_check` against the step 0 snapshot
    (`baselines/s095p2/step0/`): **96 of 96 exact**
    (`step1_p4_check.txt`);
  - mixer: 58 equal, 1 shrank, 0 grew (`step1_mixer.txt`);
  - Alkaline ladder loops (`DiodeLadder_renderB2Osc`, `B2Osc2`, `B2OscPwm`,
    `B2OscPwmWt`) equal; `space_cpu.py` identical; `crumpbit_cpu.py` within
    its `--cap 184` (worst 182.7, base 182.9; `pit`'s per-block path +1
    instruction and `grn`'s -6, the remaining LTO noise);
  - `make -C tools/dsp_test all` exit 0: `special tags OK (249 rows, 0
    mismatches)`, `init.col matches the table (47 rows)`, CO-OSC...CO-WTB 0
    failures, the 8 Colloid breaks caught (55 breaks caught in all),
    `ColloidTables.c matches`;
  - `colloid_cpu.py` (voice only): plain 12 par 309.2, ser 299.1; pwm 6 par
    255.9, ser 245.9 (`step1_colloid_cpu.txt`).
- **Open for the device (yours, can share one flash with step 0's
  listening):** the boot special-tag self-check reads 0; Instrument Load lists
  `Colloid` / `ColloMrp` (copy `SD_CARD/Instrument/Colloid/` to the card);
  `init.col` plays from any slot (mono, the left block, until step 2); `sus`
  on holds while running and releases at stop; a pad hit while stopped plays
  A-D-R; a Scene save and load keeps the Colloid.

### 9.2 Step 2: the mixer's stereo path (done, 2026-11-12)

- **Applied:** S2.1-S2.6 (the two stereo functions after
  `mixer_addVoiceInt16ToOutputAndFx()`, the `right` local, its read after the
  pan, and the slot loop's stereo branch), the MX-ST group in
  `modules/colloid.c` (the §3.3 code, plus `G_MX`, `"MX-ST"`,
  `test_mixer()` in `main()`, and the header's law list), and in the
  `colloid` target the mixer extraction and the `mx` break.
- **Gates (all pass):**
  - `make all`: no new warning; flash **717,372 B** (step +3,256 B;
    **running +6,336 B**, ≤ +8,192 B); ITCM 14,904 B; `.data` / `.bss`
    unchanged (`baselines/s095p2/link_budget_step2.txt`);
  - oscillator loops: by length, no loop longer
    (`step2_loop_lengths.txt`); `elf_p4_check` against step 0: 96 of 96 exact
    (`step2_p4_check.txt`);
  - mixer against the pre-step-0 snapshot: **55 equal, 4 shrank, 0 grew**
    (`step2_mixer.txt`);
  - Alkaline ladder loops equal (`step2_alkaline_loops.txt`); `space_cpu.py`
    identical to step 0 (`step2_space_cpu.txt`); `crumpbit_cpu.py --cap 184
    --feed-cap 204` passes, feed worst 202.7 (`step2_crumpbit_cpu.txt`);
  - `make -C tools/dsp_test all` exit 0, MX-ST 0 failures, `mx` break caught
    (56 breaks caught in all);
  - `colloid_cpu.py` (the regression baseline,
    `baselines/s095p2/colloid_cpu.txt`): plain 12 par **309.2**, ser 299.1;
    pwm 6 par **255.9**, ser 245.9. `make -C tools/dsp_test
    colloid_cpu_check COL_BASE=baselines/s095p2/colloid_cpu.txt` passes. The
    count-10 line (`colloid_cpu_count10_estimate.txt`, 271.2) is counted on
    the 12-oscillator image with ten control iterations, so it is an
    *estimate*; a `COL_OSC_COUNT 10` build replaces it.

### 9.3 Steps 3 and 4: on the device (open; yours)

The current build (`build/LXRV2_lxr02.img`, 717,372 B, `DEV_MODE_LOGGING` 1,
so the probe is in) answers steps 0-3 in one flash. Copy
`SD_CARD/Instrument/Colloid/` to the card first.

1. **Step 0 listening:** a Drum `sin`, an FM carrier, a sine LFO on pitch.
2. **Step 1:** the boot special-tag self-check reads 0; Instrument Load lists
   `Colloid` and `ColloMrp`; `init.col` loads into any slot and plays; `sus`
   on holds while running and releases at stop; a pad hit while stopped plays
   A-D-R; a Scene save and load keeps the Colloid and its values.
3. **Step 2:** `spr` 127 at a high note widens the image, `spr` 0 is mono;
   `mot` moves it; `fmd ser` is mono, and `spr`/`mot` show `---`; `pos`/`psp`
   show `---` under `typ wav`; a send into Delay, Reverb or Echo keeps the
   image; into CrumpBit it is summed.
4. **Step 3 (C-D2):** the §4 Scene: one Colloid, cold, `typ wav` Saw,
   `int pwr`, `dtn`/`spr`/`mot` 127, `fmd par`, `drv` 127, `sus` on, a stereo
   send into Reverb; then `typ pwm`, then `typ wtb` with a table installed.
   Read the slot's async + sync + its share of `MIX` from the `'p'` records
   (`decode_devlogs.py`). Gate: ≤ 19,620 cycles per block. The counts
   predict 12 plain is over and 10 plain and 6 PWM fit (§C). If 12 is over:
   `#define COL_OSC_COUNT 10u` in `ColloidVoice.h` is the only edit; rebuild,
   run the host suite and `colloid_cpu_check COL_COUNT=10`, and flash again
   for the re-measure.
5. **Step 4:** the full Scene (Colloids and Alkalines, the Space Effect on),
   the `cpu` widget, underruns; then the by-ear values of §5.

Bring back the decoded probe file and your listening notes. Their records
then go in `tools/dsp_test/baselines/s095p2/`.

### 9.4 Device report 1 (2026-11-12): freeze and audio crash on switching to Colloid

**Report (user):** "freezes and audio crashes when switching instrument menu
to Colloid".

**What the code rules out.** I checked these paths and found no fault in any
of them:
- **The menu:** empty cells inside a page are skipped by design
  (`menu_voiceAbsolutePositionSelectable()`); tags 4-7 all decode through
  `menu_onoffTagNames()`.
- **The Load menu and storage:** the type list, indexes, tokens and folders
  are registry-driven (`instrumentManager_registryCount()` loops, no array
  sized for five types).
- **Initialisation:** both places that set a runtime type also initialise
  the voice (`InstrumentManager.c:1586`, `:1746`).
- **The ITCM calls:** the flash-to-ITCM calls go through linker veneers
  (`__col_osc*_veneer`).
- **Bounds:** every Colloid table index is clamped or masked.

The host bench runs the voice for 20,000 randomised blocks with no fault.

**Two mechanisms fit "freeze + audio crash", and the panel cannot tell them
apart:**
1. **Render overload.** If a block takes longer to render than to play,
   `audio_check_and_render()` (`main.c:183`) never leaves its
   `while (audioCodec_queueFreeSlots() > 0)` loop. The UI stops and the audio
   garbles.
   - The Colloid measures 309.2 instructions per sample on the host, plus
     about 48 for its stereo send. That is ~22,000 cycles estimated, against
     the 19,620 share.
   - MEMORY records that the render already sits at 99-100 % while playing.
   - So overload is the leading hypothesis. The draft's step 3 expected 12
     oscillators to be over.
2. **A fault.** There is no fault handler (`startup_stm32f765xx.s` default:
   an endless loop). The UI stops and the DMA repeats its last buffer: a
   stuck buzz.

**A real defect found while checking (fixed):**
- **The defect:** `GetRngValue()` (`random.c`) returns `RNG_DR` without
  waiting for a new value (one every ~833 ns). The Colloid's back-to-back
  reads repeated, so a trigger's twelve draws, and the twelve start phases
  at init, were nearly equal: no frame or width spread, and a phase-aligned
  stack. This is not the crash, but a phase-aligned stack starts at up to 12
  times one oscillator's level into the drive.
- **The fix:** `ColloidVoice.c` `col_rngSeed()` / `col_rngNext()` (comment
  block in place). One hardware read per trigger, salted, then xorshift32
  per oscillator. At init the seed is salted with the voice's address, so
  the six slots differ.
- **The test:** new host law CO-RND, with the RNG stand-in stuck on one value
  as the device's is between refreshes. The draws and phases must be pairwise
  distinct. Its break `rng` (a hardware read per draw) is caught.
- **A test adjusted:** CO-AMP now uses the Sin stack. With truly random
  phases, a full-bandwidth saw stack's own sample-to-sample steps exceeded
  the law's fixed allowance on some address-dependent seeds. The law tests
  the amp ramp, so a smooth signal isolates it. Ten consecutive runs are
  clean.
- **Gates after the fix:**
  - flash 717,516 B (+144 B; running +6,480 B);
  - oscillator loops 96 of 96 exact against step 0, by length and by text;
  - mixer 55 equal, 4 shrank, 0 grew;
  - the Colloid unchanged at 309.2 (the baseline check passes);
  - `make -C tools/dsp_test all` exit 0, 57 breaks caught.

**Diagnostic image (one flash, one question: overload or fault?):**
`build/LXRV2_lxr02_DIAG_2osc.img`. Rename it to `LXRV2_lxr02.img` on the
card. It is built from a scratch copy of the current tree with only
`COL_OSC_COUNT` 2 and `COL_PWM_COUNT` 2 (and their two norms). The tree keeps
12 / 6.
- Every Colloid path still runs: the pair-store loop, both filters, the
  envelopes, the positions, the stereo mixer path, the menus and storage.
- It costs about 120 instructions per sample (loops 93, per block 849 / 32),
  under half a Drum.
- **Reading the result:**
  - **no freeze:** it is overload. The fix is the planned C-D2 fallback (10
    oscillators, about 271), measured with the probe; it may also need the
    per-oscillator control cut (82-116 per oscillator per block; the motion
    and position work is half of it);
  - **still freezes:** it is a fault, now isolated to code that runs at any
    oscillator count.

The normal image (`build/LXRV2_lxr02.img`, 12 oscillators, with the RNG fix)
is also built.

### 9.5 Device report 2 (2026-11-12): the 2-oscillator image still crashes

**Result:** `LXRV2_lxr02_DIAG_2osc.img` (the Colloid at about 120
instructions per sample, under half a Drum) still freezes and crashes the
audio. **CPU overload is ruled out:** this is a fault or an endless loop in
code that runs at any oscillator count.

**Searched since, with nothing found:**
- the key-matched paths (`osc1_wave` / `osc1_type` in `InstrumentManager.c`
  and `menu.c:5330-5375`);
- the Load menu's type list (`menu_instrumentLoadStepType()`,
  `menu_instrumentLoadOptionAt()`: registry-driven);
- the filesystem's per-type index rebuild (`filesystem.c:4478-4500`:
  registry-driven).

**Diagnostic image 2 (one flash, one question: where does it stop?):**
`build/LXRV2_lxr02_DIAG_fault.img`, with its ELF
`build/lxr02_DIAG_fault.elf` for resolving the addresses. It is the current
tree (12 oscillators) plus, in a scratch copy only:
- `faultDiag.c`: HardFault / MemManage / BusFault / UsageFault handlers that
  show the stacked PC, LR, CFSR and HFSR's FORCED bit;
- a SysTick stall watch: 3 s without a main-loop pass pends PendSV, which
  shows the PC it interrupted;
- a blocking two-row LCD writer;
- a heartbeat in `main.c`'s loop.

Screen format:
- row 1 `HF pc XXXXXXXX F`: HF/MM/BF/UF for a fault, ST for a stall; F if
  the fault escalated to HardFault;
- row 2 `LLLLLLLL CCCCCCC`: the stacked LR, then CFSR's low 7 hex digits.

A legitimate main-loop block over 3 s (a sample install's flash erase) would
also trigger it; do not install samples with this image. **If nothing shows
and it still freezes:** the stall is inside an interrupt at the highest
priority, or the fault handler itself faulted. That alone narrows it a lot.

### 9.6 Device reports 3-4 (2026-11-12): the freeze is on the Load type step

- **Report 3 (first fault image):** row 1 garbage, row 2 `chg init`. `chg`
  is the Instrument Load page's type-change row (`menu.c:13655`) and `init`
  is row 000 of the Colloid index, so the index had loaded. The garbage is
  most likely the fault report itself, sent while the LCD's 4-bit interface
  was out of step.
- **Report 4 (with a nibble resync):** a blank screen. The handler ran (its
  clear landed), but its text was lost.
  - **Cause:** the LCD's `delay_us()` (`lcd.c:111`) is about 10x short
    (~93 ns per "us", by its own comment's arithmetic). The WS0010 clear
    needs milliseconds, so the bytes sent after it were ignored.
  - **Note for later:** boot works only because `lcd_init()` uses
    `delay_ms()` (SysTick) for its long waits.
- **User:** the freeze happens **as the Instrument Load type selector turns to
  Colloid**, playing or stopped, on any voice. Nothing has been confirmed or
  applied at that point.
  - With the `chg` source, the index completion only repaints
    (`menu_instrumentIndexReady()`: no selection request).
  - So the Colloid engine is not running. The fault is in Load-page, menu or
    filesystem code that meets type 5. My menu additions run only on voice
    pages and wave cells.
- **Checked, sound:**
  - `menu_instrumentLoadStepType()`, `menu_instrumentLoadOptionAt()` and
    `menu_instrumentLoadCopyTypeLabel()`;
  - the trace record's type field (`AutosaveTrace.h:656`, shift 8);
  - every array indexed by the selected type (only
    `menu_instrumentLoadIndex[]`, now 6 entries);
  - the filesystem's index rebuild selection (registry-driven).
- **Diagnostic image, third build** (`build/LXRV2_lxr02_DIAG_fault.img`,
  18:14; ELF beside it): the fault screen now times every LCD wait with the
  cycle counter (216 per us), which the handler enables and unlocks itself.
  It resyncs the 4-bit interface, re-sends the OLED power command as
  `lcd_init()` does, waits 10 ms for the clear and 200 us per byte.

### 9.7 Device report 5 (2026-11-12): CGRAM glyphs; the report moves to the next boot

- **Report 5 (third fault image, photo):** both rows are the display's own
  CGRAM glyphs (the splash pieces at codes 0-7) and `αX̄` pairs. That is the
  hex text with each byte's two nibbles swapped (`'0'` 0x30 -> 0x03, a splash
  piece). So the handler ran and wrote its report, but the WS0010 stays one
  nibble out of step through the standard 4-bit soft reset.
  - The display has no R/W line (`lcd.c:70-75`, write-only), so it is the
    nibble pairing alone.
  - Driving the LCD from a fault context is abandoned.
- **Diagnostic image, fourth build** (`build/LXRV2_lxr02_DIAG_fault.img`,
  18:22; ELF beside it). Built in a scratch copy, not the tree:
  - the fault handlers and the PendSV stall report record kind, stacked PC
    and LR, CFSR, HFSR, BFAR and MMFAR in `.devwdg_noinit` (SRAM2, NOLOAD,
    never zeroed). This is the section of the `DEV_LOGGING_IWDG` capsule,
    32 B, unused while that switch is 0 (the record is exactly 32 B, so the
    linker's 32 B assert holds);
  - then `AIRCR.SYSRESETREQ`;
  - right after `lcd_tim7_init()`, `faultDiag_bootShow()` shows the record
    through the normal LCD queue and holds. A power cycle boots normally (the
    record is cleared once shown, and RAM does not survive power-off).
- **User (binding): do not enable the watchdog** ("that only made things
  worse last time"). The image does not:
  - `DEV_LOGGING_IWDG` stays 0 and the IWDG is never started;
  - only its spare RAM section is borrowed;
  - the stall check is SysTick plus PendSV;
  - the only `0x40003000` literals in the image are the existing
    `i2s_init()` / `audioCodec_suspend()` I2S bases (+0x800), the same as in
    the normal image.

### 9.8 Device report 6 (2026-11-12): garbage screen and a hang, once a reboot

- **Report 6 (fourth fault image):** one run rebooted at once; one hung with
  the screen full of garbage.
  - **The photo:** row 1 is mostly the `…` glyph (CGRAM 0) with a few stray
    characters; row 2 is stray characters. The menu's display buffer had been
    overwritten (mostly zeros), and the hang never reached the 3 s stall
    report. This points to memory corruption, not a clean fault or loop.
  - **The reboot:** no code in the tree requests a software reset (no
    `SYSRESETREQ` / `NVIC_SystemReset`), so it was either the diagnostic's
    own reset or a hardware reset. The boot did not stop on the record
    screen, so the record was lost or never written.
- **The user's question, "type 4 / type 5":** `instrument_type_t`'s values
  (Drum 0, Snare 1, Cymbal 2, HiHat 3, Alkaline 4, Colloid 5). Files store the
  3-letter tokens; the numbers are RAM-only table indices.
- **Checked since, all sound:**
  - no `COL_*` macro collides with an existing name (none existed before);
  - the instrument-file key buffer (32 B; the longest key in any table, 24);
  - the `.hcnames` header builder (bounded);
  - `instrumentManager_resetSlot()`;
  - the type-step body (`menu_instrumentLoadStepType()`, its selectability
    check);
  - stack reach: `.bss` ends at `0x20066AA4`, so 87 KB of free SRAM1 lies
    below the 16 KB stack area;
  - the display buffers sit among the button-history variables, far from the
    file-name cache.
- **User:** consider rolling back what was done to the instrument switcher;
  the Alkaline does not do this.
  - The switcher's own code is unchanged; its list comes from the registry.
  - So the rollback is done as a bisection of my changes that the Load page
    can reach.
- **Bisection image 1:** `build/LXRV2_lxr02_BISECT_nomenu.img` (716,972 B).
  It is the current tree with `Core/Menu/menu.c` and `Core/Menu/MenuText.h`
  at `HEAD`, so my menu additions are removed: the `---` cells, the Colloid
  wave names, the selector names and their asserts.
  `ColloidParameters.c` gets local copies of the four tag values so it
  compiles unchanged. No fault diagnostics.
  - **If the freeze is gone:** it is in my menu additions.
  - **If it stays:** it is in the type's registration (registry, parameter
    table, storage folder), the next bisection.

### 9.9 Device report 7 (2026-11-12): without the menu additions it still freezes

- **Report 7 (bisection image 1, `menu.c` / `MenuText.h` at `HEAD`):**
  - still freezes, so **my menu additions are cleared**;
  - it freezes during the page redraw. Usually the top row stays
    `Load:[Alkaline]`; once it was `Load:[Colloine]`, the first five letters
    of `Colloid` written over `Alkaline`.
  - The LCD is fed one character per ~1 ms by TIM7 (priority 7), so it stops
    within a few milliseconds of the type step.
- **Priorities read:**
  - TIM7 is priority 7 (`lcd.c:462`). The render masks priorities 6 and below
    (`BASEPRI 6 << 4`, `main.c:170-181`), so TIM7 waits during each render
    block.
  - SysTick is priority 0, and PendSV is at its reset priority 0, so the
    diagnostic's stall report would have fired for a hang in the render or in
    thread mode.
  - It did not in the "garbage" run. Together with the overwritten display
    buffer, the likeliest picture is a **stray write** that corrupts the LCD
    queue, the menu and the audio state while the main loop keeps running.
- **Checked, nothing found:**
  - every filesystem array indexed by a type (none) and every IRQ mask there
    (none);
  - every constant defined as 5 (none sizes a per-type table).
- **Bisection image 2:** `build/LXRV2_lxr02_BISECT_alktable.img` (717,396 B).
  It is the current tree with the Colloid registry row pointed at the
  **Alkaline's** descriptor table and pages. Number 5, `col`, `.col`, the
  `Colloid` label and folder are unchanged. No diagnostics.
  - **If the freeze is gone:** the cause is in the Colloid parameter table
    (its 47 rows, or row kinds new to the platform), met by code that walks
    the selected type's descriptors.
  - **If it stays:** the cause is the type's identity: the number 5, the
    token, or the folder and its `.hcindex`.

### 9.10 Device report 8 (2026-11-12) and the corrupted block

- **Report 8 (bisection image 2, Colloid on the Alkaline's table):** freezes
  the same way. With report 7, **the Colloid engine, its parameter table and
  my menu additions are all cleared.** What remains is existing code meeting
  the sixth type's identity (number 5, `col`, the `Colloid` folder and its
  index) on the Load type step (Alkaline -> Colloid).
- **User:** no more bisection images.
- **Where the damage lands (from the linked map, `nm -n -S`):** one block,
  `0x20022094` to `0x200221C0`, holds every symptom:
  - `ready_tail` and `audioCodec_renderCount` (the audio queue);
  - `editDisplayBuffer` / `currentDisplayBuffer` at `0x20022124` /
    `0x20022148` (the garbage screen);
  - the event ring;
  - `audio_hw_initialized` / `audio_hw_suspended` at `0x200221B4` / `B5`
    (the audio stops; the main loop can keep running, which is why the stall
    report did not fire);
  - the LCD state machine's statics `state.4` / `cur_rs.3` at `0x200221BE`
    onward (the display stops mid-word).

  Just below the block: `parameter_values[384]` (to `0x20022030`) and the
  Scene override arrays.
- **Searched for the writer, not found:**
  - every variable-indexed write into `parameter_values[]` (all
    bounds-checked);
  - every unbounded string write (`strcpy` / `sprintf`) and every
    variable-length copy into the screen buffers in `menu.c`;
  - every screen-buffer write with a variable column;
  - the Load step's path (`menu_instrumentLoadStepType()` ->
    `menu_requestInstrumentIndexLoad()` -> `filesystem_requestLoadInstrumentIndex()`
    -> `filesystem_loadInstrumentIndex_tick()`, and the missing-index rebuild
    `filesystem_beginInstrumentIndexRecovery()` -> `filesystem_scanInstruments_tick()`
    -> `filesystem_createBootIndex_tick()`), all bounds-checked;
  - IRQ masking (`AutosaveTrace.c` only, sound); the LCD queue
    (non-blocking); codec suspension (the sample installer only).

### 9.11 Device report 9 (2026-11-12): the audio buzzes; a watchpoint image

- **User:** at the freeze the audio goes "brrrrrr": the DMA is replaying its
  last buffer, so no new blocks are rendered and committed. That fits
  `audio_hw_suspended` (or the queue state) being overwritten:
  `audio_check_and_render()` then returns early, and the main loop keeps
  running, so no stall is seen.
- **Ruled out on the way:**
  - `menu_beginNoPlaybackStorage()` suspends the codec only for Load/Save
    commands while stopped (`menu_updateNoPlaybackStorage()`), and the freeze
    happens while playing too;
  - `audioCodec_suspend()` stops the I2S hardware and PLL, which would
    silence the output rather than buzz.
- **User's choice:** the watchpoint diagnostic (option 2, no debugger), using
  all four comparators ("try other watchdog too").
- **Watchpoint image:** `build/LXRV2_lxr02_DIAG_watch.img`, ELF
  `build/lxr02_DIAG_watch.elf`. Scratch copy of the current tree plus
  `watchDiag.c`. No watchdog, no reset, no stall detector or fault handlers.
  - The four DWT comparators are in DebugMonitor mode (`DEMCR.TRCENA |
    MON_EN`, LAR unlocked), FUNCTION 5 (write), armed on the main loop's
    first pass, after boot's legitimate writes:

    | | Address | Contents (this build's map) |
    |---|---|---|
    | W0 | `0x2002215C` (mask 2) | `audio_hw_initialized`, `audio_hw_suspended`, padding |
    | W1 | `0x20022110` (mask 2) | `evt_overflow_flag`, `evt_drop_count`, padding |
    | W2 | `0x2002209E` (mask 1) | padding after `screensaver_Active` |
    | W3 | `0x200220EE` (mask 1) | padding after `editDisplayBuffer` |

    After the build, the arming code was checked in `main()`'s disassembly
    against the map.
  - `DebugMon_Handler` runs at priority 5. TIM7 is raised to 4 in this build
    only, so the LCD queue keeps draining while the handler holds. It shows
    `W<n> pc <stacked PC>` and `lr <stacked LR>`, then halts. The PC is
    imprecise, a few instructions after the store.
  - **If nothing shows and it still freezes:** the writer is not the CPU
    (DMA), or it missed these four words.

### 9.12 Device report 10 (2026-11-12): the watchpoint image hangs at boot; a rollback image

- **User:** `LXRV2_lxr02_DIAG_watch.img` does not boot. It hangs on the voice
  menu page with nothing loaded and no LEDs. The DWT image is withdrawn, and
  there will be no further diagnostic or bisection images.
- **Rollback image** `build/LXRV2_lxr02_ROLLBACK.img`, built in a scratch copy;
  the tree is unchanged. It is the tree with one line changed:
  `ColloidParameters.h` `COLLOID_INSTRUMENT_TYPE_FLAGS` becomes `0u`.
  `instrumentManager_typeSelectableForSceneSlot()` rejects a type with zero
  flags, so the Load type step never lands on Colloid. The registry row, the
  storage, the engine and the mixer path stay linked in, so flash stays at
  717,516 B. This is an interim measure while the freeze is open. It does not
  fix the freeze.
- **Freeze:** the root cause is still open. Static analysis continues.

- **Static analysis after the rollback (2026-11-12):**
  - The user reports that the freeze happens on the encoder step itself; the
    instrument loads only on "chg". The step path is
    `menu_instrumentLoadStepType()` → `filesystem_cancelBrowseRead()` →
    `menu_requestInstrumentIndexLoad(COL)` → `menu_paintTypeStepWhileBusy()`.
  - The display is drained by TIM7 (priority 7). Stopping mid-word
    ("Colloine") together with the buzz means the main loop stopped.
    Nothing is rendered, and nothing new is enqueued.
  - Checked and cleared:
    - ITCM placement and copy bounds (`_eitcm` 0x3A38);
    - the Colloid ABI attributes;
    - Colloid stack frames (96 B at most);
    - `GetRngValue()` (it does not block);
    - the step's bounds (`menu_instrumentLoadIndex[INSTRUMENT_TYPE_UNKNOWN]`);
    - the type label (`menu_copyPaddedField`);
    - the index row validator;
    - the async SD driver's loops.
  - Lead: `SD_CARD/Instrument/Colloid` is the only Instrument folder without
    a `.hcindex`, so the first browse goes through
    `filesystem_beginInstrumentIndexRecovery()` → the one-type scan →
    `filesystem_createBootIndex_tick()`, which writes `.hcindex` in a browse.
    That write path is not exercised by any other type on this card. It is
    not yet proven.

### 9.13 Resolved (2026-11-12): the freeze was a missing type folder

- **User:** the card had no `Instrument/Colloid/` folder. With the folder
  added, the normal P2 image works, and Colloid plays as built.
- **Root cause:** `filesystem_createBootIndex_tick()` case 6 dereferences
  `entry`, a local that only cases 4, 5 and 11 set (an earlier tick), when
  it creates a missing type folder. The junk pointer faults and halts
  TIM7, which explains the display stopping mid-word and the buzz.
  `filesystem_scanInstruments_tick()` also returns an unset
  `op_close_status` on that path. This predates P2; any type with a
  missing folder hits it. The fix is S095 P3 F1
  (`S095_P3_COLLOID_FOLLOW_UPS.md` §1).
- §9.4–§9.12's corruption and overload hypotheses are withdrawn. The
  diagnostic, bisection and rollback images and the scratch copies are
  deleted, and the tree never held diagnostic code.
- The follow-ups from the listening notes are planned in
  `S095_P3_COLLOID_FOLLOW_UPS.md`.
