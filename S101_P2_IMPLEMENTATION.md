# S101 P2 — Implementation log: package P (with A2q in place of B3)

**Date:** 2026-11-22. **Branch:** `dev-ph7-alkaline`, base `6a525038`
("FS_OS 1 build and test, pre cpu-reductions implement pass").
**Plan:** `S101_P2_REDUCTIONS.md` revision 4, §A.7 package P.

**Your instruction (2026-11-22):**
- "512B RAM for Delay restructure approved."
- "Build for option P as recommended *HOWEVER*, *DON'T* do B3, do A2q
  instead (the envelope control-rate change for Tetrakt)."
- "include every change made to the code in a detailed implementation log
  … Make sure that all code changes are accompanied by a detailed
  comment-block describing what the change does and why it must exist."

**Package P as built (the recommended order of §A.9):**

| Step | Item | What |
|---|---|---|
| 1 | Q-C9 | the S1 maxima behind `MIXER_PROBE_MAX` (off) |
| 2 | A1, A3, A5, E1, E2 | Tier A and the Effect plumbing |
| 3 | E5 | the Delay loop in two passes (512 B stack, approved) |
| 4 | CL1 (thirds), CL2, CL3, CL4, CL5; CH1 | the Colloid and the Advanced Colloid chorus |
| 5 | **A2q** (not A2, not B3), B1 (sub-block) | the Tetrakt |

**The baseline** (the `6a525038` tree, built before any change):
- **Flash:** 666,700 B. **ITCM:** 15,424 / 16,384 B. **DTCM statics:**
  4,396 B.
- **Gate snapshots:** `elf_p4_snapshot`, `elf_s099_snapshot` of the baseline
  ELF, kept in the session scratchpad (`p2impl/p4base`, `p2impl/s99base`)
  and used as the base for every step's `elf_p4_check` / `elf_s099_check`.
- **Cost tools** (instructions a sample or a frame):
  - `colloid_cpu.py`: `pwm6-par` 406.8, `pwmw6-par` 410.0, `scan6-par`
    410.4, `clx-pwm6-par` 453.8;
  - `tetrakt_cpu.py`: 383.5;
  - `space_cpu.py`: Delay 234.5 (254.5 with the feed), Reverb 189.5,
    Echo 205.5;
  - `crumpbit_cpu.py`: `grn` 182.9.
- **Host suite** (`make -C tools/dsp_test -k all`): every group passes
  except the two failures already present before S101 P2: `voice_control`
  14 (laws 1, 4, 7, 8) and `alkaline` AL-OD0 (the golden file).

---

## 1. Q-C9: the S1 maxima behind `MIXER_PROBE_MAX`

**Why:** the S1 maxima cost about 1.5k cycles a block inside the mixer
(`SD_CARD_S101_P2`: the send-add section 10.7k → 11.9k with no change to its
code; `S101_P2_REDUCTIONS.md` §A.1b item 7). That is diagnostic time inside
the render, and it made the logging build read about 1 % above a release
build.

**`config.h`:** new `#define MIXER_PROBE_MAX 0`, after `DEV_MODE_LOGGING`,
with a comment block (what it compiles, why it is off, the records it
removes, the RAM).

**`Core/DSPAudio/mixer.c`:**
- `mixer_probe_async_max[]`, `mixer_probe_sync_max[]`,
  `mixer_probe_sec_max[]` (76 B), `mixer_probeMaxWord()` and
  `mixer_probeFold()` are compiled only under `#if MIXER_PROBE_MAX`.
- `mixer_probeAdd()` keeps the accumulator add. The max-word lookup and the
  saturating add move into an `#if MIXER_PROBE_MAX` block.
- `mixer_probeEnd()`: the three folds, and the fifteen records of codes
  6 | (9..15) << 3 and 6 | (16..22) << 3, are under the flag.
- **Unchanged and always on with `DEV_MODE_LOGGING`:** the outside-mixer
  span (`mixer_probeOutside()`, codes 6 | 24 << 3 and 6 | 25 << 3).
- The probe's header comment records the change.

**Checks:**
- Built with the flag at 0 (the default): flash 665,884 B (−816 B), ITCM
  unchanged.
- Built once with the flag at 1, into the scratchpad: it compiles (666,756
  B).
- `elf_p4_check`: 84/84 loops exact, 56 hot loops equal.
- `elf_s099_check`: 16/16 exact.
- The cost tools: unchanged.

**Device:** the next device run should show no `6 | (9..22) << 3` records,
and the send-add section back near 10.7k.

---

## 2. Tier A and the Effect plumbing: A1, A3, A5, E1, E2

### 2.1 A1: the slot control on alternate blocks

**What it was:** every block, `mixer_calcNextSampleBlock()` called:
1. `modNode_resetTargets()` (the interpolation-slot accounting, then a
   restore of every velocity and LFO target to its base);
2. `modNode_reassignVeloMod()` (the six velocity modulations);
3. `instrumentManager_dispatchRuntimeLfos()` (six LFOs: phase step, value,
   both nodes, both adapter pairs).

That measured 7.0k cycles a block.

**What the code does now:**

| File | Change |
|---|---|
| `Core/DSPAudio/modulationNode.c` | `modNode_blockBegin()` (the slot accounting, split out unchanged) and `modNode_restoreAll()` (the restores). `modNode_resetTargets()` = both, and sets `MODNODE_CTL_FORCE` (its foreground callers leave every target at base). New `modNode_controlBlock()`, the mixer's call. New `modNode_ctlState` (1 B: velocity dirty bits 0..5, FORCE, OWED) with its comment block. |
| | The base-refresh helpers `modNode_setOriginalValueChanged()` and `modNode_setDirectOriginalValueChanged()` return whether the node matched. Matching velocity nodes set their dirty bit. A matching legacy/direct LFO node sets FORCE (the node visitor does not pass the source slot). `modNode_clearDestination()` sets FORCE. |
| `Core/DSPAudio/modulationNode.h` | the `modNode_controlBlock()` declaration and contract. |
| `Core/DSP/Instruments/InstrumentManager.c/.h` | `instrumentManager_dispatchRuntimeLfos(uint8_t steps)` (calls `lfo_stepBlock()` once more when 2 blocks are owed, then clears the dirty mask). New `instrumentManager_reapplyDirtyLfos()`. New `instrumentManager_lfoDirty` (1 B), set in `instrumentManager_noteRuntimeValueChanged()` when an adapter's base is refreshed. |
| `Core/DSPAudio/lfo.c/.h` | new `lfo_stepBlock()`: `lfo_calc()`'s step law line for line; a noise LFO that wraps draws its next value. New `lfo_reapplyValue()`: re-applies `modTarget.lastVal` to both nodes and both adapters, with no phase step. |
| `Core/DSPAudio/mixer.c` | the three calls become `modNode_controlBlock()`, with a comment block. |

**The behaviour:**
- **Every block:** the interpolation-slot accounting. The renders claim
  slots every block, so it must stay.
- **Every other block (OWED), or the next block after FORCE:** the full
  control as before. All six slots run in the same block. The LFOs step the
  blocks they owe (2, or 1 after a forced block), so every rate is exact.
