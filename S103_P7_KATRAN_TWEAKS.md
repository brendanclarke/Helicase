# S103 P7 — Katran: coupling the two cores, and the ring → LFO path (plan)

**Date:** 2026-11-25. **Branch:** `dev-ph7-alkaline` (the S103 P5 Katran;
S103 P6 did not touch it). **Status:** plan, **pinned**. No source was
changed. It waits for your review of how Katran is presented to the user (the
parameter reorganisation): **how these features become parameters is not
decided here** (§8).

**Your requests (2026-11-25):**
- write up the four ways of inter-modulating the two cores through their
  internal modulation states (not audio: `spr` covers audio). All four if
  they fit; otherwise in your order of preference: **1 cross-level coupling,
  4 cross-sync, 3 kick ping-pong, 2 anti-phase modulation**;
- **feature 5** (added the same day): the patch fed RING OUT into its LFO's
  MOD IN at audio rate. Katran's equivalent is block-rate and measures
  another signal (§6A). Two potential implementations: **5a** measure the
  right signal, **5b** a direct audio-rate AM path into the VCAs.
- "The engine still sounds fantastic as-is … let's see what we can do later
  on": every feature here is optional, and off reproduces today's sound.

**Labels:**
- **Code:** read from the tree, with a file and line.
- **Est.:** an estimate with its basis, measured at the gate named.
- **By ear:** a constant set in listening.

**Binding rules that apply** (`MEMORY.md`):
- constant CPU and the S089 P2 ceiling;
- Katran's gate is 380 instructions a sample (`tools/dsp_test/katran_cpu.py`
  `GATE`), the 5 % line under the Basic share at 1.53 CPI;
- `sizeof(KatranVoice)` ≤ 1,200 B (`InstrumentManager.c` assert);
- RAM inside the slot needs no new allocation (inside the voice, under the
  assert);
- every code change carries an adjacent comment block.

No step here edits `MEMORY.md` or `knowledge_files/` (§12).

**Names:** Osc 1 (copies 1a, 1b) and Osc 2, as on
`katran_block_diagram_p2.svg`. The code still calls them OSC A / OSC B.

---

## 0. Summary

| # | Feature | What it does | Rate | Cost (est.) | Fits? |
|---|---|---|---|---:|---|
| **1** | **Cross-level coupling** | Each core's level moves the other core. **Rivalry:** the louder core lowers the other's feedback and closes its filter, so the cores take turns. **Sympathy:** the louder core opens the other's filter and nudges its exciter pitch; never its feedback | per block (+ a level measure) | +3.1 (+2.1 without the pitch nudge) | yes |
| **4** | **Cross-sync** | An exciter copy can be hard-synced by the **other** core's delay output, by its own, or by both: the loops re-clock each other (entrainment) | **per sample** | +4 .. +6 | yes, the tightest |
| **3** | **Kick ping-pong** | A trigger kicks one core, not both: alternate, or core A then core B one loop-time later | per block | +0.3 | yes |
| **2** | **Anti-phase modulation** | Core B takes the shared modulators (patch LFO, CE2) inverted, so one core opens as the other closes | per block | +0.3 | yes |
| | **All four** | | | **+7.7 .. +9.7 → ≈ 374–376** | **under 380 by est.** (§7) |
| **5a** | **Ring → LFO: the right measure** | The patch LFO's rate (and the CE rise) follow the **level of the cores' outputs over the block**, not one delay-output sample | per block | +1 | yes |
| **5b** | **Ring → VCA: direct audio-rate AM** | The ring signal itself modulates the VCA gains every sample, recovering the audio-rate part of the patch's AMP CV | **per sample** | +3 .. +6 | **not with all four** (§7) |

**Common to all five:**
- **Off is exact:** every feature's "off" state reproduces today's output
  bit for bit (laws §9), so nothing changes until a parameter exposes it.
- **One shared groundwork step:** per-core feedback, tube and kick state (§2).
  The cores share these values today.
- **RAM:** ≈ +44 B (1,100 → ≈ 1,144 B of the 1,200 B gate).
- **No new rows in this plan** (§8).

---

## 1. What exists (code, S103 P5 tree)

