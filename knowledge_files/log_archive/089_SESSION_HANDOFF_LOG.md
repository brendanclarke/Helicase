# Session 089 — Handoff Log

**Date:** 2026-10-30
**Branch:** `dev-ph7-voice_expansion`
**Base commit:** `80029d31` (S088 closeout, uncommitted)
**Tip at close:** `d944c87f` (P3 implementation complete) + uncommitted P3
changes (pending hardware H1–H6)
**Build (at P3 close):** `text 683,512`, `data 424`, `bss 412,884`
**Flash headroom:** 69,728 B
**ITCM:** 15,240 / 16,384 B (1,144 B free)

---

## End of session block

```
DATE: 2026-10-30
SESSION GOAL: Per-instrument SRR enable, voice chain architecture, WFd
  wavefolder chain, Drum FM depth ramp efficiency
COMPLETED:
  - P0: per-instrument SRR enable (instrumentManager_slotUsesSrr() gates
    mixer_decimateBlock() per slot). Hardware H0-H6 passed.
  - P0 (disused): SRR move into engine structs — 5 attempts (A-E), all froze
    on hardware. Root cause analysis supersedes the I-cache cliff explanation.
  - P0 follow-up: integer S&H (Q31, 10→7 instructions) built and reverted
    by user decision.
  - P1: voice chain architecture — chn selector, chainType uint8_t in all
    4 engines, IM_SPECIAL_VOICE_CHAIN special writer, ONOFF_TAG_VOICE_CHAIN
  - P2: WFd wavefolder chain — 8 fold types (Tri, TrD, Sin, SnD, Asn, Chb,
    TrI, Fd2) in voiceFold.c/.h, integer Q22 triangle core, 5th-order sine
    polynomial, 4 ITCM loops, voicePost_gainDist() no-add twin, guards on
    all Snp/Ofs reads. Hardware passed.
  - P2: CPU ceiling reached — binding: no per-sample DSP cost may increase.
  - P3: Drum FM depth ramp folded into 3 dearest carrier loops (option B:
    slp, smp, wtb blend). 4 ramped twins in ITCM. Host tests pass;
    hardware H1-H6 pending.
VERIFIED ON HARDWARE:
  - P0: H0-H6 passed (user, 2026-10-30)
  - P2: hardware passed (user, 2026-10-30)
  - P3: hardware H1-H6 pending at handoff

CHANGES THIS SESSION:
  - Makefile: link_budget.py path fix (P0 step 0)
  - Core/DSP/Instruments/InstrumentManager.h: slotUsesSrr() decl (P0),
    IM_SPECIAL_VOICE_CHAIN enum (P1), slotVoiceChain(), slotFoldType() (P2)
  - Core/DSP/Instruments/InstrumentManager.c: slotUsesSrr() defn (P0),
    VOICE_CHAIN special writer (P1), slotVoiceChain(), slotFoldType() (P2)
  - Core/DSP/Instruments/Drum/DrumVoice.h: chainType field (P1)
  - Core/DSP/Instruments/Drum/DrumVoice.c: chain dispatch, Snp/Ofs guards,
    WFd path (P2), fm branch rewrite with typed/stock split (P3)
  - Core/DSP/Instruments/Drum/DrumParameters.c/.h: voice_chain row (P1)
  - Core/DSP/Instruments/Snare/Snare.h: chainType field (P1)
  - Core/DSP/Instruments/Snare/Snare.c: chain dispatch, Snp/Ofs guards,
    WFd path (P2)
  - Core/DSP/Instruments/Snare/SnareParameters.c/.h: voice_chain row (P1)
  - Core/DSP/Instruments/Cymbal/CymbalVoice.h: chainType field (P1)
  - Core/DSP/Instruments/Cymbal/CymbalVoice.c: chain dispatch, Snp/Ofs
    guards, WFd path, pitchMod hold, voicePost_gainDist() call (P2)
  - Core/DSP/Instruments/Cymbal/CymbalParameters.c/.h: voice_chain row (P1)
  - Core/DSP/Instruments/HiHat/HiHat.h: chainType field (P1)
  - Core/DSP/Instruments/HiHat/HiHat.c: chain dispatch, Snp/Ofs guards,
    WFd path, pitchMod hold, voicePost_gainDist() call, inGain 2/outGain
    0.5 (P2)
  - Core/DSP/Instruments/HiHat/HiHatParameters.c/.h: voice_chain row (P1)
  - Core/DSPAudio/voiceFold.h (new): VOICE_CHAIN_DEFAULT/WAVEFOLDER,
    VOICE_FOLD_TRI..VOICE_FOLD_COUNT, voiceFold_process() decl
  - Core/DSPAudio/voiceFold.c (new): voiceFold_process(), 4 ITCM loops
    (triLoop, chbLoop, sinLoop/asnLoop, fd2Loop), Q22 triangle core,
    5th-order sine polynomial, depth decay
  - Core/DSPAudio/voiceFoldTables.c (new, generated): decay step table
  - Core/DSPAudio/voicePostChain.h: voicePost_gainDist() no-add twin for
    WFd Cymbal/HiHat
  - Core/DSPAudio/mixer.c: SRR gate on instrumentManager_slotUsesSrr() (P0)
  - Core/DSPAudio/Oscillator.c: S089 P3 block (4 ramped twin loops +
    dispatcher osc_renderTypedFmRamp()), .itcm.s089p3 section
  - Core/DSPAudio/Oscillator.h: osc_renderTypedFmRamp() decl
  - Core/Menu/MenuText.h: ONOFF_TAG_VOICE_CHAIN (tag 2), voiceChainNames,
    voiceChainLongNames
  - Core/Menu/menu.c: fold-wave cell display, chn tag display, automation
    max under WFd
  - STM32F765VIHx_FLASH.ld: ITCM input order pinned (.itcm.s089 explicit,
    .itcm.s089p3 appended)
  - tools/plots/gen_fold_tables.py (new): decay table generator
  - tools/dsp_test/modules/voicefold.c (new): laws F1-F10 + break build
  - tools/dsp_test/modules/oscillator.c: fmramp group FR1-FR2
  - tools/dsp_test/modules/instrument_manager.c: law 6 updated, law 8 added
  - tools/dsp_test/check_special_tags.py: updated
  - tools/dsp_test/Makefile: voicefold target, fmramp group, break builds

KNOWN ISSUES INTRODUCED:
  - CPU at ceiling: worst-case Scene 100% on cpu widget (binding §8 of
    S089_P2_WAVEFOLDER_CHAIN.md)
  - Cymbal/HiHat pitchMod keeps last snap value when wav leaves Snp mid-snap
    under T+D (latent, not changed by S089, noted in P2 §9.5)
  - P3 hardware H1-H6 still pending at handoff

KNOWN ISSUES RESOLVED:
  - Makefile link_budget.py path broken since f3805478 (P0 step 0)
  - I-cache cliff explanation for S089 P0 freeze attempts superseded by P0 §1

NEXT SESSION RECOMMENDED GOAL: P3 hardware H1-H6, then session closeout
BLOCKERS: P3 hardware verification requires flashing build/LXRV2_lxr02.img

CRITICAL REMINDERS FOR NEXT SESSION:
  - CPU ceiling is binding: no per-sample DSP cost increase from here
  - Mixer byte size is NOT a cost measure — use elf_hotloops.py +
    inlined-set symbol diff (P0 F3)
  - ITCM has 1,144 B free (15,240/16,384) — any new ITCM code needs explicit
    budget sign-off
  - The SRR-move root cause is in S089_P0_NEW_SRR_DISABLE.md §1, not the
    I-cache explanation in S089_P0_SRR_MOVE_DISUSED.md §9.6
```

