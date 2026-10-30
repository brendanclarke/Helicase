# Session 087 — Handoff Log

DATE: 2026-10-28/29
SESSION GOAL: Oscillator type system (`typ` row with `wav`/`smp`/`slp`/`wtb`), sample flash catalogue v2, Q20.12 sample and wavetable readers with `pos` decisions, step-automation order fix (D6), modulator `wav` list expansion, one-shot sample mod oscillators, transient click sample list, display/naming corrections, MEMORY.md refactor.
COMPLETED: All five stages (A–E) implemented and passing host/ELF gates. Three follow-up passes (P1–P3) applied. MEMORY.md refactored from 2,773 to 525 lines.
VERIFIED ON HARDWARE: P0+P1 implementation not yet hardware-tested at session close. P2 hardware checks pending. P3 specified but hardware checks pending.

CHANGES THIS SESSION:
- `Core/DSPAudio/Oscillator.c`: all S087 oscillator code — type system, sample/loop/wavetable readers (8 loops in .itcm.s087), pos on samples, modulator wav list, osc_modulatorTrigger(), v1 removal
- `Core/DSPAudio/Oscillator.h`: type constants, OscInfo fields (oscType byte 34, interpStamp byte 35), sample/wavetable declarations, v1 declarations removed
- `Core/DSPAudio/transientGenerator.c`: sample click list
- `Core/DSPAudio/transientGenerator.h`: sample click list declarations
- `Core/DSPAudio/modulationNode.c`: interpStamp (1..255), sticky claims, v1 shims removed
- `Core/DSPAudio/modulationNode.h`: stamp declarations, v1 declarations removed
- `Core/DSP/Instruments/Drum/DrumVoice.c`: osc_modulatorTrigger() call
- `Core/DSP/Instruments/HiHat/HiHat.c`: osc_modulatorTrigger() call
- `Core/DSP/Instruments/InstrumentManager.c`: non-wave fallback, v1 stacked comments merged
- `Core/SampleRom/SampleInstall.c`: catalogue v2 commit (magic last), abort without rewind, install_fault
- `Core/SampleRom/SampleMemory.c`: v1 SampleInfo shims and stub_silence_buffer removed
- `Core/SampleRom/SampleMemory.h`: v1 declarations removed
- `Core/Hardware/SD/storageTypes.c`: first 4 + last 4 name rule
- `Core/Hardware/SD/storageTypes.h`: declaration update
- `Core/Hardware/SD/filesystem.c`: install walker (samples/wavetables, natural order, depth 3)
- `Core/Sequencer/sequencer.c`: drain cursor, seq_drainPendingAutomationTo(limit), seq_pendingAutomationMark()
- `Core/Sequencer/sequencer.h`: updated declarations
- `Core/MIDI/MidiVoiceControl.c`: voiceControl_prepareTrigger/fireTrigger split, batch pop
- `Core/Menu/menu.c`: menu_formatWaveValue3(), menu_formatWaveCell() rewrite, type-aware formatters, osc_type descriptor/page
- `main.c`: seq_drainPendingAutomation() call removed from render loop
- `tools/dsp_test/modules/samples.c`: host test expectation for first 4 + last 4 name rule
- `build/LXRV2_lxr02.img`: rebuilt

KNOWN ISSUES INTRODUCED: F12 (Snare noise loop +1 instruction) is unchanged and needs user decision; A1 allows it. Three host-test targets (`osc_mod`, `osc_pos`, `osc_stock`) remain non-compiling (their `SampleMemory.h` v1 extractions are stale; F15, host-bench follow-up excluded from P1 schedule).
KNOWN ISSUES RESOLVED: Step-automation order race (D6); v1 sample loops removed; dormant wav interpolation code removed.

NEXT SESSION RECOMMENDED GOAL: Hardware testing of S087 P0+P1 implementation (install, playback, pos on samples, wavetable blending, modulator list, type switching); P2 hardware verification (display names, wav cell formatting); P3 hardware verification (mod oscillator restart, transient click samples). Then commit.
BLOCKERS: Full hardware testing of all S087 changes before commit.

CRITICAL REMINDERS FOR NEXT SESSION:
- `seq_drainPendingAutomationTo()` runs inside `voiceControl_processPending()`, NOT in main.c — do not re-add it there
- `osc1_type` is INSTRUMENT_PARAM_FLAG_SELECTOR (0x08) — not morphable, stored single-ended
- Sample flash catalogue magic "LXC2" is programmed LAST by sampleInstall_commit() — never program the header in begin()
- sampleInstall_abortItem() must NEVER rewind the write cursor onto programmed flash
- Interpolation slot stamps are 1..255 (never 0); stamp 0 means "no claim"
- The v1 sample loops and wav interpolation code are deleted — do not restore them
- MEMORY.md was refactored to 525 lines — do not re-pollute it with session history, specification content, or resolved-issue narratives

