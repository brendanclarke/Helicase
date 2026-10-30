# S084 P5 — Filter Drive Type Remediation (plan)

**Status:** plan only.
**Prerequisite:** S084_P4 item 1 (the interface freeze). Selecting a drive
type other than `sft` currently freezes the interface on hardware, and that
must be root-caused before this work adds to the same render path.

## 1. Summary

- **Intent:** master plan §4.4 lists fourteen filter drive types and adds a
  drive-type control. Each type was meant to have its own character
  parameter, the "bias / harmonics / tracking" control, alongside the
  existing `drv` amount.
- **Built:** P2 built only the type selector (`filter_drive_type`, `dty`)
  and hard-coded every type's character as a fixed constant.
- **The fix:** add one continuous character row per instrument, with a
  per-type meaning. Byte 64 reproduces today's fixed setting, so existing
  kits sound the same. Model it in Python first, test it against the model,
  and hold its cost to an agreed target.

## 2. What was specified and what was built

| | Master plan §4.4 | P2-E / VX2 schedule | Firmware now |
|---|---|---|---|
| Type list | 14 types, `sft rct art thf fdc bsw tsw lsw lsq sfd afd asf chb rsb` | same | same (`DriveShaper.h`) |
| Selector | "Row: `dty`" | one row `filter_drive_type` / `dty` | one row, filter page cell 4 |
| Amount | "`drv` stays the input gain" | unchanged | unchanged |
| Per-type character control | implied by "added `dty` parameter" alongside the type list | **omitted** | **absent**; constants are fixed in code |
| Descriptor budget | +1 for `dty` | +1 | +1 (Drum 46 → 48 after P3) |

**Fixed constants that should have been the parameter** (from
`tools/plots/plot_waveshapers.py` and `Core/DSPAudio/DriveShaper.h`):

| Type | Fixed now | Where |
|---|---|---|
| `thf` | fold threshold 0.6 | `DriveShaper.h:173` |
| `asf` | symmetry offset 0.35 ("a fixed symmetry setting" in the script) | `:182` |
| `afd` | acceleration 0.05 (`u + 0.05u³`) | `:178` |
| `sfd` | no bias (symmetric sine fold) | `:176` |
| `chb` | 3rd-order Chebyshev only | `:203` |
| `bsw` | 8-bit quantise, swap the two nibbles | `:186` |
| `rct`, `art`, `fdc` | full rectify / octave down / fifth down, no blend with the clean signal | `:235`, `:245`, `:252` |
| `tsw`, `lsw`, `lsq`, `rsb` | tracker output only, no blend with the input; 20 ms envelope release | `:275–296` |
| all | one level trim per type (`driveShaper_trim[]`), valid only at the fixed setting | `DriveShaper.c` |

## 3. Why it was specified wrongly

1. **Reading:** I read "Row: `dty`" in §4.4 as the whole parameter set
   rather than as the type row plus the added control. The descriptor
   budget table in the master plan (§5, "+1 `dty`") was then written from
   that reading, and P2-E and the VX2 schedule copied it. Each later
   document repeated the error instead of checking it against the type
   list.
2. **The script's constants:** `plot_waveshapers.py` models each type at
   one fixed setting and says so ("a fixed symmetry setting", "threshold
   0.6"). Those values were treated as final constants, not as the default
   of a control. The script was used as a correctness reference
   (`--export-ref` for `test_shaper`) but never as a parameter
   specification.
3. **Missing source:** the "r3 table" that §4.4 refers to is not in the
   repository, so nothing during scheduling or review compared the build
   with the original intent.
4. **Review:** the P2 review measured cost and correctness against the
   model and did not ask what the user-facing controls were. That was a
   review scope gap of mine.

## 4. Fix design

### 4.1 The new row

- **Key:** `filter_drive_shape`.
- **Short name:** `dsh`.
- **Long name:** `DrvShape`.
- **Page:** filter page, cell 5 (next to `dty`, behind SELECT).
- **Labels:** come from the type (the D13 label hook already used by the
  Mix rows): `thr`, `sym`, `acc`, `bia`, `har`, `bit`, `mix`, `trk`.
- **Range and flags:** 0–127, `MOD_0_127`, Morphable, LFO and velocity
  targets, automatable, MIDI.
- **Missing key:** loads **64**, which reproduces today's sound.
- **Writer:** a new tag, `IM_SPECIAL_DRIVE_SHAPE` (count 28 ≤ 32).
  - It converts the byte to the type's parameter at control rate (one
    per-type mapping, no per-sample cost).
  - It stores the result in `DriveShaperState` (+4–8 B, which stays within
    the engine struct; no new RAM approval if the state stays ≤ 36 B).
  - The `dty` writer re-maps the stored byte when the type changes.
- **Descriptor counts after P5:** Drum 49, Snare 46, Cymbal 51, HiHat 51
  (cap 64). Appended, so existing indices, automation IDs, LFO tokens and
  NRPN IDs are unchanged.
