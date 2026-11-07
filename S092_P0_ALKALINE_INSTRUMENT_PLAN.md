# S092 P0 — Alkaline Instrument Plan (TB-303 emulation)

- **Session:** 092, phase 0 (assessment and plan; no code changed).
- **Branch:** `dev-ph7-voice_expansion`, HEAD `3abaece5`.
- **Goal:** a fifth Instrument type, **Alkaline** (`alk`), that emulates the
  TB-303 as closely as possible. Its worst-case per-sample cost must be equal
  to or below the worst-case **Drum** setting. Alkaline can then take any
  Basic slot without raising the worst-case Scene (S089 ceiling, audit item 64).
- **Binding documents read:** `MEMORY.md`; `INSTRUMENTS_DSP_REFERENCE.md`
  (§1.1, §5, §6, §8, §9.4, §12); `CPU_USE_DSP_AUDIT.md` (items 50–70, Binding
  Mandates); `089_SESSION_HANDOFF_LOG.md` §4–§5; `ResonantFilter.c`,
  `DrumVoice.c`, `voicePostChain.h`, `Oscillator.c` (`osc_setBaseNote()`),
  `InstrumentManager.c/.h`, `PatternData.h`, `config.h`,
  `tools/plots/plot_stock_waves.py` and its reference capture.
- **TB-303 sources read (P0 revision, 2026-11-07):** the Roland TB-303
  Service Notes, first edition, 19 Feb 1982 (all 8 pages, §3.1 [SN]), and
  Olney, *CT Modular Book* ch. 13 (§3.1 [OL]). Each 303 statement below is
  tagged **confirmed** (read in a source; the page is given), **inferred**
  (my reasoning from a source; not yet checked by simulation or
  measurement) or **open**. §3.3 lists every tag in one table.
- **Circuit model (ngspice):** `S092_P1_VCB_MODEL_VCO.md` (VCO, Q8, VCF input),
  `S092_P2_VCB_MODEL_VCF.md` (VCF), `S092_P3_VCB_MODEL_VCA.md` (VCA),
  `S092_P4_CONTROL_NET.md` (knobs, MEG, accent sweep, expo),
  `S092_P4_SLIDE_ACCENT_RUN.md` (end-to-end pattern run). Results
  are folded into §0, §2.1, §3.2, §3.3, §4 and §9 below, marked
  **modelled**.
- **DSP model:** `S092_P6_DSP_EQUATIONS.md`, with the Python reference in
  `tools/alkaline/`. It covers the equations and constants, the golden and
  real-time solvers, validation against the SPICE models (V1–V6), the D2
  evidence and renders, and a CPU estimate. Its consequences for this plan
  are in its §9 and are folded into §0, §1.2, §3.2, §4, §6 and §9 here.

---

## 0. Summary

| Item | Finding |
|---|---|
| CPU budget | Worst-case Drum ≈ **247 instructions/sample, 4 VDIV**, plus 10 for SRR in the mixer (≈ 257 charged to the slot). An Alkaline core is estimated at **≈ 160–205, 2–3 VDIV**, which leaves about **50–95 instructions and 1–2 VDIV** for extras. **S092_P6 hand count:** core ≈ 145 (option A) or ≈ 230 (option B) with the oscillator (§1.2 update). |
| Where the saving comes from | Alkaline needs one oscillator, not a modulator plus an FM carrier (81 → 24). It has no transient (−27) and no division-based post-chain (35 → ~12). Almost all of this goes to the filter. |
| Filter | The existing 2-pole ZDF SVF cannot sound like a 303. Alkaline needs a **new 4-pole nonlinear diode-ladder ZDF** with a high-pass **inside** the resonance loop. Modelled (P2): the resonance sits about 12× above the ladder's −3 dB point, weakens at low cutoff, and the passband drops as resonance rises. This filter is the main work, the main risk and the main cost. The circuit model reproduces the [SN] p7 calibration with **R110 = 22 kΩ** (x0xb0x KiCad value; the Roland print reads 2.2 kΩ, which cannot resonate like a 303; P2 §2.1). At Res full the loop gain is 98 % of oscillation. |
| Existing `Re3`/`Sw3` waves | **Not suitable as the Alkaline VCO** (§2). They were digitised from a capture of a 303's *filtered output*, so they contain one fixed filter setting (an extra roll-off of about 6 dB/oct; whether it rang cannot be told from the data). Feeding them into an emulated filter would filter the sound twice. |
| Closer existing material | `sawTable` matches the 303 saw: the modelled core ramp is straight to about 0.5 % (P1 §4.1). No existing table matches the 303 square, which is the saw shaped by Q8 into a **pulse whose width and level change with pitch**: about 51 % on-time at C1 to 23 % at C5 (modelled), against [OL]'s 71 % to 45 % (same direction, about 20 points apart; P1 V1). One fixed shape is not authentic (§2.1). |
| Pitch-dependent shaping | The square's pitch dependence comes from Q8's 22 ms emitter network (modelled). It is encoded per octave level of the square table, including its level (D8). The VCF input coupling is 0.72 Hz, so it adds no audible high-pass (modelled); any further high-pass character comes from the ladder loop and the VCA, which are next in P1. |
| Tuning | The 303 Tuning knob spans **about ±700 cents (a perfect fifth)** ([SN] p1), not a fine tune. The existing ±50-cent fine writer is too narrow (D6). |
| Drive into the filter | Mild at the input pair (modelled): R62 220 kΩ / R70 2.2 kΩ divides by about 101, so the saw reaches the Q12 base at about ±1.0 thermal voltage (±0.8–1.5 for the square), about 8 % peak compression. Alkaline calibrates its ladder input to that level; any stronger character comes from the ladder and its resonance loop. This is a constant inside the filter, not a separate distortion stage. |
| Flash | **Tightest resource.** Headroom is 40,980 B. A new square table costs 22,528 B, and the engine plus parameters are estimated at 6–10 KB, which leaves about 8–12 KB (`link_budget` warns below 16 KiB). See decision D3. |
| RAM | 0 B expected: `AlkalineVoice` fits the existing 1,264-B slot union. One type-sized array grows by 2 B. |
| Accent threshold | **Conflict:** `PAT_DEFAULT_VELOCITY` is 100, so with "≥ 100 = accent" every un-edited step is accented. See decision D1. |
| Gate | The sequencer has no gate or note length. The 303's sound depends on its gate. A normal step's gate is **3.5 of 6 clocks** (≈ 58 % of a step; D11, decided). A slide step's gate is held for all 6 clocks plus the next step's gate. Alkaline needs an engine-side gate timer (§5.4). |

---

## 1. Budget

### 1.1 The ceiling to match: worst-case Drum

Figures come from the reference documents (linked-image figures where they
exist). **P1 must re-measure them on the current ELF with
`elf_loopdump.py` and freeze them before any Alkaline code is written**
(§12 mandate: freeze the ceiling figures first).