---

## 1. Session Context

**Branch:** `dev-ph7-voice_expansion`
**Base commit:** `0e2d61dd` (S086 P3/P4 committed, S086 close-out documents committed)
**After P0+P1 commit:** `f3805478` (dsp cleanup and memory refactor)
**Working tree at session close:** uncommitted P2+P3 changes on top of `f3805478`

**Planning document:** `S087_P0_OSC_TYPE_SAMPLES_WAVETABLES.md` (revisions r0–r8)
**Implementation schedule:** `S087_P0_IMPLEMENTATION.md` (stages 0–5, change IDs A-1..E-*)
**Follow-up implementation:** `S087_P1_SAMPLES_WAVES_FOLLOWUP_IMPLEMENTATION.md` (18 findings fixed)
**Display corrections:** `S087_P2_SAMPLES_CORRECTION.md` (5 findings, 4 with code changes)
**Mod/transient additions:** `S087_P3_SAMPLES_MOD_TRANS.md` (two features)
**MEMORY refactor:** `S087_MEMORY_REFACTOR_CONTEXT_RECEIPTS.md` (audit and execution)

---

## 2. Architecture and Decisions

### 2.1 Binding Rules (from P0 §2)

1. **CPU rule (A1):** the worst-case CPU use of the oscillator section must not increase. The limit is the section's worst case (Drum `fm`). Other modes may rise as long as they stay below it. Modes are not frozen individually. Any increase in a worst-case instruction count needs the user's explicit approval, stated with the exact increase.
2. **Copies, not edits:** never add a parameter or a test to an existing loop. Write separate copies per type × use (plain, FM carrier, sync slave, ring carrier).
3. **ITCM placement:** new ITCM code goes in `.itcm.s087`, `noinline, noclone`. No new constant-argument call sites into the existing dispatchers.
4. **No state live across an inlined render** (S086 F1): keep such state in `OscInfo`. Gate with the mixer hot-loop check.
5. **Constant cost:** no data-dependent per-sample work.
6. **RAM:** every new or enlarged allocation needs the user's approval.
7. **Today's trigger behaviour is kept:** Drum osc2 and the HiHat modulators never reset; Cymbal resets its modulators.
8. **Spike code is suspect** (Q1, rule 10): code from the spike trees is a source of ideas only. A stage plan may name a spike hunk only after re-deriving what it must do.
9. **The current repository's behaviour is kept** (Q5, rule 11).

### 2.2 Architectural Decisions (A1–A24)

| ID | Decision |
|---|---|
| A1 | The CPU limit is the oscillator section's worst case (Drum `fm`). Other modes may rise as long as they stay below it. Modes are not frozen individually |
| A2 | The installer prepares zero padding, fixed loop regions, and gain codes at install time to remove per-sample work at play time |
| A3 | Sample FM reads are clamped at both ends (start and end of sample/loop region) |
| A4 | Two zero frames after every sample; the played-out select and next-point hold are removed |
| A5 | FM on `slp`: the read offset wraps into the loop region; lead-in plays before the loop, not frozen at loop start |
| A7 | Only wavetables use the interpolation slots; `wav`, `smp` and `slp` always step. An LFO on `wav` under `smp`/`slp` steps between samples |
| A8 | Wavetable blend claims are made at render time and are sticky (a grant made this block is kept) |
| A11 | Catalogue entry layout: 28 B samples (with loop_start, loop_end, gain_code, flags), 20 B wavetables (frame_len, frame_count, bits, flags) |
| A13 | Zero-crossing bitmap deferred (later session with the optimizer) |
| A15 | `pos` D1–D10 accepted with the install-time rule |
| A16 | Any frame length (UMULL indexing); power-of-two not required |
| A17 | Two interpolation slots (as today), switched by the global setting. The check is per slot: a wavetable blend must not greatly exceed one of today's slots |
| A18 | `osc_setType()` clears the interpolation slot stamp when the type changes |
| A19 | The "N grants from a budget" model of r2 is withdrawn |
| A20 | `wtb` `pos` = frame position; `wav` = wavetable number |
| A21 | Modulator `wav` list: stock 0–7, then s01..sNN (one-shot), l01..lNN (looped), w01..wNN (wavetable first frame), up to 127 entries |
| A22 | Modulators get no `typ` and no `pos` rows. The FM and Mod Osc pages do not change |
| A23 | No `osc2_type` or `osc3_type` keys; only `osc1_type` on every instrument |
| A24 | `typ` is step-automatable with no ramp and one value per step |

