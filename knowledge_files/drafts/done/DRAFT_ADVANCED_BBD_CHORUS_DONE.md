# DRAFT — Advanced BBD chorus: the Alpha Juno stereo chorus as an Advanced insert

- **Status:** draft proposal, 2026-11-19, tree `756b5b70` on
  `dev-ph7-alkaline`. **No firmware code is changed by this document.**
  - Figures marked *spike* are instruction counts of standalone kernels
    compiled with the Makefile's DSP flags (`-mcpu=cortex-m7 … -Ofast`) and
    counted with `tools/dsp_test/elf_looppath.py --cond-heads --in-range`
    (Appendix A). They are loop counts, not linked counts. LTO and register
    allocation in place can change them; the device probe is the gate.
  - Figures marked *measured* come from the device probe records named.
  - Figures marked *estimate* are not measured.
- **Decided (user, 2026-11-19):** no hiss on the Colloid. It already has
  its own tunable noise (`nlv nto nwd npn`). Hiss stays a **later, optional
  Alkaline-only** item (§5.2), because the Alkaline has no noise rows.
- **Decided (user, 2026-11-19):** the Colloid's chorus input is a
  **switch**, `csr`: each side's own channel (`L-R`) or the mid of both
  (`mid`) (§2.5). The slot is budgeted for the dearer `L-R`.
- **Decided (user, 2026-11-19): the Colloid goes first,** because it is the
  constrained host. The kernel is sized where the budget is tightest, and
  the Alkaline follows with the extras.
- **Brief (user, 2026-11-19):**
  - Write a stereo BBD chorus emulation of the Roland Alpha Juno's chorus,
    as an effect for the Advanced versions of instruments. The Colloid is
    first; the Alkaline or other types may follow.
  - When its effect type is selected, it uses **one 50 ms voice-unit
    allocation**: up to 25 ms each for L and R.
  - Say what fits in the Advanced extra budget, whether a reasonable
    emulation fits, which further options might fit, and what compromises
    are needed if it does not.
- **Binding rules that apply:**
  - CPU ceiling (S089 P2) and the cycle-based budget (S094 P2 §21): Basic
    19,620 and Advanced 21,803 cycles per block. **The Advanced extra is
    2,183 cycles per block, ≈ 68.2 cycles per sample.**
  - S093: per-block work counts (loops + (control + entry/exit) / 32).
  - Constant CPU: budget the dearest mode, and never skip work inside a mode.
    `mix` 0 still processes.
  - S085/§12 (`INSTRUMENTS_DSP_REFERENCE.md`): new code goes in new loops.
    No existing loop is edited, so the Basic Colloid and Alkaline stay
    bit-identical.
  - FxBuffer contract (`FxBuffer.h`): owners clear what they claim, and the
    share callback is foreground only.
  - RAM: any new or enlarged allocation needs sign-off (§3.4 gives the
    bytes). ITCM growth counts as RAM.
  - No backward compatibility and no migration.
- **Related:** `knowledge_files/drafts/DRAFT_COLLOID_ALKALINE_ADVANCED_FX.md`
  (S095 P5, 2026-11-13/14) proposed a shared modulated-delay kernel for
  Advanced Colloid/Alkaline. Its Colloid budget (12 oscillators, ≈ 3,800
  cycles of room) and its chorus estimate (≈ 38 per sample) are both out of
  date (§11). `DRAFT_S-R_PWM_WAVE.md` §7 left the Alpha Juno chorus to "the
  Effect bus's chorus-type modules"; this draft is the per-voice answer.

---

## 0. Summary

1. **What the Alpha Juno chorus is.** Two MN3009 BBDs (256 stages), each
   with its own MN3101 clock driver. One triangle LFO sweeps both lines,
   with the right line's modulation inverted (180°). Each output is dry plus
   its own line. There is no compander, and the lines are band-limited
   before and after. It is the Juno-60 / JX-3P circuit with one difference:
   **the rate is variable** (§1).
2. **The model (§2).** Two fractional-delay taps over int16 rings, one
   pre-BBD pole and one post-BBD pole per side, the LFO and delay law
   computed per block, and dry + wet × g per side. Juno-60 depth: the delay
   sweeps 1.66–5.35 ms (73–236 samples). The rings use **1,024 samples
   (23.2 ms) per side** out of the unit's 1,104, so that a mask does the wrap
   (§3.1).
3. **Memory: one voice unit, claimed when `cho` ≠ off (§3).** It holds both
   rings, their guard samples and the chorus state (288 B spare in the
   unit). The `ColloidVoice` struct grows by ≤ 12 B (1,212 → ≤ 1,224 of the
   1,264 B reserve). A slot never owns more than one unit, so the Effect
   share's worst case is unchanged (100,160 B, as with six Tetrakts today).