---

## 1. Session 089 P0 — Per-Instrument SRR Enable

### 1.1 Background and disused approach

The original plan (`S089_P0_SRR_MOVE_DISUSED.md`) was to move sample-rate
reduction state from the mixer's DTCM arrays into each voice engine struct
and run the S&H loop inside each engine's `calcVoiceSyncBlock()`. Five
implementation attempts (A–E) were made; all froze on hardware at boot.

The implementation schedule for the move is recorded in
`S089_P0_IMPLEMENTATION_DISUSED.md`. It was applied and reverted. The
22-change schedule across 4 engine structs, InstrumentManager binding, and
the 4 engine render functions is documented there in full.

### 1.2 Root cause analysis (supersedes I-cache explanation)

`S089_P0_NEW_SRR_DISABLE.md` §1 provides the analysis that supersedes the
I-cache cliff explanation from the failed attempts:

- **F1: stale image.** Since commit `f3805478`, `make all && make img` never
  rebuilt the `.img` because the Makefile called `tools/link_budget.py` at
  the old path (moved to `tools/diagnostic_tools/link_budget.py`). `make
  all` exited with error 2, so the `&&` prevented `make img`. Every hardware
  test since that commit may have flashed an old image.

- **F2: boot order.** `main.c` calls `dsp_init()` →
  `instrumentManager_runtimeInit()` **before** `mixer_init()`. Attempt E's
  `mixer_init()` set every enable to 0 after InstrumentManager had already
  set it to 1. SRR was **off** at boot, so the render was cheaper — a
  boot freeze cannot be a cycle cost.

