# S087 P0 — Implementation schedule: oscillator types (samples, loops, wavetables)

**Session:** 087. **Plan:** `S087_P0_OSC_TYPE_SAMPLES_WAVETABLES.md`
(revision r8). That document holds the decisions (A1–A24, Q1–Q13, D1–D10)
and the reasons for them. This document is the work list.
**Branch / base:** `dev-ph7-voice_expansion` at `0e2d61dd`.
**Status:** implementation in progress. The running record is §10; stage A
is implemented and passing its host/ELF gates, with its hardware list
pending. No later stage has started.

---

## 0. How to use this schedule

### 0.1 Conventions

- **Change IDs:** `<stage>-<n>`, e.g. `B-7`. Each names a file, a line,
  and **ADD**, **MOD** or **DEL**.
- **Line numbers are for the tree at `0e2d61dd`.** Every change also
  quotes an anchor (a function name or a nearby line). Apply the changes
  in a file bottom-up, or find each one by its anchor, because earlier
  edits move later lines.
- **"New file"** means the file does not exist yet.
- **Comment blocks.** Every ADD or MOD that creates or changes a function,
  field, macro or table carries the block shown with it. The block goes
  in the source, directly above the code: in the `.h` for declarations and
  in the `.c` for definitions. It uses the project form: What / Why /
  Inputs / Outputs / Accessors / Affiliates. Where the `.h` and the `.c`
  both carry a block, the `.c` copy may say "see the header" for Why and
  Affiliates. One-line count or comment edits carry a one-line
  `/* S087 <stage>: … */` comment instead.
- **Code sketches** show the intended logic only. The implementer writes
  the final code to the project's style. A sketch is not permission to
  skip a test.
- **Sources of truth:** the current tree, the P0 plan, and the tests
  written here. Nothing outside this repository is a reference. Earlier
  experimental code must not be copied (P0 §2 rule 10).

### 0.2 Binding rules (P0 §2, repeated where they shape the code)

1. **CPU:** the limit is each voice's oscillator-section worst case (A1).
   The interpolation slots are checked per slot (A17). Any rise of a
   worst-case instruction count needs the user's explicit approval, with
   the number.
2. **Copies, not edits:** existing per-sample loops are not edited. New
   loops are new functions in `.itcm.s087`, `noinline, noclone`.
3. **No new constant-argument call sites into the existing dispatchers.**
   The only dispatcher edits allowed are the stage-D removals (D-20 …
   D-27).
4. **No state live across an inlined voice render** (S086 F1). Keep it in
   `OscInfo`.
5. **Constant cost:** no per-sample work that depends on the data.
6. **RAM:** every new or enlarged allocation needs the user's approval,
   with its bytes, region, lifetime and owner (§0.5).
7. **Today's trigger behaviour is kept** (P0 rule 11). Drum osc2 and the
   HiHat modulators never reset; Cymbal resets its modulators.

### 0.3 Stages and gates

| Stage | Content | Depends on |
|---|---|---|
| 0 | freeze, baselines, frozen ceilings | — |
| A | a step's automation lands before its trigger (D6) | 0 |
| B | `osc1_type` row, the selector path, step automation of `typ` (§12.5) | A |
| C | sample-flash catalogue v2, the installer, boot notice, optimizer | B |
| D | `smp`/`slp` readers, `pos` on samples, modulator `s`/`l` entries, v1 removal | C |
| E | `wtb` reader, frame blending through the two slots, modulator `w` entries | D |

**Each stage ends with:**
1. `make all && make img`, with the figures recorded;
2. its host tests, each with a recorded break proof;
3. the identity and ELF gates;
4. `armcheck` counts against the frozen ceilings;
5. `special_tags`;
6. the hardware list (§7), with the user's result recorded.

**No stage starts before the previous one passes on hardware.**

### 0.4 Frozen ceilings (recorded at step 0, `0-4`)

| Item | Static per sample (S086 record; re-measure at 0-4) |
|---|---:|
| Drum section, `fm`, v1 samples on both | 90 standalone / ≈ 92 linked |
| Drum `rng` | 87–89 / ≈ 90–91 |
| Drum `mix` | 87 |
| Snare tonal + noise, v1 sample | measure |
| Cymbal/HiHat, v1 samples on all three | ≈ 131 standalone; measure linked |
| Per-oscillator: v1 plain / FM / ring | 40–41 / 48–49 / 46–48 (linked 41–43 / 50–51 / 49–50) |
| One interpolation slot, worst case (FM carrier between two v1 samples) | ≈ +64 standalone / ≈ +66 linked, plus per-block set-up |

### 0.5 RAM requests (approval needed before the stage that uses them)

| ID | Stage | Item | Bytes | Region | Lifetime / owner |
|---|---|---|---:|---|---|
| R-A1 | A | `seq_pending_automation_done` (drain cursor) | +1 | SRAM1 `.bss` | static / Sequencer |
| R-A2 | A | trigger batch in `voiceControl_processPending()` (32 × 3 B events + 32 reset flags) | +128 stack | main stack, foreground | per call / MidiVoiceControl |
| R-C1 | C | `sample_manifest[120]` + count removed | −13,441 | `.bss` | — |
| R-C2 | C | `SampleMemory.c` caches and install tables removed | −5,040 | `.bss` | — |
| R-C3 | C | installer walker state (3 levels × last/best names, counters) | ≈ +560 | `.bss` | modal install only / filesystem.c |
| R-D1 | D | `osc_interp_a/b` removed | −128 | DTCM (`INDTCMZ`) | — |
| R-E1 | E | slot reservation counters `modNode_interpReserved`, `modNode_interpGrantedLast` | +2 | `INCCMZ` (as the existing counter) | static / modulationNode |
| — | B, E | `OscInfo.oscType` (byte 34), `OscInfo.interpStamp` (byte 35) | 0 | existing padding | — |

The net effect is ≈ −18 KB. The exact figures come from the map file
after each stage.

### 0.6 Flash and ITCM budgets (Q12)

| Stage | Program flash (estimate) | ITCM (estimate) |
|---|---:|---:|
| A | ≤ 0.5 KB | 0 |
| B | ≤ 2 KB | 0 |
| C | ≤ 8 KB net (v1 installer removed) | 0 |
| D | ≤ 6 KB | ≤ +2.5 KB, less ≈ 1.3 KB of v1 loops and the dormant interpolation paths |
| E | ≤ 5 KB | ≤ +3.5 KB |

**Stop rule:** if a stage exceeds its estimate by more than 50 %, stop
and report before continuing. Headroom at the start: flash 94,368 B,
ITCM 7,872 B.

---

## 1. Step 0: freeze and baselines

### 0-1 `tools/dsp_test/snapshot87.sh` — ADD (new file)

Copies the files S087 edits into `tools/dsp_test/frozen87/`, keeping
their relative paths. It refuses to run when `frozen87/` exists. The
file list:
- `Core/DSPAudio/Oscillator.c/.h`, `modulationNode.c/.h`, `mixer.c`;
- `Core/MIDI/MidiVoiceControl.c`, `Core/Sequencer/sequencer.c/.h`,
  `main.c`;
- `Core/SampleRom/SampleMemory.c/.h`, `sampleFlash.c/.h`;
- `Core/Hardware/SD/filesystem.c/.h`, `storageTypes.c/.h`;
- the four engines `.c/.h` and the four `*Parameters.c/.h`;
- `Core/DSP/Instruments/InstrumentManager.c/.h`;
- `Core/Bank/Scene/Preset/presetManager.c`;
- `Core/Menu/menu.c`, `MenuText.h`;
- `config.h`.

```sh
# Freeze the pre-S087 sources (S087 step 0).
#
# What:       copies every file S087 edits into tools/dsp_test/frozen87/,
#             preserving relative paths.
# Why:        the S087 identity gates (osc87_identity, engine87_identity)
#             compare the edited files with their exact pre-change text;
#             frozen/, frozen85/ and frozen86/ belong to earlier sessions and
#             must not be refrozen.
# Inputs:     the working tree at 0e2d61dd, before any S087 source edit.
#             Run from anywhere; it cds to the repository root.
# Outputs:    tools/dsp_test/frozen87/<same relative paths>.
# Accessors:  the implementer, once.
# Affiliates: Makefile targets osc87_identity, engine87_identity,
#             elf87_baseline, elf87_mixer_snapshot.
```

### 0-2 `tools/dsp_test/Makefile` — ADD targets (after `elf86_mixer_check`, line 454)

Follow the S086 targets at lines 352 (`osc86_identity`), 420
(`engine86_identity`), 435 (`elf86_p3_check`) and 454
(`elf86_mixer_check`).

- **`osc87_identity`:** `Oscillator.c/.h` against `frozen87/`. It allows
  additions, plus the allow-listed stage-D removals (D-20 … D-27).
- **`engine87_identity`:** the four engines. Allowed: the typed branches
  (D-30 … D-37) and the modulator init lines (B-30 … B-32).
- **`elf87_baseline` / `elf87_check`:** whole-function and loop dumps of
  the oscillator symbols. The list is `OSC86_ELF_SYMS` plus
  `calcUserSampleOsc*`, `osc_renderPlainWave`, `osc_renderSyncWave`,
  `calcNextOscSampleSyncBlock` and `calcNextOscSampleRingBlock`. The
  baseline is written to `frozen87/elf_osc.txt` and `elf_loops.txt`.
- **`elf87_mixer_snapshot` / `elf87_mixer_check`:** the per-sample loops
  inlined into `mixer_calcNextSampleBlock()` (`elf_hotloops.py`), against
  `frozen87/elf_mixer.txt`. This is the S086 F1 gate for every stage.
- **`armcheck-s087`:** an ARM translation unit,
  `tools/dsp_test/armcheck/s087.c` (new). It compiles each new S087 loop
  beside the v1 loop it is budgeted against, and runs `fpseq.py --report
  --all`. It is filled in stage by stage.

### 0-3 `tools/dsp_test/Makefile:252-253` — MOD (when B-1 changes `Oscillator.h`)

The `osc_mod` target extracts `Oscillator.h` lines 49–65 (the defines)
and 71–143 (`OscInfo`). Every edit above or inside `OscInfo` must update
these ranges in the same commit. B-1 and B-2 move them.
`/* S087 B: OscInfo grew oscType comments; ranges re-measured. */`

### 0-4 Baselines (no file change)

From a clean `make all`:
- run `elf87_baseline` and `elf87_mixer_snapshot`;
- record the figures `armcheck-oscmod` / `fpseq.py` gives for every
  existing loop, standalone and linked, in the stage-A record. This
  includes the one-slot interpolation cost: `calcPeriodicInterpFmBlock`
  plus the two FM user-sample renders it calls.
- **Device:** the user records the six-slot `cpu` readings for the
  dearest Drum, Snare and Cymbal/HiHat Scenes, with v1 samples on every
  oscillator and the interpolation toggle on, with an LFO on two `wav`
  rows.

### 0-5 `tools/sampleoptimize.py` — already relocated (S087, this turn)

The optimizer now lives at `tools/sampleoptimize.py`. Stage C changes it
(C-40 … C-47).

---

## 2. Stage A: a step's automation lands before its trigger (D6)

### 2.1 What the trace shows

- **Today's order** (`main.c:213-216`):
  1. `voiceControl_processPending()` pops each trigger. For a resetting
     trigger it runs `seq_restoreAutomatedParameters()`, then
     `instrumentManager_triggerTrack()`.
  2. Then `seq_drainPendingAutomation()` applies the step's entries.
  3. **So a hit's first 32 samples use the old values.**
- **The producer:** TIM3 queues a step's trigger
  (`seq_triggerVoice()` → `voiceControl_noteOn()`) and its automation
  entries (`seq_queueStepAutomations()`) **in the same interrupt run**:
  - `seq_advanceTrackStep()`: trigger at `sequencer.c:3395`, entries at
    `:3446`;
  - `seq_fireDeferred()`: entries at `:5952`, trigger at `:5965`.

  So a foreground snapshot taken under PRIMASK sees both or neither.
- **The new order for each render pass:**
  1. pop every pending trigger **and** mark the automation count, under
     one PRIMASK section;
  2. deferred Scene apply, then the restore for each resetting trigger;
  3. drain the automation up to the mark;
  4. fire the triggers.

  Entries queued after the mark wait one block with their own triggers.
  Automation-only steps after the mark land one block later, which is
  today's worst case.

### A-1 `Core/Sequencer/sequencer.c:902` — ADD the drain cursor (R-A1)

After `static volatile uint8_t seq_pending_automation_drain = 0u;`:

```c
/*
 * Foreground drain cursor for the TIM3 automation queue (S087 stage A).
 *
 * What:       index of the first seq_pending_automation[] record not yet
 *             applied. 0 when the queue is empty. Records below it have
 *             been applied; records from it up to the producer count have
 *             not.
 * Why:        the trigger funnel now drains only up to a mark taken with
 *             its trigger pops (seq_drainPendingAutomationTo()), so a pass
 *             can end with records left over. The queue is still reset to
 *             zero only when it is fully drained; until then this cursor
 *             stops a later pass from applying the same records twice.
 * Inputs:     seq_drainPendingAutomationTo() (advances it), seq_init() and
 *             the transport reset (zero it with the count).
 * Outputs:    the start index of the next drain.
 * Accessors:  seq_drainPendingAutomationTo() only, under the same PRIMASK
 *             handoff as seq_pending_automation_count.
 * Affiliates: seq_pending_automation_count, seq_pendingAutomationMark(),
 *             voiceControl_processPending(). RAM: +1 B SRAM1 (R-A1).
 */
static volatile uint8_t seq_pending_automation_done = 0u;
```

### A-2 `Core/Sequencer/sequencer.c:1818-1819` — MOD (in `seq_init()`, line 1770)

After `seq_pending_automation_drain = 0u;` add
`seq_pending_automation_done = 0u;` with
`/* S087 A: the drain cursor starts with the queue. */`. Search for every
other line that zeroes `seq_pending_automation_count`; at `0e2d61dd` there
is only this one besides the drain itself (A-3). Each one zeroes the
cursor too.

### A-3 `Core/Sequencer/sequencer.c:3608-3770` — MOD `seq_drainPendingAutomation()` → `seq_drainPendingAutomationTo(uint8_t limit)`

- **Rename** the function and add the `limit` parameter.
- **The body is unchanged except:**
  1. `i` starts at `seq_pending_automation_done`, not 0;
  2. the inner loop runs while `i < seq_pending_automation_count && i <
     limit`;
  3. the PRIMASK handoff at lines 3751-3764:
     - if `i == seq_pending_automation_count`, zero the count, the drain
       flag and `seq_pending_automation_done`, then return (as today plus
       the cursor);
     - else if `i >= limit`, store `seq_pending_automation_done = i` and
       return;
     - else loop, as today.
  4. `seq_effectAutomationReset` and `seq_rampConsumeSteps()` at the top
     stay where they are.

Replace the comment block at lines 3591-3607 with:

```c
/*
 * Apply queued step automation up to a mark (S087 stage A; was
 * seq_drainPendingAutomation()).
 *
 * What:       applies the TIM3 automation records from the drain cursor
 *             (seq_pending_automation_done) up to, but not including,
 *             index `limit` (or the producer count if smaller). Valid voice
 *             descriptor targets update their owning runtime image through
 *             seq_writeInstrumentAutomation() and may start an S081 P2
 *             ramp; Scene targets go to seq_applySceneAutomation(); Effect
 *             targets to effects_applyAutomation() behind the type gate.
 *             When every published record is applied the queue resets
 *             under PRIMASK; otherwise the cursor keeps the position.
 * Why:        D6 (S087 P0 §6.5): a step's own automation must be applied
 *             after the restore of its resetting trigger and before the
 *             trigger fires. voiceControl_processPending() takes the mark
 *             together with its trigger pops (seq_pendingAutomationMark()),
 *             restores, calls this, then fires the triggers. Records after
 *             the mark belong to triggers not yet popped and wait one
 *             block with them, so a later restore cannot undo them.
 * Inputs:     limit (0..SEQ_PENDING_BUF_COUNT; 0xFF = everything published),
 *             the queue, the latch/erase filters, seq_menuOverride[].
 * Outputs:    runtime descriptor values, Scene/Effect automation, ramp
 *             starts, held bits (seq_automation_dirty[]), the cursor.
 * Accessors:  voiceControl_processPending() (the only caller since S087).
 * Affiliates: seq_pendingAutomationMark(), seq_restoreAutomatedParameters(),
 *             seq_rampStartIfPaired(), main.c render loop,
 *             PATTERN_DYNAMIC_STACK.md §6.1.
 */
void seq_drainPendingAutomationTo(uint8_t limit)
```

### A-4 `Core/Sequencer/sequencer.c` (after A-3) — ADD `seq_pendingAutomationMark()`

```c
/*
 * Mark the automation published so far (S087 stage A).
 *
 * What:       returns seq_pending_automation_count.
 * Why:        voiceControl_processPending() calls it inside the PRIMASK
 *             section that pops the trigger ring. TIM3 publishes a step's
 *             trigger and its entries in one interrupt run
 *             (seq_advanceTrackStep(), seq_fireDeferred()), so the popped
 *             triggers and the marked entries always belong together.
 * Inputs:     none. Must be called with interrupts masked.
 * Outputs:    the producer count (0..SEQ_PENDING_BUF_COUNT).
 * Accessors:  voiceControl_processPending().
 * Affiliates: seq_drainPendingAutomationTo(), seq_queueStepAutomations().
 */
uint8_t seq_pendingAutomationMark(void)
{
    return seq_pending_automation_count;
}
```

### A-5 `Core/Sequencer/sequencer.h:385` — MOD declarations

Replace `void seq_drainPendingAutomation(void);` with the two
declarations and a short block pointing to A-3/A-4. Update the comments
at `sequencer.h:399`, `:420`, `:432` and `:446`, and the in-code comments
in `sequencer.c` that name `seq_drainPendingAutomation()` (`grep`; about
12 sites), to the new name. These are text-only edits.

### A-6 `Core/MIDI/MidiVoiceControl.c:199-235` — MOD: split `voiceControl_triggerNow()`

Split it into two static functions. The bodies are the existing lines,
moved:

```c
/*
 * Prepare one popped trigger (S087 stage A; first half of the old
 * voiceControl_triggerNow()).
 *
 * What:       preset_applyDeferredSceneSlotForTrigger(voice), then
 *             seq_restoreAutomatedParameters(voice) when `reset` is set.
 * Why:        the restore must run before the step's own automation is
 *             drained (D6). The deferred Scene apply keeps its place ahead
 *             of the restore, so the restore reads the played Scene's
 *             morph_interpolation[] (S081 §5.8 order, unchanged).
 * Inputs:     voice 0..6, reset flag from voiceControl_popPending().
 * Outputs:    the installed Scene slot image; restored runtime values;
 *             cleared menu override for the voice.
 * Accessors:  voiceControl_processPending().
 * Affiliates: voiceControl_fireTrigger(), seq_drainPendingAutomationTo().
 */
static void voiceControl_prepareTrigger(uint8_t voice, uint8_t reset);

/*
 * Fire one popped trigger (S087 stage A; second half of the old
 * voiceControl_triggerNow()).
 *
 * What:       instrumentManager_triggerTrack(), the S074 sidechain offer and
 *             the LED pulse, exactly as before.
 * Why:        runs after the drain, so the voice starts with its step's
 *             automation already applied (D6).
 * Inputs:     voice, note, vel.
 * Outputs:    the triggered voice; sidechain weight; LED pulse.
 * Accessors:  voiceControl_processPending().
 * Affiliates: voiceControl_prepareTrigger().
 */
static void voiceControl_fireTrigger(uint8_t voice, uint8_t note, uint8_t vel);
```

