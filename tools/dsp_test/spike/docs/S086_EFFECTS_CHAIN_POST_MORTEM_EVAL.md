# S086 — Effects Chain: Post-Mortem and Re-Evaluation

## 0. Purpose

**What this document does:** it evaluates the S084 spike (`26fb7c3`)
transient, filter drive and Mix FX work as input to a re-scope.

**The idea:** one `chain` parameter in the transient/click section selects
a set of transient kinds, filter drive types and Mix FX types that run
together.
- **Budget:** the chain owns the whole effects budget of the pre-S084 voice,
  about **88 cycles per voice-sample**.
- **Spending:** that budget is spent once by the chosen chain, not three
  times by independent stages.

**What it records:**
- what worked and what did not;
- the voice-buffer problems;
- the measured cost of every built component;
- what cannot fit in 88 cycles under any chain;
- what can fit when the other stages are restricted.

**Not a design yet:** the chain catalogue in §7 is a starting point for the
S086 design session.

---

## 1. The budget

**The baseline:** pre-S084 (git `59406a4`), worst case per voice per
sample, compiled with the firmware `-Ofast` flags.

| Stage | Pre-S084 worst case | Instructions | VDIV | Est. cycles |
|---|---|---:|---:|---:|
| Transient | PCM click (22) + Drum mix-in (10) | 32 | 0 | ≈32 |
| Mix FX | distortion curve (12 + VDIV) + decimator (18) | 30 | 1 | ≈43 |
| Filter drive | `sft` input soft clip inside the ZDF | 13 | 0 | ≈13 |
| **Total** | | **75** | **1** | **≈88** |

**Frame budget:** 6 voices × 88 = **≈528 cycles per frame**, out of 4,897
(10.8 %).

**How the figures were measured:**
- **Instructions:** static counts of the steady per-sample loop.
- **Cycles:** instructions + 13 per float divide (VDIV.F32 is 14 cycles),
  an upper estimate. Dual issue usually lands 20–40 % lower.
- **Cross-check:** the CPU audit's independent estimates agree (distortion
  ×6 ≈ 150 cycles/frame; voice ZDF 600–780).

**What is not in the 88:** the ZDF filter itself (86–100 instructions plus
2–3 VDIV per voice) and the oscillators (S085). The chain can replace or
remove the three stages above, but not the filter body.

**Target measurement first:** before the chain catalogue is fixed, measure
on the target with DWT cycles per stage (P4 §3). The table figures are
instruction counts.

---

## 2. Measured cost of every built component

All figures are per voice per sample, from this session's probes
(`make -C tools/dsp_test armcheck-vx3`, `armcheck-vx2`, and the pre-S084
probes). Cycles are instructions plus 13 per VDIV, plus the stage overheads
named in each table.

### 2.1 Transient

**Overheads:** the mix-in costs 10 (Drum; fused into the post-chain, about
3, in the other engines). The buffer-stage framework costs about 3 per
sample (the per-block render call amortised).

| Option | Origin | Stage | + mix-in | + framework | Est. cycles | Behaviour status |
|---|---|---:|---:|---:|---:|---|
| none | — | 0 | 0 | — | 0 | — |
| `Snp` / `Ofs` | pre-S084 / P2 `trs` | ≈0 | 10 | — | ≈10 | worked |
| PCM click | pre-S084 | 22 | 10 | — | ≈32 | worked |
| PCM click | P2 `wav` | 18 | 10 | — | ≈28 | worked on host; not reported failing on hardware |
| sample transient | P2 `wav` | 15 | 10 | — | ≈25 | worked on host |
| stock burst (table) | P2 `wav` | 15 | 10 | — | ≈25 | worked on host |
| noise / crash burst | P2 `wav` | 14 / 16 | 10 | — | ≈24 / 26 | worked on host |
| `Flm` (flam) | P3 `buf` | 21 (after shrink) | 10 | 3 | ≈34 | fixed in remediation; **interface locks on hardware** |
| `Bzz` (stutter) | P3 `buf` | ≈21 repeat, 18 record, 0 idle | 10 | 3 | ≈34 | rewritten (looped stale audio before) |
| `Rvs` (reverse) | P3 `buf` | ≈38 playing | 10 | 3 | ≈51 | rewritten (played stale audio before); disabled on cost |
| `Res` (resonator) | P3 `buf` | 53 | 10 | 3 | ≈66 | disabled on cost |
| `Tub` (tube) | P3 `buf` | 53 | 10 | 3 | ≈66 | disabled on cost |
| `Spr` (spring) | P3 `buf` | 58 | 10 | 3 | ≈71 | disabled on cost |
| `Plk` (pluck) | P3 `buf` | 70–75 | 10 | 3 | ≈88 | was silent (seed cancelled); fixed; disabled on cost |

