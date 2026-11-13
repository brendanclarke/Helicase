# S095 P4 — FX bus tweaks (plan)

- **Status:** plan, written 2026-11-13 from the tree at `4602b3b3`. Nothing
  changed in the source. Every question is answered (user, 2026-11-13,
  §5); the next step is the schedule.
- **Items:**
  - T1: the Space types (Delay, Reverb, Echo) lose `mix`;
  - T2: which CrumpBit and Space rows show plain numbers (decided); the
    `hed` / `frz` / `rtg` stepped-row fix;
  - T3: the dual filter (`flt`, StereoFilter) is removed;
  - T4: compressor depth and the gain staging that feeds it.

---

## 0. Summary

| Item | Recommendation | Per-sample CPU | RAM | Flash |
|---|---|---|---|---|
| T1 | Remove `mix` from all three Space types; wet is always 100 %; keep the dry ramp only for the type-change handoff (target 0) | 0 | 0 | ≈ −0.1 KB |
| T2 | 17 rows go to plain 0..127 / ±63 with their laws unchanged (§2.1); Delay/Echo `fbk` unity moves from row 106 to 100, as CrumpBit's (§2.6); CrumpBit `rvb` damping uses the whole 0..127 (§2.7). `hed`, `frz`, `rtg` back to true stepped rows (0..6, 0..2), one stop per detent | 0 | 0 | small − |
| T3 | Delete StereoFilter and its float filter twin; renumber the type ids | 0 (it was never the dearest type) | 0 | ≈ −3.7 KB |
| T4 | C1 (deeper threshold curve, lower makeup reference) + C2 (filter drive floor 0.4 → 1.0) + C3 (half makeup on the voice distortion) + C5 (live gain-reduction readout). Not C4 (louder `vol`). Decided; tuned by ear on the device | 0 | 0 (C3 reuses an unused field) | small + |

---

## 1. T1 — the Space types lose `mix`

### 1.1 Why `mix` is wrong here

- The Effect is on a **send bus**: each voice's dry signal already reaches
  the outputs through the mixer, and a pre-volume copy goes to the bus
  (`MIXER_SYSTEM_DSP_REFERENCE.md` §3.3).
- The Space types add the bus input back to their output as
  `out = in × (1 − mix) + wet × mix` (`SpaceEffect.c:1587`, `:1699`). At the
  default `mix` 40 that is 0.69 × the dry bus on top of the mixer's own dry
  path: the voices come out louder when the Delay is selected, and the wet
  is only 0.31.
- CrumpBit is different: its "dry" is the 8-bit ADC signal after
  `bof`/`biv`, and `mix` crossfades that against the sub-engine. It keeps
  `mix`.

### 1.2 What changes

| Where | Change |
|---|---|
| `SpaceParameters.c:54`, `:94`, `:146` | Delete the `dly_mix`, `rvb_mix`, `ech_mix` rows |
| `SpaceParameters.h` | Delete `SPACE_DLY_MIX`, `SPACE_RVB_MIX`, `SPACE_ECH_MIX`; every later row moves down one index; the three counts drop by one (14, 20, 16) |
| `SpaceParameters.c:217–237` | The three text-kind tables lose their first entry |
| `SpaceEffect.c:1094` | `space_laws[][]` loses `SPACE_LAW_MIX` in each row; the law and `space_tapeK_t.mix` go |
| `SpaceEffect.c:1587–1594`, `:1699–1700`, `:1777–1782` | Wet gain = `1 × (1 − duck × dck)` (× `env_g` on the Reverb); dry target 0 |
| `SpaceEffect.c:1451`, `:1476` | The export's dry becomes 0 |
| `SpaceEffect.c:1653`, `:1782`, `:1879` | `sk_feedMix()` takes 1.0, 1.0 (the incoming tail is heard at full wet) |
| `EffectsManager.c` Space lane rows | Lane 1 (was `mix`) is freed; the lanes are re-listed (§1.4) |
| `tools/dsp_test/modules/space.c:157–161`, `:254–255`, `:307–311` | The host row tables `sp_dlyRows`, `sp_rvbRows`, `sp_echRows` lose their first entry (`mix` 127, which the laws use for full wet); counts 12 → 11, 18 → 17, 14 → 13 |

### 1.3 The dry path: keep it for the handoff (recommended)

The output loops add `in × dry`. After a type change `dry` ramps from the
outgoing type's export (CrumpBit at `mix` 0 hands over 1.0) to the new
type's target.

| | What | Cost | Sound |
|---|---|---|---|
| **D-a (rec.)** | Keep the term; its target is 0 | No change | A CrumpBit → Delay switch still fades CrumpBit's dry out over ~10 blocks |
| D-b | Remove the term from the three output loops | About −2 to −4 instructions per frame | The outgoing type's dry stops in one block (a step when CrumpBit was mostly dry) |

