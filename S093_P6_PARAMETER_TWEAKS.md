# S093 P6 — Alkaline Parameter Tweaks: Plan and Assessment

- **Session:** 093, phase 6. The Alkaline is in the firmware and runs on the
  device (S093 P4/P5). This document assesses and plans the requested
  changes. **No code is changed by this document.**
- **Requests (user, S093):**
  1. `sld` to the mod page (third sub-page);
  2. an attack parameter on the AEG page (second sub-page);
  3. `dec` to AEG, 10–20 % shorter at its shortest and 50 % longer at its
     longest;
  4. `acc` to the click page (fifth sub-page);
  5. maximum resonance 50 % higher;
  6. maximum drive 200 % of now;
  7. samples and wavetables as oscillator types (`typ`) beside Saw/Sqr,
     **fitted under the Drum ceiling**;
  8. `pos` on the two stock waves.
- **Revision 3 (user, S093):**
  - `dec` keeps its label and only moves to AEG, so FILTER is one screen
    (`cut res env drv`);
  - decay **−20 %** at the shortest;
  - the `16^drive` curve;
  - RAM approved;
  - the self-oscillation cost **measured** (§5.5): **0 per sample**.
- **Revision 2 (user answers, S093):**
  - **attack** is modelled as a variable CPU trigger-pulse width (the
    exponential stays);
  - **resonance** is ×1.5 feedback, and self-oscillation is fine;
  - **samples/wavetables** must find savings to stay Basic, with your
    suggestion of locking interpolation slots instead of going Advanced;
  - **`pos`** goes ahead on Saw/Sqr.
- **Reference figures** (linked image, S093 P5 §9.3):
  - Alkaline **252.1 per sample** (fused loop 214 with 3 VDIV, plus 1,218 /
    32 per block with 9 VDIV);
  - Drum ceiling **261.3**.
- **Binding rules:**
  - the CPU ceiling (whole voice, per-block work included);
  - constant per-sample cost;
  - new or enlarged RAM needs sign-off;
  - no backward compatibility (stored bytes may change meaning);
  - rows and types are appended.

---

## 0. Summary