### A-7 `Core/MIDI/MidiVoiceControl.c:362-391` — MOD `voiceControl_processPending()`

```c
/*
 * Process all pending triggers for one render pass (S087 stage A order).
 *
 * What:       1. under one PRIMASK section: pop every ring entry with its
 *                reset bit into a local batch (at most the ring size, 32),
 *                take the leftover reset bits, and mark the automation queue
 *                (seq_pendingAutomationMark());
 *             2. prepare each popped trigger (deferred Scene apply, restore
 *                when reset), then restore each leftover reset voice;
 *             3. seq_drainPendingAutomationTo(mark);
 *             4. fire each popped trigger in pop order.
 * Why:        D6 (S087 P0 §6.5): a step's automation must be applied before
 *             its trigger fires; before S087 the drain ran after the trigger
 *             and every hit's first block used the previous values. Taking
 *             the pops and the mark together keeps a step's trigger and its
 *             entries in the same pass (TIM3 publishes them in one
 *             interrupt run).
 * Inputs:     the trigger ring, voiceControl_pendingReset, the automation
 *             queue.
 * Outputs:    restored, then automated, then triggered voices.
 * Cost:       one PRIMASK section of at most 32 event copies; the batch is
 *             128 B of stack (R-A2).
 * Edge:       two triggers of one voice in one pass: both prepares run
 *             before the drain, so the drain's values serve both hits
 *             (last writer wins, as in step order).
 * Accessors:  main.c render loop (once per OUTPUT_DMA_SIZE block).
 * Affiliates: seq_pendingAutomationMark(), seq_drainPendingAutomationTo(),
 *             seq_restoreAutomatedParameters(), voiceControl_popPending().
 */
void voiceControl_processPending(void)
```

**Sketch:**
- One `voiceControl_irqSave()`.
- Loop: `while (tail != head) { batch[n] = ring[tail]; reset[n] = bit;
  clear bit; tail++; n++; }`.
- Then `leftover = voiceControl_pendingReset; voiceControl_pendingReset =
  0; mark = seq_pendingAutomationMark();`, then restore the IRQ state.
- The pop logic moves out of `voiceControl_popPending()` into this loop.
  Keep that function only if other callers remain; at `0e2d61dd` there
  are none, so DEL it (lines 167-197) together with its comment block.

### A-8 `main.c:213-216` — MOD the render loop

- Delete `seq_drainPendingAutomation();` at line 214, because the drain
  now runs inside `voiceControl_processPending()`.
- Replace the comment at line 215 with:
  `/* S081 P2 / S087 A: ramps after the step values land (the drain runs
  inside voiceControl_processPending()). */`
- Update the order note inside `voiceControl_processPending()`'s old
  comment (lines 378-384), which A-7 replaces.

### A-9 Host test `tools/dsp_test/test_stepauto_order.c` — ADD (new) + Makefile target `stepauto_order`

- **Extraction:** `voiceControl_processPending`, `voiceControl_prepareTrigger`
  and `voiceControl_fireTrigger`, plus the ring statics, from
  `MidiVoiceControl.c` via `extract.py`. Stubs record the call order:
  `preset_applyDeferredSceneSlotForTrigger`,
  `seq_restoreAutomatedParameters`, `seq_drainPendingAutomationTo(limit)`,
  `seq_pendingAutomationMark`, `instrumentManager_triggerTrack`.
- **Laws:**
  1. order = deferred apply → restore → drain → trigger;
  2. the drain limit equals the mark taken with the pops;
  3. a trigger and entries enqueued "after the mark" (simulated by the
     stub on its first call) are left for the next pass;
  4. leftover resets restore before the drain;
  5. a non-reset trigger does not restore.
- **Break proof:** move the drain after the triggers; law 1 must fail.

### A-10 Host test `tools/dsp_test/test_drain_cursor.c` — ADD (new) + target `drain_cursor`

- **Extraction:** `seq_drainPendingAutomationTo` and
  `seq_pendingAutomationMark`, with stubs for every write target (records
  in order) and a permissive `instrumentManager_targetValid`.
- **Laws:**
  1. a limited drain applies exactly `[done, limit)`;
  2. a second drain continues from the cursor, with no record applied
     twice;
  3. the queue resets only when fully drained;
  4. a record appended between passes is applied once.
- **Break proof:** start `i` at 0; law 2 must fail.

### 2.2 Stage-A gates

- `stepauto_order` and `drain_cursor` pass, with their break proofs.
- `elf87_mixer_check`: the per-sample loops are unchanged.
  `voiceControl_processPending` is not inlined into the mixer, but check
  anyway.
- **Hardware (§7.1):** a step with a large coarse-pitch jump on every
  other step, triggered. The attack must start at the new pitch, with no
  one-block blip of the old pitch. Also re-check rolls, flams, deferred
  (shuffled) steps and `rmp` ramps.

---

## 3. Stage B: the `osc1_type` row, the selector path, step automation of `typ`

**What lands:**
- **One row per instrument,** `osc1_type` (`typ`). It is appended, so
  every existing ID is kept, and a missing key loads 0 (`wav`).
- **Its kind:** a selector. It is not morphable, it is stored
  single-ended, and it is applied before the Morph image.
- **What can change it:** step automation (one value per step, never
  ramped). LFO and velocity cannot.
- **What it offers in B:** `wav` only (`OSC_TYPE_SELECTABLE` = 1).
  Stage D raises this to 3 (`smp`, `slp`), stage E to 4 (`wtb`).
- **The modulators get no new row** (A22). Their init marks them as
  flat-list oscillators (`OSC_TYPE_MODLIST`).

### B-1 `Core/DSPAudio/Oscillator.h:65` — ADD type constants (after `#define OSC_SAMPLE_START 0x08`)

```c
/*
 * Oscillator type: the typ row (S087 stage B).
 *
 * What:       OSC_TYPE_WAV 0 (stock waves, today's oscillator), OSC_TYPE_SMP 1
 *             (one-shot sample), OSC_TYPE_SLP 2 (looped sample), OSC_TYPE_WTB 3
 *             (wavetable), OSC_TYPE_COUNT 4. OSC_TYPE_MODLIST 4 is not a typ
 *             value: it marks a modulator (Drum osc2, Cymbal/HiHat osc2/osc3),
 *             whose wav list is the stock waves followed by the user entries
 *             s01.., l01.., w01.. (S087 P0 §12.4). OSC_TYPE_SELECTABLE is the
 *             number of typ values the menu and the writer accept in this
 *             build: 1 after stage B, 3 after stage D, 4 after stage E.
 * Why:        S087 P0 §4, A21/A22: osc1 selects its type with typ; the
 *             modulators have no typ row and select user content through
 *             their wav list. One byte in OscInfo carries both meanings so
 *             osc_setFreq() and the renderers can tell them apart.
 * Inputs:     the osc1_type descriptor (osc1) and the engine init functions
 *             (modulators).
 * Outputs:    OscInfo.oscType.
 * Accessors:  osc_setType(), osc_isTyped(), osc_setFreq(), the engines'
 *             typed branches, menu.c (count), InstrumentManager.c (writer).
 * Affiliates: MENU_OSC_TYPE and oscTypeNames (MenuText.h); the osc1_type rows
 *             in the four *Parameters.c files.
 */
#define OSC_TYPE_WAV         0u
#define OSC_TYPE_SMP         1u
#define OSC_TYPE_SLP         2u
#define OSC_TYPE_WTB         3u
#define OSC_TYPE_COUNT       4u
#define OSC_TYPE_MODLIST     4u
#define OSC_TYPE_SELECTABLE  1u   /* S087 B: wav only; D -> 3; E -> 4 */
_Static_assert(OSC_TYPE_SELECTABLE >= 1u && OSC_TYPE_SELECTABLE <= OSC_TYPE_COUNT,
               "OSC_TYPE_SELECTABLE out of range");
```

### B-2 `Core/DSPAudio/Oscillator.h:127` — ADD `oscType` (byte 34, after `int8_t positionBendStep;`)

```c
	/*
	 * Oscillator type (S087 stage B).
	 *
	 * What:       OSC_TYPE_WAV/SMP/SLP/WTB for osc1 (the typ row);
	 *             OSC_TYPE_MODLIST for a modulator (set once by the engine
	 *             init). 0 after a runtime-slot reset, i.e. wav.
	 * Why:        the renderers, osc_setFreq() and the pos helpers need the
	 *             type every block; the byte sits in the padding after
	 *             positionBendStep (offset 34), so OscInfo stays 72 bytes.
	 * Inputs:     osc_setType() (osc1, through IM_SPECIAL_OSC_TYPE); the
	 *             engine init functions (modulators).
	 * Outputs:    read by osc_isTyped(), osc_setFreq(), the typed renderers.
	 * Accessors:  osc_setType() is the only writer for osc1.
	 * Affiliates: OSC_TYPE_* above; S087 P0 §12.1 A22.
	 */
	uint8_t		oscType;
```

Below it, keep byte 35 free until stage E (E-2). In the assert block at
lines 163-174, ADD:

```c
_Static_assert(offsetof(OscInfo, oscType) == offsetof(OscInfo, positionBendStep) + 1u,
               "OscInfo.oscType must follow positionBendStep (padding, S087 B)");
```

The `waveInterpFrac` assert (line 170) and the `sizeof == 72` assert
(line 173) must still pass. **0-3:** update the Makefile extraction ranges.

### B-3 `Core/DSPAudio/Oscillator.h` (before `#endif`, line 584) — ADD `osc_isTyped()` and the `osc_setType()` declaration

```c
/*
 * Whether osc1 plays a user type (S087 stage B).
 *
 * What:       1 for OSC_TYPE_SMP, OSC_TYPE_SLP and OSC_TYPE_WTB; 0 for wav and
 *             for modulators (OSC_TYPE_MODLIST).
 * Why:        the engines branch on it once per block to call the typed
 *             renderers instead of the existing dispatchers (S087 P0 A2), and
 *             the pos helpers use it to choose the rule (D3).
 * Inputs:     osc.  Outputs: 0 or 1.
 * Accessors:  the four engine renders and triggers; osc_positionBendApply()/
 *             Restore(); osc_setFreq().
 * Affiliates: OSC_TYPE_*; osc_setType().
 */
static inline uint8_t osc_isTyped(const OscInfo* osc)
{
	return (uint8_t)(osc->oscType >= OSC_TYPE_SMP && osc->oscType <= OSC_TYPE_WTB);
}

/*
 * Change osc1's type (S087 stage B; extended in D-8 and E-6).
 *
 * What:       clamps `type` to OSC_TYPE_SELECTABLE - 1; when it differs from
 *             osc->oscType:
 *             - stores it;
 *             - clamps osc->waveform to an entry that exists for the new type
 *               (wav: OSC_STOCK_LAST; smp/slp: sample count - 1; wtb: table
 *               count - 1; none installed: 0) - the value is otherwise used
 *               as it is: nothing is remembered or restored (S087 P0 r8);
 *             - re-anchors pos: positionApplied = position (D7);
 *             - re-anchors the phase for the new phase units: into smp/slp
 *               the pos start point; from smp/slp into wav/wtb the pos
 *               trigger phase; between wav and wtb the phase is kept
 *               (S087 P0 §12.5);
 *             - invalidates the frequency cache and the bind
 *               (freqCacheValid = 0; bind generation 0, stage D);
 *             - releases a held interpolation slot (interpStamp = 0,
 *               stage E).
 *             An unchanged type does nothing, so repeated writes (apply,
 *             restore, automation) are idempotent.
 * Why:        the single place where a type change happens, used by the menu,
 *             Kit apply, the trigger restore and step automation alike.
 * Inputs:     osc (osc1 of a runtime slot), type 0..255.
 * Outputs:    oscType, waveform, positionApplied, phase, caches.
 * Accessors:  instrumentManager_writeSpecialRuntime() (IM_SPECIAL_OSC_TYPE).
 * Affiliates: osc_isTyped(); osc_userCount() (stage D); S087 P0 §12.5.
 */
void osc_setType(OscInfo* osc, uint8_t type);
```

### B-4 `Core/DSPAudio/Oscillator.c` (end of file, new section `S087`) — ADD `osc_setType()`

Its comment block is the same as B-3 (Why and Affiliates may point to
the header). Not in ITCM: it runs at control rate. **In stage B** only
the `wav` branch is reachable; write the full logic now with the user
counts returning 0, and stage D fills in `osc_userCount()` (D-8).

### B-5 `Core/DSP/Instruments/InstrumentManager.h:183` — ADD `INSTRUMENT_PARAM_FLAG_SELECTOR`

```c
/*
 * Selector descriptor flag (S087 stage B).
 *
 * What:       0x08 marks a row that chooses what an oscillator is (osc1_type)
 *             rather than tuning it. Such a row is never morphable (its
 *             in-between values are other choices), is stored single-ended
 *             (Normal image only), is applied before the Morph image on every
 *             voice apply, and may be step-automated as one value per step.
 * Why:        S087 P0 §12.5: the automation gate admitted only morphable rows,
 *             and Morph interpolates every morphable row linearly; a separate
 *             flag lets typ be automated without being morphed.
 * Inputs:     the ROW_SELECT_SPECIAL rows.  Outputs: flag bit.
 * Accessors:  instrumentManager_targetValid(), preset_setSupplementalParameter-
 *             Internal(), preset_applyKitVoiceSelectors(),
 *             preset_applyKitVoiceSupplementalInternal().
 * Affiliates: INSTRUMENT_PARAM_FLAG_MORPHABLE/AUTOMATABLE; seq_rampDtypeOk()
 *             (ramps are excluded by dtype, not by this flag).
 */
#define INSTRUMENT_PARAM_FLAG_SELECTOR        0x08u
```

### B-6 `Core/DSP/Instruments/InstrumentManager.h:149-150` — ADD `IM_SPECIAL_OSC_TYPE` (before `IM_SPECIAL_WRITER_COUNT`)

```c
    /*
     * S087 B: osc1_type writer. Calls osc_setType() on the tagged oscillator,
     * then refreshes the wave rows' modulation domains for the slot
     * (instrumentManager_waveDomainChanged()).
     */
    IM_SPECIAL_OSC_TYPE,
```

The assert at line 159 (`WRITER_COUNT <= 32`) still holds, at 21.

### B-7 `Core/DSP/Instruments/InstrumentManager.c:797-800` — MOD `instrumentManager_targetValid()`

Today it reads `if (!descriptor || !(flags & MORPHABLE)) return 0u;`.
Make it:

```c
    /*
     * Selector rows are automation targets only (S087 stage B).
     *
     * What:       a voice row must be MORPHABLE to be any target, except a
     *             SELECTOR row with AUTOMATABLE set, which is valid for
     *             INSTRUMENT_TARGET_AUTOMATION only. Modulation (LFO,
     *             velocity) still requires MORPHABLE and MODULATABLE.
     * Why:        S087 P0 §12.5: typ is step-automatable, one value per step,
     *             but never an LFO/velocity target and never morphed.
     * Inputs:     descriptor flags, use.  Outputs: validity.
     * Accessors:  every caller of this gate: sequencer.c:3686 (drain), :4402
     *             (ramp service); menu.c:13192, :13364, :13560, :13708,
     *             :13759, :13814, :13872 (automation target picker, live
     *             record, display); PatternData.c:1652 (step writer);
     *             InstrumentManager.c:848, :1124, :2352, :2405.
     * Affiliates: INSTRUMENT_PARAM_FLAG_SELECTOR; seq_rampDtypeOk().
     */
    if (!descriptor)
        return 0u;
    if (!(descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)) {
        if (use == INSTRUMENT_TARGET_AUTOMATION &&
            (descriptor->flags & INSTRUMENT_PARAM_FLAG_SELECTOR) &&
            (descriptor->flags & INSTRUMENT_PARAM_FLAG_AUTOMATABLE))
            return 1u;
        return 0u;
    }
```

**Checks for each caller listed in the block:**
- the result with `osc1_type` is intended;
- the picker lists `typ` among automation targets, not among LFO
  targets;
- live record records it;
- the step writer stores it.

The stage-B host test (B-40) covers the gate itself. Hardware covers the
callers (§7.2).

### B-8 `Core/DSP/Instruments/InstrumentManager.c:3358` — ADD `case IM_SPECIAL_OSC_TYPE` (beside `IM_SPECIAL_LFO_OFFSET`)

```c
    case IM_SPECIAL_OSC_TYPE: {
        /*
         * osc1_type writer (S087 stage B).
         *
         * What:       osc_setType() on the tagged oscillator, then
         *             instrumentManager_waveDomainChanged(slot) so LFO adapters
         *             and direct targets on the slot's wave rows pick up the
         *             new type's range.
         * Why:        every typ write (menu, Kit apply, restore, step
         *             automation) must change the type the same way.
         * Inputs:     slot, descriptor tag (IM_SPECIAL_OSC1), byte value.
         * Outputs:    the oscillator's type state; refreshed wave domains.
         * Callers:    instrumentManager_writeRuntimeInternal() via the tag.
         * Affiliates: osc_setType(); the osc1_type rows; S087 P0 §12.5.
         */
        OscInfo *osc = instrumentManager_oscBySelector(
            slot, (uint8_t)(special & IM_SPECIAL_OSC_MASK));
        if (!osc) return 0u;
        osc_setType(osc, byteValue);
        instrumentManager_waveDomainChanged(slot);
        return 1u; }
```

### B-9 `Core/DSP/Instruments/InstrumentManager.c:3404` — MOD `instrumentManager_classifySpecialKey()`

ADD `if (strcmp(key, "osc1_type") == 0) return IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1;`
next to the `transient_wave` line (3449). Use the form the function
already uses for oscillator-tagged keys (selector bits included).
`/* S087 B: osc1_type. */`

### B-10 `Core/DSP/Instruments/InstrumentManager.c:698-728` — MOD `instrumentManager_descriptorModDomain()` gains `slot`

- New signature: `(uint8_t slot, const ParamDescriptor *descriptor,
  instrument_mod_domain_t *domain_out)`.
- `INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX` takes its maximum from
  `instrumentManager_waveDomainMax(slot, descriptor)` (B-11).
  **In stage B that returns `modNode_getMaxWaveformIndex()`**, so
  behaviour is unchanged until D-40.
- **Call sites:** `:742` passes `INSTRUMENT_SLOT_COUNT` (no slot: the
  existence check only); `:2202` passes `slot`; `:2286` and `:2378` pass
  the target slot in scope there.
- Extend the existing comment block with:
  `S087 B: slot-aware so a wave row's range follows osc1's type and the
  modulator list (instrumentManager_waveDomainMax()).`

### B-11 `Core/DSP/Instruments/InstrumentManager.c` (after B-10) — ADD `instrumentManager_waveDomainMax()` and `instrumentManager_waveDomainChanged()`