The CPU rule counts the worst case; D-b's saving is small, and it would go
to the Effect's share, which the Reverb already nearly fills (189.7 of the
191.8 cap, `baselines/s095p1/space_cpu_phaseBCD.txt`).

### 1.4 Consequences

- **Level:** at the defaults the wet rises from 0.31 to 1.0 (+10 dB). The
  common `vol` row defaults to 127 for every type
  (`EFFECT_COMMON_DEFAULT_LEVEL`, `EffectTypes.h:72`). **Decided (Q1):**
  it stays 127. An Effect at `vol` 127 is meant to sit level with a dry
  output, so a fully wet FX output can stand in for an instrument output.
- **Renumbering.** The Space row indices are what Pattern automation
  (448 + index), LFO targets, FX lanes and AutoSave store:
  - AutoSave stores the Effect parameters by index
    (`Autosave.c:1693–1702`), so a Space Scene in the AutoSave plays every
    row one place off after the update;
  - stored Pattern automation and LFO targets on Space rows point one row
    later;
  - `.fx` files store keys: a `*_mix` key is skipped on load (unknown keys
    are ignored, `storageTypes.c:1468`);
  - MIDI Learn stores keys: unaffected.
  No migration (standing rule). Space Scenes made since S095 P1 need a
  reset or a re-save.
- **Page:** the default layout puts rows 3.. on SELECT 2 onwards
  (`menuEffects.c`), so every cell moves one place left. Delay becomes
  `tim spr fbk xfb | ton deg wow hld | rev hed dck`. **Decided (Q2):**
  the default layout.

---

## 2. T2 — value text (decided)

### 2.1 Decisions (user, 2026-11-13)

- The rows below show the plain dtype text instead of a value hook: 0..127
  (`DTYPE_0B127`) or ±63 (`DTYPE_PM63`, stored 63 shows 0).
- **Every law stays as it is**, except the Delay/Echo `fbk` unity point
  (§2.6). For the rest only the page text changes, so nothing is stored
  differently and nothing sounds different.
- `hed`, `frz` and `rtg` get the stepped-row fix, `hed` with letter labels
  (§2.5).
- `ton` follows one convention: low-pass only is 0..127 (127 flat);
  low-pass/high-pass tilt is bipolar ±63 (§2.7).

| Type | Plain from P4 | Text kept |
|---|---|---|
| CrumpBit | `fbk` in `grn` (0..127); `aux`, `p2`, `p3` (±63) | `rte` (all sub-types), `den`, `spn` (`pos`) in `grn`, `sub`, `typ`, the `---` markers, `ton` in `rvb` above 63 |
| Delay | `spr` (±63), `fbk`, `xfb`, `dck` (0..127) | `tim`; `hed` (stepped, §2.5) |
| Reverb | `pre`, `dcy`, `dtn`, `dck` (0..127); `int`, `env` (±63) | `frz` (stepped, §2.5) |
| Echo | `fbk`, `dcy`, `dck` (0..127) | `tim`; `hed`, `rtg` (stepped, §2.5) |

### 2.2 Points checked against the tree

- **The pitch rows look the same.** `aux`, `p2`, `p3` (stored 27..99) and
  `int` (39..87) already showed value − 63, which is what ±63 shows, and
  their rows stop at ±36 / ±24, so the hook's clamp never acted. What goes
  is the code, not anything you see.
- **CrumpBit `aux` keeps its `---`.** In `dly`, `cmb` and `rvb` the row
  is not read, and it shows `---` there like `snc` and `fin` do. That
  marker is the inactive-row rule, not a value text, so it stays. Only
  `crumpBit_formatSemitones()` (`CrumpBitParameters.c:486–500`) and its
  two call sites go (`:601–611`: `aux` in `pit`/`grn` returns 0, the
  `p2`/`p3` branch is deleted).
- **`env` 63 shows 0 instead of `off`.** That is the only visible change
  there.
- **`fbk` unity stays unmarked** (user). With §2.6 it is row 100 on every
  type whose feedback goes above unity.
- **`pre` is buffer-limited (`BUFFER_DEPENDENT`).** With the whole arena,
  the predelay line holds 1.37 s, so the limit is row 125
  (`space_preLimit()`). Page edits already stop at the limit
  (`effects_paramEffectiveMax()`, `menuEffects.c:461`), so the plain
  number is what plays. Only an LFO or Pattern value above it plays
  clamped, and the cell shows the stored value. Today's text showed the
  clamped value; `tim` keeps that text.

### 2.3 What changes in the code

