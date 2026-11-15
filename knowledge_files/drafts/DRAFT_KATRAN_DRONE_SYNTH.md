# Katran, the Seventh Instrument: drone and noise synth (draft)

- **Status:** draft, revision 5, written 2026-11-14 from the tree at
  `d6a9c9b7` (S095 P4 landed). Nothing in the source was changed.
  - Figures marked *measured* come from the tree, from `build/lxr02.elf`
    as built after `d6a9c9b7`, or from records in
    `tools/dsp_test/baselines/`. Figures marked *est.* are estimates from
    comparable loops in the tree. Every estimate is measured before a
    schedule is written.
  - **Decided so far: §12.1. Still open: §12.2.** Nothing open blocks
    the host model (§13 step 1).
- **History:**
  - revision 1: the patch translated as it stands;
  - revision 2 (user): the patch never had an audio input; treat it as a
    starting point; add stereo spread, thickening and filtering (§3);
  - revision 3 (user): the answers to K-Q1 to K-Q7 (K-D1 to K-D7);
  - **revision 4 (user, 2026-11-14): fit the slot reserve** (K-D8).
    - The oscillators share one `typ` and one `wav`, on the platform's
      `osc1_*` rows.
    - Pitch and `pos` are independent per oscillator. OSC A has its own
      spread rows (`dtn`, `psp`).
    - This removes the second `OscInfo`, the second wavetable bind and the
      second-oscillator platform work.
    - The scratch blocks are cut from five to four.
    - The voice is now ≈ 1,100 B *est.* against 1,264 B, with no new RAM,
      and is **targeted at ≤ 1,200 B linked**, a little under the reserve
      (user).
  - **revision 5 (user, 2026-11-14): K-Q16 and K-Q17 answered** (K-D9,
    K-D10).
    - **The transport stop engages the loop break.**
    - The break **slowly ramps down the cores' feedback** (and their
      inputs), so the sound winds down with its character intact, instead
      of cutting the inputs and leaving a clean comb decay.
    - A `wtb` frame change takes effect at the oscillator's next cycle
      start.
- **Brief (user, 2026-11-14):** a seventh instrument type, **Katran**, for
  drone and ambient tones. Its core follows a noise-synth patch the user
  built on the Roland AIRA Modular TORCIDO with the AIRA Modular
  Customizer.
- **Sources:**
  - `TORCIDO_Noise_Synth_patch.json`: the modules, their knob positions
    (visual estimates) and the user-confirmed routing. One cable was
    confirmed later and is missing from the JSON: **TORCIDO out 1 → AMP2
    in** (user, 2026-11-14);
  - `patch.png`: the Customizer screenshot;
  - `TORCIDO_Noise_Synth.bin`: not used.
- **Binding rules that apply:**
  - CPU ceiling (S089 P2): a new type is held to its class budget,
    measured in cycles per block on the device (S094 P2 §21: Basic 19,620,
    Advanced 21,803);
  - per-block work counts (S093);
  - constant CPU: the dearest setting is the one budgeted, and every loop
    runs every block;
  - `INSTRUMENTS_DSP_REFERENCE.md` §9.2 (the add-a-type checklist) and §12
    (oscillator code rules);
  - RAM and ITCM growth need sign-off;
  - the type registry is append-only, with no backward compatibility;
  - no step in this draft edits `MEMORY.md` or `knowledge_files/`. §14
    lists the statements it would make out of date.

---

## 0. Summary