### 2.3 Open Questions Decided (Q1–Q13)

| ID | Decision |
|---|---|
| Q1 | Spike trees restored as read-only references, but spike code is suspect (rule 10) |
| Q2 | Stage order A → B → C → D → E |
| Q3 | D1–D10 as recommended, with the install-time rule |
| Q4 | Only `osc1_type` (narrowed by A22) |
| Q5 | Do not change what the current repository implements |
| Q6 | No migration for kits whose `wav` pointed at v1 user samples |
| Q7 | `slp`: first `smpl` loop when present, else the whole sample; fixed at install |
| Q8 | `wtb` `pos` = frame position; `wav` = wavetable number |
| Q9 | Every `wtb` oscillator claims every block while the global setting is on (A7). Grants are sticky (A8). Without a grant: the nearest frame |
| Q10 | Stock waves plus s/l/w entries in `wav` (A21, A22); no `typ` |
| Q11 | 4× playback speed cap; tune at the hardware gate |
| Q12 | Flash budget per stage, measured after each stage |
| Q13 | `ReA`/`TrA`/`SwA` not in S087 |

### 2.4 `pos` on Samples: Decisions D1–D10

| ID | Decision |
|---|---|
| D1 | Phase format Q20.12. Start = ((pos × len) >> 7) << 12. Step = d × len per sample |
| D2 | Fixed loop region (smpl points or whole sample). `pos` is fraction of the whole sample |
| D3 | Linear difference (−127..+127) for `smp`/`slp`; circular for periodic content (stock waves, Cym) |
| D4 | One-shot edges: clamp at 0; past the end = ended; finished one-shot stays finished until next trigger |
| D5 | Time-based 4× speed cap for samples; larger change glides over several blocks |
| D6 | Stage A: a step's own automation must land before its trigger fires |
| D7 | Type/source/waveform family change re-anchors: positionApplied = position, no bend that block |
| D8 | Step kept in positionBendStep (int8_t); recomputed from bound sample length at restore |
| D9 | Sync reset target = `pos` start point |
| D10 | No modulator `pos` (A22). One-shot `smp` on Drum osc2 or HiHat modulators plays once after selection, then stays silent |

---

## 3. Stage A: Step-Automation Order Fix (D6)

### 3.1 Problem

At `0e2d61dd`, `main.c` ran `voiceControl_processPending()` (triggers) before `seq_drainPendingAutomation()`. A step's first trigger restored Morph base values, then the trigger fired, and only then did the step's automation entries apply — one block late. For phase-based parameters this was inaudible (passed on hardware in S086). For per-step sample starts (slicing), each slice would start in the wrong place.

### 3.2 Solution

The producer (TIM3) queues a step's trigger and its automation entries in one interrupt run. A foreground snapshot under PRIMASK sees both or neither. The new order per render pass:

1. Pop every pending trigger + mark the automation count, under one PRIMASK section
2. Prepare each popped trigger (deferred Scene apply, restore when reset)
3. `seq_drainPendingAutomationTo(mark)` — drain automation up to the mark
4. Fire each popped trigger in pop order

Entries queued after the mark wait one block with their own triggers.

### 3.3 Changes

| ID | File | Change |
|---|---|---|
| A-1 | `sequencer.c` | Added `seq_pending_automation_done` drain cursor (+1 B SRAM1, R-A1) |
| A-2 | `sequencer.c` | `seq_init()` zeroes the cursor |
| A-3 | `sequencer.c` | `seq_drainPendingAutomation()` → `seq_drainPendingAutomationTo(uint8_t limit)`: starts at cursor, runs to limit, resets only when fully drained |
| A-4 | `sequencer.c` | Added `seq_pendingAutomationMark()`: returns producer count under PRIMASK |
| A-5 | `sequencer.h` | Updated declarations; renamed ~12 comment sites |
| A-6 | `MidiVoiceControl.c` | Split `voiceControl_triggerNow()` into `voiceControl_prepareTrigger()` (restore) and `voiceControl_fireTrigger()` (trigger) |
| A-7 | `MidiVoiceControl.c` | Rewrote `voiceControl_processPending()`: batch pop under PRIMASK → prepare → drain → fire. +128 B stack (R-A2) |
| A-8 | `main.c` | Removed `seq_drainPendingAutomation()` call from render loop |
| A-9 | test `test_stepauto_order.c` | 5 laws: order, mark, after-mark deferral, leftover resets, non-reset |
| A-10 | test `test_drain_cursor.c` | 4 laws: limited drain, cursor continuation, reset-only-when-full, append-between-passes |