- **F3: inliner budget, not spills.** Moving `mixer_decimateBlock()` into
  `instrumentManager_calcSlotSyncBlock()` freed LTO inlining budget, which
  was re-spent on `busComp_processBlock()`. The mixer's byte size is not a
  cost measure; gate changes on `elf_hotloops.py` + inlined-set symbol diff.

- **F4: SRAM1 `.bss` layout shifts.** Every failed attempt added RAM (6–48 B),
  shifting 800+ SRAM1 data symbols. A latent OOB write hitting a different
  neighbour after a shift fits a freeze that does not scale with code size.

### 1.3 The shipped design

`instrumentManager_slotUsesSrr(uint8_t slot)` returns 1 when the slot's
runtime type is Drum, Snare, Cymbal, or HiHat (their default chain includes
SRR). Returns 0 for unknown/empty slots and any future type. The mixer gates
`mixer_decimateBlock()` on this predicate.

Key properties:
- **No new RAM.** `.bss` byte-identical to HEAD. Avoids F4.
- **No new state.** Reads `runtime_slot_type[slot]`, which is already written
  by `instrumentManager_runtimeInit()` and `resetRuntimeSlotFromScene()`.
- **No init ordering hazard.** Avoids F2.
- **Constant CPU.** The answer depends only on type (and from P1, the chain
  selector), never on the `srt` value or voice activity.

### 1.4 Implementation steps (all passed)

| Step | Description |
|---|---|
| 0 | Makefile: fix `link_budget.py` path (`tools/` → `tools/diagnostic_tools/`) |
| 1 | `InstrumentManager.h:835`: `slotUsesSrr()` declaration |
| 2 | `InstrumentManager.c:2033`: `slotUsesSrr()` definition (switch on `instrumentManager_slotType()`) |
| 3 | `mixer.c:1022`: gate `mixer_decimateBlock()` on `instrumentManager_slotUsesSrr(slot)` |

### 1.5 Spike measurements (variant Q, the shipped design)

| Metric | HEAD | Q (shipped) |
|---|---:|---:|
| Mixer bytes | 13,508 | 13,848 |
| `.text` | 676,264 | 676,600 |
| `.bss` | 412,884 | 412,884 |
| Inlined set | — | unchanged |
| Hot loops vs HEAD | — | 36 equal, 0 shrank, 1 grew |
| S&H loop | 10 instr | 10 instr (opcode-identical) |

The one loop that grew (27→28, dry-mix routing case) is a block-layout change
from any mixer edit. Instructions executed on the common path are the same.

### 1.6 Hardware verification

H0–H6 passed on hardware (user, 2026-10-30):
- H0: HEAD baseline on same SD card — boot OK, Scene switch OK
- H1: Step 0 alone (Makefile fix) — byte-identical image
- H2: Steps 0–3 — boot + worst-case Scene, `cpu` matches H0
- H3: `srt` 127 on all four types — A/B vs H0, no difference
- H4: `srt` ≈20 sustained — S&H staircase sounds same as H0
- H5: LFO→SampleRt and Velocity→SampleRt — smooth, restores
- H6: Scene switches changing slot types with `srt` < 127 — no clicks/DC

### 1.7 Integer S&H follow-up (built, REVERTED)

A Q31 integer S&H was built as a follow-up: replaces the float counter with
`SADD16`/`SSUB16` on a packed `{cnt, rate}` halfword pair. Reduces the S&H
loop from 10 to 7 instructions per sample per slot.

The change was verified (spike measurements, identical `.bss`, host tests
pass), but **reverted by user decision** on 2026-10-30. The user chose to
keep the float S&H for now. The implementation is fully documented in
`S089_P0_NEW_SRR_DISABLE.md` §8 for future reference.

---

## 2. Session 089 P1 — Voice Chain Architecture

### 2.1 The `chn` parameter

| Property | Value |
|---|---|
| File key | `voice_chain` |
| Short name | `chn` |
| Long name | `Chain` |
| Category | Voice (click/transient sub-page) |
| Display | `DTYPE_ON_OFF` with `ONOFF_TAG_VOICE_CHAIN` (tag 2) |
| Flags | AUTOMATABLE, SELECTOR (not MORPHABLE, not MODULATABLE) |
| Stored in | Normal instrument image only |
| Binding | `IM_SPECIAL_VOICE_CHAIN` to `chainType` field in each engine struct |
| Default | 0 = `T+D` (Trns+Drv; the default chain, bit-identical to pre-S089) |

Like `osc1_type` and `osc1_run`, the chain selector is applied before the
Morph image on every slot reset. It is step-automatable (one value per step,
no ramp). It is not an LFO or velocity modulation target.

### 2.2 Chain defines

```c
/* voiceFold.h */
#define VOICE_CHAIN_DEFAULT     0u   /* T+D: transient + drive (today's sound) */
#define VOICE_CHAIN_WAVEFOLDER  1u   /* WFd: wavefolder (S089 P2)              */
```

