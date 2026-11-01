# S089 P1 — Voice Chain Architecture

## 0. Purpose

A `chn` ("Voice Chain") selector on each of the four instrument types (Drum,
Snare, Cymbal, HiHat) lets the user choose an alternative post-oscillator
processing chain. An alternative chain may spend no more than the stages of
the default chain (transient, filter drive, distortion, SRR) that it replaces;
§1 gives the measured budgets.

The first implementation adds the selector architecture and one alternative
chain type alongside the default.

---

## 1. Budget

### 1.1 What a chain can replace (measured, S089 P0 build)

Figures are instructions per voice per sample, worst case, from the linked
build that passed S089 P0 on hardware. They are textual loop lengths from
`elf_loopdump.py` unless the Source column says otherwise. Cycles are
estimated as instructions + 13 per VDIV (VDIV.F32 takes 14 cycles on the M7),
the same rule as the S086 evaluation.

| Replaceable stage | Drum | Snare | Cymbal / HiHat | Source |
|---|---:|---:|---:|---|
| Transient generator (built-in PCM click, the dearest) | 22 | 22 | 22 | S087 record; the user-sample loop measures 20 |
| Transient mix-in | 2 (`QADD16` pass, pre-filter) | 2 (`QADD16` pass, post-filter) | 4 (inside the post loop) | measured |
| Filter input clip `sft` | 13 | 13 | 13 | S086 evaluation; it is inside the ZDF loop and cannot be separated in the ELF |
| Distortion curve | 6 incl. 1 VDIV | 6 incl. 1 VDIV | 6 incl. 1 VDIV | measured: `vmov, vabs, vmul, vfma, vdiv, vmul` |
| SRR (the mixer's S&H) | 10 | 10 | 10 | measured (§1.3) |

**Fixed costs.** Every chain pays these, so they are not part of any budget:
- **Filter:** the ZDF loop, 87–97 instructions + 2 VDIV.
- **The rest of the post loop:** Drum 19, Snare 24, Cymbal/HiHat 15. That
  covers the amp gain ramp, Drum's velocity ramp, Snare's tone-oscillator
  mix, the int16/float conversions, the store and the loop overhead.

**Why the old table was wrong.** It said transient 32, `sft` 13 and
post-chain 35, for a total of ≈93. Two things changed:
- The mix-in is now an unrolled `QADD16` pass, about 2 per sample instead of
  10.
- Only 6 of the post loop's 25–30 instructions are the distortion curve.
  The rest is fixed.

That old ≈93 total is superseded.

**Where the transient sits.** In Drum the click is mixed in **before** the
filter. In Snare it is added **after** the filter, in its own pass. In
Cymbal and HiHat it is added inside the fused post loop. A Cymbal or HiHat
chain that drops the transient needs a post-loop variant without the add;
that variant saves the 4 instructions. Otherwise the transient buffer has to
be zeroed every block.

### 1.2 The three replacement budgets

| Budget | Stages replaced | Drum | Snare | Cymbal / HiHat |
|---|---|---:|---:|---:|
| **A** transient + drive | generator, mix-in, `sft` | **37** (no VDIV) | **37** | **39** |
| **B** transient + drive + distortion + SRR | A + curve + SRR | **53** incl. 1 VDIV, ≈66–70 cycles | **53**, ≈66–70 | **55**, ≈68–72 |
| **C** distortion + SRR | curve + SRR | **16** incl. 1 VDIV, ≈29–33 cycles | **16**, ≈29–33 | **16**, ≈29–33 |

The upper end of each cycle range allows for the stall from the SRR's float
compare (`vcmpe`/`vmrs`). None of the fold options in §4.6 divides, so B and C
are compared in cycles: a fold can use the cycles the curve's divide no
longer takes. Budget A has no divide, so there instructions ≈ cycles.

### 1.3 SRR credit (S089 P0)

SRR is the mixer's S&H, `mixer_decimateBlock()`. It runs once per slot per
block when `instrumentManager_slotUsesSrr()` returns 1. A chain that leaves
SRR out may spend SRR's worst-case cost in its own stages: **10 instructions
per sample**, about 12–15 cycles (estimated, not measured).

This is the float S&H loop that passed hardware in S089 P0. An integer S&H
(7 instructions) was built and then reverted by user decision on 2026-10-30
(`S089_P0_NEW_SRR_DISABLE.md` §8).

### 1.4 Rules

- **Per type.** A chain on a given type must cost no more than the stages it
  replaces on that same type. If, for example, a Cymbal chain were allowed
  Drum's figure, a Scene of six Cymbals would cost more than it does today.
- **Measure before offering.** Every figure in §4.6 that is not marked as
  measured is a hand estimate. Before a chain ships, measure it in a spike
  build with `elf_loopdump.py` and pass the S089 P0 §4.2 gates: the
  inlined-set diff and the hot-loop comparison. The post loops are inlined
  into `mixer_calcNextSampleBlock()`, where LTO's inlining choices shift
  easily (P0 F3).
- **Constant cost.** New per-sample loops must not branch on the data. Use
  `SSAT`, `BIC`, `EOR`, `vabs`, `vmaxnm`/`vminnm` and IT blocks instead.

---

## 2. The `chn` parameter

### 2.1 Parameter properties

| Property | Value |
|---|---|
| File key | `voice_chain` |
| Short name | `chn` |
| Long name | `Chain` |
| Category | `Voice` (displayed in the click/transient sub-page) |
| Display | DTYPE_ON_OFF with ONOFF_TAG_VOICE_CHAIN (tag 2) |
| Flags | AUTOMATABLE, SELECTOR (not MORPHABLE, not MODULATABLE) |
| Stored in | the Normal instrument image only |
| Binding | `IM_SPECIAL_VOICE_CHAIN` to a `chainType` field in each engine struct |
| Default | 0 = `T+D` (Trns+Drv; the current chain, bit-identical) |

Like `osc1_type` and `osc1_run`, the chain selector is applied before the
Morph image on every slot reset. It is step-automatable (one value per step,
no ramp). It is not an LFO or velocity modulation target.

### 2.2 Selector constraints

The chain value can only be changed while "clicked in" to single-parameter
view. Normal pot turns do not change it. Step automation adds the value by
holding a step and turning the encoder in single-parameter view.

When clicked in: the 8-character chain name appears on the left of the
display; the 3-character short name on the right. This is the same pattern
used by the sample name display.

### 2.3 Menu placement

On the click/transient sub-page (sub-page index 4 for Drum, same position
for Snare/Cymbal/HiHat), appended as the fourth cell:

```
wav vol frq chn
```

Cell 3 (0-indexed) = `DRUM_PARAM_VOICE_CHAIN` (and the equivalent enum
values for the other three engines).

### 2.4 Descriptor row

```c
ROW_SELECT_ONOFF_SPECIAL("voice_chain", "Voice", "Chain", "chn",
    ONOFF_TAG_VOICE_CHAIN, chainType, TYPE_UINT8,
    IM_SPECIAL_VOICE_CHAIN | IM_SPECIAL_OSC1)
```

The `IM_SPECIAL_OSC1` bits are a placeholder for the oscillator selector —
here they are unused but keep the tag byte layout consistent. Alternatively,
a dedicated special-writer flag with no oscillator association could be
cleaner: `IM_SPECIAL_VOICE_CHAIN` alone.

**Append-only rule:** the new enum value is appended after
`DRUM_PARAM_OSC1_RUN` (and equivalently for Snare/Cymbal/HiHat), so every
existing descriptor index, automation ID, LFO token, NRPN ID and MIDI Learn
row is unchanged. A file without the key loads 0 (`T+D`, today's sound).

### 2.5 Name tables

```c
/* MenuText.h */
#define ONOFF_TAG_VOICE_CHAIN   2u

static const char voiceChainNames[][4] = {
    {2}, {"T+D"}, {"WFd"},
};

static const char voiceChainLongNames[][9] = {
    {2}, {"Trns+Drv"}, {"WavFldr "},
};
```

The leading byte is the count, as in `oscRunNames`. Future chain types are
appended: the count increases, and the menu clamp, automation range and
display all follow.

### 2.6 Engine struct additions

Each engine struct gains one field:

```c
uint8_t chainType;   /* VOICE_CHAIN_* */
```

This is 1 byte, fitting within existing alignment padding or the slot
reserve (2 × sizeof(DrumVoice) = 2 × 588 = 1,176 B). Verify:
- DrumVoice: 588 → 589 (reserve is 1,176 B)
- SnareVoice: 420 → 421
- CymbalVoice: 476 → 477
- HiHatVoice: 484 → 485

All well within the 1,176 B slot reserve.

---

## 3. Chain dispatch architecture

### 3.1 Design principle

The voice chain is not an independent effects system. It is a compile-time
dispatch inside each engine's existing `calc*SyncBlock*()` render function.
The chain type selects which code runs in the three budget stages:

1. **Pre-filter** (replaces transient + transient mix-in)
2. **Filter input** (replaces `sft` soft clip inside the ZDF loop)
3. **Post-filter** (replaces the fused post-chain: amp × velo × distortion)

### 3.2 Dispatch pattern

```c
/* DrumVoice.c, inside Drum_calcVoiceSyncBlock(): */
switch (voice->chainType) {
case VOICE_CHAIN_WAVEFOLDER:
    /* wavefolder chain: no transient, sft drive, wavefolder post-chain */
    wavefolder_preMix(voice, buf, modBuf, size);
    SVF_calcBlockZDF(&voice->filter, voice->filterType, buf, size);
    wavefolder_postChain(buf, voice, size);
    break;
default: /* VOICE_CHAIN_DEFAULT */
    /* today's chain, bit-identical */
    transient_calcBlock(&voice->transGen, modBuf, size);
    bufferTool_addBuffersSaturating(buf, modBuf, size);
    SVF_calcBlockZDF(&voice->filter, voice->filterType, buf, size);
    voicePost_drum(buf, ...);
    break;
}
```

The filter call is shared — the chain decides what happens before and after
it. The `itp` crossfade blend happens before the chain dispatch (it is part
of the oscillator section, not the chain).

### 3.3 Constant-CPU rule

An alternative chain may cost no more than the default-chain stages it
replaces (§1.2; the SRR credit is §1.3). If a chain
drops the transient, its replacement stage runs the same number of
instructions. If a chain drops the `sft` clip, the saved cycles go to the
post-filter stage. The chain dispatch `switch` is branch-predicted and adds
negligible overhead (one compare + conditional branch, < 1 cycle amortised).

### 3.4 No new RAM allocation

The wavefolder chain state fits inside the existing `Distortion` struct's
`shape` and `inv_shape` fields, reused for the wavefolder parameters. No new
structs, no buffer positions, no DTCM.

If a future chain type needs additional per-voice state (e.g. a delay tap),
it goes into a union with the current chain state, subject to the slot
reserve and user approval.

---

## 4. First alternate chain: WavFldr (`WFd`)

### 4.1 What it does

A pre-filter wavefolder/waveshaper replaces the transient, and the
post-filter stage simplifies to amp EG × velocity gain → int16 store
(no distortion curve, no VDIV).

The wavefolder takes the oscillator output, scales it by a depth parameter,
and folds it through a periodic triangle fold (the same `driveShaper_triFold`
from the S084 spike's `DriveShaper.h`, proven correct). The `transient_wave`
parameter repurposes as the wavefolder type selector; `transient_vol` and
`transient_freq` repurpose as fold depth and symmetry.

### 4.2 Parameter name remapping

When `chn` = `WFd`, some parameters on the click/transient page show
different names:

| Descriptor key | T+D (default) name | WFd name |
|---|---|---|
| `transient_wave` / `wav` | Waveform | FoldType |
| `transient_vol` / `vol` | Volume | Depth |
| `transient_freq` / `frq` | Frequncy | Symmetry |

The short names remain the same (`wav`, `vol`, `frq`). Only the long names
change. This is handled by an `instrumentCellFormat` lookup that reads
`chainType` and returns the appropriate string, extending the existing
mechanism that `osc1_type` uses to vary the `wav` cell's name.

For the post-filter stage: the `instrument_drive` parameter (`drv`) is still
stored but shows `---` when `chn` = `WFd` (the wavefolder chain does not use
post-filter distortion; the drive knob's value is preserved for if the user
switches back to `T+D`). Display-only change: no new parameter, no stored
value loss.

### 4.3 Wavefolder types

The `wav` cell under `WFd` selects the wavefolder algorithm. Starting set:

| Value | Short | Long | Algorithm | Estimated cost |
|---|---|---|---|---:|
| 0 | `Tri` | `TriFold ` | Triangle fold: `driveShaper_triFold(x·d + ofs, 1.0)` | ≈12 instr |
| 1 | `Sin` | `SinFold ` | Sine fold: `driveShaper_sinFold(x·d + ofs)` | ≈18 instr |
| 2 | `Asn` | `AsynFold` | Asymmetric sine fold with cubic accent: `sinFold(x·d + 0.05·(x·d)³ + ofs)` | ≈22 instr |
| 3 | `Chb` | `Chebyshv` | 3rd-order Chebyshev: `3x - 4x³` (from `DriveShaper.h` `DRIVE_TYPE_CHB`) | ≈8 instr |

These are all proven implementations from the S084 spike's `DriveShaper.h`.
They run without state, without divisions, without buffers.

The spike measured the Mix FX `Fld` type at 31–42 cycles. That figure
included an input-gain pre-pass of ≈6 and its own S&H decimator. §4.6 lists
every fold's cost by placement. The click plus its mix-in now costs 24 for
Drum and Snare and 26 for Cymbal and HiHat (§1.1), not ≈32.

### 4.4 Budget for WFd chain (Drum worst case)

WFd replaces the click with a pre-filter fold pass and drops the distortion
curve. That makes it a budget-B chain (§1.2).

| Stage | Default `T+D` (Drum) | WFd (Drum) |
|---|---:|---:|
| Pre-filter | click 22 + mix-in 2 | `Asn` fold pass ≈25 |
| Filter input | `sft` 13 | `sft` 13 |
| Distortion curve | 6 incl. 1 VDIV (≈19 cycles) | none |
| SRR | 10 | 10 kept / 0 dropped |
| **Replaceable total** | **53 incl. 1 VDIV, ≈66–70 cycles** | **≈48 (SRR kept) / ≈38 (dropped)** |

The fixed post loop (Drum 19: amp ramp × velocity ramp → int16) still
runs. WFd is ≈18–28 cycles under its budget. The same layout fits Snare
(53) and Cymbal/HiHat (55) with the same margins.

### 4.5 Constant-CPU concern

The WFd chain costs ≈38–48 against ≈66–70 for the stages of `T+D` it replaces. This violates the constant-CPU
rule in spirit (but not in letter, since the rule says no CPU saving from
skipping work "because an element is inactive, silent or set to zero" —
the chain type is an explicit algorithm choice, like filter type or oscillator
waveform, which are the existing exceptions listed in §1.1 of the DSP
reference).

Two options:
1. **Accept the variance** as an algorithm choice (like filter type). The
   worst case remains T+D, which is the default. WFd is cheaper.
2. **Pad WFd** with NOP work to match T+D's cost. This is wasteful.

Recommendation: option 1. The user chose the chain type explicitly; the
worst case (T+D) is the one that matters for headroom.

### 4.6 Wavefolding options by budget

**Sources.**
- **S084 spike** (`Helicase-dev-ph7-spike/`):
  - `DriveShaper.h`: the filter-slot types `thf`, `asf`, `sfd`, `afd`
    and `chb`.
  - `InsertFold.c`: the Mix-stage `Fld`.
  - `S086_EFFECTS_CHAIN_POST_MORTEM_EVAL.md` §2: the measured costs.
- **S085 spike** (`Helicase-dev-ph7-voice-pwm-spike/`) has no fold work. It
  was oscillator expansion, and its lesson is cost discipline: `pwm` reached
  100 % CPU on the device.

#### Fold catalogue: cost per sample, by placement

| Fold | Shape | Filter-input slot (replaces `sft`) | Post loop (replaces the curve) | Separate int16 pass |
|---|---|---:|---:|---:|
| `Chb` Chebyshev T3, `3x − 4x³` | adds the 3rd harmonic; folds once past ±1 | **20** measured (`chb`) | ≈5 | ≈13 |
| `TrI` integer triangle fold | periodic linear fold on the int sample (new; not in the spikes) | — | ≈6 | ≈11 |
| `Tri` triangle fold | periodic linear fold, `driveShaper_triFold()` | **22** measured (`thf`) | ≈10 | ≈18 |
| `Asf` asymmetric triangle fold | offset fold with DC removed; adds even harmonics | **22** measured (`asf`) | ≈11 | ≈19 |
| `Sin` sine fold | triangle fold → sin(π/2·t) polynomial; smooth | **31** measured (`sfd`) | ≈15 | ≈23 |
| `Asn` asymmetric sine fold | sine fold of x + 0.05x³ | **34** measured (`afd`) | ≈17 | ≈25 |
| Spike Mix `Fld` | `Tri` with depth/symmetry plus its own S&H | — | — | **31–36** measured (≈21–26 without its S&H) |

**How each column is costed.**
- **Filter-input slot.** These are S084 measured drive shares: the shaped ZDF
  loop minus the `sft` loop, plus `sft`'s 13.
- **Post loop.** Hand estimates. The fold works on the float sample already
  in the post loop, so it adds no conversions and no loop overhead. Depth
  multiplies into the existing gain once per block, and symmetry is one add.
- **Separate pass.** The post-loop figure plus ≈8 for the loop and the
  int16/float conversions (≈5 for `TrI`). A pass can sit anywhere in the
  render: before the filter for Drum, Cymbal and HiHat, or on Snare's tone
  oscillator before its mix.
- **Folding before or after the amp envelope.** A fold placed after the amp
  gain (post loop) changes timbre as the envelope decays, like today's
  distortion. A fold placed before it (pass or filter slot) keeps the same
  timbre throughout the decay.
- **Hardware risk.** In S084, every non-`sft` filter-slot type locked the
  interface on hardware, and the cause was never found (S084 P4 §2; the
  spike's own image build was intact, so S089 P0's stale-image finding does
  not explain it). Filter-slot placements should come last, after that
  freeze is understood.

#### Budget A: transient + drive only (Drum/Snare 37, Cymbal/HiHat 39)

The click is dropped. Under each fold's shape, the "+ `sft` kept" rows put
that fold in a separate pass, and the "no `sft`" rows put it in the filter
slot instead.

| Chain | Cost | Drum / Snare (37) | Cymbal / HiHat (39) |
|---|---:|:-:|:-:|
| `TrI` pass + `sft` kept | 11 + 13 = 24 | ✓ | ✓ |
| `Chb` pass + `sft` kept | 13 + 13 = 26 | ✓ | ✓ |
| `Tri` pass + `sft` kept | 18 + 13 = 31 | ✓ | ✓ |
| `Asf` pass + `sft` kept | 19 + 13 = 32 | ✓ | ✓ |
| `Sin` pass + `sft` kept | 23 + 13 = 36 | ✓ (1 spare) | ✓ |
| `Asn` pass + `sft` kept | 25 + 13 = 38 | ✗ (+1) | ✓ |
| `TrI` pass → `Chb` pass + `sft` kept (two folds) | 11 + 13 + 13 = 37 | ✓ (exact) | ✓ |
| `Chb` / `Tri` / `Asf` in the filter slot, no `sft` | 20–22 | ✓ | ✓ |
| `Sin` / `Asn` in the filter slot, no `sft` | 31 / 34 | ✓ | ✓ |
| `TrI` pass + `Tri` in the filter slot | 11 + 22 = 33 | ✓ | ✓ |
| Click kept + any fold | ≥ 24 + 20 = 44 | ✗ | ✗ |

#### Budget B: transient + drive + distortion + SRR (Drum/Snare ≈66–70 cycles, Cymbal/HiHat ≈68–72)

Compared in cycles, because no fold divides. "Click" means generator plus
mix-in: 24 for Drum/Snare, 26 for Cymbal/HiHat. The Cost column uses the
Drum/Snare figure.

| Chain | Cost (Drum / Snare) | Drum / Snare | Cymbal / HiHat |
|---|---:|:-:|:-:|
| Click + `sft` + post `Sin` (SRR dropped) | 24 + 13 + 15 = 52 | ✓ | ✓ |
| Click + `sft` + post `Asn` (SRR dropped) | 54 | ✓ | ✓ |
| Click + `sft` + post `Tri` + SRR kept | 24 + 13 + 10 + 10 = 57 | ✓ | ✓ |
| Click + `sft` + post `Asn` + SRR kept | 64 | ✓ (tight) | ✓ (tight) |
| Click + `sft` + post `Tri` → original curve (fold then distortion) | 24 + 13 + 10 + ≈19 = 66 | ✓ (exact) | ✓ |
| No click: `Sin` pass + `sft` + post `Tri` (two folds; WFd-like) | 23 + 13 + 10 = 46 | ✓ | ✓ |
| No click: `Asn` pass + `sft` + post `Sin` + SRR kept | 25 + 13 + 15 + 10 = 63 | ✓ | ✓ |
| No click: `Asn` in the filter slot + post `Sin` + SRR kept | 34 + 15 + 10 = 59 | ✓ | ✓ |
| Click + `Sin` in the filter slot + post `Tri` | 24 + 31 + 10 = 65 | ✓ (tight) | ✓ |
| Click + `sft` + post `Tri` → curve + SRR kept | ≈76 | ✗ | ✗ |

#### Budget C: distortion + SRR only (≈29–33 cycles, every type)

The click and `sft` stay as they are today. Only the post loop's curve
changes, and SRR is dropped unless the last column says it can stay.

| Post-loop fold | Cost | Fits (≈29–33) | SRR kept too (fold + 10–15) |
|---|---:|:-:|:-:|
| `Chb` | ≈5 | ✓ | ✓ (≈15–20) |
| `TrI` | ≈6 | ✓ | ✓ (≈16–21) |
| `Tri` | ≈10 | ✓ | ✓ (≈20–25) |
| `Asf` | ≈11 | ✓ | ✓ (≈21–26) |
| `Sin` | ≈15 | ✓ | ✓ (≈25–30, tight) |
| `Asn` | ≈17 | ✓ | ✗ / tight (≈27–32) |
| `Tri` → original curve (divide kept) | ≈10 + ≈19 = 29 | ✓ (exact) | ✗ |
| `Sin` → `Chb` (two folds) | ≈20 | ✓ | ✗ |

#### Recommendation

1. **First alternative chain: a budget-C fold.** Use `Sin` (or `Asn` with
   SRR dropped) in the post loop, in place of the curve.
   - **Smallest change:** one post-loop variant per engine. No new pass, no
     filter-slot change, and no transient change.
   - **Timbre:** fold depth follows the amp envelope, like today's
     distortion.
   - **Controls:** the existing `drv` cell can become fold depth.
2. **Second: WFd as planned (budget B).** A pre-filter fold pass whose depth
   is independent of the envelope, plus a post loop without the curve.
3. **Last: filter-slot folds.** Only after the S084 interface lock is
   explained.

---

## 5. Implementation plan

### 5.1 Phase 1 — Architecture (this session)

**Changes in order:**

1. **MenuText.h:** add `ONOFF_TAG_VOICE_CHAIN`, `voiceChainNames[]`,
   `voiceChainLongNames[]` and `VOICE_CHAIN_*` constants.

2. **Engine structs:** add `uint8_t chainType` to `DrumVoice`, `SnareVoice`,
   `CymbalVoice`, `HiHatVoice`. Verify all four still fit the slot reserve
   with `_Static_assert`.

3. **Parameter files:** append `DRUM_PARAM_VOICE_CHAIN` (and equivalents)
   after `*_PARAM_OSC1_RUN`; add the `ROW_SELECT_ONOFF_SPECIAL` descriptor
   row; increment `*_PARAM_DESCRIPTOR_COUNT`; add the cell to sub-page 4.

4. **Special writer:** add `IM_SPECIAL_VOICE_CHAIN` to `InstrumentManager.h`;
   handle it in `instrumentManager_writeSpecialRuntime()` to write
   `chainType` on the engine struct (with a range clamp).

5. **Menu.c:** extend the six `DTYPE_ON_OFF` sites to dispatch on
   `ONOFF_TAG_VOICE_CHAIN` (tag 2) using `voiceChainNames[]`. The
   single-parameter click-in view shows `voiceChainLongNames[]` on the left
   and `voiceChainNames[]` on the right.

6. **Engine render dispatch:** in each engine's `calc*SyncBlock*()`, add
   the `switch (voice->chainType)` with the default case being today's code
   (moved, not changed) and the `VOICE_CHAIN_WAVEFOLDER` case calling the
   new chain.

7. **voiceChainWavefolder.h:** a new static-inline header (like
   `voicePostChain.h`) containing the wavefolder pre-filter and simplified
   post-chain functions.

8. **Cell format:** extend `InstrumentCellFormat` (or the equivalent
   mechanism) so that when `chainType == VOICE_CHAIN_WAVEFOLDER`, the
   transient page's long names read "FoldType", "Depth", "Symmetry" instead
   of "Waveform", "Volume", "Frequncy". And `instrument_drive` shows `---`.

9. **Init and trigger:** `*_initVoice()` sets `chainType = 0`. No change
   to trigger logic is needed (the transient trigger is skipped when the
   chain does not use it; it costs nothing if `transient_calcBlock()` is
   not called).

10. **Host tests:** add wavefolder reference vectors to `tools/dsp_test/`:
    the four fold types at grid points for depth and symmetry, compared
    against the Python reference (`driveShaper_triFold` and `sinFold` from
    `plot_waveshapers.py`).

### 5.2 Phase 2 — Hardware verification (after build)

- `make all && make img`
- Flash and verify: default chain `T+D` on all voices, play through
  existing presets — must be bit-identical to pre-S089 firmware.
- Switch one voice to `WFd`: listen through all four fold types at
  various depth and symmetry settings.
- Step-automate `chn` between `T+D` and `WFd` across pattern steps.
- Worst-case Scene (all voices active) with the DEV CPU widget: confirm
  no underruns.
- H1–H17 matrix from S088 still passes.

### 5.3 Phase 3 — Future chain types (deferred)

Potential future chain types, each within its §1.2 budget:

| Chain | Short | Pre-filter | Drive | Post-filter | Notes |
|---|---|---|---|---|---|
| `T+D` | default | transient | `sft` | amp × velo × distortion | today |
| `WFd` | wavefolder | wavefolder | `sft` | amp × velo → int16 | Phase 1 |
| `DrS` | drive shapes | transient | shaped `chb`/`rct`/etc. | amp × velo → int16 | spike's drive types, as a chain |
| `Crs` | crush | bit crush + S&H | `sft` | amp × velo → int16 | spike's `Crs` insert |
| `Rng` | ring mod | ring mod osc | `sft` | amp × velo → int16 | spike's `Rng` insert |
| `Fld+D` | fold + drive | wavefolder | none (bypass) | amp × velo × distortion | fold before filter, distortion after |
| `Buf` | buffer | delay tap | `sft` | amp × velo → int16 | needs delay-line reservation, deferred |

These are listed for context. Only `T+D` and `WFd` are in Phase 1.

---

## 6. Files touched (Phase 1)

| File | Change |
|---|---|
| `Core/Menu/MenuText.h` | `ONOFF_TAG_VOICE_CHAIN`, name tables, `VOICE_CHAIN_*` constants |
| `Core/DSPAudio/voiceChainWavefolder.h` | **new**: wavefolder pre-filter and simplified post-chain |
| `Core/DSP/Instruments/Drum/DrumVoice.h` | `uint8_t chainType` |
| `Core/DSP/Instruments/Drum/DrumVoice.c` | chain dispatch in `Drum_calcVoiceSyncBlock()` |
| `Core/DSP/Instruments/Drum/DrumParameters.h` | `DRUM_PARAM_DESCRIPTOR_COUNT` 42 → 43 |
| `Core/DSP/Instruments/Drum/DrumParameters.c` | enum + descriptor row + page cell |
| `Core/DSP/Instruments/Snare/SnareVoice.h` | `uint8_t chainType` |
| `Core/DSP/Instruments/Snare/Snare.c` | chain dispatch |
| `Core/DSP/Instruments/Snare/SnareParameters.h` | count |
| `Core/DSP/Instruments/Snare/SnareParameters.c` | enum + row + cell |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.h` | `uint8_t chainType` |
| `Core/DSP/Instruments/Cymbal/CymbalVoice.c` | chain dispatch |
| `Core/DSP/Instruments/Cymbal/CymbalParameters.h` | count |
| `Core/DSP/Instruments/Cymbal/CymbalParameters.c` | enum + row + cell |
| `Core/DSP/Instruments/HiHat/HiHatVoice.h` | `uint8_t chainType` |
| `Core/DSP/Instruments/HiHat/HiHat.c` | chain dispatch |
| `Core/DSP/Instruments/HiHat/HiHatParameters.h` | count |
| `Core/DSP/Instruments/HiHat/HiHatParameters.c` | enum + row + cell |
| `Core/DSP/Instruments/InstrumentManager.h` | `IM_SPECIAL_VOICE_CHAIN` |
| `Core/DSP/Instruments/InstrumentManager.c` | special writer case |
| `Core/Menu/menu.c` | 6 DTYPE_ON_OFF sites: tag 2 dispatch; click-in display; `---` for hidden params |
| `tools/dsp_test/modules/test_wavefolder.c` | **new**: host reference tests |

---

## 7. Lessons from the spikes

### What we keep
- The `driveShaper_triFold()` and `driveShaper_sinFold()` algorithms from
  `DriveShaper.h`: proven, measured, reference-tested.
- The per-type cost tables from the S084/S086 post-mortem: they correctly
  identified which operations fit the budget.
- The "one chain type, one budget" principle from the S086 evaluation §5.3.
- The cell-format relabelling mechanism from `InstrumentCellFormat.c/h`.

### What we do not repeat
- **No independent Mix/Transient/Drive selectors** (S084 P3 complexity).
  One selector, one chain, one budget.
- **No voice buffers in Phase 1.** Buffer-using types need the delay-line
  reservation system. That is Phase 3.
- **No InsertEffectsManager, no InsertEffect registry, no type structs.**
  The chain is a direct dispatch in the engine's existing render function.
- **No per-sample state for the wavefolder.** The fold functions are pure
  (stateless).
- **No hardware-untested types.** Only proven algorithms ship.
- **No types that exceed the budget.** Every chain's worst case is measured
  and recorded before it is offered.

### The S084/S085 failures we avoid
1. **Hardware freeze (S084):** we do not add buffer positions, lease
   machinery, or Effect share callbacks. No new foreground/ISR interaction.
2. **CPU overload (S085):** the chain budget is the existing budget, not a
   new one. The worst case is still T+D.
3. **Missing character control (S084 P5):** WFd's parameters (depth,
   symmetry) are wired from day one through the existing transient-page
   descriptor rows.
4. **No Python model (S084 P3):** the fold types already have a reference
   implementation in `plot_waveshapers.py` and the host test bench.

---

## 8. Open questions

1. **Post-filter stage for WFd:** the plan drops the distortion curve
   entirely (no VDIV, just amp × velo → int16). An alternative is to keep
   the distortion and drop the transient instead (transient → nothing,
   `sft` → `sft`, post → today's amp × velo × distortion). That is a
   budget-A chain: a fold pass in place of the click (Drum/Snare ≤ 24,
   Cymbal/HiHat ≤ 26). `Sin` (≈23) fits; `Asn` (≈25) fits only Cymbal/HiHat
   (§4.6). User decision.

2. **Filter drive for WFd:** should the `sft` soft clip remain, or should
   the chain bypass it to get a "clean" pre-fold-into-filter path? Bypassing
   saves ≈13 cycles. The S086 evaluation listed "clean ZDF" as a user
   decision (§9.5).

3. **The `instrument_drive` (`drv`) knob:** when `chn` = `WFd`, should it
   (a) show `---` and be unavailable, or (b) repurpose as a wavefolder
   parameter (e.g. an additional depth/character control)?

4. **Snare voice chain:** the Snare fused post-chain includes a mix step
   (oscillator × mix + transient). Should the wavefolder chain drop the
   transient entirely or allow the mix parameter to control fold depth?
