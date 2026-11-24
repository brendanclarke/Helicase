# S103 — Katran: drone / noise synth (plan)

**Date:** 2026-11-24. **Branch:** `dev-ph7-alkaline` (S102 P1 Fix 1 tree,
uncommitted). **Status:** plan, **revision 3**. Nothing in the source was
changed.

**History:**
- revision 1: the initial plan; questions Q1–Q12.
- **revision 3 (user, 2026-11-24): the page layout.** At most two screens
  per page, except the reserved effect on `click`'s screen 3; `mix` one
  screen; `fm` gains a second screen; `mod` keeps the standard velocity
  cells. §8.2 is re-laid out; the rows themselves are unchanged.
- **revision 2 (your answers, 2026-11-24):**
  - **Q1 (a):** trim the rows and leave the LFO page as it is. The LFO page
    never changes on any instrument. Katran exposes the most musically
    relevant levers, not every parameter. Rows are combined, or use a
    base/offset split of one value range (§8).
  - **Q2–Q12:** the recommendations are accepted.
  - **New requirement, the Basic/Advanced file merge:** Colloid and Alkaline
    Basic/Advanced will later load and save the same file. The Basic type
    ignores the effect and saves it as off. That work is not in this plan,
    but **Katran must be ready for it**. Rows are reserved for a later
    insert effect, shown as the last screen of the `click` page (§9).
  - **Voice units (user, 2026-11-24):** Katran keeps **two** units (50 ms
    per core, K-D3; Q8 (a)). Katran will not get the BBD chorus; its later
    insert effects will be other ones, so the reserved effect needs **no
    voice unit** of its own (§7.2, §9).

**Sources:**
- `knowledge_files/drafts/DRAFT_KATRAN_DRONE_SYNTH.md` revision 5 (written
  from `d6a9c9b7`, S095 P4). Its decisions K-D1 to K-D10 stand unless §2
  names a change. `D§n` points into it.
- `knowledge_files/drafts/TORCIDO_Noise_Synth_patch.json`, `patch.png`.
- Your brief (2026-11-24, §1) and your answers (revision 2).
- The tree, as of today, for every name and line cited.

**Labels:**
- **Code:** read from the tree, with a file and line.
- **Measured:** from a device trace or linked-image record, with its source.
- **Est.:** an estimate, with its basis. It is measured before a schedule
  relies on it.

**Binding rules that apply** (from `MEMORY.md`):
- the S094 P2 §21 cycle budget (Basic 19,620 cycles a block) and the S101
  P3 Basic ceiling (18.6k, the Tetrakt as built);
- the S089 P2 CPU ceiling, constant CPU, and the S093 per-block accounting;
- RAM and ITCM growth need sign-off;
- the type registry is append-only, with no backward compatibility;
- `INSTRUMENTS_DSP_REFERENCE.md` §9.2 (add-a-type) and §12 (oscillators);
- the Advanced effect is always the last screen of the `click` page (S099
  P2, `ColloidParameters.c` comment above `colloid_adv_menu_pages[]`);
- no step of this plan edits `MEMORY.md` or `knowledge_files/` (§13 lists
  what it would make out of date).

---

## 0. Summary