| Drum stage, dearest setting | Instr/sample | VDIV | Source |
|---|---:|---:|---|
| Oscillator section: `fm`, `slp` carrier ramp twin + modulator | 81 | 0 | 089 log §5.3 (S089 P3) |
| Transient click 22 + saturating mix-in ~5 (T+D) | 27 | 0 | DSP ref §8.1 |
| ZDF filter, LP type (2 batched + `fastTanh`) | ≤ 104 | 3 | DSP ref §6.2, §8.1 |
| Post-chain `voicePost_drum()` (amp ramp × velocity ramp → distortion) | 35 | 1 | audit item 51 |
| **Voice render** | **≈ 247** | **4** | |
| Mixer SRR (`instrumentManager_slotUsesSrr()` = 1) | 10 | 0 | audit item 57 |
| **Charged to the slot** | **≈ 257** | **4** | |

Not included, because they are shared by every type: the mixer dry path,
decimator, FX send, control-rate work, and the system-wide `itp` pool (two
slots). Alkaline does not claim `itp` slots.

### 1.2 Alkaline core estimate

| Stage | Instr/sample | VDIV | Notes |
|---|---:|---:|---|
| VCO: one table wave via the existing plain table loop (gain 1) | 23–24 | 0 | `sawTable` or the new 303 square table; no new loop |
| VCO pitch-dependent high-pass | 0 | 0 | Not needed: the pitch dependence is encoded per octave level (D8), and the VCF input corner is 0.72 Hz (P1 §4.3) |
| Input coupling HPF (1-pole) + fixed ladder drive scale | 5–6 | 0 | 303 AC coupling into the VCF |
| Diode ladder ZDF: 4 coupled stages, per-stage `tanh(x)/x` Padé, feedback-path HPF, per-sample coefficient ramp | 120–160 | 2–3 | Batched reciprocals as in S073 Step 1 |
| VCA: envelope ramp × accent gain ramp + input HPF (corner and gain set per block from Resonance: 66 Hz → 127 Hz, P3 §3.5) | 10–14 | 0 | No division |
| **Core total** | **≈ 160–205** | **2–3** | |
| Mixer SRR | 0 or 10 | 0 | D4: Alkaline may leave out SRR, as a chain type may (S089 P0) |

**Margin against the ceiling: ≈ 50–95 instructions/sample and 1–2 VDIV**
(+10 if SRR is left out). §8 lists the extras this could pay for.

**Update (S092_P6 §5.4):**
- A hand count of the real-time kernels gives, including the network
  around the ladder, the VCA input and the OTA (the last three lines
  above):
  - `A_rt` ≈ 120 + 2 VDIV;
  - `B_rt` ≈ 205 + 3 VDIV.
- With the oscillator, the core is ≈ 145 (A) or ≈ 230 (B).
- The A figure is inside this table's range; the B figure is about 25
  above it.
- The measured count (P2) decides.

### 1.3 Rules that apply

- **Gate (binding):** Alkaline's dearest setting, measured linked, must be ≤
  the frozen Drum dearest setting. Mixer: `elf_hotloops.py` reports 0 loops
  grew, plus an inlined-set diff. Device: worst-case Scene with Alkaline in
  place of the dearest Drum; the `cpu` widget is ≤ baseline with 0 underruns.
- **Constant CPU:** every block renders in full. There is no skip for a
  closed VCA, zero resonance or a silent voice. All per-sample loops are
  branch-free and the same for every setting.
- **The LTO risk (S086 F1):** a fifth engine render inlined into
  `mixer_calcNextSampleBlock()` can change register allocation for the
  other inlined loops. Plan: mark `Alkaline_calcVoiceSyncBlock()`
  `noinline`; one call per block costs nothing measurable. Gate it on
  `elf86_mixer_check` / `elf_hotloops.py` with 0 grew.
- **No state live across a render.** Glide, gate and accent state live in
  the voice struct.
- **Type class:** Basic, if the gate passes. If the measured cost exceeds
  Drum, the fallback is Advanced, which counts towards the two-per-Scene
  limit. That fallback is a design failure to avoid, not a plan.

### 1.4 Memory

| Resource | Today | Alkaline | Notes |
|---|---:|---:|---|
| Flash headroom | 40,980 B (text 712,264 + data 420) | −22,528 (square table, D3) −6…10 KB (engine, ladder, parameters, menu text) | ≈ 8–12 KB left; below the 16 KiB `link_budget` warning |
| ITCM | 15,280 / 16,384 B | 0 | Reuses the existing table loops. The ladder runs from flash, like the ZDF (ITCM for the filter measured worse in S023) |
| SRAM1 `runtime_slots` | 7,584 B (6 × 1,264) | 0 | Estimated `AlkalineVoice` ≈ 450–550 B: 1 `OscInfo` 72, ladder state ~40, envelopes, glide/gate/accent ~40, LFO 124. `_Static_assert` against the reserve |
| `.bss` type-sized arrays | — | +2 B | `menu_instrumentLoadIndex[INSTRUMENT_TYPE_UNKNOWN]` (uint16). Needs sign-off under the RAM policy |
| Static scratch | — | 0 target | Process in place in `buf`. A float block buffer (128 B) would need sign-off |

---

## 2. Assessment of the existing 303 waves (`Re3` / `Sw3`)

**What they are.** `tools/plots/plot_stock_waves.py` digitised
`tools/plots/reference/303_square_saw.png`, kept harmonics 1–40, and
extended them to 511 with a fitted 1/nᵃ roll-off.
`tools/gen_stock_tables.py` then built the `sq3Table`/`sw3Table` octaves.

**Why they are not the raw 303 VCO:**

1. **The saw capture has bumps a raw ramp cannot have.** The `sw3` cycle
   has a bump at about 0.4 and a wiggle at about 0.8 of the cycle after
   each edge. They look like filter ringing, but the harmonic data cannot
   confirm it: fitting the `sw3` spectrum, resonant two-pole, three-pole
   and ladder low-passes do no better than one plain pole (all leave about
   5.8 dB RMS scatter, which is digitisation noise).
2. **Their spectra are already low-passed.** The `sw3` harmonics fall about
   30 dB by harmonic 10 and 45–55 dB by harmonic 20. A raw saw falls 20 dB
   and 26 dB. The `sw3` harmonics 1–40 fit a raw saw with one extra
   low-pass pole at about 2.5 × f0. The `sq3` second harmonic is only about
   4 dB below the fundamental, and its cycle is a decaying hump followed by
   a negative spike, which is not the shape of the modelled Q8 pulse
   (P1 §4.2). So `sq3` is the 303's processed output, not the square itself.
   An earlier note here explained the shape with a 40 Hz input coupling;
   P1 withdraws that (the VCF input corner is 0.72 Hz). The stages that
   shaped it are downstream of the VCF input.
3. **Only about 40 harmonics were measured.** Everything above is a fitted
   extension, so the top of the spectrum is invented.
4. **They are peak-normalised.** `Re3` plays about 8 dB and `Sw3` about
   2 dB quieter than `Rec`/`Saw`.

**Consequence.** As drum waves they are fine: they are "303-flavoured"
fixed timbres. As Alkaline's VCO they would put one filter snapshot in
front of the emulated filter, and the sound could not open up with Cutoff
or Env Mod. **Do not use them for Alkaline.**