### 2.3 Engine struct additions

Each engine struct gained `uint8_t chainType`:
- `DrumVoice.h:272`
- `Snare.h:156`
- `CymbalVoice.h:145`
- `HiHat.h:154`

All within existing alignment padding. `runtime_slots` stays 7,584 B.

### 2.4 Menu placement

On the click/transient sub-page (sub-page 4), appended as cell 3:
```
wav vol frq chn
```

When clicked in: 8-char chain name on left, 3-char short name on right.
Under WFd: `wav` lists fold names, single-parameter view shows per-type
`frq` labels (Bias, Decay, Asymmtry, Harmonic, Ratio).

### 2.5 Name tables (`MenuText.h`)

```c
#define ONOFF_TAG_VOICE_CHAIN   2u          /* MenuText.h:211 */

static const char voiceChainNames[][4] = {  /* MenuText.h:221 */
    {2}, {"T+D"}, {"WFd"},
};

static const char voiceChainLongNames[][9] = {
    {2}, {"Trns+Drv"}, {"WavFldr "},
};
```

### 2.6 Budget measurement (S089 P0 build)

| Budget | Stages replaced | Drum/Snare | Cymbal/HiHat |
|---|---|---:|---:|
| **A** transient + drive | generator, mix-in, `sft` | 37 | 39 |
| **B** A + distortion + SRR | A + curve + SRR | ~66–70 cycles | ~68–72 |
| **C** distortion + SRR | curve + SRR | ~29–33 cycles | ~29–33 |

WFd uses Budget A only: transient + drive replaced, filter drive and SRR kept.

### 2.7 SRR credit (P0)

A chain that leaves SRR out may spend its 10 instructions/sample in its own
stages. The float S&H loop that passed hardware is the reference. The integer
S&H (7 instructions) was reverted.

---

## 3. Session 089 P2 — WFd Wavefolder Chain

### 3.1 Architecture

The WFd chain replaces the transient and its mix-in with a wavefolder on
the oscillator signal, ahead of the filter. Filter drive (`sft`), the ZDF
filter, post-chain (amp ramp, velocity ramp, distortion), and SRR are all
kept — same as the default chain.

The transient page's three controls are repurposed:

| Cell | `T+D` meaning | `WFd` meaning |
|---|---|---|
| `wav` | transient waveform | fold type (8 types) |
| `vol` | transient level | fold depth |
| `frq` | transient pitch | per-type secondary control |

### 3.2 Fold placement per engine

| Engine | Fold runs on | Replaces |
|---|---|---|
| **Drum** | `buf`, after oscillator + `itp` blend, before ZDF | `transient_calcBlock()` + QADD16 mix-in |
| **Snare** | tone oscillator block (`transBuf`), before post-chain | `transient_calcBlock()` + post-filter QADD16 add |
| **Cymbal** | `buf`, after FM carrier + `itp` blend, before ZDF | `transient_calcBlock()` into `mod` + the add in post loop |
| **HiHat** | same as Cymbal (`mod1`) | same as Cymbal |

**Cymbal/HiHat** use the new `voicePost_gainDist()` no-add twin (21
instructions/sample vs 25 for the T+D add loop). They also hold
`osc.pitchMod = 1.0f` under WFd since it is only assigned in the snap path.

**HiHat** renders its carrier at 0.5, so it passes `inGain 2, outGain 0.5`
to give the same fold depth per `vol` as Cymbal while preserving T+D level.
Drum and Cymbal pass 1/1.

**Snare** folds the tone oscillator (unfiltered), not the noise ahead of the
filter. The fold sits ahead of the post-chain. Decision D1.

### 3.3 Guards

All eight `Snp`/`Ofs` reads in the four engines are guarded with
`chainType == VOICE_CHAIN_DEFAULT`:
- Drum: `DrumVoice.c:310/317/462`
- Snare: `Snare.c:213/309`
- Cymbal: `CymbalVoice.c:233/336`
- HiHat: `HiHat.c:243/355`

Under WFd the stored `transient_wave` byte means a fold type, not a waveform,
so the snap pitch envelope and oscillator start offset paths must not run.

### 3.4 The eight fold types

| # | Name | Short | Core | `frq` control |
|---|---|---|---|---|
| 0 | Tri | `Tri` | Q22 integer triangle fold | Bias (DC offset, 0–127) |
| 1 | TrD | `TrD` | same triangle + depth decay | Decay time (5 ms – 2 s) |
| 2 | Sin | `Sin` | 5th-order sine polynomial over triangle | Bias |
| 3 | SnD | `SnD` | same sine + depth decay | Decay time |
| 4 | Asn | `Asn` | sine fold + a·t² even-order accent | Asymmetry (a = 0–0.5) |
| 5 | Chb | `Chb` | x·((1+2m) − 4m·x²), unity small-signal | Harmonic blend |
| 6 | TrI | `TrI` | triangle fold with depth ×1–×16 | Bias |
| 7 | Fd2 | `Fd2` | two cascaded integer folds, stage-2 ratio 1–4 | Ratio |