4. **CPU (*spike*, §4).** The kernel must run **one channel per pass**. A
   single stereo loop spills about 16 values and costs 66–69 per frame; two
   passes of the per-channel loop cost 2 × 22.
   - **`L-R`, stereo in** (the brief's L/R rings): ≈ **49 per sample**,
     including per-block work and a dry gain, ≈ **1,980 … 2,610 cycles per
     block** at 1.2 … 1.6 cycles per instruction.
   - **`mid`, mono in** (the Juno's own topology): ≈ **46 per sample**,
     ≈ **1,870 … 2,460**.
   - **The `csr` switch picks one form per block (§2.5).** Both forms write
     both rings, so a switch is seamless and needs no memory action. Under
     the constant-CPU rule the slot is charged for `L-R`, so `mid` is a
     sound choice, not a saving.
5. **Does it fit the Advanced extra (2,183)?**
   - **Colloid: just.** Its dearest mode (pwm Saw/Rec 6) measured a median
     of 19,136 and a worst 1.5 s window of 19,766 (S096 P3). The budget
     figure is `L-R`'s. Against the median it fits at any plausible CPI
     (687 / 367 / 57 cycles spare at 1.2 / 1.4 / 1.6). Against the worst
     window it fits only at 1.2 CPI (57 spare). **A reasonable emulation
     fits, with the switch. The device probe decides, and §6 lists the cuts
     if it is over.**
   - **Alkaline: easily.** It has ≈ 5,200 cycles of room in an Advanced
     slot. The chorus leaves ≈ 2,800 … 3,400 for the options of §5.2.
6. **Options that cost nothing per sample (§5.1):** modes I / II / I+II
   (Juno-60), `var` (Alpha Juno variable rate and depth), a doubler mode
   that uses the rest of the 23 ms, LFO shape and clock law, tempo sync,
   width, and a **post-filter that follows the delay** (the BBD's clock
   falls as the delay grows). All are per-block.
7. **Options that cost per sample (§5.2):** a BBD overload clip, a 12 dB
   wet filter, a third tap (ensemble), feedback (flanger). They are
   for the Alkaline; on the Colloid only if the probe leaves room.
8. **If the Colloid is over (§6), in order:** drop the pre-pole, the
   chorus loop into ITCM, a half-rate wet path, the Colloid noise-SVF
   fallback, and last, giving up the switch (`mid` only). A truncated
   (non-interpolated) read is rejected (zipper).
9. **Row budget: tight on the Colloid (§7).** It has 59 of 64 rows
   (`INSTRUMENT_PARAM_COUNT`), so the Advanced Colloid has at most 5 chorus
   rows: `cho crt cdp cmx csr`. The switch takes the fifth row, so there is
   no `cbw` tone row on the Colloid: the wet tone is fixed by ear. That
   leaves no spare row.

---

## 1. Research: the Alpha Juno chorus

### 1.1 Sources and what they agree on

- **Parts (polynominal, Alpha Juno 2 page):** "an analog stereo chorus based
  on 2 X MN3101 + 2X MN3009 IC like Juno-60 or Jx-3p with adjustable rate."
  That is two lines, each with its own clock. The Alpha Juno 1, 2 and the
  MKS-50 share the circuit (MatrixSynth / Juno-family write-ups: "the same
  … BBD chorus chips as the rest of the 80's Roland classics, with one
  major difference: variable chorus rate").
- **The MN3009:** 256 stages; delay 0.64–12.8 ms for a clock of 200–10 kHz
  (datasheet, quoted in the sources). Delay = 256 / (2 f_clk).
- **The Juno-60 topology and depth** (pendragon-andyh, "Juno60/Chorus",
  from the schematics and recordings). This is the closest circuit with
  published figures:
  - "1 triangle-wave LFO, modulating 2 256-step Bucket-Brigade
    delay-lines (1 for left and 1 for right)"; "the modulation signal of
    the right delay-line is inverted so it is effectively 180 degrees out
    of phase".
  - Rates and sweeps:

    | Mode | LFO | Delay min … max | In samples at 44,108 Hz |
    |---|---|---|---|
    | I | 0.513 Hz | 1.66 … 5.35 ms | 73.2 … 236.0 (centre 154.6, ±81.4) |
    | II | 0.863 Hz | 1.66 … 5.35 ms | the same |
    | I+II | 9.75 Hz | 3.3 … 3.7 ms | 145.6 … 163.2 (centre 154.4, ±8.8) |

  - "A 12dB low-pass filter is used before the signal is sampled into the
    Bucket-Brigade chips."
  - No compressor/expander: "you will get distortion if the input signal
    is high."
- **What these give (derived):**
  - **Pitch swing.** A triangle sweep of ±81.4 samples gives a constant
    pitch offset of 2 × 162.8 × f_LFO / f_s. That is ±6.5 cents in mode I,
    ±11.0 cents in mode II and ±13.4 cents in I+II (sweep ±8.8 at 9.75 Hz).
    The two sides are always opposite: one sharp while the other is flat.
    That opposition is the Juno's stereo image.
  - **The BBD's sampling rate.** f_clk = 256 / (2 d): 77 kHz at 1.66 ms,
    36.6 kHz at the 3.5 ms centre, and 23.9 kHz at 5.35 ms. The pre- and
    post-filters must remove its images, so the wet path is band-limited to
    roughly 10 kHz or below. The limit is lowest at the long end of the
    sweep.

### 1.2 What is not settled, and what this draft assumes

- **The Alpha Juno's rate range** (its 0–127 "Chorus Rate") was not found
  in the sources. **Assumption:** a log map over about 0.1 … 10 Hz, which
  covers the Juno-60's three rates. Chosen by ear (D-6).
- **The Alpha Juno's depth.** No figure was found. **Assumption:** the
  Juno-60's 1.66–5.35 ms. The same chip, clock driver and family make this
  likely, but it is not confirmed.
- **The filter cutoffs and orders** after the BBD. Only the pre-filter's
  order (12 dB) is published. **Assumption:** one pole before and one after
  per side (12 dB total on the wet), with a cutoff chosen by ear around
  8–10 kHz (D-7). §5.1 adds an optional delay-tracking cutoff.
- **The clock law.** The LFO drives the MN3101's VCO, so the clock may be
  linear in the LFO, and the delay (∝ 1/f) would then be a bent triangle.
  This is unknown, and it is free to choose per block (§5.1).
- **Dry/wet balance.** It is not published reliably. The default is equal
  levels, chosen by ear (D-7).
- **The Alpha Juno's own fixed HPF** before the chorus is a voice-filter
  matter and is not covered here (`DRAFT_S-R_PWM_WAVE.md` §1.1).

---

## 2. The model

### 2.1 Signal flow

```
                 ┌─ pre pole ─► ring L (1,024) ─► tap at dL(t) ─► post pole ─► × g ─┐
dry L ──────────┤                                                                  + ─► L out
                 └──────────────────────────────────────────────────────── × gd ───┘
                 (R: the same with ring R and dR(t) = 2c − dL(t), the inverted LFO)

mono-in form:   (L + R)/2 ─► pre pole ─► one ring ─┬─► tap dL(t) ─► post ─► + L
                                                   └─► tap dR(t) ─► post ─► + R
```

- **Where it sits.** After the Colloid's whole chain (filters, overdrive,
  amp, tone), as a new pass over the two int16 blocks that
  `col_renderPar()` / `col_renderSer()` write (`outL`, `voice->right`).
  That is pre-volume and pre-pan, as the mixer expects. Neither existing
  loop changes.
- **The mono route** (`rte` a filter type, `right = left`) gets a stereo
  chorus from a mono voice, at the same cost. That is the Alpha Juno
  itself: a mono voice into a stereo chorus. There, `mid` and `L-R` are the
  same signal, and `csr` shows as inactive (§2.5).

### 2.2 Per block (control, ≈ 34 instructions, *spike*)

- Step the LFO phase (a 32-bit phase, triangle from the phase's fold).
- Slew the depth toward its target (a depth jump would otherwise move the
  delay by up to 3.7 ms in one block, an audible pitch blip).
- `dL_end = c + depth × tri`, `dR = 2c − dL` (the inverted LFO), or
  `c − w (dL − c)` with a width `w` (§5.1).
- Set each tap's read position `rp = (w << 16) − d_now` (Q16, exact at
  every block edge, so it cannot drift) and its per-sample step
  `inc = 65536 − (d_end − d_now) / 32`.
- **The delay moves linearly inside a block.** Any LFO shape or clock law
  is therefore computed only per block, at no per-sample cost. The error
  against a per-sample triangle is only at its two corners, within one
  0.73 ms block.

### 2.3 Per sample, per channel (the spike's `cho_sideD`, 22 instructions)

```c
p = smlawb((x << 1) - p, kk, p);              /* pre pole (state x2), kPre in kk.b */
ring[w & 1023] = p >> 1;                      /* write */
pair = *(uint32_t *)&ring[(rp >> 16) & 1023]; /* [i], [i+1]: guard at 1,024 */
f = (rp >> 2) & 0x3FFF;                       /* Q14 fraction */
t = smuad(pair, (f << 16) | (0x4000 - f)) >> 13;  /* linear interpolation */
o = smlawt(t - o, kk, o);                     /* post pole, kPost in kk.t */
out = ssat16(smlawb(o, g, x));                /* dry + wet x g */
w++; rp += inc;
```

- **The guard sample.** The ring is 1,024 samples plus one guard at index
  1,024. The pair read `[i], [i+1]` therefore never wraps, and one unaligned
  32-bit load (allowed on the M7 for DTCM) feeds `SMUAD`. The guard is
  refreshed once per block, after a block in which the write wrapped
  (`ring[1024] = ring[0]`).
  - This is correct while **d ≥ 2 × 32 + 1 = 65 samples (1.47 ms)**. At
    that delay the tap reaches index 1,023 at least two blocks after
    `ring[0]` was written. The Juno-60's minimum is 73. A flanger (§5.2)
    breaks this rule.
  - The longest delay is 1,022 samples (23.17 ms), because the pair read
    must not reach the frame being written.
- **Fixed point throughout.** The states are int32 holding int16 × 2, so
  that `SMLAWx` (× Q16 / 2^16) gives Q15 coefficients. No float, no VDIV.
- **The dry gain `gd`** (not in the spike; +1 per channel). Dry and wet
  each near full scale sum to +6 dB into a hard `SSAT`. Either the dry is
  scaled (`smulwb` first) or `cmx` alone sets the balance and the voice
  clips (D-7).

### 2.4 What is emulated and what is not

| BBD trait | Emulated? | How |
|---|---|---|
| Two lines, one triangle LFO, R inverted, the Juno-60 sweep and rates | **Yes** | §2.2 |
| Wet band-limit (pre- and post-filters) | **Yes**, 6 dB + 6 dB per side | one pole each |
| Delay-dependent bandwidth (clock falls as delay rises) | **Optional, free** | §5.1, per-block cutoff |
| Clock law (bent triangle) | **Optional, free** | §5.1 |
| Hiss | **Colloid: no** (its own noise rows cover it). **Alkaline: later option**, +9 (*spike*, plain C) per channel | §5.2 |
| Overload distortion (no compander) | Optional, ≈ +1 … +4 per channel | §5.2 |
| BBD sampling images, clock bleed, charge-transfer loss | **No** | below audibility after the filters; not worth a per-sample cost |

### 2.5 The source switch `csr` (Colloid; user, 2026-11-19)

| `csr` | Ring L gets | Ring R gets | Taps | Loops | Per sample |
|---|---|---|---|---|---:|
| `L-R` | L | R | L from ring L, R from ring R | `cho_sideD` × 2 | ≈ 49 |
| `mid` | (L + R) / 2 | the same mid (a second store) | L from ring L, R from ring R | pass A (`cho_monoA` + the mirror store), then pass B on ring R | ≈ 46 |

- **Both forms write both rings, every sample.** Each ring therefore
  always holds the last 23 ms of the selected source.
  - On a switch, each tap keeps reading its own ring. The wet input
    changes over at most one delay (≤ 5.35 ms in the Juno modes), as if
    the input had been crossfaded. No ring holds stale audio, nothing is
    cleared, and memory does not change.
  - The mirror store costs `mid` ≈ +1 per sample (*estimate*; not in the
    spike). `mid` stays below `L-R`, which sets the budget anyway.
- **So `csr` is an ordinary Colloid selector:** `AUTOMATABLE | SELECTOR`,
  never morphed, as `rte` and `sus` are (`ROW_SELECT_ONOFF`). A step lock
  on it is safe, unlike `cho` (§3.2).
- **The form is chosen once per block** (one branch in the sync render).
  That is a mode under the constant-CPU rule: no work is skipped inside
  it, and the slot is charged for the dearer mode, `L-R`.
- **What you hear:**
  - `L-R`: the wet keeps the Colloid's spread (`spr`, `mot`, noise pan).
    Each side choruses its own channel.
  - `mid`: the Juno, exactly. One signal feeds both lines, and the stereo
    comes only from the opposed sweeps. The dry stays fully stereo in both
    modes.
- **Mono route** (`rte` a filter type): L = R, so the two modes sound the
  same. `colloidParameters_cellInactive()` marks `csr` inactive there, as it
  does `spr` and `mot`. The pass still runs the selected form (constant
  cost).
- **The Alkaline has no `csr`.** It is a mono voice, so it runs the mid
  form only, with no add at the input.

---

## 3. Memory: the one voice unit

### 3.1 Layout of the unit (2,208 samples, 4,416 B, DTCM)

| Samples | Use |
|---|---|
| 0 … 1,023 | ring L |
| 1,024 | guard L |
| 1,025 … 1,031 | pad (keeps ring R 4-byte aligned for the pair load) |
| 1,032 … 2,055 | ring R (both `csr` forms write both rings, §2.5) |
| 2,056 | guard R |
| 2,057 … 2,063 | pad |
| 2,064 … 2,207 | **288 B: the chorus state** (≈ 64 B used: write index, two read positions and steps, four pole states, LFO phase and step, depth now, delay now, packed coefficients, gain) |

- **Why 1,024 and not the full 1,104 (25.03 ms) per side.** With a
  power-of-two ring, the wrap is one AND. The full 1,104 needs a compare per
  pointer per frame, or the Tetrakt's segmented loop (S096 P1 §17: ≤ 4
  segments, 58 instructions each, per block, ≈ 7 per sample). The Juno
  modes use at most 237 samples. The doubler mode (§5.1) is the only user
  above that, and it loses 1.9 ms of range (23.2 instead of 25.0 ms).
  **Recommendation: 1,024 (D-3).**
- **State in the unit, not in the voice.** `ColloidVoice` is 1,212 B
  (*measured* `sizeof` on this tree) against the 1,264 B reserve, so 52 B
  are left. ≈ 64 B of state does not fit there; the unit's tail holds it
  with room to spare. The voice keeps only the rows and the unit pointer.

### 3.2 Claimed when the chorus type is selected (the brief)

- **The rule.** The slot owns one unit while `cho` ≠ off, and none while it
  is off.
  - The claim and release go through `instrumentManager_slotVoiceUnits()`
    (`InstrumentManager.c:473`). Today `wanted` is 1 for a Tetrakt and 0
    otherwise. It becomes 1 for an Advanced chorus type with `cho` ≠ off.
  - It is reached from the same callers as now (type assignment,
    `instrumentManager_resetRuntimeSlotFromScene()` on Scene change and
    Instrument Load), and from a committed `cho` edit.
- **This changes the stated FxBuffer contract.** Units are "claimed at
  type assignment, never at a row change (plan §10)"
  (`InstrumentManager.c:466`). The brief asks for a claim at a row change.
  What makes that safe:
  - **`cho` is not automatable and not morphable.** The Colloid's
    selectors are `AUTOMATABLE | SELECTOR` (`ROW_SELECT_ONOFF`,
    `ColloidParameters.c:76–92`). A step lock on `cho` would claim from
    the automation drain, inside `audio_check_and_render()`, between
    blocks. `cho` needs a selector shape **without** `AUTOMATABLE` (a new
    row shape; there is no precedent in the Colloid table).
  - **Every remaining path is foreground.** These are a menu edit, a MIDI
    write (a menu edit, Q1; `midiIn_service()` runs in the main loop,
    `main.c:282–284`), a Scene change and an Instrument Load.
  - **The claim clears the unit and seeds the state,** about 4.4 KB of
    stores (S095 P1 SD5 measured a 32 KB clear at ≈ 5 % of a block, so
    this is ≈ 0.7 %). It is foreground, outside the render.
- **What the user hears when `cho` changes during play:**
  - The bus Effect's share shrinks or grows by 4,416 B, and the share
    callback reseats the Effect (CrumpBit and the Space types keep the
    newest audio). The longest Delay / CrumpBit `dly` times may be clamped
    while the unit is held. This is the path a Tetrakt type assignment
    already takes.
  - The chorus starts from a silent ring: its wet path fades in over the
    first delay (≈ 3.5 ms).
- **The claim cannot fail.** At most one unit per slot, six slots, twelve
  units. A unit released below a still-claimed one leaves a hole, and the
  next claim fills it (lowest free index nearest the top).
- **The alternative** is to claim at type assignment, so an Advanced
  Colloid always owns its unit. Toggling `cho` then never reseats the bus
  Effect, but 4.4 KB of Effect share is held even with the chorus off
  (D-1).

### 3.3 The Effect share

| Case | Units | Effect share |
|---|---|---|
| No units | 0 | 126,656 B (*measured*, `link_budget.py` on `build/lxr02.elf`) |
| Two Advanced choruses | 2 | 117,824 B |
| Four Tetrakts + two Advanced choruses (the most this proposal allows) | 6 | 100,160 B |
| Six Tetrakts (reachable today) | 6 | 100,160 B |

The worst case does not move. The Space Reverb's 64 KB ring still fits.

### 3.4 RAM for sign-off

| Item | Bytes | Region | Owner |
|---|---|---|---|
| Voice unit (claimed, not new) | 4,416 per chorus slot | DTCM, `.dtcm_fxbuf` arena | the slot's runtime, while `cho` ≠ off |
| `ColloidVoice` growth: `cho crt cdp cmx csr` (5 B) + unit pointer (4 B), padded | ≤ 12 | SRAM1, inside the existing 1,264 B union slot (1,212 → ≤ 1,224) | `ColloidVoice` |
| Alkaline (if it follows): the same ≤ 12 B + `right[32]` (64 B) | ≤ 76 | the same union slot (564 → ≤ 640) | `AlkalineVoice` |
| ITCM | 0 | — | — |

No allocation grows: the union slot's size is unchanged. The bytes are
listed for the record.

---

## 4. CPU

### 4.1 The budget

- **Advanced extra:** 21,803 − 19,620 = **2,183 cycles per block**
  (≈ 68.2 per sample). The S094 P2 §21 sum already includes two Advanced
  slots, so it is reserved budget, not new budget.
- **The Colloid's dearest mode** (*measured*, S096 P3 third run,
  `tools/dsp_test/baselines/s096p3/device_probe_p3_basic.txt`):

  | Mode | Median | p95 | Max window |
  |---|---:|---:|---:|
  | wav 10 | 18,362 | 18,701 | 19,085 |
  | wtb 10 | 18,681 | 18,877 | 19,211 |
  | **pwm 6** | **19,136** | 19,562 | **19,766** |
  | wts 6 | 19,124 | 19,363 | 19,705 |

  - The 9 windows above 19,620 were all next to an edit (wave, `psp`,
    `pos`, table). The Colloid already sits at its Basic share.
  - The `R-S` / `REx` chop waves (`DRAFT_S-R_PWM_WAVE.md` §6.3) are
    cheaper than pwm Saw/Rec 6 and do not change the dearest mode.