| Where | Change |
|---|---|
| `SpaceParameters.c:217–237` | The text-kind tables: every plain row becomes `SPACE_TXT_NONE`. Left: `tim` (`TIME`), `hed` (`HEADS`), `frz` (`FREEZE`), `rtg` (`ROUTE`) |
| `SpaceParameters.c` `space_formatValue()` | The `RATIO`, `FBK`, `PCT`, `PRE`, `DECAY`, `SIGNED`, `CENTS`, `ENV` cases and `space_tenths()` go; their enum entries go |
| `SpaceEffect.c` | No change: `space_sprRatio()`, `space_fbkGain()`, `space_rt60()` and `space_preLimit()` are still the laws (and the `pre` clamp) |
| `CrumpBitParameters.c` `crumpBit_uiFormatValue3()` | The `grn` `fbk` percent branch (`:663–667`) and the `aux`/`p2`/`p3` semitone branches (`:601–611`) go, with `crumpBit_formatSemitones()`; `aux`'s `---` stays |
| Descriptors | No change: the dtypes are already `DTYPE_0B127` / `DTYPE_PM63` |

Flash: a little less (the formatter cases). CPU: none (foreground text
only). Stored data: none.

### 2.4 Reference: the rows before P4

Kept for the record; the plain decisions are in §2.1.

**CrumpBit (`cbt`, 24 rows).**

| # | Row | Stored | Shown before P4 | Law |
|---:|---|---|---|---|
| 3 | `bof` | 0..255 | bit row `- 0 !` on the overlay; 0..255 in a cell | mask |
| 4 | `biv` | 0..255 | as `bof` | mask |
| 5 | `mix` | 0..127 | 0..127 | linear |
| 6 | `fbk` | 0..127 | 0..127; `grn`: echo gain % 0..98 | 0..1.0 at 100, to 1.2 at 127; `grn`: × 0.98/127; `rvb` (`dcy`): RT60 0.1..10 s table |
| 7 | `rte` | 0..127, clamped to the share | `dly`/`grn`: stop name or row; `cmb`: note; `pit`: ms; `rvb`: 0..127 (clamped) | per sub-type tables |
| 8 | `sub` | 0..4 | `dly cmb pit grn rvb` | selector |
| 9 | `snc` | 0..1 | on/off; `---` in `dly`/`pit` | switch |
| 10 | `spn` | 0..127 | ±63; `grn` (`pos`) above 63: stop name | balance; `grn`: position |
| 11 | `ton` | 0..127 | ±63; `rvb` (`dmp`) above 63: 0 | tilt table |
| 12 | `typ` | 0..7 | `LP HP BP UBP Nch Pek LP2 off` | selector |
| 13 | `frq` | 0..127 | 0..127 | voice cutoff table |
| 14 | `res` | 0..127 | 0..127 | R = 1 − res/127, floor 0.0625 |
| 15 | `aux` | 27..99 | semitones in `pit`/`grn`; `---` elsewhere | semitone ratio table |
| 16 | `p2` | 27..99 | semitones | semitone ratio table |
| 17 | `p3` | 27..99 | semitones | semitone ratio table |
| 18 | `spr` | 0..127 | 0..127 | linear |
| 19 | `den` | 0..127 | ±63 below 63; `off`; `buf`; stop name above | random-rate table below, stops above |
| 20 | `lng` | 0..127 | ±63 | 2^(−8(1 − m/64)), reversed below 63 |
| 21 | `wid` | 0..127 | 0..127 | linear |
| 22 | `bal` | 0..127 | ±63 | ± cutoff index |
| 23 | `fin` | 0..127 | ±63; `---` outside `pit` | voice `fin` cents law |

**Delay (`dly`).** Row numbers are before T1 (after T1 each is one lower).

| # | Row | Stored | Shown before P4 | Law |
|---:|---|---|---|---|
| 3 | `mix` | 0..127 | 0..127 | linear (removed, T1) |
| 4 | `tim` | 0..127, clamped to the tape | stop name (/64../2) or row; the clamped row | CrumpBit's `dly` delay-time table at the tempo |
| 5 | `spr` | 0..127 | R/L ratio `0.5`..`2.0` | 2^((v − 63)/63), max 2 |
| 6 | `fbk` | 0..127 | loop gain % 0..120 | linear to 1.0 at 106, then to 1.2 |
| 7 | `xfb` | 0..127 | 0..100 % | 0.5 × v/127 |
| 8 | `ton` | 0..127 | ±63 | LP 200 Hz..18 kHz below 63, HP 20 Hz..2 kHz above |
| 9 | `deg` | 0..127 | 0..127 | drive ×1..×6, bandwidth 16 → 4 kHz, hiss −72..−48 dBFS |
| 10 | `wow` | 0..127 | 0..127 | linear depth |
| 11 | `hld` | 0..1 | on/off | switch |
| 12 | `rev` | 0..1 | on/off | switch |
| 13 | `hed` | 0..127 in 7 zones | head set `1 2 3 12 13 23 123` | zones (§2.5) |
| 14 | `dck` | 0..127 | 0..100 % | linear |