- **Special tags:** `special_tags` becomes 197 rows.

### 4.2 Per-type meaning (a proposal; confirm in the model, §4.4)

| Type | Control | 0 | 64 (= today) | 127 |
|---|---|---|---|---|
| `sft` | none, so the S0 path is kept (label `---`) | — | — | — |
| `rct` | rectify blend (clean → rectified) | clean soft clip | full rectify | full rectify + a harder knee |
| `art` | sub-octave blend | clean | today | sub only |
| `thf` | fold threshold | 0.95 (rare folds) | 0.6 | 0.2 (dense folds) |
| `fdc` | fifth-down blend / crunch level | clean | today | crunch only |
| `bsw` | bits before the swap | 8 (today) at 0–64 | 8 | 4 (harsher) |
| `tsw`, `lsw`, `lsq` | tracker blend with the input | input | tracker only (today) | tracker + faster envelope release (5 ms) |
| `sfd` | fold bias (even harmonics) | −0.35 | 0 | +0.35 |
| `afd` | acceleration | 0 | 0.05 | 0.25 |
| `asf` | symmetry | 0 | 0.35 | 0.7 |
| `chb` | 3rd-harmonic blend (fundamental → T3) | fundamental | T3 (today) | T3 + T2 (even) |
| `rsb` | ring blend | sub only | today | ring only |

Byte 64 must reproduce today's output bit-for-bit where possible. This is
checked against the frozen P2 reference arrays.

### 4.3 Level trims

- **The problem:** today there is one trim per type, valid at the fixed
  setting. With the control, a fixed trim lets the level move with the
  character.
- **The proposal:** a 5-point trim table per type (bytes 0/32/64/96/127),
  generated by the model with the existing rule (a sine at drv 64 has
  `sft`'s RMS; the drv-127 peak stays within 2.0). The writer interpolates
  the trim at control rate, so the per-sample cost does not change.

### 4.4 Python first

- **Model:** extend `tools/plots/plot_waveshapers.py` with the character
  control. Each type function takes `p` (0–1); the static and stateful
  figures add a sweep panel (curves at 0/32/64/96/127).
- **Trims:** a trim table is printed and exported.
- **References:** `--export-ref` writes reference arrays at the five
  points.
- **Review:** you look at the plots and confirm the meanings before any C
  is written. This is the step that was skipped.

### 4.5 Firmware

- **Shaper:** `DriveShaper.h` takes `p` from the state (block constant) in
  each `g(u)`. `driveShaper_apply()` keeps its signature;
  `driveShaper_trim[]` becomes a per-type, per-point table.
- **Filter:** `SVF_calcBlockZDFShaped` needs no change beyond the state;
  the `sft` path stays untouched (S0).
- **Writers and rows:** the writer and the row are added in all four
  engines' parameter tables, with the classifier key `filter_drive_shape`.
- **Menu:** the filter page gets cell 5, and the label hook gains the
  per-type labels (`InstrumentCellFormat`).

### 4.6 Cost

- **Where it stands:** P2 already exceeds its own target. `sft` is 100
  instructions/sample; the shaped types measure 107 (`chb`) to 134
  (`lsw`), against the target ≤ `sft` + 6 (P2 §17.4, still open). The
  control adds about 1–3 instructions per type, a block-constant multiply
  or blend.
- **Decision for you:** either
  - (a) re-target to "measured P2 cost + 3" and accept it, or
  - (b) first recover cost in the shaped path (one unswitched loop per
    type instead of the per-sample type switch; P2 measured both shapes,
    and the branched crossings were kept), then add the control.

  The P4 item 2 cycle measurement on the target should drive this choice.

## 5. Verification

- **Host:**
  - `test_shaper` compares every type at the five control points against
    the model's references.
  - Byte 64 against the frozen P2 references (S0, or ≤ 1 LSB where the
    blend arithmetic cannot be exact).
  - `special_tags` reports 197.
  - `armcheck-vx2` probes extended with the control.
- **Hardware:**
  - each type swept across `dsh` by ear;
  - old kits load and sound unchanged;
  - LFO and Morph on `dsh` without zipper (block-rate control: check);
  - worst-case Scene CPU (P4 item 2).

## 6. Questions for you

1. **Naming:** `filter_drive_shape` / `dsh` / `DrvShape`, or another name?
   Should `dty` keep its short name?
2. **Meanings:** are the per-type meanings in §4.2 what you meant by
   "bias / harmonics / tracking etc."? Any type you want controlled
   differently?
3. **`sft`:** keep it fixed (S0 preserved), or give it a bias control (even
   harmonics; this costs the S0 path)?
4. **Modulation:** should the row be Morphable and LFO-modulatable (as
   proposed), or automatable only?
5. **Cost:** re-target (§4.6 a) or optimise first (§4.6 b)?
