# S091 P5 — CrumpBit Sub-Type Efficiencies: Implementation Schedule

- **Plan:** `S091_P5_SUBTYPE_EFFICIENCIES.md`: the accepted changes (§4),
  the rejections (§5), the gates (§7), the implementation notes (§8) and
  the user's decisions Q1–Q15 (§10, 2026-11-05; Q9 against the
  recommendation: the `mix` ramp stays).
- **Baseline ("WT"):** the working tree of `dev-ph7-voice_expansion` on
  2026-11-05: HEAD `cc8ca99a` plus the uncommitted S091 P4 implementation
  (`git status` lists the CrumpBit files, EffectsManager.c, menuEffects.c
  and the tools as modified). **Every "WT line" below refers to that tree.**
  Committing P4 first does not move any line.
- **Goal:** every CrumpBit sub-type at or below **170 instructions per
  frame** for the whole Effect, worst case, on the linked image.
- **Result (measured on the scratch build of WT + this schedule):**
  `dly` 160.0, `cmb` 153.8, `pit` 153.6, `grn` 166.5, `rvb` 163.9 (P4:
  174.2 / 170.3 / 183.6 / 203.0 / 194.9). §2.
- **Pre-verified (2026-11-05):** every code block below was applied to a
  copy of WT in the session scratchpad. That copy was:
  - built clean with the firmware flags (`make all`, `make img`: the same
    20 warnings as WT, none new);
  - measured on the linked ELF with the new gates (§2, §5);
  - tested on the host: CB1–CB19, 0 failures; all 18 break builds caught;
    `crumpbit_tables` OK.

  The blocks were copied from the built files by script: a `difflib` map of
  each unit onto WT, and a check that applying every unit to WT, bottom-up,
  rebuilds each built file byte for byte. Nothing was retyped. A second
  check parsed this document itself, applied its units to a fresh copy of
  WT, and got the built files back exactly.
- **Verified tree (session scratch, may not survive a reboot):**
  `/private/tmp/claude-501/-Users-bc-Helicase-Project-Helicase/a5bb168b-4004-4c71-8a79-0d885c1b2068/scratchpad/`:
  `impl/` (WT + this schedule, built), `base/` (WT, built), `v_nog6/` and
  `v_hit/` (the G6 measurement variants), `units.json`, `check_doc.py`.
- **Status:** implemented in the working tree on 2026-11-05 (this session).
  Every unit below was applied, the generator was rerun, and gates 1–6 of
  §5 pass with the measured numbers of §2. §9 records the work step by
  step. Hardware HW1–HW9 (§7) remain open.
- **Constraints:**
  - **RAM:** +260 B `.bss` in SRAM1 (the X2 table 256 B + its key 4 B),
    approved by the user (Q4); owner `CrumpBitEffect.c`. The runtime stays
    100 B; DTCM and the FX arena are unchanged (FXBUF 126,656 B at
    `0x20001140`).
  - **Flash:** +456 B (710,316 B used, headroom 43,348 B).
  - **No backward compatibility:** none needed. No stored key, row index,
    default or maximum changes; `dpn` loses a runtime-only flag.

---

## 0. Decisions applied, and implementation choices

