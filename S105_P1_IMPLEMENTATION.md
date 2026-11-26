# S105 P1 — Katran further tweaks: implementation schedule

**Date:** 2026-11-26. **Branch:** `dev-ph7-alkaline`. **Plan:**
`S105_P1_KATRAN_FURTHER_TWEAKS.md` revision 3 (every question decided).
**Status:** schedule only. No code was changed.

**Line numbers** are from the working tree of 2026-11-26: HEAD `d5948f59`
plus the uncommitted S104 P1 edits. Those edits touch `menu.c`, `menu.h`,
`MenuText.h`, `InstrumentManager.c/.h` and `sequencer.c/.h`. No file under
`Core/DSP/Instruments/Katran/` was modified.
- Within one file, apply the changes **bottom-up**, or find each by the quoted
  anchor text. An edit above a later one moves its line number.

**Every change** is listed as:
- **ID:** `C<step>.<nn>`;
- **Where:** file and line(s);
- **Action:** **ADD** / **REMOVE** / **MODIFY**;
- **What:** the edit itself;
- **Comment block:** the text that goes into the source, adjacent to the
  change (the project rule). The blocks follow the house form
  (What / Why / Inputs / Outputs / Cost / Accessors / Affiliates) and are
  written to serve as the in-place documentation.
- Code sketches are the intended form. Where a constant is marked *by ear*,
  the value given is the starting value; step 5 tunes it.

**Binding rules in force** (`MEMORY.md`):
- constant CPU; Katran's gate is 380 instructions a sample
  (`katran_cpu.py:47`);
- `sizeof(KatranVoice)` ≤ 1,200 B (`InstrumentManager.c:375`);
- no backward compatibility;
- nothing needed goes in `build/`: records go in
  `tools/dsp_test/baselines/s105/`;
- no step edits `MEMORY.md` or `knowledge_files/` (the out-of-date list is
  §9).

---

## 0. Overview