### 3.4 RAM

- R-A1: `seq_pending_automation_done` +1 B SRAM1 (static, Sequencer)
- R-A2: trigger batch in `voiceControl_processPending()` +128 B stack (per call, MidiVoiceControl)

---

## 4. Stage B: The `osc1_type` Row

### 4.1 Design

One `osc1_type` parameter per instrument (descriptor index 40; Snare 39). Values: `wav` (0), `smp` (1), `slp` (2), `wtb` (3). It is a **selector row**: not morphable, stored single-ended, applied before the Morph image on every slot reset. Uses `INSTRUMENT_PARAM_FLAG_SELECTOR` (0x08) and `DTYPE_MENU` with free id 4 (`MENU_OSC_TYPE`) to avoid ramp eligibility.

**Step automation:** `typ` is step-automatable with no ramp and one value per step, held until the voice's next trigger. It is not an LFO or velocity target. Four changes needed:
1. `osc_setType()` clears the interpolation slot stamp when the type changes
2. `instrumentManager_writeSpecialRuntime()` handles `osc1_type` as a special writer that calls `osc_setType()`, which rebinds the oscillator
3. The formatter reads the stored `typ` from the same image as the cell value
4. The automation display routes wave values through the type-aware formatter

### 4.2 Storage

- `OscInfo.oscType` at byte 34 (existing padding). No RAM increase.
- `OscInfo.interpStamp` at byte 35 (existing padding). No RAM increase.
- Storage key: `osc1_type`. A missing key loads 0 (`wav`).

### 4.3 Menu Placement

| Instrument / page | Screen 1 | Screen 2 |
|---|---|---|
| Drum Oscillator (page 1) | `coa fin wav pos` (done S086) | `typ` |
| Drum FM (page 4) | `amt frq wav mod` (unchanged) | — |
| Snare Oscillator (page 1) | `coa fin wav pos` (done S086) | `noi mix typ` |
| Cymbal/HiHat Oscillator (page 1) | `coa fin wav pos` (done S086) | `typ` |
| Cymbal/HiHat Mod Osc (page 4) | `f1 f2 g1 g2` (unchanged) | `wav wav` (unchanged) |

### 4.4 Host Test

`osc_type`: 2 laws + 2 break proofs.

---

## 5. Stage C: Sample Flash Catalogue v2 and Installer

### 5.1 Catalogue Layout

8 KB at `0x081FE000` (`SAMPLE_CATALOGUE_START_ADDRESS`).

**Header (32 B):**
- Word 0: magic `0x4C584332` ("LXC2"), programmed last
- Words 1–7: version, sample_entry_bytes (28), wavetable_entry_bytes (20), sample_count, wavetable_count, data_end, flags, reserved

**Sample entries (128 × 28 B):**
- `start_address` (4 B), `frame_count` (4 B), `loop_start` (4 B), `loop_end` (4 B), `gain_code` (2 B), `bits` (1 B), `flags` (1 B, bit 0 = loop present), `name[8]` (display name)

**Wavetable entries (128 × 20 B):**
- `start_address` (4 B), `frame_len` (2 B), `frame_count` (2 B), `bits` (1 B), `flags` (1 B), `name[8]` (display name), padding (2 B)

Items from `0x080C0000` (sector 7) upward. The S086 stock-wave flash block is not needed.

### 5.2 Installer

- `sampleInstall_begin()`: erases sectors 7–11; nothing programmed (header written only by commit)
- `sampleInstall_commit()`: programs header words 1–7 first, magic word 0 last. Interrupted install reads as EMPTY.
- `sampleInstall_abortItem()`: discards buffered bytes, never rewinds cursor. Lost space is accepted.
- `install_fault`: set by failed erase or write; stops further writes.

Install walker in `filesystem.c`:
- `samples/` folder: files first, subfolders, natural order (`storage_naturalCompare()`), depth limit 3
- `wavetables/` folder: each subfolder is one wavetable; the `.wav` files inside are frames
- Numeric prefix stripped from display names (`01_kick.wav` → `kick`)
- Wavetable frame length: any length (A16); at most 128 frames
- Gain code: 2^(code/24), peak within 0.25 dB of full scale
- Boot notice: `Samples invalid` / `reload samples` when v1 detected

### 5.3 P1 Fixes for the Installer