- **The block between:** only the velocity nodes and LFO sources whose
  target an ordinary write refreshed. They are re-applied at their last
  value, in slot order.

**Why it is built this way, not even/odd slots** (the plan's first form):
- **Cross-slot LFO targets exist.** Each node writes an absolute value from
  its own base, so two modulators on one target end on the last writer. On
  even/odd slots that writer would alternate every block: a 689 Hz flip
  between two modulations. With all slots in one block, the order is
  today's.
- **An ordinary write puts the unmodulated base into the target** (a menu
  edit, an automation ramp via `seq_rampService()`, a Morph apply, a Kit
  load). Today the next control re-applies the overlay before the render.
  Without the dirty bits, an automated or morphed parameter under an LFO
  would render unmodulated on every block between: a 689 Hz flip of the
  modulation depth. The dirty bits make that block re-apply exactly those
  modulators.
- **Every writer is in the main loop,** like the mixer itself
  (`voiceControl_processPending()`, `seq_rampService()`, the menu, MIDI),
  so the one-byte state needs no interrupt guard.

**Sound:**
- An LFO or velocity target updates every 1.45 ms instead of every 0.73 ms:
  a fast LFO on pitch steps at 689 Hz.
- Targets a voice ramps itself (filter cutoff, amp) are unaffected.
- A changed LFO rate, retrigger or destination takes effect up to one block
  (0.73 ms) later.
- **Listen:** LFO `frq` 127 on pitch, square and triangle.

**RAM:** 2 B of new statics (`modNode_ctlState`, `instrumentManager_lfoDirty`).
**Approved by you, 2026-11-22** ("RAM approved").

**Host laws:**
- **A1-CTL 1–4** (`tools/dsp_test/modules/modulation.c`):
  1. from a forced start the full control runs on every other block, and
     the LFO steps add up to the blocks;
  2. a refreshed velocity node, and only it, re-applies in the next block;
  3. a foreground restore forces the next block with the steps it owes,
     and the alternation then resumes;
  4. the dirty LFOs are re-applied on every block between.

  Breaks: the gap block ignoring the dirty bits fails A1-CTL 2; full runs
  stepping 1 fail A1-CTL 1. Both are caught.
- **LF6** (`modules/lfo.c`): `lfo_stepBlock()` + `lfo_calc()` equal two
  `lfo_calc()` calls (free and synced rates, overlays 0 / 0.5 / 1 / 2,
  1,000 runs), and a stepped noise wrap draws. Break: a `lfo_stepBlock()`
  that does not step fails LF6. Caught.

### 2.2 A3: `effects_service()` resolves half its rows per block

**`Core/DSP/Effects/EffectsManager.c`:**
- **New statics** (2 B: **for your sign-off**), with a comment block:
  - `effects_svcPhase` (toggles every block);
  - `effects_svcLastStep` (the last block's active sequence step).
- **In the row loop,** a row whose index parity is not this block's is
  skipped before its resolution. A BUFFER_DEPENDENT row is never skipped.
- **Every row resolves** when `force_all` is set (a type change, a Scene
  switch) or the active step changed. A new FX lock therefore lands in its
  own block.
- **The rest of the function is unchanged:** the latch consume, the step,
  the Morph, LFO and share resolution, and the drain.

**Sound:** a row driven by the menu, Morph, an overlay or an LFO reaches the
type at most one block (0.73 ms) later. The Space types glide their rows
over 150 ms anyway.

**Proof:** there is no host bench of `EffectsManager.c` (none existed before),
so the check is the device run. The resolvers it skips (`effects_interpolate`,
`effects_seqOverride`, `effects_lfoResolve`) are pure functions, so a
skipped row's value is simply resolved one block later.

### 2.3 A5: the dry ramps' per-block constants (bit-exact)

**`Core/DSPAudio/mixer.c`:** a comment block `MIXER_A5_RAMP` before
`mixer_addVoiceInt16ToOutput()`. In the four voice-add functions (mono,
mono + FX, stereo, stereo + FX):
- **The ramp:** `lastGain + i × inv_size × gain_delta`, then × 256, became
  `lastGain256 + fi × inv_size × gain_delta256`.
  - `lastGain256 = lastGain × 256` and `gain_delta256 = gain_delta × 256`
    are per block.
  - `fi` is a float count stepped by the loop
    (`for (i = 0, fi = 0.0f; …; i++, fi += 1.0f)`).
- **Why it is exact:** `fi` holds exactly `(float)i`, and × 256 (a power of
  two) commutes with every IEEE rounding. Every dry sample is bit-identical
  to before (S0).
- **What it saves:** the per-sample `uxtb`/`vmov`/`vcvt` of the index, and
  the × 256.

**Host law MX-A5** (`modules/colloid.c`): the mono, mono + FX and stereo dry
outputs, every routing, pans 0 / 63 / 127 and a ramping gain, are
bit-identical to the old law, restated in the test. Break: × 255 in the
per-block constant fails MX-A5. Caught.

### 2.4 E2: the float FX bus

**`Core/DSPAudio/mixer.c`:**
- A comment block, `MIXER_E2_FLOAT_BUS`.
- **The send halves** (the macros in both + FX functions) now add
  `data × send × pan` straight into `mixer_fx_bus.f[][]`. The send ramp is
  pre-scaled per block by 1 / 32,767 (= 256 / 8,388,352), with the same
  `fi` count.
- **Removed:** the per-sample float→int conversions, the saturating adds
  into `mx[][]`, and the int→float conversion loop before
  `effects_process()`.
- **Kept:** the bus clear (all bits 0 = +0.0f) and the mono-in / stereo-out
  zeroing.
- The union's comment explains that `mx` now remains only for the clear.

**Sound (S1):**
- The bus differs from the old int bus by each voice's int truncation
  (< 1 / 8,388,352 of full scale).
- It does not saturate: an overloaded send sum reaches the Effect above 1.0,
  and each type's input clip decides. You accepted this (revision 4,
  Q-E1).

**Host law MX-E2:** the float bus equals the old int bus ÷ 8,388,352 within
2 / 8,388,352, every routing and pan, stereo- and mono-input send. Break:
a 1 / 32,768 send scale fails MX-E2. Caught.

### 2.5 E1: the return ramps' per-block constants

**`Core/DSPAudio/mixer.c`, `mixer_addFxReturnToOutput()`:**
- Both ramps carry the 8,388,352 scale (`lastKL/R`, `deltaKL/R`), with the
  `fi` count.
- `mixer_floatToMx()` is replaced by `mixer_floatToMxScaled()`: the same
  ±255 guard in the scaled domain (±2,139,029,760, exact in float and inside
  int32). The comment block explains why.

**Sound (S1):** 8,388,352 is not a power of two, so `x × (g × K)` can differ
from `(x × g) × K` by one float rounding: at most one int16 LSB of the
return.

**Host law MX-E1:** within 256 (one int16 LSB) of the old law on every
routing, stereo and mono returns, ramping gains, including values past the
guard. Break: a 2^23 ramp scale fails MX-E1. Caught.

### 2.6 Step 2 checks

- **Build:** flash 667,244 B (+1,360 B on step 1: A1's re-apply paths).
  ITCM 15,424 B (unchanged).
- **`elf_p4_check`:** 84 / 84 oscillator loops exact.
- **`elf_s099_check`:** 16 / 16 exact.
- **The mixer's hot loops** (`elf_hotloops.py`, against the baseline):
  - 28 equal and 23 shrank. Per add loop: 26 → 22, 34 → 27, 33 → 22,
    40 → 27, 18 → 16; the return 32 → 29, 23 → 20; and the send loops
    replaced by the float form (stereo send 15).
  - The sum over the hot loops is 1,304 → 1,162 instructions.
  - "5 grew", read loop by loop:
    - **two are mis-pairings.** The deleted conversion loop (10) pairs with
      the new 15-instruction float send loop. A 9-instruction hold path
      pairs with a different 19-instruction path; that path is unchanged at
      19 in both images.
    - **three are real: +1 instruction (a `mov r0, r2`)** in the hot path of
      the Snare/HiHat/Cymbal noise oscillator, `calcNoiseBlock()`, in each of
      its three copies LTO inlines into the mixer. Its gain-1 hold path also
      went 9 → 11 and now closes with an unconditional branch.
    - This is LTO's register allocation across `mixer_calcNextSampleBlock()`;
      the noise loop's source is untouched. It costs about 32–64 cycles a
      block per noise voice.
  - **Tried and reverted:** `mixer_addFxReturnToOutput()` out of line (no
    effect); `mixer_addVoiceInt16ToOutputAndFx()` out of line (no effect);
    `calcNoiseBlock()` out of line (worse: its `INITCM` placement put
    624 B into ITCM, and four oscillator loops moved out of the gated set).
  - **Accepted as the measured cost of the step;** it is recorded here so
    the device figures can be read against it.
- **Host:** `modulation`, `lfo` and `colloid` pass with their new laws and
  breaks.

---

## 3. E5: the Delay loop in two passes (bit-exact; 512 B of stack, approved)

**`Core/DSP/Effects/Space/SpaceEffect.c`:**
- **New `SPACE_E5_FRAMES` (32)**, with
  `_Static_assert(SK_FRAMES_MAX <= SPACE_E5_FRAMES)`.
- **`space_delayRun()` is rewritten as two passes,** with a comment block
  inside the function (what, why, the RAM) and a line in its header comment.
  - **Pass 1:** the heads (or `rev`'s windows), `str`'s taps, the output
    (stored over the input at once), the input peak, the loop's mid and side,
    and the `xfb` allpass. It keeps four 32-float stack arrays: the two loop
    inputs and the two record inputs (the raw input, which pass 1 has already
    overwritten in `l`/`r`).
  - **Pass 2:** the two `space_loop()` chains with the `hld` ramps of `fbk`
    and `ing`, then the record.
  - **Each pass steps its own copy of the write position** with the same two
    operations.

**Why the split is exact:** every read lands at least `SPACE_D_MIN` (36)
frames behind the block start. All the reads may therefore come before all
the writes, and every value is the same expression in the same order.

**Two forms tried and rejected (measured):**
1. **The plan's split** (pass 1 the heads and the output, pass 2 the whole of
   step 4): pass 2 still held about 38 floats and spilled. 115 + 92 = 207, so
   only −4 a frame.
2. **Precomputing `in × ing` in pass 1:**
   - It measured 130 + 72.
   - But it was not bit-exact: SP-E5 found 157 of 320 blocks different.
     `in × ing + fbk × y` inside `space_loop()` lets the compiler fuse that
     product into the multiply-add; rounding it first changes the last bit.
   - The record input therefore stays raw, and `ing` ramps in pass 2.
3. **One chunk without the chunk loop:** 128 + 77 a frame and 249 a block
   (LTO allocated it worse). The chunk loop, which turns once per block, is
   kept, with a comment saying why.

**Cost** (`space_cpu.py`, which now measures the two passes; see below):

| | Frame loops | Per block | Delay a frame | With the feed |
|---|---:|---:|---:|---:|
| Baseline | 211 (`rev` 206) | 689 | 234.5 | 254.5 |
| E5 | pass 1 127 (normal heads 126) + pass 2 74 = 201 | 720 (the chunk loop's body, 111, is per block) | **225.5** | 245.4 |

- **The saving:** −9.0 instructions a frame, about **−0.42k cycles a block**
  at 1.47 cycles an instruction. That is half of §A.6b's −0.7k estimate: the
  duplicated position step and the array traffic take back part of the 28
  spill instructions.
- **Stack spills** in the frame loops: 28 → 3 (pass 1) + 1 (pass 2).

**`tools/dsp_test/space_cpu.py`:** new `delay_passes()`. It measures each of
`space_delayRun()`'s loops within its own range: the chunk loop, pass 1
(normal and `rev` heads) and pass 2. The Delay's frame cost is
max(pass 1) + pass 2, and the chunk loop's own body goes into the per-block
sum. The baseline ELF is refused (it has one loop), by design.

**Host law SP-E5** (`tools/dsp_test/modules/space.c`):
- **The reference:** the one-pass `space_delayRun()` of the base commit
  `6a525038`, extracted by the `space` target (`git show` →
  `build/gen/sp_delay_s100.c`, renamed `space_delayRunS100`; generated, no
  file added to the tree).
- **The law:** both run on copies of a random tape, input and states for
  8 cases × 40 consecutive blocks: `rev` off and on, a slowed tape
  (`va` 0.37), the `hld` ramps on and off, `deg`'s bit reduction, every
  `str` gain. **Result: 0 of 320 blocks differ.** Outputs, tape, both
  channel states, `xs` and the peak are all bit-identical.
- **Break:** pass 1 recording R's input for L fails SP-E5. Caught.

**RAM:** 512 B of stack inside the Effect process call (4 × 32 floats), as
you approved. It is not a static.

---

## 4. The Colloid and the Advanced Colloid chorus

**Built:** CL3 and CL5. **Tried and reverted:** CL1. **Not built:** CL2, CL4,
CH1 (each with the reason below).

### 4.1 CL5: both filters' controls every other block (built)

**`Core/DSP/Instruments/Colloid/ColloidVoice.c`:**
- **New `col_svfControl2()`** (with a comment block): `col_svfControl()` for
  a ramp across two blocks. The cutoff and damping slew by `COL_SLEW2` (two
  blocks of `COL_SLEW`: the same time constant), and `da = (t − a) / 64`.
- **New `col_svfCarry()`:** the block between moves the ramp's start on by
  one block (`a += 32 da`). The render loops never write the coefficients
  back, so the ramp reaches `t` exactly at the end of the second block.
- **In `Colloid_calcVoiceAsync()`,** both filters run `col_svfControl2()`
  when `nPhase` is 1 (the parity the noise corners already use), and both
  carry otherwise.
- `col_svfInit()` still uses the one-block `col_svfControl()`.

**Why both filters in one block:** the first build alternated them (A on one
block, B on the next). Law **CO-RTE** failed: with zero offsets the left and
right filters must be identical bit for bit, and alternating left one a
block behind the other during every sweep. With both in the same block
CO-RTE holds.

**Sound:** the filter envelope and the knobs reach the filters at most one
block (0.73 ms) later. The coefficients still ramp per sample (per sub-block
in the stereo loop). A filter-type change lands on the next control block.

**RAM:** none (it reuses `nPhase`).

**Law CO-SVF, restated:**
- On a control block the ramp starts on the last target (as before).
- On a carry block it starts where the last block ended
  (`a + 32 da`), and `t` is unchanged.
- The `snap` break (a ramp that does not start on its target) still fails
  it.

### 4.2 CL3: the noise at half rate (built)

**`col_renderPar()` and `col_renderSer()`:**
- The noise (one RNG read, the one-pole HP and LP) runs on even samples
  only, behind `if ((n & 1u) == 0u)`, and its output (`nOut`) is held for
  the odd ones.
- The per-sample filter and output code moved, unchanged, into the macros
  `COL_PAR_SAMPLE()` / `COL_SER_SAMPLE()`.
- A comment block in `col_renderPar()` says what, why, the sound and the
  measured cost.

**First form, rejected:** a loop taking two samples per iteration (one noise
per pair). It spilled 54 values against 32 and cost +4.5 a sample.

**Measured, as built:**
- the stereo loop: 126 (even) / 110 (odd) against 121, a mean of 118 (−3 a
  sample);
- the mono loop: 103 / 88 against 99 (−3.5).

**Sound:** S2, accepted in principle in revision 3. The noise is a
22.05 kHz source held to 44.1 kHz, so its band ends at 11 kHz (−3.9 dB
there). At `nlv` 0 the output is unchanged to the bit (CO-NOISE).

**New law CO-NHALF:**
- one stereo and one mono sync block each read the RNG exactly 16 times;
- the held noise reaches the output.

Break: a read every sample fails CO-NHALF. Caught.

**`tools/dsp_test/colloid_cpu.py`:** the render loop's two alternating paths
are charged their mean (`alternating_pair()`). The tool refuses a loop that
does not have exactly those two paths.

### 4.3 CL1: staggered oscillator control (built, measured, reverted)

**Built as:**
- every oscillator, every block: the pitch slew (with `2^pitch` updated by
  a per-voice `2^(±step)`), the LFO / velocity / pitch-envelope multiplier,
  `inc`, the band-limited row and the width slew;
- one pair in three per block: only the `alk_exp2f()` resync and the stereo
  position;
- a landing, a jump or a rejoin: every slot.

It needed a 10-float cache, which filled the Colloid voice to exactly the
1,264 B slot reserve.

**Measured on the linked image (the pwm Saw path):**
- a slot oscillator 209 instructions, a between oscillator 196, against
  193 before;
- `col_oscControl()`'s per-block path 352 → 431.
- **So CL1 cost more than it saved.** The forced-path scratch builds (the
  slot test made constant: 194 / 142) showed the potential, but with both
  branches in one loop the compiler keeps the register pressure of both.

**Reverted completely** (the voice is back to 1,220 B). It would need a
two-loop form (a light loop for every oscillator, then a separate loop over
the slot pair). That is a larger rewrite, left for a later step.

### 4.4 Not built

- **CL2 (the saturator without its VDIV).** No polynomial meets S1 on
  |z| ≤ 3:

  | Degree | Maximum error | Monotone | Slope at 0 |
  |---|---:|---|---:|
  | 7 (constrained to 1.0 at 3) | 1.3 × 10⁻² | no | 0.952 |
  | 9 (constrained to 1.0 at 3) | 4.4 × 10⁻³ | no | 0.981 |

  The rational is 1.0 at 0. The division stays.
- **CL4 (a trim of `col_oscControl()`).** CL1's measurement showed the loop
  is register-bound: a trim inside it gains little without the two-loop
  form. Folded into the CL1 follow-up.
- **CH1 (quarter-rate chorus rings).** At a quarter rate the shortest delay
  (65 output samples) is 16.25 ring samples, under the 17 that the ring
  guard and the mid form's pass B need (`BbdChorus.c`, the half-rate
  loops' rule). It needs a guard redesign or a longer minimum delay, which
  is a sound change beyond the plan's.

### 4.5 Colloid cost, as built (`colloid_cpu.py`, instructions a sample)

| Mode | Baseline | As built | Change |
|---|---:|---:|---:|
| `plain12-par` | 422.3 | 413.4 | −8.9 |
| `pwm6-par` | 406.8 | 397.9 | −8.9 |
| `pwmw6-par` | 410.0 | 401.1 | −8.9 |
| `scan6-par` | 410.4 | 401.5 | −8.9 |
| `clx-pwm6-par` | 453.8 | 444.8 | −9.0 |

- **Per Colloid-engine voice:** about −0.43k cycles a block (−9 × 32 ×
  1.51), CL5 −5.7 and CL3 −3.2 of it.
- **`elf_s099_check`:** `col_renderPar` and `col_renderSer` are reported
  missing from the S099 baseline. That is the intended change (CL3); the
  other 14 engine loops are exact.

---

## 5. The Tetrakt: A2q (in place of B3) and B1 (sub-block form)

### 5.1 A2q: one layer's control a block

**`Core/DSP/Instruments/Tetrakt/TetraktVoice.c/.h`:**
- **New `TKT_CTL_BLOCKS` (4).**
- **New field `TktLayer.envMul`** (the decay's per-block factor 1 − k; 0 after
  a FADE or when idle) and **`TetraktVoice.ctlPhase`** (the schedule), each
  with a comment block. The voice grows 1,064 → 1,080 B, inside the
  1,264 B slot reserve: **no RAM change**.
- **`Tetrakt_calcVoiceAsync()`:** layer i runs `tkt_layerControl()` when
  `(ctlPhase + i) % 4 == 0`, or at once for a trigger (START, FADE) or a
  rebind. A forced run does not move the schedule: a hit starts all four
  layers together, and a restarted countdown would line them all up on one
  block. The other blocks run the new `tkt_layerCarry()`.
- **`tkt_layerCarry()`** (comment block):
  - the exact per-block exponential `env ×= envMul` (idle at
    `TKT_QUIET`, as the control does);
  - the tone ramp's start moved on (`a += 32 da`).
- **`tkt_ramp()`:** the ramp spans `TKT_CTL_BLOCKS` blocks.
- **`tkt_layerControl()`:** sets `envMul` in each stage.
- **`Tetrakt_initVoice()`:** zeroes `envMul`.

**Sound:**
- **The decay envelope is unchanged:** the same exponential, step for step.
  That is better than the plan's "straight segments of 2.9 ms".
- **A row change** (tune, level, pan, drive, SRR, space, tone, decay; from
  the menu, an LFO or automation) reaches a layer at its next control, at
  most 2.2 ms later. It steps every 2.9 ms instead of 0.73 ms, so a fast LFO
  on level or pan steps four times coarser.
- **The tone envelope's coefficients** ramp across 2.9 ms.

**Law TK-A2Q (new):**
1. After a hit, every layer's decay follows `env (1 − k)` per block within
   2 ulp, on control and carried blocks alike, down to idle.
2. A level change reaches each layer on its own scheduled block, and the
   four layers do so on four different blocks.
3. The tone ramp is continuous from block to block.

Break: a carry that holds the envelope fails TK-A2Q. Caught.

### 5.2 B1, the sub-block form: tone coefficients every 8 samples

**`tkt_layerPost()`:**
- The loop runs four sub-blocks of `TKT_SUB_BLOCK` (8) samples.
- The coefficients are held at each sub-block's centre value
  (`a + da × (8 j + 4)`) and step by `8 da` after each sub-block.
- The per-sample `a += da` is gone. A comment block explains it.

**Measured:** the layer loop 57 → 53 (store), 60 → 56 (add), plus the outer
loop's 15 once per 8 samples: **−2.1 a sample per layer**.

**Law TK-TONE, restated:**
- It now runs the firmware's real ramp: four blocks, sub-block steps, the
  start carried. Before S101 P2 it ran a one-block per-sample ramp, which
  the firmware no longer has.
- **The bound is the input's step + 0.04 of full scale** (was 0.02). On the
  extreme LP → HP sweep the sub-block steps measure +0.029.
- New break: coefficients that jump to the ramp's end (no ramp) fail
  TK-TONE. Caught. A first break (a sub-block step 4× too large) was not
  caught by the 0.04 bound and was replaced.

### 5.3 Tetrakt cost (`tetrakt_cpu.py`, instructions a sample)

| | Baseline | As built |
|---|---:|---:|
| A block where every layer STARTs (4 controls, 4 binds) | 383.5 | 379.1 |
| **Steady (no START)** | 366.9 (383.5 less the binds' 532 / 32) | **329.9** (one control a block) |

- **The steady saving:** −37.0 a sample, about **−1.8k cycles per Tetrakt**
  (A2q about −1.5k, B1 about −0.4k).
- **`tetrakt_cpu.py`** now prints the steady line, and charges the post loops
  `inner + (outer − inner) / 8` (`post_per_sample()`).

---

## 6. Final checks (the as-built tree)

**Build:**

| | Baseline | As built |
|---|---:|---:|
| Flash | 666,700 B | 668,508 B (+1,808 B) |
| ITCM | 15,424 B | 15,424 B (unchanged) |
| DTCM statics | 4,396 B | 4,392 B |
| FXBUF | 126,656 B | 126,656 B |

**Gates against the baseline snapshots:**
- **`elf_p4_check`:** 84 / 84 oscillator loops exact.
- **The mixer's hot loops** (`elf_hotloops.py`): 28 equal, 23 shrank,
  "5 grew". Read loop by loop: two mis-pairings; +1 `mov` in the three
  inlined copies of the noise oscillator's hold path; the gain-1 hold path
  9 → 11. See §2.6.
- **`elf_s099_check`:** 14 / 16 exact. `col_renderPar` and `col_renderSer`
  changed shape on purpose (CL3).

**Cost tools** (instructions a sample or a frame):

| Tool | Baseline | As built |
|---|---:|---:|
| `space_cpu.py`, Delay | 234.5 | 225.5 |
| `space_cpu.py`, Reverb, Echo | | unchanged |
| `colloid_cpu.py`, `pwm6-par` | 406.8 | 397.9 (see §4.5) |
| `tetrakt_cpu.py`, steady | 366.9 | 329.9 |
| `crumpbit_cpu.py`, `grn` | 182.9 | 182.9 (unchanged; its 170 cap fails in the baseline too) |

**Host suite** (`make -C tools/dsp_test -k all`):
- **Failures:** only the two already there before S101 P2: `voice_control`
  14 and `alkaline` AL-OD0.
- **Breaks:** 99 caught (89 in the baseline, plus the 10 new: `a1ctl1`,
  `a1ctl2`, `lfo3`, `col_a5`, `col_e2`, `col_e1`, `sp_e5`, `col_nhalf`,
  `tkt_a2q`, `tkt_sub`).
- **One unexplained failure.** The first full run after the Tetrakt step
  showed **CO-PWG** failing twice. It did not reproduce: the Colloid bench
  alone three times, a second full suite, and the bench under
  AddressSanitizer + UndefinedBehaviorSanitizer all pass. The sanitizers
  found nothing in S101 P2's code; the one report is an older left shift of
  a negative value in `ColloidOsc.c`. Recorded so it is watched.

**RAM:**

| Item | RAM | Status |
|---|---:|---|
| E5's stack arrays | 512 B (stack) | approved |
| A1 (`modNode_ctlState`, `instrumentManager_lfoDirty`) | 2 B of statics | **for your sign-off** |
| A3 (`effects_svcPhase`, `effects_svcLastStep`) | 2 B of statics | **for your sign-off** |
| The Tetrakt voice (1,080 B) | none | inside the slot reserve |
| The Colloid voice (1,220 B) | none | unchanged after the CL1 revert |
| Q-C9: the S1 maxima off | −76 B (logging builds) | |

**The image:** `build/LXRV2_lxr02.img` (668,508 B) is the as-built tree.

**Device check:**
- Bank 076 Scene 5, with the probe (now without the S1 maxima).
- Expected: the mixer about 136–138k a block (§7.3), the widget about
  89–91 %.
- Copy the card after the run.
- Listen to:
  - A1: a fast LFO on pitch;
  - A2q: a fast LFO on a Tetrakt layer's level or pan;
  - B1: a Tetrakt tone-envelope sweep;
  - CL3: Colloid noise, `nlv` high.

---

## 7. Assessment

Thousands of cycles a block. These are estimates from the measured costs of
`S101_P2_REDUCTIONS.md` §A.2–§A.6b, less each built item's linked-image
saving, at the mixed-Scene cycles per instruction (1.47–1.52). Cap **144k**.

### 7.1 Each item's saving, as built

| Item | Saving | Basis |
|---|---:|---|
| Q-C9 (S1 maxima off) | −1.5 (logging builds only) | the device delta of §A.1b |
| A1 (slot control) | ≈ −3.0 | the plan's half of the 7.0k per-slot share (inlined: to confirm on the device) |
| A3 (`effects_service()`) | Delay ≈ −1.5; CrumpBit ≈ −2.0 | half the per-row part |
| A5 + E2's send halves | ≈ −2.2 with every send active (≈ −1.0 with none) | the add loops: mono 26 → 22, mono + send 34 → 27, stereo 31 → 28, stereo + send 47 → 39 a sample |
| E2's conversion loop | −0.5 | 10 a frame |
| E1 | −0.15 | the return 32 → 29 a frame |
| E5 | −0.42 | the Delay 234.5 → 225.5 a frame |
| CL3 + CL5 | −0.43 per Colloid-engine voice | −8.9 a sample |
| A2q + B1 | −1.8 per Tetrakt | the steady 366.9 → 329.9 a sample |
| B3 (not built; the option) | ≈ −0.6 per Tetrakt | `S101_P2_REDUCTIONS.md` §A.4, corrected |

### 7.2 The worst-case parts, as built

- **Common:** control 4.0 + set-up and SRR 3.5 + send adds 8.6 (with every
  send active) = 16.1; compressor 4.6. **Common 20.7.**
- **The Effect with its service** (the Delay during a feed): 18.1 − 1.5 −
  0.65 − 0.42 = **15.5.**
- **Fixed part: 36.2.**
- **The voices:**

  | Voice | Today | As built | With B3 |
  |---|---:|---:|---:|
  | Tetrakt | 20.4 | **18.6** | 18.0 |
  | Colloid | 19.4 | **19.0** | |
  | Advanced Colloid | 21.0 | **20.6** | |
  | Advanced Alkaline | ≈ 18.2 (unchanged) | ≈ 18.2 | |

### 7.3 The arithmetic for the 144k cap (§A.7), with the A2q/B3 swap and B3 as an option

| Scene | Revision 4 plan P (A2 + B1 + B3, CL1/2/4/5, CH1) | **As built (A2q + B1, no B3)** | As built + B3 |
|---|---:|---:|---:|
| **1:** 4 Tetrakts + 2 Advanced Colloids | 148.2 (−4.2) | **151.8 (−7.8)** | 149.4 (−5.4) |
| **3:** 3 Colloids + 1 Tetrakt + 2 Advanced Colloids | 146.1 (−2.1) | **153.0 (−9.0)** | 152.4 (−8.4) |
| **2:** 4 Colloids + 2 Advanced Colloids, mixed-Scene CPI | 145.4 (−1.4) | **153.4 (−9.4)** | 153.4 |
| 2, one-engine CPI (−7 % on the voices) | 137.8 (+6.2) | 145.2 (−1.2) | 145.2 |
| **Bank 076 Scene 5** (measured 148.3–148.8k) | ≈ 135.8 | **≈ 136.6 (87.2 %)** | ≈ 136.0 |

The arithmetic, as built:
- **Scene 1** = 4 × 18.6 + 2 × 20.6 + 36.2.
- **Scene 3** = 3 × 19.0 + 18.6 + 41.2 + 36.2.
- **Scene 2** = 4 × 19.0 + 41.2 + 36.2.
- **Scene 5** = 148.55 less the items above (−3.0, −1.5, −2.2, −0.5, −0.15,
  −0.42, −1.8, −0.43, −0.43, −1.5). The widget would read about 89–90 %.

**What the table says:**
1. **On the Tetrakt side the swap is close to even.** A2q (−1.5k) stands
   in for A2 + B3 (−1.05 − 0.6 = −1.65k), so A2q + B1 is −1.8k per Tetrakt
   against the plan's −2.05k: 0.25k less per Tetrakt, 1.0k in Scene 1. B3
   on top of A2q is worth another −0.6k per Tetrakt: −2.4k in Scene 1.
2. **The gap to the plan is the Colloid side.** CL1, CL2, CL4 and CH1 were
   to bring the Colloid to 17.65 and the Advanced Colloid to 18.85. Built,
   they are 19.0 and 20.6. That is +1.35 per Colloid and +1.75 per Advanced
   Colloid against the plan: +8.9k in Scene 2's voices. The common part
   came in 1.4k better and the Effect 0.5k worse, so Scene 2 is +8.0k on the
   plan (153.4 against 145.4).
3. **The binding case is now a Colloid Scene** (Scenes 2 and 3), at
   153.0–153.4 at the six-engine CPI. Scenes 2 and 3 run one and two
   engines, so their real CPI should be lower: Scene 2 at the measured
   one-engine CPI is 145.2. **The device run of the test Scenes (§A.9)
   decides how far over they really are.**
4. **Scene 5,** the real Scene, comes down from about 95 % to about 87 %.

### 7.4 Limiting the Tetrakt count, and the full-rate choruses

**With A2q + B1 the Tetrakt (18.6k) is no longer the dearest type of a Basic
slot; the Colloid (19.0k) is.** So a Tetrakt cap does not lower the worst
case:

| Rule | Its worst Scene | Total | Against the uncapped worst (Scene 2, 153.4) |
|---|---|---:|---:|
| At most 2 Tetrakts | still Scene 2 (no Tetrakt in it) | 153.4 | **0** |
| 4 Tetrakts with at most 1 Advanced | 4 Tetrakts + 1 Advanced Colloid + 1 Colloid | 150.2 | 0 (Scene 2 still allowed) |
| 6 Tetrakts with no Advanced | 6 Tetrakts | 147.8 | 0 (Scene 2 still allowed) |

- **The saving against the next-worst case is zero** in every form, because
  the next-worst case is a Colloid Scene.
- **A cap does not pay for putting A2q back either.** Without A2q the
  Tetrakt would be about 20.0k (B1 only). With at most 2 Tetrakts the worst
  Basic set would be 2 × 20.0 + 2 × 19.0 = 78.0, against 76.0 for 4
  Colloids: 2.0k worse than today's build.

**Full-rate choruses back:**
- **Advanced Colloid:** +0.49k each (the full-rate chorus measured 57.0 a
  sample in S099 P1, against 46.9 at half rate now). That is +1.0k in
  Scenes 1–3, which have two.
- **Advanced Alkaline:** +0.4 to +0.55k each (D1: −0.4k on the device,
  0.45–0.55k on the linked image).
- **Would a Tetrakt limit pay for them? No:** the limit frees nothing,
  because the binding Scenes are Colloid Scenes. With the full-rate choruses
  the as-built worst case rises from 153.4 to about 154.4 at the six-engine
  CPI.
- **What would pay for them** is a lever on the Colloid engine, which you
  have ruled out as a type cap. For example, a second Advanced slot that
  could hold an Advanced Alkaline but not a second Advanced Colloid would
  take Scene 2 down by 20.6 − 18.2 = 2.4k, more than both full-rate
  choruses together.
- **Without a cap,** the Colloid items are the way forward:
  - CL1 in its two-loop form (estimated −0.3 to −0.5k per voice);
  - CH1 with a guard redesign (≈ −0.3k per Advanced Colloid).

---

## 8. CL3 reverted (your decision, 2026-11-22: "Put back CL3")

**Why:** the saving was small for a real sound change. It measured
−3.2 a sample (≈ −0.15k per Colloid voice) against the estimated −0.3k, and
the noise band ended at 11 kHz.

**Code:** `col_renderPar()` and `col_renderSer()`, with their comment blocks,
restored from the base commit `6a525038`: the noise per sample, one RNG read
a sample, as before S101 P2. CL5 (the filter controls in the async) is
unchanged.

**Tools:** law CO-NHALF, its break `colloid_break_nhalf`, the RNG read counter
in `modules/colloid.c`, and `colloid_cpu.py`'s alternating-path charge are
removed. `colloid_cpu.py` is back to the base commit.

**Checks:**
- `elf_s099_check`: 16 / 16 engine loops exact (the render loops are the
  baseline's again).
- `make colloid`: every law passes.
- `colloid_cpu.py`: `pwm6-par` 400.8, `clx-pwm6-par` 447.7 (baseline 406.8 /
  453.8: CL5's −6.0 a sample, ≈ −0.29k per Colloid-engine voice).
- **Build:** flash 668,468 B. `build/LXRV2_lxr02.img` rebuilt.

**§7 with CL3 out:** the Colloid and the Advanced Colloid are 0.15k dearer
than in §7.2 (19.15k, 20.75k). Scenes 1–3 rise by 0.3–0.75k (two to five
Colloid-engine voices), and Scene 5 by 0.3k (≈ 136.9k).

---

## 9. Device test (`SD_CARD_S101_P2_2`, 2026-11-22)

**The card:** the image on it is this tree's `build/LXRV2_lxr02.img`, byte
for byte. The trace (`asavetrc.bin`, decoded with `decode_devlogs.py`)
carries two new boots after the `SD_CARD_S101_P2` records:

| Boot | Records | Image | How it is told apart |
|---|---|---|---|
| 5 | #27,358–#33,611 | the previous image (S1 on) | it writes the S1 maxima records (2,032 of them) |
| 6 | #33,612– | **S101 P2 as built** | no S1 records; the control section halves |

**Your report:** Scene 5 runs a few percent lower. The 4 Colloid + 2
Advanced Colloid test Scene ran lower than Scene 5; the 4 Tetrakt + 2
Advanced Colloid Scene ran higher, but never reached 100.

### 9.1 The Scenes (mixer cycles a block; medians of the 1.5 s windows while playing)

| Scene | Image | Average | Block maximum (median / highest) |
|---|---|---:|---:|
| **Bank 076 Scene 5** | previous (boot 5) | 146.9k (93.8 %) | 103.4 % / 106.3 % |
| Scene 5 | **as built**, first session | **140.4k (89.6 %)** | 101.3 % |
| Scene 5 | **as built**, after the test Scenes | **143.7k (91.7 %)** | 101.3 % / 104.1 % |
| **4 Colloids + 2 Advanced Colloids** | as built | **140.9k (89.9 %)**, 137.1k → 143.1k as the patches changed | 102.4 % / 106.4 % |
| 1 Tetrakt + 3 Colloids + 2 Advanced Colloids | as built | 143.4k (91.5 %) | 104.0 % |
| 2 Tetrakts + 2 Colloids + 2 Advanced Colloids | as built | 143.6k (91.6 %) | 103.8 % |
| 3 Tetrakts + 1 Colloid + 2 Advanced Colloids | as built | 144.3k (92.1 %) | 102.7 % |
| **4 Tetrakts + 2 Advanced Colloids** | as built | **143.3k (91.4 %)**, range 142.7–144.4k | 101.3 % / 104.3 % |

**Underruns:** one, at boot (the start-up artefact of §A.1b); none while
playing.

**The filesystem:** AutoSave's 1 ms drain slices are unchanged (median
1,022 µs, longest 1,447 µs).

**Against the estimates:**
- **Every measured Scene is at or under 144k.** The estimates were
  146–154k (§7, CL3 out). The real patches are cheaper than the worst modes
  the estimates assume, and the one- and two-engine Scenes run at the lower
  cycles per instruction that §7 expected.
- **These test Scenes are not the §A.9 worst-case patches.** That would be
  `pwm` / `wts` stereo, the chorus L-R, every send above 0, the compressor
  on. They are representative; they are not proven worst.

### 9.2 Each item on the device (Scene 5, previous image → as built)

The deterministic parts are compared: the control sections and each slot's
block-rate (control) half. A slot's render half moves ±1–2k between sessions
of the same image with the same code (the Tetrakt's render 14.6k in one
session, 16.5k in the next; the Advanced Colloid's 16.7k / 15.5k). That is
sample and flash traffic, so the renders are only read where the difference
is large.

| Item | Estimate | Device | Verdict |
|---|---:|---:|---|
| A1 (slot control) | −3.0k | **−3.0k** (control 6.76k → 3.78k) | **holds** |
| A2q (Tetrakt control) | −1.5k | **−1.2k** (the Tetrakt's control 2.90k → 1.70k) | **holds** |
| B1 (Tetrakt tone ramp) | −0.4k | −0.6k (its render, first session) | holds (render noise ±1k) |
| CL5 (Colloid filter control) | −0.29k a voice | **−0.47k** (Advanced Colloid), −0.2 to −0.3k (Colloid) | **holds** |
| E1 + E2 plumbing (bus conversion, return) | −0.65k | **−0.95k** (the stopped-transport Effect section 3.13k → 2.18k) | **holds** |
| Q-C9 (S1 off) | −1.5k | part of the totals | holds |
| A3 (`effects_service()`) | −1.5k | **−0.1k** (3.06k → 2.96k) | **did not pay.** The section's cost is in its fixed part, not the rows |
| A5 + E2's send halves (the send-add section) | −2.2k | **+0.6k** against the pre-S1 Scene 5 (10.69k → 11.31k) | **did not pay.** Fewer instructions, more cycles: the float bus's load-add-store chains run on the FPU alone where the int bus's saturating adds ran on the integer pipe |
| E5 (the Delay in two passes) | −0.42k | **≈ +1.5k**: the Delay's process (the playing Effect section less the plumbing) 10.9k → 12.5k | **a regression.** Bit-exact output, fewer instructions, but the split loops stall where the single loop overlapped its loads with the filter work |

**Net on Scene 5:** −3.2k to −6.5k (146.9k → 143.7k / 140.4k), a few points
on the widget, as you saw. The items that held add to about −8k; A3, the
send halves and E5 give back about +2k.

### 9.3 Recommended follow-ups (done: all three reverted, §10)

1. **Revert E5.** It is a device regression of about +1.5k. The output is
   bit-identical either way, so the sound does not change, and it gives back
   the 512 B of stack. Law SP-E5 and `space_cpu.py`'s two-pass spec go with
   it.
2. **Revert A3.** −0.1k for a one-block lag on the Effect rows and 2 B of
   RAM.
3. **The send halves** (A5's ramp on the send, E2's float accumulation):
   - **either** revert E2's send half to the int bus while keeping the
     conversion-free hand-off (that needs a measurement first: the
     conversion loop then has to come back);
   - **or** leave them: they cost about 0.6k but are bit-exact or S1.

   This is not worth more time unless a Scene needs the 0.6k.

---

## 10. Reverts after the device test (your decision, 2026-11-22)

**Your instruction:** "Revert 1 and 2 now. … also revert 3 and put the send
adds back. 0.6k is still meaningful and it sounds like the new method is
more likely to thrash or pressure the FPU."

### 10.1 E5 reverted (the Delay in two passes)

- **`Core/DSP/Effects/Space/SpaceEffect.c`** is the base commit's file again,
  byte for byte: the one-pass `space_delayRun()`, no `SPACE_E5_FRAMES`, no
  static assert. The 512 B of stack is gone.
- **`tools/dsp_test/space_cpu.py`** is the base's (its two-pass spec is
  gone).
- **`modules/space.c`** is the base's (law SP-E5 is gone), and the `space`
  target's reference extraction and the `space_break_e5` break are removed.
- **Why:** a device regression of about +1.5k (§9.2). The output was
  bit-identical either way, so the sound does not change.

### 10.2 A3 reverted (`effects_service()` half the rows)

- **`Core/DSP/Effects/EffectsManager.c`** is the base commit's file again:
  every row resolved every block. Its 2 B of statics are gone.
- **Why:** −0.1k on the device against −1.5k estimated, for a one-block lag
  on the Effect rows.

### 10.3 The send adds put back (A5 and E2 reverted; E1 kept)

**`Core/DSPAudio/mixer.c`:**
- **The four voice-add functions** are the base's again:
  `mixer_addVoiceInt16ToOutput()`, `mixer_addVoiceInt16ToOutputAndFx()`,
  `mixer_addVoiceStereoToOutput()` and `mixer_addVoiceStereoToOutputAndFx()`.
  That means the per-sample `lastGain + i x inv_size x gain_delta` ramps,
  `x 256` in the loop, and the send summed into the int32 bus with saturating
  adds. The `MIXER_A5_RAMP` / `MIXER_E2_FLOAT_BUS` comment blocks are gone.
- **The FX bus** is the base's again, with the int-to-float conversion loop
  before `effects_process()` (and its `inv_mx`) and the union's original
  comment.
- **Kept:**
  - **E1** (the return ramps' per-block constants and
    `mixer_floatToMxScaled()`). The return reads the Effect's float output
    and is float in both forms. E1 only removes floating-point work from
    that one loop (the per-sample index conversion and one multiply per
    channel), so it adds no FPU pressure. Its device figure is not
    separable: the −0.95k plumbing figure of §9.2 included E2's removed
    conversion loop.
  - **Q-C9** (the probe) and **A1** (the control call).

**Why:** the send-add section measured +0.6k on the device. Fewer
instructions, but the float bus's load-add-store chains all ran on the
FPU. The original int bus splits the work between the integer pipe
(saturating adds) and the FPU.

**Tests:**
- laws **MX-A5** and **MX-E2** and the breaks `colloid_break_a5` /
  `colloid_break_e2` are removed;
- **MX-E1** and its break stay (the test now carries only E1's restated old
  law);
- **MX-ST** (stereo against mono) passes on the restored functions.

### 10.4 Checks after the reverts

- **Build:** flash **668,532 B** (+1,832 B on the base 666,700). ITCM
  15,424 B (unchanged). DTCM statics 4,392 B.
  `build/LXRV2_lxr02.img` rebuilt (668,532 B).
- **`elf_p4_check`:** 84 / 84 oscillator loops exact.
- **The mixer's hot loops:** **53 equal, 3 shrank (E1's return), 0 grew.**
  The noise loop's extra `mov` of §2.6 went with the send adds.
- **`elf_s099_check`:** 16 / 16 engine loops exact.
- **Cost tools:** the Delay 234.5 a frame (the base's); `pwm6-par` 400.8,
  `clx-pwm6-par` 447.7 (CL5); the Tetrakt's steady figure 329.9 (A2q + B1).
- **Host suite** (`make -k all`):
  - only the two failures already present before S101 P2 (`voice_control`
    14, `alkaline` AL-OD0);
  - **95 breaks caught**: the baseline's 89, plus `a1ctl1`, `a1ctl2`,
    `lfo3`, `col_e1`, `tkt_a2q`, `tkt_sub`.

---

## 11. Final report — S101 P2 (the CPU pass)

**Goal:** the worst-case mixer average at or under 144k cycles a block
(about a 95 % median on the widget), with no type caps on the Tetrakt or
the Colloid (`S101_P2_REDUCTIONS.md` revision 4, package P, with A2q in
place of B3).

**The final tree** (uncommitted; every code change has a comment block;
§1–§10 log each with its laws and breaks):

| Item | What | Device (§9.2) | Sound |
|---|---|---:|---|
| Q-C9 | the S1 probe maxima behind `MIXER_PROBE_MAX` (off) | ≈ −1.5k (logging builds) | none |
| A1 | the slot control (target restores, velocity, LFO dispatch) every other block, all slots together, with the dirty re-apply | **−3.0k** | LFO and velocity targets update at 689 Hz |
| E1 | the FX return ramps' per-block constants | part of −0.95k (not separable) | ≤ 1 int16 LSB |
| CL5 | the Colloid's two filter controls every other block, two-block ramps | **−0.2 to −0.5k per Colloid-engine voice** | filter moves land ≤ 0.73 ms later, still ramped |
| A2q | the Tetrakt's layer control one layer a block, exact decay | **−1.2k per Tetrakt** | layer row changes step every 2.9 ms |
| B1 | the Tetrakt's tone coefficients stepped every 8 samples | ≈ −0.6k per Tetrakt | slightly coarser on the fastest tone sweeps |

**Built, then reverted:**
- E5, A3 and the send adds (A5 + E2) after the device test (§10);
- CL1 (more instructions than it saved, §4.3);
- CL3 (your decision, §8).

**Not built:**
- CL2: no division-free saturator within S1; the table form is
  cycle-neutral, measured;
- CL4: needs CL1's two-loop form;
- CH1: the chorus ring guard rule;
- B3: your instruction; still an option at about −0.6k per Tetrakt,
  unmeasured.

**Device result** (`SD_CARD_S101_P2_2`, before the reverts of §10):
- **Every measured Scene ran at or under 144k:**
  - 4 Tetrakts + 2 Advanced Colloids 143.3k (91.4 %);
  - 4 Colloids + 2 Advanced Colloids 140.9k (89.9 %);
  - the mixed steps 143.4–144.3k;
  - Bank 076 Scene 5 140.4–143.7k (89.6–91.7 %, down from 93.8 %).
- No underrun while playing.

**After the reverts, estimated from the device deltas (to confirm on the next
card):**
- E5's +1.5k and the send adds' +0.6k come back out.
- E2's conversion loop (about +0.5k) and A3's −0.1k go back in.
- **Net about −1.5k on every Scene above:** Scene 5 about 139–142k, the
  Tetrakt test Scene about 142k.

**RAM:**
- **2 B of new statics** (A1: `modNode_ctlState`,
  `instrumentManager_lfoDirty`), **approved 2026-11-22**.
- No stack added (E5 is gone).
- The Tetrakt voice (1,080 B) and the Colloid voice (1,220 B) are inside the
  1,264 B slot reserve.
- Q-C9 removes 76 B from logging builds.

**Tests:**
- host suite clean apart from the two old failures;
- `elf_p4_check` 84 / 84, `elf_s099_check` 16 / 16;
- the mixer's hot loops 0 grew;
- **CO-PWG failed once** in one earlier full run and has not reproduced (§6).

**Measured, for later decisions:**
- **Dropping the Colloid final drive:** −1.8 to −2.2k per Colloid-engine
  voice; with the tone too, −2.2 to −2.6k. A feature change: patches with
  drive above 0 or the tone below open would change.
- **Full-rate Advanced choruses:** about +0.5k per Advanced Colloid and per
  Advanced Alkaline.
- **A release build** (`DEV_MODE_LOGGING 0`): about −0.8 to −1.0k in the
  mixer, plus the trace's foreground time (not measured).

**Left open:**
1. The next card with this image (you are running it), to confirm the
   reverts' −1.5k.
2. The Colloid final drive (and tone): keep or drop. Written up as a draft
   for your decision in `DRAFT_COLLOID_DRIVE_SAVINGS.md` (measured on scratch
   builds; nothing built).
3. A §A.9 worst-case run with the strict patches, if you want the cap
   proven rather than measured on representative Scenes.

**Closed:** the 2 B RAM sign-off (A1), approved 2026-11-22.