```c
/*
 * Highest legal value of an oscillator wave row (S087 stage B; typed in D-40).
 *
 * What:       for "osc1_wave": the last entry of osc1's current type
 *             (wav: OSC_STOCK_LAST; smp/slp: samples - 1; wtb: tables - 1;
 *             0 when none); for a modulator wave row: the last entry of the
 *             flat list (osc_userListCount() - 1, S087 P0 §12.4). Stage B
 *             returns modNode_getMaxWaveformIndex() for every row (no change).
 * Why:        LFO and velocity ranges, the menu clamp and automation display
 *             must all agree on what exists.
 * Inputs:     slot (INSTRUMENT_SLOT_COUNT = unknown: 127), descriptor.
 * Outputs:    max value.
 * Accessors:  instrumentManager_descriptorModDomain(); menu.c via
 *             instrumentManager_waveCellCount() (D-41).
 * Affiliates: instrumentManager_osc(); osc_userCount(); osc_userListCount().
 */
static uint8_t instrumentManager_waveDomainMax(uint8_t slot,
                                               const ParamDescriptor *descriptor);

/*
 * Refresh the modulation domains of a slot's wave rows (S087 stage B).
 *
 * What:       for every active LFO adapter (lfo_descriptor_targets[][], line
 *             ~99 struct) whose slot is `slot` and whose descriptor has
 *             INSTRUMENT_MOD_DOMAIN_DYNAMIC_MAX, recompute adapter->domain
 *             with instrumentManager_descriptorModDomain(slot, ...) and clamp
 *             base_value into it; for a direct ModulationNode target on the
 *             same id, call modNode_directOriginalValueChanged() with the new
 *             range (the path at :2196-2200).
 * Why:        a typ change (or an install, D-42) changes how many entries the
 *             wave row has; an LFO set up for 8 stock waves must not keep
 *             sweeping 0..7 over a 3-sample list, nor stop short of a
 *             100-sample list.
 * Inputs:     slot.  Outputs: refreshed adapter domains and ranges.
 * Accessors:  IM_SPECIAL_OSC_TYPE writer (B-8); the post-install refresh
 *             (C-31).
 * Affiliates: instrumentManager_updateLfoAdapters() (:3151),
 *             instrumentManager_resolveModulationTarget() (:2325).
 */
void instrumentManager_waveDomainChanged(uint8_t slot);
```

Declare `instrumentManager_waveDomainChanged()` in
`InstrumentManager.h`, next to the other runtime services (after line
~260; place it beside `instrumentManager_recalcSlotFilter`).

### B-12 `Core/Menu/MenuText.h:24-25` — ADD `MENU_OSC_TYPE` (id 4, the free slot between `MENU_AUDIO_OUT` 3 and `MENU_SYNC_RATES` 5)

```c
/* S087 B: osc1_type table. Id 4 was unused; DTYPE_MENU keeps typ out of
** S081 ramps (seq_rampDtypeOk() ramps numeric dtypes only) and avoids a new
** dtype, which would wrap the 4-bit dtype field (S087 P0 §12.5). */
#define MENU_OSC_TYPE       4
```

### B-13 `Core/Menu/MenuText.h:146` — ADD `oscTypeNames` (after `waveformNames`)

```c
/*
 * osc1_type names (S087 stage B).
 *
 * What:       [0] = count (OSC_TYPE_COUNT), then wav smp slp wtb.
 * Why:        the typ cell, its automation display and the single-parameter
 *             view share one table; the menu shows only the first
 *             OSC_TYPE_SELECTABLE entries.
 * Accessors:  getMaxEntriesForMenu(), getMenuItemNameForValue(),
 *             menu_automationValueMax() (menu.c).
 * Affiliates: OSC_TYPE_* (Oscillator.h); MENU_OSC_TYPE.
 */
static const char oscTypeNames[][4] = {
    {4}, {"wav"},{"smp"},{"slp"},{"wtb"},
};
```

ADD an assert next to `menu.c:11607`:
`_Static_assert(sizeof(oscTypeNames)/sizeof(oscTypeNames[0]) - 1u == OSC_TYPE_COUNT, "oscTypeNames must list every OSC_TYPE");`

### B-14 `Core/Menu/menu.c:11454-11474` — MOD `getMaxEntriesForMenu()`

ADD `case MENU_OSC_TYPE: return (uint8_t)OSC_TYPE_SELECTABLE;  /* S087 B */`.

### B-15 `Core/Menu/menu.c:11491-11506` — MOD `getMenuItemNameForValue()`

ADD `case MENU_OSC_TYPE: p = oscTypeNames[(curParmVal < OSC_TYPE_COUNT ? curParmVal : 0) + 1]; break;  /* S087 B */`.

### B-16 `Core/Menu/menu.c:12950-12964` — MOD `menu_automationValueMax()`

ADD `case MENU_OSC_TYPE: return (uint8_t)(OSC_TYPE_SELECTABLE - 1u);  /* S087 B: step automation of typ */`.

### B-17 `Core/Menu/menu.c:5232-5244` — MOD the instrument cell commit (`menu_cellCommitValue()`)

A selector row has no Morph image: the existing `else if (!morphView)`
branch routes it to `preset_setSupplementalParameter()`, and that stays.
After that loop, ADD:

```c
        /*
         * Clamp osc1_wave after a typ switch (S087 stage B; P0 r8).
         *
         * What:       when the committed cell is a SELECTOR row tagged
         *             IM_SPECIAL_OSC_TYPE, read the slot's stored osc1_wave
         *             (Normal and Morph endpoints) in every edited Scene and,
         *             where a value exceeds instrumentManager_waveDomainMax()
         *             for the new type, commit the clamped value with
         *             preset_setInstrumentParameter() (both images).
         * Why:        the user rule: the wav value is used as it is, clamped
         *             to something that exists on switch; the stored value
         *             must show what plays. Values in range are untouched.
         * Inputs:     cell, edit mask, the new type.  Outputs: clamped stored
         *             osc1_wave where needed; changed flag.
         * Affiliates: osc_setType() (the runtime clamp); B-11.
         */
```

### B-20 … B-29 The four parameter tables

The same edits are made in each of `DrumParameters.c/.h`,
`SnareParameters.c/.h`, `CymbalParameters.c/.h` and
`HiHatParameters.c/.h`.

**B-20 `*Parameters.c` (after the `ROW_MENU_SPECIAL` macro; Drum line
87, the others at the same place):** ADD `ROW_SELECT_SPECIAL`.

```c
/*
 * ROW_SELECT_SPECIAL: a selector row with a special writer (S087 stage B).
 *
 * What:       DTYPE_MENU with the menu id in the high nibble; flags
 *             AUTOMATABLE | SELECTOR (not MORPHABLE, not MODULATABLE);
 *             MOD_NONE; a BIND_SPECIAL runtime binding.
 * Why:        osc1_type is chosen, not tuned: no Morph, no LFO or velocity,
 *             one step-automation value per step (S087 P0 §12.5).
 * Inputs:     key, category, long and short label, menu id, member, scalar
 *             type, IM_SPECIAL_* tag.
 * Outputs:    one ParamDescriptor initializer.
 * Accessors:  the osc1_type row below.
 * Affiliates: INSTRUMENT_PARAM_FLAG_SELECTOR; instrumentManager_targetValid().
 */
#define ROW_SELECT_SPECIAL(key_, cat_, long_, short_, menu_, member_, type_, special_) \
    { key_, short_, long_, cat_, (uint8_t)(DTYPE_MENU | (menu_ << 4)), \
      (INSTRUMENT_PARAM_FLAG_AUTOMATABLE | INSTRUMENT_PARAM_FLAG_SELECTOR), \
      MOD_NONE, BIND_SPECIAL(member_, type_, special_) }
```

**B-21 the enum** (Drum `:172`, Snare `:170`, Cymbal `:171`, HiHat
`:174`): after `X_PARAM_OSC1_POSITION,` ADD
`X_PARAM_OSC1_TYPE, /* S087 B: appended; preserve all prior IDs. */`.

**B-22 the row** (after the `osc1_position` row: Drum `:268`, Snare
`:258`, Cymbal `:260`, HiHat `:263`):

```c
    /*
     * osc1_type: typ, the oscillator type (S087 stage B).
     *
     * What:       0 wav, 1 smp, 2 slp, 3 wtb (OSC_TYPE_*), stored in
     *             osc.oscType through IM_SPECIAL_OSC_TYPE (osc_setType()).
     *             The menu offers the first OSC_TYPE_SELECTABLE values.
     * Why:        S087 P0 §4: osc1 selects stock waves, samples, loops or
     *             wavetables. A selector: never morphed, stored in the Normal
     *             image only, applied before the Morph image, step-automatable
     *             (one value per step, no ramp), not an LFO/velocity target.
     *             Appended last, so every existing descriptor index,
     *             automation ID, token, NRPN ID and Learn row is unchanged; a
     *             file without the key loads 0 (wav, today's sound).
     * Inputs:     menu edit, Kit/Scene/Instrument apply, the trigger restore,
     *             step automation.
     * Outputs:    osc.oscType and the type state (osc_setType()).
     * Accessors:  instrumentManager_writeSpecialRuntime().
     * Affiliates: the typed renderers (Oscillator.c S087 section);
     *             FILESYSTEM_SPEC.md (the key); S087 P0 §12.5.
     */
    ROW_SELECT_SPECIAL("osc1_type", "Oscilltr", "Type", "typ", MENU_OSC_TYPE,
                       osc.oscType, TYPE_UINT8, IM_SPECIAL_OSC_TYPE | IM_SPECIAL_OSC1),
```

**B-23 `*Parameters.h:14`:** `X_PARAM_DESCRIPTOR_COUNT` goes 40 → 41
(Snare 39 → 40), with
`/* S087 B: + osc1_type. */`. The `_Static_assert` at `.c:176` (and its
equivalents) checks it.

**B-24 the page cell (first page row):**
- Drum `:287`: cell index 4 = `DRUM_PARAM_OSC1_TYPE` (screen 2 = `typ`);
- Snare `:275`: cell index 6 = `SNARE_PARAM_OSC1_TYPE` (screen 2 = `noi
  mix typ`);
- Cymbal `:276` and HiHat `:281`: cell index 4.

Update the row comment, e.g. `/* S087 B: screen 2 is typ. */`.

**B-25 capacity:** 41 ≤ 64 (`INSTRUMENT_PARAM_COUNT`) for every table;
the existing asserts cover it.

### B-30 … B-32 Modulator role marker (engine init)

- **B-30 `DrumVoice.c:114`** (in `Drum_initVoice()`, after
  `voice->modOsc.freq = 440;`): ADD `voice->modOsc.oscType =
  OSC_TYPE_MODLIST;`.
- **B-31 `CymbalVoice.c:90` and `:97`** (in `Cymbal_initVoice()`): the
  same for `modOsc` and `modOsc2`.
- **B-32 `HiHat.c:101` and `:108`** (in `HiHat_initVoice()`): the same.

Comment, once per init:

```c
	/*
	 * Modulator role (S087 stage B, A22).
	 *
	 * What:       marks the FM-page / Mod Osc oscillators as OSC_TYPE_MODLIST:
	 *             their wav list is the stock waves followed by the user
	 *             entries (S087 P0 §12.4); they have no typ or pos row.
	 * Why:        osc_setFreq() and the dispatcher's user branch read the mark
	 *             to bind a list entry; a runtime-slot reset (memset + init)
	 *             restores it.
	 * Affiliates: OSC_TYPE_MODLIST; osc_userListBind() (stage D).
	 */
```

The Snare `noiseOsc` stays `OSC_TYPE_WAV`, because it has no `wav` list
of its own.

### B-33 `Core/Bank/Scene/Preset/presetManager.c:1086-1090` — MOD `preset_setSupplementalParameterInternal()`

Today it rejects `INSTANCE_OFFSET` binds and morphable rows. Make it:

```c
    /*
     * Selector rows use the supplemental path (S087 stage B).
     *
     * What:       a SELECTOR row is accepted although it is member-bound
     *             (INSTANCE_OFFSET). It is stored in the Normal image only and
     *             its value is also written to
     *             parameter_images.morph_interpolation[descriptor_index].
     * Why:        seq_restoreAutomatedParameters() rewrites every held row from
     *             morph_interpolation[]; the Morph sweep skips non-morphable
     *             rows, so the selector path must keep that cell equal to the
     *             stored value, or a trigger after a typ automation step would
     *             restore a stale type (S087 P0 §12.5).
     * Inputs:     scene, slot, descriptor index, value, force.
     * Outputs:    stored Normal value, morph_interpolation cell, runtime write
     *             (active Scene or forced).
     * Affiliates: preset_applyKitVoiceSelectors(); INSTRUMENT_PARAM_FLAG_SELECTOR.
     */
    if (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE)
        return 0u;
    if (descriptor->runtime.kind == INSTRUMENT_BIND_INSTANCE_OFFSET &&
        !(descriptor->flags & INSTRUMENT_PARAM_FLAG_SELECTOR))
        return 0u;
    ...store as today...
    if (descriptor->flags & INSTRUMENT_PARAM_FLAG_SELECTOR)
        instrument->parameter_images.morph_interpolation[descriptor_index] = value;
```

### B-34 `Core/Bank/Scene/Preset/presetManager.c` (before `preset_resetAndApplyKitVoiceImage()`, line 1807) — ADD `preset_applyKitVoiceSelectors()`; call it at line 1828

```c
/*
 * Apply a voice's selector rows before its Morph image (S087 stage B).
 *
 * What:       for every descriptor of the slot's instrument with
 *             INSTRUMENT_PARAM_FLAG_SELECTOR, writes the stored Normal value
 *             to morph_interpolation[i] and to the runtime
 *             (instrumentManager_writeRuntime(), always: the runtime was just
 *             rebuilt for this Scene).
 * Why:        S087 P0 §4.3: a selector is applied before the Morph image on
 *             every voice apply, so a typ change is in place before wave and
 *             position values reach the oscillator.
 * Inputs:     scene index, voice slot.
 * Outputs:    runtime selector state; morph_interpolation cells.
 * Accessors:  preset_resetAndApplyKitVoiceImage(), right after
 *             instrumentManager_resetRuntimeSlotFromScene() and
 *             preset_applyKitAudioRouting() (line 1827-1828), before
 *             presetMorph_applyVoiceNow*().
 * Affiliates: preset_setSupplementalParameterInternal() (B-33).
 */
static void preset_applyKitVoiceSelectors(uint8_t scene_index, uint8_t voice);
```

### B-35 `Core/Bank/Scene/Preset/presetManager.c:1745-1748` — MOD `preset_applyKitVoiceSupplementalInternal()`

In the loop, after `if (descriptor->flags & INSTRUMENT_PARAM_FLAG_MORPHABLE) continue;`,
ADD `if (descriptor->flags & INSTRUMENT_PARAM_FLAG_SELECTOR) continue;  /* S087 B: applied by preset_applyKitVoiceSelectors() */`.

**Check every other path that installs a voice image without
`preset_resetAndApplyKitVoiceImage()`.** The callers of
`preset_applyKitVoiceSupplemental*()` at lines 2028 and 2945, and the
Instrument load paths at 2830–2955, must apply selectors before Morph.
Where one does not, add a `preset_applyKitVoiceSelectors()` call there
and list it in the stage-B record.

### B-36 `tools/dsp_test/check_special_tags.py` — MOD

- In the key map (beside line 50), ADD the case:
  `if key == 'osc1_type': return {'IM_SPECIAL_OSC_TYPE'}` with the OSC1
  selector, following the existing pitch rows.
- The expected row count goes 159 → 163 (four `osc1_type` rows).
- Update the `special_tags` target's expectation in the Makefile (line 50).

### B-40 Host test `tools/dsp_test/test_osc_type.c` — ADD (new) + target `osc_type`

Extract from the real sources: the `OSC_TYPE_*` block (B-1),
`osc_setType()` (B-4), `instrumentManager_targetValid()` (B-7, with
stubs), `seq_rampDtypeOk()` (`sequencer.c:4202`), `menu.h` `enum
Datatypes` and the four `osc1_type` row initializers.

**Laws:**
1. `osc_setType()` clamps to `OSC_TYPE_SELECTABLE`; an unchanged type
   changes nothing (byte-compare the struct);
2. after a change, `positionApplied == position`, the freq cache is
   invalid, and `waveform` ≤ the new type's last entry;
3. the gate: `osc1_type` is valid for AUTOMATION and invalid for
   MODULATION; a non-morphable row without SELECTOR stays invalid;
4. `seq_rampDtypeOk(osc1_type)` is 0;
5. the row dtype's low nibble is `DTYPE_MENU` and its menu id is 4;
6. every `*_PARAM_OSC1_TYPE` equals the old count (appended last).

**Break proofs:** drop the SELECTOR branch in the gate (law 3 fails);
give the row `DTYPE_0B127` (law 4 fails).

### 3.1 Stage-B gates

- `osc_type`, `special_tags` (163/0), `stepauto_order`.
- `osc87_identity`: additions only.
- `elf87_check`: the loops are unchanged.
- `elf87_mixer_check`.
- **Hardware (§7.2).**

---

## 4. Stage C: sample-flash catalogue v2, the installer, the boot notice, the optimizer

**What lands:**
- **One modal install** reads `samples/` and `wavetables/` into catalogue
  v2. It replaces the v1 `samples` + `loops` installs.
- **The rest:** the boot notice, the RAM release (R-C1, R-C2, R-C3) and
  the optimizer fixes.
- **The v1 readers keep playing** samples from v2 data through a small
  read shim (C-5), so stage C can be tested before any new loop. They
  play one-shot, since `/loops` is retired.

### 4.1 The layout (re-derived; S087 P0 A11, A16, A24)

| Region | Address | Bytes |
|---|---|---:|
| Items (samples, then wavetables, in install order) | `0x080C0000` … | up to 1,302,528 |
| Catalogue v2 | `0x081FE000` – `0x081FFFFF` | 8,192 (6,176 used) |

**Header (32 B):**

| Field | Type |
|---|---|
| `magic` | `u32` "LXC2" = `0x3243584C`, programmed last |
| `version` | `u16` = 2 |
| `sample_entry_bytes` | `u8` = 28 |
| `wavetable_entry_bytes` | `u8` = 20 |
| `sample_count` | `u8` |
| `wavetable_count` | `u8` |
| `flags` | `u8` |
| `reserved0` | `u8` |
| `data_end` | `u32` |
| `reserved[4]` | `u32` |

**Sample entry (28 B):**

| Field | Type |
|---|---|
| `offset` | `u32`, 4-aligned |
| `frames` | `u32` |
| `loop_start` | `u32` |
| `loop_end` | `u32`, exclusive |
| `name[8]` | `char` |
| `gain_code` | `u8` |
| `flags` | `u8`: bit 0 = `smpl` loop present; bit 1 = bitmap reserved (A13) |
| `reserved[2]` | `u8` |

**Wavetable entry (20 B):**

| Field | Type |
|---|---|
| `offset` | `u32` |
| `frame_len` | `u16`, 16..2,048, any length (A16) |
| `frame_count` | `u8`, 1..128 |
| `bits` | `u8`, 8 or 16 |
| `name[8]` | `char` |
| `flags` | `u8`: bit 1 = bitmap reserved; bits 4–7 = mip level count reserved (A10) |
| `reserved[3]` | `u8` |

**Items:**
- **Sample:** `frames × int16`, then **two zero frames** (4 B), rounded up
  to 4.
- **Wavetable:** `frame_count × frame_len × bits/8`, rounded up to 4.
- **A future bitmap** (A13) goes right after its item, at offset + stored
  bytes rounded to 4, so no address field is needed.

### C-1 `Core/SampleRom/SampleMemory.h:72-133` — MOD (replace the v1 layout and API)