- **The Alkaline's dearest:** 16,591 (S094 P2 run 2, segment E, the cold
  first instance; not re-measured since). The S096 P3 Scene's Alkalines
  had medians of 15,897 and 15,644. Room in an Advanced slot ≈ **5,200**.

### 4.2 Spike counts (Appendix A)

| Kernel | Loop per sample | Entry/exit per call | Notes |
|---|---:|---:|---|
| One stereo loop, two rings (`cho_passS`) | 66 | 43 | ≈ 16 spills to the stack: rejected |
| The same, `SMUAD` + packed coefficients (`cho_passS2`) | 69 | 46 | still spills: rejected |
| One stereo loop, 12 dB wet + integer cubic clip (`cho_passF`) | 123 | 54 | the 64-bit cubic is dear: rejected |
| **Per channel, `SMUAD` read (`cho_sideD`)**, called for L then R | **22** | 24 | no spills |
| Per channel, plain C interpolation (`cho_side`) | 25 | 27 | |
| **Mono in, pass A** (mid, pre pole, write, tap L) (`cho_monoA`) | **25** | 28 | |
| **Mono in, pass B** (tap R only) (`cho_monoB`) | **15** | 13 | also the cost of each extra tap |
| Per channel + hiss (`cho_sideHiss`, plain C 64-bit scale) | 31 | 27 | +9; a `SMULWB` form is likely ≈ +3 (*estimate*). Alkaline-only, later |
| Control (`cho_control`) | — | 34 straight | per block |