| Item | Proposal |
|---|---|
| **The core (the patch)** | Two oscillators, a two-channel distortion (TORCIDO), a short feedback delay, a ring modulator, an unstable patch LFO and a VCA pair. The delay's output hard-resets the exciter that feeds it, so the loop re-clocks its own source (§1.2) |
| **Dual core** | The whole feedback loop runs **once per side**. Each side has its own exciter, TORCIDO pair, loop filter and delay line. `spr` spreads the loops; `dtn` and `psp` spread the exciters (§3.1) |
| **Oscillators** | **One shared `typ` (`wav` / `wtb` / `pwm`) and one shared `wav`** (the form, or the table). **Pitch and `pos` per oscillator.** `pos` is the PWM width or the wavetable frame, and replaces the patch's colour knobs (§5.1) |
| **More distinct** | Per-side delay drift, cross-feedback between the cores, a noise exciter, a mirrored patch LFO on the right core, and a clean sub-oscillator (§3.2) |
| **Filtering** | A resonant SVF per core **in the feedback path**. It takes over TORCIDO's tone and can track the loop's pitch. Plus a loop low-cut, and a stereo output SVF pair with an envelope amount and an anti-phase LFO sweep (§3.3) |
| **Loop break** | A break gain slowly ramps down both cores' feedback and inputs over about **`bkd`** seconds, so the sound winds down with its character intact. It engages from the `brk` row, and **whenever the transport stops** (§5.4) |
| **Drone behaviour** | The graph runs all the time; the Colloid's amp envelope and sustain switch open a window onto it (§6) |
| **Cost** | ≈ 360 instructions per sample *est.*, in the Colloid's range (382, host). **The class follows the Colloid's first device measurement** (§9): Basic if it fits, otherwise Advanced. A trim ladder is in §9.2 |
| **Delay memory** | Two FxBuffer voice units, one 50 ms ring per core: the units' first caller (§5.3) |
| **RAM** | **0 B new.** The voice is ≈ 1,094 B *est.* (≈ 1,148 B with the Colloid's estimate error applied), **targeted at ≤ 1,200 B**: a little under the 1,264 B slot reserve. In-slot reserves of ≈ 100–120 B more back the target (§10.1) |
| **ITCM / flash** | 0 B ITCM proposed; ≈ 11–15 KB flash *est.*, against 37,020 B headroom (*measured*) |
| **Platform** | No new platform work beyond what the Colloid's type needed: one switchable oscillator, the stereo slot path, the existing helpers with a Katran branch (§11) |

---

## 1. The source patch

### 1.1 Modules and routing (user-confirmed)

The patch was never run with an audio input; its Audio Input 1 cables are
left out.

```
                    GRF1 (pitch)       GRF5 (colour)
                          |                  |
  SQR sync out --sync-->  SAW OSC --> TORCIDO in2 --> out2 --+
  Delay out ----sync-->                                      +--> SHORT DELAY in
  Delay out ----------------------> TORCIDO in1 --> out1 ----+          |
                                                     |                  +--> out --> TORCIDO in1
                                                     |                           --> SAW sync
                                                     +--> RING in (carrier)
                                                     +--> AMP2 in                (user, 2026-11-14)
  SQR out ---------------------------------------------> RING mod in
                                     RING out --> Out1, Out2, AMP1 in, LFO mod in
  AMP1 out --> Out2        AMP2 out --> Out1

  LFO cv  --> AMP1 offset, TORCIDO tone, TORCIDO tube warm, SQR colour
  LFO inv --> AMP2 offset, DELAY feedback, SQR fine, LFO mod in (itself)
  GRF2 --> SQR pitch and DELAY time        GRF3 --> TORCIDO tone
  GRF4 --> TORCIDO dry/wet                  GRF6 --> RING on/off
```

### 1.2 How it makes its sound

1. **The core is a distorted feedback delay.** The delay's output feeds
   TORCIDO input 1, and TORCIDO output 1 feeds back into the delay. With
   enough gain the loop self-oscillates at a pitch set by the delay time (a
   comb).
2. **The saw is the loop's exciter and is never heard directly.** It goes
   only into TORCIDO input 2, whose output goes only into the delay.
3. **The loop re-clocks its own exciter.** The saw is hard-synced by the
   square **and** reset by the delay's output. The loop's output decides
   when its input restarts. This is where the chaos comes from.
4. **The outputs are asymmetric:**
   - **Out 2** is the ring (the loop × the square) plus AMP1, which is the
     ring again, swelling with the LFO;
   - **Out 1** is the ring plus AMP2, which is **the loop itself before the
     ring** (TORCIDO out 1), swelling with the inverted LFO.
5. **The LFO is unstable on purpose.** Its rate is modulated by the audio
   output and by its own inverted output. Its two outputs, in anti-phase,
   move eight destinations together.

### 1.3 Patch values (visual estimates, mapped to 0..127)

These seed `init.kat`. The JSON gives every knob as a visual estimate of
low or medium-low confidence; the host model (§13 step 1) sets them by ear.

| Module | Knob | Patch % | `init.kat` |
|---|---|---:|---|
| (both oscillators) | | | `typ` pwm, `wav` Rec (see below) |
| SQR → **OSC B** | range / colour / fine / level | 16' / 47 / 50 / 43 | `rng` 16' / `pos` 60 / `fin` 64 / `lvl` 55 |
| SAW → **OSC A** | range / colour / fine / level | 32' / 36 / 50 / 39 | `rng` 32' / `pos` 46 / `fin` 64 / `lvl` 50 |
| LFO | wave / mod in / rate / output | 42 / 45 / 58 / 40 | 53 / 57 / 74 / 51 |
| AMP | 1 offset / 1 level / 2 offset / 2 level | 42 / 67 / 42 / 38 | 53 / 85 / 53 / 48 |
| TORCIDO | dist / tone / level 1 / level 2 | 44 / 42 / 47 / 50 | 56 / 53 (the loop filter's cutoff, §3.3) / 60 / 64 |
| | tube warm / dry-wet | 58 / 45 | 74 / 57 |
| Short Delay | time / feedback / dry-wet | 58 / 48 / 50 | 74 / 61 / 64 |
| Ring Mod | on/off | on | on |

- **The shared `wav` is a trade-off** (K-D8). The patch had a saw exciter
  and a square modulator; Katran's two oscillators play the same form.
  - **`init.kat` uses Rec**, because OSC B's square is what the listener
    hears directly, in the ring's sidebands. OSC A, the exciter, is heard
    only through the distorted delay, so a pulse there changes the sound
    least.
  - Under `pwm`, each oscillator's own `pos` still sets its own width.
  - The host model renders Saw and Rec as the shared form, and the `init`
    is chosen by ear.
- **The additions start neutral** (`spr`, `dtn`, `psp`, `drf`, `xfb`,
  `nse`, `sub` 0; filters open; `brk` off), so the first sound is the patch
  itself. The two cores start from different phases, so it is already
  slightly wide.

---

## 2. Translating a modular patch to an LXR voice

| # | Patch | Katran |
|---|---|---|
| T1 | **GRF 1–6**, external bipolar CVs | **Ordinary parameter rows** (§8.2): morphable, LFO- and velocity-modulatable and step-automatable |
| T1a | GRF2 drives the square's pitch **and** the delay time | The delay time row `tim` plus a tracking amount `dtk`: the delay time follows OSC B's pitch offset (0 = independent) |
| T2 | **Feedback cables.** Patch cables have no latency in concept, but the patch contains loops | Modules run once per 32-sample block, in signal order. **A connection that runs backward in that order carries one block (0.73 ms) of delay.** The patch has two: ring out → LFO mod in (control rate anyway), and the delay's dry path inside the loop, which becomes a **32-sample tap** (K-D5, §5.3) |
| T3 | AMP2's input | TORCIDO out 1; its gain CV is the LFO's inverted output (K-D2) |
| T4 | TORCIDO's lo boost and bass switches | Not carried over. The loop low-cut and the two filters (§3.3) cover that shaping with rows |
| T5 | The patch LFO | Part of the voice, computed per block. The **platform LFO** (`lfo_*` rows) stays, as on every instrument, and can target any Katran row |
| T6 | Out 1 / Out 2 | Voice **L / R**, through the Colloid's stereo slot path (`instrumentManager_slotRight()`, `InstrumentManager.h:864`). The voice's pan is the mixer's per-slot pan (the Colloid's C-D11) |
| T7 | A saw and a square | **OSC A** (the exciter) and **OSC B** (the ring's modulator), sharing `typ` and `wav`, each with its own pitch and `pos` (K-D4, K-D8) |
| T8 | Knob scales | 0..127 rows. Every response curve is a draft law, set by ear (K-Q11) |

---

## 3. What Katran adds

### 3.1 The dual core and its spreads (K-D1, K-D8)

| Element | Per side? | Spread control (draft depths) | Cost of the second copy (*est.*, per sample) | Value |
|---|---|---|---:|---|
| **Delay line** | **Yes** | `spr`: time × (1 ± 0.12 × `spr`/127), L longer, R shorter | ≈ 22, + one voice unit | **The largest.** Two combs at different pitches; each side's loop rings and resets on its own schedule |
| **OSC A (exciter)** | **Yes** (each side's delay resets its own) | **`dtn`**: ± 0..25 cents (L sharp, R flat). **`psp`**: `pos` ± 0..16 (a width spread under `pwm`, neighbouring frames under `wtb`) | ≈ 27 | High: the exciters beat against each other across the field |
| **TORCIDO, both channels** | **Yes** (part of each loop) | none (shared knobs) | ≈ 28 | Needed for a second loop |
| **Loop filter** (§3.3) | **Yes** | `spr`: cutoff ± 0..1 octave | ≈ 24 | High: each side's loop sings at a different resonance |
| **Ring modulator** | **Yes** (each side rings its own loop) | none | ≈ 4 | Needed for a second loop |
| **Output filter** (§3.3) | **Yes** | `spr`: cutoff ± 0..½ octave | ≈ 20 | Medium |
| **OSC B** (the ring's modulator) | **No** (an option, §3.4) | | ≈ 23 | The shared OSC B keeps the ring's pitch centred, and its sync pulse drives both exciters |
| **Patch LFO** | One LFO, with a stereo mirror `lst` (§3.2) | | 0 | High, and free |
| **Sub-oscillator** | **No**: the bass stays centred | | – | |

- **The oscillator spreads sit with the oscillator** (OSC page), as the
  Colloid's `dtn` and `psp` do. `dtn` and `psp` act on OSC A's two copies
  only.
- **`spr` spreads the loops** (the delay times and both filters' cutoffs),
  each destination with a fixed depth set by ear. It is the one knob to
  automate or morph for "wider".
- At every spread 0 the two cores share every setting. They start from
  different phases, so the chaos still separates them slowly. `wid` 0
  (§5.8) gives true mono.

### 3.2 Elements that thicken it and make it distinct

| # | Element | What it does | Cost (*est.*) |
|---|---|---|---|
| A1 | **Delay drift** `drf` | Each core's delay time wanders on its own slow random walk (smoothed `GetRngValue()` steps per block, different for each side). This gives tape-like wow and slowly moving combs. 0 = still | 0 per sample (the time is already ramped per sample); ≈ 20 per block |
| A2 | **Cross-feedback** `xfb` | A share of each core's delay output is fed into the other core's delay. At 0 the loops are independent. Higher values couple them: they pull towards each other, ping-pong, and at the top lock into one wide sound | ≈ 2 per side |
| A3 | **Noise exciter** `nse` | Hardware-RNG noise added to each side's OSC A before TORCIDO channel 2. One 32-bit read per sample gives a 16-bit value per side. The RNG is the platform's accepted noise source (S073) | ≈ 4 |
| A4 | **LFO stereo mirror** `lst` | The right core takes the patch LFO's modulation scaled by `(1 − 2 lst')`. At 0 both cores move together, as in the patch. At 127 every per-core modulation is inverted on the right: tone, tube, feedback, the cutoffs | 0 per sample; a few per block |
| A5 | **Sub-oscillator** `sub`, `sbo` | A clean sine one or two octaves below the note, mono and centred, added after the output filter. It reads the 1,024-point sine (`wavetable.h:58`, `TABLESIZE` 1024). It fades with the loop break (§5.4) | ≈ 12 |
| A6 | **Stereo width** `wid` | A mid/side gain on the output: 0 = mono, 64 = as rendered, 127 = wider | ≈ 4 |
| A7 | **Sync threshold** `sth` | How loud a delay peak must be to reset OSC A. High: only the strongest peaks reset (calmer, more pitched). Low: every ripple resets (noisier) | 0 per sample |

### 3.3 Filtering: where and how

```
 OSC A + noise --> TORCIDO ch2 --+
                                 +--> [ F1 loop SVF ] --> [ F2 low-cut ] --> x gate --> DELAY write
 D ------------> TORCIDO ch1 ----+                          (+ fb x D, + xfb x D_other x gate)
                       |
                       +--> RING x OSC B --+--> (1 + g1)            --> R side    (per core)
                       +-------------------+--> + g2 x TORCIDO out 1 --> L side
                                                 |
                       [ F3 output SVF, L and R ] --> + SUB --> width --> env --> out
```

**F1, the loop filter (one per core, in the feedback path; K-D7):**
- The Colloid's linear TPT state-variable filter (`ColSvf`,
  `col_svfControl()`, `ColloidVoice.c:347-411`), with LP / BP / HP
  (`lft`), cutoff `ton` and resonance `lrs`. It has no drive of its own:
  TORCIDO drives it already.
- **It takes over TORCIDO's tone knob.** In the patch, TORCIDO's one-pole
  tone already sits inside the loop. Making it resonant and selectable turns
  that knob into the voice's main shaping control: GRF3 and the LFO's cv now
  sweep a resonant loop filter. It also removes four one-pole tone filters
  (≈ 12 per sample).
- **It is in the loop, so it shapes what the loop sustains:**
  - LP: each pass darkens the repeats;
  - BP: the loop rings at the filter's frequency, and the self-oscillation
    locks to it;
  - HP: a thin, metallic loop.
- **Loop tracking `lkt`:** the cutoff follows the loop's own pitch (1 /
  delay time), at `ton` semitones above it. With BP and high resonance it
  picks one harmonic of the comb, and keeps it as the delay time moves
  (`tim`, `dtk`, drift). 0 = absolute cutoff. Per block only.
- It sits on the delay write, so it filters everything in the ring, the
  32-sample dry tap included.

**F2, the loop low-cut `lct` (one per core):** the DC blocker with its
corner made a row, 5 to 600 Hz (a one-pole high-pass). It keeps the tube
stage's DC and low rumble from building up in the loop, and it thins the
drone from the bottom. It costs nothing extra: the DC blocker had to be
there anyway.

**F3, the output filter (L and R):**
- The same TPT SVF form, one per side, after the VCA sums and before the
  sub. Rows: cutoff `cut`, resonance `ors`, type `oft` (LP / BP / HP /
  notch).
- **Envelope amount `fen`:** bipolar; the amp envelope also moves both
  cutoffs. 0 per sample. A separate filter ADSR is an option (K-Q9).
- **LFO depth `flf`:** the patch LFO moves the left cutoff with `cv` and
  the right one with `inv`: a slow anti-phase sweep across the field.
- **Spread:** `spr` sets the two cutoffs apart.
- Coefficients per block, ramped per sample (the Colloid's C-D9). The two
  filters share one type, so their mix coefficients are stored once.

### 3.4 Considered, not recommended now

| Option | Cost | Why not now |
|---|---|---|
| A separate `wav` for OSC B (say Saw exciter, Rec modulator, as the patch) | 1 B under `wav` / `pwm`; + 16 B and a second bind under `wtb`; a Katran key branch in the `wav` helpers | Removed in K-D8 for simplicity. It is the first thing to add back if the shared form sounds wrong in the host model |
| OSC B per side, detuned | +23 per sample | The two loops already differ; it would push the dual core past the Advanced line (§9) |
| Exciter unison: two OSC A per side | +50 per sample | The same reason. Drift, `psp` and noise thicken the exciter for less |
| Wavefolder as a TORCIDO shaper type (`voiceFold`) | ≈ +30 per sample | The constant-CPU rule charges it even when unused. A candidate for a later Advanced expansion |
| A separate filter ADSR | ≈ +1 per sample (per block only); 4 rows, ≈ 12 B | Cheap (K-Q9) |
| Delay at half rate (100 ms per unit) | +6 per side | Lo-fi and aliased; 50 ms already reaches a 20 Hz comb |

---

## 4. Signal path and render order

```
per block (async, control rate)
  c1  patch LFO: rate x 2^(lfm' x (a x ringPrev + inv)), shape -> cv, inv;
      right-core modulation = (1 - 2 lst') x left-core modulation
  c2  oscillators: the wavetable bind (shared table); OSC A L/R increments (+/- dtn),
      rows, pos (A's pos +/- psp); OSC B increment (+ inv on its fine), row,
      pos (B's pos + cv)
  c3  TORCIDO: drive gain, tube amount (tub + cv), levels 1 and 2, dry/wet
  c4  loops (per core): delay time target (tim + dtk x B pitch, +/- spr, + drift) -> slew
      -> per-sample step; feedback (fbk + inv); F1 coefficients (ton + cv, tracking,
      +/- spr) -> per-sample steps; F2 coefficient;
      loop break: engaged (brk while running; the stop edge; released by a stopped
      trigger) -> gb x k_b, or back towards 1 -> per-sample step (§5.4)
  c5  output: VCA gains (g1 from cv, g2 from inv); F3 coefficients (cut + fen x env
      +/- flf x LFO +/- spr) -> per-sample steps; ring on/off target; width; sub fade
  c6  amp envelope step

per sample (sync, 32 samples; staged loops, §9.3; scratch blocks D_L, D_R, B, A)
  L1  both cores:  D_s[n] = wet x ring_s[w - T_s(n)] (fractional) + dry x ring_s[w - 32]
  L2  B[n] (the shared typ's read form); bWrap[n] (the phase-add carry)
  per core s = L, then R:
  L3  A[n] (core s's OSC A); reset if  P x bWrap[n] + D_s[n]  crosses sth upward
  L4  t1 = TOR(D_s[n]);  t2 = TOR(aLvl x A[n] + nse x noise_s[n])       (t1, t2 in registers)
      ring_s[w] = SAT16( gb[n] x (F2(F1_s(t1 + t2)) + xfb x D_other[n] + fbk_s[n] x D_s[n]) )
      r = t1 x ((1 - k) + k x bLvl x B[n])                     (k: ring on/off ramp)
      core L:  outL[n]  = r + g2[n] x t1                        (Out 1: ring + AMP2(TORCIDO out 1))
      core R:  right[n] = r x (1 + g1[n])                       (Out 2: ring + AMP1(ring))
  L5  output, in place on outL and right: F3 per side;  + sub x subFade;  width;  x env;
      int16 stores (outL is the mixer's block, right the voice's)
  end of block: ringPrev = (r_L[31] + r_R[31]) / 2
```

- **Why the delay is read first.** With a delay of at least one block (32
  samples), each core's delay output for the whole block is already in its
  ring at the block's start. The exciters can take their resets from it,
  and cross-feedback can read the other core's output, before any write.
- **Minimum delay time: 32 samples** (0.73 ms, a comb up to 1,378 Hz).
- **The internal feedback reads `D`** (the dry/wet mix), not the timed tap
  alone. This keeps one block per core instead of two; the difference is
  part of T2's model.
- **The core outputs go straight into the two output blocks** (the
  mixer's L block and the voice's `right[32]`), and L5 works on them in
  place. So `t1` and the ring need no blocks of their own.
  - The output blocks are int16 in the mixer's convention. L5 reads them
    with a headroom scale chosen with the normalisation constant (by ear).
  - If the host model shows the int16 step before F3 is audible, two float
    blocks (256 B) would replace that. They fit the margin in §10.1, but
    only just.

---

## 5. The modules

### 5.1 Oscillators: one shared type, own pitch and position (K-D4, K-D8)

**The rows.** Both oscillators follow the platform pattern that the
Alkaline and the Colloid use (the Colloid's C-D20), with `typ` and `wav`
shared:

| Row | `wav` | `wtb` | `pwm` |
|---|---|---|---|
| `typ` (shared) | the band-limited stock rows | a user wavetable | the PWM forms |
| `wav` (shared) | Saw Rec Tri Sin | the installed tables, `wNN` (one table for both) | Sin Tri Saw Rec |
| `pos` (**per oscillator**) | `---` (no effect) | that oscillator's frame | that oscillator's width (pos 0 = 50 %, 127 = narrowest; the S094 P4 law) |

- **Shared `typ` and `wav` are the platform's `osc1_type` and
  `osc1_wave`.** They are written through the existing
  `IM_SPECIAL_OSC_TYPE` writer with a Katran branch, as the Colloid has.
  The `wav` range and text come from `instrumentManager_waveDomainMax()`
  (`InstrumentManager.c:869-905`) and `menu_formatWaveCell()`
  (`menu.c:12269`), each with a Katran branch beside the Colloid's. Katran
  can use the Colloid's lists (`COL_WAVE_COUNT`, `COL_PWM_WAVE_COUNT`) as
  they stand.
- **OSC A's `pos` is `osc1_position`.** OSC B's is a Katran row,
  `kat_b_pos`, plain 0..127, converted with the same law.
- **The PWM forms are the shipped ones, restated as the Colloid did**
  (`ColloidOsc.h`):
  - Saw and Rec: the two-read form (`osc_pwmReadD()`, `Oscillator.c:3811`:
    sign +1 Saw, −1 Rec);
  - Sin and Tri: the phase warp (the WS/WT forms).
  - Saw under `pwm` is the "tone for the saw": `pos` moves it from a saw
    towards an octave-up saw.
- **`pos` is the patch's colour:** GRF5 is OSC A's `pos`, and the LFO's
  cv moves OSC B's `pos`. `psp` spreads OSC A's two copies around their
  `pos`.
- **`pos` under `wtb`:** a new frame takes effect at the oscillator's
  **next cycle start** (its phase wrap, or for OSC A its sync reset), so a
  moving `pos` never cuts a cycle in half (K-D10).
- **One wavetable bind,** shared by both oscillators (one table, own
  frames), checked once per block as `col_wtbBind()` does.
  `osc_userBind()` is never called.
- **Katran's loops are its own**, not the Colloid's. The Colloid's loops
  sum oscillator pairs into mid/side buses and have no resets; Katran's
  write blocks and take a reset per sample. Per form (plain read, two-read,
  warp) there is an OSC A pair loop (L and R, own increments and resets)
  and an OSC B single loop that also emits its wrap carry.
- **Constant cost:** the dearest form is charged. The Colloid's host counts
  put the two-read and warp forms level (`pwm6` 324.1, `pwmw6` 326.1 per
  sample, *measured*).
- **Pitch, per oscillator:** the note + `rng` (64' / 32' / 16' / 8' / 4' /
  2', −3 to +2 octaves) + `coa` (continuous semitones, ±24: the GRF CV) +
  `fin` (±50 cents). OSC A's `coa`/`fin` are `osc1_pitch_coarse`/`fine`;
  OSC B's are Katran rows. Both track the note (K-Q10). `dtn` detunes OSC
  A's two copies.
- **Level, per oscillator:** OSC A's `lvl` is the exciter's level into
  TORCIDO; OSC B's `lvl` is the ring's depth.
- **Sync:** a hard reset to phase 0, branchless (a compare and a
  conditional select per OSC A per sample), on an upward crossing of `sth`
  by `P × bWrap + D_s`. OSC B has no sync input.
- **Aliasing:** a hard reset steps a band-limited row, and a user wavetable
  is not band-limited per octave (as on every `wtb`). Accepted for a noise
  voice.
- **Free-running:** no phase is reset by a trigger. The phases are seeded
  from `GetRngValue()` at init, differently per side.

### 5.2 TORCIDO (two channels per core, shared knobs)

```
x -> x * g_dist -> shaper -> tube warm -> * level_ch -> dry/wet with x
```

- Channel 1 takes the core's delay output, channel 2 its exciter (plus
  noise). `dis`, `tub` and `mix` are shared; `lv1` and `lv2` are per
  channel. This draft treats the AIRA's two LEVEL knobs as output levels
  (K-Q12). That makes **`lv1` the loop's feedback gain** and **`lv2` the
  exciter's level into the loop**.
- **Shaper:** the cubic soft clip `y = x' − (4/27) x'^3`, `x'` clamped to
  ±1.5 (the Colloid's division-free drive), or the shipped
  `distortion_curveSampleF()` (`distortion.h:102`). The spike measures
  both. Both bound the output, which keeps the loop stable (§7).
- **Tube warm:** an even-order stage, `y = s + τ (s² − s_dc)` (draft law),
  with `τ` from `tub` + LFO cv. F2 removes the DC it adds.
- **Tone:** moved into F1 (K-D7). **Dry/wet:** `mix` (GRF4).

### 5.3 Short Delay (one per core; K-D3, K-D5)

- **Memory: two FxBuffer voice units per Katran** (`FxBuffer.h:93-100`;
  2,208 int16 samples each, 50 ms at 44.1 kHz), one ring per core. They are
  claimed with `fxbuf_voiceAcquire()` and read with `fxbuf_voiceUnit()`.
  Each ring lives in one unit, so the first-fit claim (`FxBuffer.c:311-318`,
  which does not keep a slot's units adjacent) causes no split reads.
  - **Claim** when the type is assigned to a slot; **release** when the
    slot leaves Katran; never on a parameter change (the S095 P5 §4.4
    rule). The owner clears what it claims, outside the render.
  - **The Effect share shrinks** by 8,832 B per Katran: two Katrans leave
    108,992 B of 126,656 B; six (if Basic) leave 73,664 B. The Space
    Reverb's 64 KB ring fits either way. The Delay and CrumpBit `dly`
    maximum times shrink with the share, as designed.
- **Time:** `tim`, log law from 32 samples to 50 ms (draft), plus `dtk`
  (OSC B's pitch), `spr` and `drf`. The target is slewed per block and
  ramped per sample, so a time change bends the pitch like tape. The read
  is fractional (linear interpolation).
- **Feedback:** `fbk` + the LFO's inverted output, clamped (draft ≤ 0.95).
  Cross-feedback `xfb` adds the other core's output. The write saturates
  to int16.
- **The dry path is the 32-sample tap** (K-D5): `ring_s[w − 32]`, read
  from the ring itself. `dmx` crossfades between a 0.73 ms comb and the
  timed tap, which gives the loop a second, high resonance. It costs one
  more integer read per core and no memory.

### 5.4 The loop break (K-D6)

**What it does.** A break **slowly ramps down both cores' feedback** (user,
K-D9). The sound winds down over about `bkd` seconds with its character
intact (still distorted, still re-clocking, still modulated) until it is
silent. The oscillators keep running underneath. When the break ends, the
cores' gain comes back up, the exciters refill the delays, and the drone
builds up again within a few delay times.

**How: one break gain per voice, `gb`, from 1 (closed) to 0 (silent):**
1. **What it scales.** In each core, both of the delay's inputs:
   - **the feedback:** `fb × D` becomes `fb × gb × D`;
   - **the input:** TORCIDO's outputs, through F1 and F2, and the
     cross-feedback. This carries the exciters and the external loop
     through TORCIDO channel 1.

   With both scaled, nothing can keep the loop alive, and its level falls
   at least as fast as `gb`.
2. **Down, slowly.** While a break is engaged, `gb` falls exponentially:
   - per block, `gb ← gb × k_b`, with `k_b = 10^(−3 × 32 / (fs × bkd))`
     from a 128-entry table (the `colEgK[]` pattern; no `exp` at run
     time);
   - so `gb` reaches −60 dB at `bkd`. Below 1e-4 it snaps to 0;
   - it ramps per sample inside each block, like every other gain, so the
     ramp-down has no steps.
3. **Back up, quickly.** When the break ends, `gb` returns to 1 over
   ≈ 20 ms (draft), so the drone restarts promptly without a click.
4. **Everything heard follows.** The ring (TORCIDO out 1 × OSC B) and the
   AMP2 path (TORCIDO out 1) both come from the decaying `D`, so they wind
   down with it. The sub, which does not come from the loop, is scaled by
   the same `gb`.
5. **`bkd`:** 0.5 to 30 s, log (draft law, set by ear).

**When it engages (K-D9):**

| Situation | Break |
|---|---|
| Transport running | follows the `brk` row (stored default: off) |
| **The transport stops** (the running → stopped edge, read once per block from `seq_isRunning()`) | **engages**, so the drone dies out slowly instead of stopping dead or droning on |
| A trigger while stopped (a pad, a MIDI note, the preview) | **released** for that note, so the note plays the full drone. It ends through its amp envelope, which plays A-D-R while stopped (the Colloid's C-D19) |
| The transport starts | follows `brk` again |

**The amp envelope at a stop (a departure from the Colloid).** The
Colloid releases its envelopes on the stop edge. Katran does not:
- a held note keeps its envelope level at the stop edge, so the slow
  ramp-down is what you hear;
- when `gb` reaches 0, the envelope is set idle, so the voice reads quiet
  (`instrumentManager_ampEnvelopeQuiet()`);
- a note that was already releasing goes on releasing.

The previous running flag is 1 B in the voice, as in the Colloid.

**In a Pattern:**
- a step lock of `brk` on lets the drone wind down from that step;
- the next triggered step's reset restores the base value (off), so the
  next note brings it back up. A step with automation but no trigger still
  queues its entries (`PATTERN_DYNAMIC_STACK.md` §6), so a lock on an empty
  step works;
- with `sus` on, a break winds the sound down inside a held note: an ending
  that comes from the sound, not from the envelope.

**Cost:** one multiply for the input and one folded into the feedback
ramp, ≈ 2 per core per sample. Per block: one table multiply, the edge
test and the ramp step, ≈ 15.

### 5.5 Loop filter and low-cut

See §3.3, F1 and F2: per core, F1 then F2, on the summed TORCIDO outputs,
before the break gate.

### 5.6 Ring modulator (one per core)

- `r_s = t1_s × bLvl × B`.
- **On/off** (`rmo`, GRF6): off passes the carrier `t1_s` through. The
  switch is a one-block crossfade, so it never clicks. Step locks on `rmo`
  give the rhythmic switching that GRF6 gave the patch.

### 5.7 The patch LFO

- **Per block.** Outputs `cv` and `inv = −cv`, both scaled by `lfl`; the
  right core's copy is scaled by `(1 − 2 lst')` (§3.2 A4).
- **Shape `lfw`:** sine / tri / saw / square / random. **Rate `lfr`:**
  0.02 to 20 Hz, log. Both are draft (K-Q13).
- **Mod in `lfm`:** the rate is multiplied by `2^(lfm' × (a × ringPrev +
  inv))`. `ringPrev` is a block-rate sample-and-hold of the output: the
  jitter it gives is the character the audio-rate modulation gave the
  patch. The `inv` term is the self-modulation that skews the waveform.
- **Destinations,** as the patch, each with a fixed depth set by ear and
  `lfl` scaling them all:
  - `cv`: AMP1, the loop filter cutoff (was the tone), the tube, OSC B's
    `pos`;
  - `inv`: AMP2, the delay feedback, OSC B's fine pitch, the LFO's own
    rate;
  - plus `flf` on F3.

### 5.8 Output stage

- **The VCA pair (K-D2):** `g1 = a1l × clamp(a1o + cv, 0, 1)`, `g2 = a2l ×
  clamp(a2o + inv, 0, 1)` (draft law), both ramped per sample.
  - **L (Out 1)** = ring + AMP2, whose input is TORCIDO out 1:
    `o_L = r_L + g2 × t1_L`.
  - **R (Out 2)** = ring + AMP1, whose input is the ring:
    `o_R = r_R × (1 + g1)`.
  - Each side takes its own core's signals. The patch's asymmetry is kept:
    the raw loop fades in and out on the left as the ring swells on the
    right.
- Then, in place on the two output blocks: F3 per side, the sub (mono)
  with its break fade, the width (`wid`, mid/side), the amp envelope, the
  normalisation constant, and the int16 stores.

---

## 6. Notes, the envelope and the trigger

- **The graph never stops.** The oscillators, both loops and the LFO run
  every block, whether or not a note is sounding. The amp envelope opens a
  window onto a patch that keeps evolving. This costs nothing extra: the
  constant-CPU rule renders every block anyway.
- **Amp envelope:** the Colloid's lightweight ADSR and its sustain switch,
  with the same rules (C-D8, C-D19):
  - rows `atk dec slv rel` / `sus`;
  - `sus` on holds at the sustain level while the transport runs; `sus`
    off, or a stopped transport, plays A-D-R;
  - the reuse: `colEgK[]` is already extern (`ColloidTables.h:43`).
    `col_egStep()` is static in `ColloidVoice.c:301`. Moving it to a
    shared file is the implementer's choice, gated on the Colloid staying
    bit-identical;
  - the time table is extended to ≈ 30 s for drone attacks and releases
    (a Katran table, by ear).
- **Two ways a sound ends:** the envelope closes the window (`sus` off,
  release), or the loop break lets the sound itself wind down (§5.4). They
  combine freely.
- **A transport stop** engages the break. A held note keeps its envelope
  level and winds down with the loop, rather than releasing on the stop
  edge as a Colloid does (K-D9).
- **The envelope's second use:** `fen` sends it to F3's cutoffs (§3.3).
- **A trigger** starts or retriggers the envelope from its current level,
  and sets the note that the oscillators and the sub track. It does not
  touch the loops or the phases.
- **Velocity:** the platform velocity rows (`dst amt`).
- **Quiet test:** a Katran branch in
  `instrumentManager_ampEnvelopeQuiet()`: the envelope is idle. The loops
  may still run underneath, inaudibly. That is what makes the next note
  start inside an already-moving sound.

---

## 7. Stability

- **Each loop is bounded by construction:**
  - the shaper limits each TORCIDO channel's output to its level;
  - the delay write saturates to int16;
  - the feedback is clamped;
  - F2 keeps DC out of the loop.
- **The loop filter's resonance** raises the gain at one frequency inside
  the loop; the shaper and the saturating write still bound it. A
  resonating BP loop is meant to self-oscillate.
- **Cross-feedback** adds the other core's bounded output. `fbk + xfb` is
  clamped as one sum (draft ≤ 1.1, since the shaper limits anyway); the
  host model checks it.
- **The break** always ends in silence: `gb` scales every input to the
  delays, the feedback included, and falls to 0.
- **Constant cost:** every loop runs every block. Ring off still
  multiplies, `nse` 0 still adds, `xfb` 0 still reads, `gb` multiplies at
  1, and `wav` runs the dearest form's budget.

---

## 8. Parameters and pages (draft)

### 8.1 Pages

Eight SELECT pages of up to 16 cells each, shown as four-cell screens
(`INSTRUMENT_MENU_PAGE_CELLS` 16, `InstrumentManager.h:359`). New keys take
the `kat_` prefix. The shared oscillator rows and OSC A use the `osc1_*`
platform rows; OSC B uses `kat_b_*` rows.

| Page (SELECT) | Screens (4 cells each) | Notes |
|---|---|---|
| 0 OSC → **OSC A (exciter)** | `typ wav` / `rng coa fin pos` / `lvl dtn psp` / `nse sth` | `typ`, `wav` shared (`osc1_type`, `osc1_wave`); OSC A `osc1_pitch_coarse`, `osc1_pitch_fine`, `osc1_position`; its spreads |
| 1 AEG | `atk dec slv rel` / `sus fen brk bkd` | the envelope, and the two ways a sound ends |
| 2 mod | `dst amt` / `lfw lfr lfm lfl` / `lst` | velocity; the patch LFO and its stereo mirror |
| 3 FM → **OSC B (modulator), RING, SUB** | `rng coa fin pos` / `lvl rmo` / `sub sbo` | `kat_b_*`; it plays the shared `typ`/`wav` from page 0 |
| 4 click → **LOOP** | `dis tub lv1 lv2` / `mix tim fbk dmx` / `dtk xfb drf` | TORCIDO and the delays |
| 5 FILTER | `ton lrs lft lkt` / `lct` / `cut ors oft flf` | F1, F2, F3 |
| 6 LFO | the platform LFO (the Colloid's layout) | |
| 7 MIX | `vol pan spr wid` / `a1o a1l a2o a2l` | the loop spread, the width and the VCA pair |

- About **50 Katran rows** beyond the platform's.
- **Selectors:**
  - `typ`: tag 3, the existing names, through `osc1_type`;
  - `rng` ×2: one new tag;
  - `lfw`, `sbo`: new tags;
  - `lft`, `oft`: tag 6 (`ONOFF_TAG_COL_FILT_TYPE`) if they offer the
    Colloid's filter types;
  - `rmo`, `sus`, `brk`: plain on/off.

  Tags 1, 2, 3, 5 and 6 are defined (`MenuText.h:183`, `:213`, `:240`,
  `:286-287`); 4 and 7–15 have no definition. Katran needs two or three.
- **`brk`** is `AUTOMATABLE | SELECTOR`: step-lockable, never morphed (as
  `sus`).
- **Continuous rows** are `MORPHABLE | MODULATABLE | AUTOMATABLE`, converted
  at control rate in the async, so no new special writers are needed (the
  Colloid's pattern).

### 8.2 Where the GRFs went, and the performance rows

| GRF | Patch destination | Katran row |
|---|---|---|
| 1 | SAW pitch | OSC A `coa` |
| 2 | SQR pitch and delay time | OSC B `coa`, with `dtk` linking `tim` |
| 3 | TORCIDO tone | `ton`, now the loop filter's cutoff |
| 4 | TORCIDO dry/wet | `mix` |
| 5 | SAW colour | OSC A `pos` |
| 6 | Ring on/off | `rmo` |

From the additions, `spr`, `dtn`, `drf`, `xfb`, `cut` and `brk` are the
next candidates for Morph, the platform LFO and Pattern automation.

---

## 9. Cost

### 9.1 Composition (*est.*, instructions per sample, dearest settings)

| Part | Per sample | Basis |
|---|---:|---|
| L1 two delay reads (timed fractional + 32-sample tap, mixed) | 28 | Space tape tap (`delayRun`) less its filters and drive |
| L2 OSC B, dearest form, with its wrap carry | 24 | the Colloid's PWM single, 23 (*measured*, S095 P2 spike) |
| L3 OSC A pair, dearest form, own resets and sync sums | 54 | the Colloid's PWM pair with own widths, 44 (*measured*), + resets |
| Noise exciter | 4 | one RNG read split L/R |
| L4 TORCIDO ×4 (gain, cubic, tube, level, mix) | 56 | ≈ 14 per channel |
| F1 ×2 + F2 ×2 | 48 | TPT SVF ≈ 20 (the Colloid's spike `f2_one`, 26 with drive), one-pole 4 |
| Delay writes ×2 (break gate, feedback, cross-feedback, saturate) | 20 | |
| Ring ×2 with the on/off crossfade; the two VCA sums; the core output stores | 16 | |
| L5 in place: F3 ×2, sub, width, envelope ramp, two stores | 74 | the Colloid's parallel output loop, 72 (*measured*, `f5_par`), less its drive, plus the sub and the int16 reads |
| **Loops** | **≈ 324** | |
| Per block ≈ 1,350 (patch LFO, drift, three `exp2`s, the break gain, six filter coefficient sets with ramps, one wavetable bind, slews, envelope, unit pointers, seven loop entries) / 32 | ≈ 42 | the Colloid's per block is 2,116–3,172 (*measured*, `baselines/s095p3/colloid_cpu.txt`) |
| **Total** | **≈ 366** | |

### 9.2 Class, and the trim ladder

- **Katran's closest relative is the Colloid:** 382.1 per sample on the
  host (`plain12` stereo, *measured*), with **no device cycle count yet**
  (S095 P5 §2.2: the probe run is P5's step 1).
  - At the Alkaline's measured ≈ 1.9 cycles per instruction (S094 P2 §18,
    with 3 VDIV per sample), 366 is ≈ 22,000 cycles of render plus a stereo
    send (≈ 2,100–3,000). That is over both budgets.
  - At ≈ 1.5 (no VDIV per sample, delay reads from DTCM), it is ≈ 17,500
    plus the send, ≈ 20,000: under Advanced, just over Basic.
- **The Colloid's probe run decides Katran's class** (§13 step 0). It
  costs nothing extra: P5 needs it anyway.
  - **The dual core (K-D1):** Basic if the measurement allows. Otherwise
    **Advanced** (21,803; at most two per Scene, shared with the planned
    Advanced Alkaline and Colloid).
  - If even Advanced is short, the ladder below applies up to step 4.
    Step 5 gives up the dual core and is your call.
- **The trim ladder** (each step *est.*), applied in order until Katran
  fits:

  | Step | Saves | Leaves |
  |---|---:|---:|
  | 1 Drop the sub-oscillator | ≈ 12 | ≈ 354 |
  | 2 Drop the noise exciter | ≈ 4 | ≈ 350 |
  | 3 TORCIDO channel 2 to a bare cubic (no tube, no dry/wet) | ≈ 12 | ≈ 338 |
  | 4 F3 to a one-pole tilt per side (no resonance) | ≈ 35 | ≈ 303 |
  | 5 *(needs your decision)* A single core: the patch's mono loop with F1, F2, F3, the break and the stereo output stage | ≈ 125 from the full set | ≈ 240: Basic for certain |

- Under S089 the unused margin is not headroom for other types; the ladder
  only takes Katran down to its line.

### 9.3 Staged loops

- **Staged** loops (§4): each has few live values, so none should spill
  (the S086 lesson). The price is a 32-sample delay minimum and four
  scratch blocks (§10.1).
- **Fused** (one per-sample loop per core) allows a delay down to one
  sample and needs fewer blocks, but carries ≈ 20 live values and will
  spill.
- The spike measures both forms.

---

## 10. Memory

### 10.1 RAM: inside the slot reserve

`runtime_slots[6]` (`InstrumentManager.c:226`) reserves 1,264 B per slot.
The Colloid uses 1,112 B (`:223`).

| Member | Bytes (*est.*) |
|---|---:|
| `OscInfo osc` (the shared `typ` / `wav`, OSC A's pitch and `pos`: the `osc1_*` rows and writers) | 72 |
| Oscillator state: OSC A ×2, OSC B, sub (phase, increment, row, width or frame row) | 64 |
| Wavetable bind, shared (pointer, frames, index, generation) | 16 |
| `Lfo lfo` (the platform LFO) | 124 |
| Patch LFO, two drift walkers | 36 |
| TORCIDO coefficients (shared) | 24 |
| F1 ×2: state 2 × 8, coefficients and steps 2 × 24, shared mix 12 | 76 |
| F2 ×2 | 12 |
| F3 ×2 (the same layout) | 76 |
| Delay ×2 (unit pointer, write index, time now / step) + shared feedback, cross-feedback, wet / dry | 48 |
| Break (`gb` now / step, `k_b`) | 12 |
| Ring crossfade, VCA gains and steps, width, envelope ramp | 48 |
| `ColEg` amp envelope, sustain, break and running-edge flags | 16 |
| Parameter members (≈ 45 rows outside the `OscInfo`) | ≈ 150 |
| Scratch blocks (int16): `D_L`, `D_R`, `B`, `A` (A reused by each core in turn) | 256 |
| `right[32]` (the voice's right output block; also L5's work block) | 64 |
| **Total** | **≈ 1,094** (≤ 1,264; ≈ 170 to spare) |

**The target: a little under the reserve, not at it.**
- **The line:** the linked `sizeof(KatranVoice)` **≤ 1,200 B**, which
  keeps at least 64 B (≈ 5 %) of the reserve free. This is the spike's
  gate (§13 step 2).
- **The estimate, calibrated.** The Colloid's draft estimated its voice at
  ≈ 1,060 B (`S095_P2_DRAFT_COLLOID.md` §7.1); it linked at 1,112 B
  (`InstrumentManager.c:223`), 4.9 % over. The same error on Katran's
  1,094 B gives **≈ 1,148 B**. That is under the 1,200 B line by ≈ 52 B,
  and under the reserve by ≈ 116 B.
- **If the linked size is over 1,200 B,** the implementer applies the
  in-slot reserves below, in order, until it is under. All three stay
  inside the slot, so none needs new RAM or a sign-off.

**What revision 4 removed (≈ 176 B):**
- OSC B's `OscInfo`: −72 B;
- the second wavetable bind: −16 B;
- the `t1` scratch block: −64 B. It stays in registers, and the core
  outputs go straight to the output blocks (§4);
- the filters' shared mix coefficients: −24 B.

**The in-slot reserves,** in order:
1. **`B` in `right[32]`:** core L reads `B[n]` and writes the mixer's L
   block. Core R reads `B[n]` and writes `right[n]` at the same index, in
   place. −64 B, reaching ≈ 1,084 B calibrated;
2. **parameter members kept as bytes** and converted in the async rather
   than stored as floats: ≈ −40 to −60 B.

Together they give ≈ 100–120 B more, so even a 15 % estimate error stays
under 1,200 B. (`A` cannot borrow an output block the same way: core R
needs its own `A` while the L block already holds core L's output.)

**RAM summary:**
- **No new static, DTCM or SRAM allocation.** The spike's gate is the
  linked `sizeof(KatranVoice)` against the reserve, with its
  `_Static_assert`.
- **The delay lines** are two voice units, 8,832 B per Katran, from the
  existing FX arena (DTCM): approved (K-D3).

### 10.2 ITCM and flash

| | Bytes (*est.*) | Where |
|---|---:|---|
| The loops: three oscillator forms × (OSC A pair, OSC B single), the TORCIDO / loop-filter loop, the delay reads, the output loop | ≈ 4,000 | flash (proposed). ITCM has 1,152 B free (*measured*: 15,232 / 16,384), which the S095 P5 FX may want. The spike measures both placements |
| Voice, control, parameters, pages, tables | ≈ 7,000–10,000 | flash |
| **Total** | **≈ 11–15 KB** | against 37,020 B headroom (*measured*, `link_budget.py`) |

---

## 11. Platform integration (`INSTRUMENTS_DSP_REFERENCE.md` §9.2)

| §9.2 item | Katran |
|---|---|
| 1 Enum and registry | `INSTRUMENT_TYPE_KAT` after `INSTRUMENT_TYPE_COL`, before `UNKNOWN` (`InstrumentManager.h:101-102`). Registry row: `"kat"`, `".kat"`, label `"Katran"` (Load Morph shows `KatraMrp`), directory `"Katran"`, the class flag set by §9.2 (`INSTRUMENT_FLAG_BASIC` / `_ADVANCED`, `:302-303`). Type-sized arrays grow by one |
| 2 Files | `Core/DSP/Instruments/Katran/`: `KatranVoice.c/.h`, `KatranOsc.c/.h` (the oscillator loops with resets), `KatranParameters.c/.h`, `KatranTables.c/.h` (the delay time, loop tracking, break decay, envelope time and LFO rate laws, generated). The Colloid's files are the template; its SVF control (`col_svfControl()`) and envelope step are candidates to share |
| 3 Runtime | union member + `_Static_assert` (beside `:223` and `:244`); a Katran case at every site with a Colloid case (`INSTRUMENT_TYPE_COL` appears on 19 lines of `InstrumentManager.c`, the registry included; the schedule re-lists them). That includes the `IM_SPECIAL_OSC_TYPE` writer (`:4070`), `instrumentManager_waveDomainMax()` (`:869-905`), and `menu_formatWaveCell()` (`menu.c:12269`), all keyed on `osc1_*` as for the Colloid. Also: a Katran case in `instrumentManager_slotRight()`; an `ampEnvelopeQuiet()` branch; noinline async and sync renders (the `AlkalineVoice.h` precedent) |
| 3a **New: voice units** | Claim two units at type assignment and release them when the slot leaves Katran (no caller exists today). The schedule names the sites: the runtime slot reset, and every path that changes a slot's type |
| 4 Build | include path; parameters in `SRCS`; the voice and oscillator files in `DSP_SRCS` with explicit `-Ofast` rules |
| 5 Host proof | `check_special_tags.py` `TABLES['KAT']`; an `init.kat` generator and check; a host module `tools/dsp_test/modules/katran.c` (the extracted-source pattern) |
| 6 Storage | an append-only token; AutoSave stores `kat`; `SD_CARD/Instrument/Katran/init.kat`. The boot directory trim already enumerates every `/Instrument` subdirectory |
| 7 Class | set by §9.2; two FxBuffer voice units |

---

## 12. Decisions and questions

### 12.1 Decided (user, 2026-11-14)

| # | Question | Decision |
|---|---|---|
| **K-D1** | Dual or single core | **Dual, if possible.** The class follows the Colloid's probe run; the trim ladder up to step 4 is the implementer's; a single core (step 5) comes back to you |
| **K-D2** | AMP2's input | **TORCIDO out 1**; its gain CV is the LFO's inverted output. L = ring + AMP2(TORCIDO out 1), R = ring + AMP1(ring) |
| **K-D3** | Two voice units per Katran, the units' first use, the Effect share it takes | **Approved** |
| **K-D4** | The oscillators' colour | **The standard set** (`typ` `wav`/`wtb`/`pwm`, `wav`, `pos`); colour becomes `pos` (the PWM morph; Saw under `pwm` is the saw's tone) |
| **K-D5** | The delay's dry path inside the loop | **The 32-sample tap** |
| **K-D6** | How a drone ends | **A switchable interrupt in the feedback path that lets the tone die out slowly:** the loop break `brk` / `bkd` (§5.4). The envelope keeps the Colloid's law (except at the stop edge, K-D9), with times extended to ≈ 30 s |
| **K-D7** | TORCIDO's tone | **Moved into the resonant loop filter F1** |
| **K-D8** | Fit the slot reserve | **The oscillators share `typ` and `wav`; pitch and `pos` per oscillator; spread rows `dtn` and `psp` for OSC A's two copies.** The voice fits the reserve with no new RAM, **targeted a little under: ≤ 1,200 B linked** (user, 2026-11-14; §10.1). A separate `wav` for OSC B is the first option back (§3.4), and only if it keeps the voice under that line |
| **K-D9** | What the transport stop does to the drone (was K-Q16) | **The stop engages the loop break:** the cores' feedback and inputs slowly ramp down, so the sound dies out over about `bkd` (user: "slowly ramp down the feedback in the core so the sound dies out relatively slowly"). The envelope holds at the stop edge and goes idle when the break reaches silence. A trigger while stopped releases the break for that note (§5.4) |
| **K-D10** | `pos` under `wtb` (was K-Q17) | **A frame change takes effect at the oscillator's next cycle start** (§5.1) |

### 12.2 Open

**Needed before the host model:** none.

**Needed before the schedule:**

| # | Question | Recommendation |
|---|---|---|
| K-Q8 | The loop spread: one `spr` knob with fixed depths, or offset rows per destination | One knob (the oscillator spreads are already their own rows) |
| K-Q9 | F3's envelope: the amp envelope's second use (`fen`), or its own ADSR (four rows, ≈ 12 B, per block only) | `fen` |
| K-Q10 | Do the oscillators track the note, or does one drone at a fixed pitch? | Both track; `rng`/`coa` set the interval |
| K-Q11 | Can you record the AIRA patch (its saved settings, then a sweep of each GRF) for the by-ear comparison? | Yes, if the hardware is still set up |
| K-Q12 | TORCIDO's LEVEL 1 / LEVEL 2: input or output levels? | Output (as drafted) |
| K-Q13 | The AIRA LFO's wave knob: stepped or a morph? Does its rate reach audio rate? | Stepped, five shapes, 0.02–20 Hz until we know |
| K-Q14 | Any of the §3.4 options in place of a recommended element | None for now; OSC B's own `wav` first, if the host model asks for it |
| K-Q15 | The name (`kat`, `.kat`, `Katran`, `KatraMrp`) and the page layout of §8.1 | As listed |

---

## 13. Order of work

| Step | What | Gate | Deliverable |
|---|---|---|---|
| **0** | **The Colloid's probe run** (S095 P5 step 1, already planned): one Colloid, `plain12` stereo, cold, with the cycle probe | – | the cycles per instruction for this kind of code; Katran's class |
| **1** | **Host model** (`tools/dsp_test/modules/katran.c`, or a Python model first). Render WAVs of the patch alone (one core, at §1.3's values, with Saw and with Rec as the shared form); then the dual core; then each addition switched in on its own (`spr`, `dtn`, `psp`, `drf`, `xfb`, `nse`, `lst`, the sub, F1 with tracking, F3, the `typ` choices); then the break at several `bkd`, from `brk` and from a transport stop | your ear, against the AIRA recordings (K-Q11) | the draft laws set; the `init` form; the additions kept or cut; records in `tools/dsp_test/baselines/<session>/` |
| 2 | **Spike** (a scratch copy outside the tree): staged and fused loops for each oscillator form, the per-block path, a stub voice with two unit claims | linked loop counts (`elf_looppath.py --cond-heads --in-range`); per-block path (`elf_blockpath.py`, `control_cost.py --elf`); the mixer gate (0 grew); ITCM and flash; **the linked `sizeof(KatranVoice)` ≤ 1,200** (§10.1; the in-slot reserves applied if needed) | the measured counts; staged or fused; the trim step needed; the reserves used, if any |
| 3 | **Device run** of the stub voice (one flash): cold, the dearest form, a stereo send, with the probe | ≤ the class budget | the measured record |
| 4 | **Schedule** (`S0NN_KATRAN_IMPLEMENTATION.md`), every change cited by file and line | §12.2 answered; you approve it | |
| 5 | Registry and type, an empty voice, the unit claims and releases | builds; `check_special_tags.py`; Load lists `Katran`; Scene save and load round-trip; the Effect share shrinks and grows back | |
| 6 | Oscillators, TORCIDO, delays, loop filters, break, ring, output stage | host laws (`gb` reaches −60 dB at `bkd`; the stop edge engages the break and holds the envelope; a stopped trigger releases it; bounded loops; the frame change at cycle start); linked loop counts equal to the spike's | |
| 7 | Parameters, pages, `init.kat` | DEV self-check; host law for keys, labels, defaults and lanes | |
| 8 | Device: a Katran alone, then a Scene with Katrans, Colloids and Alkalines (the `cpu` widget, underruns); then listening | ≤ the class budget; no underruns | the measured record; the by-ear values recorded |

---

## 14. Statements this draft would make out of date (for you to log, if adopted)

- `INSTRUMENTS_DSP_REFERENCE.md`: the instrument count; §3.1 (the engine
  table and the union); §3.2 (the dispatchers); a new engine section;
  §9.2 (voice-unit claim and release as a checklist item).
- `EFFECTS_BUS_REFERENCE.md` and `EFFECTS_MODULES_DSP_REFERENCE.md`: voice
  units now have a caller, and the Effect share can shrink while a Katran
  is loaded.
- `BANK_PRESET_ARCHITECTURE.md`, `FILESYSTEM_SPEC.md`: the seventh type,
  its token, extension and folder.
- `STORAGE_SRAM_MANIFEST.md`: the union member; the voice-unit use of the
  FX arena; flash.
- `CPU_USE_DSP_AUDIT.md`: Katran's cost items; if Advanced, the first user
  of the Advanced budget since S094 P2 §22.
- `MEMORY.md`: the type list; "No current type is Advanced" (if Katran
  lands on Advanced); and S095 P5's "voice units: no caller" (via
  `S095_P5_COLLOID_ALKALINE_ADVANCED_FX.md` §1).
- `TORCIDO_Noise_Synth_patch.json`: lacks the TORCIDO out 1 → AMP2 in
  connection confirmed on 2026-11-14.