**Keep:**
- the file header (lines 1-63);
- `SAMPLE_ROM_START_ADDRESS`, `SAMPLE_FIRST_SECTOR`, `SAMPLE_PAGE_SIZE`
  and `SAMPLE_DISPLAY_NAME_LEN`;
- `sampleFlash.c` and `memtest.c` depend on these.

**ADD** the following, each with a comment block in the form below:
- the region constants of §4.1;
- `SAMPLE_CATALOGUE_*`;
- `SAMPLE_MAX_COUNT 128`, `WAVETABLE_MAX_COUNT 128`,
  `WAVETABLE_MAX_FRAMES 128`;
- `WAVETABLE_FRAME_MIN 16`, `WAVETABLE_FRAME_MAX 2048`;
- `SAMPLE_GAIN_STEPS_PER_OCTAVE 24`;
- `SAMPLE_MAX_FRAMES 524286u`, with an assert that
  `(SAMPLE_MAX_FRAMES + 2) << 12 < 2^31`;
- the three record `typedef`s, with size asserts (32/28/20) and a
  region assert (`catalogue end == 0x08200000`);
- the read API: `sampleMemory_init/refresh/catalogueState/getGeneration`,
  `sampleMemory_getNumSamples/getNumWavetables`,
  `sampleMemory_sampleEntry(index)` and
  `sampleMemory_wavetableEntry(index)` (const pointers into flash, or
  NULL), `sampleMemory_getDisplayName/getWavetableName`;
- `SAMPLE_FLASH_PTR()` (the host-test address map);
- `sampleMemory_gainFromCode()` (inline).

```c
/*
 * Sample flash catalogue v2 (S087 stage C).
 *
 * What:       the item region from SAMPLE_ROM_START_ADDRESS to
 *             SAMPLE_CATALOGUE_START_ADDRESS, and an 8 KB catalogue (32 B
 *             header, 128 x 28 B sample entries, 128 x 20 B wavetable entries)
 *             read in place, no RAM copy. The header magic is programmed
 *             last, so a catalogue is valid only after a completed install.
 * Why:        S087 P0 §5, A11: per-item loop points, gain code, wavetable
 *             geometry and flags for the bind; the v1 120-entry manifest and
 *             its RAM caches (5,040 B) go away.
 * Inputs:     SampleInstall.c writes it during the modal install.
 * Outputs:    read by the oscillator bind, the menu and InstrumentManager.
 * Accessors:  the read API below.
 * Affiliates: sampleFlash.c (erase/program), FILESYSTEM_SPEC.md,
 *             STORAGE_SRAM_MANIFEST.md, tools/dsp_test/test_catalogue.c.
 */
```

**C-1a (interim, removed in D-29):** keep `SampleInfo` (v1 shape) and
`sampleMemory_getSampleInfo()` / `sampleMemory_isLooped()` as shims:
- `getSampleInfo(i)` = `{frames, SAMPLE_FLASH_PTR(offset)}`, or the
  silence buffer;
- `isLooped(i)` = 0.

They exist only so the v1 loops compile and play v2 data until stage D.
Shim comment: `/* S087 C (interim): v1 readers on v2 data until stage D
(D-29 removes). */`.

DEL the v1 install API declarations (lines 120-131,
`sampleMemory_install*`) and `sampleMemory_setNumSamples`,
`sampleMemory_loadSamples`. Moved to SampleInstall.h.

### C-2 `Core/SampleRom/SampleMemory.c` — MOD (rewrite)

- **DEL** the caches and install tables at lines 26-46 (R-C2: −5,040 B):
  `sample_info_cache`, `sample_name_cache`, `sample_loop_cache`,
  `install_info`, `install_names`, `install_*`.
- **DEL** the v1 install functions at lines 159-325.
- **MOD** `sampleMemory_refresh()` (76), `sampleMemory_init()` (106) and
  the getters (112-158) to read the catalogue in place:
  - **validity:** the magic, the version, the entry sizes, counts ≤ max,
    and every counted entry's `[offset, offset + bytes)` inside the item
    region and 4-aligned;
  - **counts:** the leading valid entries are counted. A broken entry
    ends the count, so the oscillators can never read outside flash.
- **ADD** `sampleMemory_gainMantissa[24]`, the 2^(k/24) table.
- **ADD** `sampleMemory_catalogueState()` (EMPTY / V2 / STALE):
  - STALE = no magic and the word at `0x080C0000` is not `0xFFFFFFFF`
    (a v1 install);
  - EMPTY = no magic and erased.
- **Keep** `sample_generation` and its increment on every refresh. It is
  the bind's invalidation stamp.

### C-3 `Core/SampleRom/SampleInstall.h` / `SampleInstall.c` — ADD (new files)

**API, one comment block each:**
- `sampleInstall_acceptFormat(const uint8_t *fmt, uint32_t bytes, kind,
  sample_install_fmt_t *out)`:
  - PCM (tag 1) or extensible (0xFFFE with the PCM subformat);
  - 1–2 channels; 8/16/24-bit;
  - **any rate** (A15: rate conversion stays on the host; a sample at
    another rate plays transposed);
  - `stored_bits`: samples are always 16. Frames keep 8 when every frame
    of the table is 8-bit, otherwise 16.
- `sampleInstall_begin()`: erase sectors 7–11 (`sampleFlash_eraseAllSamples()`),
  reset the state.
- `sampleInstall_beginSample(name, frames)` / `writeFrames(src, bytes)` /
  `finishSample(loop_start, loop_end, has_loop)`:
  - converts each block (24 → 16 by `>> 8`; stereo → mono as `(L+R)/2`;
    8 → 16 for samples as `(u8 − 128) << 8`);
  - tracks the peak;
  - writes the 4 B zero pad, then the 28 B entry with the gain code.
  - **Caps a sample at `SAMPLE_MAX_FRAMES` = 524,286 points** (2¹⁹ − 2,
    ≈ 11.9 s). Longer files are truncated, and the result screen counts
    them. This keeps every Q19.12 sample phase below 2³¹, so the readers
    use signed compares (D-11).
- `sampleInstall_beginWavetable(name, frame_len, frame_count, bits)` /
  `writeFrame(...)` / `finishWavetable()`: frames are converted as for
  samples, except that 8-bit frames stay 8-bit when the table is 8-bit
  (an 8-bit frame in a 16-bit table becomes `<< 8`).
- `sampleInstall_abortItem()`: one unreadable file skips its item. The
  write cursor rolls back to the item start, which stays erased-unused,
  and the item gets no entry.
- `sampleInstall_commit()`: programs the header, magic last;
  `sampleMemory_refresh()`.
- `sampleInstall_gainCode(peak)`: the largest c in 0..255 with
  peak × 2^(c/24) ≤ 32,767 (0 for silence). It uses the mantissa table,
  no libm.
- `sampleInstall_bytesFree()`.

```c
/*
 * Modal sample/wavetable installer core (S087 stage C).
 *
 * What:       turns accepted WAV data into catalogue v2 items: format
 *             acceptance, block conversion (24->16 bit, stereo->mono, 8->16
 *             for samples), the zero pad after each sample, the gain code from
 *             the peak, loop points from smpl, one entry per finished item,
 *             the header with its magic last.
 * Why:        S087 P0 §5.2, A11, A15: the device converts depth and channels
 *             so most files install without the host tool; the catalogue is
 *             only valid after a complete install.
 * Inputs:     filesystem.c's install walker (fmt bytes, data blocks, smpl).
 * Outputs:    sample flash items and catalogue v2.
 * Accessors:  filesystem_installUserContentBlocking().
 * Affiliates: SampleMemory.h (layout), sampleFlash.c (program), the host
 *             tests test_wav_install.c and test_catalogue.c.
 * RAM:        a word carry, the write cursor and item bookkeeping: ≈ 32 B
 *             (part of R-C3).
 */
```

### C-4 `Core/SampleRom/sampleFlash.c` — no change expected

Check that `addr_in_sample_region()` (line 166) and the floor check
(151) accept `0x081FE000`–`0x081FFFFF`. They cover sectors 7–11, so the
check is a read only; record it.

### C-5 `Core/DSPAudio/Oscillator.c:138-158` — MOD `osc_updateSampleCache()` (interim)

It already reads `sampleMemory_getSampleInfo()` / `isLooped()`, which
C-1a provides. No edit, unless the shim types change. Removed in D-21.

### C-10 `Core/Hardware/SD/filesystem.c:23350-23372` and `:24477-24810` — DEL the v1 install / ADD the v2 install walker

**DEL:**
- `FS_SAMPLE_MANIFEST_MAX`, `fs_sample_manifest_t`,
  `sample_manifest[]` and `sample_manifest_count` (R-C1: −13,441 B);
- `filesystem_insertSampleManifest()` (24491),
  `filesystem_compareSampleNameStrict()` (24477) and
  `filesystem_scanSamples()` (24604);
- `filesystem_manifestCanFit()` (24592), `filesystem_installOneSample()`
  (24689) and `filesystem_installSampleFolderBlocking()` (24740);
- `filesystem_installSamplesBlocking()` (24802) and
  `filesystem_installLoopsBlocking()` (24807).

**Keep:**
- `sample_io_buf` (23373);
- the block-I/O helpers (23401-23530: `filesystem_blockOpen*`, including
  `filesystem_blockOpenDirLfn` for subfolders);
- the LFN helpers (24353-24476).

**MOD `filesystem_parseWav()` (24516):**
- walk **every** chunk to the end of the file, not only `fmt `/`data`;
- take `fmt ` (via `sampleInstall_acceptFormat()`), `data` (offset,
  bytes) and `smpl`. From `smpl`, take loop 1: `dwStart` at chunk +44,
  `dwEnd` at +48, inclusive, so `loop_end = dwEnd + 1`. Use it only when
  `start < end ≤ frames`.

New out-struct `fs_wav_info_t`, on the stack, ≈ 40 B.

**ADD the walker:**

```c
/*
 * Install samples/ and wavetables/ into catalogue v2 (S087 stage C).
 *
 * What:       one blocking pass, audio suspended by the caller:
 *             sampleInstall_begin(); walk samples/ (depth <= 3; in each
 *             folder: its .wav files in natural order, then its subfolders in
 *             natural order), each accepted file one sample item; walk
 *             wavetables/ the same way, each folder holding .wav files one
 *             wavetable (frames = those files in natural order; one frame
 *             length per table, 16..2,048 points, any length; mixed lengths
 *             skip the table); stop at 128 samples / 128 tables / 128 frames
 *             or when flash is full; sampleInstall_commit().
 * Why:        S087 P0 §5.2: natural order, folders, depth 3, wavetables as
 *             folders; one unreadable file skips its item only; /loops is
 *             retired (every sample can loop, A6).
 * Natural order without a name buffer: each folder level is read with a
 *             selection scan - repeat a directory scan, keep the smallest
 *             LFN (storage_naturalCompare()) greater than the last one taken,
 *             install it, until none is left. O(n^2) scans of one directory
 *             (n <= 128) during a modal install; no per-folder RAM array.
 * Inputs:     the card; the 512 B sample_io_buf.
 * Outputs:    catalogue v2; counts for the result screen; 1 on success.
 * Accessors:  menu_loadSamplesModal().
 * Affiliates: SampleInstall.c, storage_naturalCompare(), filesystem_parseWav(),
 *             filesystem_blockOpenDirLfn(), FILESYSTEM_SPEC.md.
 * RAM:        per depth level (3): last-taken LFN (80 B), best LFN (80 B),
 *             best short name (13 B), counters: ≈ 540 B static (R-C3).
 */
uint8_t filesystem_installUserContentBlocking(void);
```

**Display names:**
- a sample's name is its LFN without the extension;
- a table's name is its folder name;
- a leading numeric prefix followed by `_`, `-` or a space is stripped,
  then the name is cut to 8 characters (`storage_displayNameFromLfn()`,
  C-12).

### C-11 `Core/Hardware/SD/filesystem.h:1371-1372` — MOD

Replace the two v1 declarations with
`uint8_t filesystem_installUserContentBlocking(void);`. It carries the
C-10 block in short form.

### C-12 `Core/Hardware/SD/storageTypes.c/.h` — ADD `storage_naturalCompare()` and `storage_displayNameFromLfn()`

```c
/*
 * Natural-order name compare (S087 stage C).
 *
 * What:       compares two names case-insensitively, with every run of
 *             digits compared as a number ("w2" < "w10"; "01_cut" == "1_cut"
 *             ordered by the remaining text, then by length of the digit run).
 * Why:        S087 P0 §5.2: items, frames and folders install in natural
 *             order, so a numeric prefix orders them as the user expects.
 * Inputs:     two NUL-terminated names.  Outputs: <0, 0, >0.
 * Accessors:  the install walker; tools/dsp_test/test_natural_order.c.
 * Affiliates: tools/sampleoptimize.py natural_key() (the same order).
 */
int8_t storage_naturalCompare(const char *a, const char *b);
```

`storage_displayNameFromLfn(dst[8], lfn, strip_ext)` gets its own block:
prefix stripping as in C-10, padding with spaces.

### C-20 `Core/Menu/menu.c:1049-1110` — MOD `menu_loadSamplesModal()`

- **Replace** lines 1090-1096: the two installs, their messages and the
  `failedMessage` logic become one call,
  `ok = filesystem_installUserContentBlocking();`, with the messages
  `"Sample upload" / "Writing flash"`.
- **On failure:** `"Sample upload" / "failed"`.
- **On success:** show `"Sxxx Wxxx"`, the counts from
  `sampleMemory_getNumSamples()` / `getNumWavetables()`.
- **Keep** `menu_setNumSamples()` (line 1099) until D-44.
- **ADD** after `audioCodec_resume()`: `for (slot) instrumentManager_waveDomainChanged(slot);`
  (C-31).

Update the function's block: `S087 C: one install for samples/ and
wavetables/ (catalogue v2); /loops retired.`

### C-21 `main.c:582` — ADD the boot notice after `sampleMemory_init();`

```c
	/*
	 * Sample catalogue boot notice (S087 stage C).
	 *
	 * What:       when sampleMemory_catalogueState() is STALE (a v1 install
	 *             from an older firmware), show "Samples invalid" /
	 *             "reload samples" for about two seconds; EMPTY and V2 show
	 *             nothing.
	 * Why:        S087 P0 §5.2: v1 data is not readable as catalogue v2; the
	 *             user must reinstall from the card.
	 * Inputs:     the catalogue state.  Outputs: the LCD notice.
	 * Affiliates: SampleMemory.c sampleMemory_catalogueState();
	 *             menu_loadSamplesModal().
	 */
	boot_showSampleCatalogueNotice();
```

The function itself is ADDed beside the other `boot_show*` functions
(line ~437). It uses the LCD calls already used by
`flashImage_verifyAtBoot()`.

### C-31 Post-install refresh

After an install, every slot's wave domains are refreshed (C-20), and
the next block's bind sees the new generation (D-9).

### C-40 … C-47 `tools/sampleoptimize.py` — MOD (P0 §11.5, A10, A14, A18)

| ID | Where | Change |
|---|---|---|
| C-40 | docstring 1-80 | Rewrite for S087: catalogue v2, any frame length, no growth, the `smpl` rules, the RIFF writer |
| C-41 | constants 86-89 | `SAMPLE_FLASH_BYTES` = `0x081FE000 − 0x080C0000` = 1,302,528; DEL `STOCK_WAVES_BYTES` and its use in `main()` |
| C-42 | `optimise_wavetable()` 199-252 | **Never output more than the input** (A10): try shorter lengths, any n from 16 up (not only powers of two, A16); keep the smallest that passes; otherwise copy unchanged (verbatim). Mixed lengths: unify at the longest length only when that does not grow the table; else leave it unchanged and report that it will not install |
| C-43 | `process_samples()` 288-336 | Walk **subfolders** to depth 3 like the installer (`os.walk`, natural order). **Trim only after a `smpl` loop end** (A18); without `smpl`, no trim. 8/24-bit and stereo are no longer "non-conforming": the device converts them (A15). Only rates ≠ 44,100 are converted |
| C-44 | new `read_wav_chunks()` / `write_wav_riff()` | A small RIFF reader and writer that keeps `smpl` (rewriting `dwStart`/`dwEnd` after a rate change) and writes chunks in the order `fmt `, `smpl`, `data` |
| C-45 | every write path | Use C-44 for every file it writes. A file that needs no change is still byte-copied (no flag in S087; A13) |
| C-46 | `main()` 362-427 | The summary uses C-41; `--dry-run` is unchanged |
| C-47 | new `tools/dsp_test/test_sampleoptimize.py` | Runs the optimizer on a temporary card: a 600-point table stays 600 or shrinks, never grows; `smpl` survives a rewrite; a subfolder sample is processed; no trim without `smpl` |

Each changed Python function gets a docstring in the What / Why / Inputs
/ Outputs / Affiliates form.

### C-50 Host tests (new)

- **`tools/dsp_test/test_catalogue.c`** (target `catalogue`): defines
  `SAMPLE_FLASH_PTR` onto a RAM image of sectors 7–11, compiles the real
  `SampleMemory.c` and `SampleInstall.c` with a RAM `sampleFlash` stub,
  installs items, and reads them back. **Laws:**
  1. entry sizes and offsets;
  2. zero pad present;
  3. magic last (an aborted install reads EMPTY);
  4. a broken entry ends the count;
  5. STALE detection;
  6. gain code law;
  7. `abortItem` leaves no entry.

  **Break proof:** program the magic first; law 3 must fail.
- **`tools/dsp_test/test_natural_order.c`** (target `natural_order`):
  `storage_naturalCompare()` and `storage_displayNameFromLfn()` on a
  table that includes `01_cut.wav`, `AKWF_hvoice_0001.wav` … `_0104.wav`
  and `w2`/`w10`.
- **`tools/dsp_test/test_wav_install.c`** (target `wav_install`):
  `sampleInstall_acceptFormat()` and the block converters on synthetic
  8/16/24-bit mono/stereo data, and `smpl` parsing (via an extracted
  `filesystem_parseWav` core) with an inclusive `dwEnd`.

### 4.2 Stage-C gates

- `catalogue`, `natural_order`, `wav_install` and `test_sampleoptimize.py`
  pass, with break proofs.
- The build figures show the RAM release.
- `elf87_check`: oscillator loops unchanged.
- `elf87_mixer_check`.
- **Hardware (§7.3).**

---

## 5. Stage D: `smp`/`slp` readers, `pos` on samples, modulator `s`/`l` entries, v1 removal

**What lands:**
- **`typ` offers `wav smp slp`.**
- **Each sample type gets four copies:** plain, FM, sync and ring.
- **`pos` gains the sample rules D1–D9.**
- **The modulator list** gains `s01…` and `l01…` (A21, A24).
- **Drum `snc`:** a loop-wrap builder for `l` masters.
- **The v1 user-sample loops are removed,** and so is the dormant `wav`
  interpolation (A7, A19).

**All new per-sample code lives at the end of `Oscillator.c`, in an S087
section.** Two reasons:
- it reuses the file's static inline helpers `osc_syncPull()`
  (`:1017`) and `osc_ringSample()` (`:1049`) without moving them;
- `osc87_identity` allows additions.

### D-0 `Core/DSPAudio/Oscillator.c` (end) — ADD the S087 section macros

```c
/*
 * S087 ITCM placement (stage D/E).
 *
 * What:       .itcm.s087 for every S087 per-sample function; OSC_S087_LOOP adds
 *             noinline/noclone so each loop keeps its own symbol and LTO cannot
 *             fold it into an existing loop or re-clone a dispatcher.
 * Why:        S087 P0 §2 rules 2-3 (the S085/S086 placement rules): appending
 *             inside .itcm moved existing loops (S085 D7); the linker places
 *             *(.itcm.*) after *(.itcm) (STM32F765VIHx_FLASH.ld:150).
 * Accessors:  the S087 loops and typed renderers below.
 * Affiliates: OSC_S085_SECTION/LOOP (:983-990); link_budget.py.
 */