For a `buf` transient the generator outputs silence, so the 10-cycle mix-in
could be skipped in a chain that has no generator transient (−10).

### 2.2 Filter drive

The drive share is the shaped ZDF loop minus the `sft` loop, plus `sft`'s
own 13. No type adds a divide.

| Type | ZDF LP loop (P2 measured) | Drive share | Behaviour status |
|---|---:|---:|---|
| `sft` | 100 | 13 | pre-S084; worked |
| `chb` | 107 | 20 | built; **interface locks on hardware** (all non-`sft`) |
| `rct` | 108 | 21 | as above |
| `thf`, `asf` | 109 | 22 | as above |
| `bsw` | 117 | 30 | as above |
| `sfd` | 118 | 31 | as above |
| `rsb` | 120 | 33 | as above |
| `afd` | 121 | 34 | as above |
| `art` | 124 | 37 | as above |
| `fdc` | 126 | 39 | as above |
| `tsw`, `lsq` | 132 | 45 | as above |
| `lsw` | 134 | 47 | as above |

- **The missing character control:** about 1–3 more per type (P5), a
  block-constant blend or offset.
- **Clip bypass option:** removing the input clip entirely (a "clean" ZDF)
  saves the 13. It changes the filter's character at high `drv`.

### 2.3 Mix FX

**Overheads:** the ×1.25 input-gain pre-pass costs about 6 for every type
except `D+S`. The buffer-stage framework costs about 3.

| Type | Stage | + input gain | + framework | VDIV | Est. cycles | Behaviour status |
|---|---:|---:|---:|---:|---:|---|
| none | 0 | — | — | 0 | 0 | — |
| `D+S` (drive + SRR) | 27–33 | — | — | 1 | ≈40–46 | worked (pre-S084 equivalent 43); a drive-0 bypass would drop the VDIV (≈27–33) |
| `Crs` (crush) | 25–27 | 6 | — | 0 | ≈31–33 | built; no hardware report |
| `Rng` (ring) | 27 | 6 | — | 0 | ≈33 | built; no hardware report |
| `Fld` (fold) | 31–36 | 6 | — | 0 | ≈37–42 | built; no hardware report |
| `Pha` (phaser) | 33 | 6 | — | 0 | ≈39 | built; no hardware report |
| `Cmb` (comb) | 35–40 (after shrink) | 6 | 3 | 0 | ≈44–49 | **interface locks on hardware** |
| `Ech` / `Ec2` (echo) | ≈40 (after shrink) | 6 | 3 | 0 | ≈49 | as above |
| `Rev` / `Rv2` (room) | ≈50 (24 / 76 alternating) | 6 | 3 | 0 | ≈59 | was unstable (full-scale runaway); rebuilt; disabled on cost |
| `Cho` (chorus) | 51 | 6 | 3 | 0 | ≈60 | per-sample `sinf` removed; disabled on cost |
| `Fla` (flanger) | 57 | 6 | 3 | 0 | ≈66 | as above |

### 2.4 Tails

**What a tail costs:** a buffer type that changes or is replaced keeps
ringing on a tail runner (up to 2 runners), for up to about 3.5 s. Each
running tail costs its type's tail loop, about the same as the type's
process loop.

**The worst case:** 2 × `Fla` ≈ **130 cycles per frame**, shared by all
voices. That is about 25 % of the 528-cycle frame budget.

**Consequence:** with six voices each running an 88-cycle chain, live tails
push the total to about 125 %. Tails must therefore be budgeted inside the
frame budget (§5.3).

---

## 3. What worked