**Per sample, per-block work included** (S093 rule):

| Form | Loops | Entry/exit / 32 | Control / 32 (+ ≈ 10 call glue, *estimate*) | Dry gain | **Total** |
|---|---:|---:|---:|---:|---:|
| `L-R`, stereo in (`cho_sideD` × 2) | 44 | 1.5 | 1.4 | +2 | **≈ 49** |
| `mid`, mono in (A + B, + the ring-R mirror store, §2.5) | 41 | 1.3 | 1.4 | +2 | **≈ 46** |

### 4.3 Cycles

- **Cycles per instruction.** The Colloid measures 1.37 … 1.49 (float
  loops in ITCM) and the Tetrakt ≈ 1.32. These are integer loops in flash
  reading DTCM, with dependent multiplies. The draft plans on **1.2 … 1.6**.
- **Cold I-cache.** The chorus code is ≈ 250 B (≈ 8 lines). Allow ≈ 100
  cycles per block (*estimate*; the whole Colloid's measured penalty is
  ≈ 470 for ≈ 4 KB).

| Form | CPI 1.2 | 1.4 | 1.6 |
|---|---:|---:|---:|
| `L-R` (the budget figure), cycles per block | 1,980 | 2,300 | 2,610 |
| `mid`, cycles per block | 1,870 | 2,160 | 2,460 |
| *`L-R` against the Advanced extra (2,183)* | +203 | −117 | −427 |