| Item | Plan |
|---|---|
| **Type** | The **eighth** type: `INSTRUMENT_TYPE_KAT`, token `kat`, after `INSTRUMENT_TYPE_ALX` (value 9; `UNKNOWN` 9 → 10) |
| **Class** | **Basic.** Device gate: median ≤ **18.6k** cycles a block (5.2 % under 19,620). Design target ≈ 17.7–18.2k. Estimate ≈ 349 instructions a sample ≈ **17.0k** at 1.52 cycles an instruction (§6) |
| **Core** | The draft's dual feedback core (D§1–§5): exciter OSC A → TORCIDO → resonant loop filter → delay, re-clocked by its own output; ring modulation by OSC B; asymmetric L/R VCA pair. **Stereo** |
| **Run** | `run`: 127 = on (the loops sustain); 0–126 = off, dying out over 0.25–60 s, slower the higher the value (§3.1) |
| **Kick** | Any trigger re-arms the run gain, injects an energy burst scaled by velocity, and at high `kik` also swells the feedback. No envelope is retriggered (§3.2) |
| **Envelopes** | Two free-running cycling **chaos envelopes**: CE1 → amplitude, CE2 → filter cutoffs. Triggers never reset them (§3.3) |
| **Buffer read** | Either or both oscillators can read the DTCM FxBuffer arena as a raw 1,024-sample window: `pos` is the position, the pitch rows are the speed, with no limit. A DC block, the `dmp` damping low-pass and a level guard are always on in this form (§3.4) |
| **Rows** | **36 own rows + 21 platform rows = 57.** Indices 57–59 are kept free for additions found by ear; **60–63 are reserved for the insert effect** (`fx_type`, `fx_rate`, `fx_depth`, `fx_mix`). Two screens a page at most (the effect on `click`'s screen 3 excepted); `mix` one screen; the LFO page unchanged (§8) |
| **Merge-ready** | Same keys, order and page placement as `clx`/`alx` for the effect rows; `click` cells 8–11 left empty for it. Katran's later effects will not be the chorus and must need no voice unit (§9) |
| **RAM** | **0 B new.** Voice ≈ 1,175 B calibrated est., within the ≤ 1,200 B gate (K-D8) and the 1,264 B slot reserve |
| **Delay memory** | **Two** FxBuffer voice units (K-D3), one 50 ms ring per core: −8,832 B of the Effect share per Katran. This uses the whole per-slot cap of 2 (§7.2) |
| **ITCM / flash** | 0 B ITCM. Flash ≈ 12–16 KB est., against ≈ 79 KB headroom (753,664 B application region − 674,520 B image, S102 P1 §0) |

---

## 1. The brief (2026-11-24), as received

1. The instrument is stereo.
2. It must fit the **Basic** budget, ideally 5–10 % below it.
3. It always produces sound, but it has a continuous on/off parameter: on
   at the top, off at the bottom. Off lets the internal feedback die out
   naturally, so the sound ends reasonably, and the higher the value, the
   slower it dies.
4. An active sequencer step **kicks** the feedback loop instead of
   retriggering envelopes. Envelopes repeat, with a chaotic progression.
5. Either or both oscillators can be fed from the DTCM buffer: a rough read
   from anywhere in it, with a position and a speed (coarse tune), not
   limited. Feedback is expected, and there must be a way to damp it. DC
   blocking keeps it under control.
6. (Revision 2.) Expose the musically relevant levers, not every parameter.
   The LFO page is never changed. Reserve the insert-effect rows for the
   later Basic/Advanced file merge.

---

## 2. Decisions

### 2.1 Revision 2 decisions (user, 2026-11-24)

| # | Decision |
|---|---|
| D-R2-1 | **Rows:** trimmed to the musically relevant levers by combining rows and base/offset ranges (§8). The **LFO page is the platform's, unchanged on every instrument**. Q1 (a) |
| D-R2-2 | **Q2–Q12 as recommended** in revision 1: G1 decay (Q2); stop decays, boot silent until a kick or start (Q3); kick K1–K3 (Q4); two chaos envelopes, no ADSR (Q5); arena read R2, arena only, forward only (Q6); level guard on (Q7); Katran fade on a pending Scene apply (Q9); sub and ch2 cuts (Q10); draft recommendations for K-Q8–K-Q15 (Q11); name `kat` / `Katran` (Q12) |
| D-R2-3 | **Merge-ready** (new requirement): effect rows reserved at indices 60–63 with the `clx`/`alx` keys; `click` cells 8–11 reserved. **Two voice units stay** (Q8 (a)); Katran's later insert effects will not be the chorus, and must need no unit (§7.2) |

### 2.2 What changes from the draft

| Draft item | Plan | Why |
|---|---|---|
| Type after `col` (D§11) | After `alx`, value 9 | Append-only; `tkt`, `clx`, `alx` came first |
| Class follows the Colloid probe (D§9.2) | **Basic** | Your brief. The probe has run (S101 P1 §2.1) |
| Voice units have no caller (D§5.3) | Three callers exist (`instrumentManager_unitsWanted()`, `InstrumentManager.c:549`) | S096 P1, S099 P1, S101 P3 |
| `brk` + `bkd` (K-D6, K-D9) | `run` (§3.1) | Brief item 3 |
| Colloid ADSR, `sus`, `fen` (D§6) | Two chaos envelopes (§3.3) | Brief item 4 |
| A trigger restarts the envelope | A trigger kicks the loop (§3.2) | Brief item 4 |
| Sub-oscillator (D§3.2 A5) | Removed | CPU (§6) |
| TORCIDO ch2 full stage | Bare cubic, fixed level | CPU; ch2 sees only the exciter |
| `wav` / `wtb` / `pwm` forms | + an arena read form per oscillator (§3.4) | Brief item 5 |
| ≈ 50 own rows (D§8.1) | **36 own rows** (§8) | D-R2-1, the 64-row cap, the 4 reserved rows |
| `dtn` and `psp` as two rows (K-D8) | One spread row, `dtn`, moves both | D-R2-1 |
| Everything else in D§1–§5, §7 | Stands | K-D1, K-D2, K-D4, K-D5, K-D7, K-D8 (shared `typ`/`wav`), K-D10 |

---

## 3. The new behaviour

### 3.1 The run control (`run`)

**One gain per voice, `gr`, scales every input to both delay rings:** the
TORCIDO outputs after F1/F2, the feedback term and the cross-feedback. This
is the draft's `gb` (D§5.4 item 1). With every input scaled, the loops'
level falls at least as fast as `gr`, so off always ends in silence (G1,
Q2).

| `run` | Meaning | `gr` |
|---|---|---|
| **127** | **On** | Rises to 1 over ≈ 20 ms and stays there |
| **0–126** | **Off**, decay time T(run), log from ≈ 0.25 s to ≈ 60 s (by ear) | `gr ← gr × k(run)` per block, `k = 10^(−3 × 32 / (fs × T))`: −60 dB at T. Below 1e-4 it snaps to 0 |

- **Per block:** one read of `katRunK[128]` (the `colEgK[]` pattern; no
  `exp` at run time) and one multiply. **Per sample:** the ramp already
  counted in the delay write.
- **A kick re-arms it** (§3.2): `gr` returns to 1, then decays again at
  T(run). Below the top, Katran is a struck, ringing instrument. At the top
  it is a continuous drone, and kicks accent it.
- **Moving `run` to 127** brings `gr` back to 1 without a kick.
- **Step locks:** lock 127 on a step to hold the drone from there. The step
  reset (S081 §5.8) restores the base value at the next triggered step.
- **Transport and boot (Q3):**
  - the **stop edge** (read once per block from `seq_isRunning()`, as
    `ColloidVoice.c:1364`) starts a decay even at 127, at T(126);
  - a **kick while stopped** re-arms it, so a pad or the preview plays;
  - **transport start** re-arms it;
  - **boot and Scene load:** `gr` starts at 0 until the first kick or start.
- **A pending Scene/Kit apply (Q9):** `gr` fades to 0 over ≈ 20 ms, and then
  the voice reports quiet (§11, R6).

### 3.2 The kick

A **kick** is any trigger for the slot: an active step (each roll hit
included), a pad, a MIDI note-on, the chromatic overlay (S102), the preview.
It is handled in `Katran_triggerVoice()` (the `Colloid_triggerVoice()`
pattern, `ColloidVoice.c:1245`) and applied **per block**. It costs **0 per
sample**, because each part acts on a gain the loops already apply.

**One row, `kik`, as a base/offset range:**

| `kik` | K2 burst depth | K3 feedback swell |
|---|---|---|
| 0 | 0 (the kick only re-arms) | 0 |
| 1–63 | rising to full | 0 |
| 64–127 | full | rising to the clamp |

| Part | What it does |
|---|---|
| **K1 re-arm** | `gr` rises to 1 over ≈ 5 ms |
| **K2 burst** | A kick envelope `kE = burst × velocity`, decaying over ≈ 80 ms (fixed, by ear), is added to the noise exciter gain and to OSC A's level. Energy enters both loops through TORCIDO ch2 |
| **K3 swell** | `swell × kE` is added to the feedback, inside the shared feedback clamp (D§7). The loops are pushed briefly towards self-oscillation |
| **Pitch** | The note sets both oscillators' pitch (draft K-Q10) |

- **It does not** reset phases, clear the delays, or touch the chaos
  envelopes.
- **Note-off** does nothing; there is no gate.

### 3.3 The chaos envelopes (CE1, CE2)

**Two free-running cycling envelopes**, each a rise/fall function generator
stepped per block:

| Row | Range / law |
|---|---|
| `cyc` cycle time | rise + fall, ≈ 10 ms to 60 s, log |
| `skw` skew | 0 = instant rise and long fall; 63 = symmetric; 127 = long rise and instant fall. Each stage is at least 2 ms |
| `dep` depth | CE1: 0 = steady, 127 = full swell. CE2: bipolar, stored 63 = 0 |
| `chs` chaos (shared by both, and the patch LFO's jitter) | 0 = regular cycles. Rising: at each cycle start, the next cycle time is multiplied by `2^(chs' × r)` (r uniform in ±1, one `GetRngValue()` per cycle) and the peak by `1 − chs' × |r₂| / 2`. Above 64, the rise rate is also scaled by the loop's block energy (`ringPrev`, D§5.7), so a loud loop hurries its own envelope |

- **Never reset by a trigger** (your brief). Seeded from the RNG at init, so
  the two never start aligned.
- **CE1 → output amplitude:** `out × (1 − dep' + dep' × CE1)`. This is the
  only level shaping besides `run`.
- **CE2 → the loop filter (F1) cutoff**, ±4 octaves, and the output filter
  (F3) at half that amount. It replaces the draft's `fen`.
- **Cost:** ≈ 30 instructions a block each. The CE1 gain ramp sits in the
  output loop where the draft's envelope ramp was (0 new per sample).

### 3.4 The arena read form

**What is read:** the FxBuffer arena, `.dtcm_fxbuf`: every DTCM byte after
the `INDTCM`/`INDTCMZ` statics (`FxBuffer.h`; ≈ 126,656 B per the draft
§5.3, re-read with `fxbuf_arenaBytes()` in the spike). It holds:
- the Effect share at the bottom: a 512 B Space prefix (control block and
  float state), then the Delay/Echo/Reverb rings (16-bit) or the CrumpBit
  lines (**8-bit**, `CrumpBitEffect.c:964`);
- the claimed voice units at the top: Tetrakt and `Spc` lines, chorus rings
  and the `BbdChorusState` inside a unit (`BbdChorus.h:218`), **and
  Katran's own delay rings**;
- free units and unused share, which hold stale audio or power-up garbage.
  The arena is NOLOAD and never cleared (`FxBuffer.h`: "owners clear what
  they claim").

Everything is read as int16. Float state and 8-bit data come out as harsh
noise: the rough character you asked for, and the largest loudness risk
(R5).

**Selection:** `src` = `off` / `A` / `B` / `AB`. A selected oscillator reads
the arena instead of `typ`/`wav`; the other plays the shared form.

**The read model (R2, Q6):**
- The oscillator keeps its phase accumulator, its pitch law, OSC A's sync
  resets (D§5.1) and the change-at-cycle-start rule (K-D10).
- The phase indexes a **1,024-sample window** starting at `S`:
  `x = arena[S + (phase >> 22)]`. There is no interpolation.
- **Position:** the oscillator's `pos` sets `S = pos' × (N − 1,024)` across
  the whole arena, about 490 samples a step. The platform LFO, CE2, Morph
  and step locks move it. A change takes effect at the next cycle start,
  computed per block, so there is no wrap test per sample.
- **Speed (coarse tune):** the pitch rows. At the root note with `coa` 0,
  the window plays one sample per sample (43 Hz). Each octave up doubles
  the speed. **Unlimited:** the increment is not clamped, so high settings
  skip through the window and alias. Forward only.
- OSC A's two copies read the same window at their own detuned speeds and
  offset positions (`dtn`).

**Conditioning, always on in this form (constant cost):**
1. a DC block (one-pole HP at ≈ 10 Hz);
2. the damping low-pass, `dmp`: one-pole, open at 0, ≈ 300 Hz at 127;
3. the **level guard** (Q7): a peak on every 8th sample of each read block
   drives a fast-attack, slow-release gain on the read;
4. the oscillator's `lvl`, then into the core as any oscillator.

**Feedback paths:**

| Path | Loop | Damping |
|---|---|---|
| Own delay rings | read → TORCIDO → delay → read back | `dmp`, DC block, guard, TORCIDO's bounded shaper, the int16 write saturation, `run` |
| The Effect share | Katran → FX send → Effect ring → read back | the same, plus the FX send level |
| Other voices' units | another voice → Katran | open loop, unless that voice hears Katran |
| Garbage / float state | none, but full-scale noise | `lvl`, `dmp`, the guard |

**Cost:** ≈ 8 (phase, index, load) + 8 (conditioning and guard) ≈ 16 per
read. The dearest PWM form is ≈ 22 per oscillator. Under constant CPU the
dearest form sets the charge, so the arena form adds **0 per sample** to
the budgeted case. The spike measures this (§15 step 2).

---

## 4. Signal path (revised from D§4)

```
per block (async, control rate)
  c0  transport edge; pending-apply fade; kick: kE decay, K1 re-arm, K2/K3 gains;
      run: gr target / k(run)
  c1  patch LFO (cv, inv; self-modulated rate, shape from lfm; D§5.7)
  c1a chaos envelopes CE1, CE2 (per-cycle RNG; loop-coupled rise)
  c2  oscillators: shared typ/wav bind; increments (pitch, dtn, inv on B fine); pos -> frame
      or arena window start S (applied at cycle start); src picks the form per oscillator
  c3  TORCIDO: drive (tube at a fixed ratio, + cv), wet; ch2 fixed level
  c4  loops (per core): delay time (tim + dtk + spr + drift); feedback (fbk + inv + swell);
      cross-feedback (spr below 63); F1 coeffs (frq + cv + CE2 + tracking + spr); F2; gr ramp
  c5  output: VCA gains from vca + LFO; F3 coeffs (cut + CE2/2 + spr); ring lvl 0 crossfade;
      width; CE1 gain

per sample (sync, 32 samples; staged, D§9.3)
  L1  both cores: D_s[n] = wet x ring_s[w - T_s(n)] (fractional) + dry x ring_s[w - 32]
  L2  B[n] (shared form, or arena read + DC + damp + guard); bWrap[n]
  per core s = L, then R:
  L3  A[n] (core s's OSC A, form or arena read); reset if P x bWrap[n] + D_s[n] crosses sth
  L4  t1 = TOR1(D_s[n]);  t2 = cubic(aLvl x ((1-nse) A[n] + nse x noise_s[n]))
      ring_s[w] = SAT16( gr[n] x (F2(F1_s(t1 + t2)) + xfb x D_other[n] + fbk_s x D_s[n]) )
      r = t1 x ((1 - k) + k x bLvl x B[n])
      core L: outL[n] = r + g2[n] x t1        core R: right[n] = r x (1 + g1[n])
  L5  in place on outL and right: F3 per side; width; x CE1 gain; int16 stores
```

---

## 5. Stability

- The draft's bounds stand (D§7): the shaper bounds each TORCIDO channel; the
  delay write saturates; feedback + cross-feedback + swell are clamped as one
  sum; F2 keeps DC out of the loop.
- `run` off always ends in silence.
- The arena read can carry a loop's own output back in. It enters through
  `lvl`, the guard and TORCIDO ch2, so it is bounded like any exciter.
- **Output normalisation:** one constant, set by ear in the host model, with
  a full-level read of garbage as the loudest case.

---

## 6. CPU

### 6.1 The target

| Line | Cycles a block | Below the Basic share |
|---|---:|---:|
| Basic share (S094 P2 §21) | 19,620 | — |
| 5 % below | 18,639 | 5 % |
| **Basic ceiling** (S101 P3 §1, the Tetrakt as built, validated on the device) | **18,600** | 5.2 % |
| 10 % below | 17,658 | 10 % |

- **Device gate:** median ≤ 18.6k; window maximum ≤ 19.62k.
- **Design target:** ≈ 17.7–18.2k.
- **Planning CPI:** 1.52 (the Tetrakt, S101 P2 A2; the Effect measured
  1.47, S101 P2 §A.6b). The hot data is in DTCM, and the per-sample path has
  no VDIV.
- **Linked-image gate:** ≤ **360** instructions a sample, all-in (loops plus
  per-block ÷ 32, S093), i.e. 17.5k at 1.52.

### 6.2 Composition (est., instructions a sample, dearest settings)

| Part | Draft | Plan | Change |
|---|---:|---:|---|
| L1 two delay reads (timed fractional + 32 tap) | 28 | 28 | |
| L2 OSC B, dearest form, wrap carry | 24 | 24 | arena form ≈ 16, cheaper |
| L3 OSC A pair, dearest form, resets | 54 | 54 | arena form ≈ 32 for the pair |
| Noise exciter (now a crossfade) | 4 | 4 | |
| L4 TORCIDO ×4 | 56 | 46 | ch2 bare cubic (−10) |
| F1 ×2 + F2 ×2 | 48 | 48 | |
| Delay writes ×2 | 20 | 20 | |
| Ring ×2, VCA sums, core stores | 16 | 16 | |
| L5: F3 ×2, width, CE1 gain, stores | 74 | 62 | sub removed (−12) |
| **Loops** | **324** | **302** | |
| Per block | 1,350 ÷ 32 = 42 | ≈ 1,500 ÷ 32 = **47** | + CEs, kick, run, window starts; − ADSR |
| **Total** | **366** | **≈ 349** | |

- **At 1.52:** ≈ **17.0k** (13 % under the share). With +5 % estimate error,
  17.8k (9 % under); with +9 %, 18.5k (6 % under).
- **If over the gate after the spike**, the ladder:
  1. noise exciter out (−4);
  2. F3 to a one-pole tilt per side (≈ −35);
  3. the 32-sample tap out (≈ −8; reverses K-D5, your call);
  4. single core (your call, K-D1).
- **If under the target by more than ≈ 10 %:** first add back the sub
  (+12, one of the free rows 57–59), then ch2's full stage (+10).
- **A later Advanced Katran:** its effect (not the chorus) has ≈ 3.2k cycles a
  block between ≈ 18.6k and the Advanced share (21,803). For scale, the
  Colloid's half-rate chorus costs ≈ 1.6k (S101 P2 §A.6).

### 6.3 Known CPU hazards

- **Mixer register re-allocation** (S101 P3 §4): a new inlined render
  lengthened three mixer loops by one instruction each. The renders are
  `noinline` (the `AlkalineVoice.h` precedent), and the spike runs the
  mixer hot-loop diff.
- **Flash read variance** (the Tetrakt's 15.2–17.3k spread, S101 P1 §2.1)
  applies only to the shared `wtb` and stock-table reads. The arena reads are
  in DTCM.

---

## 7. Memory

### 7.1 Voice struct (inside the 1,264 B slot reserve, `InstrumentManager.c:291`)

| Member | Bytes (est.) |
|---|---:|
| Draft total (D§10.1) | 1,094 |
| − Colloid ADSR and its rows; − sub state and rows | −26 |
| − parameter members for the rows dropped or merged (≈ 50 → 36 own rows; fixed values become constants) | −30 |
| + two chaos envelopes (level, stage, rates, peak, RNG state) | +28 |
| + kick and run state | +12 |
| + arena read: window starts ×3, DC ×3, damp ×3, guard ×3, coefficients | +56 |
| **Total** | **≈ 1,134** |
| Calibrated (+4.9 %, the Colloid's draft-to-link error, D§10.1) | **≈ 1,190** |

- **Gate:** linked `sizeof(KatranVoice)` ≤ **1,200 B** (K-D8), with its
  `_Static_assert` beside the others (`InstrumentManager.c:324` onward).
- **The insert effect:** its four row bytes (≈ 4 B) are counted in the
  parameter members now. Any **state** a later Katran effect needs must
  fit the slot reserve, with no voice unit (§7.2): ≈ 74 B between the
  calibrated ≈ 1,190 B and the 1,264 B reserve, plus whatever the in-slot
  reserves below free. The effect's design is held to that.
- **In-slot reserves**, if the link is over: `B` in `right[32]` (−64 B);
  parameter members kept as bytes (−40 to −60 B) (D§10.1).
- **No new static, DTCM or SRAM allocation.**

### 7.2 Voice units: two per Katran (K-D3; Q8 (a), confirmed 2026-11-24)

- **Two units,** one 50 ms ring (2,208 samples) per core. The lowest loop
  pitch is 20 Hz; the minimum delay is 32 samples (1,378 Hz). The `tim` law
  tops out at ≈ 44 ms, which leaves ±12 % for the spread and drift.
- **The per-slot cap is 2** (`FXBUF_VOICE_UNITS_PER_SLOT`, `FxBuffer.h`), so
  Katran uses all of it.
- **The claim path today takes 0 or 1.** `instrumentManager_slotVoiceUnits()`
  (`InstrumentManager.c:604`) is documented "wanted (0 or 1)" and returns
  only ordinal 0. Katran needs `wanted` 2, with `fxbuf_voiceUnit(slot, 1)`
  for the second ring, and `instrumentManager_unitsWanted()` returning 2
  for KAT. The voice clears both units when it claims them, outside the
  render.
- **The Effect share:** −8,832 B per Katran. Six Katrans use all 12 units and
  leave ≈ 73,664 B (draft §5.3 arithmetic). The Space Reverb ring (64 KB +
  512 B prefix) still fits. Delay/Echo and CrumpBit `dly` maximum times
  shrink with the share (`space_timeLimit()`, `SpaceEffect.c:1174`).
- **Fragmentation:** a released unit below a claimed one does not return to
  the share (no compaction, `FxBuffer.h`). Two units per slot make this more
  likely in mixed Scenes that swap Katrans in and out.
- **The later insert effect needs no unit (user, 2026-11-24).** Katran will
  not get the BBD chorus, which takes a whole unit (`BbdChorus.h:89–92`).
  Its effects will be others, designed to run from the voice struct alone
  (§7.1) or from Katran's own rings. An effect that needs its own buffer
  memory would reopen the per-slot cap; that is not part of this plan.

### 7.3 Flash, ITCM

- **Flash:** ≈ 12–16 KB est. Headroom ≈ 79 KB.
- **ITCM:** 0 B proposed (960 B free: 15,424 / 16,384 B, S102 P1 §0).

---

## 8. Parameters and pages

### 8.1 The row budget

| | Rows |
|---|---:|
| `INSTRUMENT_PARAM_COUNT` (`InstrumentManager.h:19`) | 64 |
| Platform rows Katran keeps (the Colloid's set, `ColloidParameters.c`): `instrument_vol`, `instrument_pan`, 12 × `lfo_*`, 5 × `osc1_*`, 2 × `velo_mod_*` | 21 |
| **Reserved: the insert effect** (`fx_type`, `fx_rate`, `fx_depth`, `fx_mix`; §9) | 4 |
| Available for Katran's own rows | 39 |
| **Used** | **36** |
| Free (indices 57–59), for additions the host model asks for | 3 |

### 8.2 The menu, page by page

**Layout rules (user, 2026-11-24, revision 3):**
- every page has at most **two** four-cell screens (cells 0..7);
- **the one exception is `click`**: its screen 3 (cells 8..11) is the
  reserved insert effect, as on `clx`/`alx`;
- **`mix` has one screen** (four cells);
- **`fm` has a second screen**;
- **`mod` keeps the standard velocity cells** `dst amt` at cells 0..1;
- the `lfo` page is the platform's and is not changed. It is the only page
  with more than two screens, and the rule above does not touch it.

**P** = a platform row; every other row is Katran's own (`kat_*` keys).
The last column names the draft rows each one replaces or combines.

**Overview:**

| Page | Screen 1 (cells 0–3) | Screen 2 (cells 4–7) | Screen 3 (cells 8–11) |
|---|---|---|---|
| 0 **osc** | `coa fin pos dtn` | `wav typ src dmp` | — |
| 1 **aeg** | `run kik cy1 sk1` | `dp1 cy2 sk2 dp2` | — |
| 2 **mod** | `dst amt chs` | `lfr lfm lfl vca` | — |
| 3 **fm** | `coa fin pos lvl` (OSC B) | `exc nse sth` (the exciter) | — |
| 4 **click** | `drv wet tim fbk` | `drf dtk` | **reserved: `fx dpt rte mix`** |
| 5 **filter** | `frq res typ lct` | `cut` | — |
| 6 **lfo** | the platform layout, unchanged | | |
| 7 **mix** | `vol pan spr wid` | — | — |

Own rows by page: 3 + 8 + 5 + 7 + 6 + 5 + 0 + 2 = **36**. Empty cells inside
the first two screens: `mod` 3, `fm` 7, `click` 6–7, `filter` 5–7 (seven in
all), for the three free indices (57–59) if the host model asks for them.

**Page 0 — `osc` (the shared form and OSC A's pitch)**

| Screen | Cell | Label | Key | Range / law | Combines / replaces |
|---|---|---|---|---|---|
| 1 | 0 | `coa` | `osc1_pitch_coarse` **P** | semitones; under `src` A it is the read speed | `rng` + `coa` (`rng` dropped) |
| 1 | 1 | `fin` | `osc1_pitch_fine` **P** | ±50 cents | |
| 1 | 2 | `pos` | `osc1_position` **P** | PWM width / `wtb` frame / **arena window position** | |
| 1 | 3 | `dtn` | `kat_spread_a` | 0 = none; rising: L/R detune 0..25 cents **and** `pos` spread 0..16 together | `dtn` + `psp` |
| 2 | 4 | `wav` | `osc1_wave` **P** | the shared form | |
| 2 | 5 | `typ` | `osc1_type` **P** | `wav` / `wtb` / `pwm` | |
| 2 | 6 | `src` | `kat_source` | `off` / `A` / `B` / `AB`: which oscillators read the arena | new |
| 2 | 7 | `dmp` | `kat_damp` | arena damping low-pass: open .. ≈ 300 Hz | new |

**Page 1 — `aeg` (how the sound lives and ends: run, kick, both chaos envelopes)**

| Screen | Cell | Label | Key | Range / law | Combines / replaces |
|---|---|---|---|---|---|
| 1 | 0 | `run` | `kat_run` | 127 on; 0..126 off, decay 0.25 .. 60 s | `brk` + `bkd` |
| 1 | 1 | `kik` | `kat_kick` | 0..63 burst depth; 64..127 full burst + feedback swell | `kik` + `kfb` |
| 1 | 2 | `cy1` | `kat_ce1_cycle` | CE1 cycle time, 10 ms .. 60 s | `atk` `dec` `slv` `rel` |
| 1 | 3 | `sk1` | `kat_ce1_skew` | CE1 rise/fall balance | |
| 2 | 4 | `dp1` | `kat_ce1_depth` | CE1 → amplitude: 0 steady .. 127 full swell | `sus` |
| 2 | 5 | `cy2` | `kat_ce2_cycle` | CE2 cycle time | `fen` |
| 2 | 6 | `sk2` | `kat_ce2_skew` | CE2 rise/fall balance | |
| 2 | 7 | `dp2` | `kat_ce2_depth` | bipolar, stored 63 = 0: F1 ±4 octaves, F3 half that | |

**Page 2 — `mod` (velocity, chaos, the patch LFO, the VCA pair)**

| Screen | Cell | Label | Key | Range / law | Combines / replaces |
|---|---|---|---|---|---|
| 1 | 0 | `dst` | `velo_mod_dest` **P** | standard | |
| 1 | 1 | `amt` | `velo_mod_amount` **P** | standard (also scales the kick burst) | |
| 1 | 2 | `chs` | `kat_chaos` | 0 regular .. 127: per-cycle time and peak jitter for CE1, CE2 and the patch LFO; loop-coupled above 64 | new |
| 1 | 3 | | | empty | |
| 2 | 4 | `lfr` | `kat_plfo_rate` | patch LFO 0.02 .. 20 Hz | `lfr` |
| 2 | 5 | `lfm` | `kat_plfo_mod` | 0 = sine; rising: audio and self rate modulation, which also skews the shape | `lfm` + `lfw` |
| 2 | 6 | `lfl` | `kat_plfo_level` | depth to every fixed destination (AMP1/2, F1, tube, OSC B `pos`, feedback, OSC B fine, F3 sweep) | `lfl` + `flf` + `a1o`/`a2o` |
| 2 | 7 | `vca` | `kat_vca` | bipolar, stored 63 = the patch balance: towards 0, more of the raw loop on L (AMP2); towards 127, more ring swell on R (AMP1). The patch LFO swings both around it | `a1l` + `a2l` |

**Page 3 — `fm` (OSC B and the ring; then the exciter)**

| Screen | Cell | Label | Key | Range / law | Combines / replaces |
|---|---|---|---|---|---|
| 1 | 0 | `coa` | `kat_b_coarse` | semitones (−36 .. +24); under `src` B it is the read speed | `rng` + `coa` |
| 1 | 1 | `fin` | `kat_b_fine` | ±50 cents | |
| 1 | 2 | `pos` | `kat_b_pos` | width / frame / arena window position | |
| 1 | 3 | `lvl` | `kat_b_level` | 0 = ring off (the carrier passes, one-block crossfade); 1..127 ring depth | `lvl` + `rmo` |
| 2 | 4 | `exc` | `kat_level_a` | OSC A (the exciter) level into the loop | `lvl` + `lv2` (`lv2` fixed) |
| 2 | 5 | `nse` | `kat_noise` | 0 = oscillator only .. 127 = noise only (crossfade) | `nse` (was additive) |
| 2 | 6 | `sth` | `kat_sync` | sync threshold: low = every ripple resets OSC A, 127 = resets off | `sth` |
| 2 | 7 | | | empty | |

**Page 4 — `click` (the loop: TORCIDO and the delay), then the reserved effect**

| Screen | Cell | Label | Key | Range / law | Combines / replaces |
|---|---|---|---|---|---|
| 1 | 0 | `drv` | `kat_drive` | TORCIDO drive; the tube warmth follows at a fixed ratio (+ LFO) | `dis` + `tub` |
| 1 | 1 | `wet` | `kat_wet` | TORCIDO dry/wet | `mix` (renamed: `mix` is the effect's label on this page) |
| 1 | 2 | `tim` | `kat_time` | delay time, log, 32 samples .. ≈ 44 ms | `tim` |
| 1 | 3 | `fbk` | `kat_feedback` | 0 .. 0.95 (+ LFO) | `fbk` + `lv1` (`lv1` fixed) |
| 2 | 4 | `drf` | `kat_drift` | 0 still .. 127 tape wander | `drf` |
| 2 | 5 | `dtk` | `kat_track` | 0 = free time .. 127 = the delay follows OSC B's pitch | `dtk` |
| 2 | 6–7 | | | empty | |
| **3** | **8–11** | **`fx dpt rte mix`** | **reserved: `fx_type` `fx_depth` `fx_rate` `fx_mix`** | **empty on Basic** (§9) | the `clx`/`alx` placement |

**Page 5 — `filter` (F1 loop filter, F2 low-cut, F3 output)**

| Screen | Cell | Label | Key | Range / law | Combines / replaces |
|---|---|---|---|---|---|
| 1 | 0 | `frq` | `kat_loop_cutoff` | F1 cutoff; when tracked, semitones above the loop's pitch | `ton` |
| 1 | 1 | `res` | `kat_loop_reso` | F1 resonance | `lrs` |
| 1 | 2 | `typ` | `kat_loop_type` | LP / BP / HP, each absolute or tracked (6 values) | `lft` + `lkt` |
| 1 | 3 | `lct` | `kat_low_cut` | loop low-cut, 5 .. 600 Hz | `lct` |
| 2 | 4 | `cut` | `kat_out_cut` | F3, bipolar: 63 open; below: LP closing; above: HP opening (fixed moderate resonance) | `cut` + `oft` + `ors` |
| 2 | 5–7 | | | empty | |

**Page 6 — `lfo`:** the platform LFO, **unchanged** (the Colloid's layout:
rate, sync, wave, offset, retrigger, polarity, two amounts, two targets).
12 platform rows.

**Page 7 — `mix` (one screen)**

| Screen | Cell | Label | Key | Range / law | Combines / replaces |
|---|---|---|---|---|---|
| 1 | 0 | `vol` | `instrument_vol` **P** | | |
| 1 | 1 | `pan` | `instrument_pan` **P** | | |
| 1 | 2 | `spr` | `kat_spread` | bipolar, stored 63: 0..62 = cross-coupled (cross-feedback rising to its maximum at 0); 63 = independent cores; 64..127 = spread apart (delay time ±12 %, F1 ±1 octave, F3 ±½ octave) | `spr` + `xfb` |
| 1 | 3 | `wid` | `kat_width` | 0 mono, 64 as rendered, 127 wider | `wid` |

### 8.3 What becomes fixed (constants, set by ear in the host model)

| Draft row | Fixed at | Still moved by |
|---|---|---|
| `lv1`, `lv2` (TORCIDO levels) | the patch values (60, 64) | `fbk`, OSC A `lvl` |
| `tub` | a fixed ratio of `drv` | the patch LFO (`cv`) |
| `dmx` (32-sample tap mix) | 64 (the tap stays, K-D5) | — |
| `lst` (LFO mirror) | 0 (both cores together, as the patch) | — |
| `a1o`, `a2o` | the patch value (53) | the patch LFO, `vca` |
| `flf` | a fixed depth | `lfl` |
| `ors`, `oft` | moderate resonance; LP/HP by the sign of `cut` | — |
| `lfw` | sine, skewed by `lfm` | `lfm` |
| Kick decay, burst targets | ≈ 80 ms; noise and OSC A level | `kik`, velocity |

### 8.4 Selectors, tags, flags

- **New menu tags:** `src` (4 values) and `typ` on the filter page (6). Tags 4
  and 10–15 are free (`MenuText.h:192–411` defines 1, 2, 3, 5, 6, 7, 8, 9).
  The effect's `fx` row will reuse `ONOFF_TAG_FX_TYPE` (9).
- **Shared form `typ`** through `IM_SPECIAL_OSC_TYPE` with a Katran branch,
  as the Colloid.
- **Flags:** continuous rows `MORPHABLE | MODULATABLE | AUTOMATABLE`,
  converted in the async (no new special writers). `run` morphs. `src` and
  the filter `typ` are `AUTOMATABLE | SELECTOR`, never morphed.
- **Bipolar rows** (`dp2`, `cut`, `spr`, `vca`) use `DTYPE_PM63` with
  63 = centre (the one-pan rule's display convention, S075).

---

## 9. Ready for the Basic/Advanced file merge (D-R2-3)

**Not built here.** These are the constraints Katran follows now, so the
merge can treat `kat` exactly like `col`/`clx` and `alk`/`alx`:

| Constraint | Katran now | Precedent |
|---|---|---|
| **Effect rows last, after the `@advanced-rows` marker** | Basic rows 0..56 (57..59 free); indices 60..63 reserved for `fx_type`, `fx_rate`, `fx_depth`, `fx_mix`, in that order. `_Static_assert(KAT_PARAM_BASIC_COUNT <= 60)` | `ColloidParameters.c` marker before `fx_type`; `COL_IDX_FX_TYPE` 59 (`ColloidParameters.h:56`); `ALK_IDX_FX_TYPE` 31 |
| **The same keys** | `fx_type`, `fx_rate`, `fx_depth`, `fx_mix`; none of Katran's own keys starts with `fx_` | `ColloidParameters.c`, `AlkalineParameters.c:323` |
| **The effect is the last screen of `click`** | `click` uses cells 0..5; cells 8..11 (screen 3, the only screen 3 outside `lfo`) left empty for `fx dpt rte mix`; cells 6..7 may take a later Katran row, since they stay on screen 2 | `colloid_adv_menu_pages[]` (cells 8..11 of page 4) |
| **Labels free on that page** | TORCIDO dry/wet is `wet`, not `mix` | the effect's `mix` (`SHORT_FXM`) |
| **No unit for the effect** | Katran holds both units of its per-slot cap; its later effects (not the chorus) need none | §7.2; user 2026-11-24 |
| **Effect state in the slot** | the four row bytes are budgeted in §7.1; the effect's state must fit ≈ 74 B of reserve (more with the in-slot reserves) | §7.1 |
| **CPU** | Basic ≈ 17.0k; ≈ 3.2k a block left for the effect under the Advanced share | §6.2 |
| **`init.kat`** | written up to the marker, as `make_init_col.py` writes `init.col` | `ColloidParameters.c` marker comment |

What the merge itself will decide (not here): whether a Basic file writes
`fx_type off` or omits the four keys, and how the Load browser lists one
file under two types.

---

## 10. Platform integration (`INSTRUMENTS_DSP_REFERENCE.md` §9.2)

| Item | Katran |
|---|---|
| 1 Enum and registry | `INSTRUMENT_TYPE_KAT` after `INSTRUMENT_TYPE_ALX` (`InstrumentManager.h:141`). Registry row `"kat"`, `".kat"`, `"Katran"`, `INSTRUMENT_FLAG_BASIC` (`InstrumentManager.c:85–130`). `instrument_engine_of[]` KAT → KAT (`:157`). `menu_instrumentLoadIndex[]` +2 B `.bss` |
| 2 Files | `Core/DSP/Instruments/Katran/`: `KatranVoice.c/.h`, `KatranOsc.c/.h` (reset loops, arena forms), `KatranParameters.c/.h`, `KatranTables.c/.h` (delay time, tracking, run decay, CE times, LFO rate, kick decay; generated) |
| 3 Runtime | union member + `_Static_assert`; a Katran case at each Colloid/Tetrakt site in `InstrumentManager.c` (the schedule lists them); the `IM_SPECIAL_OSC_TYPE` writer; `instrumentManager_waveDomainMax()`; `menu_formatWaveCell()`; `instrumentManager_slotRightUpdate()` (right block always); an `instrumentManager_ampEnvelopeQuiet()` branch (R6) |
| 3a Units | `instrumentManager_unitsWanted()` returns 2 for KAT; `instrumentManager_slotVoiceUnits()` takes 0..2; the voice binds ordinals 0 and 1 |
| 3b Arena view | **New** read-only FxBuffer accessor `fxbuf_arenaView(const int16_t **base, uint32_t *samples)`. It amends FxBuffer's contract: an Instrument may read, never write, bytes it does not own |
| 3c Pending-apply fade | `presetManager.c`: the deferred applies (`:2218`, `:2380`) signal the slot; Katran fades `gr` over ≈ 20 ms, then reports quiet (Q9) |
| 4 Build | include path; `KatranParameters.c` in `SRCS`; voice, oscillator and tables in `DSP_SRCS` with explicit `-Ofast` rules (the Tetrakt's, `Makefile:394`) |
| 5 Host proof | `check_special_tags.py` `TABLES['KAT']`; `init.kat` generator and check; `tools/dsp_test/modules/katran.c`; `tools/dsp_test/katran_cpu.py` (the `tetrakt_cpu.py` / `colloid_cpu.py` pattern) |
| 6 Storage | append-only token; AutoSave stores `kat`; `SD_CARD/Instrument/Katran/init.kat`; `filesystem.c` creates the folder on a first boot without it (as `ColAdv`) |
| 7 MIDI | normal note mode only; note-on = kick; note-off ignored. The S102 chromatic overlay's single-type path (`midiNoteMode_noteOn/Off`) needs no change |

---

## 11. New components

| # | Component | New / reuse | Notes |
|---|---|---|---|
| N1 | Katran engine (voice, async, sync, trigger) | new | the Colloid/Tetrakt pattern |
| N2 | Oscillator loops with a per-sample hard reset and a wrap-carry output | new | the Colloid's loops have no resets; 3 forms + arena form, OSC A pair and OSC B single = 8 loops |
| N3 | **Arena read form** (window read, DC, damping, guard) | new | §3.4 |
| N4 | **`fxbuf_arenaView()`** | new, FxBuffer | read-only; contract change |
| N5 | Two fractional delay rings (one unit each), with a 32-sample tap | pattern reuse | `tkt_spaceOut()` (`TetraktVoice.c:796`), `voiceSpace.c` |
| N6 | TORCIDO shaper (cubic + tube) | new | D§5.2 |
| N7 | TPT SVF ×4 (F1 ×2, F3 ×2) | reuse candidate | `col_svfControl()` is `static` (`ColloidVoice.c:550`); sharing it is gated on the Colloid staying bit-identical, else a Katran copy |
| N8 | **Run gain** and its decay table | new | §3.1 |
| N9 | **Kick processor** (K1–K3, one-row base/offset) | new | per block only |
| N10 | **Chaos envelope** (cycling, RNG and loop-coupled) | new | a small module, `KatranCe`, reusable by a later type |
| N11 | Patch LFO with self-modulated rate and shape | new | D§5.7 |
| N12 | Pending-apply fade hook | new | `presetManager.c` |
| N15 | Two-unit claim | change | `instrumentManager_unitsWanted()`, `instrumentManager_slotVoiceUnits()` |
| N13 | Menu tags `src`, filter `typ`; the bipolar rows | new | `MenuText.h` |
| N14 | Host model, `katran_cpu.py`, synthetic arena dumps | new tooling | §15 step 1 |

---

## 12. Risks

| # | Risk | Effect | Mitigation |
|---|---|---|---|
| R1 | **CPU estimate error** | Over the 18.6k ceiling | Linked gate ≤ 360 in the spike; device stub run before the schedule; ladder §6.2 |
| R2 | **Mixer register re-allocation** (S101 P3 §4) | +1 instruction in unrelated mixer loops | `noinline` renders; the mixer hot-loop diff gates the spike |
| R3 | **Combined rows hide a lever the host model shows is needed** | A sound cannot be reached | 3 free indices (57–59); every fixed value in §8.3 is a named constant, so it can become a row without a redesign |
| R4 | **RAM near the line:** ≈ 1,190 B calibrated against the 1,200 B gate | Link gate fails | The two in-slot reserves (−100 to −120 B), no sign-off needed |
| R5 | **Arena read loudness:** float state, 8-bit lines and power-up garbage read as full-scale int16; outer feedback through the FX send | Painful levels; a loop pinned at full scale | `lvl`, `dmp`, DC block, the guard, output normalisation set on the loudest case; the bus compressor after it |
| R6 | **A voice that never goes quiet** gates deferred Scene/Kit applies (`presetManager.c:2218`, `:2380`) | Without the fade hook, a click and a delay before the swap | The pending-apply fade (Q9, §10 item 3c) |
| R7 | **Recall is not repeatable** with the arena form: the contents depend on the Effect, its share, the other slots' units and their history | A saved sound plays differently in another Scene or after boot | Inherent; documented. `src` is off in `init.kat` |
| R8 | **Two units per slot** shrink the Effect share fastest of any type, and fragment it on swaps (§7.2) | Shorter Delay/Echo maximum times than expected | Accepted (K-D3) |
| R8a | **A later Katran effect has no buffer memory and ≈ 74 B of state** | Limits which effects Katran can take | Effects chosen to fit; a buffer effect would reopen the per-slot cap |
| R9 | **The host model can't reproduce the arena** | The arena form can only be tuned by ear on the device | Synthetic dumps from the known layout; optionally a DEV-only arena dump to the card (diagnostic, last in queue, per the S099 P3 trace rule §0) |
| R10 | **Stop/boot semantics** (decided, Q3) surprise in use | Usability | Revisit after the device listening step |
| R11 | **Patch values are visual estimates** (D§1.3) | `init.kat` far from the AIRA sound | K-Q11 (an AIRA recording) |
| R12 | **Aliasing** from hard resets, `wtb` and the unlimited arena speed | Harsh at high pitches | Accepted for a noise voice (D§5.1) |
| R13 | **The merge's own rules are not written yet** | A later rule could need a Katran change | §9 follows the only precedent (`clx`, `alx`); Katran keeps no Basic row after index 59 |

---

## 13. Statements this plan would make out of date (for you to log, if adopted)

- `MEMORY.md`: the type list (Katran, the eighth type, Basic); voice-unit
  use; FxBuffer read access by an Instrument.
- `INSTRUMENTS_DSP_REFERENCE.md`: instrument count; §3.1 engine table and
  union; §3.2 dispatchers; a new engine section; §9.2 (the arena view; the
  reserved effect rows and `click` cells for merge-ready types).
- `EFFECTS_BUS_REFERENCE.md`, `EFFECTS_MODULES_DSP_REFERENCE.md`: the Effect
  share shrinks by two units per Katran; an Instrument may read arena
  contents.
- `BANK_PRESET_ARCHITECTURE.md`, `FILESYSTEM_SPEC.md`: the eighth type, its
  token, extension and folder.
- `STORAGE_SRAM_MANIFEST.md`: the union member; the two units; flash.
- `CPU_USE_DSP_AUDIT.md`: Katran's cost items.
- `MENU_UI_SPEC.md`: Katran's pages; the reserved effect screen.
- `knowledge_files/drafts/DRAFT_KATRAN_DRONE_SYNTH.md`: superseded in the
  parts listed in §2.2.

---

## 14. Questions

**Answered (revision 2):** Q1 (a) with D-R2-1; Q2–Q7 and Q9–Q12 as
recommended; Q8 (a), two units, confirmed.

**Open (needed before the schedule, not before the host model):**

| # | Question | Recommendation |
|---|---|---|
| Q13 | **The combined rows of §8.2**, in particular `kik` (burst then swell), `spr` (coupling below centre, spread above), `vca` (balance), `cut` (LP below, HP above), `dtn` (detune + position spread), `lfm` (self-mod + shape) | As listed. The host model renders each range end to end for your ear |
| Q14 | **The three free indices (57–59):** hold them for the host model's findings, or assign now. Candidates: the sub (`sub`), F1/F3 tube `tub`, OSC B's own `wav` (D§3.4) | Hold them |

---

## 15. Order of work

| Step | What | Gate | Deliverable |
|---|---|---|---|
| 1 | **Host model** (`tools/dsp_test/modules/katran.c`). WAVs: the patch alone (one core, D§1.3 values, Saw and Rec); the dual core; each combined row swept end to end (§8.2); `run` at several values from the top and from a stop; kicks at several velocities and `kik` values; CE1/CE2 at low and high chaos; the arena form on synthetic dumps (silence, a ring of a Katran's own output, CrumpBit 8-bit lines, float state, random garbage) | your ear (and the AIRA recording, K-Q11) | the laws and fixed values of §8.3 set; `init.kat` values; records in `tools/dsp_test/baselines/s103/` |
| 2 | **Spike** (scratch copy outside the tree): every loop form, the per-block path, a stub voice claiming two units | linked all-in ≤ 360 a sample (`elf_looppath.py --cond-heads --in-range`, `elf_blockpath.py`, `control_cost.py --elf`); arena form ≤ the dearest form; mixer hot loops unchanged; `sizeof(KatranVoice)` ≤ 1,200 | the counts; staged or fused (D§9.3); any ladder step |
| 3 | **Device stub run** (one flash, `DEV_MODE_LOGGING`, the `p` probe): one Katran, dearest form, stereo send | median ≤ 18.6k; target ≤ 18.2k | the measured record |
| 4 | **Schedule** (`S103_P2_KATRAN_IMPLEMENTATION.md`, or the next free number), every change by file and line | Q13, Q14 answered; you approve | |
| 5 | Registry, type, empty voice, two-unit claim and release, `fxbuf_arenaView()`, the pending-apply fade | builds; `check_special_tags.py`; Load lists `Katran`; Scene round-trip; the Effect share shrinks by 8,832 B per Katran and grows back; a Scene change over a drone fades without a click | |
| 6 | Oscillators (incl. the arena form), TORCIDO, delays, F1/F2, run, kick, CEs, ring, output | host laws: `gr` −60 dB at T(run); stop and kick per Q3/Q4; bounded loops; frame/window change at cycle start; linked counts equal to the spike's | |
| 7 | Parameters, pages (§8.2; `click` cells 8..11 empty), `init.kat` up to the marker | DEV self-check; host law for keys, labels, defaults, lanes; `KAT_PARAM_BASIC_COUNT <= 60` asserted; no `fx_` key among the Basic rows | |
| 8 | Device: one Katran, then six-slot Scenes (Katrans with Tetrakts, Colloids, Advanced types; the 144k-cap Scenes of S101 P2 §A.7) | ≤ 18.6k per Katran; no underruns; the `cpu` widget | measured record; by-ear values |