| ID | Finding | Fix |
|---|---|---|
| P1-1 (F1) | Catalogue header programmed twice; commit fails on hardware | Header written only by `sampleInstall_commit()`, not by `begin()` |
| P1-2 (F2) | Aborted item rewinds onto programmed flash | Abort discards buffer; cursor never moves back |
| P1-3 (F3) | Wavetable folder with > 128 frames loops forever | 128-frame cap keeps advancing the last-taken name |
| P1-4 (F4) | Third folder level writes out of bounds | Walk starts at depth 0; recursion stops before per-level arrays exceeded |

### 5.4 RAM Changes (Net ≈ −18 KB)

| Item | Bytes |
|---|---|
| `sample_manifest[120]` + count removed (R-C1) | −13,441 |
| `SampleMemory.c` caches removed (R-C2) | −5,040 |
| Installer walker state (R-C3) | +560 |
| `install_fault` (within R-C3) | +1 |

### 5.5 Host Tests

- `catalogue`: flash-accurate stub (program-once, read-back mismatch returns 3); F1/F2 laws
- `wav_install`, `natural_order`, `sampleoptimize`: all pass

---

## 6. Stage D: `smp`/`slp` Readers and `pos` on Samples

### 6.1 Reader Architecture

Q20.12 phase (`OSC_SAMPLE_PHASE_SHIFT` 12). Up to 2²⁰ points per sample. Eight loops in `.itcm.s087`, `noinline, noclone`:

| Loop | Function |
|---|---|
| smp plain | `osc_smpPlainBlock` |
| smp FM | `osc_smpFmBlock` |
| smp sync | `osc_smpSyncBlock` |
| smp ring | `osc_smpRingBlock` |
| slp plain | `osc_slpPlainBlock` |
| slp FM | `osc_slpFmBlock` |
| slp sync | `osc_slpSyncBlock` |
| slp ring | `osc_slpRingBlock` |

Install-time preparation:
- Two zero frames after every sample (A4): removes played-out select and next-point hold
- Fixed loop region (Q7): `smpl` loop 1 when present, else whole sample; stored as start/end in entry
- Gain code: per block

### 6.2 `pos` on Samples

| Type | At trigger | While playing |
|---|---|---|
| `smp` | start = pos/128 of the sample | linear bend in Q20.12, capped at 4× playback speed, clamped at 0; past end = ended |
| `slp` | start = pos/128 of the whole sample | linear bend, capped, wrapped into fixed loop region |

- **Linear difference** (D3): 127 → 0 is a jump back, not +1 step
- **One-shot edges** (D4): finished one-shot stays finished until next trigger
- **Speed cap** (D5): time-based, ≤ 4× playback speed; larger change glides over blocks
- **Re-anchor** (D7): type/source change sets positionApplied = position
- **Sync reset target** (D9): the `pos` start point

`osc_slpBlockInc()` converts a backward glide once per block (P1 fix F5).

### 6.3 Modulator `wav` List (A21/A22)

After the stock waves (0–7):
- `s01`, `s02`, … `sNN` — one-shot samples
- `l01`, `l02`, … `lNN` — looped samples (same items, looped playback)
- `w01`, `w02`, … `wNN` — wavetable first frame

Up to 127 entries total. The modulators get no `typ` or `pos` rows. A value past the list end is clamped to an entry that exists.

Sync master rules by list entry:
- Stock → phase clock (as today)
- `sNN` → as noise: one reset per nominal period at `frq`
- `lNN` → loop wrap
- `wNN` → frame wrap (first frame only, so the phase wrap)

### 6.4 v1 Removal

Removed in P1 (P1-17, F14/F19):
- `calcUserSampleOscBlock`, `calcUserSampleOscFmBlock`, `calcUserSampleOscRingBlock`, `calcUserSampleOscSyncBlock` (four dispatcher entries)
- `osc_syncWaveform()`, `osc_interp_a/b` (128 B DTCM released, R-D1)
- `stub_silence_buffer` (512 B flash)
- v1 `SampleInfo` shims in `SampleMemory.c/.h`
- `modNode_getWaveTargetOsc()`, `modNode_getMaxWaveformIndex()` in `modulationNode.c/.h`

ITCM: 15,032 → 12,640 B (net savings from v1 loop removal and dormant interpolation paths).

### 6.5 P1 Fixes for Readers