| # | Change | Per-sample CPU | Status / main risk |
|---|---|---|---|
| 1 | `sld` → mod page | **0** | Planned |
| 2 | Attack = CPU trigger width (byte 0 = today's attack, exactly) | **≈ 0** | Planned. RAM +8 B inside the voice slot (**approved**) |
| 3 | `dec` → AEG (label unchanged); range **−20 %** / +50 % | **0** | Planned. It stays the filter (MEG) decay, by choice |
| 4 | `acc` → click page | **0** | Planned |
| 5 | Resonance: feedback ×1.5 at max, self-oscillation allowed | **0 (measured, §5.5)** | Planned. The 64-sample network holds; flash +1,152 B (13 Res points) |
| 6 | Drive ×16 at max, `16^drive` | **0** | Planned. Level/accuracy checks |
| 7 | `typ` with `smp`/`slp`/`wtb` | Plain modes **≈ 256–260 (Basic)**; the blend premium **on the interpolation pool**, as Drum's blend is | Planned with four savings (§7.3). Your slot lock is the reserve if the measured plain modes miss the ceiling (§7.5). Item 5 no longer takes any of this margin |
| 8 | `pos` on Saw/Sqr (start phase; 0 = free-running) | **+0.8** | Planned |

---

## 1. Menu layout (items 1, 3, 4 and the new rows)

Page index = front-panel SELECT button (S093 P4 F3). Each page is 16 cells,
shown as four-cell screens.

| Page | Button | Now | After P6 |
|---|---|---|---|
| 0 OSC | SELECT1 | `coa tun wav sld` | **`coa tun wav pos` \| `typ`** (Drum's order: `pos` 4th, `typ` on the second screen) |
| 1 AEG | SELECT2 | empty | **`atk dec`** |
| 2 mod | SELECT3 | `dst amt` | **`dst amt sld`** |
| 3 FM | SELECT4 | empty | empty |
| 4 click | SELECT5 | empty | **`acc`** |
| 5 FILTER | SELECT6 | `cut res env dec` \| `drv` | **`cut res env drv`** (one screen) |
| 6 LFO | SELECT7 | Drum layout | unchanged |
| 7 MIX | SELECT8 | `acc vol pan` | `vol pan` |

- **Change:** `alkaline_menu_pages[]` in `AlkalineParameters.c`. Cells name
  rows by enum, so moving them changes no stored index, NRPN number or file
  key. CPU 0, RAM 0.
- **New rows, appended** to `alkaline_param_index_t`: `alk_attack` (26),
  `osc1_position` (27), `osc1_type` (28). The existing NRPN numbers and Kit
  cells stay.
- **Follow-ups:**
  - `init.alk`, `make_init_alk.py` `INIT`, `Alkaline_initVoice()`;
  - `check_special_tags.py`: `osc1_position` / `osc1_type` take their Drum
    tags; `osc1_type` is `IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1`;
  - the docs (NRPN table, FILESYSTEM_SPEC keys, counts).

---

## 2. Attack: the CPU trigger width (item 2)

### 2.1 The model (your intent)

The 303's VEG attack is set by how long the CPU's trigger pulse turns Q32 on:
- C41 charges through R134 (τ 2.2 ms), and Q32 empties C42 through R132
  (S092 P3 §3.2). Near the threshold a slightly shorter trigger turns Q32 on
  less, so C42 empties more slowly.
- The attack keeps its **exponential** shape (the C42 discharge); only its
  delay and time constant change.
- This is also the part that differs between real units (S092 P3: "real
  units would differ with component tolerance").

The SPICE sweep (12 V gate):

| Trigger | Attack 10→90 % | Trigger to 90 % |
|---:|---:|---:|
| 0.55 ms | 5.15 ms | 6.20 ms |
| 0.60 ms | 3.45 ms | 4.42 ms |
| **0.65 ms** | **2.61 ms** | 3.52 ms |
| 0.70 ms | 2.11 ms | 2.99 ms |

Today's model is the 0.65 ms point: a delay of 0.65 ms
(`ALK_VEG_ATT_DELAY_S`), then τ = 1.2 ms (`ALK_VEG_TAU_ATT_S`) for 8 τ
(`alkVeg_step()`, `AlkalineControl.c` l. 276).

### 2.2 The parameter

- **Row:** `ROW("alk_attack", "Amp EG", "Attack", "atk", DTYPE_0B127,
  MOD_0_127, attack, TYPE_FLT)`.
- **Range:**
  - **byte 0 = trigger 0.65 ms, today's attack, bit for bit** (the same
    delay and τ constants);
  - byte 127 = trigger 0.55 ms, about 5.15 ms 10→90 %.
  - The bytes in between follow the trigger width, linear from 0.65 to
    0.55 ms, which is what a trim on the trigger would do. The attack then
    lengthens faster towards the top, as in the sweep.
- **Table:** `gen_alk_tables.py` emits a small `alkVegAttack[9]` table of
  (delay, τ) pairs. It is fitted to SPICE VCA runs at 9 trigger widths from
  0.65 to 0.55 ms (the S092 P3 harness, `tools/spice/tb303`). Point 0 is
  forced to today's constants.
- **Code:**
  - `alkVeg_trigger()` interpolates the (delay, τ) pair from the attack value
    and stores the delay, the per-block discharge factor
    `alk_expf(−ALK_DT / τ)` and the discharge length (8 τ) in `AlkVeg`. The
    trigger runs in the main loop, not on the block path.
  - `alkVeg_step()` multiplies by the stored factor instead of the folded
    constant.
- **Init byte:** 0.

### 2.3 What you will hear

As modelled and as on a 303, the attack shows on notes that follow a rest or
a long note. On running 16ths, C42 has not recharged (τ 1.5 s), so those
notes open almost at once whatever the trigger width. That is the
circuit's behaviour, kept on purpose.

**Fallback (only if you later want an attack on every note):** a VCA
current ramp from byte 0 = today's attack exactly (no ramp) up to about
5 ms. It would replace the trigger-width row, not sit beside it, so the
exponential variation is no longer exposed. Cost about +0.16 per sample.

### 2.4 Cost and RAM

- **CPU ≈ 0 per block:** one stored factor in place of an immediate; the
  `alk_expf()` and the table lookup run at the trigger.
- **RAM: +8 B in `AlkVeg`** (factor and discharge length; the delay field
  exists), so `AlkalineVoice` goes from 540 to 548 B. It stays inside the
  existing 1,264 B slot reserve; no allocation grows. **Approved (user,
  S093).**

### 2.5 Tests

- A new law, **AL-A**:
  - byte 0 is bit-identical to today's VEG over a full note;
  - bytes 0 / 64 / 127 give 10→90 % attack after silence of
    2.6 / ≈ 3.4 / 5.15 ms (±1 block);
  - legato gives no new attack (AL-S unchanged).
- VEG decay and accent are unchanged.

---

## 3. Decay (item 3)

### 3.1 What the knob is

`dec` is the 303's **VR6, the MEG (filter-envelope) decay**. It discharges
C62 through R138 68 kΩ + VR6 1 MΩ (`alkMeg_step()`, l. 113). The 303 has
no amp decay control: the VEG decay is fixed (τ 1.5 s, D10).
- It moves to the AEG page with its label unchanged (`Decay` / `dec`); it
  still shapes the cutoff sweep, by your choice. The aim is FILTER on one
  screen: `cut res env drv`.
- A VEG decay knob would be a separate control-rate row at 0 per-sample cost
  (Devil Fish, E3). Not planned unless asked.

### 3.2 Range

Model: decay to 10 % is 173 ms at the minimum and 2.54 s at the maximum
(S092 P4 §3.3).

| Setting | Now | Target |
|---|---|---|
| `dec` 0 | 173 ms | **−20 % ≈ 138 ms** |
| `dec` 127 | 2.54 s | **+50 % ≈ 3.81 s** |
| `dec` 64 | ≈ 0.6 s | ≈ 0.8 s (the taper keeps its shape) |

- **Law:** `r = ALK_MEG_R_MIN + ALK_MEG_R_SPAN × alk_taper(decay)` (now
  `68e3 + 1e6 × alk_taper(decay)`). First order: about 54 kΩ and 1.60 MΩ.
  The diode stack makes the time not exactly proportional to r, so both
  constants are **fitted with `alk_model.py`** and generated into
  `AlkalineTables.h` by `gen_alk_tables.py`.
- **Accent unchanged:** the accent branch keeps 68 kΩ + 250 Ω (the 4066).
- **CPU 0, RAM 0.**
- **Tests:** the MEG decay law at `dec` 0 / 127 (138 ms / 3.81 s ±3 %); the
  accented decay unchanged (±1 %).

---

## 4. Accent (item 4)

A menu move (§1). CPU 0.

---

## 5. Resonance: feedback ×1.5, self-oscillation allowed (item 5)

### 5.1 Model change

Res is the dual pot VR4. VR4a is the wiper divider TP6 → W that sets the
ladder feedback (`FeedbackNet(k)`, `alk_model.py` l. 278); VR4b is in the
accent sweep. At the 303's maximum the wiper is at the top, so ×1.5 needs a
virtual stage at the wiper.

**Choice, measured (§5.5): an ideal transformer at the wiper** for k > 1:
- V(W) = k · V(TP6), and TP6 supplies k × the W-side current; the pot at the
  top stays as its 50 kΩ load on W.
- At k = 1 it is exactly the stock wire. The patched network matches the
  stock one to 4·10⁻⁸ (A, B, C and D matrices).
- A plain buffer (VCVS) was tried first. It moved the network dynamics by
  2 % at k = 1, because it stops C15 and C22 loading TP6, so it would put a
  step in the knob at the 303's maximum.

**Law:** the whole travel runs k_eff = 1.5 · k (k = byte / 127). The 303's
maximum sits at byte ≈ 85; above it is new.

**VR4b** (accent sweep) stays physical: `min(k_eff, 1)`.

**Regenerated by `gen_alk_tables.py`** (a `--res-max 1.5` option and 13 points
in the firmware build):
- `alkResTable` with **13 points over k_eff 0..1.5**. That is the same 0.125
  spacing as today's 9 points over 0..1, so k_eff = 1.0 is a table point.
  Flash +4 × 288 B = **+1,152 B**.
- `alkPrewarpC` over the same range, with `alk_prewarp()`'s clamp moved from
  1 to 1.5. In the experiment it was held at its k = 1 value above 1; the
  firmware version derives it from the model for k > 1.
- The **r6 reduced basis**, from the Gramians over 0..1.5.

**In the tree:**
- `alk_model.py` `FeedbackNet` gets the transformer branch for k > 1 (the
  experiment's `patch.py`, §5.5);
- `gen_alk_tables.py` gets the range option;
- the SPICE netlist (`tools/spice/tb303/e2e.py`) gets the transformer as an
  E/F source pair at the wiper, for golden runs above k = 1.

### 5.2 Self-oscillation

Above the threshold the loop sustains a tone, bounded by the ladder's
nonlinear rungs and the input pair. Whether it sustains depends on the
cutoff, as on any resonant filter:
- **at the calibration point** (cutoff centre, `env`/`dec` minimum), k_eff
  1.5 rings strongly but still decays: 0.823 per cycle against today's
  0.363, period 1.791 ms against 2.018 ms;
- **at the acid pattern's cutoffs** it sustains with no input (§5.5).

### 5.3 Cost

**0 per sample** (§5.5):
- The 64-sample network (T2-2) holds at ×1.5; the 32-sample network gives
  the same result, so it is not needed. It would have cost +180 per block,
  +5.6 per sample.
- The per-block count with the 13-point ×1.5 tables is 1,285, the same as
  today (the blend is the same; only the point index scales).

### 5.4 Tests

- **AL-R:**
  - at k_eff = 1.0 the stock ring (2.018 ms, 0.363 per cycle) within the
    interpolation;
  - at k_eff = 1.5 the calibration ring 1.791 ms / 0.823 per cycle (the new
    expected values).
- **AL-SO, new:** at k_eff = 1.5 with a high cutoff, the output sustains
  after the input stops; it is bounded below full scale; its pitch is within
  1 % of the golden model.
- **AL-B:** bounded at every Res, as now.
- **C1/C2** against the extended model.
- **AL-D** with Res full and drive ×16 together, the loudest corner.

### 5.5 Measured (scratch experiment, S093 P6)

**Method:**
- `alk_model.FeedbackNet` patched in-process with the wiper transformer;
  `gen_alk_tables.py` with the Res axis scaled to k_eff = 1.5 · k. Three
  table sets were generated outside the tree: the stock control, ×1.5 on the
  64-sample network, and ×1.5 on the 32-sample network; then 13 points.
- The bench (`modules/alkaline.c`) was built against each, and its
  `--traces` mode was driven with the SPICE `acid` inputs (0.6 s) at full
  Res.
- The golden RK4 model (`LadderRef`, 8× oversampled, the same patched
  network) is the reference.
- A sustain test zeroed the input after 40 % of the window.

**Results:**

| Check | Result |
|---|---|
| Control (stock tables through the experiment path) | AL-R 2.018 ms / 0.363: identical to the shipped build; all ten bench groups pass |
| Boundedness at every Res (AL-B), ×1.5, both networks | 0 failures |
| Calibration ring at k_eff 1.5 | 64-sample 1.791 ms / 0.823; 32-sample 1.768 ms / 0.820 |
| Stock maximum, golden comparison (`acid`) | B2R LSD60 0.37 dB, level −0.07 dB; no self-oscillation (the sustain tail dies) |
| ×1.5, `acid` | Golden and C both self-oscillate. Pitch 698.9 Hz against golden 701.7 Hz; level −0.37 dB; LSD60 3.36 dB |
| ×1.5, sustain (no input for the last 60 %) | Sustained in golden and C: 776.5 Hz against 773.8 Hz (0.35 %); tail RMS 0.48 V against 0.51 V (−0.5 dB); peak about 1.9 V (full scale is 7 V) |
| 64- against 32-sample network at ×1.5 | LSD60 3.36 against 3.37 dB, level −0.37 against −0.39 dB, tail equal: **T2-2 is not the limit** |
| Kernel at ×1.5 | B2R 3.36 / B-R 3.57 / A-R 4.15 dB LSD60. The all-nonlinear B-R is no closer, so the 3.4 dB is the real-time discretisation's harmonic balance in the limit cycle, not the B2R simplification |
| Stock range with the ×1.5 tables (13 points, new basis) against the shipped tables | Null −56.1 dB at k_eff 0.5, −57.7 dB at k_eff 1.0; LSD60 0.03 dB. **Bytes up to ≈ 85 sound as today** |
| Per block | 1,285 with the 13-point ×1.5 tables (as today); 1,465 on the 32-sample network |

**Open for listening:** in self-oscillation the pitch and level match the
model, but the harmonic balance differs by about 3.4 dB LSD60 (spectral
shape). That is a device listening item, not a cost item.

---

## 6. Drive ×16 (item 6)

- **Law (decided):** `16^drive` = `2^(4 drive)`: ×1 / ×4.0 at mid / ×16.
- **CPU 0** (the same `alk_exp2f()`).
- **Checks:**
  - **AL-D at ×16:** the input-pair argument reaches about 13.5 (6.76 at ×8,
    clamp 3.5); the output stays below full scale.
  - **C2 against the golden model at ×16:** the [5/4] Padé tail accuracy;
    add a ×16 run to the SPICE e2e harness.
  - `ALK_MAKEUP_OCT` by ear on the device (still open from P5).

---

## 7. Samples and wavetables as oscillator types, under Drum (item 7)

### 7.1 What it adds

- **Rows:** `osc1_type` (`typ`: `wav` / `smp` / `slp` / `wtb`, as on Drum).
  In `wav` the `wav` cell keeps Saw/Sqr; in the other types it is the sample
  catalogue index.
  - This is a type-dependent name source for one cell. Drum's `wav`/`typ`
    pair works this way; Alkaline's `wav` uses the ON/OFF tag in `wav` mode
    and Drum's `MOD_WAVE` dynamic range in the others.
- **`pos`** means sample start / frame position in those types (as on Drum)
  and start phase on Saw/Sqr (§8).
- **Render:** a per-block dispatch, with no branch in any loop.

### 7.2 The problem

| Mode, unfused (P3 §9.5 + linked per block) | Loop | Per block / 32 | Total |
|---|---:|---:|---:|
| `wav` (fused, today) | 214 | 38.1 | 252.1 |
| `slp` | 198 + 27 | ≈ 44.5 | ≈ 270 |
| `wtb` plain | 198 + 24–26 | ≈ 44.5 | ≈ 267–269 |
| `wtb` blend | 198 + 34–38 | ≈ 44.5 | ≈ 277–281 |

Per block for the new modes, 1,424 in all:
- the 1,218 of today;
- `osc_setFreq()` ≈ +100 in place of `osc_setTableFreq()`;
- `osc_typedPositionApply()` / `Restore` ≈ +86 (Drum's measured 75 + 11);
- the reader's entry ≈ +20.

### 7.3 The savings

| # | Saving | How | Per sample (est.) |
|---|---|---|---:|
| **S1** | **Fused sample/wavetable reads** | Three new fused B2R loops, as `DiodeLadder_renderB2Osc` fused the table read (that fusion saved 9 per sample, bit-identical, AL-X): one for `smp`/`slp` (Q20.12 sample read), one for `wtb` plain, one for `wtb` blend. Each is a compile-time variant of the same `dl_renderB2Body()` with the reader's inner read; no runtime branch. Flash ≈ +2–3 KB | **−8…−9** |
| **S2** | **Direct pitch for samples** | Alkaline already computes its pitch in log2 Hz each block. For the sample types, the phase increment is the sample's root ratio × that frequency: the same direct calculation as `osc_setTableFreq()`, not `osc_setFreq()`'s generic bind/clamp path. The bind (which sample, root, loop region) is done at `typ`/`wav` change, in the writer, as on Drum | ≈ **−2.5** (+100 → ≈ +20 per block) |
| **S3** | **`pos` without the typed restore** | Alkaline recomputes `phaseInc` every block (pitch from the note), so the bend need not be subtracted after the render. The restore half (11) goes away; the apply can be folded into the pitch update | ≈ **−0.5** |
| **S4** | **Blend premium on the interpolation pool** | Accounted exactly as Drum's blend: a blend renders only with one of the two system-wide interpolation slots (`modNode_interpClaim()`, `OSC_WAVE_INTERP_MAX_ACTIVE` 2); without a slot it steps frames at the plain cost. The Drum figure already excludes the blend for this reason (P3 §1) | the blend premium (≈ +10–12) **off the per-voice figure** |

After S1–S3, per block ≈ 1,218 + 20 + 75 + 20 = 1,333, so 41.7 per sample:

| Mode | Loop (fused) | Per block / 32 | Total | Against 261.3 |
|---|---:|---:|---:|---:|
| `wav` | 214 | 38.1 (+0.8 for `pos`, §8) | **≈ 253** | −8 |
| `slp` / `smp` | ≈ 216–218 | ≈ 41.7 | **≈ 258–260** | −1…−3 |
| `wtb` plain | ≈ 214–216 | ≈ 41.7 | **≈ 256–258** | −3…−5 |
| `wtb` blend | ≈ 225–228 | ≈ 42.5 (plan) | ≈ 268–270, of which **≈ +10–12 on the pool** | per voice as `wtb` plain |

The plain modes fit as Basic, but `slp` only just (−1…−3); these are
estimates until linked. Item 5 was measured at 0 per sample (§5.5), so it
takes nothing from this margin.

### 7.4 What the pool accounting means in use

- At most two oscillators in the whole system blend in any block; they are
  shared with Drum's blends and `itp` fades.
- An Alkaline that wants to blend and finds no slot steps between frames at
  the plain cost, which is Drum's behaviour today. The sound changes from a
  blend to steps, never to silence.
- Claims are sticky: a holder keeps its slot from block to block
  (`modNode_interpReserved`), so a blend does not flicker.

### 7.5 Your slot lock: the reserve

If the linked figures show a plain sample mode over 261.3, the extra goes to
the pool as you suggested:
- **always lock one interpolation slot while an Alkaline runs `smp`/`slp`/
  `wtb`**;
- **lock the second only while it actually blends** (frame position between
  frames).

The pool budget then covers the excess, as it covers a blend today.

The lock differs from today's per-block claim in one way: **a sample voice
cannot fall back to a cheaper sound** the way a blend falls back to steps.
So the lock is taken where the type is chosen, not per block:
- **Grant:** at a `typ` change, Load, Morph endpoint apply or `typ`
  automation step, the writer asks the pool for a **persistent
  reservation**. In `modulationNode.c`, a `modNode_interpLocked` count is
  subtracted from the free slots; **RAM +1 B, approved (user, S093)**.
- **Refusal:** with both slots locked, a third Alkaline cannot enter a sample
  mode. The writer keeps `wav` and the menu shows `typ` refused, the same
  pattern as the two-Advanced rule
  (`instrumentManager_typeSelectableForSceneSlot()`).
- **Release:** at the change back to `wav`, a type change of the slot, or a
  Scene swap.
- **Cost of the lock:** one fewer blend slot for Drum and `itp` while an
  Alkaline sample voice runs. They step instead, as they already do when the
  pool is busy.

**This is only built if §7.6 step 3 shows it is needed.**

### 7.6 Order

1. **Item 5 is measured** (§5.5): 0 per sample, so the margin above stands.
2. **Build S1–S4** and re-measure linked (`control_cost.py --elf`, the loops
   with `elf_looppath.py`): every mode's whole-voice figure, with the blend
   premium reported separately.
3. **Decide** between plain modes as Basic, and the lock (§7.5) for any mode
   over 261.3.

### 7.7 Work (for its own schedule)

- `typ` row, `wav` name source per `typ`, the S2 bind in the `typ`/`wav`
  writer.
- `Alkaline_calcVoiceAsync()`: the direct sample pitch (S2).
- `Alkaline_calcVoiceSyncBlock()`: the per-block dispatch to the four fused
  loops.
- `DiodeLadder.c`: the three fused variants (S1).
- Blend: `modNode_interpClaim()` per block (S4); the lock only if §7.6 says
  so.
- **Tests:**
  - AL-X extended: every fused variant bit-identical to reader + plain B2R;
  - `elf_p4_check`: no other engine's loop changed;
  - G3–G5 for each mode;
  - the pool behaviour: with the slots busy, an Alkaline blend steps.

---

## 8. `pos` on Saw/Sqr (item 8)

### 8.1 Why it needs a phase reset

On the other instruments, `pos` is a phase offset added at the trigger plus
a bend while it moves (INSTRUMENTS_DSP_REFERENCE §6.1.1). Those oscillators
are reset each hit. The Alkaline oscillator **runs free**, as on the 303
(`Alkaline_triggerVoice()` leaves the phase alone), so an offset on top of
an arbitrary phase would not be heard.

### 8.2 Meaning

- **`pos` 0 = free-running:** today's sound, bit for bit.
- **`pos` 1..127:** each **non-legato** trigger sets the phase to `pos`/128
  of a cycle, so every note starts from the same point in the waveform. A
  start at a zero crossing and a start at a peak give a different onset
  click into the ladder.
- **While `pos` moves** (LFO, automation, knob): the per-block bend, as
  elsewhere, so a moving `pos` is a small pitch excursion. With S3 the bend
  folds into the per-block pitch update (Alkaline recomputes `phaseInc`
  every block).
- **Legato notes keep the running phase:** slide is unchanged.

### 8.3 Cost and tests

- **Cost:** +0.8 per sample (the bend in the pitch update; the reset is in
  the trigger, main loop), so ≈ 253.
- **Tests:**
  - `pos` 0 bit-identical to today;
  - `pos` 64 starts every non-legato note at half a cycle;
  - legato keeps the phase;
  - a `pos` sweep ends with no lasting pitch error.

---

## 9. Stored data and documents

- **Meaning changes without migration** (the rule): stored `dec`, `res` and
  `drv` bytes play differently (decay range, ×1.5 feedback, ×16 drive).
- **New rows, appended:** `alk_attack` (26), `osc1_position` (27),
  `osc1_type` (28). `init.alk` is regenerated; the card copy follows.
- **Documents:**
  - `INSTRUMENTS_DSP_REFERENCE.md` §5.5 / §6.8;
  - `FILESYSTEM_SPEC.md` (keys, count);
  - `MIDI_NRPN_TABLE.md` (regenerate);
  - `MIDI_SPEC.md` value notes;
  - `CPU_USE_DSP_AUDIT.md` (the new figures, and the pool accounting for the
    Alkaline blend);
  - the 093 log.

---

## 10. Gates

| Gate | Pass |
|---|---|
| Host | `alkaline` all groups plus AL-A (attack), the updated MEG decay law, AL-R (stock ring at byte 85; bounded self-oscillation at 127), AL-D (×16, with Res full), AL-X (every fused variant), the `pos` laws; `special_tags` (new rows, `init.alk`); `seq_steplen`; `oscillator` |
| Model | `check_c.py` C0–C2 with the updated model (decay constants, wiper gain, ×16); SPICE golden with the wiper E-source and ×16 |
| Static | `control_cost.py --elf` and `elf_looppath.py`: every `typ` mode ≤ 261.3 per voice, with the blend premium reported separately as pool cost; `elf_p4_check` against `tools/dsp_test/baselines/s093p4` |
| Device | Listening (attack after rests, decay range, the resonance top and self-oscillation, drive ×16, samples/wavetables, `pos`); CPU worst case: four Alkalines at Res full, drive ×16, in the dearest plain sample mode, two blends running |

---

## 11. Decisions

| # | Question | State |
|---|---|---|
| P6-D1 | Attack form | **Decided:** trigger-pulse width, exponential kept, byte 0 = today. Ramp only as a later replacement (§2.3) |
| P6-D2 | `dec` label on AEG | **Decided:** unchanged (`Decay` / `dec`); FILTER becomes `cut res env drv` |
| P6-D3 | Decay at min | **Decided:** −20 % (≈ 138 ms) |
| P6-D4 | Resonance | **Decided:** feedback ×1.5 (wiper transformer), self-oscillation allowed, whole-travel law, 13 points; **measured 0 per sample** (§5.5) |
| P6-D5 | Drive law | **Decided:** `16^drive` |
| P6-D6 | Samples/wavetables | **Decided:** Basic with S1–S4; your slot lock (§7.5) as the reserve |
| P6-D7 | `pos` on Saw/Sqr | **Decided:** start phase, 0 = free-running |
| P6-D8 | RAM | **Approved:** +8 B in `AlkVeg` inside the slot (§2.4); +1 B `modNode_interpLocked` if the lock is built |

## 12. Work order

| Phase | Content | Gate |
|---|---|---|
| **P6-1** | Menu moves; `alk_attack` (trigger table, SPICE sweep); decay law (−20 % / +50 %); drive law (`16^drive`); `pos` on Saw/Sqr | Host laws; G3–G5 (≈ 253) |
| **P6-2** | Model: decay constants fitted; ×16; SPICE ×16 and the attack sweep | C0–C2 |
| **P6-3** | Resonance ×1.5 into the tree: the `FeedbackNet` transformer, `gen_alk_tables.py --res-max 1.5`, 13 points, prewarp for k > 1, VR4b clamp, SPICE E/F pair. Feasibility and cost already measured (§5.5) | AL-R (new values), AL-SO, AL-B; C1/C2; G4/G5 unchanged |
| **P6-4** | Samples/wavetables: S1–S4, `typ` row, linked measurement; the slot lock only if a plain mode is over 261.3 | AL-X; G3–G5 per mode; `elf_p4_check` |
| **P6-5** | `init.alk`, docs, NRPN table; device trial | Device |

P6-1, P6-2 and P6-3 are independent of each other. P6-4 (item 7) gets its
own literal schedule, as S093 P4 did.