| Step | Plan | What | Gate at the end |
|---|---|---|---|
| 1 | A1, A3, A4, A8 | the labels and tags; the descriptor table, pages and index map; the chaos law; the band filter; a type per oscillator | `make -C tools/dsp_test katran special_tags katran_tables`; `make all && make img`; `katran_cpu_check` (the band's saving measured, §0.2) |
| 2 | A5 | the ring crossfade; F3 after the matrix; the output scale; the drive wrappers | KT-OUT, KT-RING; count |
| 3 | A2 | the synced rate table, `seq_getElapsedTicks()`, the clock pull, the `rte` display | KT-RATE |
| 4 | A6, A7 | the loop PM (Ring/FM mode); Osc 2 self-feedback | KT-FM, KT-SDB; count |
| 5 | — | `init.kat` and `Katran_initVoice()` values by ear | your ear |
| 6 | Part B | G; features 1, 4, 3, 2, 5a; 5b only if ≥ 3 a sample remain | PC-*; count |
| 7 | — | bounds, break builds, image, hot-loop diff, size, flash; diagrams | all |
| 8 | — | device: `p` probe ≤ 18.6k cycles; listening | yours |

### 0.1 Files touched

| File | Steps |
|---|---|
| `Core/DSP/Instruments/Katran/KatranVoice.h` | 1, 2, 3, 4, 6 |
| `Core/DSP/Instruments/Katran/KatranVoice.c` | 1, 2, 3, 4, 5, 6 |
| `Core/DSP/Instruments/Katran/KatranOsc.c` / `.h` | 4, 6 |
| `Core/DSP/Instruments/Katran/KatranParameters.c` / `.h` | 1, 3, 6 |
| `Core/DSP/Instruments/Katran/KatranTables.c` / `.h` (generated) | 1, 2, 3 |
| `tools/katran/gen_kat_tables.py` | 1, 2, 3 |
| `Core/Menu/menu.h`, `Core/Menu/MenuLabels.c`, `Core/Menu/MenuText.h`, `Core/Menu/menu.c` | 1, 3, 6 |
| `Core/DSP/Instruments/InstrumentManager.c` / `.h` | 1 |
| `Core/Sequencer/sequencer.c` / `.h` | 3 |
| `tools/dsp_test/check_special_tags.py` | 1 |
| `tools/katran/make_init_kat.py`, `SD_CARD/Instrument/Katran/init.kat` | 1, 5, 6 |
| `tools/dsp_test/modules/katran.c`, `tools/dsp_test/Makefile`, `tools/dsp_test/katran_cpu.py` | 1, 2, 3, 4, 6 |
| `tools/katran/make_block_diagram.py`, `make_block_diagram_p2.py`, `KATRAN_BLOCK_DIAGRAM_SVG.md`, both `.svg` | 7 |

### 0.2 Findings made during this deep-dive

| # | Finding | Consequence for the schedule |
|---|---|---|
| D-1 | **Osc 2 renders after the delay reads** (`KatranVoice.c:1620–1632`). A6's loop PM needs this block's Osc 2 in the delay read. Osc 2's loops read only their own state (`KatranOsc.c:110–229`), so they can move first | C4.06 moves Osc 2 (and its guard) ahead of `kat_delayReadA/B()` |
| D-2 | **`kat_b_type` cannot be a plain row:** a wave row's dynamic range is refreshed only by the `IM_SPECIAL_OSC_TYPE` writer (`instrumentManager_waveDomainChanged()`, `InstrumentManager.c:5552`), and the menu clamps the wave only after that writer (`menu.c:5689–5730`) | it is tagged `IM_SPECIAL_OSC_TYPE \| IM_SPECIAL_OSC2`; the writer gets a Katran branch **before** its oscillator lookup, which returns NULL for a Katran OSC2 (`InstrumentManager.c:3745–3749`); `check_special_tags.py` gets one rule (C1.45) |
| D-3 | **A `kat_b_wave` key falls into the modulator-list range** (`InstrumentManager.c:1352–1356`) and the modulator display (`menu.c:12436–12439`) | C1.39 and C1.44 add Katran branches |
| D-4 | **The special writer replaces the generic store** (`InstrumentManager.c` `if (instrumentManager_writeSpecialRuntime(...)) return 1u;`). Writing `buf` (3) through `osc1_type` never stores 3 into `osc.oscType` (3 is `OSC_TYPE_WTB`, `Oscillator.h:98`) | `Katran_setOscType()` keeps `osc.oscType` a real table type and sets a separate `aBuf` flag (C1.32) |
| D-5 | **5b with m = Osc 2 folds into the existing gains on core A:** core A's o = t1 (o0 + o1 B) + kAM B t1 is t1 (o0 + (o1 + kAM) B), which is more ring and no new sound. On core B it adds one term, t1 · b · kAM · B² (sidebands at 2 fB) | C6.08 implements 5b as that one core-B term (+1 a sample, not +3) |
| D-6 | **`katLowCutK[]` and `kat_svfControl()` / `kat_svfInit()` have no reader after A4** | removed (C1.17–C1.21): −512 B flash for the table |
| D-7 | **`KA-RANGE` drives `kat_bPwmD()`** (`modules/katran.c:671`), and `katran_break_pwm`'s sed matches its read text (`Makefile:1381`). A7 changes that line | C4.09 updates the sed with the new text |

---

## 1. Step 1 — the table, labels, chaos, band and per-oscillator types

Step 1 lands A1, A3, A4 and A8 together. The descriptor table must have one
row per index, so the rows that A3, A4 and A8 remove and add go in at once.
Every new row byte is bound in this step. `r/f`, `sdb` and `drv` are read
from steps 2 and 4; until then they are stored and have no effect.

### 1A. Label ids (`menu.h`, `MenuLabels.c`)

#### C1.01 — MODIFY `Core/Menu/menu.h:213–215` (categories)
**What:** after `CAT_PLATE,` add `CAT_OSC1, CAT_OSC2, CAT_BUFFER,
CAT_EXCITER, CAT_CHAOS, CAT_STEREO, CAT_VCA, CAT_RING_FM, CAT_TORCIDO,`
before `CAT_LABEL_COUNT`.
**Comment block:**
```c
    /*
     * S105 P1 (A1): Katran's block categories (MenuLabels.c, same order).
     *
     * What:       nine categories named after the blocks of the Katran
     *             diagram: Osc 1, Osc 2, Buffer (the arena read's damping),
     *             Exciter (Osc 1's level and the noise into Torcido ch 2),
     *             Chaos (the four chaos-envelope rows), Stereo (rotation,
     *             width), VCA (the balance), Ring/FM (Osc 2's action on
     *             the cores) and Torcido (the loop's shaper).
     * Why:        S105_P1_KATRAN_FURTHER_TWEAKS.md §A1.1: Category + Long must
     *             read as a phrase naming what a knob does ("Ring/FM Depth"),
     *             and the short name abbreviates the long name. The earlier
     *             categories (Oscilltr, FM, Envelope, Delay for the shaper)
     *             did not say which block a row belongs to.
     * Affiliates: KatranParameters.c (the rows), MenuLabels.c catNames[].
     */
```

#### C1.02 — MODIFY `Core/Menu/MenuLabels.c:134`
**What:** after `{"Plate"},` add
`{"Osc 1"},{"Osc 2"},{"Buffer"},{"Exciter"},{"Chaos"},{"Stereo"},{"VCA"},{"Ring/FM"},{"Torcido"},`
**Comment block:** `/* S105 P1 (A1): Katran's block categories (menu.h CAT_OSC1 .. CAT_TORCIDO) */`

#### C1.03 — MODIFY `Core/Menu/menu.h:290–293` (Katran long names)
**What:** replace the group with `LONG_RUN, LONG_KICK, LONG_CYCLE,
LONG_SKEW, LONG_SELFMOD, LONG_NOISEMIX, LONG_SYNCTHR, LONG_TRACK,
LONG_INTERMOD, LONG_SIDEBAND,`. **REMOVE:** `LONG_CHAOS`, `LONG_VCABAL`,
`LONG_EXCITER`, `LONG_WET`, `LONG_DRIFT`, `LONG_LOWCUT`, `LONG_OUTFILT`.
Each was read only by `KatranParameters.c` (checked with `grep` over `Core/`).
**Comment block** (replaces the line-290 comment):
```c
    /*
     * S103 P1 / S105 P1 (A1): the Katran's own long names (MenuLabels.c,
     * same order). S105 P1 removed Chaos, VCA Bal, Exciter, Wet, Drift,
     * Low Cut and OutFilt: their rows now read as Chaos Depth, VCA Balance,
     * Exciter Level, Torcido Blend, Delay WowFlutr and Voice Tone (existing
     * ids), and the low-cut row is gone (the band filter, A4). Added
     * Intermod (Chaos Intermod) and Sideband (Ring/FM Sideband). Label ids
     * are compile-time only; later ids renumber with no stored effect.
     */
```

#### C1.04 — MODIFY `Core/Menu/MenuLabels.c:198–201`
**What:** the Katran long strings become
`{"Run"},{"Kick"},{"Cycle"},{"Skew"},{"SelfMod"},{"NoiseMix"},{"SyncThr"},{"Tracking"},{"Intermod"},{"Sideband"},`
**Comment block:** `/* S103 P1 / S105 P1: the Katran rows (menu.h LONG_RUN .. LONG_SIDEBAND) */`

#### C1.05 — MODIFY `Core/Menu/menu.h:390–400` (Katran short names)
**What:** the group becomes `SHORT_KIK, SHORT_CYC, SHORT_SKW, SHORT_IMD,
SHORT_CHS, SHORT_DTK, SHORT_EXC, SHORT_SLF, SHORT_RFM, SHORT_SDB,`.
- `SHORT_CY1`, `SHORT_SK1`, `SHORT_DP1` are renamed in place: `cyc`, `skw`,
  `imd`.
- **REMOVE** `SHORT_CY2`, `SHORT_SK2`, `SHORT_DP2`, `SHORT_SRCE`, `SHORT_LCT`.

**Comment block** (replaces lines 390–398):
```c
    /*
     * S103 P1 / S105 P1 (A1): the Katran's own short names.
     *
     * What:       kik; the chaos rows cyc skw imd (the former cy1 sk1 dp1,
     *             renamed in place) and chs; dtk; exc (Exciter Level), slf
     *             (Mod Osc SelfMod), r/f (Ring/FM Mode), sdb (Ring/FM
     *             Sideband). Removed: cy2 sk2 dp2 (A3, seven chaos rows ->
     *             four), src (A8: the buffer is an oscillator type), lct
     *             (A4: the band filter has no separate low-cut).
     * Why:        the rule of S105_P1_KATRAN_FURTHER_TWEAKS.md §A1.1: a short
     *             name abbreviates its long name. S103 P3 Tier B's reuses
     *             that broke it (frq for Rate, dpt for Level, lvA / lvB) are
     *             undone in KatranParameters.c; shared ids stay shared only
     *             for the same word (drv, wid, dpt on different pages).
     */
```

#### C1.06 — MODIFY `Core/Menu/MenuLabels.c:88–91`
**What:** `{"kik"},{"cyc"},{"skw"},{"imd"},{"chs"},{"dtk"},{"exc"},{"slf"},{"r/f"},{"sdb"},`
**Comment block:** `/* S103 P1 / S105 P1: the Katran's own short labels (menu.h SHORT_KIK .. SHORT_SDB) */`

### 1B. Selector tags (`MenuText.h`, `menu.c`)

#### C1.07 — MODIFY `Core/Menu/MenuText.h:436–458`
**What:**
- tag 4 becomes `ONOFF_TAG_KAT_OSC_TYPE`, names `{4} wav wtb pwm buf`
  (`katOscTypeNames`);
- tag 10 becomes `ONOFF_TAG_KAT_RING_MODE`, names `{4} rng fmA fmB fAB`
  (`katRingModeNames`);
- **REMOVE** `katSourceNames`, `katLoopTypeNames`.

**Comment block:**
```c
/*
 * The Katran's selectors (S103 P1; S105 P1: A8, A6).
 *
 * What:       ONOFF_TAG_KAT_OSC_TYPE 4 (katOscTypeNames: wav wtb pwm buf):
 *             the type of each Katran oscillator, osc1_type (Osc 1) and
 *             kat_b_type (Osc 2). wav / wtb / pwm are the table forms in
 *             alkTypeNames' order; buf reads the DTCM FxBuffer arena as a
 *             1,024-sample window (KAT_TYPE_*, KatranVoice.h).
 *             ONOFF_TAG_KAT_RING_MODE 10 (katRingModeNames: rng fmA fmB fAB):
 *             kat_ring_mode, whether Osc 2 rings a core's output or phase-
 *             modulates its loop, per core (KAT_RM_*).
 *             The rows' dtype byte is DTYPE_ON_OFF | (tag << 4).
 * Why:        S105 P1 A8 replaced the src selector (tag 4, off/A/B/AB: an
 *             override of the shared typ) with a type per oscillator, and A4
 *             removed the loop filter's typ (tag 10). Both tags are reused
 *             rather than taking new ones; menu_onoffTagNames() resolves the
 *             list per tag, so display, clamp, automation range and the
 *             step-edit line follow the counts. Osc 1's typ leaves
 *             ONOFF_TAG_ALK_TYPE (three values) for the four-value list.
 * Accessors:  KatranParameters.c; menu.c menu_onoffTagNames().
 * Affiliates: KAT_TYPE_COUNT, KAT_RM_COUNT (KatranVoice.h): asserted in menu.c.
 */
#define ONOFF_TAG_KAT_OSC_TYPE     4u
#define ONOFF_TAG_KAT_RING_MODE    10u
static const char katOscTypeNames[][4] = {
    {4}, {"wav"},{"wtb"},{"pwm"},{"buf"},
};
static const char katRingModeNames[][4] = {
    {4}, {"rng"},{"fmA"},{"fmB"},{"fAB"},
};
```

#### C1.08 — MODIFY `Core/Menu/menu.c:12861–12862`
**What:** `case ONOFF_TAG_KAT_OSC_TYPE: return katOscTypeNames; /* S105 P1 A8 */`
and `case ONOFF_TAG_KAT_RING_MODE: return katRingModeNames; /* S105 P1 A6 */`.
**Comment block:** none beyond the trailing comments. The function's block at
`:12830–12852` gains one line: "ONOFF_TAG_KAT_* -> the Katran's type and
ring-mode lists (S105 P1)".

#### C1.09 — MODIFY `Core/Menu/menu.c:13043–13047`
**What:** two asserts replace the source and loop-type asserts:
`(sizeof(katOscTypeNames)/sizeof(katOscTypeNames[0])) - 1u == KAT_TYPE_COUNT`
and `... katRingModeNames ... == KAT_RM_COUNT`.
**Comment block:** `/* S103 P1 / S105 P1: the Katran lists name every value (KatranVoice.h KAT_TYPE_*, KAT_RM_*). */`

#### C1.10 — MODIFY `Core/Menu/menu.c:71`
**What:** the include comment becomes `/* S103 P1 / S105 P1: KAT_TYPE_COUNT, KAT_RM_COUNT (the name asserts) */`.

### 1C. The voice header (`KatranVoice.h`)

#### C1.11 — MODIFY `KatranVoice.h:1–35` (file header)
**What:** rewrite the chain description: per core, an exciter → Torcido →
**the band filter** (one-pole HP → one-pole LP with resonance feedback) →
the delay ring. Osc 1 and Osc 2 each have a type (`wav wtb pwm buf`). F3 is
added in step 2 and the FM in step 4; their sentences are written then.
**Comment block** (the "What" paragraph's new text for the loop):
```c
 *             ... Per core: an exciter (Osc 1, a table or the arena: its
 *             type, KAT_TYPE_*) -> TORCIDO (two shaped channels) -> the band
 *             filter (S105 P1 A4: a one-pole high-pass then a one-pole
 *             low-pass, their corners from cutoff / width / offset, with a
 *             resonance feedback that rises as the band narrows; it replaced
 *             the F1 TPT SVF and the F2 low-cut) -> a 2,048-sample delay
 *             ring in one FxBuffer voice unit, whose output re-clocks the
 *             exciter (hard sync on a threshold crossing). Osc 2 (its own
 *             type and wave since S105 P1 A8) rings each core's TORCIDO
 *             channel 1 ...
```

#### C1.12 — MODIFY `KatranVoice.h:76–107` (constants)
**REMOVE:**
- `KAT_SPREAD_F1_ST` (`:78–79`): `spr` no longer moves the filter; `ofs`
  does (A4.4);
- `KAT_SRC_*` (`:92–98`);
- `KAT_LT_*` (`:99–107`).

**ADD:**
```c
/*
 * An oscillator's type (S105 P1 A8; the osc1_type and kat_b_type bytes).
 *
 * What:       0 wav, 1 wtb, 2 pwm (alkTypeNames' order: the table forms),
 *             3 buf (the arena window read). Osc 1's buf is aBuf (its
 *             osc.oscType keeps the table type it had, so leaving buf
 *             restores its wave); Osc 2's type is the bType byte itself, its
 *             table type bOscType.
 * Why:        S105 P1 A8 (your proposal): the buffer read is a type of each
 *             oscillator. It replaced the src selector (off / A / B / AB),
 *             which overrode the one typ / wav pair that both oscillators
 *             shared. Each oscillator now has its own type and wave.
 *             Per oscillator, not per core (Q-A11): Osc 1's two copies
 *             (1a core A, 1b core B) share Osc 1's type.
 */
#define KAT_TYPE_WAV        0u
#define KAT_TYPE_WTB        1u
#define KAT_TYPE_PWM        2u
#define KAT_TYPE_BUF        3u
#define KAT_TYPE_COUNT      4u
/*
 * The band filter's laws (S105 P1 A4; constants by ear).
 *
 * KAT_BAND_OFS_ST     ofs at +-63 moves core A's corners up and core B's
 *                     down by this many semitones (and the reverse).
 * KAT_BAND_RES_W      the width below which resonance starts (w = wid / 127).
 * KAT_BAND_R_MAX      the resonance feedback at width 0: r Gc <= 0.9 with the
 *                     coincident one-poles' centre gain Gc = 0.5.
 * KAT_BAND_MK         the make-up: mk = 1 + KAT_BAND_MK (1 - w)^2.
 * KAT_BAND_TRACK      the loop-pitch tracking at width 0 (none at 127):
 *                     both corners move by 0.85 (1 - w) (loopSt - ref).
 * KAT_BAND_TRACK_REF  ref: the loop pitch of tim 74 (init.kat), 32.7
 *                     semitones above 20 Hz (132 Hz).
 */
#define KAT_BAND_OFS_ST     24.0f
#define KAT_BAND_RES_W      0.5f
#define KAT_BAND_R_MAX      1.8f
#define KAT_BAND_MK         1.0f
#define KAT_BAND_TRACK      0.85f
#define KAT_BAND_TRACK_REF  32.7f
/*
 * The chaos envelopes' laws (S105 P1 A3; constants by ear).
 *
 * KAT_CE_N_MAX     the slow envelope's whole multiple of the fast one's
 *                  cycle: N = 1 + round(KAT_CE_N_MAX x (cyc / 127)^2), 1..8.
 * KAT_CE_PULL_F    imd's cross-rate pull on the fast envelope (by the slow
 *                  one's level), KAT_CE_PULL_S on the slow one (by the
 *                  fast one's).
 * KAT_CE_ROT       the skew pair's turn per slow cycle at imd 127, in turns
 *                  (a quarter: opposite -> same -> swapped -> same -> ...).
 * KAT_CE_BAND_ST   the slow envelope's reach on both band corners at chs
 *                  127: +-24 semitones about the cutoff (48 (L - 0.5)).
 * KAT_CE_F3_ST     the same on F3, half (+-12).
 */
#define KAT_CE_N_MAX        7.0f
#define KAT_CE_PULL_F       0.8f
#define KAT_CE_PULL_S       0.4f
#define KAT_CE_ROT          0.25f
#define KAT_CE_BAND_ST      48.0f
#define KAT_CE_F3_ST        24.0f
```

#### C1.13 — ADD after `KatranVoice.h:164` (after `KatSvf`)
**What:** the band state type.
```c
/*
 * One core's band filter (S105 P1 A4).
 *
 * What:       sH, sL the high-pass and low-pass one-pole states (sL is also
 *             the band's output, fed back one sample late for the
 *             resonance); gH, gL the one-pole coefficients at the block's
 *             start and dgH, dgL their per-sample steps; hSt, lSt the slewed
 *             corners in semitones above 20 Hz; r the resonance feedback and
 *             mk the input make-up for this block.
 * Why:        S105_P1_KATRAN_FURTHER_TWEAKS.md §A4: F1 (a TPT SVF with a
 *             type) and F2 (a fixed low-cut) both high-passed; one band with
 *             Colloid's corner law (col_noiseControl()) does both jobs.
 *             40 B a core, against 52 B for a KatSvf plus 4 B for f2x and a
 *             share of f2K: the pair is 36 B smaller.
 */
typedef struct { float sH, sL, gH, gL, dgH, dgL, hSt, lSt, r, mk; } KatBand;
```
**Note:** `KatSvf` stays, for `f3`. Its comment at `:152–163` gains:
"S105 P1: used by F3 only (a, da, mL, ic1, ic2, cutSt); the loop filters are
`KatBand`."

#### C1.14 — MODIFY `KatranVoice.h:166–176` (`KatCe` comment)
**What:** the comment text only; the type is unchanged.
```c
/*
 * One chaos envelope (S103 P1, P§3.3; S105 P1 A3: the four-row law).
 *
 * What:       level 0..peak; up, down: the per-block steps of the current
 *             cycle's rise and fall (linear); peak: this cycle's top;
 *             stage 0 rise, 1 fall. ce[0] is the fast envelope (the output
 *             gain, through the stereo matrix); ce[1] the slow one (both
 *             band corners and F3). Its cycle is a whole multiple N of the
 *             fast one's (kat_ceStart()).
 * Why:        free-running and never reset by a trigger (the brief). S105
 *             P1: chs (depths), cyc (both rates, spread wider when faster),
 *             skw (bipolar, opposite on the two) and imd (the relationship)
 *             replace the seven cy1 sk1 dp1 cy2 sk2 dp2 chs rows. CE2's
 *             level, which no code read (finding F-1), now drives the band.
 */
```

#### C1.15 — MODIFY `KatranVoice.h:196–264` (the struct)
| Line | Action | Member change |
|---|---|---|
| `:200–202` | ADD after `rowB, *rowBNext;` | `uint8_t aBuf, bOscType, bBindTag, bBindIndex; uint16_t bBindFrames; uint32_t bBindGen; const int16_t *bBindData;` (12 B + padding) |
| `:209–210` | REPLACE | `KatSvf f1[2];` and `float f2x[2], f2K;` → `KatBand band[2]; /* S105 P1 A4: the loop band filters ([0] A, [1] B) */` |
| `:238` | ADD after `KatCe ce[2];` | `float skPh; /* S105 P1 A3: the skew pair's rotation, in turns */` |
| `:255–260` | MODIFY | row bytes. Remove `source, ce1Cycle, ce1Skew, ce1Depth, ce2Cycle, ce2Skew, ce2Depth, loopCut, loopRes, loopType, lowCut`. Rename `bLevel` → `ringDepth`. Add `bType, bWave, chaosCycle, chaosSkew, chaosImod, ringMode, ringSideband, outDrive, fltCut, fltWidth, fltOffset` (11 out, 11 in) |

**Comment block** for the A8 members (ADD at `:202`):
```c
	/*
	 * S105 P1 A8: the oscillator types. aBuf: Osc 1 reads the arena (its
	 * osc1_type is buf); osc.oscType keeps Osc 1's table type. bOscType:
	 * Osc 2's table type (OSC_TYPE_WAV / _WTB / _PWM from bType 0..2,
	 * unchanged while bType is buf). bBind*: Osc 2's own wavetable bind
	 * (Osc 1's lives in osc.sampleCache*), the key (tag, table index,
	 * catalogue generation) and the frame count and rows; kat_wtbBindB()
	 * fills it. RAM: 12 B, inside the slot reserve.
	 */
```
**Comment block** for the row bytes (replaces the line-254 comment):
```c
	/* --- row bytes (KatranParameters.c binds them; converted in the async).
	 * S105 P1: bType / bWave (Osc 2's type and wave, A8); chaosCycle,
	 * chaosSkew, chaosImod with chaos (A3); ringDepth (was bLevel),
	 * ringMode, ringSideband (A5.2, A6, A7); fltCut, fltWidth, fltOffset
	 * (the band, A4); outDrive (A5.3). --- */
```

#### C1.16 — MODIFY `KatranVoice.h:282`, ADD after `:294`
**What:** the entry-point list gains
`Katran_setOscBType() the kat_b_type writer (wav / wtb / pwm / buf)`; the
`Katran_setOscType()` line becomes "the osc1_type writer (wav / wtb / pwm /
buf)". Prototype: `void Katran_setOscBType(KatranVoice *voice, uint8_t typ);`.

### 1D. The tables (`gen_kat_tables.py`, `KatranTables.h/.c`)

#### C1.17 — REMOVE `tools/katran/gen_kat_tables.py:55–58` and `:104–105`
**What:** `low_cut_k()` and the `katLowCutK` emission. The module docstring
(`:4`) says "six 128-entry laws" from here (seven before), and seven again
once C2.01 adds the two drive tables and C3.01 swaps the rate table.

#### C1.18 — REMOVE `KatranTables.h:10` and `:31`
**What:** the `katLowCutK` description line and its `extern`.

#### C1.19 — REGENERATE `KatranTables.c`
**What:** `python3 tools/katran/gen_kat_tables.py`; `--check` must pass.
Flash: −512 B.

### 1E. The voice source (`KatranVoice.c`)

#### C1.20 — MODIFY `KatranVoice.c:24–79` (control constants)
**REMOVE:** `KAT_K_SPAN` (`:33`, `:49`; only the SVF read it).
**ADD:**
```c
/* S105 P1 A4: the band's one-pole coefficient argument per Hz, at the full
 * rate (core A) and half rate (core B): G = 1 - exp(-2 pi f / fs) is
 * computed as 1 - 2^(-f x KAT_BAND_ARG), so one alk_exp2f() a corner. */
#define KAT_BAND_ARG          (6.28318531f * 1.44269504f / REAL_FS)
#define KAT_BAND_ARG_H        (2.0f * KAT_BAND_ARG)
```
`KAT_PI_OVER_FS_H` (`:74`) is removed (only F1 used it). `KAT_CUT_ST_MAX_H`
(`:75`) stays: it is core B's corner ceiling.

#### C1.21 — REMOVE `KatranVoice.c:211–283`; ADD the band control in their place
**REMOVE:** `kat_svfControl()` and `kat_svfInit()`. After A4 nothing reads
them: F3 has its own inline control (`:1519–1544`).
**ADD:**
```c
/*
 * One core's band coefficients for this block (S105 P1 A4).
 *
 * What:       the block's start = where the last ramp ended (g += count
 *             dg); each corner slews to its target (hT, lT, semitones above
 *             20 Hz, clamped 0..stMax) by KAT_SLEW; f = 20 x 2^(st / 12) and
 *             G = 1 - 2^(-f x arg) (the impulse-invariant one-pole, F2's
 *             form; arg = KAT_BAND_ARG or _H); dg = (G - g) / count; r and mk
 *             are stored for the block.
 * Why:        the per-sample loop (kat_coreBody()) ramps gH, gL and reads r
 *             and mk; every law is here. No tan() and no division: four
 *             alk_exp2f() a block for the two corners, where F1's
 *             kat_svfControl() took two VDIV and a tan() a core.
 * Inputs:     b, hT, lT, r, mk, arg (per Hz), stMax (115.7 core A, 104 core
 *             B: its 8.1 kHz ceiling), count (32 core A, 16 core B).
 * Cost:       about 75 a block a core (est.).
 * Accessors:  Katran_calcVoiceAsync() (x2), kat_bandInit().
 */
static __attribute__((noinline)) void kat_bandControl(KatBand *b, const float hT, const float lT,
                                                      const float r, const float mk,
                                                      const float arg, const float stMax,
                                                      const uint32_t count)
{
	const float inv = 1.0f / (float)count;
	float gh, gl;

	b->gH += (float)count * b->dgH;
	b->gL += (float)count * b->dgL;
	b->hSt += KAT_SLEW * (fminf(fmaxf(hT, 0.0f), stMax) - b->hSt);
	b->lSt += KAT_SLEW * (fminf(fmaxf(lT, 0.0f), stMax) - b->lSt);
	gh = 1.0f - alk_exp2f(-KAT_CUT_BASE_HZ * alk_exp2f(b->hSt * (1.0f / 12.0f)) * arg);
	gl = 1.0f - alk_exp2f(-KAT_CUT_BASE_HZ * alk_exp2f(b->lSt * (1.0f / 12.0f)) * arg);
	b->dgH = (gh - b->gH) * inv;
	b->dgL = (gl - b->gL) * inv;
	b->r = r;
	b->mk = mk;
}

/* A band at rest on its targets: the first block starts on them
 * (Katran_initVoice()). */
static void kat_bandInit(KatBand *b, const float hT, const float lT, const float r,
                         const float mk, const float arg, const float stMax)
```
**Note on `kat_bandInit()`:** its body is: `memset` the band, set `hSt` and
`lSt` to the clamped targets, `gH` / `gL` from them as above, `dgH = dgL =
0`, then `r`, `mk`.

**ADD** right after it: the corner law, shared by the async and the init.
```c
/*
 * One core's band targets for this block (S105 P1 A4, §A4.2–A4.5).
 *
 * What:       t = (cut - 63) / 63, w = wid / 127, o = (ofs - 63) / 63 (both
 *             clamped to +-1). The width-127 corners (Colloid's
 *             col_noiseControl() law): t > 0 HP at t x 115.7, LP at 115.7
 *             (raising the cutoff cuts lows); t < 0 HP at 0, LP at (1 + t) x
 *             115.7 (lowering it cuts highs); t = 0 the full band. As the
 *             width falls both close on their centre c: h = c + w (h1 - c),
 *             l = c + w (l1 - c). Both corners then move by
 *             mod (the patch LFO, the slow chaos envelope; Part B later)
 *             + KAT_BAND_TRACK (1 - w)(loopSt - KAT_BAND_TRACK_REF)
 *             + (core A +o, core B -o) x KAT_BAND_OFS_ST.
 *             r = KAT_BAND_R_MAX x max(0, 1 - w / KAT_BAND_RES_W)^2;
 *             mk = 1 + KAT_BAND_MK (1 - w)^2.
 * Why:        your three rows (cutoff bipolar, width, offset) replace F1's
 *             cutoff, res and typ and F2's low-cut. The tracking replaces
 *             the tracked types (LPt BPt HPt): none at full width, most on a
 *             narrow band, which then stays on the same harmonic of the comb
 *             as tim moves (Q-A5: automatic, no row). The offset replaces
 *             spr's F1 spread.
 * Inputs:     v, c (core), loopSt (the core's loop pitch, kat_delayControl()),
 *             mod (semitones on both corners).
 * Outputs:    *hT, *lT (semitones, unclamped), *r, *mk.
 * Accessors:  Katran_calcVoiceAsync(), Katran_initVoice().
 */
static inline void kat_bandTargets(const KatranVoice *v, const uint32_t c, const float loopSt,
                                   const float mod, float *hT, float *lT, float *r, float *mk)
```
**Body:** as the What: about twelve lines of arithmetic with no branch past
the `t` sign tests.

#### C1.22 — MODIFY `KatranVoice.c:285–316` (the wavetable bind)
**What:**
- `kat_wtbBind()` stays Osc 1's;
- **ADD** `kat_wtbBindB()`, which binds Osc 2 into `bBind*` from `bWave`;
- **ADD** `kat_wtbKeyChangedB()`, the same key test on `bBind*`.

**Comment block** (above the two new functions):
```c
/*
 * Osc 2's wavetable bind (S105 P1 A8).
 *
 * What:       kat_wtbBind()'s rule for Osc 2: the table = bWave clamped to
 *             the installed count; the key (KAT_BIND_WTB, the index, the
 *             catalogue generation) and the frame count and first row into
 *             bBind*. No table: one frame and NULL (the async then plays
 *             sawTable[1], as for Osc 1).
 * Why:        each oscillator has its own wave now, so two wtb oscillators
 *             may read two tables. Osc 1 keeps osc.sampleCache* (the platform
 *             wave-row code reads that bind for osc1_wave); Osc 2 has no
 *             OscInfo, so its bind is four voice members.
 * Accessors:  Katran_calcVoiceAsync() (when bOscType is wtb and the key
 *             changed).
 */
```

#### C1.23 — MODIFY `KatranVoice.c:318–359` (`kat_ceStart()`)
**What:** the new cycle law (§A3.3).
```c
	const float x = (float)(v->chaosCycle & 127u) * (1.0f / 127.0f);
	const float m = (float)(v->chaosImod & 127u) * (1.0f / 127.0f);
	const float s = fminf(fmaxf(((float)(v->chaosSkew & 127u) - 63.0f) * (1.0f / 63.0f),
	                            -1.0f), 1.0f);
	const float tf = (float)katCeBlocks[127u - (v->chaosCycle & 127u)];
	const float nMul = 1.0f + floorf(KAT_CE_N_MAX * x * x + 0.5f);
	/* the skew pair (s, -s) turned by skPh: cos and sin from sine_table,
	 * which holds -cos (cos = -T[k], sin = -T[(k + 768) & 1023]) */
	const uint32_t k = (uint32_t)(v->skPh * 1024.0f) & 0x3FFu;
	const float cs = -(float)sine_table[k] * (1.0f / 32767.0f);
	const float sn = -(float)sine_table[(k + 768u) & 0x3FFu] * (1.0f / 32767.0f);
	const float sigma = (i == 0u) ? s * (sn - cs) : s * (cs + sn);
	float total = ((i == 0u) ? tf : nMul * tf) * alk_exp2f(m * r);
	...
	rise = (uint32_t)((float)n * (0.5f + 0.5f * fminf(fmaxf(sigma, -1.0f), 1.0f)) + 0.5f);
	...
	e->peak = 1.0f - m * fabsf(r2) * 0.5f;
	...
	/* imd above 64 turns the skew pair once a slow cycle */
	if (i == 1u && m > 0.5f) {
		v->skPh += KAT_CE_ROT * (2.0f * m - 1.0f);
		v->skPh -= floorf(v->skPh);
	}
```
- The draws r, r2 are kept and always drawn (two xorshift steps a cycle,
  as now). At m = 0 the factor 2^(0 · r) is exactly 1.0, so the cycle is
  exactly N · T_f blocks: the "stable relationship" law of KT-CE.
- No index into `ce[]` changes: `ce[0]` stays the amplitude envelope and
  `ce[1]` the filter envelope, so every other reader keeps its index.

**Comment block** (replaces `:318–328`):
```c
/*
 * Start a chaos envelope's new cycle (S103 P1, P§3.3; S105 P1 A3).
 *
 * What:       the fast envelope (i 0) lasts T_f = katCeBlocks[127 - cyc]
 *             blocks (the table read backwards: a higher cyc is faster); the
 *             slow one (i 1) N x T_f, N = 1 + round(7 (cyc / 127)^2): the
 *             faster the pair, the wider apart. Each total x 2^(imd' r)
 *             (r uniform +-1), so imd 0 keeps an exact N : 1 rhythm and a
 *             higher imd lets each cycle drift. Rise share 0.5 (1 + sigma)
 *             with (sigma_slow, sigma_fast) = (s, -s) turned by skPh (s =
 *             skw bipolar): opposite on the two at skPh 0. Each stage at
 *             least 3 blocks (as before). Peak 1 - imd' |r2| / 2 (>= 0.05).
 *             Above imd 64 each slow cycle turns skPh by KAT_CE_ROT (2 imd'
 *             - 1) turns, so the shapes rotate: opposite, same, swapped.
 * Why:        S105_P1_KATRAN_FURTHER_TWEAKS.md §A3 (your four rows). The
 *             former cy1 sk1 cy2 sk2 set each envelope alone; the former chs
 *             jittered both (that jitter is imd's (i) now).
 */
```

#### C1.24 — MODIFY `KatranVoice.c:361–396` (`kat_ceStep()`)
**What:** the step is scaled by the cross-rate pull, and the loud-loop hurry
moves to `imd`:
```c
	const float m = (float)(v->chaosImod & 127u) * (1.0f / 127.0f);
	const float other = v->ce[i ^ 1u].level;
	const float pull = (i == 0u) ? (1.0f + KAT_CE_PULL_F * m * (2.0f * other - 1.0f))
	                             : (1.0f - KAT_CE_PULL_S * m * (2.0f * other - 1.0f));
	/* rise: up x pull (x the hurry above imd 64); fall: down x pull */
```
**Comment block** (replaces `:361–370`):
```c
/*
 * One chaos envelope's block (S103 P1, P§3.3; S105 P1 A3).
 *
 * What:       rise: level += up x pull until level >= peak, then fall; fall:
 *             level -= down x pull until level <= 0, then a new cycle. pull
 *             is imd's cross-rate coupling: the fast envelope runs faster
 *             while the slow one is high (1 + 0.8 imd' (2 L_slow - 1)), the
 *             slow one slower while the fast one is high (1 - 0.4 imd'
 *             (2 L_fast - 1)); 1 at imd 0. Above imd 64 the rise is also
 *             scaled by 1 + 2 (imd' - 0.5) min(1, 4 ringPrev): a loud loop
 *             hurries its own envelope (S103 P1's chs > 64 rule, on imd).
 * Why:        imd keeps the two envelopes from holding a stable rate
 *             relationship (your point P5); the coupling is per block, so the
 *             cost per sample is 0.
 */
```

#### C1.25 — MODIFY `KatranVoice.c:554–750` (`kat_oscControl()`)
**What:** each oscillator takes its own type, wave and form.

| Line | Change |
|---|---|
| `:590–597` | `aArena = v->aBuf`, `bArena = (v->bType == KAT_TYPE_BUF)`. A: `aPwm = (osc->oscType == OSC_TYPE_PWM)`, `aWtb`, `waveA = osc->waveform`, `pwmFormA`, `dTabA`. B: `bPwm = (v->bOscType == OSC_TYPE_PWM)`, `bWtb`, `waveB = v->bWave`, `pwmFormB`, `dTabB` |
| `:650–693` | the A rows use the A set |
| `:694–723` | the B rows use the B set; under wtb they read `bBindData` / `bBindFrames` |
| `:732–749` | the width loop picks `isPwm` and `dTab` per index: c < 2 is Osc 1, c = 2 is Osc 2 |

**Comment block** (added to the function's What, after item 2):
```c
 *             S105 P1 A8: each oscillator's form comes from its own type:
 *             Osc 1 from osc.oscType and osc.waveform (its two copies alike),
 *             or the arena while aBuf; Osc 2 from bOscType and bWave, or the
 *             arena while bType is buf. Osc 2's wavetable is its own bind
 *             (bBind*). The width tables (dTab) and the PWM forms are per
 *             oscillator, so a pulse Osc 1 can sit with a sine Osc 2.
```

#### C1.26 — MODIFY `KatranVoice.c:886–1021` (`kat_coreBody()`)
**What:** the band replaces F1 and F2 in the loop.

| Line | Change |
|---|---|
| `:957–959` | replace `KatSvf *const f1 = &v->f1[c]; const float mH ..., mL ...; const float f2k = ...;` with `KatBand *const bd = &v->band[c]; const float r = bd->r, mk = bd->mk; const float dgH = bd->dgH, dgL = bd->dgL;` |
| `:964–967` | replace the SVF / F2 locals with `float gH = bd->gH, gL = bd->gL; float sH = bd->sH, sL = bd->sL;` |
| `:984–996` | the per-sample filter (below) |
| `:1011–1013` | `gH += dgH; gL += dgL;` in place of the three `a +=` |
| `:1016–1018` | write back `bd->sH = sH; bd->sL = sL;` |

```c
		const float xb = mk * xin + r * sL;
		float hp, wr;

		sH += gH * (xb - sH);
		hp = xb - sH;
		sL += gL * (hp - sL);
		wr = gr * (sL + xfbS * Do + fb * D);
```
**Comment block** (in the function's What, replacing the `u = F2(F1(t1 + t2))`
text):
```c
 *             S105 P1 A4: the band: xb = mk (t1 + t2) + r sL[n - 1] (the
 *             resonance: the band's last output fed back; r = 0 in the wide
 *             half of the width); sH += gH (xb - sH), hp = xb - sH (the
 *             high-pass corner); sL += gL (hp - sL) (the low-pass corner,
 *             the output). gH, gL ramp per sample to the block's targets
 *             (kat_bandControl()). Both one-poles are F2's impulse-invariant
 *             form; core B's coefficients are computed at its half rate. It
 *             replaced the F1 TPT SVF (its three ramps, its mode mix) and
 *             F2: est. 7 to 9 fewer instructions a sample on core A, half
 *             of that on core B, and five fewer live registers.
```

#### C1.27 — MODIFY `KatranVoice.c:1089–1091` (`kat_stereoTargets()`)
**What:** the fast envelope's depth from `chs`:
`d_f = x * x` with `x = chaos / 127`; then `g = (1 - d_f + d_f * v->ce[0].level) * ...`.
The function's What (`:1065–1067`) is updated to say "CE1's gain (1 − d_f +
d_f × level), d_f = (chs / 127)², the fast envelope coming in more at higher
depths (S105 P1 A3)".

#### C1.28 — MODIFY `Katran_initVoice()` (`KatranVoice.c:1264–1443`)
| Line | Change |
|---|---|
| `:1300–1336` | the row defaults: `bType = KAT_TYPE_PWM; bWave = 2u;` (Osc 2 as today: pwm Rec), `aBuf = 0u; bOscType = OSC_TYPE_PWM;`; `chaos = 40u; chaosCycle = 37u; chaosSkew = 63u; chaosImod = 20u;` (F-2: the envelopes are on, by ear in step 5); `ringDepth = 127u;` (today's lvB 55 was ring-only, so the crossfade's ring-only end keeps that sound at the restored level); `ringMode = 0u; ringSideband = 0u; outDrive = 0u;`; `fltCut = 26u; fltWidth = 127u; fltOffset = 63u;` (an LP corner at 48 semitones ≈ 320 Hz, today's F1 at 53) |
| `:1365–1368` | unchanged (`xfb`, `fb` from `spread`, `feedback`) |
| `:1377–1378` | REMOVE `f2K`, `f2x` |
| `:1391–1398` | REPLACE the two `kat_svfInit()` with `kat_bandTargets()` + `kat_bandInit()` per core (`loopSt` from `katTimeSamples[time]`; `mod` 0) |
| `:1428–1433` | `skPh = 0.0f;` before the two `kat_ceStart()` |

**Comment block** (above the row defaults):
```c
	/* the rows of SD_CARD/Instrument/Katran/init.kat (P§11; S105 P1: the
	 * Osc 2 type and wave, the four chaos rows, the band, the ring rows and
	 * the drive; make_init_kat.py INIT holds the same bytes) */
```

#### C1.29 — MODIFY `Katran_calcVoiceAsync()` (`KatranVoice.c:1445–1573`)
| Line | Change |
|---|---|
| `:1445–1473` | the What: the band in place of F1 / F2, the chaos law, per-oscillator types (comment below) |
| `:1480` | REMOVE `dp2`. ADD `const float cx = chaos / 127; const float dS = cx * (2 - cx);` and `const float ceS = dS * (v->ce[1].level - 0.5f);` (the slow envelope's term, **finding F-1 fixed**) |
| `:1481–1482` | `sp` stays (it is used only by `kat_delayControl()` now; the local here is removed) |
| `:1494–1495` | the Osc 1 bind as now, plus `if (v->bType != KAT_TYPE_BUF && v->bOscType == OSC_TYPE_WTB && kat_wtbKeyChangedB(v)) kat_wtbBindB(v);` and the bWave clamp to Osc 2's list |
| `:1499–1517` | REPLACE the F1 loop: per core, `kat_bandTargets(v, c, loopSt[c], v->cv * lfl * 12.0f + KAT_CE_BAND_ST * ceS, &hT, &lT, &r, &mk)` then `kat_bandControl(&v->band[c], hT, lT, r, mk, c ? KAT_BAND_ARG_H : KAT_BAND_ARG, c ? KAT_CUT_ST_MAX_H : KAT_CUT_ST_MAX, c ? KAT_HALF_N : OUTPUT_DMA_SIZE)` |
| `:1518` | REMOVE the `f2K` line |
| `:1536` | `st += KAT_CE_F3_ST * ceS;` in place of `dp2 * 24.0f` |

**Comment block** (the async's What, filter part):
```c
 *             ... the band per core (S105 P1 A4: kat_bandTargets() then
 *             kat_bandControl(); its corners move with the patch LFO cv x lfl
 *             x 12, the slow chaos envelope 48 d_s (L_s - 0.5) (S105 P1 A3:
 *             d_s = x (2 - x), x = chs / 127; this term is CE2's, which the
 *             S103 P1 code applied as a fixed offset), the loop pitch at
 *             narrow widths and ofs); F3: ... + 24 d_s (L_s - 0.5) ...
```

#### C1.30 — MODIFY `Katran_calcVoiceSyncBlock()` (`KatranVoice.c:1575–1675`)
| Line | Change |
|---|---|
| `:1603–1612` | `aArena = voice->aBuf`; `bArena = (voice->bType == KAT_TYPE_BUF)`; `tableFormA` and `signA` from `osc.oscType` / `osc.waveform`; `tableFormB` and `signB` from `bOscType` / `bWave` |
| `:1625–1630` | Osc 2's form from `tableFormB`, and `kat_bPwmD(voice, signB)` |
| `:1637–1654` | Osc 1's forms from `tableFormA` / `signA` |

**Comment block** (in the What):
```c
 *             S105 P1 A8: the form of each oscillator is chosen from its own
 *             type (Osc 1: aBuf or osc.oscType / osc.waveform; Osc 2: bType
 *             or bOscType / bWave); the loops are the same equal-cost
 *             wrappers as before, so a mixed pair costs what its dearest
 *             oscillator forms cost.
```

#### C1.31 — MODIFY `KatranVoice.c:1677–1693` (the block comment)
**What:** the `Katran_setOscType()` paragraph becomes "byte 0 wav, 1 wtb, 2 pwm
→ `OSC_TYPE_*` (clamp `osc.waveform` to the new list; drop the bind); **3
buf**: `aBuf = 1`, `osc.oscType` unchanged. Any of 0..2 clears `aBuf`." Add a
paragraph for `Katran_setOscBType()` (below).

#### C1.32 — MODIFY `Katran_setOscType()` (`KatranVoice.c:1722–1741`); ADD `Katran_setOscBType()`
**What:**
- `Katran_setOscType()`: `typ >= KAT_TYPE_BUF` → `aBuf = 1u; return;`.
  Otherwise `aBuf = 0u`, then today's body.
- **ADD** `Katran_setOscBType()`:
  1. `bType = min(typ, 3)`;
  2. for 0..2: `bOscType = map(typ)`; if it changed, clamp `bWave` to the new
     list (wav 4, pwm `OSC_PWM_WAVE_COUNT`, wtb the installed count) and set
     `bBindTag = KAT_BIND_NONE`.

**Comment block** (above `Katran_setOscBType()`):
```c
/*
 * The Osc 2 type writer (S105 P1 A8). Contract: KatranVoice.h.
 *
 * What:       stores kat_b_type's byte (0 wav, 1 wtb, 2 pwm, 3 buf) in bType;
 *             for a table type sets bOscType (OSC_TYPE_WAV / _WTB / _PWM) and,
 *             when it changed, clamps bWave to the new type's list and drops
 *             Osc 2's bind; buf leaves bOscType, so leaving buf restores
 *             Osc 2's wave.
 * Why:        kat_b_type is tagged IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC2, so
 *             the platform's typ writer (InstrumentManager.c) calls this and
 *             then refreshes the wave rows' modulation ranges
 *             (instrumentManager_waveDomainChanged()), as for osc1_type. The
 *             special writer replaces the generic store, so the byte is
 *             written here.
 * Accessors:  instrumentManager_writeSpecialRuntime() (IM_SPECIAL_OSC_TYPE).
 */
```

### 1F. The descriptor table and pages (`KatranParameters.c/.h`)

#### C1.33 — MODIFY `KatranParameters.c:7–27` (file header)
**What:** "58 rows (S105 P1: 57 Katran-own indices reorganised, §A1.4); 58..59
free; 60..63 reserved". The "Not offered" list drops nothing.
**Comment block** (added paragraph):
```c
 * S105 P1:    the reorganisation of S105_P1_KATRAN_FURTHER_TWEAKS.md §A1:
 *             every row's category / long / short agree (the short name
 *             abbreviates the long name); seven chaos rows became four
 *             (A3); F1 + F2 became the band (A4: cut wid ofs); src became
 *             a type per oscillator (A8: kat_b_type, kat_b_wave); the ring
 *             rows (dpt r/f sdb) and the output drive were added. Freed
 *             indices were reused in place and kat_rotate moved 57 -> 37,
 *             so the table stays one row per index with no hole. No
 *             migration (the standing rule): an old Scene's byte at a reused
 *             index loads into the new row.
```

#### C1.34 — MODIFY `KatranParameters.c:82–155` (the index enum)
**What:** the enum in index order:

| Idx | Enum name | | Idx | Enum name |
|---:|---|---|---:|---|
| 0–5 | unchanged (`OSC1_PITCH_COARSE` .. `OSC1_TYPE`) | | 34 | `KAT_PARAM_FLT_CUT` |
| 6 | `KAT_PARAM_B_TYPE` (was `SOURCE`) | | 35 | `KAT_PARAM_FLT_WIDTH` |
| 7–9 | unchanged (`DAMP`, `RUN`, `KICK`) | | 36 | `KAT_PARAM_FLT_OFFSET` |
| 10 | `KAT_PARAM_CHAOS_CYCLE` | | 37 | `KAT_PARAM_ROTATE` (moved from 57) |
| 11 | `KAT_PARAM_CHAOS_SKEW` | | 38 | `KAT_PARAM_OUT_CUT` (unchanged) |
| 12 | `KAT_PARAM_CHAOS_IMOD` | | 39–56 | unchanged (`INSTRUMENT_VOL` .. `LFO_TARGET_PARAM_2`) |
| 13 | `KAT_PARAM_RING_MODE` | | 57 | `KAT_PARAM_B_WAVE` |
| 14 | `KAT_PARAM_RING_SIDEBAND` | | | `KAT_PARAM_COUNT` (58) |
| 15 | `KAT_PARAM_OUT_DRIVE` | | | |
| 16–23 | unchanged (`CHAOS` .. `B_POS`) | | | |
| 24 | `KAT_PARAM_RING_DEPTH` (was `B_LEVEL`) | | | |
| 25–33 | unchanged | | | |

**Comment block** (replaces `:82–94`):
```c
/*
 * Katran-local descriptor indices (S103 P1; S105 P1 reorganised).
 *
 * What:       the order of katran_param_descriptors[]: Scene/Kit storage,
 *             automation IDs, LFO/velocity tokens and NRPN numbers (slot x 64
 *             + index) use these numbers, and the file writer emits keys in
 *             this order.
 * Why:        S105 P1 (§A1.4): the rows A3, A4 and A8 removed left indices
 *             6, 10..15, 34..37; the new rows took them in place, kat_rotate
 *             moved from 57 to 37 and kat_b_wave took 57, so the table stays
 *             contiguous at 58 rows. 58..59 are free (Part B's two rows);
 *             60..63 stay reserved for the insert effect.
 */
```

#### C1.35 — MODIFY `KatranParameters.c:176–264` (the rows)
**What:** the table in index order; the macros are the file's own. Rows not
listed keep their line unchanged.

```c
    ROW_SPECIAL("osc1_pitch_coarse", CAT_OSC1, LONG_COARSE, SHORT_COARSE, ...),   /* 0 */
    ROW_SPECIAL("osc1_pitch_fine", CAT_OSC1, LONG_FINE, SHORT_FINE, ...),         /* 1 */
    ROW("osc1_position", CAT_OSC1, LONG_POSITION, SHORT_POS, ...),                /* 2 */
    ROW("kat_spread_a", CAT_OSC1, LONG_DETUNE, SHORT_DTN, ...),                   /* 3 */
    ROW_MENU("osc1_wave", CAT_OSC1, LONG_WAVE, SHORT_WAVE, MENU_WAVEFORM, MOD_WAVE, osc.waveform, TYPE_UINT8),
    ROW_SELECT_ONOFF_SPECIAL("osc1_type", CAT_OSC1, LONG_TYPE, SHORT_FIL_TYPE, ONOFF_TAG_KAT_OSC_TYPE, osc.oscType, TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1),
    ROW_SELECT_ONOFF_SPECIAL("kat_b_type", CAT_OSC2, LONG_TYPE, SHORT_FIL_TYPE, ONOFF_TAG_KAT_OSC_TYPE, bType, TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC2),   /* 6 */
    ROW("kat_damp", CAT_BUFFER, LONG_DAMPING, SHORT_DMP, DTYPE_0B127, MOD_0_127, damp_, TYPE_UINT8),
    ROW("kat_run", CAT_VOICE, LONG_RUN, SHORT_PLAY_MODE, ...),                    /* 8, unchanged */
    ROW("kat_kick", CAT_TRIGGER, LONG_KICK, SHORT_KIK, ...),                      /* 9, unchanged */
    ROW("kat_chaos_cycle", CAT_CHAOS, LONG_CYCLE, SHORT_CYC, DTYPE_0B127, MOD_0_127, chaosCycle, TYPE_UINT8),
    ROW("kat_chaos_skew", CAT_CHAOS, LONG_SKEW, SHORT_SKW, DTYPE_PM63, MOD_PM63, chaosSkew, TYPE_UINT8),
    ROW("kat_chaos_imod", CAT_CHAOS, LONG_INTERMOD, SHORT_IMD, DTYPE_0B127, MOD_0_127, chaosImod, TYPE_UINT8),
    ROW_SELECT_ONOFF("kat_ring_mode", CAT_RING_FM, LONG_MODE, SHORT_RFM, ONOFF_TAG_KAT_RING_MODE, ringMode, TYPE_UINT8),   /* 13 */
    ROW("kat_ring_sideband", CAT_RING_FM, LONG_SIDEBAND, SHORT_SDB, DTYPE_0B127, MOD_0_127, ringSideband, TYPE_UINT8),
    ROW("kat_out_drive", CAT_VOICE, LONG_DRIVE_WORD, SHORT_DRIVE, DTYPE_0B127, MOD_0_127, outDrive, TYPE_UINT8),         /* 15 */
    ROW("kat_chaos", CAT_CHAOS, LONG_DEPTH, SHORT_CHS, DTYPE_0B127, MOD_0_127, chaos, TYPE_UINT8),                      /* 16 */
    ROW("kat_plfo_rate", CAT_MOD_OSC, LONG_RATE, SHORT_RTE, DTYPE_0B127, MOD_0_127, plfoRate, TYPE_UINT8),             /* 17; dtype tag in C3.06 */
    ROW("kat_plfo_mod", CAT_MOD_OSC, LONG_SELFMOD, SHORT_SLF, ...),
    ROW("kat_plfo_level", CAT_MOD_OSC, LONG_DEPTH, SHORT_FXD, ...),
    ROW("kat_vca", CAT_VCA, LONG_BALANCE, SHORT_BAL, DTYPE_PM63, MOD_PM63, vca, TYPE_UINT8),
    ROW("kat_b_coarse", CAT_OSC2, LONG_COARSE, SHORT_COARSE, ...),               /* 21 */
    ROW("kat_b_fine", CAT_OSC2, LONG_FINE, SHORT_FINE, ...),
    ROW("kat_b_pos", CAT_OSC2, LONG_POSITION, SHORT_POS, ...),
    ROW("kat_ring_depth", CAT_RING_FM, LONG_DEPTH, SHORT_FXD, DTYPE_0B127, MOD_0_127, ringDepth, TYPE_UINT8),           /* 24 */
    ROW("kat_level_a", CAT_EXCITER, LONG_LEVEL, SHORT_EXC, ...),
    ROW("kat_noise", CAT_EXCITER, LONG_NOISEMIX, SHORT_NOISE, ...),
    ROW("kat_sync", CAT_OSC1, LONG_SYNCTHR, SHORT_SYNC, ...),
    ROW("kat_drive", CAT_TORCIDO, LONG_DRIVE_WORD, SHORT_DRIVE, ...),             /* 28 */
    ROW("kat_wet", CAT_TORCIDO, LONG_BLEND, SHORT_BLN, ...),
    ROW("kat_time", CAT_DELAY, LONG_TIME, SHORT_MICROTIMING, ...),                /* unchanged */
    ROW("kat_feedback", CAT_DELAY, LONG_FEEDBACK, SHORT_FBK, ...),                /* unchanged */
    ROW("kat_drift", CAT_DELAY, LONG_WOWFLUTR, SHORT_WOW, ...),
    ROW("kat_track", CAT_DELAY, LONG_TRACK, SHORT_DTK, ...),                      /* 33, unchanged */
    ROW("kat_flt_cut", CAT_FILTER, LONG_CUTOFF, SHORT_CUT, DTYPE_PM63, MOD_PM63, fltCut, TYPE_UINT8),                   /* 34 */
    ROW("kat_flt_width", CAT_FILTER, LONG_WIDTH, SHORT_WID, DTYPE_0B127, MOD_0_127, fltWidth, TYPE_UINT8),
    ROW("kat_flt_offset", CAT_FILTER, LONG_OFFSET, SHORT_OFFSET, DTYPE_PM63, MOD_PM63, fltOffset, TYPE_UINT8),
    ROW("kat_rotate", CAT_STEREO, LONG_ROTATION, SHORT_ROTATION, DTYPE_0B127, MOD_0_127, rotate, TYPE_UINT8),           /* 37 */
    ROW("kat_out_cut", CAT_VOICE, LONG_TONE, SHORT_TON, DTYPE_PM63, MOD_PM63, outCut, TYPE_UINT8),                      /* 38 */
    ROW("instrument_vol", ...), ROW("instrument_pan", ...),                       /* 39, 40 unchanged */
    ROW("kat_spread", CAT_DELAY, LONG_SPREAD, SHORT_SPR, ...),                    /* 41 */
    ROW("kat_width", CAT_STEREO, LONG_WIDTH, SHORT_WID, ...),                     /* 42 */
    /* 43..56: the LFO and velocity rows, unchanged */
    ROW_MENU("kat_b_wave", CAT_OSC2, LONG_WAVE, SHORT_WAVE, MENU_WAVEFORM, MOD_WAVE, bWave, TYPE_UINT8),               /* 57 */
```
- **The checker needs one rule:** `kat_b_type` is a `kat_*` key with a
  special tag (C1.45).
- **Comments to replace:**
  - `:193–204` (the S103 P3 Tier B block) becomes the S105 P1 label note
    (below);
  - the `kat_width` comment (`:235–237`) stays;
  - the `kat_rotate` comment (`:254–263`) is shortened to "the stereo
    rotation (S103 P4); index 37 since S105 P1 (it was appended at 57)".

**Comment block** (replaces `:193–204`):
```c
    /*
     * S105 P1 (A1): every row is named Category + Long + Short, and the short
     * name abbreviates the long name (S105_P1_KATRAN_FURTHER_TWEAKS.md §A1.1).
     * Categories are the diagram's blocks: Osc 1, Osc 2, Buffer, Exciter,
     * Chaos, Mod Osc, VCA, Ring/FM, Torcido, Delay, Filter, Stereo, Voice.
     * Three shorts repeat, each for the same word on a different page: drv
     * (Torcido Drive, Voice Drive), wid (Stereo Width, Filter Width), dpt
     * (Mod Osc Depth, Ring/FM Depth); the single-parameter view shows the
     * category. S103 P3 Tier B's reuses of an unrelated word (frq for Rate,
     * dpt for Level, lvA / lvB) are undone.
     */
```

#### C1.36 — MODIFY `KatranParameters.c:276–330` (the pages)
**What:** the comment table and `katran_menu_pages[]`:
```
 *               0 osc     coa fin pos dtn | typ wav snc dmp     (Osc 1; S105 P1)
 *               1 aeg     run kik exc noi | chs cyc skw imd     (S105 P1 A3)
 *               2 mod     dst amt rot wid | rte slf dpt bal     (S105 P1)
 *               3 fm      coa fin pos typ | wav dpt r/f sdb     (Osc 2; S105 P1)
 *               4 click   drv bln tim fbk | wow dtk spr --- | (fx dpt rte mix)
 *               5 filter  cut wid ofs ---                       (the band, A4)
 *               6 LFO     the platform layout, unchanged
 *               7 mix     vol pan ton drv                       (S105 P1 A5)
```
The page arrays list the matching `KAT_PARAM_*` in cell order:
- page 0: `..., KAT_PARAM_OSC1_TYPE, KAT_PARAM_WAVE, KAT_PARAM_SYNC, KAT_PARAM_DAMP`;
- page 3: `KAT_PARAM_B_COARSE, KAT_PARAM_B_FINE, KAT_PARAM_B_POS, KAT_PARAM_B_TYPE, KAT_PARAM_B_WAVE, KAT_PARAM_RING_DEPTH, KAT_PARAM_RING_MODE, KAT_PARAM_RING_SIDEBAND`;
- the others as the comment.

**Comment block** (the Why of the layout):
```c
 * Why:        S105 P1 (§A1.2): each page is one block of the diagram. osc
 *             is Osc 1 alone (its type, wave and the sync threshold that
 *             resets it); fm is Osc 2 and what it does to the cores; aeg
 *             holds what feeds and moves the loop (run, kick, the exciter
 *             level and noise the kick boosts, the four chaos rows); mod the
 *             patch LFO, the VCA balance and the stereo pair; click the loop
 *             (Torcido, the delay, spr); filter the band; mix vol pan ton
 *             drv, as Alkaline and Colloid. Click's screen 3 stays empty for
 *             the insert effect.
```

#### C1.37 — MODIFY `katranParameters_cellInactive()` (`KatranParameters.c:338–357`)
**What:**
```c
    case KAT_PARAM_OSC1_POSITION:   /* wav and not buf: free-running */
        return (uint8_t)(voice->osc.oscType == OSC_TYPE_WAV && !voice->aBuf);
    case KAT_PARAM_B_POS:
        return (uint8_t)(voice->bOscType == OSC_TYPE_WAV && voice->bType != KAT_TYPE_BUF);
    case KAT_PARAM_WAVE:            /* buf reads no table */
        return voice->aBuf;
    case KAT_PARAM_B_WAVE:
        return (uint8_t)(voice->bType == KAT_TYPE_BUF);
    case KAT_PARAM_DAMP:            /* nothing reads the arena */
        return (uint8_t)(!voice->aBuf && voice->bType != KAT_TYPE_BUF);
```
**Comment block:** the function's header comment is
updated (`KatranParameters.h:47–58`): "1 for `dmp` while neither oscillator is buf; for each `pos` under
wav when its oscillator is not buf; for each `wav` while its oscillator is
buf (S105 P1 A8)".

#### C1.38 — MODIFY `KatranParameters.h:1–35`
**What:** "the 58 descriptor rows (S105 P1: reorganised, §A1.4)";
`KATRAN_PARAM_DESCRIPTOR_COUNT 58u` stays; the line-10 split reads "21
platform rows + 37 Katran rows".

### 1G. The instrument manager (`InstrumentManager.c/.h`)

#### C1.39 — ADD at `InstrumentManager.c:1310` (`instrumentManager_waveDomainMax()`, inside the `_wave` block, before the `osc1_wave` test)
```c
        /*
         * S105 P1 A8: Katran's Osc 2 wave (kat_b_wave) lists its own type's
         * waves: Saw Rec Tri Sin under wav (COL_WAVE_COUNT, the Colloid's
         * band-limited rows, as osc1_wave), the platform's three under pwm,
         * the installed tables under wtb. Without this branch the key falls
         * to the modulator list below (osc_userListCount()), the wrong range
         * for the LFO / velocity domain, the menu clamp and automation.
         */
        if (strcmp(descriptor->file_key, "kat_b_wave") == 0 &&
            slot < INSTRUMENT_SLOT_COUNT &&
            instrumentManager_slotType(slot) == INSTRUMENT_TYPE_KAT) {
            const KatranVoice *kat = instrumentManager_katranRuntime(slot);
            const uint8_t t = kat ? kat->bOscType : (uint8_t)OSC_TYPE_WAV;
            uint8_t count;

            if (t == OSC_TYPE_WAV)
                return (uint8_t)(COL_WAVE_COUNT - 1u);
            count = osc_userCount(t);
            return count ? (uint8_t)(count - 1u) : 0u;
        }
```

#### C1.40 — MODIFY `InstrumentManager.c:5512–5555` (the `IM_SPECIAL_OSC_TYPE` writer)
**What:** a Katran branch goes **before** `instrumentManager_oscBySelector()`.
That lookup returns NULL for a Katran OSC2 (`:3745–3749`). The S103 P1
branch at `:5543–5548` is removed.
```c
        /*
         * S103 P1 / S105 P1 A8: a Katran's typ writers. osc1_type (OSC1) ->
         * Katran_setOscType() (wav / wtb / pwm, and buf: Osc 1 reads the
         * arena); kat_b_type (OSC2) -> Katran_setOscBType(). Handled before
         * the OscInfo lookup: Osc 2 has no OscInfo (the lookup returns NULL
         * for OSC2), and both writers work on the voice. The wave rows'
         * ranges are refreshed as for every type.
         */
        if (instrumentManager_slotType(slot) == INSTRUMENT_TYPE_KAT) {
            KatranVoice *kat = instrumentManager_katranRuntime(slot);

            if (!kat) return 0u;
            if ((special & IM_SPECIAL_OSC_MASK) == IM_SPECIAL_OSC2)
                Katran_setOscBType(kat, byteValue);
            else
                Katran_setOscType(kat, byteValue);
            instrumentManager_waveDomainChanged(slot);
            return 1u;
        }
```
The writer's block comment (`:5513–5528`) adds "S105 P1: the Katran's two
type rows (OSC1, OSC2)" to its What.

#### C1.41 — ADD `instrumentManager_katranOsc2Type()` (`InstrumentManager.c` after `:1393`; prototype in `InstrumentManager.h` after `:992`)
```c
/*
 * A Katran slot's Osc 2 table type (S105 P1 A8).
 *
 * What:       OSC_TYPE_WAV / _WTB / _PWM of the slot's Katran Osc 2
 *             (bOscType), or OSC_TYPE_WAV for any other slot or type.
 * Why:        Menu names kat_b_wave's values by Osc 2's type
 *             (menu_formatWaveCell()), as it names osc1_wave's by
 *             instrumentManager_osc1Type(), without owning the runtime.
 * Inputs:     slot.  Outputs: type.
 * Accessors:  menu_formatWaveCell().
 */
uint8_t instrumentManager_katranOsc2Type(uint8_t slot);
```

#### C1.42 — MODIFY `InstrumentManager.c:3219–3221` and `:325`
**What:** the `cellInactive` comment reads "dmp while neither oscillator is
buf; each pos and wav by its own oscillator's type (S105 P1)". The `:325`
size comment reads "≈ 1,114 B after S105 P1 Part A (est.)".

### 1H. The menu (`menu.c`)

#### C1.43 — MODIFY `menu_formatWaveCell()` (`menu.c:12428–12560`)
**What:** a Katran Osc 2 wave cell is named by Osc 2's type.
```c
    /* S105 P1 A8: Katran's kat_b_wave is named like osc1_wave, by Osc 2's
     * own type (instrumentManager_katranOsc2Type()); isOscRow covers both */
    const uint8_t isKatB = (uint8_t)(isInstrument &&
        strcmp(cell->descriptor->file_key, "kat_b_wave") == 0);
    const uint8_t isOscRow = (uint8_t)(isOsc1 || isKatB);
    const uint8_t type = isOsc1 ? instrumentManager_osc1Type(cell->slot)
                       : (isKatB ? instrumentManager_katranOsc2Type(cell->slot)
                                 : (uint8_t)OSC_TYPE_WAV);
```
- The Katran wav branch (`:12504–12511`), the shared pwm branch
  (`:12541–12545`) and the catalogue branch (`:12546`) test `isOscRow`
  instead of `isOsc1`.
- The Alkaline and Colloid branches keep `isOsc1`: they also test the
  engine.

#### C1.44 — MODIFY the typ-switch clamp (`menu.c:5670–5730`)
**What:** the wave row to clamp follows the type row's oscillator.
```c
            /* S105 P1 A8: a Katran's kat_b_type (OSC2) clamps kat_b_wave;
             * every other typ row clamps osc1_wave */
            const char *waveKey =
                ((cell->descriptor->runtime.special & IM_SPECIAL_OSC_MASK) == IM_SPECIAL_OSC2 &&
                 owner && owner->type == INSTRUMENT_TYPE_KAT) ? "kat_b_wave" : "osc1_wave";
```
`strcmp(d->file_key, "osc1_wave")` at `:5703` becomes `strcmp(d->file_key,
waveKey)`. The block comment (`:5671–5688`) adds: "S105 P1: Katran's Osc 2 type
clamps its own wave row".

### 1I. Tools and the host tests

#### C1.45 — MODIFY `tools/dsp_test/check_special_tags.py:49–52` and `expected()` (`:59`)
**What:** in `expected()`, before the `osc` prefix tests:
```python
    # S105 P1 A8: the Katran's Osc 2 type has the platform typ writer
    # (IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC2: Katran_setOscBType(), then
    # the wave-domain refresh); every other kat_* key stays NONE.
    if t == 'KAT' and key == 'kat_b_type':
        return {'IM_SPECIAL_OSC_TYPE', 'IM_SPECIAL_OSC2'}
```
The `'KAT'` comment adds "kat_b_type is OSC_TYPE | OSC2 (S105 P1)".

#### C1.46 — MODIFY `tools/katran/make_init_kat.py:12–22` (docstring) and `:53–78` (`INIT`)
**What:**
- `INIT` loses the 11 removed keys and gains the new ones, with the bytes of
  C1.28: `kat_b_type 2`, `kat_b_wave 2`, `kat_chaos 40`,
  `kat_chaos_cycle 37`, `kat_chaos_skew 63`, `kat_chaos_imod 20`,
  `kat_ring_mode 0`, `kat_ring_sideband 0`, `kat_out_drive 0`,
  `kat_ring_depth 127`, `kat_flt_cut 26`, `kat_flt_width 127`,
  `kat_flt_offset 63`.
- The docstring's value list follows.
- **KT-LBL is added here** (S105 P1, §A1.1), run by `--check`. The check
  parses each row's `CAT_*`, `LONG_*` and `SHORT_*` ids and resolves them
  through `menu.h` and `MenuLabels.c` (the method of
  `S103_P3_SHORT_NAMES_MERGE.md`). It fails if:
  - a short is over 3, a category over 8 or a long over 9 characters;
  - two rows on one page share a short;
  - two rows share category + long.

**Comment block** (the KT-LBL function's docstring):
```python
"""S105 P1 (KT-LBL): the Katran's labels obey §A1.1.

What:   resolves every row's label ids to their strings (menu.h enums,
        MenuLabels.c tables, index-aligned) and the page layout
        (katran_menu_pages[]); fails on a width over the LCD fields (3 / 8
        / 9), a short name repeated on one page, or two rows with the same
        category and long name (the long view could not tell them apart).
Why:    the reorganisation's rule must not erode with later rows.
"""
```

#### C1.47 — REGENERATE `SD_CARD/Instrument/Katran/init.kat`
`python3 tools/katran/make_init_kat.py`; `--check` passes.

#### C1.48 — MODIFY `tools/dsp_test/modules/katran.c`
| Line | Change |
|---|---|
| `:1–58` | the header's law list adds KT-CE (extended), KT-BAND and KT-TYPE; KA-F3's text stays until C2.10 |
| `:120–131` | groups `G_BAND`, `G_TYPE` (KT-CE uses `G_CE`) |
| `:331`, `:350`, `:365`, `:389`, `:563` | `V.source = KAT_SRC_*` becomes `V.aBuf` / `V.bType` (A → `aBuf 1`; B → `bType KAT_TYPE_BUF`; AB both; OFF → `aBuf 0`, `bType KAT_TYPE_PWM`) |
| `:570` | `V.loopRes = 127u;` → `V.fltWidth = 0u;` (the narrowest, most resonant band) |
| `:798–806`, `:817–853` | **KA-FOLD's reference:** the double-precision core uses the band recurrence (C1.26) with S's `band[c]` (sH, sL, gH, gL, dgH, dgL, r, mk) in place of the SVF and F2 |
| `:942` | `V.ce1Depth = 0u;` → `V.chaos = 0u;` |
| new | **KT-CE (extended):** (a) `chs` 0: two voices that differ only in `ce[1].level` give identical `band[c].hSt` / `lSt` and `mx[]` targets; (b) `chs` 127: the slow term reaches the corners (`hSt` moves by 48 (L − 0.5) after the slew settles, 0.1 semitone); (c) `imd` 0: after `kat_ceStart()` both envelopes' total = peak/up + peak/down blocks, and slow = N × fast exactly; (d) `skw` +63: slow rise share ≥ 0.97, fast ≤ 0.03 (the 3-block rule aside); (e) `imd` 127: `skPh` advances by 0.25 a slow cycle |
| new | **KT-BAND:** from `kat_bandControl()`'s coefficients, a host recurrence identical to C1.26 measures the gain at 30 Hz, 1 kHz and 12 kHz (sine, peak after settling): `cut` 63 / `wid` 127 flat within 1 dB; `cut` 127 at 1 kHz ≤ −40 dB; `cut` 0 at 1 kHz ≤ −40 dB; `wid` 0 at the centre ≥ +15 dB above `wid` 64's; `ofs` 127: core A's `hSt` − core B's = 48 semitones (minus the core B ceiling clamp); a 4,000-block run with `fbk` 127, `drv` 127 and every `cut` / `wid` / `ofs` extreme stays finite and inside int16 |
| new | **KT-TYPE:** for the four combinations (`aBuf` 0/1 × `bType` pwm/buf), `kat_oscControl()`'s `rowANext[]` / `rowBNext` point into the arena exactly for the buf oscillators and to stock rows for the others; with `osc1` pwm Rec and Osc 2 wav Sin, `rowBNext == sine_table` and Osc 1's row is a `sawTable` octave row (the PWM D form); `Katran_setOscType(3)` then `(2)` restores `osc.oscType` and `osc.waveform`; `Katran_setOscBType()` clamps `bWave` (pwm → wav keeps 0..3, wtb with 0 tables clamps to 0) |

**Comment block** for KT-BAND (above its test function):
```c
/*
 * KT-BAND (S105 P1 A4): the band filter's corners, resonance and bounds.
 *
 * What:       kat_bandControl() is run to its targets for each setting, then
 *             a double recurrence of the loop's own form (xb = mk x + r sL;
 *             sH += gH (xb - sH); hp = xb - sH; sL += gL (hp - sL)) measures
 *             the steady sine gain at three frequencies. KA-FOLD checks that
 *             kat_coreBody() runs that recurrence, so together they check
 *             the loop's filter.
 * Why:        the laws of §A4.2–A4.4: full band at cut 0 / wid 127; raising
 *             the cutoff cuts lows, lowering it cuts highs; a narrowing band
 *             resonates; ofs splits the cores; nothing diverges.
 */
```

#### C1.49 — MODIFY `tools/dsp_test/katran_cpu.py:17–23` (docstring) and `:111–117`
**What:** `('kat_svfControl', 2)` becomes `('kat_bandControl', 2)`; the
docstring line becomes "kat_bandControl x2 (S105 P1: the band, F1 + F2;
F3 inline)".

#### C1.50 — ADD to `tools/dsp_test/Makefile:1335–1405` (target `katran`)
**What:** a break build `katran_break_band`, after `katran_break_rot`:
```make
	mkdir -p build/break_kat_band
	sed 's/const float xb = mk \* xin + r \* sL;/const float xb = mk * xin - r * sL;/' \
	  $(GEN)/KatranVoice.host.c > build/break_kat_band/KatranVoice.host.c
	@! grep -q 'mk \* xin + r \* sL;' build/break_kat_band/KatranVoice.host.c || \
	  { echo "katran_break_band: sed did not apply"; exit 1; }
	$(HOSTCC) -Ibuild/break_kat_band $(HOSTFLAGS) $(OSC_HOST_INC) -DWAVETABLE_ROW_POINTS=1024u \
	  modules/katran.c -o build/katran_break_band -lm
	@if ./build/katran_break_band >/dev/null 2>&1; then \
	  echo "katran_break_band: FAILED (an inverted resonance not caught)"; exit 1; \
	else echo "katran_break_band: caught (law KA-FOLD fails as expected)"; fi
```
**Comment** (in the target's header block, `:1335–1351`): "S105 P1:
katran_break_band inverts the band's resonance feedback; KA-FOLD's band
reference must catch it."

### 1J. Gate 1
1. `make -C tools/dsp_test katran katran_tables special_tags`: every law
   passes, every break build is caught, `--check` passes.
2. `make all && make img`; `link_budget.py` (flash); the size assert holds.
   Record the size: `arm-none-eabi-gdb -batch -ex 'p sizeof(KatranVoice)'
   build/main.elf` → `tools/dsp_test/baselines/s105/katran_size.txt`.
3. `make -C tools/dsp_test katran_cpu_check FW_ELF=…` →
   `tools/dsp_test/baselines/s105/katran_cpu_s1.txt`.
   - **Expected:** ≈ 356–359 (366.6 − 8 .. −11, plus ≈ +0.6 for the chaos law
     and per-block A8).
   - **The ladder (plan §C):** a saving below 5 puts the drive on the ladder.
     The fallback, the 2-pole LP corner (plan §A4.7), is decided then with
     you.
4. The mixer hot-loop and inlined-set diff (`elf_hotloops.py`, the S089 P0
   F3 rule) against the pre-S105 image.

---

## 2. Step 2 — the output stage (A5)

#### C2.01 — MODIFY `gen_kat_tables.py` (ADD two laws), `KatranTables.h`, REGENERATE `KatranTables.c`
```python
def out_drive_gain(r):
    """S105 P1 A5: the output drive's input gain, 2^(4 r / 127) (x1 .. x16)."""
    return 2.0 ** (4.0 * r / 127.0)


def out_drive_makeup(r):
    """S105 P1 A5: its make-up, gain^-0.6 (by ear: louder with drive, not x16)."""
    return out_drive_gain(r) ** -0.6
```
Emitted as `katOutDriveGain[128]` and `katOutDriveMakeup[128]`, declared in
`KatranTables.h` with the line: "katOutDriveGain / katOutDriveMakeup the
output drive: ×1 .. ×16 in, gain^−0.6 out (S105 P1 A5)". Flash +1,024 B.

#### C2.02 — MODIFY `KatranVoice.h:121–124` (`KAT_OUT_NORM`) and the struct
**What:**
- `KAT_OUT_NORM` is set from the C2.12 measurement;
- ADD `float drvA, drvB;` after `cB1Step` (`:235`);
- the `f3` member comment (`:211`): "ic1 L's state, ic2 R's (S105 P1: F3
  after the matrix)".

**Comment block** for `KAT_OUT_NORM`:
```c
/* The output normalisation (P§5; S105 P1 A5: raised from 0.5 to the value
 * measured in tools/dsp_test/baselines/s105/level.txt, which puts init.kat's
 * RMS on init.col's within 1 dB; the drive's cubic catches the peaks). */
```
**Comment block** for `drvA`, `drvB`:
```c
	/* S105 P1 A5: the output drive's cubic for this block, y = c (drvA -
	 * drvB c^2) with c the core output clamped to +-1.5 x 32768: drvA = the
	 * make-up, drvB = make-up x (4/27) / 32768^2 (int16 units in and out).
	 * Read by the drive wrappers only (kat_coreRenderAD / BD). */
```

#### C2.03 — MODIFY `kat_outGains()` (`KatranVoice.c:1236–1262`)
**What:** the ring crossfade per core and the drive gain.
```c
static void kat_outGains(const KatranVoice *v, const float g1, const float g2,
                         const float bA, const float bB, const float G, float o[4])
{
	const float dS = KAT_OUT_SCALE * KAT_INV_32768;

	o[0] = (1.0f - bA + g2) * KAT_OUT_SCALE * G;
	o[1] = bA * dS * G;
	o[2] = (1.0f - bB) * (1.0f + g1) * KAT_OUT_SCALE * G;
	o[3] = bB * (1.0f + g1) * dS * G;
}
```
**Comment block** (replaces `:1236–1251`):
```c
/*
 * The two cores' output gains for one set of targets (S103 P4; S105 P1 A5).
 *
 * What:       the ring as a crossfade per core, b = the core's ring depth
 *             (ringDepth / 127, or 0 for a core in FM, A6), and the VCA
 *             gains g1 (core B, AMP1) and g2 (core A, AMP2):
 *               cA0 = (1 - bA + g2) 32767 G,  cA1 = bA 32767 / 32768 G;
 *               cB0 = (1 - bB)(1 + g1) 32767 G, cB1 = bB (1 + g1) 32767 / 32768 G;
 *             G = the output drive's input gain (katOutDriveGain[drv]; 1.0
 *             at drv 0, so the no-drive path is unchanged).
 * Why:        S105 P1 finding F-3: the S103 P1 law switched the ring on
 *             (rk = 1) for any lvB above 0, so lvB 1 removed the dry signal
 *             and left a 1/127 ring: a level cliff, and core B near silence.
 *             The crossfade keeps the ends (b 0 = today's lvB 0, b 1 =
 *             today's lvB 127, the same gains) and has no cliff between.
 *             Folding G here keeps the per-sample cost of the drive to the
 *             clip itself (kat_coreBody()).
 */
```

#### C2.04 — MODIFY `kat_coreBody()` (`KatranVoice.c:933`, `:998–1005`) and ADD two wrappers (`:1023–1034`)
**What:**
- `kat_coreBody()` takes a constant `drive` (0 or 1).
- With `drive`, after `o = t1 * (o0 + o1 * Bv);`:
  ```c
  		if (drive) {
  			const float c = fminf(fmaxf(o, -KAT_DRV_CLIP), KAT_DRV_CLIP);   /* +-1.5 x 32768 */

  			o = c * (dA - dB * c * c);
  		}
  ```
  (`dA = v->drvA`, `dB = v->drvB`, loaded once a block).
- ADD the wrappers `kat_coreRenderAD()` and `kat_coreRenderBD()`
  (`kat_coreBody(v, 0, out, 0, 1)` and `(v, 1, right, 1, 1)`). The existing
  two pass `drive` 0.

**Comment block** (in the body's What):
```c
 *             S105 P1 A5 (drive, the D wrappers only): o is soft-clipped
 *             before its store, c = clamp(o, +-1.5 x 32768), o = c (drvA -
 *             drvB c^2): the cubic (4/27) with the make-up, in int16 units;
 *             the input gain is already in o0 / o1 (kat_outGains()). This is
 *             the drive ahead of the tone filter (F3, after the matrix). In
 *             the core loop it costs 5 a sample on core A and 5 per
 *             half-rate sample on core B (2.5 an output sample), against 10
 *             for a clip on L and R after the matrix. At drv 0 the plain
 *             wrappers run, so drv 0 is bit-identical to no drive.
```
**ADD constant** (`KatranVoice.c` constants block):
`#define KAT_DRV_CLIP (1.5f * 32768.0f) /* S105 P1 A5: the drive's cubic knee, in int16 units */`

#### C2.05 — MODIFY `kat_outRender()` (`KatranVoice.c:1149–1201`)
**What:** the matrix first, then F3 on L and R.
```c
		const float l = m0 * inA + m1 * inB;
		const float rr = m2 * inA + m3 * inB;
		float yl, yr;

		zl += a * (l - zl);
		zr += a * (rr - zr);
		yl = zl + hp * (l - 2.0f * zl);
		yr = zr + hp * (rr - 2.0f * zr);
		outL[n] = kat_sat16f(yl);
		v->right[n] = kat_sat16f(yr);
```
**Comment block** (replaces `:1149–1164`):
```c
/*
 * The output stage, in place on buf and right (S103 P1, P§4 L5; S103 P4:
 * the stereo matrix; S105 P1 A5: F3 after it).
 *
 * What:       per sample: a = core A (buf), b = core B (right), as
 *             int16-valued floats; L = m0 a + m1 b, R = m2 a + m3 b with the
 *             four gains ramped (mx[], kat_stereoControl()); then F3 on L and
 *             R with the shared ramped coefficient (f3.ic1 L's state, ic2
 *             R's): the one-pole LP / HP tilt (ton, PM63; 63 = a 1.0, an
 *             exact pass); SSAT16 stores, L into buf, R into right.
 * Why:        S105 P1 A5 (your point P4): F3 is the last stage before the
 *             mixer and stays bipolar. Moving it after the rotation costs
 *             nothing (the same two one-poles), and the drive (in the core
 *             loops) is ahead of it.
 */
```

#### C2.06 — MODIFY `Katran_calcVoiceAsync()` (`KatranVoice.c:1561–1569`)
**What:**
- `G = katOutDriveGain[outDrive]`; `drvA = katOutDriveMakeup[outDrive]`;
  `drvB = drvA * (4/27) / (32768 · 32768)`;
- `b = ringDepth / 127` for both cores (step 4 sets 0 for an FM core);
- `kat_outGains(v, g1T, g2T, bA, bB, G, og)`.

**Comment block:** one line added to the async's What: "the output drive's
gain (folded into the four gains) and cubic (drvA, drvB), S105 P1 A5".

#### C2.07 — MODIFY `Katran_calcVoiceSyncBlock()` (`:1643`, `:1655`)
**What:** `voice->outDrive ? kat_coreRenderAD(voice, buf) : kat_coreRenderA(voice, buf)`,
and the same for B.
**Comment block:** `/* S105 P1 A5: the drive wrappers while drv > 0 (equal-cost choice per block; the gate counts the dearer) */`

#### C2.08 — MODIFY `Katran_initVoice()` (`:1369–1376`)
**What:** `kat_outGains(voice, 0, 0, b, b, 1.0f, og)` with `b = ringDepth /
127`; `drvA = 1.0f; drvB = 4/27 / 32768²`.

#### C2.09 — MODIFY `tools/dsp_test/katran_cpu.py:52–54`, `:104–111`
**What:**
- `CORE` adds `kat_coreRenderAD` and `kat_coreRenderBD`;
- `per_sample` takes `max(longest(A), longest(AD))` and
  `max(B, BD) / 2.0`;
- the block-path loop adds the D wrappers' entry paths through the same
  max.

**Comment** (the docstring line): "S105 P1: core A and core B count the
dearer of the plain and drive wrappers."

#### C2.10 — MODIFY `tools/dsp_test/modules/katran.c`
| Line | Change |
|---|---|
| `:507–520` (KA-VCA) | `g1` read back as `target_of(cB1) / (b · s / 32768) − 1` with b = 127/127 (`init`'s ring depth), so the formula holds |
| `:537–552` (KA-F3) | unchanged (the coefficient law) |
| `:883–905` (KA-FOLD part 2) | the reference is the crossfade: r = t1 ((1 − b) + b B / 32768); `kat_outGains(&V, g1, g2, b, b, 1.0f, og)` |
| `:1014–1044` (KA-ROT output loop) | the expected output is F3(matrix): with `f3.a1 = 1`, `mL = 1` F3 passes, so the expectation stays L = m0 a + m1 b |
| new | **KT-RING:** `ringDepth` 0 gives cA1 = cB1 = 0 and cA0 / cB0 the dry gains; 127 gives cA0 = g2 · 32767 and cB0 = 0 (the S103 P5 lvB 127 gains); output RMS over `ringDepth` 0..127 in steps of 8 has no step above 6 dB between neighbours |
| new | **KT-OUT:** (a) `outCut` 63: `kat_outRender()` output = the matrix output exactly; (b) `outDrive` 0: `kat_coreRenderA()` output = the plain path (identity, the same wrapper); (c) `outDrive` 127 with a full-scale input: no core sample reaches ±32,767 (the cubic's ceiling ×makeup < 1); (d) the level print (C2.12) |

#### C2.11 — ADD to `tools/dsp_test/Makefile` (target `katran`)
No new break build: KT-RING's identity at the ends and KA-FOLD part 2 cover
the crossfade law.

#### C2.12 — MEASURE and set `KAT_OUT_NORM`
1. Add a `--level` mode to `modules/katran.c`: `init` values, a kick at
   velocity 100, 2,000 blocks; print the output RMS in dBFS (L and R).
2. Add the same 10-line print to `modules/colloid.c`: `init.col`, a note at
   velocity 100, 2,000 blocks.
3. Set `KAT_OUT_NORM` = 0.5 × 10^((RMS_col − RMS_kat) / 20).
4. Record both values and the result in
   `tools/dsp_test/baselines/s105/level.txt`.

### Gate 2
- `make -C tools/dsp_test katran`; `katran_cpu_check` →
  `baselines/s105/katran_cpu_s2.txt`. **Expected:** +7.5 on step 1.
- **If the count is over:** the plan's ladder step 3 (drive dropped, gain
  kept). Remove C2.04's wrappers and keep C2.03 with G = 1.

---

## 3. Step 3 — the synced rate (A2)

#### C3.01 — MODIFY `gen_kat_tables.py` (`plfo_hz` → the synced table), `KatranTables.h`, REGENERATE
```python
# S105 P1 A2: the patch LFO's step-synced periods in 96-PPQ ticks, shortest
# first: CrumpBit's 16 (gen_crumpbit_tables.py GRAIN_STOP_TICKS, /64 .. 1br)
# then 2, 4, 8, 16, 32 bars. Read backwards: row 127 = /64 (fastest).
PLFO_STOP_TICKS = [6, 8, 12, 16, 18, 24, 32, 36, 48, 64, 72, 96, 144, 192,
                   288, 384, 768, 1536, 3072, 6144, 12288]


def plfo_stop_rows():
    """The row of each stop, 127 - round(127 log2(t / 6) / log2(2048)),
    fastest first in the list's order (row 127 for /64 .. row 0 for 32b)."""


def plfo_inv_ticks():
    """1 / ticks per row: equal ratios between neighbouring stops (the
    delay_ticks_table() law), exact at every stop, read backwards."""
```
- Emitted as `katPlfoInvTicks[128]` (replacing `katPlfoHz[]`, 0 B net) and
  `katPlfoStopRow[21]` (uint8).
- `KatranTables.h` replaces its `:11` line: "katPlfoInvTicks[128] the
  patch LFO's period as 1 / (96-PPQ ticks), /64 at 127 .. 32 bars at 0, the
  21 stops exact (S105 P1 A2); katPlfoStopRow[21] the stops' rows".
- ADD `#define KAT_PLFO_STOPS 21u`.

#### C3.02 — ADD `seq_getElapsedTicks()` (`sequencer.c` after `seq_getBpm()`, `:2191`; `sequencer.h` after `:304`)
```c
/*
 * The master tick count (S105 P1 A2).
 *
 * What:       seq_elapsedPpqTicks: 96-PPQ ticks since the last start or
 *             pattern reset (0 while stopped). One aligned 32-bit load, atomic
 *             against TIM3, which writes it.
 * Why:        Katran's patch LFO is pulled onto the master clock's phase
 *             (KatranVoice.c kat_plfoStep()); the render reads the count once
 *             a block. Read-only: nothing in the sequencer changes.
 * Context:    any (render, foreground).
 */
uint32_t seq_getElapsedTicks(void)
{
	return seq_elapsedPpqTicks;
}
```

#### C3.03 — MODIFY `KatranVoice.h` (constants, struct)
**ADD** to the constants:
```c
/*
 * The patch LFO's clock pull (S105 P1 A2; by ear).
 *
 * KAT_PLFO_PULL     the pull per block toward the master clock's phase at
 *                   slf 0 (about 70 ms); x (1 - 0.9 slf') above.
 * KAT_PLFO_TPS_BPM  96-PPQ ticks per second per BPM: 96 / 60.
 */
#define KAT_PLFO_PULL       0.01f
#define KAT_PLFO_TPS_BPM    1.6f
```
**ADD** to the struct after `plfoJit` (`:241`):
`uint32_t tickE; uint16_t tickBlocks; /* S105 P1 A2: the last master tick seen, blocks since it changed */`

#### C3.04 — MODIFY `kat_plfoStep()` (`KatranVoice.c:398–434`)
**What:** the synced rate and the pull.
```c
	const uint16_t bpm0 = seq_getBpm();
	const float bpm = (float)(bpm0 ? bpm0 : 1u);
	const float inv = katPlfoInvTicks[v->plfoRate & 127u];
	float rate = bpm * KAT_PLFO_TPS_BPM * inv
	           * alk_exp2f(lfm * (1.5f * v->ringPrev + invCv) + v->plfoJit);
	float pull = 0.0f;

	if (v->wasRunning) {                     /* kat_runControl() ran first */
		const uint32_t e = seq_getElapsedTicks();
		double est, pc, err;

		if (e != v->tickE) { v->tickE = e; v->tickBlocks = 0u; }
		else if (v->tickBlocks < 0xFFFFu) v->tickBlocks++;
		est = (double)e + (double)v->tickBlocks
		    * ((double)OUTPUT_DMA_SIZE / (double)REAL_FS) * (double)bpm * 1.6;
		pc = est * (double)inv;
		pc -= floor(pc);
		err = pc - (double)v->lfoPh;
		err -= floor(err + 0.5);             /* (-0.5, +0.5] */
		pull = KAT_PLFO_PULL * (1.0f - 0.9f * lfm) * (float)err;
	}
	v->lfoPh += rate * ((float)OUTPUT_DMA_SIZE / REAL_FS) + pull;
	if (v->lfoPh < 0.0f)
		v->lfoPh += 1.0f;
	/* then the existing wrap, jitter draw, warp and sine read (:425–433) */
```
The local `inv` of today (the inverted cv, `:415`) is renamed `invCv`.

**Comment block** (replaces `:398–409`):
```c
/*
 * The patch LFO's block (S103 P1, D§5.7; S105 P1 A2: step-synced).
 *
 * What:       the synced rate bpm x 96 / 60 / ticks(rte) Hz (rte reads
 *             katPlfoInvTicks[] backwards: 127 = /64 .. 0 = 32 bars; the 21
 *             step / bar stops exact), times today's modulation
 *             2^(slf' (1.5 ringPrev + inv) + jitter). The phase advances by
 *             the rate x 32 / fs plus a pull toward the master clock's phase:
 *             frac(E / ticks), E the 96-PPQ tick count extrapolated between
 *             ticks from the tempo, computed in double; the error wrapped to
 *             (-0.5, 0.5] times KAT_PLFO_PULL (1 - 0.9 slf'). Transport
 *             stopped: no pull, the synced rate free-running. The shape:
 *             as before (the warp by slf' x inv, the sine read).
 * Why:        your point P1: rte is step-synced in general; slf (and chs's
 *             per-cycle jitter) let it drift away, and the pull, weaker as
 *             slf rises, brings it back. A sequencer reset moves E: the phase
 *             glides back, it never jumps.
 * Cost:       two calls, one double multiply and floor, about 35 a block.
 */
```

#### C3.05 — MODIFY `Katran_initVoice()` (`:1426`)
**What:** `voice->tickE = 0u; voice->tickBlocks = 0u;` beside `lfoPh`.

#### C3.06 — the `rte` display: `menu.h`, `KatranParameters.c/.h`, `menu.c`
- **ADD `menu.h` after `:521`:**
  ```c
  /*
   * A tagged DTYPE_0B127 (S105 P1 A2): Katran's Mod Osc Rate (rte).
   *
   * What:       a plain 0..127 row (every dtype switch masks the low
   *             nibble) whose 21 stop rows display the stop's name (/64 ..
   *             32b, katranParameters_rateName3()); other rows show their
   *             number.
   * Why:        the rate is step-synced (katPlfoInvTicks[]), so its
   *             musical stops must read as steps and bars.
   * Accessors:  KatranParameters.c (kat_plfo_rate); menu.c
   *             menu_formatDtype3().
   */
  #define DTYPE_TAG_KAT_RATE   2u
  #define DTYPE_0B127_KAT_RATE ((uint8_t)(DTYPE_0B127 | (DTYPE_TAG_KAT_RATE << 4)))
  ```
- **MODIFY** `KatranParameters.c` row 17: dtype `DTYPE_0B127_KAT_RATE`.
- **ADD** to `KatranParameters.c` (after the cell-inactive function), with
  its prototype in `KatranParameters.h`:
  ```c
  /*
   * The rte stop names (S105 P1 A2).
   *
   * What:       for a value that is one of katPlfoStopRow[], copies the
   *             stop's 3-character name (/64 32t /32 16t d32 /16 8Tr d16 /8
   *             4Tr d/8 /4 d/4 /2 d/2 1br 2br 4br 8br 16b 32b; the first 16
   *             are CrumpBit's crumpBit_grainStopNames, CrumpBitParameters.c
   *             :456, copied here because that table is static) and
   *             returns 1; else 0 (the caller prints the number).
   * Accessors:  menu.c menu_formatDtype3().
   */
  uint8_t katranParameters_rateName3(uint8_t value, char out[3]);
  ```
- **MODIFY** `menu.c:3477–3489` (`menu_formatDtype3()`, `case DTYPE_0B127`):
  ```c
          /* S105 P1 A2: Katran's rte names its step / bar stops */
          if ((uint8_t)(dtype >> 4) == DTYPE_TAG_KAT_RATE)
              return katranParameters_rateName3(value, out);
  ```
  - ADD `#include "KatranParameters.h"` beside `:71`.
  - The function's block (`:3458–3474`) adds "a tagged 0..127
    (S099 P2 fx depth, S105 P1 Katran rate)".

#### C3.07 — MODIFY `tools/dsp_test/modules/katran.c`
- **Stubs** after `seq_isRunning()` (`:88`):
  - `static uint16_t host_bpm = 120u; uint16_t seq_getBpm(void) { return host_bpm; }`
  - `static uint32_t host_ticks; uint32_t seq_getElapsedTicks(void) { return host_ticks; }`
  - the test advances `host_ticks` by tempo per block.
- **New KT-RATE:**
  - (a) at 60, 120 and 200 BPM, row 104 (/16) advances the phase by
    bpm / 60 × 4 × 32 / fs a block (1e-4), with `slf` 0 and the pull off;
  - (b) the rate rises with the row;
  - (c) running, `slf` 0, `chs` 0, a start phase 0.4 off: the error to
    frac(E / 24) is < 1 % after 2 s;
  - (d) `slf` 127: the error passes 25 % and returns below 5 % within 20
    periods;
  - (e) stopped: no pull (the phase step equals the rate exactly).
- **Group:** `G_RATE`.

### Gate 3
`make -C tools/dsp_test katran`; `make all`. The count rises ≈ +1
(`katran_cpu_s3.txt`).

---

## 4. Step 4 — Ring/FM and the sidebands (A6, A7)

#### C4.01 — MODIFY `KatranVoice.h` (constants, struct)
```c
/*
 * Osc 2's action on each core (S105 P1 A6; kat_ring_mode).
 *
 * What:       KAT_RM_RING both cores ring (Osc 2 x Torcido ch 1 at the
 *             output, the crossfade of kat_outGains()); KAT_RM_FMA core A's
 *             loop is phase-modulated by Osc 2 (its delay read displaced by
 *             k x B[n]) and core B rings; KAT_RM_FMB the reverse; KAT_RM_FAB
 *             both cores FM. A core in FM has no ring (its b = 0) and
 *             ringDepth is its FM depth: k = (ringDepth / 127)^2 x
 *             KAT_PM_MAX samples (core B half, in its half-rate samples).
 * Why:        your point P6: the ring switches to FM per core. Phase-
 *             modulating the loop acts where the ring acts (on the core's
 *             own signal, Torcido ch 1 being the delay output) and builds up
 *             with feedback; it leaves the Osc 1 loops untouched.
 */
#define KAT_RM_RING         0u
#define KAT_RM_FMA          1u
#define KAT_RM_FMB          2u
#define KAT_RM_FAB          3u
#define KAT_RM_COUNT        4u
#define KAT_PM_MAX          8.0f      /* full-rate samples at depth 127 (by ear) */
/* S105 P1 A7: Osc 2's self-feedback is scaled down above this pitch (Hz),
 * against aliasing of the harmonics it adds. */
#define KAT_SDB_FMAX        2000.0f
```
**ADD** to the struct after `syncPrev[2]` (`:244`):
```c
	/* S105 P1 A6: each core's loop-PM depth in Q8 of the Q16 read position
	 * per B LSB (0 for a ringing core): the read moves by (pmQ x B) >> 8. */
	int32_t  pmQ[2];
	/* S105 P1 A7: Osc 2's self-feedback: the phase offset per output LSB
	 * (0 .. 32768, a quarter turn at full scale) and the last output. */
	int32_t  bFbK, bFbPrev;
```

#### C4.02 — ADD `kat_ringControl()` (`KatranVoice.c`, before `kat_delayControl()` at `:752`)
```c
/*
 * Osc 2's action on the cores for this block (S105 P1 A6).
 *
 * What:       from kat_ring_mode and ringDepth: per core, the ring depth b
 *             (ringDepth / 127 for a ringing core, 0 for an FM one) into
 *             bOut[2], and the loop-PM depth pmQ[c] = k x 512 (k = (ringDepth
 *             / 127)^2 x KAT_PM_MAX samples for core A, half for core B in its
 *             half-rate samples; 0 for a ringing core).
 * Why:        kat_delayControl() needs k to keep the read behind the block's
 *             writes (T >= one block + k), so this runs first;
 *             kat_outGains() takes b. In ring mode pmQ is 0 and the delay
 *             read is today's integer read exactly.
 * Accessors:  Katran_calcVoiceAsync().
 */
static void kat_ringControl(KatranVoice *v, float bOut[2])
```

#### C4.03 — MODIFY `kat_delayControl()` (`KatranVoice.c:791–808`)
**What:** the clamp keeps the modulated read inside the written region.
```c
		/* S105 P1 A6: the PM moves the read by up to k samples either way:
		 * keep T - k >= one block (the staged-read rule) and T + k inside
		 * the ring */
		const float pmA = (float)v->pmQ[0] * (1.0f / 512.0f);
		target = fminf(fmaxf(target, KAT_TIME_MIN + pmA), 2040.0f - pmA);
		...
			v->tStep[1] = (fmaxf(0.5f * target,
			                     KAT_TIME_MIN_H + (float)v->pmQ[1] * (1.0f / 512.0f))
			               - v->tNow[1]) * (1.0f / (float)KAT_HALF_N);
```

#### C4.04 — MODIFY `kat_delayBody()` (`KatranVoice.c:845–870`)
```c
	const int32_t pq = v->pmQ[c];
	const int16_t *const bB = v->blkB;
	...
	for (n = 0u; n < count; n++) {
		const int32_t pm = pq * (int32_t)bB[half ? (n << 1) : n];
		const uint32_t pos = ((w + n + KAT_RING) << 16) - tq - (uint32_t)(pm >> 8);
```
**Comment block** (added to the function's What):
```c
 *             S105 P1 A6: a core in FM reads at pos - (pmQ x B[n]) >> 8: Osc
 *             2 (this block's, rendered first) displaces the timed tap by up
 *             to k samples, a phase modulation of everything in the loop
 *             whose index rises with each partial's frequency (2 pi f k /
 *             fs). Core B reads B at 2n, in half-rate samples. The dry tap is
 *             not moved. pmQ 0 (ring) is today's read exactly. Cost: a load,
 *             a multiply and a shifted subtract, 3 a sample (core B 1.5).
```

#### C4.05 — MODIFY `Katran_calcVoiceAsync()`
**What:** `kat_ringControl(v, bOut)` before `kat_delayControl()` (`:1497`);
`kat_outGains(..., bOut[0], bOut[1], G, og)`.

#### C4.06 — MODIFY `Katran_calcVoiceSyncBlock()` (`KatranVoice.c:1620–1632`)
**What:** Osc 2's form call and its guard (`:1623–1632`) move **before**
`kat_delayReadA()` / `kat_delayReadB()` (finding D-1).
**Comment block:**
```c
	/* S105 P1 A6: Osc 2 first. Its loops read only their own state, and the
	 * delay reads of a core in FM need this block's Osc 2 (the loop PM).
	 * Every consumer of blkB (the cores, the sync tests) still reads it
	 * after it is written. */
```

#### C4.07 — MODIFY the four Osc 2 loops (`KatranOsc.c:109–229`)
**What:** each reads at a phase offset by its last output:
```c
	const int32_t kf = v->bFbK;
	int32_t prev = v->bFbPrev;
	...
		const uint32_t fo = (uint32_t)(kf * prev);   /* |kf prev| <= 2^30 */
		const int32_t s = kat_read(row, ph + fo);     /* plain */
		...
		prev = s;
	...
	v->bFbPrev = prev;
```
- PWM D: `(kat_read(row, ph + fo) + sign * kat_read(row, q + fo)) >> 1`.
- Warp: `osc_pwmWarp(ph + fo, ...)`.
- Arena: `row[(ph + fo) >> 22]`.
- The phase step, the wrap flag and the row swap keep using `ph`.

**Comment block** (one, above `kat_bPlain()`; the others point to it):
```c
/*
 * OSC B's self-feedback (S105 P1 A7, Ring/FM Sideband).
 *
 * What:       every Osc 2 form reads its table at ph + bFbK x y[n - 1], y
 *             its own last output (int16), bFbK 0 .. 32768 (a quarter turn
 *             at full scale): a DX-style operator feedback. The phase step,
 *             the wrap flag in the LSB and the row swap use the plain phase,
 *             so cycle starts, the sync's wrap bit and K-D10 are unchanged.
 * Why:        your point P7: raising sdb adds Osc 2's harmonics one order
 *             after another, so the ring's (and the FM's) sidebands reach
 *             progressively higher (S105_P1_KATRAN_FURTHER_TWEAKS.md §A7).
 *             bFbK 0 is today's read exactly.
 * Cost:       a multiply and an add a sample (+1 for PWM D's second read).
 */
```

#### C4.08 — MODIFY `kat_oscControl()` (end, `:749`)
**What:** `v->bFbK = (int32_t)(32768.0f * s * s * fminf(1.0f, KAT_SDB_FMAX / fmaxf(fB, 1.0f)))`
with `s = ringSideband / 127`.
**Comment:**
```c
	/* S105 P1 A7: Osc 2's self-feedback for this block, (sdb / 127)^2 of a
	 * quarter turn, scaled down above KAT_SDB_FMAX against aliasing */
```

#### C4.09 — MODIFY `tools/dsp_test/Makefile`
- **`katran_break_pwm` (`:1381–1384`):** the sed must still unhalve
  `kat_bPwmD()` (KA-RANGE drives it), so it matches both read forms:
  ```make
  	sed 's/sign \* kat_read(row, q\( + fo\)\{0,1\})) >> 1;/sign * kat_read(row, q\1));/g' \
  ```
  and the check: `! grep -q 'kat_read(row, q[^)]*)) >> 1;'`.
- **ADD `katran_break_pmclamp`:**
  `sed 's/KAT_TIME_MIN + pmA/KAT_TIME_MIN/'`, which must fail KT-FM's clamp
  check. Comment: "S105 P1: the FM clamp removed; KT-FM must catch a read of
  the current block's unwritten samples."

#### C4.10 — MODIFY `tools/dsp_test/modules/katran.c`
- **New KT-FM:**
  - (a) `ringMode` 0: `pmQ` = {0, 0}, and `kat_delayReadA()` output equals
    today's integer read (KA-DLY's reference);
  - (b) `fmA` at depth 127: core A's `cA1` = 0, `cA0` = the dry gain, and
    each `blkD[0][n]` equals a reference read at w + n − T − k B[n] / 32768
    (2 LSB);
  - (c) across 2,000 blocks with a moving `tim` and FM 127, no read index
    falls within the current block's write span: the host checks
    T(n) − k|B| ≥ 32 at each sample (16 for core B).
- **New KT-SDB:**
  - (a) `sdb` 0: `blkB` bit-identical to a run with `bFbK` forced 0;
  - (b) Osc 2 on wav Sin at 220 Hz: the share of energy above the
    fundamental (one DFT bin sum) rises monotonically over `sdb` 0, 32,
    64, 96, 127;
  - (c) finite and inside int16 at 127 on every form.
- **Groups:** `G_FM`, `G_SDB`.

#### C4.11 — MODIFY `KatranOsc.h:1–35` (header)
**What:** the What adds: "S105 P1 A7: the OSC B loops read at the phase
offset by their own last output (bFbK, Ring/FM Sideband)".

### Gate 4
`make -C tools/dsp_test katran` (including both new break builds);
`katran_cpu_check` → `katran_cpu_s4.txt`. **Expected:** ≈ +7 on step 3
(FM +4.5, feedback +2.5). This is Part A complete: ≈ 372.6 (plan §C).

---

## 5. Step 5 — the default sound

#### C5.01 — MODIFY `Katran_initVoice()` row defaults (C1.28's lines)
**What:** the values found in host renders and on the device, starting
from C1.28's. Fixed during this step:
- the chaos rows (F-2: audibly on by default);
- `kat_flt_cut` / `kat_flt_width` against today's F1 tone;
- `kat_ring_depth` against today's ring level;
- the constants marked *by ear* (§A3, §A4, A2's pull, A5's make-up,
  `KAT_PM_MAX`, `KAT_SDB_FMAX`).

#### C5.02 — MODIFY `make_init_kat.py` `INIT`; REGENERATE `init.kat`
The same bytes as C5.01. `--check` passes.

#### C5.03 — ADD `tools/dsp_test/baselines/s105/init_values.txt`
Each value chosen, the one it replaced, and why (one line each).

---

## 6. Step 6 — Part B, the core couplings (as far as the count allows)

Order: G with PC-0, then features 1, 4, 3, 2, 5a, and 5b only if ≥ 3 a
sample remain after step 4's count. Each feature's off state is the Part A
voice, bit for bit.

#### C6.01 — G: per-core control state
| Where | Change |
|---|---|
| `KatranVoice.h:212–217` | `tTube` → `tTube[2]`; `tExc, tNse` → `[2]`; `kickSwell` → `[2]`; `kE` → `[2]`; `fb, fbStep` → `fb[2], fbStep[2]` (`xfb` stays one) |
| `KatranVoice.c:522–533` (`kat_kickControl()`) | per core: `kE[c] *= katKickK`; `tExc[c]`, `tNse[c]`, `kickSwell[c]` from `kE[c]` |
| `KatranVoice.c:944–961` (`kat_coreBody()`) | `tube = v->tTube[c]`; `eG` from `v->tExc[c]`, `eA` / `eN` from `v->tNse[c]`; `fbS` from `v->fbStep[c]`; `fb` from `v->fb[c]` (c is a constant per wrapper: the loop is unchanged) |
| `KatranVoice.c:1546–1560` (async) | the tube and feedback targets per core: `fbT[c]` with `kickSwell[c]` and the clamps per core |
| `KatranVoice.c:1660` (sync) | `fb[c] += 32 · fbStep[c]` for both |
| `KatranVoice.c:1349–1368` (init), `:1719` (trigger) | both elements initialised alike; the trigger sets `kE[0] = kE[1] = vel` (feature 3 changes this) |

**Comment block** (struct):
```c
	/*
	 * S105 P1 Part B (G1-G3): each core's feedback ramp, tube, kick level and
	 * the kick's exciter level, noise mix and swell. Today both elements
	 * are equal (the Part A voice, bit for bit: law PC-0); the couplings
	 * (features 1, 2, 3) move one core's without the other. Each core's
	 * loop loads its own once a block; the per-sample cost is unchanged.
	 * RAM +28 B.
	 */
```

#### C6.02 — ADD the frozen Part A reference (PC-0)
**What:**
1. Copy `KatranVoice.c/.h`, `KatranOsc.c/.h` and `KatranTables.c/.h` as they
   stand at gate 4 into `tools/dsp_test/baselines/s105/katran_partA/` (in
   the tree, not `build/`).
2. The `katran` target extracts them like the live files, renaming
   `Katran_` → `KatranRef_` and `kat_` → `katref_` (and `KatranVoice` →
   `KatranRefVoice`) by `sed`, and includes both in `modules/katran.c`.

**PC-0:** with every Part B value off, the live and the frozen voice give
bit-identical `buf`, `right[]`, both rings and every state over 2,000
blocks, for `init` and for KA-BND's extremes.
**Comment** (Makefile): "S105 P1 Part B: PC-0 against the frozen Part A
voice (the S103 P6 CO-MONO1 method)".

#### C6.03 — Feature 1: cross-level coupling (without the pitch nudge)
| Where | Change |
|---|---|
| `KatranVoice.h` struct | `float lvl[2];` (followers); row byte `couple` (PM63, index 58, C6.09) |
| `KatranVoice.c` ADD `kat_levelUpdate()` | P_c = max \|blkD[c][i]\| / 32768 over every 4th sample (8 reads core A, 4 core B; last block's D, still in the voice); `lvl[c] += k (P_c − lvl[c])`, k 0.07 rising, 0.0073 falling |
| async, before the band and feedback targets | d_A = min(1, 2 lvl[1]), d_B = min(1, 2 lvl[0]); x = (couple − 63) / 63. Rivalry (x < 0): `fbT[c] *= 1 − 0.8 |x| d_c`; band mod −24 |x| d_c. Sympathy (x > 0): band mod +24 x d_c; feedback untouched |

**Comment block** (`kat_levelUpdate()`):
```c
/*
 * Each core's level follower (S105 P1 Part B, feature 1).
 *
 * What:       the peak of |blkD[c]| over every 4th sample (the block's delay
 *             output, still in the voice from the last sync block) / 32768,
 *             followed with a 10 ms attack and a 100 ms release (k 0.07 /
 *             0.0073 a block).
 * Why:        the cross-level coupling moves each core by the other's level:
 *             rivalry lowers the other's feedback and closes its band (the
 *             louder core wins and they take turns), sympathy opens it. The
 *             feedback is never raised: two loops raising each other's gain
 *             would latch at full scale.
 * Cost:       about 40 a block.
 */
```

#### C6.04 — Feature 4: cross-sync, the wrapper form
| Where | Change |
|---|---|
| `KatranOsc.c:231–343` (`kat_aBody()`, body from `:266`) | ADD a constant `xs` (0 own, 1 other): `dSync = xs ? v->blkD[c ^ 1] : v->blkD[c]`, indexed `xs ? (half ? (n << 1) : (n >> 1)) : n` (core A reads core B's held, core B reads core A's decimated: the xfb indexing of `kat_coreBody()`) |
| `KatranOsc.c:345–387` | eight ADDed wrappers `kat_a{Plain,PwmD,Warp,Arena}Core{A,B}X()` with `xs` 1 |
| `KatranOsc.h:52–80` | their prototypes and a paragraph |
| `KatranVoice.c:1635–1654` | the form chosen with `xs` from the link mode (C6.09) |
| `katran_cpu.py:50–51` | `AL_FORMS` / `AH_FORMS` hold the X wrappers too; the dearest counts |

**Comment block** (`kat_aBody()` What):
```c
 *             S105 P1 Part B (feature 4): xs 1 syncs the copy from the other
 *             core's delay output instead of its own (1a from core B's
 *             half-rate block held, 1b from core A's decimated), so the two
 *             loops re-clock each other. A separate wrapper per form and
 *             copy, chosen per block: 0 a sample (the mask form would cost
 *             4..6); "both" is not offered (plan §B3.3).
```

#### C6.05 — Feature 3: kick ping-pong
| Where | Change |
|---|---|
| `KatranVoice.h` struct | `uint8_t ppToggle, ppEcho; float ppVel;` (+6 B) |
| `Katran_triggerVoice()` (`:1705–1720`) | by the link mode's kick half: both `kE[0] = kE[1] = vel`; alternate `kE[ppToggle] = vel, ppToggle ^= 1`; echo `kE[0] = vel, ppEcho = tNow[0] / 32 (≤ 56), ppVel = vel`; random `kE[rng & 1] = vel` |
| `kat_kickControl()` | echo: `if (ppEcho && --ppEcho == 0) kE[1] = ppVel;` |

The run gain's re-arm stays shared (Q-B3).
**Comment block** (trigger):
```c
	/* S105 P1 Part B (feature 3): which core a trigger kicks (the link
	 * mode): both (S103 P1), alternate, echo (core B one core-A loop time
	 * later) or random. Only the burst and the swell move: the run gain's
	 * re-arm stays shared, so the drone's on / off is still one control. */
```

#### C6.06 — Feature 2: anti-phase modulation
| Where | Change |
|---|---|
| `KatranVoice.h` struct | `uint8_t phase;` (not bound to a row: Q-B1 places it after listening; 0 = identity) |
| async | s_B = 1 − 2 (phase / 127): core B's `cv · lfl · 12` band term, `0.15 cv lfl` tube term, `0.2 inv lfl` feedback term and the slow chaos band term take s_B |

**Comment block:**
```c
	/* S105 P1 Part B (feature 2): core B takes the shared modulators (the
	 * patch LFO's band, tube and feedback terms; the slow chaos envelope)
	 * with sign 1 - 2 phase', so one core opens as the other closes. Osc 2's
	 * terms, F3 and the VCA offsets are untouched. phase 0 is identity. */
```

#### C6.07 — Feature 5a: the right level measure
| Where | Change |
|---|---|
| `KatranVoice.h` struct | `float lvlOut; uint8_t rlvSrc;` (`rlvSrc` fixed by ear in this step: Q-B2) |
| `Katran_calcVoiceSyncBlock()` before `kat_outRender()` (`:1674`) | the peaks of \|buf\| and \|right\| every 4th sample, through §C6.03's follower, the mean of the two → `lvlOut` |
| `kat_plfoStep()`, `kat_ceStep()` | `const float rl = v->rlvSrc ? v->lvlOut : v->ringPrev;` in place of `ringPrev` |

**Comment block:**
```c
	/* S105 P1 Part B (feature 5a): the patch's RING OUT -> LFO MOD IN, at
	 * block rate the meaningful measure is the level of the cores' outputs
	 * (after the drive, before the matrix), not one delay sample
	 * (ringPrev). rlvSrc 0 keeps ringPrev exactly (the Part A sound). */
```

#### C6.08 — Feature 5b (only if ≥ 3 a sample remain)
**What** (finding D-5):
- **core A:** kAM folds into o1 (`cA1 += kAM · dS · G` in
  `kat_outGains()`): 0 a sample;
- **core B:** kAM (1 − b) folds into cB1; the new part is one term in
  `kat_coreBody()`'s half-rate branch:
  `o += t1 * kB2 * Bv * Bv;` with
  `kB2 = b · kAM · 32767 / 32768² · G` per block. That is +2 per half-rate
  sample, **+1 a sample**.

Struct: `uint8_t kAM;` (not a row: Q-B1), `float kB2, kB2Step;`.
**Comment block:**
```c
 *             S105 P1 Part B (5b, m = Osc 2): the patch's audio-rate AMP CV.
 *             On core A it is t1 (o0 + (o1 + kAM) B): more ring, folded into
 *             cA1 at no cost. On core B, r (1 + g1 + kAM B) adds
 *             t1 b kAM B^2: Osc 2 squared, sidebands at twice its frequency,
 *             the one new term (kB2, ramped). kAM 0 is identity.
```

#### C6.09 — Part B's rows (after you confirm Q-B1 by ear)
| File | Change |
|---|---|
| `KatranParameters.c` enum / table | 58 `kat_couple` (CAT_CORES, LONG_COUPLE, SHORT_CPL, PM63, `couple`); 59 `kat_link` (CAT_CORES, LONG_LINK, SHORT_LNK, `ROW_SELECT_ONOFF`, `ONOFF_TAG_KAT_LINK`, `link`) |
| `KatranParameters.h` | `KATRAN_PARAM_DESCRIPTOR_COUNT 60u` (the `≤ KATRAN_PARAM_FX_FIRST` assert still holds: 60 ≤ 60) |
| pages | filter cell 3 = `cpl`; mix screen 2 cell 4 = `lnk` |
| `menu.h`, `MenuLabels.c` | `CAT_CORES` "Cores"; `LONG_COUPLE` "Couple", `LONG_LINK` "Link"; `SHORT_CPL` "cpl", `SHORT_LNK` "lnk" |
| `MenuText.h`, `menu.c` | `ONOFF_TAG_KAT_LINK 11u` (free: tags 1..10 are taken), names `{8} obt oal oec orn xbt xal xec xrn` (own / other sync × both / alt / echo / random kick); `menu_onoffTagNames()` case and its assert (`KAT_LINK_COUNT 8`) |
| `make_init_kat.py`, `init.kat` | `kat_couple 63`, `kat_link 0` |

**Comment block** (the rows):
```c
    /*
     * S105 P1 Part B (§B7): how the two cores relate. cpl: bipolar, below 0
     * rivalry (the louder core lowers the other's feedback and closes its
     * band), above 0 sympathy (opens it); 0 none. lnk: which core's delay
     * syncs each Osc 1 copy (own / other) and which core a trigger kicks
     * (both / alternate / echo / random). Both off reproduce the Part A
     * voice (PC-0).
     */
```

#### C6.10 — MODIFY `tools/dsp_test/modules/katran.c` (Part B laws)
**What:** the laws of plan §D, Part B:
- PC-LVL, PC-RIV and PC-SYM ("F1" read as the band's corners);
- PC-SYNC (own / other);
- PC-PP, PC-PHASE, PC-RLV and PC-AM (the D-5 form: core A's output equals a
  `ringDepth` + kAM fold; core B's B² term against a double reference);
- PC-BND.

**Break builds:** `katran_break_riv` (rivalry raises the feedback) and
`katran_break_xsync` (core A's X wrapper reads `n` instead of `n >> 1`).

### Gate 6
- PC-0 bit-identical; every Part B law passes.
- `katran_cpu_check` → `katran_cpu_s6.txt`. **Expected:** ≈ 375.5 without
  5b, 376.5 with it (D-5).
- Over 380: the ladder of plan §C.

---

## 7. Step 7 — gates and diagrams

| ID | What |
|---|---|
| C7.01 | KA-BND / PC-BND at every new extreme (`wid` 0, `cut` ±63, `ofs` ±63, `drv` 127, FM 127 on both cores, `sdb` 127, `imd` 127, `cpl` ±63) with `fbk` 127, `drv` (Torcido) 127, 4,000 blocks: finite, inside int16, the feedback clamps per core |
| C7.02 | `make all && make img`; `link_budget.py` (flash, est. +2.8 KB); the size assert; `sizeof` recorded (`baselines/s105/katran_size.txt`, est. ≈ 1,165 B) |
| C7.03 | the mixer hot-loop and inlined-set diff against the pre-S105 image (`elf_hotloops.py`, the S089 P0 F3 rule) |
| C7.04 | `katran_cpu_check` final → `baselines/s105/katran_cpu.txt` (gate 380) |
| C7.05 | **diagrams:** `tools/katran/make_block_diagram.py`: the Osc 2 box (`:116–118`), the Osc 1 / buffer boxes and the `src` pill (`:139–154`), the F1 and F2 boxes (`:207–216`) become the band box (cut / wid / ofs, the resonance, the tracking); add the drive and F3 after the matrix, the FM path into each delay read, Osc 2's feedback, the four chaos rows. `tools/katran/make_block_diagram_p2.py`: the buffer module (`:98`), Osc 2 (`:110`), `src` (`:117`), RING (`:139`), EXCITER MIX (`:147`), F1 / F2 (`:152–157`); then the modulation sources (CE1 / CE2 → fast / slow, `rte` synced) |
| C7.06 | `KATRAN_BLOCK_DIAGRAM_SVG.md`: the summary (`:58–91`), the row tables (§2: `src` → the two types, `:117`; §2.3 Osc 2; the filter section; the chaos section) and the note at `:15` (your `buf` proposal: done). Regenerate both `.svg` |

---

## 8. Step 8 — the device

1. **The `p` cycle probe** (DEV_MODE_LOGGING): median ≤ 18.6k cycles a block.
   Measure on the worst forms:
   - Osc 1 pwm Rec with Osc 2 buf;
   - FM on both cores;
   - `sdb` 127;
   - `drv` > 0;
   - with Part B: the X sync wrappers and the link modes.
2. **Listening:** the chaos rows, the band (resonance and tracking), `rte` at
   its stops against the sequencer, the drive, FM against ring, the
   sidebands, Osc 2 on Sin; with Part B, `cpl` and `lnk`.
3. **Q-B1 by ear:** whether C6.09's rows are confirmed, and where `phase`
   and `kAM` go.

---

## 9. Statements this makes out of date (for you to log; no step edits them)

- **`MEMORY.md`:** none. It has no Katran entry. If you add one, record:
  - the voice size (≈ 1,165 B);
  - the CPU count;
  - 58 rows (60 with Part B's two), and the index map of plan §A1.4;
  - the label ids added and removed (§A1.5).
- **`knowledge_files/drafts/DRAFT_KATRAN_DRONE_SYNTH.md`:** the row list, the
  F1 / F2 / F3 chain, `src`, the chaos rows.
- **`S103_KATRAN_NOISE_SYNTH.md`** (a historical plan in the root): §3.3
  (the chaos envelopes), §3.4 (`src`) and §8.2 (the rows). It records the
  S103 design, so it is not edited.
