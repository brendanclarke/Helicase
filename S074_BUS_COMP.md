# S074 — Master bus compressor (`cmp`): general specification

**Status:** draft 2 implemented in source (2026-09-29); hardware/listening
acceptance remains pending. Every question is settled (§11). The line-level
record is `S074_BUS_COMP_IMPLEMENTATION.md`.

**Goal (user, 2026-09-29):** a master bus compressor with a trigger
sidechain:

- It is **not an Effect slot type**. It works on **one** stereo output pair,
  ST1 or ST2, and is configured on the settings menu (SHIFT+LOAD/SAVE).
- It has four controls, **saved per Scene**. Their page is **always the last
  page of the settings menu**, marked with the VOICE mix page's Scene-setting
  cues: `^` instead of `>` as the last character, and `+` on the page itself.
- Its CPU use counts towards the worst-case Scene. You account for it
  manually against the CPU widget, so it needs no reserved budget. It does
  no work while `cmp` is `off`.
- Character: soft-knee, RMS, optical, LA-2A-like.

**Base:** branch `dev-ph5-effects`, HEAD `1dbdd70` (CrumpBit accepted).

**Authorities used:**

- `EFFECTS_MIXER_DSP_REFERENCE.md` §2 (number formats), §3 (mixer), §6
  (cost), and the DSP CPU policy in `MEMORY.md`;
- `AUTOSAVE.md`;
- the current code: `mixer.c`, `MidiVoiceControl.c`, `main.c`,
  `SceneData.h`, `Autosave.h`, `menu.c`, `menuPages.h`, `ParameterArray.h`,
  `presetManager.c`, `storageTypes.c`, `filesystem.c`.

### Changes from draft 1

- **§2 correction.** The draft had the buffers reversed. `St1` (DAC1, MAIN)
  is the mixer's **`output2`** buffer; `St2` (DAC2, OUT2) is **`output`**.
  `mixer_moveDataToOutput()` writes `MIXER_ROUTING_DAC1_*` to `outL2`/`outR2`.
- **No jack fallback** (answer 4). The compressor stays on the pair it is
  given.
- **Runs only when on** (answer 2), with a one-block fade whenever the
  target changes, so the target never steps.
- **Sidechain depth** (answer 5). The trigger's own step velocity is the main
  control, and velocity 127 always gives the deepest duck. The depth also
  scales with `cam`.
- **Makeup** (answer 6) aims for about the same output level at a
  moderately full input, across the whole `cam` range.
- **Saturation** (answer 6) is the cheaper cubic curve. It is gentle and
  grows a little with `cam`.
- **Threshold range:** −3 to −30 dBFS (it was −36). With the new makeup rule
  this caps makeup at about +16 dB (§4.2).
- **Page placement** (answer 3): option (a), a named constant and a
  diagnostic-build check.
- **RAM** (answer 1): approved. The runtime state is in DTCM and uses 24 of
  the approved 32 B.
- **Files and keys** (answer 7): approved. There is no host test file.
- **Storage:** the four bytes are one array, `bus_comp[4]`, indexed by a
  named enum. The layout and byte count are the same as four named fields.

---

## 1. Summary

```
          all voices + FX return summed into the two DAC buses (int32, 24-bit scale)
                                             │
             cmp = St1 → DAC1 bus (`output2`)  ·  cmp = St2 → DAC2 bus (`output`)
                                             │
 ┌──────────────────────────────────────────────────────────────────────────┐
 │ per block (32 frames):  smoothed mean square → dB → soft-knee curve (cam) │
 │                         → optical cell: attack / 2-stage release (ctm)    │
 │   csc trigger ─────────►  (velocity³ × cam depth "lights" the cell)       │
 │                         → gain + makeup (cam) → linear gain g             │
 │ per sample:             x · g(ramped) → cubic saturation (cam) → bus      │
 └──────────────────────────────────────────────────────────────────────────┘
                                             │
                                  DMA pack (24-bit clamp) → codec
```

- **Where:** the last stage of `mixer_calcNextSampleBlock()`, after every
  voice and the FX return have been summed into the selected DAC bus, and
  before the DMA pack. It is a master-bus stage, so it sees everything
  routed to that pair (§2).
