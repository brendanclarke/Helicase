# Tetrakt — DSP Modelling Reference

> Four-layer sample player. 7th instrument type, Basic class.
> Introduced in Session 096. Source: `S096_P1_TETRAKT_SAMPLE_PLAYER.md` (revision 7).

---

## 1. Architecture

The Tetrakt plays up to four user samples simultaneously through independent per-layer processing chains, sums them into a mono voice, widens via an envelope-derived allpass (W2), and optionally feeds a 50 ms space delay (S1). Every stage runs every block regardless of parameter settings (constant-CPU rule).

```
           ┌─── Layer A ──────────────────────────────┐
           │  read → decay×vel → drive → SRR → tone  │──→ level → pan ──┐
           └──────────────────────────────────────────┘                  │
           ┌─── Layer B ───────────────────────────────┐                 │
           │                 (same chain)              │──→              │
           └───────────────────────────────────────────┘                 ├──→ sum (mono) ──→ W2 (allpass width) ──→ L/R out
           ┌─── Layer C ───────────────────────────────┐                 │       │
           │                 (same chain)              │──→              │       └──→ S1 (space delay) ──→ space L/R
           └───────────────────────────────────────────┘                 │
           ┌─── Layer D ───────────────────────────────┐                 │
           │                 (same chain)              │──→              │
           └───────────────────────────────────────────┘                ─┘
```

---

## 2. Per-Layer Chain

### 2.1 Sample Read (Fused)

The sample read is the voice's own, inside the per-layer post loop (not the platform's `osc_renderTyped()`). The bind is the platform's `osc_userBind()`.

**Per sample:**
```
s = (a << 12) + (ph & 4095) × (b − a)
```
where `a`, `b` are int16 frames at positions `ph >> 12` and `(ph >> 12) + 1`. The result is exact in integers since `|s| < 2^29`. Phase advances by `phaseInc` and clamps to end (the two installed zero frames stop the read).

**Gain:** `x = s × g`, where `g` = `(envelope_gain × bindGain) / 4096`. With `bindGain` 1.0, the factor is exactly `2^-12`.

**Guard:** only an `OSC_BIND_SMP` bind with data and non-zero size reads. Anything else reads a static zero pair at phase 0 (silence).

### 2.2 Decay Envelope