| ID | Finding | Fix |
|---|---|---|
| P1-5 (F5, F16) | Backward `pos` glide on `slp`; D4 limit | `osc_slpBlockInc()` converts backward step once per block |
| P1-6 (F6) | `slp` FM reads relative to loop start; lead-in frozen | FM read is absolute; picks loop/lead-in rule per block |
| P1-7 (F7) | Modulator `w` entries use sample-law increment | Bind resolves `WTB8` before comparing; picks increment law from what is bound |
| P1-8 (F8) | Wavetable blending rounds to whole frames | `osc_wtbPlanBlock()`/`osc_wtbFramePos()` give fractional frames; at most two segments per block |
| P1-9 (F9) | 8-bit wavetables rebind every block | Bind generation comparison fixed |
| P1-10 (F10) | Per-step sample change starts from previous sample's length | `osc_typedTriggerPhase()` binds first |
| P1-13 (F20) | Loop sync builder after master kind change | Builder rechecked |
| P1-14 (F21) | `slp` sync reset target past `smpl` loop end | Target clamped |
| P1-15 | Modulator `wav` past list end binds nothing | Clamped to last entry that exists |
| P1-16 | Ring-law depth add one step late | S085 ring law `d_i = depthQ15 + (i + 1) × step` applied |

### 6.6 CPU After Stage D + P1

| Loop | Per sample (linked) |
|---|---|
| `osc_slpPlainBlock` | 27 |
| `osc_slpFmBlock` | 49/50 inside loop; 39 in lead-in |
| `osc_slpRingBlock` | 42 |
| `osc_smpRingBlock` | 38 |
| `osc_wtbRingBlock` | 32/34 |
| `wtb` blend inner loops (plain/FM/ring/sync) | 34–38 / 42–46 / 43–47 / 44–48 |

Section worst cases:
- Drum `fm` with `slp` on both oscillators: 27 + 50 = **77**, against frozen ceiling **≈ 92**
- Cymbal/HiHat with samples on all three: 27 + 27 + 50 = **104**, against frozen ceiling **≈ 131**
- Per-slot blend cost: +10..+13 over one frame, against ≈ +66 today

All sections stay below their frozen worst cases (A1 satisfied).

---

## 7. Stage E: `wtb` Reader

### 7.1 Design

- Any frame length (UMULL indexing, A16). One-frame base loop.
- Frame blending through two interpolation slots (A17). Sticky claims: a grant made this block is kept (A8). Stamps 1..255, never 0 (P1-11, F11/F18).
- `osc_wtbPlanBlock()` splits at frame crossings (P1-8, F8).
- `pos` = frame position, read every block. Without a slot: nearest frame (Q9 fallback).
- Four blend copies: plain, FM, ring, sync.

### 7.2 Interpolation Slots

- `modNode_interpStampOf()` gives 1..255 (wraps from 255 to 1, never 0)
- `modNode_interpLastStamp` (1 B `INCCMZ`) + `modNode_interpReserved` (1 B) = R-E1, +2 B
- Claims are first come, first served, across all slots
- `modNode_resetTargets()` resets the count every block
- `osc_setType()` clears the slot stamp when the type changes (A18, P1-12 F17)

---

## 8. P2: Display and Naming Corrections

### 8.1 Findings and Fixes

| ID | Defect | Cause | Fix |
|---|---|---|---|
| P2-1 | `wav` cell shows stock name under `smp`/`slp`/`wtb` | `getMenuItemNameForValue()` knows only stock names 0..7 and `sNN` above | Route MENU_WAVEFORM through `menu_formatWaveValue3()` |
| P2-2 | Modulator list shows `sNN` for all entries | Same lookup; no `l`/`w` distinction | `menu_formatWaveCell()` rewrote with type-aware routing |
| P2-3 | NUL write past 8-char buffer in automation formatter | Catalogue getters write NUL at byte 8; 8-byte stack buffer overflows | Helper owns 9-byte local; copies 8 characters out |
| P2-4 | Names cut to first 8 chars; user wants first 4 + last 4 | `storage_displayNameFromLfn()` kept first 8 | Rewritten: names ≤ 8 chars kept; longer names → first 4 + last 4 |
| P2-5 | Sample level seems low | No attenuation found; peak within 0.25 dB of full scale | Closed: crest factor and one-shot length explain perceived difference |

### 8.2 Code Changes

| ID | File | Change |
|---|---|---|
| MU-1 | `menu.c` | `menu_formatVoiceAutomationValue3()` now calls `menu_formatWaveValue3()`; owns no name buffer |
| MU-2 | `menu.c` | `menu_formatWaveCell()` rewritten: 9-byte local for getters; 16-bit list arithmetic; type-aware routing. `menu_formatWaveValue3()` added |
| MU-3 | `menu.c` | `menu_formatCellValue3()` DTYPE_MENU case routes MENU_WAVEFORM to new helper |
| MU-4 | `menu.c` | `va_formatValue3()` DTYPE_MENU case routes MENU_WAVEFORM to new helper |
| MU-5 | `menu.c` | Forward declaration for `menu_formatWaveValue3()` |
| ST-1 | `storageTypes.c` | `storage_displayNameFromLfn()`: first 4 + last 4 for names over 8 chars |
| ST-2 | `storageTypes.h` | Declaration comment updated |
| TB-1 | `modules/samples.c` | Host test: `"longight"`, `"kickd_01"` expectations |
| TB-2 | `modules/samples.c` | Law 3 text updated |