- **Controls (per Scene):**
  - `cmp`: target (`off`, `St1`, `St2`);
  - `cam`: amount (threshold, ratio, makeup and saturation in one control);
  - `ctm`: time (attack and, mainly, release in one control);
  - `csc`: trigger sidechain (`off`, voice 1..6).
- **Cost:** while `off`, none. While on, the gain is computed once per block,
  and each sample gets a gain ramp and a cubic saturator: about 1 % of the
  CPU (§5).

---

## 2. Placement and routing

- **Buses.** `mixer_calcNextSampleBlock(output, output2)` fills two
  interleaved int32 stereo buffers:

  | Pair | Buffer | Codec | Jacks |
  |---|---|---|---|
  | `St1` | **`output2`** | I2S3 → DAC1 | MAIN (headphones when nothing is plugged in) |
  | `St2` | **`output`** | I2S2 → DAC2 | OUT2 |

  Values are signed 24-bit scale; `1.0` = int16 full scale = 8,388,352
  (`EFFECTS_MIXER_DSP_REFERENCE.md` §2). The int32 bus holds about 48 dB of
  headroom above full scale, and the pack clamps to 24 bits.
- **Stage position.** The compressor runs on the selected buffer at the end
  of `mixer_calcNextSampleBlock()`, after the FX return (or its `off`
  branch). It sees:
  - every voice routed to that pair, including the mono routes `L1`/`R1` or
    `L2`/`R2`, which land in that pair's channels;
  - the FX return, when it is routed there.
- **No jack fallback (answer 4).** `cmp = St2` always processes the DAC2
  buffer. When nothing is plugged into OUT2, the mixer moves `St2` routes
  onto DAC1, so the compressor then sees only what is still routed to DAC2.
  This is intended.
- **Stereo link (BC2).** One detector and one gain for both channels, so the
  stereo image does not shift. If L and R of a pair are used as two
  independent mono outputs, they are compressed together.
- **Not an Effect.** It has no registry row, no FX send, no FX-sequencer
  lanes and no arena use, and it is independent of the Effect slot.
- **Trigger timing.** `main.c` drains the trigger queue
  (`voiceControl_processPending()`) immediately before each 32-frame mixer
  block, in the foreground. A sidechain trigger therefore lands in the same
  block as the voice it belongs to.

---

## 3. Controls

| Short | Long (≤ 8) | Range and display | Default | Meaning |
|---|---|---|---|---|
| `cmp` | `BusComp` | `off` `St1` `St2` (0..2) | `off` | Which output pair is compressed |
| `cam` | `CompAmt` | 0..127 | 48 | Amount: threshold down, ratio up, makeup up, saturation up (§4.2) |
| `ctm` | `CompTime` | 0..127 | 48 | Time: slower release (mainly) and slightly slower attack as it rises (§4.3) |
| `csc` | `CompSC` | `off` `1`..`6` (0..6) | `off` | Voice whose triggers duck the bus; depth set by the trigger's velocity and by `cam` (§4.4) |

- **Per Scene.** The four values belong to the Scene
  (`scene_settings_t::bus_comp[]`). They are saved in `sceneset.scg` and
  AutoSave, and travel with Scene and Bank save/load. A Scene switch changes
  them (§4.6).
- **Edits** go to every Scene in the VOICE edit mask, as PERF `srt` does
  (BC15). Each goes through a Preset setter that writes the Scene value with
  AutoSave marking, then updates the page mirror.
- **Not modulatable in v1:** no Pattern automation, LFO or Morph (BC16).
- **Labels (BC17):** a full view shows category `Scene` and the long name.
  `cmp` shows `off`/`St1`/`St2`; `csc` shows `off`/`1`..`6`. They need custom
  value text, because all 16 `DTYPE_MENU` ids and all 16 dtype codes are
  taken (§7).

---

## 4. DSP model (LA-2A flavour)

All numbers are **starting points for tuning**. Let `a = cam/127` and
`t = ctm/127`. The block period is `T_b = 32 / 44,108 Hz = 0.7255 ms`, so the
block rate is 1,378 Hz. Every one-pole below uses the per-block coefficient
`k(τ) = T_b / (τ + T_b/2)`: one division, no exponential.

### 4.1 Detector (feed-forward, RMS)

- For the selected bus, per sample, the loop accumulates `L² + R²` on the
  **input** (before gain), in int32 units converted to float.