**Reverb (`rvb`).**

| # | Row | Stored | Shown before P4 | Law |
|---:|---|---|---|---|
| 3 | `mix` | 0..127 | 0..127 | linear (removed, T1) |
| 4 | `pre` | 0..127, clamped to the share | ms to 999, then s | (v/127)² × 1.4 s |
| 5 | `siz` | 0..127 | 0..127 | 0.35 + 0.65 × v/127 |
| 6 | `dcy` | 0..127 | RT60 s, 0.3..20 | 0.3 × 2^(6.06 v/127) |
| 7 | `dif` | 0..127 | 0..127 | 0.5 + 0.25 × v/127 |
| 8 | `lsh` | 0..127 | ±63 | low-band decay ×0.5..×2 |
| 9 | `lsf` | 0..127 | 0..127 | 100..600 Hz |
| 10 | `hsh` | 0..127 | 0..127 | high-band damping ×1..×0.1 |
| 11 | `hsf` | 0..127 | 0..127 | 1.5..9 kHz |
| 12 | `mod` | 0..127 | 0..127 | linear, 0..7 samples |
| 13 | `wid` | 0..127 | 0..127 | v/63 (0..2) |
| 14 | `frz` | 0..127 in 3 zones | `off on shm` | zones |
| 15 | `shm` | 0..127 | 0..127 | 0.7 × v/127 |
| 16 | `int` | 39..87 | semitones −24..+24 | 2^(st/12) |
| 17 | `dtn` | 0..127 | cents 0..50 | ± cents |
| 18 | `env` | 0..127 | `off` at 63, else ±63 | bloom 20..800 ms below, gate 30..600 ms above |
| 19 | `dck` | 0..127 | 0..100 % | linear |
| 20 | `ton` | 0..127 | 0..127 | one-pole LP 300 Hz..flat |

**Echo (`ech`).**

| # | Row | Stored | Shown before P4 | Law |
|---:|---|---|---|---|
| 3 | `mix` | 0..127 | 0..127 | linear (removed, T1) |
| 4 | `tim` | as Delay | as Delay | as Delay |
| 5 | `fbk` | 0..127 | 0..120 % | as Delay |
| 6 | `ton` | 0..127 | ±63 | as Delay |
| 7 | `deg` | 0..127 | 0..127 | as Delay |
| 8 | `wow` | 0..127 | 0..127 | as Delay |
| 9 | `e>r` | 0..127 | 0..127 | linear |
| 10 | `bln` | 0..127 | 0..127 | linear |
| 11 | `dcy` | 0..127 | RT60 s | as Reverb |
| 12 | `lsh` | 0..127 | ±63 | as Reverb |
| 13 | `hsh` | 0..127 | 0..127 | as Reverb |
| 14 | `hed` | 0..127 in 7 zones | head set | zones |
| 15 | `dck` | 0..127 | 0..100 % | linear |
| 16 | `rtg` | 0..127 in 3 zones | `e>r r>e lp` | zones |

Two inconsistencies:

- `fbk` reached unity at row **100** on CrumpBit and row **106** on the
  Space tape types: settled by §2.6.
- still open (Q5): `ton` on CrumpBit and the Space tape types is ±63 (tilt); `ton` on the
  Reverb is 0..127 with 127 = flat.

### 2.5 The `hed`, `frz` and `rtg` fix (decided)

**What the code does:**

- `hed` stores 0..127 and plays one of seven head sets by zone:
  stop = value × 7 / 128 (`space_stop()`, `SpaceEffect.h:123`). The DSP
  (`SpaceEffect.c:1185`) and the label (`SpaceParameters.c:302`) use the same
  zone, so they always agree with each other.
- Each stop covers about 18 knob detents. The value text is the head set,
  `1 2 3 12 13 23 123`. These look like numbers but are names (D/3, 2D/3, D
  and their sums).
- So turning the knob from 0 to 127 shows "1" for 18 detents, then "2",
  then "3", then "12" … The number on the screen has nothing to do with
  the knob's position.
- S095 P1 §S18 moved `hed` from 0..6 to zones, citing two causes: the
  0..6 range ("most of the travel clamped at 6") and labels that were not
  monotone (3 2 1 13 23 12 123). On the endless pots a knob edit is
  value + delta, clamped to the row's maximum (`menu.c:17695–17701`), so a
  0..6 row steps one stop per detent and rests at 6, as CrumpBit's `sub`
  and `typ` do. The reordered labels are what made it read correctly.

