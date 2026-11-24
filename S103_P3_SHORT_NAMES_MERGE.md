# S103 P2 — Short label names: what can be merged

**Date:** 2026-11-24. **Branch:** `dev-ph7-alkaline` (the S102 P1 Fix 1
tree). **Status:** analysis and proposal, **revision 2**. **No code has been
changed.**

**History:**
- revision 1: merges limited by "no two modulatable rows of one instrument
  may share a short name".
- **revision 2 (your note, 2026-11-24):** targets also show their long name
  in the single-parameter view. Shared 3-letter names in a list are not a
  problem. Avoid them only where they are adjacent, or where they don't make
  sense as a group. §1 checks where long names are shown; every tier is
  revised on that rule.

**Your instructions (2026-11-24):**
- "run a pass on what short label names can be condensed into each other";
- "check that and revise based on your findings".

**Why now:** the short-label table holds 228 of its 256 possible ids
(`MenuLabels.c:194` asserts ≤ 256; ids are `uint8_t`).
`S103_P1_IMPLEMENTATION.md` ME-1 adds 21 for Katran, which would leave 249.

**Method:**
- Every `shortNames[]` entry (`Core/Menu/MenuLabels.c:16–82`) was read
  beside its `enum shortNamesEnum` id (`Core/Menu/menu.h:290–365`).
- Each id's uses were counted across `Core/` and `main.c`. `tools/` holds no
  `SHORT_*` reference (checked).
- Every place a short label is drawn was traced, to see whether a long name
  is reachable from it (§1).

---

## 0. Summary

| Tier | What | Ids saved | Display changes? | Status |
|---|---|---:|---|---|
| **A** | Exact duplicate strings under two ids, plus one unused id | **5** | none | ready: the edit list is §7 |
| **B** | Katran's planned labels (`S103_P1` ME-1) that can reuse an existing id | **10** of the 21 planned (11 new remain) | Katran's cells only | needs your OK; updates the S103 P1 schedule |
| **C** | Synonyms in different engines | **6** | yes: 6 labels change text | your call, label by label |
| **D** | Same-instrument groups now allowed by your rule (the Colloid's filter envelope) | **5** | yes: 5 Colloid labels change text | your call |
| — | Families that still should not merge (same page, or not a group) | 0 | — | §6 |
| — | Legacy `valueNames` rows that look unreferenced | up to 11 | — | out of scope: needs its own reachability check (§6) |

**Totals:**

| | Today | After A | + Katran (B applied) | + C | + D |
|---|---:|---:|---:|---:|---:|
| Short ids | 228 | **223** | **234** (planned: 249) | **228** | **223** |

**Revision 1's OSC B conflict is withdrawn:** Katran's OSC B may keep `coa
fin pos`. They sit on the `fm` page, apart from OSC A's on `osc`. Their
long view reads "FM Coarse" against "Oscilltr Coarse" (§3).

---

## 1. Where short labels are shown, and where the long name is

Every place a short label is drawn was traced. **Each has a long-name view
one action away:**

| Where | What it shows | The long-name view | Source |
|---|---|---|---|
| A page's cells (instrument, Kit, Effect) | the 3-character cell name | turning or touching the cell: row 0 shows **category + long name** (8 + 8) | `menu_repaintGeneric()`, `menu.c:15697–15711` (instrument cells via `menu_instrumentCellLabels()`; Effect cells via `menuEffects_cellLabels()`) |
| LFO / velocity destination cells (`ds1`, `ds2`, `dst`) | the target's short name only | the single-parameter view: row 1 shows the target's **category + long name** (`menu_displayInstrumentTargetFull()`, Effect and Scene targets included) | compact `menu.c:6809` → `menu_formatInstrumentTargetShort()` (`:6450`); full `menu.c:15732` → `:6517` |
| Step automation, compact row | the slot number + the target's short name | editing the target (cursor 3): row 1 shows **category + long name** (14 characters) | compact `menu.c:15548`; full `menu.c:15366–15428` |
| Step automation, an Effect target | the row's short name | the same cursor-3 view, through `menu_stepAutomationEffectLabel()` | `menu.c:15559` |
| The Effect page | the 3-character cell name | `menuEffects_cellLabels()` supplies category and long name for the single-parameter view | `menuEffects.c:578`, `:593–608` |

**No place shows a short label without a reachable long name.** No other
code reads `shortNames[]` (`grep`: `menu.c:13413` and `:15936` are the Global
`valueNames[]` cells, which have the same single-parameter view).

**One condition for any merge:** two rows that share a short name must still
differ in **category or long name**, or the long view cannot tell them apart
either. Every merge below was checked for it.

**The rule this document now uses (yours):**
1. A shared short name is fine **within** an instrument's target list.
2. Avoid it where the rows are **adjacent**: on the same page, whose screens
   are flipped through as one group.
3. Avoid it where the shared name **doesn't read as a group** (two
   unrelated meanings that would be confusing side by side).