- At the block end: `ms = sum / 64` in full-scale units. It is smoothed in
  the power domain, `P += k(5 ms) · (ms − P)`, so the detector is a true RMS
  average and a 32-sample window of bass is not read as a peak.
- At the next block start: `level_dB = 10·log10(P + 10⁻¹²)` through one
  `log2f()` (`3.0103 × log2`). The ε puts the floor at −120 dBFS and keeps
  `log2f` away from 0.
- The gain for block N therefore uses the level measured up to block N−1.
  This one-block (0.73 ms) detector lag lets the stage read and write the
  buffer in a single pass. It is negligible against the 5–15 ms attack.
  There is no look-ahead and no added latency.

### 4.2 Static curve, makeup and the `cam` macro

The soft-knee gain computer uses `over = level_dB − T`, a knee width
`W = 12 dB`, and slope `s = 1 − 1/R`:

- `over ≤ −W/2`: `GR = 0`;
- `|over| < W/2`: `GR = −s · (over + W/2)² / (2W)`;
- `over ≥ W/2`: `GR = −s · over`.

`GR` is in dB and ≤ 0. The wide knee gives the LA-2A's gradual onset, and
the effective ratio rises with level.

`cam` moves four things together:

- **Threshold:** `T = −3 − 27·a` dBFS, so −3 at `cam` 0 and −30 at `cam` 127.
- **Ratio:** `R = 1 + 3a + 4a³`, so 1:1 at `cam` 0 and 8:1 at `cam` 127.
- **Makeup (answer 6, tuning revision 1):** `M = −GR(L_ref) · (1 − 0.3·a)`,
  with `L_ref = −12 dBFS RMS` (a moderately full bus). At low `cam` a steady
  input at `L_ref` leaves at about `L_ref`. Towards the top the level rise
  tapers off progressively: a −18 dBFS RMS bus leaves at about −17.5 dBFS at
  `cam` 127 (it was −12.8 before the revision).
- **Saturation drive (tuning revision 1):** `d = 1 + 0.5·a²` (was `0.4·a²`)
  (§4.5).

Worked values at `L_ref`:

| `cam` | `T` (dBFS) | `R` | Static GR at −12 dBFS | Makeup `M` (was) | `d` (was) |
|---:|---:|---:|---:|---:|---:|
| 0 | −3.0 | 1.00 | 0 | 0 dB | 1.00 |
| 48 (default) | −13.2 | 2.35 | −1.2 dB (in the knee) | +1.1 dB (+1.2) | 1.07 (1.06) |
| 64 | −16.6 | 3.02 | −3.1 dB | +2.7 dB (+3.1) | 1.13 (1.10) |
| 96 | −23.4 | 5.00 | −9.1 dB | +7.1 dB (+9.1) | 1.29 (1.23) |
| 127 | −30.0 | 8.00 | −15.8 dB | +11.0 dB (+15.8) | 1.50 (1.40) |

At `cam` 0 the compressor applies no gain change (ratio 1:1, makeup 0). Only
the sidechain duck (§4.4) and the saturation ceiling at full scale (§4.5)
remain.

### 4.3 Optical cell and the `ctm` macro

The cell holds two gain-reduction states in dB. Each block moves them
towards the static target `GR`:

- **Fast stage `G_f`** (the main release):
  - **Attack**, when the target is below `G_f`: `τ = 5 + 10·t` ms (5..15).
    The LA-2A's attack is about 10 ms and barely adjustable.
  - **Release**, when the target is above `G_f`: `τ = 60 + 540·t²` ms
    (60..600; 137 ms at the default).