**Depth mapping:** `depth = 1 + span × vol²` (span 7 for most types, 15 for
TrI). Fine control near clean end. Last depth stored in `transGen.midiFreq`
as Q11.

**Integer Q22 triangle core:** `smlawb` (depth × sample + bias), `sbfx`
24-bit wrap, `|·|`, one subtract. Matches the spike's float `triFold` to
4.8×10⁻⁷.

**5th-order sine polynomial:** `1.5703368 t − 0.6421070 t³ + 0.0717703 t⁵`.
Error 1.04×10⁻⁴, |p| ≤ 1. Applied over the integer triangle output.

**Asn redefined:** `y = sinfold(t) + a·t²` — an even-order accent after the
fold. `frq` = a in 0–0.5, exactly 0 at silence. The planned pre-fold cubic
was dropped (would have cost ~3 more per sample).

**Chb:** `y = x·((1 + 2m) − 4m·x²)` normalised to unity small-signal gain.
Input scale capped at |x| ≤ 1, no clamp needed. `vol` 0 drives at 1/8 full
scale.

**Fd2:** Two cascaded folds; `frq` controls the stage-2 depth ratio (1–4).
At `frq` 0 equals Tri at bias 0.

**Decay table:** `voiceFold_decayStep[128]` (5 ms – 2 s), generated by
`tools/plots/gen_fold_tables.py` (`--check`).

### 3.5 Code structure

- **`voiceFold.h`** (`voiceFold.h:56–57`): chain and fold type defines.
  `VOICE_FOLD_TRI` = 0, `VOICE_FOLD_COUNT` = 8.
- **`voiceFold.c`** (`voiceFold.c:337`): `voiceFold_process()` entry point
  (runs from flash). Per-sample loops in ITCM (`.itcm.s089`, section
  attribute at `voiceFold.c:39`).
- **`voiceFoldTables.c`**: generated decay step table.
- **`voicePostChain.h:139`**: `voicePost_gainDist()` no-add twin.

### 3.6 Measured loop costs (linked build, after §19 fixes)

| Loop | Types | Instructions/sample | Budget |
|---|---|---:|---:|
| `voiceFold_triLoop` | Tri, TrD, TrI | 12 | 24 / 26 |
| `voiceFold_chbLoop` | Chb | 14 | 24 / 26 |
| `voiceFold_fd2Loop` | Fd2 | 20 | 24 / 26 |
| `voiceFold_sinLoop` | Sin, SnD | 22 | 24 / 26 |
| `voiceFold_asnLoop` | Asn | 22 | 24 / 26 |
| `voicePost_gainDist` (Cym/HiHat, WFd) | — | 21 | — |

All loops are branch-free; cost does not depend on `vol`, `frq`, or settings.

### 3.7 Decisions as taken

| # | Plan | Decision |
|---|---|---|
| D1 | Snare folds tone oscillator | **Accepted.** Normalised: `inGain = 1/(1−mix)` capped at 16, `outGain = (1−mix)`. |
| D2 | `Asn` on Cymbal/HiHat only if over budget | **Superseded:** all types fit. |
| D3 | DC compensation only (no DC blocker) | **Accepted.** |
| D4 | Fold code in ITCM | **Approved.** Only per-sample loops in ITCM. |
| D5 | Six core types first | **All eight types shipped.** |
| D6 | Linear depth | **Fine control:** `depth = 1 + span × vol²`. |
| D7 | `frq` raw 0–127 | **Accepted.** |

### 3.8 Build metrics (after P2 §19 fixes)

| Metric | P0 | P2 |
|---|---:|---:|
| ITCM | 12,656 B | 13,032 B |
| Flash headroom | 76,644 B | 72,008 B |
| `runtime_slots` | 7,584 B | 7,584 B (unchanged) |
| `.data` | 420 B | +4 B (LTO placement) |
| `.bss` start | — | shifted +8 B (hardware passed) |

Default chain `T+D` is arithmetic-identical to S089 P0.

### 3.9 Host tests

- `voicefold` module: laws F1–F10 (identity at depth 1, fold symmetry,
  sine polynomial accuracy, Chb unity gain, decay convergence, etc.)
- `instrument_manager`: law 6 updated, law 8 added (chain tag)
- `special_tags`: 167 rows, 0 mismatches
- Break build: catches fold law violations

### 3.10 Assessment fixes (§19)