| # | Plan item / decision | Applied as |
|---|---|---|
| A, Q11 | The `grn` Position clamp moves into the engine; the label unchanged | `dpn` row without BUFFER_DEPENDENT (C2); `effective_max` loses `dpn` (E60, E61); the shared fit search (E5); `crumpBit_grainPick()` limits the stop to the fit (E27–E30) |
| B | Integer (Q14) tap read for `dly`/`cmb` | Q14 helpers above the tape loop (E14); the loop (E15); `crumpBit_lineRead()` removed (E13) |
| P1 | `pit` heads without interpolation | `crumpBit_headRead()` (E20, E25) |
| R1, Q10 | `rvb` damping low-pass only; `ton` > 63 shown flat | `crumpBit_runReverb()` (E47–E53); label (C4) |
| G1, Q1 | Dropped | not carried (P4's five draws stay) |
| G3 | Grain window without the lower clamp, done fully | clamp removed (E36); spans rounded to a block boundary (E43–E45); idle voices parked (E42); the P4 age defect fixed (E41) |
| S1, Q9 | `fbk` and `dpn` gains per block; **`mix` keeps its ramp** | tape loop (E14, E15), `pit` (E22, E24), `grn` (E32, E35, E42); `feedback_prev`/`pan_prev` removed (H6, H7) |
| X2, Q4 | `bof`/`biv` through a 256 B table in `.bss` | table, key, rebuild, sentinel (E6, E7); stage 1 (E11, E12); engines (E15, E23, E33, E39, E50, E52); `process` (E58) |
| G4, Q7 | `grn` echo plain, gain 0..0.98 over the row | the echo (E37); the curve (E3, E9, E42); state (H1, H4); label (C4) |
| G6, Q3 | Fit cached in the freed bytes; kept only if ≥ 0.5 per frame | cache (E40) in `fit_key`/`fit` (H1–H5); **measured −1.5 per frame typical (§2.3): kept** |
| G7, Q2 | Dropped | not carried (the two-span loop stays) |
| F1, Q5, Q6, Q12, Q14 | One-pole output filter in `grn`/`rvb`; mapping; generated k; seeding in `enter`; no switch crossfade | type table and helper (E10); stage 4 (E56–E59); hand-over (E54, E55); `crumpBit_onePoleK` (B1, B2, G1–G8, R1); labels (C3, C4, P1) |
| Q8 | CB4 against P2 + Q14 read + stepped gains | reference (X7–X14) |
| Q13 | Voice filters not in P5 | — |
| Q15 | `elf_blockpath.py` and `crumpbit_cpu_check` in the same change | N1, N2, T2, T6; `elf_looppath.py` (L1–L5) |

**Implementation choices made here** (beyond the plan's text):

1. **A P4 defect, found and fixed (E41).** When a trigger is dropped (a
   steal already running) or retried (nothing recorded yet), the voices'
   ages had already been advanced to the event frame; the block-end step
   then added the whole block again. Both voices aged up to 31 frames too
   fast, cutting grains short with a step in their window (a small click
   on P4). Under G3 it moves an end off its block boundary, which the
   clamp-free window cannot tolerate. CB14 (i) found it; break 18 proves
   the law catches it.
2. **The park is by zero gains, not a zero slope (E42).** A parked `out`
   with slope 0 needs a per-voice slope register in the loop; zero gains
   cost nothing per frame and add exactly 0.
3. **Steals are padded to a block boundary (E45).** The plan assumed a
   steal's fade ends on the next event split. That fails when the deferred
   trigger is retried, or takes the other voice. With the pad the stolen
   voice ends, and the deferred trigger fires, at a block start; the window
   holds 1 for the pad (≤ 31 frames).
4. **The span rounding is in `crumpBit_runGrain()`, not
   `crumpBit_grainPick()`** (plan §6): it needs the event frame, and the
   pick stays a pure function (CB14 calls it directly).
5. **`crumpBit_grainPick()` takes the fit as an argument (E29),** for the
   same reason; the engine passes its cached `g->fit`.
6. **One stop-fit search for the label and the DSP (E5),** on one ticks
   expression, so the page names exactly the stop that plays (CB6 checks
   it across shares and tempos).
7. **Stage 4's coefficients move after the engine, one branch per filter
   (E56–E59).** Each branch sets up and runs its own loop, so the per-block
   path through one filter loop cannot include the other's coefficient code.
   That made the prototype's `throughpath.py` unnecessary: N1's `--via-loop`
   is enough. The SVF arithmetic is unchanged (CB4, CB16 bit-exact).
8. **The X2 rebuild is out of line (E6),** so `crumpBit_process()` keeps
   exactly two frame loops for the gates.
9. **`feedback_prev` and `pan_prev` are removed (H7),** not left as dead
   stores; the runtime stays 100 B (padding).
10. **G6 was measured and kept** (§2.3, the plan's rule).
11. **Tools:** `elf_looppath.py` needed three changes (L1–L5): the old loop
    search does not finish on the new `crumpBit_runGrain` (cold blocks whose
    in-range spans reach 1,144 instructions; > 120 s). The search now
    follows only instructions on a cycle through the head: identical
    results (checked on the P4 image, the prototype and all 43 mixer loops),
    0.35 s. Also a disassembly cache and `--rank`. The CPU arithmetic is a
    separate script (`crumpbit_cpu.py`, N2), so `elf_blockpath.py` (N1)
    stays generic.
12. **Not carried from the prototype:** the X3 scaffolding
    (`crumpbit_filt_t`, `crumpBit_filterFrame()`, the block's `filt`
    pointer), the R3 constant `CRUMPBIT_RVB_MIRROR`, the file-static G6 and
    seeding state, G1 and G7, and the re-ordered output stores
    (`{ float yr …; float yl …; }`). Every loop measures at or below the
    prototype + 1 (the kept `mix` ramp) without them.

---

## 1. Change list

| Units | File | What |
|---|---|---|
| H1–H8 | `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | `grn` state: `fit_key`, `reserved`, `fit` (G4, G6); block-aligned ends (G3); runtime: two origin bytes removed (S1), the one-pole state (F1); `grainPosLimit` comment (A) |
| E1–E61 | `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | constants and asserts; the stop-fit helpers (A); the data-line table (X2); the `grn` curve (G4); the one-pole table and helper (F1); stage 1 (X2); the Q14 tape loop (B, S1); `pit` (P1, S1); `grn` (A, G3, G4, G6, S1, the P4 age defect); `rvb` (R1); `enter` (F1); `process` (X2, F1); `effective_max` (A) |
| P1 | `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` | filter-type comment |
| C1–C4 | `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | `dpn` plain row (A); labels (F1, R1, G4) |
| B1–B2, R1 | `CrumpBitTables.h`, generated `CrumpBitTables.c` | `crumpBit_onePoleK` |
| G1–G8 | `tools/plots/gen_crumpbit_tables.py` | the one-pole k law and check C7 |
| L1–L5 | `tools/dsp_test/elf_looppath.py` | cycle-pruned search, disassembly cache, `--rank` |
| N1, N2 | `tools/dsp_test/elf_blockpath.py`, `tools/dsp_test/crumpbit_cpu.py` (new) | per-block path; the 170 cap gate |
| T1–T6 | `tools/dsp_test/Makefile` | breaks 13–18; ELF limits; `crumpbit_cpu_check` |
| X1–X26 | `tools/dsp_test/modules/crumpbit.c` | CB4 reference (Q8); CB6, CB14, CB15, CB16 extended; CB17–CB19 new |
| D1–D7 | documentation | §6 |

**Checked and unchanged:**
- `Core/DSP/Effects/EffectsManager.c`: the BUFFER_DEPENDENT loops are
  generic (lines 372–385, 2088–2107); the runtime guards
  (`sizeof(CrumpBitRuntime) <= sizeof(effects_runtime_t)`,
  `effects_runtime_t == 100`, lines 141 and 153) hold.
- `Core/Menu/menuEffects.c` and `menu.c`: the label hook already runs
  before generic and MENU-dtype formatting.
- `Core/DSPAudio/mixer.c`: unchanged source; its 43 frame loops measure
  identically; `io.frames` is always `OUTPUT_DMA_SIZE` (32), which G3's
  block alignment relies on (mixer.c:1100).
- The firmware `Makefile`: no new source file.

---

## 2. Measured results (scratch build of WT + this schedule)

Counts are instructions on the linked `-Ofast` ELF, longest path, measured
by the new gate (`crumpbit_cpu.py`, N2). The frame loops are
`elf_looppath.py`'s longest paths; "per block" is the longest path through
the function with its frame loops weighted 0 (`elf_blockpath.py`, N1).
Whole Effect per frame = engine loop + filter loop + (engine per block +
`process` per block on that filter's branch + manager) / 32.

### 2.1 Whole CrumpBit Effect, worst case, instructions per frame

| Sub-type | P3 | P4 (WT) | Prototype `ab6` | Projected (plan §2.1a) | **P5 (this schedule)** | Cap |
|---|---:|---:|---:|---:|---:|---|
| `dly` | 172.5 | 174.2 | 158.3 | ≈ 159.3 | **160.0** | ✓ |
| `cmb` | 168.6 | 170.3 | 152.2 | ≈ 153.2 | **153.8** | ✓ |
| `pit` | 181.9 | 183.6 | 151.6 | ≈ 152.6 | **153.6** | ✓ |
| `grn` | 192.3 | 203.0 | 167.5 | ≈ 168.8 | **166.5** | ✓ (3.5 margin) |
| `rvb` | 193.2 | 194.9 | 163.1 | ≈ 164.1 | **163.9** | ✓ |

`grn` beats its projection: the full G3 form and the dropped G7 cost less
in the loop than the prototype's partial forms (116 against 118).

### 2.2 By component

| | `dly` | `cmb` | `pit` | `grn` | `rvb` |
|---|---:|---:|---:|---:|---:|
| Engine frame loop (P4) | 111 (124) | 109 (124) | 106 (135) | 116 (136) | 131 (145) |
| Output filter loop | 31 SVF | 31 SVF | 31 SVF | **16 one-pole** | **16 one-pole** |
| Engine per block (P4) | 129 + 113 Sync search (166 + 113) | 105 (154) | 195 (228) | 814 (736) | 252 (269) |
| `process` per block, this filter's branch | 249 | 249 | 249 | 204 | 204 |
| Manager per block: `effectiveMax` 23 (P4 29) + `tableLimit` 63 | 86 | 86 | 86 | 86 | 86 (P4 `grn`: + `grainPosLimit` 76) |
| **Total** | **160.0** | **153.8** | **153.6** | **166.5** | **163.9** |

- `process` per block includes a sub-type switch's `crumpBit_enter()` (the
  longest path), the X2 key check and the filter coefficients; the X2
  rebuild is a call (60 B, 10 + 256 × 6 instructions) on a `bof`/`biv`
  change only, outside the cap by the plan's decision.
- `grn` per block grew (736 → 814): A's fit cache and its miss branch, the
  G3 rounding, pad and park, and the E41 fix (+8); the loop fell 20.
- No VDIV in any frame loop.

### 2.3 G6 measured on its own (plan Q3)

| `grn` per block | Count | Per frame |
|---|---:|---:|
| Without G6 (the fit searched every block) | 809 | 166.3 total |
| G6, cache hit (tempo and line unchanged: every ordinary block) | 760 | 164.8 total |
| G6, cache miss (worst case, the gated figure) | 814 | 166.5 total |

G6 saves 1.5 per frame on every ordinary block and costs 0.16 on a block
where the tempo or the line changes. The plan's rule (keep if ≥ 0.5): **kept**.

### 2.4 Image

| | WT (P4) | P5 | Change |
|---|---:|---:|---:|
| `.text` | 709,440 | 709,896 | +456 |
| `.data` | 420 | 420 | 0 |
| `.bss` | 413,016 | 413,284 | +268 (table 256 + key 4 + 8 B alignment) |
| Flash used / headroom | 709,860 / 43,804 | 710,316 / 43,348 | +456 |
| ITCM | 15,328 | 15,280 | −48 (LTO setup code in the wavetable blocks; all their loops identical) |
| DTCM statics | 4,392 | 4,396 | +4 (LTO padding; the same symbols and sizes) |
| FXBUF | 126,656 B at `0x20001140` | unchanged | 0 |
| `CrumpBitRuntime` / `effects_runtime_t` | 100 / 100 | 100 / 100 | 0 |
| `crumpBit_dataLines` | — | 256 B at `0x20064ED0` (SRAM1 `.bss`) | new |
| Image (`make img`) | 709,860 B | 710,316 B, `Written … OK` | |
| Warnings (clean build) | 20 | 20, the same set | |

Function sizes (B): `crumpBit_process` 1,264 → 1,544; `runDelay` 1,192 →
964; `runComb` 1,138 → 872; `runPitch` 1,408 → 1,120; `runGrain` 4,336 →
4,504; `runReverb` 1,560 → 1,430; `effectiveMax` 352 → 324;
`uiFormatValue3` 880 → 1,124; new: `buildDataLines` 60,
`crumpBit_onePoleK` 512, `crumpBit_onePoleMixTable` 64.

### 2.5 Host evidence

- CB4 (`dly` against the Q8 reference): bit-exact over 60 s, 82,800
  blocks, loop bytes identical.
- CB8 (`dly` at 1.2): peak 1.000, mean loop code within ±0.06 LSB.
- CB15: decay 15.24 dB over 0.125 s (law 15.2), freeze 2.07 dB; R1: eight
  random `ton` values above 63 bit-identical to 63.
- CB18 (`grn` echo): settles at 0, 0, 2 and 25 codes for `fbk` 0, 64, 100,
  127 (floors 0.5, 0.99, 2.19, 25.0); the peak never grows.
- CB19: one-pole −3.01 dB and SVF −3.00/−3.02/−3.01 dB at the SVF's −3 dB
  frequency for `frq` 50, 70, 90 (423, 777, 1,458 Hz); the type mapping
  and `res` bit-exact; the hand-over exact, the step at a switch 0.013
  (steady 0.008).
- CB14 (i): 0 voices off a block boundary over 40,000 blocks with 815 steal
  blocks (276 before E41).
- All 18 breaks caught (12 existing, unchanged patterns; 6 new).

**Not run here:** `make -C tools/dsp_test all`. S091 P4 §2 records that it
stops at `voice_control` and `run_modes` on its baseline for reasons
outside CrumpBit; this schedule ran the CrumpBit targets directly
(`crumpbit`, `crumpbit_tables`, `crumpbit_elf_check`,
`crumpbit_cpu_check`).

---

## 3. Firmware changes

How to read a unit:

- **Location** gives the WT line range and the action: **modify** (replace
  WT lines a–b with the new text), **add** (insert after WT line a), or
  **remove**. "New lines" are the lines the text occupies in the finished
  file, for checking. Apply the units of a file **bottom-up** (highest WT
  line first) and every WT line number stays valid until its unit is
  applied.
- The description gives what the change does, why it must exist, its
  inputs and outputs where it has them, its accessors and affiliated code,
  and the host law that proves it.
- "Current text" is the WT text being replaced (shown when it is 24 lines
  or fewer), so each unit can be checked before it is applied.
- "New text" is exact. The comment blocks inside it are part of the change:
  they document variables, mathematical statements and loops in place, not
  only function heads.

### 3.1 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`

#### H1 — `grn` state comment: `fit_key` and `reserved` replace the DC blocker

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, WT lines 106–106: **modify** (replace 1 line with 8; new lines 106–113).

- **What:** the field list of `CrumpBitGrainState` loses `dc_x, dc_y` (the
  delay line's DC blocker, removed by G4) and gains:
  - `fit_key`: BPM | N << 16, the tempo and line length the cached Position
    fit belongs to (G6). The zeroed key never matches a running line
    (N ≥ `CRUMPBIT_GRAIN_LINE_MIN` = 512), so every fresh state recomputes;
  - `reserved`: the other 4 B, free, so the state stays 44 B.
- **Why:** Q3 put the G6 cache into the bytes G4 frees, with no runtime
  growth. Keeping 44 B keeps `sizeof(CrumpBitRuntime) == 100` and
  `effects_runtime_t == 100` (EffectsManager.c:153), so no other layout
  moves.
- **Accessors:** `crumpBit_runGrain()` (E40). **Affiliates:** H4 (the
  fields), H5 (`fit`), E4 (the 16-bit end assert).

Current text (WT 106–106):

```c
 * - dc_x, dc_y: the delay line's feedback DC blocker;
```

New text:

```c
 * - fit_key (S091 P5 G6, in the 4 B the delay line's DC blocker `dc_x`
 *   held until G4 removed it): the tempo and line length the cached
 *   Position fit was worked out for, BPM | N << 16. A line is at least
 *   CRUMPBIT_GRAIN_LINE_MIN long while the engine runs, so the zeroed key
 *   (enter, init, the manager's union clear) never matches and the first
 *   block works the fit out;
 * - reserved (S091 P5): the 4 B of G4's former `dc_y`, free; kept so the
 *   state stays 44 B and the runtime 100 B (no layout change elsewhere);
```

#### H2 — `grn` state comment: block-aligned ends and the steal pad

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, WT lines 114–115: **modify** (replace 2 lines with 5; new lines 121–125).

- **What:** the `end_a`/`end_b` description adds the G3 rules: a steal sets
  `end = age + 128 + pad`, the window holding 1 for `pad` frames; every end
  lands on a block boundary.
- **Why:** the frame loop drops the window's lower clamp (E36) only because
  no sounding voice can pass its end inside a block; the state comment is
  where that invariant is stated for every reader of `end`.
- **Affiliates:** E43 (trigger rounding), E45 (steal pad), host law CB14 (i).

Current text (WT 114–115):

```c
 *   seamless, plan A1.9). A steal sets end = age + 128 (the fade-out starts
 *   at once). A voice is free once age >= end;
```

New text:

```c
 *   seamless, plan A1.9). A steal sets end = age + 128 + pad (the
 *   fade-out starts at once, from a held 1 for `pad` frames). S091 P5 G3:
 *   every end lands on a block boundary (a grain's span is rounded down, a
 *   steal's pad rounds its end up), so a voice never runs past its end
 *   inside a block. A voice is free once age >= end;
```

#### H3 — `grn` state comment: `fit` replaces the alignment byte

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, WT lines 124–124: **modify** (replace 1 line with 3; new lines 134–136).

- **What:** `spare` becomes `fit`, the cached count of rhythmic stops
  (0..16, shortest first) that fit one line at the tempo in `fit_key`.
- **Why:** G6 (Q3: the fit lives in `spare`).
- **Accessors:** `crumpBit_runGrain()` writes it on a key change (E40) and
  passes it to `crumpBit_grainPick()` (E41); host law CB6 reads it.

Current text (WT 124–124):

```c
 * - spare: one byte of alignment.
```

New text:

```c
 * - fit (S091 P5 G6, the former alignment byte): the cached Position fit,
 *   how many rhythmic stops (0..16, shortest first) fit one line at the
 *   tempo in fit_key (crumpBit_grainStopFit()).
```

#### H4 — `grn` state fields: `fit_key`, `reserved`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, WT lines 134–135: **modify** (replace 2 lines with 2; new lines 146–147).

- **What:** `float dc_x; float dc_y;` → `uint32_t fit_key; uint32_t
  reserved;` at the same offsets (12 and 16).
- **Why:** G4 removes the echo's DC blocker (its memories are dead) and G6
  needs a 4 B key. Same size, same alignment: the union stays 44 B and the
  static asserts at CrumpBitEffect.c:154–157 hold unchanged.

Current text (WT 134–135):

```c
    float dc_x;
    float dc_y;
```

New text:

```c
    uint32_t fit_key;
    uint32_t reserved;
```

#### H5 — `grn` state field: `fit`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, WT lines 149–149: **modify** (replace 1 line with 1; new lines 161–161).

- **What:** `uint8_t spare;` → `uint8_t fit;`.
- **Why:** G6 (Q3). The byte was alignment padding; nothing else read it.

Current text (WT 149–149):

```c
    uint8_t spare;
```

New text:

```c
    uint8_t fit;
```

#### H6 — Runtime comment: 54 B shared, the one-pole state, `*_prev`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, WT lines 180–188: **modify** (replace 9 lines with 15; new lines 192–206).

- **What:**
  - the shared part is now 54 B used plus 2 B padding before the union (H7
    removes two bytes; the union stays at offset 56 because it holds 32-bit
    members, so the runtime stays 100 B);
  - `svf_*`: in `grn`/`rvb` (F1) the one-pole state is `svf_l1`/`svf_r1`,
    `svf_*2` rest; `crumpBit_enter()` moves the state on a switch (E55);
  - `*_prev`: only the mix stage and the output filter keep ramp origins;
    S1 removed `feedback_prev` and `pan_prev`.
- **Why:** the runtime comment is the record of what every field means;
  the F1 reuse of `svf_l1/r1` and the removal of two fields must be stated
  there (STORAGE_SRAM_MANIFEST.md mirrors it, D4).

Current text (WT 180–188):

```c
 * Shared part (56 B):
 * - ac_y_*, ac_c_*: stage 1 AC coupling (outputs; previous codes, int16,
 *   seeded 128 by init);
 * - svf_*: the output filter's two TPT integrators per channel (stage 4);
 * - row bytes, written by write_param() whatever the sub-type: their
 *   meaning changes per sub-type, their storage does not (plan §2.2);
 * - subtype: the resolved `sub` row; active_subtype: the engine that ran
 *   last block. A difference triggers crumpBit_enter() (plan §2.4);
 * - *_prev: the raw rows the previous block ramped to (ramp origins);
```

New text:

```c
 * Shared part (54 B used, 2 B padding before the union since S091 P5):
 * - ac_y_*, ac_c_*: stage 1 AC coupling (outputs; previous codes, int16,
 *   seeded 128 by init);
 * - svf_*: the output filter's two TPT integrators per channel (stage 4).
 *   S091 P5 F1: in `grn` and `rvb` the filter is a one-pole whose state is
 *   svf_l1/svf_r1 (svf_*2 rest); crumpBit_enter() moves the state between
 *   the two forms on a switch;
 * - row bytes, written by write_param() whatever the sub-type: their
 *   meaning changes per sub-type, their storage does not (plan §2.2);
 * - subtype: the resolved `sub` row; active_subtype: the engine that ran
 *   last block. A difference triggers crumpBit_enter() (plan §2.4);
 * - *_prev: the raw rows the previous block ramped to (ramp origins: the
 *   mix stage and the output filter's type and resonance). S091 P5 S1 removed
 *   feedback_prev and pan_prev: `fbk` and `dpn` take their target at the
 *   block start, so they have no origin to keep;
```

#### H7 — Runtime fields: remove `feedback_prev` and `pan_prev`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, WT lines 230–231: **remove** 2 lines (nothing replaces them; the text after them starts at new line 248).

- **What:** removes the two ramp-origin bytes.
- **Why:** S1 steps `fbk` and the `dpn` gains once per block on the
  current target, so nothing reads an origin. Leaving them would keep two
  dead stores per block in three engines (E16, E17, E19, E26, E46 remove
  the writes). Size is unchanged (padding; asserted 100 B).
- **Affiliates:** host code does not reference them (checked:
  `tools/dsp_test/modules/crumpbit.c`, EffectsManager.c, menu code).

Removed text:

```c
    uint8_t feedback_prev;     /* sub-type ramp origin */
    uint8_t pan_prev;          /* sub-type ramp origin */
```

#### H8 — `crumpBit_grainPosLimit()` declaration comment: label only

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h`, WT lines 326–326: **modify** (replace 1 line with 4; new lines 342–345).

- **What:** adds that since A the function serves the page label and host
  law CB6; the DSP clamps with the same fit itself, so `dpn` is no longer
  BUFFER_DEPENDENT and the manager never calls it per block.
- **Why:** the old comment implied a manager clamp that no longer exists.
- **Accessors:** `crumpBit_uiFormatValue3()` (CrumpBitParameters.c),
  CB6. **Affiliates:** `crumpBit_grainStopFit()` (E5).

Current text (WT 326–326):

```c
 * or below 63 (follow, scatter) are never limited (plan A1.8, A3.4).
```

New text:

```c
 * or below 63 (follow, scatter) are never limited (plan A1.8, A3.4). S091
 * P5 A: the page label and host law CB6 use it; the DSP clamps the stop
 * itself with the same fit (crumpBit_grainStopFit()), so `dpn` is no longer
 * BUFFER_DEPENDENT and the manager never clamps it.
```

### 3.2 `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`

Units E1–E61 in file order. The larger changes, by plan item:

| Item | Units |
|---|---|
| A (Position clamp in the engine) | E5, E27–E30, E40, E41 (fit pass), E60, E61 |
| B (Q14 tap read) | E13, E14, E15 |
| P1 (`pit` drop-sample) | E20, E21, E25 |
| R1 (`rvb` low-pass damping) | E47–E53 |
| G3 (no window lower clamp) | E2, E4, E34, E36, E38, E41 (age defect), E42 (park), E43–E45 |
| S1 (`fbk`/`dpn` per block) | E8, E14–E19, E22, E24, E26, E32, E35, E42, E46 |
| X2 (data-line table) | E6, E7, E10 (comment), E11, E12, E15, E23, E33, E35, E39, E50, E52, E58 |
| G4 (plain `grn` echo) | E2, E3, E9, E13, E31, E32, E34, E37, E42, E46 |
| G6 (fit cache) | E40 |
| F1 (one-pole output filter) | E1, E10, E54, E55, E56–E59 |

#### E1 — File header: stage 4's two forms; the 170 cap

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 17–19: **modify** (replace 3 lines with 7; new lines 17–23).

- **What:** the four-stage summary now says stage 4 is the resonant TPT SVF
  in `dly`/`cmb`/`pit` and a one-pole in `grn`/`rvb` (F1), and records the
  user's cap (170 per frame, every sub-type, worst case) and its gate
  (`crumpbit_cpu_check`).
- **Why:** the file header is the first thing a later session reads; the
  cap is a binding constraint on every future change to this file.

Current text (WT 17–19):

```c
 *   4 shared   TPT output filter, a second pass
 * Within a sub-type every statement runs on every frame whatever its
 * settings (constant CPU); the sub-type is chosen once per block.
```

New text:

```c
 *   4 shared   output filter, a second pass: the resonant TPT SVF in
 *              dly | cmb | pit, a one-pole in grn | rvb (S091 P5 F1)
 * Within a sub-type every statement runs on every frame whatever its
 * settings (constant CPU); the sub-type is chosen once per block.
 * S091 P5 caps every sub-type at 170 instructions per frame for the whole
 * Effect, worst case (S091_P5_SUBTYPE_EFFICIENCIES.md); crumpbit_cpu_check
 * gates it on the linked image.
```

#### E2 — Constants comment: block boundary; `CRUMPBIT_GRAIN_FB_MAX`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 121–125: **modify** (replace 5 lines with 11; new lines 125–135).

- **What:** documents two constant roles:
  - `CRUMPBIT_GRAIN_MIN_PERIOD` (32, one block) is also the boundary grain
    ends are rounded to (G3);
  - `CRUMPBIT_GRAIN_FB_MAX` (0.98): the `grn` echo gain at `fbk` 127. With
    no knee and no DC blocker (G4) the gain must stay below 1; the line
    decays to the 8-bit rounding floor |code| ≤ 0.5 / (1 − gain) (25 codes
    at 0.98, measured by CB18).
- **Why:** these two numbers carry the G3 and G4 safety arguments.

Current text (WT 121–125):

```c
 *   loop runs as two spans around it;
 * - CRUMPBIT_GRAIN_LAG0 the lag at Position 0: two frames behind the write
 *   head (the newest written sample is one frame back);
 * - CRUMPBIT_GRAIN_DETUNE_CENTS the detune at `spr` 127 (plan A1.5: +-1
 *   semitone), applied through crumpBit_ratioOf().
```

New text:

```c
 *   loop runs as two spans around it; S091 P5 G3: also the boundary every
 *   grain end is rounded to;
 * - CRUMPBIT_GRAIN_LAG0 the lag at Position 0: two frames behind the write
 *   head (the newest written sample is one frame back);
 * - CRUMPBIT_GRAIN_DETUNE_CENTS the detune at `spr` 127 (plan A1.5: +-1
 *   semitone), applied through crumpBit_ratioOf();
 * - CRUMPBIT_GRAIN_FB_MAX (S091 P5 G4) the `grn` echo gain at `fbk` 127.
 *   The echo has no soft knee and no DC blocker since G4, so its gain stays
 *   below 1 and the line always decays (to the 8-bit line's rounding
 *   floor, |code| < 0.5 / (1 - gain)); `fbk` spreads 0..0.98 over the whole
 *   row (crumpBit_grainFeedbackGain(), plan Q7).
```

#### E3 — Constant: `CRUMPBIT_GRAIN_FB_MAX`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, after WT line 140: **add** 1 line (new lines 151–151).

- **What:** `#define CRUMPBIT_GRAIN_FB_MAX 0.98f`.
- **Accessors:** `crumpBit_grainFeedbackGain()` (E9), host law CB18.

New text:

```c
#define CRUMPBIT_GRAIN_FB_MAX         0.98f
```

#### E4 — Static asserts: the steal pad fits `uint16_t`; the block mask

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 158–161: **modify** (replace 4 lines with 17; new lines 169–185).

- **What:**
  - the 16-bit end assert widens to `N + 2·fade + block`: a steal sets
    `end = age + 128 + pad` on a voice that may already be fading out
    (age < its end ≤ N + 128), and the pad is under one block;
  - a new assert that `CRUMPBIT_GRAIN_MIN_PERIOD` is a power of two, since
    G3 rounds with a mask.
- **Why:** both are compile-time guarantees the G3 arithmetic relies on;
  63,328 + 256 + 32 < 0xFFFF holds with room.

Current text (WT 158–161):

```c
/* The grain line fits 16-bit indices with room for a grain's end. */
_Static_assert(CRUMPBIT_LOOP_MAX_BYTES / 2u + CRUMPBIT_GRAIN_FADE <
               CRUMPBIT_GRAIN_NO_ANCHOR,
               "grn ends (L + fade) and the no-anchor marker fit uint16_t");
```

New text:

```c
/*
 * The grain line fits 16-bit indices with room for a grain's end: L + fade
 * for a grain, and (S091 P5 G3) a steal's age + fade + its block pad
 * (< one block) on a voice that may already be fading out.
 */
_Static_assert(CRUMPBIT_LOOP_MAX_BYTES / 2u + 2u * CRUMPBIT_GRAIN_FADE +
               CRUMPBIT_GRAIN_MIN_PERIOD < CRUMPBIT_GRAIN_NO_ANCHOR,
               "grn ends (L + fade, steal pad) and the no-anchor marker fit "
               "uint16_t");
/*
 * S091 P5 G3: grain ends are rounded to block boundaries with a mask, so the
 * block (CRUMPBIT_GRAIN_MIN_PERIOD = OUTPUT_DMA_SIZE, the only block size
 * mixer.c passes) must be a power of two.
 */
_Static_assert((CRUMPBIT_GRAIN_MIN_PERIOD &
                (CRUMPBIT_GRAIN_MIN_PERIOD - 1u)) == 0u,
               "the grn block-boundary mask needs a power-of-two block");
```

#### E5 — `crumpBit_grainStopFit()`, `crumpBit_grainMaxTicks()`; `crumpBit_grainPosLimit()` on them

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 372–392: **modify** (replace 21 lines with 53; new lines 396–448).

- **What:**
  - `crumpBit_grainStopFit(max_ticks)`: the S091 P4 five-probe search,
    moved out of `crumpBit_grainPosLimit()` unchanged: how many of the 16
    stops fit, 0..16;
  - `crumpBit_grainMaxTicks(max_samples, bpm)`: the longest lag in ticks,
    `max_samples · bpm · (1 / samples-per-tick)`, the exact P4 expression;
  - `crumpBit_grainPosLimit()` becomes `63 + 4 · fit` over those two.
- **Why (A, G6):** the engine now clamps Position itself. The page label
  must show the stop the DSP plays, so both use one search on one
  expression. `runGrain` passes `N − 2`, which equals
  `crumpBit_grainMaxSamples()` for the same share, so the fits are
  bit-identical (CB6 checks it across shares and three tempos).
- **Inputs:** ticks (float). **Output:** 0..16.
- **Callers:** `crumpBit_grainPosLimit()` (label), `crumpBit_runGrain()`
  (E40, on a key change only). **Affiliates:** `crumpBit_grainStopCc`,
  `crumpBit_grainTime`.

Current text (WT 372–392):

```c
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
```

New text:

```c
/*
 * How many `grn` rhythmic stops fit one line (S091 P4 search; shared since
 * S091 P5 A).
 *
 * What: the count of stops k (shortest first) whose delay time
 * crumpBit_grainTime[crumpBit_grainStopCc[k]] is at most max_ticks, 0..16.
 * The stop lengths ascend, so the fitting ones are a prefix; the search
 * builds the count from the top bit down (8, 4, 2, 1: fixed probes,
 * compare-selects), then a fifth probe decides the 16th stop, as
 * crumpBit_tableLimit() does for 128.
 * Why one helper: the Position label (crumpBit_grainPosLimit(), through the
 * page) and the DSP's clamp (crumpBit_runGrain(), S091 P5 A) must agree to
 * the stop, so both run this search on the same max_ticks expression
 * (crumpBit_grainMaxTicks()).
 * Input: the longest lag one line holds, in 96-PPQ ticks. Output: 0..16.
 * Callers: crumpBit_grainPosLimit(), crumpBit_runGrain() (on a tempo or
 * line change only, G6). Affiliates: crumpBit_grainStopCc, host law CB6.
 */
static inline uint32_t crumpBit_grainStopFit(float max_ticks)
{
    /* fit: the stops known to fit so far (a prefix of the 16). */
    uint32_t fit = 0u;
    uint32_t step;

    for (step = CRUMPBIT_GRAIN_STOPS / 2u; step != 0u; step >>= 1u)
        fit += (crumpBit_grainTime[crumpBit_grainStopCc[fit + step - 1u]] <=
                max_ticks) ? step : 0u;
    /* Fifth probe: stop `fit` (<= 15, in range); at 15 it decides the 16th. */
    fit += (crumpBit_grainTime[crumpBit_grainStopCc[fit]] <= max_ticks)
        ? 1u : 0u;
    return fit;
}

/*
 * The longest `grn` lag of one line in ticks: (N - guard) x BPM / samples
 * per tick at 1 BPM. One expression for the label and the DSP (S091 P5 A),
 * so their fits are bit-identical. Inputs: the line's longest lag in samples
 * (crumpBit_grainMaxSamples(), or N - CRUMPBIT_LOOP_GUARD in the engine),
 * bpm (0 treated as 1). Output: ticks.
 */
static inline float crumpBit_grainMaxTicks(float max_samples, uint16_t bpm)
{
    return max_samples * (float)(bpm ? bpm : 1u) *
           (1.0f / CRUMPBIT_SAMPLES_PER_TICK);
}

uint8_t crumpBit_grainPosLimit(const fx_share_t *share)
{
    /* fit: the stops that fit one half line at the current tempo. */
    const uint32_t fit = crumpBit_grainStopFit(
        crumpBit_grainMaxTicks(crumpBit_grainMaxSamples(share),
                               seq_getBpm()));

```

#### E6 — X2: the data-line table, its key, `crumpBit_buildDataLines()`, `crumpBit_dataLinesFor()`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, after WT line 598: **add** 66 lines (new lines 655–720).

- **What:**
  - `static uint8_t crumpBit_dataLines[256]`: code c → `(c & keep) ^ inv`;
  - `static uint32_t crumpBit_dataLinesKey`: `bof | biv << 8`, or
    `CRUMPBIT_DATA_LINES_NONE` (0xFFFFFFFF) before the first build;
  - `crumpBit_buildDataLines(key)`: the 256-entry rebuild, **out of line**
    (`CRUMPBIT_NOINLINE`);
  - `crumpBit_dataLinesFor(rt)`: compares the key once per block and
    rebuilds only on a change.
- **Why:** stage 1 reads one byte per channel instead of AND + EOR, which
  frees a register in every engine loop (measured −1 to −3 per frame).
  Out of line, the rebuild's loop is not one of `crumpBit_process()`'s
  frame loops, so the ELF and CPU gates see exactly the two filter loops;
  its cost (≈ 48 per frame for one block on a change) is outside the cap by
  the plan's decision (X2).
- **RAM (approved, Q4):** 256 + 4 B `.bss` in SRAM1, owner
  `CrumpBitEffect.c`; measured at `0x20064ED0`. Not DTCM: every DTCM byte
  is FX arena.
- **Why a sentinel:** both rows span 0..255, so every 16-bit key is real; a
  zeroed `.bss` key would match `bof 0 / biv 0` against an all-zero table
  and turn every code into 0. `crumpBit_init()` sets the sentinel (E7).
- **Accessors:** `crumpBit_process()` (E58), `crumpBit_init()` (E7), host
  law CB17. **Affiliates:** `crumpBit_stage1()` (E12).

New text:

```c
 * Stage 1's data-line table (S091 P5 X2; RAM approved by the user, Q4).
 *
 * What:
 * - crumpBit_dataLines[c]: the 8-bit ADC code c with the forced-off data
 *   lines cleared and the inverted ones flipped, (c & keep) ^ invert, for
 *   every code 0..255 (invert wins over off, spec §3.2: keep clears only the
 *   off bits that are not also inverted). Stage 1 reads one byte per
 *   channel instead of an AND and an EOR, which frees a register in every
 *   engine loop (measured -1 to -3 per frame);
 * - crumpBit_dataLinesKey: the pair the table holds, bit_off | bit_invert
 *   << 8, or CRUMPBIT_DATA_LINES_NONE before the first build. Both rows
 *   span 0..255, so every 16-bit key is a real pair and a zeroed key would
 *   wrongly match bof 0 / biv 0 against an all-zero table: crumpBit_init()
 *   sets the sentinel, so the first block always builds.
 * Why a rebuild on change only: `bof`/`biv` are neither Morphable nor LFO
 * targets; the page or an FX lane changes them at most once per step. A
 * rebuild costs about 48 instructions per frame for that one block (256
 * stores, out of line), outside the per-frame cap by decision (plan X2).
 * Placement: .bss in SRAM1 (256 + 4 B, write-through cached), not DTCM,
 * where every byte would come out of the FX arena (CrumpBit delay).
 * One table serves the one live CrumpBit runtime; because the key is
 * checked every block, several runtimes (the host tests) stay correct and
 * only rebuild more often.
 * Accessors: crumpBit_dataLinesFor() (process, per block),
 * crumpBit_buildDataLines() (the rebuild), crumpBit_init() (sentinel), host
 * law CB17. Affiliates: crumpBit_stage1(), STORAGE_SRAM_MANIFEST.md.
 */
#define CRUMPBIT_DATA_LINES_NONE   0xFFFFFFFFu
static uint8_t crumpBit_dataLines[256];
static uint32_t crumpBit_dataLinesKey;

/*
 * Rebuild the data-line table for one key (out of line: it runs only on a
 * `bof`/`biv` change, so its 256-iteration loop stays out of
 * crumpBit_process()'s per-block path and loop gate).
 * Inputs: key = bit_off | bit_invert << 8. Output: crumpBit_dataLines and
 * its key. Caller: crumpBit_dataLinesFor().
 */
static CRUMPBIT_NOINLINE void crumpBit_buildDataLines(uint32_t key)
{
    /* inv: the inverted lines; keep: every line except off-and-not-inverted. */
    const uint32_t inv = key >> 8;
    const uint32_t keep = (uint32_t)~((key & 0xFFu) & ~inv) & 0xFFu;
    uint32_t c;

    for (c = 0u; c < 256u; c++)
        crumpBit_dataLines[c] = (uint8_t)((c & keep) ^ inv);
    crumpBit_dataLinesKey = key;
}

/*
 * The data-line table for this block's `bof`/`biv` (S091 P5 X2): rebuilt
 * only when the pair differs from the one it holds.
 * Input: runtime. Output: the 256-entry table. Caller: crumpBit_process().
 */
static inline const uint8_t *crumpBit_dataLinesFor(const CrumpBitRuntime *rt)
{
    const uint32_t key = (uint32_t)rt->bit_off |
                         ((uint32_t)rt->bit_invert << 8);

    if (key != crumpBit_dataLinesKey)
        crumpBit_buildDataLines(key);
    return crumpBit_dataLines;
}

/*
```

#### E7 — `crumpBit_init()`: the data-line sentinel

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, after WT line 652: **add** 5 lines (new lines 775–779).

- **What:** `crumpBit_dataLinesKey = CRUMPBIT_DATA_LINES_NONE;` at the end
  of init.
- **Why:** no table is trusted across an Effect entry; the first block
  builds it for the rows the forced resolution has written. CB17 corrupts
  the table and key before an init and checks the first block rebuilt it.

New text:

```c
    /*
     * S091 P5 X2: no table is trusted across an entry; the first block
     * builds it for the rows the forced resolution wrote.
     */
    crumpBit_dataLinesKey = CRUMPBIT_DATA_LINES_NONE;
```

#### E8 — Gain helpers comment: only `mix` ramps; the `grn` curve

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 795–811: **modify** (replace 17 lines with 25; new lines 922–946).

- **What:** rewrites the comment over `crumpBit_mixGain()` …
  `crumpBit_panGainR()`:
  - `mix` keeps its two calls per block (origin and target) and its
    per-frame ramp (Q9, user);
  - feedback and the balance gains are one value per block (S1);
  - the new `grn` curve (G4, Q7) and its label (100 × gain).
- **Why:** the helpers' callers and timing changed; the comment states the
  ramp policy per row.

Current text (WT 795–811):

```c
 * Ramp targets of the delay rows (S091 P2 §8: the ramp origins are no longer
 * stored as floats).
 *
 * What: the per-block conversion of one raw row to its gain, as one pure
 * function each. process() calls each twice per block, once on the previous
 * block's raw byte (the ramp origin, which S074 stored as the previous
 * target) and once on the current one (the new target). Because the function
 * and its input are the same, the origin is bit-identical to the float S074
 * stored, and RAM drops by 13 B net.
 * - mix: row/127, 0..1;
 * - feedback (plan Q3, §0A D6): 0..100 -> 0..1.0 linear, then
 *   100..127 -> 1.0..1.2. 100 * (1/100) rounds to exactly 1.0f;
 * - delay pan balance centred on 63 (S075 F2-E, F2-Q4): 63 is both-side
 *   unity and displays 0, 0 is right silent, and 127 is left silent. This
 *   matches the Effect return stereo law in mixer.c.
 * Inputs: raw row 0..127 (the descriptor maximum). Output: gain.
 * Callers: crumpBit_process(). Affiliates: crumpBit_descriptors.
```

New text:

```c
 * Gains of the delay rows (S091 P2 §8: the ramp origins are no longer stored
 * as floats; S091 P5 S1: only `mix` still ramps).
 *
 * What: the per-block conversion of one raw row to its gain, as one pure
 * function each.
 * - mix: row/127, 0..1. process() calls it twice per block, once on the
 *   previous block's raw byte (the ramp origin, which S074 stored as the
 *   previous target) and once on the current one (the new target); the
 *   function and its input being the same, the origin is bit-identical to
 *   the float S074 stored. The mix stage keeps this per-frame ramp in every
 *   sub-type (plan Q9, user: it is the most exposed control);
 * - feedback (plan Q3, §0A D6): 0..100 -> 0..1.0 linear, then
 *   100..127 -> 1.0..1.2. 100 * (1/100) rounds to exactly 1.0f. S091 P5
 *   S1: applied as one value for the whole block (the target), no ramp;
 * - `grn` feedback (S091 P5 G4, plan Q7): row x 0.98/127, linear over the
 *   whole row, so 127 is exactly CRUMPBIT_GRAIN_FB_MAX and the echo always
 *   decays; the other sub-types keep the 0..1.2 curve on the same row;
 * - delay pan balance centred on 63 (S075 F2-E, F2-Q4): 63 is both-side
 *   unity and displays 0, 0 is right silent, and 127 is left silent. This
 *   matches the Effect return stereo law in mixer.c. S091 P5 S1: one value
 *   per block, no ramp.
 * Inputs: raw row 0..127 (the descriptor maximum). Output: gain.
 * Callers: crumpBit_process() (mix), the crumpBit_run*() engines (the
 * others). Affiliates: crumpBit_descriptors, crumpBit_uiFormatValue3() (the
 * `grn` `fbk` label shows 100 x the `grn` gain).
```

#### E9 — `crumpBit_grainFeedbackGain()`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, after WT line 823: **add** 6 lines (new lines 959–964).

- **What:** `raw · (0.98 / 127)`: linear over the whole row, 127 → exactly
  `CRUMPBIT_GRAIN_FB_MAX` (`0.98f / 127.0f · 127.0f` rounds to `0.98f`;
  CB18 checks it bit for bit).
- **Why (Q7):** with the 0..1.2 curve, rows above the unity row would all
  play the 0.98 cap; the `grn`-only mapping keeps the whole knob usable.
  Other sub-types keep `crumpBit_feedbackGain()`.
- **Input:** row 0..127. **Output:** 0..0.98. **Caller:**
  `crumpBit_runGrain()` (E42). **Affiliates:** C4 (`fbk` label 0..98).

New text:

```c
}

static inline float crumpBit_grainFeedbackGain(uint8_t raw)
{
    /* 127 x (0.98 / 127) rounds to 0.98f (host law CB18 checks it). */
    return (float)raw * (CRUMPBIT_GRAIN_FB_MAX / 127.0f);
```

#### E10 — F1 type table and helper; block-context comment

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 911–918: **modify** (replace 8 lines with 49; new lines 1052–1100).

This unit inserts after `crumpBit_filterMix()` and rewrites the comment
over `crumpbit_block_t` (they are adjacent).

- **`crumpBit_onePoleMixTable[8][2]`:** (cx, cl) per stored `typ`:
  LP, BP, UBP, LP2 → low-pass (0, 1); HP, notch → high-pass x − s (1, −1);
  peak, off → flat (1, 0).
  - **Why (Q5):** `typ` is a shared row; its stored value survives sub-type
    switches, so every value needs a meaning in `grn`/`rvb`.
  - **Why a table:** branch-free per block, and no jump-table data inside
    `process` for the loop gate to misread (the same reason as
    `crumpBit_filterMixTable`).
- **`crumpBit_filterIsOnePole(subtype)`:** nonzero for `grn` and `rvb`.
  One definition for stage 4 (E59) and the hand-over (E55).
- **`crumpbit_block_t` comment:** `lines` (X2) replaces the masks.
- **Accessors:** `crumpBit_process()`, `crumpBit_enter()`, host law CB19.

Current text (WT 911–918):

```c
 * One block's shared context (S091 P3 §2.1).
 *
 * What: what crumpBit_process() resolves once per block and every sub-engine
 * reads: the io channels (right aliases left for a mono block), the frame
 * count, the stage-1 data-line masks, the ramp step, the mix stage's ramp
 * (origin and step; the mix stage owns them, engines only apply them), the
 * arena base and the seated length in the engine's own unit (bytes, line
 * samples or words).
```

New text:

```c
 * The one-pole output filter of `grn` and `rvb` (S091 P5 F1).
 *
 * What: in these two sub-types stage 4 is a one-pole per channel,
 * s += k (x - s), out = cx x + cl s, instead of the resonant TPT SVF:
 * - k from crumpBit_onePoleK[] at the channel's cutoff index (`frq` with the
 *   `bal` spread, as the SVF), matched so the one-pole's -3 dB point is the
 *   SVF's at `res` 0 (plan Q6; generated);
 * - every `typ` value keeps a meaning, because the row is shared and its
 *   stored value survives sub-type switches (plan Q5): LP, LP2, BP, UBP ->
 *   low-pass (cx 0, cl 1); HP, notch -> high-pass x - s (1, -1); peak, off
 *   -> flat (1, 0). The page shows the mapped type (LP/HP/off);
 * - `res` has no effect (the page shows `---`); the type crossfade still
 *   ramps over one block, as the SVF's.
 * Why: the SVF loop is 31 instructions per frame, the one-pole 16; `grn` and
 *   `rvb`, the two dearest engines, could not otherwise reach the 170 cap
 *   (S091_P5_SUBTYPE_EFFICIENCIES.md §2).
 * Index: CRUMPBIT_FILTER_* (MENU_FILTER order). Columns: cx, cl.
 * Accessors: crumpBit_process() (stage 4), crumpBit_filterIsOnePole() (also
 * crumpBit_enter()'s state hand-over). Affiliates: crumpBit_onePoleK,
 * crumpBit_uiFormatValue3() (`typ`/`res` labels), host law CB19.
 */
static const float crumpBit_onePoleMixTable[CRUMPBIT_FILTER_OFF + 1u][2] = {
    /*            cx     cl  */
    /* LP   */ {  0.0f,  1.0f },
    /* HP   */ {  1.0f, -1.0f },
    /* BP   */ {  0.0f,  1.0f },
    /* UBP  */ {  0.0f,  1.0f },
    /* Nch  */ {  1.0f, -1.0f },
    /* Pek  */ {  1.0f,  0.0f },
    /* LP2  */ {  0.0f,  1.0f },
    /* off  */ {  1.0f,  0.0f },
};

/* Nonzero when a sub-type's output filter is the one-pole (`grn`, `rvb`). */
static inline uint32_t crumpBit_filterIsOnePole(uint8_t subtype)
{
    return (subtype == CRUMPBIT_SUBTYPE_GRAIN ||
            subtype == CRUMPBIT_SUBTYPE_REVERB) ? 1u : 0u;
}

/*
 * One block's shared context (S091 P3 §2.1).
 *
 * What: what crumpBit_process() resolves once per block and every sub-engine
 * reads: the io channels (right aliases left for a mono block), the frame
 * count, the stage-1 data-line table (S091 P5 X2: `lines`, replacing the
 * keep/invert masks), the ramp step, the mix stage's ramp (origin and step;
 * the mix stage owns them, engines only apply them), the arena base and the
 * seated length in the engine's own unit (bytes, line samples or words).
```

#### E11 — `crumpbit_block_t`: `lines` replaces `keep`/`invert`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 928–929: **modify** (replace 2 lines with 1; new lines 1110–1110).

- **What:** two `uint32_t` mask fields → one `const uint8_t *lines`.
- **Why:** X2. The struct is on `process`'s stack; 4 B smaller.
- **Accessors:** every engine copies `b->lines` into a local before its
  loop (E15, E23, E39, E50).

Current text (WT 928–929):

```c
    uint32_t keep;
    uint32_t invert;
```

New text:

```c
    const uint8_t *lines;   /* stage 1 data-line table (X2) */
```

#### E12 — `crumpBit_stage1()`: the table read

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 939–956: **modify** (replace 18 lines with 21; new lines 1120–1140).

- **What:** the signature takes `lines` instead of `keep, invert`; each
  channel's code is `lines[crumpBit_adc(x)]`.
- **Why:** X2. `lines[c]` equals `(c & keep) ^ invert` for every c, so the
  codes, and everything after them, are bit-identical (CB4 and CB10 stay
  bit-exact; CB17 checks the table).
- **Inputs/outputs:** unchanged otherwise. **Callers:** every engine loop.

Current text (WT 939–956):

```c
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
```

New text:

```c
 * What: the S091 P1/P2 statements as one inline helper so every engine runs
 * the identical dry path: the 8-bit code of each channel through the
 * data-line table (S091 P5 X2: lines[adc] equals the former
 * (adc & keep) ^ invert for every code, so the codes are bit-identical);
 * then h = (code - code_prev)/128 + R*h_prev, the exact R4 form (the integer
 * difference converted by `vcvt #7`, one VFMA). The AC memories advance to
 * this frame.
 * Inputs: the two input samples, the data-line table, and the four AC
 * memories by pointer (locals of the engine loop, kept in registers after
 * inlining).
 * Outputs: *hl, *hr, the dry signal (the mix stage's `h`).
 * Callers: every crumpBit_run*() loop.
 */
static inline void crumpBit_stage1(float xl, float xr, const uint8_t *lines,
                                   int32_t *ac_cl, int32_t *ac_cr,
                                   float *ac_yl, float *ac_yr,
                                   float *hl, float *hr)
{
    /* code_*: the channel's code after the data lines (one LDRB each). */
    const int32_t code_l = (int32_t)lines[crumpBit_adc(xl)];
    const int32_t code_r = (int32_t)lines[crumpBit_adc(xr)];
```

#### E13 — Remove `crumpBit_lineRead()`; return-chain comment

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1000–1034: **modify** (replace 35 lines with 10; new lines 1184–1193).

- **What:**
  - removes the S074 float tape read (its only caller was the tape loop,
    which now reads in Q14, E14);
  - the return-chain comment: the feedback is per block (S1); callers are
    the tape loop and `pit` only, because `grn`'s echo no longer uses it
    (G4, E37).
- **Why:** dead code otherwise (a `static inline` with no caller compiles
  away, but leaves a misleading reference implementation in the file).

Current text: WT lines 1000–1034 (35 lines, replaced whole).

New text:

```c
 * The feedback return chain shared by the tape-family engines (S091 P2
 * §5.3): DC blocker (y = w - w_prev + R_dc y_prev, ~5 Hz), the `ton` tilt
 * (one pole s += k (v - s); output a s + b v: LP (1,0), flat (0,1), HP
 * (-1,1)), the feedback gain, then the cubic soft knee
 * y = v (1 - (4/27) v^2) on v clamped to +-1.5.
 * Inputs: the line read, the four memories/coefficients by value/pointer,
 * the block's feedback (stepped per block since S091 P5 S1). Output: the
 * value added to the loop write.
 * Callers: crumpBit_tapLoop(), crumpBit_runPitch(). (S091 P5 G4: `grn`'s
 * echo no longer uses it: plain gain below unity.)
```

#### E14 — B: Q14 read helpers above the tape loop; the tape loop's head and locals

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1071–1098: **modify** (replace 28 lines with 122; new lines 1230–1351).

- **What:**
  - `crumpBit_readQ14()` moves here from its P3 place (E20 removes the old
    copy), comment updated: callers are now the tape loop (B); `pit` reads
    drop-sample (P1);
  - new `crumpBit_floatToQ14(d)`: `trunc(d · 2^14)`, one `VCVT.S32.F32
    #14` on ARM, C on the host (exact: × 2^14 is exact in float, the cast
    truncates as the VCVT does);
  - new `crumpBit_q21ToFloat(x)`: `x / 2^21`, one `VCVT.F32.S32 #21`; a
    Q14 code-unit value read this way is code / 128, exact;
  - `crumpBit_tapLoop()`: the comment (B, S1, `always_inline`), the
    signature (one feedback value; the `fb_to` origin is gone), `lim14`,
    the block's balance gains as constants on `pan_raw`.
- **Why:**
  - **B:** the float read spent ~25 instructions on float/int conversions
    and a float wrap; the integer read is ~13 (loop 124 → 111 with the mix
    ramp kept);
  - **S1:** no per-frame `fb`/`gain` adds and no ramp registers;
  - **always_inline (§8.2):** with the larger body LTO outlined the loop
    once into a shared `crumpBit_tapLoop.isra.0` with runtime glide and
    feedback; forced inline, `dly` and `cmb` each get a constant-folded
    loop.
- **Sound:** the same linear interpolation to 1/16,384 of a sample (finer
  than float at long delays); 60 s of random input against P2 differ by RMS
  ≈ 0.05 of an 8-bit step (plan B). `fbk`/`dpn` move in 0.7 ms steps.
- **Inputs:** runtime, block, target delay ≤ len − 2, glide constant,
  feedback gain (signed for `cmb`). **Outputs:** io, loop bytes, tape and
  AC state. **Callers:** `crumpBit_runDelay()`, `crumpBit_runComb()`.

Current text: WT lines 1071–1098 (28 lines, replaced whole).

New text:

```c
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
 * Why integer: the float read it replaced (the S074 crumpBit_lineRead(),
 * removed in S091 P5) spent ~25 instructions on float/int conversions and a
 * float wrap; this is ~13. S091 P5 B moved it above crumpBit_tapLoop(): the
 * tape sub-types read through it too.
 * Inputs: line, its length and length << 14, rp. Output: the sample,
 * code x 2^14.
 * Callers: crumpBit_tapLoop() (`dly`, `cmb`; S091 P5 B). `pit` reads
 * without interpolation since S091 P5 P1 (crumpBit_headRead()).
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

/*
 * Delay in samples to Q14 (S091 P5 B): trunc(d x 2^14), one
 * VCVT.S32.F32 with 14 fractional bits (round toward zero, the conversion
 * scales, no multiply). Host builds use the exact C equivalent (x 2^14 is
 * exact in float; the cast truncates toward zero as the VCVT does).
 * Input: d, 0 <= d <= 126,654 (< 2^17, so d x 2^14 < 2^31). Output: Q14.
 * Caller: crumpBit_tapLoop(), once per frame on the glided delay.
 */
static inline int32_t crumpBit_floatToQ14(float d)
{
#if defined(__arm__) && defined(__VFP_FP__) && !defined(__SOFTFP__)
    int32_t x;

    __asm__("vcvt.s32.f32 %1, %1, #14\n\t"
            "vmov %0, %1"
            : "=r"(x), "+t"(d));
    return x;
#else
    return (int32_t)(d * 16384.0f);
#endif
}

/*
 * Q21 fixed point to float (S091 P5 B): x / 2^21, VMOV plus one
 * VCVT.F32.S32 with 21 fractional bits (as crumpBit_q20ToFloat()). A Q14
 * code-unit read (code x 2^14) read this way is code / 128, the normalised
 * +-1 sample, exact (|x| <= 2^21 < 2^24). Host builds use the C
 * equivalent. Input: x. Output: x * 2^-21. Caller: crumpBit_tapLoop().
 */
static inline float crumpBit_q21ToFloat(int32_t x)
{
#if defined(__arm__) && defined(__VFP_FP__) && !defined(__SOFTFP__)
    float f;

    __asm__("vmov %0, %1\n\t"
            "vcvt.f32.s32 %0, %0, #21"
            : "=t"(f)
            : "r"(x));
    return f;
#else
    return (float)x * (1.0f / 2097152.0f);
#endif
}

/*
 * Engine A frame loop: one fractional tape tap with glide (`dly`, `cmb`).
 *
 * What: the S091 P2 stage-2 loop with stage 1 and the mix stage as the
 * shared helpers: glide towards `target`, fractional read muted beyond
 * `valid`, the return chain, the 8-bit re-record, and the wet =
 * delay-pan-balanced read. Every statement runs every frame.
 * S091 P5:
 * - B: the read is the `pit` integer read (crumpBit_readQ14()). The glided
 *   delay is converted once per frame to Q14 (d14), the position
 *   (wp << 14) - d14 wraps by its sign bit, and one scaling VCVT (#21)
 *   turns the Q14 code-unit result into the +-1 sample. The same linear
 *   interpolation as the float read, to 1/16,384 of a sample (finer than a
 *   float position at long delays); the `valid` gate is an integer compare
 *   applied as a mask on the integer read;
 * - S1: the feedback gain and the two balance gains are this block's
 *   targets for every frame (a step per 0.7 ms block, no ramp); the mix
 *   stage's `mix` keeps its per-frame ramp (plan Q9);
 * - always_inline: with the Q14 body LTO outlined the loop once
 *   (crumpBit_tapLoop.isra.0, one shared loop with runtime glide and
 *   feedback); forced inline, `dly` and `cmb` each get their own
 *   constant-folded loop (plan §8.2).
 * Inputs: runtime, block, target delay (<= len - 2), glide constant, the
 * block's feedback gain (`cmb` passes it signed), tone coefficients.
 * Outputs: io samples (dry/wet mixed), loop bytes, tape state, AC state.
 * Callers: crumpBit_runDelay() (glide 1.5113e-4, the S074 tape) and
 * crumpBit_runComb() (glide 4.5245e-3).
 */
static inline __attribute__((always_inline)) void crumpBit_tapLoop(
    CrumpBitRuntime *rt, const crumpbit_block_t *b, float target,
    float glide_k, float fb)
{
    CrumpBitTapState *t = &rt->u.tap;
    int8_t *loop = b->base;
    const uint32_t len = b->len;
    /* lim14: the loop length in Q14, added to a read that wrapped below 0. */
    const uint32_t lim14 = len << 14;
    /*
     * Delay-pan balance: the delay's wet placement (S075 F2-E law), this
     * block's value for every frame (S1).
     */
    const float gain_l = crumpBit_panGainL(rt->pan_raw);
    const float gain_r = crumpBit_panGainR(rt->pan_raw);
```

#### E15 — Tape loop body: Q14 read, gate mask, `mix` ramp only

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1131–1150: **modify** (replace 20 lines with 27; new lines 1384–1410).

- **What:**
  - locals: `lines` replaces the masks;
  - per frame: `mix += mix_inc` stays (Q9); the `fb`/`gain_l`/`gain_r`
    adds are gone (S1);
  - `d14 = crumpBit_floatToQ14(delay)`; the read position
    `(wp << 14) − d14`, wrapped by its sign bit inside `crumpBit_readQ14()`;
  - the gate: all ones while `d14 + 2·2^14 ≤ valid << 14` (the S074 float
    test `delay + 2 ≤ valid`, in integers), applied as an AND on the Q14
    read before the scaling VCVT.
- **Why:** B and S1. The mask keeps the loop branch-free.
- **Ranges:** `valid << 14` ≤ 126,656 · 2^14 < 2^32 (unsigned); `wp << 14`
  < 2^31 (signed); `d14` ≥ 0.
- **Host law:** CB4 is bit-exact against a reference that writes the same
  read from the plan's equations with branches and 64-bit compares (X13).

Current text (WT 1131–1150):

```c
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
```

New text:

```c
    const uint8_t *const lines = b->lines;
    const float mix_inc = b->mix_inc;

    for (n = 0u; n < frames; n++) {
        float hl;
        float hr;
        float wet;
        /* d14: this frame's glided delay in Q14 (trunc(delay x 2^14)). */
        int32_t d14;

        /* Stage 1 (shared). */
        crumpBit_stage1(left[n], right[n], lines,
                        &ac_cl, &ac_cr, &ac_yl, &ac_yr, &hl, &hr);
        /* Stage 2, engine A: mix ramp, glide, read, return, re-record. */
        mix += mix_inc;
        delay += (target - delay) * glide_k;
        d14 = crumpBit_floatToQ14(delay);
        /*
         * The read, gated: all ones while the oldest sample the
         * interpolation reaches (d + 2 behind, both in Q14) was written
         * since the loop was treated as empty, else zero (the S074 float
         * test (delay + 2) <= valid, in integers). The AND mutes the Q14
         * read; the scaling VCVT (#21) gives the +-1 sample.
         */
        wet = crumpBit_q21ToFloat(
            crumpBit_readQ14(loop, len, lim14, (int32_t)(wp << 14) - d14) &
            -(int32_t)((uint32_t)d14 + (2u << 14) <= (valid << 14)));
```

#### E16 — Tape loop tail: no `pan_prev` write

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1173–1181: **modify** (replace 9 lines with 9; new lines 1433–1441).

- **What:** removes `rt->pan_prev = rt->pan_raw;` (H7).

Current text (WT 1173–1181):

```c
    rt->pan_prev = rt->pan_raw;
}

/*
 * `dly`: the S074/S091 tape delay (engine A).
 *
 * What: per block the Rate/Sync target clamped to this loop (the S091 P2
 * crumpBit_targetSamples()), the feedback ramp (S091 P2 curve, up to 1.2),
 * then the shared tape loop with the 150 ms glide.
```

New text:

```c
}

/*
 * `dly`: the S074/S091 tape delay (engine A).
 *
 * What: per block the Rate/Sync target clamped to this loop (the S091 P2
 * crumpBit_targetSamples()), the block's feedback (S091 P2 curve, up to
 * 1.2; one value per block since S091 P5 S1), then the shared tape loop with
 * the 150 ms glide.
```

#### E17 — `crumpBit_runDelay()`: one feedback value

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1193–1195: **modify** (replace 3 lines with 1; new lines 1453–1453).

- **What:** passes `crumpBit_feedbackGain(rt->feedback_raw)` only; removes
  the origin argument and the `feedback_prev` write.
- **Why:** S1 (H7).

Current text (WT 1193–1195):

```c
                     crumpBit_feedbackGain(rt->feedback_prev),
                     crumpBit_feedbackGain(rt->feedback_raw));
    rt->feedback_prev = rt->feedback_raw;
```

New text:

```c
                     crumpBit_feedbackGain(rt->feedback_raw));
```

#### E18 — `crumpBit_runComb()` comment: the per-block gain

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1205–1205: **modify** (replace 1 line with 1; new lines 1463–1463).

- **What:** "signing the per-block gains" → "gain" (one value now).

Current text (WT 1205–1205):

```c
 * harmonics) by signing the per-block gains (0 per sample). With `ton`
```

New text:

```c
 * harmonics) by signing the per-block gain (0 per sample). With `ton`
```

#### E19 — `crumpBit_runComb()`: one signed feedback value

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1222–1224: **modify** (replace 3 lines with 1; new lines 1480–1480).

- **What:** passes `sign · crumpBit_feedbackGain(rt->feedback_raw)`;
  removes the origin argument and the `feedback_prev` write.
- **Why:** S1 (H7).

Current text (WT 1222–1224):

```c
                     sign * crumpBit_feedbackGain(rt->feedback_prev),
                     sign * crumpBit_feedbackGain(rt->feedback_raw));
    rt->feedback_prev = rt->feedback_raw;
```

New text:

```c
                     sign * crumpBit_feedbackGain(rt->feedback_raw));
```

#### E20 — P1: `crumpBit_headRead()` drop-sample; remove the old `crumpBit_readQ14()`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1291–1338: **modify** (replace 48 lines with 28; new lines 1547–1574).

- **What:**
  - removes the P3 copy of `crumpBit_readQ14()` (it moved above the tape
    loop, E14);
  - `crumpBit_headRead()` computes the head position itself, wraps it by
    the sign-bit mask, and returns `loop[rp >> 14] << 14`: the sample at
    the integer part, no second sample, no `i1` wrap, no interpolation. The
    return value stays in Q14 code units, so the head crossfade below is
    unchanged. The `len` parameter goes (only the wrap of `i1` used it).
- **Why (P1):** the second read, its wrap and the multiply-add, twice per
  frame, were the largest share of `pit`'s loop. With S1 the loop went
  135 → 106.
- **Sound:** grittier, more aliased shifting, mostly at large intervals
  (`grn` already reads this way). The ratio is unchanged (CB13).
- **Inputs:** line, `lim14`, `wpg` = (wp << 14) − guard, the phase word,
  `w14` = W · 2^14. **Output:** code · 2^14. **Caller:**
  `crumpBit_runPitch()` (E25).

Current text: WT lines 1291–1338 (48 lines, replaced whole).

New text:

```c
 * One `pit` head: guard + W x phase behind the write head.
 *
 * What: rp = wpg - (phase x w14) >> 32, where wpg is (wp << 14) - guard
 * (one per frame, shared by both heads) and the high word of the 32 x 32
 * product (one UMULL) is W x phase in Q14; the position wraps by its sign
 * bit (+ lim14 when negative, as crumpBit_readQ14()); then S091 P5 P1: the
 * sample at the integer part, rp >> 14, read without interpolation
 * (drop-sample, as `grn` reads), returned in the same Q14 code units
 * (code x 2^14) so the head mix below is unchanged.
 * Why P1: the second sample, its wrap and the interpolation multiply-add,
 * twice per frame, were the largest share of `pit`'s loop; dropping them
 * (with S1) took the loop from 135 to 104. Sound: grittier, more aliased
 * shifting, mostly at large intervals; the pitch ratio (host law CB13) is
 * unchanged, the head speeds being the same.
 * Inputs: line, line length << 14, wpg, the phase word, w14 = W x 2^14.
 * Output: the head sample, code x 2^14.
 * Callers: crumpBit_runPitch() (heads A and B).
 */
static inline int32_t crumpBit_headRead(const int8_t *loop, uint32_t lim14,
                                        int32_t wpg, uint32_t phase,
                                        uint32_t w14)
{
    /* rp: the head's position in Q14, guard + W x phase behind the write. */
    int32_t rp = wpg - (int32_t)(((uint64_t)phase * w14) >> 32);

    /* Below 0 it wrapped: add the line once (sign-bit mask, no branch). */
    rp += (int32_t)(lim14 & (uint32_t)(rp >> 31));
    return (int32_t)loop[(uint32_t)rp >> 14] << 14;
```

#### E21 — `crumpBit_runPitch()` comment: drop-sample heads, S1

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1352–1358: **modify** (replace 7 lines with 9; new lines 1588–1596).

- **What:** the per-frame description adds the drop-sample read (P1), and
  the block description adds that feedback and balance are this block's
  values with `mix` still ramping.

Current text (WT 1352–1358):

```c
 * half turn is one integer add, its wrap free); the triangular crossfade
 * wA = 1 - |2 phase - 1| is the phase word folded by its sign bit
 * (phase XOR (phase ASR 31)) read as a 0.31 fraction; wB = 1 - wA, so the
 * mix is B + wA (A - B) and the 1/128 scale and the gate are one per-block
 * factor. The shifted signal feeds the shared return chain into the loop
 * (each repeat shifted again: shimmer) and is the wet, balanced by `dpn` as
 * the delay's.
```

New text:

```c
 * half turn is one integer add, its wrap free), each read drop-sample (S091
 * P5 P1, crumpBit_headRead()); the triangular crossfade
 * wA = 1 - |2 phase - 1| is the phase word folded by its sign bit
 * (phase XOR (phase ASR 31)) read as a 0.31 fraction; wB = 1 - wA, so the
 * mix is B + wA (A - B) and the 1/128 scale and the gate are one per-block
 * factor. The shifted signal feeds the shared return chain into the loop
 * (each repeat shifted again: shimmer) and is the wet, balanced by `dpn` as
 * the delay's. S091 P5 S1: the feedback and the balance gains are this
 * block's targets for every frame; `mix` keeps its per-frame ramp.
```

#### E22 — `crumpBit_runPitch()`: block constants for feedback and balance

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1381–1389: **modify** (replace 9 lines with 4; new lines 1619–1622).

- **What:** `fb`, `gain_l`, `gain_r` become `const` values on the current
  rows; the `*_inc` ramp steps and the `*_prev` origins are gone.
- **Why:** S1. Three fewer live float registers in the loop.

Current text (WT 1381–1389):

```c
    float fb = crumpBit_feedbackGain(rt->feedback_prev);
    const float fb_to = crumpBit_feedbackGain(rt->feedback_raw);
    float gain_l = crumpBit_panGainL(rt->pan_prev);
    float gain_r = crumpBit_panGainR(rt->pan_prev);
    const float fb_inc = (fb_to - fb) * b->per_frame;
    const float gain_l_inc =
        (crumpBit_panGainL(rt->pan_raw) - gain_l) * b->per_frame;
    const float gain_r_inc =
        (crumpBit_panGainR(rt->pan_raw) - gain_r) * b->per_frame;
```

New text:

```c
    /* S1: the block's feedback and balance gains, no per-frame ramp. */
    const float fb = crumpBit_feedbackGain(rt->feedback_raw);
    const float gain_l = crumpBit_panGainL(rt->pan_raw);
    const float gain_r = crumpBit_panGainR(rt->pan_raw);
```

#### E23 — `crumpBit_runPitch()` locals: `lines`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1436–1437: **modify** (replace 2 lines with 1; new lines 1669–1669).

- **What:** `lines = b->lines` replaces the two mask locals (X2).

Current text (WT 1436–1437):

```c
    const uint32_t keep = b->keep;
    const uint32_t invert = b->invert;
```

New text:

```c
    const uint8_t *const lines = b->lines;
```

#### E24 — `crumpBit_runPitch()` loop: stage 1 call; no feedback/balance adds

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1453–1458: **modify** (replace 6 lines with 3; new lines 1685–1687).

- **What:** passes `lines` to `crumpBit_stage1()`; removes the three
  per-frame adds; `mix += mix_inc` stays (Q9).

Current text (WT 1453–1458):

```c
        crumpBit_stage1(left[n], right[n], keep, invert,
                        &ac_cl, &ac_cr, &ac_yl, &ac_yr, &hl, &hr);
        mix += mix_inc;
        fb += fb_inc;
        gain_l += gain_l_inc;
        gain_r += gain_r_inc;
```

New text:

```c
        crumpBit_stage1(left[n], right[n], lines,
                        &ac_cl, &ac_cr, &ac_yl, &ac_yr, &hl, &hr);
        mix += mix_inc;
```

#### E25 — `crumpBit_runPitch()` loop: head reads without `len`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1466–1468: **modify** (replace 3 lines with 2; new lines 1695–1696).

- **What:** both `crumpBit_headRead()` calls drop the `len` argument (E20).

Current text (WT 1466–1468):

```c
        ra = crumpBit_headRead(loop, len, lim14, wpg, phase, w14);
        rb = crumpBit_headRead(loop, len, lim14, wpg, phase + 0x80000000u,
                               w14);
```

New text:

```c
        ra = crumpBit_headRead(loop, lim14, wpg, phase, w14);
        rb = crumpBit_headRead(loop, lim14, wpg, phase + 0x80000000u, w14);
```

#### E26 — `crumpBit_runPitch()` tail: no origin writes

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1498–1499: **remove** 2 lines (nothing replaces them; the text after them starts at new line 1726).

- **What:** removes the `feedback_prev` and `pan_prev` writes (H7).

Removed text:

```c
    rt->feedback_prev = rt->feedback_raw;
    rt->pan_prev = rt->pan_raw;
```

#### E27 — `crumpBit_grainPick()` comment: the Position limit (A)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1517–1519: **modify** (replace 3 lines with 7; new lines 1743–1749).

- **What:** the Position bullet adds: the rhythmic stop is limited to the
  last of the `fit` stops that fit the line (none fitting: Position 0's
  lag), as the P4 manager clamp did, now also for LFO and Pattern values;
  a fitting stop that is not yet recorded still waits.

Current text (WT 1517–1519):

```c
 *   at the current tempo, and until that lag is recorded the grain does
 *   not sound (end 0: the trigger is retried), so a delay never starts at
 *   a wrong, shorter lag; left of 63 uniform in [2, 2 + m (max - 2)] with
```

New text:

```c
 *   at the current tempo, limited (S091 P5 A) to the last stop whose lag
 *   fits the line (`fit` stops fit; none: Position 0's lag), so a stop too
 *   long for the line plays the longest one that fits, as the P4 manager
 *   clamp did, now also for LFO and Pattern values; until that lag is
 *   recorded the grain does not sound (end 0: the trigger is retried), so
 *   a delay never starts at a wrong, shorter lag; left of 63 uniform in
 *   [2, 2 + m (max - 2)] with
```

#### E28 — `crumpBit_grainPick()` comment: the `fit` input

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1534–1534: **modify** (replace 1 line with 2; new lines 1764–1765).

- **What:** documents the new `fit` argument (0..16, from
  `crumpBit_grainStopFit()`).

Current text (WT 1534–1534):

```c
 * frozen flag, samples per 96-PPQ tick. Output: *out.
```

New text:

```c
 * frozen flag, samples per 96-PPQ tick, fit (S091 P5 A: the stops, 0..16,
 * that fit the line at this tempo, crumpBit_grainStopFit()). Output: *out.
```

#### E29 — `crumpBit_grainPick()` signature: `uint32_t fit`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1561–1561: **modify** (replace 1 line with 1; new lines 1792–1792).

- **What:** adds `fit` after `per_tick`.
- **Why:** the pick stays a pure function of its arguments (host CB14 calls
  it directly); reading the cache from `rt->u.grn` would make CB14's
  hand-built runtimes clamp every positive Position to Position 0.
- **Callers:** `crumpBit_runGrain()` (E41), CB14 (X17–X21).

Current text (WT 1561–1561):

```c
                               uint32_t frozen, float per_tick,
```

New text:

```c
                               uint32_t frozen, float per_tick, uint32_t fit,
```

#### E30 — `crumpBit_grainPick()`: the stop limited to the fit (A)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1616–1617: **modify** (replace 2 lines with 11; new lines 1847–1857).

- **What:**
  - `stop = posStop(pos)`, then `stop < fit ? stop : (fit ? fit − 1 : 0)`;
  - `lag = fit ? grainTime[grainStopCc[stop]] · per_tick : LAG0`.
- **Why the guarded index:** with `fit == 0`, `fit − 1u` would index 0xFFFF…;
  the ternary keeps the index in range even if the compiler evaluates the
  load speculatively, and the `fit == 0` case selects Position 0's lag (the
  value the P4 manager clamp produced: `dpn` clamped to 63).
- **Unchanged:** `span = (lag > max_lag) ? 0 : span` (not yet recorded:
  the trigger retries).
- **Host law:** CB14 (c): every positive Position at N = 60,000, 120 BPM
  (fit 14): stops 0..13 play themselves, 14..15 play stop 13; with
  `valid` at half the lag the grain waits.

Current text (WT 1616–1617):

```c
        lag = crumpBit_grainTime[crumpBit_grainStopCc[
                  crumpBit_grainPosStop(pos)]] * per_tick;
```

New text:

```c
        /*
         * stop: Position's stop, limited to the last of the `fit` stops
         * that fit the line (S091 P5 A; the index stays in range when none
         * fits, and that case plays Position 0's lag instead).
         */
        uint32_t stop = crumpBit_grainPosStop(pos);

        stop = (stop < fit) ? stop : ((fit != 0u) ? fit - 1u : 0u);
        lag = (fit != 0u)
            ? crumpBit_grainTime[crumpBit_grainStopCc[stop]] * per_tick
            : CRUMPBIT_GRAIN_LAG0;
```

#### E31 — Grain loop-state comment: echo gain, no DC memories

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1690–1692: **modify** (replace 3 lines with 3; new lines 1930–1932).

- **What:** the run-state description: the echo gain replaces the ramps
  and the DC-blocker memories (G4).

Current text (WT 1690–1692):

```c
 * (positions, steps, wraps, the window ramps, gains, the delay line's
 * return memories, the ramps, the AC memories); crumpbit_grain_k_t the
 * block's constants. Both live on the stack of crumpBit_runGrain() and are
```

New text:

```c
 * (positions, steps, wraps, the window ramps, gains, the echo gain, the mix
 * ramp, the AC memories; S091 P5 G4 removed the delay line's DC-blocker
 * memories); crumpbit_grain_k_t the block's constants. Both live on the stack of crumpBit_runGrain() and are
```

#### E32 — `crumpbit_grain_run_t`: remove `dc_x`, `dc_y`; `fb` is the block's

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1720–1722: **modify** (replace 3 lines with 1; new lines 1960–1960).

- **What:** two fields gone; `fb` documented as this block's echo gain,
  below 1.
- **Why:** G4, S1.

Current text (WT 1720–1722):

```c
    float dc_x;         /* delay-line return DC blocker */
    float dc_y;
    float fb;           /* feedback ramp */
```

New text:

```c
    float fb;           /* echo gain, this block's (S1), below 1 (G4) */
```

#### E33 — `crumpbit_grain_k_t`: `lines`; no `fb_inc`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1738–1741: **modify** (replace 4 lines with 2; new lines 1976–1977).

- **What:** `lines` replaces `keep`/`invert` (X2); `fb_inc` removed (S1).

Current text (WT 1738–1741):

```c
    uint32_t keep;
    uint32_t invert;
    uint32_t d;         /* delay-line tap D, whole frames (2..len-2) */
    float fb_inc;
```

New text:

```c
    const uint8_t *lines;   /* stage 1 data-line table (X2) */
    uint32_t d;         /* delay-line tap D, whole frames (2..len-2) */
```

#### E34 — `crumpBit_grainSpan()` comment: the window without a lower clamp; plain echo

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1755–1764: **modify** (replace 10 lines with 15; new lines 1991–2005).

- **What:**
  - the window is `min(in, out, 1)`; a sounding voice's `out` stays above
    0 on every frame because every end is on a block boundary; an idle
    voice's `out` falls below 0, but its gains are 0 (the park, E42);
  - the echo write is `grains + fb · tap`, no DC blocker, no knee, gain
    below 1, the ADC clamping peaks.
- **Why:** the per-frame contract changes with G3 and G4.

Current text (WT 1755–1764):

```c
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
```

New text:

```c
 * - each voice's flat-top window w = min(in, out, 1): `in` rises 1/128 per
 *   frame from 0 at the start, `out` falls 1/128 per frame and reaches 0 at
 *   the grain's end (L + 128), so a grain fades in over 128 frames, holds 1,
 *   and fades out over the 128 frames after L. All steps are exact (2^-7).
 *   S091 P5 G3: no lower clamp. Every end lands on a block boundary
 *   (crumpBit_runGrain()), so a sounding voice's `out` is above 0 on every
 *   frame of a block; an idle voice's `out` does fall below 0, but its
 *   balance gains are 0 for the block (the park), so it adds exactly 0;
 * - the voices scaled by their windows (the scaling VCVT gives +-1) and
 *   their balance gains into the stereo grain signal;
 * - the delay line: the code exactly D behind the write head (one subtract
 *   and compare wrap), gate-masked, then (S091 P5 G4) re-recorded as the
 *   grains' mono sum plus fb x the tap: no DC blocker and no soft knee, the
 *   gain staying below 1 (CRUMPBIT_GRAIN_FB_MAX) and the ADC clamping any
 *   peak;
```

#### E35 — `crumpBit_grainSpan()`: stage 1 call; no feedback add

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1791–1794: **modify** (replace 4 lines with 3; new lines 2032–2034).

- **What:** `k->lines` into `crumpBit_stage1()`; removes `s->fb +=
  k->fb_inc`; `s->mix += k->mix_inc` stays (Q9).

Current text (WT 1791–1794):

```c
        crumpBit_stage1(k->left[n], k->right[n], k->keep, k->invert,
                        &s->ac_cl, &s->ac_cr, &s->ac_yl, &s->ac_yr, &hl, &hr);
        s->mix += k->mix_inc;
        s->fb += k->fb_inc;
```

New text:

```c
        crumpBit_stage1(k->left[n], k->right[n], k->lines,
                        &s->ac_cl, &s->ac_cr, &s->ac_yl, &s->ac_yr, &hl, &hr);
        s->mix += k->mix_inc;
```

#### E36 — `crumpBit_grainSpan()`: remove the window's lower clamp (G3)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1805–1811: **modify** (replace 7 lines with 9; new lines 2045–2053).

- **What:** removes `wa = (wa < 0) ? 0 : wa;` and the same for `wb`; the
  comment states why it is safe.
- **Why (G3):** two compare-selects per voice per frame. Safe because:
  - every grain's end and every steal's end is on a block boundary (E43,
    E45), so a sounding voice never reaches its end inside a block;
  - a voice idle at the block start has zero gains (E42), so its negative
    window multiplies to exactly 0;
  - the P4 age defect is fixed (E41), which otherwise moved ends off the
    boundary.
- **Host laws:** CB14 (e) (the copy identity across two retriggers, every
  frame within 2·10⁻⁶) passes unchanged; CB14 (i) checks the block
  alignment over 40,000 blocks with 815 steal blocks.

Current text (WT 1805–1811):

```c
        /* Flat-top windows: the lower ramp, at most 1, never negative. */
        wa = (s->in_a < s->out_a) ? s->in_a : s->out_a;
        wb = (s->in_b < s->out_b) ? s->in_b : s->out_b;
        wa = (wa > 1.0f) ? 1.0f : wa;
        wb = (wb > 1.0f) ? 1.0f : wb;
        wa = (wa < 0.0f) ? 0.0f : wa;
        wb = (wb < 0.0f) ? 0.0f : wb;
```

New text:

```c
        /*
         * Flat-top windows: the lower ramp, at most 1. Never negative while
         * the voice sounds (G3: ends on block boundaries); a parked voice
         * may go below 0, multiplied by its zero gains below.
         */
        wa = (s->in_a < s->out_a) ? s->in_a : s->out_a;
        wb = (s->in_b < s->out_b) ? s->in_b : s->out_b;
        wa = (wa > 1.0f) ? 1.0f : wa;
        wb = (wb > 1.0f) ? 1.0f : wb;
```

#### E37 — `crumpBit_grainSpan()`: the plain echo (G4)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1831–1838: **modify** (replace 8 lines with 3; new lines 2073–2075).

- **What:** the delay line writes `adc(½(grains_l + grains_r) + fb · tap)`;
  the return chain call, its dummy `tone_s` and the DC memories go.
- **Why (G4):** the DC blocker and the cubic knee were the main share of
  the `grn` loop drop (with X2: 136 → 116). Below unity gain DC decays on
  its own and no knee is needed; the ADC clamps any peak.
- **Sound:** no self-oscillation above unity, no soft saturation in the
  echo; the echo decays to the 8-bit rounding floor (CB18: 25 codes at
  0.98, 2 at 0.77, 0 at 0.49).

Current text (WT 1831–1838):

```c
        {
            float tone_s = 0.0f;

            k->dline[s->wp] = (int8_t)((int32_t)crumpBit_adc(
                0.5f * (grains_l + grains_r) +
                crumpBit_returnChain(tap, &s->dc_x, &s->dc_y, &tone_s, 1.0f,
                                     0.0f, 1.0f, s->fb)) - 128);
        }
```

New text:

```c
        /* Echo (G4): grains' mono sum + fb x tap, fb < 1, ADC-clamped. */
        k->dline[s->wp] = (int8_t)((int32_t)crumpBit_adc(
            0.5f * (grains_l + grains_r) + s->fb * tap) - 128);
```

#### E38 — `crumpBit_runGrain()` comment: the pad, the fit cache, the park, block-aligned ends

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1870–1879: **modify** (replace 10 lines with 22; new lines 2107–2128).

- **What:** the per-block description adds:
  - the steal's fade waits for the pad to the next block boundary;
  - the Position fit worked out only when tempo or line length changes (A,
    G6);
  - the park (idle voices' gains 0) and the block echo gain (S1, G4);
  - the G3 rounding at the event: a grain's span rounded down to a block
    boundary (≤ 31 frames shorter, fades unchanged); a steal padded up.

Current text (WT 1870–1879):

```c
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
```

New text:

```c
 *   fade (128 frames, plus S091 P5 G3's pad to the next block boundary),
 *   when that voice is free. One steal at a time: while a trigger is
 *   deferred, a further Density trigger with no free voice is dropped;
 * - ONE candidate (crumpBit_grainPick(), from one hardware RNG word) every
 *   block whether used or not (constant cost). A trigger whose candidate
 *   cannot sound yet (end 0: nothing or too little recorded behind its
 *   lag) is not consumed: it fires again on the next block, so a grain
 *   starts as soon as its audio exists (no silent period at entry);
 * - S091 P5 A, G6: the Position fit (how many rhythmic stops fit the line
 *   at this tempo, crumpBit_grainStopFit()) is worked out only when the
 *   tempo or the line length changes, and kept in the grain state with its
 *   key; the candidate limits its stop to it;
 * - the run state from the stored state: window ramps from age and end,
 *   gains from the pan bytes; S091 P5 G3: a voice idle at the block start
 *   is parked (gains 0), the echo gain is this block's (S1) on the `grn`
 *   curve (G4).
 * - S091 P5 G3, at the event: a started grain's span is rounded down so
 *   that its end lands on a block boundary (up to 31 frames shorter, the
 *   fades unchanged); a steal's fade-out is padded so that it, and the
 *   deferred trigger it waits for, land on one (`out` starts above 1 and
 *   the window holds 1 for the pad). Every end then falls between blocks,
 *   which is what lets the frame loop drop the window's lower clamp.
```

#### E39 — `crumpBit_runGrain()`: `k.lines`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 1935–1936: **modify** (replace 2 lines with 1; new lines 2184–2184).

- **What:** `k.lines = b->lines` replaces the two mask copies (X2).

Current text (WT 1935–1936):

```c
    k.keep = b->keep;
    k.invert = b->invert;
```

New text:

```c
    k.lines = b->lines;
```

#### E40 — `crumpBit_runGrain()`: the cached Position fit (A, G6)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, after WT line 2028: **add** 19 lines (new lines 2277–2295).

- **What:** before the candidate pick:
  - `key = bpm | len << 16` (BPM ≤ 0xFFFF, N < 2^16: exact);
  - on a key change: `g->fit = crumpBit_grainStopFit(crumpBit_grainMaxTicks(
    N − guard, bpm))`, `g->fit_key = key`.
- **Why:**
  - **A:** the engine clamps Position itself, which saves the manager's
    per-block Position search in every sub-type (A: manager 3.7 → 2.7 per
    frame);
  - **G6:** measured on this tree (§2.3): the five-probe search every block
    costs 809 per block; the cached hit path 760 (−1.5 per frame); a miss
    block 814 (+0.16). The plan's rule (keep if ≥ 0.5) keeps it.
- **Invalidation:** none needed: the key includes N, so a seat or share
  change recomputes; `crumpBit_enter()` and the manager's union clear zero
  the key, which no running line matches.
- **Host law:** CB6 compares `g.u.grn.fit` with
  `crumpBit_grainPosLimit()` after one block at every share step and at 120,
  47 and 300 BPM.

New text:

```c
    /*
     * S091 P5 A, G6: the Position fit for this tempo and line, worked out
     * only when either changes. key: BPM in the low half, N in the high
     * (N < 2^16, so the pair is exact); the zeroed key of a fresh state
     * never matches (N >= CRUMPBIT_GRAIN_LINE_MIN here). The fit uses the
     * label's expression (crumpBit_grainMaxTicks() of N - guard, which is
     * crumpBit_grainMaxSamples() of this share), so the page names the stop
     * the DSP plays (host law CB6).
     */
    {
        const uint32_t key = (uint32_t)bpm | (len << 16);

        if (key != g->fit_key) {
            g->fit = (uint8_t)crumpBit_grainStopFit(crumpBit_grainMaxTicks(
                (float)(len - CRUMPBIT_LOOP_GUARD), bpm));
            g->fit_key = key;
        }
    }

```

#### E41 — `crumpBit_runGrain()`: pass the fit; the dropped/retried trigger's ages (P4 defect)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2037–2043: **modify** (replace 7 lines with 20; new lines 2304–2323).

- **What:**
  - the candidate pick passes `g->fit`;
  - after the retry test: `if (ev >= frames) { age_a = g->age_a; age_b =
    g->age_b; }`.
- **Why the age reset (a defect found by the G3 law):** the ages are taken
  at the event frame (`age = stored + ev`) for the voice choice. When the
  trigger is then **dropped** (a steal is already running) or **retried**
  (nothing recorded yet), the event is cancelled (`ev = frames`), and the
  block-end step adds `frames − 0` — so both voices aged up to 31 frames
  too fast. On P4 that cut grains short with a step in their window (a
  small click). Under G3 it moves an end off its block boundary, and with
  no lower clamp the window could go negative inside a block. The stored
  ages are saturated at their ends, so they are the right block-start
  values.
- **Host law:** CB14 (i) found it (276 misaligned voice states before the
  fix, 0 after); break 18 removes the reset and must fail CB14.

Current text (WT 2037–2043):

```c
                           per_tick, &cand);
    }
    if (ev_trigger && cand.end == 0u) {
        /* Nothing to play yet: the trigger is kept for the next block. */
        retry = 1u;
        ev = frames;
        ev_trigger = 0u;
```

New text:

```c
                           per_tick, g->fit, &cand);
    }
    if (ev_trigger && cand.end == 0u) {
        /* Nothing to play yet: the trigger is kept for the next block. */
        retry = 1u;
        ev = frames;
        ev_trigger = 0u;
    }
    /*
     * S091 P5 (a P4 defect, found by the G3 law): the ages above were
     * taken at the event frame; a trigger dropped (a steal already
     * running) or retried leaves no event, and the block-end step adds the
     * whole block, so the ages go back to the block start (stored
     * saturated at each end). Without this a voice aged up to 31 frames too
     * fast: its grain was cut short with a step in its window, and its end
     * left the block boundary G3 needs.
     */
    if (ev >= frames) {
        age_a = g->age_a;
        age_b = g->age_b;
```

#### E42 — `crumpBit_runGrain()` run state: the park (G3), the echo gain (S1, G4)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2061–2065: **modify** (replace 5 lines with 15; new lines 2341–2355).

- **What:**
  - after the gains from the pan bytes: a voice idle at the block start
    (`g->age ≥ end`) gets gains 0 for the block;
  - `s.fb = crumpBit_grainFeedbackGain(rt->feedback_raw)`, one value per
    block; the DC memories and `fb_inc` go.
- **Why the park by gains:** the plan's form parked the `out` ramp with a
  slope of 0, which needs a per-voice slope register in the loop. Zero
  gains cost nothing per frame and give exactly 0 contribution (the window
  may go negative; `x · 0` adds ±0, which leaves the sum unchanged). A
  trigger in the block sets the voice's gains again at the event.

Current text (WT 2061–2065):

```c
    s.dc_x = g->dc_x;
    s.dc_y = g->dc_y;
    s.fb = crumpBit_feedbackGain(rt->feedback_prev);
    k.fb_inc = (crumpBit_feedbackGain(rt->feedback_raw) - s.fb) *
               b->per_frame;
```

New text:

```c
    /*
     * G3 park: a voice idle at the block start (age >= end) adds nothing
     * this block whatever its window does (its `out` falls below 0 from
     * here); a trigger in this block sets its gains again at the event.
     */
    if (g->age_a >= end_a) {
        s.gla = 0.0f;
        s.gra = 0.0f;
    }
    if (g->age_b >= end_b) {
        s.glb = 0.0f;
        s.grb = 0.0f;
    }
    /* S1, G4: the block's echo gain, 0..0.98 over the row. */
    s.fb = crumpBit_grainFeedbackGain(rt->feedback_raw);
```

#### E43 — `crumpBit_runGrain()` trigger: the span rounded to a block boundary (G3)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2076–2080: **modify** (replace 5 lines with 15; new lines 2366–2380).

- **What:** `end = ((ev + cand.end) & ~(32 − 1)) − ev`; the voice's `out`
  starts at `end / 128`.
- **Why:** the grain then ends at a block boundary; with 32-frame blocks
  (the only size mixer.c passes, `io.frames = OUTPUT_DMA_SIZE`) no voice
  reaches its end inside a block.
- **Bounds:** `cand.end > 128`, so the rounded span keeps ≥ 97 frames;
  rounding only shortens it, so every read stays inside the range the
  overwrite clamp already checked (CB14 (d) unchanged).
- **Sound:** a grain is up to 31 frames (0.7 ms) shorter; the fades are
  unchanged. The copy setting (N = 63,328 = 1,979 blocks, triggers at frame
  0) is unaffected: CB14 (e) passes unchanged.
- **Why here, not in `crumpBit_grainPick()`:** the rounding needs the
  event frame, which the pure pick does not know.

Current text (WT 2076–2080):

```c
            /* The voice starts the candidate; its fade-in starts at 0. */
            const uint32_t pos = cand.start << 16;
            const uint32_t step = (uint32_t)((int32_t)cand.inc * 16);
            const uint32_t wrap = (cand.inc < 0) ? 0u - k.lim : k.lim;
            const float out = (float)cand.end * CRUMPBIT_GRAIN_FADE_K;
```

New text:

```c
            /*
             * The voice starts the candidate; its fade-in starts at 0.
             * end (G3): the candidate's span rounded down so that the event
             * frame plus the span is a block boundary (the mask works
             * because a block is a power of two). cand.end > 128, so the
             * rounded span keeps more than 96 frames; the clamp only
             * shortens it, so every read stays inside the candidate's
             * checked range.
             */
            const uint32_t end = ((ev + cand.end) &
                                  ~(CRUMPBIT_GRAIN_MIN_PERIOD - 1u)) - ev;
            const uint32_t pos = cand.start << 16;
            const uint32_t step = (uint32_t)((int32_t)cand.inc * 16);
            const uint32_t wrap = (cand.inc < 0) ? 0u - k.lim : k.lim;
            const float out = (float)end * CRUMPBIT_GRAIN_FADE_K;
```

#### E44 — `crumpBit_runGrain()` trigger, voice B: the rounded end

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2091–2091: **modify** (replace 1 line with 1; new lines 2391–2391).

- **What:** `end_b = end` (was `cand.end`).

Current text (WT 2091–2091):

```c
                end_b = cand.end;
```

New text:

```c
                end_b = end;
```

#### E45 — `crumpBit_runGrain()`: voice A's rounded end; the steal pad (G3)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2102–2117: **modify** (replace 16 lines with 25; new lines 2402–2426).

- **What:**
  - `end_a = end`;
  - the steal: `pad = (0 − (ev + 128)) & 31`; the stolen voice's `out`
    starts at `(128 + pad) / 128 ≥ 1` (the window holds 1 for `pad` frames,
    then falls 1/128 per frame as before); `end = age + 128 + pad`;
    `pend_next = 1 + ev + 128 + pad − frames`.
- **Why:** with the pad the stolen voice ends, and the deferred trigger
  fires, at a block start (frame 0). `ev + pad` is 0 or 32, so `pending`
  stays ≤ 129 (CB11's bound 161 holds).
- **Sound:** a steal's fade starts up to 31 frames later; the deferred
  grain starts up to 31 frames later.

Current text (WT 2102–2117):

```c
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
```

New text:

```c
                end_a = end;
                age_a = 0u;
            }
        } else {
            /*
             * The steal: the older voice's fade-out starts now and ends
             * one fade later, and the trigger waits until then. G3: pad,
             * the frames from ev + fade to the next block boundary
             * (0..31), so the voice ends, and the deferred trigger fires,
             * at a block start. out = (fade + pad)/128 >= 1: the window
             * holds 1 for the pad, then falls 1/128 per frame as before.
             */
            const uint32_t pad = (0u - (ev + CRUMPBIT_GRAIN_FADE)) &
                                 (CRUMPBIT_GRAIN_MIN_PERIOD - 1u);
            const float out = (float)(CRUMPBIT_GRAIN_FADE + pad) *
                              CRUMPBIT_GRAIN_FADE_K;

            if (voice) {
                s.out_b = out;
                end_b = age_b + CRUMPBIT_GRAIN_FADE + pad;
            } else {
                s.out_a = out;
                end_a = age_a + CRUMPBIT_GRAIN_FADE + pad;
            }
            pend_next = 1u + ev + CRUMPBIT_GRAIN_FADE + pad - frames;
```

#### E46 — `crumpBit_runGrain()` tail: no DC memories, no origin

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2152–2158: **modify** (replace 7 lines with 4; new lines 2461–2464).

- **What:** removes `g->dc_x/dc_y` (G4) and `feedback_prev` (H7) writes.

Current text (WT 2152–2158):

```c
    g->dc_x = s.dc_x;
    g->dc_y = s.dc_y;
    rt->ac_c_l = (int16_t)s.ac_cl;
    rt->ac_c_r = (int16_t)s.ac_cr;
    rt->ac_y_l = s.ac_yl;
    rt->ac_y_r = s.ac_yr;
    rt->feedback_prev = rt->feedback_raw;
```

New text:

```c
    rt->ac_c_l = (int16_t)s.ac_cl;
    rt->ac_c_r = (int16_t)s.ac_cr;
    rt->ac_y_l = s.ac_yl;
    rt->ac_y_r = s.ac_yr;
```

#### E47 — `crumpBit_runReverb()` comment: low-pass damping (R1)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2186–2188: **modify** (replace 3 lines with 6; new lines 2492–2497).

- **What:** the damping is low-pass only: `k = toneK[min(ton, 63)]`; 63 and
  above are an exact pass-through, so the high-pass half of the row plays
  flat in `rvb`.

Current text (WT 2186–2188):

```c
 * input muted); the per-line damping tilt (`ton`); the width (`dpn`); the
 * per-block gate that keeps feedback and wet off until `valid` covers the
 * longest line (no stale bytes recirculate after an Effect type change).
```

New text:

```c
 * input muted); the per-line damping (`ton`; S091 P5 R1: low-pass only,
 * k = crumpBit_toneK[min(ton, 63)], so 63 and above are an exact
 * pass-through and the high-pass half of the row plays flat in `rvb`); the
 * width (`dpn`); the per-block gate that keeps feedback and wet off until
 * `valid` covers the longest line (no stale bytes recirculate after an
 * Effect type change).
```

#### E48 — `crumpBit_runReverb()`: `tone_k` from the low-pass half

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2227–2229: **modify** (replace 3 lines with 7; new lines 2536–2542).

- **What:** `const float tone_k = crumpBit_toneK[min(tone_raw, 63)]`;
  `tone_a`/`tone_b` go.
- **Why (R1):** the tilt `a·s + b·y` cost two multiply-adds per line per
  frame. `crumpBit_toneK[63]` is exactly 1.0, so at 63 `s += 1·(y − s)`
  gives `s = y` bit for bit.
- **Host law:** CB15 (d): every `ton` above 63 plays bit-identically to 63
  over random `fbk`/`rte`; decay (15.24 dB) and freeze (2.07 dB) unchanged.

Current text (WT 2227–2229):

```c
    float tone_k;
    float tone_a;
    float tone_b;
```

New text:

```c
    /*
     * tone_k (R1): the damping pole, the `ton` table's low-pass half only;
     * crumpBit_toneK[63] is exactly 1, so s_k += 1 (y_k - s_k) gives
     * s_k = y_k bit for bit (flat).
     */
    const float tone_k = crumpBit_toneK[(rt->tone_raw > CRUMPBIT_TONE_FLAT)
                                        ? CRUMPBIT_TONE_FLAT : rt->tone_raw];
```

#### E49 — `crumpBit_runReverb()`: remove the tilt setup

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2253–2253: **remove** 1 line (nothing replaces them; the text after them starts at new line 2566).

- **What:** removes `crumpBit_toneSetup()` (R1).

Removed text:

```c
    crumpBit_toneSetup(rt->tone_raw, &tone_k, &tone_a, &tone_b);
```

#### E50 — `crumpBit_runReverb()` locals: `lines`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2263–2264: **modify** (replace 2 lines with 1; new lines 2575–2575).

- **What:** `lines = b->lines` (X2).

Current text (WT 2263–2264):

```c
    const uint32_t keep = b->keep;
    const uint32_t invert = b->invert;
```

New text:

```c
    const uint8_t *const lines = b->lines;
```

#### E51 — `crumpBit_runReverb()` loop locals: remove `z0..z3`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2277–2280: **remove** 4 lines (nothing replaces them; the text after them starts at new line 2588).

- **What:** the tilt outputs are gone; the Hadamard reads `s0..s3`.

Removed text:

```c
        float z0;
        float z1;
        float z2;
        float z3;
```

#### E52 — `crumpBit_runReverb()` loop: stage 1 call

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2291–2291: **modify** (replace 1 line with 1; new lines 2598–2598).

- **What:** passes `lines` (X2). `mix += mix_inc` stays (Q9).

Current text (WT 2291–2291):

```c
        crumpBit_stage1(left[n], right[n], keep, invert,
```

New text:

```c
        crumpBit_stage1(left[n], right[n], lines,
```

#### E53 — `crumpBit_runReverb()` loop: damping is the low-pass state (R1)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2309–2322: **modify** (replace 14 lines with 14; new lines 2616–2629).

- **What:** the four `z_k = a·s_k + b·y_k` lines go; the Hadamard sums are
  formed from `s_k` directly.
- **Why:** R1 (loop 145 → 131 with X2).

Current text (WT 2309–2322):

```c
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
```

New text:

```c
        /*
         * Damping per line (`ton`, R1): the one-pole low-pass state is the
         * line's damped value, s_k += k (y_k - s_k) (was a tilt
         * a s_k + b y_k; the low-pass side had a = 1, b = 0 already).
         */
        s0 += tone_k * (y0 - s0);
        s1 += tone_k * (y1 - s1);
        s2 += tone_k * (y2 - s2);
        s3 += tone_k * (y3 - s3);
        /* 4 x 4 Hadamard, scaled 1/2 (orthonormal: energy preserving). */
        h01 = s0 + s1;
        h01d = s0 - s1;
        h23 = s2 + s3;
        h23d = s2 - s3;
```

#### E54 — `crumpBit_enter()` comment: the filter hand-over (F1)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2439–2440: **modify** (replace 2 lines with 11; new lines 2746–2756).

- **What:** a switch between an SVF sub-type and a one-pole one moves the
  output filter's state: SVF low integrator → one-pole state; one-pole
  state → SVF low integrator with the band integrator cleared. For a held
  input both are the steady state. No crossfade (S091 P3 S7 stands).

Current text (WT 2439–2440):

```c
 * Inputs: runtime, the new sub-type, the seated bytes. Output: the union
 * holds the new engine's state; active_subtype = new.
```

New text:

```c
 * S091 P5 F1: a switch between an SVF sub-type and a one-pole one (`grn`,
 * `rvb`) hands the output filter's state over, so the filter carries on
 * from where it was instead of starting from a stale integrator: the
 * one-pole starts from the SVF's low integrator (svf_*2 -> svf_*1), the SVF
 * from the one-pole state as its low integrator with the band integrator
 * cleared (svf_*1 -> svf_*2, svf_*1 = 0); for a held input both are the
 * filter's steady state. No crossfade of the switch itself (S091 P3 S7: the
 * switch glitch is wanted); this only avoids an extra filter click.
 * Inputs: runtime, the new sub-type, the seated bytes. Output: the union
 * holds the new engine's state; active_subtype = new; the stage 4 state in
 * the new filter's form.
```

#### E55 — `crumpBit_enter()`: the hand-over (F1, Q12)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, after WT line 2450: **add** 12 lines (new lines 2767–2778).

- **What:** before the union is rebuilt:
  - SVF → one-pole: `svf_l1 = svf_l2`, `svf_r1 = svf_r2`;
  - one-pole → SVF: `svf_l2 = svf_l1`, `svf_r2 = svf_r1`, `svf_l1 = svf_r1
    = 0`;
  - SVF ↔ SVF and one-pole ↔ one-pole: untouched.
- **Why:** without it the one-pole would start from the SVF's band
  integrator, and the SVF from a stale low integrator: a filter click on
  top of the wanted switch glitch. In `enter`, so menu, MIDI, Scene and
  Pattern switches are all covered, no runtime field, no per-frame cost
  (the prototype's file-static flag goes).
- **Host law:** CB19 (d): the state moves exactly on every switch, and the
  largest output step at a switch is 0.013 against 0.008 steady (bound
  0.05; break 15 skips the hand-over and must fail).

New text:

```c
    /* F1: the output filter's state into the new sub-type's form. */
    if (crumpBit_filterIsOnePole(next) &&
        !crumpBit_filterIsOnePole(rt->active_subtype)) {
        rt->svf_l1 = rt->svf_l2;
        rt->svf_r1 = rt->svf_r2;
    } else if (!crumpBit_filterIsOnePole(next) &&
               crumpBit_filterIsOnePole(rt->active_subtype)) {
        rt->svf_l2 = rt->svf_l1;
        rt->svf_r2 = rt->svf_r1;
        rt->svf_l1 = 0.0f;
        rt->svf_r1 = 0.0f;
    }
```

#### E56 — `crumpBit_process()` comment: five steps, stage 4's two branches

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2515–2530: **modify** (replace 16 lines with 23; new lines 2843–2865).

- **What:** the per-block steps: 3 is the data-line table, ramp step and
  mix ramp; 5 is stage 4 with its coefficients **after** the engine, one
  branch per filter form, each computing its own coefficients and running
  its own loop.
- **Why the move:** the engines never read the filter coefficients, so
  computing them after the engine keeps them from being live across the
  engine call, and each branch is self-contained: the CPU gate's per-block
  path through one filter loop cannot pick up the other branch's
  coefficient code (the reason the prototype needed `throughpath.py`).

Current text (WT 2515–2530):

```c
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
```

New text:

```c
 * P2 §5; S091 P3 §2.1; S091 P5).
 *
 * What, per block:
 * 1. enter a new sub-engine when the resolved `sub` differs from the one
 *    that ran (any source: menu, MIDI, Scene, Pattern automation);
 * 2. seat the engine in the share (passes the block through untouched when
 *    the share is too small);
 * 3. the shared per-block work: the data-line table (S091 P5 X2, rebuilt
 *    only on a `bof`/`biv` change), the ramp step, the mix stage's ramp
 *    (origin from mix_prev);
 * 4. one sub-engine loop (stages 1, 2 and 3 per frame);
 * 5. stage 4, a second pass: the output filter's coefficients and its
 *    one-block type crossfade, then its loop. S091 P5 F1: `dly`/`cmb`/`pit`
 *    run the resonant TPT SVF (S091 P2, unchanged), `grn`/`rvb` the
 *    one-pole. Each branch computes its own coefficients and runs its own
 *    loop, so a block pays for one filter only, and the coefficients (now
 *    worked out after the engine, which never reads them) are not live
 *    across the engine call.
 * Inputs: runtime and normalized stereo io. Output: io->l/io->r in place,
 * arena bytes, runtime. Caller: effects_process().
 * Affiliates: crumpBit_run*(), crumpBit_enter(), crumpBit_seatActive(),
 * crumpBit_dataLinesFor(), crumpBit_filterMix(), crumpBit_filterG,
 * crumpBit_onePoleMixTable, crumpBit_onePoleK.
```

#### E57 — `crumpBit_process()` locals: remove the filter locals from the top

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2538–2557: **remove** 20 lines (nothing replaces them; the text after them starts at new line 2873).

- **What:** the coefficient, crossfade and state locals move into stage
  4's SVF branch (E59). `n` stays.

Removed text:

```c
    /* a1..a3 per channel (S091 P4 `bal`: each side has its own cutoff). */
    float a1l;
    float a2l;
    float a3l;
    float a1r;
    float a2r;
    float a3r;
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
```

#### E58 — `crumpBit_process()` step 3: the table; the coefficient block moves

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2578–2616: **modify** (replace 39 lines with 8; new lines 2893–2900).

- **What:**
  - `blk.lines = crumpBit_dataLinesFor(rt)` replaces the mask computation
    (X2);
  - the SVF coefficient block that sat before the engine call is removed
    (it reappears in stage 4's SVF branch, E59).

Current text: WT lines 2578–2616 (39 lines, replaced whole).

New text:

```c
    /* Data lines (X2): the table for this block's `bof`/`biv`. */
    blk.lines = crumpBit_dataLinesFor(rt);
    /* Ramp step per frame: the block's ramps land on their targets. */
    blk.per_frame = 1.0f / (float)blk.frames;
    /* The mix stage's ramp: origin from the previous block's raw row. */
    blk.mix = crumpBit_mixGain(rt->mix_prev);
    mix_to = crumpBit_mixGain(rt->mix_raw);
    blk.mix_inc = (mix_to - blk.mix) * blk.per_frame;
```

#### E59 — `crumpBit_process()` stage 4: the one-pole branch (F1) and the SVF branch

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2630–2678: **modify** (replace 49 lines with 142; new lines 2914–3055).

- **What:**
  - the `bal` spread (`tilt`, `fl`, `fr`) is computed once for both forms;
  - **one-pole branch** (`grn`/`rvb`): `kl = onePoleK[fl]`, `kr =
    onePoleK[fr]`; `cx`/`cl` ramp from the previous type's row to this
    one's over the block; per frame `s += k (x − s)`, `y = cx·x + cl·s`;
    the state is `svf_l1`/`svf_r1`;
  - **SVF branch** (`dly`/`cmb`/`pit`): the S091 P2/P4 coefficients and
    loop, unchanged in arithmetic (the `r_from` local folds into the call).
- **Why:** F1: the filter loop 31 → 16 in the two dearest sub-types; the
  table (Q6) removes two divides per block (the prototype's `g/(½ + g)`).
- **Measured:** SVF loop 31, one-pole loop 16; `process` per block 249 on
  the SVF path, 204 on the one-pole path (longest paths, including a
  sub-type switch's `enter`).
- **Host laws:** CB4 (bit-exact on `dly`), CB16 on both paths, CB19
  (−3 dB match ±0.01 dB measured, type mapping, `res` inert, hand-over).

Current text: WT lines 2630–2678 (49 lines, replaced whole).

New text:

```c
     * S091 P4 `bal` (row 22, 63 = 0): the right channel's cutoff index is
     * frq + (bal - 63), the left's frq - (bal - 63), each clamped to 0..127
     * (S091_P4_GRN_TWEAKS.md A6.2), for both filter forms. In each loop both
     * inputs are read before either output is stored, so a mono block
     * (right aliases left) filters the same input on both states and keeps
     * the left result, as the engines do.
     */
    {
        /* tilt: the balance in cutoff steps, -63..64. */
        const int32_t tilt = (int32_t)((rt->filter_bal > 127u) ? 127u
                                                                : rt->filter_bal) - 63;
        const int32_t f = (int32_t)((rt->filter_freq > 127u) ? 127u
                                                              : rt->filter_freq);
        /* fl, fr: each channel's cutoff index, frq -+ tilt, clamped. */
        const int32_t fl = (f - tilt < 0) ? 0 : ((f - tilt > 127) ? 127 : f - tilt);
        const int32_t fr = (f + tilt < 0) ? 0 : ((f + tilt > 127) ? 127 : f + tilt);

        if (crumpBit_filterIsOnePole(rt->active_subtype)) {
            /*
             * F1, `grn`/`rvb`: k per channel from the generated table (no
             * divide); the type's (cx, cl) ramp from the previous block's
             * type to this one's over the block. Types above `off` read as
             * `off`, as crumpBit_filterMix() does.
             */
            const float kl = crumpBit_onePoleK[fl];
            const float kr = crumpBit_onePoleK[fr];
            const float *from = crumpBit_onePoleMixTable[
                (rt->filter_type_prev > CRUMPBIT_FILTER_OFF)
                    ? CRUMPBIT_FILTER_OFF : rt->filter_type_prev];
            const float *to = crumpBit_onePoleMixTable[
                (rt->filter_type > CRUMPBIT_FILTER_OFF)
                    ? CRUMPBIT_FILTER_OFF : rt->filter_type];
            /* cx, cl: input and low-pass weights, ramped per frame. */
            float cx = from[0];
            float cl = from[1];
            const float cx_inc = (to[0] - cx) * blk.per_frame;
            const float cl_inc = (to[1] - cl) * blk.per_frame;
            /* s_l, s_r: the one-pole states (the svf_*1 fields). */
            float s_l = rt->svf_l1;
            float s_r = rt->svf_r1;

            for (n = 0u; n < blk.frames; n++) {
                const float xl = blk.left[n];
                const float xr = blk.right[n];

                /* Output-mix ramp: lands on this block's type at the end. */
                cx += cx_inc;
                cl += cl_inc;
                /* One-pole low-pass: s moves k of the way towards x. */
                s_l += kl * (xl - s_l);
                s_r += kr * (xr - s_r);
                /* LP s, HP x - s, flat x: one weighted sum for all. */
                blk.right[n] = cx * xr + cl * s_r;
                blk.left[n] = cx * xl + cl * s_l;
            }
            rt->svf_l1 = s_l;
            rt->svf_r1 = s_r;
        } else {
            /*
             * `dly`/`cmb`/`pit`: the Simper TPT SVF (S091 P2): g from the
             * voice-law table, k = 2R, a1 = 1/(1 + g(g + k)), a2 = g a1,
             * a3 = g a2 per channel (at bal 63 both sets are the S091 P2
             * set bit for bit); the output mix crossfades over the block.
             */
            const float r = crumpBit_filterDamping(rt->filter_reso);
            /* a1..a3 per channel (S091 P4 `bal`: each side its cutoff). */
            float a1l;
            float a2l;
            float a3l;
            float a1r;
            float a2r;
            float a3r;
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

            crumpBit_filterCoeffs(crumpBit_filterG[fl], r, &a1l, &a2l, &a3l);
            crumpBit_filterCoeffs(crumpBit_filterG[fr], r, &a1r, &a2r, &a3r);
            crumpBit_filterMix(rt->filter_type_prev,
                               crumpBit_filterDamping(rt->filter_reso_prev),
                               &cx, &cb, &cl);
            crumpBit_filterMix(rt->filter_type, r, &cx_to, &cb_to, &cl_to);
            cx_inc = (cx_to - cx) * blk.per_frame;
            cb_inc = (cb_to - cb) * blk.per_frame;
            cl_inc = (cl_to - cl) * blk.per_frame;
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
                 * v2 = ic2 + a2*ic1 + a3*v3; then the trapezoidal state
                 * updates ic1 = 2*v1 - ic1 and ic2 = 2*v2 - ic2.
                 */
                v3 = xl - s_l2;
                v1 = a1l * s_l1 + a2l * v3;
                v2 = s_l2 + a2l * s_l1 + a3l * v3;
                s_l1 = 2.0f * v1 - s_l1;
                s_l2 = 2.0f * v2 - s_l2;
                yl = cx * xl + cb * v1 + cl * v2;

                v3 = xr - s_r2;
                v1 = a1r * s_r1 + a2r * v3;
                v2 = s_r2 + a2r * s_r1 + a3r * v3;
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
        }
    }
```

#### E60 — `crumpBit_effectiveMax()` comment: `dpn` no longer clamped (A)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2709–2714: **modify** (replace 6 lines with 6; new lines 3086–3091).

- **What:** removes the Position clamp from the description; any row other
  than `rte` returns 255.

Current text (WT 2709–2714):

```c
 * half line, the `rvb` word buffer). S091 P4: for `dpn` in `grn` (Position),
 * the last rhythmic lag that fits a half line (crumpBit_grainPosLimit(); the
 * follow and scatter values below 64 are never limited); in every other
 * sub-type `dpn` is not limited. Any other row: 255. The runtime may be NULL
 * (a page edit of a Scene that is not live): the delay rule applies, and
 * `dpn` is not limited. Stored values are never rewritten (A30).
```

New text:

```c
 * half line, the `rvb` word buffer). Any other row: 255. S091 P5 A: `dpn`
 * (Position in `grn`) is no longer clamped here: the engine limits the stop
 * itself (crumpBit_grainPick(), with the fit crumpBit_runGrain() caches),
 * which saves the manager's per-block Position search in every sub-type.
 * The runtime may be NULL (a page edit of a Scene that is not live): the
 * delay rule applies. Stored values are never rewritten (A30).
```

#### E61 — `crumpBit_effectiveMax()`: remove the `dpn` case (A)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c`, WT lines 2724–2726: **remove** 3 lines (nothing replaces them; the text after them starts at new line 3101).

- **What:** removes the `CRUMPBIT_PARAM_DLY_PAN` branch.
- **Why:** with C2 the manager never asks for `dpn`; the page edit clamp
  (`effects_paramEffectiveMax()`) only asks for BUFFER_DEPENDENT rows.
  Measured: `effectiveMax` per block 29 → 23, and no per-block Position
  search (76) in `grn`.

Removed text:

```c
    if (index == CRUMPBIT_PARAM_DLY_PAN)
        return (rt && rt->subtype == CRUMPBIT_SUBTYPE_GRAIN)
            ? crumpBit_grainPosLimit(share) : 255u;
```

### 3.3 `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`

#### P1 — Filter-type comment: the one-pole mapping

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h`, WT lines 174–174: **modify** (replace 1 line with 5; new lines 174–178).

- **What:** adds the `grn`/`rvb` mapping of the eight stored values onto
  low-pass / high-pass / flat, and the new accessors
  (`crumpBit_onePoleMixTable`, `crumpBit_uiFormatValue3()`).

Current text (WT 174–174):

```c
 * Accessors: crumpBit_descriptors (default/max), crumpBit_filterMix().
```

New text:

```c
 * S091 P5 F1: in `grn` and `rvb` the filter is a one-pole and the eight
 * values map onto three: LP, LP2, BP, UBP low-pass; HP, notch high-pass;
 * peak, off flat (the page shows LP/HP/off there).
 * Accessors: crumpBit_descriptors (default/max), crumpBit_filterMix(),
 * crumpBit_onePoleMixTable, crumpBit_uiFormatValue3().
```

### 3.4 `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`

#### C1 — Descriptor comment: `dpn` back to a plain image row

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, WT lines 42–45: **modify** (replace 4 lines with 6; new lines 42–47).

- **What:** row 10's description: S091 P4 clamped Position in the manager;
  S091 P5 A moved the clamp into the engine, so the row is a plain image
  row; the page still shows the stop that plays.

Current text (WT 42–45):

```c
 *   Pattern automation, like voice rows. S091 P4: delay pan (10) is also
 *   BUFFER_DEPENDENT, because in `grn` it is Position, whose rhythmic lags
 *   are clamped to the line (crumpBit_ops.effective_max); in the other
 *   sub-types it is never limited.
```

New text:

```c
 *   Pattern automation, like voice rows. Delay pan (10) is Position in
 *   `grn`, whose rhythmic lags must fit the line: S091 P4 clamped it in the
 *   manager (BUFFER_DEPENDENT); S091 P5 A moved the clamp into the engine
 *   (crumpBit_grainPick()), so the row is a plain image row again and the
 *   manager's per-block Position search is gone. The page still shows the
 *   stop that plays (crumpBit_grainPosLimit()).
```

#### C2 — `dpn` row: no BUFFER_DEPENDENT flag (A)

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, WT lines 116–119: **modify** (replace 4 lines with 3; new lines 118–120).

- **What:** `EFFECT_ROW_FLAGS(…, EFFECT_PARAM_FLAG_BUFFER_DEPENDENT)` →
  `EFFECT_ROW(…)`; key, names, dtype, flags, modulation, default and
  maximum unchanged.
- **Why:** the manager re-applied BUFFER_DEPENDENT rows every block
  (`effects_service()`), calling `effective_max` and its Position search in
  every sub-type. Storage and `.fx` files are unaffected (the flag is
  runtime-only).
- **Affiliates:** EffectsManager.c:372–385 and :2088–2107 (generic loops,
  unchanged), E61.

Current text (WT 116–119):

```c
    /* S091 P4: BUFFER_DEPENDENT for the `grn` Position clamp. */
    EFFECT_ROW_FLAGS("crump_dly_pan", "Delay", "DlyPan", "dpn", DTYPE_PM63,
                     EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u,
                     EFFECT_PARAM_FLAG_BUFFER_DEPENDENT),
```

New text:

```c
    /* S091 P5 A: the `grn` Position clamp is the engine's; no flag. */
    EFFECT_ROW("crump_dly_pan", "Delay", "DlyPan", "dpn", DTYPE_PM63,
               EFFECT_FLAGS_IMAGE, EFFECT_MOD_0_127, 63u, 127u),
```

#### C3 — Value-text comment: the P5 labels

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, WT lines 401–401: **modify** (replace 1 line with 10; new lines 402–411).

- **What:** documents the four new labels: `typ` mapped and `res` `---` in
  `grn`/`rvb` (F1, Q5), `ton` above 63 shown `  0` in `rvb` (R1, Q10),
  `fbk` in `grn` as a percentage 0..98 (G4, Q7).

Current text (WT 401–401):

```c
 * - `aux` where it has no meaning yet (`dly`, `cmb`, `rvb`): `---`.
```

New text:

```c
 * - `aux` where it has no meaning yet (`dly`, `cmb`, `rvb`): `---`;
 * - S091 P5, what the two cheaper paths play:
 *   - `typ` in `grn`/`rvb` (F1, Q5): the one-pole's mapped type, `LP`
 *     (LP, LP2, BP, UBP), `HP` (HP, notch) or `off` (peak, off); the stored
 *     value is untouched, so `dly`/`cmb`/`pit` still play it as stored;
 *   - `res` in `grn`/`rvb` (F1): `---` (the one-pole has no resonance);
 *   - `ton` (`dmp`) in `rvb` above 63 (R1, Q10): `  0`, as 63 shows (the
 *     damping is low-pass only; values above 63 play flat);
 *   - `fbk` in `grn` (G4, Q7): the echo gain in percent, 0..98
 *     (crumpBit_grainFeedbackGain() x 100, rounded).
```

#### C4 — `crumpBit_uiFormatValue3()`: the P5 labels

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c`, after WT line 502: **add** 34 lines (new lines 513–546).

- **What:**
  - `grn`/`rvb`, `typ`: `LP ` (LP, LP2, BP, UBP), `HP ` (HP, notch), `off`
    (peak, off);
  - `grn`/`rvb`, `res`: `---`;
  - `rvb`, `ton` > 63: `numtostrps(out, 0)`, the text 63 shows (`  0`);
  - `grn`, `fbk`: `round(value · 98 / 127)` via `numtostrpu`, i.e. 100 ×
    the echo gain.
- **Why:** the page shows what plays. The stored values are untouched, so
  the other sub-types still play and show them as stored, and pots, locks,
  storage and automation keep raw values (the hook only formats).
- **Inputs:** row index, shown value, 3-character buffer. **Output:**
  nonzero when written. **Accessor:** `menuEffects_formatParamValue3()`
  (menuEffects.c), which serves compact cells, the full view and held
  steps; the hook runs before generic and MENU-dtype formatting
  (`menu_formatCellValue3()`), so `typ`'s MENU_FILTER text is replaced.
- **Affiliates:** `crumpBit_onePoleMixTable` (the mapping must match),
  `numtostrps()`/`numtostrpu()` (menu.c:11963).

New text:

```c
        return 1u;
    }
    /*
     * S091 P5 F1: the one-pole sub-types show the type they play and no
     * resonance. The mapping is crumpBit_onePoleMixTable's (CrumpBitEffect.c):
     * the low-pass rows, then HP and notch as high-pass, the rest flat.
     */
    if (sub == CRUMPBIT_SUBTYPE_GRAIN || sub == CRUMPBIT_SUBTYPE_REVERB) {
        if (index == CRUMPBIT_PARAM_FILTER_TYPE) {
            const char *mapped = "off";

            if (value == CRUMPBIT_FILTER_LP || value == CRUMPBIT_FILTER_LP2 ||
                value == CRUMPBIT_FILTER_BP || value == CRUMPBIT_FILTER_UBP)
                mapped = "LP ";
            else if (value == CRUMPBIT_FILTER_HP ||
                     value == CRUMPBIT_FILTER_NOTCH)
                mapped = "HP ";
            memcpy(out, mapped, 3u);
            return 1u;
        }
        if (index == CRUMPBIT_PARAM_FILTER_RESO) {
            memcpy(out, "---", 3u);
            return 1u;
        }
    }
    /* S091 P5 R1: `rvb` damping above 63 plays flat, so it shows 63's 0. */
    if (sub == CRUMPBIT_SUBTYPE_REVERB && index == CRUMPBIT_PARAM_TONE &&
        value > CRUMPBIT_TONE_FLAT) {
        numtostrps(out, 0);
        return 1u;
    }
    /* S091 P5 G4: `grn` echo gain 0..0.98 as a percentage, 0..98. */
    if (sub == CRUMPBIT_SUBTYPE_GRAIN && index == CRUMPBIT_PARAM_FEEDBACK) {
        numtostrpu(out, (uint8_t)((value * 98u + 63u) / 127u), ' ');
```

### 3.5 `Core/DSP/Effects/CrumpBit/CrumpBitTables.h`

#### B1 — Tables comment: `crumpBit_onePoleK`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitTables.h`, after WT line 53: **add** 6 lines (new lines 54–59).

- **What:** documents the new table: the one-pole k per `frq` index,
  matched so its −3 dB point is the SVF's at `res` 0; 0 at index 0,
  increasing, below 1; a lookup replacing two divides per block.

New text:

```c
 * S091 P5 output filter table (plan F1, Q6):
 * - crumpBit_onePoleK[frq]: the one-pole coefficient k (s += k (x - s)) of
 *   the `grn`/`rvb` output filter, matched so its -3 dB point is the TPT
 *   SVF's at `res` 0 for the same index (crumpBit_filterG[frq]); 0 at frq 0
 *   (closed), strictly increasing, below 1. A lookup replaces two divides
 *   per block.
```

#### B2 — Declaration: `crumpBit_onePoleK`

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitTables.h`, after WT line 81: **add** 1 line (new lines 88–88).

- **What:** `extern const float crumpBit_onePoleK[CRUMPBIT_TABLE_SIZE];`
  (128 floats, 512 B flash).

New text:

```c
extern const float crumpBit_onePoleK[CRUMPBIT_TABLE_SIZE];
```

### 3.6 `Core/DSP/Effects/CrumpBit/CrumpBitTables.c` (generated)

#### R1 — `crumpBit_onePoleK`, appended by the generator

**Location:** `Core/DSP/Effects/CrumpBit/CrumpBitTables.c`, after WT line 353: **add** 36 lines (new lines 354–389). Do not edit by hand: run `python3 tools/plots/gen_crumpbit_tables.py` from the repository root after G1–G8, then `make -C tools/dsp_test crumpbit_tables` (`crumpbit tables OK`). Every existing table is byte-identical; the file only grows at its end.

- **What:** 128 floats, k per `frq` index; k[0] = 0 (closed, as `crumpBit_filterG[0]`), k[127] < 1.
- **Why (Q6):** replaces the prototype's two divides per block; matched to the SVF's −3 dB point at `res` 0 (C7, CB6, CB19 (a)).
- **Size:** 512 B flash.

New text (as generated):

```c

/* grn/rvb one-pole output filter k, frq 0..127: -3 dB at the SVF's (res 0). */
const float crumpBit_onePoleK[128] = {
    0.000000000e+00f, 7.602298973e-04f, 1.531381919e-03f, 2.313693007e-03f,  /*   0..  3 */
    3.107407000e-03f, 3.912774889e-03f, 4.730055082e-03f, 5.559513678e-03f,  /*   4..  7 */
    6.401424756e-03f, 7.256070676e-03f, 8.123742391e-03f, 9.004739784e-03f,  /*   8.. 11 */
    9.899372003e-03f, 1.080795783e-02f, 1.173082605e-02f, 1.266831586e-02f,  /*  12.. 15 */
    1.362077727e-02f, 1.458857153e-02f, 1.557207163e-02f, 1.657166271e-02f,  /*  16.. 19 */
    1.758774262e-02f, 1.862072242e-02f, 1.967102692e-02f, 2.073909529e-02f,  /*  20.. 23 */
    2.182538167e-02f, 2.293035576e-02f, 2.405450357e-02f, 2.519832807e-02f,  /*  24.. 27 */
    2.636234997e-02f, 2.754710850e-02f, 2.875316224e-02f, 2.998108997e-02f,  /*  28.. 31 */
    3.123149164e-02f, 3.250498931e-02f, 3.380222819e-02f, 3.512387770e-02f,  /*  32.. 35 */
    3.647063266e-02f, 3.784321447e-02f, 3.924237241e-02f, 4.066888499e-02f,  /*  36.. 39 */
    4.212356137e-02f, 4.360724292e-02f, 4.512080480e-02f, 4.666515770e-02f,  /*  40.. 43 */
    4.824124965e-02f, 4.985006791e-02f, 5.149264110e-02f, 5.317004131e-02f,  /*  44.. 47 */
    5.488338645e-02f, 5.663384271e-02f, 5.842262720e-02f, 6.025101075e-02f,  /*  48.. 51 */
    6.212032091e-02f, 6.403194511e-02f, 6.598733412e-02f, 6.798800564e-02f,  /*  52.. 55 */
    7.003554824e-02f, 7.213162554e-02f, 7.427798065e-02f, 7.647644097e-02f,  /*  56.. 59 */
    7.872892336e-02f, 8.103743965e-02f, 8.340410259e-02f, 8.583113221e-02f,  /*  60.. 63 */
    8.832086273e-02f, 9.087574999e-02f, 9.349837937e-02f, 9.619147454e-02f,  /*  64.. 67 */
    9.895790666e-02f, 1.018007046e-01f, 1.047230658e-01f, 1.077283681e-01f,  /*  68.. 71 */
    1.108201826e-01f, 1.140022878e-01f, 1.172786845e-01f, 1.206536126e-01f,  /*  72.. 75 */
    1.241315690e-01f, 1.277173271e-01f, 1.314159587e-01f, 1.352328573e-01f,  /*  76.. 79 */
    1.391737638e-01f, 1.432447948e-01f, 1.474524740e-01f, 1.518037656e-01f,  /*  80.. 83 */
    1.563061127e-01f, 1.609674783e-01f, 1.657963914e-01f, 1.708019975e-01f,  /*  84.. 87 */
    1.759941152e-01f, 1.813832980e-01f, 1.869809043e-01f, 1.927991744e-01f,  /*  88.. 91 */
    1.988513164e-01f, 2.051516031e-01f, 2.117154793e-01f, 2.185596826e-01f,  /*  92.. 95 */
    2.257023799e-01f, 2.331633192e-01f, 2.409640022e-01f, 2.491278791e-01f,  /*  96.. 99 */
    2.576805675e-01f, 2.666501018e-01f, 2.760672154e-01f, 2.859656607e-01f,  /* 100..103 */
    2.963825728e-01f, 3.073588826e-01f, 3.189397859e-01f, 3.311752750e-01f,  /* 104..107 */
    3.441207395e-01f, 3.578376421e-01f, 3.723942726e-01f, 3.878665770e-01f,  /* 108..111 */
    4.043390513e-01f, 4.219056680e-01f, 4.406707732e-01f, 4.607498313e-01f,  /* 112..115 */
    4.822697909e-01f, 5.053686504e-01f, 5.301934519e-01f, 5.568952673e-01f,  /* 116..119 */
    5.856184625e-01f, 6.164789971e-01f, 6.495213256e-01f, 6.846324407e-01f,  /* 120..123 */
    7.213672324e-01f, 7.585835060e-01f, 7.936538184e-01f, 8.207153746e-01f,  /* 124..127 */
};
```


---

## 4. Tools and host tests

### 4.1 `tools/plots/gen_crumpbit_tables.py`

#### G1 — Generator docstring: S091 P5

**Location:** `tools/plots/gen_crumpbit_tables.py`, WT lines 3–3: **modify** (replace 1 line with 1; new lines 3–3).

- **What:** the one-line summary names the S091 P5 table.

Current text (WT 3–3):

```python
the `grn` rework tables S091 P4).
```

New text:

```python
the `grn` rework tables S091 P4; the one-pole output filter S091 P5).
```

#### G2 — Generator docstring: `crumpBit_onePoleK`

**Location:** `tools/plots/gen_crumpbit_tables.py`, after WT line 68: **add** 11 lines (new lines 69–79).

- **What:** the law: the SVF at R = 1 is two equal poles, −3 dB where
  `tan(w/2) = g·√(√2 − 1)`; the one-pole with pole `a = 1 − k` is −3 dB at
  `w` where `a = (2 − cos w) − √((2 − cos w)² − 1)`.

New text:

```python
            S091 P5 one-pole output filter (`grn`, `rvb`; plan F1, Q6):
              crumpBit_onePoleK[v]   the coefficient k of s += k (x - s) for
                                     `frq` index v, matched to the SVF at
                                     `res` 0: that filter (R = 1, two equal
                                     poles) is -3 dB where
                                     tan(w/2) = g sqrt(sqrt(2) - 1), g =
                                     crumpBit_filterG[v]; the one-pole with
                                     pole a = 1 - k is -3 dB at w where
                                     a = (2 - cos w) - sqrt((2 - cos w)^2 - 1).
                                     0 at v 0 (closed, as filterG[0] = 0),
                                     strictly increasing, below 1.
```

#### G3 — Generator docstring: check C7

**Location:** `tools/plots/gen_crumpbit_tables.py`, WT lines 84–84: **modify** (replace 1 line with 4; new lines 95–98).

- **What:** C7: k[0] = 0, strictly increasing, below 1, and |H(w3)|² = ½
  within 10⁻⁹ at every index's SVF −3 dB frequency.

Current text (WT 84–84):

```python
            (entry 0 unused, 1) and strictly decreasing from there; C5 --check: regenerate in memory and
```

New text:

```python
            (entry 0 unused, 1) and strictly decreasing from there; C7
            (S091 P5) one-pole k: 0 at index 0, strictly increasing, below
            1, and |H(w3)|^2 = 1/2 within 1e-9 at every index's SVF -3 dB
            frequency; C5 --check: regenerate in memory and
```

#### G4 — Generator: `svf_w3()`, `one_pole_k_table()`, `one_pole_gain2()`

**Location:** `tools/plots/gen_crumpbit_tables.py`, after WT line 204: **add** 28 lines (new lines 219–246).

- **What:**
  - `svf_w3(g)`: the SVF's −3 dB angle at `res` 0, `2·atan(g·√(√2 − 1))`
    (the prototype `1/(s+1)²` is −3 dB at `s = √(√2 − 1)`, mapped back
    through the bilinear pre-warp `tan(w/2) = g·s`);
  - `one_pole_k_table(filt)`: `k = 1 − a` per index, 0 where g = 0;
  - `one_pole_gain2(k, w)`: `k² / (1 − 2a cos w + a²)` for the C7 check.
- **Why (Q6):** a generated table matches the voice-law cutoff exactly and
  removes the per-block divides.
- **Derivation:** `|H|² = ½` ⇔ `2(1 − a)² = 1 − 2a c + a²` ⇔ `a² − (4 −
  2c) a + 1 = 0`; the smaller root is the stable pole.

New text:

```python
def svf_w3(g):
    """The -3 dB angular frequency (radians per sample) of the TPT SVF
    low-pass at R = 1 (`res` 0) with pre-warped cutoff g: the prototype
    1/(s + 1)^2 is -3 dB at s = sqrt(sqrt(2) - 1), mapped back through
    tan(w/2) = g s."""
    return 2.0 * math.atan(g * math.sqrt(math.sqrt(2.0) - 1.0))


def one_pole_k_table(filt):
    """k for each `frq` index: the one-pole y += k (x - y) whose -3 dB point
    is the SVF's at `res` 0 (S091 P5 F1, Q6)."""
    vals = []
    for g in filt:
        if g == 0.0:
            vals.append(0.0)
            continue
        c = math.cos(svf_w3(g))
        a = (2.0 - c) - math.sqrt((2.0 - c) ** 2 - 1.0)
        vals.append(1.0 - a)
    return vals


def one_pole_gain2(k, w):
    """|H(w)|^2 of y += k (x - y): k^2 / (1 - 2 a cos w + a^2), a = 1 - k."""
    a = 1.0 - k
    return k * k / (1.0 - 2.0 * a * math.cos(w) + a * a)


```

#### G5 — Generator `render()`: unpack `opk`

**Location:** `tools/plots/gen_crumpbit_tables.py`, WT lines 242–242: **modify** (replace 1 line with 1; new lines 284–284).

- **What:** the extra tuple carries the new table.

Current text (WT 242–242):

```python
    comb, win, semi, frac, size, decay, gtime, gstop, gdens = extra
```

New text:

```python
    comb, win, semi, frac, size, decay, gtime, gstop, gdens, opk = extra
```

#### G6 — Generator `render()`: emit `crumpBit_onePoleK`

**Location:** `tools/plots/gen_crumpbit_tables.py`, after WT line 260: **add** 2 lines (new lines 303–304).

- **What:** appends the table after `crumpBit_grainDensity` (so the
  committed file only grows at its end, R1).

New text:

```python
    lines += render_table("crumpBit_onePoleK",
                          "grn/rvb one-pole output filter k, frq 0..127: -3 dB at the SVF's (res 0).", opk)
```

#### G7 — Generator `main()`: build the table

**Location:** `tools/plots/gen_crumpbit_tables.py`, after WT line 268: **add** 1 line (new lines 313–313).

- **What:** `opk = one_pole_k_table(filt)` from the same `filterG` values.

New text:

```python
    opk = one_pole_k_table(filt)
```

#### G8 — Generator `main()`: check C7; pass the table

**Location:** `tools/plots/gen_crumpbit_tables.py`, WT lines 286–293: **modify** (replace 8 lines with 13; new lines 331–343).

- **What:** the C7 checks; `opk` into `render()`; the failure message
  names C7.

Current text (WT 286–293):

```python
    text = render(rate, tone, filt,
                  (comb, win, semi, frac, size, decay, gtime, gstop, gdens))
    if "--check" in sys.argv:
        ok = ok and open(PATH).read() == text
        print("crumpbit tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("crumpbit tables: C1-C4/C6 failed")
```

New text:

```python
    ok = ok and opk[0] == 0.0 and all(a < b for a, b in zip(opk, opk[1:]))
    ok = ok and all(0.0 <= k < 1.0 for k in opk)
    ok = ok and all(abs(one_pole_gain2(k, svf_w3(g)) - 0.5) < 1e-9
                    for k, g in zip(opk[1:], filt[1:]))
    text = render(rate, tone, filt,
                  (comb, win, semi, frac, size, decay, gtime, gstop, gdens,
                   opk))
    if "--check" in sys.argv:
        ok = ok and open(PATH).read() == text
        print("crumpbit tables", "OK" if ok else "FAILED")
        return 0 if ok else 1
    if not ok:
        print("crumpbit tables: C1-C4/C6/C7 failed")
```

### 4.2 `tools/dsp_test/elf_looppath.py`

#### L1 — `elf_looppath.py` docstring: `--rank`; importers

**Location:** `tools/dsp_test/elf_looppath.py`, WT lines 30–35: **modify** (replace 6 lines with 13; new lines 30–42).

- **What:** documents `--rank K` and that `elf_blockpath.py` and
  `crumpbit_cpu.py` import the module.

Current text (WT 30–35):

```python
            laid out contiguously measure the same with it).
Outputs:    one line per loop; exit 1 when --max is exceeded.
Accessors:  tools/dsp_test/Makefile crumpbit_elf_check; by hand.
Affiliates: elf_loopdump.py (loop text compare), fpseq.py (value ops).
"""
import argparse
```

New text:

```python
            laid out contiguously measure the same with it), --rank K
            (S091 P5: --max applies to the K-th longest loop, default 1;
            crumpBit_process() has two filter loops, the SVF and the
            one-pole, each with its own limit).
Outputs:    one line per loop; exit 1 when --max is exceeded.
Accessors:  tools/dsp_test/Makefile crumpbit_elf_check; by hand; S091 P5:
            imported by elf_blockpath.py and crumpbit_cpu.py (rows(),
            successors(), loop_paths(); disassembly() runs objdump once per
            ELF and process).
Affiliates: elf_loopdump.py (loop text compare), fpseq.py (value ops).
"""
import argparse
import functools
```

#### L2 — `elf_looppath.py`: `disassembly()` cached once per ELF

**Location:** `tools/dsp_test/elf_looppath.py`, WT lines 47–50: **modify** (replace 4 lines with 11; new lines 54–64).

- **What:** the `objdump -d` text is produced by an `lru_cache`d
  `disassembly(elf)`; `rows()` reads it.
- **Why:** the CPU gate reads about twenty functions of one image; without
  the cache each would disassemble the whole ELF again (minutes).

Current text (WT 47–50):

```python
def rows(elf, sym):
    text = subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn',
                           elf], check=True, capture_output=True,
                          text=True).stdout
```

New text:

```python
@functools.lru_cache(maxsize=None)
def disassembly(elf):
    """The ELF's whole `objdump -d` text, once per process (S091 P5: the CPU
    gate reads about twenty functions of one image)."""
    return subprocess.run(['arm-none-eabi-objdump', '-d', '--no-show-raw-insn',
                           elf], check=True, capture_output=True,
                          text=True).stdout


def rows(elf, sym):
    text = disassembly(elf)
```

#### L3 — `elf_looppath.py`: `cycle_nodes()`; `loop_paths()` searches only cycle nodes

**Location:** `tools/dsp_test/elf_looppath.py`, WT lines 88–96: **modify** (replace 9 lines with 42; new lines 102–143).

- **What:**
  - `cycle_nodes(r, idx, h, inside)`: the instructions reachable from the
    head and able to reach it (forward and backward reachability, both
    through instructions accepted by `inside`);
  - `loop_paths()` follows only those instructions.
- **Why:** a path head → … → head can only use instructions on a cycle
  through the head, so the result is identical, but the search no longer
  walks every path through cold blocks that never return. The implemented
  `crumpBit_runGrain` has heads whose in-range spans reach 1,144
  instructions; the old search did not finish in 120 s, the new one takes
  0.35 s. Verified identical on the P4 image (136/133) and the prototype
  (118/118), and on all 43 mixer loops.

Current text (WT 88–96):

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
```

New text:

```python
def cycle_nodes(r, idx, h, inside):
    """The instructions on some cycle through index h: reachable from h and
    able to reach h, both through instructions accepted by inside(j). A
    path h -> ... -> h can only use these, so loop_paths() searches no
    other (S091 P5: the S091 P5 crumpBit_runGrain has cold blocks whose
    conditional branches close far back into the function; the old search
    walked every path through them that never returned, for minutes)."""
    n = len(r)
    succ = [[j for j in successors(r, idx, i) if inside(j)] if inside(i)
            else [] for i in range(n)]
    pred = [[] for _ in range(n)]
    for i in range(n):
        for j in succ[i]:
            pred[j].append(i)
    seen = []
    for edges in (succ, pred):
        mark = {h}
        todo = [h]
        while todo:
            i = todo.pop()
            for j in edges[i]:
                if j not in mark:
                    mark.add(j)
                    todo.append(j)
        seen.append(mark)
    return seen[0] & seen[1]


def loop_paths(r, idx, head, last=None):
    """Every simple path head -> ... -> head, as (length, vdivs, indices).
    With `last` (an index), only instructions idx[head]..last are followed.
    Only instructions on a cycle through the head are followed
    (cycle_nodes()): the result is the same, the search far smaller."""
    found = []
    h = idx[head]
    on_cycle = cycle_nodes(r, idx, h,
                           lambda j: last is None or h <= j <= last)
    stack = [(h, [h])]
    while stack:
        i, path = stack.pop()
        for j in successors(r, idx, i):
            if j not in on_cycle:
```

#### L4 — `elf_looppath.py`: the `--rank` option

**Location:** `tools/dsp_test/elf_looppath.py`, after WT line 115: **add** 1 line (new lines 163–163).

- **What:** `--rank K`, default 1.

New text:

```python
    ap.add_argument('--rank', type=int, default=1)
```

#### L5 — `elf_looppath.py`: `--max` against the K-th longest loop

**Location:** `tools/dsp_test/elf_looppath.py`, WT lines 148–150: **modify** (replace 3 lines with 6; new lines 196–201).

- **What:** `--max` compares `loops[rank − 1]`; a rank beyond the loop
  count exits with a message.
- **Why:** `crumpBit_process()` now has two filter loops with their own
  limits (31 and 16).

Current text (WT 148–150):

```python
    if a.max is not None and loops[0][0] > a.max:
        print(f'elf_looppath.py: FAIL {a.sym} longest {loops[0][0]} > '
              f'{a.max}')
```

New text:

```python
    if a.rank < 1 or a.rank > len(loops):
        sys.exit(f'elf_looppath.py: {a.sym} has {len(loops)} loops, '
                 f'no rank {a.rank}')
    if a.max is not None and loops[a.rank - 1][0] > a.max:
        print(f'elf_looppath.py: FAIL {a.sym} loop {a.rank} '
              f'{loops[a.rank - 1][0]} > {a.max}')
```

### 4.3 `tools/dsp_test/elf_blockpath.py` (new file)

#### N1 — the per-block path tool

**Location:** new file `tools/dsp_test/elf_blockpath.py` (executable, `chmod +x` like its neighbours): **add** 141 lines.

- **What:** the longest path from a function's entry to any exit, with
  every instruction on a frame loop weighted 0: the per-block work on the
  worst branch choices. Frame loops are found as `elf_looppath.py
  --cond-heads --in-range` finds them (it imports that module: `rows()`,
  `successors()`, `loop_paths()`). An iterative DFS drops the edges back to
  a node on the stack, which removes every cycle; `down[i]` (longest to an
  exit) is one pass over the post-order. `--via-loop K` forces the path
  through the head of the K-th longest loop: `up[i]` (longest from the
  entry, over the reversed post-order, a topological order) plus `down[i]`.
  Calls count 1 and are listed, so the caller adds the callee's own count.
- **Why:** plan §8.4 and Q15: the session's `blockpath.py` and
  `throughpath.py` as one repository tool. Checked against the prototype
  image: it reproduces the session's figures exactly (`runGrain` 779,
  `runDelay` 154, `runComb` 136, `runPitch` 215, `runReverb` 255,
  `effectiveMax` 23).
- **Inputs:** `--elf`, `--sym` (suffixes matched), `--via-loop`, `--max`.
  **Outputs:** one line; exit 1 above `--max`.
- **Accessors:** `crumpbit_cpu.py` (N2); by hand.

```python
#!/usr/bin/env python3
"""Longest per-block path through a function, its frame loops weighted 0.

What:       disassembles one function of the linked firmware ELF (as
            elf_looppath.py does) and reports the longest acyclic path from
            its entry to any exit, in instructions, where every instruction
            on a frame loop counts 0: the per-block work on the worst branch
            choices, which the per-frame loop counts (elf_looppath.py) do not
            include. Loops are found as elf_looppath.py --cond-heads
            --in-range finds them (the targets of conditional backward
            branches, each searched between its head and its furthest
            closing branch). The DFS is iterative: an edge back to a node on
            the current stack is dropped, which removes every cycle; the
            longest path is then a pass over the post-order. Calls (`bl`)
            count 1 and are listed, so a caller adds the callee's own count.
            Options:
              --via-loop K  the longest path forced through the head of the
                            K-th longest frame loop (1 = longest): with two
                            branches that each set up and run their own loop
                            (crumpBit_process() stage 4: SVF, one-pole), the
                            path through one loop is that branch's per-block
                            cost;
              --max N       exit 1 when the count is above N.
Why:        S091 P5: the 170-per-frame cap is engine loop + filter loop +
            (per-block work) / 32, and the per-block work had been counted by
            session scratch scripts (blockpath.py, throughpath.py), not a
            repository tool. crumpbit_cpu.py uses this module.
Inputs:     --elf PATH, --sym NAME (the base name; .lto_priv/.constprop/.isra
            suffixes are matched), optional --via-loop K, --max N.
Outputs:    one line: the per-block count and the calls; exit 1 when --max
            is exceeded.
Accessors:  crumpbit_cpu.py (import), tools/dsp_test/Makefile
            crumpbit_cpu_check; by hand.
Affiliates: elf_looppath.py (rows(), successors(), loop_paths()).
"""
import argparse
import re
import sys

import elf_looppath as E


def frame_loops(r, idx):
    """[(longest, head, members)] of the function's frame loops, longest
    first. members: every instruction index on any of the loop's paths."""
    heads = sorted({E.target(ops) for (addr, op, ops) in r
                    if E.BRANCH.match(op) and E.target(ops) is not None
                    and E.target(ops) <= addr and E.target(ops) in idx
                    and op not in E.UNCOND})
    closer = {}
    for i, (addr, op, ops) in enumerate(r):
        t = E.target(ops) if E.BRANCH.match(op) else None
        if t in heads and t <= addr:
            closer[t] = max(closer.get(t, 0), i)
    loops = []
    for h in heads:
        paths = E.loop_paths(r, idx, h, closer[h])
        if paths:
            members = set()
            for p in paths:
                members.update(p[2])
            loops.append((max(p[0] for p in paths), h, members))
    loops.sort(key=lambda x: -x[0])
    return loops


def block_path(elf, sym, via_loop=None):
    """(count, calls): the longest entry-to-exit path with loop
    instructions weighted 0; through the head of the via_loop-th longest
    loop (1-based) when given."""
    r = E.rows(elf, sym)
    idx = {addr: i for i, (addr, _, _) in enumerate(r)}
    loops = frame_loops(r, idx)
    in_loop = set()
    for _, _, members in loops:
        in_loop.update(members)
    n = len(r)
    succ = [E.successors(r, idx, i) for i in range(n)]
    # Iterative DFS from the entry: tree/forward/cross edges form the DAG,
    # back edges (to a node still on the stack) are dropped.
    state = [0] * n
    order = []
    dag = [[] for _ in range(n)]
    stack = [(0, 0)]
    state[0] = 1
    while stack:
        i, k = stack.pop()
        if k < len(succ[i]):
            stack.append((i, k + 1))
            j = succ[i][k]
            if state[j] == 1:
                continue
            dag[i].append(j)
            if state[j] == 0:
                state[j] = 1
                stack.append((j, 0))
        else:
            state[i] = 2
            order.append(i)
    w = [0 if i in in_loop else 1 for i in range(n)]
    # down[i]: the longest path from i to an exit, i included.
    down = [0] * n
    for i in order:
        down[i] = w[i] + max((down[j] for j in dag[i]), default=0)
    calls = sorted({re.sub(r'.*<|>.*', '', ops)
                    for (_, op, ops) in r if op == 'bl'})
    if via_loop is None:
        return down[0], calls
    if via_loop < 1 or via_loop > len(loops):
        sys.exit(f'elf_blockpath.py: {sym} has {len(loops)} loops, '
                 f'not {via_loop}')
    head = idx[loops[via_loop - 1][1]]
    # up[i]: the longest path from the entry to i, i included.
    neg = -10 ** 9
    up = [neg] * n
    up[0] = w[0]
    for i in reversed(order):
        if up[i] == neg:
            continue
        for j in dag[i]:
            up[j] = max(up[j], up[i] + w[j])
    return up[head] + down[head] - w[head], calls


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--sym', required=True)
    ap.add_argument('--via-loop', type=int)
    ap.add_argument('--max', type=int)
    a = ap.parse_args()
    count, calls = block_path(a.elf, a.sym, a.via_loop)
    via = f' (through loop {a.via_loop})' if a.via_loop else ''
    print(f'{a.sym}: per-block longest {count}{via}, loops 0; calls {calls}')
    if a.max is not None and count > a.max:
        print(f'elf_blockpath.py: FAIL {a.sym} per block {count} > {a.max}')
        sys.exit(1)


if __name__ == '__main__':
    main()
```

### 4.4 `tools/dsp_test/crumpbit_cpu.py` (new file)

#### N2 — the 170 cap as a gate

**Location:** new file `tools/dsp_test/crumpbit_cpu.py` (executable): **add** 95 lines.

- **What:** per sub-type, engine loop + filter loop + (engine per block,
  `dly` adding the Sync search's straight-line count + `process` per block
  through that sub-type's filter loop + the manager's
  `effectiveMax` per block and `tableLimit` straight-line count) / 32;
  fails above `--cap` (170), or when `crumpBit_process()` does not have
  exactly two frame loops (the method would no longer apply).
- **Why:** Q15: the 170 cap becomes a gate, not a manual calculation, so a
  later change that pushes any sub-type over it fails at once.
- **Inputs:** `--elf`, `--cap`. **Outputs:** one line per sub-type and a
  summary; exit 1 on failure.
- **Accessors:** `make -C tools/dsp_test crumpbit_cpu_check` (T6), after
  `make all`.
- **Output on the implemented tree:**

  ```
  dly: loop 111 + filter 31 + (engine 242 + process 249 + manager 86) / 32 = 160.0
  cmb: loop 109 + filter 31 + (engine 105 + process 249 + manager 86) / 32 = 153.8
  pit: loop 106 + filter 31 + (engine 195 + process 249 + manager 86) / 32 = 153.6
  grn: loop 116 + filter 16 + (engine 814 + process 204 + manager 86) / 32 = 166.5
  rvb: loop 131 + filter 16 + (engine 252 + process 204 + manager 86) / 32 = 163.9
  crumpbit cpu: worst 166.5 <= cap 170
  ```

```python
#!/usr/bin/env python3
"""CrumpBit whole-Effect CPU per frame, worst case, for every sub-type.

What:       measures the linked firmware ELF and prints, per sub-type, the
            whole CrumpBit Effect's instructions per frame on its longest
            path (S091 P5 method):
              engine frame loop + output-filter frame loop
              + (engine per block + crumpBit_process() per block on that
                 sub-type's filter branch + the manager's clamps per block)
                / frames per block (32)
            where:
              - the frame loops are elf_looppath.py's longest paths
                (crumpBit_runGrain with --cond-heads --in-range: the longest
                of its two span loops);
              - crumpBit_process() has exactly two frame loops, the SVF
                (longer, `dly`/`cmb`/`pit`) and the one-pole (`grn`/`rvb`);
                its per-block count is the longest path through that loop's
                head (elf_blockpath.py --via-loop), so it includes that
                branch's coefficient work only;
              - per block, every frame loop counts 0 (elf_blockpath.py), and
                the callees add their own counts: `dly` the Sync search
                crumpBit_divisionFor() (straight-line count); the manager
                crumpBit_effectiveMax() (per-block path) plus the unrolled
                crumpBit_tableLimit() (straight-line count), run once per
                block for the BUFFER_DEPENDENT `rte`.
            It fails when any sub-type is above --cap (170, the user's cap,
            S091_P5_SUBTYPE_EFFICIENCIES.md §1).
Why:        the cap is a gate, not a manual calculation (plan Q15).
Inputs:     --elf PATH, optional --cap N (default 170).
Outputs:    one line per sub-type with its terms and total; exit 1 when a
            total is above the cap, or when crumpBit_process() does not have
            exactly two frame loops (the method no longer applies).
Accessors:  tools/dsp_test/Makefile crumpbit_cpu_check (after `make all`).
Affiliates: elf_looppath.py, elf_blockpath.py, CrumpBitEffect.c.
"""
import argparse
import sys

import elf_blockpath as B
import elf_looppath as E

FRAMES = 32
# Sub-type: (engine symbol, filter loop rank in process, Sync search).
SUBTYPES = [
    ('dly', 'crumpBit_runDelay', 1, True),
    ('cmb', 'crumpBit_runComb', 1, False),
    ('pit', 'crumpBit_runPitch', 1, False),
    ('grn', 'crumpBit_runGrain', 2, False),
    ('rvb', 'crumpBit_runReverb', 2, False),
]


def longest_loop(elf, sym):
    r = E.rows(elf, sym)
    loops = B.frame_loops(r, {a: i for i, (a, _, _) in enumerate(r)})
    if not loops:
        sys.exit(f'crumpbit_cpu.py: no frame loop in {sym}')
    return loops


def straight(elf, sym):
    return len(E.rows(elf, sym))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--elf', required=True)
    ap.add_argument('--cap', type=float, default=170.0)
    a = ap.parse_args()
    proc = longest_loop(a.elf, 'crumpBit_process')
    if len(proc) != 2:
        sys.exit(f'crumpbit_cpu.py: crumpBit_process has {len(proc)} frame '
                 f'loops, expected 2 (SVF, one-pole)')
    manager = (B.block_path(a.elf, 'crumpBit_effectiveMax')[0] +
               straight(a.elf, 'crumpBit_tableLimit'))
    sync = straight(a.elf, 'crumpBit_divisionFor')
    worst = 0.0
    for name, sym, rank, has_sync in SUBTYPES:
        loop = longest_loop(a.elf, sym)[0][0]
        filt = proc[rank - 1][0]
        engine = B.block_path(a.elf, sym)[0] + (sync if has_sync else 0)
        process = B.block_path(a.elf, 'crumpBit_process', rank)[0]
        total = loop + filt + (engine + process + manager) / FRAMES
        worst = max(worst, total)
        print(f'{name}: loop {loop} + filter {filt} + (engine {engine} + '
              f'process {process} + manager {manager}) / {FRAMES} = '
              f'{total:.1f}')
    if worst > a.cap:
        print(f'crumpbit_cpu.py: FAIL worst {worst:.1f} > cap {a.cap:g}')
        sys.exit(1)
    print(f'crumpbit cpu: worst {worst:.1f} <= cap {a.cap:g}')


if __name__ == '__main__':
    main()
```

### 4.5 `tools/dsp_test/Makefile`

#### T1 — Makefile header: 18 crumpbit breaks

**Location:** `tools/dsp_test/Makefile`, WT lines 12–18: **modify** (replace 7 lines with 11; new lines 12–22).

- **What:** the target list names the six P5 breaks.

Current text (WT 12–18):

```make
#               crumpbit            modules/crumpbit.c (+ 12 breaks: P2
#                                   Sync key, R4 AC seed, P2 flat tone, P2
#                                   seat delay clamp, P2 Rate-limit probe;
#                                   P3 wet around the mix stage, grain
#                                   freeze, pitch direction, lane map; P4
#                                   grain fade asymmetry, `bal` side swap,
#                                   grain overwrite clamp)
```

New text:

```make
#               crumpbit            modules/crumpbit.c (+ 18 breaks: P2
#                                   Sync key, R4 AC seed, P2 flat tone, P2
#                                   seat delay clamp, P2 Rate-limit probe;
#                                   P3 wet around the mix stage, grain
#                                   freeze, pitch direction, lane map; P4
#                                   grain fade asymmetry, `bal` side swap,
#                                   grain overwrite clamp; P5 data-line
#                                   table, `grn` echo curve, filter
#                                   hand-over, grain block-aligned ends,
#                                   one-pole `bal` side swap, dropped-
#                                   trigger ages)
```

#### T2 — Makefile `.PHONY`: `crumpbit_cpu_check`

**Location:** `tools/dsp_test/Makefile`, WT lines 46–46: **modify** (replace 1 line with 1; new lines 50–50).

- **What:** adds the new target (not in `all`: it needs the firmware ELF).

Current text (WT 46–46):

```make
        crumpbit_elf_check
```

New text:

```make
        crumpbit_elf_check crumpbit_cpu_check
```

#### T3 — Makefile crumpbit comment: CB1–CB19

**Location:** `tools/dsp_test/Makefile`, WT lines 264–265: **modify** (replace 2 lines with 2; new lines 268–269).

- **What:** the law range and the bit-exact laws.

Current text (WT 264–265):

```make
# the references in modules/crumpbit.c (laws CB1-CB16; CB1-CB4, CB10 and
# CB16 bit-exact).
```

New text:

```make
# the references in modules/crumpbit.c (laws CB1-CB19; CB1-CB4, CB10, CB16,
# CB17 and the CB15/CB19 identity laws bit-exact).
```

#### T4 — Makefile crumpbit comment: the six P5 break proofs

**Location:** `tools/dsp_test/Makefile`, WT lines 293–293: **modify** (replace 1 line with 13; new lines 297–309).

- **What:** documents breaks 13–18 (what each breaks, which law catches
  it).

Current text (WT 293–293):

```make
#     overwritten audio) must fail CB14's read-safety law.
```

New text:

```make
#     overwritten audio) must fail CB14's read-safety law;
#   - S091 P5: the data-line table built with OR instead of EOR (inverted
#     lines forced high) must fail CB17;
#   - the `grn` echo on the shared 0..1.2 curve instead of 0..0.98 (the
#     echo grows) must fail CB18;
#   - the SVF -> one-pole hand-over skipped (the one-pole starts from the
#     band integrator) must fail CB19;
#   - a grain's span not rounded to a block boundary must fail CB14's
#     block-aligned law;
#   - the one-pole's left k taken from the right cutoff must fail CB16 on
#     `rvb`;
#   - the ages of a dropped or retried trigger not put back to the block
#     start (the P4 defect) must fail CB14's block-aligned law.
```

#### T5 — Makefile: breaks 13–18

**Location:** `tools/dsp_test/Makefile`, WT lines 458–471: **modify** (replace 14 lines with 78; new lines 474–551).

- **What:** six `sed` breaks of the generated `CrumpBitEffect.host.c`, each
  with a `grep -q` that the pattern applied, a build, and a run that must
  fail:
  - 13: the data-line table with OR instead of EOR → CB17;
  - 14: the `grn` echo on the shared 0..1.2 curve → CB18;
  - 15: the SVF → one-pole hand-over skipped (left) → CB19;
  - 16: the grain span not rounded → CB14 (i);
  - 17: the one-pole's left k from the right cutoff → CB16 on `rvb`;
  - 18: the dropped/retried trigger's age reset removed → CB14 (i).
- **Why:** every new law is proved able to fail. All 18 breaks are caught
  on the implemented tree; breaks 3, 6, 10, 11, 12 still apply unchanged
  (plan §7.3 re-check: their patterns were kept).

Current text (WT 458–471):

```make

# S091 P2: the committed CrumpBit tables must equal the generator's output.
crumpbit_tables:
	cd $(ROOT) && python3 tools/plots/gen_crumpbit_tables.py --check

# S091 ARM gate (needs `make all` at the repository root first):
#   - S091 P3: each sub-engine's frame loop (stages 1-3, one symbol each,
#     CRUMPBIT_NOINLINE) at or below its measured longest path, no VDIV:
#     `dly` 124 and `cmb` 124 (engine A; the S091 P2 loop was 126), `pit`
#     135 (Q14 integer heads), `grn` 136 (S091 P4: two spans around the
#     block's one trigger or steal; flat-top windows; --cond-heads
#     --in-range: its cold blocks close backward branches into the body,
#     which are not frame loops), `rvb` 145;
#   - crumpBit_process(): the stage-4 output-filter loop, 31 (S091 P2);
```

New text:

```make
	mkdir -p build/break_cb13 build/break_cb14 build/break_cb15
	sed 's/        crumpBit_dataLines\[c\] = (uint8_t)((c \& keep) ^ inv);/        crumpBit_dataLines[c] = (uint8_t)((c \& keep) | inv);/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb13/CrumpBitEffect.host.c
	@grep -q '(uint8_t)((c & keep) | inv);' \
	  build/break_cb13/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb13 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break13 -lm
	@if ./build/crumpbit_break13 >/dev/null 2>&1; then \
	  echo "crumpbit_break13: FAILED (data-line table break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break13: caught (law CB17 fails as expected)"; fi
	sed 's/    s.fb = crumpBit_grainFeedbackGain(rt->feedback_raw);/    s.fb = crumpBit_feedbackGain(rt->feedback_raw);/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb14/CrumpBitEffect.host.c
	@grep -q '    s.fb = crumpBit_feedbackGain(rt->feedback_raw);' \
	  build/break_cb14/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb14 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break14 -lm
	@if ./build/crumpbit_break14 >/dev/null 2>&1; then \
	  echo "crumpbit_break14: FAILED (grn echo curve break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break14: caught (law CB18 fails as expected)"; fi
	sed 's/        rt->svf_l1 = rt->svf_l2;/        (void)0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb15/CrumpBitEffect.host.c
	@grep -q '        (void)0;' \
	  build/break_cb15/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb15 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break15 -lm
	@if ./build/crumpbit_break15 >/dev/null 2>&1; then \
	  echo "crumpbit_break15: FAILED (filter hand-over break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break15: caught (law CB19 fails as expected)"; fi
	mkdir -p build/break_cb16 build/break_cb17 build/break_cb18
	sed 's/~(CRUMPBIT_GRAIN_MIN_PERIOD - 1u)) - ev;/0xFFFFFFFFu) - ev;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb16/CrumpBitEffect.host.c
	@grep -q '0xFFFFFFFFu) - ev;' \
	  build/break_cb16/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb16 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break16 -lm
	@if ./build/crumpbit_break16 >/dev/null 2>&1; then \
	  echo "crumpbit_break16: FAILED (grain block-end break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break16: caught (law CB14 fails as expected)"; fi
	sed 's/const float kl = crumpBit_onePoleK\[fl\];/const float kl = crumpBit_onePoleK[fr];/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb17/CrumpBitEffect.host.c
	@grep -q 'const float kl = crumpBit_onePoleK\[fr\];' \
	  build/break_cb17/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb17 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break17 -lm
	@if ./build/crumpbit_break17 >/dev/null 2>&1; then \
	  echo "crumpbit_break17: FAILED (one-pole bal side break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break17: caught (law CB16 fails as expected)"; fi
	sed 's/        age_a = g->age_a;/        (void)0;/' \
	  $(GEN)/CrumpBitEffect.host.c > build/break_cb18/CrumpBitEffect.host.c
	@grep -q '        (void)0;' \
	  build/break_cb18/CrumpBitEffect.host.c
	$(HOSTCC) -Ibuild/break_cb18 $(HOSTFLAGS) modules/crumpbit.c \
	  -o build/crumpbit_break18 -lm
	@if ./build/crumpbit_break18 >/dev/null 2>&1; then \
	  echo "crumpbit_break18: FAILED (dropped-trigger age break not caught)"; \
	  exit 1; \
	else echo "crumpbit_break18: caught (law CB14 fails as expected)"; fi

# S091 P2: the committed CrumpBit tables must equal the generator's output.
crumpbit_tables:
	cd $(ROOT) && python3 tools/plots/gen_crumpbit_tables.py --check

# S091 ARM gate (needs `make all` at the repository root first):
#   - each sub-engine's frame loop (stages 1-3, one symbol each,
#     CRUMPBIT_NOINLINE) at or below its measured longest path, no VDIV
#     (S091 P5 limits, measured on the implemented tree): `dly` 111 and
#     `cmb` 109 (engine A with the Q14 read; P4 124), `pit` 106 (drop-sample
#     heads; P4 135), `grn` 116 (two spans around the block's one trigger or
#     steal; no window lower clamp, plain echo; P4 136; --cond-heads
#     --in-range: its cold blocks close backward branches into the body,
#     which are not frame loops), `rvb` 131 (low-pass damping; P4 145);
#   - crumpBit_process(): the stage-4 output-filter loops, the SVF 31 (S091
#     P2; loop 1) and the one-pole 16 (S091 P5 F1; --rank 2);
```

#### T6 — Makefile: ELF limits; `crumpbit_cpu_check`

**Location:** `tools/dsp_test/Makefile`, WT lines 480–495: **modify** (replace 16 lines with 26; new lines 560–585).

- **What:**
  - `crumpbit_elf_check`: `dly` 111, `cmb` 109, `pit` 106, `grn` 116,
    `rvb` 131; `process` loop 1 (SVF) 31 and loop 2 (one-pole) 16 via
    `--rank 2`; `divisionFor` 117 and `tableLimit` 63 unchanged;
  - `crumpbit_cpu_check`: `crumpbit_cpu.py --cap 170`.
- **Why:** the limits are the implemented tree's measurements (plan §7.2:
  the prototype's were a floor because the `mix` ramp is kept); the cap
  becomes a gate (Q15).

Current text (WT 480–495):

```make
	  --sym crumpBit_runDelay --max 124
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runComb --max 124
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runPitch --max 135
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runGrain --cond-heads --in-range --top 2 --max 136
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runReverb --max 145
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --max 31
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_divisionFor --straight --max 117
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_tableLimit --straight --max 63

```

New text:

```make
	  --sym crumpBit_runDelay --max 111
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runComb --max 109
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runPitch --max 106
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runGrain --cond-heads --in-range --top 2 --max 116
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_runReverb --max 131
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --top 2 --max 31
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_process --top 2 --rank 2 --max 16
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_divisionFor --straight --max 117
	python3 elf_looppath.py --elf $(ROOT)/build/lxr02.elf \
	  --sym crumpBit_tableLimit --straight --max 63

# S091 P5: the whole CrumpBit Effect at or below 170 instructions per frame
# in every sub-type, worst case (the user's cap): engine loop + filter loop
# + (engine per block + crumpBit_process() per block on that filter's
# branch + the manager's clamps) / 32, `dly` adding its Sync search
# (crumpbit_cpu.py; needs `make all` at the repository root first).
crumpbit_cpu_check:
	python3 crumpbit_cpu.py --elf $(ROOT)/build/lxr02.elf --cap 170

```

### 4.6 `tools/dsp_test/modules/crumpbit.c`

#### X1 — Test header: S091 P5

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 3–3: **modify** (replace 1 line with 1; new lines 3–3).

- **What:** the title names S091 P5.

Current text (WT 3–3):

```c
 * sub-types; S091 P4 `grn` rework and `bal`).
```

New text:

```c
 * sub-types; S091 P4 `grn` rework and `bal`; S091 P5 efficiencies).
```

#### X2 — Test header: CB4's new reference (Q8)

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 22–22: **modify** (replace 1 line with 4; new lines 22–25).

- **What:** CB4 is bit-exact against P2 + the Q14 tap read + per-block
  feedback and balance, `mix` still ramping.

Current text (WT 22–22):

```c
 *                    (which the firmware recomputes from raw bytes), over
```

New text:

```c
 *                    (which the firmware recomputes from raw bytes), and
 *                    (S091 P5, plan Q8) the Q14 tap read (B) and the
 *                    feedback and balance gains stepped per block (S1;
 *                    `mix` still ramps, Q9), over
```

#### X3 — Test header: CB6 additions; CB8 scope

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 49–51: **modify** (replace 3 lines with 9; new lines 52–60).

- **What:** CB6 adds `effective_max(dpn)` 255 everywhere, the engine fit
  equal to the label's, and the one-pole table's −3 dB law; CB8 is `dly`
  (the tape curve), `grn`'s echo being CB18.

Current text (WT 49–51):

```c
 *               CB7  output resonance bound: crumpBit_filterDamping() is in
 *                    [0.0625, 1] for every `res` (Q <= 8), 1 at res 0;
 *               CB8  stability at fbk 127 (gain 1.2): 60 s of
```

New text:

```c
 *                    S091 P5 A: effective_max(dpn) is 255 in every
 *                    sub-type, and the Position fit `grn` caches after a
 *                    block equals crumpBit_grainPosLimit()'s for the share,
 *                    across shares and a tempo change; the one-pole k
 *                    table (F1) is -3 dB at the SVF's res-0 frequency;
 *               CB7  output resonance bound: crumpBit_filterDamping() is in
 *                    [0.0625, 1] for every `res` (Q <= 8), 1 at res 0;
 *               CB8  stability at fbk 127 (gain 1.2; `dly`, the tape
 *                    curve; `grn`'s echo is CB18): 60 s of
```

#### X4 — Test header: CB11 crosses filter forms

**Location:** `tools/dsp_test/modules/crumpbit.c`, after WT line 71: **add** 2 lines (new lines 81–82).

- **What:** notes that the switch storm crosses the SVF and the one-pole
  (CB19 checks the hand-over itself).

New text:

```c
 *                    S091 P5: the switches cross the SVF and one-pole
 *                    filters (CB19 checks the hand-over itself);
```

#### X5 — Test header: CB14 (A, G3), CB15 (R1), CB16, CB17–CB19

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 92–98: **modify** (replace 7 lines with 28; new lines 103–130).

- **What:** documents the new and extended laws.

Current text (WT 92–98):

```c
 *               CB15 `rvb`: stable and bounded at every extreme of rte,
 *                    fbk, snc, ton and dpn; an RT60 of ~0.49 s (fbk 44)
 *                    decays 9..21 dB over 0.125 s; freeze holds the tail
 *                    within +-6 dB over 1 s;
 *               CB16 `bal` (S091 P4): with balance b, the left channel is
 *                    bit-identical to a run at frq - b and balance 0, the
 *                    right to one at frq + b, for every filter type.
```

New text:

```c
 *                    S091 P5: Position beyond the line plays the last
 *                    fitting stop (A); the candidate takes the fit as an
 *                    argument; every voice's end lands on a block boundary
 *                    (G3: after every 32-frame block, end - age is a whole
 *                    number of blocks), across steals and retriggers;
 *               CB15 `rvb`: stable and bounded at every extreme of rte,
 *                    fbk, snc, ton and dpn; an RT60 of ~0.49 s (fbk 44)
 *                    decays 9..21 dB over 0.125 s; freeze holds the tail
 *                    within +-6 dB over 1 s; S091 P5 R1: every `ton`
 *                    above 63 plays bit-identically to 63;
 *               CB16 `bal` (S091 P4): with balance b, the left channel is
 *                    bit-identical to a run at frq - b and balance 0, the
 *                    right to one at frq + b, for every filter type; S091
 *                    P5: on the SVF (`dly`) and the one-pole (`rvb`) path;
 *               CB17 (S091 P5 X2) the data-line table equals
 *                    (c & keep) ^ invert for all 256 codes after the first
 *                    block and after every `bof`/`biv` change;
 *               CB18 (S091 P5 G4) the `grn` echo: `fbk` 127 is exactly
 *                    0.98; with no input the delay line's peak never grows
 *                    echo to echo and settles inside the 8-bit rounding
 *                    floor, |code| < 0.5 / (1 - gain), at every `fbk`;
 *               CB19 (S091 P5 F1) the one-pole output filter of `grn`/
 *                    `rvb`: -3 dB at the SVF's res-0 frequency (+-0.3 dB,
 *                    sines through the whole Effect, mix 0); the type
 *                    mapping (LP = LP2 = BP = UBP, HP = notch, peak = off,
 *                    bit for bit; off passes the dry signal exactly); `res`
 *                    has no effect; a switch hands the state over (SVF low
 *                    integrator -> one-pole state -> SVF low integrator).
```

#### X6 — Test header: 18 breaks; affiliates

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 108–111: **modify** (replace 4 lines with 5; new lines 140–144).

- **What:** the break count and the schedule reference.

Current text (WT 108–111):

```c
 * Accessors:  make -C tools/dsp_test crumpbit (twelve break builds follow).
 * Affiliates: CrumpBitEffect.c/.h; CrumpBitTables.c; CrumpBitParameters.h;
 *             StepScale.c; S091_P2_Implementation.md §5;
 *             S091_P3_Implementation.md (host laws CB4-CB15).
```

New text:

```c
 * Accessors:  make -C tools/dsp_test crumpbit (18 break builds follow).
 * Affiliates: CrumpBitEffect.c/.h; CrumpBitTables.c; CrumpBitParameters.h;
 *             StepScale.c; S091_P2_Implementation.md §5;
 *             S091_P3_Implementation.md (host laws CB4-CB15);
 *             S091_P5_IMPLEMENTATION.md (CB17-CB19).
```

#### X7 — Reference model comment: the P5 differences

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 223–224: **modify** (replace 2 lines with 7; new lines 256–262).

- **What:** stored ramp origins are now only `mix` and the output mix;
  the Q14 read is written from the plan's statement (truncation, a wrap by
  branch, the integer interpolation, ÷ 2^21, the gate in 64-bit integers),
  not the firmware's mask and fixed-point forms; feedback and balance are
  per block.
- **Why:** the reference proves the firmware's branch-free forms compute
  exactly the documented equations (Q8: a tolerance would hide
  regressions).

Current text (WT 223–224):

```c
 * - ramp origins (mix, fb, gain_l, gain_r and the output mix cx/cb/cl) are
 *   STORED floats, as S074 stored them, not recomputed from raw bytes;
```

New text:

```c
 * - ramp origins (mix and the output mix cx/cb/cl) are STORED floats, as
 *   S074 stored them, not recomputed from raw bytes;
 * - S091 P5 (plan Q8): the tap is read in Q14 as the plan states it
 *   (trunc(delay 2^14), a wrap by branch, the integer interpolation, /2^21,
 *   the gate d + 2 <= valid in 64-bit integers), not through the
 *   firmware's mask and fixed-point VCVT forms; the feedback and the balance
 *   gains are this block's targets on every frame (S1, no ramp);
```

#### X8 — `RefP2`: remove the feedback and balance origins

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 235–237: **remove** 3 lines (nothing replaces them; the text after them starts at new line 273).

- **What:** `feedback`, `gain_l`, `gain_r` go (S1).

Removed text:

```c
    float feedback;
    float gain_l;
    float gain_r;
```

#### X9 — `ref2_process()` locals

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 361–363: **modify** (replace 3 lines with 3; new lines 396–398).

- **What:** `flen`, the `*_to` and `*_inc` feedback/balance locals go.

Current text (WT 361–363):

```c
    float flen, max, target, per_tick, free_run, synced;
    float mix, fb, gl, gr, mix_to, fb_to, gl_to, gr_to;
    float mix_inc, fb_inc, gl_inc, gr_inc, per_frame;
```

New text:

```c
    float max, target, per_tick, free_run, synced;
    float mix, fb, gl, gr, mix_to;
    float mix_inc, per_frame;
```

#### X10 — `ref2_process()`: remove `flen`

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 371–371: **remove** 1 line (nothing replaces them; the text after them starts at new line 406).

- **What:** the float read is gone.

Removed text:

```c
    flen = (float)len;
```

#### X11 — `ref2_process()`: per-block feedback and balance

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 387–399: **modify** (replace 13 lines with 8; new lines 421–428).

- **What:** `fb`, `gl`, `gr` are the targets for the whole block; only
  `mix` keeps its origin and step.

Current text (WT 387–399):

```c
    fb_to = ref2_fbGain(rt->feedback_raw);
    gl_to = (rt->pan_raw <= 63u) ? 1.0f
        : (float)(127u - rt->pan_raw) * (1.0f / 64.0f);
    gr_to = (rt->pan_raw >= 63u) ? 1.0f
        : (float)rt->pan_raw * (1.0f / 63.0f);
    mix = rt->mix;
    fb = rt->feedback;
    gl = rt->gain_l;
    gr = rt->gain_r;
    mix_inc = (mix_to - mix) * per_frame;
    fb_inc = (fb_to - fb) * per_frame;
    gl_inc = (gl_to - gl) * per_frame;
    gr_inc = (gr_to - gr) * per_frame;
```

New text:

```c
    /* S1: the block's feedback and balance gains, every frame. */
    fb = ref2_fbGain(rt->feedback_raw);
    gl = (rt->pan_raw <= 63u) ? 1.0f
        : (float)(127u - rt->pan_raw) * (1.0f / 64.0f);
    gr = (rt->pan_raw >= 63u) ? 1.0f
        : (float)rt->pan_raw * (1.0f / 63.0f);
    mix = rt->mix;
    mix_inc = (mix_to - mix) * per_frame;
```

#### X12 — `ref2_process()` frame locals: the Q14 read

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 424–424: **modify** (replace 1 line with 2; new lines 453–454).

- **What:** `d14`, `rp`, `q` (int32) replace the float position.

Current text (WT 424–424):

```c
        float rp, frac, wet, dc, tilt, ret;
```

New text:

```c
        float wet, dc, tilt, ret;
        int32_t d14, rp, q;
```

#### X13 — `ref2_process()`: the Q14 read from the equations

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 432–445: **modify** (replace 14 lines with 12; new lines 462–473).

- **What:** `d14 = (int32_t)(delay · 16384)`; `rp = wp·16384 − d14`, plus
  `len·16384` when negative; `i0 = rp / 16384`, `i1 = i0 + 1` or 0;
  `q = s0·16384 + (rp % 16384)(s1 − s0)`; `wet = q / 2097152` when
  `d14 + 32768 ≤ valid·16384` (int64), else 0.
- **Result:** CB4 is bit-exact over 60 s (82,800 blocks).

Current text (WT 432–445):

```c
        fb += fb_inc;
        gl += gl_inc;
        gr += gr_inc;
        rt->delay += (target - rt->delay) * CRUMPBIT_GLIDE_K;
        rp = (float)wp - rt->delay;
        rp += (rp < 0.0f) ? flen : 0.0f;
        i0 = (uint32_t)rp;
        frac = rp - (float)i0;
        i0 -= (i0 >= len) ? len : 0u;
        i1 = i0 + 1u;
        i1 -= (i1 >= len) ? len : 0u;
        wet = ((float)loop[i0] +
               frac * (float)(loop[i1] - loop[i0])) * CRUMPBIT_LSB;
        wet = ((rt->delay + 2.0f) <= (float)valid) ? wet : 0.0f;
```

New text:

```c
        rt->delay += (target - rt->delay) * CRUMPBIT_GLIDE_K;
        /* S091 P5 B: position wp - d in Q14, wrapped once, interpolated. */
        d14 = (int32_t)(rt->delay * 16384.0f);
        rp = (int32_t)(wp * 16384u) - d14;
        if (rp < 0)
            rp += (int32_t)(len * 16384u);
        i0 = (uint32_t)rp / 16384u;
        i1 = (i0 + 1u == len) ? 0u : i0 + 1u;
        q = (int32_t)loop[i0] * 16384 +
            (rp % 16384) * ((int32_t)loop[i1] - (int32_t)loop[i0]);
        wet = ((int64_t)d14 + 2 * 16384 <= (int64_t)valid * 16384)
            ? (float)q / 2097152.0f : 0.0f;
```

#### X14 — `ref2_process()` tail: no feedback/balance origins

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 493–495: **remove** 3 lines (nothing replaces them; the text after them starts at new line 521).

- **What:** removes the three stores (X8).

Removed text:

```c
    rt->feedback = fb_to;
    rt->gain_l = gl_to;
    rt->gain_r = gr_to;
```

#### X15 — CB6 comment: the Position limit is the label's

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 1068–1068: **modify** (replace 1 line with 5; new lines 1093–1097).

- **What:** the law's comment: `effective_max(dpn)` is 255 everywhere; the
  engine's cached fit is the label's.

Current text (WT 1068–1068):

```c
    /* Position's clamp in grn: 63 + 4 x (stops that fit); 255 elsewhere. */
```

New text:

```c
    /*
     * Position's limit (the page label): 63 + 4 x (stops that fit). S091
     * P5 A: the manager no longer clamps `dpn` (255 in every sub-type);
     * the engine's own fit, cached by a `grn` block, is the label's.
     */
```

#### X16 — CB6: `grainPosLimit` law; 255 everywhere; the engine fit; the k table

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 1079–1087: **modify** (replace 9 lines with 56; new lines 1108–1163).

- **What:**
  - `crumpBit_grainPosLimit()` equals 63 + 4 · (a linear stop count) for
    every share step;
  - `effective_max(dpn)` is 255 in all five sub-types;
  - a `grn` runtime processes one block per share (1,024 B to the arena in
    4 KiB steps) at 120, 47 and 300 BPM, and `g.u.grn.fit` must equal
    `(grainPosLimit − 63) / 4` each time (A, G6: the cache follows both
    the line and the tempo);
  - every `onePoleK` entry: |H|² at the SVF's −3 dB angle within 10⁻⁵ of ½,
    increasing, below 1; k[0] = 0.

Current text (WT 1079–1087):

```c
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_SUBTYPE,
                                 CRUMPBIT_SUBTYPE_GRAIN);
        snprintf(msg, sizeof msg, "CB6 grn position limit bytes %u", bytes);
        expect(crumpBit_ops.effective_max(&rt, CRUMPBIT_PARAM_DLY_PAN,
                                          &share) == 63u + 4u * fit, msg);
        crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_SUBTYPE,
                                 CRUMPBIT_SUBTYPE_DELAY);
        expect(crumpBit_ops.effective_max(&rt, CRUMPBIT_PARAM_DLY_PAN,
                                          &share) == 255u, msg);
```

New text:

```c
        snprintf(msg, sizeof msg, "CB6 grn position limit bytes %u", bytes);
        expect(crumpBit_grainPosLimit(&share) == 63u + 4u * fit, msg);
        for (sub = 0u; sub < CRUMPBIT_SUBTYPE_COUNT; sub++) {
            crumpBit_ops.write_param(&rt, CRUMPBIT_PARAM_SUBTYPE,
                                     (uint8_t)sub);
            expect(crumpBit_ops.effective_max(&rt, CRUMPBIT_PARAM_DLY_PAN,
                                              &share) == 255u, msg);
        }
    }
    {
        /* The engine's cached fit across shares and a tempo change. */
        static const uint16_t bpms[3] = { 120u, 47u, 300u };
        CrumpBitRuntime g;
        float l[32];
        float r[32];
        uint32_t t;

        memset(l, 0, sizeof l);
        memset(r, 0, sizeof r);
        memset(&g, 0, sizeof g);
        crumpBit_ops.init(&g, NULL);
        crumpBit_ops.write_param(&g, CRUMPBIT_PARAM_SUBTYPE,
                                 CRUMPBIT_SUBTYPE_GRAIN);
        for (t = 0u; t < 3u; t++) {
            cb_bpm = bpms[t];
            for (bytes = 1024u; bytes <= CRUMPBIT_LOOP_MAX_BYTES;
                 bytes += 4096u) {
                fx_share_t share = { arena_new, 0u, bytes };
                effect_io_t io = { l, r, 32u, 2u, &share };

                crumpBit_ops.process(&g, &io);
                snprintf(msg, sizeof msg,
                         "CB6 grn engine fit bpm %u bytes %u: %u vs %u",
                         cb_bpm, bytes, g.u.grn.fit,
                         (crumpBit_grainPosLimit(&share) - 63u) / 4u);
                expect(g.u.grn.fit ==
                       (crumpBit_grainPosLimit(&share) - 63u) / 4u, msg);
            }
        }
        cb_bpm = 120u;
    }
    /*
     * S091 P5 F1: the one-pole k is -3 dB where the SVF at res 0 is,
     * tan(w/2) = g sqrt(sqrt 2 - 1), for every index above 0 (0: closed).
     */
    expect(crumpBit_onePoleK[0] == 0.0f, "CB6 one-pole k[0] = 0");
    for (i = 1u; i < 128u; i++) {
        const double w = 2.0 * atan((double)crumpBit_filterG[i] *
                                    sqrt(sqrt(2.0) - 1.0));
        const double k = crumpBit_onePoleK[i];
        const double a = 1.0 - k;
        const double g2 = k * k / (1.0 - 2.0 * a * cos(w) + a * a);

        snprintf(msg, sizeof msg, "CB6 one-pole k %u: |H|^2 %.6f", i, g2);
        expect(fabs(g2 - 0.5) < 1e-5 && k < 1.0 &&
               crumpBit_onePoleK[i] > crumpBit_onePoleK[i - 1u], msg);
```

#### X17 — CB14 (a): the pick's `fit` argument

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 1643–1643: **modify** (replace 1 line with 1; new lines 1719–1719).

- **What:** passes `CRUMPBIT_GRAIN_STOPS` (all fit) to keep the pitch-slot
  law independent of Position.

Current text (WT 1643–1643):

```c
                               per_tick, &c);
```

New text:

```c
                               per_tick, CRUMPBIT_GRAIN_STOPS, &c);
```

#### X18 — CB14 (b): the pick's `fit` argument

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 1705–1705: **modify** (replace 1 line with 1; new lines 1781–1781).

- **What:** as X17, for the Width law.

Current text (WT 1705–1705):

```c
                                   per_tick, &c);
```

New text:

```c
                                   per_tick, CRUMPBIT_GRAIN_STOPS, &c);
```

#### X19 — CB14 (c): Position beyond the line plays the last fitting stop (A)

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 1729–1754: **modify** (replace 26 lines with 36; new lines 1805–1840).

- **What:** at N = 60,000 and 120 BPM, `fit = grainStopFit(grainMaxTicks(N
  − 2, 120))` must be 14; every positive Position plays `min(stop, 13)` at
  its exact lag and sounds; with `valid` at half that lag the grain waits
  (end 0).

Current text: WT lines 1729–1754 (26 lines, replaced whole).

New text:

```c
    /*
     * (c) Position laws, at unity pitch (no headroom). S091 P5 A: a stop
     * longer than the line plays the last stop that fits it (`fit`, as the
     * engine works it out); a fitting stop not yet recorded still waits.
     */
    for (i = 0u; i < 128u; i++) {
        crumpbit_grain_t c;
        uint32_t hash = 0x777u + i;
        const uint32_t len = 60000u;
        const uint32_t ref = 1000u;
        const uint32_t fit = crumpBit_grainStopFit(
            crumpBit_grainMaxTicks((float)(len - 2u), 120u));
        uint32_t lag;

        rt.pan_raw = (uint8_t)i;
        rt.aux_raw = 63u;
        rt.grain_length = 63u;
        crumpBit_grainPick(&hash, &rt, len, ref, len, 0u, per_tick, fit, &c);
        lag = (ref >= c.start) ? ref - c.start : ref + len - c.start;
        snprintf(msg, sizeof msg, "CB14 position %u lag %u end %u fit %u", i,
                 lag, c.end, fit);
        if (i == 63u) {
            expect(lag == 2u && c.end != 0u, msg);
        } else if (i > 63u) {
            uint32_t stop = crumpBit_grainPosStop((uint8_t)i);
            float want;

            stop = (stop < fit) ? stop : fit - 1u;
            want = crumpBit_grainTime[crumpBit_grainStopCc[stop]] * per_tick;
            expect(fit == 14u && want <= (float)(len - 2u) &&
                   lag == (uint32_t)want && c.end != 0u, msg);
            /* Recorded only half as far back as the lag: it waits. */
            hash = 0x777u + i;
            crumpBit_grainPick(&hash, &rt, len, ref, (uint32_t)want / 2u, 0u,
                               per_tick, fit, &c);
            expect(c.end == 0u, msg);
```

#### X20 — CB14 (d): a random fit in the read-safety law

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 1785–1785: **modify** (replace 1 line with 2; new lines 1871–1872).

- **What:** the safety law draws `fit` 0..16, so the limited stops and the
  none-fit case go through the overwrite clamp too.

Current text (WT 1785–1785):

```c
                           rndf(30.0f, 1500.0f), &c);
```

New text:

```c
                           rndf(30.0f, 1500.0f),
                           rnd() % (CRUMPBIT_GRAIN_STOPS + 1u), &c);
```

#### X21 — CB14 (g): the pick's `fit` argument

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 1961–1961: **modify** (replace 1 line with 1; new lines 2048–2048).

- **What:** as X17, for the anchored Position law.

Current text (WT 1961–1961):

```c
                               per_tick, &c);
```

New text:

```c
                               per_tick, CRUMPBIT_GRAIN_STOPS, &c);
```

#### X22 — CB14 (i): block-aligned ends (G3)

**Location:** `tools/dsp_test/modules/crumpbit.c`, after WT line 2007: **add** 49 lines (new lines 2095–2143).

- **What:** 40,000 blocks of `grn` with Density switching between dense
  random and rhythmic, random Length, Position, P1 and freeze, every 97
  blocks; after each block every sounding voice must have `(end − age) %
  32 == 0`; the run must contain more than 100 steal blocks.
- **Result:** 0 misaligned (815 steal blocks). Before the E41 fix: 276.

New text:

```c
    }

    /*
     * (i) S091 P5 G3: every voice's end lands on a block boundary, so after
     * every 32-frame block a sounding voice has a whole number of blocks
     * left (end - age = 0 mod 32), and its window never goes below 0
     * inside a block. Steal-heavy: random Density (short intervals), long
     * grains, every Length and Position, retriggers, freezes.
     */
    {
        fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
        float l[32], r[32];
        uint32_t block;
        uint32_t n;
        uint32_t bad = 0u;
        uint32_t steals = 0u;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        for (block = 0u; block < 40000u; block++) {
            if ((block % 97u) == 0u) {
                p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY,
                         (uint8_t)((rnd() & 1u) ? rnd() % 40u
                                                : 90u + rnd() % 38u), &share);
                p3_write(&rt, CRUMPBIT_PARAM_GRAIN_LENGTH,
                         (uint8_t)(rnd() & 0x7Fu), &share);
                p3_write(&rt, CRUMPBIT_PARAM_DLY_PAN,
                         (uint8_t)(rnd() & 0x7Fu), &share);
                p3_write(&rt, CRUMPBIT_PARAM_AUX, (uint8_t)(rnd() & 0x7Fu),
                         &share);
                p3_write(&rt, CRUMPBIT_PARAM_SYNC,
                         (uint8_t)((rnd() & 7u) == 0u), &share);
            }
            for (n = 0u; n < 32u; n++) {
                l[n] = rndf(-0.8f, 0.8f);
                r[n] = rndf(-0.8f, 0.8f);
            }
            p3_block(&rt, &share, l, r, 32u);
            steals += (rt.u.grn.pending != 0u);
            if (rt.u.grn.age_a < rt.u.grn.end_a)
                bad += ((rt.u.grn.end_a - rt.u.grn.age_a) % 32u) != 0u;
            if (rt.u.grn.age_b < rt.u.grn.end_b)
                bad += ((rt.u.grn.end_b - rt.u.grn.age_b) % 32u) != 0u;
        }
        snprintf(msg, sizeof msg,
                 "CB14 block-aligned ends: %u voices off (%u steal blocks)",
                 bad, steals);
        expect(bad == 0u && steals > 100u, msg);
```

#### X23 — CB15 (d): `ton` above 63 equals 63 (R1); CB16 comment

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 2131–2135: **modify** (replace 5 lines with 59; new lines 2267–2325).

- **What:**
  - eight passes: two `rvb` runtimes, one at `ton` 63 and one at a random
    64..127, with the same random `fbk`/`rte`, 0.2 s of noise then silence
    over 3,000 blocks: outputs bit-identical;
  - the CB16 comment: it now runs on `dly` (SVF) and `rvb` (one-pole); `grn`
    is not used because it draws a hardware random word per block.

Current text (WT 2131–2135):

```c
    free(out);
}

/* CB16: `bal` (S091 P4) ------------------------------------------------------ */
static void law_cb16(void)
```

New text:

```c

    /*
     * (d) S091 P5 R1: the damping is low-pass only, so every `ton` above
     * 63 plays bit-identically to 63 (flat), whatever the other rows.
     */
    for (pass = 0u; pass < 8u; pass++) {
        static uint8_t arena_r1[2][16384];
        CrumpBitRuntime rr[2];
        fx_share_t sh[2];
        const uint8_t ton = (uint8_t)(64u + rnd() % 64u);
        const uint8_t fbk = (uint8_t)(rnd() & 0x7Fu);
        const uint8_t rte = (uint8_t)(rnd() & 0x7Fu);
        uint32_t bad = 0u;
        uint32_t block;
        uint32_t k;

        for (k = 0u; k < 2u; k++) {
            memset(arena_r1[k], 0, sizeof arena_r1[k]);
            sh[k].base = arena_r1[k];
            sh[k].offset = 0u;
            sh[k].bytes = sizeof arena_r1[k];
            p3_init(&rr[k], CRUMPBIT_SUBTYPE_REVERB);
            p3_write(&rr[k], CRUMPBIT_PARAM_MIX, 127u, &sh[k]);
            p3_write(&rr[k], CRUMPBIT_PARAM_FEEDBACK, fbk, &sh[k]);
            p3_write(&rr[k], CRUMPBIT_PARAM_RATE, rte, &sh[k]);
            p3_write(&rr[k], CRUMPBIT_PARAM_TONE,
                     k ? ton : CRUMPBIT_TONE_FLAT, &sh[k]);
        }
        for (block = 0u; block < 3000u; block++) {
            float l[2][32], r[2][32];
            uint32_t n;

            for (n = 0u; n < 32u; n++) {
                const float a = (block < 300u) ? rndf(-1.0f, 1.0f) : 0.0f;
                const float b = (block < 300u) ? rndf(-1.0f, 1.0f) : 0.0f;

                l[0][n] = l[1][n] = a;
                r[0][n] = r[1][n] = b;
            }
            for (k = 0u; k < 2u; k++)
                p3_block(&rr[k], &sh[k], l[k], r[k], 32u);
            for (n = 0u; n < 32u; n++)
                bad += !same_bits(l[0][n], l[1][n]) +
                       !same_bits(r[0][n], r[1][n]);
        }
        snprintf(msg, sizeof msg, "CB15 R1 ton %u = flat: %u samples differ",
                 ton, bad);
        expect(bad == 0u, msg);
    }
    free(out);
}

/*
 * CB16: `bal` (S091 P4) ------------------------------------------------------
 * S091 P5: run on `dly` (the SVF) and on `rvb` (the one-pole; `grn` would
 * also be, but it draws a hardware random word per block, so three runs
 * would not see the same engine output).
 */
static void law_cb16_on(uint8_t sub)
```

#### X24 — CB16: the sub-type is a parameter

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 2160–2160: **modify** (replace 1 line with 1; new lines 2350–2350).

- **What:** `p3_init(&rt[k], sub)`.

Current text (WT 2160–2160):

```c
            p3_init(&rt[k], CRUMPBIT_SUBTYPE_DELAY);
```

New text:

```c
            p3_init(&rt[k], sub);
```

#### X25 — CB16 on both paths; CB17, CB18, CB19

**Location:** `tools/dsp_test/modules/crumpbit.c`, WT lines 2187–2189: **modify** (replace 3 lines with 355; new lines 2377–2731).

- **CB16:** `law_cb16_on(sub)`; `law_cb16()` runs `dly` and `rvb`.
- **CB17 (X2):** the table and key are corrupted, a runtime is inited, and
  one block must rebuild the identity table; then 2,000 blocks of random
  `bof`/`biv`/`sub` changes, the table checked against `(c & keep) ^ inv`
  after each.
- **CB18 (G4):** `grainFeedbackGain(127) == 0.98f`, `(0) == 0`; at `fbk` 0,
  64, 100, 127: 0.2 s of grains into the echo (rte 0, 1/64 at 120 BPM),
  then Density off and silence; from 0.58 s, the output peak per echo
  period never grows and ends within the rounding floor 0.5 / (1 − gain)
  codes. Measured: 0, 0, 2, 25 codes (floors 0.5, 0.99, 2.19, 25.0).
- **CB19 (F1):**
  - (a) at `frq` 50, 70, 90, a sine at the SVF's −3 dB frequency through
    `rvb` LP and `dly` LP, both against `off`, mix 0: −3.01 dB ± 0.3
    (measured −3.01/−3.00, −3.01/−3.02, −3.01/−3.01);
  - (b, c) nine `rvb` runtimes, the eight types plus LP at `res` 120, mix 64,
    2,000 blocks: LP = LP2 = BP = UBP, HP = notch, peak = off, `res` 120 =
    `res` 0, bit for bit;
  - `off` on the one-pole path equals `dly` `off` (the dry signal) bit for
    bit;
  - (d) a 100 Hz sine, LP `frq` 90, mix 0, `sub` cycling dly → rvb → dly →
    grn every 37 blocks: the state moved exactly at every switch (checked on
    a copy through `crumpBit_enter()`), and the step at a switch below 0.05
    (measured 0.013, steady 0.008).

Current text (WT 2187–2189):

```c
                 "CB16 bal pass %u type %u frq %d d %d: %u samples differ",
                 pass, type, f, d, bad);
        expect(bad == 0u, msg);
```

New text:

```c
                 "CB16 bal sub %u pass %u type %u frq %d d %d: %u samples "
                 "differ", sub, pass, type, f, d, bad);
        expect(bad == 0u, msg);
    }
}

static void law_cb16(void)
{
    law_cb16_on(CRUMPBIT_SUBTYPE_DELAY);
    law_cb16_on(CRUMPBIT_SUBTYPE_REVERB);
}

/* CB17 (S091 P5 X2): the data-line table ---------------------------------- */
static int cb17_table_ok(uint8_t off, uint8_t inv)
{
    const uint32_t keep = (uint32_t)~((uint32_t)off & ~(uint32_t)inv) & 0xFFu;
    uint32_t c;

    for (c = 0u; c < 256u; c++)
        if (crumpBit_dataLines[c] != (uint8_t)((c & keep) ^ inv))
            return 0;
    return 1;
}

static void law_cb17(void)
{
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    CrumpBitRuntime rt;
    float l[32], r[32];
    uint32_t i;
    uint32_t bad = 0u;
    char msg[96];

    memset(l, 0, sizeof l);
    memset(r, 0, sizeof r);
    /* A stale table from another runtime must not survive an init. */
    crumpBit_dataLinesKey = 0u;
    memset(crumpBit_dataLines, 0, sizeof crumpBit_dataLines);
    p3_init(&rt, CRUMPBIT_SUBTYPE_DELAY);
    p3_block(&rt, &share, l, r, 32u);
    expect(cb17_table_ok(0u, 0u), "CB17 first block builds the table");
    for (i = 0u; i < 2000u; i++) {
        const uint8_t sub = (uint8_t)(rnd() % CRUMPBIT_SUBTYPE_COUNT);

        if (rnd() & 1u)
            p3_write(&rt, CRUMPBIT_PARAM_BIT_OFF, (uint8_t)rnd(), &share);
        if (rnd() & 1u)
            p3_write(&rt, CRUMPBIT_PARAM_BIT_INVERT, (uint8_t)rnd(), &share);
        p3_write(&rt, CRUMPBIT_PARAM_SUBTYPE, sub, &share);
        p3_block(&rt, &share, l, r, 32u);
        bad += !cb17_table_ok(rt.bit_off, rt.bit_invert);
    }
    snprintf(msg, sizeof msg, "CB17 table after changes: %u wrong", bad);
    expect(bad == 0u, msg);
}

/* CB18 (S091 P5 G4): the `grn` echo always decays -------------------------- */
static void law_cb18(void)
{
    static const uint8_t fbks[4] = { 0u, 64u, 100u, 127u };
    fx_share_t share = { arena_p3, 0u, CRUMPBIT_LOOP_MAX_BYTES };
    const float per_tick = CRUMPBIT_SAMPLES_PER_TICK / 120.0f;
    /* d: the echo period at rte 0 (1/64 at 120 BPM), whole frames. */
    const uint32_t d = (uint32_t)(crumpBit_grainTime[0] * per_tick);
    uint32_t t;
    char msg[160];

    expect(crumpBit_grainFeedbackGain(127u) == CRUMPBIT_GRAIN_FB_MAX &&
           crumpBit_grainFeedbackGain(0u) == 0.0f,
           "CB18 grn fbk 127 = 0.98, 0 = 0");
    cb_bpm = 120u;
    for (t = 0u; t < 4u; t++) {
        CrumpBitRuntime rt;
        const float gain = crumpBit_grainFeedbackGain(fbks[t]);
        /* floor: the largest code the 8-bit re-record can hold forever. */
        const float floor_codes = 0.5f / (1.0f - gain);
        float window = 0.0f;
        float last = 1e9f;
        uint32_t grew = 0u;
        uint32_t frame = 0u;
        uint32_t block;

        memset(arena_p3, 0, sizeof arena_p3);
        p3_init(&rt, CRUMPBIT_SUBTYPE_GRAIN);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 127u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_RATE, 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FEEDBACK, fbks[t], &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_LENGTH, 63u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY, 100u, &share);
        for (block = 0u; block < 10000u; block++) {     /* 7.3 s */
            float l[32], r[32];
            uint32_t n;

            if (block == 300u)      /* 0.2 s of grains, then none */
                p3_write(&rt, CRUMPBIT_PARAM_GRAIN_DENSITY, 63u, &share);
            for (n = 0u; n < 32u; n++) {
                l[n] = (block < 300u) ? rndf(-1.0f, 1.0f) : 0.0f;
                r[n] = (block < 300u) ? rndf(-1.0f, 1.0f) : 0.0f;
            }
            p3_block(&rt, &share, l, r, 32u);
            if (block < 800u)       /* the last grains end, the echo rings */
                continue;
            for (n = 0u; n < 32u; n++) {
                window = fmaxf(window, fabsf(l[n]) * 128.0f);
                if (++frame == d) {
                    grew += (window > last + 1e-3f);
                    last = window;
                    window = 0.0f;
                    frame = 0u;
                }
            }
        }
        snprintf(msg, sizeof msg,
                 "CB18 grn fbk %u (gain %.3f): echo peak %.2f codes (floor "
                 "%.2f), grew %u times", fbks[t], (double)gain, (double)last,
                 (double)floor_codes, grew);
        expect(grew == 0u && last < floor_codes + 0.01f, msg);
        printf("%s\n", msg);
    }
}

/* CB19 (S091 P5 F1): the one-pole output filter --------------------------- */

/* Goertzel power at f of `sub`'s output for a sine at f (mix 0). */
static double cb19_power(uint8_t sub, uint8_t type, uint8_t frq, uint8_t res,
                         double f)
{
    static uint8_t arena_f[16384];
    fx_share_t share = { arena_f, 0u, sizeof arena_f };
    const uint32_t total = 44108u;
    float *out = calloc(total, sizeof(float));
    CrumpBitRuntime rt;
    uint32_t got = 0u;
    uint32_t t = 0u;
    double p;

    memset(arena_f, 0, sizeof arena_f);
    p3_init(&rt, sub);
    p3_write(&rt, CRUMPBIT_PARAM_MIX, 0u, &share);
    p3_write(&rt, CRUMPBIT_PARAM_FILTER_TYPE, type, &share);
    p3_write(&rt, CRUMPBIT_PARAM_FILTER_FREQ, frq, &share);
    p3_write(&rt, CRUMPBIT_PARAM_FILTER_RESO, res, &share);
    while (got < total) {
        float l[32], r[32];
        uint32_t n;

        for (n = 0u; n < 32u; n++, t++) {
            l[n] = 0.5f * sinf(2.0f * (float)M_PI * (float)f * (float)t /
                               44108.0f);
            r[n] = l[n];
        }
        p3_block(&rt, &share, l, r, 32u);
        memcpy(out + got, l, sizeof l);
        got += 32u;
    }
    p = goertzel(out + total / 2u, total / 2u, f);
    free(out);
    return p;
}

static void law_cb19(void)
{
    static const uint8_t frqs[3] = { 50u, 70u, 90u };
    uint32_t i;
    char msg[160];

    /* (a) -3 dB at the SVF's res-0 frequency, both filters, vs `off`. */
    for (i = 0u; i < 3u; i++) {
        const double w3 = 2.0 * atan((double)crumpBit_filterG[frqs[i]] *
                                     sqrt(sqrt(2.0) - 1.0));
        const double f3 = w3 * 44108.0 / (2.0 * M_PI);
        const double off = cb19_power(CRUMPBIT_SUBTYPE_REVERB,
                                      CRUMPBIT_FILTER_OFF, frqs[i], 0u, f3);
        const double one = cb19_power(CRUMPBIT_SUBTYPE_REVERB,
                                      CRUMPBIT_FILTER_LP, frqs[i], 0u, f3);
        const double svf = cb19_power(CRUMPBIT_SUBTYPE_DELAY,
                                      CRUMPBIT_FILTER_LP, frqs[i], 0u, f3);
        const double db1 = 10.0 * log10(one / off);
        const double db2 = 10.0 * log10(svf / off);

        snprintf(msg, sizeof msg,
                 "CB19 frq %u at %.0f Hz: one-pole %.2f dB, SVF %.2f dB",
                 frqs[i], f3, db1, db2);
        expect(fabs(db1 + 3.01) < 0.3 && fabs(db2 + 3.01) < 0.3, msg);
        printf("%s\n", msg);
    }

    /*
     * (b) the type mapping and (c) `res` inert, bit for bit, on `rvb`
     * (LP = LP2 = BP = UBP; HP = notch; peak = off; res 0 = res 120).
     */
    {
        /* cls: each type's one-pole class, 0 low-pass, 1 high-pass, 2 flat. */
        static const uint8_t cls[8] = { 0u, 1u, 0u, 0u, 1u, 2u, 0u, 2u };
        static uint8_t arena_m[9][4096];
        CrumpBitRuntime rm[9];
        fx_share_t sh[9];
        uint32_t bad = 0u;
        uint32_t block;
        uint32_t k;

        for (k = 0u; k < 9u; k++) {
            /* 0..7: `rvb` with type k; 8: `rvb` LP at res 120. */
            sh[k].base = arena_m[k];
            sh[k].offset = 0u;
            sh[k].bytes = sizeof arena_m[k];
            memset(arena_m[k], 0, sizeof arena_m[k]);
            p3_init(&rm[k], CRUMPBIT_SUBTYPE_REVERB);
            p3_write(&rm[k], CRUMPBIT_PARAM_MIX, 64u, &sh[k]);
            p3_write(&rm[k], CRUMPBIT_PARAM_FILTER_FREQ, 60u, &sh[k]);
            p3_write(&rm[k], CRUMPBIT_PARAM_FILTER_TYPE,
                     (k < 8u) ? (uint8_t)k : CRUMPBIT_FILTER_LP, &sh[k]);
            p3_write(&rm[k], CRUMPBIT_PARAM_FILTER_RESO,
                     (k == 8u) ? 120u : 0u, &sh[k]);
        }
        for (block = 0u; block < 2000u; block++) {
            float l[9][32], r[9][32];
            uint32_t n;

            for (n = 0u; n < 32u; n++) {
                const float a = rndf(-1.0f, 1.0f);
                const float b = rndf(-1.0f, 1.0f);

                for (k = 0u; k < 9u; k++) {
                    l[k][n] = a;
                    r[k][n] = b;
                }
            }
            for (k = 0u; k < 9u; k++)
                p3_block(&rm[k], &sh[k], l[k], r[k], 32u);
            for (n = 0u; n < 32u; n++) {
                for (k = 1u; k < 8u; k++) {
                    /* the first type of each class: LP 0, HP 1, flat 5. */
                    const uint32_t first = (cls[k] == 0u) ? 0u
                                         : ((cls[k] == 1u) ? 1u : 5u);

                    bad += !same_bits(l[k][n], l[first][n]) +
                           !same_bits(r[k][n], r[first][n]);
                }
                bad += !same_bits(l[8][n], l[0][n]) +
                       !same_bits(r[8][n], r[0][n]);
            }
        }
        snprintf(msg, sizeof msg,
                 "CB19 type mapping and res inert: %u samples differ", bad);
        expect(bad == 0u, msg);
    }
    {
        /* `off` on the one-pole path is the dry signal, as `dly` `off`. */
        static uint8_t arena_o[2][4096];
        CrumpBitRuntime ro[2];
        fx_share_t sh[2];
        uint32_t bad = 0u;
        uint32_t block;
        uint32_t k;

        for (k = 0u; k < 2u; k++) {
            sh[k].base = arena_o[k];
            sh[k].offset = 0u;
            sh[k].bytes = sizeof arena_o[k];
            p3_init(&ro[k], k ? CRUMPBIT_SUBTYPE_REVERB
                              : CRUMPBIT_SUBTYPE_DELAY);
            p3_write(&ro[k], CRUMPBIT_PARAM_MIX, 0u, &sh[k]);
        }
        for (block = 0u; block < 1000u; block++) {
            float l[2][32], r[2][32];
            uint32_t n;

            for (n = 0u; n < 32u; n++) {
                l[0][n] = l[1][n] = rndf(-1.5f, 1.5f);
                r[0][n] = r[1][n] = rndf(-1.5f, 1.5f);
            }
            for (k = 0u; k < 2u; k++)
                p3_block(&ro[k], &sh[k], l[k], r[k], 32u);
            for (n = 0u; n < 32u; n++)
                bad += !same_bits(l[0][n], l[1][n]) +
                       !same_bits(r[0][n], r[1][n]);
        }
        snprintf(msg, sizeof msg, "CB19 one-pole off = dry: %u differ", bad);
        expect(bad == 0u, msg);
    }

    /*
     * (d) the hand-over on a switch: the state moves exactly, and the
     * output does not jump. A 100 Hz sine through `LP` (frq 90, -3 dB
     * near 2.6 kHz), mix 0; the sub-type flips dly <-> rvb (and dly ->
     * grn) every 37 blocks.
     */
    {
        static uint8_t arena_s[4096];
        static const uint8_t subs[4] = { CRUMPBIT_SUBTYPE_DELAY,
                                         CRUMPBIT_SUBTYPE_REVERB,
                                         CRUMPBIT_SUBTYPE_DELAY,
                                         CRUMPBIT_SUBTYPE_GRAIN };
        fx_share_t share = { arena_s, 0u, sizeof arena_s };
        CrumpBitRuntime rt;
        float prev = 0.0f;
        float worst_switch = 0.0f;
        float worst_run = 0.0f;
        uint32_t moved_bad = 0u;
        uint32_t t = 0u;
        uint32_t block;

        memset(arena_s, 0, sizeof arena_s);
        p3_init(&rt, CRUMPBIT_SUBTYPE_DELAY);
        p3_write(&rt, CRUMPBIT_PARAM_MIX, 0u, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FILTER_TYPE, CRUMPBIT_FILTER_LP, &share);
        p3_write(&rt, CRUMPBIT_PARAM_FILTER_FREQ, 90u, &share);
        for (block = 0u; block < 3000u; block++) {
            float l[32], r[32];
            const uint32_t sw = (block >= 100u && (block % 37u) == 0u);
            uint32_t n;

            if (sw) {
                /* The hand-over itself, checked on the bare enter. */
                const uint8_t next = subs[(block / 37u) % 4u];
                CrumpBitRuntime probe = rt;

                if (next != probe.active_subtype) {
                    const uint32_t from1 =
                        crumpBit_filterIsOnePole(probe.active_subtype);
                    const float l1 = probe.svf_l1;
                    const float l2 = probe.svf_l2;

                    crumpBit_enter(&probe, next, share.bytes);
                    if (crumpBit_filterIsOnePole(next) && !from1)
                        moved_bad += !same_bits(probe.svf_l1, l2);
                    if (!crumpBit_filterIsOnePole(next) && from1)
                        moved_bad += !same_bits(probe.svf_l2, l1) ||
                                     probe.svf_l1 != 0.0f;
                }
                p3_write(&rt, CRUMPBIT_PARAM_SUBTYPE, next, &share);
            }
            for (n = 0u; n < 32u; n++, t++) {
                l[n] = 0.5f * sinf(2.0f * (float)M_PI * 100.0f * (float)t /
                                   44108.0f);
                r[n] = l[n];
            }
            p3_block(&rt, &share, l, r, 32u);
            if (block >= 50u) {
                const float jump = fabsf(l[0] - prev);

                if (sw)
                    worst_switch = fmaxf(worst_switch, jump);
                else
                    worst_run = fmaxf(worst_run, jump);
            }
            prev = l[31];
        }
        snprintf(msg, sizeof msg,
                 "CB19 hand-over: %u states wrong; jump at a switch %.4f "
                 "(steady %.4f)", moved_bad, (double)worst_switch,
                 (double)worst_run);
        expect(moved_bad == 0u && worst_switch < 0.05f, msg);
        printf("%s\n", msg);
```

#### X26 — `main()`: run CB17–CB19

**Location:** `tools/dsp_test/modules/crumpbit.c`, after WT line 2210: **add** 3 lines (new lines 2753–2755).

- **What:** calls the three new laws after CB16.

New text:

```c
    law_cb17();
    law_cb18();
    law_cb19();
```


---

## 5. Gates

Run in this order after applying every unit (all passed on the scratch
tree):

| # | Command (from the repository root) | Expected |
|---|---|---|
| 1 | `python3 tools/plots/gen_crumpbit_tables.py` | `wrote Core/DSP/Effects/CrumpBit/CrumpBitTables.c` (R1) |
| 2 | `make all && make img` | clean link; the same 20 warnings as WT; `Flash : 710,316`; `FXBUF : 126,656 B at 0x20001140`; `Written:build/LXRV2_lxr02.img (710316b) OK` |
| 3 | `make -C tools/dsp_test crumpbit_tables` | `crumpbit tables OK` |
| 4 | `make -C tools/dsp_test crumpbit` | `crumpbit failures: 0`; `crumpbit_break1` … `crumpbit_break18: caught` |
| 5 | `make -C tools/dsp_test crumpbit_elf_check` | `dly` 111, `cmb` 109, `pit` 106, `grn` 116/112, `rvb` 131, `process` 31 and 16, `divisionFor` 113, `tableLimit` 63; exit 0 |
| 6 | `make -C tools/dsp_test crumpbit_cpu_check` | the five lines of §4.4 N2; `crumpbit cpu: worst 166.5 <= cap 170` |

Order of work (plan §8.1) still applies if the units are applied in steps:
A, B, S1 → P1, R1 → X2 → F1 (R1/G-units first) → G3, G4, G6 → tests →
docs. Rebuild and run gates 5 and 6 after each step: register pressure
moves when code moves. The units are independent of that order only as
text; the numbers in §2 are for all of them together.

---

## 6. Documentation changes (same step)

### D1 — `tools/dsp_test/DSP_TEST.md`

- **Line 38 (`crumpbit.c` in the tree):** "… the S091 P4 `grn`/`bal` laws
  and the S091 P5 laws (CB1–CB19)".
- **Line 50 (`elf_looppath.py`):** add "(cycle-pruned search, `--rank`)";
  add two tree lines below it:
  - `elf_blockpath.py` — longest per-block path of one function, frame
    loops weighted 0 (`--via-loop K`);
  - `crumpbit_cpu.py` — the whole CrumpBit Effect per frame per sub-type
    against the 170 cap.
- **Line 91 (the `crumpbit` row):** CB4 "against the S091 P2 reference
  with the S091 P5 Q14 read and per-block feedback/balance"; CB6 adds
  "`dpn` never clamped by the manager; the `grn` engine's cached Position
  fit equals the label's; the one-pole k table's −3 dB law"; CB14 adds
  "Position beyond the line plays the last fitting stop; block-aligned grain
  ends"; CB15 adds "`ton` above 63 = 63"; CB16 "on the SVF (`dly`) and the
  one-pole (`rvb`)"; new: "CB17 the data-line table; CB18 the `grn` echo
  decays to the rounding floor; CB19 the one-pole filter (−3 dB match, type
  mapping, `res` inert, hand-over)"; "Eighteen break builds".
- **Line 93 (the `crumpbit_elf_check` row):** `dly` 111, `cmb` 109, `pit`
  106, `grn` 116 (`--cond-heads --in-range`), `rvb` 131, the SVF filter 31
  and the one-pole 16 (`--rank 2`).
- **New row after line 93:** `crumpbit_cpu_check` — "linked image: the
  whole CrumpBit Effect per frame per sub-type (engine loop + filter loop +
  per-block work / 32, `crumpbit_cpu.py`) at or below 170 (needs `make all`
  first; not in `all`)".
- **Line 96 (the `elf_looppath.py` usage):** add `[--rank K]`, and the
  `elf_blockpath.py --elf … --sym NAME [--via-loop K] [--max N]` usage.

### D2 — `knowledge_files/specification_reference/dsp_instruments_effects/EFFECTS_MIXER_DSP_REFERENCE.md`

- **§4.4 (line 343), the output filter:** stage 4 is the resonant TPT SVF in
  `dly`/`cmb`/`pit` and a one-pole in `grn`/`rvb` (k from
  `crumpBit_onePoleK`, −3 dB matched to the SVF at `res` 0; `typ` mapped to
  LP/HP/flat; `res` without effect; the state handed over on a switch);
  coefficients worked out after the engine. Stage 1: the data-line table
  (256 B `.bss`, rebuilt on a `bof`/`biv` change).
- **§4.4a (line 380), the engines:**
  - `dly`/`cmb`: Q14 integer read; feedback and `dpn` balance per block;
  - `pit`: drop-sample heads; feedback and balance per block;
  - `grn`: the engine clamps Position to the stops that fit (cached per
    tempo and line); grain ends on block boundaries (spans rounded, steals
    padded, idle voices parked); no window lower clamp; the echo plain
    with gain 0..0.98 over `fbk`; feedback per block;
  - `rvb`: damping low-pass only (`ton` above 63 flat);
  - `mix` still ramps per frame in every sub-type.
- **§6.2a (line 829), the CrumpBit row:** "per sub-type, worst case per
  frame (S091 P5, gated by `crumpbit_cpu_check` at 170): `dly` 160.0, `cmb`
  153.8, `pit` 153.6, `grn` 166.5, `rvb` 163.9".

### D3 — `knowledge_files/specification_reference/dsp_instruments_effects/EFFECTS_BUS_REFERENCE.md`

- **Line 69 (stale since P4):** "126,688 B" → "126,656 B".
- **Line 535 (stale since P3):** `format_target3` names `sub` only; remove
  "and its per-sub-type `rte` text".
- **§8.6 (line 617), the row table (lines 655–664):**
  - `fbk` in `grn`: "echo gain 0..0.98 (shown 0..98)";
  - `ton` in `rvb`: "damping (low-pass; above 63 flat, shown 0)";
  - add the shared rows' meaning in `grn`/`rvb`: `typ` LP/HP/off (mapped),
    `res` `---`.
- **§8.6, the value labels (line 671 on):** add the four P5 labels (C4).
  Position's rhythmic half: "shows the stop it plays (the engine limits it
  to the line; `dpn` is not BUFFER_DEPENDENT since S091 P5)".

### D4 — `knowledge_files/specification_reference/STORAGE_SRAM_MANIFEST.md`

- **Line 697 (`effects_runtime`):** "CrumpBit 100 B: 54 B shared (S091 P5
  removed two ramp-origin bytes; 2 B padding) + 44 B sub-engine union (the
  `grn` member holds the Position-fit cache in the bytes its echo DC
  blocker held)".
- **New row in the SRAM1 `.bss` table:** `crumpBit_dataLines` 256 B and
  `crumpBit_dataLinesKey` 4 B, `CrumpBitEffect.c`, "stage 1 data-line
  table (S091 P5 X2, user-approved Q4, 2026-11-05)".
- **The arena paragraph:** FXBUF unchanged (126,656 B at `0x20001140`).

### D5 — `knowledge_files/specification_reference/dsp_instruments_effects/CPU_USE_DSP_AUDIT.md`

After item 64 (line 1045), add:

```markdown
65. **REDUCED (S091 P5): CrumpBit sub-type efficiencies.** User cap: 170
    instructions per frame for the whole Effect, every sub-type, worst
    case, gated by `crumpbit_cpu_check`. Q14 tape read (`dly`/`cmb`);
    drop-sample `pit` heads; low-pass-only `rvb` damping; `fbk`/`dpn` per
    block (`mix` keeps its ramp, user); stage 1 through a 256 B table;
    `grn`: Position clamped in the engine (cached fit), grain ends on block
    boundaries and no window lower clamp, plain echo below unity; a one-pole
    output filter in `grn`/`rvb`.
    - Linked worst case per frame: `dly` 174.2 -> 160.0, `cmb` 170.3 ->
      153.8, `pit` 183.6 -> 153.6, `grn` 203.0 -> 166.5, `rvb` 194.9 ->
      163.9. No per-sample VDIV.
    - RAM +260 B `.bss` (SRAM1); runtime 100 B unchanged; text +456 B.
    - Rejected after measurement: X3 (filter in the engine loops, +6..+13),
      R3 (+1.1), V2 (no saving), V4 (hardware: the reciprocal table is up
      to 10 cycles per sample slower; `VDIV` overlaps other work on the
      M7). Plan: `S091_P5_SUBTYPE_EFFICIENCIES.md` §5.
    - Host CB1-CB19, eighteen breaks.
```

### D6 — `MEMORY.md` (after the hardware result)

- **Status line:** "S091 P5 implemented (2026-11-xx): every CrumpBit
  sub-type ≤ 170 per frame (`crumpbit_cpu_check`); …; hardware HW1–HW9
  pending. Notes: `S091_P5_IMPLEMENTATION.md`."
- **Binding Rules → Effects, the CrumpBit line:** add "the whole Effect ≤
  170 instructions per frame in every sub-type, worst case
  (`crumpbit_cpu_check`); `grn`/`rvb` use the one-pole output filter;
  `mix` ramps per frame, `fbk`/`dpn` per block; `grn` grain ends lie on
  block boundaries (no window lower clamp)".

### D7 — `knowledge_files/volatile/OPEN_TESTS.md`

Under "Hardware soak / confirmation", add "**S091 P5 CrumpBit efficiencies
HW1–HW9**" with the §7 list (and keep the S091 P4 H1–H9 entry: still open).

---

## 7. Hardware checks

| # | Check | Expected |
|---|---|---|
| HW1 | FluxMax (the worst-case Scene), CrumpBit in each sub-type in turn; `cpu` widget, DEV `UNR` | below P3's `dly` reading in every sub-type; no underruns |
| HW2 | `dly`/`cmb` A/B against P4 by ear | the same character (Q14 read) |
| HW3 | Fast LFO on `fbk`, `dpn` (S1); `mix` as a control | `mix` smooth as on P4; judge zipper on `fbk`/`dpn` |
| HW4 | `pit` at ±12/±24 st | grittier, acceptable (P1) |
| HW5 | `rvb` `ton` (`dmp`) sweep | dark below centre, flat above and shown 0 (R1) |
| HW6 | `grn` `fbk` sweep to max (shown 0..98) | the whole knob usable; the echo decays and never runs away; at the top a quiet residual pattern may hold (§8, item 1) |
| HW7 | `typ`/`frq`/`bal` in `grn`/`rvb` (`typ` shows LP/HP/off, `res` `---`); switch sub-types under audio | one-pole response; no click beyond the wanted switch glitch (F1) |
| HW8 | `bof`/`biv` from the page and from FX lanes at fast steps | no audible change from P4; no underrun on a change (X2 rebuild) |
| HW9 | `grn` with dense random Density (`den` 0..30), long Length, steals | no clicks at grain ends or steals (G3, the E41 fix) |

The SD card still holds the S092 cycle-test image and an edited AutoSave
(plan §9): restore the P5 image to the card before HW1.

---

## 8. Risks and notes

1. **The `grn` echo decays to the 8-bit rounding floor, not to silence.**
   The line re-records `round(gain · code)`; a code `c` stays fixed while
   `|c| · (1 − gain) ≤ ½`. At `fbk` 127 (0.98) a pattern of up to ±25 codes
   (about −14 dBFS) can repeat indefinitely; at `fbk` 100 (0.77) up to ±2
   codes; below about 0.5 nothing remains (CB18). P4's echo had the same
   floor at the same gain (its DC blocker passes a periodic pattern), and
   the tape sub-types behave the same way. If HW6 finds the residual
   unwanted, two cheap options: a top gain of 0.95 (floor 10 codes), or
   one LSB of decay toward 0 per pass in the echo write (one more
   compare-select per frame; would need re-measuring against the 3.5
   margin).
2. **G3 relies on 32-frame blocks.** mixer.c always passes
   `OUTPUT_DMA_SIZE` (32). A future variable block size would let a
   voice's end fall inside a block, where the window can go negative:
   restore the lower clamp, or round to the real block length.
3. **`grn` margin is 3.5 per frame.** Any later per-frame addition to
   `grn` must be measured by `crumpbit_cpu_check` before it is accepted.
4. **The X2 rebuild block** costs about 48 per frame once per `bof`/`biv`
   change (outside the cap by decision). An FX lane changing both rows every
   step at the fastest scale costs one rebuild per step, still once per
   many blocks.
5. **The P4 age defect (E41)** also affected P4 sound (grains up to 31
   frames short, a small step in their window when a trigger was dropped or
   retried). The fix changes `grn` output in those blocks only; no law
   depended on the defect.
6. **Host CB11 uses short blocks.** With blocks under 32 frames, G3's
   rounding does not land on the host's block ends; CB11 only checks
   finiteness and bounds there, which still hold (a negative window lasts
   less than one block and is bounded by 31/128).

---

## 9. Work log — implementation in this working tree (2026-11-05)

Baseline: the S091 P4 working tree at HEAD `cc8ca99a` plus its uncommitted
changes ("WT" above). The working tree was not committed or reset by this
session; every edit below is an uncommitted change on top of WT.

1. **Baseline check before editing.** This document was parsed into its 124
   units (H1–H8, E1–E61, P1, C1–C4, B1–B2, R1, G1–G8, L1–L5, N1–N2, T1–T6,
   X1–X26). Every unit with a "Current text" block matched the working tree
   byte for byte, every stated "new lines" count matched, and every
   `add` anchor was in range. No unit was retyped: the validated units
   were applied bottom-up per file and the result was checked against the
   expected line counts. Nothing else was edited.
2. **Firmware source units applied** (all except R1, which the generator
   owns, and the two new tools):
   - `CrumpBitEffect.h`: H1–H8 (runtime stays 100 B, `grn` state 44 B);
   - `CrumpBitEffect.c`: E1–E61 (A, B, P1, R1, G3, S1, X2, G4, G6, F1,
     and the P4 age defect E41);
   - `CrumpBitParameters.h` P1; `CrumpBitParameters.c` C1–C4;
   - `CrumpBitTables.h` B1–B2.
   One comment-only clarification was made next to H6: the `filter_bal`
   bullet no longer says the shared part "stays 56 B"; it now says 54 B
   used plus 2 B of padding (56 B before the union), matching H6.
3. **Tables regenerated.** G1–G8 in `tools/plots/gen_crumpbit_tables.py`,
   then `python3 tools/plots/gen_crumpbit_tables.py`; `--check` prints
   `crumpbit tables OK`. Existing tables are unchanged against the P4
   generated copy (`tools/dsp_test/build/gen/cb_tables.c`); the file only
   grew by `crumpBit_onePoleK` (512 B flash).
4. **Tooling units.** L1–L5 in `elf_looppath.py` (cycle-pruned search,
   disassembly cache, `--rank`); N1 `elf_blockpath.py` (141 lines) and
   N2 `crumpbit_cpu.py` (95 lines), both executable; T1–T6 in the host
   Makefile (breaks 13–18, ELF limits, `crumpbit_cpu_check`).
5. **Host tests.** X1–X26 in `modules/crumpbit.c`: `make -C tools/dsp_test
   crumpbit` gives `crumpbit failures: 0` and catches all eighteen breaks
   (break1–break18). Evidence matches §2.5: CB15 15.24 dB / 2.07 dB; CB18
   echo peaks 0, 0, 2 and 25 codes for `fbk` 0, 64, 100, 127 (floors 0.50,
   0.99, 2.19, 25.00), never growing; CB19 one-pole −3.01 dB and SVF
   −3.00/−3.02/−3.01 dB; hand-over 0 states wrong, switch jump 0.0131
   against 0.0076 steady.
6. **Linked image.** `make clean && make all && make img`: text 709,896,
   data 420, bss 413,284, Flash 710,316 / 753,664 (headroom 43,348), ITCM
   15,280 / 16,384, DTCM statics 4,396, FXBUF 126,656 B at
   `0x20001140` (margin 3,776), `Written:build/LXRV2_lxr02.img (710316b)
   OK`. Clean build: 20 warnings, the same set as WT, none in a CrumpBit
   file.
7. **ELF gate** (`crumpbit_elf_check`): `dly` 111, `cmb` 109, `pit` 106,
   `grn` 116/112, `rvb` 131, `process` 31 (SVF) and 16 (one-pole),
   `divisionFor` 113, `tableLimit` 63; no VDIV in any frame loop.
8. **CPU gate** (`crumpbit_cpu_check`): `dly` 160.0, `cmb` 153.8, `pit`
   153.6, `grn` 166.5, `rvb` 163.9; `crumpbit cpu: worst 166.5 <= cap
   170`. Every term reproduces §2.2 exactly.
9. **Documentation (§6).** D1 `tools/dsp_test/DSP_TEST.md`; D2
   `EFFECTS_MIXER_DSP_REFERENCE.md` (§4.4, §4.4a, §6.2a); D3
   `EFFECTS_BUS_REFERENCE.md` (§8.6 rows and labels, the two stale P4
   lines); D4 `STORAGE_SRAM_MANIFEST.md` (the 260 B table row, the
   runtime note, the FXBUF statement); D5 `CPU_USE_DSP_AUDIT.md` item 65;
   D7 `OPEN_TESTS.md` (S091 P4 H1–H9 still open, S091 P5 HW1–HW9). D6
   (`MEMORY.md`) is deliberately not yet applied: its own note says
   "after the hardware result".
10. **Not done here:** hardware HW1–HW9; D6; a commit (commits belong to
    the user). The SD card still holds the S092 cycle-test image and the
    edited AutoSave described in the plan §9.

---

## 10. Assessment of the landed change (2026-11-05)

The work log (§9) was checked claim by claim against an independent
rebuild; every figure it gives (units applied, tables, host evidence, image
sizes, gates, documentation status) is confirmed below.

### 10.1 Code against the schedule

Each landed file was compared byte for byte with the verified scratch tree
(`impl/`, §0):

| File | Result |
|---|---|
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | identical (E1–E61) |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | identical except one comment (below) |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` / `.c` | identical (P1, C1–C4) |
| `Core/DSP/Effects/CrumpBit/CrumpBitTables.h` / `.c` | identical (B1–B2, R1) |
| `tools/plots/gen_crumpbit_tables.py` | identical (G1–G8) |
| `tools/dsp_test/elf_looppath.py` | identical (L1–L5) |
| `tools/dsp_test/elf_blockpath.py`, `crumpbit_cpu.py` (new) | identical (N1, N2); executable |
| `tools/dsp_test/Makefile`, `modules/crumpbit.c` | identical (T1–T6, X1–X26) |
| `EffectsManager.c`, `menuEffects.c` | unchanged by P5, as scheduled |

**The one difference is an improvement.** `CrumpBitEffect.h` lines 211–212
(the `filter_bal` bullet of the runtime comment) now read "Since S091 P5
(S1) freed two bytes, the used shared bytes are 54 and 2 B of padding
precede the union (56 B in all)". The schedule (H6) left the old "so the
shared part stays 56 B" there, which contradicted H6's own "54 B used, 2 B
padding". Comment only; the binary is unaffected (§10.2).

### 10.2 Independent rebuild and gates

A fresh copy of the repository (sources only, empty `build/`) was built
clean and every gate run on it:

| Gate | Result |
|---|---|
| `make all && make img` | 20 warnings, the same set as WT; Flash 710,316 B (headroom 43,348); ITCM 15,280; DTCM statics 4,396; FXBUF 126,656 B at `0x20001140`; `Written … (710316b) OK` |
| Binary | `build/lxr02.bin` **byte-identical** to the verified build; the repository's `build/LXRV2_lxr02.img` is that same image |
| `crumpbit_tables` | `crumpbit tables OK` |
| `crumpbit` | `crumpbit failures: 0`; all 18 breaks caught |
| `crumpbit_elf_check` | exit 0: `dly` 111, `cmb` 109, `pit` 106, `grn` 116/112, `rvb` 131, `process` 31 and 16, `divisionFor` 113, `tableLimit` 63 |
| `crumpbit_cpu_check` | exit 0: `dly` 160.0, `cmb` 153.8, `pit` 153.6, `grn` 166.5, `rvb` 163.9; worst 166.5 ≤ 170 |

The host figures repeat §2.5 exactly: CB8 peaks 1.000; CB15 decay 15.24 dB,
freeze 2.07 dB; CB18 echo floors 0 / 0 / 2 / 25 codes; CB19 −3.01 dB
(one-pole) against −3.00 / −3.02 / −3.01 dB (SVF), the hand-over exact,
step at a switch 0.0131 (steady 0.0076).

### 10.3 Documentation (D1–D7)

| Unit | Result |
|---|---|
| D1 `DSP_TEST.md` | done: tree lines for the three tools, the `crumpbit` row (CB1–CB19, 18 breaks), the new ELF limits, the `crumpbit_cpu_check` row, both usages |
| D2 `EFFECTS_MIXER_DSP_REFERENCE.md` | done, two small gaps (below) |
| D3 `EFFECTS_BUS_REFERENCE.md` | done: line 69 (126,656 B), line 535 (`format_target3` names `sub` only), the row table (`fbk` in `grn`, `ton` in `rvb`), the P5 labels, `dpn` not BUFFER_DEPENDENT |
| D4 `STORAGE_SRAM_MANIFEST.md` | done: `effects_runtime` 54 + 2 + 44 B; the 260 B `.bss` row; the change-log line |
| D5 `CPU_USE_DSP_AUDIT.md` | done: item 65 as scheduled |
| D6 `MEMORY.md` | not yet, correctly: the schedule defers it to the hardware result |
| D7 `OPEN_TESTS.md` | done: HW1–HW9 added, the S091 P4 H1–H9 entry kept |

**D2 gaps (documentation only, no code impact):**
1. **§4.4 pseudo-code, the delay sub-type:** the tap read is still the S074
   float form (`rp = wp − delay`, `i0 = ⌊rp⌋`, `frac = rp − i0`, muted
   unless `delay + 2 ≤ valid`). Since B it is the Q14 integer read (`d14 =
   trunc(delay·2¹⁴)`, `rp = (wp << 14) − d14` wrapped by its sign,
   `wet = (s0·2¹⁴ + frac·(s1 − s0)) / 2²¹`, gated by `d14 + 2·2¹⁴ ≤ valid <<
   14`), and the §4.4a `cmb` bullet already refers to "the Q14 integer tap
   as `dly`". The pseudo-code also does not say that `fb` and the balance
   gain `g` are per-block values (S1). There is no `dly` bullet in §4.4a to
   carry these instead.
2. **§4.4a `grn` bullet:** "it always decays" should say "decays to the
   8-bit rounding floor" (up to ±25 codes held at `fbk` 127; §8 item 1,
   CB18).

### 10.4 Open

- **Hardware:** HW1–HW9 (§7, `OPEN_TESTS.md`), and the S091 P4 H1–H9,
  still open. Restore the P5 image to the card first (the card holds the
  S092 cycle-test image and an edited AutoSave, plan §9).
- **HW6 decides** whether the `grn` echo's residual at the top of `fbk` is
  acceptable (§8 item 1).
- **D6** (`MEMORY.md` status and the CrumpBit binding line) after the
  hardware result. `MEMORY.md` still names S091 P3 as the latest CrumpBit
  status; the P4 line was also left for its hardware result.
- **The two D2 gaps** in §10.3.

### 10.5 Verdict

The landed code is the verified schedule, with one comment improved: the
firmware image is byte-identical to the measured one, every host law and
break holds, and every sub-type is under the 170 cap (worst `grn` 166.5).
Ready for hardware.

---

## 11. Follow-up fixes: pitch rows ±36 and the `pit` window glide (user, 2026-11-05)

Applied directly to the repository after §10 (not through a scratch
schedule). Every figure below was measured on the repository build.

### 11.1 Report, investigation and findings

**User report:** some pitch intervals keep changing the sound while their
displayed value has stopped (at 24): the three `grn` pitches and `pit`'s
`int`. Check every other row for the same fault. Separately: adjusting `win`
in `pit` steps or glitches.

**Pitch rows, measured on the host for every row value 0..127:**

| Row | Label (before) | Sound (before) |
|---|---|---|
| `pit` `int` (`aux`) | `row − 63`, held at ±24 from rows 39/87 | ±24 st spread over the whole row, 0.38 st per step (`snp` rounding it): row 75 showed +12 and played +4.6; from row 87 the label sat at 24 while the pitch kept rising to +24 |
| `grn` P1 (`aux`), P2, P3 | `row − 63`, held at ±24 | the same: whole semitones, held at ±24; rows 0–39 and 87–127 did nothing |

So `pit` `int` was the mismatch the user heard. The `grn` pitches agreed
with their labels but had two dead outer thirds.

**Every other row, checked the same way** (label law against the DSP law):
no other row's label stops while its sound continues. Dead zones where both
stop together:
- `cmb` `rte` above 96 (the note holds at its lowest, 20.6 Hz): unchanged,
  reported to the user;
- `rvb` `ton` above 63 (flat by the P5 decision R1, shown `0`).
The share-limited rows (`rte` everywhere, `grn` Position) show the clamped
value, as designed.

**`win` in `pit`:** each head trails the write head by `W × phase`. One
`rte` step changes W by about 2.4 % (up to ~200 samples), so both read
positions jumped in one block: a click. Measured as the largest second
difference of the output in the two blocks after a `win` step, against
steady blocks (a 220 Hz sine through `pit`, `win` stepping one value every
20 blocks): **0.59 at a step against 0.031 steady (19×)**.

### 11.2 Decisions (user) and choices made here

- **All four pitch rows are ±36 st, one semitone per step, and clamped
  there**, so the parameter shows and plays the same value and stops at
  both ends.
- **Add the window glide.**

Choices made here:
1. **A row minimum for Effect rows.** Effect rows had only an inclusive
   maximum (`max_value`). The modulation domain already carries a
   `min_value` (it bounds the LFO), so a row built with the new
   `EFFECT_MOD_RANGE(lo, hi)` gets that `lo` as its floor everywhere a value
   is set or resolved (`effects_descriptorMin()`): page edits and pots,
   Morph-view edits, MIDI (through `effects_setParameter()`), FX-lane locks,
   Pattern automation, STEP automation editing, and the manager's per-block
   resolution (which also clamps a value that arrives another way, e.g. an
   older file). Every existing row has min 0, so nothing else changes.
2. **Stored 27..99** (63 = 0, `CRUMPBIT_PITCH_ROW_MIN/MAX`), so existing
   Scenes keep their meaning within ±36 st (a stored `aux` 63 is still 0 st).
   No backward compatibility is needed: a value saved outside 27..99 plays
   at the end it exceeds and moves inside on its next edit.
3. **`pit` `int` is whole semitones** (the `grn` law), so `snp` (`snc` in
   `pit`) has nothing left to snap: it is inert and shows `---`. Open: give
   it a new job or take it off the `pit` page (§11.6).
4. **The `grn` Q3.12 step stops at 32,767.** At +36 st the ratio is exactly
   8.0, one past `int16_t`; the step is held, so the very top plays 0.05
   cent flat. Widening the step to 32 bits (into `reserved`) was not needed
   for that.
5. **The glide:** a 20 ms one-pole per block (`CRUMPBIT_PITCH_WIN_GLIDE`
   0.03563 = 1 − e^(−32 / (0.020 × 44,108))), ramped linearly across the
   block in the heads' Q14 distance (one integer add per frame). The first
   `pit` block starts on the target (`primed`); the target and the stored
   window are clamped to the loop every block (a share shrink never leaves a
   head outside it). The phase step uses the block's end window. While `win`
   moves, the pitch bends briefly (tape-like) instead of clicking. State:
   `CrumpBitPitchState.window`, 28 → 32 B inside the 44 B union (no runtime
   growth; the user noted the room).

### 11.3 Changes by file (line numbers in the changed files)

| File | Lines | Change |
|---|---|---|
| `Core/DSP/Effects/EffectParamRows.h` | 31–40 | **add** `EFFECT_MOD_RANGE(lo, hi)` and its comment |
| `Core/DSP/Effects/EffectsManager.h` | 299–319 | **add** `effects_descriptorMin()` (inline; the domain's `min_value`, else 0) with its comment |
| `Core/DSP/Effects/EffectsManager.c` | 731–733, 1142–1144, 1563–1565, 2099–2104 | **add** the floor next to the four `max_value` clamps: `effects_setParameterScene()`, `effects_setSeqLaneLockScene()`, the Pattern automation setter, `effects_service()` resolution |
| | 1731–1733 | **modify** the registry self-check: code 7 also when the default is below the floor |
| `Core/Menu/menuEffects.c` | 463–469 | **add** the floor for PARAM cells in `menuEffects_clampValue()` (encoder, pots, held-step edits stop at −36) |
| `Core/Menu/menu.c` | 14321–14323 | **add** the floor in `menu_stepAutomationCurrentValue()` (a new STEP automation entry starts in range) |
| | 14520–14521, 14559–14562, 14565–14566 | **add** `min_val` to the STEP automation value editor for 7-bit Effect targets |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` | 164–175 | **add** `CRUMPBIT_PITCH_ROW_MIN` (27) / `_MAX` (99) and their comment |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | 70–73 | **modify** the `aux` description |
| | 138–141, 143–156 | **modify** rows `aux`, `p2`, `p3`: `EFFECT_MOD_RANGE(27, 99)`, maximum 99 |
| | 377–388 | **modify** `crumpBit_formatSemitones()`: clamp ±`CRUMPBIT_SEMITONE_SPAN` (36) |
| | 417–420, 507–511 | **modify/add** the value-text comment and `snp` in `pit` → `---` |
| `Core/DSP/Effects/CrumpBit/CrumpBitTables.h` | 37–38, 72 | **modify** `CRUMPBIT_SEMITONE_SPAN` 24 → 36 and the table comment |
| `Core/DSP/Effects/CrumpBit/CrumpBitTables.c` | 184–204 | **regenerated** `crumpBit_semitoneRatio[73]` (−36..+36); every other table byte-identical |
| `tools/plots/gen_crumpbit_tables.py` | 36–40, 93–94, 125–127, 190–191, 298, 330 | **modify** the semitone table to `SEMITONE_SPAN = 36` (docstring, constant, table, comment, check) |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | 82–87, 97 | **add** `CrumpBitPitchState.window` (32 B) and its comment |
| | 214 | **modify** the `primed` comment (also `pit`) |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | 113–119, 151 | **add** `CRUMPBIT_PITCH_WIN_GLIDE` and its comment |
| | 218–224 | **add** the static assert: rows 27..99 = 63 ± the table span |
| | 1501–1506, 1522 | **modify** the `crumpBit_rowSemitones()`/`crumpBit_ratioOf()` comments (±36) |
| | 1599–1610 | **modify** the `crumpBit_runPitch()` comment (whole semitones, the glide) |
| | 1632–1639 | **modify** `runPitch` locals: `target`, `window`, `w_to`, `w14_inc` |
| | 1669–1704 | **modify** `runPitch` per block: `semis = crumpBit_rowSemitones(aux)`; the glide; `dphi` on the end window (bound 7/441 turn at +36 st); `w14`/`w14_inc`; the gate on the longer window |
| | 1736–1737 | **add** `w14 += w14_inc` per frame |
| | 1782–1783, 1880–1889 | **modify** the `grn` pick: the ±36 comment; `q` held at 32,767 |
| `tools/dsp_test/Makefile` | 12, 22–23, 269, 310–314 | **modify** comments (20 breaks, CB1–CB21, the two new proofs) |
| | 343 | **add** `CRUMPBIT_PITCH_ROW_MIN/MAX` to the extracted macros |
| | 431 | **modify** break 8's pattern (`/ window` → `/ w_to`) |
| | 542–562 | **add** break 19 (glide 1.0, must fail CB21) and break 20 (no step guard, must fail CB20) |
| `tools/dsp_test/modules/crumpbit.c` | 86–89, 125–134, 151 | **modify** the header: CB13 at ±36; CB20, CB21; 20 breaks |
| | 1058–1063 | **modify** CB6: span 36, ratio[0] = 1/8, ratio[72] = 8 |
| | 1538–1543 | **add** CB11: the `pit` window stays inside the loop |
| | 1630–1654 | **modify** CB13: `aux` 75/51/63/99/27 → ratios 2, ½, 1, 8, ⅛ (no `snc`) |
| | 1726–1731 | **modify** CB14 (a): slot steps from `CRUMPBIT_SEMITONE_SPAN` |
| | 2759–2897, 2919–2920 | **add** CB20 and CB21; run them from `main()` |

The central code, as landed:

```c
/* EffectsManager.h */
static inline uint8_t effects_descriptorMin(
    const effect_param_descriptor_t *descriptor)
{
    return (descriptor &&
            descriptor->base.mod_domain.flags != INSTRUMENT_MOD_DOMAIN_NONE)
        ? (uint8_t)descriptor->base.mod_domain.min_value : 0u;
}
```

```c
/* crumpBit_runPitch(), per block */
    semis = (float)crumpBit_rowSemitones(rt->aux_raw);
    target = (target > flen - 2.0f * CRUMPBIT_PITCH_GUARD)
        ? flen - 2.0f * CRUMPBIT_PITCH_GUARD : target;
    if (!rt->primed) {
        ps->window = target;
        rt->primed = 1u;
    }
    window = (ps->window > flen - 2.0f * CRUMPBIT_PITCH_GUARD)
        ? flen - 2.0f * CRUMPBIT_PITCH_GUARD : ps->window;
    w_to = window + (target - window) * CRUMPBIT_PITCH_WIN_GLIDE;
    ps->window = w_to;
    dphi = (uint32_t)(int32_t)((1.0f - crumpBit_ratioOf(semis)) / w_to *
                               4294967296.0f);
    w14 = (uint32_t)(window * 16384.0f);
    w14_inc = (int32_t)((w_to - window) * 16384.0f * b->per_frame);
/* per frame */
        phase += dphi;
        w14 += (uint32_t)w14_inc;
```

### 11.4 Measured (repository build)

| | Before (§10) | After |
|---|---:|---:|
| `dly` / `cmb` / `grn` / `rvb` per frame | 160.0 / 153.8 / 166.5 / 163.9 | unchanged (`grn` engine 814 → 815 per block: the step guard) |
| `pit` per frame | 153.6 | **153.8** (loop 106 unchanged; per block 195 → 201) |
| Worst (`crumpbit_cpu_check`) | 166.5 | **166.5** ≤ 170 |
| `.text` / Flash used (headroom) | 709,896 / 710,316 (43,348) | **710,320 / 710,740 (42,924)**: +424 B (the 96 B larger ratio table, code) |
| `.bss`, ITCM, DTCM statics, FXBUF | 413,284; 15,280; 4,396; 126,656 B at `0x20001140` | unchanged |
| Runtime / `pit` state | 100 B / 28 B | 100 B / **32 B** (union 44 B) |
| Warnings (clean build) | 20 | 20, the same set |
| `mixer_calcNextSampleBlock` | 43 loops | 43 loops, identical counts |
| Image | 710,316 B | **710,740 B**, `Written … OK` (`build/LXRV2_lxr02.img` in the repository) |

Gates: `crumpbit_tables` OK; `crumpbit` 0 failures with CB1–CB21 and all
20 breaks caught; `crumpbit_elf_check` exit 0 (limits unchanged);
`crumpbit_cpu_check` exit 0.

Host evidence:
- CB13: `pit` at +12, −12, 0, **+36 and −36** st moves a 441 Hz sine's
  energy to 882, 220.5, 441, 3,528 and 55.1 Hz.
- CB20: 128/128 rows give `clamp(row − 63, ±36)`; the ratio exact at all 73
  semitones; the `grn` step right for every row 27..99, both directions.
- CB21: the first block starts on the target; a 60 → 90 change glides
  monotonically and settles within 0.1 % in 400 blocks; at a `win` step
  the second difference is **0.065 against 0.031 steady (2.1×; 19× before,
  law < 3×)**.

### 11.5 Hardware checks (add to HW1–HW9)

| # | Check | Expected |
|---|---|---|
| HW10 | Sweep `int` (`pit`) and P1/P2/P3 (`grn`) over the whole knob; also an LFO, a Pattern value and an FX lane on `aux` | the value runs −36..+36 and stops at both ends; each step is one semitone; what shows is what plays |
| HW11 | Sweep `win` (`pit`) slowly and quickly with sustained input | no clicks; a brief pitch bend while it moves |
| HW12 | `snp` in `pit` | shows `---`; no effect |
| HW13 | An existing Scene with `aux`/`p2`/`p3` set before this change | values inside ±36 st sound as before (one semitone per step in `grn`; in `pit` the interval now equals the shown value) |

### 11.6 Open

- **`snp` in `pit` has no job** (choice 3). Give it a new role or remove it
  from the `pit` page.
- **`cmb` `rte` above 96** holds the lowest note (20.6 Hz); unchanged.
- **Reference documents not yet updated** for this follow-up:
  - `EFFECTS_BUS_REFERENCE.md` §8.6: `aux`/`p2`/`p3` ±36 st, one semitone
    per step, stopping at 27..99; `snp` in `pit` `---`;
  - `EFFECTS_MIXER_DSP_REFERENCE.md` §4.4a: `pit` whole semitones ±36 and
    the window glide; `grn` pitches ±36; `pit` 153.8 per frame;
  - `tools/dsp_test/DSP_TEST.md`: CB13 at ±36, CB20, CB21, twenty breaks;
  - `STORAGE_SRAM_MANIFEST.md`: `pit` state 32 B (union unchanged);
  - `CPU_USE_DSP_AUDIT.md` item 65: `pit` 153.8;
  - and, with D6, the `MEMORY.md` CrumpBit binding line: Effect rows can
    carry a floor (`EFFECT_MOD_RANGE`, `effects_descriptorMin()`).
- **On hardware only:** the registry self-check (code 7, default within
  [min, max]) runs at boot; the CrumpBit defaults (63) are inside 27..99.
- §2–§10 describe P5 as scheduled; for `pit` and the image size, §11.4
  supersedes them.

---

## 12. Follow-up: a fine pitch control in `pit` (user, 2026-11-05)

Applied directly to the repository after §11. The §11.3 line numbers refer
to the files before this section's edits; the numbers below are the
current ones.

### 12.1 Request and design

**Request:** put a fine pitch control where `snp` was, "like the voice fine
pitch".

**The voice fine pitch** (`osc1_pitch_fine`, short `fin`, e.g.
DrumParameters.c:235) is a DTYPE_PM63 row, 0..127, centre 63, shown
−63..+64, Morph/LFO/Pattern. The oscillator plays it through
`midiPitch_detune()` (MidiPitch.h:192, called by `osc_recalcFreq()`):

    ratio = 1 + (fin / 127 − ½) · (2^(1/12) − 1)

That is linear in ratio and spans about **−52 to +51 cents** (not ±1
semitone, the first wording of the request); 63 sits 0.4 cent flat, exactly
as on a voice.

**Why a new row.** `snp`'s cell held `snc` (row 9), a shared on/off row
(Sync in `dly`, polarity in `cmb`, freeze in `grn`/`rvb`), so it cannot carry
a continuous value. CrumpBit gets **row 23, `fin`** (`crump_pit_fine`), a
sub-type row read by `pit` only:
- the descriptor is a copy of the voice row (DTYPE_PM63, `EFFECT_MOD_0_127`,
  default 63, maximum 127, long name "Fine", category "Pitch");
- the DSP multiplies the interval's ratio by `midiPitch_detune(fin)`, the
  voices' own function (no copy of the law);
- it takes `snp`'s place on the `pit` page (SELECT 2 screen 3: `fbk fin dpn
  ton`) **and** on `pit`'s FX lane 6, which drove `snc`, dead in `pit` since
  §11; `snc` itself is unchanged in the other sub-types;
- outside `pit` the label is `---`; the row is on no other page or lane.

**Costs:** the runtime byte `pit_fine` takes one of the two padding bytes S1
freed, so the runtime stays 100 B (asserted). Row count 23 → 24 (within
`EFFECT_PARAM_COUNT` 64, the 32-bit owner masks and the Pattern-local limit
63). `.fx`/AutoSave: a file without `crump_pit_fine` loads 63. A `pit` `.fx`
file saved before this change has its lane 6 under `lane.crump_sync`, which
no longer names a `pit` lane, so those lane steps are skipped on load (no
backward compatibility, user rule).

### 12.2 Changes by file (current line numbers)

| File | Lines | Change |
|---|---|---|
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.h` | 36, 42–43 | **modify** the row-range comment (3..23; `drv` would be 24) |
| | 46–47 | **modify** `CRUMPBIT_PARAM_COUNT` 23 → 24 with its comment |
| | 88–93 | **add** `CRUMPBIT_PARAM_PIT_FINE` (23) to the enum, with its comment |
| | 129–130, 156–157 | **modify** `CRUMPBIT_ROWS_SUBTYPE`: `fin` is a sub-type row (comment and mask) |
| `Core/DSP/Effects/CrumpBit/CrumpBitParameters.c` | 34, 91 | **modify** the descriptor comment (rows 3..23, 24 rows) |
| | 168–176 | **add** the `crump_pit_fine` row (the voice `fin` copy) and its comment |
| | 431–432, 524–528 | **add** `fin` outside `pit` → `---` (comment and code) |
| | 655–656, 693 | **modify** `crumpBit_layoutPitch` screen 3: `fbk fin dpn ton` (comment and cells) |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.h` | 198–199, 217–218 | **modify** the runtime comment (55 B used, 1 B padding; `pit_fine`) |
| | 263 | **add** `uint8_t pit_fine;` |
| `Core/DSP/Effects/CrumpBit/CrumpBitEffect.c` | 33 | **add** `#include "MidiPitch.h"` |
| | 790–792 | **add** init: `pit_fine = 63` |
| | 935–936 | **add** `write_param`: row 23 → `pit_fine` |
| | 1612–1616 | **add** the fine-pitch paragraph to the `crumpBit_runPitch()` comment |
| | 1697–1707 | **modify** `dphi`: `(1 − ratioOf(semis) · midiPitch_detune(pit_fine)) / w_to`; the bound comment (7.24/441 turn at +36 st with the fine at its top) |
| | 3186–3187, 3223 | **modify** the lane-map table comment and `pit` lane 6: `CRUMPBIT_PARAM_PIT_FINE` |
| `tools/dsp_test/Makefile` | 12, 24, 270, 315–317 | **modify** comments (21 breaks, CB1–CB22, break 21's proof) |
| | 321, 359–361 | **add** `cb_midipitch.c`: `midiPitch_detune()` and `MIDI_PITCH_SEMITONE_UP` extracted from MidiPitch.h |
| | 437–440 | **modify** break 8's pattern for the new `dphi` line |
| | 569–580 | **add** break 21 (the fine ignored, must fail CB22) |
| `tools/dsp_test/modules/crumpbit.c` | 136–141, 158 | **modify** the header: CB22; 21 breaks |
| | 206 | **add** `#include "cb_midipitch.c"` |
| | 731–733, 759, 1423 | **modify** CB4 and CB10 write `fin` too (it must change neither); CB5: `pit_fine` 63 |
| | 1283–1286, 1299–1302 | **add** CB9: `fin` is a lane of `pit` only; `pit` lane 6 is `fin` |
| | 1362–1393 | **add** `band_power()`: band energy and centre (below) |
| | CB13 (`law_cb13`) | **modify** the measurement to band energies ±25 % (below) |
| | 2956–3022, 3046 | **add** CB22 and its call |

The DSP change, as landed:

```c
    /*
     * (1 - r)/W turns per sample, r = the interval's ratio times the fine
     * detune: at most 7.24/441 of a turn (r = 8 x 1.0297, +36 st and fine
     * at its top, 10 ms), so x 2^32 fits a signed word; ...
     */
    dphi = (uint32_t)(int32_t)((1.0f - crumpBit_ratioOf(semis) *
                                           midiPitch_detune(rt->pit_fine)) /
                               w_to * 4294967296.0f);
```

### 12.3 A test finding: `pit`'s crossfade sidebands

Measuring the fine shift exposed a weakness in the pitch laws, not in the
DSP. `pit`'s two heads sit half a window apart, and their triangular
crossfade amplitude-modulates the shifted tone at the crossfade rate
|1 − r|·fs/W (15.6 Hz at +12 st, 106 Hz at +36 st with `win` 80). Depending
on the heads' relative phase the carrier partly cancels, and a sideband can
become the strongest single frequency: the peak of a +12 st tone measured
895 Hz (882 + 15.6). CB13 compared power at one frequency, and its +36 st
case (added in §11) passed only by that phase: the fine's 0.4 cent at 63
tipped it to a failure. The laws now measure bands (`band_power()`):
- **CB13:** energy within ±25 % of the expected tone against the same band
  around the input;
- **CB22:** the power-weighted band centre, whose ratio to the `fin` 63
  centre must equal `detune(fin) / detune(63)` within 0.2 % (3.5 cents; the
  fine's ends are 3 % away), and whose absolute value must be within 1 %.
  (Every centre sits the same ~4.5 Hz high because the sidebands are not
  perfectly symmetric, hence the relative law.)

CB21 (the window glide) also moved with the phase, 2.1× → 2.8× steady
against its 3× bound, so it now compares equal two-block windows against a
5× bound. Without the glide (break 19) it reads 1.54 against 0.031 (49×).

### 12.4 Measured (repository build)

| | Before (§11.4) | After |
|---|---:|---:|
| `pit` per frame | 153.8 | **154.1** (loop 106 unchanged; per block 201 → 211) |
| `dly` / `cmb` / `grn` / `rvb` | 160.0 / 153.8 / 166.5 / 163.9 | unchanged; worst 166.5 ≤ 170 |
| `.text` / Flash used (headroom) | 710,320 / 710,740 (42,924) | **710,456 / 710,876 (42,788)**: +136 B |
| `.bss`, ITCM, DTCM statics, FXBUF | unchanged since §10 | unchanged |
| Runtime | 100 B | 100 B (`pit_fine` in padding) |
| Warnings (clean build) | 20 | 20, the same set |
| `mixer_calcNextSampleBlock` | 43 loops | 43 loops, identical counts |
| Image | 710,740 B | **710,876 B**, `Written … OK` (`build/LXRV2_lxr02.img`) |

Gates: `crumpbit_tables` OK; `crumpbit` **0 failures, CB1–CB22, all 21
breaks caught**; `crumpbit_elf_check` exit 0; `crumpbit_cpu_check` exit 0.

CB22: band centre 886.51 Hz at `fin` 63; **×1.0292 at 127 (want ×1.0300)
and ×0.9701 at 0 (want ×0.9705)**, i.e. +50 and −52 cents, the voice range.

### 12.5 Hardware checks

| # | Check | Expected |
|---|---|---|
| HW14 | `fin` on the `pit` page (SELECT 2 screen 3, where `snp` was): sweep with a held tone at `int` 0 and +12 | shows −63..+64 like a voice `fin`; the pitch moves about ±½ semitone, smoothly |
| HW15 | `fin` from FX lane 6 in `pit`, an LFO, and a Pattern value | the same response; in other sub-types lane 6 still drives `snc` |
| HW16 | `fin` with the Effect page on `dly`/`cmb`/`grn`/`rvb` | not on their pages; if shown elsewhere, `---` |

### 12.6 Open

- **Range.** This matches the voice exactly (about ±½ semitone). The first
  wording asked for ±1 semitone: doubling the span would need its own law
  (`1 + (fin/127 − ½)(2^(2/12) − 1)` or a symmetric cents law), no longer
  "like the voice".
- **`snc` in `pit`** stays stored but is now on no `pit` page or lane; its
  `---` label (§11) remains for any other view.
- **Reference documents** (add to the §11.6 list): `EFFECTS_BUS_REFERENCE.md`
  §4.2a (24 rows; `pit` lane 6 `fin`) and §8.6 (the `pit` page `fbk fin dpn
  ton`; `fin` row); `EFFECTS_MIXER_DSP_REFERENCE.md` §4.4a (`pit` fine,
  the voice law); `DSP_TEST.md` (CB13 band law, CB22, 21 breaks).