### 2.1 The 303 VCO from the service notes

Component references are from [SN] p5 (analog schematic) and p3 (block
diagram). The drawing `tools/plots/303_raw_vco_vs_stock.png` predates this
section: it uses a falling saw and a fixed 47.5 % duty, and it must be
regenerated after P1's circuit simulation.

**Saw core (confirmed topology, [SN] p3/p5):**
- Q26 is a matched 2SC1583 pair, the exponential converter, with R100
  (560 Ω posistor) for temperature compensation and IC11a (AN6562).
- Its current charges C33 (0.01 µF).
- Q24/Q25/Q27 reset C33. The block diagram draws them as the reset switch.
- Q28 (2SK30 JFET, source follower into R105 10 kΩ) buffers the ramp.
  - The x0xb0x build notes describe "the discharge through the JFET". In
    the Roland schematic the JFET is the output buffer; the reset is
    bipolar (my reading of [SN]).
- The scope sketch beside Q28 ([SN] p5) shows a vertical rise to **12 V**
  and a ramp **down** to **5.5 V**, which fits an NPN collector draining a
  grounded C33. **The saw falls.** The rising ramps on the p7 adjustment
  page are a drawing for setting the width.
- **Modelled** (P1 §3.1): about 11.2 V to 5.8 V at the Q28 source, if the
  reset reaches 11.3 V (12 V less D25 and Q27's saturation; inferred). If
  the reset reaches 12 V, the top of the ramp is compressed (P1 §3.2, V2).

**Saw shape:**
- **Modelled:** the core ramp is straight to about 0.5 % (bow), slowing by
  about 5 % as it falls (Q26's Early effect; 2.7–12.5 % across Early
  voltages of 300–30 V). No capture of a real core has been found, so the
  model is **not verified** by measurement.
- **Polarity does not matter:** the ladder's nonlinearity is symmetric, so
  `sawTable` (falling) can be used with a negative gain.
- **Recommendation: reuse `sawTable` (0 B flash)** for the core ramp.

**Square shaper Q8 (confirmed values, [SN] p5):**
- The parts list includes 2SA733(P), the high-gain PNP that the x0x
  community names for Q8. Which transistor sits at Q8 cannot be read in the
  scan.
- **Base:** the saw arrives through R35 (100 kΩ), with R34 (10 kΩ) in
  series with C10 (0.01 µF) in parallel. The second path is a fast edge
  path with τ = 100 µs (about 1.6 kHz).
- **Emitter:** R45 (22 kΩ) to +12 V, with C11 (1 µF) to ground,
  τ = 22 ms (about 7 Hz).
- **Collector:** R36 (10 kΩ) to ground.
- **Scope sketch:** levels marked 8 V / 5 V / GND, with a 9 ms period on
  the A key. The exact swing is to be confirmed by simulation.

**Square duty and pitch:**
- **[OL] measured** about 45 % duty at high pitches and about 71 % at the
  lowest.
- **Modelled (P1 §4.2):** the square is a pulse.
  - The collector sits near 0 V for most of the cycle. Q8 switches on late
    in the falling ramp and off at the reset, and the "on" level rises
    through the pulse.
  - On-time: C1 51 %, C2 38 %, A2 30 %, C3 28 %, C4 26 %, C5 23 %.
  - Level at the pulse top: 4.1 V at C1 to 7.8 V at C5. At the filter input
    that is −2.5 dB (C1) to +3.2 dB (C5) against the saw.
  - **Mechanism:** C11 recharges through R45 (τ 22 ms) between pulses, so a
    longer period gives a longer pulse. Q8's gain hardly matters.
- **Model against [OL]:** the direction matches, but the model sits about
  20 points lower. The threshold definition and the polarity do not
  explain it. This is P1 open question V1.

**High-pass relationship between the waves (confirmed observation, [OL]):**
"each of these waves is very similar to high pass filtered versions of the
other wave". The [OL] patch high-passes both at about 115 Hz, with the
cutoff following pitch. That is a fitted modular-patch value, not a circuit
value.

**Consequences for the tables:**
- A single fixed square table cannot be authentic across the range.
- D8 chooses between two ways to handle the pitch dependence:
  - **(a) Duty per octave level:** each of the 11 table levels already
    serves one octave, so each level can carry its own duty and shape at
    zero runtime cost. This stays within the S086 fixed-table mandate. The
    catch is that duty steps at octave edges, about 5–8 % per octave, which
    needs a listening check.
  - **(b) A pitch-independent table plus a runtime one-pole high-pass at
    the circuit's position:** about 5 instructions per sample, which the
    §1.2 budget includes. The shaper's switching point cannot move at
    runtime without a new per-sample shaper loop, which the S086 mandate
    rules out. So (b) covers the high-pass part only, and the duty still
    needs (a).
- The generator models the Q8 circuit; it does not digitise output
  captures. It is a new line in `tools/plots/gen_stock_tables.py` with
  checks C1–C4.
  - That script's paths assume it lives in `tools/`; they must be fixed
    first (it cannot find `wavetable.c` or the CSV as it stands).
- **Level into the filter:** keep the 303's relative saw/square levels, not
  two peak-normalised tables. The modelled square level changes with
  pitch (−2.5 to +3.2 dB against the saw, C1–C5), so it is baked into each
  octave level's amplitude (D8). The saw table stays as it is, with one
  gain constant.

**Reference data (D5):**
- The schematic is in hand.
- The P1 SPICE model of the VCO (Q26/C33 core, reset, Q28, Q8 network) and
  of the VCF input is built and run (`S092_P1_VCB_MODEL_VCO.md`).
- Raw-VCO captures of a real 303 (Q28 source and Q8 collector, at two or
  three pitches) would verify it and settle V1/V2. Without them, the model
  is checked against [OL]'s duty figures and the [SN] scope sketches only.

---

## 3. What "authentic" means here

The behaviours the emulation must reproduce. Each carries its status:
**confirmed** (with the source page), **inferred** or **open**. Open
figures must be settled before they become constants.

### 3.1 References

- **[SN]** Roland TB-303 Service Notes, first edition, 19 Feb 1982:
  <http://machines.hyperreal.org/categories/sequencers/TB-303/schematics/>
  (p1 specifications; p3 block diagram; p5 analog schematic; p6–p7
  adjustment, higher-resolution scans by R. Whittle; p8 VCF envelope
  modulation and parts list). Read in full for this revision.
- **[OL]** S. Olney, *CT Modular Book*, ch. 13 "TB-303":
  <https://olney.ai/ct-modular-book/tb-303.html>. A modular-synth
  reconstruction checked against Din Sync reference recordings (one C note
  across knob positions; the filter settings are not stated). Source for
  the square duty against pitch, the gate length, and the 18 dB/oct
  behaviour (via Stinchcombe).
- **[XB]** `cozycactus/x0xb0x-vcf-bench` (CC0): a model fitted to an ngspice
  run of an x0xb0x (TB-303 clone) KiCad schematic. Used for part values
  and as a second model to compare against (R110 = 22 kΩ; VEG constants).
  No code is used.
- T. Stinchcombe, analysis of the transistor and diode ladder filters (the
  303 transfer function). Used for the linear frequency-response law.
- V. Zavalishin, *The Art of VA Filter Design* (diode-ladder ZDF
  structure).
- R. Whittle, Devil Fish documentation (stock behaviour and modded ranges).
- Open303 (R. Schmidt) and its KVR development thread: a cross-check of
  constants only. No code is copied.
- Din Sync reference recordings, for A/B listening (D5).

### 3.2 Behaviours

| # | Behaviour | Status and source | Alkaline design |
|---|---|---|---|
| A1 | **VCO free-runs:** note-on never resets its phase. | Inferred: there is no reset path from the gate or trigger to the VCO in [SN] p3/p5. | No phase reset at trigger, unlike Drum. No `pos` or `run` rows. |
| A2 | **Filter state is never cleared** at note-on. | Inferred: no clear path in [SN] p5. | No `SVF_reset()`-style clear. |
| A3 | Cutoff CV is exponential (Q10/Q11 antilog pair): Cutoff + Env Mod × MEG + accent sweep. **The Env Mod pot also lowers the base cutoff** as it rises. **Modelled (P4 §3.2–§3.4):** Cutoff alone spans about 3 octaves (217 Hz → 1.74 kHz at Env Mod 0); Env Mod lowers the rest point by up to 1.4 octaves and scales the sweep from +0.8 to +5.1 octaves. TM3 calibrates to 153 kΩ ([XB] 159 kΩ). | **Confirmed** [SN] p8: "Rotating wiper ② of VR5 closer to terminal ③ increases ENV MOD voltage … which in turn lowers VCF cutoff frequency … equal to turning CUTOFF knob counterclockwise." Q9 sets the base bias. | Control rate: sum in the log-frequency domain, one `exp2` approximation and one `fastTan` per block, then a per-sample coefficient ramp. Clamp f ≤ 0.45 fs. **S092_P6 §4.1, §5.2:** the expo law gives the ladder current directly; the `fastTan` is the TPT prewarp (high cutoffs otherwise run up to 4 semitones flat). |
| A4 | Four-stage ladder. The **bottom capacitor is about half the others**. The resonance feedback has a series coupling capacitor (a high-pass), and the output buffer is differential. Behaves as about 18 dB/oct over much of the audible range. | **Confirmed** [SN] p5: C18 = 0.018 µF (bottom), C19/C24/C26 = 0.033 µF; transistor pairs Q13/Q15, Q14/Q17, Q16/Q23 (diode-connected) on the Q12 driver pair (Roland: "TR. C LADDER"; the literature calls it a diode ladder); feedback through C15 (1 µF); output Q21 pair → Q20/Q19. 18 dB/oct: [OL] citing Stinchcombe. **Modelled** (P2, with R110 = 22 kΩ): loop gain 18.7 against a threshold of 19.1 at Res full; a stable, lightly damped lower resonance at 7–9 Hz (Stinchcombe: "very real", about 8 Hz). | New ladder (§4) with the 0.018/0.033 ratio and the high-pass inside the loop. |
| A4b | **Resonance calibration:** with Cutoff centred, Res full, Env Mod/Decay/Accent at minimum and the saw selected, TP6 shows ringing with a **2 ms ± 0.5 ms period** (about 400–670 Hz) that **decays within each cycle**. | **Confirmed** [SN] p7 (and the p5 sketch: "15 ms KEY C"). Stock maximum resonance rings and decays under this condition rather than sustaining. | Host law AL2b (§7.1): the calibration as a test. Self-oscillation only as an extra (E3). |
| A5 | **MEG:** very fast attack; **decay to 10 % from 200 ms (Decay min) to 2.5 s (max)**. **Modelled (P4 §3.3):** 173 ms / 2.54 s; the same peak on every note (no stacking). | **Confirmed** [SN] p5 ("DECAY VR MAX T=2.5 sec. MIN T=200 ms", 100 %→10 % graph). Consistent with C62 = 1 µF discharging through R138 68 kΩ + VR6 1 MΩ (inferred: 1.07 MΩ × 1 µF × ln 10 ≈ 2.5 s). Attack: C62 charged through D37 + R152 100 Ω (inferred: well under 1 ms). | Control-rate RC envelope; Decay maps exponentially 0.2–2.5 s to 10 %. |
| A6 | **Accent (modelled, P4 §3.5):** at Res 0 a sharp cutoff spike per accent; at Res full a smooth swell peaking about 30 ms after the hit, climbing over consecutive accents (+2.3 dB of current on the second, levelling after three). The Decay bypass lasts while ACCENT is high (assumed one step, P4 K3). Circuit: IC12 (4066) switches the Decay pot out, so the MEG decays through R138 alone (the minimum). Accent × MEG goes through the **accent sweep** network into the cutoff. The **second gang of the Resonance pot** is in that network. Accent also feeds the VCA. | **Confirmed** [SN] p3/p5/p8: sweep network C13 1 µF, R46 47 kΩ, D24; VR4 "50kB x 2" with its second gang to C13; accent level VR7 50 kΩ(B). **VCA accent (user's trace, modelled P3 §3.4):** VR7's wiper → R119 47 kΩ → C36 → R120 22 kΩ → D27 → IC15 pin 1. It adds +5.9 dB (VR7 half) or +11.4 dB (full) at the note's start, gone after about 200 ms. Consecutive accents give rising peaks ([OL]). The accent decay time is **inferred** at about 157 ms (68 kΩ × 1 µF × ln 10) or the printed 200 ms minimum. | Control-rate RC model of C13/R46/D24 with a resistance-dependent gang term; per-block VCA accent gain following the accented MEG, ramped per sample. **S092_P6 §4.3:** one 5-node solve per block for both accent branches, because they share VR7's wiper (separate solves put the first accent's cutoff 27 % high). |
| A7 | **End-to-end run (S092_P4_SLIDE_ACCENT_RUN §4):** (1) The VEG is the C42 state. At normal tempos notes open at once at gate-on (C42 has not recharged); the 2.5–3 ms attack appears only after silence. (2) The VCA accent path is not gated: an accented note keeps sounding after gate-off, following the accented MEG, until ACCENT drops at the step's end. **VEG: the Roland constants are used (D10, decided).** Modelled (P3 §3) with the 12 V gate: attack **2.5–3 ms** (user's figure; it corresponds to a CPU trigger of about 0.65–0.75 ms); decay −6 dB at 1.0 s, −20 dB at 2.7 s, silent at 4.5 s (an exponential with τ 1.5 s, minus an offset); accent +5.9 dB (Accent half) / +11.4 dB (full). Release under 1 µs (D9). The OTA is linear at the 303's level. Circuit as read: a separate envelope (Q31/Q32) into the IC15 BA662A OTA ("vari-conductance amp"), with GATE, TRIGGER and ACCENT inputs. **Attack:** the trigger pulse (through D36, R134 22 kΩ, C41 0.1 µF) turns Q32 on, which empties C42 (1 µF) through R132 (100 Ω), so the attack is very fast. **Decay:** C42 then recharges through R123 (1.5 MΩ), **τ ≈ 1.5 s**, and Q31 (emitter R131 220 kΩ) turns that voltage into the OTA's control current. **Gate and accent** feed Q31's emitter supply through the D34 / R137 1 kΩ / D35 / R133 2.2 kΩ diode network, which has no timing capacitor, so release at gate-off is fast. Decay is not affected by the Decay knob. | Values **confirmed** [SN] p5. The circuit roles and τ are **inferred** from the drawing. D34 sits on the gate line that also feeds the Gate Out jack, specified at +12 V ([SN] p1); a stock 303 has no gate input. [XB]'s constants (τ 2.29 s, smaller accent) were considered and not used (D10). | Control-rate envelope: attack 2.5–3 ms, decay table fitted to the P3 SPICE curve, release per D9, plus the accent gain (A6). **S092_P6 §4.4–§4.5:** the C42 state with a fitted current table; the accent current added ungated. |
| A8 | **Gate:** a normal step's gate lasts **3.5 of 6 clocks** (≈ 58 % of a step): "three pulses and the positive portion of the fourth" ([OL]); a web summary gives 3 clocks. A slide step's gate is held for all 6 clocks plus the next step's gate, and the CV glide runs across that window. | 3.5 decided (D11). | Engine gate timer (§5.4). |
| A9 | **Slide:** RC glide of the pitch CV, **τ ≈ 22 ms** (about 66 ms to 95 %); the slid-to note does not retrigger MEG or VEG. **Confirmed end to end** (P4 run §3.3): 50 % in 15.2 ms, 90 % in 50.7 ms, within 10 cents in 66–81 ms; no trigger and no strike on legato notes. | **Confirmed values** [SN] p5: the pitch DAC is R74–R90, all 200 kΩ (0.1 % matched), in an R-2R pattern (each rung two 200 kΩ in parallel), so its output impedance is ≈ 100 kΩ (inferred from the pattern). IC11b is a unity follower (R107 2.2 kΩ feedback). IC12 (4066) pins 8–9 and 10–11 are driven in anti-phase by Q29/Q30 (R91 1 MΩ). With slide off, C35 (0.22 µF) is tied to the follower output and tracks it. With slide on, C35 is switched onto the DAC node and the CV slews through ≈ 100 kΩ: τ = 100 kΩ × 0.22 µF ≈ 22 ms (inferred; the 4066 on-resistance is negligible). The glide is exponential and linear in volts, so it is exponential in log-pitch. Legato is **inferred** from the gate being held. | Per-block exponential approach in the log-pitch domain, α = 1 − e^(−0.7256 ms / 22 ms) per block, then `osc_setFreq()`. 0 per-sample cost. |
| A10 | **A slide flag belongs to the note slid *from***: step N with slide glides into step N+1. | Confirmed [OL] (the slide gate extends into the next step). | The engine latches `sld` at N's trigger and uses the latch at N+1's trigger (§5.3). |
| A11 | Accent on a slid-to note: no new MEG peak, because there is no retrigger. The accent level and VCA boost apply to the running envelope. | **Confirmed end to end** (P4 run §4 item 2): no new cutoff peak, and the VCA accent follows the running MEG. | Follows from the latch. |
| A12 | Coupling high-passes before the VCF, in the resonance loop, at the ladder output and before the VCA. **Modelled:** VCF input 0.72 Hz (P1); resonance loop/output coupling about 60–120 Hz, inside the loop (P2 §3.1); the VCA input sums TP6 (A, 66 Hz) and VR4's wiper (B, 154 Hz; user's trace), so its corner moves from **66 Hz at Res 0 to 127 Hz at Res full** and its level rises by 10 dB (P3 §3.5); after the VCA 2.9 Hz. | **Confirmed parts** [SN] p5. **VCF audio input:** S1 → R62 → C17 1 µF → Q12 base, R70 2.2 kΩ to the bias node (C16 10 µF). R62 is **220 kΩ** per the x0xb0x documentation (the scan is smudged). **Modelled:** corner 0.72 Hz, so not audible (P1 §4.3). **Resonance feedback input:** buffer Q18 → C23 1 µF → R97 10 kΩ → R98 2.2 kΩ → Q12's other base. Resonance feedback through C15 1 µF and VR4. Ladder output buffer Q21 couples through C25/C27 0.1 µF into R113/R114 100 kΩ. VCA output C38 1 µF; mixer Q33/Q34; output impedance 10 kΩ (p1). **Open:** the effective corner of each, which depends on node impedances (a SPICE job, not a reading). As one-pole RC estimates they range from a few Hz to tens of Hz, which does not by itself explain the 303's low-cutoff resonance loss; that comes from the feedback loop as a whole. | One-pole HPFs (in the §1.2 budget), corners from the P1 simulation. **Superseded by S092_P6 §3.4, §5.3:** separate high-passes do not reproduce the loop. The whole linear network runs as two fast modes and two asymptote poles per sample, with the slow remainder per block. |
| A13 | **Tuning knob ≈ ±700 cents (a perfect fifth).** | **Confirmed** [SN] p1: "TUNING Control: approx. ±700 cents (perfect fifth)"; VR2 50 kΩ(B). | `tun` must span ±700 cents (D6); the existing ±50-cent writer is too narrow. |
| A14 | **Root:** with Tuning centred, the A key plays **110 Hz** and the low C key **65.41 Hz (C2)**. CV out is 1 V/oct, +1 V to +5 V. | **Confirmed** [SN] p5/p7: "Press A key and adjust TM4 for 110 Hz" with TUNE centred; low C to high C is 2:1; KEY C period 15 ms; CV out +1 V to +5 V (p1). | Coarse default **36 (C2)**. Pitch = coarse + (step note − 63), so a default step plays C2. |
| A15 | Tempo range 40–300 BPM; the internal clock is calibrated on the adjustment page. | Confirmed [SN] p1/p6. | Gate timer covers this range (§5.4). |

### 3.3 Still open after the service notes

Resolved from [SN] p5 on a second, closer reading (2026-11-07): the slide
time constant (A9), the VCA envelope attack and decay (A7), and the parts
of every coupling network (A12). What remains is either behaviour a
schematic does not show directly (it needs simulation) or CPU firmware
behaviour.

| Item | How P1 settles it |
|---|---|
| Saw core straightness and reset time | **Modelled** (P1 §4.1): straight to about 0.5 %. Open: the reset level, 11.3 V or 12 V (P1 V2) |
| Q8 output swing, square shape and duty against pitch | **Modelled** (P1 §4.2). Open: the ~20-point duty offset against [OL] (P1 V1) |
| Saw/square level ratio into the VCF | **Modelled** (P1 §3.1): the square runs from −2.5 dB (C1) to +3.2 dB (C5) against the saw |
| Effective high-pass corners | VCF input **modelled** at 0.72 Hz (P1 §4.3). Resonance loop, ladder output and VCA output: P1 VCF/VCA models |
| Resonance-loop behaviour at low cutoff | SPICE of the ladder with C15/VR4 and the Q21 output coupling, checked against A4b |
| VEG release time | **Modelled:** under 1 µs (P3 §3.3); D9 decides the DSP ramp |
| R119, VCA inputs A/B, R123's supply | **Resolved** (user's traces; P3 §6 V1, V2, V4) |
| Gate level at D34 | **12 V** ([SN] p1 Gate Out spec; P3 §3.1). It is built into the chosen VEG constants (D10) |
| VCF output stage (the ×10 loop-gain gap) | **Resolved:** R110 = 22 kΩ (P2 §2.1); all other values confirmed by the user |
| CPU trigger width (VCA attack) | **Resolved by assumption:** attack 2.5–3 ms (user); a trigger of about 0.65–0.75 ms reproduces it (P3 §3.2) |
| VEG constants and release | **Decided:** the Roland/P3 constants (D10); release implementation per D9 |
| Gate pulse count per step (24 PPQN assumption) and slide into a rest | CPU behaviour, not in [SN]: DIN sync documentation, [OL], recordings |
| R62 value (ladder bias, next to C17) | Smudged in the scan; any other copy of p5 |