### 4.4 Colloid Advanced: does it fit 21,803?

The slot is charged for `L-R`, the dearer side of the switch (§2.5):

| Base | CPI 1.2 | 1.4 | 1.6 |
|---|---:|---:|---:|
| pwm 6 median 19,136 | 21,116 (**687** spare) | 21,436 (**367**) | 21,746 (**57**) |
| pwm 6 max window 19,766 | 21,746 (**57**) | 22,066 (**−263**) | 22,376 (**−573**) |

- **Answer: yes, a reasonable emulation fits, just, with the switch.** It
  fits the median at every CPI. The worst window (an edit transient) is the
  margin case and fits only at 1.2.
- **Why the Colloid is built first** (user): this is the tightest host.
  If the kernel fits here, it fits everywhere, and the Alkaline's extras
  are then sized against a known kernel.
- **If the probe is over,** §6 applies in order. Giving up the switch
  (`mid` only, ≈ −3 per sample, ≈ −140 cycles) is the last cut, not the
  first.
- **This was a near miss.** The S096 P1 Tetrakt S1 loop (one unit, two
  fixed fractional taps) was estimated at 25–30 per sample and first
  measured at 84 (S096 P1 §16 F3, §17). Here, the first stereo loop
  measured 66 against a paper figure of ≈ 44. Splitting the channels is
  what fixed it. The linked loop must be gated the same way (§9).

### 4.5 Alkaline Advanced

- The Alkaline is mono, so it runs the **mid form only**, with no `csr`
  and no add at the input. That is exactly the Alpha Juno.
- **It follows the Colloid** (user, 2026-11-19) and reuses its kernel as
  built and probed.
- 16,591 + 1,820 … 2,390 = 18,411 … 18,981. That leaves **≈ 2,800 …
  3,400** of the Advanced slot for §5.2.
- Numerically the chorus would even fit a Basic Alkaline (≈ 3,000 of room).
  The S089 P2 ceiling forbids raising a Basic type's dearest setting
  without a matching saving, so it belongs to the Advanced type.
- **New for the Alkaline:**
  - a `right[32]` block;
  - `instrumentManager_slotRight()` returns it while `cho` ≠ off, and NULL
    otherwise, so the mono mixer path is unchanged with the chorus off;
  - the mixer's per-slot pan then acts as a balance on the two blocks, as
    for the Colloid (`mixer.c:1436–1460`).
  - **The mixer's stereo path costs more than its mono path per slot.**
    This is in the shared mixer share and is measured (§9).

---

## 5. Additional options

### 5.1 Free per sample (per-block work only)

These change only what §2.2 computes, so the loop and its cost stay the
same. They are offered on both hosts.

| Option | What | Rows |
|---|---|---|
| **Modes** | `I`, `II`, `1+2` (the Juno-60 table, §1.1) and `var` (the Alpha Juno: rate from `crt`, depth from `cdp`) | `cho` values |
| **Doubler** `dbl` | centre ≈ 12–20 ms, small slow sweep, the R tap mirrored: a wide double that uses the rest of the 23 ms | a `cho` value |
| **LFO shape** | triangle (Juno), sine, or the **bent clock-law triangle** (delay ∝ 1/clock) | fixed per mode, or a row if one is free |
| **Tracking bandwidth** | the post-pole cutoff follows the delay: it is lowest at 5.35 ms, where the real clock is 23.9 kHz | Colloid: fixed by ear (no row left, D-4/D-7); Alkaline: the `cbw` law |
| **Tempo sync** | `crt` read through the existing LFO sync tables (`lfoRateHz[]` / `IM_SPECIAL_LFO_SYNC`) | needs a row, or a `var` sub-value |
| **Width** | `dR = c − w (dL − c)`: w = 1 is the Juno, w = 0 makes the wet mono | needs a row |
| **LFO retrigger on note** | phase reset at the trigger | needs a row |

### 5.2 Costed per sample