Five fixes F1–F5 were applied after the initial implementation assessment:
- **F1:** HiHat `inGain 2 / outGain 0.5` for carrier-at-0.5 normalisation
- **F2–F5:** minor corrections documented in `S089_P2_IMPLEMENTATION.md` §19

After fixes, re-measured: 0 loops grew vs P0, ITCM 13,032 B, all gates pass.

---

## 4. CPU Ceiling — Binding (S089 P2 §8)

**The worst-case Scene reached 100% on the `cpu` widget after P2.** The user
declared: "absolutely no possibility to increase anything at this point."

### 4.1 What the ceiling covers

- Every voice engine, every chain (`T+D` and `WFd`)
- Every oscillator mode and type
- The mixer, the bus, and the Effects
- Small additions too: 1 instruction per sample in a per-voice loop is 6 per
  frame across the slots

### 4.2 New features under the ceiling

Any new DSP feature must be paid for by an equal or larger measured saving
elsewhere, gated with `elf_loopdump.py`, `elf_hotloops.py`, and the
inlined-set diff, and confirmed on the `cpu` widget against the worst-case
Scene.

A new chain or fold type may only replace stages within its §2 budget. The
unused margin under WFd budgets is not headroom to spend: the worst-case
Scene is set by the dearest settings across the whole system (e.g. Drum `fm`
at ~90 per sample in the oscillator section), not by WFd.

### 4.3 Where savings could come from

- The `itp` crossfade pool
- The Drum FM depth-ramp loop (10 per sample) — **addressed in P3**
- The integer S&H (−3 per slot, built and reverted)

---

## 5. Session 089 P3 — Drum FM Depth Ramp Efficiency

### 5.1 The problem

Drum's `fm` branch scales the modulator block by a per-sample depth ramp
before the FM carrier reads it. The ramp loop costs 10 instructions per
sample every block in `fm` mode, even though it only changes anything on
the one block after a retrigger (`ampRampArm`). On all other blocks,
`s = 1.0` and the pass is the identity.

### 5.2 The solution (option B, user decision)

Replace `osc->fmMod` with a local depth `d` that ramps per sample
(`d += dStep`) inside the carrier loop itself. The separate 10-instruction
pass goes away. Net saving: ~9 instructions per sample.

**Only the 3 dearest FM carriers** get ramped copies (option B):
- `osc_slpFmBlock` → `osc_slpFmRampBlock` (looped sample)
- `osc_smpFmBlock` → `osc_smpFmRampBlock` (one-shot sample)
- `osc_wtbFmBlendBlock` → `osc_wtbFmBlendRampBlock` (wavetable blend,
  with `osc_wtbFmRampBlock` as fallback when no blend slot is free)

Every carrier already below the new worst case (Crash, table waves, sine,
noise, plain `wtb`) keeps the existing pass.

### 5.3 Carrier ranking (measured, S089 P2 build)

| Carrier | FM loop | Today (with pass) | Folded (twin + 1) | Covered |
|---|---:|---:|---:|:-:|
| `slp` | 50 | **90** | 81 | yes |
| `wtb` blend | 46 | **86** | 77 | yes |
| `smp` | 42 | **82** | 73 | yes |
| Crash | 38 | 78 | — | no (keeps pass) |
| Table waves | 35 | 75 | — | no |
| `wtb` not blending | 32–34 | 74 | (73–75 as blend fallback) | fallback only |
| Sine | 29 | 69 | — | no |
| Noise (FM ignored) | 10 | 50 | — | no |

### 5.4 Implementation

- **4 ramped twin loops** in `Oscillator.c` (`.itcm.s089p3` section):
  - `osc_smpFmRampBlock` (`Oscillator.c:3361`)
  - `osc_slpFmRampBlock` (`Oscillator.c:3445`)
  - `osc_wtbFmBlendRampBlock` (with `osc_wtbFmRampBlock` fallback)

- **Flash dispatcher** `osc_renderTypedFmRamp()` (`Oscillator.c:3705`,
  `Oscillator.h:921`): selects among the three by `oscType`.

- **Drum `fm` branch** (`DrumVoice.c:843`): when osc 1 is `slp`/`smp`/`wtb`,
  calls the ramped dispatcher with `(d0, dStep)` and skips the modulator-
  scaling pass. For other carriers, keeps today's pass + existing loops.

- **Each twin** = original loop + 1 `vadd.f32` (depth ramp per sample).
  The mnemonic multiset differs by exactly one `vadd.f32`.

### 5.5 Ramp endpoints

- `d0 = clamp(lastFmDepth, 0, 4·fmMod)` — only when armed and `fmMod > 0.0001`
- `d1 = fmMod`
- `dStep = (d1 − d0) / (size − 1)`
- **Unarmed blocks:** `d0 = fmMod`, `dStep = 0` → S0 (bit-identical). FR1 host law.
- **Armed blocks:** S2 (slightly less quantisation — inaudible). FR2 host law.