- **Cheap transients (P2 `trs`/`wav`):** PCM 18, sample 15, bursts 14–16,
  all under the old PCM cost of 22.
  - Sample transients reuse catalogue v2 (S085), with gain codes.
  - These are the most budget-friendly additions in the spike.
- **`D+S` split:** `D+S` as a separate stage is bit-identical to the old
  post-chain by construction (same truncation points). The compiler moves
  the curve's divide into the sample-and-hold branch, so lower rates are
  cheaper. Not proven by a test (`test_insert_ds` was never written).
- **Cheap Mix types:** `Crs`, `Rng`, `Fld` and `Pha` (31–42 cycles) are
  inside or near the `D+S` cost and need no buffer.
- **Shrunk buffer types:**
  - `Cmb` and `Ech` reach about 44–49 with a block-constant fractional tap;
  - `Flm` and `Bzz` reach about 34 with integer and record-then-loop reads.
- **Allocator mechanics:** the FxBuffer position allocator geometry, the
  floor, pass coalescing and the CrumpBit effective-minimum Rate are
  correct on host.
- **Tooling:**
  - **Tests:** the remediation's real tests (decay ≤ 3 s at every corner,
    trigger semantics, lifecycle regression cases) fail on the broken
    versions (the original Room fails 24 checks).
  - **Cost report:** `armcheck-vx3` and `looplen.py` give per-type costs
    from the real compiled code.
- **UI mechanics:** the label hooks (row labels follow the active type) and
  option hiding work as UI mechanisms.

## 4. What did not work

| Area | Problem | Evidence |
|---|---|---|
| Estimates | Buffer-type costs were estimated 2–5× low (`Ech` 14 → 71, `Cmb` 14 → 56, `Fla` 24 → 59). A fractional `delay_tap` alone costs about 20, from the int16 ↔ float conversions. | P3 §18.4 |
| Simulation | No P3 type had a Python/DSP model; the algorithms went straight to C. | Room unstable; `Plk` silent; `Bzz`/`Rvs` played the wrong audio |
| Filter drive | The per-type character control was never specified; every type's character is a fixed constant. Every shaped type misses its cost target (`sft` + 6) by 1–34. | P5; P2 §17.4 |
| Hardware | Any buffer-using type and any non-`sft` drive type lock the interface. Cause not found. | P3 §17.2; P4 §2 |
| Tests | The first type test was a tautology (an int16 is always within the int16 range). The lifecycle test had 8 checks for 16 cases. G2/G3/G4 were reported passed but never ran. | P3 §17.6, §18.6 |
| Controls | Parameters step per block (no ramp); Room's `dmp` was unwired (fixed); the transient `frq` mapping wasted 40 % of the knob (fixed). | P3 §17.4 |

## 5. The voice-buffer problems

### 5.1 What was built

- **Positions:** 16 FxBuffer positions (12 regular, 4 lease).
- **Records:** 14 records of 80 B in DTCM (1,120 B): one per slot × stage,
  plus 2 tail runners.
- **Lifecycle:** Hold overflow, same-use growth (`Ech` → `Ec2`), lease
  retagging for tails, re-adoption of a returning tail, pass-coalesced
  Effect share changes.
- **Elastic CrumpBit:** it compacts its newest audio when units take the
  top of its share.

### 5.2 What went wrong (P3 §17.3, fixed in §18 unless noted)

1. **Stuck runner:** re-adoption from a non-buffer type left a runner held
   forever (two events and every buffer change Holds).
2. **Wrong slot's tail:** tails could be adopted by the wrong slot.
3. **Moving ring base:** growth moved the ring base and ran the echo inside
   an uncleared unit. With the fix, growth almost never succeeds under
   first-free packing, so `Ech` → `Ec2` restarts instead of extending.
4. **Stale selection:** a held change could be applied after the user had
   chosen another type (two retry owners).
5. **CrumpBit hole:** a new unit was cleared before CrumpBit compacted,
   leaving a hole of up to 56 ms in its loop.
6. **Fit check:** the fit test ignored that the old ring stays occupied as
   a lease.
7. **Load order (not fixed):** on Scene/Instrument load, `trt` is applied
   before `fxt`, so `buf` against an outgoing `Ec2` is dropped (and the
   reverse problem exists in the other order). Every load also spent a
   runner on the TYPE→WAVE double selection (mitigated).