- **Memory stage `G_m`** (the T4 cell's second, slow release):
  - It charges towards `0.5·G_f` with a fixed `τ = 300 ms`. A brief peak
    barely charges it; sustained compression charges it to half the
    reduction.
  - It releases towards `0.5·G_f` with `τ = 500 + 4,500·t²` ms (0.5..5 s;
    1.1 s at the default).
- **Applied reduction:** `min(G_f, G_m)`, the deeper of the two. Brief peaks
  release quickly. After sustained compression, up to half the reduction
  lingers and recovers over seconds (program dependence).
- Everything runs in the dB domain at block rate, so there are no
  per-sample exponentials.

### 4.4 Trigger sidechain (`csc`)

- **Source:** the trigger funnel `voiceControl_triggerNow()`
  (`MidiVoiceControl.c:151`). The sequencer, rolls, MIDI and front-panel
  previews all pass through it.
- **Voice matching:** `csc` = *n* matches track *n*. **Track 7 (VOICE7, slot
  6's alternate sound) counts as voice 6.** This is the working assumption
  for BC11; see §11.
- **Depth (answer 5):** `duck = −D(a) · f(v)` dB:
  - `f(v) = (v/127)³`: velocity 127 always gives the full depth, and most of
    the range lies between velocity 64 and 127;
  - `D(a) = 2 + 19·a` dB (tuning revision 1; was `9 + 9·a`): extremely mild
    at low `cam` (2 dB at `cam` 0), 9.2 dB at the default, and fairly extreme
    at the top (21 dB at `cam` 127, was 18).

  | Velocity | `f(v)` | Duck at `cam` 0 | at `cam` 48 | at `cam` 127 |
  |---:|---:|---:|---:|---:|
  | 127 | 1.00 | −2.0 dB | −9.2 dB | −21.0 dB |
  | 100 | 0.49 | −1.0 | −4.5 | −10.3 |
  | 80 | 0.25 | −0.5 | −2.3 | −5.3 |
  | 64 | 0.13 | −0.3 | −1.2 | −2.7 |
  | 30 | 0.01 | −0.0 | −0.1 | −0.3 |

- **How it acts:** in the block after the trigger, the fast stage is set to
  `min(G_f, GR + duck)`. It then releases through the `ctm` ballistics, as if
  a light pulse had darkened the cell (BC10).
  - Measuring the duck from the static target `GR`, not from `G_f`, keeps
    velocity 127 a consistent depth *below the program's own compression*.
    Fast repeated triggers do not accumulate.
  - Sustained triggering charges the memory stage, as real optical pumping
    does.
- **Timing:** the gain ramps linearly over one block, about 0.7 ms (BC12).
  There is no click.
- **Several triggers in one block:** the largest `f(v)` wins.
- **Velocity 0:** no duck.
- **`cmp` off:** the sidechain has no effect (BC13). Pending triggers are
  discarded.
- **Hook:** one call in `voiceControl_triggerNow()`. The compressor reads
  the active Scene's `csc` and stores the pending weight. It runs in the
  foreground only, with no interrupt state.

### 4.5 Gain application and saturation (per sample)

- **Linear gain per block:** `g = 2^((min(G_f, G_m) + M) / 6.0206)` through
  one `exp2f()`.
- **Per sample:** `g` ramps linearly from the previous block's value (as the
  mixer's own gains do), and `y = x · g`.
- **Saturator (answer 6, the cheapest):** a cubic soft clip, which is
  polynomial with no division:
  - `u = clamp(d·y/1.5, −1, 1)`;
  - `h = u − u³/3`;
  - `out = 1.5·h/d`.
- **Small signals** keep unity gain, because `1.5/d · d/1.5 = 1`. The
  ceiling is `1/d`: 0 dBFS at `cam` 0 and −3.5 dBFS at `cam` 127 (−2.9
  before tuning revision 1).
- **Gentleness:**
  - at `cam` 0 a −6 dBFS peak loses 0.3 dB, and full scale loses 1.4 dB;
  - at `cam` 127 a −6 dBFS peak loses 0.76 dB (0.65 before revision 1);
  - on a moderately full, compressed bus the RMS falls by a few tenths of a
    dB, within "approximately the same level".
- **It is also the soft ceiling:** makeup can never drive this pair into the
  pack's hard 24-bit clip (R1).
- **Scaling:** `d/1.5` is folded into the ramped gain, and `1.5/d` and the
  int32 scale into one output constant. `d` is taken from the current block
  for both, so the small-signal gain stays continuous when `cam` changes.

### 4.6 Transitions

- **Scene switch:** the four values change with the Scene. For the same
  target, the cell state continues and the gain ramps, so a change of `cam`
  or `ctm` is smooth.
- **Turning on (`off` → `St1`/`St2`):** the cell is seeded as if the bus sat
  at `L_ref`: `P = L_ref`, `G_f = GR(L_ref)`, `G_m = 0`, and the ramp starts
  from `g = 1`. The output therefore starts from the cell's steady state for a
  reference-level bus and settles from there with no makeup-driven jump. It crossfades from dry to wet over one block
  (`out = dry + w·(wet − dry)`, with `w` ramping 0 → 1).
- **Turning off:** the processed pair crossfades from wet back to dry over
  one block. From the next block no work is done.
- **`St1` ↔ `St2`:**
  - in block N, the old pair fades out as above;
  - in block N+1, the new pair fades in, reseeded;
  - neither pair steps. This supersedes the draft-1 BC4 default, in which the
    old pair stepped to unity.
- **The dry path is exact:** `float(int32)` is exact up to ±2²⁴, well above
  the 24-bit pack range, so a fully dry sample is unchanged.

---

## 5. CPU

- **Only when on (answer 2).** With `cmp = off`, the stage returns after one
  settings read and clears any pending sidechain weight. With `cmp` on, its
  cost counts towards the worst-case Scene, and you account for it against
  the CPU widget.
- **Estimate** (cycles, `-Ofast`, to be measured):

  | Part | Per stereo frame | Per block |
  |---|---:|---:|
  | Two int→float converts, `L²+R²` accumulate | about 4 | — |
  | Gain ramp, two multiplies, two clamps (VMINNM/VMAXNM) | about 7 | — |
  | Cubic saturator, both channels (3 operations each) | about 6 | — |
  | Crossfade (two operations each), two float→int converts, stores | about 9 | — |
  | Detector `log2f`, knee, cell, two divisions, `exp2f`, sidechain | — | about 350–450 |

  In total, about 26 cycles per frame (≈1.15 M/s) plus about 0.55 M/s of
  block work. That is about **0.8–1 % of the 216 MHz CPU**, independent of
  the settings.
- **No per-sample transcendental functions:** `log2f` and `exp2f` run once
  per block.

---

## 6. Storage and RAM

| Item | Change | RAM |
|---|---|---|
| `scene_settings_t` | `uint8_t bus_comp[4]` (`cmp`, `cam`, `ctm`, `csc`) | `scene_settings_t` 41 → 45 B, `scene_t` 1,622 → 1,626 B (measured): **+64 B SRAM1** in `scenes[16]`. **Approved** (answer 1). |
| AutoSave | Scene parameters 41–44 (`AUTOSAVE_SCENE_PARAM_BUS_COMP_BASE`); live bytes 41 → 45 of the 118 reserved per Scene; no format-version change (the S072 Effect Morph cell set the precedent) | **0 B.** The record stays 34,768 B and the mask is unchanged. Old records hold 0 in these cells, so the first restore after the upgrade reads `off`, `cam 0`, `ctm 0`, `csc off` (§10 R8). |
| `sceneset.scg` | keys `bus_comp_mode`, `bus_comp_amount`, `bus_comp_time`, `bus_comp_sidechain` (approved); out-of-range values are clamped; a missing key keeps the default | file only |
| Page mirrors | four `PAR_BUS_COMP_*` ids appended in `ParameterArray.h`; `NUM_PARAMS` stays 384, so they use existing `parameter_values[]` bytes | **0 B** |
| Compressor runtime | `P`, `G_f`, `G_m`, previous `g`, pending sidechain weight (five floats), active pair (one byte) = **24 B** | **DTCM `.dtcmz`, approved up to 32 B** (answer 1). DTCM is the faster place for state that is read every block. The FX arena base is 32-byte aligned, so the arena shrinks by 32 B (126,624 → 126,592 B), still 3,712 B above the 122,880 B linker minimum. |
| Typed load staging | the stage cache holds Scene settings | the `filesystem.c:1092` assert sums to 2,009 of 2,048 B after +4 B (fits) |

---

## 7. Settings menu

- **Placement (BC18 = a):** the compressor page is sub-page 2 of
  `MENU_MIDI_PAGE`, first half: `cmp cam ctm csc`. Its second half is empty.
  - It follows the page that ends with the Pattern allocation indicator
    (`PAR_PAT_STORE_USE`).
  - A named constant, `MENU_GLOBAL_SCENE_SUBPAGE = 2`, marks it. **Rule: new
    global pages go before it**, and the constant moves up with them.
  - A check in the diagnostic build (`DEV_MODE_DIAGNOSTIC`, the FxBf
    precedent) verifies at boot that the page is the compressor page, that
    its second half is empty, and that no later global sub-page is
    populated. It is silent on success. On failure it shows a 1.5 s message.
    Production builds compile it out.
- **Navigation:** no change is needed.
  - The encoder reaches the page after the `pts` cell and stops at its last
    cell.
  - SELECT 3 opens it directly.
  - Pressing SELECT again loops back to the first page (existing behaviour).
- **Scene-setting cues:** the VOICE mix convention in `checkScrollSign()`
  (`menu.c:8075`):

  | Screen | Today | With the compressor page |
  |---|---|---|
  | Sub-page 0, first half (first settings screen) | `>` | `^`: a Scene page follows at the end |
  | Middle screens | `*` | `*` (unchanged) |
  | Sub-page 1, second half (`… pts`) | `<` (last page) | `*`, automatic, because sub-page 2 is now populated |
  | Compressor page | — | `+`: Scene-owned, never `<` |

- **Cells:** four static cells. Their values are the active Scene's,
  mirrored into `parameter_values[]`.
- **Commit path:** a dedicated branch in `menu_cellCommitValue()`. It clamps,
  sends the value to every edit-masked Scene through Preset, then refreshes
  the mirror.
  - It never uses `menu_parseGlobalParam()` or the settings.cfg dirty mark.
    The Global bulk apply (`menu_sendAllGlobals()` and
    `menu_tickGlobalApply()`) replays every id from
    `PAR_BEGINNING_OF_GLOBALS` up after a Settings Load. The legacy binary and
    stale-globals load paths (`filesystem_resetGlobalsToDefaults()`) also
    zero those bytes first. The keyed settings.cfg reset does not.
  - In `menu_parseGlobalParam()` the four ids therefore only **refresh the
    mirrors from the active Scene**. A Settings Load can never write the
    Scenes, and the page never shows reset zeros.
- **Value text:** `cmp` and `csc` are special-cased in
  `menu_formatCellValue3()` and the full view, because no dtype code is free.
  `cam` and `ctm` are plain 0..127.
- **Scene switch while on the page:** the mirrors refresh in
  `preset_applySceneSettings()`, and the page repaints.
- **No meter or gain-reduction display** (not requested; no extra widgets).

---

## 8. Files

| File | Change |
|---|---|
| `Core/DSPAudio/BusCompressor.c/.h` | **new** (approved): the gain computer, cell, sidechain intake and per-sample stage. `-Ofast` through `DSP_SRCS` |
| `Core/DSPAudio/mixer.c` | Call the stage at the end of `mixer_calcNextSampleBlock()` with (`output2`, `output`) |
| `Core/MIDI/MidiVoiceControl.c` | Sidechain intake in `voiceControl_triggerNow()` |
| `Core/Bank/Scene/SceneData.c/.h` | Field enum, the array, defaults, clamp, change-aware setter, getter |
| `Core/Bank/Scene/Autosave.c/.h` | Parameters 41–44: enum, live bytes, getter and reader branches, group asserts |
| `Core/Hardware/SD/storageTypes.c/.h`, `filesystem.c` | Four keys (parse, clamp, write); defaults in the two filesystem default paths |
| `Core/Bank/Scene/Preset/presetManager.c/.h` | Setter with mirror, mirror sync, Scene-apply refresh |
| `Core/Bank/Scene/Preset/ParameterArray.h` | Four `PAR_BUS_COMP_*` ids |
| `Core/Menu/menu.h`, `MenuText.h`, `menuPages.h`, `menu.c` | Text/long/short/category ids and labels, page row, constant, commit, clamp, value text, cues, bulk-apply guard, the diagnostic check |
| `Makefile` | `DSP_SRCS` gains `BusCompressor.c` |
| Docs | `EFFECTS_MIXER_DSP_REFERENCE.md`, `AUTOSAVE.md`, `FILESYSTEM_SPEC.md`, `STORAGE_SRAM_MANIFEST.md`, `MODULE_INTERCHANGE_SPEC.md`, `DEV_MODES.md` |

---

## 9. Verification (outline)

The full matrix is in the implementation schedule §9.

- **Build:**
  - `data` is unchanged;
  - `bss` grows by 64 B (Scenes), and `.dtcmz` by 24 B, a 32 B arena step;
  - the AutoSave record is unchanged (34,768 B);
  - the `link_budget.py` report;
  - no new warnings.
- **Hardware (yours):**
  - cues `^`, `*` and `+`;
  - `St1`, `St2` and `off`, with fades and no clicks;
  - headphones on ST1;
  - `cam` and `ctm` sweeps on a full mix, with the output level roughly
    constant across `cam`;
  - a `csc` kick at velocities 30, 64, 100 and 127;
  - track 7 against `csc 6`;
  - Scene switches with different settings;
  - `sceneset.scg` save and load;
  - AutoSave restore;
  - Settings Load must not change the page values;
  - the worst-case Scene with `cmp` on against `off`, read from the `cpu`
    widget.

---

## 10. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | Makeup pushes the bus into the pack's hard clip. | The saturator is a soft ceiling at `1/d` ≤ full scale. |
| R2 | Pumping on sustained low end. | Intended LA-2A behaviour, and the 5 ms RMS smoothing removes the per-block ripple. A sidechain high-pass is a possible later option (not in v1). |
| R3 | Makeup of up to +16 dB lifts quiet material (tails, noise) at high `cam`. | The nature of heavy levelling. The threshold range was reduced to −30 dBFS. Tune by ear. |
| R4 | A duck on `St2` with nothing plugged into OUT2 has no audible effect (no jack fallback). | Your decision (answer 4). |
| R5 | RAM: +64 B SRAM1 and 24 B DTCM. | Approved. |
| R6 | "Always last" can break when a later global page is added. | The named constant, the rule, and the diagnostic boot check. |
| R7 | CPU: about 1 % while on. | Counted against the worst-case Scene; measure on it. |
| R8 | The first AutoSave restore after the upgrade reads `cam 0`, `ctm 0` from old records' reserved cells (not the defaults 48). | Safe (`cmp` stays `off`). Set the values once and they persist. |

---

## 11. Decisions

| # | Question | Decision |
|---|---|---|
| BC1 | Jack fallback | **None**: the compressor stays on its pair (answer 4) |
| BC2 | Stereo link | One linked detector and gain (default) |
| BC3 | Always running | **No**: no work while `off`. The cost counts towards the worst-case Scene (answer 2) |
| BC4 | Target transitions | One-block fades; `St1` ↔ `St2` fades the old pair out, then the new pair in (§4.6) |
| BC5 | `cam` curves, saturation at `cam` 0 | §4.2 (threshold to −30 dBFS, makeup at the −12 dBFS reference). The ceiling stays in the path at `cam` 0 |
| BC6 | `ctm` curves | §4.3 starting points |
| BC7 | Saturator | Cubic, the cheapest (answer 6) |
| BC8 | Detector | `½(L² + R²)`, smoothed over 5 ms (default) |
| BC9 | Sidechain depth | `(v/127)³ × (9 + 9a)` dB, with velocity as the main control (answer 5) |
| BC10 | Sidechain combination | The trigger lights the same cell and releases with `ctm` (answer 5) |
| BC11 | Track 7 | **Working assumption: counts as voice 6.** Please confirm on hardware; changing it is a one-line edit in `busComp_sidechainTrigger()` |
| BC12 | Duck attack | One-block ramp (default) |
| BC13 | `cmp` off | The sidechain has no effect (default) |
| BC14 | RAM | +64 B SRAM1 and 24 B DTCM (approved up to 32 B) (answer 1) |
| BC15 | Fan-out | Every Scene in the VOICE edit mask (default) |
| BC16 | Modulation | None in v1 (default) |
| BC17 | Labels | `cmp cam ctm csc`; `BusComp CompAmt CompTime CompSC`; category `Scene`; `csc` numbers (default) |
| BC18 | "Always last" | (a): the constant, the rule, and the diagnostic check (answer 3) |
| BC19 | Defaults | `off`, 48, 48, `off` (default) |
| BC20 | New files | `BusCompressor.c/.h` only (answer 7) |
| BC21 | File keys | `bus_comp_mode`, `bus_comp_amount`, `bus_comp_time`, `bus_comp_sidechain` (answer 7) |