---

## 2. Tier A: same string, two ids (no visible change)

| Merge | Keep | Remove | Uses of the removed id |
|---|---|---|---|
| A1 `mix` | `SHORT_MIX` [14] | `SHORT_FXM` [227] | `AlkalineParameters.c:326`, `ColloidParameters.c:407` (`fx_mix`) |
| A2 `rte` | `SHORT_RTE` [133] | `SHORT_FXR` [225] | `AlkalineParameters.c:324`, `ColloidParameters.c:405` (`fx_rate`) |
| A3 `mod` | `SHORT_MOD` [6] | `SHORT_MODE` [44] | `menu.c:1415` (`TEXT_MODE`), `:1441` (`TEXT_TRIGGER_GATE_MODE`) |
| A4 `rol` | `SHORT_ROLL_RATE` [83] | `SHORT_ROLL` [32] | `menu.c:1398` (`TEXT_ROLL_SPEED`) |
| A5 (unused) | — | `SHORT_EG2` [5] `"eg2"` | **none** (0 uses) |

**Saves 5 ids (228 → 223).** No behaviour, storage or display change.

---

## 3. Tier B: Katran's planned labels (S103 P1 N-8, ME-1)

Each row is checked against Katran's own pages (§4 N-8 of S103 P1): its
category or long name differs from any other Katran row with the same short
name (§1 condition); no same-page repeat (rule 2).