### 5.6 Static gates (all passed)

| Gate | Result |
|---|---|
| Twin hot loops | `slp` 51/40, `smp` 43, `wtb` blend 47/43, `wtb` fallback 33/35: each original + 1 |
| Originals and stock FM loops | `elf_loopdump.py` text identical to baseline |
| ITCM addresses | every function keeps its address; twins at `0x32d0`–`0x3b70` |
| Mixer | `elf_hotloops.py`: 39 equal, 0 shrank, 0 grew |
| Sizes | text 683,512, data 424, bss 412,884 |
| ITCM total | 15,240 / 16,384 B |

### 5.7 Linker

ITCM input order in `STM32F765VIHx_FLASH.ld:180`:
```
*(.itcm.s089)
*(.itcm.s089p3)
```

`.itcm.s089` is now named explicitly (was caught by `*(.itcm.*)`).
`.itcm.s089p3` follows. Existing ITCM addresses are stable.

### 5.8 Host tests

- `fmramp` group: FR1 (unarmed = original, sample-for-sample) and FR2
  (armed ramp endpoints) — both pass
- 2 break proof builds: `fmr1` break catches FR1 violations (160 failures),
  `fmr2` break catches FR2 violations (90 failures)
- All other test groups unaffected

### 5.9 Hardware

`build/LXRV2_lxr02.img` (683,952 B) was ready at handoff. The six hardware
checks H1–H6 were **pending** (not yet done by user):
- H1: boot and play Drum `fm` with `slp`/`smp`/`wtb` — sounds correct
- H2: A/B against the P2 image with `fm` carriers — no audible difference
- H3: worst-case Scene `cpu` reads lower than P2
- H4: `itp` on `fm` carriers — smooth crossfade
- H5: Scene switch between `fm` types — no glitch
- H6: non-`fm` Drum sounds unchanged

---

## 6. Files and Code Sections (full reference)

### 6.1 voiceFold.h

New file. Defines:
- `VOICE_CHAIN_DEFAULT` (0), `VOICE_CHAIN_WAVEFOLDER` (1)
- `VOICE_FOLD_TRI` (0) through `VOICE_FOLD_FD2` (7), `VOICE_FOLD_COUNT` (8)
- `voiceFold_process()` declaration

### 6.2 voiceFold.c

New file. Contains:
- `voiceFold_process(int16_t *buf, uint8_t size, ...)` — entry point, runs
  from flash. Dispatches to one of four ITCM loops based on fold type.
- `voiceFold_triLoop` — Q22 integer triangle fold (Tri, TrD, TrI). 12 instr.
- `voiceFold_chbLoop` — Chebyshev-style fold (Chb). 14 instr.
- `voiceFold_sinLoop` — 5th-order sine fold (Sin, SnD, Asn shares this). 22 instr.
- `voiceFold_asnLoop` — Asn (sine + even-order accent). 22 instr.
- `voiceFold_fd2Loop` — Two-stage cascaded fold (Fd2). 20 instr.
- Section attribute `.itcm.s089` on the loops.

### 6.3 voicePostChain.h

Added `voicePost_gainDist()` no-add twin at line 139. Used by Cymbal and
HiHat under WFd. 21 instructions/sample vs 25 for the T+D add loop. Removes
the transient-buffer add from the post-chain for engines that don't use the
transient under WFd.

### 6.4 Oscillator.c — S089 P3 block

Starting at `Oscillator.c:3339`, section `.itcm.s089p3`:
- `osc_smpFmRampBlock()` — ramped one-shot sample FM twin
- `osc_slpFmRampBlock()` — ramped looped sample FM twin (with lead-in)
- `osc_wtbFmBlendRampBlock()` — ramped wavetable blend FM twin
  (with `osc_wtbFmRampBlock()` fallback for no-blend-slot case)
- `osc_renderTypedFmRamp()` at `Oscillator.c:3705` — flash dispatcher

### 6.5 mixer.c

SRR gate at `mixer.c:1022`:
```c
if (instrumentManager_slotUsesSrr(slot))
    mixer_decimateBlock(slot, sampleData);
```

### 6.6 InstrumentManager.h/.c

- `instrumentManager_slotUsesSrr()` — `InstrumentManager.h:835` /
  `InstrumentManager.c:2033`
- `IM_SPECIAL_VOICE_CHAIN` enum — `InstrumentManager.h:193` (value 22)
- `instrumentManager_slotVoiceChain()` — reads `chainType` from engine struct
- `instrumentManager_slotFoldType()` — reads `transGen.waveform` (fold type
  under WFd)

### 6.7 Engine structs and dispatch