#define OSC_S087_SECTION __attribute__((section(".itcm.s087")))
#define OSC_S087_LOOP    OSC_S087_SECTION __attribute__((noinline, noclone))
```

### D-1 `Core/DSPAudio/Oscillator.h:133-137` — MOD: the v1 sample cache becomes the bind state (offsets 50–63 unchanged)

| Offset | Was | Becomes |
|---:|---|---|
| 50 | `sampleCacheIndex` | `bindIndex` (`uint8_t`): the catalogue index bound |
| 51 | `sampleCacheLooped` | `bindKind` (`uint8_t`): `OSC_BIND_*` |
| 52 | `sampleCacheGeneration` | `bindGeneration` (`uint32_t`): catalogue generation; 0 = unbound |
| 56 | `sampleCacheSize` | `bindFrames` (`uint32_t`): sample frames / wavetable frame length |
| 60 | `sampleCacheData` | `bindData` (`const void*`): item base in sample flash |

```c
	/*
	 * User-content bind (S087 stage D; was the v1 sample cache).
	 *
	 * What:       which catalogue item this oscillator plays and its base
	 *             address and length: bindKind (OSC_BIND_NONE/SMP/SLP, and
	 *             WTB8/WTB16 from stage E), bindIndex, bindFrames, bindData,
	 *             and bindGeneration (the sampleMemory_getGeneration() stamp;
	 *             0 = unbound). Loop points, gain and wavetable geometry are
	 *             read from the catalogue entry each block.
	 * Why:        osc_userBind() runs every block in osc_setFreq(); comparing
	 *             kind, index and generation lets it re-read flash only when
	 *             something changed (an install, a wav or typ change). The
	 *             fields reuse the v1 cache bytes, so OscInfo stays 72 B and
	 *             no offset used by an existing loop moves.
	 * Inputs:     osc_userBind().  Outputs: read by the S087 readers.
	 * Accessors:  osc_userBind(), the typed and list renderers,
	 *             osc_syncClockInc(), the typed pos helpers.
	 * Affiliates: SampleMemory.h catalogue v2; S087 P0 §12.4.
	 */
```

Rename every use. At this point the only remaining users are the v1
functions, which D-20 … D-22 delete.

### D-2 `Core/DSPAudio/Oscillator.h` — MOD `OSC_TYPE_SELECTABLE` to 3

`/* S087 D: wav smp slp */`.

### D-3 `Core/DSPAudio/Oscillator.h` (after B-1) — ADD the sample constants

```c
/*
 * Sample reader constants (S087 stage D).
 *
 * What:       OSC_SAMPLE_PHASE_SHIFT 12 (Q19.12 phase: point index << 12 +
 *             fraction); OSC_BIND_NONE/SMP/SLP (0/1/2; WTB8/WTB16 = 3/4 in
 *             stage E); OSC_POS_SAMPLE_SPEED_CAP 4 (D5: a pos glide moves the
 *             playhead at most 4x the playback speed, at least one step per
 *             block); OSC_FM_SAMPLE_SHIFT 9 (the FM read offset: the same
 *             fraction of a 1,024-point nominal period as the table loops'
 *             << 19 on a 2^32 cycle and v1's << 14 on << 17).
 * Why:        S087 P0 D1, D5, A17 addendum (amt means the same on every
 *             type). SAMPLE_MAX_FRAMES (SampleMemory.h) keeps every phase
 *             < 2^31, so the readers use signed compares.
 * Accessors:  the S087 readers, osc_userBind(), the typed pos helpers.
 * Affiliates: SampleMemory.h SAMPLE_MAX_FRAMES; test_osc_sample.c.
 */
```

### D-4 `Core/DSPAudio/Oscillator.h` (after B-3) — ADD declarations, each with its block (the bodies are D-9 … D-19)

- `void osc_userBind(OscInfo* osc);`
- `uint8_t osc_userCount(uint8_t type);`: the number of entries for a
  type (wav: 8; smp/slp: samples; wtb: tables).
- `uint8_t osc_userListCount(void);`: the modulator list length,
  `min(8 + 2S + W, 128)`; W is counted from stage E.
- `void osc_renderTyped(OscInfo*, int16_t* buf, uint8_t size, float gain);`
- `void osc_renderTypedFm(OscInfo*, const int16_t* mod, int16_t* buf, uint8_t size, float gain);`
- `void osc_renderTypedSync(OscInfo*, const uint32_t* over, uint32_t clockInc, int32_t depthQ30, int16_t* buf, uint8_t size);`
- `void osc_renderTypedRing(OscInfo*, const int16_t* modBuf, int16_t* buf, uint8_t size, int32_t depthQ15, int32_t depthStepQ15);`
- `void osc_renderUserList(OscInfo*, int16_t* buf, uint8_t size, float gain);`
- `uint32_t osc_typedTriggerPhase(OscInfo* osc);`
- `void osc_typedPositionApply(OscInfo* osc, uint8_t bend);` /
  `void osc_typedPositionRestore(OscInfo* osc);`
- `uint8_t osc_syncMasterIsLoop(const OscInfo* master);` /
  `uint32_t osc_buildLoopSyncList(OscInfo* master, uint32_t pos, uint32_t* over, uint8_t size);`

### D-5 `Core/DSPAudio/Oscillator.h:545-584` — MOD `osc_positionBendApply()` / `osc_positionBendRestore()`

**Insert a typed branch at the top of each:**
`if (osc_isTyped(osc)) { osc_typedPositionApply(osc, bend); return; }`,
and the same with `Restore`. **The `wav` path is unchanged.** The
modulators never call them.

Add to both blocks:
`S087 D: a typed osc1 (smp/slp/wtb) takes the out-of-line rules (D3-D5,
D8; wtb frame position, E-7).`

**These helpers are inlined into `mixer_calcNextSampleBlock()`.** Run
`elf87_mixer_check` after this change: the per-sample loops must be
unchanged (S086 F1).

### D-6 `Core/DSPAudio/Oscillator.h:506-513` — MOD `osc_positionTrigger()`

Add a guard: `if (osc_isTyped(osc)) return 0u;  /* S087 D: typed start: osc_typedTriggerPhase() */`.

### D-7 `Core/DSPAudio/Oscillator.c:1758-1811` — MOD `osc_setFreq()`

**Insert before the cache test:**

```c
	/*
	 * Typed osc1 and modulator list entries (S087 stage D).
	 *
	 * What:       osc1 of type wav with a wav value past the stock list is
	 *             clamped to OSC_STOCK_LAST (P0 r8: the value is used as it is,
	 *             clamped to something that exists); a typed osc1, or a
	 *             modulator whose wav value is past the stock list, binds its
	 *             catalogue item (osc_userBind()) and returns: osc_userBind()
	 *             also sets phaseInc. Everything else continues as before.
	 * Why:        every writer of wav/typ (Morph, LFO, velocity, automation,
	 *             menu) runs before this call in the block (mixer.c:866-882),
	 *             so the render always sees a bound item or a stock wave.
	 * Affiliates: osc_userBind(); OSC_TYPE_*.
	 */
	if (osc->oscType == OSC_TYPE_WAV && osc->waveform > OSC_STOCK_LAST)
		osc->waveform = OSC_STOCK_LAST;
	if (osc_isTyped(osc) ||
	    (osc->oscType == OSC_TYPE_MODLIST && osc->waveform >= OSC_SAMPLE_START)) {
		osc_userBind(osc);
		return;
	}
```

**Also:**
- **The `default:` case (1798-1800)** now handles only an impossible value.
  Map it to `osc_calcWavetableFreqValue()` with the comment
  `/* S087 D: unreachable - see the typed branch above */`.
- **DEL** `osc_calcUserSampleFreqValue()` (1753-1756), now unused.
- **Keep** `osc_calcSampleFreqValue()` for CRASH.
- **Keep the cache:** the typed path does not update `freqCache*`. A
  later return to `wav` sees `freqCacheValid = 0`, which `osc_setType()`
  set.

### D-8 `Core/DSPAudio/Oscillator.c` (S087 section) — MOD `osc_setType()` sample branch; ADD `osc_userCount()`, `osc_userListCount()`

- **The waveform clamp** uses `osc_userCount(type)`.
- **The phase re-anchor:**
  - into `smp`/`slp`: `osc->phase = osc_typedTriggerPhase(osc)` (the
    `pos` start, D1);
  - from `smp`/`slp` into `wav`: `osc->phase = 0u +
    osc_positionTrigger(osc)` (the `pos` phase);
  - between `wav` and `wtb`: unchanged.
- **The bind:** `osc->bindGeneration = 0` forces a re-read.

### D-9 `Core/DSPAudio/Oscillator.c` (S087 section) — ADD `osc_userBind()`

```c
/*
 * Bind the oscillator's catalogue item for this block (S087 stage D).
 *
 * What:       resolves (kind, index):
 *             - typed osc1: kind from oscType (smp/slp; wtb in stage E),
 *               index = min(waveform, osc_userCount(type) - 1);
 *             - modulator: entry = min(waveform, osc_userListCount() - 1) -
 *               8; < S: smp index; < 2S: slp index entry - S; else wtb
 *               (stage E);
 *             - nothing installed: kind NONE (the renderers output silence and
 *               still advance the phase).
 *             When kind, index or sampleMemory_getGeneration() differ from
 *             the bind: reads the entry, sets bindData/bindFrames, and
 *             re-anchors (D7): positionApplied = position; a smp phase past
 *             the new end is clamped to the end; a slp phase past the loop end
 *             is wrapped into the loop once (one modulo at the change only).
 *             Every block: phaseInc = the sample law f x 2^22 / REAL_FS
 *             (original speed at f = REAL_FS / 1,024, as v1), f = freq x
 *             pitchMod x modNodeValue.
 * Why:        S087 P0 §6.3, D7, A23, A24: one bind serves osc1 and the
 *             modulators; a reinstall or a wav change can never leave a
 *             pointer outside the catalogue; the clamp is the "use as is,
 *             clamped to something that exists" rule.
 * Inputs:     osc (oscType, waveform, freq state, position); the catalogue.
 * Outputs:    bind fields, phaseInc, positionApplied (on change), phase (on
 *             change).
 * Cost:       a few compares per block; flash reads and a divide only on a
 *             change.
 * Accessors:  osc_setFreq() (every block, before the render).
 * Affiliates: SampleMemory.c read API; osc_userCount(), osc_userListCount();
 *             test_osc_sample.c.
 */
void osc_userBind(OscInfo* osc);
```

### D-10 `Core/DSPAudio/Oscillator.c` (S087 section) — ADD `osc_typedTriggerPhase()`, `osc_typedPositionApply()`, `osc_typedPositionRestore()`

```c
/*
 * Typed trigger start and pos rules (S087 stage D: D1-D5, D8; wtb E-7).
 *
 * What:       osc_typedTriggerPhase(): positionApplied = position; smp/slp:
 *             start = ((position x frames) >> 7) << 12 (D1); for slp a start
 *             at or past loop_end is clamped to loop_start (A5); returns the
 *             start. (wtb returns the table-wave start chosen by the engine,
 *             E-7.)
 *             osc_typedPositionApply(): smp/slp: d = position -
 *             positionApplied (linear, -127..127, D3); capped so |d| x frames
 *             <= OSC_POS_SAMPLE_SPEED_CAP x phaseInc, at least 1 (D5); smp:
 *             d = 0 once the one-shot has ended (phase at its end; D4, no
 *             revival); d is reduced so the block-end phase stays >= 0 (D4);
 *             positionApplied += d; when `bend`, phaseInc += d x frames and
 *             positionBendStep = d, else positionBendStep = 0 (snc).
 *             osc_typedPositionRestore(): phaseInc -= positionBendStep x
 *             bindFrames; positionBendStep = 0.
 * Why:        S087 P0 §6.5: one step over one 32-sample block is exactly
 *             d x frames per sample in Q19.12 (frames/128 points x 4,096 /
 *             32); bindFrames cannot change inside a block, so the restore is
 *             exact without a live local (D8, S086 F1).
 * Inputs:     osc (bound).  Outputs: phase/phaseInc/positionApplied/
 *             positionBendStep.
 * Accessors:  osc_positionBendApply()/Restore() (typed branch, D-5); the
 *             engines' typed trigger branch (D-30 ... D-33).
 * Affiliates: OSC_POS_SAMPLE_SPEED_CAP; test_osc_sample.c laws L8-L11.
 */
```

### D-11 … D-18 `Core/DSPAudio/Oscillator.c` (S087 section) — ADD the eight sample loops (`OSC_S087_LOOP`)

| ID | Function | Per-sample body (Q19.12, signed) | Budget (static) |
|---|---|---|---:|
| D-11 | `osc_smpPlainBlock` | `i = ph >> 12; a = d[i]; b = d[i+1]` (pad zeros at end, end+1); out = a + frac × (b − a); `buf = out × g`; `ph += inc; ph = ph > endPh ? endPh : ph` | ≤ 33 (ceiling 40–43) |
| D-12 | `osc_slpPlainBlock` | `n = (i+1 == loopEnd) ? loopStart : i+1` (the seam reads the loop start, A6); `ph += inc; ph = ph >= loopEndPh ? ph − loopLenPh : ph`; inc reduced modulo the loop length once per block (A5) | ≤ 35 (ceiling 40–43) |
| D-13 | `osc_smpFmBlock` | `off = ssat(mod × fmMod) << 9`; `rp = ph + off` clamped to `[0, endPh]` | ≤ 45 (ceiling 48–51) |
| D-14 | `osc_slpFmBlock` | `rp` wrapped once each way into the loop, then clamped to `[0, loopEndPh − 1]` (A5) | ≤ 47 (ceiling 48–51) |
| D-15 | `osc_smpRingBlock` | D-11 + the ring law (`osc_ringSample()`, depth ramp) | ≤ 41 (ceiling 46–50) |
| D-16 | `osc_slpRingBlock` | D-12 + the ring law | ≤ 43 (ceiling 46–50) |
| D-17 | `osc_smpSyncBlock` | D-11 + `rel = ph − startPh; rel = osc_syncPull(rel, over[i], ratio, depth); ph = startPh + rel` (D9: reset target = `pos` start; a finished one-shot restarts on a reset) | ≤ 46; section ≤ Drum `fm` ceiling (A1) |
| D-18 | `osc_slpSyncBlock` | D-12 + the same | ≤ 48; section ≤ Drum `fm` ceiling |

**Rules for all eight:**
- **Gain is set once per block.** `g` = the gain argument ×
  `sampleMemory_gainFromCode(entry->gain_code)`; the ring and sync copies
  run at gain 1 × the code.
- **Silence:** kind NONE writes zeros and advances `ph` by `inc` with the
  same wrap or clamp, at constant cost.
- **The loop's start block is in each function body:**

```c
/*
 * <name>: one <smp|slp> block, <plain|FM carrier|ring carrier|sync slave>
 * (S087 stage D).
 *
 * What:       <the row's body, in words>; reads the bound item (bindData,
 *             bindFrames) and its entry (loop points, gain) once per block.
 * Why:        S087 P0 §6.2-§6.3: install-time padding and a fixed loop
 *             region remove the v1 per-sample end/loop tests; a separate copy
 *             per use keeps every role at constant cost (rule 2).
 * Inputs:     osc (bound), <mod/over/depth>, buf, size, gain.
 * Outputs:    buf; osc->phase (and osc->output where the role needs it).
 * Cost:       budget in the table (armcheck-s087, against the frozen v1
 *             figure for the same role).
 * Accessors:  osc_renderTyped*() / osc_renderUserList().
 * Affiliates: osc_userBind(); SampleMemory.h; test_osc_sample.c.
 */
```

### D-19 `Core/DSPAudio/Oscillator.c` (S087 section) — ADD the typed renderers and `osc_renderUserList()` (`OSC_S087_SECTION`, `noinline, noclone`)

```c
/*
 * Typed renderers (S087 stage D; wtb in E-6).
 *
 * What:       osc_renderTyped / Fm / Sync / Ring switch once per block on
 *             osc->bindKind and call the matching S087 loop (smp/slp; silence
 *             for NONE). osc_renderUserList() is the modulator path: plain
 *             loops only (a modulator is never an FM carrier, ring carrier or
 *             sync slave), reached from the existing dispatcher's user branch
 *             (D-23).
 * Why:        S087 P0 A2: the engines call these instead of the existing
 *             dispatchers when osc_isTyped(); one entry per role keeps the
 *             call sites non-constant and the old dispatchers untouched
 *             except for the stage-D removals.
 * Inputs:     as the loops.  Outputs: buf, phase.
 * Accessors:  the four engines (D-30 ... D-37); calcNextOscSampleBlock()
 *             default branch (D-23).
 * Affiliates: osc_userBind(); OSC_S087_LOOP.
 */
```

### D-20 … D-28 Removals and dispatcher edits in `Core/DSPAudio/Oscillator.c` / `.h` (the allow-listed identity changes)

| ID | Where (`0e2d61dd`) | Action |
|---|---|---|
| D-20 | `Oscillator.c:788-895` `calcUserSampleOscFmBlock()`, `calcUserSampleOscBlock()` | DEL |
| D-21 | `Oscillator.c:138-158` `osc_updateSampleCache()` | DEL |
| D-22 | `Oscillator.c:1576-~1635` `calcUserSampleOscRingBlock()` (with its block) | DEL |
| D-23 | `Oscillator.c:640-644` `calcNextOscSampleBlock()` `default:` | MOD to `osc_renderUserList(osc, buf, size, gain); break;` with `/* S087 D: modulator list entries (osc_setFreq() clamps osc1 wav values) */` |
| D-24 | `Oscillator.c:502-503` `calcNextOscSampleFmBlock()` `default:` | MOD: zero-fill `buf` (unreachable: only osc1, whose wav value `osc_setFreq()` clamps; see D-7) with a comment saying so |
| D-25 | `Oscillator.c:1721-1724` ring dispatcher `default:` | MOD: as D-24 |
| D-26 | `Oscillator.c:1140-1143` `osc_renderPlainWave()` `default:` | MOD: as D-24 |
| D-27 | `Oscillator.c:1338-1340` `osc_renderSyncWave()` `default:` | MOD: NOISE only (rename the comment to `/* NOISE: unsynced */`) |
| D-28 | `Oscillator.h:221-223` | DEL the `calcUserSampleOsc*Block` declarations |

**Dormant `wav` interpolation (A7, A19), removed in the same commit:**
- **DEL** `Oscillator.c:123-297`: `osc_interp_a/b` (R-D1),
  `osc_clearWaveInterp`, `osc_waveInterpActive`,
  `osc_evalWaveAtPhase*`, `calcPeriodicInterp*` (plain and FM, block and
  scalar).
- **DEL** the `if (osc_waveInterpActive(osc)) {…}` blocks at 466-469,
  511-514, 605-608 and 650-653.
- **DEL** the sync interpolation at 1350-1404 (`calcPeriodicInterpSyncBlock`
  and its block) and its use at 1409-1412.
- **DEL** the ring interpolation at 1630-1690 and its use at 1692-1695.
- **Keep** `OscInfo.waveInterpNext/Frac/Generation`, the layout. Change
  their comments to `S087: unused since stage D; kept for the
  OscInfo layout (offsets used by existing loops follow them)`.

**D-29 `SampleMemory.h/.c`: DEL the C-1a shims** (`SampleInfo`,
`getSampleInfo`, `isLooped`).

### D-30 … D-37 The engines

**Trigger sites.** Each typed branch is ADDed before the existing
start-phase chain:
- Drum `DrumVoice.c:205-235`;
- Snare `Snare.c:144-171`;
- Cymbal `CymbalVoice.c:152-180`;
- HiHat `HiHat.c:166-193`.

```c
		/*
		 * Typed osc1 start (S087 stage D).
		 *
		 * What:       a typed osc1 (smp/slp; wtb in stage E) starts at
		 *             osc_typedTriggerPhase(): the pos start point (D1), no
		 *             transient offset; wav keeps the chain below unchanged.
		 * Why:        the chain below tests waveform, which is a sample index
		 *             under smp/slp; its table/sine start phases do not apply.
		 * Affiliates: osc_typedTriggerPhase(); D-6.
		 */
		if (osc_isTyped(&voice->osc))
			voice->osc.phase = osc_typedTriggerPhase(&voice->osc);
		else {
			/* existing chain and the osc_positionTrigger() add, unchanged */
		}