### 8.3 Build After P2

| Item | Value |
|---|---|
| text / data / bss | 670,628 / 420 / 412,392 |
| Flash | 670,628 / 753,664 (headroom 83,036) |
| ITCM | 12,640 / 16,384 |
| DTCM statics | 4,344 |
| FX arena margin | 3,840 |

---

## 9. P3: One-Shot Sample Mod Oscillators and Transient Click Samples

### 9.1 One-Shot Samples as Mod Oscillator

New `osc_modulatorTrigger()` in `Oscillator.c/.h`: restarts the modulator's sample at its start on every hit when its `wav` selects an `s` entry (one-shot sample). Binds the current selection first, so a sample chosen by automation on the same step is the one that restarts. Stock waves and `l`/`w` entries return immediately.

Called from DrumVoice and HiHat triggers. Cymbal needed no change: it already resets both modulators to phase 0 on every hit.

**Cost:** one extra call per hit from Drum/HiHat; returns immediately for stock waves. No worst-case increase.

### 9.2 Transient Click Sample List

The click/transient list is extended: Snp, Ofs, 12 built-in clicks, then one entry per installed sample (`s01`, `s02`, …). One-shots only, no loop or wavetable entries.

- Compact cells show `sNN`; single-parameter view shows the sample's 8-character name on the left
- Menu limit and step-automation range follow the sample count
- `frq` sets the speed (1.0 = file's own rate)
- `vol` applied on top of install normalisation
- Silent at the end until the next hit
- After reinstall: stored value past installed samples → last sample; no samples → last built-in click with one silent block first

**CPU:** new sample loop is 20 instructions per sample. Built-in click loop stays at 22. No worst-case increase.

**Behaviour:** switching between a built-in click and a sample mid-hit stays silent until the next trigger.

---

## 10. Build Metrics Across the Session

| Metric | Pre-S087 (S086 close) | After P0+P1 | After P2 |
|---|---|---|---|
| text / data / bss | 658,872 / 424 / 430,296 | 670,328 / 420 / 412,392 | 670,628 / 420 / 412,392 |
| Flash (headroom) | 659,296 (94,368) | 670,748 (82,916) | 670,628 (83,036) |
| ITCM | 8,512 / 16,384 | 12,640 / 16,384 | 12,640 / 16,384 |
| DTCM statics | 4,472 | 4,344 | 4,344 |
| FX arena margin | 3,712 | 3,840 | 3,840 |

Flash and ITCM budget per stage (actual vs estimate):
| Stage | Flash est. | ITCM est. | Notes |
|---|---|---|---|
| A | ≤ 0.5 KB | 0 | Within estimate |
| B | ≤ 2 KB | 0 | Within estimate |
| C | ≤ 8 KB net | 0 | Net savings from v1 removal offset new code |
| D | ≤ 6 KB | ≤ +2.5 KB less ~1.3 KB v1 | ITCM 8,512 → 15,032 (before P1 removal) → 12,640 (after P1) |
| E | ≤ 5 KB | ≤ +3.5 KB | Within estimate |

---

## 11. Host Tests and Gates

### 11.1 Tests Added

| Target | Laws | Break proofs |
|---|---|---|
| `stepauto_order` | 5 (order, mark, after-mark deferral, leftover resets, non-reset) | 1 (drain after triggers) |
| `drain_cursor` | 4 (limited drain, cursor continuation, reset-only-when-full, append-between-passes) | 1 (start i at 0) |
| `osc_type` | 2 | 2 |
| `catalogue` | flash-accurate stub laws | 1 (header in begin) |
| `osc_sample` L4 | backward `slp` glide wraps in loop | — |
| `osc_sample` L6 | `slp` FM with loop_start reads absolute point | — |
| `osc_wtb` law 7 | held `pos` on 2-frame table gives blended value | — |
| `interp_slots` law 5 | stamps wrap 255→1 across 300 blocks | — |

### 11.2 Gates Passed

| Gate | Result |
|---|---|
| `make all` / `make img` | clean, figures in §10 |
| `engine87_identity` | pass (13 allowed) |
| `elf87_mixer_check` | 37 equal, 1 shrank, Snare noise 14→15 (F12, user decision) |
| `stepauto_order` / `drain_cursor` | 0 failures; break proofs caught |
| `osc_type` | 0 failures; break proofs caught |
| `catalogue` | 0 failures; break proof caught |
| `wav_install`, `natural_order`, `sampleoptimize` | 0 failures |
| `osc_sample` | 0 failures including L4/L6 |
| `osc_wtb` | 0 failures including law 7 |
| `interp_slots` | 0 failures including law 5 |
| `special_tags` | 163 rows, 0 mismatches |
| `stock_tables` | OK |
| `selftest` | 276,480,000 samples, 0 differing (int16 and float) |
| `filter`, `pack`, `postchain`, `octave`, `mixer` | pass |

### 11.3 Known Non-Compiling Targets

`osc_mod`, `osc_pos`, `osc_stock` — their `SampleMemory.h` v1 extractions are stale (F15). Host-bench follow-up excluded from P1 schedule.

---

## 12. Open Items at Session Close

1. **F12 (Snare noise loop +1):** the Snare noise loop grew from 14 to 15 instructions per sample. A1 allows it (stays below the section's worst case). Needs user decision — accept or investigate.
2. **F15 (stale host targets):** `osc_mod`, `osc_pos`, `osc_stock` need their `SampleMemory.h` extractions updated for v2.
3. **Hardware testing:** S087 P0+P1 implementation, P2 display/naming, P3 mod oscillator and transient clicks — all pending.
4. **Commit:** all S087 changes are uncommitted. The working tree is P2+P3 changes on top of `f3805478`.

---

## 13. MEMORY.md Refactor

Performed during S087 as a separate task (`S087_MEMORY_REFACTOR_CONTEXT_RECEIPTS.md`).

### 13.1 Summary

MEMORY.md was reduced from 2,773 to 525 lines. Categories of removed content:
- **Session history** (A): 12 session summaries and closed-item narratives → deleted (belong in log archive)
- **Specification content** (B): module details, parameter lists, render-loop internals → moved to spec docs
- **Verbose binding rules** (C): compressed to one-line-plus-reference form
- **Resolved items** (D): decisions, closed bugs, accepted fixes → removed
- **Agent mandates** (E): reviewed, kept only what applies to all agents
- **Stale volatile** (F): old working notes deleted
- **Duplicate references** (G): consolidated
- **Open items** (H): confirmed still open, or removed if resolved

### 13.2 Documents Updated

- `HARDWARE_MAP.md`: corrections found during audit
- `EFFECTS_MIXER_DSP_REFERENCE.md`: content moved from MEMORY.md
- `MODULE_INTERCHANGE_SPEC.md`: content moved from MEMORY.md

### 13.3 Documents Created

- `REPOSITORY_LAYOUT.md`: repository structure reference
- `OPEN_TESTS.md`: hardware verification items extracted from MEMORY.md

### 13.4 Documents Deleted

- `S070_WORKING_NOTES.md` (stale)
- `S076_WORKING_NOTES.md` (stale)

### 13.5 Standing Rule

**MEMORY.md must not be re-polluted.** It is a project startup context document, not a session diary. Session history belongs in `knowledge_files/log_archive/`. Specification details belong in `knowledge_files/specification_reference/`. Resolved items are removed, not kept as narratives.

---

## 14. Files Modified (git diff summary)

Staged:
- `Core/DSP/Instruments/Drum/DrumVoice.c` — P3: `osc_modulatorTrigger()` call
- `Core/DSP/Instruments/HiHat/HiHat.c` — P3: `osc_modulatorTrigger()` call
- `Core/DSPAudio/Oscillator.c` — Major: all S087 oscillator code
- `Core/DSPAudio/Oscillator.h` — Major: type constants, OscInfo fields, declarations
- `Core/DSPAudio/transientGenerator.c` — P3: sample click list
- `Core/DSPAudio/transientGenerator.h` — P3: sample click list declarations
- `Core/Hardware/SD/storageTypes.c` — P2: first 4 + last 4 name rule
- `Core/Hardware/SD/storageTypes.h` — P2: declaration update
- `Core/Menu/menu.c` — P2: wave display formatting; B: osc_type page/descriptor
- `build/LXRV2_lxr02.img` — rebuilt
- `tools/dsp_test/modules/samples.c` — P2: test expectation update

Untracked at session close:
- `S087_P2_SAMPLES_CORRECTION.md`
- `S087_P3_SAMPLES_MOD_TRANS.md`
- `tools/dsp_test/spike/__pycache__/`