| Option | *Spike* / *estimate* per sample | Cycles per block at 1.4 | Colloid | Alkaline |
|---|---|---:|---|---|
| BBD overload: `SSAT` at a lower ceiling on the write (clips about 6 dB before the dry path) | ≈ +1 per channel (*estimate*) | ≈ +90 | likely | **yes** |
| Soft overload (polynomial, `SMULWB` form) | ≈ +4 per channel (*estimate*; the spike's 64-bit cubic was +28) | ≈ +360 | no | **yes** |
| 12 dB wet (a second post pole) | ≈ +2 per channel (*estimate*) | ≈ +180 | no | **yes** |
| Hiss on the wet, **Alkaline only, later** (hardware RNG, `GetRngValue()`'s register; the Colloid uses its own noise rows) | +9 per channel in plain C (*spike*); ≈ +3 with `SMULWB` (*estimate*) | +130 … +400 (one RNG value for both taps: half) | **no** (user) | later option |
| Ensemble: a third tap at 120° (pass B again) | **+15** (*spike*) + ≈ 0.4 | ≈ +690 | no | **yes** |
| Feedback (flanger) | ≈ +2 per channel, plus a per-frame guard (+2), because short delays break the d ≥ 65 rule (§2.3) | ≈ +360 | no | possible; a separate loop |

A realistic Alkaline "full" set (soft overload + 12 dB wet, both taps) is
about +12 per sample, ≈ 540 cycles. That is well inside its ≈ 2,800
of remaining room. The ensemble tap would also fit, and so would hiss if
it is added later (+≈ 130 … 400).

---

## 6. Compromises if the Colloid is over

In order of preference. Each is one build-time form behind a constant (the
`COL_CHOP_READ_FORM` / `ALK_PWM_READ_FORM` precedent), not a row.

| # | Change | Saves per sample | Cycles at 1.4 | Sound |
|---|---|---:|---:|---|
| C1 | Drop the pre-pole (the post-pole stays: a 6 dB wet) | ≈ 2 per channel | ≈ 180 | A slightly brighter, grainier wet |
| C2 | The chorus loop into ITCM (≈ 150 B for `cho_sideD`, ≈ 280 B with both `csr` forms) | the cold misses only (≤ ≈ 100) | ≤ 100 | None. **It competes with D-9 of the S-R draft** (the chop loop, ≈ 300 B of the 424 B free); both do not fit without D-10's ≈ 810 B move |
| C3 | **Half-rate wet:** write and read every other sample, hold the wet for two outputs, post-pole at full rate | ≈ 15 (*estimate*) | ≈ 670 | A wet bandwidth ≤ 11 kHz (BBD-like at the long end). The hold's images near 22 kHz − f are only partly removed by one pole: a host image-level check first |
| C4 | The Colloid noise-SVF fallback (S096 P3 §3.2) | ≈ 8 | ≈ 380 | A change to the base engine's noise. It applies to the Basic Colloid too, unless the Advanced type runs its own loop copy |
| C5 | **Give up the switch:** `mid` only, `csr` removed (its row comes free) | ≈ 3 | ≈ 140 | The wet loses the Colloid's spread; the Juno sound stays. Last, because the switch is a user decision |

**Rejected:**
- **A truncated (drop-sample) read.** It saves ≈ 4 per channel. The tap
  crosses a sample boundary ≈ 170 times per second in mode I (≈ 280 in
  mode II), and every crossing is a step: audible grit on sustained notes.
- **Fewer Colloid oscillators in the Advanced type.** A pwm pair is ≈ 2,800
  cycles. It would free plenty, but the user's decision is six (S-R draft
  §6.3, §6.5), and an "Advanced" Colloid with fewer oscillators than the
  Basic one is backwards.
- **Fusing the chorus into a copy of `col_renderPar()`.** It saves only the
  int16 round trip (≈ 2 per channel), costs ≈ 1.1 KB of flash per route,
  and the filter loop's live registers would bring back the spills.

---

## 7. The Advanced type, rows and menu

### 7.1 The type

- **Colloid Advanced:** a new registry type (token and extension D-5; the
  S095 P5 draft suggested `clx`). It has `INSTRUMENT_FLAG_ADVANCED` and is
  subject to the kit cap of two (`instrumentManager_typeSelectableForSceneSlot()`).
- It runs the Colloid engine unchanged, then the chorus pass. The Basic
  Colloid stays bit-identical, because its loops are not touched and its
  rows keep their indices.
- **Descriptor table.** The Colloid's 59 rows, then the chorus rows, at
  the same indices. If the registry allows a type to point at the Colloid's
  table with its own count, the Basic type's 59 rows and the Advanced type's
  64 can share one table: ≈ 1.2 KB of flash saved against a copy. This is
  to be checked in the plan. Flash headroom is 32,888 B (*measured*,
  `link_budget.py` on `build/lxr02.elf`).
- **Converting** a Basic file to Advanced is a load into the other type,
  with no converter (standing rule).

### 7.2 Rows: 5 at most (59 + 5 = `INSTRUMENT_PARAM_COUNT` 64)

| Row | Short | Range / values | Flags | Used by |
|---|---|---|---|---|
| Chorus type | `cho` | `off I II 1+2 var dbl` | selector, **not automatable**, not morphed (§3.2) | all; claims the unit |
| Rate | `crt` | 0..127, log ≈ 0.1 … 10 Hz (D-6) | morphable, automatable | `var`, `dbl` (inactive cell otherwise, via `colloidParameters_cellInactive()`) |
| Depth | `cdp` | 0..127 → 0 … ±81.4 samples (Juno I/II at 127) | morphable, automatable; slewed per block | `var`, `dbl` |
| Mix | `cmx` | 0..127, default equal dry/wet | morphable, automatable | all |
| Source | `csr` | `L-R mid` | selector, automatable, never morphed (§2.5) | all; inactive in a mono route |

- **The minimal Alpha Juno is two rows** (chorus on/off and rate). `cdp`
  and `cmx` make the Juno-60 modes and the doubler useful, and `csr` is the
  switch.
- **The switch takes the fifth row, so the Colloid has no tone row**
  (`cbw`). The wet cutoff and its tracking (§5.1) are fixed by ear (D-7).
  The Advanced Colloid then has no spare row for any later Colloid feature.
  Freeing a row means folding something, for example `csr` into the `cho`
  values (12 values instead of 6), which is not recommended.
- **Names.** None of `cho crt cdp cmx csr cbw` is used in `Core/Menu` or
  `Core/DSP` (checked). `cho` and `csr` each need a new on/off tag list
  for their value names (`MenuText.h`).
- **Menu.** The MIX page's second screen (cells 4–7) is empty
  (`ColloidParameters.c`, page 7): `cho crt cdp cmx`, with `csr` on the
  third screen. No SELECT position moves.
- **Alkaline Advanced** has 30 rows. It takes `cho crt cdp cmx`, plus
  `cbw` (the tone row the Colloid cannot have) and rows for any
  §5.1/§5.2 options that need one (width, overload; a hiss level if hiss is added later).

---

## 8. Integration points

- **The quiet test.** `instrumentManager_ampEnvelopeQuiet()` gates the
  deferred kit/Scene image commit (`presetManager.c:2218`, `:2380`). After
  the amp envelope idles, the ring still holds up to 236 samples (Juno
  modes) or 1,022 (`dbl`). The chorus's quiet test must also count the
  blocks since the envelope went idle: ≥ 8 blocks for the Juno modes, ≥ 33
  for `dbl`. This is a counter per block. Otherwise a commit at "quiet"
  cuts the wet tail.
- **Scene change, same type.** The slot keeps its unit (the
  `slotVoiceUnits()` "already owns the right count" path), so the ring and
  state carry over with no click. A Scene whose `cho` differs claims or
  releases, foreground, through the same function.
- **FxBuffer handoff.** Record the unit's write position per Scene switch,
  as the Tetrakt does for its line (`fxbuf_handoffBeginExit()` voice
  entries).
- **Constant CPU.** With `cho` ≠ off, the pass runs every block whatever
  `cmx` or the envelope is doing. `cho` off is a mode with no unit and no
  pass. The slot is budgeted for its dearest (chorus on).
- **No existing loop moves.** Gate the oscillator and mixer loop sets
  (`elf_p4_check`) and the inlined-set diff at each step. The chorus
  functions are `noinline` and live in flash, so LTO does not inline them
  into `mixer_calcNextSampleBlock()` (S086 F1).

---

## 9. Tests and gates

- **Host module** (`tools/dsp_test/modules/`, one file):
  - **CH-TAP:** the `SMUAD` read against a float linear interpolation, for
    every fraction at a fixed index (bit-exact to the Q14 rounding).
  - **CH-GUARD:** the guard rule. Run 1,000 blocks at d = 65 … 1,022 with
    wraps in every block position. Break proof: remove the guard refresh,
    and the law must fail at d near 1,023.
  - **CH-LAW:** the per-block delay ramp. The delay at each block edge is
    exact, R = 2c − L, and the mode table gives the §1.1 ranges and the
    pitch swing (±6.5 / ±11.0 / ±13.4 cents).
  - **CH-MIX0:** at `cmx` 0 the output equals the dry exactly (the pass
    still runs).
  - **CH-SLEW:** a depth step moves the delay by no more than the slew
    bound per block.
  - **CH-SRC:** both `csr` forms leave both rings holding the selected
    source's last 1,024 samples. A switch every block on a steady input
    gives an output equal to the reference, in which the ring input
    changes source at that block and nothing else changes. Break proof:
    drop the `mid` form's ring-R mirror store, and the law must fail on a
    `mid` → `L-R` switch.
- **Cost:**
  - `elf_looppath.py` on the linked image: each chorus loop's count. The
    stereo form must stay ≤ 22 per channel and mono ≤ 25 + 15. Exceeding
    those means spills came back.
  - `elf_blockpath.py` for the control and entry/exit; a `colloid_cpu.py`
    row for the Advanced type.
- **Device probe** (the gate, on the D-cached image, cold first instance):
  - an Advanced Colloid in pwm Saw/Rec 6, `rte` `str`, `cho` = `II`,
    `csr` = `L-R` (the charged mode), at most 21,803 per window; then the
    same with `csr` = `mid` (it must be lower);
  - a Scene with two Advanced types (Colloid + Alkaline) and an active bus
    Effect. The whole-mixer figure is compared with the S096 P3 Scene. That
    Scene **already** had 377 of 2,550 windows with a block above 156,705,
    with no Advanced slot at all (R-1).
- **Ear:** the three Juno modes against a recording if one is available;
  the `var` rate range (D-6); the tone and mix defaults (D-7); and C2 if it
  is used.

### Risks

- **R-1: the mixer peaks.** The S094 P2 §21 sum reserves two Advanced
  slots. A real Scene already peaks above the per-block budget (S096 P3:
  median 138,745 = 88.5 %, peak 161,954). Those peaks were absorbed by the
  two-slot queue. Two Advanced slots add up to ≈ 4,400 per block on top.
  The two-Advanced probe is the check.
- **R-2: the linked CPI.** At 1.6 CPI, `L-R` leaves only 57 cycles spare
  against the median and is 573 over the worst window. Then the §6 order
  applies, before the switch is given up.

---

## 10. Decisions for the user

| # | Question | Recommendation |
|---|---|---|
| D-1 | When the unit is claimed: on `cho` ≠ off (the brief), or at type assignment | **On `cho` ≠ off, as asked,** with `cho` made non-automatable (§3.2). It changes the stated FxBuffer rule "never at a row change"; a `cho` toggle during play reseats the bus Effect |
| D-2 | Colloid input: `L-R`, `mid`, or switchable | **Decided (user, 2026-11-19): switchable, `csr` (§2.5)**, budgeted at `L-R`. The Alkaline runs `mid` only |
| D-3 | Ring length: 1,024 (23.2 ms, mask) or 1,104 (25.0 ms, compares or segments) | **1,024** (§3.1) |
| D-4 | Colloid rows | **Follows from D-2:** `cho crt cdp cmx csr` (64 of 64). No `cbw`; the tone is fixed by ear. The Alkaline gets `cbw` |
| D-5 | Tokens, extensions, folder names for the Advanced types | `clx` / `alx` (S095 P5 Q5), still open |
| D-6 | `var` rate range | Log 0.1 … 10 Hz; set by ear |
| D-7 | Defaults: wet cutoff (and tracking), dry/wet balance, the dry gain (+2 per sample) or no dry gain | Cutoff ≈ 8–10 kHz with tracking on; equal balance; a dry gain only if equal levels clip on loud patches |
| D-8 | Read the S089 P2 ceiling as met when an Advanced type spends only the reserved Advanced share and every Basic type stays bit-identical | Yes. This is the S-R draft's D-7 reading applied to the Advanced share |
| D-9 | Order | **Decided (user, 2026-11-19): the Colloid first**, because it is the constrained host. The Alkaline follows on the same kernel, with the §5.2 options |
| D-10 | §5.2 options for the Alkaline Advanced | Overload clip + 12 dB wet first; ensemble by ear; hiss held as a later Alkaline-only option |

---

## 11. Statements this change would make out of date (for the user to act on)

This proposal edits nothing in `MEMORY.md` or `knowledge_files/`. After
implementation:

- `MEMORY.md` §DSP, "Cymbal/HiHat now Basic (S094 P2 §22)": "No current
  type is Advanced; the Advanced machinery is preserved."
- `MEMORY.md` §DSP, Drum FM ramp twins: "ITCM 13,984 / 16,384 B after
  S094". **Already stale:** the current image is 15,960 B (also noted in the
  S-R draft §10).