| Item | Where | Shared or per core |
|---|---|---|
| **The loop level `ringPrev`:** the mean of \|D\| of **one** sample per core per block (`blkD[0][31]`, `blkD[1][15]`) | `KatranVoice.c:1671` | one value for both |
| ringPrev → patch LFO rate (× 2^(mod'·1.5·level)) | `:423` | shared |
| ringPrev → CE rise (chs > 64) | `:380` | shared |
| **Feedback** `fb` / `fbStep` (fbk, LFO inv, kick swell), one ramp read by both cores | `:1560`; read in `kat_coreBody()` `:954`, `:961` | **one for both** |
| **Tube** `tTube` | `:1548`; read `:946` | one for both |
| **Kick** `kE`, its decay; `tExc`, `tNse`, `kickSwell` | `:529–532`; read `:949–951` | **one for both** |
| **F1 cutoff:** computed per core in the async loop | `:1499–1511` | per core already |
| **Osc 1 increments:** `incA[0]` (1a), `incA[1]` (1b) | `:646–647` | per copy already |
| **The sync test:** `st = wrapB·KAT_SYNC_WRAP + dSelf[n]`, dSelf = the core's own `blkD[c]` | `KatranOsc.c:272`, `:298` | own core only |
| **Both delay reads run before either core renders** (`kat_delayReadA()` / `B()` first) | `KatranVoice.c:1620` | so each core's D for this block is available to the other |
| **The patch LFO:** one phase step and one `cv` per block | `kat_plfoStep()` `:410`, `:424` | shared |
| **The VCA:** each core's output `o = t1 · (o0 + o1·B)`, the ring, the VCA and the output scale folded into two ramped gains (S103 P4); B loaded as `Bv` | `:977`, `:998` | per core |
| Core outputs land in `buf` (core A) and `right[]` (core B) before the stereo matrix | `:1655`, `:1674` | per core |
| **CPU now:** loops 295.0 + per block 2,290/32 = **366.6** | `katran_cpu.py` on the S103 P5 image | gate 380 |

---

## 2. Groundwork: per-core control state (G)

**Why:** features 1, 2 and 3 move one core's feedback, tube or kick without
the other. Today those are single values read by both cores' loops.

| Change | Detail | RAM |
|---|---|---:|
| G1 | `fb`, `fbStep` → `fb[2]`, `fbStep[2]`. The async computes each core's target (§3, §6, §5 add to it); `kat_coreBody()` reads `v->fb[c]` / `v->fbStep[c]` (`c` is a constant in each wrapper, so the loop is unchanged) | +8 |
| G2 | `tTube` → `tTube[2]` (feature 2) | +4 |
| G3 | `kE` → `kE[2]`; `tExc`, `tNse`, `kickSwell` → `[2]` (feature 3) | +16 |
| G4 | the clamps stay per core: fb ≤ 0.95, fb + xfb ≤ 1.1 (`:1554–1560` laws, per core) | 0 |

- **Off state:** both elements equal → bit-identical output (law PC-0).
- **Per sample:** nothing changes. Each core's loop loads its values once,
  as now.

---

## 3. Feature 1 — cross-level coupling (first preference)

### 3.1 Per-core level followers

- **The measure:** P_c = max |blkD[c][i]| over every 4th sample of the block
  (core A 8 reads, core B 4), ÷ 32,768. It is the `kat_guardUpdate()`
  pattern (`KatranVoice.c`, every 8th sample) at twice the density.
- **The follower:**
  - L_c += k·(P_c − L_c);
  - attack k = 0.07 (≈ 10 ms) when P_c > L_c, release k = 0.0073
    (≈ 100 ms); by ear.
- **RAM:** `lvl[2]`, +8 B.
- **`ringPrev`:** kept exactly as it is for its two current consumers
  (identity, §9). Whether they switch to (L_A + L_B)/2 is a later sound
  decision (Q2).

### 3.2 The law (per block, applied to the other core)

The drive for core B from core A is d_B = min(1, 2·L_A); for core A from
core B, d_A = min(1, 2·L_B). x ∈ [−1, +1] is the coupling (§8: not yet a
row).

| x | Core B receives (and core A symmetrically) | Bound |
|---|---|---|
| **x < 0, rivalry** | fb_B × (1 − 0.8·\|x\|·d_B); F1_B − 24·\|x\|·d_B semitones | only lowers the gain: self-limiting |
| **x > 0, sympathy** | F1_B + 24·x·d_B semitones; Osc 1b pitch × 2^(x·d_B·3/12) (up to +3 semitones) | **never raises feedback** (two loops raising each other's gain latch at full scale) |
| **x = 0** | nothing | identity |

- **The constants** (0.8, 24 semitones, 3 semitones, the ×2 normaliser) are
  by ear, from a host render of a drone at `init.kat` with x swept.
- **Where it acts:**
  - the feedback target in G1's per-core computation;
  - F1's semitone sum (`:1499–1511`, already per core);
  - Osc 1's per-copy increments (`:646–647`): one `alk_exp2f()` per copy
    per block while x > 0. Branch-free form: always compute, with
    exponent 0 when x ≤ 0.

### 3.3 Cost (est.)

| Part | Instructions |
|---|---:|
| Measure: 12 reads + compare per block | ≈ 40 a block |
| Follower and law: two cores | ≈ 30 a block |
| Two extra `alk_exp2f()` (the pitch nudge) | ≈ 30 a block |
| **Total** | ≈ 100 a block ≈ **+3.1 a sample**; 2.1 if the pitch nudge is dropped |

Per sample: 0.

---

## 4. Feature 4 — cross-sync (second preference)

### 4.1 The law

The sync test (`KatranOsc.c:298`) becomes:

```
st = wrapB·KAT_SYNC_WRAP + (D_own[n] & mOwn) + (D_other[i(n)] & mOther)
```

- **The masks** mOwn, mOther (0 or all ones) are set per block from the
  copy's mode:

| Mode | mOwn | mOther | Note |
|---|---|---|---|
| **own** | ones | 0 | today, bit-identical |
| **other** | 0 | ones | |
| **both** | ones | ones | the sum: either loop can reset it; the threshold `snc` is shared |

- **The index into the other core's D:**
  - 1a (full rate, sample n) reads core B's half-rate block at n >> 1 (held);
  - 1b (half rate, sample k) reads core A's at 2k (decimated).
  - The cores already exchange D this way for xfb (`kat_coreBody()`, `:940`
    onward). Both D blocks exist before either core renders (§1).
- **Modes:** per copy (four combinations), or one mode for both copies
  (§8). The body takes them as two per-block masks either way.

### 4.2 Cost (est.)

- Per sample, in each Osc 1 copy's loop: one load, two ANDs, one add (≈ 4),
  plus possible register pressure (the dearest form, PWM D, measured 45 / 49;
  S103 P4).
- Copy 1a: +4 to +5 a sample. Copy 1b (half rate): +2 to +2.5.
- **Total ≈ +4 to +6** (est.; the linked count decides).

### 4.3 If the count is too high (the fallback)

Drop "both". Make "other" a **separate wrapper per form and copy** that reads
the other core's D with its own index law, chosen per block like the forms
are now: 0 per sample.
- Flash: +8 wrappers, est. +1.5 KB.
- The `KatranOsc.c` `kat_aBody()` template takes a constant `src` like its
  `form` and `half` (S103 P2's pattern).

---

## 5. Feature 3 — kick ping-pong (third preference)

### 5.1 The modes (per voice; §8 not yet a row)

| Mode | A trigger kicks |
|---|---|
| **both** | both cores (today, bit-identical) |
| **alternate** | core A, then core B on the next trigger, … (one toggle bit) |
| **echo** | core A now, core B after core A's current loop time (`tNow[0]` / 32 blocks, ≤ 56 blocks: a one-byte countdown) |
| **random** | core A or B, drawn from the voice's xorshift |

- **The kick has four parts**, all per core after G3:
  - the re-arm of the run gain;
  - the burst into the exciter level and the noise mix;
  - the feedback swell;
  - the velocity.
- **The run gain `gr` stays shared:** both cores re-arm on any kick, so
  ping-pong moves only the burst and the swell. Making gr per core too is a
  separate choice (Q3).

### 5.2 Cost (est.)

≈ 10 a block (+0.3 a sample). RAM: 2 B (mode state, countdown).

---

## 6. Feature 2 — anti-phase modulation (fourth preference)

### 6.1 The law

- **The sign:** a phase amount φ ∈ [0, 1] gives core B's sign s_B = 1 − 2φ.
  Core A keeps +1.
- **Core B's shared-modulator terms** take s_B (G1, G2 give them their own
  values):
  - the patch LFO's `cv·lfl·12` on F1;
  - its `0.15·cv·lfl` on the tube;
  - its `0.2·inv·lfl` on the feedback;
  - CE2's `±48·dp2` on F1.
- **Untouched:**
  - Osc 2's terms (one oscillator, shared);
  - F3 (one coefficient set);
  - the VCA offsets (cv / inv already split by core, the patch's own
    anti-phase).
- **φ = 0:** today, bit-identical.

### 6.2 Cost (est.)

≈ 10 a block (+0.3 a sample). RAM: 0 beyond G2.

---

## 6A. Feature 5 — the ring → LFO path

### 6A.1 What the patch does, and what Katran does

- **The patch:** RING OUT is patched into the LFO's MOD IN, so the ring
  signal FMs the LFO's rate **at audio rate**. The LFO's CV and inverted
  outputs then carry audio-rate sidebands into all eight destinations. The
  most audible part is the AMP1 / AMP2 offsets: audio-rate amplitude
  modulation of the outputs.
- **Katran today:**
  - **the signal** is `ringPrev`, the mean |D| of **one** delay-output
    sample per core per block (`:1671`). It is not the ring output, and one
    sample is a noisy level.
  - **the rate:** the LFO runs once per block (`:410`); its `cv` is one
    value per block.
  - **the destinations** ramp (the VCAs, the feedback), slew (F1, 0.28 a
    block) or hold (the tube) that value.
  - **nothing above ≈ 690 Hz** (half the block rate) reaches any
    destination, and in practice much less.

### 6A.2 Option 5a — measure the right signal (per block)

- **The measure:** after both cores render and before the stereo matrix
  (between `:1655` and `:1674`), take the peak of |buf[n]| (core A) and
  |right[n]| (core B) over every 4th sample, ÷ 32,768.
  - Those blocks hold each core's ring, through its VCA: the patch's
    outputs, of which the ring is the main part.
  - The level is the meaningful block-rate equivalent of an audio-rate FM
    input. A raw bipolar sample at 1.38 kHz would be sampled noise.
- **The follower:** the same as feature 1's (§3.1: ≈ 10 ms attack,
  ≈ 100 ms release), one shared value (the mean of the two cores) for the
  LFO.
- **Use:** in place of `ringPrev` in the LFO rate (`:423`) and, optionally,
  the CE rise (`:380`).
- **Off is today:** a source switch (internal control value, §8) selects
  `ringPrev` (today, bit-identical) or the new level. **The switch is
  required**, because a smoother, correct measure changes the sound.
- **Shared with feature 1:** if both are built, one measuring pass serves
  both. Feature 1 could follow the same outputs instead of D (a choice for
  Q2).
- **Cost (est.):** 12 reads and a compare a block, plus the follower:
  ≈ 40 a block, **+1.2 a sample**; ≈ +0.2 if feature 1's measure is shared.
- **RAM:** +4 B (the shared level) and 1 B (the source).

### 6A.3 Option 5b — direct audio-rate AM into the VCAs (per sample)

**The law** (per sample, in `kat_coreBody()`, after `o` at `:998`):

| Core | Today | With 5b |
|---|---|---|
| A (AMP2: + g2·loop) | o = r + g2·t1 | o = r + (g2 + kAM·m[n])·t1 |
| B (AMP1: ring × (1 + g1)) | o = r·(1 + g1) | o = r·(1 + g1 + kAM·m[n]) |

The modulator m[n] has two candidates:
- **(i) the ring itself:** m = r[n]. Closest to the patch's chain (ring →
  LFO → AMP). r must be separated from the folded gains:
  `r = t1·(rk0 + rk1·B)` (two operations), then the AM term (two).
- **(ii) Osc 2 alone:** m = B[n]. Already loaded as `Bv` (`:977`); only the
  AM term (two operations). It AMs the outputs at Osc 2's rate, a cleaner
  "ring-rate tremolo".

**Bounds and identity:**
- kAM ≤ 0.5, so with |m| ≲ 0.7 the VCA gain stays near its 0.42 offset
  range and never strongly inverts. No per-sample clamp (it would cost two).
- **kAM = 0 is bit-identical:** `fma(0, …, o)` returns o exactly. Law PC-0.
- **One value per voice, per block (no ramp):** a step change of kAM at a
  block edge is a small AM step. Add a ramp (+1 a sample) only if listening
  hears it.

**Cost (est., per sample):**

| Variant | Core A | Core B (half rate) | Total |
|---|---:|---:|---:|
| (i) m = r | +4 | +2 | **+6** |
| (ii) m = B | +2 | +1 | **+3** |

Both before any register-pressure effect in `kat_coreBody()`; the linked
count decides.

RAM: +1 B (kAM), +1 B (the variant, if both are kept).

**Not taken: a per-sample patch LFO (option 3 of the discussion).** The
LFO's phase and sine read every sample, FM'd by r, fed per sample to the
VCAs, the feedback, the tube and Osc 2's phase increment.
- **Cost:** ≈ +20 to +25 a sample, over the gate with anything else.
- **Limit:** F1 would stay block-rate (a per-sample cutoff is a `tan()` a
  sample).
- Recorded for a later CPU budget, not planned.

---

## 7. CPU and RAM fit

| Item | Instructions a sample (est.) |
|---|---:|
| Now (S103 P5, measured) | 366.6 |
| G (per-core state) | ≈ 0 (one more load per core loop entry, per block) |
| 1 cross-level (with the pitch nudge) | + 3.1 |
| 4 cross-sync (masks) | + 4 .. + 6 |
| 3 kick ping-pong | + 0.3 |
| 2 anti-phase | + 0.3 |
| **All four** | **≈ 374.3 – 376.3** |
| 5a the right measure | + 1.2 (≈ + 0.2 shared with feature 1) |
| 5b audio-rate AM | + 3 (m = B) .. + 6 (m = r) |
| **All five (5a + 5b)** | **≈ 377.5 – 383.5** |
| Gate | 380 |

- **Margin:** **3.7–5.7 under the gate by estimate.** That is thin, and only
  the linked count decides.
- **Feature 5 against the four:**
  - 5a fits with all four;
  - 5b does not fit with all four at the m = r variant;
  - at the m = B variant it is at the gate.
  - **5b's priority against features 1–4 is not set** (Q5). Feature 4's
    fallback (§4.3) frees 4–6 a sample, which pays for 5b exactly.
- **The drop ladder, if the count exceeds 380:**
  1. cross-sync's fallback (§4.3: 0 per sample, no "both");
  2. feature 1 without the pitch nudge (−1);
  3. then your order, dropping the last first: 2, then 3. These two save
     only ≈ 0.3 each, so in practice the ladder ends at steps 1–2.
- **The S094 P2 cycle gate:** the `p` probe on the device, median ≤ 18.6k
  cycles a block, as for S103 P4. Today's estimate at 1.53 CPI:
  ≈ 18.3–18.4k.
- **RAM:**

| Item | Bytes |
|---|---:|
| G1–G3 | 28 |
| Followers | 8 |
| Sync masks | 2 |
| Ping-pong state | 2 |
| Feature 5 (level, source, kAM, variant) | 7 |
| Alignment | ≈ 4 |
| **Total** | **≈ 51** |

  1,100 → ≈ 1,151 B (gate 1,200).

---

## 8. Exposure: deliberately open

**Your note:** you will first review how Katran's model is presented and
reorganise it; only then decide the parameters.

**What each feature needs from a control surface** (inputs, not rows):

| Feature | Control values it reads | Off |
|---|---|---|
| 1 cross-level | x ∈ [−1, +1] (rivalry ↔ sympathy); optionally the follower speed | x = 0 |
| 4 cross-sync | mode per copy (own / other / both), or one mode for both | own |
| 3 kick ping-pong | mode (both / alternate / echo / random) | both |
| 2 anti-phase | φ ∈ [0, 1] | 0 |
| 5a the right measure | the level source (today's `ringPrev` / the core outputs) | today's |
| 5b audio-rate AM | kAM ∈ [0, 0.5]; the modulator (ring / Osc 2) if both are kept | kAM = 0 |

- **Implementation (when scheduled):** each value is a byte in
  `KatranVoice` with a documented law, read in the async, so any later row,
  combined row or macro can bind to it without touching the DSP.
- **Defaults:** every value starts "off", so the voice sounds as now until a
  row exposes it.
- **Candidate homes, for your review** (not decided):
  - `spr`'s upper half for 2 (the "how the cores relate" row);
  - a new bipolar "couple" row for 1;
  - `snc`'s mode for 4;
  - `kik`'s sign or a selector for 3.
- **Free indices:** 58 and 59; more appear if the reorganisation merges
  rows.

---

## 9. Laws (host, `tools/dsp_test/modules/katran.c`)

| Law | What |
|---|---|
| **PC-0** | All five off (x 0, own, both, φ 0, today's level source, kAM 0) with G applied: output, both rings and every state **bit-identical** to the S103 P5 voice over 2,000 blocks (a frozen copy of the P5 render, the CO-MONO1 method of S103 P6) |
| PC-LVL | The follower: a step to a full-scale D reaches 63 % in ≈ 10 ms, falls in ≈ 100 ms; a silent core stays 0 |
| PC-RIV | Rivalry x = −1, core A loud, core B quiet: core B's feedback target ≤ 0.2 × its own; never above it at any x < 0 |
| PC-SYM | Sympathy x = +1: core B's feedback target equals its x = 0 value exactly (never raised); F1_B up by 24·d_B; 1b's increment × 2^(d_B·0.25) |
| PC-SYNC | Mode other: copy 1a resets on core B's D crossing, never on its own; both: on either; own: exactly today's resets (KA-SYNC's case) |
| PC-PP | Alternate: two triggers kick A then B; echo: core B's burst starts T_A/32 blocks after core A's; both: today |
| PC-PHASE | φ = 1: core B's F1 LFO and CE2 terms are the negatives of core A's; φ = 0: equal |
| PC-RLV | 5a: a steady tone in both cores gives a level within 5 % of its peak over the block; the source "today" gives `ringPrev` exactly |
| PC-AM | 5b: kAM = 0 is bit-identical; at kAM > 0 with a constant m the output equals the S103 P5 output with the VCA gain shifted by kAM·m (within float rounding); at full kAM no output exceeds int16 beyond the existing saturation |
| PC-BND | All five at their extremes with `fbk` 127, `drv` 127, `res` 127 over 4,000 blocks: finite, bounded, the feedback clamps hold per core |
| Break builds | `katran_break_riv` (rivalry raising feedback) must fail PC-RIV; `katran_break_xsync` (the other core's D read at the wrong index) must fail PC-SYNC |

---

## 10. Order of work (when un-pinned)

| Step | What | Gate |
|---|---|---|
| 0 | Your parameter reorganisation; this plan's §8 then names the rows | your decision |
| 1 | G1–G3 with PC-0 | bit identity; `katran_cpu.py` ≈ 366.6 |
| 2 | Feature 1 (followers, law) | PC-LVL, PC-RIV, PC-SYM; a host render by ear |
| 3 | Feature 4 (masks) | PC-SYNC; the linked count (the fallback if needed) |
| 4 | Features 3 and 2 | PC-PP, PC-PHASE |
| 4A | Feature 5: 5a, then 5b (the variant by ear) | PC-RLV, PC-AM; the linked count |
| 5 | PC-BND, break builds; the image; the mixer hot-loop diff; the size assert | all |
| 6 | Device: the `p` probe (≤ 18.6k); listening | yours |

---

## 11. Risks

| # | Risk | Mitigation |
|---|---|---|
| R1 | The CPU estimate is low (cross-sync's register pressure) | The §4.3 fallback costs 0 per sample |
| R2 | Sympathy plus high feedback still runs hot through the filter path | The feedback is never raised; the existing clamps; PC-BND |
| R3 | Rivalry at block rate buzzes on fast loops | The follower's 10 / 100 ms smoothing; the feedback ramp is per sample (unchanged) |
| R4 | Feature 1 needs the followers that would better feed the LFO and CE too, and switching them changes today's sound | Kept separate until you decide (Q2) |
| R5 | Cross-sync makes the sound depend on both cores' levels; `snc` was tuned for one core | Shared threshold for now; per-mode thresholds only if listening asks for them |
| R6 | 5a's smoother level changes the patch LFO's character (today's one-sample `ringPrev` is noisy, which may be part of the sound) | The source switch keeps today's as an option; decide by ear |
| R7 | 5b's AM at high kAM with loud rings reaches the saturating store (hard clipping at the output) | kAM ≤ 0.5; PC-AM's bound; a ramp or a soft clip only if listening asks |

---

## 12. Questions (for the un-pinning)

| # | Question | Recommendation |
|---|---|---|
| Q1 | The exposure of all four (§8) | After your reorganisation |
| Q2 | Should the patch LFO and the CE rise move to the new per-core followers (smoother, a sound change), or keep `ringPrev`? | Decide by ear in step 2; keep `ringPrev` until then |
| Q3 | Ping-pong: also split the run gain per core (a kicked core re-arms alone)? | No at first: a shared gr keeps the drone's on / off one control |
| Q4 | Cross-sync mode per copy, or one for both? | One for both, unless the reorganisation has room |
| Q5 | Feature 5's priority against 1–4 (5b does not fit with all four), and 5b's modulator (ring r or Osc 2 B) | 5a with feature 1 (they share the measure); 5b by ear against feature 4, since 4's fallback pays for 5b |

**Statements this would make out of date (for you to log, when
implemented):**
- `KATRAN_BLOCK_DIAGRAM_SVG.md` and both diagrams:
  - the new couplings;
  - the per-core feedback and kick;
  - the "how the cores relate" note on page 2.
- `MEMORY.md`: the Katran voice size and its CPU figure.