| Katran row | Page | Planned | **Use instead** | Long view reads | Same short elsewhere in Katran |
|---|---|---|---|---|---|
| `kat_noise` | fm | `nse` | **`noi`** (`SHORT_NOISE`) | Noise NoiseMix | — |
| `kat_drift` | click | `drf` | **`wow`** (`SHORT_WOW`) | Delay Drift | — |
| `kat_wet` | click | `wet` | **`bln`** (`SHORT_BLN`) | Delay Wet | — (the effect's `mix` on click screen 3 is why `mix` is not used) |
| `kat_level_a` | fm | `exc` | **`lvA`** (`SHORT_LVA`) | Oscilltr Exciter | — |
| `kat_b_level` | fm | `lvl` | **`lvB`** (`SHORT_LVB`) | FM Level | — |
| `kat_sync` | fm | `sth` | **`snc`** (`SHORT_SYNC`) | Oscilltr SyncThr | `lfo_sync` on the `lfo` page (LFO ClockSnc): another page |
| `kat_vca` | mod | `vca` | **`bal`** (`SHORT_BAL`) | Voice VCA Bal | — |
| `kat_plfo_rate` | mod | `lfr` | **`frq`** (`SHORT_FREQ`) | Mod Osc Rate | `kat_loop_cutoff` (Filter Cutoff, filter page); `lfo_rate` (LFO Frequncy, lfo page): other pages |
| `kat_plfo_mod` | mod | `lfm` | **`mod`** (`SHORT_MOD`) | Mod Osc SelfMod | — |
| `kat_plfo_level` | mod | `lfl` | **`dpt`** (`SHORT_FXD`) | Mod Osc Level | the reserved effect's `dpt` (click screen 3): another page |

**Kept as planned (and why):**

| Row | Short | Why not merged |
|---|---|---|
| OSC B `kat_b_coarse`, `kat_b_fine`, `kat_b_pos` | `coa fin pos` (existing ids) | Fine under rule 1 (revision 1's conflict withdrawn): OSC A's are on `osc`, OSC B's on `fm`; the long view reads Oscilltr / FM |
| `kat_ce1_*`, `kat_ce2_*` | `cy1 sk1 dp1 cy2 sk2 dp2` (new) | Both envelopes are on the `aeg` page, CE1's depth and CE2's three on the same screen: rule 2 |
| `kat_kick`, `kat_chaos`, `kat_source`, `kat_track`, `kat_low_cut` | `kik chs src dtk lct` (new) | No existing label means the same |

**Saves 10 of the 21 planned ids:** Katran adds **11** new ids (`kik chs src
dtk lct cy1 sk1 dp1 cy2 sk2 dp2`).

**The pages with Tier B applied:**

| Page | Screen 1 | Screen 2 | Screen 3 |
|---|---|---|---|
| osc | `coa fin pos dtn` | `wav typ src dmp` | — |
| aeg | `run kik cy1 sk1` | `dp1 cy2 sk2 dp2` | — |
| mod | `dst amt chs` | `frq mod dpt bal` | — |
| fm | `coa fin pos lvB` | `lvA noi snc` | — |
| click | `drv bln tim fbk` | `wow dtk` | (reserved: `fx dpt rte mix`) |
| filter | `frq res typ lct` | `cut` | — |
| lfo | platform | | |
| mix | `vol pan spr wid` | — | — |

Note: with B, `dpt` appears on `mod` (patch LFO level) and in the reserved
effect screen of `click`. Those are different pages, so this meets rule 2.
If you would rather keep the patch LFO's three as a visible family, keep
`lfr lfm lfl` (3 more new ids, 14 total).

---

## 4. Tier C: synonyms in different engines (display text changes)

Each pair means the same thing in different engines. Merging changes the
**displayed text** of the removed label. None puts two equal names on one
page.

| # | Today | Becomes | Rows affected | Long view still distinct? |
|---|---|---|---|---|
| C1 | `dcy` (`SHORT_DCY`) | **`dec`** (`SHORT_DECAY`) | Space `rvb_decay` (`SpaceParameters.c:123`), `ech_decay` (`:204`) | Reverb Decay / Plate Decay |
| C2 | `lng` (`SHORT_LNG`) | **`len`** (`SHORT_LENGTH`) | CrumpBit `crump_grain_length` (`CrumpBitParameters.c:179`); its long name is already "Length" | Grain Length |
| C3 | `stm` (`SHORT_STM`) | **`tim`** (`SHORT_MICROTIMING`) | Tetrakt `tkt_space_time` (`TetraktParameters.c:201`) | Space Time; matches the `Spc` chain's `tim` (`menu.c:12784`) and the Space delays |
| C4 | `sfb` (`SHORT_SFB`) | **`fbk`** (`SHORT_FBK`) | Tetrakt `tkt_space_feedback` (`:202`) | Space Feedback; matches `Spc` |
| C5 | `sdt` (`SHORT_SDT`) | **`dtn`** (`SHORT_DTN`) | Tetrakt `tkt_space_detune` (`:203`) | Space Detune |
| C6 | `sto` (`SHORT_STO`) | **`ton`** (`SHORT_TON`) | Tetrakt `tkt_space_tone` (`:204`) | Space Tone; the layer tones `tnA`..`tnD` sit on another page |

**Saves 6 ids.** C3–C6 make the Tetrakt's space rows read like the drum
family's `Spc` chain, which is the same kind of stage (S101 P3).

---

## 5. Tier D: same-instrument groups your rule now allows

| # | Today | Becomes | Page | Long view (category) | Rule check |
|---|---|---|---|---|---|
| D1–D4 | the Colloid's filter envelope `fat fde fsl fre` | **`atk dec slv rel`** | `click` (alone with `fam drv`) | **Filt EG** Attack / Decay / SustLvl / Release, against the amp envelope's **Amp EG** (`aeg` page) | another page from the amp envelope; it reads as the envelope group it is |
| D5 | the Colloid's `fam` (filter env amount) | **`amt`** (`SHORT_FM_AMNT`) | `click` | Filt EG Amount, against velocity's Velocity Amount (`mod` page) | another page from the velocity `amt` |

**Saves 5 ids.** The Advanced Colloid's `click` page would then read
`atk dec slv rel | amt drv | fx dpt rte mix`.

**Not proposed under D, though rule 1 would allow it:**
- **The Colloid's pitch envelope `pat pde psl pre`:** it sits on screen 2 of
  the `aeg` page, right after the amp envelope's `atk dec slv rel`. Rule 2
  keeps it distinct.
- **`pam` (pitch env amount):** it sits on the `osc` page, but its envelope
  is on `aeg`. As `amt` it would not read as part of a group (rule 3).

---

## 6. What still should not merge

| Family | Ids | Why |
|---|---:|---|
| Tetrakt layers A–D: `sm tu lv dy pn dr sr sp tn te` × 4 | 40 | The four letters of each family sit on **one screen** (rule 2). One id per family would also give the four cells the same category and long name, failing §1's condition. A merge needs a "base label + layer letter" mechanism in the descriptor, which is asserted at 20 B (`InstrumentManager.h:444`): a separate design task (≈ 30 ids saved) |
| Colloid pitch envelope `pat pde psl pre` | 4 | Same page as the amp envelope (§5) |
| Colloid filter offsets `fro reo eno dro` | 4 | Screen 2 of the filter page, after `frq res typ rte`: as `frq res …` they would repeat on one page |
| HiHat `d1 d2`; Cymbal/HiHat `f1 f2 g1 g2` | 6 | Pairs on one screen |
| LFO `am1 am2 vo1 vo2 ds1 ds2` | 6 | Pairs on the one LFO page (which never changes) |
| Katran `cy1 sk1 dp1` / `cy2 sk2 dp2` | 6 | One page (§3) |
| Chains / overlays / triggers (`cn1 cn2 nx1 nx2 pb< pb> pn< pn> pv< pv> co1 co2`) | 12 | Pairs on one Global page |

**Out of scope here — legacy `valueNames` rows.** Several `TEXT_*` ids in
`valueNames[]` (`menu.c:1358`) are not referenced by name anywhere in
`Core/`: for example `TEXT_EQ_GAIN`, `TEXT_EQ_FREQ`, `TEXT_POS_X`,
`TEXT_POS_Y`, `TEXT_FLUX`, `TEXT_MIDI_MODE`, `TEXT_FETCH`, `TEXT_PARAM_VAL`,
`TEXT_PROBABILITY`, `TEXT_PATTERN_NEXT`, `TEXT_TARGET_VOICE_LFO`. Eleven
short ids are used only by such rows (`voi eqg eqf prb nxt x y flx mid fch
val`).
- A row can still be reached by arithmetic on another id. For example,
  `TEXT_TRIGGER_OUT2_PPQ` has no reference by name, but `TEXT_TRIGGER_OUT1_PPQ`
  does.
- So removing them needs a reachability trace of every `valueNames[]` index
  (`menu.c:13413`, `:15936`). That is a separate pass. This document does
  not propose those removals.

---

## 7. Tier A edit list (ready to implement)

The ids are removed from the enum and the table together, so the
`MenuLabels.c:188` assert keeps them aligned.

| # | File | Line | Action |
|---|---|---|---|
| A1, A2 | `Core/Menu/menu.h` | 364 | **modify**: `SHORT_FX, SHORT_FXR, SHORT_FXD, SHORT_FXM,` → `SHORT_FX, SHORT_FXD,` |
| A1, A2 | `Core/Menu/MenuLabels.c` | 81 | **modify**: `{"fx "},{"rte"},{"dpt"},{"mix"},` → `{"fx "},{"dpt"},` |
| A1 | `Core/DSP/Instruments/Alkaline/AlkalineParameters.c` | 326 | **modify**: `SHORT_FXM` → `SHORT_MIX` |
| A1 | `Core/DSP/Instruments/Colloid/ColloidParameters.c` | 407 | **modify**: `SHORT_FXM` → `SHORT_MIX` |
| A2 | `Core/DSP/Instruments/Alkaline/AlkalineParameters.c` | 324 | **modify**: `SHORT_FXR` → `SHORT_RTE` |
| A2 | `Core/DSP/Instruments/Colloid/ColloidParameters.c` | 405 | **modify**: `SHORT_FXR` → `SHORT_RTE` |
| A3 | `Core/Menu/menu.h` | 304 | **modify**: remove `SHORT_MODE,` |
| A3 | `Core/Menu/MenuLabels.c` | 23 | **modify**: remove the second `{"mod"},` (after `{"nxt"},`) |
| A3 | `Core/Menu/menu.c` | 1415, 1441 | **modify**: `SHORT_MODE` → `SHORT_MOD` |
| A4 | `Core/Menu/menu.h` | 301 | **modify**: remove `SHORT_ROLL,` |
| A4 | `Core/Menu/MenuLabels.c` | 22 | **modify**: remove the first `{"rol"},` (before `{"mrp"},`) |
| A4 | `Core/Menu/menu.c` | 1398 | **modify**: `SHORT_ROLL` → `SHORT_ROLL_RATE` |
| A5 | `Core/Menu/menu.h` | 293 | **modify**: remove `SHORT_EG2,` |
| A5 | `Core/Menu/MenuLabels.c` | 17 | **modify**: remove `{"eg2"},` |

**The comment** placed above the S099 P1 effect labels in `menu.h` (≈ line
362) and `MenuLabels.c` (lines 79–80):

```c
/*
 * S103 P2 (A1, A2): the effect's mix and rate cells use the shared
 * SHORT_MIX ("mix") and SHORT_RTE ("rte") ids; the identical SHORT_FXM and
 * SHORT_FXR entries were removed. Labels are compile-time ids (no stored
 * data), so the later ids renumber with no effect.
 */
```

and at the removed `SHORT_EG2`, `SHORT_ROLL`, `SHORT_MODE`:

```c
/* S103 P2: SHORT_EG2 (unused), SHORT_ROLL (= SHORT_ROLL_RATE, "rol") and
 * SHORT_MODE (= SHORT_MOD, "mod") removed; their rows use the kept ids. */
```

**Gates:**
- `make all && make img` builds: the `MenuLabels.c:188` and `:194` asserts
  hold.
- `make -C tools/dsp_test special_tags` passes: label ids are not parsed.
- **Image unchanged except the label data:** `shortNames[]` shrinks by 20 B
  (5 × 4 B); the descriptor tables differ only in their `short_id` bytes.
- Device: the Advanced `click` page still shows `fx dpt rte mix`; the Global
  trigger-mode and roll cells still show `mod` and `rol`.

---

## 8. If adopted: what else follows

- **Tier B:** in `S103_P1_IMPLEMENTATION.md`:
  - ME-1/ME-2 drop to 11 new short labels (`SHORT_KIK SHORT_CHS SHORT_SRCE
    SHORT_DTK SHORT_LCT SHORT_CY1 SHORT_SK1 SHORT_DP1 SHORT_CY2 SHORT_SK2
    SHORT_DP2`);
  - N-8's short column changes for the ten rows of §3;
  - its §0 count "short 249 / 256" becomes 234;
  - the pages comment of N-8 takes the table in §3.
- **Tier C / D:** each label is an edit to `menu.h` (remove the id),
  `MenuLabels.c` (remove the string) and the rows that use it (§4, §5
  lines; the Colloid's filter-envelope rows are at `ColloidParameters.c`
  lines 309–313, `col_fenv_*`).
- **Statements made out of date (for you to log):**
  - `MENU_UI_SPEC.md`, if it lists labels;
  - `SPACE_GROUP_EFFECTS_MODULE_REFERENCE.md` (C1);
  - `EFFECTS_MODULES_DSP_REFERENCE.md` (C2);
  - `TETRAKT_MODULE_REFERENCE.md` (C3–C6);
  - `COLLOID_MODULE_REFERENCE.md` (D);
  - `MEMORY.md`, if it records the label count.