- `knowledge_files/drafts/DRAFT_COLLOID_ALKALINE_ADVANCED_FX.md`:
  - §0, §2.2, §3.3: the Colloid's 12 / pwm 8 counts and its ≈ 3,800 cycles
    of Advanced room. **Already stale:** S096 P3 set 10 / 6 and measured
    pwm 6 at 19,136 (median), so the room is ≈ 2,000 … 2,700.
  - §1: "Colloid 1,112 B, 152 B left". **Already stale:** 1,212 B, 52 B left.
  - §1: "Voice units … No caller". **Already stale:** the Tetrakt claims one
    (S096 P1).
  - §3.1: the chorus at ≈ 38 per sample (*estimate*), against this draft's
    *spike* figure of ≈ 45 … 49 with per-block work.
- `InstrumentManager.c:466` (comment): "claimed at type assignment, never
  at a row change" (if D-1 is taken as recommended).
- `InstrumentManager.c:237–239` (comments): "S095 P3: 1,112 B" for
  `ColloidVoice` (already 1,212 B) and the new sizes.
- `EFFECTS_BUS_REFERENCE.md` / `EFFECTS_MODULES_DSP_REFERENCE.md`: the
  voice-unit owners and the share's worst case.
- `INSTRUMENTS_DSP_REFERENCE.md`: the Advanced type entries, the chorus
  loop cost rows, and the add-a-type checklist use.