---

## 4. The filter: design and choice

This is the deciding piece. Three candidates:

| Option | Model | Est. instr/sample | VDIV | Fidelity |
|---|---|---:|---:|---|
| **A** | Linear TPT diode ladder (closed-form ZDF solve) + one `tanh` on the feedback sum + feedback HPF | 90–115 | 2 | Correct small-signal response and resonance shape. Saturation only at the loop input; loses the per-stage diode squelch at high drive. |
| **B (preferred)** | Diode ladder with a per-stage `tanh(x)/x` linearisation around the previous state (the same style as today's ZDF SVF: Padé pieces, batched reciprocals) + feedback HPF | 120–160 | 2–3 | Per-stage nonlinearity, so drive and resonance interact like the hardware. Same numerical style the project already validated in S073. |
| C | Newton–Raphson nonlinear solve (1–2 iterations) | 250–400 | 4+ | Highest. **Does not fit.** |

2× oversampling of A or B roughly doubles the filter cost and does not
fit alongside the rest of the core. It can only be considered as an extra in
place of everything else in §8.

**Decision method (P1):** build A and B as host models (Python for
exploration, then C in `tools/dsp_test`). Compare them with the
Stinchcombe linear response and the reference recordings. Count both with
`fpseq.py` on standalone ARM objects. Choose B if it fits ≤ 160 and ≤ 3
VDIV; otherwise choose A. The user hears both before the choice (D2).

**Host models done (S092_P6 §5–§7):**
- **Option A, as built:** linear rungs, with the input pair and Q21 still
  nonlinear.
- **Option B:** all six nonlinearities (secant / "mystran" linearisation,
  one division for the tridiagonal and one for the loop).
- **The loop:** both close through the whole linear network, not one
  feedback HPF. Its real-time form puts the slow part at control rate.
- **Fidelity:** at the 303's own drive both match SPICE within 0.1 dB per
  step. When driven (E2), B follows the golden reference (LSD 0.22 dB at
  ×8) and A does not (0.78 dB).
- **Renders:** `tools/alkaline/alk_d2_*.wav`.

**Implementation rules:**

- A new function, for example `DiodeLadder_calcBlock()` in a new file
  `Core/DSPAudio/DiodeLadder.c/.h`. **Do not edit `SVF_calcBlockZDF()` or its
  float twin.** The S073 twin rule applies only if a float Effect version is
  ever added.
- State is loaded into locals and written back once per block. No `switch`
  in the loop (one output type). Saturate to int16 once at the end.
- Coefficients are computed per block (one `fastTan` and the derived stage
  gains) and ramped linearly per sample.
- Stage capacitor ratio from [SN] p5: bottom stage 0.018 µF, upper three
  0.033 µF (A4).
- **The loop high-pass comes from the output coupling** (C27/C25 into
  Q21, about 60–120 Hz, P2 §3.1), not from C15 (whose corner is a few Hz).
  It belongs inside the loop.
- "Cutoff" in Alkaline means the **resonance frequency**, about 12× the
  ladder's −3 dB point (P2 §4.3).
- Loop gain at Res full is **98 %** of the oscillation threshold at mid
  cutoffs (P2 §3.1, R110 = 22 kΩ): strong ringing, stable.
- Two tanh nonlinearities: the Q12 input pair (saw at about ±1 Vt) and the
  Q21 output pair inside the loop (P2 §3.4).
- The model has a lightly damped lower resonance at 7–9 Hz (P2 §3.1). It
  is sub-audio, but it affects how the bass and DC settle.
- Acceptance includes the [SN] p7 calibration (A4b, host law AL2b).
- Input drive is a calibrated constant per waveform (§2.1), plus an optional
  user `drv` (§8, E2).

---

## 5. Instrument definition

### 5.1 Registry and files

| Item | Value |
|---|---|
| Type | `INSTRUMENT_TYPE_ALK` (inserted before `INSTRUMENT_TYPE_UNKNOWN`) |
| Token / extension / directory | `alk` / `.alk` / `/Instrument/Alkaline` (8 characters, FAT 8.3-safe) |
| Display label | `Alkaline` |
| Flags | `INSTRUMENT_FLAG_BASIC` (subject to the §1.3 gate) |
| New files | `Core/DSP/Instruments/Alkaline/AlkalineVoice.c/.h`, `AlkalineParameters.c/.h`, `Core/DSPAudio/DiodeLadder.c/.h`, square table in `wavetableStock.c` (generated) |
| Makefile | Voice and ladder in `DSP_SRCS` with an explicit `-Ofast` rule (§9.4 step 4); parameters in the normal sources; include path |

### 5.2 Parameter rows (proposal)

Canonical controls first. Platform rows (vol, pan, LFO pair, velocity) keep
Alkaline consistent with Morph, LFO, automation and storage. Prefer plain
offset bindings (byte or normalised float, converted at control rate) over
new special writers: 23 of 32 writer tags are in use.

| Page | Row (`key` / short) | dtype | Flags | Binding |
|---|---|---|---|---|
| 303 | `alk_wave` / `wav` | `DTYPE_MENU` Saw/Sqr (new menu text) | AUTOMATABLE, SELECTOR | offset `uint8` |
| 303 | `osc1_pitch_coarse` / `coa` (default 36) | `DTYPE_0B127` | M/MOD/A | `IM_SPECIAL_PITCH_COARSE \| OSC1` (existing) |
| 303 | `alk_tuning` / `tun` | `DTYPE_PM63` | M/MOD/A | new writer `IM_SPECIAL_ALK_TUNE`: ±700 cents ([SN] p1, A13), which takes writer tag 24 of 32. The existing `IM_SPECIAL_PITCH_FINE` gives only ±50 cents (D6) |
| 303 | `alk_slide` / `sld` | `DTYPE_ON_OFF` | AUTOMATABLE | offset `uint8` (latched at trigger, §5.3) |
| VCF | `alk_cutoff` / `cut` | `DTYPE_0B127` | M/MOD/A | offset float, mapped to Hz per block |
| VCF | `alk_reso` / `res` | `DTYPE_0B127` | M/MOD/A | offset float |
| VCF | `alk_envmod` / `env` | `DTYPE_0B127` | M/MOD/A | offset float |
| VCF | `alk_decay` / `dec` | `DTYPE_0B127` | M/MOD/A | offset float, mapped to time per block |
| Voice | `alk_accent` / `acc` | `DTYPE_0B127` | M/MOD/A | offset float |
| Voice | `instrument_vol` / `vol`, `instrument_pan` / `pan` | as Drum | as Drum | as Drum |
| Voice | `instrument_decimation` / `srt` | as Drum | — | only if D4 keeps SRR |
| LFO / Velocity | the 12 LFO rows and 2 velocity rows as Drum | as Drum | as Drum | as Drum |

About 25–28 rows. Every row needs its `<TYPE>_PARAM_DESCRIPTOR_COUNT`
assert and `check_special_tags.py` coverage, and every row is on one line.
`IM_SPECIAL_ALK_TUNE` needs its classifier entry (§9.2 of the DSP
reference).
`MIDI_NRPN_TABLE.md` gains the Alkaline rows.

### 5.3 Trigger semantics (accent, slide)

At `Alkaline_triggerVoice(voice, slot, velocity, note)`:

1. `accent = (velocity ≥ ALK_ACCENT_THRESHOLD)` (D1).
2. `legato = voice->slideLatch && gate still held`.
   - **Legato:** set a new pitch target only (glide). MEG and VEG are not
     retriggered. The accent flag updates the VCA accent gain and the
     accent sweep input (A11).
   - **Otherwise:** pitch jumps to the target (no glide), and MEG and VEG
     retrigger. An accent forces the MEG decay to its minimum and charges
     the accent sweep.
3. `voice->slideLatch = voice->slide`, which is the `sld` value at **this**
   trigger. It decides what happens at the **next** trigger (A10). This is
   needed because the automation order is restore → drain → fire (S087 D6,
   S081): by the time step N+1 fires, `sld` already holds N+1's value.
4. Gate: if `slideLatch`, the gate is held until the next trigger
   (time-limited, §5.4). Otherwise it closes after the gate duration.
5. No oscillator phase reset and no filter clear (A1, A2).

Rolls: each roll hit is a trigger and follows the same rules. A held `sld`
(knob on, no automation) makes every note legato, like an all-slide 303
pattern.

### 5.4 Gate timer (new; the sequencer has no gate)

- At each trigger, the gate length in blocks = 3.5/6 of the track's
  effective step duration (D11; A8) ÷ 0.7256 ms. The duration comes from a new
  read-only sequencer query (for example `seq_trackStepDurationBlocks(track)`
  built on `seq_getBpm()` and `seq_effectiveTrack*[]`; it never writes the
  Pattern region). The trigger funnel (`voiceControl_fireTrigger()` →
  `instrumentManager_triggerTrack()`) passes it in, or InstrumentManager
  asks for it. The engine does not call the sequencer itself.
- With `slideLatch`, the gate is held for the whole step plus the next
  step's gate (A8). If no next trigger comes, it closes at that time:
  6 + 3.5 = 9.5 clocks, about 1.58 steps.
- Live presses and MIDI with transport stopped use the current BPM.
- External clock uses the estimated BPM.
- Cost: control rate only. One countdown per block.

### 5.5 Platform interactions to settle

- **Track 7 alternate (slot 6):** non-Choke types borrow a hidden decay. For
  Alkaline, map it to the MEG decay (proposal).
- **`run` / `chn` / `pos` / `typ` rows:** not offered. Alkaline has one
  fixed chain.
- **Velocity modulation:** still available as a target, in addition to the
  accent decision.
- **Morph:** all continuous rows are Morphable. `wav` and `sld` are
  selectors.
- **Storage:** registry-driven paths work as they are. Audit the hard-coded
  type lists (for example the default Scene stage `filesystem.c:18242`) and
  every `[INSTRUMENT_TYPE_UNKNOWN]`-sized array. Load/Save type stepping,
  `.hcindex` per type, HCNAMES type fields, `kit` snapshot and boot-reader
  types all need tests.
- **No backward compatibility:** a new enum value before `UNKNOWN` changes
  the numeric value of `UNKNOWN`. Check that no stored byte holds `UNKNOWN`
  as data; if one does, it changes meaning, and per policy there is no
  migration.

---

## 6. Phases

| Phase | Work | Exit gate |
|---|---|---|
| **P1 — Freeze and model** (models done: `S092_P1`–`P4`, the end-to-end run, and the DSP equations with host models A/B in `S092_P6`; still open: the ceiling freeze, the `gen_stock_tables.py` paths, D2 and D8) | Re-measure and freeze the Drum ceiling on the current ELF (`elf_loopdump.py`, mixer snapshot). SPICE model of the VCO and Q8 from [SN] p5 (§2.1, §3.3), then the VCF ladder, MEG/accent and VCA. Trace the open [SN] paths (slide R, VEG discharge, couplings). Fix the `gen_stock_tables.py` paths. Host models: filter options A and B, MEG/VEG/accent sweep, glide, gate. | Ceiling frozen. Q8 simulation reproduces [OL]'s 45 %/71 % duty, or the difference is explained. §3.3 closed or accepted as estimates by the user. User hears A vs B renders (D2) and chooses D8. |
| **P2 — DSP core** | Square table (generator + checks, D3). `DiodeLadder.c` (chosen option). Standalone ARM counts. | Ladder ≤ 160 instr and ≤ 3 VDIV standalone. Host laws AL1–AL4 pass with breaks. |
| **P3 — Engine and type** | `AlkalineVoice.c` (init, trigger, async, render `noinline`), parameters and menu pages, registry, every dispatcher case (§3.2 of the DSP reference), accessors for the special writers, Makefile. | Builds. Special-tag check passes. Flash ≥ the agreed floor (D3). Slot `_Static_assert` holds. |
| **P4 — Sequencer integration** | Step-duration query, gate timer, slide latch, accent threshold (D1), track-7 mapping. | Host laws AL5–AL7 pass. |
| **P5 — Gates** | Linked counts and mixer gate (§1.3). Storage round trips. Hardware worst-case Scene. Listening A/B. | All of §7 green. User sign-off on sound and `cpu` widget. |
| **P6 — Extras** | Chosen §8 items, inside the margin measured in P5. | Re-run the P5 gates. |

---

## 7. Tests

### 7.1 Host (`tools/dsp_test/modules/test_alkaline*.c`; every law has a break proof)

| Law | Checks |
|---|---|
| AL1 | Square table: band limit per level, peak, DC, `--check` regenerates byte-identically. Per-level duty follows the simulated duty-against-pitch curve (D8 a). Saw/square level ratio as simulated. |
| AL2 | Ladder, linear: small-signal magnitude response against the Stinchcombe transfer function (tolerance set in P1) across cutoff. Resonance peak against `res`. Low-cutoff resonance loss from the feedback HPF. |
| AL2b | [SN] p7 calibration: saw at C2, Cutoff centre, Res max, Env Mod/Decay/Accent min. Output rings with a 2.0 ± 0.5 ms period and decays within each cycle (no sustained oscillation). |
| AL3 | Ladder, nonlinear: full grid of cutoff × reso × drive × max input gives bounded output with no NaN/Inf. Self-oscillation behaviour at maximum `res` matches the chosen stock reference. Coefficient ramp produces no block-rate step (spectral check at 1,378 Hz and its harmonics). |
| AL4 | MEG decay to 10 %: 200 ms at `dec` min, 2.5 s at max ([SN] p5). Accent forces the minimum decay. VEG: attack 2.5–3 ms; decay −6 dB at 1.0 s, −20 dB at 2.7 s, silent at 4.5 s (P3 §3.1, ±0.5 dB to the table); accent +5.9 / +11.4 dB at Accent half / full (P3 §3.4); release per D9. Accent sweep: three consecutive accents raise the cutoff peak at high `res` and not at low `res`. |
| AL5 | Slide latch: slide on N affects N→N+1 only. No MEG/VEG retrigger on legato. Glide time constant. Slide into an empty step releases within the limit. |
| AL6 | Accent threshold edge (D1 value − 1 and D1 value). |
| AL7 | Gate length is 3.5/6 of the step and tracks BPM (40–300) and track scale; under a slide latch it is held for 6 + 3.5 clocks. |
| AL9 | Pitch: Coarse 36 with Tuning centred plays 65.41 Hz, and step note +9 plays 110 Hz ([SN] p7). Tuning spans ±700 cents. |
| AL8 | Constant cost: the per-sample loops are branch-free (`fpseq.py`), and the same loops run for every setting. |

### 7.2 ARM and build

`fpseq.py` (standalone), `elf_loopdump.py` (linked Alkaline loops against
the frozen Drum ceiling), `elf_hotloops.py` + inlined-set diff
(mixer: 0 grew), `elf86_mixer_check`, `special_tags`, `stock_tables`,
`make all && make img`, the flash budget and ITCM unchanged.

### 7.3 Device (added to `OPEN_TESTS.md` in P5)

- Worst-case Scene with Alkaline replacing the dearest Drum; a Scene with 6×
  Alkaline at the dearest setting: `cpu` widget ≤ baseline, 0 underruns.
- Listening A/B against the reference recordings: saw and square cutoff
  sweeps at three resonance settings, Env Mod and Decay sweeps, accent runs,
  slide runs, accent + slide.
- Save/load `.alk`, Kit/Scene/Bank round trip, AutoSave restore, Load/Save
  type stepping, Morph, LFO and step automation on every row, MIDI
  velocity → accent, NRPN rows.

---

## 8. Extras (inside the margin of about 50–95 instructions and 1–2 VDIV)

| # | Extra | Per-sample cost | Fits? |
|---|---|---:|---|
| E1 | **Post-VCA overdrive** using the shared `distortion_curveSampleF()` (the usual "303 into a pedal" sound; Drum already pays for it) | +22–25, +1 VDIV | Yes |
| E2 | **Filter input drive** `drv` (scales the calibrated ladder input) | ~0 (one constant becomes a parameter) | Yes |
| E3 | **Devil Fish-style ranges:** adjustable slide time, accent decay, VEG decay, wider MEG decay, resonance up to self-oscillation, filter key tracking | 0 (control rate) | Yes |
| E4 | **MIDI note-off ends the gate** (`voiceControl_noteOff()` is a stub for instruments today) | 0 | Yes |
| E5 | **Tie** (hold the gate without a glide) as a second step flag | 0 | Yes |
| E6 | Sub-oscillator or second table voice | +24 | Yes, but not authentic |
| E7 | 2× oversampled ladder | +120–160 | Only as the sole extra, and probably not even then |

Recommended set: **E1 + E2 + E3 + E4**. Estimated total ≈ 185–230 against
the ≈ 257 ceiling, still with a margin. E5 depends on the Pattern format
(a new step special would need its own plan).

---

## 9. Decisions for the user

| # | Decision | Recommendation |
|---|---|---|
| **D1** | Accent threshold, given that the default step velocity is 100 | Use **> 100** (101+) so un-edited steps are unaccented, or keep ≥ 100 and accept that every default step is accented. |
| **D2** | Filter option A vs B | **Decided (user, S093): B-R, fitted under the Drum ceiling as a Basic type, for a listening trial** (`S093_P1_ALKALINE_OVERHEAD_REDUCTIONS.md`: B-R with the [3/2] OTA tanh, 230 + 24 oscillator = 254 against Drum's ≈ 257). After listening, either reduce it further, or make it Advanced (replacing Cymbal or HiHat in a Scene) and spend the margin on post-distortion/FX types. Implementation brief: `S093_P2_ALKALINE_B-R_PRE_IMPLEMENTATION.md`. Earlier inputs: S092 P6 §7, S093 P0 §6. |
| **D3** | Flash for the square table (22,528 B of 40,980 B headroom) | Either accept it and go below the 16 KiB warning, or **replace `Re3`** with the authentic square. Replacing is flash-neutral, but it changes every Kit that uses `Re3`, and per policy there is no migration. |
| **D4** | SRR (`srt`) on Alkaline | Leave it out (saves 10 per sample). The 303 has no sample-rate reduction, and the Effect can provide it. |
| **D5** | Reference material | The schematic is in hand ([SN]); P1 simulates from it. Still wanted: raw-VCO captures of a real 303 (Q28 source and S1, two or three pitches) and the Din Sync recordings for A/B. |
| **D6** | Tuning range | **±700 cents, as [SN] p1 states.** Needs a new writer (`IM_SPECIAL_ALK_TUNE`), using writer tag 24 of 32. |
| **D7** | Extras for P6 | E1 + E2 + E3 + E4. |
| **D9** | VCA release at gate-off (the Roland circuit cuts in under 1 µs, P3 §3.3) | **Decided (user, 2026-11-07): a one-block per-sample ramp (0.73 ms).** Inaudible as a fade and follows the platform's ramp rule; a one-sample step (as the circuit does) may click |
| **D10** | VEG constants | **Decided (user, 2026-11-07): the Roland/P3 constants.** Attack 2.5–3 ms; decay τ 1.5 s with offset (−6 dB at 1.0 s, silent at 4.5 s); accent +5.9 / +11.4 dB. [XB] (τ 2.29 s, accent +9.5 dB) was considered. Its decay is slower than R123·C42 allows, so it is an x0xb0x constant, not a Roland one (P3 §4) |
| **D11** | Normal gate length | **Decided (user, 2026-11-07): 3.5 of 6 clocks** ([OL]'s wording). A one-constant change if recordings say otherwise (A8) |
| **D8** | How the square's pitch-dependent duty and shaping are made (§2.1) | (a) Each octave level of the square table carries its own pulse width, rising top and level, sampled from the P1 SPICE model once V1 is settled (0 runtime cost). (b) A runtime one-pole high-pass is not needed for the VCO: the VCF input corner is 0.72 Hz. Listening check at the octave edges. |