8. **`Ec2` → `Ech`:** treated as the same use, so it held two units.
9. **Compaction cost (not measured):** the CrumpBit rotate path does three
   full-length byte reversals in the foreground, about 1.5–2.8 ms against
   about 4.4 ms of queue slack.
10. **Freeze (not resolved):** the freeze may sit in this machinery. The
    first unit claim is also the first time the Effect share callback ever
    ran on the target.

### 5.3 What the chain changes about buffers

**Selection timing:** if a chain is chosen per slot and is not automatable
(applied only on edit and on Scene/Instrument load), most of the machinery
is unnecessary.

| Simplification | Removes | Cost of the simplification |
|---|---|---|
| At most **one buffer stage per chain** | the second record per slot (6 records instead of 12), the `trt`/`fxt` load-order problem, two-stage fits | no chain can pair a buffer transient with a buffer Mix type (they do not fit 88 together anyway, §6) |
| **One unit per buffer** (drop `Ec2`/`Rv2`) | growth, adjacency, pairs, same-use C3/C4, the 2-unit lease math | no 100 ms echo or long room |
| With ≤ 1 unit per slot and 2 one-unit tails, **at most 8 positions** are used, all within positions 0–11 | **the elastic CrumpBit work and compaction entirely**: positions 0–11 never overlap CrumpBit's 70,592 B loop | the four lease positions and the floor change become unused; the floor stays at its pre-S084 value |
| Tails budgeted **inside the frame budget** | the 125 % worst case | the per-voice chain budget falls to about 66 when two tails may run (6 × 66 + 2 × 66 ≈ 528), or tails are limited to the cheaper types |
| Alternative: **no tails** | runners, leases, Hold, re-adoption | conflicts with the standing rule that tails are never cut; would need your explicit decision |

**Recommendation:** one buffer stage per chain, one unit per buffer, two
one-unit tail runners, and no elastic CrumpBit. Reserve the unit at chain
apply (not at boot), release it into a tail on a chain change, and budget
tails inside the frame.

---

## 6. What absolutely will not fit in 88 cycles

These hold even with the other stages at their minimum: no transient (0),
`sft` (13), or no Mix stage (0), whichever applies.

| Combination | Minimum cost | Why it fails |
|---|---:|---|
| `Plk` as built (any chain) | 75 + 3 + 13 = 91 (+10 with mix-in) | over on its own |
| Any buffer transient + any buffer Mix type, with mix-in | ≥ 34 + 44 + 13 = 91 | two buffer stages; also excluded by §5.3 |
| `Res`/`Tub`/`Spr` + any Mix type other than none | ≥ 66 + 31 + 13 = 110 | over |
| `Rvs` + `D+S` or any buffer Mix | ≥ 51 + 40 + 13 = 104 | over |
| Stateful drive `tsw`/`lsq`/`lsw` (45–47) + any buffer Mix type | ≥ 45 + 44 = 89 | over even with no transient |
| Stateful drive (≥ 37) + `D+S` + a PCM/sample/burst transient | ≥ 37 + 40 + 24 = 101 | over |
| `Fla` + `sft` + any transient | ≥ 66 + 13 + 10 = 89 | over; fits only with no transient (79) |
| Any chain combining an expanded option from all three stages at their worst (`Plk` + `lsw` + `Fla`) | ≈ 201 | the original S084 assumption |
| Any chain with live tails, if each voice already uses the full 88 | ≥ 528 + tails | tails must come out of the budget (§5.3) |

---

## 7. What might fit if the other options are excluded

**Example chains** (estimated cycles per voice-sample, ≤ 88, before
tails):