**The fix:** `hed`, `frz` and `rtg` become true stepped rows, the way
CrumpBit's `sub` (0..4) and `typ` (0..7) work:

- stored 0..6 (`frz`, `rtg` 0..2), one stop per detent, the labels in the
  current rising order;
- the Pattern value, the FX lane value and the Morph endpoint are the stop
  itself (Morph between two stops steps through the ones between, as it
  does now and as `typ` does);
- `hed`'s labels become letters, one per head, in the same rising order:
  `  A` (D/3), `  B` (2D/3), `  C` (D), ` AB`, ` AC`, ` BC`, `ABC`. Letters
  cannot be read as a number or a knob position (Q3). `frz` (`off on shm`)
  and `rtg` (`e>r r>e lp`) keep their names (Q3a).

| Where | Change |
|---|---|
| `SpaceParameters.c:79–82`, `:170–172` (`dly_heads`, `ech_heads`) | max 127 → 6; default `SPACE_HEADS_DEFAULT` 46 → 2 (`  C`, D only) |
| `SpaceParameters.c` `rvb_freeze`, `ech_routing` | max 127 → 2; default 0 |
| `SpaceParameters.h` | `SPACE_HEADS_DEFAULT` 2; the zone comment goes |
| `SpaceParameters.c:246–248` | `space_hedNames`: `  A`, `  B`, `  C`, ` AB`, ` AC`, ` BC`, `ABC` |
| `SpaceParameters.c:302`, `:331`, `:334` | The label index is the value (clamped to the last stop), not `space_stop()` |
| `SpaceEffect.c:1192`, `:1205`, `:1227–1229` | The same in the laws |
| `SpaceEffect.h:123` | `space_stop()` has no caller left in the firmware: delete |
| `tools/dsp_test/modules/space.c:398–428` (SP6) | Walk `hed` 0..6 instead of 0..127; the stop is the value; the default check becomes `SPACE_HEADS_DEFAULT == 2` |

**Stored data (no migration):** a stored value above the new maximum is
clamped to the last stop when it resolves. An existing `hed` at the old
default 46 plays `ABC` (all three heads), a `frz` or `rtg` above 2 plays
`shm` / `lp`. Space Scenes made since S095 P1 need these three rows set
again (they need a reset for T1 anyway, §1.4).

### 2.6 Feedback unity at row 100 (user, 2026-11-13)

Unity stays unmarked on the page. Every feedback row that goes above unity
reaches it at row 100, then rises to the same top as now (×1.2 at 127):

| Type, row | Above unity? | Before | After |
|---|---|---|---|
| CrumpBit `fbk` in `dly`, `cmb`, `pit` | yes | unity at 100, ×1.2 at 127 | unchanged |
| CrumpBit `fbk` in `grn` | no (×0.98 at 127) | — | unchanged |
| CrumpBit `fbk` in `rvb` (`dcy`) | no (decay time) | — | unchanged |
| Delay `fbk`, Echo `fbk` | yes | unity at **106**, ×1.2 at 127 | unity at **100**, ×1.2 at 127 |

| Where | Change |
|---|---|
| `SpaceEffect.c:200–205` `space_fbkGain()` | `row ≤ 100: row / 100`; above: `1 + (row − 100) × 0.2 / 27`, CrumpBit's law (`CRUMPBIT_FEEDBACK_UNITY_ROW` 100, `CRUMPBIT_FEEDBACK_ABOVE_STEP` 0.2/27, `CrumpBitEffect.c:104–105`) |
| `SpaceEffect.h:118`, `SpaceParameters.h:35` | The comments: unity at 100 |
| Host | No law pins 106; SP2 runs at `fbk` 127, whose gain (×1.2) does not change |

Stored values keep their bytes and play slightly more feedback: row 64
goes from ×0.60 to ×0.64, and rows 101–106 (below unity before) now run
above unity into the tape's soft clip. No migration.

### 2.7 The `ton` convention (user, 2026-11-13)

A `ton` that is low-pass only is 0..127 with 127 flat; a `ton` that tilts
both ways (low-pass one side, high-pass the other) is bipolar, ±63 with 63
flat.

| Type, row | Kind | Today | After |
|---|---|---|---|
| CrumpBit `ton` in `dly`, `cmb`, `pit` | LP/HP tilt | ±63 | unchanged |
| Delay `ton`, Echo `ton` | LP/HP tilt | ±63 | unchanged |
| Reverb `ton` | LP only | 0..127, 127 flat | unchanged |
| CrumpBit `ton` in `rvb` (`dmp`) | LP only | ±63; 0..63 sweeps the low-pass, **64..127 all play flat** and show 0 | **0..127, 127 flat** |
| CrumpBit `ton` in `grn` | not read | — | unchanged |