```

**Render sites.** Wrap each osc1 call as `if (osc_isTyped(&voice->osc))
<typed call>; else <existing call>;`:

| ID | Site | Existing call | Typed call |
|---|---|---|---|
| D-34 | `DrumVoice.c:461` (`mix`) | `calcNextOscSampleBlock(&voice->osc, …)` | `osc_renderTyped(&voice->osc, buf, size, 1.f - fmModAmount)` |
| D-34 | `DrumVoice.c:474` (`snc`) | `calcNextOscSampleSyncBlock(…)` | `osc_renderTypedSync(…same args…)` |
| D-34 | `DrumVoice.c:501` (`rng`) | `calcNextOscSampleRingBlock(…)` | `osc_renderTypedRing(…)` |
| D-34 | `DrumVoice.c:511` (`fm`) | `calcNextOscSampleFmBlock(…)` | `osc_renderTypedFm(…)` |
| D-35 | `Snare.c:280` | `calcNextOscSampleBlock(&voice->osc, …)` | `osc_renderTyped(…)` |
| D-36 | `CymbalVoice.c:293` | `calcNextOscSampleFmBlock(&voice->osc, …)` | `osc_renderTypedFm(…)` |
| D-37 | `HiHat.c:311` | as Cymbal | as Cymbal |

**The comment on each wrapped call:**
`/* S087 D (A2): a typed osc1 renders through the S087 renderers; the
existing dispatcher is untouched for wav. */`. Each engine's render
block (e.g. `DrumVoice.c:431-451`) gets one paragraph:
`S087 D: typed osc1 branch (osc_isTyped()); one per-block test, no state
kept across the render (S086 F1).`

**Drum snc master (D-38, `DrumVoice.c:469-474`).** The `snc` master
picks its builder:

```c
		/*
		 * snc master builder by kind (S087 stage D, A12).
		 *
		 * What:       an l-entry master (osc_syncMasterIsLoop()) resets the
		 *             slave each time its loop wraps (osc_buildLoopSyncList(),
		 *             master position in Q19.12, kept in voice->syncClock);
		 *             every other master uses the periodic clock as before
		 *             (s entries act as noise: osc_syncClockInc() scales the
		 *             Q19.12 increment << 10 to one reset per nominal
		 *             1,024-point period).
		 * Affiliates: osc_buildLoopSyncList(), osc_syncClockInc() (D-39).
		 */