All four engines:
- **Struct:** `chainType` uint8_t field added
- **Init:** `chainType` set to 0 (`VOICE_CHAIN_DEFAULT`)
- **Render:** chain dispatch in `calc*SyncBlock()` — if `chainType ==
  VOICE_CHAIN_WAVEFOLDER`, calls `voiceFold_process()` instead of
  `transient_calcBlock()` + mix-in
- **Guards:** all `Snp`/`Ofs` reads conditioned on `chainType ==
  VOICE_CHAIN_DEFAULT`

### 6.8 STM32F765VIHx_FLASH.ld

Line 180: ITCM section ordering pinned:
```
*(.itcm.s089)
*(.itcm.s089p3)
```
Previously `.itcm.s089` was caught by the `*(.itcm.*)` catch-all. Now
explicit, with P3's subsection appended after it.

### 6.9 Descriptor files

All four `*Parameters.c/.h`: `voice_chain` row added using
`ROW_SELECT_ONOFF_SPECIAL()` with `ONOFF_TAG_VOICE_CHAIN`, `chainType`,
`IM_SPECIAL_VOICE_CHAIN`. Appended after `osc1_run`. Descriptor counts +1.

### 6.10 MenuText.h

- `ONOFF_TAG_VOICE_CHAIN` = 2 (line 211)
- `voiceChainNames` / `voiceChainLongNames` (line 221)

### 6.11 menu.c

- Six `DTYPE_ON_OFF` sites updated for the tag split (shared with S088)
- Fold-wave cell: shows fold names under WFd, count, long names
- Automation max clamped under WFd
- Name-table asserts added

---

## 7. Specification documents to update

The following reference documents need S089 updates (documented here for the
logging session to execute):

- `INSTRUMENTS_DSP_REFERENCE.md` — voice chain architecture, WFd fold types,
  fold loop costs, `chn` parameter, the budget framework
- `CPU_USE_DSP_AUDIT.md` — items for P0 (SRR enable), P2 (fold loops, no-add
  post), P3 (ramped twins, dispatcher, Drum fm savings), binding mandates
- `EFFECTS_MIXER_DSP_REFERENCE.md` — conditional SRR
  (`instrumentManager_slotUsesSrr()`)
- `STORAGE_SRAM_MANIFEST.md` — S089 P2 `.data` shift, HiHat +4 B, ITCM
  progression
- `FILESYSTEM_SPEC.md` — `voice_chain` key, fold type values
- `MIDI_SPEC.md` §5.5 — `chn` automation, NRPN range

---

## 8. Failed Approaches

### 8.1 SRR move into engine structs (P0 original plan)

**Five attempts (A–E), all froze on hardware at boot.** Fully documented in
`S089_P0_SRR_MOVE_DISUSED.md` and `S089_P0_IMPLEMENTATION_DISUSED.md`.

The plan was to move `decimation_rate`, `decimation_cnt`, and
`decimation_held` from the mixer's DTCM arrays into each engine struct, run
the S&H loop inside each engine's `calcVoiceSyncBlock()`, and rewire the
`INSTRUMENT_BIND_SLOT_DECIMATION` binding.

Root causes of the freezes (§1.2 above): stale image from broken Makefile
path (F1), `mixer_init()` ordering overwrite (F2), LTO inliner budget
re-spending (F3), and SRAM1 `.bss` layout shifts (F4). The I-cache cliff
explanation from those attempts is **superseded** by `S089_P0_NEW_SRR_DISABLE.md` §1.

### 8.2 Integer S&H (P0 follow-up)

Built and measured: Q31 packed halfword pair, 10→7 instructions. Reverted by
user decision. Kept as a documented option for future CPU savings if needed.

---

## 9. Pre-existing issues (not changed by S089)

- Cymbal/HiHat `pitchMod` keeps the last snap value when `wav` leaves `Snp`
  mid-snap (T+D only). Under WFd the guard prevents it; under T+D it remains
  latent.
- Triangle fold aliasing is accepted as character (no oversampling).
- The `srt` cell still writes `mixer_decimation_rate[slot]` under a chain
  without SRR, but nothing reads it. The value is preserved so switching back
  to the default chain restores it.

---

## 10. Build metrics progression

| Metric | S088 close | P0 | P2 | P3 |
|---|---:|---:|---:|---:|
| `.text` | 676,264 | 676,600 | ~680K | 683,512 |
| `.data` | 420 | 420 | 424 | 424 |
| `.bss` | 412,884 | 412,884 | 412,884 | 412,884 |
| ITCM | 12,656 | 12,656 | 13,032 | 15,240 |
| ITCM free | 3,728 | 3,728 | 3,352 | 1,144 |
| Flash headroom | 77,400 | 76,644 | 72,008 | 69,728 |