| Chain idea | Transient | Drive | Mix | Est. total | Notes |
|---|---|---|---|---:|---|
| **Classic** (default, pre-S084 behaviour plus P2 transients) | `trs`/`wav` (≤ 28) | `sft` 13 | `D+S` (≤ 46) | ≤ 87 | must be the default; old kits load into it |
| **Drive** | `trs` (10) | any shaped type (≤ 47) | `Crs`/`Rng` (≤ 33) or none | ≤ 90 → limit to the types ≤ 45, or no Mix | lightest transient leaves room for the stateful drives |
| **Drive + click** | `wav` (≤ 28) | any shaped type (≤ 47) | none | ≤ 75 | `D+S` does not fit beside a stateful drive and a PCM click |
| **Mild drive + D+S** | `trs` (10) | `chb`/`rct`/`thf`/`asf` (≤ 22) | `D+S` (≤ 46) | ≤ 78 | |
| **Colour** | `trs`/`wav` (≤ 28) | `sft` 13 | `Crs`/`Rng`/`Fld`/`Pha` (≤ 42) | ≤ 83 | no buffer |
| **Space** | none or `trs` (≤ 10) | `sft` 13 | `Ech`/`Cmb` (≤ 49), `Rev` (59), `Cho` (60) | ≤ 83 | one buffer; tails need room (§5.3) |
| **Space, wide** | none | `sft` 13 | `Fla` (66) | 79 | flanger only without a transient |
| **Resonant hit** | `Res`/`Tub`/`Spr` (≤ 71, mix-in skipped: ≤ 61) | `sft` 13 | none | ≤ 84 (≤ 74) | one buffer |
| **Rhythmic hit** | `Flm`/`Bzz` (≈ 34, mix-in skipped: ≈ 24) | `sft` 13 | `Crs`/`Rng` (≤ 33) | ≤ 80 | one buffer |
| **Reverse hit** | `Rvs` (≈ 51, ≈ 41 without mix-in) | `sft` 13 | none or `Crs` (≤ 33) | ≤ 87 | one buffer |
| **Pluck** | `Plk` after the P4 rework (≈ 30–40 target) | `sft` 13 | none or cheap Mix | ≤ 86 | only if the rework lands |

**Implications:**
- **Re-enabling:** the types disabled in the spike on per-type caps (`Rev`,
  `Cho`, `Fla`, `Res`, `Tub`, `Spr`, `Rvs`) become possible inside a chain
  that gives them most of the 88.
- **The drive types:** they become viable in chains that drop `D+S`.

---

## 8. Requirements for the S086 design

1. **Freeze first.** Find the interface freeze on the spike build (P4 §2
   capture plan) before re-implementing either buffers or shaped drives.
2. **The `chain` row:**
   - **Where:** a selector row in the transient section, not automatable,
     applied on edit and at Scene/Instrument load.
   - **Value 0** is the Classic chain (old kits unchanged).
   - **Other rows:** the transient wave, drive type and Mix type rows read
     their option lists from the active chain (the `InstrumentCellFormat`
     mechanism used for `typ` → `wav`). Hidden options are skipped.
   - **Placement:** appended after S085's rows.
3. **Per-chain cost gate.** Each chain's worst case (all options at their
   most expensive, plus its share of tails) is measured on the target and is
   ≤ the chain budget before the chain is offered.
4. **Models before code.** A Python/host model for every transient and Mix
   type in a chain, with reference outputs the host tests compare against.
   The filter drive model gains the P5 character parameter.
5. **Tests that can fail:** decay ≤ 3 s at every control corner, trigger
   semantics, the lifecycle cases for the simplified buffer model, and
   `test_insert_ds` (`D+S` identical to the pre-S084 chain).
6. **Buffers per §5.3:** at most one buffer stage per chain, one unit per
   buffer, tails inside the budget, no elastic CrumpBit (unless your
   decisions in §9 change this).
7. **Filter drive:** add the second parameter (P5), byte 64 = today's fixed
   setting. Settle the drive cost target with the target measurement.

## 9. Decisions for you before S086 starts

1. **Tails:** budget two runners inside the frame (per-voice chain budget
   about 66), or allow tails only for chains whose types are cheap, or no
   tails (this needs your explicit change to the "tails are never cut"
   rule).
2. **Two-unit types:** keep `Ec2`/`Rv2` (brings back growth and pairs, and
   possibly the elastic CrumpBit) or drop them.
3. **Chain catalogue:** which of the §7 chains to build first.
4. **Classic chain transients:** the P2 `trs`/`wav` set, or the pre-S084
   set only.
5. **Clean ZDF option:** whether a chain may bypass the `sft` clip to free
   13 cycles.
6. **Disabled types:** which disabled types are worth reworking (P4 §4)
   once a chain can give them the budget.