Shared `colEgK[]` exponential decay table (the same curve as the Colloid's ADSR):
```
env *= colEgK[decay_param]    (per block, 32 samples)
```
Monotone non-increasing. Reaches IDLE (env ≡ 0) when below `1e-4`. The `decay` row (0..127) maps to the table's time constants.

### 2.3 Velocity

Each layer latches its own trigger velocity. The velocity destination row names a TKT_KIND_* group (9 groups: tune, level, decay, pan, drive, SRR, space, tone, toneEnv). When a group is active, the platform velocity modulator applies an offset to each layer's row in that group, scaled by the layer's own latched velocity and the global amount.

### 2.4 Drive (Tanh Soft Clip)

```
y = tanh(x × k)
```
where `k` = `tktDriveK[drive_param]` (0..127 → 1.0..~16.0). At `drive` 0, `k` = 1.0 and the signal passes through exactly (no coloration). A DC blocker follows: `y -= hpState; hpState += y × hpCoeff`.

### 2.5 Sample-Rate Reduction (SRR)

Hold-and-step decimator:
```
cnt += rate;  if (cnt >= 1.0) { cnt -= 1.0; output = new_sample; }
```
where `rate` = `tktSrrRate[srr_param]` (0..127 → 1.0..31.75 samples per step). At `srr` 127, `rate` = 1.0 and every sample passes (exact bypass). The hold branch compiles to a real `blt`, so cost varies with rate.

### 2.6 Tone Filter (2-Pole Bipolar SVF)

A state-variable filter whose cutoff sweeps from LP to HP:
- `tone` row (±63): centre at 63 (bypass); 0 = LP corner ≈120 Hz; 126 = HP corner ≈3 kHz
- `toneEnv` row (±63): envelope modulation depth — the decay envelope moves the cutoff

Coefficients from `tktToneB1[]`/`tktToneB2[]` lookup tables. At `tone` 63 and `toneEnv` 63, `y == x` exactly (bit-identical bypass).

### 2.7 Level and Pan

- **Level:** linear gain from row value (0..127 → 0.0..1.0)
- **Pan:** ±63 centre-panned. Per-layer L/R gain from a constant-power or linear pan law into the summing bus
- **Space send:** per-layer send amount into the S1 delay feed

---

## 3. Trigger and Fade

### 3.1 Layer Switch (`lon`)

One 4-bit mask (row index 20). Bit 0 = layer A, bit 3 = layer D. A trigger restarts only the layers whose bits are set; clear layers continue undisturbed. Displayed as four on/off bit cells on the MOD page.

### 3.2 R1 Fade-Then-Start Stages

Each layer independently tracks its stage:
1. **IDLE**: envelope ≡ 0, the layer produces silence
2. **PLAY**: envelope decaying (the normal sounding state)
3. **FADE**: a retriggered layer that is still sounding enters a one-block linear fade to exactly 0.0
4. **START**: the fade finished; the layer re-arms and begins PLAY on the next block

A fresh voice (never triggered) starts directly in PLAY with no fade. This avoids a click on retrigger while allowing the full first attack.

---

## 4. Space Stage

### 4.1 S1 — Delay Line

A 50 ms delay line stored in one FxBuffer voice unit (2,208 × int16 samples):

- **Write:** the mono sum of all four layers' space sends, written at the advancing write position
- **Left tap:** at `tapL = stm × 50 ms` delay, Hermite-interpolated
- **Right tap:** at `tapL × (1 − sdt × TKT_SDT_SPAN)` with a ±0.15 sinusoidal detune wobble (32-step phase accumulator, 32-entry cosine table)
- **Feedback:** `sfb` row, 0..0.97 (`TKT_FB_MAX`), via an allpass modulated by `sto` (tone)
- **Write position advances** by block size (32) each block

### 4.2 W2 — Envelope-Derived Width

An allpass disperser whose coefficient is derived from the master decay envelope:
- At `wid` 0: the voice is dry mono panned (busR = y × gR exactly)
- At `wid` 127 at envelope 1.0: the allpass disperses fully (same identity: `(1 − env)^2` mix is 0)
- As the envelope decays, the allpass effect increases, spreading the stereo image

The allpass coefficient: `TKT_BUS_AP` = −0.6.

---

## 5. Tables

| Table | Source | Domain | Range | Purpose |
|-------|--------|--------|-------|---------|
| `tktDecayK[]` | `gen_tkt_tables.py` | 0..127 | Exponential time constants | Per-block envelope multiplier (shared with `colEgK[]`) |
| `tktDriveK[]` | `gen_tkt_tables.py` | 0..127 | 1.0..~16.0 | Tanh soft-clip gain |
| `tktSrrRate[]` | `gen_tkt_tables.py` | 0..127 | 1.0..31.75 | SRR hold period in samples |
| `tktToneB1[]` | `gen_tkt_tables.py` | 0..126 | SVF coefficients | Tone filter first coefficient |
| `tktToneB2[]` | `gen_tkt_tables.py` | 0..126 | SVF coefficients | Tone filter second coefficient |

---

## 6. CPU Cost Model

**Constant-CPU rule:** every layer runs every stage every block, regardless of parameter settings. The only variable is the SRR hold branch (`blt` in compiled code), which the worst case takes every sample (SRR 127).

| Component | Instructions per sample | Notes |
|-----------|----------------------|-------|
| Store loop (layer 0) | 57 | Post loop with fused read (§19) |
| Add loop (layers 1–3) | 60 each | ×3 |
| Space frame | 64 | S1 + W2, every sample |
| **Total per sample** | **301** | All four layers + space |
| Per block overhead | +2,615 | Bind, dispatch, control rate |
| **Worst block** | **382.7 instr** | Against gate of 390 |

Device measurement: 1.52 cycles per instruction (1.41..1.63 range). The higher ratio vs Colloid (1.37..1.41) is caused by four simultaneous flash reads from internal flash (`0x080C0000`).

---

## 7. Design Decisions

| ID | Decision | Rationale |
|----|----------|-----------|
| Q1 | No attack envelope (decay only) | Percussion instrument: samples carry their own attack |
| Q2 | Basic class | Any slot, any number per Scene; held to Basic share |
| Q3 | No global tune row | The trigger note transposes all four layers |
| Q4 | No `srt` row (global SRR) | Per-layer SRR replaces it |
| Q5 | No `run` row | One-shot playback only; samples decay naturally |
| Q6 | No S1 curve row | The wobble shape is fixed (sinusoidal) |
| Q8 | No portamento | The trigger note sets the pitch instantly |
| Q9 | The bind is `osc_userBind()`, the read is the voice's own | Exact integer arithmetic in the post loop |
| Q10 | 448 B shared work area → 384 B after §19 | Approved, in .bss |
| R1 | FADE is one block | A whole block avoids the per-sample branch; the click is below audibility |
| R4 | Layer switch as one row with four bit cells | Menu, automation, search, MIDI Learn see one `lon` row |
| R5 | Held-step layer mask: one toggle writes the full mask | A step lock is a snapshot of all four layers |

---

## 8. Failed Approaches and Rejected Alternatives

- **Reusing `osc_smpPlainBlock()` for the read**: the platform read rounds to int16 per sample and dispatches through `osc_renderTyped()`, adding 40 instructions per block for the four dispatches plus the int16 scratch buffer. The fused read (§19) is exact in integers and saves 44 instructions per sample.
- **ITCM for the post loops**: not needed. The loops compile outside ITCM with acceptable cost. The space function in ITCM (+80 B, later dropped to 0 when §19 eliminated the `osc_renderTyped()` ITCM section).
- **SRR as a global row**: rejected because per-layer SRR creates richer textures (each layer can have a different decimation rate).
- **8 oscillators (Colloid model)**: not applicable. Each layer reads one sample; the layer count is fixed at 4.
- **Attack envelope**: rejected. Percussion samples carry their own attack transient; an attack envelope would delay the onset and add cost.