**The one change: CrumpBit `rvb` damping.** It shares the bipolar `ton` row
with the tilt sub-types (a sub-type change keeps the row's value), so the
row stays `DTYPE_PM63`; in `rvb` only:

- **law:** the damping pole reads `crumpBit_toneK[ton >> 1]` instead of
  `crumpBit_toneK[min(ton, 63)]` (`CrumpBitEffect.c:2702–2703`). 0 is the
  lowest corner (150 Hz), 126 and 127 are exactly flat (`toneK[63]` = 1),
  64 steps across the whole knob. Per block, no per-sample change;
- **page:** `crumpBit_uiFormatValue3()` shows the plain 0..127 value in
  `rvb` (`numtostrpu`) in place of the "above 63 shows 0" branch
  (`CrumpBitParameters.c:657–661`);
- **stored values:** the row's default (63, flat for the tilt sub-types)
  now plays mid damping (≈ `toneK[31]`) in `rvb`, and a `dly` Scene
  switched to `rvb` is darker than before. No migration;
- **host:** CB15(d) (`tools/dsp_test/modules/crumpbit.c:2473–2515`) tested
  "every `ton` above 63 equals 63". It becomes: `ton` 126 and 127 are
  bit-identical to the old flat run, and `ton` v plays as the old law at
  v >> 1.

---

## 3. T3 — remove the dual filter (`flt`)

### 3.1 What goes

| Where | Change |
|---|---|
| `Core/DSP/Effects/StereoFilter/` | Delete the four files |
| `Makefile:42`, `:123`, `:174`, `:285–290` | Include path, two sources, the `-Ofast` rule |
| `EffectsManager.h:72–77` | Ids renumbered: CrumpBit 1, Delay 2, Reverb 3, Echo 4, count 5. The ids only index the registry; tokens are the stored identity |
| `EffectsManager.c` | The include, the union member `stereo_filter`, the registry row, its size assert |
| `ResonantFilter.c:384–513`, `.h:121–134` | `SVF_calcBlockZDFFloat()` has no other caller: delete. Comments that name StereoFilter (`ResonantFilter.c:286`, `:440`) |
| Comments | `FxBuffer.c:425`, `SpaceKit.h:352`, `:379`, `CrumpBitParameters.h:200`, `config.h:252` (`DEV_EFFECT_FORCE_TYPE`: "Id 1 is StereoFilter") |

`tools/dsp_test/spike/` holds frozen copies of `ResonantFilter`; they stay.

### 3.2 Resources

- **Flash:** about −3.7 KB (`SVF_calcBlockZDFFloat` 3,076 B, StereoFilter's
  functions and tables about 650 B, from the current ELF).
- **RAM:** none; the runtime union is sized by CrumpBit (100 B).
- **CPU:** none: the Effect share is set by the dearest type (CrumpBit
  `grn` / the Space types), never by StereoFilter.
- **ITCM:** none; the float filter is placed in flash (`INITCM_EFFECT_NOINLINE`
  is empty, `config.h:1030`).

### 3.3 Stored data (no migration)

- `.fx` with `type=flt`: the parser returns `BAD_TYPE` and the Scene load
  ends as an invalid Scene (`filesystem.c:14449–14458`). Three files on
  the test card:
  - `SD_CARD/Bank/000 FluxTime/11 Eris/effects.fx`
  - `SD_CARD/Bank/002 FluxTim2/11 Eris/effects.fx`
  - `SD_CARD/Bank/027 NoBankOk/11 Eris/effects.fx`

  Change them to `type=off` (or delete the `[params]` lines too) before the
  card is used with the new image.
- AutoSave with `flt`: the Scene's Effect loads as `off`
  (`Autosave.c:1693–1695`).
- MIDI Learn rows on `flt`: the row is rejected on load
  (`MidiLearn.c:427`).

---

## 4. T4 — compressor depth and gain staging

### 4.1 Where the level goes today

A clean voice (no drive) reaches the bus well below the threshold for
most `cam` settings. Distortion lifts it into range.

**Voice side (Drum, Snare, Cymbal, HiHat):**

| Stage | Law | Effect on level |
|---|---|---|
| Oscillator | full scale int16 (`DrumVoice.c` comment, `voiceFold_process` gains 1/1) | 0 dB |
| Filter drive | `0.4 + 6 × (drv/127)²` into the filter (`ResonantFilter.c:150`) | **−8 dB at `drv` 0**, −2 dB at 32, +5.7 dB at 64, +16 dB at 127 |
| Filter output | LP: `fastTanh(y1)`; others × 0.883; type `off` passes the input unchanged | `off` is **8 dB louder** than LP at `drv` 0 |
| Distortion | `(1+k)x / (1+k|x|)`, `k = 2s/(1−s)`, s = `drv`/128 (`distortion.h:72`) | small-signal +2.2 dB (16), +9.5 dB (64), +17 dB (96), +48 dB (127); **no makeup** |
| Channel `vol` | value / 127, default 0.8 (`DrumVoice.c:105`) | −1.9 dB |
| Slider | 30 dB log taper (`config.h:809`) | 0 dB at the top |
| Mixer | float gain × 256 into int32 (`mixer.c:452`) | nothing clips until the 24-bit pack |

Alkaline and Colloid already have makeup on their drives
(`AlkalinePostTables`, `COL_MAKEUP`).

**Model** (55 Hz kick, 120 ms decay, LP at `drv` 0, `vol` 0.8, slider up):
peak ≈ −10.5 dBFS, 5 ms RMS ≈ −12 dBFS. Through the voice distortion
(same input):

| `drv` (distortion) | 0 | 32 | 64 | 96 | 127 |
|---|---:|---:|---:|---:|---:|
| 5 ms RMS, dBFS | −11.8 | −8.8 | −5.9 | −3.1 | −0.1 |
| 300 ms RMS, dBFS | −20.5 | −17.0 | −13.4 | −9.1 | −1.0 |

**Compressor side** (`BusCompressor.c:263–267`): threshold
`T = −3 − 27a`, ratio `1 + 3a + 4a³`, a 12 dB knee (it starts 6 dB below
T), makeup `−GR(−12 dBFS) × (1 − 0.3a)`, detector = 5 ms RMS. Gain
reduction at a few detector levels:

| `cam` | T | Ratio | Makeup | at −10 | at −14 | at −18 | at −22 |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 32 | −9.8 | 1.8 | +0.3 | −0.6 | −0.1 | 0 | 0 |
| 48 | −13.2 | 2.4 | +1.1 | −2.0 | −0.6 | 0 | 0 |
| 64 | −16.6 | 3.0 | +2.7 | −4.4 | −2.1 | −0.6 | 0 |
| 96 | −23.4 | 5.0 | +7.1 | −10.7 | −7.5 | −4.3 | −1.8 |
| 127 | −30.0 | 8.0 | +11.0 | −17.5 | −14.0 | −10.5 | −7.0 |

So the clean kick at the default `cam` 48 gets about 1 dB of reduction,
and the makeup is larger than the reduction; at `drv` 64 its 5 ms RMS rises
6 dB and the reduction roughly triples. This matches what you hear.

### 4.2 Options

All five are per-block or per-row constants. None adds per-sample work.

**C1 — a deeper threshold curve (recommended).**
`T = −8 − 32a` (−8..−40 dBFS), makeup reference −20 dBFS instead of −12;
ratio and drive unchanged. Two constants and one formula in
`busComp_processBlock()`.

| `cam` | T | Makeup | at −10 | at −14 | at −18 | at −22 |
|---:|---:|---:|---:|---:|---:|---:|
| 32 | −16.1 | +0.1 | −2.7 | −1.2 | −0.3 | 0 |
| 48 | −20.1 | +0.8 | −5.8 | −3.5 | −1.6 | −0.4 |
| 64 | −24.1 | +2.4 | −9.5 | −6.8 | −4.1 | −1.8 |
| 96 | −32.2 | +7.5 | −17.7 | −14.5 | −11.3 | −8.1 |
| 127 | −40.0 | +12.2 | −26.2 | −22.8 | −19.2 | −15.8 |

The top end is extreme on purpose (`cam` 127 as a squash). The exact
numbers are for your ear; the shape is the point.

**C2 — the filter drive floor (recommended).**
`SVF_setDrive()`: `0.4 + 6x²` → `1.0 + 5.4x²` (same top, ×6.4).

- every filtered Drum/Snare/Cymbal/HiHat is +8 dB at `drv` 0, less above;
- the 8 dB jump between filter `off` and LP goes away;
- the filter's Padé saturator now sees a full-scale oscillator at 1.0, so
  a little soft saturation is always there on full-scale material. A
  middle ground is `0.7 + 5.7x²` (+4.9 dB at 0).

**C3 — makeup on the voice distortion (recommended at half).**
Fold a makeup factor `m(drv)` into the curve's numerator, computed once
per row change in `setDistortionShape()`. `Distortion.inv_shape` is
already in the struct and unused anywhere (`distortion.h:47`), so no RAM.
The fused loops (`voicePostChain.h`) keep the same operation count if the
curve becomes `x × A / (1 + k|x|)` with `A = m(1+k)` precomputed; that has
to pass the loop gate (`elf_hotloops.py` and the inlined-set diff).

| `drv` | 16 | 32 | 48 | 64 | 80 | 96 | 112 | 127 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Full makeup, dB | −1.9 | −3.7 | −5.6 | −7.6 | −9.8 | −12.4 | −15.8 | −22.4 |
| Half makeup, dB | −0.9 | −1.9 | −2.8 | −3.8 | −4.9 | −6.2 | −7.9 | −11.2 |

(Matched on the 300 ms RMS of the kick and a snare model at −10 dBFS
input; it depends on the input level, so it is a starting table, not an
identity.) Full makeup makes the distortion a tone control; half keeps
some of the push you are used to.

**C4 — a louder channel `vol` (not recommended alone).**
Let `vol` 127 be ×2 (+6 dB) on all six types (the mixer is float into
int32, so nothing clips in the voice path). With `cmp` off nothing limits
the bus, and a bus over full scale hits the pack's hard 24-bit clip. A
limiter that always runs would cost about 15–20 instructions per frame,
which the CPU ceiling does not have.

**C5 — a live gain-reduction readout (recommended).**
Show the detector level and the applied reduction (`busComp.power`,
`min(gr_fast, gr_memory)`) on the compressor settings page. Foreground
read of two floats through a getter; no DSP cost. It makes C1 and C3
tunable by ear and eye on the device.

### 4.3 Recommendation (decided, Q6)

C1 + C2 (floor 1.0) + C3 (half) + C5; you try it on the device and we
retune from there. Together they put a clean kit near −8 to −10 dBFS
on hits (C2), give 3–6 dB of reduction at `cam` 48 without drive (C1), and
stop distortion from being the only way in (C3). No kit is kept equal to
how it sounds now.

### 4.4 Tests

- Host: a Python replica of the block formulas (as S074 used) checks the
  C1 table and the makeup reference; a host law on `distortion_curveSampleF()`
  checks C3's table and that `drv` 0 is bit-identical.
- Gates: `make -C tools/dsp_test all`; `elf_hotloops.py` and the inlined-set
  diff over the fused voice loops (C3); link budget.
- Device: C5 on screen; a clean kit at `cam` 48 / 64 / 96; then the same
  kit with `drv` 64 to hear how far C3 levels it.

---

## 5. Questions (all answered, user 2026-11-13)

| # | Question | Answer |
|---|---|---|
| Q1 | Space `vol` default at 100 % wet | Stays 127: an Effect at 127 should sit level with a dry output (§1.4) |
| Q2 | Page layout | The default layout (§1.4) |
| Q3 | `hed` labels | Letters: `A B C AB AC BC ABC` (§2.5) |
| Q3a | `frz` and `rtg` take the `hed` fix | Yes (§2.5) |
| Q4 | Plain rows and laws | 17 rows plain, laws unchanged (§2.1) |
| Q5 | `fbk` unity; `ton` convention | Unity at row 100 everywhere, unmarked (§2.6); LP-only `ton` 0..127, LP/HP `ton` ±63 (§2.7) |
| Q6 | T4 options | C1 + C2 (1.0) + C3 (half) + C5 (§4.3) |

---

## 6. Statements this makes out of date (for you to update)

- `MEMORY.md` §DSP S073 invariants: "int16 voice filter and float Effect
  filter are twins" (T3: the float twin is gone).
- `EFFECTS_BUS_REFERENCE.md` §4.2a (registered types), §8, and the other
  StereoFilter mentions (18 in that file).
- `EFFECTS_MODULES_DSP_REFERENCE.md` §5 (StereoFilter) and Part I mentions.
- `EFFECTS_MODULES_DSP_REFERENCE.md` §6.1 (CrumpBit `rvb`: S091 P5 R1,
  damping above 63 plays flat; §2.7 replaces it).
- `CPU_USE_DSP_AUDIT.md` (StereoFilter as the Effect in the worst-case
  tables, 9 mentions).
- `MIXER_SYSTEM_DSP_REFERENCE.md` §4.2 (the `cam` table and makeup
  reference, if C1 lands).
- `INSTRUMENTS_DSP_REFERENCE.md`, `MODULE_INTERCHANGE_SPEC.md`,
  `REPOSITORY_LAYOUT.md`, `STORAGE_SRAM_MANIFEST.md`, `AUTOSAVE.md`,
  `DEV_MODES.md`, `FILESYSTEM_SPEC.md`: one or two StereoFilter / `flt`
  mentions each.
- `S095_P1_FX_DELAY_RVB_ECHO.md` D4 and `S095_P1_IMPLEMENTATION.md`:
  `fbk` unity at 106 (§2.6).
- `S095_P1_IMPLEMENTATION.md` §S18 row list (`mix`, the zone rule for
  `hed`/`frz`/`rtg`).