```

**D-39 `Oscillator.c:1156-1166` — MOD `osc_syncClockInc()`.**
- **Replace** the `waveform >= OSC_SAMPLE_START` test (the v1 `<< 17`
  case, scaled `<< 5`) with: a MODLIST master bound to SMP or SLP scales
  the Q19.12 increment `<< 10`, clamped first to `0x003FFFFF`.
- **CRASH keeps its `<< 5`.**
- Update the comment block at lines 268-288 in `Oscillator.h`.

**ADD `osc_buildLoopSyncList()`** (`OSC_S087_LOOP`), with a block:
- **per sample:** `pos += inc; over[i] = pos >= loopEndPh ? (pos −=
  loopLenPh, pos − loopStartPh) : NONE;`;
- **once per block:** the master increment is reduced modulo the loop
  length;
- **returns** the position.
- **Its slave ratio** uses the master's Q19.12 increment as `clockInc`.

### D-40 `Core/DSP/Instruments/InstrumentManager.c` — MOD `instrumentManager_waveDomainMax()` (B-11): typed

- `osc1_wave` → `osc_userCount(osc->oscType) − 1`;
- modulator wave rows → `osc_userListCount() − 1`;
- an empty count → 0.

### D-41 `Core/Menu/menu.c` — MOD the wave cells (cell-aware count and names)

- **ADD `instrumentManager_waveCellCount(slot, descriptor)`** to
  `InstrumentManager.c/.h`. It returns `waveDomainMax + 1` and has a
  block.
- **`menu_clampCellValue()` (`menu.c:6677-6683`), DTYPE_MENU branch:**
  when the menu id is `MENU_WAVEFORM` and the cell is an instrument cell,
  `n = instrumentManager_waveCellCount(cell->slot, cell->descriptor)`.
- **`getMaxEntriesForMenu()` (`:11466`):** `MENU_WAVEFORM` returns the
  stock count only (`waveformNames[0][0]`), for callers without a cell.
- **`menu_formatSampleShortName()` (`:11476-11489`)** becomes
  `menu_formatUserShortName(char prefix, uint8_t index, char *buf)`: `s`,
  `l` or `w`; above 99 the form is `sA0` …. Block:
  `S087 D: s/l/w codes (P0 §4.3, §12.4)`.
- **The edit-line display (`:14101-14111`)** becomes
  `menu_formatWaveCell(&cell, value, &editDisplayBuffer[1][0])`:
  - osc1 by type: wav → stock name; smp → name + `sNN`; slp → name +
    `lNN`; wtb (stage E) → name + `wNN`;
  - modulators by list entry: stock → name; `s`/`l`/`w` → the item name
    (8 characters, left) + the code (right).
- **The automation display, `menu_formatAutomationValue3()`
  (`:12985-13001`):** the `MENU_WAVEFORM` branch formats with the
  target's slot (`instrumentParam_slot(target)`) through
  `menu_formatWaveCell`. Pass the target ID from its callers (search
  `menu_formatAutomationValue3(`).
- **Automation maximum (`menu_automationValueMax()`):** wave rows → 127,
  unchanged (the bind clamps).

### D-42 `Core/DSPAudio/modulationNode.c:771-800` — DEL the `wav` interpolation claim (A7, A19)

The `if (osc && modNode_waveInterpActiveCount < OSC_WAVE_INTERP_MAX_ACTIVE) {…}`
block goes; the plain `*dst = (uint8_t)modulated;` remains. Update the
comment block at lines 59-65:

`S087 D: the two slots now serve wavetable frame blending
(modNode_interpClaim(), stage E) and, from S088, the run mode; a wav LFO
steps.`

`modNode_getMaxWaveformIndex()` (`:76-84`): DEL it (with its `.h:242`
declaration) once D-40 no longer calls it. Check with `grep`.

### D-43 `config.h:876-881` — MOD the comment of `OSC_WAVE_INTERP_MAX_ACTIVE`

The value stays 2. The new comment:

`S087: the two interpolation slots per block, switched by the global
PAR_OSC_WAVE_INTERP; used by wavetable frame blending (stage E) and, from
S088, the oscillator run mode; a claimant without a slot steps.`

### D-44 `Core/Menu/menu.c:1658`, `:18944`, `:1099`; `main.c:663` — DEL `menu_numSamples` / `menu_setNumSamples()`

The counts come from the catalogue (D-41).

### D-45 Host test `tools/dsp_test/test_osc_sample.c` — ADD (new) + target `osc_sample`

**Extraction:**
- the S087 section of `Oscillator.c` (loops, bind, pos rules);
- `osc_syncPull`, `osc_ringSample`;
- the `OSC_TYPE_*` / `OSC_BIND_*` / sample constants;
- `SampleMemory.c` over a RAM catalogue image (the C-50 harness).

**Laws:**

| # | Law |
|---|---|
| L1 | `smp` plays the item exactly, then holds 0 from the end (pad), at any increment |
| L2 | `slp` with a non-zero-crossing seam: the read across the seam interpolates last → loop start, never 0 (A6) |
| L3 | `slp` lead-in, then loop; a start past the loop end clamps to the loop start (A5) |
| L4 | `slp` with inc ≥ the loop length wraps correctly (the per-block reduction) |
| L5 | the FM copies: offsets are clamped (`smp`) or wrapped then clamped (`slp`); never a read outside `[0, frames + 1]` |
| L6 | the FM depth scale matches the table FM fraction (A17 addendum) |
| L7 | ring copy = plain × the ring law; sync copy resets to the `pos` start (D9) |
| L8 | D1: the start point for `pos` 0/64/127 |
| L9 | D3/D5: linear bend, capped; at least 1 step |
| L10 | D4: no negative phase; a finished one-shot is not revived by `pos` |
| L11 | D8: Apply then Restore leaves `phaseInc` exact |
| L12 | the bind: a reinstall with fewer items clamps (A23); the modulator list maps 8 / 8+S / 8+2S correctly (A24), and the cut-off in order `s`, `l`, `w` at 128 entries |
| L13 | the typ switch clamps `waveform` and re-anchors the phase (P0 r8, §12.5) |

**Break proofs:**
- the seam reads 0 (L2 fails);
- the end clamp removed (L1 fails);
- the D4 clamp removed (L10 fails).

### D-46 `tools/dsp_test/armcheck/s087.c` — fill the stage-D part

Compile D-11 … D-18 and `osc_buildLoopSyncList` beside the frozen v1
figures (0-4). Report standalone and linked (`fpseq.py` on the ELF
symbols). **Gate:** each loop ≤ its ceiling in the table above; each
voice section ≤ its frozen worst case (A1).

### 5.1 Stage-D gates

- `osc_sample`, `osc_type` (updated for `OSC_TYPE_SELECTABLE` 3),
  `catalogue`, `stepauto_order`.
- `osc87_identity`: additions plus the D-20 … D-28 allow-list only.
- `elf87_check`: the existing table/sine/crash/noise loops are unchanged.
  The dispatchers change, as allow-listed.
- `elf87_mixer_check`: the existing per-sample loops are unchanged.
- `armcheck-s087` within budget.
- `special_tags`.
- **Hardware (§7.4).**

---

## 6. Stage E: `wtb` reader, frame blending through the two slots, modulator `w` entries

**What lands:**
- **`typ` offers `wtb`.**
- **One-frame copies and blend copies** for each role, 8- and 16-bit.
- **`pos` = frame position.**
- **A blend takes one of the two interpolation slots** (sticky claims,
  A8) and steps when none is free (A7). The global toggle switches it.
- **The modulator list** gains `w01…`, the first frame, always looped.

### E-1 `Core/DSPAudio/Oscillator.h` — MOD `OSC_TYPE_SELECTABLE` to 4

`/* S087 E: wav smp slp wtb */`.

### E-2 `Core/DSPAudio/Oscillator.h` (after B-2) — ADD `interpStamp` (byte 35)

```c
	/*
	 * Interpolation slot stamp (S087 stage E).
	 *
	 * What:       low 8 bits of the block generation in which this oscillator
	 *             last held one of the two interpolation slots; 0 = none.
	 * Why:        sticky claims (S087 P0 A8): a holder keeps its slot from
	 *             block to block, so it does not flip between blending and
	 *             stepping. Byte 35 is padding before waveInterpFrac, so
	 *             OscInfo stays 72 B.
	 * Inputs:     modNode_interpClaim(); osc_setType() clears it.
	 * Outputs:    read by modNode_interpClaim().
	 * Affiliates: modNode_resetTargets(); OSC_WAVE_INTERP_MAX_ACTIVE.
	 */
	uint8_t		interpStamp;
```

ADD an assert: `offsetof(OscInfo, interpStamp) == offsetof(OscInfo, oscType) + 1u`.
The `waveInterpFrac` and size asserts must still pass.

### E-3 `Core/DSPAudio/modulationNode.c:66-68`, `:432-441` — ADD the sticky claim (R-E1)

- **ADD** `static INCCMZ uint8_t modNode_interpReserved = 0u;` and
  `static INCCMZ uint8_t modNode_interpGrantedLast = 0u;` after line 68.
- **MOD `modNode_resetTargets()` (432):** after the generation
  increment, `modNode_interpReserved = modNode_waveInterpActiveCount;`
  (last block's grants), then the existing
  `modNode_waveInterpActiveCount = 0u;`.
- **ADD `modNode_interpClaim()`**, declared in `modulationNode.h` beside
  line 229:

```c
/*
 * Claim one of the two interpolation slots for this block (S087 stage E).
 *
 * What:       returns 1 and stamps osc->interpStamp with this block's
 *             generation when a slot is granted; 0 otherwise. Rules:
 *             - the global toggle (PAR_OSC_WAVE_INTERP,
 *               modNode_getWaveInterpEnabled()) off: 0;
 *             - a holder (interpStamp == last generation) takes one of the
 *               slots reserved for last block's holders;
 *             - a newcomer is granted only if grants this block + unclaimed
 *               reservations < OSC_WAVE_INTERP_MAX_ACTIVE.
 *             A holder that stops claiming (type change, slot rebind) frees
 *             its reservation after one block.
 * Why:        S087 P0 A3, A7, A8, A17: wavetable frame blending must use
 *             the two slots and step without one; sticky grants avoid
 *             block-rate flipping; S088's run mode will share the same slots
 *             (ongoing crossfades are holders, new ones newcomers).
 * Inputs:     osc; the block generation and counters.
 * Outputs:    grant flag; interpStamp; counters.
 * Accessors:  osc_renderTyped*() (wtb branch).
 * Affiliates: modNode_resetTargets(); OscInfo.interpStamp; config.h
 *             OSC_WAVE_INTERP_MAX_ACTIVE.
 */
uint8_t modNode_interpClaim(OscInfo* osc);
```

### E-4 `Core/DSPAudio/Oscillator.c` — MOD `osc_userBind()`, `osc_userCount()`, `osc_userListCount()`: the wtb kinds

- **Kinds:** `OSC_BIND_WTB8` 3 and `OSC_BIND_WTB16` 4, from the entry's
  `bits`.
- **Fields:** `bindFrames = frame_len`; `bindData` = frame 0.
- **The index:**
  - typed osc1: `min(waveform, tables − 1)`;
  - modulator `w` entry: list index − 8 − 2S, and the modulator always
    plays frame 0.
- **The increment** is the cycle law (`freq2PhaseIncr()`, 2³² per cycle,
  as Sine).
- **The counts:** `osc_userCount(OSC_TYPE_WTB)` = tables;
  `osc_userListCount()` now counts W.

Add to the D-9 block:
`S087 E: wavetables (any frame length, A16); modulator w entries play
frame 0 (A24).`

### E-5 `Core/DSPAudio/Oscillator.c` (S087 section) — ADD the wavetable loops (`OSC_S087_LOOP`)

**One source definition per role,** expanded for `int8_t` and `int16_t`
frames by a macro (`OSC_WTB_LOOPS(depth, T)`). This gives 16 functions:
one-frame and blend × plain, FM, sync and ring.

**Index (any length, A16):** `m = (uint64_t)ph × len` (one `UMULL`);
`i = m >> 32`; `fr = (uint32_t)m × 2⁻³²`;
`n = (i + 1 == len) ? 0 : i + 1`. The 8-bit × 256 is folded into the
block gain.

| Role | One frame: body | Budget | Blend: body (with a granted slot) | Blend budget |
|---|---|---:|---|---:|
| plain | `out = F[i] + fr × (F[n] − F[i])` | ≤ 27 | `x0 = A[i] + ff(B[i] − A[i]); x1 = A[n] + ff(B[n] − A[n]); out = x0 + fr(x1 − x0); ff += ffStep` | one frame + ≤ 15 |
| FM | `rp = ph + (ssat(mod × fmMod) << 19)`, then as plain on `rp` | ≤ 38 | as plain on `rp` | + ≤ 15 |
| ring | plain + `osc_ringSample()` (depth ramp) | ≤ 34 | blend + ring law | + ≤ 15 |
| sync | plain + `ph = osc_syncPull(ph, over[i], ratio, depth)` | ≤ 42 | blend + pull | + ≤ 15 |

- **Per-slot check (A17):** each blend copy minus its one-frame copy
  must be ≤ +15 static (estimate), against today's ≈ +66 per slot (0-4).
  A blend that greatly exceeds +15 is reported before acceptance.
- **The section check (A1):** only while no slot is held. A held slot is
  accounted per slot.
- Each function gets the D-11 … D-18 block form, with "wavetable" and
  "(any frame length, UMULL index; A16)".

### E-6 `Core/DSPAudio/Oscillator.c` — MOD the typed renderers and `osc_renderUserList()`: the wtb branch

```c
	/*
	 * Wavetable frame choice and blending (S087 stage E).
	 *
	 * What:       frame position fe = positionApplied x (count - 1) / 127 and
	 *             fs = (positionApplied - positionBendStep) x (count - 1) / 127
	 *             (this block's start and end, set by the typed pos rule E-7,
	 *             which keeps both inside one frame pair). count >= 2 and
	 *             modNode_interpClaim(osc): blend copy on the pair
	 *             (floor(min(fs, fe)), +1) with ff ramping fs -> fe across the
	 *             block. Otherwise: the one-frame copy on the nearest frame to
	 *             fe (the fallback, Q9). A modulator w entry always uses the
	 *             one-frame plain copy at frame 0 and never claims.
	 * Why:        S087 P0 A3/A7/A8/A20: the frame index (pos) is what
	 *             interpolates, through one of the two slots; the table (wav)
	 *             steps.
	 * Affiliates: modNode_interpClaim(); osc_typedPositionApply() (wtb).
	 */
```

### E-7 `Core/DSPAudio/Oscillator.c` — MOD `osc_typedTriggerPhase()` / `osc_typedPositionApply()` / `Restore()`: the wtb rules

- **Trigger:** wtb returns phase 0 (`pos` is the frame position, not a
  phase) and sets `positionApplied = position` (no glide from the last
  hit's frame).
- **Apply (wtb):**
  - `d = position − positionApplied`, linear;
  - capped so the block moves within one frame pair:
    `|d| ≤ max(1, 127 / (count − 1))`;
  - `positionApplied += d`; `positionBendStep = d`;
  - **no `phaseInc` change, whatever `bend` is** (in `snc` the frame
    still moves).
- **Restore (wtb):** `positionBendStep = 0`.

Extend the D-10 block with:
`S087 E: wtb - frame position, linear, capped to one frame pair per
block; no phase bend.`

### E-8 Drum `snc` with a `w` master (`Oscillator.c` `osc_syncClockInc()`)

A MODLIST master bound to WTB returns `phaseInc` unchanged (cycle units:
one reset per frame cycle, A12). Add one line to the D-39 block.

### E-9 `Core/Menu/menu.c` — MOD `menu_formatWaveCell()` (D-41): the `w` codes

- osc1 under `wtb`: the table name + `wNN`;
- modulator `w` entries: the table name + `wNN`.

### E-10 `Core/DSP/Instruments/InstrumentManager.c` — MOD `instrumentManager_waveDomainMax()`: `wtb` count

`osc1_wave` under `wtb` → tables − 1.

### E-11 Host tests (new)

- **`tools/dsp_test/test_osc_wtb.c`** (target `osc_wtb`). **Laws:**
  1. the UMULL index equals the exact `i = ⌊ph × len / 2³²⌋` for lengths
     16, 600, 1,000, 2,048;
  2. the next point wraps at `len`;
  3. 8- and 16-bit outputs agree within ±1 LSB × 256;
  4. a blend with ff = 0 equals the one-frame output of frame A, and with
     ff = 1 frame B;
  5. a frame ramp within a block stays inside one pair (E-7 cap);
  6. without a slot, the nearest frame is used;
  7. the FM offset scale equals the table FM (A17 addendum);
  8. a modulator `w` entry plays frame 0;
  9. a `wav` change under `wtb` steps (A20).

  **Break proofs:** an off-by-one in the wrap (law 2); the cap removed
  (law 5).
- **`tools/dsp_test/test_interp_slots.c`** (target `interp_slots`),
  extracting `modNode_interpClaim()` and `modNode_resetTargets()`.
  **Laws:**
  1. at most 2 grants per block;
  2. a holder keeps its slot while it claims every block, even when
     newcomers claim first in render order;
  3. a holder that stops claiming frees the slot after one block;
  4. the toggle off grants nothing;
  5. `osc_setType()` clears the stamp.

  **Break proof:** the reservation removed (law 2 fails).

### E-12 `tools/dsp_test/armcheck/s087.c` — fill the stage-E part

Add the 16 wavetable functions, and the per-slot differences
(blend − one-frame) for each role and depth.

### 6.1 Stage-E gates

- `osc_wtb`, `interp_slots`, `osc_sample`, `osc_type`, `catalogue`.
- `osc87_identity`, `elf87_check`, `elf87_mixer_check`.
- `armcheck-s087`, including the per-slot check.
- `special_tags`.
- **Hardware (§7.5).**

---

## 7. Hardware lists (the user runs these; results recorded per stage)

### 7.1 Stage A

1. A Drum track with coarse pitch alternating 20/100 on every step,
   triggered: every hit starts at its own pitch, with no blip.
2. The same with shuffle and micro-timing (deferred steps), rolls and a
   flam.
3. `rmp on`: ramps still run and end where the next step lands.
4. An automation-only step (no trigger) still applies, at most one block
   later.
5. The six-slot worst-case Scene: `cpu` is the same as at 0-4, with no
   underruns.

### 7.2 Stage B

1. `typ` appears on screen 2 of every oscillator page (Drum, Cymbal,
   HiHat: `typ`; Snare: `noi mix typ`), shows `wav`, and cannot leave
   `wav` in stage B.
2. An old Kit loads with `typ` `wav` and sounds the same.
3. Kit save/load keeps `osc1_type`.
4. SHIFT (Morph view) does not edit `typ`.
5. `typ` is offered as a step automation target and is **not** offered
   as an LFO or velocity target.
6. Live record of a `typ` change records an entry.
7. The FM and Mod Osc pages are unchanged.

### 7.3 Stage C

1. **Install from today's `SD_CARD`:** 1 sample (`01_cut.wav`) and 1
   wavetable (`ak_hvox`, 104 frames). The result screen counts them.
2. A three-deep folder, a long-named folder, a truncated WAV (skipped,
   not aborted), a stereo 24-bit file (installs converted), and a file
   with `smpl` loop points.
3. A v1 flash at boot shows the notice.
4. v1 `wav` 8+ plays the new `s01` (one-shot) until stage D.
5. Run the optimizer on a copy of the card: tables never grow; `smpl`
   is kept.

### 7.4 Stage D

1. **`typ smp`/`slp` on osc1 of each instrument:**
   - `pos` start sweeps;
   - an LFO on `pos`: glides, capped;
   - `slp` with a sample whose end is not a zero crossing: a clean loop
     seam (A6);
   - a short loop at high pitch.
2. **Drum:**
   - `fm`/`mix`/`rng`/`snc` with osc1 `smp`/`slp`;
   - `snc` with osc2 = `l01` (resets at each loop) and `s01` (noise
     clock).
3. **Modulators:**
   - the FM page `wav` lists `s01`, `l01` after the stock waves;
   - Cymbal/HiHat Mod Osc likewise;
   - an `s01` on the never-resetting Drum osc2 plays once after
     selection (known).
4. **Step automation of `typ`:** `wav` ↔ `smp` per step, with no wrong
   attack; the next untriggered-entry step restores the Kit `typ`.
5. A typ switch with `wav` at 7 → `smp` clamps to the last sample.
6. Reinstall with fewer samples: selections clamp.
7. **CPU:**
   - the six-slot worst-case Scene with samples on every oscillator, each
     instrument;
   - `cpu` ≤ the 0-4 reading, with no underruns;
   - long samples at high pitch (flash stalls).

### 7.5 Stage E

1. **`typ wtb`:** `wav` = table (steps), `pos` = frame. With the toggle
   on, the first two `wtb` oscillators blend smoothly and the others step
   by frames. With the toggle off, all step.
2. Sticky slots: no flutter when a third `wtb` voice plays.
3. FM-carrier `wtb` (Cymbal/HiHat, Drum `fm`) with sample modulators.
4. Modulator `w01` (the first frame).
5. Drum `snc` with a `w01` master.
6. **CPU:** the worst-case Scene with `wtb` on every osc1, two blending.
   `cpu` stays within the 0-4 reading plus the per-slot allowance, with no
   underruns.

---

## 8. Documentation updates (per stage, after its gates pass)

| Stage | Files |
|---|---|
| A | `PATTERN_DYNAMIC_STACK.md` §6.1–6.2b (the restore → drain → trigger order, the mark); `INSTRUMENTS_DSP_REFERENCE.md` §2, §7.5 |
| B | `INSTRUMENTS_DSP_REFERENCE.md` §4.1–4.4 (the SELECTOR flag, the `osc1_type` row), §5, §6.1; `BANK_PRESET_ARCHITECTURE.md` (selector apply); `MODULE_INTERCHANGE_SPEC.md` (`osc1_type` key); `MIDI_SPEC.md` (`typ` NRPN/Learn target, automation only); `DSP_TEST.md` |
| C | `FILESYSTEM_SPEC.md` (catalogue v2, the walker, natural order, depth, boot notice, `smpl`); `STORAGE_SRAM_MANIFEST.md` (flash map, R-C1 … R-C3); `tools/sampleoptimize.py` docstring |
| D | `INSTRUMENTS_DSP_REFERENCE.md` §6.1 (types, readers, `pos` on samples, the modulator list), §8 (costs), §9.3, §10; `CPU_USE_DSP_AUDIT.md`; `OSC_INTERP_AUDIT.md` (`wav` interpolation retired); `STORAGE_SRAM_MANIFEST.md` (R-D1) |
| E | `INSTRUMENTS_DSP_REFERENCE.md` (`wtb`, slots, sticky claims); `OSC_INTERP_AUDIT.md`; `CPU_USE_DSP_AUDIT.md` (per-slot figures); `STORAGE_SRAM_MANIFEST.md` (R-E1) |

**At the session close:**
- `MEMORY.md`, the S087 handoff log, and comment re-pointing from this
  schedule to the log (the S086 pattern);
- `S087_P0_*.md` become superseded root documents.

---

## 9. Approvals and confirmations needed before code

| # | Before | What |
|---|---|---|
| 1 | Stage A | RAM R-A1 (+1 B SRAM1) and R-A2 (+128 B stack in `voiceControl_processPending()`) |
| 2 | Stage C | RAM R-C1/R-C2 (released) and R-C3 (≈ +560 B install walker state), net ≈ −17.9 KB |
| 3 | Stage C | Two install rules settled while writing this schedule: samples at rates other than 44,100 Hz install unconverted and play transposed (A15 keeps rate conversion on the host); samples longer than 524,286 points (≈ 11.9 s) are truncated, so the readers can use signed Q19.12 compares |
| 4 | Stage D | RAM R-D1 (−128 B DTCM, released) |
| 5 | Stage E | RAM R-E1 (+2 B, `INCCMZ`) |

---

## 10. Implementation record (running)

All RAM expansions in §0.5 are user-approved for this session (task
instruction): R-A1, R-A2, R-C1/C2/C3 released/added, R-D1 and R-E1.

### 10.1 Step 0 (done)

- `tools/dsp_test/snapshot87.sh` added and run: 39 files into
  `tools/dsp_test/frozen87/` (the §0-1 list, plus the four engines and
  four `*Parameters` pairs). It refuses to refreeze.
- Makefile targets added: `osc87_identity`, `engine87_identity`,
  `elf87_baseline`, `elf87_check`, `elf87_mixer_snapshot`,
  `elf87_mixer_check`. `FROZEN87 := frozen87`; `OSC87_ELF_SYMS` is
  `OSC86_ELF_SYMS` plus the S087 readers and the user-sample symbols.
  `frozen87/identity_allowed.txt` and
  `frozen87/engine_identity_allowed.txt` start empty (no removals before
  stage D).
- Baselines from the clean `0e2d61dd` build: `text 658,872 / data 424 /
  bss 430,296`; flash 659,296 / 753,664 (headroom 94,368); ITCM
  8,512 / 16,384; DTCM statics 4,472; FX margin 3,712. These match
  §1.3, so the frozen ceilings stand.
- `frozen87/elf_osc.txt`, `elf_loops.txt` (61 loops in 26 symbols) and
  `elf_mixer.txt` (97 loops) written.
- The §1.4 static ceilings were not re-measured in this pass: the tree
  is byte-identical to the S086 close that produced them, and the link
  figures match. They remain the frozen reference.
- Device (`cpu` readings, §0-4) is the user's; not recorded here.

### 10.2 Stage A (implemented; host/ELF gates pass)

- A-1 … A-4: `seq_pending_automation_done` cursor added (+1 B static,
  measured bss +8 B with alignment); `seq_drainPendingAutomation()`
  became `seq_drainPendingAutomationTo(uint8_t limit)`; the queue resets
  only when fully drained; `seq_pendingAutomationMark()` added. The
  PRIMASK handoff now uses the file's `__get_PRIMASK`/`__disable_irq`/
  `__set_PRIMASK` intrinsics (same semantics, host-testable) instead of
  raw asm.
- A-5: declarations and every comment reference updated (no old name
  remains outside historical notes).
- A-6/A-7: `voiceControl_triggerNow()` split into
  `voiceControl_prepareTrigger()` / `voiceControl_fireTrigger()`;
  `voiceControl_popPending()` replaced by
  `voiceControl_popPendingBatch()`, which pops the whole ring and takes
  the automation mark in one PRIMASK section (R-A2: 128 B of stack for
  the 32-event batch and its reset flags). New order: pop+mark →
  prepares/leftover restores → drain(mark) → fires.
- A-8: `main.c` no longer calls the drain; the ramp comment names the
  new location.
- A-9/A-10: `test_stepauto_order.c` (5 laws) and
  `test_drain_cursor.c` (4 laws) added with Makefile targets
  `stepauto_order` and `drain_cursor`. Both pass with 0 failures. Break
  proofs: the `-DBREAK_DRAIN_AFTER_FIRE` build fails law 1 (6
  failures), and the cursor-start break (`i = 0`) fails law 2; both are
  run automatically by the targets.
- Gates: `make all` clean; `text 659,040 / data 424 / bss 430,304`
  (flash 659,464; headroom 94,200; ITCM 8,512 unchanged);
  `elf87_mixer_check` — 39 hot loops equal, 0 grew.
- Hardware §7.1: pending (user).

### 10.3 Stage B (implemented; host/identity/ELF gates pass)

- B-1/B-2/B-3: `OSC_TYPE_*` and `oscType` (byte 34) added; `OscInfo`
  still 72 B (asserts pass); `osc_isTyped()` and `osc_setType()`
  declared. `OSC_TYPE_SELECTABLE` is 1, so stage B accepts `wav` only.
- B-4: `osc_setType()` written in full (clamp, wav clamp, pos re-anchor,
  phase re-anchor, cache invalidation, idempotent no-op). The sample
  arms return 0/`pos` start placeholders until D-8, as planned.
- B-5/B-6: `INSTRUMENT_PARAM_FLAG_SELECTOR` (0x08) and
  `IM_SPECIAL_OSC_TYPE` (writer 20 of 21) added.
- B-7: `instrumentManager_targetValid()` accepts a SELECTOR + AUTOMATABLE
  row for AUTOMATION only; modulation still requires MORPHABLE.
- B-8/B-9: the writer calls `osc_setType()` then
  `instrumentManager_waveDomainChanged()`; the diagnostic classifier
  maps `osc1_type`.
- B-10/B-11: `instrumentManager_descriptorModDomain()` and
  `buildModulationRange()` take a slot; the stage-B `waveDomainMax()`
  returns `modNode_getMaxWaveformIndex()` (no range change yet).
  `instrumentManager_waveDomainChanged()` refreshes LFO adapters and
  direct targets. `instrumentManager_waveCellCount()` (D-41) was added
  early because B-17 needs it.
- B-12…B-16: `MENU_OSC_TYPE` = 4, `oscTypeNames`, the menu count/name
  helpers and `menu_automationValueMax()` (0, since SELECTABLE is 1).
- B-17: the menu commit clamps the stored `osc1_wave` in both images
  after a `typ` write (a no-op until samples exist).
- B-20…B-25: `ROW_SELECT_SPECIAL`, `X_PARAM_OSC1_TYPE` appended, the
  `osc1_type` row and the first-page cell (`typ` at cell 4; Snare cell
  6 = `noi mix typ`); counts 41/40/41/41. All descriptor tables still
  fit the 64-row capacity and every earlier index is unchanged.
- B-30…B-32: Drum `modOsc`, Cymbal/HiHat `modOsc`/`modOsc2` marked
  `OSC_TYPE_MODLIST` in their init functions.
- B-33…B-35: the supplemental setter accepts a member-bound SELECTOR
  row and keeps `morph_interpolation[]` equal to it;
  `preset_applyKitVoiceSelectors()` runs after the runtime reset and
  routing, before the Morph image, in
  `preset_resetAndApplyKitVoiceImage()`; the supplemental loop skips
  selector rows. The other supplemental call sites (`:2028`, `:2945`)
  are reached only through that reset path or as a later rebind, so no
  extra call was needed (checked).
- B-36: `check_special_tags.py` maps `osc1_type`; `special tags OK
  (163 rows, 0 mismatches)`.
- B-40: `test_osc_type.c` added (6 laws) — 0 failures. Break proofs: the
  gate branch removed and the row given `DTYPE_0B127` are compiled by
  the target and both are caught.
- 0-3: the `osc_mod` extraction ranges moved to 49–99 and 104–192;
  `osc_mod` still reports 0 failures.
- Gates: `make all` clean; `text 660,224 / data 424 / bss 430,296`
  (flash 660,648; headroom 93,016; ITCM 8,512); `osc87_identity` 0
  removed lines; `engine87_identity` 0; `elf87_check` 26/26 symbols,
  61/61 loops unchanged; `elf87_mixer_check` 39 equal, 0 grew.
- Hardware §7.2: pending (user).

### 10.4 Stage C (implemented; host gates pass)

- C-1/C-2: SampleMemory.h/.c rewritten for catalogue v2: 32 B header,
  128 x 28 B sample entries, 128 x 20 B wavetable entries at 0x081FE000;
  in-place validation/counting (a broken entry ends the count); the
  EMPTY/V2/STALE state; the 24-step gain mantissa. The v1 SampleInfo
  shims remain until D-29.
- C-3: new SampleInstall.h/.c: fmt acceptance (PCM/extensible, 1-2
  channels, 8/16/24 bit, any rate), 24->16, stereo->mono, 8->16 for
  samples, signed 8-bit frames, two zero frames after every sample, gain
  code from the peak, entries written as items finish, header with the
  magic last; samples capped at 524,286 points. A partial v2 header
  marks an interrupted install (it reads EMPTY, not STALE).
- C-10: the v1 manifest/scan/parse/install code was removed (R-C1/R-C2)
  and replaced by filesystem_installUserContentBlocking(): natural-order
  selection scans through afatfs_findNextObject(), samples/ files then
  subfolders to depth 3, wavetables/ folders as tables (two passes, one
  frame length/format), per-item abort. The LFN helpers stay, marked
  unused.
- C-12: storage_naturalCompare() and storage_displayNameFromLfn().
- C-20/C-21: one modal install with an Sxxx Wxxx result screen and a
  wave-domain refresh for every slot; a boot notice for a STALE
  catalogue.
- C-40..C-46: the optimizer reads/writes RIFF chunks (keeping the first
  smpl loop), never grows a table (any length from 16), trims only after
  a smpl loop end, walks subfolders to depth 3, and leaves 8/24-bit and
  stereo to the device converter.
- C-50/C-47: catalogue, wav_install, natural_order and sampleoptimize
  pass. Break proof: the magic-first build fails catalogue law 3.
- RAM: bss 430,296 -> 412,396 (-17,900 B, the R-C1/R-C2/R-C3 net).
- Hardware §7.3: pending (user).

### 10.5 Stage D (implemented; host gates pass)

- D-1 deviation: the bind fields reuse the v1 cache bytes and keep the
  v1 names (sampleCacheIndex/Looped/Generation/Size/Data) instead of a
  risky mass rename; the struct comment names them as the bind.
  OscInfo stays 72 B.
- D-2..D-10: Q19.12 constants; osc_userBind(), osc_userCount(),
  osc_userListCount(); the typed trigger/pos rules (linear delta, 4x
  speed cap, no negative phase, no revival of a finished one-shot,
  exact restore); osc_setFreq() clamps/binds; typed branches in the pos
  helpers.
- D-11..D-18: eight separate sample loops in .itcm.s087 (noinline,
  noclone). Measured: smp/slp plain 24/27, FM 42/53-47, ring 38/42,
  sync 36/41.
- D-19..D-28: typed renderers and osc_renderUserList(); the v1 sample
  loops are unreferenced (the linker drops them); dispatcher defaults
  are typed or zero-fill; the sync default is NOISE only; the wav
  interpolation claim is gone.
- D-30..D-39: engines start a typed osc1 at its pos start and render
  through the typed renderers; Drum snc uses the loop-wrap builder for
  an l master and the master's Q19.12 increment as the ratio clock;
  osc_syncClockInc() scales a sample master <<10.
- D-40/D-41: wave ranges follow osc_userCount()/osc_userListCount();
  the menu clamp and edit line are type/list aware (name + s/l/w code).
- D-44: menu_numSamples/menu_setNumSamples removed.
- D-45: osc_sample (13 laws) passes.
- ITCM 15,032 / 16,384 after E; flash 672,504 (headroom 81,160).
- CPU note (needs the user's decision): elf87_mixer_check shows 37 hot
  loops equal, 1 shrank (53->44) and 1 grew 14->15 instructions. The
  slp FM copy's dearest path is 53 vs the 47 budget; the Drum fm
  section with a sample carrier is about 72 (budget 92).
- Hardware §7.4: pending (user).

### 10.6 Stage E (implemented; host gates pass)

- E-1/E-2: OSC_TYPE_SELECTABLE 4; OscInfo.interpStamp (byte 35).
- E-3: modNode_interpClaim() with sticky reservations
  (modNode_interpReserved, modNode_interpLastGen; R-E1 is 4 INCCMZ bytes,
  the grant counter reuses the active count).
- E-4..E-8: wavetable bind (kind by entry depth, frame 0, cycle law),
  one-frame and blend copies for plain/FM/ring/sync, nearest-frame
  fallback, one-pair pos cap, no phase bend, cycle clock for a w master.
- E-9/E-10: menu w codes; wtb wave-row maximum.
- E-11: osc_wtb (laws 1-6, 8) and interp_slots (laws 1-4) pass.
- Hardware §7.5: pending (user).

### 10.7 Carry-over / not done

- The automation display branch for MENU_WAVEFORM still uses the stock
  table (menu_formatAutomationValue3); the edit line and clamps are
  type-aware.
- armcheck-s087 was not added; loop sizes came from elf_loopdump.py on
  the linked image.
- elf87_check reports the v1 user-sample symbols as missing/changed
  (their metadata source and callers changed); its symbol allow-list is
  not written yet.
- The wav-interpolation code is dormant but still linked (the claim is
  gone); it can be deleted to reclaim ITCM.

---

## 11. Assessment of the implementation (2026-10-28)

**Scope:** stages 0 and A–E as recorded in §10, in the working tree on
`0e2d61dd`. No source was changed for this assessment. The reproductions
below were built in a scratch directory against the real sources.

### 11.1 What was checked

- **Build:** clean `make all && make img`.
  - text 672,080 / data 424 / bss 412,396;
  - flash 672,504 / 753,664 (headroom 81,160); ITCM 15,032 / 16,384;
    DTCM statics 4,476; FX margin 3,712;
  - the image rebuilds bit-identically.
  - New warnings from S087: `calcUserSampleOscRingBlock` and
    `modNode_getWaveTargetOsc` are defined but unused (both dead code
    left by D-20 … D-27; see F19).
- **Host targets:**

  | Result | Targets |
  |---|---|
  | Pass, break proofs caught | `stepauto_order`, `drain_cursor`, `osc_type`, `catalogue`, `wav_install`, `natural_order`, `sampleoptimize`, `osc_sample`, `osc_wtb`, `interp_slots`, `special_tags` (163/0), `stock_tables`, `filter`, `selftest`, `pack`, `postchain`, `octave`, `mixer` |
  | **Do not compile** | `osc_mod`, `osc_pos`, `osc_stock` (F15) |

- **Gates:**

  | Gate | Result |
  |---|---|
  | `osc87_identity` | pass, 19 allowed |
  | `engine87_identity` | pass, 13 allowed |
  | `elf87_check` | **fails** (no allow-list; F15) |
  | `elf87_mixer_check` | **fails**: one loop grew (F12) |

- **Code review** of every stage against the plan and the schedule.
- **Reproductions:**
  - the catalogue test with flash-accurate programming (F1, F2);
  - `pos` bends on long and looped samples (F5, F16);
  - per-loop instruction counts from the linked ELF (§11.4).

### 11.2 Verdict

- **Stages A and B are sound:**
  - the step-automation order and the drain cursor are correct and
    tested;
  - the selector path (`typ` as an automation target that is not
    morphable, the restore value, the apply order) matches §12.5 of the
    plan.
- **Stage C cannot work on hardware** as written: no install can commit
  (F1). A skipped file breaks the next item (F2), a large table folder
  hangs the install (F3), and the third folder level writes out of
  bounds (F4).
- **Stages D and E play, with defects** in the looped-sample bend (F5),
  `slp` FM with loop points (F6), modulator wavetables (F7), wavetable
  interpolation itself (F8, F9) and per-step sample changes (F10).
- **CPU:** every voice section is below its frozen worst case, and each
  wavetable blend costs +11 … +15 per slot against today's ≈ +66. Two
  per-loop rises need noting (F12, F13). ITCM is nearly full (F14).
- **Hardware:** none of §7.1–§7.5 has been run. Stage C–E hardware tests
  cannot pass until F1–F4 are fixed.

### 11.3 Findings

**Severity:**
- **C, critical:** blocks the feature on hardware, or hangs it.
- **H, high:** wrong sound or behaviour in normal use.
- **M, medium:** wrong in edge cases, or a gate or test gap.
- **L, low:** clean-up or documentation.

#### Critical

**F1 (C) — the catalogue never commits on hardware.**
- **Where:** `SampleInstall.c` `sampleInstall_begin()` (:346-370) and
  `sampleInstall_commit()` (:379-410).
- **What:**
  - `begin()` programs the 32 B header with `sample_count`,
    `wavetable_count`, `flags` and `data_end` = 0.
  - `commit()` programs the same words again with the real values.
  - Flash programming only clears bits, and `sampleFlash_writeWord()`
    reads back and returns 3 on a mismatch.
  - So `commit()` fails before the magic is written, and every install
    ends with an EMPTY catalogue and "failed".
- **Evidence:** `test_catalogue.c` against a stub that ANDs and verifies
  as `sampleFlash.c` does: 6 failures (commit, state V2, both entries,
  commit after abort). The repository stub (`catalogue_host.c:36-41`)
  overwrites with `memcpy`, which hid this.
- **Fix:**
  - program each header word once, at commit: `begin()` writes nothing
    to the header, and an interrupted install already reads EMPTY,
    because the magic is erased;
  - make the host stub flash-accurate (AND plus read-back) permanently,
    so every catalogue law runs under real programming rules.

**F2 (C) — a skipped item breaks the item after it.**
- **Where:** `SampleInstall.c` `sampleInstall_abortItem()` (:337-344).
- **What:**
  - It rewinds `write_addr` to the item start.
  - The aborted item's bytes are already programmed, so the next item
    programs over them, its read-back fails, and that item is lost too.
    This repeats for every later item.
- **Origin:** this was an error in my schedule (C-3 said the region
  stays erased). The implementation followed it.
- **Fix:** leave `write_addr` after the last programmed word (rounded up
  to 4), and count the wasted bytes for the result screen. Add a law to
  the flash-accurate stub: abort, then install, and both later items
  read back.

**F3 (C) — a wavetable folder with more than 128 `.wav` files hangs the install.**
- **Where:** `filesystem.c` `filesystem_installWavetableHere()`, pass 1
  (:24773-24776).
- **What:** at 128 frames, `continue` skips the "last taken" update, so
  `filesystem_installPick()` returns the same file forever. Audio is
  suspended during the modal, so the device hangs.
- **Fix:** update `fs_install_last` on every path (stop counting at 128,
  keep advancing), or break out of the scan at 128.

#### High

**F4 (H) — the third folder level writes out of bounds and never installs.**
- **Where:** `filesystem.c`:
  - the arrays at :23379-23381 have `FS_INSTALL_DEPTH` = 3 rows;
  - the walkers at :24840-24878 and :24892-24924 start at depth 1 and
    recurse while `depth < 3`.
- **What:**
  - At depth 3 (`samples/a/b/`), `fs_install_last[3][0] = '\0'` writes
    one byte past the array.
  - `filesystem_installPick()` then refuses depth 3, so nothing at that
    level installs.
- **Fix:**
  - index with `depth − 1`, or size the arrays `DEPTH + 1`;
  - decide whether "depth 3" counts `samples/` itself;
  - add the three-deep folder to the hardware list's install case
    (§7.3 item 2 has it).

**F5 (H) — a backward `pos` glide on a looped sample jumps.**
- **Where:** `Oscillator.c` `osc_slpPlainBlock`, `osc_slpFmBlock`,
  `osc_slpRingBlock` and `osc_slpSyncBlock` (the `inc %= lenPh` lines
  :2353, :2449, :2566, :2664), together with `osc_typedPositionApply()`
  (:2230-2232).
- **What:**
  - A backward bend makes the block's total increment negative (signed
    −195,905 in the probe).
  - The loops treat it as unsigned, so `inc >= lenPh` is true and the
    modulo turns it into an arbitrary forward step.
- **Evidence:** a 200,000-point loop, `pos` 64 → 60: one block moved
  **−47,099 points**, against the expected ≈ −1,562.
- **Fix:**
  - **Either** keep the increment signed: reduce the magnitude modulo
    the loop once per block, and wrap both ways (+1 compare per sample;
    measure against the FM figure, F13);
  - **or** limit a backward `slp` bend so the block cannot cross below
    the loop start, and let it glide over more blocks (no per-sample
    cost).

  The second fits D5 (a capped glide) and the CPU rule better.

**F6 (H) — `slp` FM reads the wrong region when the loop does not start at 0.**
- **Where:** `osc_slpFmBlock` (:2466-2483).
- **What:**
  - `rp` is relative to the loop start, but `idx = rp >> 12` indexes
    `data[]` directly, and `next` compares that relative index with the
    absolute `le`.
  - Any sample with `smpl` loop points (`loop_start > 0`) plays the start
    of the sample instead of its loop under FM.
  - During the lead-in (phase before the loop) the read is clamped to
    the loop start, so the FM carrier holds one point.
- **Fix:**
  - index `ls + (rp >> 12)` and compare `next` in absolute terms;
  - use the `smp` clamp for a phase still in the lead-in;
  - add a law to `osc_sample` with `loop_start > 0` (L2/L5 currently use
    `ls` = 0 or a plain read).

**F7 (H) — modulator `w` entries play about 10 octaves low; a `w` sync master runs 1,024× slow.**
- **Where:** `osc_userBind()` (:2118-2120) and `osc_syncClockInc()`
  (:1160-1178).
- **What:**
  - the cycle law (`freq2PhaseIncr()`) is chosen only when `oscType ==
    OSC_TYPE_WTB`;
  - a modulator's `w` entry is `OSC_TYPE_MODLIST` with a wavetable bind,
    so it gets the Q19.12 sample law, 2²² instead of 2³² per cycle.
- **Fix:** choose the law by the bind kind (`WTB8`/`WTB16` → cycle law),
  then check that `osc_syncClockInc()` returns it unscaled (E-8).
  `osc_wtb` law 8 ("a modulator `w` entry plays frame 0") should also
  check the pitch.

**F8 (H) — wavetable blending never interpolates between frames.**
- **Where:** `osc_wtbFrameOf()` (:2912-2917) and every blend copy
  (:2976-2985, :3060-3069, :3148-3157, :3229-3238).
- **What:**
  - `fs`/`fe` are `pos` rounded to the nearest whole frame, so `ff` only
    ramps 0→1 between whole frames when the rounded frame changes.
  - A `pos` held between two frames plays one frame.
  - With 2 frames, `pos` 0–63 plays frame 0 and 64–127 frame 1, with no
    morph.
  - A3/A20 make the frame index the interpolation target, so stage E's
    main feature is missing.
- **Fix:**
  - compute the frame position as a fraction (e.g. Q8: `pos × (count −
    1) × 256 / 127`);
  - the pair = its integer part; `ff` = its fraction, ramping from the
    block start's to the block end's fraction;
  - the one-frame fallback keeps the nearest frame;
  - add an `osc_wtb` law: a held `pos` at half a frame gives the
    50 / 50 mix.

**F9 (H) — 8-bit wavetables rebind every block, so their frame glide is lost.**
- **Where:** `osc_userBind()` (:2050-2056, :2078-2091).
- **What:**
  - the provisional kind is always `WTB16`, and `WTB8` is chosen only
    inside the change branch;
  - so an 8-bit table never matches its stored kind and rebinds every
    block;
  - each rebind sets `positionApplied = position` (no glide) and
    re-reads flash.
- **Fix:** resolve the depth before the comparison, or compare a base
  kind (WTB) and the index only.

**F10 (H) — a per-step sample change starts from the previous sample's length.**
- **Where:** the engines' typed trigger branch → `osc_typedTriggerPhase()`
  (:2134-2159).
- **What:**
  - Stage A drains the step's `wav` entry before its trigger, but the
    bind follows only in the next block's `osc_setFreq()`.
  - So the `pos` start uses the old item's frame count and loop end.
  - When the new one-shot is shorter, the start lies past its end. The
    next bind clamps the phase to the end and the hit is silent.
  - A looped sample starts at a wrapped, wrong point.
  - This is the slicing case stage A was built for.
- **Fix:** `osc_typedTriggerPhase()` calls `osc_userBind()` first; it
  is cheap when nothing changed. Add a law: change `wav` and trigger
  in the same pass.

#### Medium

**F11 (M) — both blends drop for one block every 256 blocks when both slots are held.**
- **Where:** `modulationNode.c` `modNode_interpClaim()` (:101-120).
- **What:**
  - `interpStamp` 0 means "no slot", but a holder stamped in a block
    whose generation's low byte is 0 also stores 0.
  - In the next block neither holder is recognised, both reservations
    block them, and both step for one block (≈ every 186 ms).
- **Fix:** map the stamp to 1..255 (e.g. `(gen % 255) + 1`) on both
  sides.

**F12 (M) — an existing per-sample loop grew by one instruction.**
- **Where:** the Snare noise loop inlined into
  `mixer_calcNextSampleBlock()` (`elf87_mixer_check`: base#0, 14 → now#76,
  15).
- **What:**
  - one extra `mov r0, r1`: register allocation changed next to the new
    Snare typed branch (S086 F1 class);
  - it costs +1 per sample in every Snare setting.
  - Snare's worst case still falls, because its dearest setting today is
    the v1 sample (≈ 40 + 14) and the new types are cheaper. So A1
    allows it.
- **Fix:** move the typed branch out of the inlined code path, or
  accept. **Decision for the user;** record it either way.

**F13 (M) — the `slp` FM carrier's dearest path is 53.**
- **Where:** `osc_slpFmBlock`; the paths are 47 / 51 / 53.
- **What:**
  - 53 is above the v1 FM loop (48–51 linked) and the schedule's 47.
  - The sections stay below their frozen worst cases:
    - Drum `fm`: `slp` modulator 27 + 53 = 80, against ≈ 92;
    - Cymbal/HiHat: 27 + 27 + 53 = 107, against ≈ 131.
  - Allowed under A1. Re-measure after F5/F6.

**F14 (M) — ITCM has 1,352 B free.**
- **What:**
  - The dormant `wav` interpolation was not removed (D-20 … D-28).
  - `osc_renderPlainWave` is now inlined twice into the dormant
    ring-interpolation path, so `calcNextOscSampleRingBlock` grew from
    576 B to 1,852 B.
  - Those inlined loops are unreachable (no claim is made), so this
    costs ITCM, not CPU.
- **Fix:** remove the dormant paths as scheduled. This recovers ≈ 1.3–2
  KB, which S088's run mode needs.

**F15 (M) — test and gate gaps.**
- **The S085/S086 suites are not running:** `osc_mod`, `osc_pos` and
  `osc_stock` do not compile. Their `Makefile` extraction ranges
  (`Oscillator.h` 49-124 / 128-229; `SampleMemory.h` 104-109, the v1
  `SampleInfo`) are stale since stage C, so the sync/ring/stock/`pos`
  laws have not run since. §10.3's "osc_mod reports 0 failures" was true
  only at stage B.
- **`elf87_check` fails** with no allow-list for the planned v1 removals,
  and the per-loop comparison never ran.
- **`elf87_mixer_check` fails** on F12.
- **The host flash stub is not flash-accurate** (F1, F2).
- **`armcheck-s087` was not added.**

**F16 (M) — the D4 limit uses the wrong units.**
- **Where:** `osc_typedPositionApply()` (:2221-2227).
- **What:**
  - `d ≥ −points / 32` ignores the sample length; a step moves
    `frames / 128` points.
  - It is harmless for `smp` (the probe lands at the start correctly),
    because the playhead never falls behind the applied `pos` start.
  - It does not protect `slp` after a wrap.
- **Fix:** with F5.

#### Low

**F17 (L)** — `osc_setType()` does not clear `interpStamp`, though its
header says it does. `interp_slots` law 5 is missing.

**F18 (L)** — R-E1 was approved at +2 B. `modNode_interpLastGen` is a
`uint32_t`, so the actual figure is +5 B (`INCCMZ`). Make it a `uint8_t`
or approve +5 B.

**F19 (L) — deviations from the schedule:**
- D-1 (the renaming) was skipped;
- the D-20 … D-28 removals were not done: the dead v1 ring loop, the
  dormant interpolation, `modNode_getWaveTargetOsc`;
- the automation display of wave values is not type-aware (§10.7);
- `modNode_getMaxWaveformIndex()` is still the default path.

**F20 (L)** — after the Drum osc2 master changes kind, `syncClock`
carries 2³²-units into the loop builder, causing a burst of resets for a
few blocks. Clamp `pos` into the loop at the builder's entry.

**F21 (L)** — the `slp` sync reset target (`startPh`) is not clamped
into the loop when `pos` starts past an `smpl` loop end (A5; the
trigger start is).

**F22 (L, process)** — all five stages were implemented before any
hardware gate. §0.3 requires each stage to pass on hardware first.

### 11.4 Measured loop figures (static per sample, linked ELF)

| Loop | Paths | Ceiling / budget | Note |
|---|---:|---|---|
| `osc_smpPlainBlock` | 24 | 40–43 | |
| `osc_slpPlainBlock` | 27 | 40–43 | |
| `osc_smpFmBlock` | 42 | 48–51 | |
| `osc_slpFmBlock` | 47 / 51 / 53 | 48–51 | F13 |
| `osc_smpRingBlock` / `osc_slpRingBlock` | 38 / 42 | 46–50 | |
| `osc_smpSyncBlock` / `osc_slpSyncBlock` | 36 / 41 | section ≤ Drum `fm` | `snc` with the builder ≤ 49 |
| `osc_wtbPlainBlock` | 24 / 26 (16 / 8-bit) | 40–43 | |
| `osc_wtbFmBlock` | 32 / 34 | 48–51 | |
| `osc_wtbRingBlock` / `osc_wtbSyncBlock` | 32 / 34, 34 / 36 | — | |
| `wtb` blend − one frame (per slot) | plain +11/+13; FM +11/+13; ring +13/+15; sync +11/+14 | ≤ +15 against ≈ +66 today | A17 met |
| `osc_buildLoopSyncList` | 8 | — | periodic builder 7 |
| Dispatchers (`calcNextOscSample*`) | equal counts | — | registers only |
| Mixer-inlined loops | 37 equal, 1 shrank (53 → 44), 1 grew (14 → 15) | — | F12 |

### 11.5 Recommended order before hardware

1. **Install (stage C):**
   - F1 and F2, with the stub made flash-accurate;
   - F3, F4;
   - then the §7.3 install list on the device.
2. **Playback:**
   - F10 (bind at trigger);
   - F7 (`w` pitch);
   - F9 and F8 (wavetable kind, then fractional frames);
   - F5 and F16 (`slp` backward bend);
   - F6 (`slp` FM index);
   - F11 (stamp).

   Each fix gets a new host law with a break proof.
3. **Gates:**
   - restore `osc_mod`/`osc_pos`/`osc_stock` (F15);
   - write the `elf87_check` allow-list;
   - decide F12;
   - add `armcheck-s087`.
4. **ITCM:** remove the dormant interpolation paths and the dead v1 code
   (F14, F19).
5. **Hardware:** run §7.1 → §7.5 in order, stopping at the first failure.