- `CPU_USE_DSP_AUDIT.md`: the Advanced slots' first user and their
  measured cost.

---

## Appendix A — spike kernels (counted, not linked)

The spike sources live in the session scratchpad, not the tree. The
recommended kernels are reproduced here. When implemented, their source
and counts go to `tools/dsp_test/baselines/<session>/`.

```c
/* -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -Ofast
 * elf_looppath.py --cond-heads --in-range: cho_sideD 22, cho_monoA 25,
 * cho_monoB 15 per sample; --straight 46 / 53 / 28. */
#include <stdint.h>
#include <string.h>
#define N 32u
#define RMASK 1023u
typedef struct { int16_t *ring; uint32_t rp; int32_t inc; int32_t p, o; } ChoSide;

static inline int32_t tapD(const int16_t *r, uint32_t rp)
{
	const uint32_t i = (rp >> 16) & RMASK, f = (rp >> 2) & 0x3FFFu;
	uint32_t pair;
	memcpy(&pair, r + i, 4);                       /* [i], [i+1]; guard at 1,024 */
	return __builtin_arm_smuad((int32_t)pair, (int32_t)((f << 16) | (0x4000u - f))) >> 13;
}

/* stereo in: one channel per call, L then R, same starting w */
__attribute__((noinline)) void cho_sideD(ChoSide *s, int16_t *x, uint32_t w,
                                         int32_t kk, int32_t g)
{
	int16_t *const r = s->ring;
	uint32_t rp = s->rp; const uint32_t inc = (uint32_t)s->inc;
	int32_t p = s->p, o = s->o;
	int16_t *const end = x + N;
	do {
		const int32_t xi = *x;
		p = __builtin_arm_smlawb((xi << 1) - p, kk, p);
		r[w & RMASK] = (int16_t)(p >> 1);
		o = __builtin_arm_smlawt(tapD(r, rp) - o, kk, o);
		*x++ = (int16_t)__builtin_arm_ssat(__builtin_arm_smlawb(o, g, xi), 16);
		w++; rp += inc;
	} while (x != end);
	if ((w & RMASK) < N) r[1024] = r[0];          /* guard, after a wrap */
	s->rp = rp; s->p = p; s->o = o;
}

/* mono in, pass A: mid -> pre pole -> ring; tap L (the mid is read before L
 * is overwritten) */
__attribute__((noinline)) void cho_monoA(ChoSide *s, int16_t *xL, const int16_t *xR,
                                         uint32_t w, int32_t kk, int32_t g)
{
	int16_t *const r = s->ring;
	uint32_t rp = s->rp; const uint32_t inc = (uint32_t)s->inc;
	int32_t p = s->p, o = s->o;
	int16_t *const end = xL + N;
	do {
		const int32_t l = *xL, m = l + *xR++;
		p = __builtin_arm_smlawb(m - p, kk, p);
		r[w & RMASK] = (int16_t)(p >> 1);
		o = __builtin_arm_smlawt(tapD(r, rp) - o, kk, o);
		*xL++ = (int16_t)__builtin_arm_ssat(__builtin_arm_smlawb(o, g, l), 16);
		w++; rp += inc;
	} while (xL != end);
	if ((w & RMASK) < N) r[1024] = r[0];
	s->rp = rp; s->p = p; s->o = o;
}

/* mono in, pass B: tap R from the ring pass A wrote (also each extra tap) */
__attribute__((noinline)) void cho_monoB(ChoSide *s, const int16_t *r, int16_t *x,
                                         int32_t kk, int32_t g)
{
	uint32_t rp = s->rp; const uint32_t inc = (uint32_t)s->inc;
	int32_t o = s->o;
	int16_t *const end = x + N;
	do {
		const int32_t xi = *x;
		o = __builtin_arm_smlawt(tapD(r, rp) - o, kk, o);
		*x++ = (int16_t)__builtin_arm_ssat(__builtin_arm_smlawb(o, g, xi), 16);
		rp += inc;
	} while (x != end);
	s->rp = rp; s->o = o;
}
```

- **The `csr` design (§2.5) differs from the spike in one place.** The
  `mid` form's pass A also stores the mid into ring R (+1, not counted), and
  pass B reads ring R, not the ring pass A tapped. That keeps both rings
  current in both modes.
- The control spike (`cho_control`, 34 instructions straight-line) steps
  a 32-bit triangle phase, slews the depth by 1/8 per block, mirrors R about
  the centre, and sets `rp = (w << 16) − d_now` and
  `inc = 65536 − (d_end − d_now) / 32` per tap (§2.2).
- `kk` packs kPre (bottom halfword) and kPost (top), both Q16 / 2 through
  the ×2 states. `g` is the wet gain, Q15, in the bottom halfword.

## Sources

- [polynominal, Roland Alpha Juno 2](https://www.polynominal.com/site/studio/gear/synth/Roland_alpha_juno/roland-alpha-juno-2.html)
  (2 × MN3101 + 2 × MN3009, adjustable rate)
- [polynominal, Roland Alpha Juno](https://www.polynominal.com/site/studio/gear/synth/Roland_alpha_juno/index.html)
- [MatrixSynth, "Roland Alpha Juno chorus compared to …" (2021-06-29)](https://www.matrixsynth.com/2021/06/roland-alpha-juno-chorus-compared-to.html)
  (variable chorus rate as the difference; from a search summary, page not
  read in full)
- [pendragon-andyh, Juno60 / Chorus README](https://github.com/pendragon-andyh/Juno60/blob/master/Chorus/README.md)
  (Juno-60 topology, rates, delay ranges, pre-filter, no compander; read)
- The MN3009's 0.64–12.8 ms range is the datasheet figure, as quoted by
  several Juno-chorus product pages in search results. The 256-stage law
  `d = 256 / (2 f_clk)` reproduces both ends.
- Not used: figures of "10 to 30 ms" (a hobby write-up) and "7.5 / 15 ms
  per side" (a forum summary) conflict with the MN3009's 256 stages and the
  schematic analysis above.
